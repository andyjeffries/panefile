#include "ui/Sidebar.h"

#include "core/Format.h"
#include "core/Logging.h"
#include "core/WorkerPools.h"
#include "fs/Trash.h"
#include "model/SymbolicIcon.h"
#include "platform/Paths.h"
#include "ui/SymbolicWidgets.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace pf::ui {
namespace {

/// Marks a row as a heading rather than a place, so it can be skipped when the
/// cursor moves and rendered differently.
constexpr int kIsHeadingRole = Qt::UserRole + 1;
constexpr int kPathRole = Qt::UserRole + 2;

/// §7.11's Devices rows carry a volume id as well as (when mounted) a path.
constexpr int kVolumeIdRole = Qt::UserRole + 3;

/// The row's symbolic icon, by name, and how loudly it is drawn. Stored rather
/// than applied once so that refreshTheme() can re-tint every row.
constexpr int kIconNameRole = Qt::UserRole + 4;
constexpr int kToneRole = Qt::UserRole + 5;

/// A hairline between groups, painted by the delegate.
constexpr int kDividerRole = Qt::UserRole + 6;

/// Faint text at the row's right-hand end: the wastebasket's count and size.
constexpr int kDetailRole = Qt::UserRole + 7;

/// A mounted volume that can be unmounted shows an eject button.
constexpr int kEjectRole = Qt::UserRole + 8;

/// A pinned folder rather than a built-in place: removing it unpins it.
constexpr int kPinnedRole = Qt::UserRole + 9;

/// The wastebasket's row, so its summary can be written into it.
constexpr int kTrashRole = Qt::UserRole + 10;

/// A place being dragged out of the sidebar. Its own type, not a file URL: a
/// place dropped on a panel must not be taken as "copy Downloads in here".
constexpr QLatin1String kPlaceMimeType("application/x-panefile-sidebar-place");

constexpr int kEjectSize = 14;
constexpr int kRowEndPadding = 8;

/// How a row takes its colours from the theme.
enum class Tone {
    /// A place: icon in `subtext`, label in the stylesheet's text colour.
    Place,
    /// An unmounted device: the same glyph, a step back in `overlay`, label in
    /// `subtext`. §7.11's "mount state indicator" — told apart by more than
    /// colour, since the tooltip and the label weight say it too.
    Unmounted,
    /// A section heading: no icon, label in `overlay`.
    Heading,
};

/// The size the places' glyphs are drawn at: the row's text height, as
/// Nautilus and Review draw theirs.
constexpr int kPlaceIconSize = 16;

/// The glyph for a mounted or unmounted volume. Heroicons' "server" is the
/// drive-shaped one; "server-stack" reads as a rack.
constexpr QLatin1String kDeviceIcon("server");

/// Where a row's eject button is, in the list's viewport.
QRect ejectRect(const QRect &row)
{
    return {row.right() - kRowEndPadding - kEjectSize + 1, row.center().y() - (kEjectSize / 2),
            kEjectSize, kEjectSize};
}

/// The sidebar's rows, as the stylesheet draws them, plus what it cannot: the
/// divider line, the faint detail at a row's end, and the eject button.
///
/// The divider used to be a QFrame set as the row's item widget. It painted in
/// an offscreen render and not on a real desktop, where the row showed as a
/// gap; painting it here leaves nothing to go missing.
class SidebarDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const ThemePalette &palette = currentPalette();

        if (index.data(kDividerRole).toBool()) {
            painter->fillRect(QRectF(option.rect.left() + 6, option.rect.center().y(),
                                     option.rect.width() - 12, 1),
                              palette.border);
            return;
        }

        QString detail = index.data(kDetailRole).toString();
        const bool eject = index.data(kEjectRole).toBool();

