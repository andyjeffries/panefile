#pragma once

#include "platform/VolumeMonitor.h"
#include "ui/SidebarLayout.h"

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

/// The user's sections of places, the wastebasket and mounted volumes (§5.1).
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

    /// The user's arrangement. setLayout() never emits layoutChanged(); every
    /// mutation below does.
    void setLayout(const SidebarLayout &layout);
    const SidebarLayout &sidebarLayout() const { return m_layout; }

    /// §6.3's `pinned_directory` (`P`): adds the path to the "Pinned" section
    /// (created at the end if missing) or removes it from wherever it is,
    /// returning what it did so the caller can report it.
    bool togglePin(const QString &path);
    bool isPinned(const QString &path) const;

    /// Removes the entry a path stands for; the wastebasket is hidden instead.
    /// Devices cannot be removed; they come and go by themselves. Returns false
    /// when there was nothing to remove.
    bool removePlace(const QString &path);

    /// Removes a section and its entries (the files are untouched). Looked up by
    /// id, so a stale confirmation does nothing.
    void removeSection(const QString &id);
    void addSection(const QString &title);
    /// A new section at the end holding the entry for `path` (moved from where
    /// it was).
    void addSectionWith(const QString &title, const QString &path);
    void renameSection(const QString &id, const QString &title);

    /// Alt+Up / Alt+Down: moves the current entry within its section, or into
    /// the neighbouring one at an edge.
    void moveCurrentEntry(int delta);
    /// True while the sidebar has focus on an entry that can be moved.
    bool canMoveCurrentEntry() const;

    /// Re-adds missing built-ins (to the first section, or a new "Favourites")
    /// and shows the wastebasket again.
    void restoreDefaultPlaces();
    bool hasMissingDefaults() const;

    /// What a drag out of the sidebar carries. Public so tests can build the
    /// same payload the sidebar does.
    static QByteArray entryPayload(const QString &sectionId, int index, const QString &path);
    static QByteArray sectionPayload(const QString &sectionId);
    static QByteArray trashPayload();
    static QLatin1String placeMimeType();

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

    /// The arrangement changed; the application saves it.
    void layoutChanged();

    /// A file entry was chosen: show it in its folder rather than open it.
    void revealRequested(const QString &path);

    /// A non-empty section was dragged out or removed from the menu; the
    /// application confirms, then calls removeSection(id).
    void sectionRemovalRequested(const QString &id, const QString &title, int count);
    /// `moveEntryPath`, when set, is moved into the new section.
    void newSectionRequested(const QString &moveEntryPath);
    void renameSectionRequested(const QString &id, const QString &currentTitle);

    void statusMessage(const QString &message);

private:
    void addHeading(const QString &title, const QString &sectionId = {});
    /// A hairline between groups of places, inset like the rows.
    void addDivider();
    /// Adds a place unless it is hidden or does not exist. `removable` is false
    /// only for rows the user cannot take out (none, today, but devices go
    /// through addDevices()).
    QListWidgetItem *addPlace(const QString &title, const QString &path, const QString &iconName);
    void addPlaceholder(const QString &sectionId);
    static QString iconNameForEntry(const QString &path);
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

    /// Where a drop at `position` would land, and where to draw the line.
    struct DropTarget {
        enum Kind { None, Entry, Section } kind = None;
        int section = -1; ///< Index into the layout; -1 with Entry: no sections yet.
        int index = 0;    ///< Gap index: the item goes before the one now at it.
        int lineY = 0;    ///< Viewport y of the indicator.
    };
    DropTarget dropTargetAt(const QPoint &position, bool draggingSection) const;
    void showDropLine(int y);
    void hideDropLine();

    /// Applies a change: repopulate, notify, and keep the cursor on `keepPath`.
    void commitLayout(const QString &keepPath = {});
    void moveEntryByPath(const QString &path, int delta);
    void moveSectionBy(const QString &id, int delta);
    /// Removes a section now if it is empty, else asks the application to
    /// confirm.
    void requestSectionRemoval(const QString &id);

    void showContextMenu(const QPoint &position);

    QListWidget *m_list = nullptr;
    SidebarLayout m_layout = SidebarLayout::defaults();
    QWidget *m_dropLine = nullptr;

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
