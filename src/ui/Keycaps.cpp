#include "ui/Keycaps.h"

#include "ui/ThemePalette.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>

namespace pf::ui::keycaps {
namespace {

constexpr qreal kPressGap = 4;
constexpr qreal kBindingGap = 16;

/// HelpModal's separator between bindings.
QString bindingSeparator()
{
    return QStringLiteral("  ·  ");
}

qreal capWidth(const QFontMetricsF &metrics, const QString &press)
{
    return std::max<qreal>(kHeight, metrics.horizontalAdvance(press) + 12);
}

} // namespace

QFont font(const QFont &base)
{
    QFont font = base;
    font.setFamilies(
        {QStringLiteral("JetBrains Mono"), QStringLiteral("SF Mono"), QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace);
    if (base.pixelSize() > 0) {
        font.setPixelSize(std::max(1, base.pixelSize() - 2));
    }
    return font;
}

qreal width(const QString &text, const QFont &base)
{
    const QFontMetricsF metrics(font(base));
    qreal total = 0;
    const QStringList bindings = text.split(bindingSeparator());
    for (qsizetype bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex) {
        total += bindingIndex > 0 ? kBindingGap : 0;
        const QStringList presses = bindings[bindingIndex].split(QLatin1Char(' '));
        for (const QString &press : presses) {
            total += capWidth(metrics, press) + kPressGap;
        }
        total -= kPressGap;
    }
    return total;
}

qreal paint(QPainter *painter, const QPointF &origin, const QString &text, const QFont &base)
{
    const ThemePalette &palette = currentPalette();
    const QFont capFont = font(base);
    const QFontMetricsF metrics(capFont);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setFont(capFont);

    qreal x = origin.x();
    const qreal top = origin.y() - (kHeight / 2.0);
    const QStringList bindings = text.split(bindingSeparator());
    for (qsizetype bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex) {
        if (bindingIndex > 0) {
            painter->setPen(palette.overlay);
            painter->drawText(QRectF(x, top, kBindingGap, kHeight), Qt::AlignCenter,
                              QStringLiteral("·"));
            x += kBindingGap;
        }
        const QStringList presses = bindings[bindingIndex].split(QLatin1Char(' '));
        for (const QString &press : presses) {
            const QRectF cap(x, top, capWidth(metrics, press), kHeight);
            painter->setPen(QPen(palette.border, 1));
            painter->setBrush(config::mixColours(palette.surface, palette.text,
                                                 palette.isLight() ? 0.035 : 0.06));
            painter->drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            painter->setPen(palette.text);
            painter->drawText(cap, Qt::AlignCenter, press);
            x += cap.width() + kPressGap;
        }
        x -= kPressGap;
    }

    painter->restore();
    return x - origin.x();
}

} // namespace pf::ui::keycaps
