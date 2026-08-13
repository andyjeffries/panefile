#pragma once

#include "fs/ArchiveReader.h"
#include "fs/Job.h"

namespace pf::fs {

/// Extracts an archive (§7.10).
///
/// Two rules from §7.10 shape it:
///
///   * "Extracts into `<archive-basename>/` in the panel's cwd. Detect
///     'tarbombs' — if the archive has a single top-level directory, extract
///     directly instead of nesting." Nesting a well-made archive gives
///     `foo/foo/…`, which everyone has undone by hand at some point.
///
///   * "**Guard against path traversal.** Reject any entry whose resolved
///     destination escapes the extraction root. This is not optional." The
///     check is ArchiveReader::isSafeDestination, applied to every entry
///     without exception, and a violation stops the extraction rather than
///     skipping the entry — an archive containing `../../etc/passwd` is hostile,
///     not merely malformed, and the rest of it is not to be trusted either.
class ExtractJob : public Job
{
    Q_OBJECT

public:
    ExtractJob(QString archivePath, QString destinationDirectory, QObject *parent = nullptr);

    QString description() const override;

    /// Where the contents ended up: the destination directory itself for a
    /// tarbomb-free archive, or the single top-level directory inside it.
    QString extractedTo() const;

    /// Chooses the extraction root for a listing (§7.10's tarbomb rule).
    /// Static and pure so §14 can check the rule without an archive on disk.
    static QString rootFor(const QString &archivePath, const QString &destinationDirectory,
                           const ArchiveListing &listing);

protected:
    bool enumerate() override;
    void execute() override;

private:
    QString m_archivePath;
    QString m_destinationDirectory;
    QString m_root;
    ArchiveListing m_listing;
};

} // namespace pf::fs
