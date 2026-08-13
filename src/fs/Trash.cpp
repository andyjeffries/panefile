#include "fs/Trash.h"

#include "core/Logging.h"
#include "fs/FsError.h"
#include "platform/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cerrno>

namespace pf::fs {
namespace {

constexpr QLatin1String kInfoSuffix{".trashinfo"};

/// The XDG spec's date format: ISO 8601 in *local* time, with no zone suffix.
constexpr QLatin1String kDateFormat{"yyyy-MM-ddTHH:mm:ss"};

QString defaultTrashRoot()
{
#ifdef PF_PLATFORM_DARWIN
    // macOS has one trash per volume; ~/.Trash is the one for the boot volume
    // and the only one an application can write to without privileges.
    return platform::homeDir() + QStringLiteral("/.Trash");
#else
    // §7.5: $XDG_DATA_HOME/Trash. platform::stateDir() is
    // $XDG_DATA_HOME/panefile, so the trash is its sibling rather than its
    // child — the trash belongs to the desktop, not to this application.
    return QFileInfo(platform::stateDir()).absolutePath() + QStringLiteral("/Trash");
#endif
}

} // namespace

QString TrashedItem::name() const
{
    return QFileInfo(trashedPath).fileName();
}

Trash::Trash() : m_root(defaultTrashRoot()) {}

Trash::Trash(const QString &root) : m_root(QDir::cleanPath(root)) {}

QString Trash::root() const
{
    return m_root;
}

QString Trash::filesDirectory() const
{
    return m_root + QStringLiteral("/files");
}

QString Trash::infoDirectory() const
{
    return m_root + QStringLiteral("/info");
}

bool Trash::ensureDirectories() const
{
    return QDir().mkpath(filesDirectory()) && QDir().mkpath(infoDirectory());
}

QString Trash::buildTrashInfo(const QString &originalPath, const QDateTime &deletedAt)
{
    // §7.5: "a <name>.trashinfo containing Path= (URL-encoded, absolute) and
    // DeletionDate= in ISO 8601 local time."
    //
    // The path is percent-encoded but the separators are not, which is what the
    // spec asks for and what makes the file readable. Without the encoding, a
    // filename containing a newline would produce a .trashinfo that parses as
    // something else entirely.
    const QByteArray encoded =
        QUrl::toPercentEncoding(originalPath, QByteArrayLiteral("/"), QByteArray());

    return QStringLiteral("[Trash Info]\nPath=%1\nDeletionDate=%2\n")
        .arg(QString::fromLatin1(encoded), deletedAt.toString(kDateFormat));
}

bool Trash::parseTrashInfo(const QString &text, QString *originalPath, QDateTime *deletedAt)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty() || lines.constFirst().trimmed() != QLatin1String("[Trash Info]")) {
        return false;
    }

    bool sawPath = false;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("Path="))) {
            if (originalPath != nullptr) {
                *originalPath = QUrl::fromPercentEncoding(line.mid(5).toUtf8());
            }
            sawPath = true;
        } else if (line.startsWith(QLatin1String("DeletionDate="))) {
            if (deletedAt != nullptr) {
                *deletedAt = QDateTime::fromString(line.mid(13).trimmed(), kDateFormat);
            }
        }
    }

    // A .trashinfo without a Path is useless: the item can be listed but never
    // restored, which is worse than not listing it.
    return sawPath;
}

QString Trash::uniqueTrashName(const QString &fileName) const
{
    const QDir files(filesDirectory());
    if (!files.exists(fileName)) {
        return fileName;
    }

    // §7.5: "On name collision, append -1, -2, … to the trashed name."
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int counter = 1; counter < 100000; ++counter) {
        const QString candidate =
            suffix.isEmpty() ? QStringLiteral("%1-%2").arg(base).arg(counter)
                             : QStringLiteral("%1-%2.%3").arg(base).arg(counter).arg(suffix);
        if (!files.exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

QString Trash::moveToTrash(const QString &path, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        if (error != nullptr) {
            *error = describeErrno(ENOENT);
        }
        return {};
    }

    if (!ensureDirectories()) {
        if (error != nullptr) {
            *error = QObject::tr("Cannot create the trash directory at %1").arg(m_root);
        }
        return {};
    }

    const QString trashName = uniqueTrashName(info.fileName());
    if (trashName.isEmpty()) {
        if (error != nullptr) {
            *error = QObject::tr("Too many trashed files with this name");
        }
        return {};
    }

    QString destination = filesDirectory() + QLatin1Char('/') + trashName;
    const QDateTime now = QDateTime::currentDateTime();

    // The info file is written *first*. An item in files/ with no info file
    // cannot be restored and shows up as an orphan; an info file with no item
    // is merely tidied away on the next listing. Of the two possible halves of
    // an interrupted move, this is the harmless one.
    const QString infoPath = infoDirectory() + QLatin1Char('/') + trashName + kInfoSuffix;
    QFile infoFile(infoPath);
    if (!infoFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error != nullptr) {
            *error = infoFile.errorString();
        }
        return {};
    }
    infoFile.write(buildTrashInfo(info.absoluteFilePath(), now).toUtf8());
    infoFile.close();

    if (!QFile::rename(path, destination)) {
        // A rename across filesystems fails, and §7.5's own answer is that the
        // spec puts a .Trash-$uid at the mount point for exactly this case.
        // Falling back to Qt's implementation is better than implementing
        // mount-point discovery here, and it is what §7.5 asks for first.
        QFile::remove(infoPath);

        QString qtTrashPath;
        if (QFile::moveToTrash(path, &qtTrashPath)) {
            qCDebug(pfJobs) << "trashed via the platform implementation:" << qtTrashPath;
            return qtTrashPath;
        }

        if (error != nullptr) {
            *error = describeErrno(errno, path, QObject::tr("Cannot move"));
        }
        return {};
    }

    return destination;
}

