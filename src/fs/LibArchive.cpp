#include "fs/LibArchive.h"

#include "core/Logging.h"

#include <QLibrary>
#include <QStringList>

namespace pf::fs {
namespace {

/// Candidates in the order they should be tried: the versioned soname first,
/// because that is what a distribution ships and what the runtime linker would
/// have picked had this been a link-time dependency. The bare name is the
/// fallback for a machine that has the development package installed.
QStringList candidateNames()
{
#ifdef Q_OS_DARWIN
    return {QStringLiteral("libarchive.13"), QStringLiteral("libarchive"),
            QStringLiteral("/opt/homebrew/opt/libarchive/lib/libarchive"),
            QStringLiteral("/usr/local/opt/libarchive/lib/libarchive")};
#else
    return {QStringLiteral("libarchive.so.13"), QStringLiteral("libarchive.so"),
            QStringLiteral("libarchive")};
#endif
}

/// Resolves one symbol, recording the first failure. Templated on the pointer
/// type so each call site keeps its own signature and no cast is written by
/// hand — a mistyped cast here would be a crash at the first archive rather
/// than a compile error.
template<typename Fn>
bool resolve(QLibrary &library, Fn &target, const char *name, QString &error)
{
    target = reinterpret_cast<Fn>(library.resolve(name));
    if (target == nullptr && error.isEmpty()) {
        error = QStringLiteral("libarchive is missing %1").arg(QLatin1String(name));
    }
    return target != nullptr;
}

LibArchive load()
{
    LibArchive table;

    // Function-local rather than a member: the QLibrary has to outlive the
    // resolved pointers, and a static here is the whole lifetime of the
    // process, which is exactly right for a library that is never unloaded.
    static QLibrary library;

    bool opened = false;
    for (const QString &name : candidateNames()) {
        library.setFileName(name);
        if (library.load()) {
            opened = true;
            break;
        }
    }

    if (!opened) {
        table.error = QStringLiteral("libarchive is not installed");
        qCDebug(pfFs) << "libarchive not found:" << library.errorString();
        return table;
    }

    bool ok = true;
    ok &= resolve(library, table.readNew, "archive_read_new", table.error);
    ok &= resolve(library, table.readSupportFilterAll, "archive_read_support_filter_all",
                  table.error);
    ok &= resolve(library, table.readSupportFormatAll, "archive_read_support_format_all",
                  table.error);
    ok &= resolve(library, table.readOpenFilename, "archive_read_open_filename", table.error);
    ok &= resolve(library, table.readNextHeader, "archive_read_next_header", table.error);
    ok &= resolve(library, table.readDataSkip, "archive_read_data_skip", table.error);
    ok &= resolve(library, table.readClose, "archive_read_close", table.error);
    ok &= resolve(library, table.readFree, "archive_read_free", table.error);

    ok &= resolve(library, table.readDataBlock, "archive_read_data_block", table.error);

    ok &= resolve(library, table.writeNew, "archive_write_new", table.error);
    ok &= resolve(library, table.writeSetFormatFilterByExt,
                  "archive_write_set_format_filter_by_ext", table.error);
    ok &= resolve(library, table.writeSetFormat, "archive_write_set_format", table.error);
    ok &= resolve(library, table.writeAddFilter, "archive_write_add_filter", table.error);
    ok &= resolve(library, table.writeOpenFilename, "archive_write_open_filename", table.error);
    ok &= resolve(library, table.writeHeader, "archive_write_header", table.error);
    ok &= resolve(library, table.writeData, "archive_write_data", table.error);
    ok &= resolve(library, table.writeFinishEntry, "archive_write_finish_entry", table.error);
    ok &= resolve(library, table.writeClose, "archive_write_close", table.error);
    ok &= resolve(library, table.writeFree, "archive_write_free", table.error);

    ok &= resolve(library, table.writeDiskNew, "archive_write_disk_new", table.error);
    ok &=
        resolve(library, table.writeDiskSetOptions, "archive_write_disk_set_options", table.error);
    ok &= resolve(library, table.writeDiskSetStandardLookup,
                  "archive_write_disk_set_standard_lookup", table.error);

    ok &= resolve(library, table.entryNew, "archive_entry_new", table.error);
    ok &= resolve(library, table.entryFree, "archive_entry_free", table.error);
    ok &= resolve(library, table.entrySetPathname, "archive_entry_set_pathname", table.error);
    ok &= resolve(library, table.entrySetSize, "archive_entry_set_size", table.error);
    ok &= resolve(library, table.entrySetFiletype, "archive_entry_set_filetype", table.error);
    ok &= resolve(library, table.entrySetPerm, "archive_entry_set_perm", table.error);
    ok &= resolve(library, table.entrySetMtime, "archive_entry_set_mtime", table.error);
    ok &= resolve(library, table.entrySetSymlink, "archive_entry_set_symlink", table.error);

    ok &= resolve(library, table.errorString, "archive_error_string", table.error);
    ok &= resolve(library, table.errnoOf, "archive_errno", table.error);
    ok &= resolve(library, table.formatName, "archive_format_name", table.error);
    ok &= resolve(library, table.filterName, "archive_filter_name", table.error);

    ok &= resolve(library, table.entryPathname, "archive_entry_pathname", table.error);
    ok &= resolve(library, table.entrySize, "archive_entry_size", table.error);
    ok &= resolve(library, table.entryMtime, "archive_entry_mtime", table.error);
    ok &= resolve(library, table.entryFiletype, "archive_entry_filetype", table.error);
    ok &= resolve(library, table.entrySymlink, "archive_entry_symlink", table.error);
    ok &= resolve(library, table.entryIsEncrypted, "archive_entry_is_encrypted", table.error);

    table.loaded = ok;
    if (ok) {
        qCDebug(pfFs) << "libarchive loaded from" << library.fileName();
    }
    return table;
}

} // namespace

const LibArchive &LibArchive::instance()
{
    static const LibArchive table = load();
    return table;
}

} // namespace pf::fs
