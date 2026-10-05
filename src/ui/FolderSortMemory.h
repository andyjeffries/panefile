#pragma once

#include "model/FilterSortProxy.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <optional>

namespace pf::ui {

/// A sort key and its direction — what the `o` menu chooses.
struct SortOrder {
    SortKey key = SortKey::Name;
    bool reverse = false;

    bool operator==(const SortOrder &other) const = default;
};

/// Remembers the sort order chosen for individual directories.
///
/// Some directories are always wanted in a particular order — Downloads newest
/// first, a log directory by size — without that order becoming the setting
/// for every directory. So a panel's sort belongs to the directory it shows:
/// choosing one records it here, and entering a directory restores it, or the
/// configured default when nothing was chosen.
///
/// Only departures from the default are stored; FilePanel forgets a directory
/// whose order is set back to it. Unlike CursorMemory this persists, to
/// `<stateDir>/folder-sorts.json`, and is written on every change: an entry is
/// a deliberate choice the user made, not something that can be recomputed,
/// and changes are rare enough that writing each one costs nothing.
class FolderSortMemory
{
public:
    static FolderSortMemory &instance();

    std::optional<SortOrder> recall(const QString &directory);
    void remember(const QString &directory, SortOrder order);
    void forget(const QString &directory);

    /// Empties the memory without touching the file. For tests.
    void clear();
    int size() const;

    /// The file's format, kept pure so it can be round-tripped in isolation.
    static QByteArray toJson(const QHash<QString, SortOrder> &orders);
    static QHash<QString, SortOrder> fromJson(const QByteArray &bytes);

private:
    FolderSortMemory() = default;

    void ensureLoaded();
    void save() const;

    QHash<QString, SortOrder> m_orders;
    bool m_loaded = false;
};

} // namespace pf::ui
