#include "ui/MainWindow.h"

#include "core/Format.h"
#include "core/Logging.h"
#include "core/StartupTrace.h"
#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "ui/FilePanel.h"
#include "ui/PanelStrip.h"
#include "ui/Sidebar.h"
#include "ui/ThemePalette.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QResizeEvent>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace pf::ui {
namespace {

/// §7.1: below this total width the sidebar hides itself.
constexpr int kSidebarHideThreshold = 600;

/// How long a transient footer message stays before the footer goes back to
/// describing the cursor item. A message that never clears stops being read.
constexpr int kStatusMessageMs = 4000;

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_splitter(new QSplitter(Qt::Horizontal)), m_sidebar(new Sidebar),
      m_strip(new PanelStrip), m_footer(new QLabel), m_pending(new QLabel)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Panefile"));
    resize(1200, 720);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter->setObjectName(QStringLiteral("mainSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_strip);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({180, 1020});
    layout->addWidget(m_splitter, 1);

    // The footer row carries both the cursor metadata and the pending-chord
    // indicator, which sits at the right where it does not shift the metadata
    // around as it appears and disappears.
    auto *footerRow = new QWidget(central);
    auto *footerLayout = new QHBoxLayout(footerRow);
    footerLayout->setContentsMargins(currentPalette().panelPadding, 3,
                                     currentPalette().panelPadding, 3);
    footerLayout->setSpacing(12);

    m_footer->setObjectName(QStringLiteral("footer"));
    m_footer->setTextFormat(Qt::PlainText);
    footerLayout->addWidget(m_footer, 1);

    m_pending->setObjectName(QStringLiteral("pendingKeys"));
    m_pending->setTextFormat(Qt::PlainText);
    footerLayout->addWidget(m_pending, 0);

    layout->addWidget(footerRow);
    setCentralWidget(central);

    // The process bar is inserted above the footer when it first appears, so
    // §5.1's ordering — panels, footer, process bar — holds without the widget
    // existing before there is a job to report.
    m_footerRow = footerRow;

    connect(m_strip, &PanelStrip::focusedPanelChanged, this, [this](FilePanel *panel) {
        connectPanel(panel);
        updateFooter();
        if (panel != nullptr) {
            setWindowTitle(QStringLiteral("%1 — Panefile").arg(panel->path()));
        }
    });
    connect(m_strip, &PanelStrip::statusMessage, this, &MainWindow::showStatusMessage);
}

MainWindow::~MainWindow() = default;

PanelStrip *MainWindow::panelStrip() const
{
    return m_strip;
}

Sidebar *MainWindow::sidebar() const
{
    return m_sidebar;
}

FilePanel *MainWindow::activePanel() const
{
    return m_strip->focusedPanel();
}

void MainWindow::connectPanel(FilePanel *panel)
{
    // The footer follows the *focused* panel, so the previous panel's
    // connections go first. Qt::UniqueConnection would be the obvious way to
    // avoid duplicates and does not work with lambdas — it asserts — so the
    // connections are tracked and dropped explicitly.
    disconnect(m_panelCursorConnection);
    disconnect(m_panelPathConnection);

    if (panel == nullptr) {
        return;
    }

    m_panelCursorConnection = connect(panel, &FilePanel::cursorChanged, this,
                                      [this](const QString &) { updateFooter(); });
    m_panelPathConnection =
        connect(panel, &FilePanel::pathChanged, this, [this](const QString &path) {
            setWindowTitle(QStringLiteral("%1 — Panefile").arg(path));
        });
}

void MainWindow::showPendingKeys(const QString &text)
{
    m_pending->setText(text);
}

void MainWindow::showStatusMessage(const QString &message)
{
    m_footer->setText(message);

    QTimer::singleShot(kStatusMessageMs, this, [this, message] {
        if (m_footer->text() == message) {
            updateFooter();
        }
    });
}

void MainWindow::showProcessBar(QWidget *processBar)
{
    if (processBar == nullptr) {
        return;
    }

    if (m_processBar != processBar) {
        m_processBar = processBar;
        auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
        if (layout != nullptr) {
            layout->insertWidget(layout->indexOf(m_footerRow) + 1, m_processBar);
        }
    }
    m_processBar->show();
}

void MainWindow::hideProcessBar()
{
    if (m_processBar != nullptr) {
        m_processBar->hide();
    }
}

void MainWindow::toggleFooter()
{
    m_footer->parentWidget()->setVisible(!m_footer->parentWidget()->isVisible());
}

void MainWindow::toggleSidebar()
{
    m_sidebar->setVisible(!m_sidebar->isVisible());
    // An explicit toggle takes precedence over the width rule, or the sidebar
    // would reappear the next time the window was resized.
    m_sidebarHiddenByWidth = false;
}

void MainWindow::updateFooter()
{
    const FilePanel *panel = activePanel();
    if (panel == nullptr) {
        m_footer->clear();
        return;
    }

    const QModelIndex current = panel->view()->currentIndex();
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

    // §5.1: permissions, owner, size, mtime. Owner stays numeric for now:
    // getpwuid() goes through NSS, which on a machine with a network directory
    // service can block for seconds, and this runs on every cursor movement.
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

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    // §7.1: hide the sidebar below 600 px of total width. Tracked separately
    // from an explicit toggle so that widening the window restores a sidebar
    // the width rule hid, but not one the user chose to hide.
    const bool tooNarrow = event->size().width() < kSidebarHideThreshold;

    if (tooNarrow && m_sidebar->isVisible()) {
        m_sidebar->hide();
        m_sidebarHiddenByWidth = true;
    } else if (!tooNarrow && m_sidebarHiddenByWidth) {
        m_sidebar->show();
        m_sidebarHiddenByWidth = false;
    }
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
