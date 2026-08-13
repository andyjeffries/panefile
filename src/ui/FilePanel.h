#pragma once

#include "model/FileEntry.h"
#include "model/FilterSortProxy.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;

namespace pf {
class DirectoryModel;
}

namespace pf::ui {

class FileItemDelegate;
class PanelView;

/// One independent directory panel (§5.2).
///
/// Panels are peers: none is the parent of another, and each keeps its own
/// working directory, cursor, selection, history, sort order and filter. That
/// independence is the whole design, so nothing here reaches for a "current
/// panel" global or asks its parent what to display.
class FilePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FilePanel(QWidget *parent = nullptr);
    ~FilePanel() override;

    void navigateTo(const QString &path);
    QString path() const;

    /// Navigates to the parent, leaving the cursor on the directory just left.
    void goToParent();

    /// Enters the directory under the cursor, or emits fileActivated for a file.
    void activateCursorItem();

    bool goBack();
    bool goForward();

    QString cursorName() const;
    void setCursorName(const QString &name);

    /// The cursor item's absolute path, empty when there is no cursor.
    QString cursorPath() const;

    /// The cursor item's metadata. Quick Look and the footer both want it, and
    /// reaching through the panel's view into the model from outside would tie
    /// them to the proxy's role numbering.
    FileEntry cursorEntry() const;

    void moveCursor(int delta);
    void moveCursorToStart();
    void moveCursorToEnd();
    void movePage(int direction);

    /// §7.7's `thumbnails.enabled`, applied to this panel's model.
    void setThumbnailsEnabled(bool enabled);

    void setShowHidden(bool show);
    bool showHidden() const;
    void toggleShowHidden();

    void setSortKey(SortKey key);
    SortKey sortKey() const;
    void setReverseSort(bool reverse);
    bool reverseSort() const;

    void setFilterText(const QString &text);
    QString filterText() const;

    /// §7.8's `config.search.fuzzy`.
    void setFuzzyMatching(bool fuzzy);

    /// §7.8's in-panel filter: `/` opens the box, `Enter` keeps the filter and
    /// returns focus to the list, `Esc` clears it and closes.
    void openFilterBar();
    void closeFilterBar(bool keepFilter);
    bool isFilterBarOpen() const;

    /// Handles Esc in the filter box. Public because QObject declares it so.
    bool eventFilter(QObject *watched, QEvent *event) override;

    /// §6.1's Selection mode. Movement extends the selection while it is on.
    void setSelectionMode(bool on);
    bool isSelectionMode() const;
    void toggleSelectionMode();

    void toggleSelectionAt(const QString &name);
    void selectAll();
    void clearSelection();

    /// Absolute paths of the selected entries, or of the cursor item when
    /// nothing is selected.
    ///
    /// Falling back to the cursor is what makes every file operation work
    /// without a selection first — §6.3's bindings act on "the selection", and
    /// a user who has not made one means the thing under the cursor.
    QStringList selectedPaths() const;
    int selectionCount() const;

    /// True when this panel is the focused one. Drives the border and
    /// background treatment that §9 calls the single most important visual
    /// affordance in the application.
    /// Re-reads the theme after a hot reload. The delegate paints from the
    /// palette directly, so a stylesheet change alone does not reach it.
    void refreshTheme();

    void setActive(bool active);
    bool isActive() const;

    PanelView *view() const;

Q_SIGNALS:
    /// §7.12: files were dropped on this panel. The panel does not act on it —
    /// transferring files is FileOperations' business, and it is the only place
    /// that knows about conflicts and the undo stack.
    void filesDropped(const QStringList &paths, const QString &destinationDirectory,
                      Qt::DropAction action);

    void pathChanged(const QString &path);
    void cursorChanged(const QString &name);
    void selectionChanged(int count);
    void modeChanged();
    void fileActivated(const QString &absolutePath);

    /// Transient message for the footer — "nothing to go back to", and similar.
    void statusMessage(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;

    /// Draws the stylesheet's background and border. A plain QWidget subclass
    /// does not do this for itself, and §9's focused-panel border depends on it.
    void paintEvent(QPaintEvent *event) override;

private:
    void applyPalette();
    void applyHeaderElision();

    /// §7.7: queues thumbnails for the rows on screen and the overshoot either
    /// side. The proxy reorders and filters, so the visible *source* rows are
    /// not a contiguous range and have to be mapped one by one.
    void updateThumbnailWindow();

    /// Drops the filter, and the box with it. Navigation does this: a filter
    /// describes the directory you were in.
    void clearFilter();

    /// Says so when a filter is hiding everything. An empty panel otherwise
    /// looks the same whether the directory is empty, the scan failed, or a
    /// filter matched nothing.
    void updateFilterStatus();

    /// §7.3: after the watched directory disappears, move to the nearest
    /// ancestor that still exists.
    void walkUpToExistingAncestor();
    void onScanFinished(const QString &path, int count);
    void onScanFailed(const QString &path, const QString &reason);
    void updateHeader();
    void restoreCursor();
    void rememberCursor();
    void setPathInternal(const QString &path, bool pushHistory);

    // Declaration order is initialisation order, and these are constructed in
    // the member initialiser list, so it has to match it.
    DirectoryModel *m_model = nullptr;
    FilterSortProxy *m_proxy = nullptr;
    PanelView *m_view = nullptr;
    FileItemDelegate *m_delegate = nullptr;
    QLabel *m_header = nullptr;
    QLabel *m_status = nullptr;

    /// §3.4: built on the first `/`, not at startup. A user who never filters
    /// never pays for it.
    QLineEdit *m_filterBar = nullptr;

    QString m_path;
    QSet<QString> m_selection;
    QStringList m_backStack;
    QStringList m_forwardStack;

    /// The name to place the cursor on once the current scan delivers rows.
    /// Cleared when applied — a scan that finds nothing must not leave a stale
    /// target that hijacks the cursor after the next navigation.
    QString m_pendingCursorName;

    /// The header's full text before elision. The label shows an elided copy
    /// sized to whatever width the panel currently has, so the untruncated
    /// version has to be kept to re-elide on resize.
    QString m_headerText;

    /// True while the status label is showing this panel's own filter message,
    /// so that clearing it cannot discard a scan error that owns the same
    /// label.
    bool m_showingFilterStatus = false;

    bool m_active = false;
    bool m_selectionMode = false;
};

} // namespace pf::ui
