#pragma once

#include <QMainWindow>

namespace pf::ui {

/// The single top-level window: sidebar, panel strip and status furniture
/// (§5.1). Panels replace tabs, and Quick Look is an overlay rather than a
/// separate window, so this is the only QMainWindow the application creates.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

Q_SIGNALS:
    /// Emitted once, after the window's first paint completes. Drives
    /// --quit-after-paint and the ScanFirstBatch-relative startup measurements
    /// of §3.4, which are taken from a real paintEvent rather than from show()
    /// returning.
    void firstPaintCompleted();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_firstPaintDone = false;
};

} // namespace pf::ui
