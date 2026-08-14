#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/FileOperations.h"
#include "app/PanelController.h"
#include "app/QuickLookController.h"
#include "app/SearchController.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"

#include <QFile>
#include <QTest>

using namespace pf;

namespace {

/// Actions the default keymap binds that nothing implements yet.
///
/// Every entry here is a key a user can press today that does nothing at all.
/// They are listed rather than quietly tolerated so that the count can only go
/// down: implementing one means deleting its line, and binding a *new* key to
/// nothing fails this test immediately.
///
/// The two that used to be here — compress_file and extract_file — were the
/// reason it was written. §7.10's archive support was fully built and tested
/// and then never registered, so no key reached any of it.
const QStringList kNotImplementedYet{
    QStringLiteral("focus_on_process_bar"),
    QStringLiteral("go_previous"),
    QStringLiteral("go_trash"),
    QStringLiteral("open_command_line"),
    QStringLiteral("open_current_directory_with_editor"),
    QStringLiteral("open_file_with_editor"),
    QStringLiteral("open_panefile_prompt"),
    QStringLiteral("open_sort_options_menu"),
    QStringLiteral("open_terminal_here"),
    QStringLiteral("open_with_default_app"),
    QStringLiteral("toggle_theme_dark_light"),
};

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
