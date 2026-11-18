#pragma once

#include <QString>
#include <QStringList>

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

// --- Open and Save dialogs ---------------------------------------------------

/// Whether the xdg-desktop-portal FileChooser can be pointed at Panefile.
/// True on Linux, false on macOS.
bool fileChooserSupported();

/// Where the portal FileChooser stands. Gathered by reading files, so never on
/// the GUI thread.
struct FileChooserStatus {
    bool supported = false;

    /// `panefile.portal`, which tells xdg-desktop-portal the backend exists.
    QString portalFile;
    bool portalFileExists = false;

    /// The D-Bus activation file that starts `pf --portal`, and its `Exec=`.
    QString serviceFile;
    QString serviceExecutable;
    bool serviceExecutableExists = false;

    /// The portals.conf in effect, empty when there is none.
    QString configFile;

    /// The backends it names for the FileChooser, in order.
    QStringList backends;

    /// Those of them whose portal file doesn't offer a FileChooser, so the
    /// portal skips them.
    QStringList withoutChooser;

    bool isPanefile() const
    {
        return !backends.isEmpty() && backends.constFirst() == QLatin1String("panefile") &&
               portalFileExists && serviceExecutableExists;
    }

    /// The backend after Panefile that takes over if it is missing, or the
    /// first one when Panefile is not chosen; empty when there is none.
    QString fallback() const;

    /// "GTK", "KDE", or the backend's own name.
    static QString displayName(const QString &backend);
};

FileChooserStatus queryFileChooser();

struct FileChooserSwitchResult {
    bool ok = false;
    QString error;
    QString configFile;
    QString portalFile;
    QString serviceFile;
    /// Set when the portal could not be restarted; the change still stands
    /// and takes effect at the next login.
    QString restartNote;
};

/// Writes the portal file, the activation file and Panefile's line in the
/// portals.conf that is in effect, then restarts xdg-desktop-portal.
FileChooserSwitchResult makePanefileFileChooser();

/// `pf --portal`: answers the portal's FileChooser calls on the session bus
/// until it has been idle for a while, starting `pf --chooser` for each. Needs a
/// QCoreApplication. Returns the exit code; non-zero when the name is taken or
/// there is no bus, and always on macOS.
int runFileChooserPortal();

/// Puts the previous FileChooser line back and restarts the portal.
FileChooserSwitchResult restorePanefileFileChooser();

} // namespace pf::platform
