#include "model/ThumbnailCache.h"

#include "plugins/PluginInterfaces.h"
#include "core/Logging.h"
#include "platform/Paths.h"
#include "platform/PluginHost.h"
#include "platform/Thumbnailers.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPointer>
#include <QProcess>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>

#include "core/WorkerPools.h"
#include <QUrl>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
    // Registered with WorkerPools so shutdown waits for it: a task still
    // running when main() returns can reach a Qt global that static
    // destruction has already torn down.
    static QThreadPool *pool = WorkerPools::acquire("pf-thumbnails", 2);
    return pool;
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

/// How a generation attempt ended.
enum class Outcome {
    Generated,
    /// The file could not be decoded; recorded in the fail cache (§7.7).
    Failed,
    /// Nothing in this installation can decode it — a video with no
    /// pf-video-thumb plugin. Not fail-cached, so installing the plugin later
    /// is enough to start getting thumbnails.
    Unavailable,
};

/// Whether bubblewrap can set up a sandbox here (user namespaces may be
/// disabled). Decided once by a probe Panefile controls, never from what a
/// sandboxed thumbnailer prints: its stderr is as hostile as the file it reads.
bool sandboxUsable(const QString &bwrap)
{
    static const bool usable = [&bwrap] {
        const QTemporaryDir probeDir;
        if (!probeDir.isValid()) {
            return false;
        }
        const platform::ThumbnailerCommand probe{QStringLiteral("/usr/bin/true"), {}, false};
        QStringList argv =
            platform::Thumbnailers::sandboxed(probe, bwrap, probeDir.path(), probeDir.path());
        QProcess process;
        process.setProgram(argv.takeFirst());
        process.setArguments(argv);
        process.setStandardOutputFile(QProcess::nullDevice());
        process.start();
        const bool ok = process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit &&
                        process.exitCode() == 0;
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        }
        if (!ok) {
            qCWarning(pfPlugins) << "bubblewrap cannot start a sandbox here; running "
                                    "thumbnailers with a timeout only";
        }
        return ok;
    }();
    return usable;
}

constexpr int kExternalTimeoutMs = 10'000;
constexpr int kCancelPollMs = 250;

