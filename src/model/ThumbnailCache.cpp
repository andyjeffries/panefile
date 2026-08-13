#include "model/ThumbnailCache.h"

#include "core/Logging.h"
#include "platform/Paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QThreadPool>
#include <QUrl>

#include <functional>

namespace pf {
namespace {

/// §7.7's text chunk keys, spelled exactly as the freedesktop spec spells them.
const char *const kUriKey = "Thumb::URI";
const char *const kMTimeKey = "Thumb::MTime";

/// A dedicated pool, small on purpose. Thumbnailing is IO-bound and speculative
/// — it happens for rows the user has not asked about — so it must never take
/// the threads a copy or a scan wants.
QThreadPool *thumbnailPool()
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(2);
        pool.setObjectName(QStringLiteral("pf-thumbnails"));
        pool.setExpiryTimeout(30000);
        return true;
    }();
    Q_UNUSED(configured)
    return &pool;
}

/// Whether this build can decode `mime` into an image.
bool isThumbnailable(const QMimeType &mime)
{
    if (mime.name().startsWith(QLatin1String("image/"))) {
        // Ask Qt rather than assume: which image formats are readable depends
        // on which plugins are installed, and claiming HEIC on a build without
        // the plugin would fill the fail cache with files that a later build
        // could have handled.
        const QByteArray subtype = mime.preferredSuffix().toLatin1();
        if (!subtype.isEmpty() && QImageReader::supportedImageFormats().contains(subtype)) {
            return true;
        }
        return QImageReader::supportedMimeTypes().contains(mime.name().toLatin1());
    }
    return false;
}

/// The generation worker. Holds no pointer back to the cache: it reports
/// through a QObject-free callback bound to a shared cancellation flag, and the
/// cache connects to it with a queued connection.
class ThumbnailTask : public QRunnable
{
public:
    ThumbnailTask(QString path, ThumbnailCache::Size size, QString destination,
                  std::shared_ptr<std::atomic<bool>> cancelled,
                  std::function<void(QString, QImage, bool)> report)
        : m_path(std::move(path)), m_size(size), m_destination(std::move(destination)),
          m_cancelled(std::move(cancelled)), m_report(std::move(report))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_cancelled->load(std::memory_order_relaxed)) {
            return;
        }

        const QFileInfo info(m_path);
        QImageReader reader(m_path);
        reader.setAutoTransform(true);

        const QSize source = reader.size();
        const int target = ThumbnailCache::pixelsFor(m_size);

        // Scaled at decode time where the format allows it, so a 40-megapixel
        // JPEG never materialises in full just to produce a 128 px square.
        if (source.isValid() && (source.width() > target || source.height() > target)) {
            reader.setScaledSize(source.scaled(target, target, Qt::KeepAspectRatio));
        }

        QImage image = reader.read();
        if (image.isNull() || m_cancelled->load(std::memory_order_relaxed)) {
            m_report(m_path, QImage(), image.isNull());
            return;
        }

        if (image.width() > target || image.height() > target) {
            image = image.scaled(target, target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        // §7.7: the URI and mtime chunks are what make a cached thumbnail
        // verifiable rather than merely present.
        image.setText(QString::fromLatin1(kUriKey), ThumbnailCache::fileUri(m_path));
        image.setText(QString::fromLatin1(kMTimeKey),
                      QString::number(info.lastModified().toSecsSinceEpoch()));

        QDir().mkpath(QFileInfo(m_destination).absolutePath());

        // QSaveFile, so a thumbnail is never half-written: another application
        // reading this cache concurrently must not see a truncated PNG.
        QSaveFile file(m_destination);
        if (file.open(QIODevice::WriteOnly)) {
            if (image.save(&file, "PNG")) {
                file.commit();
            } else {
                file.cancelWriting();
            }
        }

        if (!m_cancelled->load(std::memory_order_relaxed)) {
            m_report(m_path, image, false);
        }
    }

private:
    QString m_path;
    ThumbnailCache::Size m_size;
    QString m_destination;
    std::shared_ptr<std::atomic<bool>> m_cancelled;
    std::function<void(QString, QImage, bool)> m_report;
};

} // namespace

