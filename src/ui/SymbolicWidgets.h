#pragma once

#include <QLabel>
#include <QPushButton>

#include <functional>

namespace pf::ui {

/// A symbolic glyph that paints itself from the current palette.
///
/// A stylesheet cannot recolour an image, so a glyph that is set once as a
/// pixmap keeps the colour of whichever theme was current when it was made —
/// the settings tabs' icons would stay light-theme grey through a live preview
/// of a dark theme. This asks for its colour every time it paints, so a theme
/// change (which repaints everything) is all it needs.
class SymbolicLabel : public QLabel
{
    Q_OBJECT

public:
    /// `colour` is asked at paint time; it may depend on the widget's state.
    SymbolicLabel(QString iconName, int size, std::function<QColor()> colour,
                  QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_iconName;
    int m_size;
    std::function<QColor()> m_colour;
};

/// A flat, icon-only button whose glyph follows the palette: faint at rest, the
/// text colour under the pointer.
class SymbolicButton : public QPushButton
{
    Q_OBJECT

public:
    SymbolicButton(QString iconName, int iconSize, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_iconName;
    int m_iconSize;
};

} // namespace pf::ui
