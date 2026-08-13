#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

namespace pf {

/// One run of matched characters, for highlighting (§5.3's MatchSpansRole).
struct MatchSpan {
    int start = 0;
    int length = 0;

    bool operator==(const MatchSpan &other) const = default;
};

/// The outcome of matching a query against a candidate (§7.8).
struct FuzzyMatch {
    bool matched = false;
    int score = 0;
    QList<MatchSpan> spans;

    explicit operator bool() const { return matched; }
};

/// An fzf-style fuzzy matcher (§7.8).
///
/// §7.8: "a case-insensitive subsequence test as a fast reject, then a scoring
/// pass rewarding consecutive matches, matches at word boundaries and camelCase
/// humps, matches after path separators, and penalising gap length. Return both
/// the score and the matched character spans so the delegate can highlight
/// them. This must be a pure, header-testable function."
///
/// Pure it is: no state, no allocation beyond the result, and nothing here
/// knows what a file is. §14 tests it directly on strings.
///
/// The scoring is greedy-with-backtracking rather than a full dynamic program.
/// fzf's own optimal algorithm is O(n·m) in both time and memory; for filenames
/// — tens of characters against a query of a handful — the greedy pass finds
/// the same answer in the cases that matter, and the tie-breaking rules below
/// are what a user actually perceives as "the right one first".
class FuzzyMatcher
{
public:
    // The weights. Named rather than inlined because their *relative* sizes are
    // the whole behaviour, and they are meaningless individually.

    /// Every matched character earns this before any bonus.
    static constexpr int kMatchScore = 16;

    /// A match immediately after the previous one. The largest bonus, because
    /// a contiguous run is the strongest signal that the user meant this.
    static constexpr int kConsecutiveBonus = 8;

    /// A match at the start of a word — after a space, `_`, `-` or `.`.
    static constexpr int kWordBoundaryBonus = 8;

    /// A match on a camelCase hump: a capital preceded by a lower-case letter.
    static constexpr int kCamelBonus = 7;

    /// A match immediately after a path separator.
    static constexpr int kSeparatorBonus = 9;

    /// A match on the very first character.
    static constexpr int kFirstCharacterBonus = 10;

    /// Subtracted per character skipped. Small, so that a later but
    /// better-placed match can still beat an early sloppy one.
    static constexpr int kGapPenalty = 3;

    /// The most a gap may cost, so one long skip in a long path does not swamp
    /// every bonus that follows it.
    static constexpr int kMaxGapPenalty = 20;

    /// Matches `query` against `candidate`, case-insensitively.
    ///
    /// An empty query matches everything with a score of zero and no spans:
    /// that is what makes an empty filter box show the whole directory rather
    /// than nothing.
    static FuzzyMatch match(const QString &query, const QString &candidate);

    /// The fast reject on its own (§7.8's "case-insensitive subsequence test").
    /// Exposed because a caller filtering a hundred thousand rows wants to ask
    /// the cheap question without paying for the scoring pass.
    static bool isSubsequence(const QString &query, const QString &candidate);

    /// Plain case-insensitive substring matching, for
    /// `config.search.fuzzy = false` (§7.8). Returns the single span so the
    /// delegate highlights the same way either way.
    static FuzzyMatch matchSubstring(const QString &query, const QString &candidate);
};

} // namespace pf

// The delegate receives the spans through the model's MatchSpansRole (§4.2),
// which means through a QVariant.
Q_DECLARE_METATYPE(pf::MatchSpan)
