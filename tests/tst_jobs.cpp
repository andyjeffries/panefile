// File operations (§7.4, §7.5, §7.13, §14).
//
// §14 asks for "Copy/move/delete jobs including cross-device simulation,
// conflicts, cancellation mid-copy leaving no partial file" and for a
// ".trashinfo generation and parsing round-trip, including URL encoding of odd
// filenames".
//
// The recurring theme is that a file manager's failure modes destroy data. Most
// of what follows checks that something did *not* happen: no partial file left
// behind, no symlink followed, no directory copied into itself, no restore over
// a file the user has since created.

#include "fs/Trash.h"
#include "fs/UndoStack.h"
#include "fs/jobs/DeleteJob.h"
#include "fs/jobs/TransferJob.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace pf::fs;

class TestJobs : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;

    QString path(const QString &relative) const
    {
        return m_root.path() + QLatin1Char('/') + relative;
    }

    void write(const QString &relative, const QByteArray &contents)
    {
        QDir().mkpath(QFileInfo(path(relative)).absolutePath());
        QFile file(path(relative));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    QByteArray read(const QString &relative) const
    {
        QFile file(path(relative));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// Runs a job to completion on this thread. The engine gives it a worker in
    /// production; here the point is the job's behaviour, not its threading.
    static JobResult runJob(Job &job)
    {
        QSignalSpy finished(&job, &Job::finished);
        job.run();
        return job.result();
    }

private Q_SLOTS:
    void init();

    // Copy
    void copiesAFile();
    void copiesADirectoryTree();
    void refusesToCopyADirectoryIntoItself();
    void refusesToCopyIntoItsOwnDescendant();
    void recreatesSymlinksWithoutFollowingThem();
    void preservesModeAndModificationTime();
    void reportsRealTotalsFromTheEnumerationPass();

    // Conflicts
    void conflictSkipLeavesTheDestination();
    void conflictOverwriteReplacesIt();
    void conflictRenameAddsASuffix();
    void conflictApplyToAllStopsAsking();

    // Cancellation
    void cancellationLeavesNoPartialFile();

    // Move
    void movesWithinOneFilesystem();
    void moveRemovesTheSourceTree();

    // Delete
    void permanentDeleteRemovesATree();
    void permanentDeleteDoesNotFollowSymlinks();

    // Trash
    void trashInfoRoundTrips();
    void trashInfoEncodesOddFilenames();
    void trashInfoRejectsGarbage();
    void trashMovesAndLists();
    void trashSuffixesNameCollisions();
    void trashRestoresToTheOriginalPath();
    void trashRefusesToRestoreOverAnExistingFile();
    void trashEmptyRemovesEverything();

    // Undo
    void undoRestoresATrashedFile();
    void undoIsBounded();
    void undoRefusesWhenTheNameIsTaken();
};

void TestJobs::init()
{
    QVERIFY(m_root.isValid());
    // A clean tree per test: these all mutate the filesystem, and a leftover
    // from one would be an invisible precondition for the next.
    const QFileInfoList entries =
        QDir(m_root.path())
            .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir() && !entry.isSymLink()) {
            QDir(entry.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(entry.absoluteFilePath());
        }
    }
}

void TestJobs::copiesAFile()
{
    write(QStringLiteral("src/a.txt"), "hello");
    QDir().mkpath(path(QStringLiteral("dst")));

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src/a.txt"))},
                    path(QStringLiteral("dst")));
    const JobResult result = runJob(job);

    QVERIFY2(result.succeeded(), qPrintable(result.errors.value(0).reason));
    QCOMPARE(read(QStringLiteral("dst/a.txt")), QByteArray("hello"));
    // The source is untouched — this is a copy.
    QCOMPARE(read(QStringLiteral("src/a.txt")), QByteArray("hello"));
}

