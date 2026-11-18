#include "ui/ChooserBar.h"

#include "ui/ElidingComboBox.h"
#include "ui/ThemePalette.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QAbstractItemView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>

namespace pf::ui {
namespace {

constexpr int kControlHeight = 30;
constexpr int kGap = 8;
constexpr int kNameWidth = 440;
constexpr int kChoicesGap = 16;
constexpr int kAcceptMaxWidth = 260;

QLabel *plainLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    return label;
}

QString plainToolTip(const QString &text)
{
    return Qt::convertFromPlainText(text, Qt::WhiteSpaceNormal);
}

QComboBox *makeCombo(const QStringList &names)
{
    auto *combo = new ElidingComboBox;
    combo->setMinimumHeight(kControlHeight);
    for (int i = 0; i < names.size(); ++i) {
        combo->addItem(names.at(i));
        combo->setItemData(i, plainToolTip(names.at(i)), Qt::ToolTipRole);
    }
    combo->view()->setMinimumWidth(combo->view()->sizeHintForColumn(0) + 24);
    return combo;
}

} // namespace

ChooserBar::ChooserBar(const platform::FileChooserRequest &request, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("chooserBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    const int padding = currentPalette().panelPadding;
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(padding, 10, padding, 10);
    column->setSpacing(kGap);

    const bool saving = request.mode == platform::FileChooserRequest::Mode::Save;
    if (saving) {
        auto *row = new QHBoxLayout;
        row->setSpacing(kGap);
        auto *label = plainLabel(tr("Name"));
        m_name = new QLineEdit(request.currentName);
        m_name->setObjectName(QStringLiteral("chooserName"));
        m_name->setFixedSize(kNameWidth, kControlHeight);
        label->setBuddy(m_name);
        m_name->installEventFilter(this);
        connect(m_name, &QLineEdit::textChanged, this, &ChooserBar::nameEdited);
        connect(m_name, &QLineEdit::returnPressed, this, [this] {
            if (m_accept->isEnabled()) {
                Q_EMIT acceptRequested();
            }
        });
        row->addWidget(label);
        row->addWidget(m_name);
        row->addStretch(1);
        column->addLayout(row);
    }

    m_controls = new QHBoxLayout;
    m_controls->setSpacing(kGap);

    if (!request.choices.isEmpty()) {
        m_choiceBox = new QWidget;
        m_choiceBox->setObjectName(QStringLiteral("chooserChoices"));
        auto *choicesRow = new QHBoxLayout(m_choiceBox);
        choicesRow->setContentsMargins(0, 0, 0, 0);
        choicesRow->setSpacing(kGap);
        for (const platform::FileChoice &choice : request.choices) {
            QWidget *control = nullptr;
            if (choice.options.isEmpty()) {
                auto *box = new QCheckBox(choice.label);
                box->setChecked(choice.initial == QLatin1String("true"));
                control = box;
            } else {
                QStringList names;
                for (const auto &option : choice.options) {
                    names << option.second;
                }
                auto *combo = makeCombo(names);
                for (int i = 0; i < choice.options.size(); ++i) {
                    combo->setItemData(i, choice.options.at(i).first);
                }
                combo->setCurrentIndex(std::max<int>(0, combo->findData(choice.initial)));
                auto *label = plainLabel(choice.label);
                label->setBuddy(combo);
                choicesRow->addWidget(label);
                control = combo;
            }
            choicesRow->addWidget(control);
            if (&choice != &request.choices.last()) {
                choicesRow->addSpacing(kGap);
            }
            m_choices.append({choice.id, control});
        }
        column->addWidget(m_choiceBox, 0, Qt::AlignLeft);
    }

    m_actions = new QWidget;
    m_actions->setObjectName(QStringLiteral("chooserActions"));
    auto *actions = new QHBoxLayout(m_actions);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(kGap);
    if (!request.filters.isEmpty()) {
        QStringList names;
        for (const platform::FileFilter &filter : request.filters) {
            names << filter.name;
        }
        m_type = makeCombo(names);
        m_type->setObjectName(QStringLiteral("chooserType"));
        m_type->setCurrentIndex(std::max(0, request.currentFilter));
        connect(m_type, &QComboBox::currentIndexChanged, this, &ChooserBar::filterChanged);
        actions->addWidget(plainLabel(tr("Type")));
        actions->addWidget(m_type);
    }

    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setFocusPolicy(Qt::NoFocus);
    cancel->setMinimumHeight(kControlHeight);
    connect(cancel, &QPushButton::clicked, this, &ChooserBar::cancelRequested);
    actions->addWidget(cancel);

    m_accept = new QPushButton;
    m_accept->setObjectName(QStringLiteral("chooserAccept"));
    m_accept->setDefault(true);
    m_accept->setFocusPolicy(Qt::StrongFocus);
    m_accept->setMinimumHeight(kControlHeight);
    connect(m_accept, &QPushButton::clicked, this, &ChooserBar::acceptRequested);
    actions->addWidget(m_accept);

    m_controls->addStretch(1);
    m_controls->addSpacing(kChoicesGap);
    m_controls->addWidget(m_actions);
    column->addLayout(m_controls);
}

void ChooserBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placeChoices();
}

