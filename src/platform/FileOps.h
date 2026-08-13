#pragma once

#include <QString>

#include <sys/types.h>

#include <functional>

namespace pf::platform {

/// Outcome of an accelerated copy attempt.
enum class CopyAcceleration {
    /// The whole file was copied by the kernel — a reflink, a server-side copy,
    /// or an APFS clone. Nothing further is needed.
    Complete,
    /// The platform has no acceleration for this pair of files, or the two are
    /// on different filesystems. The caller falls back to a read/write loop.
    Unsupported,
    /// Acceleration was attempted and failed for a real reason. The caller
    /// should report it rather than silently falling back, since the same error
    /// will very likely recur on the buffered path.
    Failed,
};

/// Attempts a kernel-side copy of an entire file (§7.4).
///
/// "Copies use copy_file_range(2) where available (enables server-side/reflink
/// copies on btrfs and XFS), falling back to a 1 MiB buffered read/write loop."
/// The macOS equivalent is clonefile/fcopyfile, which on APFS produces a
/// copy-on-write clone in constant time regardless of file size.
///
/// Both descriptors must be open and positioned at zero. On Complete, the
/// destination holds `size` bytes. `progress` is called as bytes are copied
/// where the platform reports partial progress, and returns false to cancel.
CopyAcceleration copyFileAccelerated(int sourceFd, int destinationFd, quint64 size,
                                     const std::function<bool(quint64)> &progress);

/// Copies extended attributes from one file to another, best effort (§7.4).
///
/// "Preserve mode, mtime, and — best-effort, never fatal — ownership and
/// extended attributes." Failure is expected and normal: the destination
/// filesystem may not support xattrs at all, and a copy that aborted because a
/// Finder tag could not be carried across would be absurd.
///
/// Returns the number of attributes copied.
int copyExtendedAttributes(const QString &sourcePath, const QString &destinationPath);

/// True when the two paths are on the same filesystem, so rename(2) will work
/// and a move is instant (§7.4: "Detect by comparing st_dev").
bool onSameFilesystem(const QString &first, const QString &second);

/// The device id of a path's filesystem, or 0 if it cannot be determined.
dev_t deviceIdOf(const QString &path);

} // namespace pf::platform
