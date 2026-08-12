// Directory scanning (§7.2, §14).
//
// §14 asks specifically for a scanner exercised against symlinks, broken
// symlinks, permission-denied subdirectories and a 10,000-file directory. Those
// are the cases where a scanner quietly does the wrong thing: following a link
// it should not, dropping an entry it cannot stat, or blocking the GUI thread
// on a directory large enough for it to show.

#include "fs/DirectoryScanner.h"
#include "model/FileEntry.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace pf;
using namespace pf::fs;

class TestScanner : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString path(const QString &relative) const
    {
        return m_dir.path() + QLatin1Char('/') + relative;
    }

    void writeFile(const QString &relative, const QByteArray &contents = "x")
    {
        QFile file(path(relative));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    /// Runs a scan to completion and returns the entries, in readdir order.
    QList<FileEntry> scanToCompletion(const QString &target, bool *failed = nullptr,
                                      QString *reason = nullptr);

private Q_SLOTS:
    void initTestCase();

    void listsEveryEntryExceptDotAndDotDot();
    void reportsSizesAndDirectories();
    void marksHiddenEntries();
    void resolvesSymlinks();
    void detectsBrokenSymlinks();
    void doesNotFollowSymlinkedDirectoriesWhenListing();
    void reportsPermissionDeniedWithAReadableReason();
    void reportsMissingDirectory();
    void deliversLargeDirectoryInBatches();
    void supersededScanDeliversNothing();
    void emptyDirectoryFinishesWithZero();
    void handlesAwkwardFilenames();
};

