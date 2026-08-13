#include "ui/FilePanel.h"

#include "core/Logging.h"
#include "model/DirectoryModel.h"
#include "ui/CursorMemory.h"
#include "ui/FileItemDelegate.h"
#include "ui/ThemePalette.h"

#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

namespace pf::ui {

FilePanel::FilePanel(QWidget *parent)
    : QWidget(parent), m_model(new DirectoryModel(this)), m_proxy(new FilterSortProxy(this)),
      m_view(new QListView(this)), m_delegate(new FileItemDelegate(this)),
      m_header(new QLabel(this)), m_status(new QLabel(this))
{
    setObjectName(QStringLiteral("filePanel"));
    // Narrow enough that ten panels fit §7.1's maximum on a normal display,
    // wide enough that a filename column is still worth reading.
    setMinimumWidth(140);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header->setObjectName(QStringLiteral("panelHeader"));
    m_header->setTextFormat(Qt::PlainText);
    // A QLabel's size hint grows with its text, and a header showing a long
    // path would therefore impose a minimum width on the whole panel that a
    // QSplitter cannot shrink below — which is what left a third panel as an
    // unreadable sliver instead of one of three equal columns. Ignoring the
    // horizontal hint lets the panel be as narrow as the splitter wants, and
    // the text is elided to fit.
    m_header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_header->setMinimumWidth(0);
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

    // §7.3: watch the directory a panel is showing, and walk up when it goes.
    m_model->setWatchingEnabled(true);
    connect(m_model, &DirectoryModel::directoryVanished, this, [this](const QString &gone) {
        Q_EMIT statusMessage(tr("%1 no longer exists").arg(QDir::toNativeSeparators(gone)));
        walkUpToExistingAncestor();
    });

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

    // §7.7's viewport window, refreshed on the two things that change it:
    // scrolling, and rows arriving.
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { updateThumbnailWindow(); });

    applyPalette();
    setActive(false);
}

void FilePanel::applyPalette()
{
    // The stylesheet built in M3 now carries the panel's colours, so nothing is
    // set by hand here any more. What remains is the one thing a stylesheet
    // cannot express: the view's *item* colours, which the style consults
    // through QPalette when it draws selection and alternating rows.
    const ThemePalette &theme = currentPalette();

    QPalette viewPalette = m_view->palette();
    viewPalette.setColor(QPalette::Base, Qt::transparent);
    viewPalette.setColor(QPalette::Text, theme.text);
    viewPalette.setColor(QPalette::Highlight, theme.cursorBackground);
    viewPalette.setColor(QPalette::HighlightedText, theme.text);
    m_view->setPalette(viewPalette);

    // Padding is the stylesheet's job. Setting contentsMargins here as well
    // double-counted it, and the header elided against a width that was wider
    // than the space it actually had — which clipped the item count off the
    // right-hand end.
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
    // A selection names entries in the directory being left. Carrying it into
    // the next one would leave names selected that mean different files.
    m_selection.clear();

    // §5.2: the cursor lands on the remembered entry for this directory, which
    // for the common case of navigating up is the directory just left.
    m_pendingCursorName = CursorMemory::instance().recall(m_path);

    m_status->hide();
    m_model->setPath(m_path);
    updateHeader();

    Q_EMIT pathChanged(m_path);
}

