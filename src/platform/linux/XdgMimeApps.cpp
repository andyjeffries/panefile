#include "platform/linux/XdgMimeApps.h"

#include "platform/Paths.h"
#include "platform/PathsInternal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace pf::platform::xdg {
namespace {

QStringList absoluteEntries(const QByteArray &raw, const QStringList &fallback)
{
    QStringList result;
    for (const QString &entry : QString::fromLocal8Bit(raw).split(QLatin1Char(':'))) {
        // The base directory specification: a relative entry is ignored.
        if (!entry.isEmpty() && QDir::isAbsolutePath(entry)) {
            result << QDir::cleanPath(entry);
        }
    }
    return result.isEmpty() ? fallback : result;
}

QByteArray readFile(const QString &path, bool *exists = nullptr)
{
    QFile file(path);
    if (exists != nullptr) {
        *exists = file.exists();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

/// A line without its `\r`, if it has one. The `\r` itself is never removed
/// from the file: lines are edited in place, and CRLF survives.
QByteArray withoutCarriageReturn(const QByteArray &line)
{
    return line.endsWith('\r') ? line.chopped(1) : line;
}

/// The group a line opens, or a null QByteArray if it does not open one.
QByteArray groupOpenedBy(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.size() < 2 || !trimmed.startsWith('[') || !trimmed.endsWith(']')) {
        return {};
    }
    return trimmed.mid(1, trimmed.size() - 2);
}

/// The key a line assigns, or a null QByteArray for a comment, a blank or a
/// group header. Whitespace around the key is not significant.
QByteArray keyAssignedBy(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith('[')) {
        return {};
    }
    const qsizetype equals = trimmed.indexOf('=');
    if (equals <= 0) {
        return {};
    }
    return trimmed.left(equals).trimmed();
}

QStringList splitList(const QByteArray &value)
{
    QStringList items;
    for (const QByteArray &item : value.split(';')) {
        const QByteArray trimmed = item.trimmed();
        if (!trimmed.isEmpty()) {
            items << QString::fromUtf8(trimmed);
        }
    }
    return items;
}

QByteArray valueOf(const QByteArray &line)
{
    const QByteArray bare = withoutCarriageReturn(line);
    return bare.mid(bare.indexOf('=') + 1);
}

/// `line` with its value replaced. The key, the spacing on either side of the
/// `=`, and a trailing `\r` are all kept exactly as written.
QByteArray replaceValue(const QByteArray &line, const QByteArray &value)
{
    qsizetype valueStart = line.indexOf('=') + 1;
    while (valueStart < line.size() &&
           (line.at(valueStart) == ' ' || line.at(valueStart) == '\t')) {
        ++valueStart;
    }
    QByteArray replaced = line.left(valueStart) + value;
    if (line.endsWith('\r')) {
        replaced += '\r';
    }
    return replaced;
}

/// A file as an editable list of lines.
///
/// Split on `\n` and joined back with `\n`, which reproduces the original
/// bytes exactly — including a missing final newline and every `\r` — so only
/// the lines an edit touches can differ.
class LineEditor
{
public:
    explicit LineEditor(const QByteArray &content)
        : m_lines(content.split('\n')), m_crlf(content.contains("\r\n"))
    {
        // An empty file splits to one empty line. Treating it as no lines at
        // all keeps a new file from starting with a blank.
        if (content.isEmpty()) {
            m_lines.clear();
        }
    }

    QByteArray content() const { return m_lines.join('\n'); }

    /// Calls `edit` with every `key=` line in every `[group]`, in order. The
    /// function returns the replacement line. Returns how many it found.
    template<typename Edit>
    int editKey(const QByteArray &group, const QByteArray &key, Edit edit)
    {
        int found = 0;
        QByteArray current;
        for (QByteArray &line : m_lines) {
            if (const QByteArray opened = groupOpenedBy(line); !opened.isNull()) {
                current = opened;
                continue;
            }
            if (current == group && keyAssignedBy(line) == key) {
                line = edit(line);
                ++found;
            }
        }
        return found;
    }

