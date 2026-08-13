#include "ui/quicklook/QuickLookDock.h"

namespace pf::ui {

QuickLookDock parseDock(const QString &name)
{
    if (name == QLatin1String("right")) {
        return QuickLookDock::Right;
    }
    if (name == QLatin1String("left")) {
        return QuickLookDock::Left;
    }
    if (name == QLatin1String("bottom")) {
        return QuickLookDock::Bottom;
    }
    if (name == QLatin1String("panel")) {
        return QuickLookDock::Panel;
    }
    if (name == QLatin1String("full")) {
        return QuickLookDock::Full;
    }
    return QuickLookDock::Float;
}

QString dockName(QuickLookDock dock)
{
    switch (dock) {
    case QuickLookDock::Right:
        return QStringLiteral("right");
    case QuickLookDock::Left:
        return QStringLiteral("left");
    case QuickLookDock::Bottom:
        return QStringLiteral("bottom");
    case QuickLookDock::Panel:
        return QStringLiteral("panel");
    case QuickLookDock::Full:
        return QStringLiteral("full");
    case QuickLookDock::Float:
        break;
    }
    return QStringLiteral("float");
}

bool isDocked(QuickLookDock dock)
{
    return dock != QuickLookDock::Float;
}

QuickLookDock nextDock(QuickLookDock dock)
{
    switch (dock) {
    case QuickLookDock::Float:
        return QuickLookDock::Right;
    case QuickLookDock::Right:
        return QuickLookDock::Left;
    case QuickLookDock::Left:
        return QuickLookDock::Bottom;
    case QuickLookDock::Bottom:
        return QuickLookDock::Panel;
    case QuickLookDock::Panel:
    case QuickLookDock::Full:
        break;
    }
    return QuickLookDock::Float;
}

} // namespace pf::ui
