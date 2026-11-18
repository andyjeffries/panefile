#include "ui/ElidingComboBox.h"

#include <QPaintEvent>
#include <QStyleOptionComboBox>
#include <QStylePainter>

#include <algorithm>

namespace pf::ui {
namespace {

constexpr int kMaxWidth = 440;
constexpr int kMinWidth = 120;

} // namespace

QSize ElidingComboBox::sizeHint() const
{
    QSize size = QComboBox::sizeHint();

    // The base hint can fall short of what displayText() needs for the widest
    // name, which would elide a name that has room.
    QStyleOptionComboBox option;
    initStyleOption(&option);
    option.rect = QRect(0, 0, size.width(), std::max(size.height(), minimumHeight()));
    const QRect edit =
        style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, this);
    int widest = 0;
    for (int i = 0; i < count(); ++i) {
        widest = std::max(widest, fontMetrics().horizontalAdvance(itemText(i)));
    }
    size.setWidth(std::max(size.width(), size.width() - edit.width() + widest + 4));
    size.setWidth(std::min(size.width(), kMaxWidth));
    return size;
}

QSize ElidingComboBox::minimumSizeHint() const
{
    QSize size = sizeHint();
    size.setWidth(std::min(size.width(), kMinWidth));
    return size;
}

QString ElidingComboBox::displayText() const
{
    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QRect edit = style()->subControlRect(QStyle::CC_ComboBox, &option,
                                               QStyle::SC_ComboBoxEditField, this);
    return fontMetrics().elidedText(currentText(), Qt::ElideRight, edit.width() - 2);
}

void ElidingComboBox::paintEvent(QPaintEvent *)
{
    QStylePainter painter(this);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    painter.drawComplexControl(QStyle::CC_ComboBox, option);
    option.currentText = displayText();
    painter.drawControl(QStyle::CE_ComboBoxLabel, option);
}

} // namespace pf::ui
