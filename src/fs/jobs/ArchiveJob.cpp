#include "fs/jobs/ArchiveJob.h"

#include "core/Logging.h"
#include "fs/FsError.h"
#include "fs/LibArchive.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <array>
#include <cerrno>

#ifdef PF_HAVE_LIBARCHIVE
// Declarations only; the symbols come from LibArchive's runtime table (§3.4).
#include <archive.h>
#include <archive_entry.h>
#endif

namespace pf::fs {
namespace {

/// Read in 64 KiB chunks, which is the size the transfer job settled on for the
/// same reason: large enough to amortise the syscall, small enough that
/// cancellation is felt immediately.
constexpr qint64 kChunkSize = static_cast<qint64>(64) * 1024;

/// QFileInfo::permissions() returns QFile::Permissions, whose bits are Qt's own
/// (ReadOwner is 0x4000) and are *not* the POSIX mode. Masking them with 0777
/// and storing the result produces an archive whose directories cannot be
/// entered after extraction — which is exactly the bug this exists to avoid.
int posixModeOf(QFile::Permissions permissions)
{
    int mode = 0;
    if ((permissions & QFile::ReadOwner) != 0) {
        mode |= 0400;
    }
    if ((permissions & QFile::WriteOwner) != 0) {
        mode |= 0200;
    }
    if ((permissions & QFile::ExeOwner) != 0) {
        mode |= 0100;
    }
    if ((permissions & QFile::ReadGroup) != 0) {
        mode |= 040;
    }
    if ((permissions & QFile::WriteGroup) != 0) {
        mode |= 020;
    }
    if ((permissions & QFile::ExeGroup) != 0) {
        mode |= 010;
    }
    if ((permissions & QFile::ReadOther) != 0) {
        mode |= 04;
    }
    if ((permissions & QFile::WriteOther) != 0) {
        mode |= 02;
    }
    if ((permissions & QFile::ExeOther) != 0) {
        mode |= 01;
    }
    return mode;
}

} // namespace

QString extensionFor(ArchiveFormat format)
{
    switch (format) {
    case ArchiveFormat::Zip:
        return QStringLiteral(".zip");
    case ArchiveFormat::TarGz:
        return QStringLiteral(".tar.gz");
    case ArchiveFormat::TarZst:
        return QStringLiteral(".tar.zst");
    case ArchiveFormat::SevenZip:
        return QStringLiteral(".7z");
    }
    return QStringLiteral(".zip");
}

QString displayNameFor(ArchiveFormat format)
{
    switch (format) {
    case ArchiveFormat::Zip:
        return QCoreApplication::translate("ArchiveJob", "ZIP archive");
    case ArchiveFormat::TarGz:
        return QCoreApplication::translate("ArchiveJob", "Gzipped tar");
    case ArchiveFormat::TarZst:
        return QCoreApplication::translate("ArchiveJob", "Zstandard tar");
    case ArchiveFormat::SevenZip:
        return QCoreApplication::translate("ArchiveJob", "7z archive");
    }
    return {};
}

#ifdef PF_HAVE_LIBARCHIVE

bool isFormatSupported(ArchiveFormat format)
{
    const LibArchive &api = LibArchive::instance();
    if (!api.loaded) {
        return false;
    }

    // Asked of the library rather than assumed. zstd and 7z are compile-time
    // options in libarchive, so a build that offered them unconditionally would
    // let a user pick a format and then fail at the moment of writing.
    archive *writer = api.writeNew();
    if (writer == nullptr) {
        return false;
    }

    bool supported = false;
    switch (format) {
    case ArchiveFormat::Zip:
        supported = api.writeSetFormat(writer, ARCHIVE_FORMAT_ZIP) == ARCHIVE_OK;
        break;
    case ArchiveFormat::TarGz:
        supported = api.writeSetFormat(writer, ARCHIVE_FORMAT_TAR_PAX_RESTRICTED) == ARCHIVE_OK &&
                    api.writeAddFilter(writer, ARCHIVE_FILTER_GZIP) == ARCHIVE_OK;
        break;
    case ArchiveFormat::TarZst:
        supported = api.writeSetFormat(writer, ARCHIVE_FORMAT_TAR_PAX_RESTRICTED) == ARCHIVE_OK &&
                    api.writeAddFilter(writer, ARCHIVE_FILTER_ZSTD) == ARCHIVE_OK;
        break;
    case ArchiveFormat::SevenZip:
        supported = api.writeSetFormat(writer, ARCHIVE_FORMAT_7ZIP) == ARCHIVE_OK;
        break;
    }

    api.writeFree(writer);
    return supported;
}

#else

bool isFormatSupported(ArchiveFormat)
{
    return false;
}

#endif

ArchiveJob::ArchiveJob(QStringList sources, QString destination, ArchiveFormat format,
                       QObject *parent)
    : Job(parent), m_sources(std::move(sources)), m_destination(std::move(destination)),
      m_format(format)
{}

QString ArchiveJob::description() const
{
    return tr("Creating %1").arg(QFileInfo(m_destination).fileName());
}

QString ArchiveJob::destination() const
{
    return m_destination;
}

void ArchiveJob::collect(const QString &path, const QString &prefix)
{
    const QFileInfo info(path);

    // Symlinks are stored, not followed: following one would put a full copy of
    // its target in the archive, which is not what was selected.
    if (info.isSymLink() || !info.isDir()) {
        m_items.append(Item{.absolutePath = path, .archivePath = prefix});
        ++m_filesTotal;
        m_bytesTotal += static_cast<quint64>(info.isSymLink() ? 0 : info.size());
        return;
    }

    // The directory entry itself, so an empty directory survives the round trip.
    m_items.append(Item{.absolutePath = path, .archivePath = prefix + QLatin1Char('/')});

    QDirIterator iterator(path,
                          QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (iterator.hasNext()) {
        const QString child = iterator.next();
        collect(child, prefix + QLatin1Char('/') + iterator.fileName());
    }
}

bool ArchiveJob::enumerate()
{
    if (!isFormatSupported(m_format)) {
        addError(m_destination,
                 tr("This build cannot write %1 archives").arg(displayNameFor(m_format)));
        return false;
    }

    if (QFileInfo::exists(m_destination)) {
        // Refused rather than overwritten. §7.4's conflict flow is about
        // destinations the user is copying onto; an archive name they typed is
        // a different question, and silently replacing an existing archive is
        // not an answer to it.
        addError(m_destination, tr("“%1” already exists").arg(QFileInfo(m_destination).fileName()));
        return false;
    }

    for (const QString &source : std::as_const(m_sources)) {
        const QFileInfo info(source);
        if (!info.exists() && !info.isSymLink()) {
            addError(source, describeErrno(ENOENT));
            continue;
        }
        collect(source, info.fileName());
    }

    return m_filesTotal > 0;
}

#ifdef PF_HAVE_LIBARCHIVE

void ArchiveJob::execute()
{
    const LibArchive &api = LibArchive::instance();

    archive *writer = api.writeNew();
    if (writer == nullptr) {
        addError(m_destination, tr("Cannot allocate an archive writer"));
        return;
    }

    switch (m_format) {
    case ArchiveFormat::Zip:
        api.writeSetFormat(writer, ARCHIVE_FORMAT_ZIP);
        break;
    case ArchiveFormat::TarGz:
        api.writeSetFormat(writer, ARCHIVE_FORMAT_TAR_PAX_RESTRICTED);
        api.writeAddFilter(writer, ARCHIVE_FILTER_GZIP);
        break;
    case ArchiveFormat::TarZst:
        api.writeSetFormat(writer, ARCHIVE_FORMAT_TAR_PAX_RESTRICTED);
        api.writeAddFilter(writer, ARCHIVE_FILTER_ZSTD);
        break;
    case ArchiveFormat::SevenZip:
        api.writeSetFormat(writer, ARCHIVE_FORMAT_7ZIP);
        break;
    }

    // §7.4's `.pf-partial` convention: a cancelled or crashed archive must not
    // be left looking like a complete one.
    const QString partial = m_destination + QStringLiteral(".pf-partial");

    if (api.writeOpenFilename(writer, QFile::encodeName(partial).constData()) != ARCHIVE_OK) {
        addError(m_destination, QString::fromUtf8(api.errorString(writer)));
        api.writeFree(writer);
        return;
    }

    std::array<char, kChunkSize> buffer{};

    for (const Item &item : std::as_const(m_items)) {
        if (isCancelled()) {
            break;
        }

        const QFileInfo info(item.absolutePath);
        archive_entry *entry = api.entryNew();

        api.entrySetPathname(entry, QFile::encodeName(item.archivePath).constData());
        api.entrySetMtime(entry, info.lastModified().toSecsSinceEpoch(), 0);
        api.entrySetPerm(entry, posixModeOf(info.permissions()));

        if (info.isSymLink()) {
            api.entrySetFiletype(entry, AE_IFLNK);
            api.entrySetSymlink(entry, QFile::encodeName(info.symLinkTarget()).constData());
            api.entrySetSize(entry, 0);
        } else if (info.isDir()) {
            api.entrySetFiletype(entry, AE_IFDIR);
            api.entrySetSize(entry, 0);
        } else {
            api.entrySetFiletype(entry, AE_IFREG);
            api.entrySetSize(entry, info.size());
        }

        if (api.writeHeader(writer, entry) != ARCHIVE_OK) {
            addError(item.absolutePath, QString::fromUtf8(api.errorString(writer)));
            api.entryFree(entry);
            continue;
        }
        api.entryFree(entry);

        if (info.isSymLink() || info.isDir()) {
            ++m_filesDone;
            reportProgress(item.archivePath);
            continue;
        }

        QFile file(item.absolutePath);
        if (!file.open(QIODevice::ReadOnly)) {
            addError(item.absolutePath, describeErrno(errno));
            continue;
        }

        while (!file.atEnd()) {
            if (isCancelled()) {
                break;
            }
            const qint64 read = file.read(buffer.data(), buffer.size());
            if (read <= 0) {
                break;
            }
            api.writeData(writer, buffer.data(), static_cast<size_t>(read));
            m_bytesDone += static_cast<quint64>(read);
            reportProgress(item.archivePath);
        }

        api.writeFinishEntry(writer);
        ++m_filesDone;
    }

    api.writeClose(writer);
    api.writeFree(writer);

    if (isCancelled()) {
        QFile::remove(partial);
        return;
    }

    if (!QFile::rename(partial, m_destination)) {
        addError(m_destination, describeErrno(errno));
        QFile::remove(partial);
    }
}

#else

void ArchiveJob::execute()
{
    addError(m_destination, tr("This build has no archive support"));
}

#endif

} // namespace pf::fs
