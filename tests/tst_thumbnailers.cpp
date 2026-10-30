#include "model/ThumbnailCache.h"
#include "platform/Thumbnailers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

using namespace pf;
using pf::platform::Thumbnailers;

namespace {

bool writeFile(const QString &path, const QByteArray &content, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(content);
    file.close();
    if (executable) {
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup);
    }
    return true;
}

QByteArray entry(const QString &exec, const QString &mime, const QString &tryExec = {})
{
    QString text = QStringLiteral("[Thumbnailer Entry]\n");
    if (!tryExec.isEmpty()) {
        text += QStringLiteral("TryExec=%1\n").arg(tryExec);
    }
    text += QStringLiteral("Exec=%1\nMimeType=%2\n").arg(exec, mime);
    return text.toUtf8();
}

} // namespace

class TestThumbnailers : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        // The process-wide registry reads these on first use.
        qputenv("XDG_DATA_HOME", (m_root.path() + QStringLiteral("/data-home")).toUtf8());
        // The system dirs stay, after the fixture: the MIME database lives there.
        qputenv(
            "XDG_DATA_DIRS",
            (m_root.path() + QStringLiteral("/data-dirs:/usr/local/share:/usr/share")).toUtf8());
        qputenv("PANEFILE_CACHE_DIR", (m_root.path() + QStringLiteral("/cache")).toUtf8());

        // A thumbnailer that copies a fixture PNG to %o.
        QImage png(40, 20, QImage::Format_ARGB32);
        png.fill(Qt::blue);
        QVERIFY(png.save(m_root.filePath(QStringLiteral("fixture.png")), "PNG"));
        QVERIFY(writeFile(m_root.filePath(QStringLiteral("bin/fake-thumb")),
                          QStringLiteral("#!/bin/sh\ncp \"%1\" \"$2\"\n")
                              .arg(m_root.filePath(QStringLiteral("fixture.png")))
                              .toUtf8(),
                          true));
        QVERIFY(writeFile(m_root.filePath(QStringLiteral("bin/slow-thumb")),
                          "#!/bin/sh\nsleep 30\n", true));
        QVERIFY(writeFile(
            m_root.filePath(QStringLiteral("data-dirs/thumbnailers/fake.thumbnailer")),
            entry(m_root.filePath(QStringLiteral("bin/fake-thumb")) + QStringLiteral(" %i %o"),
                  QStringLiteral("application/pdf;"))));
        QVERIFY(writeFile(
            m_root.filePath(QStringLiteral("data-dirs/thumbnailers/slow.thumbnailer")),
            entry(m_root.filePath(QStringLiteral("bin/slow-thumb")) + QStringLiteral(" %i %o"),
                  QStringLiteral("application/vnd.oasis.opendocument.text;"))));

        // bwrap must not hide the fixture scripts under its private /tmp, so
        // the end-to-end runs use a PATH without it (sh and cp only).
        const QString tools = m_root.filePath(QStringLiteral("tools"));
        QDir().mkpath(tools);
        for (const char *name : {"sh", "cp", "sleep"}) {
            const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
            QVERIFY2(!found.isEmpty(), name);
            QVERIFY(QFile::link(found, tools + QLatin1Char('/') + QString::fromLatin1(name)));
        }
        qputenv("PATH", tools.toUtf8());
    }

    void parsesEvinceStyleEntry()
    {
        const auto parsed = Thumbnailers::parseEntry(
            "# comment\n[Thumbnailer Entry]\nTryExec=evince-thumbnailer\n"
            "Exec=evince-thumbnailer -s %s %u %o\nMimeType=application/pdf;application/x-pdf;\n"
            "[Other]\nExec=nope\n");
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->tryExec, QStringLiteral("evince-thumbnailer"));
        QCOMPARE(parsed->exec, QStringLiteral("evince-thumbnailer -s %s %u %o"));
        QCOMPARE(parsed->mimeTypes, (QStringList{QStringLiteral("application/pdf"),
                                                 QStringLiteral("application/x-pdf")}));
    }

    void rejectsEntryWithoutExecOrMime()
    {
        QVERIFY(!Thumbnailers::parseEntry("[Thumbnailer Entry]\nExec=x\n"));
        QVERIFY(!Thumbnailers::parseEntry("[Thumbnailer Entry]\nMimeType=a/b;\n"));
    }

    void expandsFieldCodesWithoutAShell()
    {
        const QStringList argv = Thumbnailers::expandExec(
            QStringLiteral("ffmpegthumbnailer -i %i -o %o -s %s 100%% %z"),
            QStringLiteral("/tmp/a b; rm -rf.mp4"), QStringLiteral("/out/o.png"), 256);
        QCOMPARE(argv, (QStringList{QStringLiteral("ffmpegthumbnailer"), QStringLiteral("-i"),
                                    QStringLiteral("/tmp/a b; rm -rf.mp4"), QStringLiteral("-o"),
                                    QStringLiteral("/out/o.png"), QStringLiteral("-s"),
                                    QStringLiteral("256"), QStringLiteral("100%")}));

        const QStringList uri = Thumbnailers::expandExec(
            QStringLiteral("t \"%u\" \"a \\\"q\\\" b\""), QStringLiteral("/x/a b.pdf"), {}, 0);
        QCOMPARE(uri, (QStringList{QStringLiteral("t"), QStringLiteral("file:///x/a%20b.pdf"),
                                   QStringLiteral("a \"q\" b")}));
    }

    void higherDirectoryShadowsAndMissingTryExecIsSkipped()
    {
        QTemporaryDir high;
        QTemporaryDir low;
        const QString sh = QStandardPaths::findExecutable(QStringLiteral("sh"));
        QVERIFY(!sh.isEmpty());
        QVERIFY(writeFile(
            high.filePath(QStringLiteral("a.thumbnailer")),
            entry(sh + QStringLiteral(" high %i %o"), QStringLiteral("application/pdf;"))));
        QVERIFY(writeFile(
            low.filePath(QStringLiteral("a.thumbnailer")),
            entry(sh + QStringLiteral(" low %i %o"), QStringLiteral("application/pdf;"))));
        QVERIFY(writeFile(low.filePath(QStringLiteral("b.thumbnailer")),
                          entry(sh + QStringLiteral(" b"), QStringLiteral("application/zip;"),
                                QStringLiteral("definitely-not-installed-xyz"))));

        const Thumbnailers registry = Thumbnailers::fromDirectories({high.path(), low.path()});
        QCOMPARE(registry.entryCount(), 1);
        const QMimeDatabase db;
        const auto command =
            registry.commandFor(db.mimeTypeForName(QStringLiteral("application/pdf")),
                                QStringLiteral("/in.pdf"), QStringLiteral("/o.png"), 128);
        QVERIFY(command.has_value());
        QCOMPARE(command->arguments.first(), QStringLiteral("high"));
        QVERIFY(!registry.handles(db.mimeTypeForName(QStringLiteral("application/zip"))));
    }

    void matchesByAlias()
    {
        QTemporaryDir dir;
        const QString sh = QStandardPaths::findExecutable(QStringLiteral("sh"));
        // application/x-pdf is an alias of application/pdf.
        QVERIFY(
            writeFile(dir.filePath(QStringLiteral("p.thumbnailer")),
                      entry(sh + QStringLiteral(" %i %o"), QStringLiteral("application/x-pdf;"))));
        const Thumbnailers registry = Thumbnailers::fromDirectories({dir.path()});
        const QMimeType pdf = QMimeDatabase().mimeTypeForName(QStringLiteral("application/pdf"));
        if (!pdf.aliases().contains(QStringLiteral("application/x-pdf"))) {
            QSKIP("shared-mime-info has no x-pdf alias");
        }
        QVERIFY(registry.handles(pdf));
    }

    void sandboxedArgvShape()
    {
        platform::ThumbnailerCommand cmd{
            .program = QStringLiteral("/usr/bin/t"),
            .arguments = {QStringLiteral("-i"), QStringLiteral("/in/a.pdf")}};
        const QStringList argv =
            Thumbnailers::sandboxed(cmd, QStringLiteral("/usr/bin/bwrap"),
                                    QStringLiteral("/in/a.pdf"), QStringLiteral("/work"));
        QCOMPARE(argv.first(), QStringLiteral("/usr/bin/bwrap"));
        QVERIFY(argv.contains(QStringLiteral("--unshare-all")));
        QVERIFY(argv.contains(QStringLiteral("--die-with-parent")));
        QVERIFY(argv.contains(QStringLiteral("--clearenv")));
        const qsizetype sep = argv.indexOf(QStringLiteral("--"));
        QVERIFY(sep > 0);
        QCOMPARE(argv.mid(sep + 1), (QStringList{QStringLiteral("/usr/bin/t"), QStringLiteral("-i"),
                                                 QStringLiteral("/in/a.pdf")}));
        const qsizetype ro = argv.indexOf(QStringLiteral("/in/a.pdf"));
        QCOMPARE(argv.at(ro - 1), QStringLiteral("--ro-bind"));
        const qsizetype bind = argv.indexOf(QStringLiteral("--bind"));
        QCOMPARE(argv.at(bind + 1), QStringLiteral("/work"));
    }

    void sandboxRunsAThumbnailerWhenBwrapIsAvailable()
    {
        const QString bwrap = qEnvironmentVariable("PF_TEST_BWRAP");
        QString found = bwrap;
        const QStringList candidates{QStringLiteral("/usr/bin/bwrap"),
                                     QStringLiteral("/bin/bwrap")};
        for (const QString &candidate : candidates) {
            if (found.isEmpty() && QFileInfo(candidate).isExecutable()) {
                found = candidate;
            }
        }
        if (found.isEmpty()) {
            QSKIP("bubblewrap is not installed");
        }
        QTemporaryDir work;
        const QString input = m_root.filePath(QStringLiteral("fixture.png"));
        platform::ThumbnailerCommand cmd{
            .program = QStringLiteral("/usr/bin/cp"),
            .arguments = {input, work.filePath(QStringLiteral("out.png"))}};
        QStringList argv = Thumbnailers::sandboxed(cmd, found, input, work.path());
        QProcess process;
        const QString program = argv.takeFirst();
        process.start(program, argv);
        QVERIFY(process.waitForFinished(10000));
        if (process.readAllStandardError().startsWith("bwrap:")) {
            QSKIP("bubblewrap cannot create a sandbox here");
        }
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(QFileInfo::exists(work.filePath(QStringLiteral("out.png"))));
    }

    /// End to end: a registered thumbnailer produces a cache entry with the
    /// spec's chunks, for a type Qt cannot decode itself.
    void externalThumbnailerFillsTheCache()
    {
        const QString file = m_root.filePath(QStringLiteral("doc.pdf"));
        QVERIFY(writeFile(file, "%PDF-1.4\n"));

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QVERIFY(thumbnails.canThumbnail(file));

        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        thumbnails.request(file, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() == 1, 15000);

        const QString path =
            ThumbnailCache::thumbnailPathFor(file, ThumbnailCache::Size::Normal, cache.path());
        const QImage written = QImageReader(path).read();
        QVERIFY(!written.isNull());
        QCOMPARE(written.text(QStringLiteral("Thumb::URI")), ThumbnailCache::fileUri(file));
        QCOMPARE(written.text(QStringLiteral("Thumb::MTime")),
                 QString::number(QFileInfo(file).lastModified().toSecsSinceEpoch()));
    }

    /// A thumbnailer that hangs is killed at the timeout and fail-cached.
    void hungThumbnailerIsKilledAndFailCached()
    {
        const QString file = m_root.filePath(QStringLiteral("hang.odt"));
        QVERIFY(writeFile(file, "x"));

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        if (!thumbnails.canThumbnail(file)) {
            QSKIP("odt is not a known MIME type here");
        }
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        thumbnails.request(file, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 20000);
        QVERIFY(thumbnails.hasFailed(file));
    }
};

QTEST_MAIN(TestThumbnailers)
#include "tst_thumbnailers.moc"
