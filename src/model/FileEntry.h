#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <sys/types.h>

namespace pf {

/// One directory entry.
///
/// A cheap, copyable value type (§4.1). Entries are produced on a scanner
/// thread and delivered to the model by queued signal, so this must stay
/// copyable and free of anything thread-affine — no QObject, no QIcon, no
/// pointer back into the model.
///
/// §4.1 is explicit that QFileSystemModel is not used: it is tree-shaped, does
/// its own watching, hits the filesystem on the GUI thread, and gives no
/// control over batching. A flat per-directory model of these is both simpler
/// and faster.
struct FileEntry {
    QString name; ///< basename only; the panel knows the directory

    quint64 size = 0;
    QDateTime modified;

    mode_t mode = 0;
    uid_t uid = 0;
    gid_t gid = 0;

    bool isDir = false; ///< resolved through symlinks
    bool isSymlink = false;
    bool isBroken = false; ///< dangling symlink: lstat succeeded, stat did not
    bool isHidden = false; ///< leading dot
    bool isExecutable = false;

    QString linkTarget; ///< empty unless isSymlink

    /// Filled lazily, and only for entries that become visible (§4.3).
    /// Empty means "not resolved yet", not "no type".
    QString mimeName;

    /// True when the entry could not be stat()ed at all. Such an entry is still
    /// listed — a directory you cannot stat is still a directory you can see —
    /// but its size, mode and timestamps are meaningless.
    bool statFailed = false;
};

} // namespace pf

Q_DECLARE_METATYPE(pf::FileEntry)
