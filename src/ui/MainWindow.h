#pragma once

#include <QMainWindow>

class QLabel;
class QSplitter;

namespace pf::ui {

class FilePanel;
class PanelStrip;
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

    /// Shows the pending chord prefix, e.g. `g-` (§6.2 step 3).
    void showPendingKeys(const QString &text);

    void toggleFooter();
    void toggleSidebar();

Q_SIGNALS:
    /// Emitted once, after the window's first paint completes. Drives
    /// --quit-after-paint and the startup measurements of §3.4, which are taken
    /// from a real paintEvent rather than from show() returning.
    void firstPaintCompleted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateFooter();
    void connectPanel(FilePanel *panel);

    QSplitter *m_splitter = nullptr;
    Sidebar *m_sidebar = nullptr;
    PanelStrip *m_strip = nullptr;
    QLabel *m_footer = nullptr;
    QLabel *m_pending = nullptr;

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
