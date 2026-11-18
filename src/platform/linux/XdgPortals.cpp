#include "platform/linux/XdgPortals.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace pf::platform::xdg {
namespace {

const QByteArray kGroup = "preferred";
const QByteArray kKey = QByteArray(kFileChooserInterface);
const QByteArray kReplacedPrefix = "# Panefile replaced: ";

QByteArray stripCr(const QByteArray &line)
{
    return line.endsWith('\r') ? line.chopped(1) : line;
}

QByteArray groupOf(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.size() >= 2 && trimmed.startsWith('[') && trimmed.endsWith(']')) {
        return trimmed.mid(1, trimmed.size() - 2);
    }
    return {};
}

QByteArray keyOf(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith('[')) {
        return {};
    }
    const qsizetype equals = trimmed.indexOf('=');
    return equals <= 0 ? QByteArray() : trimmed.left(equals).trimmed();
}

QStringList listOf(const QByteArray &line)
{
    QStringList items;
    for (const QByteArray &item : stripCr(line.mid(line.indexOf('=') + 1)).split(';')) {
        if (!item.trimmed().isEmpty()) {
            items << QString::fromUtf8(item.trimmed());
        }
    }
    return items;
}

/// Where the lines of `[preferred]` are, and the two that matter in it.
struct PreferredGroup {
    qsizetype begin = -1; ///< index of the `[preferred]` header
    qsizetype end = -1;   ///< one past its last line
    qsizetype chooser = -1;
    qsizetype fallback = -1; ///< the `default=` line
};

PreferredGroup findPreferred(const QList<QByteArray> &lines)
{
    PreferredGroup found;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (const QByteArray group = groupOf(lines.at(i)); !group.isNull()) {
            if (found.begin >= 0) {
                found.end = i;
                break;
            }
            if (group == kGroup) {
                found.begin = i;
            }
            continue;
        }
        if (found.begin < 0) {
            continue;
        }
        const QByteArray key = keyOf(lines.at(i));
        if (key == kKey) {
            found.chooser = i;
        } else if (key == "default") {
            found.fallback = i;
        }
    }
    if (found.begin >= 0 && found.end < 0) {
        found.end = lines.size();
    }
    return found;
}

QList<QByteArray> split(const QByteArray &content)
{
    return content.isEmpty() ? QList<QByteArray>() : content.split('\n');
}

QByteArray join(const QList<QByteArray> &lines)
{
    return lines.join('\n');
}

} // namespace

QStringList portalConfigSearchPaths(const Environment &env)
{
    QStringList directories{env.configHome};
    directories << env.configDirs << QStringLiteral("/etc") << env.dataHome << env.dataDirs
                << QStringLiteral("/usr/share");

    QStringList paths;
    for (const QString &directory : std::as_const(directories)) {
        const QString base = directory + QStringLiteral("/xdg-desktop-portal/");
        for (const QString &desktop : env.desktops) {
            paths << base + desktop + QStringLiteral("-portals.conf");
        }
        paths << base + QStringLiteral("portals.conf");
    }
    paths.removeDuplicates();
    return paths;
}

QString effectivePortalConfig(const Environment &env)
{
    for (const QString &path : portalConfigSearchPaths(env)) {
        if (QFileInfo(path).isFile()) {
            return path;
        }
    }
    return {};
}

