#include "fs/ArchiveReader.h"

#include "core/Logging.h"
#include "fs/LibArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef PF_HAVE_LIBARCHIVE
// Declarations only: including these links nothing, which is what lets §3.4's
// "must not be a direct link-time dependency" hold while the code is still
// written against libarchive's real types and constants.
#include <archive.h>
#include <archive_entry.h>
#endif

namespace pf::fs {

QString ArchiveListing::singleTopLevelDirectory() const
{
    QString candidate;

    for (const ArchiveEntry &entry : entries) {
        const QString normalised = QDir::cleanPath(entry.path);
        if (normalised.isEmpty() || normalised == QLatin1String(".")) {
            continue;
        }

        const qsizetype slash = normalised.indexOf(QLatin1Char('/'));
        const QString top = slash < 0 ? normalised : normalised.left(slash);

        // An entry at the top level that is not a directory means the archive
        // would spill files into the current directory — the tarbomb §7.10 is
        // about — so there is no single root to extract into.
        if (slash < 0 && !entry.isDirectory) {
            return {};
        }

        if (candidate.isEmpty()) {
            candidate = top;
        } else if (candidate != top) {
            return {};
        }
    }

    return candidate;
}

bool ArchiveReader::isSafeDestination(const QString &extractionRoot, const QString &entryPath)
{
    // An absolute path in an archive is already an escape: extracting it would
    // write wherever it says rather than under the root.
    if (entryPath.startsWith(QLatin1Char('/'))) {
        return false;
    }

    // Windows-style absolute paths and drive letters, which a zip made on
    // Windows can legitimately contain.
    if (entryPath.contains(QLatin1Char('\\')) || entryPath.contains(QLatin1String(":/")) ||
        entryPath.contains(QLatin1String(":\\"))) {
        return false;
    }

    const QString root = QDir::cleanPath(extractionRoot);
    // cleanPath resolves the `..` segments textually, which is exactly the
    // check wanted: `a/../../etc/passwd` becomes `../etc/passwd`, and the
    // prefix test below then rejects it. Resolving against the filesystem
    // instead would follow symlinks the archive itself may have just created.
    const QString resolved = QDir::cleanPath(root + QLatin1Char('/') + entryPath);

    if (resolved == root) {
        return true;
    }
    return resolved.startsWith(root + QLatin1Char('/'));
}

bool ArchiveReader::isAvailable()
{
#ifdef PF_HAVE_LIBARCHIVE
    // Compiled against the headers *and* able to find the library at runtime.
    // Both are needed, and they are genuinely independent: a binary built on a
    // machine with libarchive-dev can be run on one without libarchive at all.
    return LibArchive::instance().loaded;
#else
    return false;
#endif
}

#ifdef PF_HAVE_LIBARCHIVE

ArchiveListing ArchiveReader::list(const QString &path, int maxEntries)
{
    ArchiveListing listing;
    listing.archiveSize = static_cast<quint64>(QFileInfo(path).size());

    const LibArchive &api = LibArchive::instance();
    if (!api.loaded) {
        listing.error = api.error;
        return listing;
    }

    archive *reader = api.readNew();
    if (reader == nullptr) {
        listing.error = QObject::tr("Cannot allocate an archive reader");
        return listing;
    }

    // Every format and filter libarchive knows. Narrowing this to the
    // extensions we recognise would mean an archive named wrongly — which is
    // most downloads — failing to open for no good reason.
    api.readSupportFilterAll(reader);
    api.readSupportFormatAll(reader);

    constexpr size_t kBlockSize = static_cast<size_t>(64) * 1024;
    if (api.readOpenFilename(reader, QFile::encodeName(path).constData(), kBlockSize) !=
        ARCHIVE_OK) {
        listing.error = QString::fromUtf8(api.errorString(reader));
        api.readFree(reader);
        return listing;
    }

    archive_entry *entry = nullptr;
    int count = 0;

    while (api.readNextHeader(reader, &entry) == ARCHIVE_OK) {
        if (count++ >= maxEntries) {
            // A listing is for looking at. An archive with a million entries is
            // not something to render, and reading them all to say so would
            // take longer than the user will wait.
            break;
        }

        ArchiveEntry item;
        item.path = QFile::decodeName(api.entryPathname(entry));
        item.size = static_cast<quint64>(api.entrySize(entry));
        item.modified = QDateTime::fromSecsSinceEpoch(api.entryMtime(entry));
        item.isDirectory = api.entryFiletype(entry) == AE_IFDIR;
        item.isSymlink = api.entryFiletype(entry) == AE_IFLNK;
        if (item.isSymlink) {
            const char *target = api.entrySymlink(entry);
            if (target != nullptr) {
                item.linkTarget = QFile::decodeName(target);
            }
        }

        // Trailing separator rather than the filetype for formats that do not
        // record one: a zip made by some tools marks directories only this way.
        if (!item.isDirectory && item.path.endsWith(QLatin1Char('/'))) {
            item.isDirectory = true;
        }

        if (api.entryIsEncrypted(entry) != 0) {
            listing.encrypted = true;
        }

        listing.totalSize += item.size;
        listing.entries.append(item);

        api.readDataSkip(reader);
    }

    const char *formatName = api.formatName(reader);
    const char *filterName = api.filterName(reader, 0);
    listing.format = formatName != nullptr ? QString::fromUtf8(formatName) : QString();
    listing.compression = filterName != nullptr ? QString::fromUtf8(filterName) : QString();

    if (listing.entries.isEmpty() && api.errnoOf(reader) != 0) {
        listing.error = QString::fromUtf8(api.errorString(reader));
    }

    api.readClose(reader);
    api.readFree(reader);
    return listing;
}

#else

ArchiveListing ArchiveReader::list(const QString &path, int maxEntries)
{
    Q_UNUSED(path)
    Q_UNUSED(maxEntries)

    ArchiveListing listing;
    listing.error = QObject::tr("This build has no archive support");
    return listing;
}

#endif

} // namespace pf::fs
