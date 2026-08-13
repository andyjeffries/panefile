#include "fs/jobs/TransferJob.h"

#include "core/Logging.h"
#include "fs/FsError.h"
#include "platform/FileOps.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cerrno>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

namespace pf::fs {
namespace {

/// §7.4's fallback when the kernel cannot accelerate the copy.
constexpr qint64 kBufferSize = 1024LL * 1024;

/// Appends " (2)", " (3)" … until the name is free (§7.4's rename resolution).
QString uniqueDestination(const QString &path)
{
    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int counter = 2; counter < 10000; ++counter) {
        // The counter goes before the extension, not after it: "notes (2).txt"
        // stays a text file, "notes.txt (2)" does not.
        const QString candidate =
            suffix.isEmpty()
                ? QStringLiteral("%1/%2 (%3)").arg(directory, base).arg(counter)
                : QStringLiteral("%1/%2 (%3).%4").arg(directory, base).arg(counter).arg(suffix);

        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

/// True when `child` is inside `parent`, or is `parent`.
///
/// §7.4: "Refuse, with a clear error, to copy a directory into itself or into
/// its own descendant." Without this the enumeration walks into the copy it is
/// making and never terminates.
bool isInside(const QString &parent, const QString &child)
{
    const QString cleanParent = QDir::cleanPath(parent);
    const QString cleanChild = QDir::cleanPath(child);

    if (cleanParent == cleanChild) {
        return true;
    }
    return cleanChild.startsWith(cleanParent + QLatin1Char('/'));
}

} // namespace

TransferJob::TransferJob(Mode mode, QStringList sources, const QString &destinationDirectory,
                         QObject *parent)
    : Job(parent), m_mode(mode), m_sources(std::move(sources)),
      m_destinationDirectory(QDir::cleanPath(destinationDirectory))
{}

QString TransferJob::description() const
{
    const int count = m_filesTotal > 0 ? m_filesTotal : static_cast<int>(m_sources.size());
    return m_mode == Mode::Copy ? tr("Copying %n item(s)", nullptr, count)
                                : tr("Moving %n item(s)", nullptr, count);
}

QStringList TransferJob::createdPaths() const
{
    return m_createdPaths;
}

QStringList TransferJob::removedSources() const
{
    return m_removedSources;
}

bool TransferJob::enumerate()
{
    for (const QString &source : std::as_const(m_sources)) {
        const QString cleanSource = QDir::cleanPath(source);

        // §7.4's guard, checked before anything is written. A copy into its own
        // descendant would otherwise enumerate the copy it is creating.
        if (isInside(cleanSource, m_destinationDirectory)) {
            addError(cleanSource, tr("Cannot copy a directory into itself or into one of its own "
                                     "subdirectories"));
            return false;
        }

        if (!collect(cleanSource, m_destinationDirectory)) {
            return false;
        }
        if (isCancelled()) {
            return false;
        }
    }

    return !m_items.isEmpty();
}

bool TransferJob::collect(const QString &source, const QString &destinationDirectory)
{
    const QFileInfo info(source);
    const QString destination = destinationDirectory + QLatin1Char('/') + info.fileName();

    // isSymLink() before isDir(): §7.4 says never follow symlinks, and a link
    // to a directory answers yes to both. Testing dir-ness first would walk
    // into the target and copy somebody's whole home directory because they
    // had a convenience link in it.
    if (info.isSymLink()) {
        m_items.append(Item{.source = source,
                            .destination = destination,
                            .size = 0,
                            .isDirectory = false,
                            .isSymlink = true});
        ++m_filesTotal;
        return true;
    }

    if (info.isDir()) {
        m_items.append(Item{.source = source,
                            .destination = destination,
                            .size = 0,
                            .isDirectory = true,
                            .isSymlink = false});
        m_sourceDirectories.append(source);

        // §12: "use iterative traversal with an explicit stack, not recursion."
        // A directory tree deep enough to overflow the stack is rare but
        // trivially constructible, and a file manager is exactly the program
        // somebody points at one.
        QList<QPair<QString, QString>> pending{{source, destination}};

        while (!pending.isEmpty()) {
            if (isCancelled()) {
                return false;
            }
            const auto [currentSource, currentDestination] = pending.takeLast();

            const QDir directory(currentSource);
            const QFileInfoList entries = directory.entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

            for (const QFileInfo &entry : entries) {
                const QString childDestination =
                    currentDestination + QLatin1Char('/') + entry.fileName();

                if (entry.isSymLink()) {
                    m_items.append(Item{.source = entry.absoluteFilePath(),
                                        .destination = childDestination,
                                        .size = 0,
                                        .isDirectory = false,
                                        .isSymlink = true});
                    ++m_filesTotal;
                } else if (entry.isDir()) {
                    m_items.append(Item{.source = entry.absoluteFilePath(),
                                        .destination = childDestination,
                                        .size = 0,
                                        .isDirectory = true,
                                        .isSymlink = false});
                    m_sourceDirectories.append(entry.absoluteFilePath());
                    pending.append({entry.absoluteFilePath(), childDestination});
                } else {
                    m_items.append(Item{.source = entry.absoluteFilePath(),
                                        .destination = childDestination,
                                        .size = static_cast<quint64>(entry.size()),
                                        .isDirectory = false,
                                        .isSymlink = false});
                    ++m_filesTotal;
                    m_bytesTotal += static_cast<quint64>(entry.size());
                }
            }
        }
        return true;
    }

    m_items.append(Item{.source = source,
                        .destination = destination,
                        .size = static_cast<quint64>(info.size()),
                        .isDirectory = false,
                        .isSymlink = false});
    ++m_filesTotal;
    m_bytesTotal += static_cast<quint64>(info.size());
    return true;
}

QString TransferJob::resolveDestination(const Item &item)
{
    if (!QFileInfo::exists(item.destination)) {
        return item.destination;
    }

    // A directory that already exists is not a conflict — the transfer merges
    // into it, which is what every file manager does and what a user copying
    // one tree over another expects.
    if (item.isDirectory && QFileInfo(item.destination).isDir()) {
        return item.destination;
    }

    const QFileInfo sourceInfo(item.source);
    const QFileInfo destinationInfo(item.destination);

    const ConflictInfo info{.sourceSize = static_cast<quint64>(sourceInfo.size()),
                            .destinationSize = static_cast<quint64>(destinationInfo.size()),
                            .sourceModified = sourceInfo.lastModified(),
                            .destinationModified = destinationInfo.lastModified(),
                            .destinationIsDirectory = destinationInfo.isDir()};

    const ConflictResolution resolution = askAboutConflict(item.source, item.destination, info);

    switch (resolution.action) {
    case ConflictAction::Overwrite:
        return item.destination;

    case ConflictAction::OverwriteIfNewer:
        // Strictly newer. Equal timestamps mean the same file as far as anyone
        // can tell, and rewriting it would be work with no result.
        if (sourceInfo.lastModified() > destinationInfo.lastModified()) {
            return item.destination;
        }
        return {};

    case ConflictAction::Rename:
        return uniqueDestination(item.destination);

    case ConflictAction::Skip:
    case ConflictAction::Cancel:
        break;
    }
    return {};
}

bool TransferJob::copySymlink(const Item &item)
{
    // §7.4: "Never follow symlinks. Recreate them as symlinks pointing at the
    // same target." The target string is copied verbatim, relative links
    // included — resolving them would silently change what the copy points at.
    std::array<char, PATH_MAX> buffer{};
    const ssize_t length =
        ::readlink(QFile::encodeName(item.source).constData(), buffer.data(), buffer.size() - 1);
    if (length <= 0) {
        addError(item.source, describeErrno(errno));
        return false;
    }

    const QByteArray target(buffer.data(), length);
    const QByteArray destination = QFile::encodeName(item.destination);

    ::unlink(destination.constData());
    if (::symlink(target.constData(), destination.constData()) != 0) {
        addError(item.destination, describeErrno(errno));
        return false;
    }
    return true;
}

bool TransferJob::copyFileContents(const Item &item, const QString &partialPath)
{
    const QByteArray sourcePath = QFile::encodeName(item.source);
    const QByteArray partial = QFile::encodeName(partialPath);

    const int sourceFd = ::open(sourcePath.constData(), O_RDONLY | O_CLOEXEC);
    if (sourceFd < 0) {
        addError(item.source, describeErrno(errno));
        return false;
    }

    struct stat sourceInfo{};
    ::fstat(sourceFd, &sourceInfo);

    const int destinationFd =
        ::open(partial.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (destinationFd < 0) {
        addError(partialPath, describeErrno(errno));
        ::close(sourceFd);
        return false;
    }

    const quint64 startingBytes = m_bytesDone;
    bool ok = true;

    const auto onProgress = [this, startingBytes, &item](quint64 copied) {
        m_bytesDone = startingBytes + copied;
        reportProgress(item.source);
        return !isCancelled();
    };

    const platform::CopyAcceleration accelerated =
        platform::copyFileAccelerated(sourceFd, destinationFd, item.size, onProgress);

    if (accelerated == platform::CopyAcceleration::Unsupported) {
        // §7.4's fallback: a 1 MiB buffered loop, checking cancellation between
        // chunks so a large file can be abandoned partway.
        m_bytesDone = startingBytes;
        ::lseek(sourceFd, 0, SEEK_SET);
        ::lseek(destinationFd, 0, SEEK_SET);
        if (::ftruncate(destinationFd, 0) != 0) {
            // Not fatal on its own; the write below overwrites from zero.
            qCDebug(pfJobs) << "could not truncate" << partialPath;
        }

        QByteArray buffer(kBufferSize, Qt::Uninitialized);
        while (true) {
            if (isCancelled()) {
                ok = false;
                break;
            }
            const ssize_t readBytes = ::read(sourceFd, buffer.data(), kBufferSize);
            if (readBytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                addError(item.source, describeErrno(errno));
                ok = false;
                break;
            }
            if (readBytes == 0) {
                break;
            }

            ssize_t written = 0;
            while (written < readBytes) {
                const ssize_t chunk =
                    ::write(destinationFd, buffer.constData() + written, readBytes - written);
                if (chunk < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    addError(partialPath, describeErrno(errno));
                    ok = false;
                    break;
                }
                written += chunk;
            }
            if (!ok) {
                break;
            }

            m_bytesDone += static_cast<quint64>(readBytes);
            reportProgress(item.source);
        }
    } else if (accelerated == platform::CopyAcceleration::Failed) {
        addError(item.source, describeErrno(errno));
        ok = false;
    }

    ::close(sourceFd);
    ::close(destinationFd);

    if (!ok || isCancelled()) {
        // §7.4: "Cancellation is cooperative and must leave no half-written
        // destination file." The partial file is the whole point — nothing is
        // ever written directly to the destination name.
        ::unlink(partial.constData());
        return false;
    }

    return true;
}

void TransferJob::applyMetadata(const QString &source, const QString &destination)
{
    struct stat info{};
    if (::lstat(QFile::encodeName(source).constData(), &info) != 0) {
        return;
    }

    const QByteArray target = QFile::encodeName(destination);

    // §7.4: "Preserve mode, mtime, and — best-effort, never fatal — ownership
    // and extended attributes."
    ::chmod(target.constData(), info.st_mode & 07777);

    struct utimbuf times{};
    times.actime = info.st_atime;
    times.modtime = info.st_mtime;
    ::utime(target.constData(), &times);

    // Ownership needs privileges we usually do not have, so its failure is
    // expected rather than exceptional and is not reported.
    ::chown(target.constData(), info.st_uid, info.st_gid);

    platform::copyExtendedAttributes(source, destination);
}

void TransferJob::transferOne(const Item &item)
{
    if (item.isDirectory) {
        QDir().mkpath(item.destination);
        m_createdDirectories.append(item.destination);
        applyMetadata(item.source, item.destination);
        return;
    }

    const QString destination = resolveDestination(item);
    if (destination.isEmpty()) {
        // Skipped, or the job was cancelled. Either way the bytes were counted
        // during enumeration, so the progress bar has to be told they are done
        // or it stops short of the end.
        m_bytesDone += item.size;
        ++m_filesDone;
        return;
    }

    // §7.4: a move within one filesystem is a rename and is instant.
    if (m_mode == Mode::Move && platform::onSameFilesystem(item.source, m_destinationDirectory)) {
        if (::rename(QFile::encodeName(item.source).constData(),
                     QFile::encodeName(destination).constData()) == 0) {
            m_createdPaths.append(destination);
            m_removedSources.append(item.source);
            m_bytesDone += item.size;
            ++m_filesDone;
            reportProgress(item.source);
            return;
        }
        // A failed rename falls through to copy-and-delete rather than being
        // reported: EXDEV here means the two paths turned out to be on
        // different filesystems after all, which is a normal thing to discover.
    }

    bool ok = false;
    if (item.isSymlink) {
        ok = copySymlink(item);
        if (ok) {
            ++m_filesDone;
        }
    } else {
        const QString partial = destination + kPartialSuffix;
        ok = copyFileContents(item, partial);

        if (ok) {
            applyMetadata(item.source, partial);
            // The rename is what makes the file appear, complete, at its final
            // name — never a partially written file under the name a user is
            // about to open.
            if (::rename(QFile::encodeName(partial).constData(),
                         QFile::encodeName(destination).constData()) != 0) {
                addError(destination, describeErrno(errno));
                ::unlink(QFile::encodeName(partial).constData());
                ok = false;
            } else {
                ++m_filesDone;
            }
        }
    }

    if (!ok) {
        return;
    }

    m_createdPaths.append(destination);

    if (m_mode == Mode::Move) {
        // §7.4: "Cross-device moves are copy-then-delete, and the delete only
        // happens after a fully successful copy."
        if (::unlink(QFile::encodeName(item.source).constData()) == 0) {
            m_removedSources.append(item.source);
        } else {
            addError(item.source, describeErrno(errno));
        }
    }

    reportProgress(item.source);
}

void TransferJob::execute()
{
    if (!QDir().mkpath(m_destinationDirectory)) {
        addError(m_destinationDirectory, tr("Cannot create the destination directory"));
        return;
    }

    for (const Item &item : std::as_const(m_items)) {
        if (isCancelled()) {
            return;
        }
        transferOne(item);
    }

    if (m_mode != Mode::Move || isCancelled()) {
        return;
    }

    // Source directories go last and deepest-first, because a directory can
    // only be removed once its contents have gone. rmdir rather than a
    // recursive delete: anything still inside was skipped or failed, and
    // removing it here would destroy a file the user chose to keep.
    std::ranges::reverse(m_sourceDirectories);
    for (const QString &directory : std::as_const(m_sourceDirectories)) {
        if (::rmdir(QFile::encodeName(directory).constData()) == 0) {
            m_removedSources.append(directory);
        }
    }
}

} // namespace pf::fs
