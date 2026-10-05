#pragma once

#include "platform/VolumeMonitor.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include <memory>

class QFileSystemWatcher;
class QListWidget;
class QListWidgetItem;
class QTimer;

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

    /// Built-in places — Home, the XDG folders, the wastebasket — the user has
    /// removed from the sidebar, by path. Pinned folders are not in this list;
    /// removing one unpins it.
    QStringList hiddenPlaces() const;
    void setHiddenPlaces(const QStringList &paths);

    /// Removes the place a row stands for: unpins a pinned folder, hides a
    /// built-in one. Devices cannot be removed; they come and go by
    /// themselves. Returns false when there was nothing to remove.
    bool removePlace(const QString &path);

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

    /// The same, for a volume by id: the eject button on its row.
    void unmountVolume(const QString &volumeId);

    /// Re-applies everything the sidebar takes from the theme: the list's
    /// palette and every row's icon and text colour.
    ///
    /// Needed because neither comes from the stylesheet. The palette roles are
    /// the ones the item view's style reads directly, and the icons are tinted
    /// pixmaps, which QSS cannot recolour — so both were fixed at whatever
    /// theme was current when the sidebar was built. Call after
    /// ui::setCurrentPalette() on a theme change or hot reload.
    void refreshTheme();

    /// Drops the current row when the list loses focus. Public because QObject
    /// declares it so.
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    /// The user chose a place. The panel controller decides which panel it
    /// opens in; the sidebar deliberately does not know.
    void placeActivated(const QString &path);

    /// The menu button was pressed; the menu belongs at `globalPosition`.
    /// Built by the application, which owns the actions it lists.
    void menuRequested(const QPoint &globalPosition);

    void pinnedPathsChanged();

    /// A built-in place was hidden, or the hidden ones were restored.
    void hiddenPlacesChanged();

    void statusMessage(const QString &message);

private:
    void addHeading(const QString &title);
    /// A hairline between groups of places, inset like the rows.
    void addDivider();
    /// Adds a place unless it is hidden or does not exist. `removable` is false
    /// only for rows the user cannot take out (none, today, but devices go
    /// through addDevices()).
    QListWidgetItem *addPlace(const QString &title, const QString &path, const QString &iconName,
                              bool pinned = false);
    void addDevices();

    /// The palette roles the list's style reads directly; see refreshTheme().
    void applyPalette();

    /// Sets a row's icon and text colour from the current theme and the row's
    /// stored icon name and tone, so refreshTheme() can redo it.
    static void applyItemTheme(QListWidgetItem *item);

    /// The symbolic icon for a path: the matching place's glyph when the path
    /// is home or an XDG user directory, otherwise a folder.
    static QString iconNameForPath(const QString &path);

    /// Opens the place or mounts the device a row stands for. Tolerates being
    /// called twice for one gesture: a double click delivers both itemClicked
    /// and itemActivated.
    void openItem(QListWidgetItem *item);

    /// Drops both the current row and the selection, so no place is left
    /// looking like a state.
    void clearHighlight();

    /// The wastebasket's item count and size, worked out on a worker thread —
    /// a trash can hold a great deal — and written into its row when done.
    void refreshTrashSummary();
    void applyTrashSummary();

    /// Drag in to pin a folder, drag a place out to remove it.
    bool handleViewportEvent(QEvent *event);
    void startPlaceDrag(QListWidgetItem *item);
    void setDropHighlight(bool on);

    void showContextMenu(const QPoint &position);

    QListWidget *m_list = nullptr;
    QStringList m_pinned;
    QStringList m_hidden;

    /// "3 · 1.2 GB", or empty for an empty wastebasket.
    QString m_trashSummary;
    QFileSystemWatcher *m_trashWatcher = nullptr;
    QTimer *m_trashSummaryTimer = nullptr;
    int m_trashSummaryGeneration = 0;

    /// A press on a place that may turn into a drag out of the sidebar.
    QPoint m_dragStart;
    QListWidgetItem *m_dragCandidate = nullptr;

    /// Set when a place being dragged leaves the sidebar, so a drag that ends
    /// back inside it — or never left — does not remove anything.
    bool m_dragLeftSidebar = false;

    /// §3.4: null until startWatchingDevices() is called.
    std::unique_ptr<platform::VolumeMonitor> m_volumes;

    /// Volumes with a mount already requested, so one gesture cannot ask twice.
    QSet<QString> m_mounting;

    bool m_populated = false;
};

} // namespace pf::ui
