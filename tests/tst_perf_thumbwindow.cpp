#include "model/DirectoryModel.h"
#include "model/ThumbnailCache.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

/// Performance probe: what a scroll tick costs once the visible tiles already
/// have thumbnails. FilePanel calls requestThumbnails() on every scrollbar
/// valueChanged (per pixel in icon view), with the same visible rows.
class TestPerfThumbWindow : public QObject
{
    Q_OBJECT

private:
    void measure(int fillerFiles)
    {
        constexpr int kImages = 60;
        QTemporaryDir dir;
        QTemporaryDir cache;
        ThumbnailCache::instance().setCacheRoot(cache.path());
        ThumbnailCache::instance().setEnabled(true);

        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(Qt::red);
        for (int i = 0; i < kImages; ++i) {
            QVERIFY(image.save(dir.filePath(QStringLiteral("img%1.png").arg(i)), "PNG"));
        }
        for (int i = 0; i < fillerFiles; ++i) {
            QFile file(dir.filePath(QStringLiteral("f%1.txt").arg(i, 6, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        DirectoryModel model;
        model.setPath(dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), fillerFiles + kImages, 30000);
        QTRY_VERIFY(!model.isScanning());
        model.setThumbnailsEnabled(true);

        QList<int> visible;
        for (int i = 0; i < kImages; ++i) {
            visible.append(model.indexOfName(QStringLiteral("img%1.png").arg(i)));
        }

        QSignalSpy ready(&ThumbnailCache::instance(), &ThumbnailCache::ready);
        model.requestThumbnails(visible, {});
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= kImages, 10000);
        QTest::qWait(100);

        ready.clear();
        QSignalSpy changed(&model, &DirectoryModel::dataChanged);
        constexpr int kTicks = 100;
        QElapsedTimer timer;
        timer.start();
        for (int tick = 0; tick < kTicks; ++tick) {
            model.requestThumbnails(visible, {}); // same window: nothing new
        }
        const qint64 ms = timer.elapsed();
        QCOMPARE(ready.count(), 0);
        QCOMPARE(changed.count(), 0);
        qInfo().noquote() << QStringLiteral(
                                 "entries=%1 ticks=%2 total=%3ms per-tick=%4ms ready=%5 "
                                 "dataChanged=%6")
                                 .arg(fillerFiles + kImages)
                                 .arg(kTicks)
                                 .arg(ms)
                                 .arg(double(ms) / kTicks, 0, 'f', 2)
                                 .arg(ready.count())
                                 .arg(changed.count());
    }


    /// Scrolling one row of tiles at a time: each tick, a row that was only
    /// overshoot becomes visible. Its thumbnail is already done.
    void measureSlide(int fillerFiles)
    {
        constexpr int kPerRow = 8;
        constexpr int kVisibleRows = 6;
        constexpr int kOvershootRows = 20;
        constexpr int kTicks = 30;
        constexpr int kImages = kPerRow * (kVisibleRows + kOvershootRows + kTicks + 1);
        QTemporaryDir dir;
        QTemporaryDir cache;
        ThumbnailCache::instance().setCacheRoot(cache.path());
        ThumbnailCache::instance().setEnabled(true);

        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(Qt::red);
        for (int i = 0; i < kImages; ++i) {
            QVERIFY(image.save(dir.filePath(QStringLiteral("img%1.png").arg(i, 5, 10, QLatin1Char('0'))), "PNG"));
        }
        for (int i = 0; i < fillerFiles; ++i) {
            QFile file(dir.filePath(QStringLiteral("f%1.txt").arg(i, 6, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        DirectoryModel model;
        model.setPath(dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), fillerFiles + kImages, 30000);
        QTRY_VERIFY(!model.isScanning());
        model.setThumbnailsEnabled(true);

        QList<int> rows;
        for (int i = 0; i < kImages; ++i) {
            rows.append(model.indexOfName(QStringLiteral("img%1.png").arg(i, 5, 10, QLatin1Char('0'))));
        }
        // Generate them all up front.
        QSignalSpy ready(&ThumbnailCache::instance(), &ThumbnailCache::ready);
        model.requestThumbnails(rows, {});
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= kImages, 30000);
        QTest::qWait(100);
        model.requestThumbnails({}, {});

        const auto windowAt = [&](int tick, QList<int> *visible, QList<int> *around) {
            visible->clear();
            around->clear();
            const int first = tick * kPerRow;
            const int last = first + (kVisibleRows * kPerRow) - 1;
            for (int r = first; r <= last; ++r) {
                visible->append(rows[r]);
            }
            for (int step = 1; step <= kOvershootRows * kPerRow; ++step) {
                if (last + step < kImages) {
                    around->append(rows[last + step]);
                }
                if (first - step >= 0) {
                    around->append(rows[first - step]);
                }
            }
        };
        QList<int> visible;
        QList<int> around;
        windowAt(0, &visible, &around);
        model.requestThumbnails(visible, around);
        QTest::qWait(100);

        ready.clear();
        QSignalSpy changed(&model, &DirectoryModel::dataChanged);
        QElapsedTimer timer;
        timer.start();
        for (int tick = 1; tick <= kTicks; ++tick) {
            windowAt(tick, &visible, &around);
            model.requestThumbnails(visible, around);
        }
        const qint64 us = timer.nsecsElapsed() / 1000;
        qInfo().noquote() << QStringLiteral("slide entries=%1 ticks=%2 per-tick=%3ms ready=%4 dataChanged=%5")
                                 .arg(fillerFiles + kImages)
                                 .arg(kTicks)
                                 .arg(double(us) / 1000.0 / kTicks, 0, 'f', 3)
                                 .arg(ready.count())
                                 .arg(changed.count());
        // Only paths new to the window may be requested, not every row that
        // moves from the overshoot into view.
        QVERIFY(ready.count() <= kTicks * kPerRow);
    }

private Q_SLOTS:
    void scrollTickCost_data()
    {
        QTest::addColumn<int>("filler");
        QTest::newRow("2k") << 2000;
        QTest::newRow("20k") << 20000;
        QTest::newRow("100k") << 100000;
    }

    void scrollTickCost()
    {
        QFETCH(int, filler);
        measure(filler);
    }

    void slideTickCost_data() { scrollTickCost_data(); }

    void slideTickCost()
    {
        QFETCH(int, filler);
        measureSlide(filler);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestPerfThumbWindow test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_perf_thumbwindow.moc"