        QFont detailFont = option.font;
        if (detailFont.pixelSize() > 0) {
            detailFont.setPixelSize(std::max(1, detailFont.pixelSize() - 1));
        }
        detailFont.setFeature(QFont::Tag("tnum"), 1);
        const QFontMetrics detailMetrics(detailFont);

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        // Where the name starts and how far it may run: the row's padding, the
        // icon and the gap after it on the left; the eject button and the
        // detail, each with a gap before it, on the right.
        const int nameLeft = kRowEndPadding + opt.decorationSize.width() + 6;
        const int ejectWidth = eject ? kEjectSize + kRowEndPadding : 0;
        const int nameWidth = opt.fontMetrics.horizontalAdvance(opt.text);
        const auto room = [&](const QString &end) {
            const int endWidth = end.isEmpty() ? 0 : detailMetrics.horizontalAdvance(end) + 10;
            return opt.rect.width() - nameLeft - kRowEndPadding - ejectWidth - endWidth;
        };

        // The name wins. A detail that would squeeze it is dropped — it is in
        // the tooltip too — rather than leaving "Wa…et" beside "39 · 17 GB".
        if (!detail.isEmpty() && nameWidth > room(detail)) {
            detail.clear();
        }
        if (nameWidth > room(detail)) {
            opt.text =
                opt.fontMetrics.elidedText(opt.text, Qt::ElideMiddle, std::max(0, room(detail)));
        }
        const QWidget *widget = option.widget;
        const QStyle *style = widget != nullptr ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        int right = option.rect.right() - kRowEndPadding;
        if (eject) {
            const QRect target = ejectRect(option.rect);
            const qreal ratio =
                painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
            painter->drawPixmap(target, SymbolicIcon::pixmap(QStringLiteral("eject"),
                                                             palette.subtext, kEjectSize, ratio));
            right = target.left() - kRowEndPadding;
        }
        if (!detail.isEmpty()) {
            painter->save();
            painter->setFont(detailFont);
            painter->setPen(palette.overlay);
            painter->drawText(QRect(option.rect.left(), option.rect.top(),
                                    right - option.rect.left() + 1, option.rect.height()),
                              Qt::AlignRight | Qt::AlignVCenter, detail);
            painter->restore();
        }
    }
};

} // namespace

Sidebar::Sidebar(QWidget *parent)
    : QWidget(parent), m_list(new QListWidget(this)), m_trashSummaryTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("sidebar"));

    // Without this a plain QWidget subclass ignores the stylesheet's
    // background-color entirely — Qt only paints one automatically for the
    // widget classes that already draw themselves. The list inside painted its
    // own grey and the section label's strip did not, which is what left a
    // paler band across the top of the sidebar.
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(140);
    setMaximumWidth(280);
    // Wide enough for a place's name and the wastebasket's count and size side
    // by side; GNOME Files' is wider still. A sidebar of shortcuts earns no more.
    resize(220, height());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // A section label, so the sidebar reads as a sidebar rather than as a
    // column of words sharing an edge with the first panel — with the
    // application menu at the other end of the same row, where GNOME Files
    // keeps its own.
    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    // One line, both centred on it: the label's padding is horizontal only, so
    // the text sits in the middle of the button's height rather than resting
    // on a baseline below it.
    headerLayout->setContentsMargins(0, 4, 8, 4);
    headerLayout->setSpacing(0);

    auto *section = new QLabel(tr("Favourites"), header);
    section->setObjectName(QStringLiteral("sidebarSection"));
    section->setTextFormat(Qt::PlainText);
    headerLayout->addWidget(section, 1, Qt::AlignVCenter);

    auto *menu = new SymbolicButton(QStringLiteral("bars-3"), 16, header);
    menu->setObjectName(QStringLiteral("sidebarMenu"));
    menu->setFocusPolicy(Qt::NoFocus);
    menu->setFixedSize(28, 28);
    menu->setToolTip(tr("Menu"));
    connect(menu, &QPushButton::clicked, this, [this, menu] {
        Q_EMIT menuRequested(menu->mapToGlobal(QPoint(0, menu->height() + 4)));
    });
    headerLayout->addWidget(menu, 0, Qt::AlignVCenter);
    layout->addWidget(header);

    m_list->setObjectName(QStringLiteral("sidebarList"));
    m_list->setFrameShape(QFrame::NoFrame);
    // Not uniform: the divider above the wastebasket is a hairline, not a row.
    m_list->setUniformItemSizes(false);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The sidebar is a short fixed list of shortcuts. A scrollbar track running
    // down it is chrome for a problem it does not have.
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setIconSize(QSize(kPlaceIconSize, kPlaceIconSize));
    m_list->setItemDelegate(new SidebarDelegate(m_list));
    m_list->setMouseTracking(true);
    layout->addWidget(m_list);

    // Drag a folder in to pin it; drag a place out to remove it. The view's
    // own drag and drop stays off — it would move rows about — and the
    // viewport's events are handled in handleViewportEvent().
    m_list->setAcceptDrops(true);
    m_list->viewport()->setAcceptDrops(true);
    m_list->viewport()->installEventFilter(this);

    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, &Sidebar::showContextMenu);

    m_trashSummaryTimer->setSingleShot(true);
    // Trashing a folder of a thousand files is a thousand notifications; one
    // recount after they stop is enough.
    m_trashSummaryTimer->setInterval(300);
    connect(m_trashSummaryTimer, &QTimer::timeout, this, &Sidebar::refreshTrashSummary);

    applyPalette();

    // §5.1's places are shortcuts, not a state. A highlight left on one reads
    // as "you are here", which in a window holding several panels at several
    // paths is true of none of them — so the current row is dropped whenever
    // the sidebar stops being the thing you are working in.
    m_list->installEventFilter(this);

    // Both signals, because they cover different gestures and neither covers
    // all of them.
    //
    // itemActivated is emitted on Enter, and on a *double* click — on macOS the
    // style does not activate an item on a single click, which is why clicking
    // a place in the sidebar did nothing at all. itemClicked is the single
    // click. Wiring both means a double click fires twice, which openItem is
    // written to tolerate.
    connect(m_list, &QListWidget::itemClicked, this, &Sidebar::openItem);
    connect(m_list, &QListWidget::itemActivated, this, &Sidebar::openItem);
}

