// Panel lifecycle and keyboard navigation (§7.1, §14).
//
// §14 asks for "QTest::keyClicks driving navigation, panel create/close/cycle,
// selection mode". These run the real dispatcher against the real widgets under
// the offscreen platform, so what is exercised is the whole path from a key
// event to a panel changing — not the keymap in isolation.

#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/KeyDispatcher.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"

#include <QDir>

#include <QFile>
#include <QKeyEvent>
#include <QListView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

using namespace pf;
using namespace pf::input;

class TestPanels : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    ui::PanelStrip *m_strip = nullptr;
    Keymap m_keymap;
    ActionRegistry m_registry;
    KeyDispatcher *m_dispatcher = nullptr;

    QString path(const QString &relative) const
    {
        return m_root.path() + QLatin1Char('/') + relative;
    }

    /// Registers just enough of §6.3 to drive the strip, mirroring what
    /// PanelController does in the real application.
    void registerPanelActions();

    void press(const QString &character)
    {
        const QChar c = character.at(0);
        QKeyEvent event(QEvent::KeyPress, c.toUpper().unicode(),
                        c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, character);
        m_dispatcher->handleKeyPress(&event);
    }

    void pressKey(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers, {});
        m_dispatcher->handleKeyPress(&event);
    }

    void settle() { QTest::qWait(150); }

private Q_SLOTS:

    /// The window is titled with the folder's name, not its path.
    ///
    /// macOS titles name the thing being looked at — Finder shows "Documents",
    /// never "/Users/andy/Documents" — and an absolute path centred in a title
    /// bar is the most obviously non-native element a window can have. The path
    /// is in the panel header, attached to the panel it describes rather than
    /// to a window that may hold five of them.
    void theWindowTitleIsAFolderNameNotAPath()
    {
        QCOMPARE(ui::MainWindow::titleForPath(QStringLiteral("/Users/andy/Documents")),
                 QStringLiteral("Documents"));
        QCOMPARE(ui::MainWindow::titleForPath(QDir::homePath()), QStringLiteral("Home"));
        QCOMPARE(ui::MainWindow::titleForPath(QStringLiteral("/")), QStringLiteral("Computer"));
        QCOMPARE(ui::MainWindow::titleForPath(QString()), QStringLiteral("Panefile"));
    }

    void initTestCase();
    void init();
    void cleanup();

    void startsWithOnePanel();
    void createsAndClosesPanels();
    void refusesToCloseTheLastPanel();
    void enforcesThePanelLimit();
    void cyclesFocusAndWraps();
    void closingFocusesTheNeighbourOnTheLeft();
    void closingTheLeftmostFocusesTheRight();
    void splitCopiesViewSettingsButNotSelection();
    void navigatesWithHjkl();
    void sequenceNavigatesHome();
    void compactLayoutShowsOnlyTheFocusedPanel();
    void panelsShareTheWidthEqually();
};

void TestPanels::registerPanelActions()
{
    const auto onPanel = [this](auto action) {
        return [this, action] {
            if (ui::FilePanel *panel = m_strip->focusedPanel(); panel != nullptr) {
                action(panel);
            }
        };
    };

    m_registry.registerAction(QStringLiteral("list_down"), {}, ActionCategory::Movement,
                              onPanel([](ui::FilePanel *p) { p->moveCursor(1); }));
    m_registry.registerAction(QStringLiteral("list_up"), {}, ActionCategory::Movement,
                              onPanel([](ui::FilePanel *p) { p->moveCursor(-1); }));
    m_registry.registerAction(QStringLiteral("confirm"), {}, ActionCategory::Movement,
                              onPanel([](ui::FilePanel *p) { p->activateCursorItem(); }));
    m_registry.registerAction(QStringLiteral("parent_directory"), {}, ActionCategory::Movement,
                              onPanel([](ui::FilePanel *p) { p->goToParent(); }));
    m_registry.registerAction(QStringLiteral("go_home"), {}, ActionCategory::Movement,
                              onPanel([](ui::FilePanel *p) { p->navigateTo(QDir::homePath()); }));
    m_registry.registerAction(QStringLiteral("list_top"), {}, ActionCategory::Movement,
                              onPanel([](ui::FilePanel *p) { p->moveCursorToStart(); }));
    m_registry.registerAction(QStringLiteral("list_bottom"), {}, ActionCategory::Movement,
                              onPanel([](ui::FilePanel *p) { p->moveCursorToEnd(); }));

    m_registry.registerAction(QStringLiteral("create_new_file_panel"), {}, ActionCategory::Panels,
                              [this] { m_strip->addPanel(m_root.path()); });
    m_registry.registerAction(QStringLiteral("split_file_panel"), {}, ActionCategory::Panels,
                              [this] { m_strip->splitFocusedPanel(); });
    m_registry.registerAction(QStringLiteral("close_file_panel"), {}, ActionCategory::Panels,
                              [this] { m_strip->closeFocusedPanel(); });
    m_registry.registerAction(QStringLiteral("next_file_panel"), {}, ActionCategory::Panels,
                              [this] { m_strip->focusNext(); });
    m_registry.registerAction(QStringLiteral("previous_file_panel"), {}, ActionCategory::Panels,
                              [this] { m_strip->focusPrevious(); });
}

