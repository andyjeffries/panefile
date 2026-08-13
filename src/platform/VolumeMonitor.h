#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace pf::platform {

/// A removable or mountable volume (§7.11).
struct Volume {
    /// Stable identity, used to match a hotplug event to a row. udisks2 object
    /// path on Linux, BSD device name on macOS.
    QString id;

    QString name;       ///< what to show: the label, or the device
    QString device;     ///< `/dev/sdb1`, `disk4s1`
    QString mountPoint; ///< empty when not mounted
    quint64 size = 0;

    bool isMounted = false;
    bool isRemovable = false;

    /// True when unmounting is something the user may ask for. The volume the
    /// system is running from is not.
    bool canUnmount = false;

    bool operator==(const Volume &other) const = default;
};

/// Watches mountable and removable volumes (§7.11).
///
/// §7.11 asks for udisks2 over the system bus on Linux and hotplug through
/// `InterfacesAdded`/`InterfacesRemoved`; macOS has DiskArbitration, which
/// answers the same questions through a different shape. One interface, one
/// implementation per platform, selected at CMake level.
///
/// Two rules from §7.11 hold on both:
///
///   * "Never block the GUI thread on a D-Bus call." Every operation here is
///     asynchronous and reports through a signal; nothing returns a result
///     synchronously, including mount() and unmount().
///
///   * §3.4: "Never open a bus connection at startup." Nothing happens until
///     start() is called, which the sidebar does when its Devices section first
///     becomes visible.
class VolumeMonitor : public QObject
{
    Q_OBJECT

public:
    /// The implementation for this platform. Never null: a build without the
    /// backing service gets one that reports nothing and says it is
    /// unavailable, so callers never branch on the platform.
    static std::unique_ptr<VolumeMonitor> create(QObject *parent = nullptr);

    explicit VolumeMonitor(QObject *parent = nullptr) : QObject(parent) {}
    ~VolumeMonitor() override = default;

    /// Whether this build and machine can enumerate volumes at all.
    virtual bool isAvailable() const = 0;

    /// Connects to the service and performs the first enumeration. Idempotent.
    /// §3.4: called on first use, never at startup.
    virtual void start() = 0;

    virtual QList<Volume> volumes() const = 0;

    /// Asks for `id` to be mounted. Reports through mounted() or
    /// operationFailed(); never blocks.
    virtual void mount(const QString &id) = 0;

    /// Asks for `id` to be unmounted.
    virtual void unmount(const QString &id) = 0;

Q_SIGNALS:
    /// The set of volumes changed — hotplug, or a mount state change.
    void volumesChanged();

    /// A mount finished, with the path it landed on, so the caller can navigate
    /// there (§7.11: "Enter mounts … and navigates").
    void mounted(const QString &id, const QString &mountPoint);

    void unmounted(const QString &id);

    /// Something failed, with a message worth showing.
    void operationFailed(const QString &id, const QString &reason);
};

} // namespace pf::platform
