// Security regression (SEC-1): code running inside the bubblewrap sandbox must
// not be able to switch the sandbox off by printing "bwrap:" to stderr.
#include "model/ThumbnailCache.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <unistd.h>

using namespace pf;

class TestSecSandbox : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        if (QStandardPaths::findExecutable(QStringLiteral("bwrap")).isEmpty()) {
            QSKIP("bubblewrap is not installed");
        }
        qputenv("XDG_DATA_HOME", (m_root.path() + QStringLiteral("/data-home")).toUtf8());
        qputenv("XDG_DATA_DIRS",
                (m_root.path() + QStringLiteral("/data-dirs:/usr/local/share:/usr/share")).toUtf8());
        qputenv("PANEFILE_CACHE_DIR", (m_root.path() + QStringLiteral("/cache")).toUtf8());

        // A thumbnailer installed in /usr whose behaviour is decided by the
        // file it reads: a stand-in for a parser bug exploited by file content.
        const QString dir = m_root.filePath(QStringLiteral("data-dirs/thumbnailers"));
        QVERIFY(QDir().mkpath(dir));
        QFile entry(dir + QStringLiteral("/sh.thumbnailer"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Thumbnailer Entry]\nExec=/usr/bin/sh %i %o\n"
                    "MimeType=application/vnd.ms-excel;\n");
    }

    void sandboxedThumbnailerCannotDisableTheSandbox()
    {
        const QString marker = m_root.filePath(QStringLiteral("escaped"));
        const QString file = m_root.filePath(QStringLiteral("evil.xls"));
        QFile evil(file);
        QVERIFY(evil.open(QIODevice::WriteOnly));
        // Inside the sandbox /etc/passwd is absent: pretend bwrap failed.
        // Outside it, write to the host.
        evil.write(QStringLiteral("if [ ! -e /etc/passwd ]; then echo 'bwrap: fake' >&2; exit 1; fi\n"
                                  "echo unsandboxed > '%1'\nexit 1\n")
                       .arg(marker)
                       .toUtf8());
        evil.close();

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QVERIFY(thumbnails.canThumbnail(file));
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        thumbnails.request(file, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 20000);

        QVERIFY2(!QFileInfo::exists(marker),
                 "the same file ran again outside the sandbox and wrote to the host");
    }
    // SEC-2: the sandbox shares its output directory with the host, so code in
    // it can leave a FIFO where the PNG should be. Reading it must not block a
    // thumbnail worker for ever (the pool has two, and quitting waits on them).
    void sandboxedThumbnailerCannotHangTheWorkerWithAFifo()
    {
        const QString file = m_root.filePath(QStringLiteral("fifo.xls"));
        QFile evil(file);
        QVERIFY(evil.open(QIODevice::WriteOnly));
        evil.write("mkfifo \"$1\"\nexit 0\n");
        evil.close();

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QVERIFY(thumbnails.canThumbnail(file));
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        thumbnails.request(file, ThumbnailCache::Size::Normal);

        const bool finished = QTest::qWaitFor(
            [&] { return failed.count() + ready.count() > 0; }, 20000);

        // Unblock the stuck reader so the test process can exit either way.
        const QString work = qEnvironmentVariable("PANEFILE_CACHE_DIR") + QStringLiteral("/thumbnailer");
        for (const QString &task : QDir(work).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QByteArray fifo =
                QFile::encodeName(work + QLatin1Char('/') + task + QStringLiteral("/out.png"));
            if (const int fd = ::open(fifo.constData(), O_WRONLY | O_NONBLOCK); fd >= 0) {
                ::close(fd);
            }
        }
        QVERIFY2(finished, "a FIFO left as out.png blocked the thumbnail worker for 20 s+");
    }

    // SEC-2: a symlink left as out.png must not make Panefile decode a host
    // file as the hostile file's thumbnail.
    void sandboxedThumbnailerCannotPointOutputAtAHostFile()
    {
        const QString hostPng = m_root.filePath(QStringLiteral("host.png"));
        QImage secret(8, 8, QImage::Format_RGB32);
        secret.fill(Qt::red);
        QVERIFY(secret.save(hostPng, "PNG"));

        const QString file = m_root.filePath(QStringLiteral("link.xls"));
        QFile evil(file);
        QVERIFY(evil.open(QIODevice::WriteOnly));
        evil.write(QStringLiteral("ln -s '%1' \"$1\"\nexit 0\n").arg(hostPng).toUtf8());
        evil.close();

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        QSignalSpy ready(&thumbnails, &ThumbnailCache::ready);
        thumbnails.request(file, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() + ready.count() > 0, 20000);
        QCOMPARE(ready.count(), 0);
    }

    // SEC-3: the sandbox shares its work directory with the host. A directory
    // it makes unreadable survives QTemporaryDir's cleanup, so its contents stay
    // in ~/.cache/panefile/thumbnailer for good.
    void sandboxedThumbnailerLeavesNothingBehind()
    {
        const QString file = m_root.filePath(QStringLiteral("residue.xls"));
        QFile evil(file);
        QVERIFY(evil.open(QIODevice::WriteOnly));
        evil.write("d=$(dirname \"$1\"); mkdir \"$d/keep\"; head -c 5000000 /dev/zero > \"$d/keep/blob\"; chmod 000 \"$d/keep\"; exit 1\n");
        evil.close();

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        thumbnails.request(file, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 20000);
        const QString work = qEnvironmentVariable("PANEFILE_CACHE_DIR") + QStringLiteral("/thumbnailer");
        const QStringList left = QDir(work).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &t : left) {
            QFile::setPermissions(work + QLatin1Char('/') + t + QStringLiteral("/keep"),
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        QVERIFY2(left.isEmpty(), qPrintable(left.join(QLatin1Char(','))));
    }
    // SEC-5: a tree deeper than PATH_MAX cannot be walked or removed by path,
    // so the work directory must still go.
    void sandboxedThumbnailerLeavesNoDeepTreeBehind()
    {
        const QString file = m_root.filePath(QStringLiteral("deep.xls"));
        QFile evil(file);
        QVERIFY(evil.open(QIODevice::WriteOnly));
        evil.write("cd -P \"$(dirname \"$1\")\" || exit 1; p=a; i=1; while [ $i -lt 1000 ]; do p=$p/a; i=$((i+1)); done; "
                   "for n in 1 2 3; do mkdir -p $p && cd -P $p || exit 1; done; "
                   "head -c 5000000 /dev/zero > blob; exit 1\n");
        evil.close();

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        thumbnails.request(file, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 20000);
        const QString work = qEnvironmentVariable("PANEFILE_CACHE_DIR") + QStringLiteral("/thumbnailer");
        const QStringList left = QDir(work).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &t : left) {
            QProcess::execute(QStringLiteral("rm"), {QStringLiteral("-rf"), work + QLatin1Char('/') + t});
        }
        QVERIFY2(left.isEmpty(), qPrintable(left.join(QLatin1Char(','))));
    }
    // SEC-6: removeTree re-lists (and fstatats every entry of) a directory each
    // time it climbs back into it, so a wide directory costs O(n^2) syscalls on
    // a thumbnail worker thread.
    void sandboxedThumbnailerCannotStallCleanupWithAWideTree()
    {
        const int n = qEnvironmentVariableIntValue("PF_WIDE_N") > 0 ? qEnvironmentVariableIntValue("PF_WIDE_N") : 20000;
        const QString file = m_root.filePath(QStringLiteral("wide.xls"));
        QFile evil(file);
        QVERIFY(evil.open(QIODevice::WriteOnly));
        evil.write(QStringLiteral("cd -P \"$(dirname \"$1\")\" || exit 1; seq 1 %1 | xargs mkdir; exit 1\n").arg(n).toUtf8());
        evil.close();

        QTemporaryDir cache;
        ThumbnailCache thumbnails;
        thumbnails.setCacheRoot(cache.path());
        QSignalSpy failed(&thumbnails, &ThumbnailCache::failed);
        QElapsedTimer timer;
        timer.start();
        thumbnails.request(file, ThumbnailCache::Size::Normal);
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 600000);
        qInfo() << n << "subdirectories: request to failed took" << timer.elapsed() << "ms";
        QVERIFY2(timer.elapsed() < 15000, "cleanup of a wide tree is too slow");
    }
};

QTEST_MAIN(TestSecSandbox)
#include "tst_sec_sandbox.moc"