void ChooserBar::placeChoices()
{
    if (m_choiceBox == nullptr) {
        return;
    }
    const QMargins margins = layout()->contentsMargins();
    const int room = width() - margins.left() - margins.right();
    const bool inlineNow =
        m_choiceBox->sizeHint().width() + kChoicesGap + m_actions->sizeHint().width() <= room;
    if (inlineNow == m_choicesInline) {
        return;
    }
    m_choicesInline = inlineNow;
    auto *column = static_cast<QVBoxLayout *>(layout());
    if (inlineNow) {
        column->removeWidget(m_choiceBox);
        m_controls->insertWidget(0, m_choiceBox);
    } else {
        m_controls->removeWidget(m_choiceBox);
        column->insertWidget(column->indexOf(m_controls), m_choiceBox, 0, Qt::AlignLeft);
    }
}

QLineEdit *ChooserBar::nameField() const
{
    return m_name;
}

QString ChooserBar::name() const
{
    return m_name != nullptr ? m_name->text() : QString();
}

void ChooserBar::setName(const QString &name)
{
    if (m_name != nullptr) {
        m_name->setText(name);
    }
}

void ChooserBar::focusName()
{
    if (m_name == nullptr) {
        return;
    }
    m_name->setFocus(Qt::OtherFocusReason);

    // The stem, so typing replaces the name and keeps the extension. A leading
    // dot is part of the name, not an extension.
    const QString text = m_name->text();
    const qsizetype dot = text.lastIndexOf(QLatin1Char('.'));
    m_name->setSelection(0, static_cast<int>(dot > 0 ? dot : text.size()));
}

void ChooserBar::setAcceptLabel(const QString &label)
{
    m_acceptLabel = label;
    // The stylesheet draws the default button semibold, a weight the widget's
    // own font doesn't report, so measure the label in that weight.
    QFont font = m_accept->font();
    font.setWeight(QFont::DemiBold);
    m_accept->setText(QFontMetrics(font).elidedText(label, Qt::ElideMiddle, kAcceptMaxWidth));
    m_accept->setToolTip(m_accept->text() == label ? QString() : plainToolTip(label));
    placeChoices();
}

QString ChooserBar::acceptLabel() const
{
    return m_acceptLabel;
}

void ChooserBar::setAcceptEnabled(bool enabled)
{
    m_accept->setEnabled(enabled);
}

bool ChooserBar::isAcceptEnabled() const
{
    return m_accept->isEnabled();
}

int ChooserBar::filterIndex() const
{
    return m_type != nullptr ? m_type->currentIndex() : -1;
}

QList<QPair<QString, QString>> ChooserBar::choiceValues() const
{
    QList<QPair<QString, QString>> values;
    for (const auto &[id, control] : m_choices) {
        if (const auto *box = qobject_cast<const QCheckBox *>(control); box != nullptr) {
            values.append(
                {id, box->isChecked() ? QStringLiteral("true") : QStringLiteral("false")});
        } else if (const auto *combo = qobject_cast<const QComboBox *>(control); combo != nullptr) {
            values.append({id, combo->currentData().toString()});
        }
    }
    return values;
}

bool ChooserBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_name && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<const QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Escape:
            Q_EMIT cancelRequested();
            return true;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
        case Qt::Key_Down:
            Q_EMIT listFocusRequested();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace pf::ui