    /// Adds `key=value` at the end of the first `[group]`, creating the group
    /// at the end of the file if there is none.
    void appendKey(const QByteArray &group, const QByteArray &key, const QByteArray &value)
    {
        const QByteArray newLine = key + '=' + value + (m_crlf ? "\r" : "");

        qsizetype groupStart = -1;
        for (qsizetype i = 0; i < m_lines.size(); ++i) {
            if (groupOpenedBy(m_lines.at(i)) == group) {
                groupStart = i;
                break;
            }
        }

        if (groupStart >= 0) {
            // After the group's last non-blank line, so the blank lines that
            // separate it from the next group stay where they are.
            qsizetype insertAt = groupStart + 1;
            for (qsizetype i = groupStart + 1; i < m_lines.size(); ++i) {
                if (!groupOpenedBy(m_lines.at(i)).isNull()) {
                    break;
                }
                if (!withoutCarriageReturn(m_lines.at(i)).trimmed().isEmpty()) {
                    insertAt = i + 1;
                }
            }
            m_lines.insert(insertAt, newLine);
            return;
        }

        // A file ending in a newline has an empty last element; new lines go
        // before it, so it still ends in one.
        const bool endsWithNewline = !m_lines.isEmpty() && m_lines.constLast().isEmpty();
        if (endsWithNewline) {
            m_lines.removeLast();
        }
        const QByteArray blank = m_crlf ? "\r" : "";
        if (!m_lines.isEmpty() && !withoutCarriageReturn(m_lines.constLast()).trimmed().isEmpty()) {
            m_lines.append(blank);
        }
        m_lines.append('[' + group + ']' + (m_crlf ? "\r" : ""));
        m_lines.append(newLine);
        // The empty last element is the file's final newline.
        m_lines.append(QByteArray());
    }

private:
    QList<QByteArray> m_lines;
    bool m_crlf = false;
};

/// Whether `path` lives under one of the user's own base directories. Only
/// those are candidates for editing: a system file being writable — as root,
/// say — is not a reason to change every user's default.
bool isUserFile(const Environment &env, const QString &path)
{
    return path.startsWith(env.configHome + QLatin1Char('/')) ||
           path.startsWith(env.dataHome + QLatin1Char('/'));
}

/// The file a write to `path` would actually replace: the target if `path` is
/// a symlink, even a dangling one, and `path` itself otherwise.
QString writeTarget(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isSymLink()) {
        return path;
    }
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.symLinkTarget() : canonical;
}

/// Whether an atomic replace of `path` can succeed: the target itself must be
/// writable, and so must its directory, since that is where the temporary goes.
bool isReplaceable(const QString &path)
{
    const QString target = writeTarget(path);
    const QFileInfo info(target);
    const QFileInfo directory(info.absolutePath());
    return (!info.exists() || info.isWritable()) && directory.isWritable();
}

/// Quotes a path for a D-Bus service file's `Exec=`, which the bus splits with
/// shell rules. Plain paths are left bare, because that is what every service
/// file on the system looks like.
QString quoteForExec(const QString &path)
{
    static const QString kSafe = QStringLiteral("/._-+:@%,=");
    const bool plain = std::ranges::all_of(
        path, [](QChar c) { return c.isLetterOrNumber() || kSafe.contains(c); });
    if (plain) {
        return path;
    }
    QString escaped = path;
    escaped.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString findInApplicationsDir(const QString &directory, const QString &desktopId)
{
    QString direct = directory + QLatin1Char('/') + desktopId;
    if (QFileInfo(direct).isFile()) {
        return direct;
    }

    // `a-b-c.desktop` may be `a/b-c.desktop` or `a/b/c.desktop`. Only
    // directories that exist are descended into, so this stays proportional to
    // what is on disk rather than to the number of dashes.
    for (qsizetype dash = desktopId.indexOf(QLatin1Char('-')); dash > 0;
         dash = desktopId.indexOf(QLatin1Char('-'), dash + 1)) {
        const QString subdirectory = directory + QLatin1Char('/') + desktopId.left(dash);
        if (QFileInfo(subdirectory).isDir()) {
            const QString found = findInApplicationsDir(subdirectory, desktopId.mid(dash + 1));
            if (!found.isEmpty()) {
                return found;
            }
        }
    }
    return {};
}

bool isHidden(const QString &desktopFile)
{
    const QByteArray content = readFile(desktopFile);
    LineEditor editor(content);
    bool hidden = false;
    editor.editKey("Desktop Entry", "Hidden", [&hidden](const QByteArray &line) {
        hidden = withoutCarriageReturn(valueOf(line)).trimmed() == "true";
        return line;
    });
    return hidden;
}

} // namespace

