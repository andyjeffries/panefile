#include "ui/quicklook/QuickLookLoader.h"

#include "core/Format.h"
#include "core/Logging.h"
#include "fs/ArchiveReader.h"
#include "ui/quicklook/renderers/ArchiveRenderer.h"
#include "ui/quicklook/renderers/DirectoryRenderer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QRunnable>
#include <QThreadPool>

#include "core/WorkerPools.h"

#include <algorithm>
#include <atomic>

namespace pf::ui {
namespace {

/// The pool Quick Look loads on.
///
/// Separate from the scanner's (§3.3) and deliberately small: a preview load is
/// latency-sensitive and there is only ever one worth having in flight, so a
/// larger pool would only let abandoned loads compete with the live one.
QThreadPool *loaderPool()
{
    // Registered with WorkerPools so shutdown waits for it: a task still
    // running when main() returns can reach a Qt global that static
    // destruction has already torn down.
    static QThreadPool *pool = WorkerPools::acquire("pf-quicklook", 2);
    return pool;
}

/// Reads a directory listing for DirectoryRenderer (§7.6).
void loadDirectory(QuickLookContent &content, const std::atomic<bool> &cancelled)
{
    const QDir directory(content.path);
    const QFileInfoList entries = directory.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);

    QList<QPair<QString, quint64>> children;
    quint64 total = 0;

    for (const QFileInfo &entry : entries) {
        if (cancelled.load(std::memory_order_relaxed)) {
            return;
        }
        const auto size = static_cast<quint64>(entry.isDir() ? 0 : entry.size());
        children.append(
            {entry.fileName() + (entry.isDir() ? QStringLiteral("/") : QString()), size});
        total += size;
    }

    // §7.6: "du-style top-5 largest children" ahead of the listing, because the
    // question a directory preview usually answers is "what is taking up the
    // room in here".
    QList<QPair<QString, quint64>> largest = children;
    std::ranges::sort(largest, [](const auto &a, const auto &b) { return a.second > b.second; });

    for (int i = 0; i < std::min<int>(DirectoryRenderer::kTopChildren, largest.size()); ++i) {
        if (largest.at(i).second == 0) {
            break;
        }
        content.facts.append({largest.at(i).first, formatSize(largest.at(i).second)});
    }

    if (!content.facts.isEmpty()) {
        content.facts.append({QStringLiteral("—"), QString()});
    }

    // §7.6: "Child listing (first 200)".
    for (int i = 0; i < std::min<int>(DirectoryRenderer::kMaxChildren, children.size()); ++i) {
        content.facts.append({children.at(i).first, children.at(i).second > 0
                                                        ? formatSize(children.at(i).second)
                                                        : QString()});
    }

    content.text = QObject::tr("%n item(s) · %1", nullptr, static_cast<int>(children.size()))
                       .arg(formatSize(total));

    if (children.size() > DirectoryRenderer::kMaxChildren) {
        content.text += QObject::tr(" · showing the first %1").arg(DirectoryRenderer::kMaxChildren);
    }
}

/// Reads an archive listing for ArchiveRenderer (§7.6).
void loadArchive(QuickLookContent &content)
{
    const fs::ArchiveListing listing = fs::ArchiveReader::list(content.path);

    if (!listing.isValid()) {
        content.error = listing.error;
        return;
    }

    for (const fs::ArchiveEntry &entry : listing.entries) {
        // The two columns travel in one string separated by a unit separator,
        // because QuickLookContent's facts are pairs and inventing a third
        // field for one renderer would spread archive detail through a type
        // every renderer shares.
        QString ratio;
        if (entry.size > 0 && entry.compressedSize > 0) {
            ratio = QStringLiteral("%1%").arg(entry.compressedSize * 100 / entry.size);
        }
        content.facts.append({entry.path, (entry.isDirectory ? QString() : formatSize(entry.size)) +
                                              QLatin1Char('\x1f') + ratio});
    }

    QStringList summary;
    summary << QObject::tr("%n entries", nullptr, static_cast<int>(listing.entries.size()));
    if (!listing.format.isEmpty()) {
        summary << listing.format;
    }
    if (!listing.compression.isEmpty() && listing.compression != QLatin1String("none")) {
        summary << listing.compression;
    }
    summary << formatSize(listing.totalSize);
    if (listing.encrypted) {
        // §7.10: "Password-protected archives prompt". Saying so in the preview
        // means the user learns it before they try to extract.
        summary << QObject::tr("encrypted");
    }
    content.text = summary.join(QStringLiteral(" · "));
}

} // namespace