/// Removes the tree at `path`, however hostile its shape. The sandbox owns its
/// work directory, so it can leave mode-000 directories, nest them deeper than
/// PATH_MAX, or make one directory very wide; by-path removal then fails, or
/// re-listing a parent after every child makes cleanup quadratic.
/// This lists each directory exactly once, unlinking files as it goes and
/// keeping its subdirectory names on an explicit stack. It holds one open
/// descriptor at a time, descends with openat and climbs with openat("..") so
/// no path is ever built, and chmods each directory before listing it.
/// Symlinks are unlinked, never followed.
void removeTree(const QString &path)
{
    const QByteArray encoded = QFile::encodeName(path);
    int cur = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (cur < 0) {
        return;
    }
    struct Frame
    {
        std::string name;                  // this directory's name in its parent
        std::vector<std::string> pending;  // subdirectories not yet removed
    };
    std::vector<Frame> chain(1); // chain[0] is the root
    bool entered = false;        // whether chain.back() has been listed
    for (;;) {
        if (!entered) {
            ::fchmod(cur, S_IRWXU);
            Frame &frame = chain.back();
            if (DIR *dir = ::fdopendir(::dup(cur))) {
                while (const dirent *entry = ::readdir(dir)) {
                    const std::string name = entry->d_name;
                    if (name == "." || name == "..") {
                        continue;
                    }
                    struct stat st {};
                    if (::fstatat(cur, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                        S_ISDIR(st.st_mode)) {
                        frame.pending.push_back(name);
                    } else {
                        ::unlinkat(cur, name.c_str(), 0);
                    }
                }
                ::closedir(dir);
            }
            entered = true;
        }
        Frame &frame = chain.back();
        if (!frame.pending.empty()) {
            std::string subdir = std::move(frame.pending.back());
            frame.pending.pop_back();
            // A mode-000 directory cannot be opened until it is chmodded.
            ::fchmodat(cur, subdir.c_str(), S_IRWXU, 0);
            const int child =
                ::openat(cur, subdir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (child < 0) {
                continue; // cannot enter it; leave it and carry on with its siblings
            }
            ::close(cur);
            cur = child;
            chain.push_back({std::move(subdir), {}});
            entered = false;
            continue;
        }
        if (chain.size() == 1) {
            ::close(cur);
            ::rmdir(encoded.constData());
            return;
        }
        const int parent = ::openat(cur, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        ::close(cur);
        if (parent < 0) {
            return;
        }
        ::unlinkat(parent, chain.back().name.c_str(), AT_REMOVEDIR);
        chain.pop_back();
        cur = parent;
    }
}

/// A per-task work directory that is removed even when the sandboxed code left
/// unreadable or very deep directories in it.
class WorkDir
{
public:
    explicit WorkDir(const QString &templatePath) : m_dir(templatePath) { m_dir.setAutoRemove(false); }
    ~WorkDir()
    {
        if (m_dir.isValid()) {
            removeTree(m_dir.path());
        }
    }
    bool isValid() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }
    QString filePath(const QString &name) const { return m_dir.filePath(name); }

private:
    QTemporaryDir m_dir;
};

/// Removes task directories a crashed or killed earlier run left behind. Only
/// directories older than an hour go, so a second running instance is safe.
void sweepStaleWorkDirs(const QString &root)
{
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-3600);
    const QFileInfoList stale = QDir(root).entryInfoList({QStringLiteral("task-*")},
                                                         QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &entry : stale) {
        if (entry.lastModified() < cutoff) {
            removeTree(entry.absoluteFilePath());
        }
    }
}

/// Runs an external thumbnailer for `path` and reads back its PNG.
QImage runExternal(const QString &path, int target, const std::atomic<bool> &cancelled,
                   Outcome *outcome)
{
    *outcome = Outcome::Failed;

    static const QMimeDatabase database;
    const QMimeType mime = database.mimeTypeForFile(path);

    const QString root = platform::cacheDir() + QStringLiteral("/thumbnailer");
    QDir().mkpath(root);
    static std::once_flag swept;
    std::call_once(swept, sweepStaleWorkDirs, root);
    const WorkDir work(root + QStringLiteral("/task-XXXXXX"));
    if (!work.isValid()) {
        return {};
    }
    const QString output = work.filePath(QStringLiteral("out.png"));

    const auto command = platform::Thumbnailers::instance().commandFor(mime, path, output, target);
    if (!command) {
        return {};
    }

    QString program = command->program;
    QStringList arguments = command->arguments;
    if (!command->selfSandboxed) {
        if (const QString bwrap = QStandardPaths::findExecutable(QStringLiteral("bwrap"));
            !bwrap.isEmpty() && sandboxUsable(bwrap)) {
            QStringList argv =
                platform::Thumbnailers::sandboxed(*command, bwrap, path, work.path());
            program = argv.takeFirst();
            arguments = argv;
        }
    }

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setStandardOutputFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForStarted(5000)) {
        return {};
    }

    int waited = 0;
    while (!process.waitForFinished(kCancelPollMs)) {
        waited += kCancelPollMs;
        if (cancelled.load(std::memory_order_relaxed) || waited >= kExternalTimeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            return {};
        }
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return {};
    }

    // Only PNG is accepted, and it is decoded by Qt, never trusted.
    // The sandbox owns the work directory, so out.png may be a FIFO or a
    // symlink: open without following or blocking, and require a regular file.
    const int fd = ::open(QFile::encodeName(output).constData(),
                          O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    struct stat st {};
    constexpr qint64 kMaxOutputBytes = 64 * 1024 * 1024;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > kMaxOutputBytes) {
        ::close(fd);
        return {};
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return {};
    }
    QImageReader reader(&file, "png");
    QImage image = reader.read();
    if (!image.isNull()) {
        *outcome = Outcome::Generated;
    }
    return image;
}

/// Decodes an image file, scaled at decode time where the format allows it so
/// a 40-megapixel JPEG never materialises in full just to produce a 128 px
/// square.
QImage decodeImage(const QString &path, int target)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);

    const QSize source = reader.size();
    if (source.isValid() && (source.width() > target || source.height() > target)) {
        reader.setScaledSize(source.scaled(target, target, Qt::KeepAspectRatio));
    }
    return reader.read();
}

