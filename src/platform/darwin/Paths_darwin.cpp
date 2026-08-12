// macOS path roots.
//
// Native locations rather than XDG: a macOS user expects application data under
// ~/Library. The environment overrides still work, so a user who does want to
// share dotfiles with a Linux machine can point PANEFILE_CONFIG_DIR at
// ~/.config/panefile and get exactly that.

#include "platform/Paths.h"
#include "platform/PathsInternal.h"

#include "core/Version.h"

#include <QDir>

namespace pf::platform {
namespace {

QString applicationSupportDir()
{
    return homeDir() + QStringLiteral("/Library/Application Support/" PF_APPLICATION_NAME);
}

} // namespace

QString configDir()
{
    if (const QString override = envDir("PANEFILE_CONFIG_DIR"); !override.isEmpty()) {
        return override;
    }
    return applicationSupportDir();
}

QString stateDir()
{
    if (const QString override = envDir("PANEFILE_STATE_DIR"); !override.isEmpty()) {
        return override;
    }
    // macOS has no separate data-vs-config split, so state nests inside the
    // Application Support directory. §8's actual requirement — that state never
    // lands in a file the user hand-edits — is still met.
    return applicationSupportDir() + QStringLiteral("/state");
}

QString cacheDir()
{
    if (const QString override = envDir("PANEFILE_CACHE_DIR"); !override.isEmpty()) {
        return override;
    }
    // Unlike Linux this *is* application-scoped: there is no shared
    // freedesktop thumbnail cache on macOS to contribute to.
    return homeDir() + QStringLiteral("/Library/Caches/" PF_APPLICATION_NAME);
}

QString runtimeDir()
{
    if (const QString override = envDir("PANEFILE_RUNTIME_DIR"); !override.isEmpty()) {
        return override;
    }
    // QDir::tempPath() maps to the confined per-user $TMPDIR on macOS, which is
    // the closest equivalent of XDG_RUNTIME_DIR: user-private and cleaned up.
    return QDir::tempPath();
}

} // namespace pf::platform
