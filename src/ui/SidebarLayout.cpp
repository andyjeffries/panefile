#include "ui/SidebarLayout.h"

#include "core/Logging.h"
#include "fs/Trash.h"
#include "platform/Paths.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace pf::ui {
namespace {

constexpr QLatin1String kBuiltinPrefix("builtin:");

QString sidebarPath()
{
    return platform::stateDir() + QStringLiteral("/sidebar.ini");
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(8);
}

QString clean(const QString &path)
{
    return QDir::cleanPath(path);
}

// Paths may hold any byte but NUL, including newlines, which would end the
// line and let the rest be read as new keys or sections.
QString escapePath(const QString &path)
{
    QString result;
    for (const QChar c : path) {
        if (c == QLatin1Char('\\')) {
            result += QLatin1String("\\\\");
        } else if (c == QLatin1Char('\n')) {
            result += QLatin1String("\\n");
        } else if (c == QLatin1Char('\r')) {
            result += QLatin1String("\\r");
        } else {
            result += c;
        }
    }
    return result;
}

QString unescapePath(const QString &text)
{
    QString result;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar next = text.at(++i);
            result += next == QLatin1Char('n')   ? QLatin1Char('\n')
                      : next == QLatin1Char('r') ? QLatin1Char('\r')
                                                 : next;
        } else {
            result += text.at(i);
        }
    }
    return result;
}

} // namespace

QStringList SidebarLayout::builtinKeys()
{
    return {QStringLiteral("home"),      QStringLiteral("desktop"),  QStringLiteral("downloads"),
            QStringLiteral("documents"), QStringLiteral("pictures"), QStringLiteral("music"),
            QStringLiteral("videos")};
}

QString SidebarLayout::builtinPath(const QString &key)
{
    if (key == QLatin1String("home")) {
        return QDir::homePath();
    }

    QStandardPaths::StandardLocation location;
    if (key == QLatin1String("desktop")) {
        location = QStandardPaths::DesktopLocation;
    } else if (key == QLatin1String("downloads")) {
        location = QStandardPaths::DownloadLocation;
    } else if (key == QLatin1String("documents")) {
        location = QStandardPaths::DocumentsLocation;
    } else if (key == QLatin1String("pictures")) {
        location = QStandardPaths::PicturesLocation;
    } else if (key == QLatin1String("music")) {
        location = QStandardPaths::MusicLocation;
    } else if (key == QLatin1String("videos")) {
        location = QStandardPaths::MoviesLocation;
    } else {
        return {};
    }

    // QStandardPaths falls back to the home directory for locations that are
    // not configured; "Documents" that opens $HOME is worse than no entry.
    const QString path = QStandardPaths::writableLocation(location);
    return clean(path) == clean(QDir::homePath()) ? QString() : path;
}

QString SidebarEntry::resolvedPath() const
{
    return builtin.isEmpty() ? path : SidebarLayout::builtinPath(builtin);
}

QString SidebarLayout::cleanTitle(const QString &title)
{
    QString result = title;
    result.replace(QLatin1Char('\r'), QLatin1Char(' '));
    result.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return result.trimmed();
}

SidebarEntry SidebarLayout::entryFor(const QString &path)
{
    const QString cleaned = clean(path);
    for (const QString &key : builtinKeys()) {
        const QString location = builtinPath(key);
        if (!location.isEmpty() && clean(location) == cleaned) {
            return {.builtin = key, .path = {}};
        }
    }
    return {.builtin = {}, .path = cleaned};
}

QString SidebarLayout::toIni() const
{
    QString text;
    text += QStringLiteral("[sidebar]\nversion=1\nwastebasket=%1\n").arg(wastebasketHidden ? 0 : 1);

    for (const SidebarSection &section : sections) {
        text += QStringLiteral("\n[section]\nid=%1\ntitle=%2\n").arg(section.id, section.title);
        for (const SidebarEntry &entry : section.entries) {
            text += QStringLiteral("entry=%1\n")
                        .arg(entry.builtin.isEmpty() ? escapePath(entry.path) : kBuiltinPrefix + entry.builtin);
        }
    }
    return text;
}