/// The generation worker. Holds no pointer back to the cache: it reports
/// through a QObject-free callback bound to a shared cancellation flag, and the
/// cache connects to it with a queued connection.
class ThumbnailTask : public QRunnable
{
public:
    enum class Mode { Image, Video, External };

    ThumbnailTask(QString path, ThumbnailCache::Size size, Mode mode, QString destination,
                  std::shared_ptr<std::atomic<bool>> cancelled,
                  std::function<void(QString, QImage, Outcome)> report)
        : m_path(std::move(path)), m_size(size), m_mode(mode),
          m_destination(std::move(destination)), m_cancelled(std::move(cancelled)),
          m_report(std::move(report))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_cancelled->load(std::memory_order_relaxed)) {
            return;
        }

        const QFileInfo info(m_path);
        const int target = ThumbnailCache::pixelsFor(m_size);

        QImage image;
        if (m_mode == Mode::External) {
            Outcome outcome = Outcome::Failed;
            image = runExternal(m_path, target, *m_cancelled, &outcome);
        } else if (m_mode == Mode::Video) {
            // Loaded here, on the worker, and never on the GUI thread: the
            // plugin pulls in libffmpegthumbnailer and the whole of libav*,
            // which is not a cost a directory listing should pause to pay.
            const auto *thumbnailer =
                platform::plugin<plugins::VideoThumbnailPlugin>(platform::Plugin::VideoThumbnails);
            if (thumbnailer == nullptr) {
                m_report(m_path, QImage(), Outcome::Unavailable);
                return;
            }
            QString error;
            image = thumbnailer->thumbnail(m_path, target, &error);
        } else {
            image = decodeImage(m_path, target);
        }

        if (image.isNull() || m_cancelled->load(std::memory_order_relaxed)) {
            m_report(m_path, QImage(), image.isNull() ? Outcome::Failed : Outcome::Generated);
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
            m_report(m_path, image, Outcome::Generated);
        }
    }

private:
    QString m_path;
    ThumbnailCache::Size m_size;
    Mode m_mode = Mode::Image;
    QString m_destination;
    std::shared_ptr<std::atomic<bool>> m_cancelled;
    std::function<void(QString, QImage, Outcome)> m_report;
};

} // namespace

int ThumbnailCache::pixelsFor(Size size)
{
    return size == Size::Large ? 256 : 128;
}

ThumbnailCache::Size ThumbnailCache::sizeFor(int logicalPx, qreal dpr)
{
    return static_cast<qreal>(logicalPx) * dpr <= 128.0 ? Size::Normal : Size::Large;
}

QString ThumbnailCache::memoryKey(const QString &absolutePath, Size size)
{
    return (size == Size::Large ? QStringLiteral("L|") : QStringLiteral("N|")) + absolutePath;
}

void ThumbnailCache::remember(const QString &absolutePath, Size size, const QImage &image)
{
    const int cost = std::max<int>(1, static_cast<int>(image.sizeInBytes() / 1024));
    m_memory.insert(memoryKey(absolutePath, size),
                    new MemoryEntry{.image = image,
                                    .modifiedSeconds =
                                        QFileInfo(absolutePath).lastModified().toSecsSinceEpoch()},
                    cost);
}

