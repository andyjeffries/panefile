#include "model/DirectoryModel.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

static QString make(const QString &root, int count, const char *tag)
{
    QImage image(300, 200, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QByteArray png;
    QBuffer b(&png);
    b.open(QIODevice::WriteOnly);
    image.save(&b, "PNG");
    const QString dir = root + QStringLiteral("/%1%2").arg(QLatin1String(tag)).arg(count);
    QDir().mkpath(dir);
    for (int i = 0; i < count; ++i) {
        QFile f(dir + QStringLiteral("/img-%1.png").arg(i, 6, 10, QLatin1Char('0')));
        f.open(QIODevice::WriteOnly);
        f.write(png);
    }
    return dir;
}

static qint64 run(const QString &dir, bool removeFirst)
{
    DirectoryModel model;
    model.setImageSizesWanted(true);
    model.setPath(dir);
    (void)QTest::qWaitFor([&] { return !model.isScanning(); }, 120000);
    QCoreApplication::processEvents();
    for (int r = 0; r < model.rowCount(); ++r)
        (void)model.index(r, 0).data(DirectoryModel::AspectRatioRole);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1); // flush -> workers start
    if (removeFirst) {
        model.expectRemoval({model.entryAt(0)->name});
    }
    qint64 ns = 0;
    QElapsedTimer slice, wall;
    wall.start();
    int pending = 1;
    while (pending && wall.elapsed() < 120000) {
        slice.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        ns += slice.nsecsElapsed();
        pending = 0;
        for (int r = 0; r < model.rowCount(); ++r)
            if (!model.index(r, 0).data(DirectoryModel::ImageSizeRole).toSize().isValid()) { pending = 1; break; }
    }
    return ns;
}

class T : public QObject
{
    Q_OBJECT
    QTemporaryDir m_root;
private Q_SLOTS:
    void shift()
    {
        const QString a = make(m_root.path(), 30000, "a");
        const QString b = make(m_root.path(), 30000, "b");
        const qint64 base = run(a, false);
        const qint64 shifted = run(b, true);
        qWarning("gui event ms: no removal %lld, removal at row0 %lld", base / 1000000, shifted / 1000000);
    }
};
QTEST_MAIN(T)
#include "tst_perf_performance_mosaic_shift.moc"
