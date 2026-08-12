#include "ui/PanelStrip.h"

#include "core/Logging.h"
#include "ui/FilePanel.h"
#include "ui/ThemePalette.h"

#include <QHBoxLayout>
#include <QListView>
#include <QResizeEvent>
#include <QSplitter>

namespace pf::ui {
namespace {

/// §7.1: below this total width, only the focused panel is shown. A 400 px
/// window split three ways gives three columns too narrow to read a filename
/// in, which is worse than showing one.
constexpr int kCompactWidthThreshold = 400;

} // namespace

PanelStrip::PanelStrip(QWidget *parent)
    : QWidget(parent), m_splitter(new QSplitter(Qt::Horizontal, this))
{
    setObjectName(QStringLiteral("panelStrip"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_splitter->setObjectName(QStringLiteral("panelSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    layout->addWidget(m_splitter);
}

FilePanel *PanelStrip::addPanel(const QString &path)
{
    if (m_panels.size() >= kMaxPanels) {
        qCDebug(pfUi) << "panel limit reached";
        Q_EMIT panelLimitReached();
        Q_EMIT statusMessage(tr("At most %1 panels").arg(kMaxPanels));
        return nullptr;
    }

    auto *panel = new FilePanel(m_splitter);
    // Set before the widget is shown, so the very first polish sees it and the
    // stylesheet's [panelActive] rule applies without a repolish.
    panel->setProperty("panelActive", false);
    m_splitter->addWidget(panel);
    m_panels.append(panel);
    connectPanel(panel);

    panel->navigateTo(path);
    equalise();

    Q_EMIT panelCountChanged(count());
    setFocusedPanel(panel);
    return panel;
}

FilePanel *PanelStrip::splitFocusedPanel()
{
    const FilePanel *source = focusedPanel();
    if (source == nullptr) {
        return nullptr;
    }

    FilePanel *panel = addPanel(source->path());
    if (panel == nullptr) {
        return nullptr;
    }

    // §7.1: copies the path, sort and filter settings but not the selection.
    // Carrying the selection across would mean a subsequent delete acted on
    // files the user selected in a different panel.
    panel->setSortKey(source->sortKey());
    panel->setReverseSort(source->reverseSort());
    panel->setShowHidden(source->showHidden());
    panel->setCursorName(source->cursorName());
    return panel;
}

bool PanelStrip::closePanel(FilePanel *panel)
{
    if (panel == nullptr || !m_panels.contains(panel)) {
        return false;
    }
    if (m_panels.size() <= kMinPanels) {
        // §7.1: never the last panel. Closing it would leave a window with
        // nothing in it and no way to get a panel back.
        Q_EMIT statusMessage(tr("The last panel cannot be closed"));
        return false;
    }

    const int index = static_cast<int>(m_panels.indexOf(panel));
    m_panels.removeAt(index);
    panel->setParent(nullptr);
    panel->deleteLater();

    // §7.1: focus moves to the panel on the left, or to the right if the closed
    // one was leftmost.
    const int nextIndex = std::clamp(index - 1, 0, static_cast<int>(m_panels.size()) - 1);
    m_focused = nullptr;
    setFocusedPanel(m_panels.at(nextIndex));

    equalise();
    Q_EMIT panelCountChanged(count());
    return true;
}

bool PanelStrip::closeFocusedPanel()
{
    return closePanel(focusedPanel());
}

void PanelStrip::connectPanel(FilePanel *panel) const
{
    connect(panel, &FilePanel::statusMessage, this, &PanelStrip::statusMessage);
}

void PanelStrip::setFocusedPanel(FilePanel *panel)
{
    if (panel == nullptr || !m_panels.contains(panel) || m_focused == panel) {
        return;
    }

    if (m_focused != nullptr) {
        m_focused->setActive(false);
    }
    m_focused = panel;
    m_focused->setActive(true);
    m_focused->view()->setFocus(Qt::OtherFocusReason);

    if (m_compact) {
        // In compact mode the newly focused panel is the only visible one.
        applyResponsiveLayout(width());
    }

    Q_EMIT focusedPanelChanged(m_focused);
}

void PanelStrip::focusPanelAt(int index)
{
    if (index >= 0 && index < m_panels.size()) {
        setFocusedPanel(m_panels.at(index));
    }
}

void PanelStrip::focusNext()
{
    if (m_panels.size() < 2) {
        return;
    }
    // Wraps, unlike the cursor within a panel: cycling panels with Tab is a
    // ring, and stopping at the end would make the last panel a dead end.
    focusPanelAt((focusedIndex() + 1) % static_cast<int>(m_panels.size()));
}

void PanelStrip::focusPrevious()
{
    if (m_panels.size() < 2) {
        return;
    }
    const int size = static_cast<int>(m_panels.size());
    focusPanelAt((focusedIndex() - 1 + size) % size);
}

FilePanel *PanelStrip::focusedPanel() const
{
    return m_focused;
}

int PanelStrip::focusedIndex() const
{
    return m_focused == nullptr ? -1 : static_cast<int>(m_panels.indexOf(m_focused));
}

int PanelStrip::count() const
{
    return static_cast<int>(m_panels.size());
}

FilePanel *PanelStrip::panelAt(int index) const
{
    return index >= 0 && index < m_panels.size() ? m_panels.at(index) : nullptr;
}

QList<FilePanel *> PanelStrip::panels() const
{
    return m_panels;
}

void PanelStrip::equalise()
{
    const int count = static_cast<int>(m_panels.size());
    if (count == 0) {
        return;
    }

    // Two things are needed, and only doing the first is what left a newly
    // added third panel as an unreadable sliver.
    //
    // setSizes() distributes the width now, but a QSplitter re-divides using
    // *stretch factors* whenever it is resized or a widget is added, and a
    // widget added later defaults to a stretch of zero. So the stretch factors
    // have to be equalised too, or the next layout pass undoes this one.
    for (int i = 0; i < count; ++i) {
        m_splitter->setStretchFactor(i, 1);
    }

    const int available = m_splitter->width();
    if (available <= 0) {
        // Before the first layout the splitter has no width to divide, and
        // setSizes() with zeroes would collapse every panel. The stretch
        // factors above already give the right result once it does.
        return;
    }

    QList<int> sizes;
    sizes.reserve(count);
    for (int i = 0; i < count; ++i) {
        sizes.append(std::max(1, available / count));
    }
    m_splitter->setSizes(sizes);
}

void PanelStrip::applyResponsiveLayout(int availableWidth)
{
    const bool compact = availableWidth > 0 && availableWidth < kCompactWidthThreshold;

    if (compact != m_compact) {
        m_compact = compact;
        qCDebug(pfUi) << "panel strip compact mode:" << compact;
    }

    for (FilePanel *panel : std::as_const(m_panels)) {
        panel->setVisible(!m_compact || panel == m_focused);
    }
}

void PanelStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyResponsiveLayout(event->size().width());
}

} // namespace pf::ui
