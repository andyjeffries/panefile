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
#include <QMimeDatabase>
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

/// The id of the section a heading, entry or placeholder belongs to.
constexpr int kSectionIdRole = Qt::UserRole + 9;

/// The wastebasket's row, so its summary can be written into it.
constexpr int kTrashRole = Qt::UserRole + 10;

/// An entry's position in its section's list.
constexpr int kEntryIndexRole = Qt::UserRole + 11;

/// The faint "Drag files or folders here" row of an empty section.
constexpr int kPlaceholderRole = Qt::UserRole + 12;

/// An entry that is a file: clicking it reveals it rather than opening it.
constexpr int kFileRole = Qt::UserRole + 13;

/// An entry or section being dragged around or out of the sidebar. Its own
/// type, not a file URL: a place dropped on a panel must not be taken as "copy
/// Downloads in here".
constexpr QLatin1String kPlaceMimeType("application/x-panefile-sidebar-place");

constexpr int kDropLineHeight = 2;

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

        if (index.data(kPlaceholderRole).toBool()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            QPen pen(palette.border);
            pen.setStyle(Qt::DashLine);
            painter->setPen(pen);
            const QRectF box = QRectF(option.rect).adjusted(2.5, 2.5, -2.5, -2.5);
            painter->drawRoundedRect(box, 6, 6);
            painter->setPen(palette.overlay);
            painter->drawText(box, Qt::AlignCenter,
                              option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                            Qt::ElideRight,
                                                            static_cast<int>(box.width()) - 8));
            painter->restore();
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

    // The first section's heading row carries its title, so the header is only
    // the application menu's button, right-aligned.
    headerLayout->addStretch(1);

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

    // The line a drop will land on. A child of the viewport so it scrolls
    // with nothing and paints over the rows.
    m_dropLine = new QWidget(m_list->viewport());
    m_dropLine->setFixedHeight(kDropLineHeight);
    m_dropLine->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropLine->setAutoFillBackground(true);
    m_dropLine->hide();
    {
        QPalette linePalette = m_dropLine->palette();
        linePalette.setColor(QPalette::Window, currentPalette().borderFocused);
        m_dropLine->setPalette(linePalette);
    }

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
    QPalette linePalette = m_dropLine->palette();
    linePalette.setColor(QPalette::Window, currentPalette().borderFocused);
    m_dropLine->setPalette(linePalette);
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
    if (!path.isEmpty() && item->data(kFileRole).toBool()) {
        clearHighlight();
        Q_EMIT revealRequested(path);
        return;
    }
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

void Sidebar::addHeading(const QString &title, const QString &sectionId)
{
    auto *item = new QListWidgetItem(title, m_list);
    item->setData(kSectionIdRole, sectionId);
    item->setData(kIsHeadingRole, true);
    item->setData(kToneRole, static_cast<int>(Tone::Heading));
    item->setFlags(Qt::NoItemFlags);

    // Small bold capitals, and a gap above every heading but the first, so one
    // section's end and the next one's start are not the same grey.
    QFont font = m_list->font();
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::PercentageSpacing, 104);
    if (font.pixelSize() > 0) {
        font.setPixelSize(std::max(1, font.pixelSize() - 1));
    } else if (font.pointSizeF() > 1) {
        font.setPointSizeF(font.pointSizeF() - 1);
    }
    item->setFont(font);
    if (m_list->count() > 1) {
        item->setSizeHint(QSize(0, 44));
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);
    }
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
                                   const QString &iconName)
{
    // Only places that exist. An entry on an unmounted drive stays in the
    // layout and comes back when the drive does.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return nullptr;
    }

    auto *item = new QListWidgetItem(title, m_list);
    item->setData(kPathRole, path);
    item->setData(kIsHeadingRole, false);
    item->setData(kIconNameRole, iconName);
    item->setData(kToneRole, static_cast<int>(Tone::Place));
    item->setToolTip(QDir::toNativeSeparators(path));
    applyItemTheme(item);
    return item;
}

void Sidebar::addPlaceholder(const QString &sectionId)
{
    auto *item = new QListWidgetItem(tr("Drag files or folders here"), m_list);
    item->setData(kIsHeadingRole, true);
    item->setData(kPlaceholderRole, true);
    item->setData(kSectionIdRole, sectionId);
    item->setData(kToneRole, static_cast<int>(Tone::Heading));
    item->setFlags(Qt::NoItemFlags);
    item->setSizeHint(QSize(0, 32));
    applyItemTheme(item);
}

