#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace pf {

/// The chrome's symbolic glyphs: sidebar places, settings tabs, Quick Look's
/// close button and the like.
///
/// Heroicons v2 outline (24×24, 1.5px stroke, round caps and joins), the set
/// Review and Folio use, vendored under data/icons/symbolic with its licence
/// and compiled in as `:/icons/symbolic/<name>.svg`. Compiled in rather than
/// installed so that an icon can never be missing at run time; §3.4 permits
/// exactly that for icons the first paint can need, and the sidebar is part of
/// the first window.
///
/// Rendering, tinting and caching are TintedIcon's: crisp at the painted device
/// pixel ratio, recoloured to one colour, cached by (name, colour, size, ratio).
/// A glyph is tinted at the call site rather than in a stylesheet because QSS
/// cannot recolour an icon — which is also why a theme change has to re-ask
/// for every icon it shows (see Sidebar::refreshTheme()).
///
/// Lives in `model` rather than `ui` because IconProvider shares its renderer,
/// and `model` is the layer that resolves icons; `ui` uses it from above.
class SymbolicIcon
{
public:
    /// An icon for `name` (a file stem, e.g. "home") in `colour`, rendered at
    /// whatever size and ratio it is painted at. Null for an unknown name.
    static QIcon icon(const QString &name, const QColor &colour);

    /// The same glyph as a pixmap of `size` logical pixels at `devicePixelRatio`,
    /// for the few places that paint a pixmap rather than an icon.
    static QPixmap pixmap(const QString &name, const QColor &colour, int size,
                          qreal devicePixelRatio);

    /// Whether `name` is one of the bundled glyphs.
    static bool exists(const QString &name);

    /// `:/icons/symbolic/<name>.svg`.
    static QString resourcePath(const QString &name);
};

} // namespace pf
