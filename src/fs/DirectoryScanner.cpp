#include "fs/DirectoryScanner.h"

#include "core/Logging.h"
#include "fs/FsError.h"

#include <QFile>
#include <QMimeDatabase>
#include <QRunnable>
#include <QThreadPool>

#include "core/WorkerPools.h"

#include <array>
#include <atomic>
#include <mutex>

#include <cerrno>
#include <climits>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pf::fs {
namespace {

/// Copies the parts of a stat buffer that FileEntry cares about.
void applyStat(FileEntry &entry, const struct stat &info)
{
    entry.size = static_cast<quint64>(info.st_size);
    entry.mode = info.st_mode;
    entry.uid = info.st_uid;
    entry.gid = info.st_gid;
    entry.modified = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(info.st_mtime));
    entry.isExecutable = (info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

/// Builds one entry from a directory fd and a name.
///
/// Uses the *at() family throughout so no full path is ever assembled: for a
/// 100,000-entry directory that is 100,000 string concatenations avoided, and
/// it sidesteps PATH_MAX entirely (§12).
///
/// §7.2: d_type is used where it answers the question. It cannot answer the
/// size, mode or mtime that the footer and the sort keys need, so one lstat per
/// entry is unavoidable — but the *second* stat, the one that resolves a
/// symlink, happens only for entries that actually are symlinks.
FileEntry buildEntry(int dirFd, const char *name, unsigned char dtype)
{
    FileEntry entry;
    entry.name = QString::fromLocal8Bit(name);
    entry.isHidden = entry.name.startsWith(QLatin1Char('.'));

    struct stat info{};
    if (::fstatat(dirFd, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        // A directory entry we cannot stat is still an entry the user can see.
        // Listing it with unknown metadata beats hiding it.
        entry.statFailed = true;
        entry.isDir = (dtype == DT_DIR);
        return entry;
    }

    applyStat(entry, info);
    entry.isSymlink = S_ISLNK(info.st_mode);

    if (!entry.isSymlink) {
        entry.isDir = S_ISDIR(info.st_mode);
        return entry;
    }

    // §4.2: lstat then stat; a failing stat after a successful lstat is exactly
    // what a dangling symlink looks like.
    struct stat target{};
    if (::fstatat(dirFd, name, &target, 0) == 0) {
        entry.isDir = S_ISDIR(target.st_mode);
        // The size and mtime a user wants to see for a symlink are the target's,
        // but the mode is the link's own — that is what ls -l shows.
        entry.size = static_cast<quint64>(target.st_size);
        entry.modified = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(target.st_mtime));
        entry.isExecutable = (target.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
    } else {
        entry.isBroken = true;
    }

    std::array<char, PATH_MAX> linkBuffer{};
    const ssize_t length = ::readlinkat(dirFd, name, linkBuffer.data(), linkBuffer.size() - 1);
    if (length > 0) {
        entry.linkTarget =
            QString::fromLocal8Bit(linkBuffer.data(), static_cast<qsizetype>(length));
    }

    return entry;
}

} // namespace

/// Owns the signals of a single scan and outlives the scanner if it has to.
///
/// A worker thread must be able to emit into the GUI thread without checking
/// whether its scanner still exists. Both the scanner and the running task hold
/// a shared_ptr to this, so it survives until the last of them is done, and the
/// custom deleter routes destruction back to the GUI thread through
/// deleteLater() for the case where the worker holds the final reference.
class DirectoryScanner::Channel : public QObject
{
    Q_OBJECT

public:
    std::atomic<bool> cancelled{false};

Q_SIGNALS:
    void started(const QString &path);
    void entriesReady(const QString &path, const QList<pf::FileEntry> &entries);
    void finished(const QString &path, int totalCount);
    void failed(const QString &path, const QString &reason);
};

namespace {

class ScanTask : public QRunnable
{
public:
    ScanTask(std::shared_ptr<DirectoryScanner::Channel> channel, QString path)
        : m_channel(std::move(channel)), m_path(std::move(path))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        warmMimeDatabase();

        Q_EMIT m_channel->started(m_path);

        const QByteArray nativePath = QFile::encodeName(m_path);
        DIR *dir = ::opendir(nativePath.constData());
        if (dir == nullptr) {
            const int error = errno;
            if (!m_channel->cancelled.load(std::memory_order_relaxed)) {
                Q_EMIT m_channel->failed(m_path, describeErrno(error));
            }
            return;
        }

        // Every entry is stat'ed relative to this descriptor. dirfd() cannot
        // realistically fail for a stream opendir() just returned, but every
        // fstatat below would silently resolve against the *current* directory
        // if it did — listing one directory while reporting another's contents.
        const int dirFd = ::dirfd(dir);
        if (dirFd < 0) {
            const int error = errno;
            ::closedir(dir);
            if (!m_channel->cancelled.load(std::memory_order_relaxed)) {
                Q_EMIT m_channel->failed(m_path, describeErrno(error));
            }
            return;
        }

        QList<FileEntry> batch;
        batch.reserve(DirectoryScanner::kBatchSize);
        int total = 0;

        while (true) {
            errno = 0;
            const struct dirent *record = ::readdir(dir);
            if (record == nullptr) {
                if (errno != 0) {
                    // A read error partway through leaves us with a partial
                    // listing. Reporting it is better than silently truncating.
                    const int error = errno;
                    ::closedir(dir);
                    if (!m_channel->cancelled.load(std::memory_order_relaxed)) {
                        Q_EMIT m_channel->failed(m_path, describeErrno(error));
                    }
                    return;
                }
                break;
            }

            if (isDotOrDotDot(record->d_name)) {
                continue;
            }

            batch.append(buildEntry(dirFd, record->d_name, record->d_type));
            ++total;

            if (batch.size() >= DirectoryScanner::kBatchSize) {
                // §7.2: check the cancellation flag every batch, and abandon
                // rather than deliver.
                if (m_channel->cancelled.load(std::memory_order_relaxed)) {
                    ::closedir(dir);
                    return;
                }
                Q_EMIT m_channel->entriesReady(m_path, batch);
                batch.clear();
                batch.reserve(DirectoryScanner::kBatchSize);
            }
        }

        ::closedir(dir);

        if (m_channel->cancelled.load(std::memory_order_relaxed)) {
            return;
        }
        if (!batch.isEmpty()) {
            Q_EMIT m_channel->entriesReady(m_path, batch);
        }
        Q_EMIT m_channel->finished(m_path, total);
    }

private:
    /// Forces the shared-mime-info caches to load, here rather than wherever
    /// they would otherwise first be needed.
    ///
    /// §4.3: "This is unavoidable but happens on the scanner thread, not the
    /// GUI thread." Without this the first call lands in the delegate's paint(),
    /// resolving an icon for the first visible row — squarely on the path to
    /// first paint, which §11 puts a budget on. Doing it here overlaps it with
    /// window realisation instead, and by the time any row is painted the
    /// caches are warm.
    static void warmMimeDatabase()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            const QMimeDatabase database;
            (void)database.mimeTypeForFile(QStringLiteral("a.txt"), QMimeDatabase::MatchExtension);
        });
    }

    static bool isDotOrDotDot(const char *name)
    {
        return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
    }

    std::shared_ptr<DirectoryScanner::Channel> m_channel;
    QString m_path;
};

} // namespace

