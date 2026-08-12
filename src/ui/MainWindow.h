#pragma once

#include <QMainWindow>

class QLabel;

namespace pf::ui {

class FilePanel;

/// The single top-level window: sidebar, panel strip and status furniture
/// (§5.1). Panels replace tabs, and Quick Look is an overlay rather than a
/// separate window, so this is the only QMainWindow the application creates.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// The panel the user is working in. M2 turns this into a strip of them
    /// managed by a FocusManager; until then there is exactly one.
    FilePanel *activePanel() const;

    /// Shows a transient message in the footer.
    void showStatusMessage(const QString &message);

Q_SIGNALS:
    /// Emitted once, after the window's first paint completes. Drives
    /// --quit-after-paint and the startup measurements of §3.4, which are taken
    /// from a real paintEvent rather than from show() returning.
    void firstPaintCompleted();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void updateFooter();

    FilePanel *m_panel = nullptr;
    QLabel *m_footer = nullptr;
    bool m_firstPaintDone = false;
};

} // namespace pf::ui
