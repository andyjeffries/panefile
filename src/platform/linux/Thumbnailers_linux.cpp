#include "platform/Thumbnailers.h"
#include "platform/linux/XdgMimeApps.h"

#include <mutex>

namespace pf::platform {

Thumbnailers &Thumbnailers::instance()
{
    static std::once_flag once;
    static Thumbnailers registry;
    std::call_once(once, [] {
        const xdg::Environment env = xdg::Environment::fromProcess();
        QStringList directories{env.dataHome + QStringLiteral("/thumbnailers")};
        for (const QString &dir : env.dataDirs) {
            directories << dir + QStringLiteral("/thumbnailers");
        }
        registry = fromDirectories(directories);
    });
    return registry;
}

} // namespace pf::platform
