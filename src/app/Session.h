#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

namespace pf {

/// One panel, as it was when the session was saved.
struct SessionPanel {
    QString path;
    QString cursorName;
    QString sortKey = QStringLiteral("name");
    bool reverseSort = false;
    bool showHidden = false;

    bool operator==(const SessionPanel &other) const = default;
};

/// What is worth restoring (§8: "State (pinned dirs, session, window geometry)
/// goes to `$XDG_DATA_HOME/panefile/`, **not** the config dir. Never write to a
/// user's config file.").
///
/// Deliberately small. A session file is a convenience, and every field in it
/// is one more thing that can be wrong on the next launch — so it holds where
/// the panels were and nothing that could be recomputed.
struct Session {
    QList<SessionPanel> panels;
    int focusedPanel = 0;

    QRect windowGeometry;
    bool windowMaximised = false;

    QStringList pinnedPaths;

    /// The Quick Look dock, which §7.6 says persists across sessions.
    QString quickLookDock;

    bool isEmpty() const { return panels.isEmpty(); }

    /// Serialises to the INI form written under the state directory. Pure, so
    /// §14 can round-trip it without touching the user's real state.
    QString toIni() const;
    static Session fromIni(const QString &text);

    /// Reads and writes `<stateDir>/session.ini`.
    static Session load();
    void save() const;

    /// Drops panels whose directory has gone. A session pointing at an
    /// unmounted volume must not leave the user staring at an error on every
    /// launch, and must not stop the panels that are still valid from opening.
    Session pruned() const;
};

} // namespace pf
