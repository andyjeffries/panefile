#include "core/FuzzyMatcher.h"

#include <QTest>

using namespace pf;

/// §7.8's matcher. §14: "FuzzyMatcher: scoring order, span correctness,
/// empty/pathological inputs, Unicode."
class TestFuzzyMatcher : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ------------------------------------------------------------- matching

    void emptyQueryMatchesEverything()
    {
        const FuzzyMatch result = FuzzyMatcher::match(QString(), QStringLiteral("anything"));
        QVERIFY(result.matched);
        QVERIFY(result.spans.isEmpty());
        QCOMPARE(result.score, 0);
    }

    void emptyCandidateMatchesNothing()
    {
        QVERIFY(!FuzzyMatcher::match(QStringLiteral("a"), QString()).matched);
    }

    void bothEmptyMatches() { QVERIFY(FuzzyMatcher::match(QString(), QString()).matched); }

    void matchesASubsequence()
    {
        QVERIFY(FuzzyMatcher::match(QStringLiteral("fb"), QStringLiteral("foobar")).matched);
        QVERIFY(!FuzzyMatcher::match(QStringLiteral("bf"), QStringLiteral("foobar")).matched);
    }

    void isCaseInsensitive()
    {
        QVERIFY(FuzzyMatcher::match(QStringLiteral("FB"), QStringLiteral("foobar")).matched);
        QVERIFY(FuzzyMatcher::match(QStringLiteral("fb"), QStringLiteral("FooBar")).matched);
    }

    void queryLongerThanCandidateCannotMatch()
    {
        QVERIFY(!FuzzyMatcher::match(QStringLiteral("abcdef"), QStringLiteral("abc")).matched);
    }

    // ---------------------------------------------------------------- spans

    /// The spans are what the delegate highlights, so a wrong one is visible on
    /// every row.
    void spansCoverExactlyTheMatchedCharacters()
    {
        const FuzzyMatch result =
            FuzzyMatcher::match(QStringLiteral("src"), QStringLiteral("mysrcdir"));

        QCOMPARE(result.spans.size(), 1);
        QCOMPARE(result.spans.first(), (MatchSpan{.start = 2, .length = 3}));
    }

    void adjacentMatchesCollapseIntoOneSpan()
    {
        const FuzzyMatch result =
            FuzzyMatcher::match(QStringLiteral("abc"), QStringLiteral("abcdef"));

        QCOMPARE(result.spans.size(), 1);
        QCOMPARE(result.spans.first(), (MatchSpan{.start = 0, .length = 3}));
    }

    void separatedMatchesProduceSeparateSpans()
    {
        const FuzzyMatch result =
            FuzzyMatcher::match(QStringLiteral("ad"), QStringLiteral("abcdef"));

        QCOMPARE(result.spans.size(), 2);
        QCOMPARE(result.spans.at(0), (MatchSpan{.start = 0, .length = 1}));
        QCOMPARE(result.spans.at(1), (MatchSpan{.start = 3, .length = 1}));
    }

    // -------------------------------------------------------------- scoring

    /// The ordering is the entire user-visible behaviour of a fuzzy finder.
    void contiguousBeatsScattered()
    {
        const int contiguous =
            FuzzyMatcher::match(QStringLiteral("abc"), QStringLiteral("abcxxxxx")).score;
        const int scattered =
            FuzzyMatcher::match(QStringLiteral("abc"), QStringLiteral("axbxcxxx")).score;

        QVERIFY2(contiguous > scattered,
                 qPrintable(QStringLiteral("%1 vs %2").arg(contiguous).arg(scattered)));
    }

    /// §7.8: "matches at word boundaries and camelCase humps".
    void wordBoundaryBeatsMidWord()
    {
        const int boundary =
            FuzzyMatcher::match(QStringLiteral("f"), QStringLiteral("a_foo")).score;
        const int midWord = FuzzyMatcher::match(QStringLiteral("f"), QStringLiteral("aafoo")).score;

        QVERIFY(boundary > midWord);
    }

    void camelHumpBeatsMidWord()
    {
        const int hump = FuzzyMatcher::match(QStringLiteral("b"), QStringLiteral("fooBar")).score;
        const int flat = FuzzyMatcher::match(QStringLiteral("b"), QStringLiteral("foobar")).score;

        QVERIFY(hump > flat);
    }

    /// §7.8: "matches after path separators".
    void afterSeparatorBeatsMidComponent()
    {
        const int afterSlash =
            FuzzyMatcher::match(QStringLiteral("m"), QStringLiteral("src/main")).score;
        const int midComponent =
            FuzzyMatcher::match(QStringLiteral("m"), QStringLiteral("srcxmain")).score;

        QVERIFY(afterSlash > midComponent);
    }

    /// The backward pass exists for exactly this case: the tight match at the
    /// end must win over the sprawling one at the start.
    void findsTheTightMatchNotTheFirstOne()
    {
        const FuzzyMatch result =
            FuzzyMatcher::match(QStringLiteral("src"), QStringLiteral("source/src"));

        QCOMPARE(result.spans.size(), 1);
        QCOMPARE(result.spans.first(), (MatchSpan{.start = 7, .length = 3}));
    }

    void shorterGapsScoreHigher()
    {
        const int near = FuzzyMatcher::match(QStringLiteral("ab"), QStringLiteral("axb")).score;
        const int far =
            FuzzyMatcher::match(QStringLiteral("ab"), QStringLiteral("axxxxxxxxb")).score;

        QVERIFY(near > far);
    }

    // -------------------------------------------------------------- Unicode

    /// Matching must work on characters, not bytes: "é" is two UTF-8 bytes and
    /// one QChar, and a byte-oriented matcher would split it.
    void handlesAccentedCharacters()
    {
        const FuzzyMatch result =
            FuzzyMatcher::match(QStringLiteral("café"), QStringLiteral("mon café noir"));

        QVERIFY(result.matched);
        QCOMPARE(result.spans.size(), 1);
        QCOMPARE(result.spans.first(), (MatchSpan{.start = 4, .length = 4}));
    }

    void accentedMatchingIsCaseInsensitive()
    {
        QVERIFY(FuzzyMatcher::match(QStringLiteral("É"), QStringLiteral("café")).matched);
    }

    void handlesNonLatinScripts()
    {
        const FuzzyMatch result =
            FuzzyMatcher::match(QStringLiteral("日本"), QStringLiteral("日本語のファイル"));

        QVERIFY(result.matched);
        QCOMPARE(result.spans.first(), (MatchSpan{.start = 0, .length = 2}));
    }

    // -------------------------------------------------------- pathological

    void veryLongCandidateDoesNotMisbehave()
    {
        const QString candidate = QString(10000, QLatin1Char('x')) + QStringLiteral("needle");
        const FuzzyMatch result = FuzzyMatcher::match(QStringLiteral("needle"), candidate);

        QVERIFY(result.matched);
        QCOMPARE(result.spans.size(), 1);
        QCOMPARE(result.spans.first().length, 6);
    }

    void repeatedCharactersDoNotDoubleCount()
    {
        const FuzzyMatch result =
            FuzzyMatcher::match(QStringLiteral("aaa"), QStringLiteral("aaaa"));

        QVERIFY(result.matched);
        QCOMPARE(result.spans.size(), 1);
        QCOMPARE(result.spans.first().length, 3);
    }

    // ------------------------------------------------------------ substring

    /// §7.8's `search.fuzzy = false` path.
    void substringModeRejectsSubsequences()
    {
        QVERIFY(
            FuzzyMatcher::matchSubstring(QStringLiteral("oob"), QStringLiteral("foobar")).matched);
        QVERIFY(
            !FuzzyMatcher::matchSubstring(QStringLiteral("fb"), QStringLiteral("foobar")).matched);
    }

    void substringModeReturnsOneSpan()
    {
        const FuzzyMatch result =
            FuzzyMatcher::matchSubstring(QStringLiteral("oba"), QStringLiteral("foobar"));

        QCOMPARE(result.spans.size(), 1);
        QCOMPARE(result.spans.first(), (MatchSpan{.start = 2, .length = 3}));
    }

    void substringModeEmptyQueryMatches()
    {
        QVERIFY(FuzzyMatcher::matchSubstring(QString(), QStringLiteral("anything")).matched);
    }

    // -------------------------------------------------------- fast reject

    void subsequenceTestAgreesWithTheMatcher()
    {
        const QStringList queries{QStringLiteral("fb"), QStringLiteral("bf"), QStringLiteral(""),
                                  QStringLiteral("foobar"), QStringLiteral("foobarbaz")};

        for (const QString &query : queries) {
            const bool cheap = FuzzyMatcher::isSubsequence(query, QStringLiteral("foobar"));
            const bool full = FuzzyMatcher::match(query, QStringLiteral("foobar")).matched;
            QCOMPARE(cheap, full);
        }
    }
};

QTEST_APPLESS_MAIN(TestFuzzyMatcher)
#include "tst_fuzzymatcher.moc"