QString Sidebar::iconNameForEntry(const QString &path)
{
    if (QFileInfo(path).isDir()) {
        return iconNameForPath(path);
    }
    const QString mime =
        QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
    if (mime.startsWith(QLatin1String("image/"))) {
        return QStringLiteral("photo");
    }
    if (mime.startsWith(QLatin1String("video/"))) {
        return QStringLiteral("film");
    }
    if (mime.startsWith(QLatin1String("audio/"))) {
        return QStringLiteral("musical-note");
    }
    return QStringLiteral("document-text");
}

void Sidebar::populate()
{
    m_dragCandidate = nullptr;
    m_list->clear();

    const auto builtinTitle = [](const QString &key) {
        if (key == QLatin1String("home")) {
            return tr("Home");
        }
        if (key == QLatin1String("desktop")) {
            return tr("Desktop");
        }
        if (key == QLatin1String("downloads")) {
            return tr("Downloads");
        }
        if (key == QLatin1String("documents")) {
            return tr("Documents");
        }
        if (key == QLatin1String("pictures")) {
            return tr("Pictures");
        }
        if (key == QLatin1String("music")) {
            return tr("Music");
        }
        return tr("Videos");
    };

    for (const SidebarSection &section : std::as_const(m_layout.sections)) {
        addHeading(section.title, section.id);
        if (section.entries.isEmpty()) {
            addPlaceholder(section.id);
            continue;
        }
        for (int i = 0; i < section.entries.size(); ++i) {
            const SidebarEntry &entry = section.entries.at(i);
            const QString path = entry.resolvedPath();
            QListWidgetItem *item = nullptr;
            if (!entry.builtin.isEmpty()) {
                item = addPlace(builtinTitle(entry.builtin), path, iconNameForPath(path));
            } else {
                item = addPlace(QFileInfo(path).fileName(), path, iconNameForEntry(path));
                if (item != nullptr && !QFileInfo(path).isDir()) {
                    item->setData(kFileRole, true);
                }
            }
            if (item != nullptr) {
                item->setData(kSectionIdRole, section.id);
                item->setData(kEntryIndexRole, i);
            }
        }
    }

    // The wastebasket, under a divider: a place, but not one of yours, as
    // GNOME Files and Finder both set it apart. It opens where the trashed
    // items are — XDG's files/, or Finder's ~/.Trash itself — and that is
    // created if nothing has been trashed yet, as the XDG trash spec allows,
    // so the entry opens onto an empty folder rather than an error.
    if (const QString trash = fs::Trash().filesDirectory();
        !trash.isEmpty() && !m_layout.wastebasketHidden) {
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

namespace {

/// What a drag from the sidebar's own rows carries; see Sidebar::entryPayload().
struct DragPayload {
    enum Kind { Invalid, Entry, Section, Trash } kind = Invalid;
    QString sectionId;
    int index = -1;
    QString path;
};

DragPayload parsePayload(const QMimeData *mime)
{
    DragPayload payload;
    const QStringList parts =
        QString::fromUtf8(mime->data(kPlaceMimeType)).split(QLatin1Char('\n'));
    if (parts.value(0) == QLatin1String("entry") && parts.size() >= 4) {
        payload.kind = DragPayload::Entry;
        payload.sectionId = parts.at(1);
        payload.index = parts.at(2).toInt();
        // The path may itself contain a newline; it is the rest of the data.
        payload.path = parts.mid(3).join(QLatin1Char('\n'));
    } else if (parts.value(0) == QLatin1String("section") && parts.size() >= 2) {
        payload.kind = DragPayload::Section;
        payload.sectionId = parts.at(1);
    } else if (parts.value(0) == QLatin1String("trash")) {
        payload.kind = DragPayload::Trash;
    }
    return payload;
}

bool isLocalExisting(const QUrl &url)
{
    return url.isLocalFile() && QFileInfo::exists(url.toLocalFile());
}

} // namespace

QLatin1String Sidebar::placeMimeType()
{
    return kPlaceMimeType;
}

QByteArray Sidebar::entryPayload(const QString &sectionId, int index, const QString &path)
{
    return QStringLiteral("entry\n%1\n%2\n%3").arg(sectionId).arg(index).arg(path).toUtf8();
}

QByteArray Sidebar::sectionPayload(const QString &sectionId)
{
    return QStringLiteral("section\n%1").arg(sectionId).toUtf8();
}

QByteArray Sidebar::trashPayload()
{
    return QByteArrayLiteral("trash");
}

Sidebar::DropTarget Sidebar::dropTargetAt(const QPoint &position, bool draggingSection) const
{
    struct Block {
        int top = -1;
        int bottom = -1; ///< Exclusive.
    };
    QList<Block> blocks(m_layout.sections.size());
    for (int row = 0; row < m_list->count(); ++row) {
        const QListWidgetItem *item = m_list->item(row);
        const int section = m_layout.sectionIndex(item->data(kSectionIdRole).toString());
        if (section < 0) {
            continue;
        }
        const QRect rect = m_list->visualItemRect(item);
        Block &block = blocks[section];
        if (block.top < 0) {
            block.top = rect.top();
        }
        block.bottom = rect.bottom() + 1;
    }

    DropTarget target;
    if (m_layout.sections.isEmpty()) {
        // Nothing to land between: a drop makes the first section.
        target.kind = DropTarget::Entry;
        return target;
    }

    if (draggingSection) {
        // The gap before or after the block the pointer is over: whichever
        // half of the block it is in.
        target.kind = DropTarget::Section;
        target.index = static_cast<int>(blocks.size());
        target.lineY = blocks.last().bottom;
        for (int i = 0; i < blocks.size(); ++i) {
            if (blocks.at(i).top >= 0 &&
                position.y() < (blocks.at(i).top + blocks.at(i).bottom) / 2) {
                target.index = i;
                target.lineY = blocks.at(i).top;
                break;
            }
        }
        return target;
    }

    target.kind = DropTarget::Entry;
    const QListWidgetItem *hit = m_list->itemAt(position);
    const int section =
        hit != nullptr ? m_layout.sectionIndex(hit->data(kSectionIdRole).toString()) : -1;
    if (hit != nullptr && section >= 0) {
        target.section = section;
        const QRect rect = m_list->visualItemRect(hit);
        if (hit->data(kPlaceholderRole).toBool()) {
            target.index = 0;
            target.lineY = rect.top();
        } else if (hit->data(kIsHeadingRole).toBool()) {
            target.index = 0;
            target.lineY = rect.bottom() + 1;
        } else {
            const bool upper = position.y() < rect.center().y();
            target.index = hit->data(kEntryIndexRole).toInt() + (upper ? 0 : 1);
            target.lineY = upper ? rect.top() : rect.bottom() + 1;
        }
        return target;
    }

    // Below the last section, or over the divider, wastebasket or a device:
    // the end of the last section.
    target.section = static_cast<int>(m_layout.sections.size()) - 1;
    target.index = static_cast<int>(m_layout.sections.last().entries.size());
    target.lineY = blocks.last().bottom;
    return target;
}

void Sidebar::showDropLine(int y)
{
    const int inset = 8;
    m_dropLine->setGeometry(inset, y - kDropLineHeight / 2, m_list->viewport()->width() - 2 * inset,
                            kDropLineHeight);
    m_dropLine->raise();
    m_dropLine->show();
}

void Sidebar::hideDropLine()
{
    m_dropLine->hide();
}

void Sidebar::commitLayout(const QString &keepPath)
{
    if (m_populated) {
        populate();
    }
    if (!keepPath.isEmpty()) {
        for (int row = 0; row < m_list->count(); ++row) {
            const QListWidgetItem *item = m_list->item(row);
            if (!item->data(kSectionIdRole).toString().isEmpty() &&
                item->data(kPathRole).toString() == keepPath) {
                m_list->setCurrentRow(row);
                break;
            }
        }
    }
    Q_EMIT layoutChanged();
}

bool Sidebar::handleViewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
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
        // Entries, the wastebasket and section headings can be dragged;
        // devices come and go by themselves.
        const bool isEntry = !item->data(kPathRole).toString().isEmpty() &&
                             item->data(kVolumeIdRole).toString().isEmpty();
        const bool isHeading = item->data(kIsHeadingRole).toBool() &&
                               !item->data(kPlaceholderRole).toBool() &&
                               !item->data(kSectionIdRole).toString().isEmpty();
        if (isEntry || isHeading) {
            m_dragCandidate = item;
            m_dragStart = position;
        }
        return false;
    }
    case QEvent::MouseMove: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
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
        const QPoint position = drag->position().toPoint();
        if (mime->hasFormat(kPlaceMimeType)) {
            // One of our own rows, being arranged: not a removal.
            m_dragLeftSidebar = false;
            const DragPayload payload = parsePayload(mime);
            const DropTarget target = dropTargetAt(position, payload.kind == DragPayload::Section);
            if (payload.kind == DragPayload::Trash || target.kind == DropTarget::None ||
                (target.kind == DropTarget::Entry && target.section < 0)) {
                hideDropLine();
            } else {
                showDropLine(target.lineY);
            }
            drag->setDropAction(Qt::MoveAction);
            drag->accept();
            return true;
        }
        if (!std::ranges::any_of(mime->urls(), isLocalExisting)) {
            return false;
        }
        const DropTarget target = dropTargetAt(position, false);
        if (target.section >= 0) {
            showDropLine(target.lineY);
        }
        // A link, as far as the source is concerned: nothing is copied or
        // moved, the path is only remembered here.
        drag->setDropAction(Qt::CopyAction);
        drag->accept();
        setDropHighlight(true);
        return true;
    }
    case QEvent::DragLeave:
        m_dragLeftSidebar = true;
        setDropHighlight(false);
        hideDropLine();
        return false;

    case QEvent::Drop: {
        auto *drop = static_cast<QDropEvent *>(event);
        setDropHighlight(false);
        hideDropLine();
        const QMimeData *mime = drop->mimeData();
        const QPoint position = drop->position().toPoint();

        if (mime->hasFormat(kPlaceMimeType)) {
            const DragPayload payload = parsePayload(mime);
            const SidebarLayout before = m_layout;
            QString keep;
            if (payload.kind == DragPayload::Section) {
                const DropTarget target = dropTargetAt(position, true);
                const int from = m_layout.sectionIndex(payload.sectionId);
                if (from >= 0 && target.kind == DropTarget::Section) {
                    m_layout.moveSection(from, target.index);
                }
            } else if (payload.kind == DragPayload::Entry) {
                const DropTarget target = dropTargetAt(position, false);
                const int fromSection = m_layout.sectionIndex(payload.sectionId);
                int fromIndex = payload.index;
                // The payload's index is checked against the path, so a layout
                // that changed under the drag cannot move the wrong entry.
                if (fromSection >= 0 &&
                    (fromIndex < 0 ||
                     fromIndex >= m_layout.sections.at(fromSection).entries.size() ||
                     QDir::cleanPath(
                         m_layout.sections.at(fromSection).entries.at(fromIndex).resolvedPath()) !=
                         QDir::cleanPath(payload.path))) {
                    fromIndex = -1;
                }
                if (fromSection >= 0 && fromIndex >= 0 && target.section >= 0) {
                    m_layout.moveEntry(fromSection, fromIndex, target.section, target.index);
                    keep = payload.path;
                }
            }
            if (m_layout != before) {
                commitLayout(keep);
                Q_EMIT statusMessage(payload.kind == DragPayload::Section
                                         ? tr("Moved the section")
                                         : tr("Moved %1").arg(QFileInfo(payload.path).fileName()));
            }
            drop->setDropAction(Qt::MoveAction);
            drop->accept();
            return true;
        }

        DropTarget target = dropTargetAt(position, false);
        QStringList added;
        const SidebarLayout before = m_layout;
        for (const QUrl &url : mime->urls()) {
            if (!isLocalExisting(url)) {
                continue;
            }
            const QString path = QDir::cleanPath(url.toLocalFile());
            if (target.section < 0) {
                m_layout.addSection(tr("Pinned"));
                target.section = 0;
                target.index = 0;
            }
            const SidebarEntry entry = SidebarLayout::entryFor(path);
            m_layout.insertEntry(target.section, target.index, entry);
            // The entry may have been moved from earlier in this section, so
            // the next one goes after where it actually landed.
            if (const auto landed = m_layout.find(QDir::cleanPath(entry.resolvedPath()))) {
                target.index = landed->second + 1;
            }
            added << QFileInfo(path).fileName();
        }
        if (m_layout != before) {
            commitLayout();
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
    // Taken now: the rows are rebuilt while the drag runs if a device appears,
    // and the pointers to them go with them. Ids and paths survive that.
    const QString path = item->data(kPathRole).toString();
    const QString title = item->text();
    const QString sectionId = item->data(kSectionIdRole).toString();
    const bool isSection = item->data(kIsHeadingRole).toBool();
    const bool isTrash = item->data(kTrashRole).toBool();

    auto *mime = new QMimeData;
    QByteArray payload;
    if (isSection) {
        payload = sectionPayload(sectionId);
    } else if (isTrash) {
        payload = trashPayload();
    } else {
        payload = entryPayload(sectionId, item->data(kEntryIndexRole).toInt(), path);
    }
    mime->setData(kPlaceMimeType, payload);

    auto *drag = new QDrag(m_list);
    drag->setMimeData(mime);
    QRect row = m_list->visualItemRect(item);
    if (isSection) {
        // The whole block, so it is plain that the section is what moves.
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(kSectionIdRole).toString() == sectionId) {
                row = row.united(m_list->visualItemRect(m_list->item(i)));
            }
        }
    }
    drag->setPixmap(m_list->viewport()->grab(row));
    drag->setHotSpot(m_dragStart - row.topLeft());

    m_dragLeftSidebar = false;
    const Qt::DropAction result = drag->exec(Qt::MoveAction);
    hideDropLine();

    // Dropped somewhere that did not want it, having left the sidebar, and not
    // let go over the sidebar itself: taken off. The pointer check is there for
    // a drop on the sidebar's own header, which leaves the list but not the
    // sidebar.
    const bool overSidebar = rect().contains(mapFromGlobal(QCursor::pos()));
    if (result != Qt::IgnoreAction || !m_dragLeftSidebar || overSidebar) {
        return;
    }
    if (isSection) {
        requestSectionRemoval(sectionId);
    } else if (removePlace(path)) {
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
    const QString sectionId = item != nullptr ? item->data(kSectionIdRole).toString() : QString();
    const bool isPlace =
        item != nullptr && !path.isEmpty() && item->data(kVolumeIdRole).toString().isEmpty();
    const bool isUserEntry = isPlace && !sectionId.isEmpty();
    const bool isHeading = item != nullptr && item->data(kIsHeadingRole).toBool() &&
                           !item->data(kPlaceholderRole).toBool() && !sectionId.isEmpty();

    QMenu menu(this);
    if (isPlace) {
        const bool isFile = item->data(kFileRole).toBool();
        connect(menu.addAction(tr("Open")), &QAction::triggered, this, [this, isFile, path] {
            if (isFile) {
                Q_EMIT revealRequested(path);
            } else {
                Q_EMIT placeActivated(path);
            }
        });
        connect(menu.addAction(tr("Remove from Sidebar")), &QAction::triggered, this,
                [this, path] { removePlace(path); });
    }
    if (isUserEntry) {
        menu.addSeparator();
        connect(menu.addAction(tr("Move Up")), &QAction::triggered, this,
                [this, path] { moveEntryByPath(path, -1); });
        connect(menu.addAction(tr("Move Down")), &QAction::triggered, this,
                [this, path] { moveEntryByPath(path, 1); });
        QMenu *sections = menu.addMenu(tr("Move to Section"));
        for (const SidebarSection &section : std::as_const(m_layout.sections)) {
            if (section.id == sectionId) {
                continue;
            }
            const QString id = section.id;
            connect(sections->addAction(section.title), &QAction::triggered, this,
                    [this, path, id] {
                        const int to = m_layout.sectionIndex(id);
                        const auto from = m_layout.find(QDir::cleanPath(path));
                        if (to >= 0 && from) {
                            m_layout.moveEntry(
                                from->first, from->second, to,
                                static_cast<int>(m_layout.sections.at(to).entries.size()));
                            commitLayout(path);
                        }
                    });
        }
        if (!sections->isEmpty()) {
            sections->addSeparator();
        }
        connect(sections->addAction(tr("New Section…")), &QAction::triggered, this,
                [this, path] { Q_EMIT newSectionRequested(path); });
    }
    if (isHeading) {
        const int index = m_layout.sectionIndex(sectionId);
        const QString title = index >= 0 ? m_layout.sections.at(index).title : QString();
        connect(menu.addAction(tr("Rename Section…")), &QAction::triggered, this,
                [this, sectionId, title] { Q_EMIT renameSectionRequested(sectionId, title); });
        connect(menu.addAction(tr("Move Section Up")), &QAction::triggered, this,
                [this, sectionId] { moveSectionBy(sectionId, -1); });
        connect(menu.addAction(tr("Move Section Down")), &QAction::triggered, this,
                [this, sectionId] { moveSectionBy(sectionId, 1); });
        menu.addSeparator();
        connect(menu.addAction(tr("Remove Section")), &QAction::triggered, this,
                [this, sectionId] { requestSectionRemoval(sectionId); });
    }
    if (item != nullptr && item->data(kEjectRole).toBool()) {
        const QString volumeId = item->data(kVolumeIdRole).toString();
        connect(menu.addAction(tr("Unmount")), &QAction::triggered, this,
                [this, volumeId] { unmountVolume(volumeId); });
    }

    if (!menu.isEmpty()) {
        menu.addSeparator();
    }
    connect(menu.addAction(tr("New Section…")), &QAction::triggered, this,
            [this] { Q_EMIT newSectionRequested(QString()); });
    if (hasMissingDefaults()) {
        connect(menu.addAction(tr("Restore Default Places")), &QAction::triggered, this,
                [this] { restoreDefaultPlaces(); });
    }
    menu.exec(m_list->viewport()->mapToGlobal(position));
}

