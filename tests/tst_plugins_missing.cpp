#include "model/ThumbnailCache.h"
#include "platform/PluginHost.h"
#include "ui/quicklook/renderers/MediaRenderer.h"
#include "ui/quicklook/renderers/PdfRenderer.h"
#include "ui/quicklook/renderers/TextRenderer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMimeDatabase>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::ui;

namespace {

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write(bytes);
    return path;
}

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

} // namespace

/// §2's graceful degradation, with every plugin absent: tests/CMakeLists.txt
/// points PANEFILE_PLUGIN_DIR at an empty directory, which is what a build
/// without the optional dependencies looks like at run time.
class TestPluginsMissing : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nothingLoads()
    {
        QCOMPARE(platform::pluginInstance(platform::Plugin::Syntax), nullptr);
        QCOMPARE(platform::pluginInstance(platform::Plugin::Media), nullptr);
        QCOMPARE(platform::pluginInstance(platform::Plugin::Pdf), nullptr);
        QCOMPARE(platform::pluginInstance(platform::Plugin::VideoThumbnails), nullptr);
    }

    /// "fall back to plain text" (§2).
    void textIsShownPlain()
    {
        QWidget parent;
        TextRenderer renderer;
        renderer.createWidget(&parent);
        renderer.setContent(
            contentFor(QStringLiteral("/tmp/main.cpp"), QStringLiteral("int x = 1;")));
        QVERIFY(!renderer.isHighlighted());
        QVERIFY(renderer.statusText().startsWith(QStringLiteral("1 line")));
    }

    void mediaAndPdfShowTheCard()
    {
        QTemporaryDir dir;
        const QString pdf = writeFile(dir, QStringLiteral("doc.pdf"), "%PDF-1.4\n");
        const QString wav = writeFile(dir, QStringLiteral("sound.wav"), "RIFF");

        QWidget parent;
        PdfRenderer pdfRenderer;
        pdfRenderer.createWidget(&parent);
        pdfRenderer.setContent(contentFor(pdf));
        QVERIFY(!pdfRenderer.isUsingPlugin());
        QVERIFY(!pdfRenderer.statusText().isEmpty());

        MediaRenderer mediaRenderer;
        mediaRenderer.createWidget(&parent);
        mediaRenderer.setContent(contentFor(wav));
        QVERIFY(!mediaRenderer.isUsingPlugin());
        QVERIFY(!mediaRenderer.statusText().isEmpty());

        // Keys go nowhere rather than to a renderer that is not there.
        QKeyEvent key(QEvent::KeyPress, Qt::Key_BracketRight, Qt::NoModifier, QStringLiteral("]"));
        QVERIFY(!pdfRenderer.handleKey(&key));
    }

    /// The promise tasks/todo.md made when the host was deferred: videos are
    /// skipped, never fail-cached, so installing the plugin later is enough.
    void videosAreSkippedNotFailCached()
    {
        QTemporaryDir dir;
        QTemporaryDir cache;
        const QString video = writeFile(dir, QStringLiteral("clip.mp4"), "not really a video");

        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QVERIFY(thumbnails.canThumbnail(video));

        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        thumbnails.request(video, ThumbnailCache::Size::Normal);

        // The worker finds no plugin and says so; after that, videos are not
        // queued again for the rest of the session.
        QTRY_VERIFY(!thumbnails.canThumbnail(video));
        QCOMPARE(thumbnails.pendingCount(), 0);
        QCOMPARE(ready.count(), 0);
        QCOMPARE(failed.count(), 0);
        QVERIFY(!thumbnails.hasFailed(video));
    }
};

QTEST_MAIN(TestPluginsMissing)
#include "tst_plugins_missing.moc"