QThreadPool *DirectoryScanner::pool()
{
    // Function-local static, so no thread pool is created at load time for a
    // process that turns out to be a --version invocation (§3.4). Registered
    // with WorkerPools so that shutdown waits for it — a scan still running
    // when main() returns can reach a Qt global that static destruction has
    // already torn down, which is exactly how it crashed.
    static QThreadPool *instance = WorkerPools::acquire("pf-scanner", 4); // §3.3
    return instance;
}

DirectoryScanner::DirectoryScanner(QObject *parent) : QObject(parent) {}

DirectoryScanner::~DirectoryScanner()
{
    // The task may still be running. Cancelling and dropping our reference is
    // enough: the channel stays alive until the task lets go of it, and the
    // signals it might still emit are no longer connected to anything.
    detachCurrentScan();
}

void DirectoryScanner::detachCurrentScan()
{
    if (!m_channel) {
        return;
    }
    m_channel->cancelled.store(true, std::memory_order_relaxed);
    m_channel->disconnect(this);
    m_channel.reset();
    m_scanning = false;
}

void DirectoryScanner::scan(const QString &path)
{
    detachCurrentScan();

    m_path = path;
    m_scanning = true;

    // deleteLater rather than delete: the last reference may be dropped on a
    // worker thread, and a QObject must be destroyed in the thread it lives in.
    m_channel =
        std::shared_ptr<Channel>(new Channel, [](Channel *channel) { channel->deleteLater(); });

    connect(m_channel.get(), &Channel::started, this, &DirectoryScanner::started);
    connect(m_channel.get(), &Channel::entriesReady, this, &DirectoryScanner::entriesReady);
    connect(m_channel.get(), &Channel::failed, this,
            [this](const QString &scanPath, const QString &reason) {
                m_scanning = false;
                Q_EMIT failed(scanPath, reason);
            });
    connect(m_channel.get(), &Channel::finished, this,
            [this](const QString &scanPath, int totalCount) {
                m_scanning = false;
                Q_EMIT finished(scanPath, totalCount);
            });

    qCDebug(pfFs) << "scanning" << path;
    pool()->start(new ScanTask(m_channel, path));
}

void DirectoryScanner::cancel()
{
    detachCurrentScan();
}

bool DirectoryScanner::isScanning() const
{
    return m_scanning;
}

QString DirectoryScanner::path() const
{
    return m_path;
}

} // namespace pf::fs

#include "DirectoryScanner.moc"