void Sidebar::setLayout(const SidebarLayout &layout)
{
    m_layout = layout;
    if (m_populated) {
        populate();
    }
}

bool Sidebar::removePlace(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty()) {
        return false;
    }
    if (const QString trash = fs::Trash().filesDirectory();
        !trash.isEmpty() && QDir::cleanPath(trash) == cleaned) {
        if (m_layout.wastebasketHidden) {
            return false;
        }
        m_layout.wastebasketHidden = true;
        commitLayout();
        return true;
    }
    const auto found = m_layout.find(cleaned);
    if (!found) {
        return false;
    }
    m_layout.removeEntry(found->first, found->second);
    commitLayout();
    return true;
}

void Sidebar::requestSectionRemoval(const QString &id)
{
    const int index = m_layout.sectionIndex(id);
    if (index < 0) {
        return;
    }
    const SidebarSection &section = m_layout.sections.at(index);
    if (section.entries.isEmpty()) {
        removeSection(id);
    } else {
        Q_EMIT sectionRemovalRequested(id, section.title, static_cast<int>(section.entries.size()));
    }
}

void Sidebar::removeSection(const QString &id)
{
    const int index = m_layout.sectionIndex(id);
    if (index < 0) {
        return;
    }
    m_layout.sections.removeAt(index);
    commitLayout();
}

