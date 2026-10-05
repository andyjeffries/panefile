#pragma once

#include <QString>

namespace pf::platform {

/// Panefile's desktop ID, which is what `mimeapps.list` names.
inline constexpr char kPanefileDesktopId[] = "panefile.desktop";

/// Whether this platform has a default file manager Panefile can become.
/// True on Linux. False on macOS, where Finder cannot be replaced.
bool defaultFileManagerSupported();

/// Where things stand. Gathered by reading files, so never on the GUI thread.
struct DefaultFileManagerStatus {
    bool supported = false;

    /// The `inode/directory` handler from `[Default Applications]`, its
    /// `Name=`, and the `mimeapps.list` that set it. Empty when none is set.
    QString handlerId;
    QString handlerName;
    QString handlerSource;

    /// Whether `panefile.desktop` is installed anywhere the desktop looks. A
    /// bare build tree is not, and cannot be made anyone's default.
    bool panefileInstalled = false;

    /// The user's org.freedesktop.FileManager1 activation file.
    QString serviceFile;
    bool serviceFileExists = false;
    QString serviceExecutable; ///< what its Exec= runs
    bool serviceExecutableExists = false;

    /// Both halves: folders open in Panefile, and "Show in folder" reaches it.
    bool isDefault() const
    {
        return handlerId == QLatin1String(kPanefileDesktopId) && serviceFileExists &&
               serviceExecutableExists;
    }

    /// "Panefile", "Files (org.gnome.Nautilus.desktop)", or "none".
    QString handlerDescription() const;
};

/// Reads the current state.
DefaultFileManagerStatus queryDefaultFileManager();

struct MakeDefaultResult {
    bool ok = false;
    QString error;
    QString mimeappsFile; ///< the mimeapps.list written, or that would have been
    QString serviceFile;
};

/// Makes Panefile the default: `inode/directory` in `mimeapps.list`, and the
/// org.freedesktop.FileManager1 activation file. Asks the session bus to
/// reload its service directories afterwards, so activation takes effect
/// without a new login. Blocking file and bus I/O — not on the GUI thread.
MakeDefaultResult makePanefileDefaultFileManager();

/// The absolute path the activation file should start: `pf` from PATH if it is
/// there, otherwise this binary.
QString panefileExecutable();

/// Who owns org.freedesktop.FileManager1 on the session bus right now —
/// "nautilus (pid 1234)" — or empty when nobody does. Blocking; for the command
/// line only.
QString fileManagerNameOwner();

} // namespace pf::platform