Environment Environment::fromProcess()
{
    Environment env;

    env.configHome = envDir("XDG_CONFIG_HOME");
    if (env.configHome.isEmpty()) {
        env.configHome = homeDir() + QStringLiteral("/.config");
    }
    env.dataHome = envDir("XDG_DATA_HOME");
    if (env.dataHome.isEmpty()) {
        env.dataHome = homeDir() + QStringLiteral("/.local/share");
    }

    env.configDirs = absoluteEntries(qgetenv("XDG_CONFIG_DIRS"), {QStringLiteral("/etc/xdg")});
    env.dataDirs = absoluteEntries(qgetenv("XDG_DATA_DIRS"), {QStringLiteral("/usr/local/share"),
                                                              QStringLiteral("/usr/share")});

    for (const QString &desktop :
         QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP")).split(QLatin1Char(':'))) {
        if (!desktop.isEmpty()) {
            env.desktops << desktop.toLower();
        }
    }

    return env;
}

QStringList mimeappsListPaths(const Environment &env)
{
    QStringList paths;
    const auto addDirectory = [&paths, &env](const QString &directory) {
        for (const QString &desktop : env.desktops) {
            paths << directory + QLatin1Char('/') + desktop + QStringLiteral("-mimeapps.list");
        }
        paths << directory + QStringLiteral("/mimeapps.list");
    };

    addDirectory(env.configHome);
    for (const QString &directory : env.configDirs) {
        addDirectory(directory);
    }
    // The data-directory locations are deprecated by the specification and
    // still honoured by GLib, so they are honoured here too: a default that
    // GIO obeys and this ignores would make the bar offer something already
    // done.
    addDirectory(env.dataHome + QStringLiteral("/applications"));
    for (const QString &directory : env.dataDirs) {
        addDirectory(directory + QStringLiteral("/applications"));
    }

    paths.removeDuplicates();
    return paths;
}

QStringList applicationDirs(const Environment &env)
{
    QStringList dirs{env.dataHome + QStringLiteral("/applications")};
    for (const QString &directory : env.dataDirs) {
        dirs << directory + QStringLiteral("/applications");
    }
    dirs.removeDuplicates();
    return dirs;
}

QString desktopFilePath(const Environment &env, const QString &desktopId)
{
    if (desktopId.isEmpty() || desktopId.contains(QLatin1Char('/'))) {
        return {};
    }
    for (const QString &directory : applicationDirs(env)) {
        const QString found = findInApplicationsDir(directory, desktopId);
        if (!found.isEmpty()) {
            // The first one found is the one that counts, Hidden or not: a
            // user's Hidden=true copy exists precisely to mask the system's.
            return isHidden(found) ? QString() : found;
        }
    }
    return {};
}

QString desktopEntryName(const QString &desktopFilePath)
{
    LineEditor editor(readFile(desktopFilePath));
    QString name;
    editor.editKey("Desktop Entry", "Name", [&name](const QByteArray &line) {
        if (name.isEmpty()) {
            name = QString::fromUtf8(withoutCarriageReturn(valueOf(line)).trimmed());
        }
        return line;
    });
    return name;
}

QStringList associationsIn(const QByteArray &content, const QString &group, const QString &mimeType)
{
    QStringList result;
    LineEditor editor(content);
    editor.editKey(group.toUtf8(), mimeType.toUtf8(), [&result](const QByteArray &line) {
        // A repeated key: the last one is what GKeyFile keeps.
        result = splitList(valueOf(line));
        return line;
    });
    return result;
}

