#include "ui/modals/RenameModal.h"

#include "ui/ThemePalette.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pf::ui {

using fs::AddPosition;
using fs::FormatPosition;
using fs::NameFormat;
using fs::RenameMode;
using fs::RenamePair;
using fs::RenamePlan;
using fs::RenamePlanner;
using fs::RenameRule;

RenameModal::RenameModal(QWidget *parent)
    : Modal(parent), m_mode(new QComboBox), m_pages(new QStackedWidget), m_preview(new QTreeWidget),
      m_problem(new QLabel), m_rename(new QPushButton)
{
    setSizePercent(56, 66);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);

    auto *title = new QLabel(tr("Rename Items"));
    title->setObjectName(QStringLiteral("modalTitle"));
    layout->addWidget(title);

    // ------------------------------------------------------------------ mode
    m_mode->setObjectName(QStringLiteral("renameMode"));
    m_mode->addItem(tr("Replace Text"),
                    QVariant::fromValue(static_cast<int>(RenameMode::ReplaceText)));
    m_mode->addItem(tr("Add Text"), QVariant::fromValue(static_cast<int>(RenameMode::AddText)));
    m_mode->addItem(tr("Format"), QVariant::fromValue(static_cast<int>(RenameMode::Format)));
    layout->addWidget(m_mode);

    m_pages->addWidget(buildReplacePage());
    m_pages->addWidget(buildAddPage());
    m_pages->addWidget(buildFormatPage());
    layout->addWidget(m_pages);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        updatePreview();
    });

    // --------------------------------------------------------------- preview
    //
    // §7.9 step 5 wants every old → new pair shown before the rename runs. Here
    // it is live rather than after the fact, which is the whole advantage of a
    // sheet over an editor round trip: a mistake is visible while it is still
    // being typed.
    m_preview->setObjectName(QStringLiteral("renamePreview"));
    m_preview->setColumnCount(2);
    m_preview->setHeaderLabels({tr("Current name"), tr("New name")});
    m_preview->setRootIsDecorated(false);
    m_preview->setUniformRowHeights(true);
    m_preview->setFocusPolicy(Qt::NoFocus);
    m_preview->header()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_preview, 1);

    m_problem->setObjectName(QStringLiteral("renameProblem"));
    m_problem->setWordWrap(true);
    m_problem->hide();
    layout->addWidget(m_problem);

    // --------------------------------------------------------------- buttons
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);

    auto *cancel = new QPushButton(tr("Cancel"));
    cancel->setFocusPolicy(Qt::NoFocus);
    connect(cancel, &QPushButton::clicked, this, &RenameModal::dismiss);
    buttons->addWidget(cancel);

    m_rename->setText(tr("Rename"));
    m_rename->setDefault(true);
    connect(m_rename, &QPushButton::clicked, this, &RenameModal::accept);
    buttons->addWidget(m_rename);

    layout->addLayout(buttons);
}

QWidget *RenameModal::buildReplacePage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);

    m_find = new QLineEdit;
    m_replace = new QLineEdit;
    m_caseSensitive = new QCheckBox(tr("Match case"));

    form->addRow(tr("Find:"), m_find);
    form->addRow(tr("Replace with:"), m_replace);
    form->addRow(QString(), m_caseSensitive);

    connect(m_find, &QLineEdit::textChanged, this, &RenameModal::updatePreview);
    connect(m_replace, &QLineEdit::textChanged, this, &RenameModal::updatePreview);
    connect(m_caseSensitive, &QCheckBox::toggled, this, &RenameModal::updatePreview);

    return page;
}

QWidget *RenameModal::buildAddPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);

    m_addText = new QLineEdit;
    m_addPosition = new QComboBox;
    m_addPosition->addItem(tr("before name"));
    m_addPosition->addItem(tr("after name"));

    form->addRow(tr("Add:"), m_addText);
    form->addRow(QString(), m_addPosition);

    connect(m_addText, &QLineEdit::textChanged, this, &RenameModal::updatePreview);
    connect(m_addPosition, &QComboBox::currentIndexChanged, this, &RenameModal::updatePreview);

    return page;
}

