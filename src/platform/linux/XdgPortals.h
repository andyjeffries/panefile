#pragma once

#include "platform/linux/XdgMimeApps.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

/// The xdg-desktop-portal side of "Panefile answers Open and Save": which
/// portals.conf is in effect, the one line in it that names the FileChooser
/// backend, and the two files that let the portal find and start Panefile.
///
/// xdg-desktop-portal reads only the first configuration file it finds, so a
/// new ~/.config/xdg-desktop-portal/portals.conf would silently hide
/// `hyprland-portals.conf` and break screen sharing. The edit goes into the
/// file that is already in effect, copied into the user's config first when it
/// is a system one.
namespace pf::platform::xdg {

inline constexpr char kPortalBackendName[] = "panefile";
inline constexpr char kPortalBusName[] = "org.freedesktop.impl.portal.desktop.panefile";
inline constexpr char kFileChooserInterface[] = "org.freedesktop.impl.portal.FileChooser";

/// Every file portals.conf(5) would read, in the order it reads them: for each
/// of the config and data directories, `<desktop>-portals.conf` for each
/// desktop in turn, then `portals.conf`.
QStringList portalConfigSearchPaths(const Environment &env);

/// The first of those that exists, or empty.
QString effectivePortalConfig(const Environment &env);

/// Where the FileChooser line should be written, and what the file holds now.
struct PortalConfigTarget {
    QString path;
    QByteArray
        content; ///< what to start from: the user's file, a system file's bytes, or `[preferred]`
};
PortalConfigTarget userPortalConfigTarget(const Environment &env);

/// Whether the backend's `<name>.portal` lists `interface`. xdg-desktop-portal
/// skips a backend that doesn't, so naming it in portals.conf does nothing. The
/// first file found, in the data directories' order, decides; none means false.
bool portalImplements(const Environment &env, const QString &backend, const char *interface);

/// Whether the file at `path`, which holds `content`, only repeats what would
/// apply without it: it is a regular file in the user's config, and the next
/// portals.conf in the search order has the same bytes, or there is none and
/// it is just `[preferred]`. Restoring removes such a file, so it stops
/// shadowing the system one.
bool isRedundantUserConfig(const Environment &env, const QString &path, const QByteArray &content);

/// The backends named for the FileChooser interface, in order: its own line if
/// there is one, otherwise `default=`. Empty when neither is set.
QStringList fileChooserBackends(const QByteArray &content);

/// `content` with Panefile first in the FileChooser line of `[preferred]`, and
/// the line it replaced kept as a `# Panefile replaced:` comment above it. The
/// old backends follow as the fallback. Every other byte is left alone, and
/// applying it twice changes nothing the second time.
QByteArray withFileChooser(const QByteArray &content);

/// The inverse: the old line back from its comment, or Panefile's line
/// removed when there was none.
QByteArray withoutFileChooser(const QByteArray &content);

/// `$XDG_DATA_HOME/xdg-desktop-portal/portals/panefile.portal`.
QString portalFilePath(const Environment &env);
QByteArray portalFileContents();

QString portalServiceFilePath(const Environment &env);
QByteArray portalServiceFileContents(const QString &executable);
WriteResult writePortalServiceFile(const Environment &env, const QString &executable);

} // namespace pf::platform::xdg
