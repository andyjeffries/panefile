#include "fs/ArchiveReader.h"
#include "fs/jobs/ArchiveJob.h"
#include "fs/jobs/ExtractJob.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace pf::fs;

namespace {

void write(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write(contents);
    file.close();
}

QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

ArchiveEntry entry(const char *path, bool isDirectory = false)
{
    return ArchiveEntry{.path = QString::fromLatin1(path), .isDirectory = isDirectory};
}

} // namespace

/// §7.10.
class TestArchives : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ================================================== the traversal guard
    //
    // §7.10: "Guard against path traversal. Reject any entry whose resolved
    // destination escapes the extraction root. This is not optional."

    void acceptsOrdinaryPaths()
    {
        const QString root = QStringLiteral("/tmp/extract");

        QVERIFY(ArchiveReader::isSafeDestination(root, QStringLiteral("file.txt")));
        QVERIFY(ArchiveReader::isSafeDestination(root, QStringLiteral("dir/file.txt")));
        QVERIFY(ArchiveReader::isSafeDestination(root, QStringLiteral("a/b/c/deep.txt")));
        QVERIFY(ArchiveReader::isSafeDestination(root, QStringLiteral("./file.txt")));

        // A `..` that stays inside is legitimate, however odd it looks.
        QVERIFY(ArchiveReader::isSafeDestination(root, QStringLiteral("dir/../file.txt")));
    }

    void rejectsEscapes()
    {
        const QString root = QStringLiteral("/tmp/extract");

        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("../escape.txt")));
        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("../../etc/passwd")));
        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("a/../../escape.txt")));
        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("a/b/../../../escape")));
    }

    void rejectsAbsolutePaths()
    {
        const QString root = QStringLiteral("/tmp/extract");

        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("/etc/passwd")));
        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("/tmp/extract/ok.txt")));
    }

    /// A zip made on Windows can carry backslashes and drive letters, which a
    /// POSIX-only check would treat as ordinary filename characters.
    void rejectsWindowsStylePaths()
    {
        const QString root = QStringLiteral("/tmp/extract");

        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("..\\escape.txt")));
        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("C:/Windows/evil.dll")));
        QVERIFY(!ArchiveReader::isSafeDestination(root, QStringLiteral("C:\\Windows\\evil.dll")));
    }

    /// A prefix match on strings alone would accept this: "/tmp/extract-evil"
    /// starts with "/tmp/extract".
    void rejectsASiblingWithTheRootAsAPrefix()
    {
        QVERIFY(!ArchiveReader::isSafeDestination(QStringLiteral("/tmp/extract"),
                                                  QStringLiteral("../extract-evil/file.txt")));
    }

    // ===================================================== the tarbomb rule
    //
    // §7.10: "Detect 'tarbombs' — if the archive has a single top-level
    // directory, extract directly instead of nesting."

    void findsASingleTopLevelDirectory()
    {
        ArchiveListing listing;
        listing.entries = {entry("project/", true), entry("project/README.md"),
                           entry("project/src/main.cpp")};

        QCOMPARE(listing.singleTopLevelDirectory(), QStringLiteral("project"));
    }

    void reportsNoRootWhenEntriesSpillIntoTheCurrentDirectory()
    {
        // The actual tarbomb: loose files at the top level.
        ArchiveListing listing;
        listing.entries = {entry("README.md"), entry("src/main.cpp")};

        QVERIFY(listing.singleTopLevelDirectory().isEmpty());
    }

    void reportsNoRootWhenThereAreSeveralTopLevelDirectories()
    {
        ArchiveListing listing;
        listing.entries = {entry("one/", true), entry("one/a"), entry("two/", true),
                           entry("two/b")};

        QVERIFY(listing.singleTopLevelDirectory().isEmpty());
    }

    /// A well-made archive extracts in place; a tarbomb gets a directory of its
    /// own, named after the archive with every archive extension stripped.
    void chooseTheExtractionRootAccordingly()
    {
        const QString destination = QStringLiteral("/home/andy/Downloads");

        ArchiveListing wellMade;
        wellMade.entries = {entry("project/", true), entry("project/a")};
        QCOMPARE(
            ExtractJob::rootFor(QStringLiteral("/tmp/project-1.2.tar.gz"), destination, wellMade),
            destination);

        ArchiveListing bomb;
        bomb.entries = {entry("a"), entry("b")};
        QCOMPARE(ExtractJob::rootFor(QStringLiteral("/tmp/loose-files.tar.gz"), destination, bomb),
                 destination + QStringLiteral("/loose-files"));

        // Only the archive extension is stripped, not everything after the
        // first dot: `my.project.v2.zip` is not `my`.
        ArchiveListing other;
        other.entries = {entry("a")};
        QCOMPARE(ExtractJob::rootFor(QStringLiteral("/tmp/my.project.v2.zip"), destination, other),
                 destination + QStringLiteral("/my.project.v2"));
    }

    // ====================================================== round trip

    void createsAndExtractsAnArchive()
    {
        if (!ArchiveReader::isAvailable()) {
            QSKIP("no libarchive on this machine");
        }

        QTemporaryDir source;
        QTemporaryDir output;

        write(source.filePath(QStringLiteral("tree/one.txt")), QByteArray("first"));
        write(source.filePath(QStringLiteral("tree/nested/two.txt")), QByteArray("second"));

        const QString archivePath = output.filePath(QStringLiteral("bundle.zip"));

        ArchiveJob create({source.filePath(QStringLiteral("tree"))}, archivePath,
                          ArchiveFormat::Zip);
        create.run();

        QVERIFY2(create.result().succeeded(),
                 create.result().errors.isEmpty()
                     ? "cancelled"
                     : qPrintable(create.result().errors.first().reason));
        QVERIFY(QFileInfo::exists(archivePath));

        // Nothing partial is left behind.
        QVERIFY(!QFileInfo::exists(archivePath + QStringLiteral(".pf-partial")));

        // The listing sees what went in.
        const ArchiveListing listing = ArchiveReader::list(archivePath, 100);
        QVERIFY2(listing.error.isEmpty(), qPrintable(listing.error));
        QCOMPARE(listing.singleTopLevelDirectory(), QStringLiteral("tree"));

        // And it comes back out with its contents intact. A single top-level
        // directory means it extracts in place rather than nesting.
        QTemporaryDir extracted;
        ExtractJob extract(archivePath, extracted.path());
        extract.run();

        QVERIFY2(extract.result().succeeded(),
                 extract.result().errors.isEmpty()
                     ? "cancelled"
                     : qPrintable(extract.result().errors.first().reason));

        QCOMPARE(read(extracted.filePath(QStringLiteral("tree/one.txt"))), QByteArray("first"));
        QCOMPARE(read(extracted.filePath(QStringLiteral("tree/nested/two.txt"))),
                 QByteArray("second"));
    }

    /// A tarbomb gets its own directory, so extracting it does not scatter
    /// files over the download folder.
    void extractsATarbombIntoItsOwnDirectory()
    {
        if (!ArchiveReader::isAvailable()) {
            QSKIP("no libarchive on this machine");
        }

        QTemporaryDir source;
        QTemporaryDir output;

        write(source.filePath(QStringLiteral("loose1.txt")), QByteArray("a"));
        write(source.filePath(QStringLiteral("loose2.txt")), QByteArray("b"));

        const QString archivePath = output.filePath(QStringLiteral("bomb.zip"));

        ArchiveJob create({source.filePath(QStringLiteral("loose1.txt")),
                           source.filePath(QStringLiteral("loose2.txt"))},
                          archivePath, ArchiveFormat::Zip);
        create.run();
        QVERIFY(create.result().succeeded());

        QTemporaryDir extracted;
        ExtractJob extract(archivePath, extracted.path());
        extract.run();

        QVERIFY(extract.result().succeeded());
        // Canonical on both sides: the job resolves its destination so that
        // libarchive's symlink guard is not tripped by /var being a symlink on
        // macOS, and the test has to compare like with like.
        QCOMPARE(extract.extractedTo(),
                 QFileInfo(extracted.path()).canonicalFilePath() + QStringLiteral("/bomb"));
        QVERIFY(QFileInfo::exists(extracted.filePath(QStringLiteral("bomb/loose1.txt"))));
    }

    /// §7.4's `.pf-partial` rule applies to archives too: refusing to overwrite
    /// keeps a typo from destroying an existing archive.
    void refusesToOverwriteAnExistingArchive()
    {
        if (!ArchiveReader::isAvailable()) {
            QSKIP("no libarchive on this machine");
        }

        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("source.txt")), QByteArray("x"));
        write(dir.filePath(QStringLiteral("taken.zip")), QByteArray("not really a zip"));

        ArchiveJob create({dir.filePath(QStringLiteral("source.txt"))},
                          dir.filePath(QStringLiteral("taken.zip")), ArchiveFormat::Zip);
        create.run();

        QVERIFY(!create.result().succeeded());
        QCOMPARE(read(dir.filePath(QStringLiteral("taken.zip"))), QByteArray("not really a zip"));
    }

    void reportsAnUnreadableArchive()
    {
        QTemporaryDir dir;
        write(dir.filePath(QStringLiteral("broken.zip")), QByteArray("this is not an archive"));

        ExtractJob extract(dir.filePath(QStringLiteral("broken.zip")), dir.path());
        extract.run();

        QVERIFY(!extract.result().succeeded());
        QVERIFY(!extract.result().errors.isEmpty());
    }
};

QTEST_MAIN(TestArchives)
#include "tst_archives.moc"
