#include "plugins/PluginInterfaces.h"
#include "model/FileEntry.h"
#include "model/ThumbnailCache.h"
#include "platform/Paths.h"
#include "platform/PluginHost.h"
#include "ui/quicklook/renderers/MediaRenderer.h"
#include "ui/quicklook/renderers/PdfRenderer.h"
#include "ui/quicklook/renderers/TextRenderer.h"

#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QKeyEvent>
#include <QMimeDatabase>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace pf;
using namespace pf::ui;

namespace {

/// Which plugins this build produced. A test of a plugin that was not built —
/// its dependency was missing at configure time — skips rather than fails,
/// exactly as the application degrades rather than breaks (§2).
bool built(platform::Plugin which)
{
    switch (which) {
    case platform::Plugin::Syntax:
        return PF_TEST_HAVE_SYNTAX;
    case platform::Plugin::Media:
        return PF_TEST_HAVE_MEDIA;
    case platform::Plugin::Pdf:
        return PF_TEST_HAVE_PDF;
    case platform::Plugin::VideoThumbnails:
        return PF_TEST_HAVE_VIDEO_THUMBS;
    }
    return false;
}

#define REQUIRE_PLUGIN(which)                                                                      \
    do {                                                                                           \
        if (!built(which)) {                                                                       \
            QSKIP("plugin not built: its dependency was not found at configure time");             \
        }                                                                                          \
    } while (false)

QuickLookContent contentFor(const QString &path, const QString &text = {})
{
    static const QMimeDatabase database;
    QuickLookContent content;
    content.path = path;
    content.mimeType = database.mimeTypeForFile(path);
    content.entry.name = QFileInfo(path).fileName();
    content.entry.size = static_cast<quint64>(QFileInfo(path).size());
    content.text = text;
    return content;
}

/// A two-page PDF, written with Qt so the suite carries no binary fixture.
QString writePdf(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("two-pages.pdf"));
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A6));
    QPainter painter(&writer);
    painter.drawText(QPointF(100, 100), QStringLiteral("one"));
    writer.newPage();
    painter.drawText(QPointF(100, 100), QStringLiteral("two"));
    painter.end();
    return path;
}

/// Services the main dispatch queue for as long as it lives. QtMultimedia's
/// AVFoundation backend finishes loading a file in a block posted to that
/// queue, and only a Core Foundation run loop drains it — which the offscreen
/// platform plugin, being a plain Unix event dispatcher, never runs. Under the
/// cocoa plugin the application has one already; this stands in for it.
class MainQueuePump
{
public:
    MainQueuePump()
    {
#ifdef Q_OS_MACOS
        QObject::connect(&m_timer, &QTimer::timeout, [] {
            while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) ==
                   kCFRunLoopRunHandledSource) {
            }
        });
        m_timer.start(10);
#endif
    }

private:
    QTimer m_timer;
};

/// One and a half seconds of 8 kHz mono silence as a WAV file. Not a whole
/// second: a backend that reports 999 ms or 1001 ms still shows "0:01".
QString writeWav(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("silence.wav"));
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);

    constexpr quint32 rate = 8000;
    constexpr quint32 samples = rate * 3 / 2;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + samples * 2);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << rate << quint32(rate * 2) << quint16(2)
        << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(samples * 2);
    for (quint32 i = 0; i < samples; ++i) {
        out << qint16(0);
    }
    return path;
}

/// A one-second test-pattern video, made with the ffmpeg command line. Empty
/// when ffmpeg is not installed — the plugin's library dependency does not
/// guarantee the command.
QString writeVideo(const QTemporaryDir &dir)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        return {};
    }
    const QString path = dir.filePath(QStringLiteral("pattern.mp4"));
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-loglevel"), QStringLiteral("error"),
                           QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("testsrc=size=320x180:duration=1:rate=10"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path});
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        return {};
    }
    return path;
}

void pressKey(QuickLookRenderer &renderer, int key, const QString &text)
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
    QVERIFY(renderer.handleKey(&event));
}

} // namespace