int ThumbnailCache::pixelsFor(Size size)
{
    return size == Size::Large ? 256 : 128;
}

QString ThumbnailCache::directoryNameFor(Size size)
{
    return size == Size::Large ? QStringLiteral("large") : QStringLiteral("normal");
}

QString ThumbnailCache::fileUri(const QString &absolutePath)
{
    // QUrl::fromLocalFile percent-encodes exactly as the spec requires, which
    // matters because the hash is of the encoded form: getting this wrong does
    // not break anything visibly, it just silently stops sharing the cache with
    // every other application.
    return QUrl::fromLocalFile(absolutePath).toString(QUrl::FullyEncoded);
}

QString ThumbnailCache::cacheFileName(const QString &uri)
{
    const QByteArray digest =
        QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex();
    return QString::fromLatin1(digest) + QStringLiteral(".png");
}

QString ThumbnailCache::thumbnailPathFor(const QString &absolutePath, Size size,
                                         const QString &cacheRoot)
{
    return cacheRoot + QLatin1Char('/') + directoryNameFor(size) + QLatin1Char('/') +
           cacheFileName(fileUri(absolutePath));
}

ThumbnailCache::ThumbnailCache(QObject *parent)
    : QObject(parent), m_root(platform::thumbnailCacheDir()), m_pool(thumbnailPool())
{}

ThumbnailCache::~ThumbnailCache()
{
    cancelAll();
}

void ThumbnailCache::setCacheRoot(const QString &root)
{
    m_root = root;
}

QString ThumbnailCache::cacheRoot() const
{
    return m_root;
}

void ThumbnailCache::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        cancelAll();
    }
}

bool ThumbnailCache::isEnabled() const
{
    return m_enabled;
}

void ThumbnailCache::setMaxFileSizeMb(int megabytes)
{
    m_maxFileSizeMb = megabytes;
}

void ThumbnailCache::setVideoEnabled(bool enabled)
{
    m_video = enabled;
}

QString ThumbnailCache::failMarkerPath(const QString &absolutePath) const
{
    // §7.7: "fail/panefile/". The per-application subdirectory is the spec's,
    // so one application's inability to read a format does not stop another
    // from trying.
    return m_root + QStringLiteral("/fail/panefile/") + cacheFileName(fileUri(absolutePath));
}

bool ThumbnailCache::hasFailed(const QString &absolutePath) const
{
    const QFileInfo marker(failMarkerPath(absolutePath));
    if (!marker.exists()) {
        return false;
    }

    // A failure marker is only binding for the version of the file that failed.
    // Editing a corrupt image into a valid one has to be enough to make it
    // thumbnail again.
    const QFileInfo source(absolutePath);
    return marker.lastModified() >= source.lastModified();
}

void ThumbnailCache::writeFailMarker(const QString &absolutePath) const
{
    const QString destination = failMarkerPath(absolutePath);
    QDir().mkpath(QFileInfo(destination).absolutePath());

    // The spec's fail marker is a zero-by-zero PNG carrying the same chunks, so
    // other implementations can validate it the same way they validate a real
    // thumbnail.
    QImage marker(1, 1, QImage::Format_ARGB32);
    marker.fill(Qt::transparent);
    marker.setText(QString::fromLatin1(kUriKey), fileUri(absolutePath));
    marker.setText(QString::fromLatin1(kMTimeKey),
                   QString::number(QFileInfo(absolutePath).lastModified().toSecsSinceEpoch()));

    QSaveFile file(destination);
    if (file.open(QIODevice::WriteOnly) && marker.save(&file, "PNG")) {
        file.commit();
    }
}

ThumbnailCache &ThumbnailCache::instance()
{
    static ThumbnailCache cache;
    return cache;
}

