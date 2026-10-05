#include "ui/PanefileStyle.h"

#include "ui/ThemePalette.h"

#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

namespace pf::ui {
namespace {

/// The size of a check box or radio button, as Review draws its own.
constexpr int kIndicatorSize = 16;

/// Black or white, whichever the accent can carry.
QColor readableOn(const QColor &background)
{
    const double luma = (0.2126 * background.redF()) + (0.7152 * background.greenF()) +
                        (0.0722 * background.blueF());
    return luma > 0.55 ? QColor(0, 0, 0) : QColor(255, 255, 255);
}

/// The indicator's square, centred in whatever rectangle the style was given.
QRectF indicatorRect(const QRect &rect)
{
    const qreal size = std::min({rect.width(), rect.height(), kIndicatorSize}) - 1.5;
    return {rect.center().x() - (size / 2.0) + 0.5, rect.center().y() - (size / 2.0) + 0.5, size,
            size};
}

void drawCheckBox(const QStyleOption *option, QPainter *painter)
{
    const ThemePalette &palette = currentPalette();
    const QRectF box = indicatorRect(option->rect);
    const bool enabled = (option->state & QStyle::State_Enabled) != 0;
    const bool on = (option->state & QStyle::State_On) != 0;
    const bool partial = (option->state & QStyle::State_NoChange) != 0;
    const bool hovered = (option->state & QStyle::State_MouseOver) != 0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(enabled ? 1.0 : 0.5);

    if (on || partial) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.accent);
        painter->drawRoundedRect(box, 4.5, 4.5);

        const QPen mark(readableOn(palette.accent), 1.8, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin);
        painter->setPen(mark);
        painter->setBrush(Qt::NoBrush);
        if (partial) {
            painter->drawLine(QPointF(box.left() + (box.width() * 0.28), box.center().y()),
                              QPointF(box.right() - (box.width() * 0.28), box.center().y()));
        } else {
            QPainterPath tick;
            tick.moveTo(box.left() + (box.width() * 0.25), box.top() + (box.height() * 0.52));
            tick.lineTo(box.left() + (box.width() * 0.43), box.top() + (box.height() * 0.70));
            tick.lineTo(box.left() + (box.width() * 0.76), box.top() + (box.height() * 0.32));
            painter->drawPath(tick);
        }
    } else {
        const QColor border = hovered && enabled
                                  ? palette.accent
                                  : config::mixColours(palette.border, palette.text, 0.22);
        painter->setPen(QPen(border, 1.5));
        painter->setBrush(palette.surface);
        painter->drawRoundedRect(box, 4.5, 4.5);
    }

    painter->restore();
}

void drawRadioButton(const QStyleOption *option, QPainter *painter)
{
    const ThemePalette &palette = currentPalette();
    const QRectF circle = indicatorRect(option->rect);
    const bool enabled = (option->state & QStyle::State_Enabled) != 0;
    const bool on = (option->state & QStyle::State_On) != 0;
    const bool hovered = (option->state & QStyle::State_MouseOver) != 0;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(enabled ? 1.0 : 0.5);

    if (on) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.accent);
        painter->drawEllipse(circle);
        painter->setBrush(readableOn(palette.accent));
        const qreal dot = circle.width() * 0.38;
        painter->drawEllipse(circle.center(), dot / 2.0, dot / 2.0);
    } else {
        const QColor border = hovered && enabled
                                  ? palette.accent
                                  : config::mixColours(palette.border, palette.text, 0.22);
        painter->setPen(QPen(border, 1.5));
        painter->setBrush(palette.surface);
        painter->drawEllipse(circle);
    }

    painter->restore();
}

