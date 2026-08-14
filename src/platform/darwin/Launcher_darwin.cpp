#include "platform/Launcher.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

namespace pf::platform {
namespace {

/// `open` is the whole of Launch Services from a shell's point of view, and
/// spelling it out through QProcess rather than QDesktopServices keeps the
/// failure visible: QDesktopServices::openUrl reports success for anything it
/// managed to hand off, including handing off to nothing.
bool runDetached(const QString &program, const QStringList &arguments,
                 const QString &workingDirectory = {})
{
    return QProcess::startDetached(program, arguments, workingDirectory);
}

} // namespace

bool Launcher::openWithDefaultApplication(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        return false;
    }
    return runDetached(QStringLiteral("/usr/bin/open"), {path});
}

bool Launcher::openTerminal(const QString &directory)
{
    if (!QFileInfo(directory).isDir()) {
        return false;
    }

    // $TERMINAL first, on both platforms: someone who has said which terminal
    // they want has said it there, and preferring Terminal.app anyway would
    // make the variable decorative.
    const QString preferred =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("TERMINAL"));
    if (!preferred.isEmpty()) {
        // A bare name is an application, a path is an executable. `open -a`
        // handles the first; the second is run directly with the directory as
        // its working directory, which is what a terminal binary expects.
        if (preferred.contains(QLatin1Char('/'))) {
            return runDetached(preferred, {}, directory);
        }
        if (runDetached(QStringLiteral("/usr/bin/open"),
                        {QStringLiteral("-a"), preferred, directory})) {
            return true;
        }
        // Fall through rather than fail: a $TERMINAL naming an application that
        // is not installed should not leave the user with nothing.
    }

    return runDetached(QStringLiteral("/usr/bin/open"),
                       {QStringLiteral("-a"), QStringLiteral("Terminal"), directory});
}

bool Launcher::openInEditor(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        return false;
    }

    const QString editor = editorName();
    if (editor.isEmpty()) {
        return openWithDefaultApplication(path);
    }

    // $EDITOR is conventionally a terminal program, so it needs a terminal to
    // live in. `open -a Terminal` cannot carry arguments, so the command goes
    // through a script osascript runs — the standard way to start an
    // interactive terminal command on macOS.
    const QString command =
        QStringLiteral("%1 %2").arg(editor, QStringLiteral("'%1'").arg(QString(path).replace(
                                                QLatin1Char('\''), QLatin1String("'\\''"))));
    const QString script = QStringLiteral("tell application \"Terminal\"\n"
                                          "  do script \"%1\"\n"
                                          "  activate\n"
                                          "end tell")
                               .arg(QString(command)
                                        .replace(QLatin1Char('\\'), QLatin1String("\\\\"))
                                        .replace(QLatin1Char('"'), QLatin1String("\\\"")));

    return runDetached(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script});
}

} // namespace pf::platform
