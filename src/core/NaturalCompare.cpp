#include "core/NaturalCompare.h"

#include <QCollator>
#include <QString>

namespace pf {
namespace {

bool isDigit(QChar character)
{
    // Deliberately ASCII digits only. QChar::isDigit() is true for Devanagari
    // and Arabic-Indic digits too, and treating those as a number here would
    // mean comparing digit *values* across scripts — an ordering no user asked
    // for, and one that would disagree with the collator about the same text.
    return character >= QLatin1Char('0') && character <= QLatin1Char('9');
}

/// Compares two runs of digits by value.
///
/// Neither run is converted to an integer: a filename may contain a hundred
/// digits, and a checksum in a name is not a number anyone wants truncated or
/// overflowed. Comparing by significant length and then lexically gives the
/// same answer for any width.
int compareDigitRuns(QStringView left, QStringView right)
{
    const auto significant = [](QStringView run) {
        qsizetype index = 0;
        while (index + 1 < run.size() && run[index] == QLatin1Char('0')) {
            ++index;
        }
        return run.sliced(index);
    };

    const QStringView leftDigits = significant(left);
    const QStringView rightDigits = significant(right);

    if (leftDigits.size() != rightDigits.size()) {
        return leftDigits.size() < rightDigits.size() ? -1 : 1;
    }
    if (const int lexical = leftDigits.compare(rightDigits); lexical != 0) {
        return lexical < 0 ? -1 : 1;
    }

    // Equal in value. Order by how they were written, so that img1 and img001
    // have a stable relative order rather than one that depends on which the
    // sort happened to compare first.
    if (left.size() != right.size()) {
        return left.size() < right.size() ? -1 : 1;
    }
    return 0;
}

} // namespace

int naturalCompare(const QString &left, const QString &right, const QCollator &collator)
{
    qsizetype leftIndex = 0;
    qsizetype rightIndex = 0;

    while (leftIndex < left.size() && rightIndex < right.size()) {
        const bool leftDigit = isDigit(left[leftIndex]);
        const bool rightDigit = isDigit(right[rightIndex]);

        if (leftDigit && rightDigit) {
            const qsizetype leftStart = leftIndex;
            const qsizetype rightStart = rightIndex;
            while (leftIndex < left.size() && isDigit(left[leftIndex])) {
                ++leftIndex;
            }
            while (rightIndex < right.size() && isDigit(right[rightIndex])) {
                ++rightIndex;
            }

            const int result =
                compareDigitRuns(QStringView(left).sliced(leftStart, leftIndex - leftStart),
                                 QStringView(right).sliced(rightStart, rightIndex - rightStart));
            if (result != 0) {
                return result;
            }
            continue;
        }

        // A maximal non-digit run from each side, compared by the collator so
        // that accents, case folding and locale-specific ordering are its
        // problem rather than ours. Comparing character by character instead
        // would break collation for anything outside ASCII.
        const qsizetype leftStart = leftIndex;
        const qsizetype rightStart = rightIndex;
        while (leftIndex < left.size() && !isDigit(left[leftIndex])) {
            ++leftIndex;
        }
        while (rightIndex < right.size() && !isDigit(right[rightIndex])) {
            ++rightIndex;
        }

        const QString leftRun = left.sliced(leftStart, leftIndex - leftStart);
        const QString rightRun = right.sliced(rightStart, rightIndex - rightStart);

        if (const int result = collator.compare(leftRun, rightRun); result != 0) {
            return result;
        }

        // The runs collate equal but may not be identical — "a" and "A" under a
        // case-insensitive collator. If the whole comparison ends in a tie, the
        // tie-break at the bottom settles it.
    }

    if (leftIndex < left.size()) {
        return 1;
    }
    if (rightIndex < right.size()) {
        return -1;
    }

    // Everything collated equal. Fall back to an exact comparison so that names
    // differing only in case still have a deterministic order — without it,
    // "README" and "readme" would compare equal and their order would depend on
    // the sort algorithm rather than on anything a user could predict.
    return QString::compare(left, right, Qt::CaseSensitive);
}

} // namespace pf
