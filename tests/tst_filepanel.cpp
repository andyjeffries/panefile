// The file panel end to end (§5.2, §14).
//
// Runs under the offscreen platform plugin, so it exercises real widgets, a
// real scan and the real delegate without needing a display. The last test
// renders the panel to a PNG in the build directory: a listing that is
// technically correct but paints its columns on top of each other passes every
// assertion above it, and the image is the only thing that catches that.

#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "ui/CursorMemory.h"
#include "ui/FilePanel.h"

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

QTEST_MAIN(TestFilePanel)
#include "tst_filepanel.moc"
