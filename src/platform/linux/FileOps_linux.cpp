// Linux file-operation acceleration: copy_file_range(2) and FICLONE.

#include "platform/FileOps.h"

#include "core/Logging.h"

#include <QByteArray>
#include <QFile>

#include <array>
#include <cerrno>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace pf::platform {
namespace {

/// One copy_file_range call is capped, so progress is reported and cancellation
/// is noticed during a large file rather than only between files. A gigabyte in
/// one call would be faster in the best case and unresponsive in every case.
constexpr size_t kChunkSize = 64ULL * 1024 * 1024;

} // namespace

CopyAcceleration copyFileAccelerated(int sourceFd, int destinationFd, quint64 size,
                                     const std::function<bool(quint64)> &progress)
{
    // FICLONE first: on btrfs and XFS it makes the destination share the
    // source's extents, so the copy is constant time and consumes no space
    // until one of them is written to. It is all-or-nothing and only works
    // within one filesystem, which is exactly why it is tried first and
    // discarded quietly.
    if (::ioctl(destinationFd, FICLONE, sourceFd) == 0) {
        if (progress) {
            (void)progress(size);
        }
        return CopyAcceleration::Complete;
    }

    // EOPNOTSUPP, EXDEV and EINVAL all mean "not this filesystem, not this
    // pair" rather than a genuine failure, so they fall through to
    // copy_file_range rather than being reported.
    quint64 copied = 0;
    bool anySucceeded = false;

    while (copied < size) {
        const size_t remaining = static_cast<size_t>(size - copied);
        const ssize_t written = ::copy_file_range(sourceFd, nullptr, destinationFd, nullptr,
                                                  std::min(remaining, kChunkSize), 0);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            // Same reasoning: these mean the kernel cannot accelerate this
            // pair, not that the copy failed.
            if (errno == EXDEV || errno == EOPNOTSUPP || errno == ENOSYS || errno == EINVAL ||
                errno == EPERM) {
                if (anySucceeded) {
                    // Part of the file was copied by the kernel and the rest
                    // cannot be. Reporting Unsupported would make the caller
                    // restart from zero, which is correct but wasteful; it is
                    // simpler and safer to make the caller redo the whole file
                    // with a known-good path.
                    qCDebug(pfJobs)
                        << "copy_file_range stopped after" << copied << "bytes; falling back";
                }
                return CopyAcceleration::Unsupported;
            }
            return CopyAcceleration::Failed;
        }

        if (written == 0) {
            // A short file, or the source shrank under us. Either way there is
            // nothing more to copy.
            break;
        }

        anySucceeded = true;
        copied += static_cast<quint64>(written);

        if (progress && !progress(copied)) {
            return CopyAcceleration::Failed;
        }
    }

    return copied >= size ? CopyAcceleration::Complete : CopyAcceleration::Unsupported;
}

int copyExtendedAttributes(const QString &sourcePath, const QString &destinationPath)
{
    const QByteArray source = QFile::encodeName(sourcePath);
    const QByteArray destination = QFile::encodeName(destinationPath);

    // llistxattr rather than listxattr: §7.4 says never follow symlinks, and
    // that applies to attributes as much as to contents.
    ssize_t listSize = ::llistxattr(source.constData(), nullptr, 0);
    if (listSize <= 0) {
        return 0;
    }

    QByteArray names(listSize, Qt::Uninitialized);
    listSize = ::llistxattr(source.constData(), names.data(), static_cast<size_t>(names.size()));
    if (listSize <= 0) {
        return 0;
    }

    int copied = 0;
    const char *cursor = names.constData();
    const char *end = cursor + listSize;

    while (cursor < end) {
        const char *name = cursor;
        cursor += std::strlen(cursor) + 1;

        const ssize_t valueSize = ::lgetxattr(source.constData(), name, nullptr, 0);
        if (valueSize < 0) {
            continue;
        }

        QByteArray value(valueSize, Qt::Uninitialized);
        if (::lgetxattr(source.constData(), name, value.data(), static_cast<size_t>(valueSize)) <
            0) {
            continue;
        }

        // Best effort throughout, per §7.4: the destination filesystem may not
        // support attributes at all, and security.* needs privileges we do not
        // have. A copy that failed because a SELinux label could not be carried
        // across would be worse than one that quietly did not carry it.
        if (::lsetxattr(destination.constData(), name, value.constData(),
                        static_cast<size_t>(value.size()), 0) == 0) {
            ++copied;
        }
    }

    return copied;
}

} // namespace pf::platform
