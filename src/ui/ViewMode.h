#pragma once

#include <QString>

namespace pf::ui {

/// How a panel lays its entries out: a column of rows, a grid of tiles, or a
/// justified mosaic of images at their own shape.
enum class ViewMode {
    List,
    Icons,
    Mosaic,
};

/// The name `panels.default_view` and the folder memory use.
inline QString viewModeName(ViewMode mode)
{
    switch (mode) {
    case ViewMode::Icons:
        return QStringLiteral("icons");
    case ViewMode::Mosaic:
        return QStringLiteral("mosaic");
    case ViewMode::List:
        break;
    }
    return QStringLiteral("list");
}

/// Anything unrecognised is a list: a hand-edited file must not strand a panel.
inline ViewMode viewModeFromName(const QString &name)
{
    if (name == QLatin1String("icons")) {
        return ViewMode::Icons;
    }
    return name == QLatin1String("mosaic") ? ViewMode::Mosaic : ViewMode::List;
}

/// "Switch view": list, then icons, then mosaic, then round again.
inline ViewMode nextViewMode(ViewMode mode)
{
    switch (mode) {
    case ViewMode::List:
        return ViewMode::Icons;
    case ViewMode::Icons:
        return ViewMode::Mosaic;
    case ViewMode::Mosaic:
        break;
    }
    return ViewMode::List;
}

} // namespace pf::ui
