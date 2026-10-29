// The actions that depend on what is selected (§7.14): image conversions and
// edits, media conversions, and extracting an archive.

#include "input/ActionRegistry.h"
#include "app/FileOperations.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"

#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

class TestFileTypeActions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // The trash, redirected so the test never touches the real one.
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
        QImage image(8, 4, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(m_dir->filePath(QStringLiteral("pic.png"))));
        for (const char *name : {"clip.mp4", "song.mp3", "notes.txt", "pack.zip"}) {
            QFile file(m_dir->filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }

        m_window = std::make_unique<ui::MainWindow>();
        m_registry = std::make_unique<input::ActionRegistry>();
        m_engine = std::make_unique<fs::JobEngine>();
        m_undo = std::make_unique<fs::UndoStack>();
        m_operations = std::make_unique<FileOperations>(
            m_window.get(), m_window->panelStrip(), m_registry.get(), m_engine.get(), m_undo.get());
        m_operations->registerActions();
        m_window->panelStrip()->addPanel(m_dir->path());
        QTRY_COMPARE_WITH_TIMEOUT(panel()->view()->model()->rowCount(), 5, 5000);
    }

    void cleanup()
    {
        qunsetenv("PANEFILE_FFMPEG");
        m_operations.reset();
        m_undo.reset();
        m_engine.reset();
        m_registry.reset();
        m_window.reset();
        m_dir.reset();
    }

    void imageActionsAreOfferedOnlyForImages()
    {
        panel()->setCursorName(QStringLiteral("pic.png"));
        QVERIFY(m_registry->isEnabled(QStringLiteral("convert_image_to_jpeg")));
        QVERIFY(m_registry->isEnabled(QStringLiteral("rotate_image_left")));
        QVERIFY(m_registry->isEnabled(QStringLiteral("flip_image_vertically")));
        // Already a PNG.
        QVERIFY(!m_registry->isEnabled(QStringLiteral("convert_image_to_png")));
        QVERIFY(!m_registry->isEnabled(QStringLiteral("convert_video_to_mp4")));
        QVERIFY(m_registry->find(QStringLiteral("rotate_image_left"))->contextual);

        panel()->setCursorName(QStringLiteral("notes.txt"));
        QVERIFY(!m_registry->isEnabled(QStringLiteral("convert_image_to_jpeg")));
        QVERIFY(!m_registry->isEnabled(QStringLiteral("rotate_image_left")));
        QVERIFY(!m_registry->isEnabled(QStringLiteral("extract_file")));
    }

    void archivesOfferExtraction()
    {
        panel()->setCursorName(QStringLiteral("pack.zip"));
        QVERIFY(m_registry->isEnabled(QStringLiteral("extract_file")));
        QVERIFY(m_registry->find(QStringLiteral("extract_file"))->contextual);
    }

    void mediaActionsNeedFfmpeg()
    {
        qputenv("PANEFILE_FFMPEG", "/opt/ffmpeg");
        panel()->setCursorName(QStringLiteral("clip.mp4"));
        QVERIFY(m_registry->isEnabled(QStringLiteral("convert_video_to_webm")));
        QVERIFY(m_registry->isEnabled(QStringLiteral("extract_audio_to_mp3")));
        QVERIFY(!m_registry->isEnabled(QStringLiteral("convert_audio_to_flac")));

        panel()->setCursorName(QStringLiteral("song.mp3"));
        QVERIFY(m_registry->isEnabled(QStringLiteral("convert_audio_to_flac")));
        QVERIFY(!m_registry->isEnabled(QStringLiteral("convert_video_to_webm")));

        // Without ffmpeg they simply are not there.
        qputenv("PANEFILE_FFMPEG", QByteArray());
        QVERIFY(!m_registry->isEnabled(QStringLiteral("convert_audio_to_flac")));
        panel()->setCursorName(QStringLiteral("clip.mp4"));
        QVERIFY(!m_registry->isEnabled(QStringLiteral("convert_video_to_webm")));
    }

    /// Rotate through the registry, then undo.
    void rotatingThroughTheRegistryIsUndoable()
    {
        panel()->setCursorName(QStringLiteral("pic.png"));
        QVERIFY(m_registry->invoke(QStringLiteral("rotate_image_right")));

        const QString path = m_dir->filePath(QStringLiteral("pic.png"));
        QTRY_COMPARE_WITH_TIMEOUT(QImage(path).size(), QSize(4, 8), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(m_undo->canUndo(), 5000);

        QString error;
        QVERIFY2(m_undo->undo(&error), qPrintable(error));
        QCOMPARE(QImage(path).size(), QSize(8, 4));
    }

    void convertingCreatesANewFile()
    {
        panel()->setCursorName(QStringLiteral("pic.png"));
        QVERIFY(m_registry->invoke(QStringLiteral("convert_image_to_jpeg")));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_dir->filePath(QStringLiteral("pic.jpg"))), 5000);
        QVERIFY(QFile::exists(m_dir->filePath(QStringLiteral("pic.png"))));
    }

private:
    ui::FilePanel *panel() const { return m_window->panelStrip()->focusedPanel(); }

    QTemporaryDir m_home;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ui::MainWindow> m_window;
    std::unique_ptr<input::ActionRegistry> m_registry;
    std::unique_ptr<fs::JobEngine> m_engine;
    std::unique_ptr<fs::UndoStack> m_undo;
    std::unique_ptr<FileOperations> m_operations;
};

QTEST_MAIN(TestFileTypeActions)
#include "tst_filetypeactions.moc"