/// The optional-feature plugins of §3.4, loaded from the build tree.
class TestPlugins : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void searchPathIsTheOverride()
    {
        // tests/CMakeLists.txt points PANEFILE_PLUGIN_DIR at the build tree's
        // staging directory; an override replaces the search outright.
        QCOMPARE(platform::pluginSearchPaths(),
                 QStringList{QDir::cleanPath(qEnvironmentVariable("PANEFILE_PLUGIN_DIR"))});
    }

    void eachBuiltPluginLoads_data()
    {
        QTest::addColumn<platform::Plugin>("which");
        QTest::newRow("pf-syntax") << platform::Plugin::Syntax;
        QTest::newRow("pf-media") << platform::Plugin::Media;
        QTest::newRow("pf-pdf") << platform::Plugin::Pdf;
        QTest::newRow("pf-video-thumb") << platform::Plugin::VideoThumbnails;
    }

    /// Each plugin implements the interface its consumer casts to — the IID
    /// check is what refuses a stale plugin from an older install.
    void eachBuiltPluginLoads()
    {
        QFETCH(platform::Plugin, which);
        REQUIRE_PLUGIN(which);

        QObject *instance = platform::pluginInstance(which);
        QVERIFY(instance != nullptr);
        QCOMPARE(platform::pluginInstance(which), instance); // loaded once

        switch (which) {
        case platform::Plugin::Syntax:
            QVERIFY(qobject_cast<plugins::SyntaxPlugin *>(instance) != nullptr);
            break;
        case platform::Plugin::Media:
        case platform::Plugin::Pdf:
            QVERIFY(qobject_cast<plugins::RendererPlugin *>(instance) != nullptr);
            break;
        case platform::Plugin::VideoThumbnails:
            QVERIFY(qobject_cast<plugins::VideoThumbnailPlugin *>(instance) != nullptr);
            break;
        }
    }

    // ---------------------------------------------------------------- syntax

    /// §7.6: "syntax highlighted via KSyntaxHighlighting".
    void syntaxHighlightsSource()
    {
        REQUIRE_PLUGIN(platform::Plugin::Syntax);
        auto *syntax = platform::plugin<plugins::SyntaxPlugin>(platform::Plugin::Syntax);
        QVERIFY(syntax != nullptr);

        QTextDocument document;
        document.setPlainText(QStringLiteral("int main() { return 0; } // done"));
        QSyntaxHighlighter *highlighter = syntax->attach(&document, QStringLiteral("main.cpp"),
                                                         QStringLiteral("text/x-c++src"), false);
        QVERIFY(highlighter != nullptr);
        QCOMPARE(highlighter->document(), &document);

        highlighter->rehighlight();
        QVERIFY(!document.firstBlock().layout()->formats().isEmpty());
    }

    /// No definition, no highlighter: the text renderer shows plain text.
    void syntaxDeclinesAnUnknownType()
    {
        REQUIRE_PLUGIN(platform::Plugin::Syntax);
        auto *syntax = platform::plugin<plugins::SyntaxPlugin>(platform::Plugin::Syntax);

        QTextDocument document;
        QCOMPARE(syntax->attach(&document, QStringLiteral("blob.zz9"),
                                QStringLiteral("application/x-zz9-unknown"), false),
                 nullptr);
    }

    void textRendererHighlightsWithinTheLimit()
    {
        REQUIRE_PLUGIN(platform::Plugin::Syntax);

        QWidget parent;
        TextRenderer renderer;
        renderer.createWidget(&parent);

        renderer.setContent(
            contentFor(QStringLiteral("/tmp/main.cpp"), QStringLiteral("int x = 1;\n")));
        QVERIFY(renderer.isHighlighted());

        // Over the cap the text is shown plain, because QSyntaxHighlighter
        // would highlight all of it synchronously on the GUI thread.
        renderer.setContent(
            contentFor(QStringLiteral("/tmp/big.cpp"),
                       QString(TextRenderer::kHighlightLimit + 1, QLatin1Char('x'))));
        QVERIFY(!renderer.isHighlighted());
    }

    // ------------------------------------------------------------------- pdf

    /// §7.6: "Paged rendering … with page navigation and zoom", and §7.6's
    /// `[` / `]` and `+` / `-` / `0` keys.
    void pdfRendersPagesAndNavigates()
    {
        REQUIRE_PLUGIN(platform::Plugin::Pdf);
        QTemporaryDir dir;
        const QString path = writePdf(dir);

        QWidget parent;
        parent.resize(600, 800);
        PdfRenderer renderer;
        QWidget *page = renderer.createWidget(&parent);
        page->resize(parent.size());

        renderer.setContent(contentFor(path));
        QVERIFY(renderer.isUsingPlugin());
        QTRY_VERIFY(renderer.statusText().startsWith(QStringLiteral("Page 1 of 2")));
        QVERIFY(renderer.statusText().contains(QStringLiteral("100%")));

        pressKey(renderer, Qt::Key_BracketRight, QStringLiteral("]"));
        QTRY_VERIFY(renderer.statusText().startsWith(QStringLiteral("Page 2 of 2")));

        pressKey(renderer, Qt::Key_Plus, QStringLiteral("+"));
        QTRY_VERIFY(renderer.statusText().contains(QStringLiteral("125%")));

        pressKey(renderer, Qt::Key_0, QStringLiteral("0"));
        QTRY_VERIFY(renderer.statusText().contains(QStringLiteral("100%")));
    }

    void pdfReportsAnUnreadableFile()
    {
        REQUIRE_PLUGIN(platform::Plugin::Pdf);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("broken.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.4\nthis is not a pdf\n");
        file.close();

        QWidget parent;
        PdfRenderer renderer;
        renderer.createWidget(&parent);
        renderer.setContent(contentFor(path));
        QTRY_COMPARE(renderer.statusText(), QStringLiteral("Not a readable PDF"));
    }

    // ----------------------------------------------------------------- media

    /// The player is built, reads the file's duration, and does not start
    /// playing on its own — Quick Look follows the cursor.
    void mediaLoadsWithoutPlaying()
    {
        REQUIRE_PLUGIN(platform::Plugin::Media);
        QTemporaryDir dir;
        const QString path = writeWav(dir);
        const MainQueuePump pump;

        QWidget parent;
        MediaRenderer renderer;
        renderer.createWidget(&parent);
        renderer.setContent(contentFor(path));
        QVERIFY(renderer.isUsingPlugin());

        QTRY_VERIFY2(renderer.statusText().contains(QStringLiteral("/ 0:01")),
                     qPrintable(renderer.statusText()));
        QVERIFY(renderer.statusText().startsWith(QStringLiteral("Paused")));
        QVERIFY(renderer.statusText().contains(QStringLiteral("p play")));

        renderer.clear();
        QCOMPARE(renderer.statusText(), QString());
    }

    // ------------------------------------------------------------ thumbnails

    void videoThumbnailFitsTheBox()
    {
        REQUIRE_PLUGIN(platform::Plugin::VideoThumbnails);
        QTemporaryDir dir;
        const QString path = writeVideo(dir);
        if (path.isEmpty()) {
            QSKIP("ffmpeg is not installed, so there is no video to thumbnail");
        }

        const auto *thumbnailer =
            platform::plugin<plugins::VideoThumbnailPlugin>(platform::Plugin::VideoThumbnails);
        QVERIFY(thumbnailer != nullptr);

        QString error;
        const QImage image = thumbnailer->thumbnail(path, 128, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(qMax(image.width(), image.height()), 128);
    }

    void videoThumbnailReportsAnUndecodableFile()
    {
        REQUIRE_PLUGIN(platform::Plugin::VideoThumbnails);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("not-a-video.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("definitely not an mp4");
        file.close();

        const auto *thumbnailer =
            platform::plugin<plugins::VideoThumbnailPlugin>(platform::Plugin::VideoThumbnails);
        QString error;
        QVERIFY(thumbnailer->thumbnail(path, 128, &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    /// §7.7 end to end: a video is now queued, generated through the plugin
    /// and cached with the spec's chunks.
    void thumbnailCacheGeneratesVideoThumbnails()
    {
        REQUIRE_PLUGIN(platform::Plugin::VideoThumbnails);
        QTemporaryDir dir;
        QTemporaryDir cache;
        const QString path = writeVideo(dir);
        if (path.isEmpty()) {
            QSKIP("ffmpeg is not installed, so there is no video to thumbnail");
        }

        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QVERIFY(thumbnails.canThumbnail(path));

        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        thumbnails.request(path, ThumbnailCache::Size::Normal);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 20000);

        const QImage cached = thumbnails.lookup(path, ThumbnailCache::Size::Normal);
        QVERIFY(!cached.isNull());
        QCOMPARE(cached.text(QStringLiteral("Thumb::URI")), ThumbnailCache::fileUri(path));
    }

    /// §7.7's `thumbnails.video = false` is honoured now that it means
    /// something.
    void videoThumbnailsCanBeTurnedOff()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("clip.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();

        ThumbnailCache thumbnails;
        thumbnails.setVideoEnabled(false);
        QVERIFY(!thumbnails.canThumbnail(path));
    }
};

QTEST_MAIN(TestPlugins)
#include "tst_plugins.moc"
