#pragma once

#include "platform/FileChooserRequest.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLineEdit;
class QPushButton;

namespace pf::ui {

/// The bottom bar of a file dialog: the name being saved, the application's own
/// options, the file type, and Cancel and the accept button.
///
/// It has no behaviour of its own beyond the keys of the Name field; what the
/// buttons do is the ChooserController's. The look comes from the stylesheet's
/// existing roles — the accept button is the default button, the rest are plain
/// buttons, fields and menus — and the bar's own object name for its surface.
class ChooserBar : public QWidget
{
    Q_OBJECT

public:
    explicit ChooserBar(const platform::FileChooserRequest &request, QWidget *parent = nullptr);

    /// The Name field, or null when the dialog has none (Open, SaveFiles, folders).
    QLineEdit *nameField() const;
    QString name() const;
    void setName(const QString &name);

    /// Focuses the Name field with the part before the extension selected.
    void focusName();

    void setAcceptLabel(const QString &label);
    QString acceptLabel() const;
    void setAcceptEnabled(bool enabled);
    bool isAcceptEnabled() const;

    /// The type menu's selection as an index into the request's filters, or -1
    /// when there are none.
    int filterIndex() const;

    /// The application's options as (id, value): "true" or "false" for a
    /// checkbox, the chosen value for a menu.
    QList<QPair<QString, QString>> choiceValues() const;

    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void acceptRequested();
    void cancelRequested();
    void filterChanged(int index);
    void nameEdited();

    /// Tab or Down in the Name field.
    void listFocusRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    /// The app's options share the row with Type and the buttons while they fit
    /// the bar's width, and take a row of their own above it when they don't.
    void placeChoices();

    QLineEdit *m_name = nullptr;
    QComboBox *m_type = nullptr;
    QPushButton *m_accept = nullptr;
    QString m_acceptLabel;
    QList<QPair<QString, QWidget *>> m_choices;
    QWidget *m_choiceBox = nullptr;
    QWidget *m_actions = nullptr;
    QHBoxLayout *m_controls = nullptr;
    bool m_choicesInline = false;
};

} // namespace pf::ui
