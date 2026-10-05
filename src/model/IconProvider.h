#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QLatin1String>
#include <QString>

namespace pf {

struct FileEntry;

/// Resolves and caches the icon for a directory entry (§4.3).
///
/// Every platform uses the bundled set in data/icons/files, compiled in as
/// `:/icons/files/<name>.svg`. The desktop's icon theme is deliberately not
/// consulted. §4.3 originally asked for QIcon::fromTheme with the bundled set
/// as a fallback, and in practice that gave a listing whose icons depended on
/// whether Qt had found a platform theme: when it had, the rows bypassed the
/// tint, the unfocused-panel dimming and the white-on-pill treatment — all of
/// which need a glyph that is one colour — and sat in a different visual style
/// from everything else in the window. One set drawn for the job, the same on
/// Linux and macOS, is what lets the delegate's colour rules always apply.
///
/// Two costs are managed here:
///
///   * Classification runs once per painted row, so it is a hash lookup on the
///     suffix — never a QMimeDatabase query, whose first call populates the
///     shared-mime-info caches and which §3.4 keeps off the GUI thread. A MIME
///     type is only consulted when the entry already carries one.
///
///   * Rendering is TintedIcon's, which renders each glyph at the device size
///     it is painted at and keeps the pixmaps in QPixmapCache. The QIcons here
///     are only engines, cached per (kind, link badge, tint) so that a
///     directory of a thousand source files shares one.
class IconProvider
{
public:
    /// Broad visual kinds. Coarser than MIME on purpose: the icon column is
    /// glanced at, so the useful distinction is "archive or image or code", not
    /// "gzip versus zstd".
    ///
    /// A symlink is not a kind. It is drawn as what it points at, with a link
    /// badge — a link to a folder is still something you open like a folder.
    enum class Kind {
        Directory,
        Generic,
        Text,
        Code,
        Markdown,
        Image,
        Audio,
        Video,
        Pdf,
        Document,
        Spreadsheet,
        Presentation,
        Archive,
        DiskImage,
        Package,
        Executable,
        Font,
        Web,
        Database,
        Config,
    };

    /// Every kind, for anything that has to cover them all (tests, previews).
    static constexpr Kind kAllKinds[] = {
        Kind::Directory,   Kind::Generic,      Kind::Text,    Kind::Code,      Kind::Markdown,
        Kind::Image,       Kind::Audio,        Kind::Video,   Kind::Pdf,       Kind::Document,
        Kind::Spreadsheet, Kind::Presentation, Kind::Archive, Kind::DiskImage, Kind::Package,
        Kind::Executable,  Kind::Font,         Kind::Web,     Kind::Database,  Kind::Config,
    };

    static IconProvider &instance();

    /// The bundled icon for an entry, tinted to `tint`, with a link badge when
    /// the entry is a symlink. Never null for a valid build.
    QIcon iconFor(const FileEntry &entry, const QColor &tint);

    /// The icon for a kind, without an entry. `linked` adds the symlink badge.
    QIcon iconFor(Kind kind, const QColor &tint, bool linked = false);

    /// The visual kind of an entry: from its name, its stat flags and — only
    /// when the entry already carries one — its MIME type. Never opens the file
    /// and never queries the MIME database, because this runs once per painted
    /// row. A symlink is classified as its target (FileEntry::isDir already
    /// follows links).
    ///
    /// Public so that the delegate can colour rows by the same classification
    /// the icon uses.
    static Kind kindOf(const FileEntry &entry);

    /// The kind a file name alone implies — whole-name matches such as
    /// "Makefile", then the suffix. Generic when the name says nothing.
    static Kind kindForName(const QString &name);

    /// The kind for a MIME type name, e.g. "image/png". Generic when nothing
    /// more specific applies.
    static Kind kindForMimeType(const QString &mimeName);

    /// The bundled file stem for a kind, e.g. "folder" for Directory.
    static QLatin1String iconNameFor(Kind kind);

    /// The MIME type name for an entry, resolved by extension only.
    ///
    /// §4.3: content sniffing is disabled by default because it means opening
    /// and reading every file in the directory. Extensionless files are the one
    /// case where the extension cannot answer, and they are sniffed lazily —
    /// only once the entry becomes visible.
    static QString mimeNameFor(const QString &directory, const FileEntry &entry);

    /// Drops the cached icons. The tint is part of each cache key, so this is
    /// never needed for correctness; it only releases engines for tints a
    /// replaced theme will not ask for again.
    void clear();

private:
    IconProvider() = default;

    QHash<QString, QIcon> m_cache;
};

} // namespace pf