QImage ThumbnailCache::peek(const QString &absolutePath, Size size) const
{
    const MemoryEntry *entry = m_memory.object(memoryKey(absolutePath, size));
    return entry != nullptr ? entry->image : QImage();
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

void ThumbnailCache::setMemoryBudgetKb(int kilobytes)
{
    m_memory.setMaxCost(kilobytes);
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

    const QString key = memoryKey(absolutePath, size);
    if (const MemoryEntry *entry = m_memory.object(key); entry != nullptr) {
        if (entry->modifiedSeconds == sourceMTime) {
            return entry->image;
        }
        m_memory.remove(key);
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

    m_memory.insert(key, new MemoryEntry{.image = image, .modifiedSeconds = sourceMTime},
                    std::max<int>(1, static_cast<int>(image.sizeInBytes() / 1024)));
    return image;
}

ThumbnailCache::Kind ThumbnailCache::kindFor(const QString &absolutePath) const
{
    if (!m_enabled) {
        return Kind::None;
    }

    const QFileInfo info(absolutePath);
    if (!info.isFile() || info.isSymLink()) {
        return Kind::None;
    }
    if (info.size() > static_cast<qint64>(m_maxFileSizeMb) * 1024 * 1024) {
        return Kind::None;
    }

    static const QMimeDatabase database;
    const QMimeType mime = database.mimeTypeForFile(info);

    if (mime.name().startsWith(QLatin1String("video/"))) {
        // §7.7's `thumbnails.video`, and pf-video-thumb (§3.4). Whether the
        // plugin is installed is found out on the worker that first tries,
        // because loading it here would load libav* on the GUI thread. Without
        // it, a registered thumbnailer (ffmpegthumbnailer's own) does the job.
        if (!m_video) {
            return Kind::None;
        }
        if (!m_videoUnavailable) {
            return Kind::Video;
        }
        return platform::Thumbnailers::instance().handles(mime) ? Kind::External : Kind::None;
    }

    // In-process decoding is faster than spawning anything, so it goes first.
    if (isThumbnailable(mime)) {
        return Kind::Image;
    }

    // The registry is read here, on first need, and never at startup.
    return platform::Thumbnailers::instance().handles(mime) ? Kind::External : Kind::None;
}

bool ThumbnailCache::canThumbnail(const QString &absolutePath) const
{
    return kindFor(absolutePath) != Kind::None;
}

void ThumbnailCache::request(const QString &absolutePath, Size size, int priority)
{
    if (const auto pending = m_pendingSizes.constFind(absolutePath);
        pending != m_pendingSizes.constEnd()) {
        if (pending.value() == size) {
            return;
        }
        // One size in flight per path: the old one is no longer wanted.
        cancel(absolutePath);
    }

    const Kind kind = kindFor(absolutePath);
    if (kind == Kind::None || hasFailed(absolutePath)) {
        return;
    }

    if (const QImage cached = lookup(absolutePath, size); !cached.isNull()) {
        Q_EMIT ready(absolutePath, cached);
        return;
    }

    const auto cancelled = std::make_shared<std::atomic<bool>>(false);
    m_pendingFlags.insert(absolutePath, cancelled);
    m_pendingSizes.insert(absolutePath, size);

    // QPointer, not `this`: the worker outlives a cancelled request by however
    // long the decode takes, and the queued invocation must not resurrect a
    // destroyed cache.
    const QPointer<ThumbnailCache> self(this);
    auto report = [self, cancelled, size, priority](const QString &path, const QImage &image,
                                                    Outcome outcome) {
        if (cancelled->load(std::memory_order_relaxed)) {
            return;
        }
        QMetaObject::invokeMethod(
            self,
            [self, path, image, outcome, size, priority] {
                if (self.isNull()) {
                    return;
                }
                self->m_pendingFlags.remove(path);
                self->m_pendingSizes.remove(path);
                if (outcome == Outcome::Unavailable) {
                    // The plugin host remembers a missing plugin for the life
                    // of the process, so asking again would only queue work
                    // that is certain to end here. A registered thumbnailer
                    // may still take the file.
                    self->m_videoUnavailable = true;
                    self->request(path, size, priority);
                    return;
                }
                if (outcome == Outcome::Failed || image.isNull()) {
                    self->writeFailMarker(path);
                    Q_EMIT self->failed(path);
                    return;
                }
                self->remember(path, size, image);
                Q_EMIT self->ready(path, image);
            },
            Qt::QueuedConnection);
    };

    const ThumbnailTask::Mode mode = kind == Kind::Video      ? ThumbnailTask::Mode::Video
                                     : kind == Kind::External ? ThumbnailTask::Mode::External
                                                              : ThumbnailTask::Mode::Image;
    m_pool->start(new ThumbnailTask(absolutePath, size, mode,
                                    thumbnailPathFor(absolutePath, size, m_root), cancelled,
                                    std::move(report)),
                  priority);
}

void ThumbnailCache::cancel(const QString &absolutePath)
{
    m_pendingSizes.remove(absolutePath);
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
    m_pendingSizes.clear();
}

int ThumbnailCache::pendingCount() const
{
    return static_cast<int>(m_pendingFlags.size());
}

QThreadPool *ThumbnailCache::pool()
{
    return thumbnailPool();
}

} // namespace pf
