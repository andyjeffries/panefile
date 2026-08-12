#include "fs/FsError.h"

#include <QCoreApplication>

#include <cerrno>
#include <cstring>

namespace pf::fs {

QString describeErrno(int error)
{
    switch (error) {
    case 0:
        return {};
    case EACCES:
        return QCoreApplication::translate("fs", "Permission denied");
    case EPERM:
        return QCoreApplication::translate("fs", "Operation not permitted");
    case ENOENT:
        return QCoreApplication::translate("fs", "No such file or directory");
    case ENOTDIR:
        return QCoreApplication::translate("fs", "Not a directory");
    case EISDIR:
        return QCoreApplication::translate("fs", "Is a directory");
    case ENOSPC:
        return QCoreApplication::translate("fs", "No space left on device");
    case EDQUOT:
        return QCoreApplication::translate("fs", "Disk quota exceeded");
    case EROFS:
        return QCoreApplication::translate("fs", "Read-only filesystem");
    case ENOTEMPTY:
        return QCoreApplication::translate("fs", "Directory is not empty");
    case EEXIST:
        return QCoreApplication::translate("fs", "Already exists");
    case EXDEV:
        return QCoreApplication::translate("fs", "Cannot move across filesystems");
    case ELOOP:
        return QCoreApplication::translate("fs", "Too many levels of symbolic links");
    case ENAMETOOLONG:
        return QCoreApplication::translate("fs", "Name is too long");
    case EMFILE:
    case ENFILE:
        return QCoreApplication::translate("fs", "Too many open files");
    case EIO:
        return QCoreApplication::translate("fs", "Input/output error — the device may be failing");
    case ENODEV:
    case ENXIO:
        return QCoreApplication::translate("fs", "Device is not available");
    case EBUSY:
        return QCoreApplication::translate("fs", "Resource is busy");
    case ETIMEDOUT:
        return QCoreApplication::translate("fs", "Timed out");
    case EINVAL:
        return QCoreApplication::translate("fs", "Invalid argument");
    default:
        break;
    }

    // Anything unlisted still gets the system's own wording rather than a
    // number. strerror_r's two incompatible signatures are not worth the
    // configure check here: this path is rare and never on a hot loop.
    return QString::fromLocal8Bit(std::strerror(error));
}

QString describeErrno(int error, const QString &path, const QString &operation)
{
    return QCoreApplication::translate("fs", "%1 %2: %3")
        .arg(operation, path, describeErrno(error));
}

} // namespace pf::fs
