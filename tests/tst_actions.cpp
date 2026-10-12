#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/DefaultFileManagerOffer.h"
#include "app/FileOperations.h"
#include "app/PanelController.h"
#include "app/QuickLookController.h"
#include "app/SearchController.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

namespace {

/// Actions the default keymap binds that nothing implements yet.
///
/// Empty, and meant to stay that way. Every entry was a key a user could press
/// that did nothing at all — thirteen of them when this test was written, of
/// which compress_file and extract_file were the reason for writing it: §7.10's
/// archive support was fully built and tested and then never registered, so no
/// key reached any of it.
///
/// The list is kept rather than deleted because a new binding added without an
/// implementation is exactly the mistake this catches, and an empty list is the
/// clearest possible statement that there is nothing outstanding.
const QStringList kNotImplementedYet{};

/// Registered by Application itself rather than by one of the controllers.
///
/// Constructing Application means a session file, a single-instance socket and
/// a real window, which is more than this test needs — so these four are
/// accounted for here instead. They are genuinely registered; the companion
/// test below proves it from the source rather than taking it on trust.
const QStringList kRegisteredByTheCompositionRoot{
    QStringLiteral("cancel"),
    QStringLiteral("quit"),
    QStringLiteral("toggle_sidebar"),
    QStringLiteral("open_help_menu"),
    QStringLiteral("focus_on_process_bar"),
    QStringLiteral("open_command_line"),
    QStringLiteral("open_panefile_prompt"),
    QStringLiteral("open_settings"),
};

} // namespace

