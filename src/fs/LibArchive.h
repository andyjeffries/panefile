#pragma once

#include <QString>

struct archive;
struct archive_entry;

namespace pf::fs {

/// libarchive, resolved at runtime rather than linked (§3.4).
///
/// §3.4 is explicit: "Optional heavy dependencies (KSyntaxHighlighting,
/// QtMultimedia, QtPdf, poppler, libffmpegthumbnailer) must **not** be direct
/// link-time dependencies of the main binary… A `DT_NEEDED` entry costs
/// relocation and page-in time at every launch whether or not the code is
/// called." §3.4's lazy list names libarchive alongside them.
///
/// So the headers are used at compile time — they are declarations, and
/// including them links nothing — and the functions are resolved from the
/// shared library on the first archive the user actually looks at. A build on a
/// machine without libarchive still runs; archives simply report that this
/// installation cannot read them, which §2 already requires of every optional
/// dependency.
///
/// Only the symbols Panefile uses are resolved. Adding one means adding it
/// here, which is the point: the surface stays visible and small.
struct LibArchive {
    // ------------------------------------------------------------- reading
    archive *(*readNew)() = nullptr;
    int (*readSupportFilterAll)(archive *) = nullptr;
    int (*readSupportFormatAll)(archive *) = nullptr;
    int (*readOpenFilename)(archive *, const char *, size_t) = nullptr;
    int (*readNextHeader)(archive *, archive_entry **) = nullptr;
    int (*readDataSkip)(archive *) = nullptr;
    int (*readClose)(archive *) = nullptr;
    int (*readFree)(archive *) = nullptr;

    // ---------------------------------------------------------- diagnostics
    const char *(*errorString)(archive *) = nullptr;
    int (*errnoOf)(archive *) = nullptr;
    const char *(*formatName)(archive *) = nullptr;
    const char *(*filterName)(archive *, int) = nullptr;

    // -------------------------------------------------------------- entries
    const char *(*entryPathname)(archive_entry *) = nullptr;
    qint64 (*entrySize)(archive_entry *) = nullptr;
    qint64 (*entryMtime)(archive_entry *) = nullptr;
    unsigned int (*entryFiletype)(archive_entry *) = nullptr;
    const char *(*entrySymlink)(archive_entry *) = nullptr;
    int (*entryIsEncrypted)(archive_entry *) = nullptr;

    /// True when every symbol above resolved.
    bool loaded = false;

    /// Why loading failed, for the message a failed listing carries.
    QString error;

    /// Loads on first call, then returns the same table. Thread-safe by way of
    /// a function-local static, which §3.4 also asks for — a namespace-scope
    /// object here would be a non-trivial static initialiser.
    static const LibArchive &instance();
};

} // namespace pf::fs
