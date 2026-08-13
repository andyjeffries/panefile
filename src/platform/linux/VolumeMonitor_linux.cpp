// The Linux half of §7.11: udisks2 over the system bus.

#include "core/Logging.h"
#include "platform/VolumeMonitor.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QMap>
#include <QVariantMap>

namespace pf::platform {
namespace {

const char *const kService = "org.freedesktop.UDisks2";
const char *const kObjectManagerPath = "/org/freedesktop/UDisks2";
const char *const kObjectManager = "org.freedesktop.DBus.ObjectManager";
const char *const kBlockInterface = "org.freedesktop.UDisks2.Block";
const char *const kFilesystemInterface = "org.freedesktop.UDisks2.Filesystem";
const char *const kDriveInterface = "org.freedesktop.UDisks2.Drive";

/// udisks2's `GetManagedObjects` return type: object path → interface →
/// property → value. Registered with the meta-type system so QDBusReply can
/// demarshal it; without this every reply is an empty map and nothing works.
using InterfaceProperties = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, InterfaceProperties>;

/// udisks2 hands out paths as NUL-terminated byte arrays, not strings, because
/// a mount point is bytes on Linux. The trailing NUL has to go or every
/// comparison against a QString fails for no visible reason.
QString decodeByteString(const QByteArray &bytes)
{
    QByteArray trimmed = bytes;
    while (trimmed.endsWith('\0')) {
        trimmed.chop(1);
    }
    return QFile::decodeName(trimmed);
}

QString firstMountPoint(const QVariantMap &filesystem)
{
    const QDBusArgument argument =
        filesystem.value(QStringLiteral("MountPoints")).value<QDBusArgument>();
    QList<QByteArray> points;
    argument >> points;

    return points.isEmpty() ? QString() : decodeByteString(points.constFirst());
}

/// §7.11: "filtering to block devices with HintAuto or removable media".
bool isInteresting(const QVariantMap &block, const QVariantMap &drive)
{
    if (block.value(QStringLiteral("HintIgnore")).toBool()) {
        return false;
    }
    // A partition that belongs to no drive is a loop device or similar.
    if (block.value(QStringLiteral("Drive")).value<QDBusObjectPath>().path() ==
        QLatin1String("/")) {
        return false;
    }

    return block.value(QStringLiteral("HintAuto")).toBool() ||
           drive.value(QStringLiteral("Removable")).toBool() ||
           drive.value(QStringLiteral("Ejectable")).toBool();
}

} // namespace

/// udisks2 over the system bus (§7.11).
///
/// At namespace scope rather than in the anonymous namespace above, because it
/// needs Q_OBJECT: QDBusConnection::connect takes a SLOT() string, so the
/// hotplug handler has to be a real slot with meta-object machinery behind it.
class UDisksMonitor : public VolumeMonitor
{
    Q_OBJECT

public:
    explicit UDisksMonitor(QObject *parent) : VolumeMonitor(parent) {}

    bool isAvailable() const override { return QDBusConnection::systemBus().isConnected(); }

    void start() override
    {
        if (m_started) {
            return;
        }
        m_started = true;

        if (!isAvailable()) {
            qCDebug(pfFs) << "no system bus; udisks2 unavailable";
            return;
        }

        qDBusRegisterMetaType<InterfaceProperties>();
        qDBusRegisterMetaType<ManagedObjects>();

        QDBusConnection bus = QDBusConnection::systemBus();

        // §7.11: "Watch InterfacesAdded/InterfacesRemoved for hotplug."
        bus.connect(QLatin1String(kService), QLatin1String(kObjectManagerPath),
                    QLatin1String(kObjectManager), QStringLiteral("InterfacesAdded"), this,
                    SLOT(onInterfacesChanged()));
        bus.connect(QLatin1String(kService), QLatin1String(kObjectManagerPath),
                    QLatin1String(kObjectManager), QStringLiteral("InterfacesRemoved"), this,
                    SLOT(onInterfacesChanged()));

        refresh();
    }

    QList<Volume> volumes() const override { return m_volumes; }

    void mount(const QString &id) override
    {
        callAsync(id, QLatin1String(kFilesystemInterface), QStringLiteral("Mount"),
                  [this, id](const QDBusMessage &reply) {
                      const QString path = reply.arguments().isEmpty()
                                               ? QString()
                                               : reply.arguments().constFirst().toString();
                      Q_EMIT mounted(id, path);
                      refresh();
                  });
    }