void Sidebar::addSection(const QString &title)
{
    const QString cleaned = SidebarLayout::cleanTitle(title);
    if (cleaned.isEmpty()) {
        return;
    }
    m_layout.addSection(cleaned);
    commitLayout();
}

void Sidebar::addSectionWith(const QString &title, const QString &path)
{
    const QString cleaned = SidebarLayout::cleanTitle(title);
    if (cleaned.isEmpty()) {
        return;
    }
    const QString id = m_layout.addSection(cleaned);
    const auto from = m_layout.find(QDir::cleanPath(path));
    if (from) {
        m_layout.moveEntry(from->first, from->second, m_layout.sectionIndex(id), 0);
    }
    commitLayout(path);
}

void Sidebar::renameSection(const QString &id, const QString &title)
{
    const int index = m_layout.sectionIndex(id);
    const QString cleaned = SidebarLayout::cleanTitle(title);
    if (index < 0 || cleaned.isEmpty() || m_layout.sections.at(index).title == cleaned) {
        return;
    }
    m_layout.sections[index].title = cleaned;
    commitLayout();
}

void Sidebar::moveSectionBy(const QString &id, int delta)
{
    const int index = m_layout.sectionIndex(id);
    const int to = index + delta;
    if (index < 0 || to < 0 || to >= m_layout.sections.size()) {
        return;
    }
    // moveSection takes a gap index: one past the neighbour when moving down.
    m_layout.moveSection(index, delta > 0 ? to + 1 : to);
    commitLayout();
}

