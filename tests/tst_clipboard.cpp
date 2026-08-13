#include "input/ActionRegistry.h"
#include "app/FileOperations.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

namespace {

QStringList clipboardPaths()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime == nullptr || !mime->hasUrls()) {
        return {};
    }
    QStringList paths;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    return paths;
}

} // namespace

/// The copy list: repeated Ctrl+C gathers files, one Ctrl+V pastes them.
class TestClipboard : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        QApplication::clipboard()->clear();

        m_dir = std::make_unique<QTemporaryDir>();
        for (const char *name : {"a.txt", "b.txt", "c.txt"}) {
            QFile file(m_dir->filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
            file.close();
        }
        QVERIFY(QDir(m_dir->path()).mkdir(QStringLiteral("target")));

        m_window = std::make_unique<ui::MainWindow>();
        m_registry = std::make_unique<input::ActionRegistry>();
        m_engine = std::make_unique<fs::JobEngine>();
        m_undo = std::make_unique<fs::UndoStack>();
        m_operations = std::make_unique<FileOperations>(
            m_window.get(), m_window->panelStrip(), m_registry.get(), m_engine.get(), m_undo.get());
        m_operations->registerActions();

        m_window->panelStrip()->addPanel(m_dir->path());
        QTRY_COMPARE_WITH_TIMEOUT(panel()->view()->model()->rowCount(), 4, 5000);
    }

    void cleanup()
    {
        m_operations.reset();
        m_undo.reset();
        m_engine.reset();
        m_registry.reset();
        m_window.reset();
        m_dir.reset();
    }

    /// The behaviour asked for: Ctrl+C on one file, then another, then another,
    /// gathers all three. No Selection mode required.
    void repeatedCopyGathersFiles()
    {
        panel()->setCursorName(QStringLiteral("a.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        QCOMPARE(clipboardPaths().size(), 1);

        panel()->setCursorName(QStringLiteral("b.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        QCOMPARE(clipboardPaths().size(), 2);

        panel()->setCursorName(QStringLiteral("c.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        QCOMPARE(clipboardPaths().size(), 3);
    }

    /// Pressing it twice on the same file is a repeat, not a second copy.
    void copyingTheSameFileTwiceAddsItOnce()
    {
        panel()->setCursorName(QStringLiteral("a.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        m_registry->invoke(QStringLiteral("copy_items"));
        QCOMPARE(clipboardPaths().size(), 1);
    }

    /// Copy and cut are not mixable, so switching starts a new list.
    void switchingBetweenCopyAndCutStartsOver()
    {
        panel()->setCursorName(QStringLiteral("a.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        panel()->setCursorName(QStringLiteral("b.txt"));
        m_registry->invoke(QStringLiteral("cut_items"));

        QCOMPARE(clipboardPaths().size(), 1);
        QCOMPARE(QFileInfo(clipboardPaths().constFirst()).fileName(), QStringLiteral("b.txt"));
    }

    /// Pasting empties the list, so a later copy cannot drag along a set the
    /// user finished with.
    void pastingClearsTheList()
    {
        panel()->setCursorName(QStringLiteral("a.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        panel()->setCursorName(QStringLiteral("b.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        QCOMPARE(clipboardPaths().size(), 2);

        QVERIFY(m_operations->hasPendingClipboard());
        m_registry->invoke(QStringLiteral("paste_items"));

        QVERIFY(!m_operations->hasPendingClipboard());
        QVERIFY(clipboardPaths().isEmpty());
    }

    /// Esc abandons a list gathered by mistake, without having to paste it
    /// somewhere to be rid of it.
    void clearingAbandonsTheList()
    {
        panel()->setCursorName(QStringLiteral("a.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        QVERIFY(m_operations->hasPendingClipboard());

        m_operations->clearClipboard();

        QVERIFY(!m_operations->hasPendingClipboard());
        QVERIFY(clipboardPaths().isEmpty());
    }

    /// An explicit selection means "these, and only these" — it replaces
    /// whatever was gathered rather than adding to it.
    void anExplicitSelectionReplacesTheList()
    {
        panel()->setCursorName(QStringLiteral("a.txt"));
        m_registry->invoke(QStringLiteral("copy_items"));
        QCOMPARE(clipboardPaths().size(), 1);

        panel()->toggleSelectionAt(QStringLiteral("b.txt"));
        panel()->toggleSelectionAt(QStringLiteral("c.txt"));
        QCOMPARE(panel()->selectionCount(), 2);

        m_registry->invoke(QStringLiteral("copy_items"));
        QCOMPARE(clipboardPaths().size(), 2);
    }

private:
    ui::FilePanel *panel() const { return m_window->panelStrip()->focusedPanel(); }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ui::MainWindow> m_window;
    std::unique_ptr<input::ActionRegistry> m_registry;
    std::unique_ptr<fs::JobEngine> m_engine;
    std::unique_ptr<fs::UndoStack> m_undo;
    std::unique_ptr<FileOperations> m_operations;
};

QTEST_MAIN(TestClipboard)
#include "tst_clipboard.moc"
