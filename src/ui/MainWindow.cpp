#include "ui/MainWindow.h"

#include "core/Format.h"
#include "core/Logging.h"
#include "core/StartupTrace.h"
#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "ui/FilePanel.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"
#include "ui/Sidebar.h"
#include "ui/ThemePalette.h"
#include "ui/quicklook/QuickLookOverlay.h"

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
    : QMainWindow(parent), m_splitter(new QSplitter(Qt::Horizontal)),
      m_contentSplitter(new QSplitter(Qt::Vertical)), m_sidebar(new Sidebar),
      m_strip(new PanelStrip), m_footer(new QLabel), m_selectionCount(new QLabel),
      m_pending(new QLabel)
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

    // The vertical splitter exists whether or not Quick Look is docked below:
    // introducing it later would mean re-parenting the panel strip at the
    // moment the user pressed a key, which loses focus and scroll position.
    m_contentSplitter->setObjectName(QStringLiteral("contentSplitter"));
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setHandleWidth(1);
    m_contentSplitter->addWidget(m_splitter);
    m_contentSplitter->setStretchFactor(0, 1);
    layout->addWidget(m_contentSplitter, 1);

    // The footer row carries both the cursor metadata and the pending-chord
    // indicator, which sits at the right where it does not shift the metadata
    // around as it appears and disappears.
    auto *footerRow = new QWidget(central);
    auto *footerLayout = new QHBoxLayout(footerRow);
    footerRow->setObjectName(QStringLiteral("footerRow"));
    footerLayout->setContentsMargins(currentPalette().panelPadding, 4,
                                     currentPalette().panelPadding, 4);
    footerLayout->setSpacing(14);

    m_footer->setObjectName(QStringLiteral("footer"));
    m_footer->setTextFormat(Qt::PlainText);
    footerLayout->addWidget(m_footer, 1);

    // The selection count, between the metadata and the pending chord. It
    // belongs in the status bar rather than only in the panel header: the
    // header answers "what is in this directory", this answers "what am I about
    // to act on", and the second question is the one you ask just before
    // pressing a key that moves files.
    m_selectionCount->setObjectName(QStringLiteral("selectionCount"));
    m_selectionCount->setTextFormat(Qt::PlainText);
    footerLayout->addWidget(m_selectionCount, 0);

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

void MainWindow::setSelectionCount(int count)
{
    // Nothing at all rather than "0 selected": a field that is always present
    // but usually zero is noise, and the status bar has to stay scannable.
    m_selectionCount->setText(
        count > 0 ? tr("%1 selected").arg(counted(count, tr("item"), tr("items"))) : QString());
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

QuickLookOverlay *MainWindow::quickLookOverlay()
{
    if (m_quickLookOverlay == nullptr) {
        m_quickLookOverlay = new QuickLookOverlay(centralWidget());
        connect(m_quickLookOverlay, &QuickLookOverlay::backdropClicked, this,
                &MainWindow::quickLookDismissed);
    }
    return m_quickLookOverlay;
}

void MainWindow::setQuickLookWidget(QWidget *view)
{
    if (m_quickLook == view) {
        return;
    }
    m_quickLook = view;
    if (m_quickLook != nullptr) {
        m_quickLook->hide();
        setQuickLookDock(m_quickLookDock, m_quickLookFloatPercent, m_quickLookDockPercent);
    }
}

QWidget *MainWindow::quickLookWidget() const
{
    return m_quickLook;
}

QuickLookDock MainWindow::quickLookDock() const
{
    return m_quickLookDock;
}

void MainWindow::detachQuickLook()
{
    if (m_quickLook == nullptr) {
        return;
    }

    if (m_quickLookOverlay != nullptr && m_quickLookOverlay->contentWidget() == m_quickLook) {
        m_quickLookOverlay->hideOverlay();
        m_quickLookOverlay->setContentWidget(nullptr);
        // The drop shadow belongs to float mode. Left in place it would render
        // as a dark smear along the edge of a docked pane.
        m_quickLook->setGraphicsEffect(nullptr);
    }

    m_strip->setQuickLookSlot(nullptr);

    // setParent(nullptr) rather than hide(): a widget left in a splitter still
    // occupies an index, and the next mode would insert beside it rather than
    // in place of it.
    m_quickLook->setParent(nullptr);
    m_quickLook->hide();

    // Panels are only hidden by full mode, and it is the only mode that has to
    // put them back.
    m_splitter->show();
}

void MainWindow::setQuickLookDock(QuickLookDock dock, int floatPercent, int dockPercent)
{
    m_quickLookDock = dock;
    m_quickLookFloatPercent = floatPercent;
    m_quickLookDockPercent = dockPercent;

    if (m_quickLook == nullptr) {
        return;
    }

    detachQuickLook();

    switch (dock) {
    case QuickLookDock::Float:
    case QuickLookDock::Full: {
        QuickLookOverlay *overlay = quickLookOverlay();
        overlay->setSizePercent(dock == QuickLookDock::Full ? 100 : floatPercent);
        overlay->setContentWidget(m_quickLook);
        if (m_quickLookVisible) {
            overlay->showOverlay();
        }
        break;
    }

    case QuickLookDock::Right:
    case QuickLookDock::Left: {
        // Beside the panel strip, inside the same splitter, so dragging the
        // handle trades width between the two the way §7.6 asks.
        const int index = dock == QuickLookDock::Left ? m_splitter->indexOf(m_strip)
                                                      : m_splitter->indexOf(m_strip) + 1;
        m_splitter->insertWidget(index, m_quickLook);
        m_quickLook->setVisible(m_quickLookVisible);

        const int total = m_splitter->width();
        const int paneWidth = total * dockPercent / 100;
        QList<int> sizes = m_splitter->sizes();
        if (sizes.size() >= 2) {
            sizes[index] = paneWidth;
            const int stripIndex = m_splitter->indexOf(m_strip);
            sizes[stripIndex] = qMax(1, sizes.at(stripIndex) - paneWidth);
            m_splitter->setSizes(sizes);
        }
        break;
    }

    case QuickLookDock::Bottom: {
        m_contentSplitter->addWidget(m_quickLook);
        m_contentSplitter->setStretchFactor(1, 0);
        m_quickLook->setVisible(m_quickLookVisible);

        const int total = m_contentSplitter->height();
        const int paneHeight = total * dockPercent / 100;
        m_contentSplitter->setSizes({qMax(1, total - paneHeight), paneHeight});
        break;
    }

    case QuickLookDock::Panel:
        // §7.6: "Occupies a slot in the panel strip itself, as though it were
        // another panel. Counts toward panels.max_count."
        m_strip->setQuickLookSlot(m_quickLook);
        m_quickLook->setVisible(m_quickLookVisible);
        break;
    }

    if (dock == QuickLookDock::Full && m_quickLookVisible) {
        m_splitter->hide();
    }
}

void MainWindow::setQuickLookVisible(bool visible)
{
    m_quickLookVisible = visible;

    if (m_quickLook == nullptr) {
        return;
    }

    if (m_quickLookDock == QuickLookDock::Float || m_quickLookDock == QuickLookDock::Full) {
        QuickLookOverlay *overlay = quickLookOverlay();
        if (visible) {
            overlay->showOverlay();
        } else {
            overlay->hideOverlay();
        }
        m_splitter->setVisible(!visible || m_quickLookDock != QuickLookDock::Full);
        return;
    }

    m_quickLook->setVisible(visible);
}

bool MainWindow::isQuickLookVisible() const
{
    return m_quickLookVisible && m_quickLook != nullptr;
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