QImage ThumbnailCache::lookup(const QString &absolutePath, Size size) const
{
    if (!m_enabled) {
        return {};
    }

    const qint64 sourceMTime = QFileInfo(absolutePath).lastModified().toSecsSinceEpoch();

    if (const auto it = m_memory.constFind(absolutePath); it != m_memory.constEnd()) {
        if (it->modifiedSeconds == sourceMTime) {
            return it->image;
        }
        m_memory.erase(it);
    }

    const QString path = thumbnailPathFor(absolutePath, size, m_root);
    if (!QFileInfo::exists(path)) {
        return {};
    }

    // Decoded before the chunks are inspected, not after: QImageReader's text
    // accessors are empty until the handler has actually run, so checking
    // staleness first would silently treat every cached thumbnail as missing.
    QImage image = QImageReader(path).read();
    if (image.isNull()) {
        return {};
    }

    const QString recorded = image.text(QString::fromLatin1(kMTimeKey));

    // §7.7: "treat a thumbnail whose Thumb::MTime doesn't match the source as
    // stale". Not "older than" — the spec says match, and a file restored from
    // a backup can legitimately move an mtime backwards.
    if (recorded.isEmpty() || recorded.toLongLong() != sourceMTime) {
        return {};
    }

    m_memory.insert(absolutePath, MemoryEntry{.image = image, .modifiedSeconds = sourceMTime});
    return image;
}

bool ThumbnailCache::canThumbnail(const QString &absolutePath) const
{
    if (!m_enabled) {
        return false;
    }

    const QFileInfo info(absolutePath);
    if (!info.isFile() || info.isSymLink()) {
        return false;
    }
    if (info.size() > static_cast<qint64>(m_maxFileSizeMb) * 1024 * 1024) {
        return false;
    }

    static const QMimeDatabase database;
    const QMimeType mime = database.mimeTypeForFile(info);

    if (mime.name().startsWith(QLatin1String("video/"))) {
        // Deliberately not fail-cached: §3.4 puts ffmpegthumbnailer behind the
        // optional plugin host, and marking every video as failed now would
        // stop a build that has it from ever generating one.
        return false;
    }

    return isThumbnailable(mime);
}

void ThumbnailCache::request(const QString &absolutePath, Size size)
{
    if (!canThumbnail(absolutePath) || m_pendingFlags.contains(absolutePath) ||
        hasFailed(absolutePath)) {
        return;
    }

    if (const QImage cached = lookup(absolutePath, size); !cached.isNull()) {
        Q_EMIT ready(absolutePath, cached);
        return;
    }

    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    m_pendingFlags.insert(absolutePath, cancelled);

    // QPointer, not `this`: the worker outlives a cancelled request by however
    // long the decode takes, and the queued invocation must not resurrect a
    // destroyed cache.
    const QPointer<ThumbnailCache> self(this);
    auto report = [self, cancelled](const QString &path, const QImage &image, bool failure) {
        if (cancelled->load(std::memory_order_relaxed)) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, path, image, failure] {
                if (self.isNull()) {
                    return;
                }
                self->m_pendingFlags.remove(path);
                if (failure || image.isNull()) {
                    self->writeFailMarker(path);
                    Q_EMIT self->failed(path);
                    return;
                }
                self->m_memory.insert(
                    path, MemoryEntry{.image = image,
                                      .modifiedSeconds =
                                          QFileInfo(path).lastModified().toSecsSinceEpoch()});
                Q_EMIT self->ready(path, image);
            },
            Qt::QueuedConnection);
    };

    m_pool->start(new ThumbnailTask(absolutePath, size,
                                    thumbnailPathFor(absolutePath, size, m_root), cancelled,
                                    std::move(report)));
}

void ThumbnailCache::cancel(const QString &absolutePath)
{
    if (const auto flag = m_pendingFlags.take(absolutePath); flag) {
        flag->store(true, std::memory_order_relaxed);
    }
}

void ThumbnailCache::cancelAll()
{
    for (const auto &flag : std::as_const(m_pendingFlags)) {
        flag->store(true, std::memory_order_relaxed);
    }
    m_pendingFlags.clear();
}

int ThumbnailCache::pendingCount() const
{
    return static_cast<int>(m_pendingFlags.size());
}

} // namespace pf
