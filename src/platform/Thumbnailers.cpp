#include "platform/Thumbnailers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace pf::platform {
namespace {

/// Desktop entry quoting: double quotes group, and inside them a backslash
/// escapes the next character.
QStringList tokenise(const QString &exec)
{
    QStringList tokens;
    QString current;
    bool started = false;
    bool quoted = false;

    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (quoted) {
            if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
                current += exec.at(++i);
            } else if (c == QLatin1Char('"')) {
                quoted = false;
            } else {
                current += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
            started = true;
        } else if (c.isSpace()) {
            if (started || !current.isEmpty()) {
                tokens << current;
                current.clear();
                started = false;
            }
        } else {
            current += c;
        }
    }
    if (started || !current.isEmpty()) {
        tokens << current;
    }
    return tokens;
}

bool isSelfSandboxed(const QString &program)
{
    return QFileInfo(program).fileName().startsWith(QLatin1String("glycin-"));
}

} // namespace

std::optional<ThumbnailerEntry> Thumbnailers::parseEntry(const QByteArray &content)
{
    ThumbnailerEntry entry;
    bool inGroup = false;

    for (const QByteArray &rawLine : content.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('['))) {
            inGroup = line == QLatin1String("[Thumbnailer Entry]");
            continue;
        }
        if (!inGroup) {
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("TryExec")) {
            entry.tryExec = value;
        } else if (key == QLatin1String("Exec")) {
            entry.exec = value;
        } else if (key == QLatin1String("MimeType")) {
            entry.mimeTypes = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        }
    }

    if (entry.exec.isEmpty() || entry.mimeTypes.isEmpty()) {
        return std::nullopt;
    }
    return entry;
}

QStringList Thumbnailers::expandExec(const QString &exec, const QString &input,
                                     const QString &output, int size)
{
    QStringList argv;
    for (const QString &token : tokenise(exec)) {
        QString expanded;
        bool hadCode = false;
        for (qsizetype i = 0; i < token.size(); ++i) {
            const QChar c = token.at(i);
            if (c != QLatin1Char('%') || i + 1 >= token.size()) {
                expanded += c;
                continue;
            }
            hadCode = true;
            switch (token.at(++i).unicode()) {
            case 'i':
                expanded += input;
                break;
            case 'u':
                expanded += QUrl::fromLocalFile(input).toString(QUrl::FullyEncoded);
                break;
            case 'o':
                expanded += output;
                break;
            case 's':
                expanded += QString::number(size);
                break;
            case '%':
                expanded += QLatin1Char('%');
                break;
            default:
                break; // unknown field code: dropped
            }
        }
        if (hadCode && expanded.isEmpty()) {
            continue;
        }
        argv << expanded;
    }
    return argv;
}

QStringList Thumbnailers::sandboxed(const ThumbnailerCommand &cmd, const QString &bwrap,
                                    const QString &input, const QString &outputDir)
{
    QStringList argv{bwrap,
                     QStringLiteral("--ro-bind"),
                     QStringLiteral("/usr"),
                     QStringLiteral("/usr"),
                     QStringLiteral("--symlink"),
                     QStringLiteral("usr/lib"),
                     QStringLiteral("/lib"),
                     QStringLiteral("--symlink"),
                     QStringLiteral("usr/lib64"),
                     QStringLiteral("/lib64"),
                     QStringLiteral("--symlink"),
                     QStringLiteral("usr/bin"),
                     QStringLiteral("/bin"),
                     QStringLiteral("--symlink"),
                     QStringLiteral("usr/sbin"),
                     QStringLiteral("/sbin")};
    for (const char *path : {"/etc/ld.so.cache", "/etc/fonts", "/etc/alternatives"}) {
        argv << QStringLiteral("--ro-bind-try") << QString::fromLatin1(path)
             << QString::fromLatin1(path);
    }
    argv << QStringLiteral("--proc") << QStringLiteral("/proc") << QStringLiteral("--dev")
         << QStringLiteral("/dev") << QStringLiteral("--tmpfs") << QStringLiteral("/tmp")
         << QStringLiteral("--ro-bind") << input << input << QStringLiteral("--bind") << outputDir
         << outputDir << QStringLiteral("--unshare-all") << QStringLiteral("--die-with-parent")
         << QStringLiteral("--new-session") << QStringLiteral("--clearenv")
         << QStringLiteral("--setenv") << QStringLiteral("PATH") << QStringLiteral("/usr/bin:/bin")
         << QStringLiteral("--setenv") << QStringLiteral("HOME") << outputDir
         << QStringLiteral("--chdir") << QStringLiteral("/") << QStringLiteral("--") << cmd.program;
    argv << cmd.arguments;
    return argv;
}

