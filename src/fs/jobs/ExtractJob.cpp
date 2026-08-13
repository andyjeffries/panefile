#include "fs/jobs/ExtractJob.h"

#include "core/Logging.h"
#include "fs/FsError.h"
#include "fs/LibArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>

#ifdef PF_HAVE_LIBARCHIVE
// Declarations only; the symbols come from LibArchive's runtime table (§3.4).
#include <archive.h>
#include <archive_entry.h>
#endif

namespace pf::fs {
namespace {

/// What archive_write_disk is allowed to restore. Times and permissions yes;
/// owner no, because extracting as root would otherwise hand files to whichever
/// uid the archive names, and an ordinary user cannot do it anyway.
#ifdef PF_HAVE_LIBARCHIVE
constexpr int kExtractFlags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
                              ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS;
#endif

/// The archive's basename with every extension removed: `foo.tar.gz` → `foo`.
QString basenameWithoutExtensions(const QString &archivePath)
{
    QString name = QFileInfo(archivePath).fileName();

    // completeSuffix() would take everything after the first dot, which mangles
    // `my.project.v2.tar.gz` into `my`. Only the known archive extensions are
    // stripped, innermost last.
    static const QStringList suffixes{
        QStringLiteral(".tar.gz"),  QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"),
        QStringLiteral(".tar.zst"), QStringLiteral(".tar.lz4"), QStringLiteral(".tgz"),
        QStringLiteral(".tbz2"),    QStringLiteral(".txz"),     QStringLiteral(".tar"),
        QStringLiteral(".zip"),     QStringLiteral(".7z"),      QStringLiteral(".rar"),
        QStringLiteral(".gz"),      QStringLiteral(".bz2"),     QStringLiteral(".xz"),
        QStringLiteral(".zst")};

    for (const QString &suffix : suffixes) {
        if (name.endsWith(suffix, Qt::CaseInsensitive)) {
            name.chop(suffix.size());
            break;
        }
    }
    return name;
}

} // namespace

ExtractJob::ExtractJob(QString archivePath, QString destinationDirectory, QObject *parent)
    : Job(parent), m_archivePath(std::move(archivePath)),
      m_destinationDirectory(std::move(destinationDirectory))
{}

QString ExtractJob::description() const
{
    return tr("Extracting %1").arg(QFileInfo(m_archivePath).fileName());
}

QString ExtractJob::extractedTo() const
{
    return m_root;
}

QString ExtractJob::rootFor(const QString &archivePath, const QString &destinationDirectory,
                            const ArchiveListing &listing)
{
    // §7.10: "if the archive has a single top-level directory, extract directly
    // instead of nesting". A well-made archive already carries its own folder,
    // and nesting it gives foo/foo/…
    if (!listing.singleTopLevelDirectory().isEmpty()) {
        return destinationDirectory;
    }

    return QDir(destinationDirectory).absoluteFilePath(basenameWithoutExtensions(archivePath));
}

bool ExtractJob::enumerate()
{
    if (!ArchiveReader::isAvailable()) {
        addError(m_archivePath, tr("This build has no archive support"));
        return false;
    }

    // The listing pass is also the safety pass: every entry is checked before a
    // single byte is written, so a hostile archive is rejected rather than
    // half-extracted.
    m_listing = ArchiveReader::list(m_archivePath, std::numeric_limits<int>::max());

    if (!m_listing.error.isEmpty()) {
        addError(m_archivePath, m_listing.error);
        return false;
    }

    if (m_listing.encrypted) {
        // §7.10: "Password-protected archives prompt; if libarchive can't handle
        // the format, fail with a clear message rather than partially
        // extracting." No prompt exists yet, so the clear message is what is
        // owed — and nothing is written.
        addError(m_archivePath, tr("“%1” is encrypted, and Panefile cannot yet ask for a password")
                                    .arg(QFileInfo(m_archivePath).fileName()));
        return false;
    }

    // The destination is canonicalised before the root is derived from it.
    //
    // libarchive's ARCHIVE_EXTRACT_SECURE_SYMLINKS refuses to write through a
    // symlink, which is exactly what protects against an archive that plants
    // one and then writes through it — but it applies to the whole path, and on
    // macOS /tmp and /var are themselves symlinks. Resolving the destination
    // the *user* chose keeps the guard pointed at the archive's own entries,
    // which are the untrusted part.
    const QString canonical = QFileInfo(m_destinationDirectory).canonicalFilePath();
    if (!canonical.isEmpty()) {
        m_destinationDirectory = canonical;
    }

    m_root = rootFor(m_archivePath, m_destinationDirectory, m_listing);

    for (const ArchiveEntry &entry : std::as_const(m_listing.entries)) {
        // §7.10: "Guard against path traversal. … This is not optional."
        if (!ArchiveReader::isSafeDestination(m_root, entry.path)) {
            addError(m_archivePath, tr("“%1” contains an entry that would be written outside the "
                                       "destination (%2), so nothing was extracted")
                                        .arg(QFileInfo(m_archivePath).fileName(), entry.path));
            return false;
        }

        if (!entry.isDirectory) {
            ++m_filesTotal;
            m_bytesTotal += entry.size;
        }
    }

    return m_filesTotal > 0 || !m_listing.entries.isEmpty();
}

#ifdef PF_HAVE_LIBARCHIVE

void ExtractJob::execute()
{
    const LibArchive &api = LibArchive::instance();

    archive *reader = api.readNew();
    archive *writer = api.writeDiskNew();

    if (reader == nullptr || writer == nullptr) {
        addError(m_archivePath, tr("Cannot allocate an archive reader"));
        return;
    }

    api.readSupportFilterAll(reader);
    api.readSupportFormatAll(reader);

    api.writeDiskSetOptions(writer, kExtractFlags);
    api.writeDiskSetStandardLookup(writer);

    constexpr size_t kBlockSize = static_cast<size_t>(64) * 1024;
    if (api.readOpenFilename(reader, QFile::encodeName(m_archivePath).constData(), kBlockSize) !=
        ARCHIVE_OK) {
        addError(m_archivePath, QString::fromUtf8(api.errorString(reader)));
        api.readFree(reader);
        api.writeFree(writer);
        return;
    }

    if (!QDir().mkpath(m_root)) {
        addError(m_root, describeErrno(errno));
        api.readFree(reader);
        api.writeFree(writer);
        return;
    }

    archive_entry *entry = nullptr;

    while (api.readNextHeader(reader, &entry) == ARCHIVE_OK) {
        if (isCancelled()) {
            break;
        }

        const QString relative = QFile::decodeName(api.entryPathname(entry));

        // Checked again here, not only in enumerate(). The listing pass and this
        // one read the archive twice, and an archive on a network filesystem
        // can differ between the two reads; the guard has to hold on the bytes
        // actually being written.
        if (!ArchiveReader::isSafeDestination(m_root, relative)) {
            addError(m_archivePath,
                     tr("Refused to write “%1” outside the destination").arg(relative));
            break;
        }

        const QString target = QDir(m_root).absoluteFilePath(relative);
        api.entrySetPathname(entry, QFile::encodeName(target).constData());

        if (api.writeHeader(writer, entry) != ARCHIVE_OK) {
            addError(relative, QString::fromUtf8(api.errorString(writer)));
            continue;
        }

        const void *block = nullptr;
        size_t size = 0;
        long long offset = 0;

        while (api.readDataBlock(reader, &block, &size, &offset) == ARCHIVE_OK) {
            if (isCancelled()) {
                break;
            }
            api.writeData(writer, block, size);
            m_bytesDone += size;
        }

        api.writeFinishEntry(writer);

        if (api.entryFiletype(entry) != AE_IFDIR) {
            ++m_filesDone;
        }
        reportProgress(relative);
    }

    api.readClose(reader);
    api.readFree(reader);
    api.writeClose(writer);
    api.writeFree(writer);
}

#else

void ExtractJob::execute()
{
    addError(m_archivePath, tr("This build has no archive support"));
}

#endif

} // namespace pf::fs
