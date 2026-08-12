#pragma once

class QCollator;
class QString;

namespace pf {

/// Compares two names the way a person reads them: file2 before file10.
///
/// Returns a negative value, zero, or a positive value, like strcmp.
///
/// §4.4 asks for natural, locale-aware, case-insensitive name sorting and
/// points at QCollator with setNumericMode(true). That is the right tool for
/// the locale-aware half and cannot be relied on for the numeric half:
/// QCollator's numeric mode is implemented by ICU, and where Qt is built
/// without ICU — or simply running under LC_ALL=C, which is the default in a
/// container and in most CI — setNumericMode() is accepted and then quietly
/// ignored. The failure is silent and total: every listing reverts to byte
/// order, and img10.png sorts above img2.png with nothing to indicate why.
///
/// So the digit runs are compared here, numerically, and everything between
/// them is handed to the collator. Numeric ordering then holds everywhere,
/// while collation still follows the user's locale wherever ICU is available.
///
/// The comparison is a pure function of its inputs, which is what makes it a
/// valid strict weak ordering for std::sort and testable without a model.
int naturalCompare(const QString &left, const QString &right, const QCollator &collator);

} // namespace pf
