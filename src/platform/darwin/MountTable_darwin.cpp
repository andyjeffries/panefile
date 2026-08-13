// The macOS half of §7.11's mount enumeration.

#include "platform/MountTable.h"

#include <QFile>

#include <sys/mount.h>
#include <sys/param.h>

namespace pf::platform {

QList<MountPoint> currentMounts()
{
    // getmntinfo(3) rather than the getfsstat(2) it wraps: it does the
    // two-call sizing dance itself and returns a static buffer that is valid
    // until the next call, which is all this needs.
    //
    // MNT_NOWAIT, not MNT_WAIT: waiting asks every mounted filesystem to
    // update its statistics, and a stalled network mount would block the caller
    // for as long as its timeout. The cached numbers are more than good enough
    // for a sidebar.
    struct statfs *buffer = nullptr;
    const int count = getmntinfo(&buffer, MNT_NOWAIT);
    if (count <= 0 || buffer == nullptr) {
        return {};
    }

    QList<MountPoint> mounts;
    mounts.reserve(count);

    for (int i = 0; i < count; ++i) {
        const struct statfs &entry = buffer[i];

        MountPoint mount;
        mount.device = QFile::decodeName(entry.f_mntfromname);
        mount.mountPoint = QFile::decodeName(entry.f_mntonname);
        mount.fsType = QString::fromLatin1(entry.f_fstypename);
        mount.readOnly = (entry.f_flags & MNT_RDONLY) != 0;
        mount.isPseudo = isPseudoFilesystem(mount.fsType);

        mounts.append(mount);
    }

    return mounts;
}

} // namespace pf::platform
