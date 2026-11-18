#pragma once

#include <QComboBox>

namespace pf::ui {

/// A combo box sized to what it holds, capped so a long name can't widen its
/// window, and showing a name that doesn't fit as "Markdown docum…".
class ElidingComboBox : public QComboBox
{
    Q_OBJECT

public:
    using QComboBox::QComboBox;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// The current text as drawn: elided to the room the control has for it.
    QString displayText() const;

protected:
    void paintEvent(QPaintEvent *event) override;
};

} // namespace pf::ui
