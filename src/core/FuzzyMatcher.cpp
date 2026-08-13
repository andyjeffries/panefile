#include "core/FuzzyMatcher.h"

#include <algorithm>
#include <utility>

namespace pf {
namespace {

/// Characters that start a new word for scoring purposes.
bool isWordSeparator(QChar character)
{
    return character == QLatin1Char(' ') || character == QLatin1Char('_') ||
           character == QLatin1Char('-') || character == QLatin1Char('.');
}

bool isPathSeparator(QChar character)
{
    return character == QLatin1Char('/') || character == QLatin1Char('\\');
}

/// The bonus a match at `index` earns from its position alone.
int positionBonus(const QString &candidate, int index)
{
    if (index == 0) {
        return FuzzyMatcher::kFirstCharacterBonus;
    }

    const QChar previous = candidate.at(index - 1);
    const QChar current = candidate.at(index);

    if (isPathSeparator(previous)) {
        return FuzzyMatcher::kSeparatorBonus;
    }
    if (isWordSeparator(previous)) {
        return FuzzyMatcher::kWordBoundaryBonus;
    }
    // §7.8's camelCase hump. Uses the previous character's case rather than a
    // dictionary, so it works on any script that has case at all — and simply
    // never fires on one that does not, which is the right failure.
    if (current.isUpper() && previous.isLower()) {
        return FuzzyMatcher::kCamelBonus;
    }
    return 0;
}

/// Appends `index` to `spans`, extending the last run when it is contiguous.
void appendMatch(QList<MatchSpan> &spans, int index)
{
    if (!spans.isEmpty() && spans.last().start + spans.last().length == index) {
        spans.last().length += 1;
        return;
    }
    spans.append(MatchSpan{.start = index, .length = 1});
}

/// Scores one complete assignment of query characters to candidate positions.
int scoreOf(const QString &candidate, const QList<int> &positions)
{
    int score = 0;
    int previous = -2;

    for (const int index : positions) {
        score += FuzzyMatcher::kMatchScore;
        score += positionBonus(candidate, index);

        if (index == previous + 1) {
            score += FuzzyMatcher::kConsecutiveBonus;
        } else if (previous >= 0) {
            const int gap = index - previous - 1;
            score -= std::min(gap * FuzzyMatcher::kGapPenalty, FuzzyMatcher::kMaxGapPenalty);
        }

        previous = index;
    }

    return score;
}

/// The greedy assignment: take the earliest position for each query character.
/// Returns an empty list when the query is not a subsequence at all.
QList<int> earliestPositions(const QString &query, const QString &candidate)
{
    QList<int> positions;
    positions.reserve(query.size());

    int at = 0;
    for (const QChar wanted : query) {
        const int found = candidate.indexOf(wanted, at, Qt::CaseInsensitive);
        if (found < 0) {
            return {};
        }
        positions.append(found);
        at = found + 1;
    }
    return positions;
}

/// The greedy assignment run backwards from the end, which finds the *tightest*
/// placement rather than the earliest.
///
/// This is the whole reason two passes exist. Matching "src" against
/// "source/src" earliest-first spreads the match across "s-o-u-r-c-e", while
/// the backward pass lands it on the contiguous "src" and scores far higher —
/// which is the result a user means every time.
QList<int> tightestPositions(const QString &query, const QString &candidate)
{
    QList<int> positions;
    positions.resize(query.size());

    int at = candidate.size() - 1;
    for (int i = static_cast<int>(query.size()) - 1; i >= 0; --i) {
        const QChar wanted = query.at(i);
        int found = -1;
        for (int j = at; j >= 0; --j) {
            if (candidate.at(j).toCaseFolded() == wanted.toCaseFolded()) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            return {};
        }
        positions[i] = found;
        at = found - 1;
    }
    return positions;
}

FuzzyMatch resultFor(const QString &candidate, const QList<int> &positions)
{
    FuzzyMatch result;
    result.matched = true;
    result.score = scoreOf(candidate, positions);
    for (const int index : positions) {
        appendMatch(result.spans, index);
    }
    return result;
}

} // namespace

bool FuzzyMatcher::isSubsequence(const QString &query, const QString &candidate)
{
    if (query.isEmpty()) {
        return true;
    }

    qsizetype at = 0;
    for (const QChar wanted : query) {
        at = candidate.indexOf(wanted, at, Qt::CaseInsensitive);
        if (at < 0) {
            return false;
        }
        ++at;
    }
    return true;
}

FuzzyMatch FuzzyMatcher::match(const QString &query, const QString &candidate)
{
    FuzzyMatch result;

    // An empty query matches everything: an empty filter box shows the whole
    // directory rather than nothing.
    if (query.isEmpty()) {
        result.matched = true;
        return result;
    }

    if (candidate.isEmpty()) {
        return result;
    }

    // §7.8's fast reject, before any scoring work.
    const QList<int> earliest = earliestPositions(query, candidate);
    if (earliest.isEmpty()) {
        return result;
    }

    const QList<int> tightest = tightestPositions(query, candidate);

    FuzzyMatch forward = resultFor(candidate, earliest);
    if (tightest.isEmpty()) {
        return forward;
    }

    FuzzyMatch backward = resultFor(candidate, tightest);
    return backward.score > forward.score ? std::move(backward) : std::move(forward);
}

FuzzyMatch FuzzyMatcher::matchSubstring(const QString &query, const QString &candidate)
{
    FuzzyMatch result;

    if (query.isEmpty()) {
        result.matched = true;
        return result;
    }

    const qsizetype at = candidate.indexOf(query, 0, Qt::CaseInsensitive);
    if (at < 0) {
        return result;
    }

    result.matched = true;
    result.spans.append(
        MatchSpan{.start = static_cast<int>(at), .length = static_cast<int>(query.size())});

    // Scored on the same scale as the fuzzy path, so a caller can sort a mixed
    // set without knowing which produced which: an earlier, word-boundary match
    // still ranks above a later one buried mid-word.
    result.score = (static_cast<int>(query.size()) * kMatchScore) +
                   positionBonus(candidate, static_cast<int>(at)) +
                   ((static_cast<int>(query.size()) - 1) * kConsecutiveBonus);
    return result;
}

} // namespace pf
