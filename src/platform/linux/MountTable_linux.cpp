// The Linux half of §7.11's mount enumeration.

#include "platform/MountTable.h"

#include <QFile>

namespace pf::platform {

QList<MountPoint> currentMounts()
{
    // §7.11: "Enumerate mounted filesystems from /proc/self/mountinfo."
    //
    // mountinfo rather than /proc/mounts or /etc/mtab: it is the only one that
    // distinguishes a bind mount from the filesystem it shadows, and it is
    // per-process, so it tells the truth inside a container.
    QFile file(QStringLiteral("/proc/self/mountinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return parseMountInfo(QString::fromUtf8(file.readAll()));
}

} // namespace pf::platform
