#include "ui/MainWindow.h"

#include "core/Format.h"
#include "core/StartupTrace.h"
#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "ui/FilePanel.h"
#include "ui/ThemePalette.h"

#include <QLabel>
#include <QListView>
#include <QTimer>
#include <QVBoxLayout>

namespace pf::ui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Panefile"));
    resize(1200, 720);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_panel = new FilePanel(central);
    m_panel->setActive(true);
    layout->addWidget(m_panel, 1);

    m_footer = new QLabel(central);
    m_footer->setObjectName(QStringLiteral("footer"));
    m_footer->setTextFormat(Qt::PlainText);
    layout->addWidget(m_footer);

    setCentralWidget(central);

    connect(m_panel, &FilePanel::cursorChanged, this, [this](const QString &) { updateFooter(); });
    connect(m_panel, &FilePanel::statusMessage, this, &MainWindow::showStatusMessage);
    connect(m_panel, &FilePanel::pathChanged, this, [this](const QString &path) {
        setWindowTitle(QStringLiteral("%1 — Panefile").arg(path));
    });

    m_panel->view()->setFocus();
}

MainWindow::~MainWindow() = default;

FilePanel *MainWindow::activePanel() const
{
    return m_panel;
}

void MainWindow::showStatusMessage(const QString &message)
{
    m_footer->setText(message);

    // Transient: the message replaces the cursor metadata for a few seconds and
    // then the footer goes back to describing what the cursor is on. A message
    // that stays forever stops being read.
    QTimer::singleShot(4000, this, [this, message] {
        if (m_footer->text() == message) {
            updateFooter();
        }
    });
}

void MainWindow::updateFooter()
{
    const QModelIndex current = m_panel->view()->currentIndex();
    if (!current.isValid()) {
        m_footer->clear();
        return;
    }

    const QVariant value = current.data(DirectoryModel::EntryRole);
    if (!value.canConvert<FileEntry>()) {
        m_footer->clear();
        return;
    }
    const auto entry = value.value<FileEntry>();

    if (entry.statFailed) {
        m_footer->setText(tr("%1 — metadata unavailable").arg(entry.name));
        return;
    }

    // §5.1: permissions, owner, size, mtime. Owner resolution is deliberately
    // left as numeric ids for now — getpwuid() hits NSS, which on a machine
    // with a network directory service can block for seconds.
    QString text = QStringLiteral("%1  %2:%3  %4  %5")
                       .arg(formatPermissions(entry.mode))
                       .arg(entry.uid)
                       .arg(entry.gid)
                       .arg(formatSize(entry.size), formatFullTime(entry.modified));

    if (entry.isSymlink && !entry.linkTarget.isEmpty()) {
        text += QStringLiteral("  →  %1").arg(entry.linkTarget);
    }

    m_footer->setText(text);
}

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
