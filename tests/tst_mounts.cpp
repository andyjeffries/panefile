#include "platform/MountTable.h"
#include "platform/VolumeMonitor.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

using namespace pf::platform;

namespace {

/// Real `/proc/self/mountinfo` lines, kept verbatim. The format's awkward parts
/// — variable-length optional fields, the `-` separator, octal escapes — are
/// only worth testing against output the kernel actually produces.
const char *const kMountInfoFixture = R"(
21 27 0:20 / /proc rw,nosuid,nodev,noexec,relatime shared:5 - proc proc rw
22 27 0:21 / /sys rw,nosuid,nodev,noexec,relatime shared:6 - sysfs sysfs rw
27 1 254:1 / / rw,relatime shared:1 - ext4 /dev/vda1 rw,errors=remount-ro
36 27 0:32 / /run/user/1000 rw,nosuid,nodev,relatime shared:20 - tmpfs tmpfs rw,size=1608836k
48 27 8:17 / /media/andy/My\040Backup rw,nosuid,nodev,relatime shared:31 - ext4 /dev/sdb1 rw
52 27 0:52 / /mnt/nas ro,relatime - nfs4 server:/export ro,vers=4.2
60 27 8:33 /subdir /mnt/bind rw,relatime shared:40 master:2 propagate_from:1 - xfs /dev/sdc1 rw
)";

} // namespace