bool portalImplements(const Environment &env, const QString &backend, const char *interface)
{
    QStringList directories{env.dataHome};
    directories << env.dataDirs << QStringLiteral("/usr/share");
    for (const QString &directory : std::as_const(directories)) {
        const QString path = directory + QStringLiteral("/xdg-desktop-portal/portals/") + backend +
                             QStringLiteral(".portal");
        if (!QFileInfo(path).isFile()) {
            continue;
        }
        bool inPortal = false;
        for (const QByteArray &raw : split(readFile(path))) {
            const QByteArray line = stripCr(raw).trimmed();
            if (line.startsWith('[')) {
                inPortal = line == "[portal]";
            } else if (inPortal && line.startsWith("Interfaces")) {
                const QByteArray value = line.mid(line.indexOf('=') + 1);
                for (const QByteArray &name : value.split(';')) {
                    if (name.trimmed() == interface) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
    return false;
}

bool isRedundantUserConfig(const Environment &env, const QString &path, const QByteArray &content)
{
    const QFileInfo info(path);
    if (!path.startsWith(env.configHome + QLatin1Char('/')) || info.isSymLink() || !info.isFile()) {
        return false;
    }
    for (const QString &other : portalConfigSearchPaths(env)) {
        if (other != path && QFileInfo(other).isFile()) {
            return readFile(other) == content;
        }
    }
    return content == "[preferred]\n";
}

PortalConfigTarget userPortalConfigTarget(const Environment &env)
{
    const QString effective = effectivePortalConfig(env);
    if (effective.isEmpty()) {
        return {.path = env.configHome + QStringLiteral("/xdg-desktop-portal/portals.conf"),
                .content = "[preferred]\n"};
    }

    const QByteArray content = readFile(effective);
    if (effective.startsWith(env.configHome + QLatin1Char('/'))) {
        return {.path = effective, .content = content};
    }
    return {.path = env.configHome + QStringLiteral("/xdg-desktop-portal/") +
                    QFileInfo(effective).fileName(),
            .content = content};
}

QStringList fileChooserBackends(const QByteArray &content)
{
    const QList<QByteArray> lines = split(content);
    const PreferredGroup group = findPreferred(lines);
    if (group.chooser >= 0) {
        return listOf(lines.at(group.chooser));
    }
    return group.fallback >= 0 ? listOf(lines.at(group.fallback)) : QStringList();
}

QByteArray withFileChooser(const QByteArray &content)
{
    QList<QByteArray> lines = split(content);
    const bool crlf = content.contains("\r\n");
    const QByteArray eol = crlf ? "\r" : "";
    PreferredGroup group = findPreferred(lines);

    QStringList backends;
    QByteArray replaced;
    if (group.chooser >= 0) {
        backends = listOf(lines.at(group.chooser));
        replaced = stripCr(lines.at(group.chooser)).trimmed();
    } else if (group.fallback >= 0) {
        backends = listOf(lines.at(group.fallback));
    }
    if (!backends.isEmpty() && backends.first() == QLatin1String(kPortalBackendName)) {
        return content;
    }

    backends.removeAll(QLatin1String(kPortalBackendName));
    backends.removeDuplicates();
    backends.prepend(QLatin1String(kPortalBackendName));
    QByteArray value;
    for (const QString &backend : std::as_const(backends)) {
        value += backend.toUtf8() + ';';
    }
    const QByteArray line = kKey + '=' + value + eol;

    if (group.chooser >= 0) {
        lines.removeAt(group.chooser);
        lines.insert(group.chooser, line);
        lines.insert(group.chooser, kReplacedPrefix + replaced + eol);
        return join(lines);
    }

    if (group.begin >= 0) {
        qsizetype at = group.begin + 1;
        for (qsizetype i = group.begin + 1; i < group.end; ++i) {
            if (!stripCr(lines.at(i)).trimmed().isEmpty()) {
                at = i + 1;
            }
        }
        lines.insert(at, line);
        return join(lines);
    }

    const bool endsWithNewline = !lines.isEmpty() && lines.constLast().isEmpty();
    if (endsWithNewline) {
        lines.removeLast();
    }
    if (!lines.isEmpty() && !stripCr(lines.constLast()).trimmed().isEmpty()) {
        lines.append(eol);
    }
    lines.append('[' + kGroup + ']' + eol);
    lines.append(line);
    lines.append(QByteArray());
    return join(lines);
}

QByteArray withoutFileChooser(const QByteArray &content)
{
    QList<QByteArray> lines = split(content);
    const PreferredGroup group = findPreferred(lines);
    if (group.chooser < 0) {
        return content;
    }
    const QStringList backends = listOf(lines.at(group.chooser));
    if (backends.isEmpty() || backends.first() != QLatin1String(kPortalBackendName)) {
        return content;
    }

    const qsizetype before = group.chooser - 1;
    if (before > group.begin && stripCr(lines.at(before)).startsWith(kReplacedPrefix)) {
        const bool cr = lines.at(before).endsWith('\r');
        lines[group.chooser] =
            stripCr(lines.at(before)).mid(kReplacedPrefix.size()) + (cr ? "\r" : "");
        lines.removeAt(before);
    } else {
        lines.removeAt(group.chooser);
    }
    return join(lines);
}

QString portalFilePath(const Environment &env)
{
    return env.dataHome + QStringLiteral("/xdg-desktop-portal/portals/") +
           QLatin1String(kPortalBackendName) + QStringLiteral(".portal");
}

QByteArray portalFileContents()
{
    return QStringLiteral("[portal]\n"
                          "DBusName=%1\n"
                          "Interfaces=%2;\n")
        .arg(QLatin1String(kPortalBusName), QLatin1String(kFileChooserInterface))
        .toUtf8();
}

QString portalServiceFilePath(const Environment &env)
{
    return dbusServiceFilePath(env, QLatin1String(kPortalBusName));
}

QByteArray portalServiceFileContents(const QString &executable)
{
    return dbusServiceFileContents(
        QLatin1String(kPortalBusName), executable, QStringLiteral("--portal"),
        QStringLiteral(
            "# Written by Panefile when it was made the Open and Save dialog.\n"
            "# It does nothing unless portals.conf names panefile for the FileChooser.\n"));
}

WriteResult writePortalServiceFile(const Environment &env, const QString &executable)
{
    return writeDbusServiceFile(portalServiceFilePath(env), portalServiceFileContents(executable),
                                executable);
}

} // namespace pf::platform::xdg