    void unmount(const QString &id) override
    {
        callAsync(id, QLatin1String(kFilesystemInterface), QStringLiteral("Unmount"),
                  [this, id](const QDBusMessage &) {
                      Q_EMIT unmounted(id);
                      refresh();
                  });
    }

private:
    /// §7.11: "Never block the GUI thread on a D-Bus call — use
    /// QDBusPendingCallWatcher throughout." Mounting a slow USB stick can take
    /// seconds, and a frozen window for that long is indistinguishable from a
    /// crash.
    template<typename Handler>
    void callAsync(const QString &objectPath, const QString &interface, const QString &method,
                   Handler onSuccess)
    {
        QDBusMessage message =
            QDBusMessage::createMethodCall(QLatin1String(kService), objectPath, interface, method);
        // udisks2's Mount and Unmount both take an a{sv} of options.
        message << QVariantMap{};

        auto *watcher =
            new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, objectPath, onSuccess](QDBusPendingCallWatcher *call) {
                    const QDBusMessage reply = call->reply();
                    call->deleteLater();

                    if (reply.type() == QDBusMessage::ErrorMessage) {
                        Q_EMIT operationFailed(objectPath, reply.errorMessage());
                        return;
                    }
                    onSuccess(reply);
                });
    }

    void refresh()
    {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QLatin1String(kService), QLatin1String(kObjectManagerPath),
            QLatin1String(kObjectManager), QStringLiteral("GetManagedObjects"));

        auto *watcher =
            new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);

        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher *call) {
                    QDBusPendingReply<ManagedObjects> reply = *call;
                    call->deleteLater();

                    if (reply.isError()) {
                        qCDebug(pfFs) << "udisks2 enumeration failed" << reply.error().message();
                        return;
                    }
                    rebuild(reply.value());
                });
    }

    void rebuild(const ManagedObjects &objects)
    {
        QList<Volume> volumes;

        for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
            const InterfaceProperties &interfaces = it.value();

            const QVariantMap block = interfaces.value(QLatin1String(kBlockInterface));
            if (block.isEmpty()) {
                continue;
            }

            const QDBusObjectPath drivePath =
                block.value(QStringLiteral("Drive")).value<QDBusObjectPath>();
            const QVariantMap drive =
                objects.value(drivePath).value(QLatin1String(kDriveInterface));

            if (!isInteresting(block, drive)) {
                continue;
            }

            const QVariantMap filesystem = interfaces.value(QLatin1String(kFilesystemInterface));
            if (filesystem.isEmpty()) {
                // No filesystem interface means nothing mountable — an extended
                // partition, or a disk with no partition table.
                continue;
            }

            Volume volume;
            volume.id = it.key().path();
            volume.device = decodeByteString(block.value(QStringLiteral("Device")).toByteArray());
            volume.size = block.value(QStringLiteral("Size")).toULongLong();
            volume.mountPoint = firstMountPoint(filesystem);
            volume.isMounted = !volume.mountPoint.isEmpty();
            volume.isRemovable = drive.value(QStringLiteral("Removable")).toBool();
            volume.canUnmount = volume.isMounted;

            // The label if there is one, then the drive's model, then the
            // device node. A USB stick with no label showing "/dev/sdb1" is
            // less friendly than "SanDisk Cruzer" but far better than nothing.
            volume.name = block.value(QStringLiteral("IdLabel")).toString();
            if (volume.name.isEmpty()) {
                volume.name = drive.value(QStringLiteral("Model")).toString();
            }
            if (volume.name.isEmpty()) {
                volume.name = volume.device;
            }

            volumes.append(volume);
        }

        if (volumes == m_volumes) {
            return;
        }
        m_volumes = volumes;
        Q_EMIT volumesChanged();
    }

    QList<Volume> m_volumes;
    bool m_started = false;

private Q_SLOTS:
    /// Invoked by the two ObjectManager signals. A real slot rather than a
    /// lambda because QDBusConnection::connect takes a SLOT() string.
    void onInterfacesChanged() { refresh(); }
};

std::unique_ptr<VolumeMonitor> VolumeMonitor::create(QObject *parent)
{
    return std::make_unique<UDisksMonitor>(parent);
}

} // namespace pf::platform

#include "VolumeMonitor_linux.moc"