void TestJobs::copiesADirectoryTree()
{
    write(QStringLiteral("src/one.txt"), "1");
    write(QStringLiteral("src/nested/two.txt"), "2");
    write(QStringLiteral("src/nested/deeper/three.txt"), "3");
    QDir().mkpath(path(QStringLiteral("dst")));

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src"))},
                    path(QStringLiteral("dst")));
    const JobResult result = runJob(job);

    QVERIFY(result.succeeded());
    QCOMPARE(read(QStringLiteral("dst/src/one.txt")), QByteArray("1"));
    QCOMPARE(read(QStringLiteral("dst/src/nested/two.txt")), QByteArray("2"));
    QCOMPARE(read(QStringLiteral("dst/src/nested/deeper/three.txt")), QByteArray("3"));
}

void TestJobs::refusesToCopyADirectoryIntoItself()
{
    // §7.4: "Refuse, with a clear error, to copy a directory into itself or
    // into its own descendant." Without the guard the enumeration walks into
    // the copy it is creating and never finishes.
    write(QStringLiteral("src/a.txt"), "x");

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src"))},
                    path(QStringLiteral("src")));
    const JobResult result = runJob(job);

    QVERIFY(!result.succeeded());
    QCOMPARE(result.errors.size(), 1);
    QVERIFY2(result.errors.first().reason.contains(QStringLiteral("itself")),
             qPrintable(result.errors.first().reason));
}

void TestJobs::refusesToCopyIntoItsOwnDescendant()
{
    write(QStringLiteral("src/a.txt"), "x");
    QDir().mkpath(path(QStringLiteral("src/nested/deeper")));

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src"))},
                    path(QStringLiteral("src/nested/deeper")));
    const JobResult result = runJob(job);

    QVERIFY(!result.succeeded());
    QVERIFY(!result.errors.isEmpty());
}

void TestJobs::recreatesSymlinksWithoutFollowingThem()
{
    // §7.4: "Never follow symlinks. Recreate them as symlinks pointing at the
    // same target." Following one would copy the target's contents — for a link
    // to $HOME, somebody's entire home directory.
    write(QStringLiteral("src/real.txt"), "content");
    QVERIFY(QFile::link(path(QStringLiteral("src/real.txt")), path(QStringLiteral("src/link"))));
    QDir().mkpath(path(QStringLiteral("dst")));

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src"))},
                    path(QStringLiteral("dst")));
    QVERIFY(runJob(job).succeeded());

    const QFileInfo copied(path(QStringLiteral("dst/src/link")));
    QVERIFY2(copied.isSymLink(), "the link was followed instead of recreated");
    QCOMPARE(QFileInfo(copied.absoluteFilePath()).symLinkTarget(),
             QFileInfo(path(QStringLiteral("src/real.txt"))).absoluteFilePath());
}

void TestJobs::preservesModeAndModificationTime()
{
    // §7.4: "Preserve mode, mtime". An executable that arrives unexecutable is
    // a broken copy even though every byte matches.
    write(QStringLiteral("src/script.sh"), "#!/bin/sh\n");
    QVERIFY(QFile::setPermissions(path(QStringLiteral("src/script.sh")),
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                      QFileDevice::ExeGroup));
    QDir().mkpath(path(QStringLiteral("dst")));

    const QDateTime originalTime = QFileInfo(path(QStringLiteral("src/script.sh"))).lastModified();

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src/script.sh"))},
                    path(QStringLiteral("dst")));
    QVERIFY(runJob(job).succeeded());

    const QFileInfo copied(path(QStringLiteral("dst/script.sh")));
    QVERIFY(copied.permissions().testFlag(QFileDevice::ExeOwner));
    // Filesystem timestamp resolution varies, so within a second is the useful
    // assertion rather than exact equality.
    QVERIFY(std::abs(copied.lastModified().secsTo(originalTime)) <= 1);
}

