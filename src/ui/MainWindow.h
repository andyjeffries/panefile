#pragma once

#include "ui/quicklook/QuickLookDock.h"

#include <QMainWindow>

class QLabel;
class QSplitter;

namespace pf::ui {

class FilePanel;
class PanelStrip;
class QuickLookOverlay;
class Sidebar;

/// The single top-level window: sidebar, panel strip and status furniture
/// (§5.1). Panels replace tabs, and Quick Look is an overlay rather than a
/// separate window, so this is the only QMainWindow the application creates.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    PanelStrip *panelStrip() const;
    Sidebar *sidebar() const;

    /// The panel the user is working in.
    FilePanel *activePanel() const;

    /// Shows a transient message in the footer.
    void showStatusMessage(const QString &message);

    /// The "n selected" field in the status bar. Empty text hides it.
    void setSelectionCount(int count);

    /// The window title for a directory: its name, as macOS titles are, rather
    /// than an absolute path.
    static QString titleForPath(const QString &path);

    /// Shows the pending chord prefix, e.g. `g-` (§6.2 step 3).
    void showPendingKeys(const QString &text);

    void toggleFooter();
    void toggleSidebar();

    /// Adopts the process bar and shows it. Called when the first job starts;
    /// §3.4 keeps the widget from existing until then.
    void showProcessBar(QWidget *processBar);
    void hideProcessBar();

    /// Adopts the Quick Look pane. Called on its first use; §3.4 keeps the
    /// widget — and every renderer behind it — from existing until then.
    void setQuickLookWidget(QWidget *view);
    QWidget *quickLookWidget() const;

    /// Places the pane for `dock` (§7.6). Idempotent, and safe to call while it
    /// is hidden: the placement is what changes, not the visibility.
    ///
    /// `floatPercent` and `dockPercent` are `quicklook.float_size_percent` and
    /// `quicklook.dock_size_percent`.
    void setQuickLookDock(QuickLookDock dock, int floatPercent, int dockPercent);
    QuickLookDock quickLookDock() const;

    void setQuickLookVisible(bool visible);
    bool isQuickLookVisible() const;

Q_SIGNALS:
    /// Emitted once, after the window's first paint completes. Drives
    /// --quit-after-paint and the startup measurements of §3.4, which are taken
    /// from a real paintEvent rather than from show() returning.
    void firstPaintCompleted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

Q_SIGNALS:
    /// The pane's close affordance, or a click on the float backdrop.
    void quickLookDismissed();

private:
    void updateFooter();
    void connectPanel(FilePanel *panel);

    /// Detaches the pane from whichever container currently holds it, without
    /// destroying it. Every dock change goes through this first, so no mode has
    /// to know which mode preceded it.
    void detachQuickLook();

    /// Lazily built, because the float backdrop is only needed by two of the
    /// six modes.
    QuickLookOverlay *quickLookOverlay();

    QSplitter *m_splitter = nullptr;

    /// Vertical, holding the horizontal splitter and — in bottom dock mode —
    /// the Quick Look pane. §7.6 calls the docked modes "user-resizable", which
    /// a splitter gives and a layout does not.
    QSplitter *m_contentSplitter = nullptr;
    Sidebar *m_sidebar = nullptr;
    PanelStrip *m_strip = nullptr;
    QLabel *m_footer = nullptr;
    QLabel *m_selectionCount = nullptr;
    QLabel *m_pending = nullptr;
    QWidget *m_processBar = nullptr;
    QWidget *m_footerRow = nullptr;

    QWidget *m_quickLook = nullptr;
    QuickLookOverlay *m_quickLookOverlay = nullptr;
    QuickLookDock m_quickLookDock = QuickLookDock::Float;
    int m_quickLookFloatPercent = 70;
    int m_quickLookDockPercent = 35;
    bool m_quickLookVisible = false;

    /// Connections to whichever panel currently has focus. Kept so they can be
    /// dropped when focus moves: Qt::UniqueConnection cannot be used with
    /// lambdas, so without this every focus change would stack another
    /// connection and the footer would be updated N times per cursor move.
    QMetaObject::Connection m_panelCursorConnection;
    QMetaObject::Connection m_panelPathConnection;

    bool m_firstPaintDone = false;
    bool m_sidebarHiddenByWidth = false;
};

} // namespace pf::ui
