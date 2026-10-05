#include "ui/SymbolicWidgets.h"

#include "model/SymbolicIcon.h"
#include "ui/ThemePalette.h"

#include <QPainter>

namespace pf::ui {

SymbolicLabel::SymbolicLabel(QString iconName, int size, std::function<QColor()> colour,
                             QWidget *parent)
    : QLabel(parent), m_iconName(std::move(iconName)), m_size(size), m_colour(std::move(colour))
{
    setAlignment(Qt::AlignCenter);
}

QSize SymbolicLabel::sizeHint() const
{
    return {m_size, m_size};
}

QSize SymbolicLabel::minimumSizeHint() const
{
    return sizeHint();
}

void SymbolicLabel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    const QPixmap glyph = SymbolicIcon::pixmap(m_iconName, m_colour(), m_size, devicePixelRatioF());
    const QRect target(QPoint((width() - m_size) / 2, (height() - m_size) / 2),
                       QSize(m_size, m_size));
    painter.drawPixmap(target, glyph);
}

SymbolicButton::SymbolicButton(QString iconName, int iconSize, QWidget *parent)
    : QPushButton(parent), m_iconName(std::move(iconName)), m_iconSize(iconSize)
{
    setFlat(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
}

void SymbolicButton::paintEvent(QPaintEvent *event)
{
    // The stylesheet's background first — the hover shape — then the glyph.
    QPushButton::paintEvent(event);

    const ThemePalette &palette = currentPalette();
    const QColor colour = underMouse() ? palette.text : palette.subtext;
    QPainter painter(this);
    const QPixmap glyph = SymbolicIcon::pixmap(m_iconName, colour, m_iconSize, devicePixelRatioF());
    painter.drawPixmap(QRect(QPoint((width() - m_iconSize) / 2, (height() - m_iconSize) / 2),
                             QSize(m_iconSize, m_iconSize)),
                       glyph);
}

} // namespace pf::ui