void Sidebar::applyPalette()
{
    // Colours come from the stylesheet; only the item-view roles the style
    // consults directly are set here.
    QPalette listPalette = m_list->palette();
    listPalette.setColor(QPalette::Active, QPalette::Highlight,
                         currentPalette().selectionBackground);
    listPalette.setColor(QPalette::Active, QPalette::HighlightedText, currentPalette().text);

    // Invisible when the sidebar is not the thing you are working in.
    //
    // §5.1's places are shortcuts — press one and a panel goes there — not a
    // state, and a highlight left on one reads as "you are here", which in a
    // window holding several panels at several paths is true of none of them.
    //
    // Done through the palette rather than the stylesheet because Qt paints an
    // unfocused selection from the Inactive group and never consults the
    // stylesheet's ::item:selected rule for it — which is why the row stayed a
    // desaturated grey however thoroughly the selection was cleared.
    listPalette.setColor(QPalette::Inactive, QPalette::Highlight, currentPalette().surface);
    listPalette.setColor(QPalette::Inactive, QPalette::HighlightedText, currentPalette().subtext);
    m_list->setPalette(listPalette);
}

void Sidebar::refreshTheme()
{
    applyPalette();
    for (int row = 0; row < m_list->count(); ++row) {
        applyItemTheme(m_list->item(row));
    }
}

void Sidebar::applyItemTheme(QListWidgetItem *item)
{
    const ThemePalette &palette = currentPalette();
    const auto tone = static_cast<Tone>(item->data(kToneRole).toInt());

    switch (tone) {
    case Tone::Heading:
        item->setForeground(palette.overlay);
        return;
    case Tone::Unmounted:
        item->setForeground(palette.subtext);
        break;
    case Tone::Place:
        // No foreground: the label takes the stylesheet's text colour, and an
        // explicit brush here would override it in every state, including
        // selected.
        item->setData(Qt::ForegroundRole, QVariant());
        break;
    }

    // The glyph is a step quieter than the label, as in Review and Nautilus:
    // it is there to be found by shape, not read.
    const QColor tint = tone == Tone::Unmounted ? palette.overlay : palette.subtext;
    item->setIcon(SymbolicIcon::icon(item->data(kIconNameRole).toString(), tint));
}

