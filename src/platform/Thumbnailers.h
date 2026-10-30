#pragma once

#include <QByteArray>
#include <QHash>
#include <QMimeType>
#include <QString>
#include <QStringList>

#include <optional>

namespace pf::platform {

/// One parsed `[Thumbnailer Entry]` of a freedesktop `.thumbnailer` file.
struct ThumbnailerEntry {
    QString tryExec;
    QString exec;
    QStringList mimeTypes;

    /// The absolute path of the program, filled in when the registry is loaded
    /// and the program was found.
    QString program;
};

/// What to run for one file. `arguments` excludes the program.
struct ThumbnailerCommand {
    QString program;
    QStringList arguments;

    /// The thumbnailer sandboxes itself (glycin), so wrapping it in a second
    /// sandbox would only break it.
    bool selfSandboxed = false;
};

/// The freedesktop thumbnailer registry (`$XDG_DATA_DIRS/thumbnailers`).
///
/// Loaded lazily, on the first query that reaches it: reading a few dozen small
/// files has no place on the startup path.
class Thumbnailers
{
public:
    /// The process registry. Lazy and thread-safe. Empty on platforms with no
    /// implementation (macOS: QuickLook is a follow-up).
    static Thumbnailers &instance();

    /// Builds a registry from these `thumbnailers` directories, highest
    /// precedence first. A file name in a higher directory shadows the same
    /// name in a lower one, and the first thumbnailer to claim a MIME type wins.
    static Thumbnailers fromDirectories(const QStringList &directories);

    /// Whether anything is registered for this type, or any of its aliases.
    bool handles(const QMimeType &mime) const;

    std::optional<ThumbnailerCommand> commandFor(const QMimeType &mime, const QString &input,
                                                 const QString &output, int sizePx) const;

    // Pure helpers, public for the tests.
    static std::optional<ThumbnailerEntry> parseEntry(const QByteArray &content);

    /// Splits `exec` with the desktop entry quoting rules and expands `%i %u %o
    /// %s %%`. Unknown codes are dropped. Never involves a shell.
    static QStringList expandExec(const QString &exec, const QString &input, const QString &output,
                                  int size);

    /// The full argv that runs `cmd` under bubblewrap: no network, read-only
    /// system, only `input` and `outputDir` visible.
    static QStringList sandboxed(const ThumbnailerCommand &cmd, const QString &bwrap,
                                 const QString &input, const QString &outputDir);

    int entryCount() const;

private:
    const ThumbnailerEntry *entryFor(const QMimeType &mime) const;

    QHash<QString, ThumbnailerEntry> m_byMime;
};

} // namespace pf::platform
