// The file panel end to end (§5.2, §14).
//
// Runs under the offscreen platform plugin, so it exercises real widgets, a
// real scan and the real delegate without needing a display. The last test
// renders the panel to a PNG in the build directory: a listing that is
// technically correct but paints its columns on top of each other passes every
// assertion above it, and the image is the only thing that catches that.

#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "config/StyleSheetBuilder.h"
#include "config/Theme.h"
#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "ui/CursorMemory.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"
#include "ui/Sidebar.h"
#include "ui/ThemePalette.h"
#include "ui/modals/HelpModal.h"

#include <QDir>
#include <QFile>
#include <QListView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::ui;

class TestFilePanel : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;

    QString path(const QString &relative) const
    {
        return m_root.path() + QLatin1Char('/') + relative;
    }

    /// Navigates and waits for the scan to settle.
    static void navigateAndSettle(FilePanel &panel, const QString &target)
    {
        QSignalSpy pathChanged(&panel, &FilePanel::pathChanged);
        panel.navigateTo(target);
        QTRY_VERIFY_WITH_TIMEOUT(panel.view()->model()->rowCount() > 0 || !pathChanged.isEmpty(),
                                 5000);
        // The row count settling is what says the scan delivered; give the
        // queued batches a turn of the event loop to arrive.
        QTest::qWait(150);
    }

private Q_SLOTS:

    /// §10.2: "A path that is a file, not a directory, navigates to its parent
    /// and places the cursor on it."
    ///
    /// The cursor is asked for before the scan has delivered a single row —
    /// which is the case for every caller that navigates and then names a
    /// cursor: the command line, the single-instance hand-off, session restore
    /// and the recursive finder. A search that simply failed left the cursor on
    /// whatever sorted first, which is not the file the user named.
    void aCursorNamedBeforeTheScanArrivesIsStillApplied()
    {
        QTemporaryDir dir;
        for (const char *name : {"aaa.txt", "target.txt", "zzz.txt"}) {
            QFile file(dir.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
        }

        FilePanel panel;
        panel.navigateTo(dir.path());

        // Immediately: the scan runs on a worker and has certainly not
        // finished.
        panel.setCursorName(QStringLiteral("target.txt"));

        QTRY_COMPARE_WITH_TIMEOUT(panel.cursorName(), QStringLiteral("target.txt"), 5000);
    }

    /// A name that never arrives must not leave the panel with no cursor at all.
    void aCursorNamedForAMissingFileFallsBackToTheFirstRow()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("only.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        FilePanel panel;
        panel.navigateTo(dir.path());
        panel.setCursorName(QStringLiteral("never-existed.txt"));

        QTRY_COMPARE_WITH_TIMEOUT(panel.cursorName(), QStringLiteral("only.txt"), 5000);
    }

    void initTestCase();

    void listsTheDirectory();
    void hiddenFilesAreHiddenUntilToggled();
    void cursorMovesAndClamps();
    void enteringADirectoryChangesPath();
    void goingUpPutsTheCursorOnTheDirectoryJustLeft();
    void historyGoesBackAndForward();
    void backAtTheStartOfHistoryReportsIt();
    void activatingAFileEmitsItsAbsolutePath();
    void unreadableDirectoryReportsAnError();
    void rendersARecognisableListing();
    void rendersTheWholeWindow();
    void rendersTheHelpModal();

private:
    void renderWithTheme(const QString &themeName, const QString &outputName);
};

