#pragma once

#include <QProxyStyle>

namespace pf::ui {

/// The few things the stylesheet cannot draw in the theme's colours.
///
/// QSS paints boxes well and glyphs badly: a check mark or a radio dot can
/// only come from an image file, and an image file cannot follow the theme. Left to the base style,
/// the settings window had Fusion's black spin buttons and crossed check boxes sitting on a warm
/// off-white page.
///
/// So the stylesheet leaves those sub-controls alone, Qt falls through to the
/// base style for them, and this is the base style — Fusion or the platform's
/// own, with these few primitives drawn from currentPalette() at paint time.
/// Arrows are drawn as chevrons for whatever still asks the base style for one.
/// It also gives menus and tooltips a translucent window, which is what lets
/// their stylesheet radius be a rounded corner instead of a square one with a
/// rounded border inside it.
class PanefileStyle : public QProxyStyle
{
    Q_OBJECT

public:
    PanefileStyle();

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;

    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;

    void polish(QWidget *widget) override;
    using QProxyStyle::polish;
};

} // namespace pf::ui
