#pragma once

#include "ui/modals/Modal.h"

class QLabel;
class QLineEdit;

namespace pf::ui {

/// A one-line text prompt (§5.4).
///
/// Used by `file_panel_item_create` (`Ctrl+N`) and `file_panel_item_rename`
/// (`Ctrl+R`). One class rather than two, because the difference between them
/// is a title, a starting value and what the caller does with the answer.
///
/// §6.1 puts the application into Typing mode while this has focus, so single
/// key bindings are suspended and `d d` cannot fire while a filename is being
/// typed.
class InputModal : public Modal
{
    Q_OBJECT

public:
    explicit InputModal(QWidget *parent);

    /// Shows the prompt. `selection` selects a range of the initial value —
    /// used to preselect a filename's stem so typing replaces the name but
    /// keeps the extension, which is what every file manager does on rename.
    void ask(const QString &title, const QString &hint, const QString &initialValue = {},
             int selectionStart = -1, int selectionLength = -1);

    QString value() const;

    /// Shows a validation message and refuses to accept until it is cleared.
    void setProblem(const QString &problem);

Q_SIGNALS:
    /// Enter, with a non-empty value.
    void submitted(const QString &value);

protected:
    void accept() override;

private:
    QLabel *m_title = nullptr;
    QLabel *m_hint = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_problem = nullptr;
};

} // namespace pf::ui