QString Sidebar::iconNameForPath(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned == QDir::cleanPath(QDir::homePath())) {
        return QStringLiteral("home");
    }

    struct Match {
        QStandardPaths::StandardLocation location;
        const char *icon;
    };
    static constexpr Match kMatches[]{
        {.location = QStandardPaths::DesktopLocation, .icon = "computer-desktop"},
        {.location = QStandardPaths::DownloadLocation, .icon = "arrow-down-tray"},
        {.location = QStandardPaths::DocumentsLocation, .icon = "document-text"},
        {.location = QStandardPaths::PicturesLocation, .icon = "photo"},
        {.location = QStandardPaths::MusicLocation, .icon = "musical-note"},
        {.location = QStandardPaths::MoviesLocation, .icon = "film"},
    };
    for (const Match &match : kMatches) {
        if (cleaned == QDir::cleanPath(QStandardPaths::writableLocation(match.location))) {
            return QString::fromLatin1(match.icon);
        }
    }
    return QStringLiteral("folder");
}

void Sidebar::openItem(QListWidgetItem *item)
{
    if (item == nullptr || item->data(kIsHeadingRole).toBool()) {
        return;
    }

    const QString path = item->data(kPathRole).toString();
    if (!path.isEmpty()) {
        // The place has been opened, so nothing here is current any more, and
        // the panel it opened in is where the user is now working.
        clearHighlight();
        Q_EMIT placeActivated(path);
        return;
    }

    // §7.11: "Enter mounts … and navigates." A device row with no path is an
    // unmounted volume; mounting is asynchronous, and the navigation happens
    // when it reports where it landed.
    const QString volumeId = item->data(kVolumeIdRole).toString();
    if (volumeId.isEmpty() || m_volumes == nullptr) {
        return;
    }

    // A double click delivers both signals, and asking to mount the same volume
    // twice is a second D-Bus call for something already under way.
    if (m_mounting.contains(volumeId)) {
        return;
    }
    m_mounting.insert(volumeId);

    Q_EMIT statusMessage(tr("Mounting…"));
    m_volumes->mount(volumeId);
}

void Sidebar::addHeading(const QString &title)
{
    auto *item = new QListWidgetItem(title, m_list);
    item->setData(kIsHeadingRole, true);
    item->setData(kToneRole, static_cast<int>(Tone::Heading));
    item->setFlags(Qt::NoItemFlags);
    applyItemTheme(item);
}

void Sidebar::addDivider()
{
    auto *item = new QListWidgetItem(m_list);
    item->setData(kIsHeadingRole, true);
    item->setData(kDividerRole, true);
    item->setFlags(Qt::NoItemFlags);
    item->setSizeHint(QSize(0, 9));
}

QListWidgetItem *Sidebar::addPlace(const QString &title, const QString &path,
                                   const QString &iconName, bool pinned)
{
    // Only places that exist. An XDG user directory pointing at something the
    // user deleted would otherwise sit in the sidebar failing to open.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return nullptr;
    }
    if (!pinned && m_hidden.contains(QDir::cleanPath(path))) {
        return nullptr;
    }

    auto *item = new QListWidgetItem(title, m_list);
    item->setData(kPathRole, path);
    item->setData(kIsHeadingRole, false);
    item->setData(kIconNameRole, iconName);
    item->setData(kToneRole, static_cast<int>(Tone::Place));
    item->setData(kPinnedRole, pinned);
    item->setToolTip(QDir::toNativeSeparators(path));
    applyItemTheme(item);
    return item;
}

