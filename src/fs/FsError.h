#pragma once

#include <QString>

namespace pf::fs {

/// Translates an errno value into text a user can act on (§12).
///
/// strerror() is the fallback, not the first choice: "Permission denied" is
/// what the user needs to read, and for the handful of errors that have an
/// obvious remedy the message says what it is. Filesystem errors are the most
/// common thing to surface in this application, and a bare errno number in the
/// footer helps nobody.
QString describeErrno(int error);

/// A full sentence for a failed operation on a path, of the form
/// "Cannot open /etc/shadow: Permission denied".
QString describeErrno(int error, const QString &path, const QString &operation);

} // namespace pf::fs