void Sidebar::moveEntryByPath(const QString &path, int delta)
{
    const auto found = m_layout.find(QDir::cleanPath(path));
    if (!found) {
        return;
    }
    const auto [section, index] = *found;
    const int size = static_cast<int>(m_layout.sections.at(section).entries.size());
    if (delta < 0) {
        if (index > 0) {
            m_layout.moveEntry(section, index, section, index - 1);
        } else if (section > 0) {
            m_layout.moveEntry(section, index, section - 1,
                               static_cast<int>(m_layout.sections.at(section - 1).entries.size()));
        } else {
            return;
        }
    } else {
        if (index < size - 1) {
            m_layout.moveEntry(section, index, section, index + 2);
        } else if (section + 1 < m_layout.sections.size()) {
            m_layout.moveEntry(section, index, section + 1, 0);
        } else {
            return;
        }
    }
    commitLayout(path);
}

void Sidebar::moveCurrentEntry(int delta)
{
    if (!canMoveCurrentEntry()) {
        return;
    }
    moveEntryByPath(m_list->currentItem()->data(kPathRole).toString(), delta < 0 ? -1 : 1);
}

bool Sidebar::canMoveCurrentEntry() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return m_list->hasFocus() && item != nullptr &&
           !item->data(kSectionIdRole).toString().isEmpty() &&
           !item->data(kPathRole).toString().isEmpty();
}

