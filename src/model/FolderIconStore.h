#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

namespace pf {

/// The glyphs the user has put on individual folders, and the lookup that
/// decides what a folder shows.
///
/// A glyph id names a source and an icon in it — `hero:rocket-launch` — so
/// another source can be added later without rewriting the file. Choices are
/// kept in `<stateDir>/folder-icons.json`, keyed by absolute path and written
/// on every change, exactly as FolderSortMemory does: they are deliberate and
/// rare, and not recomputable.
///
/// Painting asks effectiveGlyph() once per directory row, so it is a hash
/// lookup and nothing else. The SVG bytes of every glyph in use are read when
/// the store loads or a glyph is chosen, never from paint.
class FolderIconStore : public QObject
{
    Q_OBJECT

public:
    static FolderIconStore &instance();

    /// Reads the file and the bytes of the glyphs it names. Idempotent; the
    /// model calls it when it starts a scan, which is off the paint path.
    void ensureLoaded();

    /// The user's choice for this path, or empty.
    QString glyphFor(const QString &path) const;

    /// The choice, else the standard folder's glyph, else empty.
    QString effectiveGlyph(const QString &path) const;

    void set(const QString &path, const QString &glyphId);
    void clear(const QString &path);

    /// A folder was renamed or moved by Panefile: its choice, and those of
    /// everything beneath it, follow.
    void renamed(const QString &oldPath, const QString &newPath);

    /// A folder was deleted for good.
    void removed(const QString &path);

    /// The glyph's SVG file contents, or empty when it is unknown or its file
    /// is missing. Cached; reading happens once per glyph.
    QByteArray svgFor(const QString &glyphId);

    /// `^hero:[a-z0-9-]+$`, which is what keeps a hand-edited state file from
    /// reaching outside the icon directory.
    static bool isValidGlyphId(const QString &glyphId);

    /// Where the full Heroicons set lives, or empty when it is not installed.
    static QString heroiconsDirectory();

    /// Empties the store without touching the file. For tests.
    void reset();

    static QByteArray toJson(const QHash<QString, QString> &icons);
    static QHash<QString, QString> fromJson(const QByteArray &bytes);

Q_SIGNALS:
    void changed(const QString &path);

private:
    FolderIconStore() = default;

    void save() const;
    void preload(const QString &glyphId);

    QHash<QString, QString> m_icons;
    QHash<QString, QByteArray> m_svgs;
    bool m_loaded = false;
};

} // namespace pf