SidebarLayout SidebarLayout::fromIni(const QString &text)
{
    SidebarLayout layout;
    const QStringList known = builtinKeys();

    QString group;
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        // Only a trailing carriage return is dropped: a path may end in a space.
        QString line = rawLine;
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
            group = trimmed.mid(1, trimmed.size() - 2);
            if (group == QLatin1String("section")) {
                layout.sections.append(SidebarSection{});
            }
            continue;
        }

        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1);

        if (group == QLatin1String("sidebar")) {
            if (key == QLatin1String("wastebasket")) {
                layout.wastebasketHidden = value.trimmed().toInt() == 0;
            }
            continue;
        }
        if (group != QLatin1String("section") || layout.sections.isEmpty()) {
            continue;
        }

        SidebarSection &section = layout.sections.last();
        if (key == QLatin1String("id")) {
            section.id = value.trimmed();
        } else if (key == QLatin1String("title")) {
            section.title = cleanTitle(value);
        } else if (key == QLatin1String("entry")) {
            if (value.startsWith(kBuiltinPrefix)) {
                const QString builtin = value.mid(kBuiltinPrefix.size()).trimmed();
                if (known.contains(builtin)) {
                    section.entries.append({.builtin = builtin, .path = {}});
                }
            } else if (const QString path = unescapePath(value); QDir::isAbsolutePath(path)) {
                section.entries.append({.builtin = {}, .path = path});
            }
        }
    }

    // Ids are what drags and confirmations carry, so they must exist and be
    // unique even in a hand-edited file.
    QStringList seen;
    for (SidebarSection &section : layout.sections) {
        if (section.id.isEmpty() || seen.contains(section.id)) {
            section.id = newId();
        }
        seen.append(section.id);
    }

    // The at-most-once invariant, for a hand-edited file.
    QStringList paths;
    for (SidebarSection &section : layout.sections) {
        section.entries.removeIf([&paths](const SidebarEntry &entry) {
            // Key on the resolved path, as find() does, so a built-in and a
            // path to the same place count as one. An unconfigured built-in
            // has no path and is keyed by name.
            const QString resolved = entry.resolvedPath();
            const QString key = !resolved.isEmpty() ? clean(resolved)
                                                    : kBuiltinPrefix + entry.builtin;
            if (paths.contains(key)) {
                return true;
            }
            paths.append(key);
            return false;
        });
    }
    return layout;
}

SidebarLayout SidebarLayout::defaults()
{
    SidebarLayout layout;
    const QString id = layout.addSection(QObject::tr("Favourites"));
    SidebarSection &section = layout.sections[layout.sectionIndex(id)];
    for (const QString &key : builtinKeys()) {
        section.entries.append({.builtin = key, .path = {}});
    }
    return layout;
}

SidebarLayout SidebarLayout::migratedFrom(const QString &sessionIniText)
{
    QStringList pinned;
    QStringList removed;

    QString group;
    for (const QString &rawLine : sessionIniText.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            group = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        const QString value = line.mid(equals + 1);
        if (group == QLatin1String("pinned")) {
            pinned.append(value);
        } else if (group == QLatin1String("removedplaces")) {
            removed.append(clean(value));
        }
    }

    SidebarLayout layout;
    const QString favourites = layout.addSection(QObject::tr("Favourites"));
    SidebarSection &section = layout.sections[layout.sectionIndex(favourites)];
    for (const QString &key : builtinKeys()) {
        const QString location = builtinPath(key);
        // An unconfigured XDG folder never appeared in the old sidebar either.
        if (location.isEmpty() && key != QLatin1String("home")) {
            continue;
        }
        if (!location.isEmpty() && removed.contains(clean(location))) {
            continue;
        }
        section.entries.append({.builtin = key, .path = {}});
    }

    const QString trash = fs::Trash().filesDirectory();
    layout.wastebasketHidden = !trash.isEmpty() && removed.contains(clean(trash));

    if (!pinned.isEmpty()) {
        const int at = layout.sectionIndex(layout.addSection(QObject::tr("Pinned")));
        for (const QString &path : pinned) {
            if (QDir::isAbsolutePath(path)) {
                layout.insertEntry(at, layout.sections.at(at).entries.size(), entryFor(path));
            }
        }
    }
    return layout;
}