void TestFilePanel::initTestCase()
{
    QVERIFY(m_root.isValid());

    QDir root(m_root.path());
    QVERIFY(root.mkdir(QStringLiteral("alpha")));
    QVERIFY(root.mkdir(QStringLiteral("beta")));
    QVERIFY(QDir(path(QStringLiteral("alpha"))).mkdir(QStringLiteral("nested")));

    for (const auto &[name, size] :
         {std::pair{QStringLiteral("readme.md"), 4300}, std::pair{QStringLiteral("notes.txt"), 120},
          std::pair{QStringLiteral("archive.tar.gz"), 98000},
          std::pair{QStringLiteral("photo.png"), 250000},
          std::pair{QStringLiteral("script.sh"), 640}, std::pair{QStringLiteral(".hidden"), 10}}) {
        QFile file(path(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(size, 'x'));
    }

    QVERIFY(QFile::setPermissions(path(QStringLiteral("script.sh")), QFileDevice::ReadOwner |
                                                                         QFileDevice::WriteOwner |
                                                                         QFileDevice::ExeOwner));

    QVERIFY(QFile::link(path(QStringLiteral("readme.md")), path(QStringLiteral("readme-link.md"))));
    QVERIFY(QFile::link(path(QStringLiteral("gone")), path(QStringLiteral("dangling"))));
}

void TestFilePanel::listsTheDirectory()
{
    FilePanel panel;
    navigateAndSettle(panel, m_root.path());

    QCOMPARE(panel.path(), QDir::cleanPath(m_root.path()));

    // Two directories and seven visible entries; .hidden is filtered.
    QCOMPARE(panel.view()->model()->rowCount(), 9);
}

void TestFilePanel::hiddenFilesAreHiddenUntilToggled()
{
    FilePanel panel;
    navigateAndSettle(panel, m_root.path());

    const int visible = panel.view()->model()->rowCount();

    panel.toggleShowHidden();
    QVERIFY(panel.showHidden());
    QCOMPARE(panel.view()->model()->rowCount(), visible + 1);

    panel.toggleShowHidden();
    QCOMPARE(panel.view()->model()->rowCount(), visible);
}

void TestFilePanel::cursorMovesAndClamps()
{
    FilePanel panel;
    navigateAndSettle(panel, m_root.path());

    panel.moveCursorToStart();
    const QString first = panel.cursorName();
    QVERIFY(!first.isEmpty());

    // Directories sort first, so the top of the list is a directory.
    QCOMPARE(first, QStringLiteral("alpha"));

    panel.moveCursor(1);
    QCOMPARE(panel.cursorName(), QStringLiteral("beta"));

    // Clamping rather than wrapping: holding k at the top of a listing should
    // stop, not teleport to the bottom.
    panel.moveCursor(-50);
    QCOMPARE(panel.cursorName(), first);

    panel.moveCursorToEnd();
    const QString last = panel.cursorName();
    panel.moveCursor(50);
    QCOMPARE(panel.cursorName(), last);
}

void TestFilePanel::enteringADirectoryChangesPath()
{
    FilePanel panel;
    navigateAndSettle(panel, m_root.path());

    panel.setCursorName(QStringLiteral("alpha"));
    QCOMPARE(panel.cursorName(), QStringLiteral("alpha"));

    panel.activateCursorItem();
    QTest::qWait(200);

    QCOMPARE(panel.path(), QDir::cleanPath(path(QStringLiteral("alpha"))));
    QCOMPARE(panel.view()->model()->rowCount(), 1);
}

void TestFilePanel::goingUpPutsTheCursorOnTheDirectoryJustLeft()
{
    // §5.2, and the single behaviour that makes keyboard navigation feel like
    // moving around a filesystem rather than operating a list widget.
    CursorMemory::instance().clear();

    FilePanel panel;
    navigateAndSettle(panel, path(QStringLiteral("alpha")));

    panel.goToParent();
    QTest::qWait(200);

    QCOMPARE(panel.path(), QDir::cleanPath(m_root.path()));
    QCOMPARE(panel.cursorName(), QStringLiteral("alpha"));
}

void TestFilePanel::historyGoesBackAndForward()
{
    FilePanel panel;
    navigateAndSettle(panel, m_root.path());

    navigateAndSettle(panel, path(QStringLiteral("beta")));
    QCOMPARE(panel.path(), QDir::cleanPath(path(QStringLiteral("beta"))));

    QVERIFY(panel.goBack());
    QTest::qWait(200);
    QCOMPARE(panel.path(), QDir::cleanPath(m_root.path()));

    QVERIFY(panel.goForward());
    QTest::qWait(200);
    QCOMPARE(panel.path(), QDir::cleanPath(path(QStringLiteral("beta"))));
}

void TestFilePanel::backAtTheStartOfHistoryReportsIt()
{
    FilePanel panel;
    QSignalSpy status(&panel, &FilePanel::statusMessage);

    navigateAndSettle(panel, m_root.path());

    QVERIFY(!panel.goBack());
    QVERIFY(!status.isEmpty());
}

void TestFilePanel::activatingAFileEmitsItsAbsolutePath()
{
    FilePanel panel;
    navigateAndSettle(panel, m_root.path());

    QSignalSpy activated(&panel, &FilePanel::fileActivated);

    panel.setCursorName(QStringLiteral("notes.txt"));
    panel.activateCursorItem();

    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.first().first().toString(),
             QDir::cleanPath(m_root.path()) + QStringLiteral("/notes.txt"));
    // Activating a file must not navigate the panel.
    QCOMPARE(panel.path(), QDir::cleanPath(m_root.path()));
}

void TestFilePanel::unreadableDirectoryReportsAnError()
{
    // §7.2: an inline error naming the reason, with the previous listing still
    // reachable through go_back.
    FilePanel panel;
    QSignalSpy status(&panel, &FilePanel::statusMessage);

    navigateAndSettle(panel, m_root.path());
    status.clear();

    panel.navigateTo(path(QStringLiteral("does-not-exist")));
    QTRY_VERIFY_WITH_TIMEOUT(!status.isEmpty(), 5000);

    QVERIFY(status.first().first().toString().contains(QStringLiteral("No such file")));
    QVERIFY(panel.goBack());
}

