#include "ui/modals/ConflictModal.h"

#include "core/Format.h"
#include "ui/ThemePalette.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pf::ui {

ConflictModal::ConflictModal(QWidget *parent)
    : Modal(parent), m_question(new QLabel(contentWidget())),
      m_sourceDetail(new QLabel(contentWidget())), m_destinationDetail(new QLabel(contentWidget())),
      m_applyToAll(new QCheckBox(tr("Apply to all remaining"), contentWidget()))
{
    setSizePercent(50, 40);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);

    QFont questionFont = m_question->font();
    questionFont.setBold(true);
    m_question->setFont(questionFont);
    m_question->setWordWrap(true);
    layout->addWidget(m_question);

    m_sourceDetail->setTextFormat(Qt::PlainText);
    m_destinationDetail->setTextFormat(Qt::PlainText);
    layout->addWidget(m_sourceDetail);
    layout->addWidget(m_destinationDetail);

    layout->addWidget(m_applyToAll);
    layout->addStretch(1);

    auto *buttons = new QWidget(contentWidget());
    auto *buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch(1);

    struct Choice {
        QString label;
        fs::ConflictAction action;
        bool isDefault;
    };

    // Skip is the default, and deliberately: Enter is bound to "confirm" in
    // every modal (§5.4), and the least destructive choice is the one that
    // should happen when somebody presses it without reading.
    const QList<Choice> choices{
        {.label = tr("Skip"), .action = fs::ConflictAction::Skip, .isDefault = true},
        {.label = tr("Keep both"), .action = fs::ConflictAction::Rename, .isDefault = false},
        {.label = tr("Overwrite if newer"),
         .action = fs::ConflictAction::OverwriteIfNewer,
         .isDefault = false},
        {.label = tr("Overwrite"), .action = fs::ConflictAction::Overwrite, .isDefault = false},
        {.label = tr("Cancel"), .action = fs::ConflictAction::Cancel, .isDefault = false},
    };

    for (const Choice &choice : choices) {
        auto *button = new QPushButton(choice.label, buttons);
        button->setDefault(choice.isDefault);
        connect(button, &QPushButton::clicked, this,
                [this, action = choice.action] { choose(action); });
        buttonLayout->addWidget(button);
    }

    layout->addWidget(buttons);

    // Esc dismisses every modal (§5.4), but this one has a worker thread
    // blocked waiting for an answer. Dismissing without one would park the job
    // forever, so a dismissal that produced no choice is reported as Skip.
    connect(this, &Modal::dismissed, this, [this] {
        if (!m_answered) {
            Q_EMIT resolved(
                fs::ConflictResolution{.action = fs::ConflictAction::Skip, .applyToAll = false});
        }
    });
}

void ConflictModal::present(const QString &source, const QString &destination,
                            const fs::ConflictInfo &info)
{
    m_question->setText(
        tr("“%1” already exists in the destination").arg(QFileInfo(destination).fileName()));

    // Both sides, with size and mtime, per §7.4 — and the newer one marked,
    // because working out which of two timestamps is later is exactly the
    // arithmetic a dialog should be doing for the reader.
    const bool sourceIsNewer = info.sourceModified > info.destinationModified;

    m_sourceDetail->setText(tr("Copying:  %1   %2%3")
                                .arg(formatSize(info.sourceSize),
                                     formatFullTime(info.sourceModified),
                                     sourceIsNewer ? tr("   (newer)") : QString()));

    m_destinationDetail->setText(tr("Existing: %1   %2%3")
                                     .arg(formatSize(info.destinationSize),
                                          formatFullTime(info.destinationModified),
                                          sourceIsNewer ? QString() : tr("   (newer)")));

    // Unticked each time: "apply to all" is a decision about this run of
    // conflicts, and carrying it over from a previous operation would apply a
    // choice the user made about different files.
    m_applyToAll->setChecked(false);
    m_answered = false;

    Q_UNUSED(source)
    showModal();
}

void ConflictModal::choose(fs::ConflictAction action)
{
    m_answered = true;
    Q_EMIT resolved(
        fs::ConflictResolution{.action = action, .applyToAll = m_applyToAll->isChecked()});
    dismiss();
}

void ConflictModal::accept()
{
    // Enter takes the default, which is Skip.
    choose(fs::ConflictAction::Skip);
}

} // namespace pf::ui
