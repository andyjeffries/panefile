#include "ui/Sidebar.h"

#include "core/Logging.h"
#include "platform/Paths.h"
#include "ui/ThemePalette.h"

#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace pf::ui {
namespace {

/// Marks a row as a heading rather than a place, so it can be skipped when the
/// cursor moves and rendered differently.
constexpr int kIsHeadingRole = Qt::UserRole + 1;
constexpr int kPathRole = Qt::UserRole + 2;

} // namespace

Sidebar::Sidebar(QWidget *parent) : QWidget(parent), m_list(new QListWidget(this))
{
    setObjectName(QStringLiteral("sidebar"));
    setMinimumWidth(140);
    setMaximumWidth(280);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_list->setObjectName(QStringLiteral("sidebarList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list);

    const ThemePalette &theme = currentPalette();
    QPalette widgetPalette = palette();
    widgetPalette.setColor(QPalette::Base, theme.background);
    widgetPalette.setColor(QPalette::Window, theme.background);
    widgetPalette.setColor(QPalette::Text, theme.subtext);
    widgetPalette.setColor(QPalette::Highlight, theme.cursorBackground);
    widgetPalette.setColor(QPalette::HighlightedText, theme.text);
    setAutoFillBackground(true);
    setPalette(widgetPalette);
    m_list->setPalette(widgetPalette);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item == nullptr || item->data(kIsHeadingRole).toBool()) {
            return;
        }
        const QString path = item->data(kPathRole).toString();
        if (!path.isEmpty()) {
            Q_EMIT placeActivated(path);
        }
    });
}

void Sidebar::addHeading(const QString &title)
{
    auto *item = new QListWidgetItem(title, m_list);
    item->setData(kIsHeadingRole, true);
    item->setFlags(Qt::NoItemFlags);
    item->setForeground(currentPalette().overlay);
}

void Sidebar::addPlace(const QString &title, const QString &path)
{
    // Only places that exist. An XDG user directory pointing at something the
    // user deleted would otherwise sit in the sidebar failing to open.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }

    auto *item = new QListWidgetItem(title, m_list);
    item->setData(kPathRole, path);
    item->setData(kIsHeadingRole, false);
    item->setToolTip(QDir::toNativeSeparators(path));
}

void Sidebar::populate()
{
    m_list->clear();

    addPlace(tr("Home"), QDir::homePath());

    // §5.1's XDG user dirs. QStandardPaths reads user-dirs.dirs on Linux and
    // the native locations on macOS, so one call covers both platforms.
    struct Place {
        QStandardPaths::StandardLocation location;
        QString title;
    };
    const QList<Place> places{
        {.location = QStandardPaths::DesktopLocation, .title = tr("Desktop")},
        {.location = QStandardPaths::DownloadLocation, .title = tr("Downloads")},
        {.location = QStandardPaths::DocumentsLocation, .title = tr("Documents")},
        {.location = QStandardPaths::PicturesLocation, .title = tr("Pictures")},
        {.location = QStandardPaths::MusicLocation, .title = tr("Music")},
        {.location = QStandardPaths::MoviesLocation, .title = tr("Videos")},
    };

    for (const Place &place : places) {
        const QString path = QStandardPaths::writableLocation(place.location);
        // QStandardPaths falls back to the home directory for locations that
        // are not configured; listing "Documents" that opens $HOME is worse
        // than not listing it.
        if (path != QDir::homePath()) {
            addPlace(place.title, path);
        }
    }

    if (!m_pinned.isEmpty()) {
        addHeading(tr("Pinned"));
        for (const QString &path : std::as_const(m_pinned)) {
            addPlace(QFileInfo(path).fileName(), path);
        }
    }

    // Devices are deliberately absent until M8. §3.4 is emphatic that a D-Bus
    // connection must never be opened at startup — udisks2 plus
    // GetManagedObjects is 20–40 ms of pure waste for a user who never touches
    // removable media — so the section appears when it first becomes visible.

    m_populated = true;
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
