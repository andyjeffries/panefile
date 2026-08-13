#pragma once

#include <QList>
#include <QString>

namespace pf::platform {

/// One mounted filesystem (§7.11).
struct MountPoint {
    QString device;     ///< `/dev/sda1`, `//server/share`
    QString mountPoint; ///< where it is mounted
    QString fsType;     ///< `ext4`, `apfs`, `nfs`
    bool readOnly = false;

    /// True for the pseudo-filesystems nobody wants in a sidebar: proc, sysfs,
    /// cgroup, devfs and their kin.
    bool isPseudo = false;

    /// True when this is somewhere a user might reasonably want to go — a real
    /// filesystem outside the pseudo set, and not one of the many bind mounts a
    /// container runtime leaves lying around.
    bool isInteresting() const;

    bool operator==(const MountPoint &other) const = default;
};

/// The mounted filesystems (§7.11).
///
/// §7.11 reads `/proc/self/mountinfo` on Linux and `getmntinfo(3)` on macOS.
/// Both produce the same list, so only the *acquisition* is per platform: the
/// mountinfo parser is a pure string function tested against committed
/// fixtures, and the filtering rules — which filesystems are pseudo, which are
/// worth showing — are shared and tested on both.
QList<MountPoint> currentMounts();

/// Parses the contents of `/proc/self/mountinfo`.
///
/// Pure, and available on every platform, so the format's genuinely fiddly
/// parts — the variable-length optional fields terminated by `-`, and the
/// octal escapes in paths — are tested on the machine the code is written on
/// rather than only on the one it ships to.
QList<MountPoint> parseMountInfo(const QString &contents);

/// Decodes mountinfo's octal escapes: a space in a mount point is written
/// `\040`. Without this a mount under "My Documents" parses as two fields.
QString decodeMountInfoPath(const QString &field);

/// The filesystem types §7.11 has no interest in showing.
bool isPseudoFilesystem(const QString &fsType);

} // namespace pf::platform
