#pragma once

#include "fs/Job.h"

namespace pf::fs {

/// The formats §7.10 offers.
enum class ArchiveFormat {
    Zip,
    TarGz,
    TarZst,
    SevenZip,
};

/// The conventional extension for a format, e.g. `.tar.gz`.
QString extensionFor(ArchiveFormat format);

/// The name shown in the create modal.
QString displayNameFor(ArchiveFormat format);

/// Whether this build's libarchive can write the format. 7z in particular is a
/// compile-time option in libarchive and is genuinely absent on some
/// distributions, so §7.10's "`7z` if supported" is a runtime question.
bool isFormatSupported(ArchiveFormat format);

/// Creates an archive from a set of paths (§7.10).
///
/// Symlinks are stored as symlinks rather than followed, for the same reason
/// §7.4 gives for copies: following them turns one link into a full copy of
/// whatever it pointed at, which is not what the user selected.
class ArchiveJob : public Job
{
    Q_OBJECT

public:
    ArchiveJob(QStringList sources, QString destination, ArchiveFormat format,
               QObject *parent = nullptr);

    QString description() const override;

    /// The archive that was written, so the caller can put the cursor on it.
    QString destination() const;

protected:
    bool enumerate() override;
    void execute() override;

private:
    /// Every file to store, paired with the path it takes inside the archive.
    struct Item {
        QString absolutePath;
        QString archivePath;
    };

    void collect(const QString &path, const QString &prefix);

    QStringList m_sources;
    QString m_destination;
    ArchiveFormat m_format;
    QList<Item> m_items;
};

} // namespace pf::fs