SidebarLayout SidebarLayout::load()
{
    QFile file(sidebarPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fromIni(QString::fromUtf8(file.readAll()));
    }

    QFile session(platform::stateDir() + QStringLiteral("/session.ini"));
    if (session.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const SidebarLayout layout = migratedFrom(QString::fromUtf8(session.readAll()));
        layout.save();
        return layout;
    }
    return defaults();
}

void SidebarLayout::save() const
{
    QDir().mkpath(platform::stateDir());

    QSaveFile file(sidebarPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(pfApp) << "could not write the sidebar to" << sidebarPath();
        return;
    }
    file.write(toIni().toUtf8());
    if (!file.commit()) {
        qCWarning(pfApp) << "could not commit the sidebar file";
    }
}

int SidebarLayout::sectionIndex(const QString &id) const
{
    for (int i = 0; i < sections.size(); ++i) {
        if (sections.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

std::optional<std::pair<int, int>> SidebarLayout::find(const QString &cleanPath) const
{
    for (int s = 0; s < sections.size(); ++s) {
        const QList<SidebarEntry> &entries = sections.at(s).entries;
        for (int e = 0; e < entries.size(); ++e) {
            const QString resolved = entries.at(e).resolvedPath();
            if (!resolved.isEmpty() && clean(resolved) == cleanPath) {
                return std::make_pair(s, e);
            }
        }
    }
    return std::nullopt;
}

void SidebarLayout::insertEntry(int section, int index, SidebarEntry entry)
{
    if (section < 0 || section >= sections.size()) {
        return;
    }
    if (const QString resolved = entry.resolvedPath(); !resolved.isEmpty()) {
        if (const auto existing = find(clean(resolved))) {
            if (existing->first == section && existing->second < index) {
                --index;
            }
            sections[existing->first].entries.removeAt(existing->second);
        }
    }
    QList<SidebarEntry> &entries = sections[section].entries;
    entries.insert(std::clamp(index, 0, static_cast<int>(entries.size())), std::move(entry));
}

void SidebarLayout::moveEntry(int fromSection, int fromIndex, int toSection, int toIndex)
{
    if (fromSection < 0 || fromSection >= sections.size() || fromIndex < 0 ||
        fromIndex >= sections.at(fromSection).entries.size() || toSection < 0 ||
        toSection >= sections.size()) {
        return;
    }
    SidebarEntry entry = sections[fromSection].entries.takeAt(fromIndex);
    // `toIndex` was chosen against the list as it was; taking the entry out
    // shifts everything after it up by one.
    if (fromSection == toSection && toIndex > fromIndex) {
        --toIndex;
    }
    QList<SidebarEntry> &entries = sections[toSection].entries;
    entries.insert(std::clamp(toIndex, 0, static_cast<int>(entries.size())), std::move(entry));
}

void SidebarLayout::moveSection(int from, int to)
{
    if (from < 0 || from >= sections.size()) {
        return;
    }
    SidebarSection section = sections.takeAt(from);
    if (to > from) {
        --to;
    }
    sections.insert(std::clamp(to, 0, static_cast<int>(sections.size())), std::move(section));
}

void SidebarLayout::removeEntry(int section, int index)
{
    if (section < 0 || section >= sections.size() || index < 0 ||
        index >= sections.at(section).entries.size()) {
        return;
    }
    sections[section].entries.removeAt(index);
}

QString SidebarLayout::addSection(const QString &title, int at)
{
    SidebarSection section;
    section.id = newId();
    section.title = cleanTitle(title);
    const QString id = section.id;
    if (at < 0 || at > sections.size()) {
        sections.append(std::move(section));
    } else {
        sections.insert(at, std::move(section));
    }
    return id;
}

} // namespace pf::ui
