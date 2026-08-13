#pragma once

#include "platform/WatchEvent.h"

#include <QObject>
#include <QString>

#include <memory>

namespace pf::platform {

/// Watches one directory and reports raw events (§7.3).
///
/// The interface is deliberately thin. Everything §7.3 specifies about
/// behaviour — the debounce, the coalescing, the rescan threshold, walking up
/// when the directory disappears — lives above this in fs::WatchCoalescer,
/// tested on both platforms. What remains here is the syscall pump: inotify on
/// Linux, FSEvents on macOS, under a hundred lines each.
///
/// §7.3 says to wrap inotify directly rather than use QFileSystemWatcher, and
/// its reasons are all about control: coalescing bursts, applying targeted
/// updates rather than rescanning, and distinguishing a directory being deleted
/// from its contents changing. QFileSystemWatcher offers none of those, and on
/// Linux it silently switches between inotify and a polling engine depending on
/// how many watches are open.
class WatchBackend : public QObject
{
    Q_OBJECT

public:
    ~WatchBackend() override;

    /// Creates the backend for this platform.
    static std::unique_ptr<WatchBackend> create(QObject *parent = nullptr);

    /// Starts watching a directory, replacing whatever was being watched.
    /// Returns false when the directory cannot be watched at all.
    virtual bool watch(const QString &path) = 0;

    virtual void stop() = 0;

    virtual QString watchedPath() const = 0;

    /// True when this platform has a real implementation. False means events
    /// will never arrive, and the caller should say so rather than silently
    /// showing a listing that never updates.
    virtual bool isSupported() const = 0;

Q_SIGNALS:
    /// Emitted on the object's own thread for each raw event.
    /// Named rawEvent rather than event: QObject already has an event()
    /// virtual, and a signal that hides it is a trap for anyone holding a
    /// base pointer.
    void rawEvent(const pf::platform::WatchEvent &event);

protected:
    explicit WatchBackend(QObject *parent = nullptr);
};

} // namespace pf::platform
