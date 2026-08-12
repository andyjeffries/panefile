// Linux path roots: the XDG base directory specification (§8).

#include "platform/Paths.h"
#include "platform/PathsInternal.h"

#include "core/Version.h"

#include <QDir>

namespace pf::platform {
namespace {

constexpr QLatin1String kAppSuffix{"/" PF_APPLICATION_NAME};

} // namespace

QString configDir()
{
    if (const QString override = envDir("PANEFILE_CONFIG_DIR"); !override.isEmpty()) {
        return override;
    }
    QString base = envDir("XDG_CONFIG_HOME");
    if (base.isEmpty()) {
        base = homeDir() + QStringLiteral("/.config");
    }
    return base + kAppSuffix;
}

QString stateDir()
{
    if (const QString override = envDir("PANEFILE_STATE_DIR"); !override.isEmpty()) {
        return override;
    }
    QString base = envDir("XDG_DATA_HOME");
    if (base.isEmpty()) {
        base = homeDir() + QStringLiteral("/.local/share");
    }
    return base + kAppSuffix;
}

QString cacheDir()
{
    if (const QString override = envDir("PANEFILE_CACHE_DIR"); !override.isEmpty()) {
        return override;
    }
    QString base = envDir("XDG_CACHE_HOME");
    if (base.isEmpty()) {
        base = homeDir() + QStringLiteral("/.cache");
    }
    // Deliberately not suffixed with the application name: §7.7 requires the
    // shared freedesktop thumbnail cache at $XDG_CACHE_HOME/thumbnails, so that
    // thumbnails Panefile generates are reused by other file managers and
    // vice versa. Panefile-private caches nest under a subdirectory instead.
    return base;
}

QString runtimeDir()
{
    if (const QString override = envDir("PANEFILE_RUNTIME_DIR"); !override.isEmpty()) {
        return override;
    }
    if (const QString xdg = envDir("XDG_RUNTIME_DIR"); !xdg.isEmpty()) {
        return xdg;
    }
    // Rare — a session without XDG_RUNTIME_DIR — but a socket in the shared
    // temp directory still works, and §10.3 already namespaces it by uid.
    return QDir::tempPath();
}

} // namespace pf::platform
