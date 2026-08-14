#include "ui/FileItemDelegate.h"

#include <QTest>

using pf::ui::FileItemDelegate;

namespace {

constexpr int kNameLeft = 30;
constexpr int kGap = 12;

} // namespace

/// How a row divides itself between the name and the columns to its right.
class TestColumns : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// A directory prints no size, so it must not reserve room for one.
    ///
    /// It did, and that is the whole of the reported bug: 64 pixels plus a gap
    /// held back for a column that would never be drawn into, so "Applications"
    /// rendered as "Ap…ns" in a panel with room to spare.
    void directoriesReclaimTheSizeColumn()
    {
        const auto directory = FileItemDelegate::columnsFor(kNameLeft, 500, kGap, false, true);
        const auto file = FileItemDelegate::columnsFor(kNameLeft, 500, kGap, true, true);

        QVERIFY(!directory.showSize);
        QVERIFY(file.showSize);
        QVERIFY2(directory.nameRight > file.nameRight, "a directory's name should have more room");
    }

    /// The name never ends up with negative width, which is what made five
    /// panels show no filenames at all rather than short ones.
    void theNameNeverCollapses()
    {
        // Absurdly narrow: narrower than either column on its own.
        for (const int rowRight : {40, 60, 90, 120, 160, 200}) {
            const auto columns =
                FileItemDelegate::columnsFor(kNameLeft, rowRight, kGap, true, true);
            QVERIFY2(columns.nameRight >= kNameLeft,
                     qPrintable(QStringLiteral("rowRight=%1 gave nameRight=%2")
                                    .arg(rowRight)
                                    .arg(columns.nameRight)));
        }
    }

    /// Columns are dropped widest-first, and the name keeps what they release.
    void columnsAreDroppedBeforeTheNameIsSqueezed()
    {
        // Wide: both columns fit.
        const auto wide = FileItemDelegate::columnsFor(kNameLeft, 600, kGap, true, true);
        QVERIFY(wide.showSize);
        QVERIFY(wide.showTime);

        // Middling: the size goes, the date stays, and the name takes the room
        // the size column would have held — compared at the same width, which
        // is the only comparison that means anything.
        constexpr int kRowRight = 260;
        const auto middling = FileItemDelegate::columnsFor(kNameLeft, kRowRight, kGap, true, true);
        QVERIFY(!middling.showSize);
        QVERIFY(middling.showTime);

        // Where the name would have ended if both columns had been reserved.
        constexpr int kBothReserved = kRowRight - 78 - kGap - 64 - kGap;
        QVERIFY2(middling.nameRight > kBothReserved,
                 qPrintable(QStringLiteral("nameRight=%1, both-reserved would be %2")
                                .arg(middling.nameRight)
                                .arg(kBothReserved)));

        // Narrow: the date goes too, and the name takes the whole row.
        const auto narrow = FileItemDelegate::columnsFor(kNameLeft, 150, kGap, true, true);
        QVERIFY(!narrow.showSize);
        QVERIFY(!narrow.showTime);
        QCOMPARE(narrow.nameRight, 150);
    }

    /// An entry that could not be stat'ed has no date to show, and the name
    /// should have that room rather than a blank column.
    void anUnstattableEntryShowsNeitherColumn()
    {
        const auto columns = FileItemDelegate::columnsFor(kNameLeft, 500, kGap, false, false);

        QVERIFY(!columns.showSize);
        QVERIFY(!columns.showTime);
        QCOMPARE(columns.nameRight, 500);
    }
};

QTEST_MAIN(TestColumns)
#include "tst_columns.moc"
