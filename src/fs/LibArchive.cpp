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
