#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace pf {

/// One SVG in a tinted icon's stack.
struct TintedLayer {
    /// A resource path, e.g. ":/icons/files/folder.svg".
    QString resource;

    /// Erase this layer's shape from what is beneath it instead of drawing it.
    /// The symlink badge uses this to cut a halo out of the icon it sits on, so
    /// the arrow stays legible over any glyph without a plate of its own.
    bool erase = false;
};

/// Monochrome SVGs rendered at the exact device size they are painted at, then
/// recoloured to one colour (§4.3, §9).
///
/// Both icon sets go through here: the file-type glyphs IconProvider hands the
/// delegate, and the symbolic chrome glyphs of SymbolicIcon. Two properties
/// matter to both, and are why this is not QIcon(":/x.svg") with a tint
/// painted over the result:
///
///   * Crisp at any device pixel ratio. A pixmap rendered at one size and left
///     for QIcon to scale is soft at every other size, and the file glyphs are
///     drawn on a 16px grid precisely so that 16 and 32 come out pixel-exact.
///     The icon here is backed by an engine that renders at
///     `logical size × device pixel ratio` whenever it is asked to paint.
///
///   * Tinted by alpha. Each SVG is drawn in one colour at varying opacity; the
///     result is composited with the tint through SourceIn, which keeps every
///     pixel's alpha and replaces its colour. A body at 20% and an outline at
///     100% survive any tint as two tones of it, and the same file serves a
///     blue folder in a focused panel, a dimmed one in an unfocused panel and a
///     white one on the cursor pill.
///
/// The SVGs are read through Qt's svg image-format plugin, which is loaded on
/// the first icon painted. Linking QtSvg into the binary instead would add a
/// DT_NEEDED entry to every launch for the sake of a renderer that is never
/// needed before the first row paints (§3.4).
///
/// Rendered pixmaps live in QPixmapCache, keyed on (layers, colour, device
/// size, mode), so a theme change cannot grow the cache without bound: stale
/// tints age out under its LRU limit rather than having to be cleared.
///
/// GUI thread only, like QPixmap itself.
class TintedIcon
{
public:
    /// The layers rendered at `logicalSize × devicePixelRatio` device pixels and
    /// tinted to `colour`. The pixmap carries the device pixel ratio, so it
    /// paints at `logicalSize` in logical coordinates.
    ///
    /// QIcon::Disabled is drawn at reduced opacity rather than desaturated: the
    /// glyph is already one colour, and the delegate uses Disabled for a broken
    /// symlink, whose problem is that it points nowhere, not that it is grey.
    ///
    /// Returns a null pixmap when a layer does not exist or fails to render.
    static QPixmap pixmap(const QList<TintedLayer> &layers, const QColor &colour,
                          const QSize &logicalSize, qreal devicePixelRatio,
                          QIcon::Mode mode = QIcon::Normal);

    /// An icon that calls pixmap() at whatever size and device pixel ratio it
    /// is painted at. Cheap to construct; nothing is rendered until it paints.
    /// Null when the first layer does not exist.
    static QIcon icon(const QList<TintedLayer> &layers, const QColor &colour);

    /// The opacity QIcon::Disabled is drawn at.
    static constexpr qreal kDisabledOpacity = 0.45;
};

} // namespace pf
