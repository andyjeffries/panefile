#include "ui/modals/InputModal.h"

#include "ui/ThemePalette.h"

#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace pf::ui {

InputModal::InputModal(QWidget *parent)
    : Modal(parent), m_title(new QLabel), m_hint(new QLabel), m_input(new QLineEdit),
      m_problem(new QLabel)
{
    setSizePercent(44, 24);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(10);

    m_title->setObjectName(QStringLiteral("modalTitle"));
    m_title->setTextFormat(Qt::PlainText);
    layout->addWidget(m_title);

    m_hint->setObjectName(QStringLiteral("modalHint"));
    m_hint->setTextFormat(Qt::PlainText);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_input->setObjectName(QStringLiteral("modalInput"));
    layout->addWidget(m_input);

    m_problem->setObjectName(QStringLiteral("modalProblem"));
    m_problem->setWordWrap(true);
    m_problem->hide();
    layout->addWidget(m_problem);

    layout->addStretch(1);

    // A problem shown once must not survive the next keystroke, or the user
    // fixes the name and still sees the complaint about the old one.
    connect(m_input, &QLineEdit::textChanged, this, [this] { m_problem->hide(); });
}

void InputModal::ask(const QString &title, const QString &hint, const QString &initialValue,
                     int selectionStart, int selectionLength)
{
    m_title->setText(title);
    m_hint->setText(hint);
    m_hint->setVisible(!hint.isEmpty());
    m_problem->hide();

    m_input->setText(initialValue);

    showModal();
    m_input->setFocus(Qt::ShortcutFocusReason);

    if (selectionStart >= 0 && selectionLength > 0) {
        m_input->setSelection(selectionStart, selectionLength);
    } else {
        m_input->selectAll();
    }
}

QString InputModal::value() const
{
    return m_input->text().trimmed();
}

void InputModal::setProblem(const QString &problem)
{
    m_problem->setText(problem);
    m_problem->setStyleSheet(
        QStringLiteral("color: %1;").arg(currentPalette().error.name(QColor::HexRgb)));
    m_problem->setVisible(!problem.isEmpty());
}

void InputModal::accept()
{
    if (value().isEmpty()) {
        // Dismissing on an empty value would silently discard the user's
        // intent; complaining keeps them in the prompt they meant to use.
        setProblem(tr("Enter a name"));
        return;
    }

    Q_EMIT submitted(value());

    // Not Modal::accept(): the caller may have found a problem the modal could
    // not — a name already taken — and shown it with setProblem(). Whoever
    // handles submitted() dismisses when it is satisfied.
}

} // namespace pf::ui
