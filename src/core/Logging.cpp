#include "core/Logging.h"

Q_LOGGING_CATEGORY(pfApp, "panefile.app")
Q_LOGGING_CATEGORY(pfConfig, "panefile.config")
Q_LOGGING_CATEGORY(pfFs, "panefile.fs")
Q_LOGGING_CATEGORY(pfJobs, "panefile.jobs")
Q_LOGGING_CATEGORY(pfKeys, "panefile.keys")
Q_LOGGING_CATEGORY(pfUi, "panefile.ui")
Q_LOGGING_CATEGORY(pfIpc, "panefile.ipc")
Q_LOGGING_CATEGORY(pfStartup, "panefile.startup")

namespace pf {

void enableVerboseLogging()
{
    // Appended rather than assigned so an explicit QT_LOGGING_RULES from the
    // environment still applies; ours simply wins for the panefile.* tree.
    QLoggingCategory::setFilterRules(QStringLiteral("panefile.*.debug=true"));
}

} // namespace pf
