// Deleting feels instantaneous: the rows go the moment the delete starts, not
// when the filesystem reports back, and anything that could not be deleted
// comes back afterwards.

#include "input/ActionRegistry.h"
#include "app/FileOperations.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace pf;

class TestDelete : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Redirect the trash so the test never touches the real one.
        QVERIFY(m_home.isValid());
#ifdef Q_OS_MACOS
        qputenv("HOME", QFile::encodeName(m_home.path()));
#else
        qputenv("XDG_DATA_HOME", QFile::encodeName(m_home.path()));
#endif
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        for (const char *name : {"a.txt", "b.txt", "c.txt", "d.txt"}) {
            touch(m_dir->filePath(QString::fromLatin1(name)));
        }
        setUpWindow();
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

    void rowsVanishBeforeAnyEventLoopRuns()
    {
        panel()->setCursorName(QStringLiteral("b.txt"));
        QCOMPARE(countText(), QStringLiteral("4 items"));

        m_registry->invoke(QStringLiteral("delete_items"));

        // No event loop has run: the row is gone, the cursor has moved on, and
        // the count already reads the new total.
        QCOMPARE(panel()->view()->model()->rowCount(), 3);
        QCOMPARE(panel()->cursorName(), QStringLiteral("c.txt"));
        QCOMPARE(countText(), QStringLiteral("3 items"));

        QSignalSpy finished(m_engine.get(), &fs::JobEngine::jobFinished);
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 5000);
        QTest::qWait(400);

        QCOMPARE(panel()->view()->model()->rowCount(), 3);
        QVERIFY(!QFile::exists(m_dir->filePath(QStringLiteral("b.txt"))));
        QVERIFY(m_undo->canUndo());
    }

    void aFailedDeleteBringsTheRowBackAndSaysWhy()
    {
        if (::geteuid() == 0) {
            QSKIP("root can write to a read-only trash");
        }

        // A trash that cannot be written to, so every item fails to move.
        const QString trash = m_home.filePath(QStringLiteral("Trash"));
        QVERIFY(QDir().mkpath(trash + QStringLiteral("/files")));
        QVERIFY(QDir().mkpath(trash + QStringLiteral("/info")));
        const QFileDevice::Permissions readOnly = QFileDevice::ReadOwner | QFileDevice::ExeOwner;
        for (const char *sub : {"/files", "/info", ""}) {
            QVERIFY(QFile::setPermissions(trash + QLatin1String(sub), readOnly));
        }
        const auto restore = qScopeGuard([&trash, readOnly] {
            for (const char *sub : {"", "/files", "/info"}) {
                QFile::setPermissions(trash + QLatin1String(sub),
                                      readOnly | QFileDevice::WriteOwner);
            }
        });

        QStringList messages;
        connect(m_operations.get(), &FileOperations::statusMessage, this,
                [&messages](const QString &text) { messages << text; });

        panel()->setCursorName(QStringLiteral("a.txt"));
        m_registry->invoke(QStringLiteral("delete_items"));
        QCOMPARE(panel()->view()->model()->rowCount(), 3);

        QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(panel()->view()->model()->rowCount(), 4, 5000);
        QVERIFY(QFile::exists(m_dir->filePath(QStringLiteral("a.txt"))));
    }

private:
    static void touch(const QString &path)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }

    void setUpWindow()
    {
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

    ui::FilePanel *panel() const { return m_window->panelStrip()->focusedPanel(); }

    QString countText() const
    {
        const auto *label = panel()->findChild<QLabel *>(QStringLiteral("panelHeaderCount"));
        return label != nullptr ? label->text() : QString();
    }

    QTemporaryDir m_home;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ui::MainWindow> m_window;
    std::unique_ptr<input::ActionRegistry> m_registry;
    std::unique_ptr<fs::JobEngine> m_engine;
    std::unique_ptr<fs::UndoStack> m_undo;
    std::unique_ptr<FileOperations> m_operations;
};

QTEST_MAIN(TestDelete)
#include "tst_delete.moc"