Resolution resolveDefault(const Environment &env, const QString &mimeType)
{
    for (const QString &path : mimeappsListPaths(env)) {
        bool exists = false;
        const QByteArray content = readFile(path, &exists);
        if (!exists) {
            continue;
        }
        for (const QString &desktopId :
             associationsIn(content, QLatin1String(kDefaultApplicationsGroup), mimeType)) {
            if (!desktopFilePath(env, desktopId).isEmpty()) {
                return {.desktopId = desktopId, .sourceFile = path};
            }
        }
    }
    return {};
}

QByteArray withDefaultApplication(const QByteArray &content, const QString &mimeType,
                                  const QString &desktopId)
{
    LineEditor editor(content);
    const QByteArray key = mimeType.toUtf8();
    const QByteArray id = desktopId.toUtf8();

    // [Default Applications]: exactly this one application. A line that
    // already says so — with or without a trailing `;` — is left alone, which
    // is what makes a second run a no-op.
    const int defaults =
        editor.editKey(kDefaultApplicationsGroup, key, [&id, &desktopId](const QByteArray &line) {
            return splitList(valueOf(line)) == QStringList{desktopId} ? line
                                                                      : replaceValue(line, id);
        });
    if (defaults == 0) {
        editor.appendKey(kDefaultApplicationsGroup, key, id);
    }

    // [Added Associations]: first, ahead of whatever else is listed, which
    // stays listed. Some tools read this list rather than the default.
    const int added =
        editor.editKey(kAddedAssociationsGroup, key, [&desktopId](const QByteArray &line) {
            QStringList items = splitList(valueOf(line));
            if (!items.isEmpty() && items.constFirst() == desktopId) {
                return line;
            }
            items.removeAll(desktopId);
            items.prepend(desktopId);
            return replaceValue(line, items.join(QLatin1Char(';')).toUtf8() + ';');
        });
    if (added == 0) {
        editor.appendKey(kAddedAssociationsGroup, key, id + ';');
    }

    return editor.content();
}

WriteResult chooseMimeappsFile(const Environment &env, const QString &mimeType)
{
    const QStringList paths = mimeappsListPaths(env);
    const QString userFile = env.configHome + QStringLiteral("/mimeapps.list");
    const qsizetype userIndex = paths.indexOf(userFile);

    WriteResult result;
    result.ok = true;
    result.path = userFile;

    for (qsizetype i = 0; i < paths.size(); ++i) {
        const QString &path = paths.at(i);
        bool exists = false;
        const QByteArray content = readFile(path, &exists);
        if (!exists ||
            associationsIn(content, QLatin1String(kDefaultApplicationsGroup), mimeType).isEmpty()) {
            continue;
        }

        // The highest-precedence file that sets it. Editing it is the only
        // edit guaranteed to be the one that counts.
        if (isUserFile(env, path) && isReplaceable(path)) {
            result.path = path;
            return result;
        }

        // It cannot be edited. If it outranks the user's own mimeapps.list —
        // a desktop-specific file, typically — writing there would produce a
        // default that loses to it, which is worse than saying so.
        if (i < userIndex) {
            result.ok = false;
            result.path = path;
            result.error =
                QStringLiteral("%1 overrides it and isn't writable").arg(abbreviateHome(path));
            return result;
        }
        return result;
    }

    return result;
}

WriteResult setDefaultApplication(const Environment &env, const QString &mimeType,
                                  const QString &desktopId)
{
    WriteResult chosen = chooseMimeappsFile(env, mimeType);
    if (!chosen.ok) {
        return chosen;
    }

    bool exists = false;
    const QByteArray before = readFile(chosen.path, &exists);
    if (exists && before.isEmpty() && !QFileInfo(chosen.path).isReadable()) {
        chosen.ok = false;
        chosen.error = QStringLiteral("%1 isn't readable").arg(abbreviateHome(chosen.path));
        return chosen;
    }

    const QByteArray after = withDefaultApplication(before, mimeType, desktopId);
    if (exists && after == before) {
        return chosen;
    }
    return writeFileAtomically(chosen.path, after);
}

