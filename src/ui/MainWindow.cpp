#include "ui/MainWindow.h"

#include "core/StartupTrace.h"

namespace pf::ui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Panefile"));
    resize(1200, 720);
}

MainWindow::~MainWindow() = default;

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);

    if (!m_firstPaintDone) {
        m_firstPaintDone = true;
        StartupTrace::mark(StartupPhase::FirstPaint);
        // Queued rather than direct: whatever is connected to this may tear the
        // window down (--quit-after-paint does), and doing that inside a
        // paintEvent is a use-after-free waiting to happen.
        QMetaObject::invokeMethod(this, &MainWindow::firstPaintCompleted, Qt::QueuedConnection);
    }
}

} // namespace pf::ui
