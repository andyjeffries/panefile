#pragma once

#include <QList>
#include <QString>

namespace pf {

/// One of the folders every desktop sets up, and the Heroicons glyph it is
/// marked with.
struct StandardFolder {
    QString title; ///< "Downloads"
    QString glyph; ///< a folder-icon glyph id, e.g. "hero:arrow-down-tray"
    QString path;  ///< cleaned absolute path
};

/// Home, Desktop, Documents, Downloads, Music, Pictures, Videos, Public and
/// Templates — the places that get an emblem without anyone choosing one, as
/// in Finder.
///
/// One table shared by the folder icon, the picker's "Standard folders" strip
/// and the sidebar, so the three cannot disagree about which folder is which.
class StandardFolders
{
public:
    /// Every standard folder that resolves to a place of its own, in picker
    /// order. Built once, on first use. A directory the desktop leaves unset
    /// resolves to the home folder in Qt; those are skipped rather than
    /// stamping the home folder's icon on the wrong entry.
    static const QList<StandardFolder> &all();

    /// The glyph id for a path, or empty when it is not a standard folder.
    static QString glyphFor(const QString &path);

    /// The Heroicons name of the glyph (without the `hero:` prefix) for a path,
    /// for callers that draw it themselves.
    static QString heroNameFor(const QString &path);

    /// The picker's presets: the nine standard folders by title, whether or
    /// not this machine has them, in a stable order.
    static QList<StandardFolder> presets();
};

} // namespace pf
