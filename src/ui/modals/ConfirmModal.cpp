#include "ui/modals/ConfirmModal.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pf::ui {

ConfirmModal::ConfirmModal(QWidget *parent)
    : Modal(parent), m_title(new QLabel(contentWidget())), m_message(new QLabel(contentWidget())),
      m_cancel(new QPushButton(tr("Cancel"), contentWidget())),
      m_confirm(new QPushButton(contentWidget()))
{
    setSizePercent(34, 24);
    setHeightFitsContent(true);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(8);

    m_title->setObjectName(QStringLiteral("modalTitle"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    m_message->setObjectName(QStringLiteral("modalMessage"));
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    layout->addWidget(m_message);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 10, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    m_confirm->setObjectName(QStringLiteral("destructiveButton"));
    buttons->addWidget(m_confirm);
    layout->addLayout(buttons);

    connect(m_cancel, &QPushButton::clicked, this, &ConfirmModal::dismiss);
    connect(m_confirm, &QPushButton::clicked, this, [this] {
        m_confirm->setFocus();
        accept();
    });
}

void ConfirmModal::ask(const QString &title, const QString &message, const QString &confirmLabel,
                       std::function<void()> onConfirm)
{
    m_title->setText(title);
    m_message->setText(message);
    m_confirm->setText(confirmLabel);
    m_onConfirm = std::move(onConfirm);

    showModal();
}

QWidget *ConfirmModal::initialFocusWidget()
{
    return m_cancel;
}

void ConfirmModal::accept()
{
    // Enter presses the focused button, not "yes".
    if (!m_confirm->hasFocus()) {
        m_onConfirm = nullptr;
        dismiss();
        return;
    }

    // Taken before dismissing, because the callback may ask() again — the
    // second confirmation of a permanent delete — and must not find its own
    // question cleared out from under it.
    const std::function<void()> confirmed = std::move(m_onConfirm);
    m_onConfirm = nullptr;
    Modal::accept();
    if (confirmed) {
        confirmed();
    }
}

} // namespace pf::ui
