#include "fs/RenamePlan.h"
#include "fs/RenameRule.h"
#include "fs/jobs/RenameJob.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::fs;

namespace {

RenamePair pair(const char *from, const char *to)
{
    return RenamePair{.from = QString::fromLatin1(from), .to = QString::fromLatin1(to)};
}

QList<QString> names(std::initializer_list<const char *> list)
{
    QList<QString> result;
    for (const char *name : list) {
        result.append(QString::fromLatin1(name));
    }
    return result;
}

void touch(const QTemporaryDir &dir, const QString &name)
{
    QFile file(dir.filePath(name));
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write(name.toUtf8());
    file.close();
}

} // namespace

/// §7.9's bulk rename: the naming rules, the planner and the job.
class TestRename : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ============================================================== the rules

    void replacesText()
    {
        RenameRule rule;
        rule.mode = RenameMode::ReplaceText;
        rule.find = QStringLiteral("IMG");
        rule.replaceWith = QStringLiteral("Holiday");

        QCOMPARE(rule.apply(QStringLiteral("IMG_0001.jpg"), 0), QStringLiteral("Holiday_0001.jpg"));
    }

    void replacesEveryOccurrence()
    {
        RenameRule rule;
        rule.mode = RenameMode::ReplaceText;
        rule.find = QStringLiteral("a");
        rule.replaceWith = QStringLiteral("b");

        QCOMPARE(rule.apply(QStringLiteral("banana"), 0), QStringLiteral("bbnbnb"));
    }

    /// An empty Find box must be a no-op, not an insertion between every
    /// character — which is what a naive replace() does.
    void emptyFindChangesNothing()
    {
        RenameRule rule;
        rule.mode = RenameMode::ReplaceText;
        rule.replaceWith = QStringLiteral("X");

        QCOMPARE(rule.apply(QStringLiteral("photo.jpg"), 0), QStringLiteral("photo.jpg"));
    }

    void replaceIsCaseInsensitiveByDefault()
    {
        RenameRule rule;
        rule.mode = RenameMode::ReplaceText;
        rule.find = QStringLiteral("img");
        rule.replaceWith = QStringLiteral("photo");

        QCOMPARE(rule.apply(QStringLiteral("IMG_1.jpg"), 0), QStringLiteral("photo_1.jpg"));

        rule.caseSensitive = true;
        QCOMPARE(rule.apply(QStringLiteral("IMG_1.jpg"), 0), QStringLiteral("IMG_1.jpg"));
    }

    void addsTextBeforeAndAfter()
    {
        RenameRule rule;
        rule.mode = RenameMode::AddText;
        rule.addText = QStringLiteral("2026-");
        rule.addPosition = AddPosition::Before;
        QCOMPARE(rule.apply(QStringLiteral("notes.md"), 0), QStringLiteral("2026-notes.md"));

        rule.addText = QStringLiteral(" (draft)");
        rule.addPosition = AddPosition::After;
        QCOMPARE(rule.apply(QStringLiteral("notes.md"), 0), QStringLiteral("notes.md (draft)"));
    }

    void formatNumbersFromTheStartingNumber()
    {
        RenameRule rule;
        rule.mode = RenameMode::Format;
        rule.nameFormat = NameFormat::NameAndIndex;
        rule.customText = QStringLiteral("Holiday");
        rule.startNumber = 5;

        const QList<QString> result = rule.applyAll(names({"a.jpg", "b.jpg", "c.jpg"}));

        QCOMPARE(result, names({"Holiday 5", "Holiday 6", "Holiday 7"}));
    }

    void formatCounterIsZeroPadded()
    {
        RenameRule rule;
        rule.mode = RenameMode::Format;
        rule.nameFormat = NameFormat::NameAndCounter;
        rule.customText = QStringLiteral("Scan");
        rule.startNumber = 1;

        QCOMPARE(rule.apply(QStringLiteral("x"), 0), QStringLiteral("Scan 00001"));
        QCOMPARE(rule.apply(QStringLiteral("x"), 41), QStringLiteral("Scan 00042"));
    }

    void formatCanPutTheNumberFirst()
    {
        RenameRule rule;
        rule.mode = RenameMode::Format;
        rule.nameFormat = NameFormat::NameAndIndex;
        rule.formatPosition = FormatPosition::BeforeName;
        rule.customText = QStringLiteral("Track");

        QCOMPARE(rule.apply(QStringLiteral("x"), 0), QStringLiteral("1 Track"));
    }

    /// Empty custom text numbers the originals rather than producing a
    /// directory of files called "1", "2", "3", which would be unrecoverable.
    void formatWithoutCustomTextKeepsTheOriginalName()
    {
        RenameRule rule;
        rule.mode = RenameMode::Format;
        rule.nameFormat = NameFormat::NameAndIndex;

        QCOMPARE(rule.apply(QStringLiteral("photo.jpg"), 0), QStringLiteral("photo.jpg 1"));
    }

    // ============================================================ the planner

    void unchangedNamesProduceNoWork()
    {
        const RenamePlan plan =
            RenamePlanner::plan({pair("a", "a"), pair("b", "b")}, names({"a", "b"}));

        QVERIFY(plan.isValid());
        QVERIFY(plan.steps.isEmpty());
        QVERIFY(plan.changes.isEmpty());
    }

    void straightforwardRenamesKeepTheirOrder()
    {
        const RenamePlan plan =
            RenamePlanner::plan({pair("a", "x"), pair("b", "y")}, names({"a", "b"}));

        QVERIFY(plan.isValid());
        QCOMPARE(plan.steps.size(), 2);
        QCOMPARE(plan.steps.at(0).from, QStringLiteral("a"));
        QCOMPARE(plan.steps.at(0).to, QStringLiteral("x"));
    }

    /// A chain must run back to front, or the second rename lands on a name the
    /// first has not vacated yet.
    void chainsAreOrderedBackToFront()
    {
        const RenamePlan plan =
            RenamePlanner::plan({pair("a", "b"), pair("b", "c")}, names({"a", "b"}));

        QVERIFY(plan.isValid());
        QCOMPARE(plan.steps.size(), 2);
        QCOMPARE(plan.steps.at(0).from, QStringLiteral("b"));
        QCOMPARE(plan.steps.at(0).to, QStringLiteral("c"));
        QCOMPARE(plan.steps.at(1).from, QStringLiteral("a"));
        QCOMPARE(plan.steps.at(1).to, QStringLiteral("b"));
    }

    /// §7.9 step 4: "detect cycles (a→b, b→a) and resolve them via temporary
    /// names". A swap is something people do deliberately, and getting it wrong
    /// destroys a file.
    void swapsAreBrokenWithATemporary()
    {
        const RenamePlan plan =
            RenamePlanner::plan({pair("a", "b"), pair("b", "a")}, names({"a", "b"}));

        QVERIFY(plan.isValid());
        QCOMPARE(plan.steps.size(), 3);

        QVERIFY(plan.steps.at(0).viaTemporary);
        QCOMPARE(plan.steps.at(0).from, QStringLiteral("a"));

        const QString temporary = plan.steps.at(0).to;
        QVERIFY(temporary.contains(QStringLiteral(".pf-rename-")));

        // b vacates a's old name, then the temporary takes b's.
        QCOMPARE(plan.steps.at(1).from, QStringLiteral("b"));
        QCOMPARE(plan.steps.at(1).to, QStringLiteral("a"));
        QCOMPARE(plan.steps.at(2).from, temporary);
        QCOMPARE(plan.steps.at(2).to, QStringLiteral("b"));
    }

    void longerCyclesAreAlsoBroken()
    {
        const RenamePlan plan = RenamePlanner::plan(
            {pair("a", "b"), pair("b", "c"), pair("c", "a")}, names({"a", "b", "c"}));

        QVERIFY(plan.isValid());
        QCOMPARE(plan.steps.size(), 4);

        // Whatever the order, every source is vacated before anything is
        // renamed onto it — which is the only property that actually matters.
        const QList<QString> initial = names({"a", "b", "c"});
        QSet<QString> occupied(initial.constBegin(), initial.constEnd());
        for (const RenameStep &step : plan.steps) {
            QVERIFY2(!occupied.contains(step.to), qPrintable(step.to));
            occupied.remove(step.from);
            occupied.insert(step.to);
        }
    }

    void rejectsEmptyNames()
    {
        const RenamePlan plan = RenamePlanner::plan({pair("a", "")}, names({"a"}));

        QVERIFY(!plan.isValid());
        QCOMPARE(plan.problem, RenameProblem::EmptyName);
        QVERIFY(!plan.problemText().isEmpty());
    }

    /// A separator would turn a rename into a move, writing outside the
    /// directory the user was looking at.
    void rejectsPathSeparators()
    {
        const RenamePlan plan = RenamePlanner::plan({pair("a", "../a")}, names({"a"}));

        QVERIFY(!plan.isValid());
        QCOMPARE(plan.problem, RenameProblem::PathSeparator);
    }

    void rejectsTwoItemsTakingTheSameName()
    {
        const RenamePlan plan =
            RenamePlanner::plan({pair("a", "x"), pair("b", "x")}, names({"a", "b"}));

        QVERIFY(!plan.isValid());
        QCOMPARE(plan.problem, RenameProblem::DuplicateTarget);
    }

    void rejectsCollisionWithAnUninvolvedFile()
    {
        const RenamePlan plan = RenamePlanner::plan({pair("a", "c")}, names({"a", "b", "c"}));

        QVERIFY(!plan.isValid());
        QCOMPARE(plan.problem, RenameProblem::CollidesExisting);
        QCOMPARE(plan.offendingName, QStringLiteral("c"));
    }

    /// The same collision is fine when something is vacating the name.
    void allowsCollisionWithAFileThatIsItselfMoving()
    {
        const RenamePlan plan =
            RenamePlanner::plan({pair("a", "c"), pair("c", "d")}, names({"a", "c"}));

        QVERIFY(plan.isValid());
    }

    // ================================================================ the job

    void renamesFilesOnDisk()
    {
        QTemporaryDir dir;
        touch(dir, QStringLiteral("one.txt"));
        touch(dir, QStringLiteral("two.txt"));

        const RenamePlan plan =
            RenamePlanner::plan({pair("one.txt", "first.txt"), pair("two.txt", "second.txt")},
                                names({"one.txt", "two.txt"}));
        QVERIFY(plan.isValid());

        RenameJob job(dir.path(), plan);
        job.run();

        QVERIFY(job.result().succeeded());
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("first.txt"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("second.txt"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("one.txt"))));
    }

    /// The end-to-end swap: the case a naive implementation loses a file on.
    void swapsTwoFilesWithoutLosingEither()
    {
        QTemporaryDir dir;
        touch(dir, QStringLiteral("a"));
        touch(dir, QStringLiteral("b"));

        const RenamePlan plan =
            RenamePlanner::plan({pair("a", "b"), pair("b", "a")}, names({"a", "b"}));

        RenameJob job(dir.path(), plan);
        job.run();

        QVERIFY(job.result().succeeded());

        // The contents prove the swap actually happened rather than the names
        // merely still existing.
        QFile a(dir.filePath(QStringLiteral("a")));
        QVERIFY(a.open(QIODevice::ReadOnly));
        QCOMPARE(a.readAll(), QByteArray("b"));

        QFile b(dir.filePath(QStringLiteral("b")));
        QVERIFY(b.open(QIODevice::ReadOnly));
        QCOMPARE(b.readAll(), QByteArray("a"));

        // No temporary is left behind.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 2);
    }

    void reportsAMissingSourceRatherThanRenamingHalfOfIt()
    {
        QTemporaryDir dir;
        touch(dir, QStringLiteral("present"));

        const RenamePlan plan = RenamePlanner::plan({pair("present", "x"), pair("absent", "y")},
                                                    names({"present", "absent"}));
        QVERIFY(plan.isValid());

        RenameJob job(dir.path(), plan);
        job.run();

        QVERIFY(!job.result().succeeded());
        // Nothing was renamed: the check happens in the enumerate phase, before
        // anything is written.
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("present"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("x"))));
    }

    /// §7.9 step 6, "a single undoable job": the completed renames are what
    /// undo walks backwards.
    void recordsWhatItDidForUndo()
    {
        QTemporaryDir dir;
        touch(dir, QStringLiteral("a"));

        const RenamePlan plan = RenamePlanner::plan({pair("a", "b")}, names({"a"}));

        RenameJob job(dir.path(), plan);
        job.run();

        QCOMPARE(job.completedRenames().size(), 1);
        QCOMPARE(job.completedRenames().first().first, dir.filePath(QStringLiteral("a")));
        QCOMPARE(job.completedRenames().first().second, dir.filePath(QStringLiteral("b")));
        QCOMPARE(job.requestedChanges().size(), 1);
    }
};

QTEST_MAIN(TestRename)
#include "tst_rename.moc"
