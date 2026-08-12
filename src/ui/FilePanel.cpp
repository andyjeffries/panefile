#include "ui/FilePanel.h"

#include "core/Logging.h"
#include "model/DirectoryModel.h"
#include "ui/CursorMemory.h"
#include "ui/FileItemDelegate.h"
#include "ui/ThemePalette.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QScrollBar>
#include <QVBoxLayout>

namespace pf::ui {

FilePanel::FilePanel(QWidget *parent)
    : QWidget(parent), m_model(new DirectoryModel(this)), m_proxy(new FilterSortProxy(this)),
      m_view(new QListView(this)), m_delegate(new FileItemDelegate(this)),
      m_header(new QLabel(this)), m_status(new QLabel(this))
{
    setObjectName(QStringLiteral("filePanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header->setObjectName(QStringLiteral("panelHeader"));
    m_header->setTextFormat(Qt::PlainText);
    layout->addWidget(m_header);

    m_view->setObjectName(QStringLiteral("panelView"));
    m_view->setViewMode(QListView::ListMode);
    // §5.2: the single most important performance setting on the view. With it,
    // the view asks the delegate for one size and reuses it, so scrolling a
    // 100,000-entry directory does no per-row layout at all.
    m_view->setUniformItemSizes(true);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setFrameShape(QFrame::NoFrame);
    // The delegate paints the cursor row itself, in theme colours; leaving the
    // style's focus rectangle on top of that gives a doubled highlight.
    m_view->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(m_view, 1);

    m_status->setObjectName(QStringLiteral("panelStatus"));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->hide();
    layout->addWidget(m_status);

    m_proxy->setSourceModel(m_model);
    m_delegate->setSelectedNames(&m_selection);

    m_view->setModel(m_proxy);
    m_view->setItemDelegate(m_delegate);

    connect(m_model, &DirectoryModel::scanFinished, this, &FilePanel::onScanFinished);
    connect(m_model, &DirectoryModel::scanFailed, this, &FilePanel::onScanFailed);
    connect(m_model, &DirectoryModel::scanProgress, this, [this](int) { updateHeader(); });

    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) {
                if (current.isValid()) {
                    Q_EMIT cursorChanged(current.data(DirectoryModel::NameRole).toString());
                }
            });

    connect(m_view, &QListView::activated, this, [this] { activateCursorItem(); });

    applyPalette();
    setActive(false);
}

void FilePanel::applyPalette()
{
    // An interim measure: M3 compiles the theme into a stylesheet applied to
    // the whole application before any widget exists (§3.4), which is both
    // broader and cheaper than setting palettes widget by widget. Until then
    // the panel would inherit the platform's default light palette, and the
    // theme's colours — chosen against a dark background — are close to
    // unreadable on it.
    const ThemePalette &theme = currentPalette();

    QPalette widgetPalette = palette();
    widgetPalette.setColor(QPalette::Base, theme.background);
    widgetPalette.setColor(QPalette::Window, theme.background);
    widgetPalette.setColor(QPalette::Text, theme.text);
    widgetPalette.setColor(QPalette::WindowText, theme.text);
    widgetPalette.setColor(QPalette::Highlight, theme.cursorBackground);
    widgetPalette.setColor(QPalette::HighlightedText, theme.text);

    setAutoFillBackground(true);
    setPalette(widgetPalette);

    m_view->setPalette(widgetPalette);
    m_view->viewport()->setAutoFillBackground(true);

    QPalette headerPalette = widgetPalette;
    headerPalette.setColor(QPalette::Window, theme.surface);
    headerPalette.setColor(QPalette::WindowText, theme.subtext);
    m_header->setAutoFillBackground(true);
    m_header->setPalette(headerPalette);
    m_header->setContentsMargins(theme.panelPadding, 4, theme.panelPadding, 4);

    QPalette statusPalette = widgetPalette;
    statusPalette.setColor(QPalette::WindowText, theme.error);
    m_status->setPalette(statusPalette);
    m_status->setContentsMargins(theme.panelPadding, 6, theme.panelPadding, 6);
}

FilePanel::~FilePanel()
{
    // The delegate holds a raw pointer to m_selection for painting. Both are
    // destroyed with this object, but the delegate must not outlive the set it
    // points at if the destruction order ever changes.
    m_delegate->setSelectedNames(nullptr);
}

QListView *FilePanel::view() const
{
    return m_view;
}

QString FilePanel::path() const
{
    return m_path;
}

void FilePanel::navigateTo(const QString &path)
{
    setPathInternal(path, true);
}

void FilePanel::setPathInternal(const QString &path, bool pushHistory)
{
    const QString cleaned = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (cleaned == m_path) {
        return;
    }

    if (!m_path.isEmpty()) {
        rememberCursor();
        if (pushHistory) {
            m_backStack.append(m_path);
            // A new navigation invalidates the forward history, exactly as in a
            // browser: you cannot go forward to a future you have replaced.
            m_forwardStack.clear();
        }
    }

    m_path = cleaned;
    m_selection.clear();

    // §5.2: the cursor lands on the remembered entry for this directory, which
    // for the common case of navigating up is the directory just left.
    m_pendingCursorName = CursorMemory::instance().recall(m_path);

    m_status->hide();
    m_model->setPath(m_path);
    updateHeader();

    Q_EMIT pathChanged(m_path);
}

void FilePanel::goToParent()
{
    const QDir directory(m_path);
    if (directory.isRoot()) {
        Q_EMIT statusMessage(tr("Already at the filesystem root"));
        return;
    }

    const QString childName = QFileInfo(m_path).fileName();
    QDir parent(m_path);
    if (!parent.cdUp()) {
        Q_EMIT statusMessage(tr("No parent directory"));
        return;
    }

    const QString parentPath = parent.absolutePath();

    // Record where we came from *before* navigating, so the cursor lands on the
    // directory just left rather than on whatever was last visited there.
    CursorMemory::instance().remember(parentPath, childName);
    setPathInternal(parentPath, true);
}

void FilePanel::activateCursorItem()
{
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid()) {
        return;
    }

