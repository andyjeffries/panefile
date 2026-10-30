#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace pf {

/// What a folder icon looks like: its colour, the emblem on it, and its state.
struct FolderIconSpec {
    /// A FolderIconStore glyph id, or empty for a plain folder.
    QString glyphId;

    /// The theme's directory colour.
    QColor tint;

    /// Drawn closed: no sheet of paper, the flap raised, the emblem with it.
    bool empty = false;

    /// Adds the symlink badge.
    bool linked = false;

    /// For an unfocused panel: every colour, the paper included, is mixed 30%
    /// toward this one. Invalid draws at full strength.
    QColor fade;
};

/// Draws the folder (design/NOTES.md: "The folder icon").
///
/// Back panel, one sheet of paper, the flap's shadow cast onto it, then the
/// front flap with its gradient and edge lines. An emblem is debossed into the
/// flap: the glyph one device pixel lower in a highlight, then in place in a
/// darker inset, so it reads as pressed into the surface.
///
/// The picture is built as SVG text from the theme's colour and rasterised at
/// the device size it is painted at, through the same svg image plugin
/// TintedIcon uses. There are no blur filters, only gradients and opacity, so
/// the plugin's SVG Tiny subset draws it. Two masters: a 64-unit one from 21 px
/// up, and a simplified 16-unit one for list rows.
///
/// GUI thread only; pixmaps are cached in QPixmapCache.
class FolderIconRenderer
{
public:
    static QPixmap pixmap(const FolderIconSpec &spec, const QSize &logicalSize,
                          qreal devicePixelRatio, QIcon::Mode mode = QIcon::Normal);

    /// An icon that renders at whatever size and ratio it is painted at.
    static QIcon icon(const FolderIconSpec &spec);

    /// The SVG for the spec at `devicePixels`, which picks the master and the
    /// emblem's minimum size. Exposed for tests.
    static QByteArray svg(const FolderIconSpec &spec, int devicePixels);
};

} // namespace pf
