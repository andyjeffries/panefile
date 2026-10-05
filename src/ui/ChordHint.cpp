#include "ui/ChordHint.h"

#include "ui/Keycaps.h"
#include "ui/ThemePalette.h"

#include <QPainter>
#include <QPainterPath>

namespace pf::ui {
namespace {

/// Room around the card for its shadow.
constexpr int kShadow = 14;
constexpr int kPadding = 14;
constexpr int kRowHeight = 26;
constexpr int kHeaderHeight = 30;
constexpr int kFooterHeight = 24;
constexpr int kKeyColumnGap = 14;

/// A list longer than this is broken into columns rather than running off the
/// top of a small window.
constexpr int kRowsPerColumn = 10;
constexpr int kColumnGap = 26;

QFont descriptionFont(const QFont &base)
{
    return base;
}

QFont captionFont(const QFont &base)
{
    QFont font = base;
    if (base.pixelSize() > 0) {
        font.setPixelSize(std::max(1, base.pixelSize() - 2));
    }
    return font;
}

} // namespace

ChordHint::ChordHint(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void ChordHint::present(const QString &pending, const QList<Row> &rows, const QRect &area)
{
    m_pending = pending;
    m_rows = rows;

    const QSize size = sizeHint().boundedTo(area.size());
    // Bottom-right, a little in from the panels' edges: where the eye already
    // is after reading the footer's pending keys, and clear of the cursor row
    // in most of a list.
    setGeometry(area.right() - size.width() - 4, area.bottom() - size.height() - 4, size.width(),
                size.height());
    raise();
    show();
    update();
}

QSize ChordHint::sizeHint() const
{
    const QFont base = font();
    const QFontMetrics description(descriptionFont(base));

    qreal keysWidth = 0;
    int textWidth = 0;
    for (const Row &row : m_rows) {
        keysWidth = std::max(keysWidth, keycaps::width(row.first, base));
        textWidth = std::max(textWidth, description.horizontalAdvance(row.second));
    }
    textWidth = std::min(textWidth, 320);

    const auto rowCount = static_cast<int>(m_rows.size());
    const int columns = std::max(1, (rowCount + kRowsPerColumn - 1) / kRowsPerColumn);
    const int rowsShown = std::min(rowCount, kRowsPerColumn);
    const int columnWidth = static_cast<int>(std::ceil(keysWidth)) + kKeyColumnGap + textWidth;

    const int width = (2 * kPadding) + (columns * columnWidth) + ((columns - 1) * kColumnGap);
    const int height = kHeaderHeight + (rowsShown * kRowHeight) + kFooterHeight + kPadding;
    return {width + (2 * kShadow), height + (2 * kShadow)};
}

void ChordHint::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    const ThemePalette &palette = currentPalette();
    const bool light = palette.isLight();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card = QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow);
    const qreal radius = palette.borderRadius + 6;

    // The modals' shadow, smaller: stacked rounded rectangles, lower than high.
    painter.setPen(Qt::NoPen);
    for (int layer = 10; layer >= 1; --layer) {
        const qreal spread = layer * 1.1;
        painter.setBrush(QColor(0, 0, 0, light ? 6 : 12));
        painter.drawRoundedRect(card.adjusted(-spread, (-spread * 0.5) + 2, spread, spread + 4),
                                radius + spread, radius + spread);
    }

    painter.setBrush(palette.surface);
    painter.setPen(QPen(config::mixColours(palette.border, palette.text, 0.12), 1));
    painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    const QFont base = font();
    const qreal left = card.left() + kPadding;

    // The keys typed so far, as caps, then what this is.
    QString typed = m_pending;
    if (typed.endsWith(QLatin1Char('-'))) {
        typed.chop(1);
    }
    const qreal headerCentre = card.top() + (kHeaderHeight / 2.0) + 4;
    const qreal typedWidth = keycaps::paint(&painter, QPointF(left, headerCentre), typed, base);
    painter.setFont(captionFont(base));
    painter.setPen(palette.overlay);
    painter.drawText(QRectF(left + typedWidth + 8, headerCentre - 10, card.width(), 20),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("then…"));

    // The continuations, in columns of at most kRowsPerColumn.
    qreal keysWidth = 0;
    for (const Row &row : m_rows) {
        keysWidth = std::max(keysWidth, keycaps::width(row.first, base));
    }
    const QFontMetrics description(descriptionFont(base));
    int textWidth = 0;
    for (const Row &row : m_rows) {
        textWidth = std::max(textWidth, description.horizontalAdvance(row.second));
    }
    textWidth = std::min(textWidth, 320);
    const qreal columnWidth = keysWidth + kKeyColumnGap + textWidth;

    painter.setFont(descriptionFont(base));
    const qreal rowsTop = card.top() + kHeaderHeight + 4;
    for (qsizetype index = 0; index < m_rows.size(); ++index) {
        const qsizetype columnIndex = index / kRowsPerColumn;
        const auto column = static_cast<qreal>(columnIndex);
        const auto line = static_cast<qreal>(index % kRowsPerColumn);
        const qreal x = left + (column * (columnWidth + kColumnGap));
        const qreal centre = rowsTop + (line * kRowHeight) + (kRowHeight / 2.0);

        keycaps::paint(&painter, QPointF(x, centre), m_rows[index].first, base);
        painter.setFont(descriptionFont(base));
        painter.setPen(palette.text);
        const QRectF textRect(x + keysWidth + kKeyColumnGap, centre - (kRowHeight / 2.0), textWidth,
                              kRowHeight);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                         description.elidedText(m_rows[index].second, Qt::ElideRight, textWidth));
    }

    painter.setFont(captionFont(base));
    painter.setPen(palette.overlay);
    painter.drawText(QRectF(left, card.bottom() - kFooterHeight - 4, card.width(), kFooterHeight),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("Esc cancels"));
}

} // namespace pf::ui