/// Carries a load's result back to the GUI thread, and survives the loader.
class QuickLookLoader::Channel : public QObject
{
    Q_OBJECT

public:
    std::atomic<bool> cancelled{false};

Q_SIGNALS:
    void ready(const pf::ui::QuickLookContent &content);
};

namespace {

class LoadTask : public QRunnable
{
public:
    LoadTask(std::shared_ptr<QuickLookLoader::Channel> channel, QuickLookContent content,
             qint64 maxReadBytes, int maxDecodeMegabytes, bool wantsImage, qint64 desiredBytes)
        : m_channel(std::move(channel)), m_content(std::move(content)),
          m_maxReadBytes(maxReadBytes), m_maxDecodeMegabytes(maxDecodeMegabytes),
          m_wantsImage(wantsImage), m_desiredBytes(desiredBytes)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_channel->cancelled.load(std::memory_order_relaxed)) {
            return;
        }

        static const QMimeDatabase database;
        m_content.mimeType = database.mimeTypeForFile(m_content.path);

        if (m_content.entry.isDir) {
            loadDirectory(m_content, m_channel->cancelled);
        } else if (ArchiveRenderer::isArchive(m_content.mimeType)) {
            loadArchive(m_content);
        } else {
            loadFile();
        }

        if (m_channel->cancelled.load(std::memory_order_relaxed)) {
            return;
        }
        Q_EMIT m_channel->ready(m_content);
    }

private:
    void loadFile()
    {
        const auto megabytes =
            static_cast<int>(m_content.entry.size / (static_cast<quint64>(1024) * 1024));

        // §7.6: "files above quicklook.max_decode_mb show a metadata-only card
        // with an 'open anyway' action." Reading half a gigabyte to preview it
        // would stall the pool for as long as the disk took.
        if (megabytes > m_maxDecodeMegabytes) {
            m_content.metadataOnly = true;
            m_content.facts.append({QObject::tr("Size"), formatSize(m_content.entry.size)});
            return;
        }

        if (m_wantsImage) {
            QImageReader reader(m_content.path);
            reader.setAutoTransform(true); // honour the EXIF orientation

            const QSize size = reader.size();
            if (size.isValid()) {
                m_content.facts.append(
                    {QObject::tr("Dimensions"),
                     QStringLiteral("%1 × %2").arg(size.width()).arg(size.height())});
            }
            m_content.image = reader.read();
            if (m_content.image.isNull()) {
                m_content.error = reader.errorString();
            }
            return;
        }

        if (m_desiredBytes <= 0) {
            // The renderer opens the file itself — the media and PDF cards, whose
            // libraries stream far better than a wholesale read would.
            m_content.facts.append({QObject::tr("Size"), formatSize(m_content.entry.size)});
            return;
        }

        QFile file(m_content.path);
        if (!file.open(QIODevice::ReadOnly)) {
            m_content.error = file.errorString();
            return;
        }

        const qint64 cap = std::min(m_desiredBytes, m_maxReadBytes);
        m_content.bytes = file.read(cap);

        // Decoded as UTF-8 with a fallback, rather than assumed: a file that is
        // not valid UTF-8 is still worth showing, and mojibake beats an empty
        // pane.
        auto decoder = QStringDecoder(QStringDecoder::Utf8);
        m_content.text = decoder(m_content.bytes);
        if (decoder.hasError()) {
            m_content.text = QString::fromLocal8Bit(m_content.bytes);
        }
    }

    std::shared_ptr<QuickLookLoader::Channel> m_channel;
    QuickLookContent m_content;
    qint64 m_maxReadBytes;
    int m_maxDecodeMegabytes;
    bool m_wantsImage;
    qint64 m_desiredBytes;
};

} // namespace

