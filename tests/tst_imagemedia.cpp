// Image and media jobs (§7.14).

#include "fs/Trash.h"
#include "fs/UndoStack.h"
#include "fs/jobs/ImageJob.h"
#include "fs/jobs/MediaJob.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace pf::fs;

class TestImageMedia : public QObject
{
    Q_OBJECT

private:
    QString path(const QString &name) const { return m_dir.filePath(name); }

    /// A picture whose four corners differ, so a rotation or flip is visible.
    QString makeImage(const QString &name, int width = 20, int height = 10)
    {
        QImage image(width, height, QImage::Format_ARGB32);
        image.fill(Qt::white);
        image.setPixelColor(0, 0, Qt::red);
        image.setPixelColor(width - 1, 0, Qt::green);
        image.setPixelColor(0, height - 1, Qt::blue);
        image.save(path(name));
        return path(name);
    }

    static JobResult runJob(Job &job)
    {
        job.run();
        return job.result();
    }

    QTemporaryDir m_dir;

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    // ================================================================= images

    void convertsPngToJpegBesideTheOriginal()
    {
        const QString source = makeImage(QStringLiteral("pic.png"));
        ImageJob job(ImageJob::Op::ConvertJpeg, {source});
        QVERIFY(runJob(job).succeeded());

        QCOMPARE(job.createdPaths(), QStringList{path(QStringLiteral("pic.jpg"))});
        QCOMPARE(QImage(path(QStringLiteral("pic.jpg"))).size(), QSize(20, 10));
        QVERIFY(QFile::exists(source)); // the original is untouched
        QVERIFY(job.replacedItems().isEmpty());
    }

    void anExistingTargetAsksFirst()
    {
        const QString source = makeImage(QStringLiteral("clash.png"));
        QVERIFY(QFile(path(QStringLiteral("clash.jpg"))).open(QIODevice::WriteOnly));

        ImageJob job(ImageJob::Op::ConvertJpeg, {source});
        QSignalSpy asked(&job, &Job::conflict);
        connect(&job, &Job::conflict, &job, [&job] {
            job.resolveConflict({.action = ConflictAction::Rename, .applyToAll = false});
        });
        QVERIFY(runJob(job).succeeded());

        QCOMPARE(asked.size(), 1);
        QCOMPARE(job.createdPaths(), QStringList{path(QStringLiteral("clash (2).jpg"))});
        // Skip leaves the destination as it was.
        QCOMPARE(QFileInfo(path(QStringLiteral("clash.jpg"))).size(), qint64(0));
    }

    void aDanglingSymlinkAtTheTargetIsAConflictAndNeverWrittenThrough()
    {
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        const QString planted = outside.filePath(QStringLiteral("planted.jpg"));
        const QString source = makeImage(QStringLiteral("link.png"));
        QVERIFY(QFile::link(planted, path(QStringLiteral("link.jpg"))));

        ImageJob job(ImageJob::Op::ConvertJpeg, {source});
        QSignalSpy asked(&job, &Job::conflict);
        connect(&job, &Job::conflict, &job, [&job] {
            job.resolveConflict({.action = ConflictAction::Overwrite, .applyToAll = false});
        });
        QVERIFY(runJob(job).succeeded());

        QCOMPARE(asked.size(), 1);
        QVERIFY(!QFile::exists(planted));
        const QFileInfo result(path(QStringLiteral("link.jpg")));
        QVERIFY(!result.isSymLink());
        QVERIFY(result.isFile());
    }

    void rotatingRightSwapsTheSidesAndKeepsTheOriginalInTheTrash()
    {
        const QString source = makeImage(QStringLiteral("turn.png"));
        QTemporaryDir trashDir;
        Trash trash(trashDir.path());

        ImageJob job(ImageJob::Op::RotateRight, {source});
        job.setTrash(trash);
        QVERIFY(runJob(job).succeeded());

        const QImage turned(source);
        QCOMPARE(turned.size(), QSize(10, 20));
        // The top-left red corner of a clockwise turn ends at the top right.
        QCOMPARE(turned.pixelColor(9, 0), QColor(Qt::red));
        QCOMPARE(trash.list().size(), 1);
        QCOMPARE(job.replacedItems().size(), 1);

        UndoStack stack;
        stack.setTrash(trash);
        stack.push(UndoEntry{.kind = UndoEntry::Kind::Replace,
                             .description = QStringLiteral("Edit images"),
                             .movedPairs = {},
                             .trashedItems = {},
                             .replacedItems = job.replacedItems()});
        QString error;
        QVERIFY2(stack.undo(&error), qPrintable(error));

        const QImage original(source);
        QCOMPARE(original.size(), QSize(20, 10));
        QCOMPARE(original.pixelColor(0, 0), QColor(Qt::red));
        // The edited version went to the trash rather than being destroyed.
        QCOMPARE(trash.list().size(), 1);
    }

    void flippingHorizontallyMirrorsTheCorners()
    {
        const QString source = makeImage(QStringLiteral("flip.png"));
        QTemporaryDir trashDir;
        ImageJob job(ImageJob::Op::FlipHorizontal, {source});
        job.setTrash(Trash(trashDir.path()));
        QVERIFY(runJob(job).succeeded());

        const QImage flipped(source);
        QCOMPARE(flipped.pixelColor(19, 0), QColor(Qt::red));
        QCOMPARE(flipped.pixelColor(0, 0), QColor(Qt::green));
    }

    void cancellingStopsBeforeTheNextFile()
    {
        const QString first = makeImage(QStringLiteral("c1.png"));
        const QString second = makeImage(QStringLiteral("c2.png"));

        ImageJob job(ImageJob::Op::ConvertWebp, {first, second});
        connect(&job, &Job::progress, &job, [&job] { job.cancel(); });
        const JobResult result = runJob(job);

        QVERIFY(result.cancelled);
        QVERIFY(!QFile::exists(path(QStringLiteral("c2.webp"))));
    }

