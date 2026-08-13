// macOS file-operation acceleration: fcopyfile, and clonefile on APFS.

#include "platform/FileOps.h"

#include "core/Logging.h"

#include <QByteArray>
#include <QFile>

#include <cerrno>
#include <cstring>

#include <copyfile.h>
#include <sys/attr.h>
#include <sys/clonefile.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace pf::platform {

CopyAcceleration copyFileAccelerated(int sourceFd, int destinationFd, quint64 size,
                                     const std::function<bool(quint64)> &progress)
{
    // The macOS counterpart of Linux's copy_file_range and FICLONE.
    //
    // clonefile() is the closer analogue of FICLONE — on APFS it makes the
    // destination share the source's extents in constant time — but it takes
    // *paths* and insists on creating the destination itself, which cannot be
    // reconciled with §7.4's requirement to write through a `.pf-partial` file
    // and rename on completion. fcopyfile() works on the descriptors we already
    // have and still uses whatever the filesystem offers underneath.
    //
    // COPYFILE_DATA only: mode, times and attributes are handled separately by
    // the job, which needs them applied in a specific order relative to the
    // rename.
    if (::fcopyfile(sourceFd, destinationFd, nullptr, COPYFILE_DATA) == 0) {
        if (progress) {
            (void)progress(size);
        }
        return CopyAcceleration::Complete;
    }

    const int error = errno;

    // These mean "not this pair", the same as EXDEV and EOPNOTSUPP do on Linux,
    // and fall through to the caller's buffered loop.
    if (error == ENOTSUP || error == EXDEV || error == EINVAL) {
        return CopyAcceleration::Unsupported;
    }

    qCDebug(pfJobs) << "fcopyfile failed:" << std::strerror(error);
    return CopyAcceleration::Failed;
}

int copyExtendedAttributes(const QString &sourcePath, const QString &destinationPath)
{
    const QByteArray source = QFile::encodeName(sourcePath);
    const QByteArray destination = QFile::encodeName(destinationPath);

    // XATTR_NOFOLLOW throughout: §7.4 says never follow symlinks, and that
    // applies to attributes as much as to contents. It is the macOS spelling of
    // Linux's separate llistxattr/lgetxattr entry points.
    ssize_t listSize = ::listxattr(source.constData(), nullptr, 0, XATTR_NOFOLLOW);
    if (listSize <= 0) {
        return 0;
    }

    QByteArray names(listSize, Qt::Uninitialized);
    listSize = ::listxattr(source.constData(), names.data(), static_cast<size_t>(names.size()),
                           XATTR_NOFOLLOW);
    if (listSize <= 0) {
        return 0;
    }

    int copied = 0;
    const char *cursor = names.constData();
    const char *end = cursor + listSize;

    while (cursor < end) {
        const char *name = cursor;
        cursor += std::strlen(cursor) + 1;

        const ssize_t valueSize =
            ::getxattr(source.constData(), name, nullptr, 0, 0, XATTR_NOFOLLOW);
        if (valueSize < 0) {
            continue;
        }

        QByteArray value(valueSize, Qt::Uninitialized);
        if (::getxattr(source.constData(), name, value.data(), static_cast<size_t>(valueSize), 0,
                       XATTR_NOFOLLOW) < 0) {
            continue;
        }

        // Best effort, per §7.4. On macOS the attributes in play are Finder
        // tags, quarantine flags and resource forks; none of them is worth
        // failing a copy over.
        if (::setxattr(destination.constData(), name, value.constData(),
                       static_cast<size_t>(value.size()), 0, XATTR_NOFOLLOW) == 0) {
            ++copied;
        }
    }

    return copied;
}

} // namespace pf::platform
