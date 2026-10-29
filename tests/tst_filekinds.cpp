#include "app/FileKinds.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

class TestFileKinds : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kindsFollowTheExtension()
    {
        QCOMPARE(kindOf(QStringLiteral("/x/a.png")), FileKind::Image);
        QCOMPARE(kindOf(QStringLiteral("/x/a.jpg")), FileKind::Image);
        QCOMPARE(kindOf(QStringLiteral("/x/a.mp4")), FileKind::Video);
        QCOMPARE(kindOf(QStringLiteral("/x/a.mp3")), FileKind::Audio);
        QCOMPARE(kindOf(QStringLiteral("/x/a.zip")), FileKind::Archive);
        QCOMPARE(kindOf(QStringLiteral("/x/a.txt")), FileKind::Other);
    }

    void allOfKindNeedsRegularFilesOfOneKind()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const char *name : {"a.png", "b.jpg", "c.txt"}) {
            QFile file(dir.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }
        const QString png = dir.filePath(QStringLiteral("a.png"));
        const QString jpg = dir.filePath(QStringLiteral("b.jpg"));
        const QString txt = dir.filePath(QStringLiteral("c.txt"));
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("folder.png")));

        QVERIFY(allOfKind({png, jpg}, FileKind::Image));
        QVERIFY(!allOfKind({png, txt}, FileKind::Image));
        QVERIFY(!allOfKind({}, FileKind::Image));
        // A directory named like an image is not one.
        QVERIFY(!allOfKind({dir.filePath(QStringLiteral("folder.png"))}, FileKind::Image));
        QVERIFY(!allOfKind({png}, FileKind::Video));
    }

    void ffmpegCanBeOverridden()
    {
        qputenv("PANEFILE_FFMPEG", QByteArray());
        QVERIFY(ffmpegPath().isEmpty());
        qputenv("PANEFILE_FFMPEG", "/opt/ffmpeg");
        QCOMPARE(ffmpegPath(), QStringLiteral("/opt/ffmpeg"));
        qunsetenv("PANEFILE_FFMPEG");
    }
};

QTEST_MAIN(TestFileKinds)
#include "tst_filekinds.moc"