    const QString name = current.data(DirectoryModel::NameRole).toString();
    const bool isDir = current.data(DirectoryModel::IsDirRole).toBool();
    const QString absolute = m_path + QLatin1Char('/') + name;

    if (isDir) {
        setPathInternal(absolute, true);
        return;
    }

    Q_EMIT fileActivated(absolute);
}

bool FilePanel::goBack()
{
    if (m_backStack.isEmpty()) {
        Q_EMIT statusMessage(tr("No previous directory"));
        return false;
    }

    rememberCursor();
    const QString target = m_backStack.takeLast();
    m_forwardStack.append(m_path);

    // setPathInternal with pushHistory would append to the back stack we are
    // currently unwinding, so history navigation manages the stacks itself.
    const QString previous = m_path;
    m_path.clear();
    setPathInternal(target, false);
    Q_UNUSED(previous)
    return true;
}

bool FilePanel::goForward()
{
    if (m_forwardStack.isEmpty()) {
        Q_EMIT statusMessage(tr("No next directory"));
        return false;
    }

    rememberCursor();
    const QString target = m_forwardStack.takeLast();
    m_backStack.append(m_path);

    m_path.clear();
    setPathInternal(target, false);
    return true;
}

QString FilePanel::cursorName() const
{
    const QModelIndex current = m_view->currentIndex();
    return current.isValid() ? current.data(DirectoryModel::NameRole).toString() : QString();
}

