#include "fs/RecursiveFinder.h"
#include "model/DirectoryModel.h"
#include "model/FilterSortProxy.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::fs;

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write("x");
    file.close();
}

/// Collects everything a finder produces, however many batches it takes.
QList<FindResult> runSearch(RecursiveFinder &finder, const QString &root, const QString &query)
{
    QList<FindResult> collected;
    QSignalSpy done(&finder, &RecursiveFinder::finished);

    // Dropped before returning, because `collected` is a local: a connection
    // left behind would have the *next* call's batches appended to a list that
    // no longer exists.
    const QMetaObject::Connection connection =
        QObject::connect(&finder, &RecursiveFinder::resultsReady, &finder,
                         [&collected](const QList<FindResult> &batch) { collected += batch; });

    finder.search(root, query);
    [[maybe_unused]] const bool finished = done.wait(10000);
    Q_ASSERT(finished);

    QObject::disconnect(connection);
    return collected;
}

QStringList relativePathsOf(const QList<FindResult> &results)
{
    QStringList paths;
    for (const FindResult &result : results) {
        paths << result.relativePath;
    }
    paths.sort();
    return paths;
}

} // namespace

/// §7.8's two searches.
class TestSearch : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ====================================================== in-panel filter

    void substringFilterNarrowsTheModel()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("alpha.txt")));
        touch(dir.filePath(QStringLiteral("beta.txt")));
        touch(dir.filePath(QStringLiteral("gamma.txt")));

        DirectoryModel model;
        FilterSortProxy proxy;
        proxy.setSourceModel(&model);

        QSignalSpy scanned(&model, &DirectoryModel::scanFinished);
        model.setPath(dir.path());
        QVERIFY(scanned.wait(5000));

        QCOMPARE(proxy.rowCount(), 3);

        proxy.setFilterText(QStringLiteral("eta"));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.index(0, 0).data(DirectoryModel::NameRole).toString(),
                 QStringLiteral("beta.txt"));
    }

    /// Substring mode must reject what fuzzy mode accepts, or the config option
    /// means nothing.
    void fuzzyFilterAcceptsSubsequences()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("FooBarBaz.txt")));
        touch(dir.filePath(QStringLiteral("unrelated.txt")));

        DirectoryModel model;
        FilterSortProxy proxy;
        proxy.setSourceModel(&model);

        QSignalSpy scanned(&model, &DirectoryModel::scanFinished);
        model.setPath(dir.path());
        QVERIFY(scanned.wait(5000));

        proxy.setFuzzyMatching(false);
        proxy.setFilterText(QStringLiteral("fbb"));
        QCOMPARE(proxy.rowCount(), 0);

        proxy.setFuzzyMatching(true);
        QCOMPARE(proxy.rowCount(), 1);
    }

    /// §7.8: the spans the delegate highlights come through the proxy, because
    /// the model has no idea a filter exists.
    void proxyServesMatchSpans()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("readme.md")));

        DirectoryModel model;
        FilterSortProxy proxy;
        proxy.setSourceModel(&model);

        QSignalSpy scanned(&model, &DirectoryModel::scanFinished);
        model.setPath(dir.path());
        QVERIFY(scanned.wait(5000));

        // No filter: no spans, so an unfiltered listing paints nothing extra.
        QVERIFY(!proxy.index(0, 0).data(DirectoryModel::MatchSpansRole).isValid());

        proxy.setFilterText(QStringLiteral("read"));
        const auto spans =
            proxy.index(0, 0).data(DirectoryModel::MatchSpansRole).value<QList<MatchSpan>>();

        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans.first().start, 0);
        QCOMPARE(spans.first().length, 4);
    }

    /// A fuzzy filter ranks by score; an unfiltered listing keeps §4.4's order.
    void fuzzyFilterRanksByScore()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("zzz_src_zzz")));
        touch(dir.filePath(QStringLiteral("src")));

        DirectoryModel model;
        FilterSortProxy proxy;
        proxy.setSourceModel(&model);
        proxy.setFuzzyMatching(true);

        QSignalSpy scanned(&model, &DirectoryModel::scanFinished);
        model.setPath(dir.path());
        QVERIFY(scanned.wait(5000));

        // Alphabetically "src" sorts after "zzz_src_zzz"? No — but the point is
        // that the *score* decides while filtering, so the exact match leads.
        proxy.setFilterText(QStringLiteral("src"));
        QCOMPARE(proxy.rowCount(), 2);
        QCOMPARE(proxy.index(0, 0).data(DirectoryModel::NameRole).toString(),
                 QStringLiteral("src"));
    }

    // ===================================================== recursive finder

    void findsFilesInSubdirectories()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("top.txt")));
        touch(dir.filePath(QStringLiteral("a/middle.txt")));
        touch(dir.filePath(QStringLiteral("a/b/deep.txt")));

        RecursiveFinder finder;
        finder.setRespectGitignore(false);

        const QStringList found =
            relativePathsOf(runSearch(finder, dir.path(), QStringLiteral("txt")));

        QVERIFY(found.contains(QStringLiteral("top.txt")));
        QVERIFY(found.contains(QStringLiteral("a/middle.txt")));
        QVERIFY(found.contains(QStringLiteral("a/b/deep.txt")));
    }

    /// The query is matched against the relative path, so typing a directory
    /// name narrows to its contents.
    void matchesAgainstTheRelativePath()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("keep/one.txt")));
        touch(dir.filePath(QStringLiteral("drop/two.txt")));

        RecursiveFinder finder;
        finder.setRespectGitignore(false);

        const QStringList found =
            relativePathsOf(runSearch(finder, dir.path(), QStringLiteral("keep/one")));

        QVERIFY(found.contains(QStringLiteral("keep/one.txt")));
        QVERIFY(!found.contains(QStringLiteral("drop/two.txt")));
    }

    void emptyQueryListsEverything()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("a.txt")));
        touch(dir.filePath(QStringLiteral("b.txt")));

        RecursiveFinder finder;
        finder.setRespectGitignore(false);

        QCOMPARE(runSearch(finder, dir.path(), QString()).size(), 2);
    }

    /// §7.8: "respect .gitignore if config.search.respect_gitignore is true".
    void respectsGitignore()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("keep.txt")));
        touch(dir.filePath(QStringLiteral("node_modules/junk.txt")));

        QFile ignore(dir.filePath(QStringLiteral(".gitignore")));
        QVERIFY(ignore.open(QIODevice::WriteOnly));
        ignore.write("# comment\nnode_modules/\n");
        ignore.close();

        RecursiveFinder finder;
        finder.setRespectGitignore(true);

        QStringList found = relativePathsOf(runSearch(finder, dir.path(), QStringLiteral("txt")));
        QVERIFY(found.contains(QStringLiteral("keep.txt")));
        QVERIFY(!found.contains(QStringLiteral("node_modules/junk.txt")));

        // And walks it when told not to respect the file.
        finder.setRespectGitignore(false);
        found = relativePathsOf(runSearch(finder, dir.path(), QStringLiteral("txt")));
        QVERIFY(found.contains(QStringLiteral("node_modules/junk.txt")));
    }

    /// .git holds tens of thousands of files nobody searches for by name.
    void neverWalksTheGitDirectory()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral(".git/objects/abc")));
        touch(dir.filePath(QStringLiteral("real.txt")));

        RecursiveFinder finder;
        finder.setRespectGitignore(false);
        finder.setIncludeHidden(true);

        const QStringList found = relativePathsOf(runSearch(finder, dir.path(), QString()));

        for (const QString &path : found) {
            QVERIFY2(!path.startsWith(QStringLiteral(".git")), qPrintable(path));
        }
        QVERIFY(found.contains(QStringLiteral("real.txt")));
    }

    void hiddenFilesAreExcludedUnlessAskedFor()
    {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral(".hidden.txt")));
        touch(dir.filePath(QStringLiteral("visible.txt")));

        RecursiveFinder finder;
        finder.setRespectGitignore(false);

        QStringList found = relativePathsOf(runSearch(finder, dir.path(), QStringLiteral("txt")));
        QVERIFY(!found.contains(QStringLiteral(".hidden.txt")));

        finder.setIncludeHidden(true);
        found = relativePathsOf(runSearch(finder, dir.path(), QStringLiteral("txt")));
        QVERIFY(found.contains(QStringLiteral(".hidden.txt")));
    }

    /// §7.8: "Cap the recursive walk at config.search.max_results". Reporting
    /// that it stopped is what distinguishes "no more matches" from "we gave
    /// up looking".
    void stopsAtTheResultCapAndSaysSo()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 30; ++i) {
            touch(dir.filePath(QStringLiteral("file%1.txt").arg(i)));
        }

        RecursiveFinder finder;
        finder.setRespectGitignore(false);
        finder.setMaxResults(10);

        QSignalSpy done(&finder, &RecursiveFinder::finished);
        finder.search(dir.path(), QStringLiteral("txt"));
        QVERIFY(done.wait(10000));

        QCOMPARE(done.first().at(0).toInt(), 10);
        QCOMPARE(done.first().at(1).toBool(), true);
    }

    /// A cancelled walk must deliver nothing further, or the modal shows the
    /// previous query's results under the new query.
    void cancelStopsDelivery()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 200; ++i) {
            touch(dir.filePath(QStringLiteral("dir%1/file.txt").arg(i)));
        }

        RecursiveFinder finder;
        finder.setRespectGitignore(false);

        QSignalSpy done(&finder, &RecursiveFinder::finished);
        finder.search(dir.path(), QString());
        finder.cancel();

        QVERIFY(!finder.isRunning());
        QTest::qWait(500);
        QCOMPARE(done.count(), 0);
    }
};

QTEST_MAIN(TestSearch)
#include "tst_search.moc"
