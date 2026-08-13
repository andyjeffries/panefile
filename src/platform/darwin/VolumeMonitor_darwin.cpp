// The macOS half of §7.11: DiskArbitration.
//
// DiskArbitration answers the same questions udisks2 does, through a different
// shape. Where udisks2 hands over a whole object graph and signals interface
// changes, DiskArbitration registers per-event callbacks against a session
// attached to a run loop, and volumes are discovered by walking the mount table
// and asking about each one.
//
// Deliberately plain C++ against CoreFoundation rather than Objective-C++
// against Foundation. Reaching for NSString and NSURL would be marginally
// tidier to read and would add Foundation and libobjc to the binary's load-time
// dependencies, which is exactly what §3.4's dependency guard exists to catch —
// and CFStringRef converts to QString in one call anyway.

#include "core/Logging.h"
#include "platform/MountTable.h"
#include "platform/VolumeMonitor.h"

#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>

namespace pf::platform {
namespace {

/// Reads one CFBoolean out of a description dictionary.
bool boolValue(CFDictionaryRef description, CFStringRef key)
{
    const auto *const value = static_cast<CFBooleanRef>(CFDictionaryGetValue(description, key));
    return value != nullptr && CFBooleanGetValue(value) != 0;
}

QString stringValue(CFDictionaryRef description, CFStringRef key)
{
    const auto *const value = static_cast<CFStringRef>(CFDictionaryGetValue(description, key));
    if (value == nullptr) {
        return {};
    }
    return QString::fromCFString(value);
}

quint64 numberValue(CFDictionaryRef description, CFStringRef key)
{
    const auto *const value = static_cast<CFNumberRef>(CFDictionaryGetValue(description, key));
    if (value == nullptr) {
        return 0;
    }
    long long result = 0;
    CFNumberGetValue(value, kCFNumberLongLongType, &result);
    return static_cast<quint64>(result);
}

QString pathValue(CFDictionaryRef description, CFStringRef key)
{
    const auto *const url = static_cast<CFURLRef>(CFDictionaryGetValue(description, key));
    if (url == nullptr) {
        return {};
    }

    CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    if (path == nullptr) {
        return {};
    }

    const QString result = QString::fromCFString(path);
    CFRelease(path);
    return result;
}

} // namespace

/// DiskArbitration (§7.11).
class DiskArbitrationMonitor : public VolumeMonitor
{
public:
    explicit DiskArbitrationMonitor(QObject *parent) : VolumeMonitor(parent) {}

