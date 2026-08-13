#pragma once

#include "fs/WatchCoalescer.h"

#include <QObject>
#include <QString>

#include <memory>

namespace pf::platform {
class WatchBackend;
}

namespace pf::fs {

/// Watches one directory and reports coalesced changes (§7.3).
///
/// §7.3 asks for "One DirectoryWatcher per distinct open path (refcounted — two
/// panels on the same path share one watch)". The sharing is real: inotify
/// watches are a per-user kernel resource with a low default limit, and ten
/// panels on the same directory taking ten watches would waste nine of them.
///
/// Watchers are obtained through acquire() rather than constructed, which is
/// what makes the sharing invisible to callers.
class DirectoryWatcher : public QObject
{
    Q_OBJECT

public:
    /// A watcher for `path`, shared with anyone else watching it. The returned
    /// pointer stays valid until the last holder drops it.
    static std::shared_ptr<DirectoryWatcher> acquire(const QString &path);

    /// How many distinct paths are being watched. For tests, and for a future
    /// diagnostic.
    static int watchedPathCount();

    ~DirectoryWatcher() override;

    QString path() const;

    /// False when the directory could not be watched — it may have been removed
    /// between the scan and the watch, or the kernel may be out of watches.
    /// A panel that is not being watched still works; it just will not update
    /// by itself.
    bool isActive() const;

    void setDebounceInterval(int milliseconds);

Q_SIGNALS:
    /// A coalesced set of changes. The model applies these as targeted updates.
    void changed(const pf::fs::WatchDelta &delta);

private:
    explicit DirectoryWatcher(const QString &path);

    QString m_path;
    std::unique_ptr<platform::WatchBackend> m_backend;
    WatchCoalescer m_coalescer;
    bool m_active = false;
};

} // namespace pf::fs
