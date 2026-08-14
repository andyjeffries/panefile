#include "platform/Launcher.h"

#include <QFileInfo>

#include <QProcess>
#include <QProcessEnvironment>
#include <algorithm>

namespace pf::platform {
namespace {

bool runDetached(const QString &program, const QStringList &arguments,
                 const QString &workingDirectory = {})
{
    return QProcess::startDetached(program, arguments, workingDirectory);
}

/// The terminals worth trying when $TERMINAL says nothing, most-likely first.
///
/// A list rather than a single fallback because there is no such thing as "the"
/// terminal on Linux, and picking one vendor's would be wrong on most desktops.
const QStringList &terminalCandidates()
{
    static const QStringList kCandidates{
        QStringLiteral("x-terminal-emulator"),
        QStringLiteral("kitty"),
        QStringLiteral("alacritty"),
        QStringLiteral("foot"),
        QStringLiteral("wezterm"),
        QStringLiteral("ghostty"),
        QStringLiteral("konsole"),
        QStringLiteral("gnome-terminal"),
        QStringLiteral("xfce4-terminal"),
        QStringLiteral("xterm"),
    };
    return kCandidates;
}

} // namespace

bool Launcher::openWithDefaultApplication(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        return false;
    }
    // xdg-open rather than QDesktopServices: §3.4 keeps the association lookup
    // out of the process, and xdg-open is what every other Linux tool uses, so
    // it honours the same configuration the user has already set.
    return runDetached(QStringLiteral("xdg-open"), {path});
}

bool Launcher::openTerminal(const QString &directory)
{
    if (!QFileInfo(directory).isDir()) {
        return false;
    }

    const QString preferred =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("TERMINAL"));
    if (!preferred.isEmpty() && runDetached(preferred, {}, directory)) {
        return true;
    }

    const QStringList &candidates = terminalCandidates();
    return std::ranges::any_of(candidates, [&directory](const QString &candidate) {
        return runDetached(candidate, {}, directory);
    });
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

    // $EDITOR is conventionally a terminal program, so it is started inside one.
    // `-e` is the one argument every terminal on the list above understands.
    const QString preferred =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("TERMINAL"));
    const QString directory = QFileInfo(path).absolutePath();

    QStringList terminals;
    if (!preferred.isEmpty()) {
        terminals << preferred;
    }
    terminals << terminalCandidates();

    return std::ranges::any_of(terminals, [&](const QString &terminal) {
        return runDetached(terminal, {QStringLiteral("-e"), editor, path}, directory);
    });
}

} // namespace pf::platform
