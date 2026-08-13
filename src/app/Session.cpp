#include "app/Session.h"

#include "core/Logging.h"
#include "platform/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>

namespace pf {
namespace {

QString sessionPath()
{
    return platform::stateDir() + QStringLiteral("/session.ini");
}

} // namespace

QString Session::toIni() const
{
    // Written by hand rather than through QSettings so that toIni() and
    // fromIni() are pure and testable, and so the file's shape is visible in
    // one place rather than spread over a dozen setValue() calls.
    QString text;
    text += QStringLiteral("[window]\n");
    text += QStringLiteral("geometry=%1,%2,%3,%4\n")
                .arg(windowGeometry.x())
                .arg(windowGeometry.y())
                .arg(windowGeometry.width())
                .arg(windowGeometry.height());
    text += QStringLiteral("maximised=%1\n").arg(windowMaximised ? 1 : 0);
    text += QStringLiteral("focused=%1\n").arg(focusedPanel);
    if (!quickLookDock.isEmpty()) {
        text += QStringLiteral("quicklook_dock=%1\n").arg(quickLookDock);
    }

    for (int i = 0; i < panels.size(); ++i) {
        const SessionPanel &panel = panels.at(i);
        text += QStringLiteral("\n[panel%1]\n").arg(i);
        text += QStringLiteral("path=%1\n").arg(panel.path);
        if (!panel.cursorName.isEmpty()) {
            text += QStringLiteral("cursor=%1\n").arg(panel.cursorName);
        }
        text += QStringLiteral("sort=%1\n").arg(panel.sortKey);
        text += QStringLiteral("reverse=%1\n").arg(panel.reverseSort ? 1 : 0);
        text += QStringLiteral("hidden=%1\n").arg(panel.showHidden ? 1 : 0);
    }

    if (!pinnedPaths.isEmpty()) {
        text += QStringLiteral("\n[pinned]\n");
        for (int i = 0; i < pinnedPaths.size(); ++i) {
            text += QStringLiteral("%1=%2\n").arg(i).arg(pinnedPaths.at(i));
        }
    }

    return text;
}

Session Session::fromIni(const QString &text)
{
    Session session;

    QString group;
    for (const QString &rawLine : text.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            group = line.mid(1, line.size() - 2);
            if (group.startsWith(QLatin1String("panel"))) {
                session.panels.append(SessionPanel{});
            }
            continue;
        }

        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        const QString key = line.left(equals).trimmed();
        // Not trimmed: a path may legitimately end in a space, and a session
        // that silently renames the directory it restores is worse than one
        // that fails to restore it.
        const QString value = line.mid(equals + 1);

        if (group == QLatin1String("window")) {
            if (key == QLatin1String("geometry")) {
                const QStringList parts = value.split(QLatin1Char(','));
                if (parts.size() == 4) {
                    session.windowGeometry = QRect(parts.at(0).toInt(), parts.at(1).toInt(),
                                                   parts.at(2).toInt(), parts.at(3).toInt());
                }
            } else if (key == QLatin1String("maximised")) {
                session.windowMaximised = value.toInt() != 0;
            } else if (key == QLatin1String("focused")) {
                session.focusedPanel = value.toInt();
            } else if (key == QLatin1String("quicklook_dock")) {
                session.quickLookDock = value;
            }
            continue;
        }

        if (group.startsWith(QLatin1String("panel")) && !session.panels.isEmpty()) {
            SessionPanel &panel = session.panels.last();
            if (key == QLatin1String("path")) {
                panel.path = value;
            } else if (key == QLatin1String("cursor")) {
                panel.cursorName = value;
            } else if (key == QLatin1String("sort")) {
                panel.sortKey = value;
            } else if (key == QLatin1String("reverse")) {
                panel.reverseSort = value.toInt() != 0;
            } else if (key == QLatin1String("hidden")) {
                panel.showHidden = value.toInt() != 0;
            }
            continue;
        }

        if (group == QLatin1String("pinned")) {
            session.pinnedPaths.append(value);
        }
    }

    // A [panelN] header with no path in it is not a panel.
    session.panels.removeIf([](const SessionPanel &panel) { return panel.path.isEmpty(); });

    return session;
}

Session Session::load()
{
    QFile file(sessionPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return fromIni(QString::fromUtf8(file.readAll()));
}

void Session::save() const
{
    QDir().mkpath(platform::stateDir());

    // QSaveFile, so a crash mid-write does not leave a truncated session that
    // fails to parse on the next launch — which would lose the session it was
    // trying to preserve.
    QSaveFile file(sessionPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(pfApp) << "could not write the session to" << sessionPath();
        return;
    }

    file.write(toIni().toUtf8());
    if (!file.commit()) {
        qCWarning(pfApp) << "could not commit the session file";
    }
}

Session Session::pruned() const
{
    Session result = *this;

    result.panels.removeIf(
        [](const SessionPanel &panel) { return !QFileInfo(panel.path).isDir(); });

    result.pinnedPaths.removeIf([](const QString &path) { return !QFileInfo(path).isDir(); });

    // The focused index refers to the list before pruning, so clamp rather than
    // trusting it. The empty case comes first: qBound asserts when its minimum
    // exceeds its maximum, which is exactly what an empty list produces.
    if (result.panels.isEmpty()) {
        result.focusedPanel = 0;
    } else {
        result.focusedPanel =
            qBound(0, result.focusedPanel, static_cast<int>(result.panels.size()) - 1);
    }

    return result;
}

} // namespace pf
