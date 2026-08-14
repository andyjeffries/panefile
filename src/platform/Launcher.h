#pragma once

#include <QString>
#include <QStringList>

namespace pf::platform {

/// Handing a path to something else on the system.
///
/// §6.3 binds four actions to this — open with the default application, open a
/// terminal here, open a file in `$EDITOR`, open the directory in `$EDITOR` —
/// and none of them had an implementation. Every one is a different sentence in
/// each platform's dialect, which is exactly what the platform seam is for.
///
/// Each returns false when the launch could not be started. That is not the
/// same as the launched thing failing: a terminal that opens and then exits is
/// a success here, because Panefile's part is over once the process is running.
struct Launcher {
    /// Opens a file or directory with whatever the desktop considers its
    /// default handler — `open` on macOS, the XDG association on Linux.
    static bool openWithDefaultApplication(const QString &path);

    /// Opens a terminal with `directory` as its working directory.
    ///
    /// `$TERMINAL` wins if it is set, on both platforms: someone who has said
    /// which terminal they want has said it there, and second-guessing them in
    /// favour of the system default would make the variable useless.
    static bool openTerminal(const QString &directory);

    /// Opens `path` in `$EDITOR`, in a terminal, since `$EDITOR` is
    /// conventionally a terminal program. Falls back to the default handler
    /// when `$EDITOR` is unset, which is better than doing nothing and saying
    /// nothing.
    static bool openInEditor(const QString &path);

    /// The editor that openInEditor would use, for a status message that can
    /// name it. Empty when there is none and the default handler would be used.
    static QString editorName();
};

} // namespace pf::platform
