#include "ui/modals/Modal.h"

#include "ui/ThemePalette.h"

#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>

namespace pf::ui {

Modal::Modal(QWidget *parent) : QWidget(parent), m_content(new QWidget(this))
{
    Q_ASSERT_X(parent != nullptr, "Modal", "a modal must be a child widget, never a window (§5.4)");

    setObjectName(QStringLiteral("modal"));
    setFocusPolicy(Qt::StrongFocus);
    hide();

    m_content->setObjectName(QStringLiteral("modalContent"));
    m_content->setAutoFillBackground(true);

    // The modal tracks its parent's size, so it stays covering the window as it
    // is resized rather than being positioned once at show time.
    parent->installEventFilter(this);
}

QWidget *Modal::contentWidget() const
{
    return m_content;
}

void Modal::setSizePercent(int widthPercent, int heightPercent)
{
    m_widthPercent = std::clamp(widthPercent, 10, 100);
    m_heightPercent = std::clamp(heightPercent, 10, 100);
    reposition();
}

void Modal::showModal()
{
    if (parentWidget() == nullptr) {
        return;
    }
    setGeometry(parentWidget()->rect());
    reposition();
    raise();
    show();
    setFocus(Qt::PopupFocusReason);
}

void Modal::dismiss()
{
    hide();
    if (parentWidget() != nullptr) {
        // Focus must go back where it came from, or the next keystroke lands
        // nowhere and the application appears to have stopped responding.
        parentWidget()->setFocus(Qt::PopupFocusReason);
    }
    Q_EMIT dismissed();
}

void Modal::accept()
{
    Q_EMIT accepted();
    dismiss();
}

void Modal::reposition()
{
    if (parentWidget() == nullptr) {
        return;
    }

    const QSize available = parentWidget()->size();

    // The percentage is what was asked for; the floor stops a modal becoming
    // unusable in a small window, and the parent's own size is the ceiling.
    // Taking min(percentage, sizeHint) instead — as this did — meant a modal
    // asking for 70% got whatever its content happened to hint at, which for a
    // scrollable list is far too little.
    constexpr int kMinimumWidth = 360;
    constexpr int kMinimumHeight = 240;

    const int width = std::clamp(available.width() * m_widthPercent / 100,
                                 std::min(kMinimumWidth, available.width()), available.width());
    const int height = std::clamp(available.height() * m_heightPercent / 100,
                                  std::min(kMinimumHeight, available.height()), available.height());

    m_content->setGeometry((available.width() - width) / 2, (available.height() - height) / 2,
                           width, height);
}

void Modal::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);

    // The dimmed backdrop of §5.4. Semi-transparent rather than opaque so the
    // panels stay visible underneath — the modal is a layer over the user's
    // work, not a replacement for it.
    QColor backdrop = currentPalette().background;
    backdrop.setAlpha(180);
    painter.fillRect(rect(), backdrop);
}

void Modal::keyPressEvent(QKeyEvent *event)
{
    // §5.4: Esc always dismisses, Enter always confirms. Handled in the base so
    // that no subclass can forget, and so the two keys behave the same in every
    // modal in the application.
    switch (event->key()) {
    case Qt::Key_Escape:
        dismiss();
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        accept();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void Modal::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reposition();
}

bool Modal::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        setGeometry(parentWidget()->rect());
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace pf::ui
