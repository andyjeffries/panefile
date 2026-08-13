#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace pf::fs {

/// One item in the trash, as the trash browser of §7.5 shows it.
struct TrashedItem {
    QString trashedPath;  ///< where it lives now
    QString originalPath; ///< where it came from
    QDateTime deletedAt;
    quint64 size = 0;
    bool isDirectory = false;

    QString name() const;
};

/// XDG Trash, and its macOS counterpart (§7.5).
///
/// §7.5 says to use QFile::moveToTrash() where it succeeds — it implements the
/// spec including `.Trash-$uid` at the mount point for files on other
/// filesystems — and to fall back to an explicit implementation where it fails.
/// Both are here, along with the trash *browser*, which Qt does not provide at
/// all: restoring an item needs the `.trashinfo` file that records where it
/// came from.
///
/// The trash root is injectable so that the whole implementation — the
/// `.trashinfo` round trip, the URL encoding, the `-1`/`-2` collision suffixes
/// — is testable against a temporary directory rather than against the
/// developer's own trash.
class Trash
{
public:
    /// Constructs a Trash rooted at the user's real trash directory.
    Trash();

    /// Constructs a Trash rooted at `root`, which is created if needed. `root`
    /// holds `files/` and `info/`, as the XDG spec lays out.
    explicit Trash(const QString &root);

    /// Moves a path to the trash.
    ///
    /// Returns the path it now occupies, or empty on failure with `error` set.
    QString moveToTrash(const QString &path, QString *error = nullptr);

    /// Restores an item to where it came from.
    ///
    /// Returns the restored path, or empty with `error` set. A destination that
    /// is occupied is *not* overwritten: the user deleted this file and then
    /// made another with the same name, and silently replacing it would destroy
    /// data on an operation that is supposed to be a safety net.
    QString restore(const TrashedItem &item, QString *error = nullptr) const;

    /// Everything currently in the trash, newest first.
    QList<TrashedItem> list() const;

    /// Deletes one item permanently.
    bool purge(const TrashedItem &item, QString *error = nullptr) const;

    /// Empties the trash. Returns the number of items removed.
    int empty(QStringList *errors = nullptr) const;

    QString root() const;
    QString filesDirectory() const;
    QString infoDirectory() const;

    /// The `.trashinfo` body for a path, per the XDG spec: a `[Trash Info]`
    /// section with a URL-encoded absolute `Path` and an ISO 8601 local
    /// `DeletionDate`. Exposed for testing the round trip.
    static QString buildTrashInfo(const QString &originalPath, const QDateTime &deletedAt);

    /// Parses a `.trashinfo` body. Returns false when it is not one.
    static bool parseTrashInfo(const QString &text, QString *originalPath, QDateTime *deletedAt);

private:
    bool ensureDirectories() const;

    /// A name inside `files/` that is not taken, appending `-1`, `-2`, … as
    /// §7.5 requires.
    QString uniqueTrashName(const QString &fileName) const;

    QString m_root;
};

} // namespace pf::fs
