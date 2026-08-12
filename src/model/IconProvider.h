#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QString>

namespace pf {

struct FileEntry;

/// Resolves and caches the icon for a directory entry (§4.3).
///
/// Two costs are being managed here, and they are different costs:
///
///   * QMimeDatabase's first call populates the shared-mime-info caches. That
///     is unavoidable, but it must not happen at startup (§3.4), so the scanner
///     warms it on a worker thread and nothing here touches it until a row is
///     about to be painted.
///
///   * QIcon::fromTheme walks the icon theme's directory list. Doing that once
///     per row in a 100,000-entry directory would dominate scrolling, hence the
///     process-wide cache keyed on icon name that §4.3 asks for. Entries
///     sharing a MIME type — which in a source tree is most of them — resolve
///     to the same QIcon, and QIcon is itself implicitly shared.
///
/// §4.3 also calls for a bundled fallback set. On Linux that is a safety net
/// for an incomplete icon theme; on macOS, where there is no freedesktop icon
/// theme at all, it is the entire icon set. The bundled glyphs are monochrome
/// and tinted to the entry's own colour, so one file covers every kind and the
/// icon agrees with the colour the delegate paints the name in.
class IconProvider
{
public:
    /// Broad visual kinds. Coarser than MIME on purpose: the icon column is
    /// glanced at, so the useful distinction is "archive or image or code", not
    /// "gzip versus zstd".
    enum class Kind {
        Directory,
        Archive,
        Image,
        Executable,
        Symlink,
        Code,
        Text,
        Generic,
    };

    static IconProvider &instance();

    /// The icon for an entry: the desktop icon theme where there is one,
    /// otherwise the bundled glyph tinted to `tint`.
    QIcon iconFor(const FileEntry &entry, const QColor &tint);

    /// The visual kind of an entry, from its name and stat flags only — no MIME
    /// lookup, because this runs once per painted row.
    static Kind kindOf(const FileEntry &entry);

    /// The MIME type name for an entry, resolved by extension only.
    ///
    /// §4.3: content sniffing is disabled by default because it means opening
    /// and reading every file in the directory. Extensionless files are the one
    /// case where the extension cannot answer, and they are sniffed lazily —
    /// only once the entry becomes visible.
    static QString mimeNameFor(const QString &directory, const FileEntry &entry);

    /// Drops the cached icons. Called when the icon theme or the theme colours
    /// change, since the tint is baked into the cached pixmaps.
    void clear();

private:
    IconProvider() = default;

    QIcon bundledIcon(Kind kind, const QColor &tint);

    QHash<QString, QIcon> m_themeCache;
    QHash<QString, QIcon> m_bundledCache;
};

} // namespace pf
