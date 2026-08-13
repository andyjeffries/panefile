// Parts of the file-operation seam that are identical on both platforms.

#include "platform/FileOps.h"

#include <QFile>

#include <sys/stat.h>

namespace pf::platform {

dev_t deviceIdOf(const QString &path)
{
    struct stat info{};
    if (::stat(QFile::encodeName(path).constData(), &info) != 0) {
        return 0;
    }
    return info.st_dev;
}

bool onSameFilesystem(const QString &first, const QString &second)
{
    const dev_t firstDevice = deviceIdOf(first);
    const dev_t secondDevice = deviceIdOf(second);

    // A zero means the path could not be stat'ed, which for a move usually
    // means the destination does not exist yet. Answering "same" on a guess
    // would send the caller down the rename(2) path, and a rename that fails
    // with EXDEV is recoverable — whereas a copy-and-delete across what turned
    // out to be one filesystem is slow but harmless. Neither is worth guessing
    // about, so an unknown device answers no.
    if (firstDevice == 0 || secondDevice == 0) {
        return false;
    }
    return firstDevice == secondDevice;
}

} // namespace pf::platform