/// Every key in the default keymap must reach an action that exists.
class TestActions : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Shutdown work has to be connected to shutdown.
    ///
    /// Application::saveSession and WorkerPools::drainAll were both written and
    /// tested and then never called from anywhere. The session file therefore
    /// kept whatever was last written to it — reopening the application put you
    /// back in a directory you had left days before — and the drain that exists
    /// to stop a scanner thread reaching QMimeDatabase during static
    /// destruction was dead code, so that crash was never actually fixed.
    ///
    /// Read from the source because the alternative is constructing an
    /// Application and quitting it, which needs a session file, a socket and a
    /// window; this checks the wiring exists at all, which is the part that was
    /// missing.
    void shutdownWorkIsConnectedToShutdown()
    {
        QFile source(QStringLiteral(PF_SOURCE_DIR "/src/app/Application.cpp"));
        QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
        const QString text = QString::fromUtf8(source.readAll());

        const qsizetype hook = text.indexOf(QStringLiteral("aboutToQuit"));
        QVERIFY2(hook >= 0, "nothing is connected to aboutToQuit");

        // Both calls must appear after the connection, not merely exist.
        const QString afterHook = text.mid(hook);
        QVERIFY2(afterHook.contains(QStringLiteral("saveSession()")), "session is never saved");
        QVERIFY2(afterHook.contains(QStringLiteral("WorkerPools::drainAll()")),
                 "worker pools are never drained");
    }

    /// A binding whose action was never registered is a dead key: it looks
    /// supported in the help modal and in §6.3's table, and pressing it does
    /// nothing whatsoever. That is how an entire implemented feature — archive
    /// compression and extraction — stayed unreachable.
    /// Shift+Down extends from the row the cursor leaves; a plain arrow
    /// afterwards ends the selection rather than leaving it behind.
    void shiftArrowsSelectAndPlainArrowsCollapse()
    {
        QTemporaryDir root;
        for (const char *name : {"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}) {
            QFile file(root.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        ui::MainWindow window;
        input::ActionRegistry registry;
        fs::JobEngine engine;
        fs::UndoStack undo;
        PanelController panels(&window, window.panelStrip(), window.sidebar(), &registry);
        panels.registerActions();
        FileOperations operations(&window, window.panelStrip(), &registry, &engine, &undo);
        operations.registerActions();

        ui::FilePanel *panel = window.panelStrip()->addPanel(root.path());
        QTRY_COMPARE_WITH_TIMEOUT(panel->view()->model()->rowCount(), 5, 5000);
        panel->moveCursorToStart();

        // A click earlier left an anchor elsewhere; with nothing selected, the
        // range must still start at the cursor.
        panel->handleClickPress(QStringLiteral("d.txt"), Qt::NoModifier);
        panel->setCursorName(QStringLiteral("a.txt"));

        QVERIFY(registry.invoke(QStringLiteral("select_down")));
        QVERIFY(registry.invoke(QStringLiteral("select_down")));
        QCOMPARE(panel->selectionCount(), 3);
        QCOMPARE(panel->cursorName(), QStringLiteral("c.txt"));

        QVERIFY(registry.invoke(QStringLiteral("select_up")));
        QCOMPARE(panel->selectionCount(), 2);

        QVERIFY(registry.invoke(QStringLiteral("list_down")));
        QCOMPARE(panel->selectionCount(), 0);
        QCOMPARE(panel->cursorName(), QStringLiteral("c.txt"));

        // Selection mode keeps its selection through plain movement.
        panel->setSelectionMode(true);
        QVERIFY(registry.invoke(QStringLiteral("select_down")));
        QVERIFY(registry.invoke(QStringLiteral("page_down")));
        QCOMPARE(panel->selectionCount(), 2);
    }

    void everyBindingReachesARegisteredAction()
    {
        ui::MainWindow window;
        input::ActionRegistry registry;
        fs::JobEngine engine;
        fs::UndoStack undo;

        PanelController panels(&window, window.panelStrip(), window.sidebar(), &registry);
        panels.registerActions();

        FileOperations operations(&window, window.panelStrip(), &registry, &engine, &undo);
        operations.registerActions();

        QuickLookController quickLook(&window, &registry);
        quickLook.registerActions();

        SearchController search(&window, window.panelStrip(), &registry);
        search.registerActions();

        DefaultFileManagerOffer defaultOffer(&window, nullptr);
        defaultOffer.registerActions(&registry);

        const QStringList registered = registry.ids();

        input::Keymap keymap;
        installDefaultKeymap(keymap);

        QStringList dead;
        for (const input::KeymapLayer layer :
             {input::KeymapLayer::Global, input::KeymapLayer::Normal, input::KeymapLayer::Selection,
              input::KeymapLayer::Modal, input::KeymapLayer::Typing}) {
            for (const QString &id : keymap.boundActions(layer)) {
                if (!registered.contains(id) && !kNotImplementedYet.contains(id) &&
                    !kRegisteredByTheCompositionRoot.contains(id) && !dead.contains(id)) {
                    dead << id;
                }
            }
        }

        QVERIFY2(dead.isEmpty(),
                 qPrintable(QStringLiteral("keys bound to actions that do not exist: %1")
                                .arg(dead.join(QStringLiteral(", ")))));
    }

    /// The composition root really does register the four this test excuses.
    ///
    /// Read from the source, because the alternative is trusting a hard-coded
    /// list to stay true — which is exactly the failure this whole file exists
    /// to catch.
    void theCompositionRootRegistersItsOwnActions()
    {
        QFile source(QStringLiteral(PF_SOURCE_DIR "/src/app/Application.cpp"));
        QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(source.fileName()));
        const QString text = QString::fromUtf8(source.readAll());

        for (const QString &id : kRegisteredByTheCompositionRoot) {
            QVERIFY2(text.contains(QStringLiteral("\"%1\"").arg(id)), qPrintable(id));
        }
    }

    /// And the allow-list must not rot: an entry that has since been
    /// implemented has to be removed, or the list stops meaning anything.
    void theNotImplementedListIsAccurate()
    {
        ui::MainWindow window;
        input::ActionRegistry registry;
        fs::JobEngine engine;
        fs::UndoStack undo;

        PanelController panels(&window, window.panelStrip(), window.sidebar(), &registry);
        panels.registerActions();
        FileOperations operations(&window, window.panelStrip(), &registry, &engine, &undo);
        operations.registerActions();
        QuickLookController quickLook(&window, &registry);
        quickLook.registerActions();
        SearchController search(&window, window.panelStrip(), &registry);
        search.registerActions();

        DefaultFileManagerOffer defaultOffer(&window, nullptr);
        defaultOffer.registerActions(&registry);

        const QStringList registered = registry.ids();

        QStringList stale;
        for (const QString &id : kNotImplementedYet) {
            if (registered.contains(id)) {
                stale << id;
            }
        }

        QVERIFY2(stale.isEmpty(),
                 qPrintable(QStringLiteral("implemented, so remove from the list: %1")
                                .arg(stale.join(QStringLiteral(", ")))));
    }
};

QTEST_MAIN(TestActions)
#include "tst_actions.moc"