    ~DiskArbitrationMonitor() override
    {
        if (m_session != nullptr) {
            DASessionUnscheduleFromRunLoop(m_session, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
            CFRelease(m_session);
        }
    }

    bool isAvailable() const override { return true; }

    void start() override
    {
        if (m_started) {
            return;
        }
        m_started = true;

        // §3.4: nothing before this point. Creating the session is cheap, but
        // "cheap" at startup is still not free, and a user who never opens the
        // Devices section never pays for it.
        m_session = DASessionCreate(kCFAllocatorDefault);
        if (m_session == nullptr) {
            qCWarning(pfFs) << "could not create a DiskArbitration session";
            return;
        }

        // The main run loop is Qt's own on macOS, so callbacks arrive on the
        // GUI thread and nothing has to be marshalled.
        DASessionScheduleWithRunLoop(m_session, CFRunLoopGetMain(), kCFRunLoopDefaultMode);

        // Hotplug, the DiskArbitration spelling of udisks2's
        // InterfacesAdded/InterfacesRemoved.
        DARegisterDiskAppearedCallback(m_session, nullptr, &DiskArbitrationMonitor::onDiskChanged,
                                       this);
        DARegisterDiskDisappearedCallback(m_session, nullptr,
                                          &DiskArbitrationMonitor::onDiskChanged, this);
        DARegisterDiskDescriptionChangedCallback(
            m_session, nullptr, nullptr, &DiskArbitrationMonitor::onDescriptionChanged, this);

        refresh();
    }

    QList<Volume> volumes() const override { return m_volumes; }

    void mount(const QString &id) override
    {
        // A volume DiskArbitration knows about but has not mounted is one macOS
        // chose not to mount — an unrecognised filesystem, usually. DAMount
        // exists but needs a mount point to be supplied and a helper to create
        // it; until that is worth doing, saying so plainly beats a silent
        // no-op.
        Q_EMIT operationFailed(id, tr("macOS mounts removable volumes automatically"));
    }

    void unmount(const QString &id) override
    {
        DADiskRef disk = diskFor(id);
        if (disk == nullptr) {
            Q_EMIT operationFailed(id, tr("No such volume"));
            return;
        }

        // Asynchronous, as §7.11 requires of the Linux side for the same
        // reason: unmounting can block on flushing gigabytes of dirty pages.
        DADiskUnmount(disk, kDADiskUnmountOptionDefault, &DiskArbitrationMonitor::onUnmountDone,
                      this);
        CFRelease(disk);
    }

private:
    DADiskRef diskFor(const QString &id) const
    {
        if (m_session == nullptr) {
            return nullptr;
        }
        return DADiskCreateFromBSDName(kCFAllocatorDefault, m_session, id.toUtf8().constData());
    }

    static void onDiskChanged(DADiskRef /*disk*/, void *context)
    {
        static_cast<DiskArbitrationMonitor *>(context)->scheduleRefresh();
    }

    static void onDescriptionChanged(DADiskRef /*disk*/, CFArrayRef /*keys*/, void *context)
    {
        static_cast<DiskArbitrationMonitor *>(context)->scheduleRefresh();
    }

    static void onUnmountDone(DADiskRef disk, DADissenterRef dissenter, void *context)
    {
        auto *self = static_cast<DiskArbitrationMonitor *>(context);
        const QString id = QString::fromUtf8(DADiskGetBSDName(disk));

        if (dissenter != nullptr) {
            // The dissenter's status is the reason something refused — usually
            // an open file on the volume, which is exactly what the user needs
            // to be told.
            Q_EMIT self->operationFailed(
                id, tr("The volume is in use (0x%1)").arg(DADissenterGetStatus(dissenter), 0, 16));
            return;
        }

        Q_EMIT self->unmounted(id);
        self->scheduleRefresh();
    }

    /// Coalesces the burst of callbacks that arrives when one disk with four
    /// partitions is plugged in: without it the sidebar rebuilds four times.
    void scheduleRefresh()
    {
        if (m_refreshQueued) {
            return;
        }
        m_refreshQueued = true;

        QTimer::singleShot(50, this, [this] {
            m_refreshQueued = false;
            refresh();
        });
    }

    void refresh()
    {
        QList<Volume> volumes;

        // DiskArbitration has no "enumerate everything" call, so the mount
        // table is the enumeration and DiskArbitration supplies the detail.
        // Unmounted volumes are therefore invisible here — which matches what
        // macOS does anyway, since it mounts what it recognises on sight.
        for (const MountPoint &mount : currentMounts()) {
            if (!mount.isInteresting()) {
                continue;
            }

            const QString bsdName = QFileInfo(mount.device).fileName();
            DADiskRef disk = diskFor(bsdName);
            if (disk == nullptr) {
                continue;
            }

            CFDictionaryRef description = DADiskCopyDescription(disk);
            if (description == nullptr) {
                CFRelease(disk);
                continue;
            }

            const bool internalDrive = boolValue(description, kDADiskDescriptionDeviceInternalKey);
            const bool ejectable = boolValue(description, kDADiskDescriptionMediaEjectableKey);
            const bool removable = boolValue(description, kDADiskDescriptionMediaRemovableKey);

            Volume volume;
            volume.id = bsdName;
            volume.device = mount.device;
            volume.mountPoint = pathValue(description, kDADiskDescriptionVolumePathKey);
            if (volume.mountPoint.isEmpty()) {
                volume.mountPoint = mount.mountPoint;
            }
            volume.name = stringValue(description, kDADiskDescriptionVolumeNameKey);
            if (volume.name.isEmpty()) {
                volume.name = QFileInfo(volume.mountPoint).fileName();
            }
            volume.size = numberValue(description, kDADiskDescriptionMediaSizeKey);
            volume.isMounted = true;
            volume.isRemovable = ejectable || removable || !internalDrive;

            // The volume the system booted from is not something to offer to
            // unmount, whatever DiskArbitration says about its media.
            volume.canUnmount = volume.mountPoint != QLatin1String("/");

            volumes.append(volume);

            CFRelease(description);
            CFRelease(disk);
        }

        if (volumes == m_volumes) {
            return;
        }
        m_volumes = volumes;
        Q_EMIT volumesChanged();
    }

    DASessionRef m_session = nullptr;
    QList<Volume> m_volumes;
    bool m_started = false;
    bool m_refreshQueued = false;
};

std::unique_ptr<VolumeMonitor> VolumeMonitor::create(QObject *parent)
{
    return std::make_unique<DiskArbitrationMonitor>(parent);
}

} // namespace pf::platform
