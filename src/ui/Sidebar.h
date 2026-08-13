#pragma once

#include "platform/VolumeMonitor.h"

#include <QList>
#include <QString>
#include <QWidget>

#include <memory>

class QListWidget;
class QListWidgetItem;

namespace pf::ui {

/// Home, XDG user directories, pinned directories and mounted volumes (§5.1).
///
/// §3.4 requires this to be constructed empty and populated on idle: resolving
/// XDG user directories reads a config file, and enumerating mounts is worse.
/// None of it is needed to draw the first panel, so populate() is called from
/// the deferred startup queue rather than from the constructor.
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    /// Fills in the standard places. Safe to call more than once; later calls
    /// refresh rather than duplicate.
    void populate();

    /// §6.3's `pinned_directory` (`P`): pins or unpins, returning what it did
    /// so the caller can report it.
    bool togglePin(const QString &path);
    bool isPinned(const QString &path) const;

    QStringList pinnedPaths() const;
    void setPinnedPaths(const QStringList &paths);

    /// The path under the sidebar's own cursor, or empty.
    QString currentPath() const;

    /// §7.11's Devices section.
    ///
    /// §3.4: "Connect to udisks2 the first time the sidebar's Devices section
    /// becomes visible." That is what this call is — the monitor is created and
    /// started here, not at startup, and never if it is not called.
    void startWatchingDevices();

    /// The volume id under the cursor, or empty when the cursor is on an
    /// ordinary place. §6.3's `u` acts on this.
    QString currentVolumeId() const;

    /// §7.11: "`u` on a mounted device unmounts." Does nothing when the cursor
    /// is not on one.
    void unmountCurrentVolume();

Q_SIGNALS:
    /// The user chose a place. The panel controller decides which panel it
    /// opens in; the sidebar deliberately does not know.
    void placeActivated(const QString &path);

    void pinnedPathsChanged();

    void statusMessage(const QString &message);

private:
    void addHeading(const QString &title);
    void addPlace(const QString &title, const QString &path);
    void addDevices();

    QListWidget *m_list = nullptr;
    QStringList m_pinned;

    /// §3.4: null until startWatchingDevices() is called.
    std::unique_ptr<platform::VolumeMonitor> m_volumes;

    bool m_populated = false;
};

} // namespace pf::ui
