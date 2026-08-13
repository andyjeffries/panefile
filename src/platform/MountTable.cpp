#include "platform/MountTable.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace pf::platform {
namespace {

/// The pseudo-filesystems. Kernel bookkeeping, not places.
///
/// A list rather than a rule, because there is no rule: `tmpfs` is a real
/// filesystem a user may well want (`/tmp`, `/dev/shm`) and also the mechanism
/// behind a dozen things they do not, and only the name distinguishes them.
const QSet<QString> &pseudoTypes()
{
    static const QSet<QString> types{
        QStringLiteral("autofs"),    QStringLiteral("bpf"),        QStringLiteral("binfmt_misc"),
        QStringLiteral("cgroup"),    QStringLiteral("cgroup2"),    QStringLiteral("configfs"),
        QStringLiteral("debugfs"),   QStringLiteral("devpts"),     QStringLiteral("devtmpfs"),
        QStringLiteral("efivarfs"),  QStringLiteral("fusectl"),    QStringLiteral("hugetlbfs"),
        QStringLiteral("mqueue"),    QStringLiteral("proc"),       QStringLiteral("pstore"),
        QStringLiteral("ramfs"),     QStringLiteral("rpc_pipefs"), QStringLiteral("securityfs"),
        QStringLiteral("selinuxfs"), QStringLiteral("sysfs"),      QStringLiteral("tracefs"),
        QStringLiteral("devfs"),     QStringLiteral("nullfs"),     QStringLiteral("fdescfs"),
    };
    return types;
}

/// Mount points that are plumbing rather than places, whatever their type.
bool isPlumbingPath(const QString &path)
{
    static const QStringList prefixes{
        QStringLiteral("/proc"),
        QStringLiteral("/sys"),
        QStringLiteral("/dev"),
        QStringLiteral("/run"),
        // The firmlinked data volume on modern macOS. It is the same tree the
        // user already sees through `/`, so showing it is a duplicate rather
        // than a place.
        QStringLiteral("/System/Volumes/Data"),
        QStringLiteral("/System/Volumes/Preboot"),
        QStringLiteral("/System/Volumes/VM"),
        QStringLiteral("/System/Volumes/Update"),
        QStringLiteral("/System/Volumes/xarts"),
        QStringLiteral("/System/Volumes/iSCPreboot"),
        QStringLiteral("/System/Volumes/Hardware"),
    };

    return std::ranges::any_of(prefixes, [&path](const QString &prefix) {
        return path == prefix || path.startsWith(prefix + QLatin1Char('/'));
    });
}

} // namespace

bool isPseudoFilesystem(const QString &fsType)
{
    return pseudoTypes().contains(fsType);
}

bool MountPoint::isInteresting() const
{
    if (isPseudo || mountPoint.isEmpty()) {
        return false;
    }
    if (isPlumbingPath(mountPoint)) {
        return false;
    }
    // The root is where every panel starts; listing it as a "device" is noise.
    return mountPoint != QLatin1String("/");
}

QString decodeMountInfoPath(const QString &field)
{
    if (!field.contains(QLatin1Char('\\'))) {
        return field;
    }

    QString result;
    result.reserve(field.size());

    for (qsizetype i = 0; i < field.size(); ++i) {
        // Exactly three octal digits, which is what the kernel writes. A
        // backslash not followed by three digits is a literal backslash — a
        // legal character in a filename, and one that would otherwise eat the
        // next three.
        if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
            bool ok = false;
            const int value = QStringView(field).mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                result.append(QChar(value));
                i += 3;
                continue;
            }
        }
        result.append(field.at(i));
    }
    return result;
}

QList<MountPoint> parseMountInfo(const QString &contents)
{
    QList<MountPoint> mounts;

    const QStringList lines = contents.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // The format, from proc(5):
        //
        //   36 35 98:0 /mnt1 /mnt2 rw,noatime - ext3 /dev/root rw,errors=continue
        //   0  1  2    3     4     5          6 7    8         9
        //
        // Fields 6 onwards are *optional* and variable in number, terminated by
        // a literal "-". Splitting on whitespace and indexing from the end is
        // therefore wrong too, because the last field is a comma-separated
        // option list that can itself be empty. The separator is the only
        // reliable landmark.
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const qsizetype separator = fields.indexOf(QStringLiteral("-"));

        if (separator < 5 || separator + 2 >= fields.size()) {
            continue;
        }

        MountPoint mount;
        mount.mountPoint = decodeMountInfoPath(fields.at(4));
        mount.readOnly = fields.at(5).split(QLatin1Char(',')).contains(QStringLiteral("ro"));
        mount.fsType = fields.at(separator + 1);
        mount.device = decodeMountInfoPath(fields.at(separator + 2));
        mount.isPseudo = isPseudoFilesystem(mount.fsType);

        mounts.append(mount);
    }

    return mounts;
}

} // namespace pf::platform
