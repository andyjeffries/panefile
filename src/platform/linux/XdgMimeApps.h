#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

/// The freedesktop side of "default file manager": the XDG MIME Applications
/// specification, and the D-Bus service file that routes
/// org.freedesktop.FileManager1 to Panefile.
///
/// Parsed and written by hand rather than through `xdg-mime` or `gio mime`, for
/// two reasons. `xdg-mime` is a shell script that forks a dozen processes to
/// answer one question. And both of them write `mimeapps.list` by renaming a new
/// file over the old one, which replaces a symlinked `mimeapps.list` — the shape
/// every dotfiles manager leaves behind — with a plain file, silently detaching
/// it from the repository it lived in.
///
/// Everything takes an Environment rather than reading the process environment
/// itself, so the tests can point it at a temporary tree and never at the
/// developer's real configuration.
namespace pf::platform::xdg {

/// The XDG base directories and the current desktop, as the specification
/// defines them, defaults applied.
struct Environment {
    QString configHome;
    QStringList configDirs;
    QString dataHome;
    QStringList dataDirs;

    /// XDG_CURRENT_DESKTOP, split on `:` and lowercased, in order. These name
    /// the desktop-specific `$desktop-mimeapps.list` files.
    QStringList desktops;

    /// Reads the variables from the process environment. Relative values are
    /// ignored, as the base directory specification requires.
    static Environment fromProcess();
};

inline constexpr char kDirectoryMimeType[] = "inode/directory";
inline constexpr char kDefaultApplicationsGroup[] = "Default Applications";
inline constexpr char kAddedAssociationsGroup[] = "Added Associations";

/// Every `mimeapps.list` that takes part in a lookup, highest precedence first.
QStringList mimeappsListPaths(const Environment &env);

/// Every `applications/` directory, highest precedence first.
QStringList applicationDirs(const Environment &env);

/// The `.desktop` file a desktop ID resolves to, or empty when it is not
/// installed. A desktop ID maps `-` to a subdirectory (`kde4-dolphin.desktop`
/// may be `kde4/dolphin.desktop`), and an entry with `Hidden=true` hides every
/// lower-precedence entry of the same ID, so it counts as not installed.
QString desktopFilePath(const Environment &env, const QString &desktopId);

/// The entry's `Name=`, for showing to a person. Empty when unreadable.
QString desktopEntryName(const QString &desktopFilePath);

/// The desktop IDs `group` assigns to `mimeType`, in order. Values are
/// `;`-separated lists; empty items are dropped.
QStringList associationsIn(const QByteArray &content, const QString &group,
                           const QString &mimeType);

/// Which application handles a MIME type, and which file said so.
struct Resolution {
    QString desktopId;  ///< empty when no file names an installed application
    QString sourceFile; ///< the mimeapps.list it came from
};

/// Resolves `mimeType` through `[Default Applications]` in precedence order.
/// The first listed desktop ID that is installed wins; an uninstalled one is
/// skipped, not fatal, exactly as the specification says.
Resolution resolveDefault(const Environment &env, const QString &mimeType);

/// `content` with `mimeType` set to `desktopId` in `[Default Applications]`,
/// and `desktopId` moved to the front of `[Added Associations]`.
///
/// A minimal edit: an existing key is rewritten in place, a missing one is
/// added at the end of its group, a missing group at the end of the file. Every
/// other byte — comments, order, blank lines, unknown groups, CRLF line endings
/// — is left as it was. Applying it twice changes nothing the second time.
QByteArray withDefaultApplication(const QByteArray &content, const QString &mimeType,
                                  const QString &desktopId);

struct WriteResult {
    bool ok = false;
    bool changed = false;
    QString path; ///< the file written, or that would have been
    QString error;
};

/// The `mimeapps.list` to edit for `mimeType`: the highest-precedence file
/// that sets it, if it is the user's and writable; otherwise
/// `$XDG_CONFIG_HOME/mimeapps.list`. Fails, rather than writing a file that
/// loses, when a higher-precedence file sets it and cannot be written.
WriteResult chooseMimeappsFile(const Environment &env, const QString &mimeType);

/// Makes `desktopId` the default for `mimeType`, in the file chooseMimeappsFile
/// picks.
WriteResult setDefaultApplication(const Environment &env, const QString &mimeType,
                                  const QString &desktopId);

/// Replaces `path` with `contents` atomically: a temporary in the same
/// directory, fsync, rename. A symlink is resolved first and its *target*
/// replaced, so the link itself survives. The target's permissions are kept.
WriteResult writeFileAtomically(const QString &path, const QByteArray &contents);

// --- org.freedesktop.FileManager1 activation ---------------------------------

inline constexpr char kFileManagerBusName[] = "org.freedesktop.FileManager1";

/// `$XDG_DATA_HOME/dbus-1/services/org.freedesktop.FileManager1.service`. The
/// session bus searches it before `/usr/share/dbus-1/services`, which is what
/// lets it override the file Nautilus installs there.
QString fileManagerServiceFilePath(const Environment &env);

/// The service file's text. `executable` must be absolute: the bus starts the
/// service in its own activation environment, whose PATH may not include
/// `~/.local/bin`.
QByteArray fileManagerServiceFileContents(const QString &executable);

/// The program a service file's `Exec=` runs, unquoted, or empty.
QString executableInServiceFile(const QByteArray &contents);

/// Writes the service file for `executable`.
WriteResult writeFileManagerServiceFile(const Environment &env, const QString &executable);

/// `path` with the home directory shown as `~`, for messages.
QString abbreviateHome(const QString &path);

} // namespace pf::platform::xdg