void Sidebar::populate()
{
    m_dragCandidate = nullptr;
    m_list->clear();

    addPlace(tr("Home"), QDir::homePath(), QStringLiteral("home"));

    // §5.1's XDG user dirs. QStandardPaths reads user-dirs.dirs on Linux and
    // the native locations on macOS, so one call covers both platforms.
    struct Place {
        QStandardPaths::StandardLocation location;
        QString title;
        QString icon;
    };
    const QList<Place> places{
        {.location = QStandardPaths::DesktopLocation,
         .title = tr("Desktop"),
         .icon = QStringLiteral("computer-desktop")},
        {.location = QStandardPaths::DownloadLocation,
         .title = tr("Downloads"),
         .icon = QStringLiteral("arrow-down-tray")},
        {.location = QStandardPaths::DocumentsLocation,
         .title = tr("Documents"),
         .icon = QStringLiteral("document-text")},
        {.location = QStandardPaths::PicturesLocation,
         .title = tr("Pictures"),
         .icon = QStringLiteral("photo")},
        {.location = QStandardPaths::MusicLocation,
         .title = tr("Music"),
         .icon = QStringLiteral("musical-note")},
        {.location = QStandardPaths::MoviesLocation,
         .title = tr("Videos"),
         .icon = QStringLiteral("film")},
    };

    for (const Place &place : places) {
        const QString path = QStandardPaths::writableLocation(place.location);
        // QStandardPaths falls back to the home directory for locations that
        // are not configured; listing "Documents" that opens $HOME is worse
        // than not listing it.
        if (path != QDir::homePath()) {
            addPlace(place.title, path, place.icon);
        }
    }

    // The wastebasket, under a divider: a place, but not one of yours, as
    // GNOME Files and Finder both set it apart. Its directory is created if
    // nothing has been trashed yet, as the XDG trash spec allows, so the entry
    // opens onto an empty folder rather than an error.
    if (const QString trash = fs::Trash().filesDirectory();
        !trash.isEmpty() && !m_hidden.contains(QDir::cleanPath(trash))) {
        QDir().mkpath(trash);
        addDivider();
        if (QListWidgetItem *item = addPlace(tr("Wastebasket"), trash, QStringLiteral("trash"));
            item != nullptr) {
            item->setData(kTrashRole, true);
        }
        applyTrashSummary();

        // Watched so the count follows trashing and emptying, from here or
        // from anything else on the desktop.
        if (m_trashWatcher == nullptr) {
            m_trashWatcher = new QFileSystemWatcher(this);
            connect(m_trashWatcher, &QFileSystemWatcher::directoryChanged, m_trashSummaryTimer,
                    qOverload<>(&QTimer::start));
            m_trashWatcher->addPath(trash);
            refreshTrashSummary();
        }
    }

    if (!m_pinned.isEmpty()) {
        addHeading(tr("Pinned"));
        for (const QString &path : std::as_const(m_pinned)) {
            // A pinned Downloads still looks like Downloads; anything else is
            // a folder. The heading already says these are pinned, so a star
            // on each would say it again.
            addPlace(QFileInfo(path).fileName(), path, iconNameForPath(path), true);
        }
    }

    addDevices();

    clearHighlight();

    m_populated = true;
}

void Sidebar::clearHighlight()
{
    // Both, and in this order. The current row and the selection are separate
    // things in an item view: clearing the current index leaves a selected row
    // still painted, which is what kept "Home" highlighted after the current
    // index had already been dropped.
    m_list->clearSelection();
    m_list->setCurrentRow(-1);
}