void FilePanel::setCursorName(const QString &name)
{
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const QModelIndex index = m_proxy->index(row, 0);
        if (index.data(DirectoryModel::NameRole).toString() == name) {
            m_view->setCurrentIndex(index);
            m_view->scrollTo(index, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
}

void FilePanel::rememberCursor()
{
    const QString name = cursorName();
    if (!m_path.isEmpty() && !name.isEmpty()) {
        CursorMemory::instance().remember(m_path, name);
    }
}

void FilePanel::restoreCursor()
{
    if (m_proxy->rowCount() == 0) {
        return;
    }

    if (!m_pendingCursorName.isEmpty()) {
        const QString wanted = m_pendingCursorName;
        m_pendingCursorName.clear();
        setCursorName(wanted);
        if (m_view->currentIndex().isValid()) {
            return;
        }
        // The remembered entry is gone — deleted, renamed, or filtered out.
        // Falling through to the first row beats leaving no cursor at all.
    }

    if (!m_view->currentIndex().isValid()) {
        m_view->setCurrentIndex(m_proxy->index(0, 0));
    }
}

void FilePanel::moveCursor(int delta)
{
    const int count = m_proxy->rowCount();
    if (count == 0) {
        return;
    }

    const QModelIndex current = m_view->currentIndex();
    const int currentRow = current.isValid() ? current.row() : 0;
    const int target = std::clamp(currentRow + delta, 0, count - 1);

    m_view->setCurrentIndex(m_proxy->index(target, 0));
}

void FilePanel::moveCursorToStart()
{
    if (m_proxy->rowCount() > 0) {
        m_view->setCurrentIndex(m_proxy->index(0, 0));
    }
}

void FilePanel::moveCursorToEnd()
{
    const int count = m_proxy->rowCount();
    if (count > 0) {
        m_view->setCurrentIndex(m_proxy->index(count - 1, 0));
    }
}

void FilePanel::movePage(int direction)
{
    const int rowHeight = std::max(1, currentPalette().rowHeight);
    const int visibleRows = std::max(1, m_view->viewport()->height() / rowHeight);
    // One row of overlap, so a page down leaves a line of context rather than
    // making the reader work out whether anything was skipped.
    moveCursor(direction * std::max(1, visibleRows - 1));
}

void FilePanel::setShowHidden(bool show)
{
    // Remembering the cursor across the filter change stops the view jumping to
    // the top when dotfiles appear above the current entry.
    const QString name = cursorName();
    m_proxy->setShowHidden(show);
    if (!name.isEmpty()) {
        setCursorName(name);
    }
    updateHeader();
}

bool FilePanel::showHidden() const
{
    return m_proxy->showHidden();
}

void FilePanel::toggleShowHidden()
{
    setShowHidden(!showHidden());
}

void FilePanel::setSortKey(SortKey key)
{
    const QString name = cursorName();
    m_proxy->setSortKey(key);
    if (!name.isEmpty()) {
        setCursorName(name);
    }
    updateHeader();
}

SortKey FilePanel::sortKey() const
{
    return m_proxy->sortKey();
}

void FilePanel::setReverseSort(bool reverse)
{
    const QString name = cursorName();
    m_proxy->setReverseSort(reverse);
    if (!name.isEmpty()) {
        setCursorName(name);
    }
    updateHeader();
}

bool FilePanel::reverseSort() const
{
    return m_proxy->reverseSort();
}

void FilePanel::setFilterText(const QString &text)
{
    m_proxy->setFilterText(text);
    restoreCursor();
    updateHeader();
}

void FilePanel::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;

    // Set as a dynamic property so the stylesheet can key off it (M3); the
    // repolish is what makes an already-styled widget pick up the change.
    setProperty("panelActive", active);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

bool FilePanel::isActive() const
{
    return m_active;
}

void FilePanel::onScanFinished(const QString &path, int count)
{
    Q_UNUSED(path)
    Q_UNUSED(count)
    m_status->hide();
    restoreCursor();
    updateHeader();
}

void FilePanel::onScanFailed(const QString &path, const QString &reason)
{
    // §7.2: an inline error state naming the reason, with the previous listing
    // still reachable through go_back — not a modal, and not an empty panel
    // that leaves the user guessing.
    m_status->setText(tr("Cannot read %1\n%2").arg(QDir::toNativeSeparators(path), reason));
    m_status->show();
    updateHeader();
    Q_EMIT statusMessage(reason);
}

void FilePanel::updateHeader()
{
    QString display = m_path;
    const QString home = QDir::homePath();
    if (display == home) {
        display = QStringLiteral("~");
    } else if (display.startsWith(home + QLatin1Char('/'))) {
        display = QStringLiteral("~") + display.mid(home.size());
    }

    const int shown = m_proxy->rowCount();
    const int total = m_model->rowCount();

    QString counts =
        shown == total ? tr("%n item(s)", nullptr, shown) : tr("%1 of %2").arg(shown).arg(total);

    if (m_model->isScanning()) {
        counts = tr("scanning… %1").arg(shown);
    }

    m_header->setText(QStringLiteral("%1    %2").arg(display, counts));
}

} // namespace pf::ui
