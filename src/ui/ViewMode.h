#pragma once

#include <QString>

namespace pf::ui {

/// How a panel lays its entries out: a column of rows, or a grid of tiles.
enum class ViewMode {
    List,
    Icons,
};

/// The name `panels.default_view` and the folder memory use.
inline QString viewModeName(ViewMode mode)
{
    return mode == ViewMode::Icons ? QStringLiteral("icons") : QStringLiteral("list");
}

/// Anything unrecognised is a list: a hand-edited file must not strand a panel.
inline ViewMode viewModeFromName(const QString &name)
{
    return name == QLatin1String("icons") ? ViewMode::Icons : ViewMode::List;
}

} // namespace pf::ui