bool Sidebar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list && event->type() == QEvent::FocusOut) {
        clearHighlight();
    }
    if (watched == m_list->viewport() && handleViewportEvent(event)) {
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

bool Sidebar::handleViewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        const QPoint position = mouse->position().toPoint();
        QListWidgetItem *item = m_list->itemAt(position);
        if (item == nullptr) {
            return false;
        }
        // The eject button: unmount, and do not also open the drive.
        if (item->data(kEjectRole).toBool() &&
            ejectRect(m_list->visualItemRect(item)).adjusted(-4, -4, 4, 4).contains(position)) {
            unmountVolume(item->data(kVolumeIdRole).toString());
            return true;
        }
        // Places can be dragged out; devices come and go by themselves.
        if (!item->data(kPathRole).toString().isEmpty() &&
            item->data(kVolumeIdRole).toString().isEmpty()) {
            m_dragCandidate = item;
            m_dragStart = position;
        }
        return false;
    }
    case QEvent::MouseMove: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_dragCandidate == nullptr || (mouse->buttons() & Qt::LeftButton) == 0) {
            return false;
        }
        if ((mouse->position().toPoint() - m_dragStart).manhattanLength() <
            QApplication::startDragDistance()) {
            return false;
        }
        QListWidgetItem *item = m_dragCandidate;
        m_dragCandidate = nullptr;
        startPlaceDrag(item);
        return true;
    }
    case QEvent::MouseButtonRelease:
        m_dragCandidate = nullptr;
        return false;

    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        const QMimeData *mime = drag->mimeData();
        if (mime->hasFormat(kPlaceMimeType)) {
            // One of our own places, coming back: not a removal.
            m_dragLeftSidebar = false;
            drag->setDropAction(Qt::MoveAction);
            drag->accept();
            return true;
        }
        const bool hasFolder = std::ranges::any_of(mime->urls(), [](const QUrl &url) {
            return url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir();
        });
        if (!hasFolder) {
            return false;
        }
        // A link, as far as the source is concerned: nothing is copied or
        // moved, the folder is only remembered here.
        drag->setDropAction(Qt::CopyAction);
        drag->accept();
        setDropHighlight(true);
        return true;
    }
    case QEvent::DragLeave:
        m_dragLeftSidebar = true;
        setDropHighlight(false);
        return false;

    case QEvent::Drop: {
        auto *drop = static_cast<QDropEvent *>(event);
        setDropHighlight(false);
        const QMimeData *mime = drop->mimeData();
        if (mime->hasFormat(kPlaceMimeType)) {
            drop->setDropAction(Qt::MoveAction);
            drop->accept();
            return true;
        }
        QStringList added;
        for (const QUrl &url : mime->urls()) {
            const QString path = QDir::cleanPath(url.toLocalFile());
            if (!url.isLocalFile() || !QFileInfo(path).isDir()) {
                continue;
            }
            // A built-in place the user once removed comes back as itself
            // rather than as a pinned copy of itself.
            if (m_hidden.removeAll(path) > 0) {
                Q_EMIT hiddenPlacesChanged();
                added << QFileInfo(path).fileName();
                continue;
            }
            if (!m_pinned.contains(path)) {
                m_pinned.append(path);
                added << QFileInfo(path).fileName();
            }
        }
        if (!added.isEmpty()) {
            populate();
            Q_EMIT pinnedPathsChanged();
            Q_EMIT statusMessage(
                tr("Added %1 to the sidebar").arg(added.join(QStringLiteral(", "))));
        }
        drop->setDropAction(Qt::CopyAction);
        drop->accept();
        return true;
    }
    default:
        return false;
    }
}

void Sidebar::startPlaceDrag(QListWidgetItem *item)
{
    // Taken now: the row may be rebuilt while the drag runs, if a device
    // appears, and the pointer to it with it.
    const QString path = item->data(kPathRole).toString();
    const QString title = item->text();

    auto *mime = new QMimeData;
    mime->setData(kPlaceMimeType, path.toUtf8());

    auto *drag = new QDrag(m_list);
    drag->setMimeData(mime);
    const QRect row = m_list->visualItemRect(item);
    drag->setPixmap(m_list->viewport()->grab(row));
    drag->setHotSpot(m_dragStart - row.topLeft());

    m_dragLeftSidebar = false;
    const Qt::DropAction result = drag->exec(Qt::MoveAction);

    // Dropped somewhere that did not want it, having left the sidebar, and not
    // let go over the sidebar itself: taken off. The pointer check is there for
    // a drop on the sidebar's own header, which leaves the list but not the
    // sidebar.
    const bool overSidebar = rect().contains(mapFromGlobal(QCursor::pos()));
    if (result == Qt::IgnoreAction && m_dragLeftSidebar && !overSidebar && removePlace(path)) {
        Q_EMIT statusMessage(
            tr("Removed %1 from the sidebar — drag it back to restore it").arg(title));
    }
}

