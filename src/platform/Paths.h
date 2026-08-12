#pragma once

#include <QString>
#include <QStringList>

namespace pf::platform {

/// Filesystem locations, resolved per platform.
///
/// §8 pins Linux to the XDG basedirs. macOS uses its own conventions instead of
/// pretending to be Linux — `~/Library/Application Support` for configuration
/// and state, `~/Library/Caches` for the thumbnail cache — because a macOS user
/// expects their files where macOS puts everyone else's.
///
/// Every accessor honours a `PANEFILE_*_DIR` environment override. That is what
/// lets the test suite run against a temp directory instead of the developer's
/// real configuration, and it is the only supported way to relocate these.
///
/// None of these create the directory they name; callers that write do that.

/// User configuration: config.toml, hotkeys.toml, theme.toml, themes/.
/// Override with PANEFILE_CONFIG_DIR.
QString configDir();

/// Persistent state: pinned directories, session, window geometry.
/// §8 is explicit that this is *not* the config directory — Panefile never
/// writes to a user's config file. Override with PANEFILE_STATE_DIR.
QString stateDir();

/// Base cache directory. Override with PANEFILE_CACHE_DIR.
QString cacheDir();

/// Thumbnail cache root, containing normal/, large/ and fail/ (§7.7).
/// On Linux this is the shared freedesktop location, so thumbnails are reused
/// by other applications. macOS has no such shared spec, so it is private.
QString thumbnailCacheDir();

/// Directory for the single-instance socket (§10.3). XDG_RUNTIME_DIR on Linux,
/// the per-user temporary directory on macOS.
QString runtimeDir();

/// Path of the single-instance socket for the current user.
QString singleInstanceSocketPath();

/// Read-only application data (bundled themes, icons, man page), most specific
/// first: PANEFILE_DATA_DIR, then a location relative to the running binary
/// (which covers both a build tree and a relocatable macOS bundle), then the
/// compiled-in install prefix.
QStringList dataSearchPaths();

/// Directories searched for theme .toml files, user themes first (§8).
QStringList themeSearchPaths();

/// The user's home directory.
QString homeDir();

} // namespace pf::platform