void FilePanel::walkUpToExistingAncestor()
{
    // §7.3: "walk the panel up to the nearest existing ancestor". Not merely
    // the parent — a `rm -r` of a whole tree takes several levels with it, and
    // stopping at the first missing one would leave the panel showing another
    // directory that is also gone.
    QDir directory(m_path);
    while (!directory.exists() && !directory.isRoot()) {
        if (!directory.cdUp()) {
            break;
        }
    }

    const QString target = directory.exists() ? directory.absolutePath() : QDir::rootPath();

    // Without history: the directory the user was in no longer exists, so a
    // back entry pointing at it would be a dead end they could walk into.
    m_path.clear();
    setPathInternal(target, false);
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

QString FilePanel::cursorPath() const
{
    const QString name = cursorName();
    if (name.isEmpty() || m_path.isEmpty()) {
        return {};
    }
    return QDir(m_path).absoluteFilePath(name);
}

FileEntry FilePanel::cursorEntry() const
{
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid()) {
        return {};
    }
    const QVariant value = current.data(DirectoryModel::EntryRole);
    return value.canConvert<FileEntry>() ? value.value<FileEntry>() : FileEntry{};
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

QString FilePanel::filterText() const
{
    return m_proxy->filterText();
}

void FilePanel::setFuzzyMatching(bool fuzzy)
{
    m_proxy->setFuzzyMatching(fuzzy);
}

void FilePanel::openFilterBar()
{
    if (m_filterBar == nullptr) {
        // §3.4: constructed on first use. Inserted above the status label so a
        // scan error and a filter can be visible at the same time.
        m_filterBar = new QLineEdit(this);
        m_filterBar->setObjectName(QStringLiteral("panelFilter"));
        m_filterBar->setPlaceholderText(tr("Filter…"));
        m_filterBar->setClearButtonEnabled(true);

        auto *layout = qobject_cast<QVBoxLayout *>(this->layout());
        layout->insertWidget(layout->indexOf(m_status), m_filterBar);

        // Live, per §7.8: "filters the current directory's model live via the
        // proxy". The cursor follows to the best remaining row, or the list
        // would show a filtered set with the cursor on nothing.
        connect(m_filterBar, &QLineEdit::textChanged, this, [this](const QString &text) {
            setFilterText(text);
            if (m_proxy->rowCount() > 0 && !m_view->currentIndex().isValid()) {
                m_view->setCurrentIndex(m_proxy->index(0, 0));
            }
        });
        connect(m_filterBar, &QLineEdit::returnPressed, this, [this] { closeFilterBar(true); });

        // Esc is handled here rather than through the registry because §6.1
        // puts the application in Typing mode while this has focus: single-key
        // bindings are suspended, and routing Esc back through the dispatcher
        // would take it away from whatever modal is also listening for it.
        m_filterBar->installEventFilter(this);
    }

    m_filterBar->show();
    m_filterBar->setFocus(Qt::ShortcutFocusReason);
    m_filterBar->selectAll();
    Q_EMIT modeChanged();
}

void FilePanel::closeFilterBar(bool keepFilter)
{
    if (m_filterBar == nullptr) {
        return;
    }

    if (!keepFilter) {
        // §7.8: "Esc clears it". Clearing the box rather than only hiding it,
        // because a hidden box still holding a filter would leave the listing
        // mysteriously incomplete.
        m_filterBar->clear();
        setFilterText(QString());
    }

    m_filterBar->hide();
    m_view->setFocus(Qt::ShortcutFocusReason);
    Q_EMIT modeChanged();
}

bool FilePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_filterBar && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // §7.8: "Esc clears it".
            closeFilterBar(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool FilePanel::isFilterBarOpen() const
{
    return m_filterBar != nullptr && m_filterBar->isVisible();
}

void FilePanel::setThumbnailsEnabled(bool enabled)
{
    m_model->setThumbnailsEnabled(enabled);
    if (enabled) {
        updateThumbnailWindow();
    }
}

void FilePanel::updateThumbnailWindow()
{
    if (!m_model->thumbnailsEnabled()) {
        return;
    }

    const QModelIndex first = m_view->indexAt(m_view->viewport()->rect().topLeft());
    const QModelIndex last = m_view->indexAt(m_view->viewport()->rect().bottomLeft());

    const int firstRow = first.isValid() ? m_proxy->mapToSource(first).row() : 0;
    const int lastRow = last.isValid() ? m_proxy->mapToSource(last).row() : m_model->rowCount() - 1;

    m_model->requestThumbnailRange(std::min(firstRow, lastRow), std::max(firstRow, lastRow));
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

void FilePanel::refreshTheme()
{
    applyPalette();
    style()->unpolish(this);
    style()->polish(this);
    m_view->viewport()->update();
    applyHeaderElision();
    update();
}

void FilePanel::setSelectionMode(bool on)
{
    if (m_selectionMode == on) {
        return;
    }
    m_selectionMode = on;

    // Leaving Selection mode clears the selection. Keeping it would leave the
    // user with an invisible selection that the next Ctrl+C acts on.
    if (!on) {
        clearSelection();
    }
    updateHeader();
    Q_EMIT modeChanged();
}

bool FilePanel::isSelectionMode() const
{
    return m_selectionMode;
}

void FilePanel::toggleSelectionMode()
{
    setSelectionMode(!m_selectionMode);
}

void FilePanel::toggleSelectionAt(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    if (m_selection.contains(name)) {
        m_selection.remove(name);
    } else {
        m_selection.insert(name);
    }
    m_view->viewport()->update();
    updateHeader();
    Q_EMIT selectionChanged(static_cast<int>(m_selection.size()));
}

void FilePanel::selectAll()
{
    // Everything *visible*: a filter is a statement about what the user is
    // working with, and selecting entries they have filtered out would act on
    // files they cannot see.
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        m_selection.insert(m_proxy->index(row, 0).data(DirectoryModel::NameRole).toString());
    }
    m_view->viewport()->update();
    updateHeader();
    Q_EMIT selectionChanged(static_cast<int>(m_selection.size()));
}

void FilePanel::clearSelection()
{
    if (m_selection.isEmpty()) {
        return;
    }
    m_selection.clear();
    m_view->viewport()->update();
    updateHeader();
    Q_EMIT selectionChanged(0);
}

QStringList FilePanel::selectedPaths() const
{
    QStringList paths;

    if (!m_selection.isEmpty()) {
        // In the proxy's order rather than the set's, so an operation on a
        // multi-selection proceeds in the order the user sees.
        for (int row = 0; row < m_proxy->rowCount(); ++row) {
            const QString name = m_proxy->index(row, 0).data(DirectoryModel::NameRole).toString();
            if (m_selection.contains(name)) {
                paths << m_path + QLatin1Char('/') + name;
            }
        }
        return paths;
    }

    const QString cursor = cursorName();
    if (!cursor.isEmpty()) {
        paths << m_path + QLatin1Char('/') + cursor;
    }
    return paths;
}

int FilePanel::selectionCount() const
{
    return static_cast<int>(m_selection.size());
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
    updateThumbnailWindow();
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

    // §6.1: "a badge shows in the panel header" while Selection mode is on.
    if (m_selectionMode) {
        counts += m_selection.isEmpty()
                      ? tr("   [SELECT]")
                      : tr("   [SELECT %n]", nullptr, static_cast<int>(m_selection.size()));
    } else if (!m_selection.isEmpty()) {
        counts += tr("   %n selected", nullptr, static_cast<int>(m_selection.size()));
    }

    m_headerText = QStringLiteral("%1    %2").arg(display, counts);
    applyHeaderElision();
}

void FilePanel::applyHeaderElision()
{
    // Elided from the left: the tail of a path is what identifies it, so
    // "…/Developer/panefile/src" is far more useful than "/Users/andy/Deve…".
    //
    // Measured against contentsRect(), which is what the stylesheet's padding
    // has already been subtracted from. Subtracting the padding again here —
    // as this did — leaves the label thinking it has less room than it has, and
    // the elision eats text that would have fitted.
    const QFontMetrics metrics(m_header->font());
    const int available = std::max(0, m_header->contentsRect().width());
    m_header->setText(metrics.elidedText(m_headerText, Qt::ElideLeft, available));
}

void FilePanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    // A plain QWidget subclass does not render the background, border or
    // border-radius a stylesheet gives it — Qt only does that automatically for
    // the widget classes that already paint themselves. Without this, §9's
    // focused-panel border simply does not appear, which matters because §9
    // calls it "the single most important visual affordance in the app".
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void FilePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyHeaderElision();
}

} // namespace pf::ui