WriteResult writeFileAtomically(const QString &path, const QByteArray &contents)
{
    WriteResult result;
    result.path = path;

    // The target, not the link. QSaveFile would rename its temporary over
    // whatever it was given, and renaming over a symlink replaces the link —
    // which is exactly the damage `xdg-mime` does to a stowed mimeapps.list.
    const QString target = writeTarget(path);
    const QFileInfo targetInfo(target);
    const bool existed = targetInfo.exists();
    const QFileDevice::Permissions permissions = targetInfo.permissions();

    if (!QDir().mkpath(targetInfo.absolutePath())) {
        result.error =
            QStringLiteral("couldn't create %1").arg(abbreviateHome(targetInfo.absolutePath()));
        return result;
    }

    // QSaveFile writes a temporary beside the target, fsyncs it on commit and
    // renames it into place, so the file is always either the old one or the
    // new one — never half of each.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("%1: %2").arg(abbreviateHome(target), file.errorString());
        return result;
    }
    file.write(contents);
    if (!file.commit()) {
        result.error = QStringLiteral("%1: %2").arg(abbreviateHome(target), file.errorString());
        return result;
    }

    // A new temporary is created 0600. A file someone shares between machines
    // through a dotfiles repository should keep the mode it had.
    if (existed && QFileInfo(target).permissions() != permissions) {
        QFile::setPermissions(target, permissions);
    }

    result.ok = true;
    result.changed = true;
    return result;
}

QString fileManagerServiceFilePath(const Environment &env)
{
    return env.dataHome + QStringLiteral("/dbus-1/services/") + QLatin1String(kFileManagerBusName) +
           QStringLiteral(".service");
}

QByteArray fileManagerServiceFileContents(const QString &executable)
{
    return QStringLiteral("# Written by Panefile when it was made the default file manager.\n"
                          "# Delete this file to hand \"Show in folder\" back to the system's\n"
                          "# file manager.\n"
                          "[D-BUS Service]\n"
                          "Name=%1\n"
                          "Exec=%2 --dbus-service\n")
        .arg(QLatin1String(kFileManagerBusName), quoteForExec(executable))
        .toUtf8();
}

QString executableInServiceFile(const QByteArray &contents)
{
    LineEditor editor(contents);
    QString exec;
    editor.editKey("D-BUS Service", "Exec", [&exec](const QByteArray &line) {
        exec = QString::fromUtf8(withoutCarriageReturn(valueOf(line)).trimmed());
        return line;
    });
    if (exec.isEmpty()) {
        return {};
    }

    // The first word, with shell quoting undone: enough of the bus's own
    // parser for the files written above and for any plausible hand edit.
    QString program;
    QChar quote;
    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            } else if (c == QLatin1Char('\\') && quote == QLatin1Char('"') && i + 1 < exec.size()) {
                program += exec.at(++i);
            } else {
                program += c;
            }
            continue;
        }
        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            quote = c;
        } else if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
            program += exec.at(++i);
        } else if (c.isSpace()) {
            break;
        } else {
            program += c;
        }
    }
    return program;
}

WriteResult writeFileManagerServiceFile(const Environment &env, const QString &executable)
{
    const QString path = fileManagerServiceFilePath(env);
    if (!QDir::isAbsolutePath(executable)) {
        WriteResult result;
        result.path = path;
        result.error =
            QStringLiteral("the service needs an absolute path to pf, not '%1'").arg(executable);
        return result;
    }

    const QByteArray contents = fileManagerServiceFileContents(executable);
    bool exists = false;
    if (readFile(path, &exists) == contents && exists) {
        WriteResult unchanged;
        unchanged.ok = true;
        unchanged.path = path;
        return unchanged;
    }
    return writeFileAtomically(path, contents);
}

QString abbreviateHome(const QString &path)
{
    const QString home = homeDir();
    if (path == home) {
        return QStringLiteral("~");
    }
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + path.mid(home.size());
    }
    return path;
}

} // namespace pf::platform::xdg