void Sidebar::setDropHighlight(bool on)
{
    if (m_list->property("dropTarget").toBool() == on) {
        return;
    }
    m_list->setProperty("dropTarget", on);
    m_list->style()->unpolish(m_list);
    m_list->style()->polish(m_list);
}

void Sidebar::showContextMenu(const QPoint &position)
{
    const QListWidgetItem *item = m_list->itemAt(position);
    const QString path = item != nullptr ? item->data(kPathRole).toString() : QString();
    const bool isPlace =
        item != nullptr && !path.isEmpty() && item->data(kVolumeIdRole).toString().isEmpty();

    QMenu menu(this);
    if (isPlace) {
        connect(menu.addAction(tr("Open")), &QAction::triggered, this,
                [this, path] { Q_EMIT placeActivated(path); });
        connect(menu.addAction(tr("Remove from Sidebar")), &QAction::triggered, this,
                [this, path] { removePlace(path); });
    }
    if (item != nullptr && item->data(kEjectRole).toBool()) {
        const QString volumeId = item->data(kVolumeIdRole).toString();
        connect(menu.addAction(tr("Unmount")), &QAction::triggered, this,
                [this, volumeId] { unmountVolume(volumeId); });
    }
    if (!m_hidden.isEmpty()) {
        if (!menu.isEmpty()) {
            menu.addSeparator();
        }
        connect(menu.addAction(tr("Restore Removed Places")), &QAction::triggered, this, [this] {
            m_hidden.clear();
            populate();
            Q_EMIT hiddenPlacesChanged();
        });
    }
    if (!menu.isEmpty()) {
        menu.exec(m_list->viewport()->mapToGlobal(position));
    }
}

bool Sidebar::removePlace(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty()) {
        return false;
    }
    if (m_pinned.contains(cleaned)) {
        togglePin(cleaned);
        return true;
    }
    if (m_hidden.contains(cleaned)) {
        return false;
    }
    m_hidden.append(cleaned);
    if (m_populated) {
        populate();
    }
    Q_EMIT hiddenPlacesChanged();
    return true;
}

QStringList Sidebar::hiddenPlaces() const
{
    return m_hidden;
}

void Sidebar::setHiddenPlaces(const QStringList &paths)
{
    m_hidden.clear();
    for (const QString &path : paths) {
        m_hidden.append(QDir::cleanPath(path));
    }
    if (m_populated) {
        populate();
    }
}

void Sidebar::refreshTrashSummary()
{
    const QString files = fs::Trash().filesDirectory();
    if (files.isEmpty()) {
        return;
    }

    const int generation = ++m_trashSummaryGeneration;
    const QPointer<Sidebar> self(this);
    WorkerPools::acquire("sidebar", 1)->start([self, files, generation] {
        // The count is what is in the wastebasket, as a user would count it:
        // top-level entries. The size is everything under them, links not
        // followed.
        const qsizetype count =
            QDir(files)
                .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
                .size();
        qint64 bytes = 0;
        QDirIterator walk(files, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            walk.next();
            if (const QFileInfo info = walk.fileInfo(); !info.isSymLink()) {
                bytes += info.size();
            }
        }

        QMetaObject::invokeMethod(
            qApp,
            [self, generation, count, bytes] {
                if (self.isNull() || generation != self->m_trashSummaryGeneration) {
                    return;
                }
                self->m_trashSummary = count == 0 ? QString()
                                                  : QStringLiteral("%1 · %2").arg(count).arg(
                                                        formatSize(static_cast<quint64>(bytes)));
                self->applyTrashSummary();
            },
            Qt::QueuedConnection);
    });
}

