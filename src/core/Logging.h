#pragma once

#include <QLoggingCategory>

// §10.5: log to stderr under panefile.* categories. Q_LOGGING_CATEGORY expands
// to a function holding a function-local static, so declaring these costs no
// static initialiser at load time (§3.4).

Q_DECLARE_LOGGING_CATEGORY(pfApp)
Q_DECLARE_LOGGING_CATEGORY(pfConfig)
Q_DECLARE_LOGGING_CATEGORY(pfFs)
Q_DECLARE_LOGGING_CATEGORY(pfJobs)
Q_DECLARE_LOGGING_CATEGORY(pfKeys)
Q_DECLARE_LOGGING_CATEGORY(pfUi)
Q_DECLARE_LOGGING_CATEGORY(pfIpc)
Q_DECLARE_LOGGING_CATEGORY(pfStartup)

namespace pf {

/// Enables panefile.*.debug output. Called for --verbose; without it only
/// warnings and above are emitted, whatever QT_LOGGING_RULES says about us.
void enableVerboseLogging();

} // namespace pf
