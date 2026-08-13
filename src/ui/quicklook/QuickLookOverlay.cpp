#include "ui/quicklook/QuickLookOverlay.h"

#include "ui/ThemePalette.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

namespace pf::ui {
namespace {

/// How dark the backdrop goes. Enough to push the panels back without hiding
/// which directory you were looking at, which is context a preview needs.
constexpr int kBackdropAlpha = 150;

} // namespace

QuickLookOverlay::QuickLookOverlay(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("quickLookOverlay"));
    setAttribute(Qt::WA_NoMousePropagation);
    // No focus, ever: §7.6 keeps the panel cursor live while Quick Look is open.
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void QuickLookOverlay::setContentWidget(QWidget *content)
{
    m_content = content;
    if (m_content == nullptr) {
        return;
    }

    m_content->setParent(this);

    // §7.6's drop shadow. Applied to the content, not the overlay, so the
    // backdrop is not blurred along with it.
    if (m_content->graphicsEffect() == nullptr) {
        auto *shadow = new QGraphicsDropShadowEffect(m_content);
        shadow->setBlurRadius(48);
        shadow->setOffset(0, 12);
        shadow->setColor(QColor(0, 0, 0, 160));
        m_content->setGraphicsEffect(shadow);
    }

    reposition();
}

QWidget *QuickLookOverlay::contentWidget() const
{
    return m_content;
}

void QuickLookOverlay::setSizePercent(int percent)
{
    m_percent = qBound(20, percent, 100);
    reposition();
}

int QuickLookOverlay::sizePercent() const
{
    return m_percent;
}

void QuickLookOverlay::showOverlay()
{
    if (parentWidget() != nullptr) {
        parentWidget()->installEventFilter(this);
        setGeometry(parentWidget()->rect());
    }
    reposition();
    show();
    raise();
    if (m_content != nullptr) {
        m_content->show();
    }
}

void QuickLookOverlay::hideOverlay()
{
    if (parentWidget() != nullptr) {
        parentWidget()->removeEventFilter(this);
    }
    hide();
}

bool QuickLookOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

void QuickLookOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reposition();
}

void QuickLookOverlay::reposition()
{
    if (m_content == nullptr) {
        return;
    }

    const QSize available = size();
    const int width = available.width() * m_percent / 100;
    const int height = available.height() * m_percent / 100;

    m_content->setGeometry((available.width() - width) / 2, (available.height() - height) / 2,
                           width, height);
}

void QuickLookOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    if (m_percent >= 100) {
        // Full mode: nothing shows through, so there is nothing to dim.
        return;
    }

    QPainter painter(this);
    QColor backdrop = currentPalette().background;
    backdrop.setAlpha(kBackdropAlpha);
    painter.fillRect(rect(), backdrop);
}

void QuickLookOverlay::mousePressEvent(QMouseEvent *event)
{
    if (m_content != nullptr && m_content->geometry().contains(event->position().toPoint())) {
        QWidget::mousePressEvent(event);
        return;
    }
    Q_EMIT backdropClicked();
}

} // namespace pf::ui
