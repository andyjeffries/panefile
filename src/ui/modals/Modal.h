#pragma once

#include <QWidget>

namespace pf::ui {

/// Base for every modal (§5.4).
///
/// "Modals are frameless child widgets centred over the main window with a
/// dimmed backdrop, **not** separate top-level windows — that matters on a
/// tiling compositor." A separate window would be tiled by the compositor
/// alongside the main one rather than floating over it, which is not what a
/// confirmation dialog should do to somebody's workspace.
///
/// The backdrop is painted by this widget, which covers its whole parent;
/// content sits in a centred panel within it. Esc dismisses, Enter confirms,
/// and both are handled here so no subclass has to remember.
class Modal : public QWidget
{
    Q_OBJECT

public:
    explicit Modal(QWidget *parent);

    /// Shows the modal centred over its parent and takes focus.
    void showModal();

    void dismiss();

    /// The centred panel subclasses put their content in.
    QWidget *contentWidget() const;

    /// Fraction of the parent's size the content panel occupies, clamped to the
    /// content's own size hint.
    void setSizePercent(int widthPercent, int heightPercent);

    /// Tracks the parent's size so the modal keeps covering the window as it is
    /// resized. Public because QObject declares it public, and narrowing an
    /// override's visibility is a trap for anyone holding a base pointer.
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void accepted();
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

    /// Called when Enter is pressed. The default accepts and dismisses.
    virtual void accept();

private:
    void reposition();

    QWidget *m_content = nullptr;
    int m_widthPercent = 60;
    int m_heightPercent = 70;
};

} // namespace pf::ui