void TestJobs::reportsRealTotalsFromTheEnumerationPass()
{
    // §7.4's whole reason for two phases: "This gives real progress instead of
    // a spinner."
    write(QStringLiteral("src/a.txt"), QByteArray(1000, 'a'));
    write(QStringLiteral("src/b.txt"), QByteArray(2000, 'b'));
    QDir().mkpath(path(QStringLiteral("dst")));

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src"))},
                    path(QStringLiteral("dst")));
    QSignalSpy progress(&job, &Job::progress);
    QVERIFY(runJob(job).succeeded());

    QVERIFY(!progress.isEmpty());
    const QList<QVariant> last = progress.last();
    QCOMPARE(last.at(1).toULongLong(), quint64(3000)); // bytesTotal
    QCOMPARE(last.at(3).toInt(), 2);                   // filesTotal
}

void TestJobs::conflictSkipLeavesTheDestination()
{
    write(QStringLiteral("src/a.txt"), "new");
    write(QStringLiteral("dst/a.txt"), "old");

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src/a.txt"))},
                    path(QStringLiteral("dst")));
    connect(&job, &Job::conflict, &job,
            [&job] { job.resolveConflict({.action = ConflictAction::Skip, .applyToAll = false}); });
    runJob(job);

    QCOMPARE(read(QStringLiteral("dst/a.txt")), QByteArray("old"));
}

void TestJobs::conflictOverwriteReplacesIt()
{
    write(QStringLiteral("src/a.txt"), "new");
    write(QStringLiteral("dst/a.txt"), "old");

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src/a.txt"))},
                    path(QStringLiteral("dst")));
    connect(&job, &Job::conflict, &job, [&job] {
        job.resolveConflict({.action = ConflictAction::Overwrite, .applyToAll = false});
    });
    runJob(job);

    QCOMPARE(read(QStringLiteral("dst/a.txt")), QByteArray("new"));
}

void TestJobs::conflictRenameAddsASuffix()
{
    // §7.4: "rename (auto-suffix ` (2)`)". The counter goes before the
    // extension so the copy is still recognisably a text file.
    write(QStringLiteral("src/a.txt"), "new");
    write(QStringLiteral("dst/a.txt"), "old");

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src/a.txt"))},
                    path(QStringLiteral("dst")));
    connect(&job, &Job::conflict, &job, [&job] {
        job.resolveConflict({.action = ConflictAction::Rename, .applyToAll = false});
    });
    runJob(job);

    QCOMPARE(read(QStringLiteral("dst/a.txt")), QByteArray("old"));
    QCOMPARE(read(QStringLiteral("dst/a (2).txt")), QByteArray("new"));
}

void TestJobs::conflictApplyToAllStopsAsking()
{
    // §7.4's "apply to all remaining" checkbox. Asking again after it is ticked
    // would make it meaningless.
    // The individual files, not the directory: copying `src` into `dst` would
    // produce dst/src/fN.txt, which conflicts with nothing.
    QStringList sources;
    for (int i = 0; i < 5; ++i) {
        write(QStringLiteral("src/f%1.txt").arg(i), "new");
        write(QStringLiteral("dst/f%1.txt").arg(i), "old");
        sources << path(QStringLiteral("src/f%1.txt").arg(i));
    }

    TransferJob job(TransferJob::Mode::Copy, sources, path(QStringLiteral("dst")));

    int asked = 0;
    connect(&job, &Job::conflict, &job, [&job, &asked] {
        ++asked;
        job.resolveConflict({.action = ConflictAction::Overwrite, .applyToAll = true});
    });
    runJob(job);

    // Asked once, applied five times.
    QCOMPARE(asked, 1);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(read(QStringLiteral("dst/f%1.txt").arg(i)), QByteArray("new"));
    }
}

