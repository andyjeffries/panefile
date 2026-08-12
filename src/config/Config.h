#pragma once

#include <QString>
#include <QStringList>

namespace pf::config {

/// A problem found while loading a config file (§8.3).
///
/// Config parsing "must never crash or silently produce garbage. On a malformed
/// file: fall back to defaults for the affected keys, and show a dismissible
/// banner naming the file, line and problem." Every field here exists to make
/// that banner useful.
struct ConfigIssue {
    QString file;
    int line = 0;
    int column = 0;
    QString key; ///< empty for a whole-file parse error
    QString message;

    QString toString() const;
};

/// §8.1's settings, already validated.
///
/// Every field carries the default from §8.1, so a Config that has never been
/// loaded is a usable Config rather than a set of empty values. That is what
/// lets startup proceed when the config file is missing, unreadable or nonsense
/// — which is the common case for a new user and must not be a failure path.
struct Settings {
    struct General {
        QString newPanelPath = QStringLiteral("~");
        bool restoreSession = true;
        bool confirmOnQuit = false;
        bool singleInstance = true;
    } general;

    struct Panels {
        int defaultCount = 1;
        int maxCount = 10;
        bool directoriesFirst = true;
        QString defaultSort = QStringLiteral("name");
        bool showHidden = false;
    } panels;

    struct QuickLook {
        QString dock = QStringLiteral("float");
        int floatSizePercent = 70;
        int dockSizePercent = 35;
        bool chrome = true;
        int debounceMs = 120;
        qint64 maxReadBytes = 67108864;
        int maxDecodeMb = 500;
        bool followCursor = true;
        bool closeOnPanelSwitch = true;
    } quicklook;

    struct Thumbnails {
        bool enabled = true;
        bool video = true;
        int maxFileSizeMb = 200;
    } thumbnails;

    struct Search {
        bool fuzzy = true;
        bool respectGitignore = true;
        int maxResults = 10000;
    } search;

    struct Operations {
        bool confirmDelete = true;
        bool confirmTrash = false;
        QString defaultConflict = QStringLiteral("ask");
        bool followSymlinks = false;
    } operations;

    struct Cli {
        QString fileAction = QStringLiteral("select");
        QString onFocused = QStringLiteral("current_panel");
        QString onUnfocused = QStringLiteral("new_panel");
    } cli;

    struct External {
        QString editor;
        QString terminal;
    } external;

    struct Keys {
        int sequenceTimeoutMs = 1000;
        int ambiguityTimeoutMs = 500;
    } keys;
};

/// The result of loading a config file: the settings, plus whatever went wrong.
struct ConfigLoadResult {
    Settings settings;
    QList<ConfigIssue> issues;

    bool isClean() const { return issues.isEmpty(); }
};

/// Parses `config.toml` text over the §8.1 defaults.
///
/// Never fails. A malformed file yields the defaults and an issue describing
/// the problem; a file with one bad key yields every other key and an issue for
/// that one. §8.3: "fall back to defaults for the affected keys" — per key, not
/// per file, because discarding a user's whole configuration over one typo is
/// exactly the silent garbage the requirement is about.
ConfigLoadResult parseConfig(const QString &text, const QString &fileNameForIssues = {});

/// Reads and parses config.toml from the configuration directory. A missing
/// file is not an issue: it is what every new installation looks like.
ConfigLoadResult loadConfig(const QString &path);

} // namespace pf::config
