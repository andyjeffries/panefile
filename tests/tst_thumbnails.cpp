#include "model/ThumbnailCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QGuiApplication>
#include <QImageReader>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

namespace {

/// A small PNG written to disk, so the cache has something real to thumbnail.
QString writeImage(const QTemporaryDir &dir, const QString &name, int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::red);

    const QString path = dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

} // namespace

/// §7.7's freedesktop thumbnail cache.
class TestThumbnails : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// §7.7: "`<md5-of-file-uri>.png`". Interoperability with every other
    /// implementation of the spec rests on getting exactly this right, so the
    /// digest is checked against one computed independently.
    void cachePathFollowsTheSpec()
    {
        const QString path = QStringLiteral("/home/andy/Pictures/a photo.png");
        const QString uri = ThumbnailCache::fileUri(path);

        // Percent-encoded, not raw: the space matters.
        QCOMPARE(uri, QStringLiteral("file:///home/andy/Pictures/a%20photo.png"));

        const QString expected =
            QString::fromLatin1(
                QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex()) +
            QStringLiteral(".png");
        QCOMPARE(ThumbnailCache::cacheFileName(uri), expected);

        QCOMPARE(ThumbnailCache::thumbnailPathFor(path, ThumbnailCache::Size::Normal,
                                                  QStringLiteral("/cache/thumbnails")),
                 QStringLiteral("/cache/thumbnails/normal/") + expected);
        QCOMPARE(ThumbnailCache::thumbnailPathFor(path, ThumbnailCache::Size::Large,
                                                  QStringLiteral("/cache/thumbnails")),
                 QStringLiteral("/cache/thumbnails/large/") + expected);
    }

    /// §7.7: "`normal` = 128 px, `large` = 256 px".
    void sizesMatchTheSpec()
    {
        QCOMPARE(ThumbnailCache::pixelsFor(ThumbnailCache::Size::Normal), 128);
        QCOMPARE(ThumbnailCache::pixelsFor(ThumbnailCache::Size::Large), 256);
    }

    /// The generated PNG carries the chunks that make it verifiable, and is
    /// scaled into the box rather than merely copied.
    void generatesAThumbnailWithTheSpecChunks()
    {
        QTemporaryDir source;
        QTemporaryDir cache;

        const QString path = writeImage(source, QStringLiteral("big.png"), 600, 300);

        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());

        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        thumbnails.request(path, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);

        const QString thumbnail =
            ThumbnailCache::thumbnailPathFor(path, ThumbnailCache::Size::Normal, cache.path());
        QVERIFY2(QFileInfo::exists(thumbnail), qPrintable(thumbnail));

        // Read, not merely opened: a PNG's text chunks are only available once
        // the handler has decoded the file.
        const QImage written = QImageReader(thumbnail).read();
        QVERIFY(!written.isNull());

        QCOMPARE(written.text(QStringLiteral("Thumb::URI")), ThumbnailCache::fileUri(path));
        QCOMPARE(written.text(QStringLiteral("Thumb::MTime")),
                 QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch()));

        // 600×300 into a 128 box, aspect preserved.
        QCOMPARE(written.size(), QSize(128, 64));
    }

    /// §7.7: "treat a thumbnail whose Thumb::MTime doesn't match the source as
    /// stale".
    void staleThumbnailsAreNotServed()
    {
        QTemporaryDir source;
        QTemporaryDir cache;

        const QString path = writeImage(source, QStringLiteral("a.png"), 64, 64);

        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());

        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        thumbnails.request(path, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 5000);

        QVERIFY(!thumbnails.lookup(path, ThumbnailCache::Size::Normal).isNull());

        // Rewriting the source moves its mtime on, which must invalidate the
        // cached copy even though the file is still there.
        QTest::qWait(1100);
        writeImage(source, QStringLiteral("a.png"), 64, 64);

        QVERIFY(thumbnails.lookup(path, ThumbnailCache::Size::Normal).isNull());
    }

    /// §7.7: "Write failures to fail/panefile/ so you don't retry a file that
    /// can't be thumbnailed."
    void failuresAreRecordedAndNotRetried()
    {
        QTemporaryDir source;
        QTemporaryDir cache;

        // A file that claims to be a PNG and is not.
        const QString path = source.filePath(QStringLiteral("broken.png"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("not an image at all"));
        file.close();

        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());

        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        thumbnails.request(path, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 5000);

        QVERIFY(thumbnails.hasFailed(path));

        // The second request is refused outright rather than queued.
        thumbnails.request(path, ThumbnailCache::Size::Normal);
        QCOMPARE(thumbnails.pendingCount(), 0);
    }

    /// §7.7's `max_file_size_mb`, and the rule that a directory is never a
    /// thumbnail candidate.
    void refusesWhatItShould()
    {
        QTemporaryDir source;
        QTemporaryDir cache;

        const QString path = writeImage(source, QStringLiteral("a.png"), 400, 400);

        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());

        QVERIFY(thumbnails.canThumbnail(path));

        thumbnails.setMaxFileSizeMb(0);
        QVERIFY(!thumbnails.canThumbnail(path));

        thumbnails.setMaxFileSizeMb(200);
        thumbnails.setEnabled(false);
        QVERIFY(!thumbnails.canThumbnail(path));

        thumbnails.setEnabled(true);
        QVERIFY(!thumbnails.canThumbnail(source.path()));

        // A text file is not an image whatever its extension suggests.
        const QString text = source.filePath(QStringLiteral("notes.txt"));
        QFile file(text);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("plain"));
        file.close();
        QVERIFY(!thumbnails.canThumbnail(text));
    }

    /// §7.7: "cancel requests for rows that scroll away".
    void cancelledRequestsDeliverNothing()
    {
        QTemporaryDir source;
        QTemporaryDir cache;

        const QString path = writeImage(source, QStringLiteral("a.png"), 800, 800);

        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());

        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        thumbnails.request(path, ThumbnailCache::Size::Normal);
        thumbnails.cancel(path);

        QCOMPARE(thumbnails.pendingCount(), 0);
        QTest::qWait(400);
        QCOMPARE(ready.count(), 0);
    }
};

QTEST_MAIN(TestThumbnails)
#include "tst_thumbnails.moc"
