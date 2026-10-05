#include "ui/DefaultFileManagerBar.h"

#include "ui/ThemePalette.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTimer>

namespace pf::ui {
namespace {

QPushButton *makeButton(const QString &objectName)
{
    auto *button = new QPushButton;
    button->setObjectName(objectName);
    // Never the focus: showing the bar must not take the keyboard away from
    // the panel the user is typing into, and Tab belongs to panel cycling.
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QString labelWithHint(const QString &label, const QString &hint)
{
    return hint.isEmpty() ? label : QStringLiteral("%1   %2").arg(label, hint);
}

} // namespace

DefaultFileManagerBar::DefaultFileManagerBar(QWidget *parent)
    : QWidget(parent), m_text(new QLabel), m_yes(makeButton(QStringLiteral("noticeBarPrimary"))),
      m_notNow(makeButton(QStringLiteral("noticeBarButton"))),
      m_never(makeButton(QStringLiteral("noticeBarButton"))), m_hideTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("noticeBar"));
    // A plain QWidget paints no stylesheet background without this.
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::NoFocus);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(currentPalette().panelPadding, 5, currentPalette().panelPadding, 5);
    layout->setSpacing(8);

    m_text->setObjectName(QStringLiteral("noticeBarText"));
    m_text->setTextFormat(Qt::PlainText);
    m_text->setWordWrap(false);
    layout->addWidget(m_text, 1);

    layout->addWidget(m_yes);
    layout->addWidget(m_notNow);
    layout->addWidget(m_never);
    setKeyHints({}, {}, {});

    m_yes->setAccessibleName(tr("Yes, make Panefile the default file manager"));
    m_notNow->setAccessibleName(tr("Not now"));
    m_never->setAccessibleName(tr("Never ask again"));

    connect(m_yes, &QPushButton::clicked, this, &DefaultFileManagerBar::accepted);
    connect(m_notNow, &QPushButton::clicked, this, &DefaultFileManagerBar::dismissed);
    connect(m_never, &QPushButton::clicked, this, &DefaultFileManagerBar::declined);

    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(kResultMs);
    connect(m_hideTimer, &QTimer::timeout, this, [this] {
        m_showingResult = false;
        hide();
        Q_EMIT finished();
    });

    hide();
}

void DefaultFileManagerBar::setKeyHints(const QString &yes, const QString &notNow,
                                        const QString &never)
{
    m_yes->setText(labelWithHint(tr("Yes"), yes));
    m_notNow->setText(labelWithHint(tr("Not now"), notNow));
    m_never->setText(labelWithHint(tr("Never"), never));
}

void DefaultFileManagerBar::setButtonsVisible(bool visible)
{
    m_yes->setVisible(visible);
    m_notNow->setVisible(visible);
    m_never->setVisible(visible);
    m_yes->setEnabled(visible);
    m_notNow->setEnabled(visible);
    m_never->setEnabled(visible);
}

void DefaultFileManagerBar::showOffer()
{
    m_hideTimer->stop();
    m_offering = true;
    m_showingResult = false;
    m_text->setProperty("noticeState", QString());
    m_text->style()->unpolish(m_text);
    m_text->style()->polish(m_text);
    m_text->setToolTip(QString());
    m_text->setText(tr("Make Panefile your default file manager?"));
    setButtonsVisible(true);
    show();
}

void DefaultFileManagerBar::showBusy(const QString &text)
{
    m_hideTimer->stop();
    m_offering = false;
    m_showingResult = false;
    m_text->setText(text);
    setButtonsVisible(false);
    show();
}

void DefaultFileManagerBar::showResult(const QString &text, bool success)
{
    m_offering = false;
    m_showingResult = true;
    m_text->setProperty("noticeState", success ? QString() : QStringLiteral("error"));
    m_text->style()->unpolish(m_text);
    m_text->style()->polish(m_text);
    m_text->setText(text);
    m_text->setToolTip(text);
    setButtonsVisible(false);
    show();
    m_hideTimer->start();
}

// isHidden() rather than isVisible(): the question is whether the bar is up,
// not whether its window happens to be on screen at this moment.
bool DefaultFileManagerBar::isOffering() const
{
    return m_offering && !isHidden();
}

bool DefaultFileManagerBar::isShowingResult() const
{
    return m_showingResult && !isHidden();
}

QString DefaultFileManagerBar::text() const
{
    return m_text->text();
}

} // namespace pf::ui
