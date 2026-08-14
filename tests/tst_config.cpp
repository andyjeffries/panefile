// Configuration, theme and hotkey loading (§8, §9, §14).
//
// §14 asks for "Config parsing: valid, malformed, partial, unknown keys". The
// requirement behind all four is §8.3: parsing "must never crash or silently
// produce garbage. On a malformed file: fall back to defaults for the affected
// keys, and show a dismissible banner naming the file, line and problem."
//
// Two words there do the work. *Affected* — per key, not per file, because
// discarding somebody's whole configuration over one typo is the silent garbage
// the requirement is about. And *line* — an error without one sends the user
// hunting.

#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "config/Config.h"
#include "config/DefaultConfig.h"
#include "config/Hotkeys.h"
#include "config/StyleSheetBuilder.h"
#include "config/Theme.h"

#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::config;

class TestConfig : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // config.toml
    void emptyTextGivesTheDocumentedDefaults();
    void shippedTemplateParsesToTheSameDefaults();
    void validFileIsApplied();
    void partialFileKeepsOtherDefaults();
    void malformedFileKeepsEveryDefault();
    void malformedFileReportsTheLine();
    void oneBadKeyDoesNotDiscardTheRest();
    void unknownKeysAreIgnoredSilently();
    void wrongTypeIsReportedAndIgnored();
    void outOfRangeNumbersAreClampedAndReported();
    void invalidEnumIsRejectedWithTheAllowedValues();

    // theme.toml
    void themeDefaultsAreCatppuccinMocha();
    void noThemeFileFollowsTheDesktop();
    void followSystemOverridesTheNamedTheme();
    void themeColoursAreApplied();
    void invalidColourKeepsTheDefault();
    void themeUiMetricsAreClamped();
    void lightnessIsDetected();

    // stylesheet
    void styleSheetContainsTheThemeColours();
    void styleSheetStylesTheFocusedPanel();

    // hotkeys.toml
    void hotkeysReplaceDefaultsForNamedActions();
    void hotkeysLeaveUnmentionedActionsAlone();
    void emptyListUnbinds();
    void unparseableBindingIsReportedAndSkipped();
    void remapTakesTheChordFromTheDefaultThatHeldIt();
    void twoBindingsForOneChordInOneFileConflict();
    void hotkeyTimeoutsAreRead();
    void malformedHotkeysKeepTheDefaultKeymap();
};

void TestConfig::emptyTextGivesTheDocumentedDefaults()
{
    const ConfigLoadResult result = parseConfig({});

    QVERIFY(result.isClean());
    QCOMPARE(result.settings.panels.maxCount, 10);
    QCOMPARE(result.settings.general.singleInstance, true);
    QCOMPARE(result.settings.quicklook.dock, QStringLiteral("float"));
    QCOMPARE(result.settings.quicklook.debounceMs, 120);
    QCOMPARE(result.settings.search.maxResults, 10000);
    QCOMPARE(result.settings.operations.followSymlinks, false);
}

void TestConfig::shippedTemplateParsesToTheSameDefaults()
{
    // --print-default-config writes this template, and a user is expected to
    // redirect it into their config directory. If feeding it back in changed
    // any behaviour, that instruction would be a trap.
    const QString template_ = QString::fromUtf8(kDefaultConfigToml.data(),
                                                static_cast<qsizetype>(kDefaultConfigToml.size()));
    const ConfigLoadResult fromTemplate = parseConfig(template_);
    const ConfigLoadResult fromNothing = parseConfig({});

    QVERIFY2(fromTemplate.isClean(), qPrintable(fromTemplate.issues.value(0).toString()));

    QCOMPARE(fromTemplate.settings.panels.maxCount, fromNothing.settings.panels.maxCount);
    QCOMPARE(fromTemplate.settings.quicklook.maxReadBytes,
             fromNothing.settings.quicklook.maxReadBytes);
    QCOMPARE(fromTemplate.settings.operations.defaultConflict,
             fromNothing.settings.operations.defaultConflict);
    QCOMPARE(fromTemplate.settings.cli.fileAction, fromNothing.settings.cli.fileAction);
}