/// §7.11's mount enumeration.
class TestMounts : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ============================================================== parsing

    void parsesTheBasicFields()
    {
        const QList<MountPoint> mounts = parseMountInfo(QString::fromLatin1(kMountInfoFixture));

        QCOMPARE(mounts.size(), 7);

        const MountPoint &root = mounts.at(2);
        QCOMPARE(root.mountPoint, QStringLiteral("/"));
        QCOMPARE(root.device, QStringLiteral("/dev/vda1"));
        QCOMPARE(root.fsType, QStringLiteral("ext4"));
        QCOMPARE(root.readOnly, false);
    }

    /// The separator is the only reliable landmark: fields 6 onwards are
    /// optional and variable in number, so indexing from either end is wrong.
    void handlesVariableOptionalFields()
    {
        const QList<MountPoint> mounts = parseMountInfo(QString::fromLatin1(kMountInfoFixture));

        // No optional fields at all before the `-`.
        const MountPoint &nfs = mounts.at(5);
        QCOMPARE(nfs.mountPoint, QStringLiteral("/mnt/nas"));
        QCOMPARE(nfs.fsType, QStringLiteral("nfs4"));
        QCOMPARE(nfs.device, QStringLiteral("server:/export"));
        QCOMPARE(nfs.readOnly, true);

        // Three of them.
        const MountPoint &bind = mounts.at(6);
        QCOMPARE(bind.mountPoint, QStringLiteral("/mnt/bind"));
        QCOMPARE(bind.fsType, QStringLiteral("xfs"));
        QCOMPARE(bind.device, QStringLiteral("/dev/sdc1"));
    }

    /// A space in a mount point is written `\040`. Without decoding it, the
    /// mount parses as two fields and everything after it shifts.
    void decodesOctalEscapes()
    {
        QCOMPARE(decodeMountInfoPath(QStringLiteral("/media/andy/My\\040Backup")),
                 QStringLiteral("/media/andy/My Backup"));
        QCOMPARE(decodeMountInfoPath(QStringLiteral("/plain/path")), QStringLiteral("/plain/path"));

        // A backslash not followed by three octal digits is a literal
        // backslash — a legal filename character that must not eat what follows.
        QCOMPARE(decodeMountInfoPath(QStringLiteral("/odd\\xyz")), QStringLiteral("/odd\\xyz"));

        const QList<MountPoint> mounts = parseMountInfo(QString::fromLatin1(kMountInfoFixture));
        QCOMPARE(mounts.at(4).mountPoint, QStringLiteral("/media/andy/My Backup"));
    }

    void ignoresMalformedLines()
    {
        QVERIFY(parseMountInfo(QStringLiteral("nonsense")).isEmpty());
        QVERIFY(parseMountInfo(QStringLiteral("21 27 0:20 / /proc rw")).isEmpty());
        QVERIFY(parseMountInfo(QString()).isEmpty());
    }

    // ============================================================ filtering

    void recognisesPseudoFilesystems()
    {
        QVERIFY(isPseudoFilesystem(QStringLiteral("proc")));
        QVERIFY(isPseudoFilesystem(QStringLiteral("sysfs")));
        QVERIFY(isPseudoFilesystem(QStringLiteral("cgroup2")));
        QVERIFY(isPseudoFilesystem(QStringLiteral("devfs")));

        QVERIFY(!isPseudoFilesystem(QStringLiteral("ext4")));
        QVERIFY(!isPseudoFilesystem(QStringLiteral("apfs")));
        QVERIFY(!isPseudoFilesystem(QStringLiteral("nfs4")));

        // tmpfs is deliberately not pseudo: /tmp and /dev/shm are real places.
        QVERIFY(!isPseudoFilesystem(QStringLiteral("tmpfs")));
    }

    /// What survives into the sidebar: real filesystems that are not plumbing
    /// and not the root every panel already starts from.
    void picksOutWhatIsWorthShowing()
    {
        const QList<MountPoint> mounts = parseMountInfo(QString::fromLatin1(kMountInfoFixture));

        QStringList interesting;
        for (const MountPoint &mount : mounts) {
            if (mount.isInteresting()) {
                interesting << mount.mountPoint;
            }
        }

        QCOMPARE(interesting,
                 QStringList({QStringLiteral("/media/andy/My Backup"), QStringLiteral("/mnt/nas"),
                              QStringLiteral("/mnt/bind")}));
    }

    // ====================================================== live enumeration

    /// Whatever platform this runs on, the machine has a root filesystem and
    /// currentMounts() has to find it. A parser that works on fixtures and
    /// returns nothing in the field would pass every test above.
    void enumeratesTheRealMountTable()
    {
        const QList<MountPoint> mounts = currentMounts();
        QVERIFY(!mounts.isEmpty());

        bool foundRoot = false;
        for (const MountPoint &mount : mounts) {
            if (mount.mountPoint == QLatin1String("/")) {
                foundRoot = true;
                QVERIFY(!mount.fsType.isEmpty());
            }
        }
        QVERIFY(foundRoot);
    }

    // ======================================================= volume monitor

    /// §3.4: "Never open a bus connection at startup." Constructing the monitor
    /// must therefore do nothing at all until start() is called.
    void constructionDoesNotConnect()
    {
        const std::unique_ptr<VolumeMonitor> monitor = VolumeMonitor::create();
        QVERIFY(monitor != nullptr);
        QVERIFY(monitor->volumes().isEmpty());
    }

    /// A smoke test, not an assertion about hardware: what it checks is that
    /// starting the monitor does not crash and that anything it does report is
    /// internally coherent. CI machines have no removable media.
    void startProducesCoherentVolumes()
    {
        const std::unique_ptr<VolumeMonitor> monitor = VolumeMonitor::create();
        if (!monitor->isAvailable()) {
            QSKIP("no volume service on this machine");
        }

        monitor->start();
        QTest::qWait(500);

        for (const Volume &volume : monitor->volumes()) {
            QVERIFY(!volume.id.isEmpty());
            QVERIFY(!volume.name.isEmpty());
            QCOMPARE(volume.isMounted, !volume.mountPoint.isEmpty());
            // The volume the system runs from is never offered for unmounting.
            if (volume.mountPoint == QLatin1String("/")) {
                QVERIFY(!volume.canUnmount);
            }
        }
    }
};

QTEST_MAIN(TestMounts)
#include "tst_mounts.moc"