void TestJobs::cancellationLeavesNoPartialFile()
{
    // §7.4: "Cancellation is cooperative and must leave no half-written
    // destination file — write to `name.pf-partial` and `rename` on
    // completion." A user who cancels a copy and finds a truncated file under
    // the real name has been handed corrupt data that looks complete.
    write(QStringLiteral("src/big.bin"), QByteArray(8 * 1024 * 1024, 'x'));
    QDir().mkpath(path(QStringLiteral("dst")));

    TransferJob job(TransferJob::Mode::Copy, {path(QStringLiteral("src/big.bin"))},
                    path(QStringLiteral("dst")));

    // Cancel as soon as the copy reports any progress, which is partway through
    // the file rather than before it starts.
    connect(&job, &Job::progress, &job, [&job] { job.cancel(); });
    const JobResult result = runJob(job);

    QVERIFY(result.cancelled);
    QVERIFY2(!QFileInfo::exists(path(QStringLiteral("dst/big.bin"))),
             "a truncated file was left under the destination name");
    QVERIFY2(!QFileInfo::exists(path(QStringLiteral("dst/big.bin.pf-partial"))),
             "the partial file was not cleaned up");
}

void TestJobs::movesWithinOneFilesystem()
{
    write(QStringLiteral("src/a.txt"), "content");
    QDir().mkpath(path(QStringLiteral("dst")));

    TransferJob job(TransferJob::Mode::Move, {path(QStringLiteral("src/a.txt"))},
                    path(QStringLiteral("dst")));
    QVERIFY(runJob(job).succeeded());

    QCOMPARE(read(QStringLiteral("dst/a.txt")), QByteArray("content"));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("src/a.txt"))));
    QCOMPARE(job.removedSources(), QStringList{path(QStringLiteral("src/a.txt"))});
}

void TestJobs::moveRemovesTheSourceTree()
{
    write(QStringLiteral("src/one.txt"), "1");
    write(QStringLiteral("src/nested/two.txt"), "2");
    QDir().mkpath(path(QStringLiteral("dst")));

    TransferJob job(TransferJob::Mode::Move, {path(QStringLiteral("src"))},
                    path(QStringLiteral("dst")));
    QVERIFY(runJob(job).succeeded());

    QCOMPARE(read(QStringLiteral("dst/src/nested/two.txt")), QByteArray("2"));
    QVERIFY2(!QFileInfo::exists(path(QStringLiteral("src"))),
             "the emptied source directories were left behind");
}

void TestJobs::permanentDeleteRemovesATree()
{
    write(QStringLiteral("doomed/a.txt"), "a");
    write(QStringLiteral("doomed/nested/b.txt"), "b");

    DeleteJob job(DeleteJob::Mode::Permanent, {path(QStringLiteral("doomed"))});
    QVERIFY(runJob(job).succeeded());

    QVERIFY(!QFileInfo::exists(path(QStringLiteral("doomed"))));
}

void TestJobs::permanentDeleteDoesNotFollowSymlinks()
{
    // Deleting through a symlink would destroy the target's contents rather
    // than the link — the most damaging version of the symlink mistake.
    write(QStringLiteral("keep/precious.txt"), "precious");
    QDir().mkpath(path(QStringLiteral("doomed")));
    QVERIFY(QFile::link(path(QStringLiteral("keep")), path(QStringLiteral("doomed/link"))));

    DeleteJob job(DeleteJob::Mode::Permanent, {path(QStringLiteral("doomed"))});
    runJob(job);

    QVERIFY(!QFileInfo::exists(path(QStringLiteral("doomed"))));
    QCOMPARE(read(QStringLiteral("keep/precious.txt")), QByteArray("precious"));
}

void TestJobs::trashInfoRoundTrips()
{
    const QDateTime when(QDate(2026, 8, 12), QTime(14, 2, 3));
    const QString text = Trash::buildTrashInfo(QStringLiteral("/home/andy/notes.txt"), when);

    QVERIFY(text.startsWith(QStringLiteral("[Trash Info]")));

    QString path;
    QDateTime deletedAt;
    QVERIFY(Trash::parseTrashInfo(text, &path, &deletedAt));
    QCOMPARE(path, QStringLiteral("/home/andy/notes.txt"));
    QCOMPARE(deletedAt, when);
}