QList<FileEntry> TestScanner::scanToCompletion(const QString &target, bool *failed, QString *reason)
{
    DirectoryScanner scanner;
    QList<FileEntry> collected;
    bool didFail = false;
    QString failureReason;

    connect(&scanner, &DirectoryScanner::entriesReady, this,
            [&collected](const QString &, const QList<FileEntry> &batch) { collected += batch; });
    connect(&scanner, &DirectoryScanner::failed, this,
            [&didFail, &failureReason](const QString &, const QString &why) {
                didFail = true;
                failureReason = why;
            });

    QSignalSpy finished(&scanner, &DirectoryScanner::finished);
    QSignalSpy failedSpy(&scanner, &DirectoryScanner::failed);

    scanner.scan(target);

    // Either outcome ends the scan; waiting only on `finished` would hang for
    // the full timeout on every failure case.
    const int timeoutMs = 15000;
    QElapsedTimer timer;
    timer.start();
    while (finished.isEmpty() && failedSpy.isEmpty() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    if (failed != nullptr) {
        *failed = didFail;
    }
    if (reason != nullptr) {
        *reason = failureReason;
    }
    return collected;
}

void TestScanner::initTestCase()
{
    QVERIFY(m_dir.isValid());

    writeFile(QStringLiteral("alpha.txt"), "hello");
    writeFile(QStringLiteral("beta.txt"), QByteArray(1234, 'x'));
    writeFile(QStringLiteral(".hidden"));
    QVERIFY(QDir(m_dir.path()).mkdir(QStringLiteral("subdir")));

    QVERIFY(QFile::link(path(QStringLiteral("alpha.txt")), path(QStringLiteral("link-ok"))));
    QVERIFY(QFile::link(path(QStringLiteral("nowhere")), path(QStringLiteral("link-broken"))));
    QVERIFY(QFile::link(path(QStringLiteral("subdir")), path(QStringLiteral("link-dir"))));
}

void TestScanner::listsEveryEntryExceptDotAndDotDot()
{
    const QList<FileEntry> entries = scanToCompletion(m_dir.path());

    QStringList names;
    for (const FileEntry &entry : entries) {
        names << entry.name;
    }

    QVERIFY(!names.contains(QStringLiteral(".")));
    QVERIFY(!names.contains(QStringLiteral("..")));
    QVERIFY(names.contains(QStringLiteral("alpha.txt")));
    QVERIFY(names.contains(QStringLiteral(".hidden")));
    QVERIFY(names.contains(QStringLiteral("subdir")));
}

void TestScanner::reportsSizesAndDirectories()
{
    const QList<FileEntry> entries = scanToCompletion(m_dir.path());

    for (const FileEntry &entry : entries) {
        if (entry.name == QLatin1String("beta.txt")) {
            QCOMPARE(entry.size, quint64(1234));
            QVERIFY(!entry.isDir);
        }
        if (entry.name == QLatin1String("subdir")) {
            QVERIFY(entry.isDir);
            QVERIFY(!entry.isSymlink);
        }
    }
}

void TestScanner::marksHiddenEntries()
{
    // The scanner marks them; filtering is the proxy's job. A scanner that
    // dropped them would make "show hidden files" require a rescan.
    const QList<FileEntry> entries = scanToCompletion(m_dir.path());

    bool sawHidden = false;
    for (const FileEntry &entry : entries) {
        if (entry.name == QLatin1String(".hidden")) {
            sawHidden = true;
            QVERIFY(entry.isHidden);
        } else if (entry.name == QLatin1String("alpha.txt")) {
            QVERIFY(!entry.isHidden);
        }
    }
    QVERIFY(sawHidden);
}

void TestScanner::resolvesSymlinks()
{
    const QList<FileEntry> entries = scanToCompletion(m_dir.path());

    bool sawLink = false;
    for (const FileEntry &entry : entries) {
        if (entry.name != QLatin1String("link-ok")) {
            continue;
        }
        sawLink = true;
        QVERIFY(entry.isSymlink);
        QVERIFY(!entry.isBroken);
        QVERIFY(entry.linkTarget.endsWith(QLatin1String("alpha.txt")));
        // §4.2: isDir is resolved *through* the link.
        QVERIFY(!entry.isDir);
    }
    QVERIFY(sawLink);
}

void TestScanner::detectsBrokenSymlinks()
{
    const QList<FileEntry> entries = scanToCompletion(m_dir.path());

    bool sawBroken = false;
    for (const FileEntry &entry : entries) {
        if (entry.name != QLatin1String("link-broken")) {
            continue;
        }
        sawBroken = true;
        // §4.2: lstat succeeds, stat fails. The entry must still be listed —
        // a dangling link is exactly the thing a user opened the file manager
        // to find.
        QVERIFY(entry.isSymlink);
        QVERIFY(entry.isBroken);
        QVERIFY(!entry.statFailed);
    }
    QVERIFY(sawBroken);
}

void TestScanner::doesNotFollowSymlinkedDirectoriesWhenListing()
{
    const QList<FileEntry> entries = scanToCompletion(m_dir.path());

    for (const FileEntry &entry : entries) {
        if (entry.name != QLatin1String("link-dir")) {
            continue;
        }
        // It resolves *as* a directory, so Enter navigates into it, but it is
        // still flagged as a link — which is what stops recursive operations
        // walking through it (§7.4).
        QVERIFY(entry.isDir);
        QVERIFY(entry.isSymlink);
    }
}

void TestScanner::reportsPermissionDeniedWithAReadableReason()
{
    if (::geteuid() == 0) {
        QSKIP("running as root; permission bits do not apply");
    }

    QTemporaryDir locked;
    QVERIFY(locked.isValid());
    const QString target = locked.path() + QStringLiteral("/no-entry");
    QVERIFY(QDir().mkpath(target));
    QVERIFY(QFile::setPermissions(target, {}));

    bool failed = false;
    QString reason;
    const QList<FileEntry> entries = scanToCompletion(target, &failed, &reason);

    QVERIFY(entries.isEmpty());
    QVERIFY(failed);
    // §12: the errno meaning translated, not the number.
    QCOMPARE(reason, QStringLiteral("Permission denied"));

    QFile::setPermissions(target,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

void TestScanner::reportsMissingDirectory()
{
    bool failed = false;
    QString reason;
    scanToCompletion(m_dir.path() + QStringLiteral("/does-not-exist"), &failed, &reason);

    QVERIFY(failed);
    QCOMPARE(reason, QStringLiteral("No such file or directory"));
}

void TestScanner::deliversLargeDirectoryInBatches()
{
    // §14 asks for 10,000 files; §4.2 fixes the batch at 512. More than one
    // batch is the property that matters — it is what keeps a 100,000-entry
    // directory visible while it is still being read.
    QTemporaryDir large;
    QVERIFY(large.isValid());

    constexpr int kCount = 10000;
    for (int i = 0; i < kCount; ++i) {
        QFile file(large.path() + QStringLiteral("/entry-%1").arg(i));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    DirectoryScanner scanner;
    int batches = 0;
    int received = 0;
    connect(&scanner, &DirectoryScanner::entriesReady, this,
            [&batches, &received](const QString &, const QList<FileEntry> &batch) {
                ++batches;
                received += static_cast<int>(batch.size());
                QVERIFY(batch.size() <= DirectoryScanner::kBatchSize);
            });

    QSignalSpy finished(&scanner, &DirectoryScanner::finished);
    scanner.scan(large.path());
    QVERIFY(finished.wait(30000));

    QCOMPARE(received, kCount);
    QCOMPARE(finished.first().at(1).toInt(), kCount);
    QVERIFY2(batches >= kCount / DirectoryScanner::kBatchSize,
             qPrintable(QStringLiteral("only %1 batches for %2 entries").arg(batches).arg(kCount)));
}

void TestScanner::supersededScanDeliversNothing()
{
    // §7.2: "A scan that is superseded must abandon its results, not deliver
    // them." Delivering them would append one directory's entries to another
    // directory's listing.
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    for (int i = 0; i < 4000; ++i) {
        QFile file(first.path() + QStringLiteral("/old-%1").arg(i));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QFile marker(second.path() + QStringLiteral("/new-entry"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();

    DirectoryScanner scanner;
    QStringList seenPaths;
    connect(&scanner, &DirectoryScanner::entriesReady, this,
            [&seenPaths](const QString &scanPath, const QList<FileEntry> &) {
                if (!seenPaths.contains(scanPath)) {
                    seenPaths << scanPath;
                }
            });

    QSignalSpy finished(&scanner, &DirectoryScanner::finished);

    scanner.scan(first.path());
    scanner.scan(second.path()); // supersedes immediately

    QVERIFY(finished.wait(15000));

    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.first().at(0).toString(), second.path());
    QVERIFY2(!seenPaths.contains(first.path()),
             "entries from the superseded scan reached the model");
}

void TestScanner::emptyDirectoryFinishesWithZero()
{
    QTemporaryDir empty;
    QVERIFY(empty.isValid());

    DirectoryScanner scanner;
    QSignalSpy finished(&scanner, &DirectoryScanner::finished);
    scanner.scan(empty.path());
    QVERIFY(finished.wait(5000));

    QCOMPARE(finished.first().at(1).toInt(), 0);
}

void TestScanner::handlesAwkwardFilenames()
{
    QTemporaryDir awkward;
    QVERIFY(awkward.isValid());

    // Names that break naive path assembly, and the reason the scanner uses the
    // *at() family rather than concatenating strings.
    const QStringList names{QStringLiteral("with space"),    QStringLiteral("with'quote"),
                            QStringLiteral("with\"double"),  QStringLiteral("with;semi"),
                            QStringLiteral("naïve"),         QStringLiteral("emoji-🙂"),
                            QStringLiteral("-leading-dash"), QStringLiteral("--help")};

    for (const QString &name : names) {
        QFile file(awkward.path() + QLatin1Char('/') + name);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(name));
    }

    const QList<FileEntry> entries = scanToCompletion(awkward.path());

    // Compared after Unicode normalisation, because the filesystem is entitled
    // to store a different normal form than the one that was written: APFS
    // returns "naïve" decomposed (n-a-i-combining-diaeresis-v-e) where the
    // source literal here is composed. Both are the same filename, and the
    // scanner is right to hand back exactly what readdir gave it — that byte
    // sequence is what every subsequent open() and rename() has to use.
    //
    // The consequence belongs to the matcher rather than the scanner: a user
    // typing a composed "ï" into the filter must still match a decomposed name,
    // so FuzzyMatcher normalises both sides before comparing (M7).
    const auto normalised = [](QStringList list) {
        for (QString &item : list) {
            item = item.normalized(QString::NormalizationForm_C);
        }
        list.sort();
        return list;
    };

    QStringList found;
    for (const FileEntry &entry : entries) {
        found << entry.name;
    }

    QCOMPARE(normalised(found), normalised(names));
}

QTEST_MAIN(TestScanner)
#include "tst_scanner.moc"