void Sidebar::applyTrashSummary()
{
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (!item->data(kTrashRole).toBool()) {
            continue;
        }
        item->setData(kDetailRole, m_trashSummary);
        const QString path = QDir::toNativeSeparators(item->data(kPathRole).toString());
        item->setToolTip(m_trashSummary.isEmpty() ? tr("%1 — empty").arg(path)
                                                  : tr("%1 — %2").arg(path, m_trashSummary));
    }
}

void Sidebar::addDevices()
{
    // §3.4: nothing here until startWatchingDevices() has been called, which is
    // what keeps a D-Bus connection off the startup path.
    if (m_volumes == nullptr) {
        return;
    }

    const QList<platform::Volume> volumes = m_volumes->volumes();
    if (volumes.isEmpty()) {
        return;
    }

    addHeading(tr("Devices"));

    for (const platform::Volume &volume : volumes) {
        // §7.11: "a mount state indicator". The drive glyph for every volume,
        // drawn a step back — glyph in `overlay`, label in `subtext` — when it
        // is not mounted. Two signals rather than one, so the state survives
        // someone who cannot tell the two greys apart. The ●/○ markers this
        // replaces said the same thing in whatever font had the glyphs.
        auto *item = new QListWidgetItem(volume.name, m_list);
        item->setData(kIsHeadingRole, false);
        item->setData(kVolumeIdRole, volume.id);
        item->setData(kPathRole, volume.mountPoint);
        item->setData(kIconNameRole, QString(kDeviceIcon));
        item->setData(kToneRole,
                      static_cast<int>(volume.isMounted ? Tone::Place : Tone::Unmounted));
        item->setData(kEjectRole, volume.isMounted && volume.canUnmount);
        item->setToolTip(volume.isMounted
                             ? tr("%1 — mounted at %2").arg(volume.device, volume.mountPoint)
                             : tr("%1 — not mounted").arg(volume.device));
        applyItemTheme(item);
    }
}

void Sidebar::startWatchingDevices()
{
    if (m_volumes != nullptr) {
        return;
    }

    m_volumes = platform::VolumeMonitor::create(this);

    connect(m_volumes.get(), &platform::VolumeMonitor::volumesChanged, this, [this] {
        if (m_populated) {
            populate();
        }
    });

    connect(m_volumes.get(), &platform::VolumeMonitor::mounted, this,
            [this](const QString &id, const QString &mountPoint) {
                m_mounting.remove(id);
                // §7.11: "Enter mounts … and navigates."
                if (!mountPoint.isEmpty()) {
                    Q_EMIT placeActivated(mountPoint);
                }
            });

    connect(m_volumes.get(), &platform::VolumeMonitor::operationFailed, this,
            [this](const QString &id, const QString &reason) {
                m_mounting.remove(id);
                Q_EMIT statusMessage(reason);
            });

    m_volumes->start();
}

void Sidebar::unmountCurrentVolume()
{
    unmountVolume(currentVolumeId());
}

void Sidebar::unmountVolume(const QString &volumeId)
{
    if (volumeId.isEmpty() || m_volumes == nullptr) {
        return;
    }

    Q_EMIT statusMessage(tr("Unmounting…"));
    m_volumes->unmount(volumeId);
}

QString Sidebar::currentVolumeId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item == nullptr ? QString() : item->data(kVolumeIdRole).toString();
}

bool Sidebar::togglePin(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty()) {
        return false;
    }

    const bool nowPinned = !m_pinned.contains(cleaned);
    if (nowPinned) {
        m_pinned.append(cleaned);
    } else {
        m_pinned.removeAll(cleaned);
    }

    if (m_populated) {
        populate();
    }
    Q_EMIT pinnedPathsChanged();
    return nowPinned;
}

bool Sidebar::isPinned(const QString &path) const
{
    return m_pinned.contains(QDir::cleanPath(path));
}

QStringList Sidebar::pinnedPaths() const
{
    return m_pinned;
}

void Sidebar::setPinnedPaths(const QStringList &paths)
{
    m_pinned = paths;
    if (m_populated) {
        populate();
    }
}

QString Sidebar::currentPath() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item == nullptr ? QString() : item->data(kPathRole).toString();
}

} // namespace pf::ui