QWidget *RenameModal::buildFormatPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);
    form->setContentsMargins(0, 0, 0, 0);

    m_nameFormat = new QComboBox;
    m_nameFormat->addItem(tr("Name and Index"));
    m_nameFormat->addItem(tr("Name and Counter"));
    m_nameFormat->addItem(tr("Name and Date"));

    m_customText = new QLineEdit;
    m_customText->setPlaceholderText(tr("Custom Format"));

    m_formatPosition = new QComboBox;
    m_formatPosition->addItem(tr("after name"));
    m_formatPosition->addItem(tr("before name"));

    m_startNumber = new QSpinBox;
    m_startNumber->setRange(0, 999999);
    m_startNumber->setValue(1);

    form->addRow(tr("Name format:"), m_nameFormat);
    form->addRow(tr("Custom format:"), m_customText);
    form->addRow(tr("Where:"), m_formatPosition);
    form->addRow(tr("Start numbers at:"), m_startNumber);

    connect(m_nameFormat, &QComboBox::currentIndexChanged, this, [this](int index) {
        // A date has nothing to number from, so the control that would do
        // nothing is disabled rather than left to mislead.
        m_startNumber->setEnabled(index != static_cast<int>(NameFormat::NameAndDate));
        updatePreview();
    });
    connect(m_customText, &QLineEdit::textChanged, this, &RenameModal::updatePreview);
    connect(m_formatPosition, &QComboBox::currentIndexChanged, this, &RenameModal::updatePreview);
    connect(m_startNumber, &QSpinBox::valueChanged, this, &RenameModal::updatePreview);

    return page;
}

void RenameModal::setNames(const QList<QString> &names)
{
    m_names = names;
}

void RenameModal::setExistingNames(const QList<QString> &names)
{
    m_existingNames = names;
}

fs::RenameRule RenameModal::currentRule() const
{
    RenameRule rule;
    rule.mode = static_cast<RenameMode>(m_mode->currentIndex());

    rule.find = m_find->text();
    rule.replaceWith = m_replace->text();
    rule.caseSensitive = m_caseSensitive->isChecked();

    rule.addText = m_addText->text();
    rule.addPosition = static_cast<AddPosition>(m_addPosition->currentIndex());

    rule.nameFormat = static_cast<NameFormat>(m_nameFormat->currentIndex());
    rule.formatPosition = static_cast<FormatPosition>(m_formatPosition->currentIndex());
    rule.customText = m_customText->text();
    rule.startNumber = m_startNumber->value();

    return rule;
}

void RenameModal::start()
{
    m_mode->setCurrentIndex(0);
    m_find->clear();
    m_replace->clear();
    m_addText->clear();
    m_customText->clear();
    m_startNumber->setValue(1);

    updatePreview();
    showModal();
    m_find->setFocus(Qt::ShortcutFocusReason);
}

void RenameModal::updatePreview()
{
    const RenameRule rule = currentRule();
    const QList<QString> renamed = rule.applyAll(m_names);

    m_preview->clear();

    QList<RenamePair> requested;
    requested.reserve(m_names.size());

    for (int i = 0; i < m_names.size(); ++i) {
        requested.append(RenamePair{.from = m_names.at(i), .to = renamed.at(i)});

        auto *item = new QTreeWidgetItem(m_preview);
        item->setText(0, m_names.at(i));
        item->setText(1, renamed.at(i));

        if (m_names.at(i) == renamed.at(i)) {
            // Unchanged rows are dimmed rather than hidden: a rule that only
            // matched half the selection is worth noticing before pressing OK.
            item->setForeground(0, currentPalette().subtext);
            item->setForeground(1, currentPalette().subtext);
        }
    }

    const RenamePlan plan = RenamePlanner::plan(requested, m_existingNames);

    if (!plan.isValid()) {
        m_problem->setText(plan.problemText());
        m_problem->setStyleSheet(
            QStringLiteral("color: %1;").arg(currentPalette().error.name(QColor::HexRgb)));
        m_problem->show();
        m_rename->setEnabled(false);
        return;
    }

    m_problem->hide();
    m_rename->setEnabled(!plan.changes.isEmpty());
}

void RenameModal::accept()
{
    const RenameRule rule = currentRule();
    const QList<QString> renamed = rule.applyAll(m_names);

    QList<RenamePair> requested;
    for (int i = 0; i < m_names.size(); ++i) {
        requested.append(RenamePair{.from = m_names.at(i), .to = renamed.at(i)});
    }

    const RenamePlan plan = RenamePlanner::plan(requested, m_existingNames);
    if (!plan.isValid() || plan.changes.isEmpty()) {
        // The button is disabled in this state, so reaching here means Enter
        // was pressed. Refusing silently is better than running a plan the
        // sheet has already said is wrong.
        return;
    }

    Q_EMIT renameRequested(plan);
    Modal::accept();
}

} // namespace pf::ui