void TestJobs::trashInfoEncodesOddFilenames()
{
    // §14 asks for this specifically. A filename containing a newline would
    // otherwise produce a .trashinfo that parses as something else entirely,
    // and one containing a '%' would be mangled on the way back.
    const QStringList awkward{
        QStringLiteral("/tmp/with space.txt"),   QStringLiteral("/tmp/with#hash.txt"),
        QStringLiteral("/tmp/with%percent.txt"), QStringLiteral("/tmp/with\nnewline.txt"),
        QStringLiteral("/tmp/naïve.txt"),        QStringLiteral("/tmp/emoji-🙂.txt"),
        QStringLiteral("/tmp/with=equals.txt"),  QStringLiteral("/tmp/with[bracket].txt")};

    for (const QString &original : awkward) {
        const QString text = Trash::buildTrashInfo(original, QDateTime::currentDateTime());

        // The encoded form must be a single line, or the parse below finds only
        // part of the path.
        QVERIFY2(!text.mid(text.indexOf(QStringLiteral("Path=")) + 5)
                      .section(QLatin1Char('\n'), 0, 0)
                      .contains(QLatin1Char('\n')),
                 qPrintable(original));

        QString parsed;
        QVERIFY2(Trash::parseTrashInfo(text, &parsed, nullptr), qPrintable(original));
        QCOMPARE(parsed, original);
    }
}

void TestJobs::trashInfoRejectsGarbage()
{
    QVERIFY(!Trash::parseTrashInfo(QStringLiteral("not a trashinfo"), nullptr, nullptr));
    // A file with the header but no Path can be listed and never restored,
    // which is worse than not listing it.
    QVERIFY(!Trash::parseTrashInfo(QStringLiteral("[Trash Info]\nDeletionDate=2026-01-01T00:00:00"),
                                   nullptr, nullptr));
}

void TestJobs::trashMovesAndLists()
{
    write(QStringLiteral("a.txt"), "content");
    Trash trash(path(QStringLiteral("Trash")));

    QString error;
    const QString destination = trash.moveToTrash(path(QStringLiteral("a.txt")), &error);

    QVERIFY2(!destination.isEmpty(), qPrintable(error));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("a.txt"))));

    const QList<TrashedItem> items = trash.list();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().originalPath,
             QFileInfo(path(QStringLiteral("a.txt"))).absoluteFilePath());
}

void TestJobs::trashSuffixesNameCollisions()
{
    // §7.5: "On name collision, append -1, -2, … to the trashed name."
    Trash trash(path(QStringLiteral("Trash")));

    for (int i = 0; i < 3; ++i) {
        write(QStringLiteral("a.txt"), QByteArray::number(i));
        QVERIFY(!trash.moveToTrash(path(QStringLiteral("a.txt"))).isEmpty());
    }

    QCOMPARE(trash.list().size(), 3);
    QVERIFY(QFileInfo::exists(path(QStringLiteral("Trash/files/a.txt"))));
    QVERIFY(QFileInfo::exists(path(QStringLiteral("Trash/files/a-1.txt"))));
    QVERIFY(QFileInfo::exists(path(QStringLiteral("Trash/files/a-2.txt"))));
}

void TestJobs::trashRestoresToTheOriginalPath()
{
    write(QStringLiteral("nested/a.txt"), "content");
    Trash trash(path(QStringLiteral("Trash")));

    QVERIFY(!trash.moveToTrash(path(QStringLiteral("nested/a.txt"))).isEmpty());
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("nested/a.txt"))));

    const QList<TrashedItem> items = trash.list();
    QCOMPARE(items.size(), 1);

    QString error;
    QCOMPARE(trash.restore(items.first(), &error),
             QFileInfo(path(QStringLiteral("nested/a.txt"))).absoluteFilePath());
    QCOMPARE(read(QStringLiteral("nested/a.txt")), QByteArray("content"));
    // The info file goes with it, so the item is no longer listed.
    QVERIFY(trash.list().isEmpty());
}

