#include "ui/Sidebar.h"

#include "core/Logging.h"
#include "fs/Trash.h"
#include "model/SymbolicIcon.h"
#include "platform/Paths.h"
#include "ui/SymbolicWidgets.h"
#include "ui/ThemePalette.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStandardPaths>
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

} // namespace

Sidebar::Sidebar(QWidget *parent) : QWidget(parent), m_list(new QListWidget(this))
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
    // The design's width. A sidebar of shortcuts does not earn more, and at
    // less the longer XDG names start eliding.
    resize(200, height());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // A section label, so the sidebar reads as a sidebar rather than as a
    // column of words sharing an edge with the first panel — with the
    // application menu at the other end of the same row, where GNOME Files
    // keeps its own.
    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 8, 0);
    headerLayout->setSpacing(0);

    auto *section = new QLabel(tr("Favourites"), header);
    section->setObjectName(QStringLiteral("sidebarSection"));
    section->setTextFormat(Qt::PlainText);
    headerLayout->addWidget(section, 1, Qt::AlignBottom);

    auto *menu = new SymbolicButton(QStringLiteral("bars-3"), 16, header);
    menu->setObjectName(QStringLiteral("sidebarMenu"));
    menu->setFocusPolicy(Qt::NoFocus);
    menu->setFixedSize(28, 28);
    menu->setToolTip(tr("Menu"));
    connect(menu, &QPushButton::clicked, this, [this, menu] {
        Q_EMIT menuRequested(menu->mapToGlobal(QPoint(0, menu->height() + 4)));
    });
    headerLayout->addWidget(menu, 0, Qt::AlignBottom);
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
    layout->addWidget(m_list);

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
    item->setFlags(Qt::NoItemFlags);
    item->setSizeHint(QSize(0, 9));

    auto *holder = new QWidget;
    holder->setAttribute(Qt::WA_TranslucentBackground, true);
    auto *layout = new QVBoxLayout(holder);
    layout->setContentsMargins(6, 4, 6, 4);
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("sidebarDivider"));
    line->setFixedHeight(1);
    layout->addWidget(line);
    m_list->setItemWidget(item, holder);
}

void Sidebar::addPlace(const QString &title, const QString &path, const QString &iconName)
{
    // Only places that exist. An XDG user directory pointing at something the
    // user deleted would otherwise sit in the sidebar failing to open.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }

    auto *item = new QListWidgetItem(title, m_list);
    item->setData(kPathRole, path);
    item->setData(kIsHeadingRole, false);
    item->setData(kIconNameRole, iconName);
    item->setData(kToneRole, static_cast<int>(Tone::Place));
    item->setToolTip(QDir::toNativeSeparators(path));
    applyItemTheme(item);
}

void Sidebar::populate()
{
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
    if (const QString trash = fs::Trash().filesDirectory(); !trash.isEmpty()) {
        QDir().mkpath(trash);
        addDivider();
        addPlace(tr("Wastebasket"), trash, QStringLiteral("trash"));
    }

    if (!m_pinned.isEmpty()) {
        addHeading(tr("Pinned"));
        for (const QString &path : std::as_const(m_pinned)) {
            // A pinned Downloads still looks like Downloads; anything else is
            // a folder. The heading already says these are pinned, so a star
            // on each would say it again.
            addPlace(QFileInfo(path).fileName(), path, iconNameForPath(path));
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
    return QWidget::eventFilter(watched, event);
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
    const QString volumeId = currentVolumeId();
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