void TestConfig::validFileIsApplied()
{
    const ConfigLoadResult result = parseConfig(QStringLiteral(R"(
[general]
restore_session = false

[panels]
max_count = 4
show_hidden = true

[search]
fuzzy = false
max_results = 500
)"));

    QVERIFY(result.isClean());
    QCOMPARE(result.settings.general.restoreSession, false);
    QCOMPARE(result.settings.panels.maxCount, 4);
    QCOMPARE(result.settings.panels.showHidden, true);
    QCOMPARE(result.settings.search.fuzzy, false);
    QCOMPARE(result.settings.search.maxResults, 500);
}

void TestConfig::partialFileKeepsOtherDefaults()
{
    const ConfigLoadResult result = parseConfig(QStringLiteral("[panels]\nmax_count = 3\n"));

    QVERIFY(result.isClean());
    QCOMPARE(result.settings.panels.maxCount, 3);
    // Everything else still holds its documented default.
    QCOMPARE(result.settings.panels.directoriesFirst, true);
    QCOMPARE(result.settings.quicklook.debounceMs, 120);
    QCOMPARE(result.settings.general.singleInstance, true);
}

void TestConfig::malformedFileKeepsEveryDefault()
{
    // §8.3: the application starts on defaults with a banner, rather than
    // refusing to start.
    const ConfigLoadResult result =
        parseConfig(QStringLiteral("[panels\nmax_count = = 3\n"), QStringLiteral("config.toml"));

    QVERIFY(!result.isClean());
    QCOMPARE(result.settings.panels.maxCount, 10);
    QCOMPARE(result.settings.quicklook.dock, QStringLiteral("float"));
}

void TestConfig::malformedFileReportsTheLine()
{
    const ConfigLoadResult result = parseConfig(QStringLiteral(R"(
[panels]
max_count = 3

[general
restore_session = false
)"),
                                                QStringLiteral("config.toml"));

    QCOMPARE(result.issues.size(), 1);
    const ConfigIssue &issue = result.issues.first();
    QCOMPARE(issue.file, QStringLiteral("config.toml"));
    QVERIFY2(issue.line > 0, "a syntax error with no line number sends the user hunting");
    QVERIFY(issue.toString().contains(QStringLiteral("config.toml")));
}

void TestConfig::oneBadKeyDoesNotDiscardTheRest()
{
    // The heart of §8.3's "fall back to defaults for the affected keys".
    const ConfigLoadResult result = parseConfig(QStringLiteral(R"(
[panels]
max_count = "not a number"
show_hidden = true
directories_first = false
)"),
                                                QStringLiteral("config.toml"));

    QCOMPARE(result.issues.size(), 1);
    QCOMPARE(result.settings.panels.maxCount, 10);     // the affected key
    QCOMPARE(result.settings.panels.showHidden, true); // and not its neighbours
    QCOMPARE(result.settings.panels.directoriesFirst, false);
}

void TestConfig::unknownKeysAreIgnoredSilently()
{
    // Silently on purpose. A key Panefile does not know may belong to a newer
    // version the user also runs, or to a typo they will find another way;
    // warning about every one would train them to ignore the banner that also
    // reports real errors.
    const ConfigLoadResult result = parseConfig(QStringLiteral(R"(
[panels]
max_count = 5
enable_teleportation = true

[nonexistent_section]
whatever = 1
)"));

    QVERIFY(result.isClean());
    QCOMPARE(result.settings.panels.maxCount, 5);
}

void TestConfig::wrongTypeIsReportedAndIgnored()
{
    const ConfigLoadResult result = parseConfig(
        QStringLiteral("[general]\nsingle_instance = \"yes\"\n"), QStringLiteral("config.toml"));

    QCOMPARE(result.issues.size(), 1);
    QVERIFY(result.issues.first().key.contains(QStringLiteral("single_instance")));
    QVERIFY(result.issues.first().message.contains(QStringLiteral("true or false")));
    QCOMPARE(result.settings.general.singleInstance, true);
}

void TestConfig::outOfRangeNumbersAreClampedAndReported()
{
    // Clamped rather than rejected: max_count = 500 is somebody asking for more
    // panels than the design supports, not a typo, and giving them the maximum
    // is friendlier than ignoring the line. The issue still says what happened.
    const ConfigLoadResult result =
        parseConfig(QStringLiteral("[panels]\nmax_count = 500\n"), QStringLiteral("config.toml"));

    QCOMPARE(result.settings.panels.maxCount, 10);
    QCOMPARE(result.issues.size(), 1);
    QVERIFY2(result.issues.first().message.contains(QStringLiteral("using 10")),
             qPrintable(result.issues.first().message));
}

void TestConfig::invalidEnumIsRejectedWithTheAllowedValues()
{
    const ConfigLoadResult result = parseConfig(
        QStringLiteral("[quicklook]\ndock = \"sideways\"\n"), QStringLiteral("config.toml"));

    QCOMPARE(result.settings.quicklook.dock, QStringLiteral("float"));
    QCOMPARE(result.issues.size(), 1);
    // The message lists what is allowed: §8.1 documents these in comments, and
    // repeating them here is the difference between fixing and guessing.
    const QString message = result.issues.first().message;
    QVERIFY2(message.contains(QStringLiteral("float")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("bottom")), qPrintable(message));
}

void TestConfig::themeDefaultsAreCatppuccinMocha()
{
    const ThemeLoadResult result = parseTheme({});

    QVERIFY(result.issues.isEmpty());
    QCOMPARE(result.theme.background, QColor(0x1e, 0x1e, 0x2e));
    QCOMPARE(result.theme.accent, QColor(0x89, 0xb4, 0xfa));
    QCOMPARE(result.theme.rowHeight, 28);
}

void TestConfig::noThemeFileFollowsTheDesktop()
{
    // A fresh install has no theme.toml. That used to fall through to the Theme
    // struct's member initialisers — Catppuccin Mocha — so a Mac in light mode
    // opened a dark window with nothing on screen to explain it or undo it.
    //
    // It now resolves to macOS Light or macOS Dark by the desktop's colour
    // scheme, falling back to the palette-derived system theme when the bundled
    // files cannot be found. This asserts the property that matters and holds
    // either way: whatever comes back, it is not the hard-coded dark default.
    const ThemeLoadResult result =
        loadActiveTheme(QStringLiteral("/nonexistent/panefile/theme.toml"));

    QVERIFY(result.issues.isEmpty());
    QVERIFY2(result.theme.background != QColor(0x1e, 0x1e, 0x2e),
             qPrintable(result.theme.background.name()));
}

void TestConfig::followSystemOverridesTheNamedTheme()
{
    // The settings window needs "follow the desktop" to be storable. It used to
    // be implicit — no theme.toml meant follow, any theme.toml meant do not —
    // so the only way back to following was to delete a file, which is not
    // something a checkbox can offer.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("theme.toml"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("follow_system = true\nname = \"gruvbox-dark\"\n\n[ui]\nrow_height = 33\n");
    file.close();

    const ThemeLoadResult result = loadActiveTheme(path);

    QVERIFY(result.issues.isEmpty());

    // Not Gruvbox: follow_system wins over a name left in the file, which is
    // what happens when someone ticks the box after having chosen a theme.
    QVERIFY2(!result.theme.name.contains(QStringLiteral("Gruvbox")), qPrintable(result.theme.name));

    // But the [ui] block still applies, so following the desktop does not mean
    // giving up a personal row height.
    QCOMPARE(result.theme.rowHeight, 33);
}

void TestConfig::themeColoursAreApplied()
{
    const ThemeLoadResult result = parseTheme(QStringLiteral(R"(
name = "Test"

[colors]
background = "#102030"
accent = "#ff0000"

[ui]
row_height = 30
panel_padding = 12
)"));

    QVERIFY(result.issues.isEmpty());
    QCOMPARE(result.theme.name, QStringLiteral("Test"));
    QCOMPARE(result.theme.background, QColor(0x10, 0x20, 0x30));
    QCOMPARE(result.theme.accent, QColor(0xff, 0x00, 0x00));
    QCOMPARE(result.theme.rowHeight, 30);
    QCOMPARE(result.theme.panelPadding, 12);
    // Unmentioned colours keep the default rather than becoming black.
    QCOMPARE(result.theme.text, QColor(0xcd, 0xd6, 0xf4));
}

void TestConfig::invalidColourKeepsTheDefault()
{
    // Matters more here than anywhere else in the config: a black-on-black
    // listing is not a degraded application, it is an unusable one.
    const ThemeLoadResult result = parseTheme(QStringLiteral("[colors]\ntext = \"not-a-colour\"\n"),
                                              QStringLiteral("theme.toml"));

    QCOMPARE(result.issues.size(), 1);
    QCOMPARE(result.theme.text, QColor(0xcd, 0xd6, 0xf4));
    QVERIFY(result.issues.first().message.contains(QStringLiteral("not a colour")));
}

void TestConfig::themeUiMetricsAreClamped()
{
    const ThemeLoadResult result = parseTheme(QStringLiteral(R"(
[ui]
row_height = 900
font_size = 1
)"));

    // A 900-pixel row is not a design choice anybody made deliberately.
    QVERIFY(result.theme.rowHeight <= 64);
    QVERIFY(result.theme.fontSize >= 6);
}

void TestConfig::lightnessIsDetected()
{
    const ThemeLoadResult dark =
        parseTheme(QStringLiteral("[colors]\nbackground = \"#1e1e2e\"\ntext = \"#cdd6f4\"\n"));
    const ThemeLoadResult light =
        parseTheme(QStringLiteral("[colors]\nbackground = \"#ffffff\"\ntext = \"#1d1d1f\"\n"));

    QVERIFY(!dark.theme.isLight());
    QVERIFY(light.theme.isLight());
}

void TestConfig::styleSheetContainsTheThemeColours()
{
    Theme theme;
    theme.background = QColor(0x12, 0x34, 0x56);
    theme.accent = QColor(0xab, 0xcd, 0xef);

    const QString sheet = buildStyleSheet(theme);

    QVERIFY(sheet.contains(QStringLiteral("#123456")));
    QVERIFY(sheet.contains(QStringLiteral("#abcdef")));
    // Every placeholder must have been substituted; a leftover one would render
    // as a literal in the stylesheet and silently disable the rule around it.
    QVERIFY2(!sheet.contains(QStringLiteral("%{")), "unsubstituted placeholder in the stylesheet");
}

void TestConfig::styleSheetStylesTheFocusedPanel()
{
    // §9: "The focused panel must be unmistakable… This is the single most
    // important visual affordance in the app."
    Theme theme;
    theme.borderFocused = QColor(0x00, 0xff, 0x00);

    const QString sheet = buildStyleSheet(theme);

    QVERIFY(sheet.contains(QStringLiteral("panelActive")));
    QVERIFY(sheet.contains(QStringLiteral("#00ff00")));
}

void TestConfig::hotkeysReplaceDefaultsForNamedActions()
{
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);

    // `s` is focus_on_sidebar by default, so this also covers the case that
    // matters most: a remap must take the chord *from* the default. Rejecting
    // it as a conflict would mean the user's explicit instruction lost to a
    // default they were trying to replace.
    const HotkeysLoadResult result =
        applyHotkeys(QStringLiteral("[normal]\nlist_down = [\"s\"]\n"), keymap);

    QVERIFY2(result.issues.isEmpty(), qPrintable(result.issues.value(0).toString()));
    QCOMPARE(result.bindingsApplied, 1);

    QCOMPARE(keymap.lookup(input::KeymapLayer::Normal, *input::parseBinding(QStringLiteral("s")))
                 .actionId,
             QStringLiteral("list_down"));

    // Mentioning an action replaces its bindings rather than adding to them:
    // `list_down = ["s"]` plainly means "s and nothing else", and a user who
    // wanted both would have written both.
    QCOMPARE(
        keymap.lookup(input::KeymapLayer::Normal, *input::parseBinding(QStringLiteral("j"))).type,
        input::Keymap::MatchType::NoMatch);
}

void TestConfig::hotkeysLeaveUnmentionedActionsAlone()
{
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);

    applyHotkeys(QStringLiteral("[normal]\nlist_down = [\"s\"]\n"), keymap);

    QCOMPARE(keymap.lookup(input::KeymapLayer::Normal, *input::parseBinding(QStringLiteral("k")))
                 .actionId,
             QStringLiteral("list_up"));
}

void TestConfig::emptyListUnbinds()
{
    // §8.2: "`open_zoxide = []` also works."
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);

    const HotkeysLoadResult result =
        applyHotkeys(QStringLiteral("[normal]\ntoggle_dot_file = []\n"), keymap);

    QCOMPARE(result.actionsUnbound, 1);
    QCOMPARE(
        keymap.lookup(input::KeymapLayer::Normal, *input::parseBinding(QStringLiteral("."))).type,
        input::Keymap::MatchType::NoMatch);
}

void TestConfig::unparseableBindingIsReportedAndSkipped()
{
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);

    const HotkeysLoadResult result =
        applyHotkeys(QStringLiteral("[normal]\nlist_down = [\"NotAKey\", \"s\"]\n"), keymap,
                     nullptr, QStringLiteral("hotkeys.toml"));

    QCOMPARE(result.issues.size(), 1);
    QCOMPARE(result.bindingsApplied, 1);
    // The good binding in the same list still applies.
    QCOMPARE(keymap.lookup(input::KeymapLayer::Normal, *input::parseBinding(QStringLiteral("s")))
                 .actionId,
             QStringLiteral("list_down"));
}

void TestConfig::remapTakesTheChordFromTheDefaultThatHeldIt()
{
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);

    applyHotkeys(QStringLiteral("[normal]\nlist_down = [\"s\"]\n"), keymap);

    // The chord now belongs to list_down, and focus_on_sidebar has lost it.
    QCOMPARE(keymap.lookup(input::KeymapLayer::Normal, *input::parseBinding(QStringLiteral("s")))
                 .actionId,
             QStringLiteral("list_down"));
    QVERIFY(keymap.bindingsFor(input::KeymapLayer::Normal, QStringLiteral("focus_on_sidebar"))
                .isEmpty());
}

