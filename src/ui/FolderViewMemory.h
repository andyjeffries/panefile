#pragma once

#include "ui/ViewMode.h"

#include <QByteArray>
#include <QHash>
#include <QString>

#include <optional>

namespace pf::ui {

/// Remembers the view (list or icons) chosen for individual directories.
///
/// The same idea as FolderSortMemory and stored the same way: only departures
/// from `panels.default_view` are kept, in `<stateDir>/folder-views.json`,
/// written on every change.
class FolderViewMemory
{
public:
    static FolderViewMemory &instance();

    std::optional<ViewMode> recall(const QString &directory);
    void remember(const QString &directory, ViewMode mode);
    void forget(const QString &directory);

    /// Empties the memory without touching the file. For tests.
    void clear();
    int size() const;

    /// The file's format, kept pure so it can be round-tripped in isolation.
    static QByteArray toJson(const QHash<QString, ViewMode> &modes);
    static QHash<QString, ViewMode> fromJson(const QByteArray &bytes);

private:
    FolderViewMemory() = default;

    void ensureLoaded();
    void save() const;

    QHash<QString, ViewMode> m_modes;
    bool m_loaded = false;
};

} // namespace pf::ui
