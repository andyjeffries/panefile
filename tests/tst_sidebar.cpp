#include "ui/Sidebar.h"

#include "ui/ThemePalette.h"

#include "core/Format.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QListWidget>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QUrl>

namespace {

/// The colour of the most opaque pixel of an icon, i.e. its tint.
QColor tintOf(const QIcon &icon)
{
    const QImage image =
        icon.pixmap(QSize(16, 16), 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
    QColor strongest(Qt::transparent);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() > strongest.alpha()) {
                strongest = colour;
            }
        }
    }
    strongest.setAlpha(255);
    return strongest;
}

QListWidgetItem *itemNamed(QListWidget *list, const QString &name)
{
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->text() == name) {
            return list->item(i);
        }
    }
    return nullptr;
}

void writeBytes(const QString &path, int count)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray(count, 'x'));
    }
}

/// Delivered to the viewport, which is where the sidebar listens.
void dropUrls(QWidget *viewport, const QList<QUrl> &urls)
{
    QMimeData mime;
    mime.setUrls(urls);
    const QPoint centre = viewport->rect().center();
    QDragEnterEvent enter(centre, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &enter);
    QDropEvent drop(QPointF(centre), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &drop);
}

} // namespace

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

    /// Every place has a symbolic icon; headings have none.
    void everyPlaceHasAnIcon()
    {
        Sidebar sidebar;
        sidebar.setPinnedPaths({QDir::tempPath()});
        sidebar.populate();

        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(list != nullptr);

        int places = 0;
        for (int i = 0; i < list->count(); ++i) {
            const QListWidgetItem *item = list->item(i);
            const bool heading = (item->flags() & Qt::ItemIsEnabled) == 0;
            QCOMPARE(item->icon().isNull(), heading);
            places += heading ? 0 : 1;
        }
        // Home and the pinned temp directory, at least.
        QVERIFY(places >= 2);
    }

    /// The wastebasket is listed under a divider, and opens the trash's files
    /// directory — created if nothing has been trashed yet, so it opens onto an
    /// empty folder rather than an error.
    void theWastebasketFollowsADivider()
    {
        QTemporaryDir state;
        QVERIFY(state.isValid());
        // The trash is the state directory's sibling on Linux.
        qputenv("PANEFILE_STATE_DIR",
                QFile::encodeName(state.filePath(QStringLiteral("panefile"))));

        Sidebar sidebar;
        sidebar.populate();
        qunsetenv("PANEFILE_STATE_DIR");

        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(list != nullptr);

        int wastebasket = -1;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->text() == QStringLiteral("Wastebasket")) {
                wastebasket = i;
            }
        }
        QVERIFY2(wastebasket > 0, "the wastebasket should be listed, after something");

        const QListWidgetItem *divider = list->item(wastebasket - 1);
        QVERIFY(divider->text().isEmpty());
        QCOMPARE(divider->flags(), Qt::NoItemFlags);

        QSignalSpy activated(&sidebar, &Sidebar::placeActivated);
        list->itemClicked(list->item(wastebasket));
        QCOMPARE(activated.size(), 1);
        const QString path = activated.first().first().toString();
        QVERIFY2(QDir(path).exists(), qPrintable(path));
#ifndef Q_OS_MACOS
        QCOMPARE(path, state.filePath(QStringLiteral("Trash/files")));