Thumbnailers Thumbnailers::fromDirectories(const QStringList &directories)
{
    Thumbnailers registry;
    QSet<QString> seenFiles;

    for (const QString &directory : directories) {
        const QDir dir(directory);
        const QStringList files =
            dir.entryList({QStringLiteral("*.thumbnailer")}, QDir::Files, QDir::Name);
        for (const QString &fileName : files) {
            if (seenFiles.contains(fileName)) {
                continue;
            }
            seenFiles.insert(fileName);

            QFile file(dir.filePath(fileName));
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            auto entry = parseEntry(file.readAll());
            if (!entry) {
                continue;
            }

            const QStringList probe = expandExec(entry->exec, {}, {}, 0);
            if (probe.isEmpty()) {
                continue;
            }
            const QString tryExec = entry->tryExec.isEmpty() ? probe.first() : entry->tryExec;
            const QString found = tryExec.startsWith(QLatin1Char('/'))
                                      ? (QFileInfo(tryExec).isExecutable() ? tryExec : QString())
                                      : QStandardPaths::findExecutable(tryExec);
            if (found.isEmpty()) {
                continue;
            }
            entry->program = probe.first().startsWith(QLatin1Char('/'))
                                 ? probe.first()
                                 : QStandardPaths::findExecutable(probe.first());
            if (entry->program.isEmpty()) {
                continue;
            }

            for (const QString &mime : std::as_const(entry->mimeTypes)) {
                if (!registry.m_byMime.contains(mime)) {
                    registry.m_byMime.insert(mime, *entry);
                }
            }
        }
    }
    return registry;
}

const ThumbnailerEntry *Thumbnailers::entryFor(const QMimeType &mime) const
{
    if (m_byMime.isEmpty() || !mime.isValid()) {
        return nullptr;
    }
    if (const auto it = m_byMime.constFind(mime.name()); it != m_byMime.constEnd()) {
        return &it.value();
    }
    for (const QString &alias : mime.aliases()) {
        if (const auto it = m_byMime.constFind(alias); it != m_byMime.constEnd()) {
            return &it.value();
        }
    }
    return nullptr;
}

bool Thumbnailers::handles(const QMimeType &mime) const
{
    return entryFor(mime) != nullptr;
}

int Thumbnailers::entryCount() const
{
    return static_cast<int>(m_byMime.size());
}

std::optional<ThumbnailerCommand> Thumbnailers::commandFor(const QMimeType &mime,
                                                           const QString &input,
                                                           const QString &output, int sizePx) const
{
    const ThumbnailerEntry *entry = entryFor(mime);
    if (entry == nullptr) {
        return std::nullopt;
    }
    QStringList argv = expandExec(entry->exec, input, output, sizePx);
    if (argv.isEmpty()) {
        return std::nullopt;
    }
    ThumbnailerCommand command;
    const QString declared = argv.takeFirst();
    command.program = entry->program.isEmpty() ? declared : entry->program;
    command.arguments = argv;
    command.selfSandboxed = isSelfSandboxed(command.program);
    return command;
}

} // namespace pf::platform