void TestJobs::trashRefusesToRestoreOverAnExistingFile()
{
    // Restoring is a safety net. A safety net that destroys the file currently
    // occupying the name is not one.
    write(QStringLiteral("a.txt"), "original");
    Trash trash(path(QStringLiteral("Trash")));
    QVERIFY(!trash.moveToTrash(path(QStringLiteral("a.txt"))).isEmpty());

    write(QStringLiteral("a.txt"), "replacement");

    QString error;
    QVERIFY(trash.restore(trash.list().first(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(read(QStringLiteral("a.txt")), QByteArray("replacement"));
}

void TestJobs::trashEmptyRemovesEverything()
{
    Trash trash(path(QStringLiteral("Trash")));
    for (int i = 0; i < 4; ++i) {
        write(QStringLiteral("f%1.txt").arg(i), "x");
        QVERIFY(!trash.moveToTrash(path(QStringLiteral("f%1.txt").arg(i))).isEmpty());
    }

    QCOMPARE(trash.empty(), 4);
    QVERIFY(trash.list().isEmpty());
}

void TestJobs::undoRestoresATrashedFile()
{
    // §7.13: "Undo of a trash operation restores from trash by .trashinfo path."
    write(QStringLiteral("a.txt"), "content");
    Trash trash(path(QStringLiteral("Trash")));

    DeleteJob job(DeleteJob::Mode::Trash, {path(QStringLiteral("a.txt"))});
    job.setTrash(trash);
    QVERIFY(runJob(job).succeeded());
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("a.txt"))));

    UndoStack stack;
    stack.setTrash(trash);
    stack.push(UndoEntry{.kind = UndoEntry::Kind::Trash,
                         .description = QStringLiteral("Move to trash"),
                         .movedPairs = {},
                         .trashedItems = job.trashedItems()});

    QVERIFY(stack.canUndo());
    QString error;
    QVERIFY2(stack.undo(&error), qPrintable(error));
    QCOMPARE(read(QStringLiteral("a.txt")), QByteArray("content"));
    QVERIFY(!stack.canUndo());
}

void TestJobs::undoIsBounded()
{
    // §7.13: fifty entries. Without the bound a long session accumulates one
    // per operation forever.
    UndoStack stack;
    for (int i = 0; i < UndoStack::kCapacity + 20; ++i) {
        stack.push(UndoEntry{.kind = UndoEntry::Kind::Rename,
                             .description = QStringLiteral("Rename %1").arg(i),
                             .movedPairs = {},
                             .trashedItems = {}});
    }

    QCOMPARE(stack.size(), UndoStack::kCapacity);
    // The newest survives; the oldest were dropped.
    QCOMPARE(stack.nextDescription(), QStringLiteral("Rename %1").arg(UndoStack::kCapacity + 19));
}

void TestJobs::undoRefusesWhenTheNameIsTaken()
{
    write(QStringLiteral("new.txt"), "renamed");
    write(QStringLiteral("old.txt"), "something else entirely");

    UndoStack stack;
    stack.push(UndoEntry{
        .kind = UndoEntry::Kind::Rename,
        .description = QStringLiteral("Rename"),
        .movedPairs = {{path(QStringLiteral("old.txt")), path(QStringLiteral("new.txt"))}},
        .trashedItems = {}});

    QString error;
    QVERIFY(!stack.undo(&error));
    QVERIFY2(error.contains(QStringLiteral("already exists")), qPrintable(error));
    // Neither file was touched: undo must not destroy a file created after the
    // operation it is reversing.
    QCOMPARE(read(QStringLiteral("old.txt")), QByteArray("something else entirely"));
    QCOMPARE(read(QStringLiteral("new.txt")), QByteArray("renamed"));
}

QTEST_MAIN(TestJobs)
#include "tst_jobs.moc"
