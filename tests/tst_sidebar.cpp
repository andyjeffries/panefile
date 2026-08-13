#include "ui/Sidebar.h"

#include <QApplication>
#include <QDir>
#include <QListWidget>
#include <QSignalSpy>
#include <QTest>

using namespace pf;
using namespace pf::ui;

/// §5.1's places.
class TestSidebar : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// A single click opens a place.
    ///
    /// The sidebar was wired only to itemActivated, which on macOS is emitted
    /// on a *double* click — the style does not activate on a single one. So
    /// clicking a place did nothing at all, on the platform this was being
    /// developed on.
    void aSingleClickOpensAPlace()
    {
        Sidebar sidebar;
        sidebar.resize(200, 400);
        sidebar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&sidebar));
        sidebar.populate();

        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(list != nullptr);
        QVERIFY(list->count() > 0);

        // "Home" is always present; find the first row that is a real place.
        int row = -1;
        for (int i = 0; i < list->count(); ++i) {
            if (!list->item(i)->text().isEmpty() && (list->item(i)->flags() & Qt::ItemIsEnabled)) {
                row = i;
                break;
            }
        }
        QVERIFY(row >= 0);

        QSignalSpy opened(&sidebar, &Sidebar::placeActivated);

        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(list->item(row)).center());

        QCOMPARE(opened.count(), 1);
        QVERIFY(!opened.first().at(0).toString().isEmpty());
    }

    /// A heading is not a place, however it is clicked.
    void clickingAHeadingDoesNothing()
    {
        Sidebar sidebar;
        sidebar.resize(200, 400);
        sidebar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&sidebar));
        sidebar.setPinnedPaths({QDir::tempPath()});
        sidebar.populate();

        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(list != nullptr);

        int heading = -1;
        for (int i = 0; i < list->count(); ++i) {
            if ((list->item(i)->flags() & Qt::ItemIsEnabled) == 0) {
                heading = i;
                break;
            }
        }
        QVERIFY2(heading >= 0, "a pinned path should have produced a heading");

        QSignalSpy opened(&sidebar, &Sidebar::placeActivated);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(list->item(heading)).center());

        QCOMPARE(opened.count(), 0);
    }

    /// Opening a place leaves nothing highlighted: these are shortcuts, not a
    /// state, and in a window of several panels a highlight is true of none.
    void openingAPlaceLeavesNoHighlight()
    {
        Sidebar sidebar;
        sidebar.resize(200, 400);
        sidebar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&sidebar));
        sidebar.populate();

        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(list != nullptr);
        QVERIFY(list->count() > 0);

        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(list->item(0)).center());

        QCOMPARE(list->currentRow(), -1);
        QVERIFY(list->selectedItems().isEmpty());
    }
};

QTEST_MAIN(TestSidebar)
#include "tst_sidebar.moc"