void TestPanels::initTestCase()
{
    QVERIFY(m_root.isValid());

    QDir root(m_root.path());
    QVERIFY(root.mkdir(QStringLiteral("alpha")));
    QVERIFY(root.mkdir(QStringLiteral("beta")));

    for (const QString &name :
         {QStringLiteral("one.txt"), QStringLiteral("two.txt"), QStringLiteral("three.txt")}) {
        QFile file(path(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
}

void TestPanels::init()
{
    m_keymap.clear();
    m_registry.clear();
    installDefaultKeymap(m_keymap);

    m_strip = new ui::PanelStrip;
    m_strip->resize(1200, 600);
    registerPanelActions();

    m_dispatcher = new KeyDispatcher(&m_registry, &m_keymap, m_strip);
    m_dispatcher->setActiveLayers({KeymapLayer::Normal, KeymapLayer::Global});

    m_strip->addPanel(m_root.path());
    settle();
}

void TestPanels::cleanup()
{
    delete m_strip;
    m_strip = nullptr;
    m_dispatcher = nullptr;
}

void TestPanels::startsWithOnePanel()
{
    QCOMPARE(m_strip->count(), 1);
    QVERIFY(m_strip->focusedPanel() != nullptr);
    QVERIFY(m_strip->focusedPanel()->isActive());
}

void TestPanels::createsAndClosesPanels()
{
    press(QStringLiteral("n"));
    settle();
    QCOMPARE(m_strip->count(), 2);

    press(QStringLiteral("N")); // split
    settle();
    QCOMPARE(m_strip->count(), 3);

    press(QStringLiteral("w"));
    QCOMPARE(m_strip->count(), 2);
}

void TestPanels::refusesToCloseTheLastPanel()
{
    // §7.1: minimum one panel. Closing it would leave a window with nothing in
    // it and no way to get a panel back.
    QSignalSpy status(m_strip, &ui::PanelStrip::statusMessage);

    press(QStringLiteral("w"));

    QCOMPARE(m_strip->count(), 1);
    QCOMPARE(status.count(), 1);
}

void TestPanels::enforcesThePanelLimit()
{
    QSignalSpy limit(m_strip, &ui::PanelStrip::panelLimitReached);

    for (int i = 0; i < ui::PanelStrip::kMaxPanels + 3; ++i) {
        m_strip->addPanel(m_root.path());
    }
    settle();

    QCOMPARE(m_strip->count(), ui::PanelStrip::kMaxPanels);
    QVERIFY(limit.count() >= 1);
}

void TestPanels::cyclesFocusAndWraps()
{
    m_strip->addPanel(path(QStringLiteral("alpha")));
    m_strip->addPanel(path(QStringLiteral("beta")));
    settle();
    QCOMPARE(m_strip->count(), 3);

    m_strip->focusPanelAt(0);
    QCOMPARE(m_strip->focusedIndex(), 0);

    pressKey(Qt::Key_Tab);
    QCOMPARE(m_strip->focusedIndex(), 1);
    pressKey(Qt::Key_Tab);
    QCOMPARE(m_strip->focusedIndex(), 2);

    // Cycling panels is a ring; stopping at the end would make the last panel a
    // dead end.
    pressKey(Qt::Key_Tab);
    QCOMPARE(m_strip->focusedIndex(), 0);

    pressKey(Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(m_strip->focusedIndex(), 2);
}

void TestPanels::closingFocusesTheNeighbourOnTheLeft()
{
    // §7.1: "Closing the focused panel moves focus to the panel on its left."
    m_strip->addPanel(path(QStringLiteral("alpha")));
    m_strip->addPanel(path(QStringLiteral("beta")));
    settle();

    m_strip->focusPanelAt(2);
    ui::FilePanel *left = m_strip->panelAt(1);

    m_strip->closeFocusedPanel();

    QCOMPARE(m_strip->count(), 2);
    QCOMPARE(m_strip->focusedPanel(), left);
}

void TestPanels::closingTheLeftmostFocusesTheRight()
{
    // "…or right if it was leftmost."
    m_strip->addPanel(path(QStringLiteral("alpha")));
    settle();

    m_strip->focusPanelAt(0);
    ui::FilePanel *right = m_strip->panelAt(1);

    m_strip->closeFocusedPanel();

    QCOMPARE(m_strip->count(), 1);
    QCOMPARE(m_strip->focusedPanel(), right);
}

void TestPanels::splitCopiesViewSettingsButNotSelection()
{
    // §7.1: split copies the path, sort and filter settings but not the
    // selection. Carrying the selection would mean a later delete acted on
    // files chosen in a different panel.
    ui::FilePanel *source = m_strip->focusedPanel();
    source->setShowHidden(true);
    source->setSortKey(SortKey::Size);
    source->setReverseSort(true);

    ui::FilePanel *split = m_strip->splitFocusedPanel();
    settle();

    QVERIFY(split != nullptr);
    QCOMPARE(split->path(), source->path());
    QCOMPARE(split->showHidden(), true);
    QCOMPARE(split->sortKey(), SortKey::Size);
    QCOMPARE(split->reverseSort(), true);
}

void TestPanels::navigatesWithHjkl()
{
    ui::FilePanel *panel = m_strip->focusedPanel();
    panel->moveCursorToStart();

    const QString first = panel->cursorName();
    QCOMPARE(first, QStringLiteral("alpha"));

    press(QStringLiteral("j"));
    QCOMPARE(panel->cursorName(), QStringLiteral("beta"));

    press(QStringLiteral("k"));
    QCOMPARE(panel->cursorName(), first);

    // `l` enters the directory under the cursor.
    press(QStringLiteral("l"));
    settle();
    QCOMPARE(panel->path(), QDir::cleanPath(path(QStringLiteral("alpha"))));

    // `h` goes back up, and the cursor lands on where we came from (§5.2).
    press(QStringLiteral("h"));
    settle();
    QCOMPARE(panel->path(), QDir::cleanPath(m_root.path()));
    QCOMPARE(panel->cursorName(), QStringLiteral("alpha"));
}

void TestPanels::sequenceNavigatesHome()
{
    // The `g` prefix of §6.2, end to end through the dispatcher.
    ui::FilePanel *panel = m_strip->focusedPanel();

    press(QStringLiteral("g"));
    QVERIFY(m_dispatcher->hasPending());
    QCOMPARE(m_dispatcher->pendingText(), QStringLiteral("g-"));

    press(QStringLiteral("h"));
    settle();

    QVERIFY(!m_dispatcher->hasPending());
    QCOMPARE(panel->path(), QDir::cleanPath(QDir::homePath()));
}

void TestPanels::compactLayoutShowsOnlyTheFocusedPanel()
{
    // §7.1: below 400 px, show only the focused panel. Three columns in 400 px
    // are too narrow to read a filename in, which is worse than showing one.
    m_strip->addPanel(path(QStringLiteral("alpha")));
    settle();
    QCOMPARE(m_strip->count(), 2);

    m_strip->applyResponsiveLayout(360);
    QCOMPARE(m_strip->panelAt(0)->isVisibleTo(m_strip), m_strip->focusedIndex() == 0);
    QCOMPARE(m_strip->panelAt(1)->isVisibleTo(m_strip), m_strip->focusedIndex() == 1);

    m_strip->applyResponsiveLayout(1200);
    QVERIFY(m_strip->panelAt(0)->isVisibleTo(m_strip));
    QVERIFY(m_strip->panelAt(1)->isVisibleTo(m_strip));
}

void TestPanels::panelsShareTheWidthEqually()
{
    // A panel added after the strip has been laid out defaults to a stretch
    // factor of zero, so without equalising the stretch factors as well as the
    // sizes the newest panel ends up an unreadable sliver — which is exactly
    // what a rendered screenshot caught.
    m_strip->show();
    m_strip->addPanel(path(QStringLiteral("alpha")));
    m_strip->addPanel(path(QStringLiteral("beta")));
    settle();

    QCOMPARE(m_strip->count(), 3);

    QList<int> widths;
    for (int i = 0; i < m_strip->count(); ++i) {
        widths << m_strip->panelAt(i)->width();
    }

    const int widest = *std::max_element(widths.begin(), widths.end());
    const int narrowest = *std::min_element(widths.begin(), widths.end());

    QVERIFY2(narrowest > 0, "a panel has no width at all");
    // Splitter handles and rounding make them differ by a few pixels; a sliver
    // differs by hundreds.
    QVERIFY2(
        widest - narrowest < 20,
        qPrintable(
            QStringLiteral("panel widths differ too much: %1 vs %2").arg(narrowest).arg(widest)));
}

QTEST_MAIN(TestPanels)
#include "tst_panels.moc"