QList<TrashedItem> Trash::list() const
{
    QList<TrashedItem> items;

    const QDir infoDir(infoDirectory());
    if (!infoDir.exists()) {
        return items;
    }

    const QFileInfoList infoFiles =
        infoDir.entryInfoList({QStringLiteral("*") + kInfoSuffix}, QDir::Files);

    for (const QFileInfo &infoFile : infoFiles) {
        QFile file(infoFile.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        TrashedItem item;
        if (!parseTrashInfo(QString::fromUtf8(file.readAll()), &item.originalPath,
                            &item.deletedAt)) {
            continue;
        }

        QString name = infoFile.fileName();
        name.chop(kInfoSuffix.size());
        item.trashedPath = filesDirectory() + QLatin1Char('/') + name;

        const QFileInfo trashedInfo(item.trashedPath);
        // An info file whose item has gone is an orphan — the item was purged
        // by something else, or the move that created it was interrupted.
        // Listing it would offer the user a restore that cannot work.
        if (!trashedInfo.exists() && !trashedInfo.isSymLink()) {
            continue;
        }

        item.size = static_cast<quint64>(trashedInfo.size());
        item.isDirectory = trashedInfo.isDir();
        items.append(item);
    }

    std::ranges::sort(items, [](const TrashedItem &a, const TrashedItem &b) {
        return a.deletedAt > b.deletedAt;
    });

    return items;
}

QString Trash::restore(const TrashedItem &item, QString *error) const
{
    if (item.originalPath.isEmpty()) {
        if (error != nullptr) {
            *error = QObject::tr("This item does not record where it came from");
        }
        return {};
    }

    if (QFileInfo::exists(item.originalPath)) {
        // Deliberately not overwriting. Restoring from the trash is a safety
        // net, and a safety net that destroys the file currently occupying the
        // name is not one.
        if (error != nullptr) {
            *error = QObject::tr("%1 already exists").arg(item.originalPath);
        }
        return {};
    }

    const QString parent = QFileInfo(item.originalPath).absolutePath();
    if (!QDir().mkpath(parent)) {
        if (error != nullptr) {
            *error = QObject::tr("Cannot recreate %1").arg(parent);
        }
        return {};
    }

    if (!QFile::rename(item.trashedPath, item.originalPath)) {
        if (error != nullptr) {
            *error = describeErrno(errno, item.trashedPath, QObject::tr("Cannot restore"));
        }
        return {};
    }

    QFile::remove(infoDirectory() + QLatin1Char('/') + item.name() + kInfoSuffix);
    return item.originalPath;
}

bool Trash::purge(const TrashedItem &item, QString *error) const
{
    const QFileInfo info(item.trashedPath);
    bool ok = false;

    if (info.isDir() && !info.isSymLink()) {
        ok = QDir(item.trashedPath).removeRecursively();
    } else {
        ok = QFile::remove(item.trashedPath);
    }

    if (!ok && error != nullptr) {
        *error = describeErrno(errno, item.trashedPath, QObject::tr("Cannot delete"));
    }

    // The info file goes either way. Leaving it behind for a failed purge would
    // produce an orphan that list() then hides, making the item invisible but
    // still present.
    QFile::remove(infoDirectory() + QLatin1Char('/') + item.name() + kInfoSuffix);
    return ok;
}

int Trash::empty(QStringList *errors) const
{
    int removed = 0;
    const QList<TrashedItem> items = list();

    for (const TrashedItem &item : std::as_const(items)) {
        QString error;
        if (purge(item, &error)) {
            ++removed;
        } else if (errors != nullptr) {
            errors->append(error);
        }
    }
    return removed;
}

} // namespace pf::fs
