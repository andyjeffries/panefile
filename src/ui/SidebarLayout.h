#pragma once

#include <QList>
#include <QString>

#include <optional>
#include <utility>

namespace pf::ui {

/// One row the user put in the sidebar: a built-in place or a path.
struct SidebarEntry {
    /// "home", "desktop", "downloads", "documents", "pictures", "music",
    /// "videos", or empty for a path entry.
    QString builtin;
    /// Absolute path for a user entry; empty for a built-in, which is resolved
    /// at render time (an XDG folder can move).
    QString path;

    /// The built-in's folder, or the entry's own path. Empty when a built-in
    /// has no location of its own on this machine.
    QString resolvedPath() const;

    bool operator==(const SidebarEntry &other) const = default;
};

struct SidebarSection {
    /// Stable across renames and moves; drag payloads and confirmations carry it
    /// rather than an index, so they cannot act on a stale position.
    QString id;
    QString title;
    QList<SidebarEntry> entries;

    bool operator==(const SidebarSection &other) const = default;
};

/// What the sidebar shows, as the user arranged it: ordered, named sections of
/// entries. A pure value type in the style of Session, saved to its own
/// `sidebar.ini` so it persists whether or not the session does.
///
/// Invariant: a resolved path appears at most once in the whole layout.
struct SidebarLayout {
    QList<SidebarSection> sections;
    bool wastebasketHidden = false;

    bool operator==(const SidebarLayout &other) const = default;

    QString toIni() const;
    static SidebarLayout fromIni(const QString &text);

    /// A "Favourites" section holding every built-in.
    static SidebarLayout defaults();

    /// Builds a layout from the pins and removed places an old `session.ini`
    /// held. Only reads the `[pinned]` and `[removedplaces]` groups.
    static SidebarLayout migratedFrom(const QString &sessionIniText);

    /// `sidebar.ini`, else migrated from `session.ini` (and saved straight
    /// away, so migration happens once), else defaults().
    static SidebarLayout load();
    void save() const;

    int sectionIndex(const QString &id) const;

    /// (section, index) of the entry whose resolved path is `cleanPath`.
    std::optional<std::pair<int, int>> find(const QString &cleanPath) const;

    /// Inserts at `index` (clamped), first removing any existing copy of the
    /// same path and adjusting `index` if that copy sat earlier in the same
    /// section.
    void insertEntry(int section, int index, SidebarEntry entry);
    void moveEntry(int fromSection, int fromIndex, int toSection, int toIndex);
    void moveSection(int from, int to);
    void removeEntry(int section, int index);

    /// Returns the new section's id. `at` < 0 appends.
    QString addSection(const QString &title, int at = -1);

    /// Maps a path equal to a built-in's location onto that built-in.
    static SidebarEntry entryFor(const QString &path);

    /// The built-in keys in the order the sidebar has always listed them.
    static QStringList builtinKeys();
    /// A built-in's folder, or empty when unconfigured (the XDG fallback to
    /// home is treated as no folder).
    static QString builtinPath(const QString &key);

    /// Section titles are one line.
    static QString cleanTitle(const QString &title);
};

} // namespace pf::ui