void TestFilePanel::rendersARecognisableListing()
{
    FilePanel panel;
    panel.resize(760, 320);
    panel.setActive(true);
    navigateAndSettle(panel, m_root.path());
    panel.toggleShowHidden();
    panel.setCursorName(QStringLiteral("readme.md"));

    QTest::qWait(100);

    const QPixmap shot = panel.grab();
    QVERIFY(!shot.isNull());
    QCOMPARE(shot.size(), QSize(760, 320) * shot.devicePixelRatio());

    // Written where a human can look at it. QT_TESTCASE_BUILDDIR is the test's
    // own build directory, so this does not litter the source tree.
    const QString output =
        QStringLiteral("%1/filepanel-render.png").arg(QLatin1String(QT_TESTCASE_BUILDDIR));
    QVERIFY2(shot.save(output), qPrintable(output));
    qInfo("rendered panel written to %s", qPrintable(output));

    // The rows must actually have been drawn: an all-background image would
    // pass every assertion above and is exactly the failure this test exists
    // to catch.
    const QImage image = shot.toImage();
    QSet<QRgb> distinctColours;
    for (int y = 0; y < image.height(); y += 2) {
        for (int x = 0; x < image.width(); x += 2) {
            distinctColours.insert(image.pixel(x, y));
        }
    }
    QVERIFY2(distinctColours.size() > 8,
             qPrintable(QStringLiteral("only %1 distinct colours — the listing did not paint")
                            .arg(distinctColours.size())));
}

void TestFilePanel::rendersTheWholeWindow()
{
    // Renders through the real stylesheet, from a real bundled theme, so what
    // the image shows is what a user gets rather than what the defaults happen
    // to be.
    renderWithTheme(QStringLiteral("macos-light"), QStringLiteral("window-render.png"));
    renderWithTheme(QStringLiteral("catppuccin-mocha"), QStringLiteral("window-render-mocha.png"));
    renderWithTheme(QStringLiteral("gruvbox-light"), QStringLiteral("window-render-gruvbox.png"));
}

void TestFilePanel::renderWithTheme(const QString &themeName, const QString &outputName)
{
    // The whole application chrome as a user sees it: sidebar, three panels and
    // the footer. Assertions cannot tell you that the sidebar is drawing on top
    // of the first panel; the image can.
    const config::ThemeLoadResult loaded = config::loadThemeByName(themeName);
    QVERIFY2(loaded.issues.isEmpty(), qPrintable(themeName));
    ui::setCurrentPalette(loaded.theme);
    qApp->setStyleSheet(config::buildStyleSheet(loaded.theme));

    ui::MainWindow window;
    window.resize(1200, 520);
    window.sidebar()->populate();

    window.panelStrip()->addPanel(m_root.path());
    window.panelStrip()->addPanel(path(QStringLiteral("alpha")));
    window.panelStrip()->addPanel(QDir::homePath());
    QTest::qWait(400);

    window.panelStrip()->focusPanelAt(0);
    window.panelStrip()->focusedPanel()->setCursorName(QStringLiteral("readme.md"));
    window.showPendingKeys(QStringLiteral("g-"));
    QTest::qWait(150);

    const QPixmap shot = window.grab();
    QVERIFY(!shot.isNull());

    const QString output =
        QStringLiteral("%1/%2").arg(QLatin1String(QT_TESTCASE_BUILDDIR), outputName);
    QVERIFY2(shot.save(output), qPrintable(output));
    qInfo("rendered %s written to %s", qPrintable(themeName), qPrintable(output));
}

void TestFilePanel::rendersTheHelpModal()
{
    // §6.3's help modal, generated entirely from the registry and keymap.
    input::ActionRegistry registry;
    input::Keymap keymap;
    input::installDefaultKeymap(keymap);

    for (const auto &[id, description, category] :
         {std::tuple{"list_down", "Move the cursor down", input::ActionCategory::Movement},
          std::tuple{"list_up", "Move the cursor up", input::ActionCategory::Movement},
          std::tuple{"go_home", "Go to the home directory", input::ActionCategory::Movement},
          std::tuple{"list_top", "Move to the first entry", input::ActionCategory::Movement},
          std::tuple{"create_new_file_panel", "Open a new panel at home",
                     input::ActionCategory::Panels},
          std::tuple{"split_file_panel", "Duplicate the focused panel",
                     input::ActionCategory::Panels},
          std::tuple{"close_file_panel", "Close the focused panel", input::ActionCategory::Panels},
          std::tuple{"next_file_panel", "Focus the next panel", input::ActionCategory::Panels},
          std::tuple{"copy_items", "Copy the selection", input::ActionCategory::FileOperations},
          std::tuple{"paste_items", "Paste into this directory",
                     input::ActionCategory::FileOperations},
          std::tuple{"delete_items", "Move the selection to the trash",
                     input::ActionCategory::FileOperations},
          std::tuple{"open_help_menu", "Show every action and its keys",
                     input::ActionCategory::General}}) {
        registry.registerAction(QLatin1String(id), QLatin1String(description), category, [] {});
    }

    ui::MainWindow window;
    window.resize(1000, 620);

    auto *help = new ui::HelpModal(registry, keymap, &window);
    help->showModal();
    QTest::qWait(200);

    const QPixmap shot = window.grab();
    QVERIFY(!shot.isNull());

    const QString output =
        QStringLiteral("%1/help-render.png").arg(QLatin1String(QT_TESTCASE_BUILDDIR));
    QVERIFY2(shot.save(output), qPrintable(output));
    qInfo("rendered help modal written to %s", qPrintable(output));
}

QTEST_MAIN(TestFilePanel)
#include "tst_filepanel.moc"
