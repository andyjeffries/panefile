#pragma once

#include "config/Config.h"

#include <QColor>
#include <QString>

namespace pf::config {

/// A theme, as loaded from a `theme.toml` (§9).
///
/// The colour names are §9's verbatim. Two consumers exist and need different
/// things: `StyleSheetBuilder` compiles this into QSS for the widgets, and the
/// delegate reads it directly, because a delegate paints manually and cannot
/// use a stylesheet.
struct Theme {
    QString name = QStringLiteral("Catppuccin Mocha");

    QColor background{0x1e, 0x1e, 0x2e};
    QColor surface{0x31, 0x32, 0x44};
    QColor overlay{0x6c, 0x70, 0x86};
    QColor text{0xcd, 0xd6, 0xf4};
    QColor subtext{0xa6, 0xad, 0xc8};
    QColor accent{0x89, 0xb4, 0xfa};
    QColor selectionBackground{0x45, 0x47, 0x5a};
    QColor cursorBackground{0x58, 0x5b, 0x70};

    QColor directory{0x89, 0xb4, 0xfa};
    QColor executable{0xa6, 0xe3, 0xa1};
    QColor symlink{0x94, 0xe2, 0xd5};
    QColor broken{0xf3, 0x8b, 0xa8};
    QColor archive{0xfa, 0xb3, 0x87};
    QColor image{0xf9, 0xe2, 0xaf};

    QColor error{0xf3, 0x8b, 0xa8};
    QColor warning{0xfa, 0xb3, 0x87};
    QColor success{0xa6, 0xe3, 0xa1};

    QColor border{0x45, 0x47, 0x5a};
    QColor borderFocused{0x89, 0xb4, 0xfa};

    /// The banding Finder and every other list view uses to keep the eye on a
    /// line across a wide row (§9's spirit, if not its letter).
    ///
    /// Invalid by default and derived from the background when a theme does not
    /// name it, so all twenty-three bundled themes get sensible banding without
    /// twenty-three edits — and any of them can still override it.
    QColor alternateRowBackground;

    QString fontFamily;
    int fontSize = 10;
    int rowHeight = 28;
    int borderRadius = 6;
    int panelPadding = 8;

    /// §9's `[ui]` has no name for this; it is the vertical rhythm the list is
    /// laid out on, and Finder's is noticeably looser than a terminal's. Kept
    /// as a metric rather than a constant so a theme can be tight or airy.
    bool alternatingRows = true;

    /// True when the theme's background is lighter than its text, which is what
    /// the application needs to know to pick sensible derived shades — a hover
    /// tint has to go the opposite way on a light theme.
    bool isLight() const;

    /// The row banding colour, derived from the background when the theme does
    /// not set one. A hard-coded lighter() would be invisible on a light theme
    /// and washed out on a dark one, so the direction follows isLight().
    QColor effectiveAlternateRowBackground() const;
};

struct ThemeLoadResult {
    Theme theme;
    QList<ConfigIssue> issues;
};

/// Parses a theme from TOML text over the built-in defaults.
///
/// Like parseConfig(), this never fails: an unreadable or partial theme leaves
/// the remaining colours at their defaults rather than producing an unusable
/// palette. A file with one bad colour is far more likely than a file with
/// none, and the failure mode of getting it wrong is an unreadable application.
ThemeLoadResult parseTheme(const QString &text, const QString &fileNameForIssues = {});

/// Loads a theme by name, searching the user's theme directory before the
/// bundled ones (§8). Returns the defaults, and an issue, when not found.
ThemeLoadResult loadThemeByName(const QString &name);

/// The theme to use when the user has never chosen one: macOS Light or macOS
/// Dark, following the desktop's own colour scheme.
///
/// Falls back to systemTheme() when the bundled files cannot be found, which is
/// what a build tree looked like before the themes were staged into the bundle.
Theme defaultThemeForDesktop();

/// Reads `theme.toml`, which either names a theme or defines one inline (§8).
ThemeLoadResult loadActiveTheme(const QString &themeFilePath);

/// Names of every theme that can be loaded, user themes first, deduplicated.
QStringList availableThemeNames();

/// A theme derived from the desktop's own QPalette, for §9's `system` theme.
/// Requires a QGuiApplication, so it is not available before one exists.
Theme systemTheme();

} // namespace pf::config
