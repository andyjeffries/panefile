#include "ui/modals/CommandPaletteModal.h"

#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

using namespace pf::ui;

namespace {

QList<PaletteItem> sample()
{
    return {
        {.id = "open_fuzzy_find",
         .title = "Find a file anywhere below this directory",
         .detail = "General",
         .keys = "Ctrl+F",
         .group = 1,
         .path = {}},
        {.id = "convert_image_to_webp",
         .title = "Convert the selected images to WebP",
         .detail = "Image",
         .keys = {},
         .group = 0,
         .path = {}},
        {.id = "copy_path",
         .title = "Copy the cursor item's absolute path",
         .detail = "File operations",
         .keys = "Ctrl+P",
         .group = 1,
         .path = {}},
        {.id = {},
         .title = "Downloads",
         .detail = "~/Downloads",
         .keys = {},
         .group = 2,
         .path = "/home/x/Downloads"},
    };
}

QStringList titles(const QList<PaletteRow> &rows)
{
    QStringList result;
    for (const PaletteRow &row : rows) {
        result << row.item.title;
    }
    return result;
}

} // namespace

class TestCommandPalette : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// With nothing typed, the selection's actions come first, then the rest,
    /// then places — each group in the order it was given.
    void anEmptyQueryKeepsTheGroupsInOrder()
    {
        const QList<PaletteRow> rows = CommandPaletteModal::rank(sample(), QString());
        QCOMPARE(titles(rows), (QStringList{"Convert the selected images to WebP",
                                            "Find a file anywhere below this directory",
                                            "Copy the cursor item's absolute path", "Downloads"}));
    }

    /// A query ranks within a group, but never lifts a place over an action.
    void aQueryRanksWithinGroups()
    {
        const QList<PaletteRow> rows = CommandPaletteModal::rank(sample(), QStringLiteral("co"));
        QVERIFY(!rows.isEmpty());
        QCOMPARE(rows.first().item.id, QStringLiteral("convert_image_to_webp"));
        // Match spans cover the title's matched characters.
        QVERIFY(!rows.first().spans.isEmpty());
        for (qsizetype i = 1; i < rows.size(); ++i) {
            QVERIFY(rows.at(i - 1).item.group <= rows.at(i).item.group);
        }
    }

    void nonMatchesAreDropped()
    {
        QVERIFY(CommandPaletteModal::rank(sample(), QStringLiteral("zzzq")).isEmpty());
    }

    /// The id is searched too, though only a title match is highlighted.
    void theIdIsSearchedAsAFallback()
    {
        const QList<PaletteRow> rows =
            CommandPaletteModal::rank(sample(), QStringLiteral("open_fuzzy"));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().item.id, QStringLiteral("open_fuzzy_find"));
        QVERIFY(rows.first().spans.isEmpty());
    }

    void keysAreCarriedForBoundActions()
    {
        const QList<PaletteRow> rows = CommandPaletteModal::rank(sample(), QString());
        QCOMPARE(rows.at(1).item.keys, QStringLiteral("Ctrl+F"));
    }

    void enterChoosesTheCurrentRow()
    {
        QWidget parent;
        parent.resize(800, 600);
        parent.show();
        CommandPaletteModal palette(&parent);
        QSignalSpy action(&palette, &CommandPaletteModal::actionChosen);
        QSignalSpy place(&palette, &CommandPaletteModal::placeChosen);

        palette.open(sample(), QStringLiteral("Image"));
        QTest::keyClick(&palette, Qt::Key_Return);
        QCOMPARE(action.size(), 1);
        QCOMPARE(action.first().first().toString(), QStringLiteral("convert_image_to_webp"));

        palette.open(sample(), QString());
        palette.setQuery(QStringLiteral("down"));
        QTest::keyClick(&palette, Qt::Key_Return);
        QCOMPARE(place.size(), 1);
        QCOMPARE(place.first().first().toString(), QStringLiteral("/home/x/Downloads"));
    }

    void downMovesPastTheHeadings()
    {
        QWidget parent;
        parent.resize(800, 600);
        parent.show();
        CommandPaletteModal palette(&parent);
        QSignalSpy action(&palette, &CommandPaletteModal::actionChosen);
        palette.open(sample(), QStringLiteral("Image"));
        QTest::keyClick(&palette, Qt::Key_Down); // over the "Actions" heading to the next row
        QTest::keyClick(&palette, Qt::Key_Return);
        QCOMPARE(action.first().first().toString(), QStringLiteral("open_fuzzy_find"));
    }

    /// Nothing matches but the text is an exact action id: it still runs, as the
    /// prompt this replaces did.
    void anExactIdWithNoRowsStillInvokes()
    {
        QWidget parent;
        parent.resize(800, 600);
        parent.show();
        CommandPaletteModal palette(&parent);
        QSignalSpy action(&palette, &CommandPaletteModal::actionChosen);
        palette.open(QList<PaletteItem>{}, QString());
        palette.setQuery(QStringLiteral("bulk_rename"));
        QTest::keyClick(&palette, Qt::Key_Return);
        QCOMPARE(action.size(), 1);
        QCOMPARE(action.first().first().toString(), QStringLiteral("bulk_rename"));
    }

    void escapeChoosesNothing()
    {
        QWidget parent;
        parent.resize(800, 600);
        parent.show();
        CommandPaletteModal palette(&parent);
        QSignalSpy action(&palette, &CommandPaletteModal::actionChosen);
        palette.open(sample(), QString());
        QTest::keyClick(&palette, Qt::Key_Escape);
        QCOMPARE(action.size(), 0);
    }
};

QTEST_MAIN(TestCommandPalette)
#include "tst_commandpalette.moc"
