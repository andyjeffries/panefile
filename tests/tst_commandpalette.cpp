#include "ui/modals/CommandPaletteModal.h"

#include <QLineEdit>
#include <QListWidget>
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
         .path = {},
         .icon = {}},
        {.id = "convert_image_to_webp",
         .title = "Convert the selected images to WebP",
         .detail = "Image",
         .keys = {},
         .group = 0,
         .path = {},
         .icon = {}},
        {.id = "copy_path",
         .title = "Copy the cursor item's absolute path",
         .detail = "File operations",
         .keys = "Ctrl+P",
         .group = 1,
         .path = {},
         .icon = {}},
        {.id = {},
         .title = "Downloads",
         .detail = "~/Downloads",
         .keys = {},
         .group = 2,
         .path = "/home/x/Downloads",
         .icon = {}},
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

        palette.open(sample());
        QTest::keyClick(&palette, Qt::Key_Return);
        QCOMPARE(action.size(), 1);
        QCOMPARE(action.first().first().toString(), QStringLiteral("convert_image_to_webp"));

        palette.open(sample());
        palette.setQuery(QStringLiteral("down"));
        QTest::keyClick(&palette, Qt::Key_Return);
        QCOMPARE(place.size(), 1);
        QCOMPARE(place.first().first().toString(), QStringLiteral("/home/x/Downloads"));
    }

    void downMovesToTheNextRow()
    {
        QWidget parent;
        parent.resize(800, 600);
        parent.show();
        CommandPaletteModal palette(&parent);
        QSignalSpy action(&palette, &CommandPaletteModal::actionChosen);
        palette.open(sample());
        // No heading rows: the list holds exactly the visible items.
        QCOMPARE(palette.findChild<QListWidget *>()->count(),
                 static_cast<int>(palette.visibleItems().size()));
        QTest::keyClick(&palette, Qt::Key_Down);
        QTest::keyClick(&palette, Qt::Key_Return);
        QCOMPARE(action.first().first().toString(), QStringLiteral("open_fuzzy_find"));
    }

    void aClickRunsTheRow()
    {
        QWidget parent;
        parent.resize(800, 600);
        parent.show();
        CommandPaletteModal palette(&parent);
        QSignalSpy action(&palette, &CommandPaletteModal::actionChosen);
        palette.open(sample());
        QListWidget *list = palette.findChild<QListWidget *>();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(list->item(1)).center());
        QCOMPARE(action.size(), 1);
        QCOMPARE(action.first().first().toString(), QStringLiteral("open_fuzzy_find"));
        QVERIFY(!palette.isVisible());
    }

    void clickingOutsideTheCardDismisses()
    {
        QWidget parent;
        parent.resize(800, 600);
        parent.show();
        CommandPaletteModal palette(&parent);
        QSignalSpy action(&palette, &CommandPaletteModal::actionChosen);
        palette.open(sample());
        QVERIFY(palette.isVisible());
        QTest::mouseClick(&palette, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QCOMPARE(action.size(), 0);
        QVERIFY(!palette.isVisible());
    }

    void theCardIsCapped()
    {
        QWidget parent;
        parent.resize(2400, 1600);
        parent.show();
        CommandPaletteModal palette(&parent);
        palette.open(sample());
        QVERIFY(palette.contentWidget()->width() <= 640);
        QVERIFY(palette.contentWidget()->height() <= 460);
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
        palette.open(QList<PaletteItem>{});
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
        palette.open(sample());
        QTest::keyClick(&palette, Qt::Key_Escape);
        QCOMPARE(action.size(), 0);
    }
};

QTEST_MAIN(TestCommandPalette)
#include "tst_commandpalette.moc"