    void aNonImageIsReportedNotFatal()
    {
        QFile bad(path(QStringLiteral("bad.png")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not an image");
        bad.close();

        ImageJob job(ImageJob::Op::ConvertJpeg, {path(QStringLiteral("bad.png"))});
        const JobResult result = runJob(job);
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(!QFile::exists(path(QStringLiteral("bad.jpg"))));
    }

    // ================================================================== media

    void ffmpegArgumentsAreAListWithFilePrefixes()
    {
        const QStringList args =
            MediaJob::buildArguments(MediaJob::Target::VideoMp4, QStringLiteral("-weird name.mov"),
                                     QStringLiteral("out.mp4"));
        QVERIFY(args.contains(QStringLiteral("file:-weird name.mov")));
        QVERIFY(args.contains(QStringLiteral("file:out.mp4")));
        QVERIFY(args.contains(QStringLiteral("-nostdin")));
        // The name is one argument, never split or shell-quoted.
        QVERIFY(!args.contains(QStringLiteral("-weird name.mov")));
        QCOMPARE(args.at(args.indexOf(QStringLiteral("-i")) + 1),
                 QStringLiteral("file:-weird name.mov"));
        QVERIFY(args.contains(QStringLiteral("libx264")));
        QCOMPARE(args.last(), QStringLiteral("file:out.mp4"));
    }

    void codecsFollowTheTarget()
    {
        const auto args = [](MediaJob::Target target) {
            return MediaJob::buildArguments(target, QStringLiteral("a"), QStringLiteral("b"));
        };
        QVERIFY(args(MediaJob::Target::VideoWebm).contains(QStringLiteral("libvpx-vp9")));
        QVERIFY(args(MediaJob::Target::VideoGif)
                    .contains(QStringLiteral("fps=12,scale=480:-1:flags=lanczos")));
        QVERIFY(args(MediaJob::Target::VideoAudioMp3).contains(QStringLiteral("-vn")));
        QVERIFY(args(MediaJob::Target::AudioFlac).contains(QStringLiteral("flac")));
        QVERIFY(args(MediaJob::Target::AudioOgg).contains(QStringLiteral("libvorbis")));
        QVERIFY(args(MediaJob::Target::AudioMp3).contains(QStringLiteral("libmp3lame")));
    }

    void parsesDurationsAndPartialNames()
    {
        QCOMPARE(MediaJob::parseDuration(QStringLiteral("  Duration: 00:01:02.50, start: 0")),
                 62.5);
        QCOMPARE(MediaJob::parseDuration(QStringLiteral("nothing")), -1.0);
        QCOMPARE(MediaJob::partialPathFor(QStringLiteral("/d/clip.mp4")),
                 QStringLiteral("/d/.clip.pf-partial.mp4"));
    }

    void convertsWavToFlacWhenFfmpegIsInstalled()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            QSKIP("ffmpeg is not installed");
        }

        // One second of silence: a 44-byte header and 16-bit mono at 8 kHz.
        QByteArray wav;
        const quint32 dataSize = 8000 * 2;
        const auto le32 = [&wav](quint32 v) {
            for (int i = 0; i < 4; ++i)
                wav.append(char((v >> (8 * i)) & 0xff));
        };
        const auto le16 = [&wav](quint16 v) {
            wav.append(char(v & 0xff));
            wav.append(char(v >> 8));
        };
        wav += "RIFF";
        le32(36 + dataSize);
        wav += "WAVEfmt ";
        le32(16);
        le16(1);
        le16(1);
        le32(8000);
        le32(16000);
        le16(2);
        le16(16);
        wav += "data";
        le32(dataSize);
        wav += QByteArray(int(dataSize), '\0');
        QFile file(path(QStringLiteral("tone.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(wav);
        file.close();

        MediaJob job(MediaJob::Target::AudioFlac, {path(QStringLiteral("tone.wav"))}, ffmpeg);
        const JobResult result = runJob(job);
        QVERIFY2(result.succeeded(),
                 result.errors.isEmpty() ? "" : qPrintable(result.errors.first().reason));
        QVERIFY(QFile::exists(path(QStringLiteral("tone.flac"))));
        QVERIFY(!QFile::exists(MediaJob::partialPathFor(path(QStringLiteral("tone.flac")))));
    }

    void aFailedConversionLeavesNoPartialFile()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            QSKIP("ffmpeg is not installed");
        }
        QFile bad(path(QStringLiteral("broken.wav")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("this is not audio");
        bad.close();

        MediaJob job(MediaJob::Target::AudioFlac, {path(QStringLiteral("broken.wav"))}, ffmpeg);
        const JobResult result = runJob(job);
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(!QFile::exists(path(QStringLiteral("broken.flac"))));
        QVERIFY(!QFile::exists(MediaJob::partialPathFor(path(QStringLiteral("broken.flac")))));
    }

    /// A program that is not ffmpeg at all: the job reports it, and leaves
    /// nothing behind.
    void aMissingProgramIsAnError()
    {
        QFile source(path(QStringLiteral("x.wav")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("x");
        source.close();

        MediaJob job(MediaJob::Target::AudioMp3, {path(QStringLiteral("x.wav"))},
                     QStringLiteral("/no/such/ffmpeg"));
        const JobResult result = runJob(job);
        QCOMPARE(result.errors.size(), 1);
        QVERIFY(!QFile::exists(path(QStringLiteral("x.mp3"))));
    }
};

QTEST_MAIN(TestImageMedia)
#include "tst_imagemedia.moc"
