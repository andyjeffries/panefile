#include "ui/SidebarLayout.h"

#include "fs/Trash.h"

#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace pf::ui;

namespace {

/// "Title: a, b" per section, built-ins by key, paths by file name.
QStringList describe(const SidebarLayout &layout)
{
    QStringList lines;
    for (const SidebarSection &section : layout.sections) {
        QStringList names;
        for (const SidebarEntry &entry : section.entries) {
            names << (entry.builtin.isEmpty() ? QFileInfo(entry.path).fileName() : entry.builtin);
        }
        lines << section.title + QStringLiteral(": ") + names.join(QStringLiteral(", "));
    }
    return lines;
}

/// A layout of sections "A", "B" holding the given path entries.
SidebarLayout make(const QStringList &a, const QStringList &b)
{
    SidebarLayout layout;
    layout.addSection(QStringLiteral("A"));
    layout.addSection(QStringLiteral("B"));
    for (const QString &name : a) {
        layout.sections[0].entries.append({.builtin = {}, .path = QStringLiteral("/x/") + name});
    }
    for (const QString &name : b) {
        layout.sections[1].entries.append({.builtin = {}, .path = QStringLiteral("/x/") + name});
    }
    return layout;
}

} // namespace

class TestSidebarLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTripsIncludingAwkwardPaths()
    {
        SidebarLayout layout = SidebarLayout::defaults();
        layout.wastebasketHidden = true;
        const QString pinned = layout.addSection(QStringLiteral("Pinned"));
        SidebarSection &section = layout.sections[layout.sectionIndex(pinned)];
        section.entries.append({.builtin = {}, .path = QStringLiteral("/tmp/with space/a=b")});
        section.entries.append({.builtin = {}, .path = QStringLiteral("/tmp/trailing ")});

        const SidebarLayout restored = SidebarLayout::fromIni(layout.toIni());
        QVERIFY(restored == layout);
    }

    void newlinesAndBackslashesInPathsRoundTripAndCannotInject()
    {
        SidebarLayout layout;
        const QString id = layout.addSection(QStringLiteral("S"));
        SidebarSection &section = layout.sections[layout.sectionIndex(id)];
        section.entries.append({.builtin = {}, .path = QStringLiteral("/tmp/a\nb")});
        section.entries.append({.builtin = {}, .path = QStringLiteral("/tmp/x\n[section]\ntitle=Injected\nentry=/etc")});
        section.entries.append({.builtin = {}, .path = QStringLiteral("/tmp/back\\slash\\n")});
        section.entries.append({.builtin = {}, .path = QStringLiteral("/tmp/c")});

        const SidebarLayout restored = SidebarLayout::fromIni(layout.toIni());
        QCOMPARE(restored.sections.size(), 1);
        QVERIFY(restored == layout);
    }

    void aPathEqualToABuiltinIsDroppedOnLoad()
    {
        const SidebarLayout layout = SidebarLayout::fromIni(
            QStringLiteral("[section]\nid=a\ntitle=x\nentry=builtin:home\nentry=") +
            QDir::homePath() + QLatin1Char('\n'));
        QCOMPARE(layout.sections.at(0).entries.size(), 1);
    }

    void dropsRelativePathsAndUnknownBuiltins()
    {
        const SidebarLayout layout = SidebarLayout::fromIni(QStringLiteral(
            "[sidebar]\nversion=1\n\n[section]\nid=one\ntitle=T\n"
            "entry=relative/path\nentry=builtin:nonsense\nentry=builtin:home\nentry=/ok\n"));
        QCOMPARE(describe(layout), QStringList{QStringLiteral("T: home, ok")});
    }

    void repairsDuplicateAndMissingIdsAndEntries()
    {
        const SidebarLayout layout = SidebarLayout::fromIni(QStringLiteral(
            "[section]\nid=same\ntitle=A\nentry=/p\n\n[section]\nid=same\ntitle=B\nentry=/p\n"));
        QCOMPARE(layout.sections.size(), 2);
        QVERIFY(layout.sections.at(0).id != layout.sections.at(1).id);
        // The path appears once in the whole sidebar.
        QCOMPARE(layout.sections.at(0).entries.size() + layout.sections.at(1).entries.size(), 1);
    }

    void titlesAreOneLine()
    {
        SidebarLayout layout;
        layout.addSection(QStringLiteral("two\nlines"));
        QCOMPARE(layout.sections.first().title, QStringLiteral("two lines"));
    }

    void defaultsHoldEveryBuiltinInOneSection()
    {
        const SidebarLayout layout = SidebarLayout::defaults();
        QCOMPARE(layout.sections.size(), 1);
        QCOMPARE(layout.sections.first().entries.size(), SidebarLayout::builtinKeys().size());
        QVERIFY(!layout.wastebasketHidden);
    }

    void migratesPinsAndRemovedPlaces()
    {
        QTemporaryDir pins;
        QVERIFY(pins.isValid());
        const QString downloads =
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        const QString trash = pf::fs::Trash().filesDirectory();

        QString session = QStringLiteral("[window]\nfocused=0\n\n[pinned]\n0=%1\n1=/gone/folder\n")
                              .arg(pins.path());
        if (!downloads.isEmpty()) {
            session += QStringLiteral("2=%1\n").arg(downloads);
        }
        session += QStringLiteral("\n[removedplaces]\n0=%1\n1=%2\n").arg(QDir::homePath(), trash);

        const SidebarLayout layout = SidebarLayout::migratedFrom(session);
        QCOMPARE(layout.sections.size(), 2);
        QCOMPARE(layout.sections.at(0).title, QStringLiteral("Favourites"));
        QCOMPARE(layout.sections.at(1).title, QStringLiteral("Pinned"));
        QVERIFY(layout.wastebasketHidden || trash.isEmpty());

        // Home was removed, so it is not a Favourite.
        for (const SidebarEntry &entry : layout.sections.at(0).entries) {
            QVERIFY(entry.builtin != QLatin1String("home"));
        }

        // Pins come over in order, missing ones too, and a pinned built-in
        // folder becomes the built-in.
        const QList<SidebarEntry> pinned = layout.sections.at(1).entries;
        QCOMPARE(pinned.at(0).path, pins.path());
        QCOMPARE(pinned.at(1).path, QStringLiteral("/gone/folder"));
        if (!SidebarLayout::builtinPath(QStringLiteral("downloads")).isEmpty()) {
            QCOMPARE(pinned.at(2).builtin, QStringLiteral("downloads"));
        }
    }

    void migrationWithoutPinsHasNoPinnedSection()
    {
        const SidebarLayout layout = SidebarLayout::migratedFrom(QStringLiteral("[window]\n"));
        QCOMPARE(layout.sections.size(), 1);
    }

    void loadMigratesOnceAndSaves()
    {
        QTemporaryDir state;
        QVERIFY(state.isValid());
        qputenv("PANEFILE_STATE_DIR", QFile::encodeName(state.path()));

        QFile session(state.filePath(QStringLiteral("session.ini")));
        QVERIFY(session.open(QIODevice::WriteOnly));
        session.write("[pinned]\n0=/pinned/thing\n");
        session.close();

        const SidebarLayout first = SidebarLayout::load();
        QVERIFY(QFile::exists(state.filePath(QStringLiteral("sidebar.ini"))));
        QCOMPARE(first.sections.last().entries.first().path, QStringLiteral("/pinned/thing"));

        // The session's pins no longer matter once sidebar.ini exists.
        QVERIFY(session.remove());
        QVERIFY(SidebarLayout::load() == first);
        qunsetenv("PANEFILE_STATE_DIR");
    }

    void noFilesMeansDefaults()
    {
        QTemporaryDir state;
        qputenv("PANEFILE_STATE_DIR", QFile::encodeName(state.path()));
        const SidebarLayout layout = SidebarLayout::load();
        qunsetenv("PANEFILE_STATE_DIR");
        QCOMPARE(layout.sections.size(), 1);
    }

    void insertEntryDeduplicatesAcrossSections()
    {
        SidebarLayout layout = make({"a", "b"}, {"c"});
        layout.insertEntry(1, 0, {.builtin = {}, .path = QStringLiteral("/x/a")});
        QCOMPARE(describe(layout),
                 (QStringList{QStringLiteral("A: b"), QStringLiteral("B: a, c")}));
    }

    void insertEntryAdjustsForAnEarlierCopyInTheSameSection()
    {
        SidebarLayout layout = make({"a", "b", "c"}, {});
        // "a" to the gap after "b": the copy at 0 goes, so it lands at 1.
        layout.insertEntry(0, 2, {.builtin = {}, .path = QStringLiteral("/x/a")});
        QCOMPARE(describe(layout).at(0), QStringLiteral("A: b, a, c"));
    }

    void moveEntryWithinASection()
    {
        SidebarLayout layout = make({"a", "b", "c"}, {});
        layout.moveEntry(0, 0, 0, 3); // to the end
        QCOMPARE(describe(layout).at(0), QStringLiteral("A: b, c, a"));
        layout.moveEntry(0, 2, 0, 0); // back to the start
        QCOMPARE(describe(layout).at(0), QStringLiteral("A: a, b, c"));
        layout.moveEntry(0, 1, 0, 2); // onto its own gap: unchanged
        QCOMPARE(describe(layout).at(0), QStringLiteral("A: a, b, c"));
    }

    void moveEntryAcrossSections()
    {
        SidebarLayout layout = make({"a", "b"}, {"c"});
        layout.moveEntry(0, 1, 1, 0);
        QCOMPARE(describe(layout),
                 (QStringList{QStringLiteral("A: a"), QStringLiteral("B: b, c")}));
    }

    void moveSectionTakesGapIndexes()
    {
        SidebarLayout layout = make({"a"}, {"b"});
        layout.addSection(QStringLiteral("C"));
        layout.moveSection(0, 3);
        QCOMPARE(layout.sections.at(2).title, QStringLiteral("A"));
        layout.moveSection(2, 0);
        QCOMPARE(layout.sections.at(0).title, QStringLiteral("A"));
        // Dropping a section in its own gap changes nothing.
        layout.moveSection(1, 2);
        QCOMPARE(layout.sections.at(1).title, QStringLiteral("B"));
        // Out of range is ignored.
        layout.moveSection(9, 0);
        QCOMPARE(layout.sections.size(), 3);
    }

    void entryForMapsABuiltinLocation()
    {
        QCOMPARE(SidebarLayout::entryFor(QDir::homePath() + QLatin1Char('/')).builtin,
                 QStringLiteral("home"));
        const SidebarEntry other = SidebarLayout::entryFor(QStringLiteral("/some/where/"));
        QVERIFY(other.builtin.isEmpty());
        QCOMPARE(other.path, QStringLiteral("/some/where"));
    }
};

QTEST_MAIN(TestSidebarLayout)
#include "tst_sidebarlayout.moc"