QuickLookLoader::QuickLookLoader(QObject *parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDefaultDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &QuickLookLoader::start);

    m_spinnerDelay.setSingleShot(true);
    m_spinnerDelay.setInterval(kSpinnerDelayMs);
    connect(&m_spinnerDelay, &QTimer::timeout, this,
            [this] { Q_EMIT loadingSlowly(m_pendingPath); });
}

QuickLookLoader::~QuickLookLoader()
{
    cancel();
}

void QuickLookLoader::setDebounceInterval(int milliseconds)
{
    m_debounce.setInterval(std::max(0, milliseconds));
}

void QuickLookLoader::setMaxReadBytes(qint64 bytes)
{
    m_maxReadBytes = std::max<qint64>(0, bytes);
}

void QuickLookLoader::setMaxDecodeMegabytes(int megabytes)
{
    m_maxDecodeMegabytes = std::max(1, megabytes);
}

void QuickLookLoader::request(const QString &path, const FileEntry &entry,
                              QuickLookRenderer *renderer)
{
    if (renderer == nullptr) {
        return;
    }

    // §7.6: "Cancel any in-flight load when the cursor moves again." Holding j
    // through a directory would otherwise leave a queue of loads for files the
    // user has already passed.
    cancel();

    m_pendingPath = path;
    m_pendingEntry = entry;
    m_pendingRenderer = renderer;

    // §7.6: "Cache the last 5 rendered contents keyed on path plus mtime."
    // Served synchronously, because the debounce exists to avoid pointless work
    // and there is none to avoid when the answer is already in hand.
    const CacheKey key{.path = path, .modifiedSeconds = entry.modified.toSecsSinceEpoch()};
    for (int i = 0; i < m_cache.size(); ++i) {
        if (m_cache.at(i).first == key) {
            const auto hit = m_cache.takeAt(i);
            m_cache.append(hit); // most recently used
            Q_EMIT loaded(hit.second, renderer);
            return;
        }
    }

    m_debounce.start();
}

void QuickLookLoader::start()
{
    if (m_pendingRenderer == nullptr || m_pendingPath.isEmpty()) {
        return;
    }

    // §7.6: "Show a skeleton/spinner state after 200 ms of loading, never
    // before — instant content must not flash a spinner."
    m_spinnerDelay.start();

    m_channel =
        std::shared_ptr<Channel>(new Channel, [](Channel *channel) { channel->deleteLater(); });

    QuickLookRenderer *renderer = m_pendingRenderer;
    const QString path = m_pendingPath;
    const auto key =
        CacheKey{.path = path, .modifiedSeconds = m_pendingEntry.modified.toSecsSinceEpoch()};

    connect(m_channel.get(), &Channel::ready, this,
            [this, renderer, key](const QuickLookContent &content) {
                m_spinnerDelay.stop();

                m_cache.append({key, content});
                while (m_cache.size() > kCacheSize) {
                    m_cache.removeFirst();
                }

                Q_EMIT loaded(content, renderer);
            });

    QuickLookContent content;
    content.path = path;
    content.entry = m_pendingEntry;

    loaderPool()->start(new LoadTask(m_channel, std::move(content), m_maxReadBytes,
                                     m_maxDecodeMegabytes, renderer->wantsImage(),
                                     renderer->desiredReadBytes()));
}

void QuickLookLoader::cancel()
{
    m_debounce.stop();
    m_spinnerDelay.stop();

    if (m_channel) {
        m_channel->cancelled.store(true, std::memory_order_relaxed);
        m_channel->disconnect(this);
        m_channel.reset();
    }
}

void QuickLookLoader::clearCache()
{
    // Explicitly, rather than waiting for eviction: five decoded images can be
    // hundreds of megabytes, and holding them after Quick Look closes serves
    // nobody.
    m_cache.clear();
}

int QuickLookLoader::cacheSize() const
{
    return static_cast<int>(m_cache.size());
}

} // namespace pf::ui

#include "QuickLookLoader.moc"