bool Sidebar::hasMissingDefaults() const
{
    if (m_layout.wastebasketHidden) {
        return true;
    }
    for (const QString &key : SidebarLayout::builtinKeys()) {
        const QString location = SidebarLayout::builtinPath(key);
        if (!location.isEmpty() && !m_layout.find(QDir::cleanPath(location))) {
            return true;
        }
    }
    return false;
}

void Sidebar::restoreDefaultPlaces()
{
    if (m_layout.sections.isEmpty()) {
        m_layout.addSection(tr("Favourites"));
    }
    for (const QString &key : SidebarLayout::builtinKeys()) {
        const QString location = SidebarLayout::builtinPath(key);
        if (!location.isEmpty() && !m_layout.find(QDir::cleanPath(location))) {
            m_layout.insertEntry(0, static_cast<int>(m_layout.sections.first().entries.size()),
                                 {.builtin = key, .path = {}});
        }
    }
    m_layout.wastebasketHidden = false;
    commitLayout();
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
        // top-level entries, less Finder's own .DS_Store. The size is
        // everything under them, links not followed.
        qsizetype count = 0;
        qint64 bytes = 0;
        const QFileInfoList entries = QDir(files).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            if (fs::Trash::isBookkeeping(entry.fileName())) {
                continue;
            }
            ++count;
            if (entry.isSymLink()) {
                continue;
            }
            if (!entry.isDir()) {
                bytes += entry.size();
                continue;
            }
            QDirIterator walk(entry.absoluteFilePath(),
                              QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
            while (walk.hasNext()) {
                walk.next();
                if (const QFileInfo info = walk.fileInfo(); !info.isSymLink()) {
                    bytes += info.size();
                }
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

    if (const auto found = m_layout.find(cleaned)) {
        m_layout.removeEntry(found->first, found->second);
        commitLayout();
        return false;
    }

    int section = -1;
    for (int i = 0; i < m_layout.sections.size(); ++i) {
        if (m_layout.sections.at(i).title == tr("Pinned")) {
            section = i;
            break;
        }
    }
    if (section < 0) {
        section = m_layout.sectionIndex(m_layout.addSection(tr("Pinned")));
    }
    m_layout.insertEntry(section, static_cast<int>(m_layout.sections.at(section).entries.size()),
                         SidebarLayout::entryFor(cleaned));
    commitLayout();
    return true;
}

bool Sidebar::isPinned(const QString &path) const
{
    return m_layout.find(QDir::cleanPath(path)).has_value();
}

QString Sidebar::currentPath() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item == nullptr ? QString() : item->data(kPathRole).toString();
}

} // namespace pf::ui
