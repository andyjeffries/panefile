#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace pf::fs {

/// One entry inside an archive.
struct ArchiveEntry {
    QString path; ///< as stored, which may contain directory separators
    quint64 size = 0;
    quint64 compressedSize = 0;
    QDateTime modified;
    bool isDirectory = false;
    bool isSymlink = false;
    QString linkTarget;
};

struct ArchiveListing {
    QList<ArchiveEntry> entries;
    QString format;      ///< "zip", "tar", …
    QString compression; ///< "gzip", "zstd", …
    quint64 totalSize = 0;
    quint64 archiveSize = 0;
    bool encrypted = false;
    QString error;

    /// The single top-level directory, if there is exactly one and every entry
    /// is inside it.
    ///
    /// §7.10: "Detect 'tarbombs' — if the archive has a single top-level
    /// directory, extract directly instead of nesting."
    QString singleTopLevelDirectory() const;

    bool isValid() const { return error.isEmpty(); }
};

/// Reads archives with libarchive (§7.6, §7.10).
///
/// Shared between Quick Look's archive preview and the extraction job, because
/// both need the same listing and the same safety check, and having two
/// implementations of the second would be dangerous rather than merely
/// wasteful.
class ArchiveReader
{
public:
    /// Lists an archive without extracting anything.
    static ArchiveListing list(const QString &path, int maxEntries = 100000);

    /// Whether an entry's destination stays inside the extraction root.
    ///
    /// §7.10: "Guard against path traversal. Reject any entry whose resolved
    /// destination escapes the extraction root. This is not optional."
    ///
    /// A pure function of two strings, so the fixtures §14 asks for can test it
    /// without writing an archive to disk — and so it can be reasoned about in
    /// isolation, which for a security check is the point.
    static bool isSafeDestination(const QString &extractionRoot, const QString &entryPath);

    /// Whether libarchive is available in this build.
    static bool isAvailable();
};

} // namespace pf::fs