#endif
    }

    /// The wastebasket's row says how much is in it, and follows changes.
    void theWastebasketShowsItsCountAndSize()
    {
        QTemporaryDir state;
        QVERIFY(state.isValid());
        qputenv("PANEFILE_STATE_DIR",
                QFile::encodeName(state.filePath(QStringLiteral("panefile"))));
        const QString files = state.filePath(QStringLiteral("Trash/files"));
        QVERIFY(QDir().mkpath(files + QStringLiteral("/folder")));
        writeBytes(files + QStringLiteral("/a.bin"), 1000);
        writeBytes(files + QStringLiteral("/folder/b.bin"), 2000);

        Sidebar sidebar;
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(itemNamed(list, QStringLiteral("Wastebasket")) != nullptr);

        // Two entries at the top, three thousand bytes under them.
        const QString expected = QStringLiteral("2 · %1").arg(formatSize(3000));
        QTRY_VERIFY2(itemNamed(list, QStringLiteral("Wastebasket"))->toolTip().endsWith(expected),
                     qPrintable(itemNamed(list, QStringLiteral("Wastebasket"))->toolTip()));

        // Emptied: no summary, and it notices by itself.
        QVERIFY(QDir(files + QStringLiteral("/folder")).removeRecursively());
        QVERIFY(QFile::remove(files + QStringLiteral("/a.bin")));
        QTRY_VERIFY_WITH_TIMEOUT(itemNamed(list, QStringLiteral("Wastebasket"))
                                     ->toolTip()
                                     .endsWith(QStringLiteral("empty")),
                                 5000);
        qunsetenv("PANEFILE_STATE_DIR");
    }

    /// A folder dropped on the sidebar is pinned there.
    void droppingAFolderPinsIt()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        Sidebar sidebar;
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QSignalSpy changed(&sidebar, &Sidebar::pinnedPathsChanged);

        dropUrls(list->viewport(), {QUrl::fromLocalFile(folder.path())});

        QCOMPARE(changed.size(), 1);
        QCOMPARE(sidebar.pinnedPaths(), QStringList{QDir::cleanPath(folder.path())});
        QVERIFY(itemNamed(list, QFileInfo(folder.path()).fileName()) != nullptr);

        // A file is not a place.
        QTemporaryFile file;
        QVERIFY(file.open());
        dropUrls(list->viewport(), {QUrl::fromLocalFile(file.fileName())});
        QCOMPARE(sidebar.pinnedPaths().size(), 1);
    }

    /// A built-in place can be removed, and dropping its folder back on the
    /// sidebar restores it as itself rather than as a pinned copy.
    void aRemovedPlaceComesBackWhenDroppedAgain()
    {
        Sidebar sidebar;
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(itemNamed(list, QStringLiteral("Home")) != nullptr);

        QSignalSpy hidden(&sidebar, &Sidebar::hiddenPlacesChanged);
        QVERIFY(sidebar.removePlace(QDir::homePath()));
        QCOMPARE(hidden.size(), 1);
        QVERIFY(itemNamed(list, QStringLiteral("Home")) == nullptr);
        QCOMPARE(sidebar.hiddenPlaces(), QStringList{QDir::cleanPath(QDir::homePath())});

        dropUrls(list->viewport(), {QUrl::fromLocalFile(QDir::homePath())});
        QVERIFY(itemNamed(list, QStringLiteral("Home")) != nullptr);
        QVERIFY(sidebar.hiddenPlaces().isEmpty());
        QVERIFY(sidebar.pinnedPaths().isEmpty());
    }

    /// A theme change re-tints the icons. They are pixmaps, which the
    /// stylesheet cannot reach, so without refreshTheme() they kept the colour
    /// of whichever theme was current when the sidebar was built.
    void refreshThemeRetintsIcons()
    {
        const ThemePalette original = currentPalette();

        ThemePalette red = original;
        red.subtext = QColor(0xff, 0x00, 0x00);
        setCurrentPalette(red);

        Sidebar sidebar;
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(list != nullptr);
        QVERIFY(list->count() > 0);
        QCOMPARE(tintOf(list->item(0)->icon()), QColor(0xff, 0x00, 0x00));

        ThemePalette green = original;
        green.subtext = QColor(0x00, 0xff, 0x00);
        setCurrentPalette(green);
        sidebar.refreshTheme();
        QCOMPARE(tintOf(list->item(0)->icon()), QColor(0x00, 0xff, 0x00));

        setCurrentPalette(original);
    }
};

QTEST_MAIN(TestSidebar)
#include "tst_sidebar.moc"
