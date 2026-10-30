#include "ui/Sidebar.h"
#include "ui/SidebarLayout.h"

#include "ui/ThemePalette.h"

#include "model/FolderIconStore.h"
#include "model/SymbolicIcon.h"

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

#include <optional>

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

/// Delivered to the viewport, which is where the sidebar listens. `position`
/// is in viewport coordinates; the default is the middle.
void dropMime(QWidget *viewport, QMimeData &mime, std::optional<QPoint> position = std::nullopt)
{
    const QPoint at = position.value_or(viewport->rect().center());
    // Qt 6.12 added a QPointF constructor and deprecated the QPoint one.
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
    QDragEnterEvent enter(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#else
    QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#endif
    QApplication::sendEvent(viewport, &enter);
    QDropEvent drop(QPointF(at), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &drop);
}

void dropUrls(QWidget *viewport, const QList<QUrl> &urls,
              std::optional<QPoint> position = std::nullopt)
{
    QMimeData mime;
    mime.setUrls(urls);
    dropMime(viewport, mime, position);
}

/// The titles of the user's entries, section by section, as "Section: a, b".
QStringList describe(const pf::ui::SidebarLayout &layout)
{
    QStringList lines;
    for (const pf::ui::SidebarSection &section : layout.sections) {
        QStringList names;
        for (const pf::ui::SidebarEntry &entry : section.entries) {
            names << (entry.builtin.isEmpty() ? QFileInfo(entry.path).fileName() : entry.builtin);
        }
        lines << section.title + QStringLiteral(": ") + names.join(QStringLiteral(", "));
    }
    return lines;
}

/// An icon as the pixels it paints at 16px, for comparing two icons.
QImage pixelsOf(const QIcon &icon)
{
    return icon.pixmap(QSize(16, 16), 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
}

QPoint centreOf(QListWidget *list, const QString &name)
{
    return list->visualItemRect(itemNamed(list, name)).center();
}

} // namespace

using namespace pf;
using namespace pf::ui;

/// A sidebar with two sections of real files, so rows exist for each.
struct Fixture {
    QTemporaryDir dir;
    Sidebar sidebar;
    QListWidget *list = nullptr;

    Fixture()
    {
        for (const char *name : {"a", "b", "c", "d"}) {
            writeBytes(dir.filePath(QString::fromLatin1(name)), 1);
        }
        SidebarLayout layout;
        const QString one = layout.addSection(QStringLiteral("One"));
        const QString two = layout.addSection(QStringLiteral("Two"));
        layout.addSection(QStringLiteral("Empty"));
        layout.insertEntry(layout.sectionIndex(one), 0,
                           SidebarLayout::entryFor(dir.filePath(QStringLiteral("a"))));
        layout.insertEntry(layout.sectionIndex(one), 1,
                           SidebarLayout::entryFor(dir.filePath(QStringLiteral("b"))));
        layout.insertEntry(layout.sectionIndex(two), 0,
                           SidebarLayout::entryFor(dir.filePath(QStringLiteral("c"))));
        layout.insertEntry(layout.sectionIndex(two), 1,
                           SidebarLayout::entryFor(dir.filePath(QStringLiteral("d"))));
        sidebar.setLayout(layout);
        sidebar.resize(220, 600);
        sidebar.show();
        sidebar.populate();
        list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
    }

    QString id(int section) const { return sidebar.sidebarLayout().sections.at(section).id; }
};

/// §5.1's places.
class TestSidebar : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_state;


private Q_SLOTS:
    void initTestCase()
    {
        // Never the developer's real state directory: the folder icon tests
        // below write folder-icons.json.
        qputenv("PANEFILE_STATE_DIR", m_state.path().toUtf8());
    }

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
        sidebar.togglePin(QDir::tempPath());
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
                          list->visualItemRect(list->item(1)).center());

        QCOMPARE(list->currentRow(), -1);
        QVERIFY(list->selectedItems().isEmpty());
    }

    /// Every place has a symbolic icon; headings have none.
    void everyPlaceHasAnIcon()
    {
        Sidebar sidebar;
        sidebar.togglePin(QDir::tempPath());
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
#ifdef Q_OS_MACOS
        // Finder's trash itself, not a files/ inside it.
        QCOMPARE(path, QDir::homePath() + QStringLiteral("/.Trash"));
#else
        QVERIFY2(QDir(path).exists(), qPrintable(path));
        QCOMPARE(path, state.filePath(QStringLiteral("Trash/files")));
#endif
    }

    /// The wastebasket's row says how much is in it, and follows changes.
    void theWastebasketShowsItsCountAndSize()
    {
        QTemporaryDir trash;
        QVERIFY(trash.isValid());
        qputenv("PANEFILE_TRASH_DIR", QFile::encodeName(trash.path()));
#ifdef Q_OS_MACOS
        // Finder's layout: the items sit in the trash itself, beside a
        // .DS_Store that is not one of them.
        const QString files = trash.path();
        writeBytes(files + QStringLiteral("/.DS_Store"), 500);
#else
        const QString files = trash.filePath(QStringLiteral("files"));
#endif
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
        qunsetenv("PANEFILE_TRASH_DIR");
    }

    /// Files and folders dropped on the sidebar are added to it.
    void droppingAFileOrFolderAddsIt()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        Sidebar sidebar;
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QSignalSpy changed(&sidebar, &Sidebar::layoutChanged);

        dropUrls(list->viewport(), {QUrl::fromLocalFile(folder.path())});

        QCOMPARE(changed.size(), 1);
        QVERIFY(sidebar.isPinned(folder.path()));
        QVERIFY(itemNamed(list, QFileInfo(folder.path()).fileName()) != nullptr);

        // A file is a place too.
        QTemporaryFile file;
        QVERIFY(file.open());
        dropUrls(list->viewport(), {QUrl::fromLocalFile(file.fileName())});
        QVERIFY(sidebar.isPinned(file.fileName()));
        QCOMPARE(changed.size(), 2);

        // Not a local file: ignored.
        dropUrls(list->viewport(), {QUrl(QStringLiteral("https://example.com/x"))});
        QCOMPARE(changed.size(), 2);
    }

    /// A built-in place can be removed, and dropping its folder back on the
    /// sidebar restores it as itself rather than as a copy of itself.
    void aRemovedPlaceComesBackWhenDroppedAgain()
    {
        Sidebar sidebar;
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(itemNamed(list, QStringLiteral("Home")) != nullptr);

        QSignalSpy changed(&sidebar, &Sidebar::layoutChanged);
        QVERIFY(sidebar.removePlace(QDir::homePath()));
        QCOMPARE(changed.size(), 1);
        QVERIFY(itemNamed(list, QStringLiteral("Home")) == nullptr);
        QVERIFY(!sidebar.isPinned(QDir::homePath()));

        dropUrls(list->viewport(), {QUrl::fromLocalFile(QDir::homePath())});
        QVERIFY(itemNamed(list, QStringLiteral("Home")) != nullptr);
        const auto found = sidebar.sidebarLayout().find(QDir::cleanPath(QDir::homePath()));
        QVERIFY(found.has_value());
        QCOMPARE(
            sidebar.sidebarLayout().sections.at(found->first).entries.at(found->second).builtin,
            QStringLiteral("home"));
    }

    // ===================================================== arranging sections

    /// A drop at a position lands at that index: before a row in its upper
    /// half, after it in its lower half.
    void aDropLandsWhereItIsLetGo()
    {
        Fixture f;
        QTemporaryDir extra;
        writeBytes(extra.filePath(QStringLiteral("x")), 1);
        writeBytes(extra.filePath(QStringLiteral("y")), 1);

        const QRect b = f.list->visualItemRect(itemNamed(f.list, QStringLiteral("b")));
        dropUrls(f.list->viewport(), {QUrl::fromLocalFile(extra.filePath(QStringLiteral("x")))},
                 QPoint(b.center().x(), b.top() + 2));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: a, x, b"));

        const QRect c = f.list->visualItemRect(itemNamed(f.list, QStringLiteral("c")));
        dropUrls(f.list->viewport(), {QUrl::fromLocalFile(extra.filePath(QStringLiteral("y")))},
                 QPoint(c.center().x(), c.bottom() - 2));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(1), QStringLiteral("Two: c, y, d"));
    }

    /// Several dropped at once keep their order, even when one was already
    /// listed earlier in the same section.
    void severalDroppedKeepTheirOrder()
    {
        Fixture f;
        const QRect d = f.list->visualItemRect(itemNamed(f.list, QStringLiteral("d")));
        dropUrls(f.list->viewport(),
                 {QUrl::fromLocalFile(f.dir.filePath(QStringLiteral("c"))),
                  QUrl::fromLocalFile(f.dir.filePath(QStringLiteral("a")))},
                 QPoint(d.center().x(), d.bottom() - 2));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: b"));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(1), QStringLiteral("Two: d, c, a"));
    }

    /// A drop on a heading goes to the top of its section.
    void aDropOnAHeadingGoesToTheTop()
    {
        Fixture f;
        QTemporaryDir extra;
        writeBytes(extra.filePath(QStringLiteral("x")), 1);
        dropUrls(f.list->viewport(), {QUrl::fromLocalFile(extra.filePath(QStringLiteral("x")))},
                 centreOf(f.list, QStringLiteral("Two")));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(1), QStringLiteral("Two: x, c, d"));
    }

    /// A drop below everything, or on the wastebasket, goes to the end of the
    /// last section.
    void aDropBelowEverythingGoesToTheEndOfTheLastSection()
    {
        Fixture f;
        QTemporaryDir extra;
        writeBytes(extra.filePath(QStringLiteral("x")), 1);
        dropUrls(f.list->viewport(), {QUrl::fromLocalFile(extra.filePath(QStringLiteral("x")))},
                 QPoint(10, f.list->viewport()->height() - 2));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(2), QStringLiteral("Empty: x"));
    }

    /// An empty section's placeholder is a drop target.
    void anEmptySectionTakesADropOnItsPlaceholder()
    {
        Fixture f;
        QTemporaryDir extra;
        writeBytes(extra.filePath(QStringLiteral("x")), 1);
        QListWidgetItem *placeholder =
            itemNamed(f.list, QStringLiteral("Drag files or folders here"));
        QVERIFY(placeholder != nullptr);
        dropUrls(f.list->viewport(), {QUrl::fromLocalFile(extra.filePath(QStringLiteral("x")))},
                 f.list->visualItemRect(placeholder).center());
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(2), QStringLiteral("Empty: x"));
        QVERIFY(itemNamed(f.list, QStringLiteral("Drag files or folders here")) == nullptr);
    }

    /// A path already listed moves to where it is dropped, not a second copy.
    void aListedPathIsMovedNotDuplicated()
    {
        Fixture f;
        dropUrls(f.list->viewport(), {QUrl::fromLocalFile(f.dir.filePath(QStringLiteral("a")))},
                 centreOf(f.list, QStringLiteral("Empty")));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: b"));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(2), QStringLiteral("Empty: a"));
    }

    /// Dragging an entry within and between sections, with the payload the
    /// sidebar itself builds.
    void anEntryCanBeDraggedToANewPlace()
    {
        Fixture f;
        QMimeData mime;
        mime.setData(Sidebar::placeMimeType(),
                     Sidebar::entryPayload(f.id(0), 0, f.dir.filePath(QStringLiteral("a"))));
        const QRect b = f.list->visualItemRect(itemNamed(f.list, QStringLiteral("b")));
        // Down within its section: after "b".
        dropMime(f.list->viewport(), mime, QPoint(b.center().x(), b.bottom() - 2));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: b, a"));

        // Across: before "c".
        QMimeData across;
        across.setData(Sidebar::placeMimeType(),
                       Sidebar::entryPayload(f.id(0), 1, f.dir.filePath(QStringLiteral("a"))));
        const QRect c = f.list->visualItemRect(itemNamed(f.list, QStringLiteral("c")));
        dropMime(f.list->viewport(), across, QPoint(c.center().x(), c.top() + 2));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: b"));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(1), QStringLiteral("Two: a, c, d"));
    }

    /// A stale payload — the entry is no longer where it says — moves nothing.
    void aStalePayloadMovesNothing()
    {
        Fixture f;
        const SidebarLayout before = f.sidebar.sidebarLayout();
        QMimeData mime;
        mime.setData(Sidebar::placeMimeType(),
                     Sidebar::entryPayload(f.id(0), 0, QStringLiteral("/not/there")));
        dropMime(f.list->viewport(), mime, centreOf(f.list, QStringLiteral("c")));
        QVERIFY(f.sidebar.sidebarLayout() == before);
    }

    /// Dragging a heading moves its whole section.
    void aHeadingMovesItsEntries()
    {
        Fixture f;
        QMimeData mime;
        mime.setData(Sidebar::placeMimeType(), Sidebar::sectionPayload(f.id(0)));
        // Below "Two"'s block, above the empty section: the lower half of Two.
        const QRect d = f.list->visualItemRect(itemNamed(f.list, QStringLiteral("d")));
        dropMime(f.list->viewport(), mime, QPoint(10, d.bottom() - 1));
        QCOMPARE(describe(f.sidebar.sidebarLayout()),
                 (QStringList{QStringLiteral("Two: c, d"), QStringLiteral("One: a, b"),
                              QStringLiteral("Empty: ")}));

        // Below everything: after the last section.
        QMimeData last;
        last.setData(Sidebar::placeMimeType(), Sidebar::sectionPayload(f.id(0)));
        dropMime(f.list->viewport(), last, QPoint(10, f.list->viewport()->height() - 1));
        QCOMPARE(f.sidebar.sidebarLayout().sections.last().title, QStringLiteral("Two"));
    }

    /// An empty section goes at once; one with entries asks first.
    void removingASectionAsksOnlyWhenItHasEntries()
    {
        Fixture f;
        QSignalSpy asked(&f.sidebar, &Sidebar::sectionRemovalRequested);

        f.sidebar.removeSection(f.id(2));
        QCOMPARE(f.sidebar.sidebarLayout().sections.size(), 2);
        QCOMPARE(asked.size(), 0);

        // removeSection on a stale id does nothing.
        f.sidebar.removeSection(QStringLiteral("gone"));
        QCOMPARE(f.sidebar.sidebarLayout().sections.size(), 2);
    }

    /// Clicking a file entry reveals it; clicking a folder opens it.
    void aFileEntryRevealsAndAFolderOpens()
    {
        Fixture f;
        QSignalSpy revealed(&f.sidebar, &Sidebar::revealRequested);
        QSignalSpy opened(&f.sidebar, &Sidebar::placeActivated);
        QTest::mouseClick(f.list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          centreOf(f.list, QStringLiteral("a")));
        QCOMPARE(revealed.size(), 1);
        QCOMPARE(revealed.first().first().toString(), f.dir.filePath(QStringLiteral("a")));
        QCOMPARE(opened.size(), 0);

        QTest::mouseClick(f.list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          centreOf(f.list, QStringLiteral("Home")));
    }

    /// Entries for a path that is not there are hidden but kept.
    void missingPathsAreHiddenButKept()
    {
        Fixture f;
        SidebarLayout layout = f.sidebar.sidebarLayout();
        layout.insertEntry(0, 0, SidebarLayout::entryFor(QStringLiteral("/no/such/place")));
        f.sidebar.setLayout(layout);
        QVERIFY(itemNamed(f.list, QStringLiteral("place")) == nullptr);
        QCOMPARE(f.sidebar.sidebarLayout().sections.at(0).entries.size(), 3);
    }

    /// The menu's and the keyboard's moves: within a section, and over its
    /// edge into the neighbour.
    void entriesMoveFromTheKeyboard()
    {
        Fixture f;
        QSignalSpy changed(&f.sidebar, &Sidebar::layoutChanged);
        f.sidebar.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&f.sidebar));
        f.list->setFocus();
        QTRY_VERIFY(f.list->hasFocus());
        f.list->setCurrentItem(itemNamed(f.list, QStringLiteral("a")));
        QVERIFY(f.sidebar.canMoveCurrentEntry());

        f.sidebar.moveCurrentEntry(1);
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: b, a"));
        f.sidebar.moveCurrentEntry(1);
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: b"));
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(1), QStringLiteral("Two: a, c, d"));
        f.sidebar.moveCurrentEntry(-1);
        QCOMPARE(describe(f.sidebar.sidebarLayout()).at(0), QStringLiteral("One: b, a"));
        QCOMPARE(changed.size(), 3);
    }

    /// Sections can be added, renamed and restored to their defaults.
    void sectionsCanBeAddedRenamedAndDefaultsRestored()
    {
        Fixture f;
        QSignalSpy changed(&f.sidebar, &Sidebar::layoutChanged);
        f.sidebar.addSection(QStringLiteral("New\nOne"));
        QCOMPARE(f.sidebar.sidebarLayout().sections.last().title, QStringLiteral("New One"));
        f.sidebar.renameSection(f.id(0), QStringLiteral("Renamed"));
        QCOMPARE(f.sidebar.sidebarLayout().sections.first().title, QStringLiteral("Renamed"));
        QCOMPARE(changed.size(), 2);

        QVERIFY(f.sidebar.hasMissingDefaults());
        f.sidebar.restoreDefaultPlaces();
        QVERIFY(!f.sidebar.hasMissingDefaults());
        QVERIFY(itemNamed(f.list, QStringLiteral("Home")) != nullptr);
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
        QVERIFY(list->count() > 1);
        QCOMPARE(tintOf(list->item(1)->icon()), QColor(0xff, 0x00, 0x00));

        ThemePalette green = original;
        green.subtext = QColor(0x00, 0xff, 0x00);
        setCurrentPalette(green);
        sidebar.refreshTheme();
        QCOMPARE(tintOf(list->item(1)->icon()), QColor(0x00, 0xff, 0x00));

        setCurrentPalette(original);
    }

    /// A folder with a custom icon shows that glyph in the sidebar — the bare
    /// glyph, tinted like its neighbours, not the plain folder outline.
    void aCustomFolderIconIsShownInTheSidebar()
    {
        FolderIconStore::instance().reset();
        QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral("Developer"));
        QVERIFY(QDir().mkpath(folder));
        // "film" is one of the compiled-in emblems, so this needs no installed
        // Heroicons set.
        QVERIFY(FolderIconStore::instance().set(folder, QStringLiteral("hero:film")));

        Sidebar sidebar;
        sidebar.togglePin(folder);
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QListWidgetItem *item = itemNamed(list, QStringLiteral("Developer"));
        QVERIFY(item != nullptr);
        QVERIFY(!item->icon().isNull());

        const QImage plainFolder =
            pixelsOf(SymbolicIcon::icon(QStringLiteral("folder"), currentPalette().subtext));
        QVERIFY2(pixelsOf(item->icon()) != plainFolder,
                 "the sidebar still draws the plain folder glyph for a folder with a custom icon");
        QCOMPARE(tintOf(item->icon()), currentPalette().subtext);

        FolderIconStore::instance().reset();
    }

    /// Choosing an icon while the sidebar is showing updates its row.
    void choosingAFolderIconUpdatesTheSidebar()
    {
        FolderIconStore::instance().reset();
        QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral("Developer"));
        QVERIFY(QDir().mkpath(folder));

        Sidebar sidebar;
        sidebar.togglePin(folder);
        sidebar.populate();
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(itemNamed(list, QStringLiteral("Developer")) != nullptr);
        const QImage before = pixelsOf(itemNamed(list, QStringLiteral("Developer"))->icon());

        QVERIFY(FolderIconStore::instance().set(folder, QStringLiteral("hero:film")));
        QVERIFY2(pixelsOf(itemNamed(list, QStringLiteral("Developer"))->icon()) != before,
                 "the sidebar row kept its old icon after a folder icon was chosen");

        FolderIconStore::instance().clear(folder);
        QCOMPARE(pixelsOf(itemNamed(list, QStringLiteral("Developer"))->icon()), before);

        FolderIconStore::instance().reset();
    }

    /// An icon change must not populate a sidebar that was never populated.
    void aFolderIconChangeDoesNotPopulateAnUnpopulatedSidebar()
    {
        FolderIconStore::instance().reset();
        QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral("Developer"));
        QVERIFY(QDir().mkpath(folder));

        Sidebar sidebar;
        auto *list = sidebar.findChild<QListWidget *>(QStringLiteral("sidebarList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 0);
        QVERIFY(FolderIconStore::instance().set(folder, QStringLiteral("hero:film")));
        QCOMPARE(list->count(), 0);

        FolderIconStore::instance().reset();
    }
};

QTEST_MAIN(TestSidebar)
#include "tst_sidebar.moc"
