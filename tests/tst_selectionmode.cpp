#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "app/KeyDispatcher.h"
#include "ui/FilePanel.h"
#include "ui/PanelView.h"

#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::input;

namespace {

/// Sends one key through the dispatcher, exactly as Application::notify does.
bool press(KeyDispatcher &dispatcher, Qt::Key key, const QString &text,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    return dispatcher.handleKeyPress(&event);
}

} // namespace

/// §6.1's Selection mode, and §6.2's layer precedence.
class TestSelectionMode : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        for (const char *name : {"a.txt", "b.txt", "c.txt", "d.txt"}) {
            QFile file(m_dir->filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
        }
    }

    void cleanupTestCase() { m_dir.reset(); }

    /// The whole point: `v` then `j` selects, and the count goes up.
    ///
    /// None of this worked. setActiveLayers() was never called, so the
    /// dispatcher sat on {Normal, Global} for the life of the process and every
    /// binding in the Selection layer was unreachable — `v` set the mode, the
    /// header said [SELECT], and nothing else happened.
    void movementExtendsTheSelection()
    {
        ui::FilePanel panel;
        panel.navigateTo(m_dir->path());
        QTRY_COMPARE_WITH_TIMEOUT(panel.view()->model()->rowCount(), 4, 5000);
        panel.moveCursorToStart();

        ActionRegistry registry;
        Keymap keymap;
        installDefaultKeymap(keymap);
        KeyDispatcher dispatcher(&registry, &keymap);

        registry.registerAction(QStringLiteral("change_panel_mode"), QStringLiteral("mode"),
                                ActionCategory::Selection,
                                [&panel] { panel.toggleSelectionMode(); });
        registry.registerAction(QStringLiteral("select_down"), QStringLiteral("down"),
                                ActionCategory::Selection, [&panel] {
                                    panel.toggleSelectionAt(panel.cursorName());
                                    panel.moveCursor(1);
                                });
        registry.registerAction(QStringLiteral("list_down"), QStringLiteral("move"),
                                ActionCategory::Movement, [&panel] { panel.moveCursor(1); });

        // The composition root switches layers when the mode changes.
        QObject::connect(&panel, &ui::FilePanel::modeChanged, &panel, [&] {
            dispatcher.setActiveLayers(
                panel.isSelectionMode()
                    ? QList<KeymapLayer>{KeymapLayer::Selection, KeymapLayer::Normal,
                                         KeymapLayer::Global}
                    : QList<KeymapLayer>{KeymapLayer::Normal, KeymapLayer::Global});
        });

        // Before the mode, `j` is plain movement and selects nothing.
        QVERIFY(press(dispatcher, Qt::Key_J, QStringLiteral("j")));
        QCOMPARE(panel.selectionCount(), 0);

        panel.moveCursorToStart();
        QVERIFY(press(dispatcher, Qt::Key_V, QStringLiteral("v")));
        QVERIFY(panel.isSelectionMode());

        // In the mode, the same key extends the selection.
        QVERIFY(press(dispatcher, Qt::Key_J, QStringLiteral("j")));
        QCOMPARE(panel.selectionCount(), 1);

        QVERIFY(press(dispatcher, Qt::Key_J, QStringLiteral("j")));
        QCOMPARE(panel.selectionCount(), 2);

        // Leaving the mode gives `j` back to movement.
        QVERIFY(press(dispatcher, Qt::Key_V, QStringLiteral("v")));
        QVERIFY(!panel.isSelectionMode());
        QVERIFY(press(dispatcher, Qt::Key_J, QStringLiteral("j")));
        QCOMPARE(panel.selectionCount(), 2);
    }

    /// §6.2: "Current panel mode (Selection before Normal)" — the Selection
    /// layer's binding wins over Normal's for the same key.
    void theSelectionLayerOutranksNormal()
    {
        Keymap keymap;
        installDefaultKeymap(keymap);

        const auto binding = parseBinding(QStringLiteral("j"));
        QVERIFY(binding.has_value());

        QCOMPARE(keymap.lookup(KeymapLayer::Normal, *binding).actionId,
                 QStringLiteral("list_down"));
        QCOMPARE(keymap.lookup(KeymapLayer::Selection, *binding).actionId,
                 QStringLiteral("select_down"));

        // Consulted together, Selection first, as §6.2's precedence requires.
        QCOMPARE(keymap
                     .lookup({KeymapLayer::Selection, KeymapLayer::Normal, KeymapLayer::Global},
                             *binding)
                     .actionId,
                 QStringLiteral("select_down"));
    }

    /// The arrows extend too, and they are bound in Global for plain movement —
    /// so this also checks that Selection beats Global, not just Normal.
    void theArrowsExtendAsWell()
    {
        Keymap keymap;
        installDefaultKeymap(keymap);

        const auto down = parseBinding(QStringLiteral("Down"));
        QVERIFY(down.has_value());

        QCOMPARE(keymap.lookup(KeymapLayer::Global, *down).actionId, QStringLiteral("list_down"));
        QCOMPARE(
            keymap.lookup({KeymapLayer::Selection, KeymapLayer::Normal, KeymapLayer::Global}, *down)
                .actionId,
            QStringLiteral("select_down"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_MAIN(TestSelectionMode)
#include "tst_selectionmode.moc"
