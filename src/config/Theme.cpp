#include "config/Theme.h"

#include <cmath>
#include <cstdlib>

#include "core/Logging.h"
#include "platform/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

#include <toml++/toml.hpp>

#include <optional>

namespace pf::config {
namespace {

/// Reads one colour, reporting anything QColor cannot parse.
///
/// A rejected colour keeps its default rather than becoming transparent or
/// black. §8.3's "fall back to defaults for the affected keys" matters more for
/// a theme than anywhere else: a black-on-black listing is not a degraded
/// application, it is an unusable one.
void readColour(const toml::table &table, const char *key, QColor &target, const QString &file,
                QList<ConfigIssue> *issues)
{
    const auto node = table["colors"][key];
    if (!node) {
        return;
    }

    const auto value = node.value<std::string>();
    if (!value) {
        issues->append(ConfigIssue{.file = file,
                                   .line = static_cast<int>(node.node()->source().begin.line),
                                   .column = 0,
                                   .key = QStringLiteral("colors.%1").arg(QLatin1String(key)),
                                   .message = QStringLiteral("expected a colour string")});
        return;
    }

    const QColor colour(QString::fromStdString(*value));
    if (!colour.isValid()) {
        issues->append(ConfigIssue{
            .file = file,
            .line = static_cast<int>(node.node()->source().begin.line),
            .column = 0,
            .key = QStringLiteral("colors.%1").arg(QLatin1String(key)),
            .message = QStringLiteral("'%1' is not a colour").arg(QString::fromStdString(*value))});
        return;
    }

    target = colour;
}

void readUiInt(const toml::table &table, const char *key, int &target, int minimum, int maximum,
               const QString &file, QList<ConfigIssue> *issues)
{
    const auto node = table["ui"][key];
    if (!node) {
        return;
    }
    const auto value = node.value<int64_t>();
    if (!value) {
        issues->append(ConfigIssue{.file = file,
                                   .line = static_cast<int>(node.node()->source().begin.line),
                                   .column = 0,
                                   .key = QStringLiteral("ui.%1").arg(QLatin1String(key)),
                                   .message = QStringLiteral("expected a number")});
        return;
    }
    target = std::clamp(static_cast<int>(*value), minimum, maximum);
}

QStringList themeFilesIn(const QString &directory)
{
    const QDir dir(directory);
    if (!dir.exists()) {
        return {};
    }
    return dir.entryList({QStringLiteral("*.toml")}, QDir::Files, QDir::Name);
}

} // namespace

namespace {

/// Relative luminance, and the WCAG contrast ratio built on it.
///
/// Lightness would be the obvious measure and is the wrong one: several of the
/// published palettes put their cursor at almost the background's lightness and
/// distinguish it by hue instead, so a lightness comparison calls those
/// identical when the eye does not, and calls them different when it cannot.
double relativeLuminance(const QColor &colour)
{
    const auto channel = [](double value) {
        value /= 255.0;
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return (0.2126 * channel(colour.red())) + (0.7152 * channel(colour.green())) +
           (0.0722 * channel(colour.blue()));
}

double contrastRatio(const QColor &a, const QColor &b)
{
    const double first = relativeLuminance(a);
    const double second = relativeLuminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

/// Below this the cursor and the banding are the same colour to a reader.
constexpr double kMinimumCursorSeparation = 1.12;

} // namespace

QColor Theme::effectiveAlternateRowBackground() const
{
    if (alternateRowBackground.isValid()) {
        return alternateRowBackground;
    }

    // Deliberately slight. Banding that announces itself is a distraction; the
    // job is to keep the eye on one line across a wide row, which takes a few
    // percent, not a stripe.
    const int base = isLight() ? -3 : 12;

    // And never at the cursor's expense.
    //
    // The banding is derived from the background; the cursor's colour comes
    // from the theme, and several published palettes put it within a few
    // percent of their background. Band at the full amount against one of those
    // and the row telling you where you are becomes indistinguishable from
    // every other row — which is exactly what happened to one-dark,
    // tokyo-night-storm and rose-pine-dawn when banding was introduced.
    //
    // So the band is weakened until the cursor is clearly the stronger of the
    // two, and abandoned entirely when no strength is weak enough. A theme
    // whose cursor is that faint is better served by no stripes at all than by
    // stripes that hide it.
    for (int percent = base; percent != 0; percent += (base < 0 ? 1 : -1)) {
        const QColor candidate =
            percent < 0 ? background.darker(100 - percent) : background.lighter(100 + percent);

        if (contrastRatio(cursorBackground, candidate) >= kMinimumCursorSeparation) {
            return candidate;
        }
    }

    return background;
}

bool Theme::isLight() const
{
    // Perceived lightness rather than a naive average: green contributes far
    // more to how light a colour looks than blue does, and a theme with a deep
    // blue background would otherwise be misclassified.
    return background.lightnessF() > text.lightnessF();
}

namespace {

/// Applies a parsed theme table over an existing theme.
///
/// Over, not into a fresh one: §8 lets theme.toml name a theme *and* override
/// individual colours, and that only works if the overrides are applied on top
/// of the named theme rather than on top of the built-in defaults.
void applyThemeTable(const toml::table &table, Theme &theme, const QString &fileNameForIssues,
                     QList<ConfigIssue> *issues)
{
    if (const auto name = table["name"].value<std::string>()) {
        theme.name = QString::fromStdString(*name);
    }

    readColour(table, "background", theme.background, fileNameForIssues, issues);
    readColour(table, "surface", theme.surface, fileNameForIssues, issues);
    readColour(table, "overlay", theme.overlay, fileNameForIssues, issues);
    readColour(table, "text", theme.text, fileNameForIssues, issues);
    readColour(table, "subtext", theme.subtext, fileNameForIssues, issues);
    readColour(table, "accent", theme.accent, fileNameForIssues, issues);
    readColour(table, "selection_bg", theme.selectionBackground, fileNameForIssues, issues);
    readColour(table, "cursor_bg", theme.cursorBackground, fileNameForIssues, issues);
    readColour(table, "directory", theme.directory, fileNameForIssues, issues);
    readColour(table, "executable", theme.executable, fileNameForIssues, issues);
    readColour(table, "symlink", theme.symlink, fileNameForIssues, issues);
    readColour(table, "broken", theme.broken, fileNameForIssues, issues);
    readColour(table, "archive", theme.archive, fileNameForIssues, issues);
    readColour(table, "image", theme.image, fileNameForIssues, issues);
    readColour(table, "error", theme.error, fileNameForIssues, issues);
    readColour(table, "warning", theme.warning, fileNameForIssues, issues);
    readColour(table, "success", theme.success, fileNameForIssues, issues);
    readColour(table, "border", theme.border, fileNameForIssues, issues);
    readColour(table, "border_focused", theme.borderFocused, fileNameForIssues, issues);

    // Optional: left invalid when absent so that
    // effectiveAlternateRowBackground() can derive one from the background.
    readColour(table, "alternate_row_bg", theme.alternateRowBackground, fileNameForIssues, issues);

    if (const auto family = table["ui"]["font_family"].value<std::string>()) {
        theme.fontFamily = QString::fromStdString(*family);
    }
    readUiInt(table, "font_size", theme.fontSize, 6, 32, fileNameForIssues, issues);
    readUiInt(table, "row_height", theme.rowHeight, 14, 64, fileNameForIssues, issues);
    readUiInt(table, "border_radius", theme.borderRadius, 0, 24, fileNameForIssues, issues);
    readUiInt(table, "panel_padding", theme.panelPadding, 0, 32, fileNameForIssues, issues);

    if (const auto alternating = table["ui"]["alternating_rows"].value<bool>()) {
        theme.alternatingRows = *alternating;
    }
}

/// Parses TOML, reporting a syntax error as an issue rather than throwing.
std::optional<toml::table> parseOrReport(const QString &text, const QString &fileNameForIssues,
                                         QList<ConfigIssue> *issues)
{
    toml::parse_result parsed = toml::parse(text.toStdString());
    if (!parsed) {
        issues->append(ConfigIssue{
            .file = fileNameForIssues,
            .line = static_cast<int>(parsed.error().source().begin.line),
            .column = static_cast<int>(parsed.error().source().begin.column),
            .key = {},
            .message = QString::fromStdString(std::string(parsed.error().description()))});
        return std::nullopt;
    }
    return std::move(parsed.table());
}

} // namespace

ThemeLoadResult parseTheme(const QString &text, const QString &fileNameForIssues)
{
    ThemeLoadResult result;
    if (text.trimmed().isEmpty()) {
        return result;
    }

    if (const auto table = parseOrReport(text, fileNameForIssues, &result.issues)) {
        applyThemeTable(*table, result.theme, fileNameForIssues, &result.issues);
    }
    return result;
}

QStringList availableThemeNames()
{
    QStringList names;
    for (const QString &directory : platform::themeSearchPaths()) {
        for (const QString &file : themeFilesIn(directory)) {
            QString name = file;
            name.chop(QStringLiteral(".toml").size());
            if (!names.contains(name)) {
                names.append(name);
            }
        }
    }
    // §9 ships a theme derived from QPalette so the application can follow the
    // desktop. It has no file, so it is added by hand.
    if (!names.contains(QStringLiteral("system"))) {
        names.append(QStringLiteral("system"));
    }
    return names;
}

ThemeLoadResult loadThemeByName(const QString &name)
{
    if (name == QLatin1String("system")) {
        ThemeLoadResult result;
        result.theme = systemTheme();
        return result;
    }

    // User themes shadow bundled ones of the same name (§8), which is what lets
    // somebody tweak a shipped theme without renaming it.
    for (const QString &directory : platform::themeSearchPaths()) {
        const QString path = QStringLiteral("%1/%2.toml").arg(directory, name);
        if (!QFileInfo::exists(path)) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        qCDebug(pfConfig) << "loading theme" << name << "from" << path;
        return parseTheme(QString::fromUtf8(file.readAll()), QFileInfo(path).fileName());
    }

    ThemeLoadResult result;
    result.issues.append(ConfigIssue{.file = QStringLiteral("theme.toml"),
                                     .line = 0,
                                     .column = 0,
                                     .key = QStringLiteral("name"),
                                     .message = QStringLiteral("no theme called '%1'").arg(name)});
    return result;
}

ThemeLoadResult loadActiveTheme(const QString &themeFilePath)
{
    QFile file(themeFilePath);
    if (!file.exists()) {
        // No theme.toml — a fresh install, which is most people most of the
        // time. Follow the desktop rather than falling back to whatever the
        // Theme struct's member initialisers happen to say.
        //
        // They said Catppuccin Mocha, so a Mac in light mode opened a dark
        // window and there was nothing on screen explaining why or offering a
        // way out. A file manager with no configuration should look like it
        // belongs to the system it is running on.
        return {.theme = defaultThemeForDesktop(), .issues = {}};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ThemeLoadResult result;
        result.issues.append(ConfigIssue{.file = QFileInfo(themeFilePath).fileName(),
                                         .line = 0,
                                         .column = 0,
                                         .key = {},
                                         .message = file.errorString()});
        return result;
    }

    const QString text = QString::fromUtf8(file.readAll());
    const QString fileName = QFileInfo(themeFilePath).fileName();

    ThemeLoadResult result;
    const auto table = parseOrReport(text, fileName, &result.issues);
    if (!table) {
        return result;
    }

    // §8: theme.toml holds "the active theme name, or inline overrides". Both
    // at once is the useful case — name a shipped theme and change two colours
    // — which works because the named theme is loaded first and this file is
    // then applied *over* it.
    if (const auto name = (*table)["name"].value<std::string>()) {
        const ThemeLoadResult base = loadThemeByName(QString::fromStdString(*name));
        result.theme = base.theme;
        result.issues = base.issues;
    }

    applyThemeTable(*table, result.theme, fileName, &result.issues);

    // The name key selected the base theme; it is not also an override of that
    // theme's own display name.
    if (const auto name = (*table)["name"].value<std::string>()) {
        if (!availableThemeNames().contains(QString::fromStdString(*name))) {
            result.theme.name = QString::fromStdString(*name);
        }
    }

    return result;
}

Theme defaultThemeForDesktop()
{
    const bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QString name = dark ? QStringLiteral("macos-dark") : QStringLiteral("macos-light");

    // The bundled file if it can be found, and the palette-derived system theme
    // if it cannot — a source build run before `cmake --install`, say. Better a
    // window that follows the desktop imperfectly than one that ignores it.
    const ThemeLoadResult loaded = loadThemeByName(name);
    if (loaded.issues.isEmpty()) {
        return loaded.theme;
    }
    return systemTheme();
}

Theme systemTheme()
{
    Theme theme;
    theme.name = QStringLiteral("System");

    if (QGuiApplication::instance() == nullptr) {
        return theme;
    }

    const QPalette palette = QGuiApplication::palette();

    theme.background = palette.color(QPalette::Base);
    theme.surface = palette.color(QPalette::Window);
    theme.text = palette.color(QPalette::Text);
    theme.subtext = palette.color(QPalette::PlaceholderText);
    theme.overlay = palette.color(QPalette::Disabled, QPalette::Text);
    theme.accent = palette.color(QPalette::Highlight);
    theme.selectionBackground = palette.color(QPalette::Highlight);
    theme.cursorBackground = palette.color(QPalette::AlternateBase);
    theme.border = palette.color(QPalette::Mid);
    theme.borderFocused = palette.color(QPalette::Highlight);

    // The desktop palette has no opinion about what colour a broken symlink
    // should be, so the semantic colours keep their defaults, adjusted for
    // whether the derived theme turned out light or dark.
    if (theme.isLight()) {
        theme.directory = QColor(0x1e, 0x66, 0xf5);
        theme.executable = QColor(0x40, 0xa0, 0x2b);
        theme.symlink = QColor(0x17, 0x92, 0x99);
        theme.broken = QColor(0xd2, 0x0f, 0x39);
        theme.archive = QColor(0xfe, 0x64, 0x0b);
        theme.image = QColor(0xdf, 0x8e, 0x1d);
        theme.error = QColor(0xd2, 0x0f, 0x39);
        theme.warning = QColor(0xfe, 0x64, 0x0b);
        theme.success = QColor(0x40, 0xa0, 0x2b);
    }

    return theme;
}

} // namespace pf::config