void TestConfig::twoBindingsForOneChordInOneFileConflict()
{
    // Overriding a default is not a conflict; the user's own file contradicting
    // itself is. §6.2: keep the one declared first, and say so.
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);

    const HotkeysLoadResult result =
        applyHotkeys(QStringLiteral("[normal]\nlist_down = [\"z\"]\nlist_up = [\"z\"]\n"), keymap,
                     nullptr, QStringLiteral("hotkeys.toml"));

    QCOMPARE(result.issues.size(), 1);
    QVERIFY(result.issues.first().message.contains(QStringLiteral("earlier in this file")));

    const QString winner =
        keymap.lookup(input::KeymapLayer::Normal, *input::parseBinding(QStringLiteral("z")))
            .actionId;
    QVERIFY2(winner == QLatin1String("list_down") || winner == QLatin1String("list_up"),
             qPrintable(winner));
}

void TestConfig::hotkeyTimeoutsAreRead()
{
    input::Keymap keymap;
    Settings::Keys keys;

    applyHotkeys(QStringLiteral("[keys]\nsequence_timeout_ms = 2000\nambiguity_timeout_ms = 250\n"),
                 keymap, &keys);

    QCOMPARE(keys.sequenceTimeoutMs, 2000);
    QCOMPARE(keys.ambiguityTimeoutMs, 250);
}

void TestConfig::malformedHotkeysKeepTheDefaultKeymap()
{
    // An application with no keybindings would be unusable, so a broken
    // hotkeys.toml must leave the defaults untouched.
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);
    const int before = keymap.bindingCount(input::KeymapLayer::Normal);

    const HotkeysLoadResult result = applyHotkeys(QStringLiteral("[normal\nlist_down = "), keymap,
                                                  nullptr, QStringLiteral("hotkeys.toml"));

    QCOMPARE(result.issues.size(), 1);
    QVERIFY(result.issues.first().line > 0);
    QCOMPARE(keymap.bindingCount(input::KeymapLayer::Normal), before);
}

QTEST_MAIN(TestConfig)
#include "tst_config.moc"