/// A bare tick, for a checked menu item: no box, in the accent.
void drawMenuCheck(const QStyleOption *option, QPainter *painter)
{
    if ((option->state & QStyle::State_On) == 0) {
        return;
    }
    const QRectF box = indicatorRect(option->rect).adjusted(1.5, 1.5, -1.5, -1.5);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(currentPalette().accent, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath tick;
    tick.moveTo(box.left() + (box.width() * 0.18), box.top() + (box.height() * 0.52));
    tick.lineTo(box.left() + (box.width() * 0.40), box.top() + (box.height() * 0.74));
    tick.lineTo(box.left() + (box.width() * 0.82), box.top() + (box.height() * 0.28));
    painter->drawPath(tick);
    painter->restore();
}

/// A chevron rather than a filled triangle: the same stroke as the icons.
void drawChevron(QStyle::PrimitiveElement element, const QStyleOption *option, QPainter *painter)
{
    const ThemePalette &palette = currentPalette();
    const bool enabled = (option->state & QStyle::State_Enabled) != 0;

    const QRectF area = option->rect;
    const qreal width = std::min<qreal>(8.0, area.width() - 2);
    const qreal height = width / 2.0;
    const QPointF centre = area.center();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(enabled ? palette.subtext : palette.overlay, 1.5, Qt::SolidLine,
                         Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);

    QPainterPath path;
    switch (element) {
    case QStyle::PE_IndicatorArrowUp:
    case QStyle::PE_IndicatorSpinUp:
        path.moveTo(centre.x() - (width / 2.0), centre.y() + (height / 2.0));
        path.lineTo(centre.x(), centre.y() - (height / 2.0));
        path.lineTo(centre.x() + (width / 2.0), centre.y() + (height / 2.0));
        break;
    case QStyle::PE_IndicatorArrowRight:
        path.moveTo(centre.x() - (height / 2.0), centre.y() - (width / 2.0));
        path.lineTo(centre.x() + (height / 2.0), centre.y());
        path.lineTo(centre.x() - (height / 2.0), centre.y() + (width / 2.0));
        break;
    case QStyle::PE_IndicatorArrowLeft:
        path.moveTo(centre.x() + (height / 2.0), centre.y() - (width / 2.0));
        path.lineTo(centre.x() - (height / 2.0), centre.y());
        path.lineTo(centre.x() + (height / 2.0), centre.y() + (width / 2.0));
        break;
    default:
        path.moveTo(centre.x() - (width / 2.0), centre.y() - (height / 2.0));
        path.lineTo(centre.x(), centre.y() + (height / 2.0));
        path.lineTo(centre.x() + (width / 2.0), centre.y() - (height / 2.0));
        break;
    }
    painter->drawPath(path);
    painter->restore();
}

} // namespace

PanefileStyle::PanefileStyle() = default;

void PanefileStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                                  QPainter *painter, const QWidget *widget) const
{
    switch (element) {
    case PE_IndicatorCheckBox:
    case PE_IndicatorItemViewItemCheck:
        drawCheckBox(option, painter);
        return;
    case PE_IndicatorRadioButton:
        drawRadioButton(option, painter);
        return;
    case PE_IndicatorMenuCheckMark:
        drawMenuCheck(option, painter);
        return;
    case PE_IndicatorArrowDown:
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowLeft:
    case PE_IndicatorArrowRight:
    case PE_IndicatorSpinUp:
    case PE_IndicatorSpinDown:
        drawChevron(element, option, painter);
        return;
    case PE_FrameFocusRect:
        // Focus is shown by a field's border or a row's pill. A dotted
        // rectangle on top of either is a second, uglier answer.
        return;
    default:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
}

int PanefileStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
                               const QWidget *widget) const
{
    switch (metric) {
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return kIndicatorSize;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

void PanefileStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);

    // A popup's window is a rectangle; the stylesheet's border-radius only
    // rounds what is painted inside it. Translucent, the corners outside the
    // radius are see-through instead of four little squares of background.
    if (qobject_cast<QMenu *>(widget) != nullptr || widget->inherits("QTipLabel")) {
        widget->setAttribute(Qt::WA_TranslucentBackground, true);
    }
}

} // namespace pf::ui
