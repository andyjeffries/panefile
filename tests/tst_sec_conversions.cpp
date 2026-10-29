// Security re-test (cycle 2): conversions never write outside the folder
// through a symlink at the destination, and sidebar.ini paths cannot inject.

#include "fs/jobs/ImageJob.h"
#include "fs/jobs/MediaJob.h"
#include "ui/SidebarLayout.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>

using namespace pf::fs;
using pf::ui::SidebarLayout;
using pf::ui::SidebarSection;

class TestSecConversions : public QObject
{
    Q_OBJECT

    QString makeImage(const QTemporaryDir &dir, const QString &name)
    {
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        image.save(dir.filePath(name));
        return dir.filePath(name);
    }

    static void answer(Job &job, ConflictAction action)
    {
        QObject::connect(&job, &Job::conflict, &job, [&job, action] {
            job.resolveConflict({.action = action, .applyToAll = false});
        });
    }

private Q_SLOTS:
    void imageOverwriteOfLinkToExistingFileLeavesTargetAlone_data()
    {
        QTest::addColumn<int>("action");
        QTest::newRow("overwrite") << int(ConflictAction::Overwrite);
        QTest::newRow("rename") << int(ConflictAction::Rename);
        QTest::newRow("skip") << int(ConflictAction::Skip);
    }

    void imageOverwriteOfLinkToExistingFileLeavesTargetAlone()
    {
        QFETCH(int, action);
        QTemporaryDir dir;
        QTemporaryDir outside;
        const QString victim = outside.filePath(QStringLiteral("bashrc"));
        {
            QFile f(victim);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("precious\n");
        }
        const QString source = makeImage(dir, QStringLiteral("photo.jpg"));
        QVERIFY(QFile::link(victim, dir.filePath(QStringLiteral("photo.png"))));

        ImageJob job(ImageJob::Op::ConvertPng, {source});
        answer(job, static_cast<ConflictAction>(action));
        job.run();

        QFile f(victim);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("precious\n"));
    }

    void mediaDanglingLinkAtDestinationIsNotWrittenThrough()
    {
        const QString ffmpeg = QStringLiteral("/usr/bin/ffmpeg");
        if (!QFileInfo::exists(ffmpeg)) {
            QSKIP("no ffmpeg");
        }
        QTemporaryDir dir;
        QTemporaryDir outside;
        const QString planted = outside.filePath(QStringLiteral("planted.mp3"));
        const QString source = dir.filePath(QStringLiteral("a.wav"));
        QVERIFY(QProcess::execute(ffmpeg, {"-hide_banner", "-loglevel", "error", "-f", "lavfi",
                                           "-i", "sine=d=0.2", source}) == 0);
        QVERIFY(QFile::link(planted, dir.filePath(QStringLiteral("a.mp3"))));

        MediaJob job(MediaJob::Target::AudioMp3, {source}, ffmpeg);
        QSignalSpy asked(&job, &Job::conflict);
        answer(job, ConflictAction::Overwrite);
        job.run();
        qInfo() << "conflict prompts:" << asked.size()
                << "created:" << job.createdPaths()
                << "a.mp3 still a link:" << QFileInfo(dir.filePath(QStringLiteral("a.mp3"))).isSymLink()
                << "planted size:" << QFileInfo(planted).size();
        for (const auto &e : job.result().errors) {
            qInfo() << "error:" << e.reason;
        }
        QFile head(planted);
        if (head.open(QIODevice::ReadOnly)) {
            qInfo() << "planted starts with:" << head.read(4).toHex();
        }
        QVERIFY(!QFile::exists(planted));
    }

    void escapedPathsCannotBecomeBuiltinsOrKeys()
    {
        SidebarLayout layout;
        const QString id = layout.addSection(QStringLiteral("S"));
        SidebarSection &section = layout.sections[layout.sectionIndex(id)];
        section.entries.append({.builtin = {}, .path = QStringLiteral("/a\r\n[sidebar]\r\nwastebasket=0")});
        section.entries.append({.builtin = {}, .path = QStringLiteral("/trailing\\")});
        section.entries.append({.builtin = {}, .path = QStringLiteral("/x\nentry=builtin:home")});
        section.entries.append({.builtin = {}, .path = QString::fromUtf8("/u [section] title=Y")});

        const QString ini = layout.toIni();
        const SidebarLayout restored = SidebarLayout::fromIni(ini);
        QCOMPARE(restored.sections.size(), 1);
        QVERIFY(!restored.wastebasketHidden);
        QVERIFY(restored == layout);
    }
};



QTEST_GUILESS_MAIN(TestSecConversions)
#include "tst_sec_conversions.moc"
