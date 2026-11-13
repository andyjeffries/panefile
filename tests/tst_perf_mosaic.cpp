// Mosaic image-size probe: cancellation when leaving a folder, linear cost of
// applying results, and no stat per image on a revisit.

#include "model/DirectoryModel.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

namespace {

QByteArray pngBytes()
{
    QImage image(300, 200, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString makeImages(const QString &root, const QString &tag, int count)
{
    static const QByteArray png = pngBytes();
    const QString dir = root + QStringLiteral("/%1-%2").arg(tag).arg(count);
    QDir().mkpath(dir);
    for (int i = 0; i < count; ++i) {
        QFile file(dir + QStringLiteral("/img-%1.png").arg(i, 6, 10, QLatin1Char('0')));
        if (!file.open(QIODevice::WriteOnly)) {
            qFatal("cannot create %s", qPrintable(file.fileName()));
        }
        file.write(png);
    }
    return dir;
}

void scanned(DirectoryModel &model)
{
    (void)QTest::qWaitFor([&] { return !model.isScanning(); }, 120000);
    QCoreApplication::processEvents();
}

void readRows(DirectoryModel &model)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        (void)model.index(row, 0).data(DirectoryModel::AspectRatioRole);
    }
}

bool allProbed(const DirectoryModel &model)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (!model.index(row, 0).data(DirectoryModel::ImageSizeRole).toSize().isValid()) {
            return false;
        }
    }
    return true;
}

qint64 guiEventNs(const QString &dir)
{
    DirectoryModel model;
    model.setImageSizesWanted(true);
    model.setPath(dir);
    scanned(model);
    readRows(model);
    qint64 ns = 0;
    QElapsedTimer slice;
    QElapsedTimer wall;
    wall.start();
    while (!allProbed(model) && wall.elapsed() < 300000) {
        slice.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ns += slice.nsecsElapsed();
    }
    return ns;
}

} // namespace

class TestPerfMosaic : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_root.isValid()); }

    void leavingAFolderCancelsItsProbe()
    {
        const QString big = makeImages(m_root.path(), QStringLiteral("big"), 20000);
        const QString small = makeImages(m_root.path(), QStringLiteral("small"), 10);

        QElapsedTimer read;
        read.start();
        for (const QString &name : QDir(big).entryList(QDir::Files)) {
            QImageReader reader(big + QLatin1Char('/') + name);
            (void)reader.size();
            (void)reader.transformation();
        }
        const qint64 baseline = read.elapsed();

        DirectoryModel model;
        model.setImageSizesWanted(true);
        model.setPath(big);
        scanned(model);
        readRows(model);
        QCoreApplication::processEvents();

        QElapsedTimer timer;
        timer.start();
        model.setPath(small);
        scanned(model);
        readRows(model);
        QVERIFY(QTest::qWaitFor([&] { return allProbed(model); }, 60000));
        QVERIFY2(timer.elapsed() < baseline / 4,
                 qPrintable(QStringLiteral("took %1ms, baseline %2ms")
                                .arg(timer.elapsed())
                                .arg(baseline)));
    }

    void probeApplyScalesLinearly()
    {
        const QString d5 = makeImages(m_root.path(), QStringLiteral("scale"), 5000);
        const QString d20 = makeImages(m_root.path(), QStringLiteral("scale"), 20000);
        const qint64 t5 = guiEventNs(d5);
        const qint64 t20 = guiEventNs(d20);
        QVERIFY2(t20 < t5 * 8, qPrintable(QStringLiteral("5000: %1ms, 20000: %2ms")
                                              .arg(t5 / 1000000)
                                              .arg(t20 / 1000000)));
    }

    void revisitLayoutPassDoesNotStat()
    {
        const QString dir = makeImages(m_root.path(), QStringLiteral("scale"), 20000);
        {
            DirectoryModel first;
            first.setImageSizesWanted(true);
            first.setPath(dir);
            scanned(first);
            readRows(first);
            QVERIFY(QTest::qWaitFor([&] { return allProbed(first); }, 120000));
        }
        DirectoryModel again;
        again.setImageSizesWanted(true);
        again.setPath(dir);
        scanned(again);
        QElapsedTimer timer;
        timer.start();
        readRows(again);
        const qint64 firstPass = timer.elapsed();
        timer.restart();
        readRows(again);
        const qint64 laterPass = timer.elapsed();
        QVERIFY2(firstPass < laterPass * 3 + 20,
                 qPrintable(QStringLiteral("first %1ms, later %2ms").arg(firstPass).arg(laterPass)));
    }
};

QTEST_MAIN(TestPerfMosaic)
#include "tst_perf_mosaic.moc"
