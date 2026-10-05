#pragma once

#include "ui/modals/Modal.h"

#include <functional>

class QLabel;
class QPushButton;

namespace pf::ui {

/// A yes-or-no question about something destructive, in the window rather
/// than over it.
///
/// It replaces QMessageBox, which §5.4 rules out — a top-level dialog is tiled
/// by a tiling compositor rather than floated — and which arrived in the
/// platform's own widgets on a window that is otherwise themed throughout.
///
/// The safe answer has the keyboard. Enter presses whichever button has focus,
/// and that starts as Cancel: a confirmation exists to be read, and somebody
/// pressing Enter without reading it should lose nothing.
class ConfirmModal : public Modal
{
    Q_OBJECT

public:
    explicit ConfirmModal(QWidget *parent);

    /// Asks, and calls `onConfirm` if the answer is yes. Nothing is called on
    /// a no, Esc, or a second ask() replacing this one.
    void ask(const QString &title, const QString &message, const QString &confirmLabel,
             std::function<void()> onConfirm);

protected:
    void accept() override;

    /// Cancel, so Enter without reading loses nothing.
    QWidget *initialFocusWidget() override;

private:
    QLabel *m_title = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_confirm = nullptr;
    std::function<void()> m_onConfirm;
};

} // namespace pf::ui
