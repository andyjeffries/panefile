#pragma once

#include "fs/Job.h"
#include "ui/modals/Modal.h"

class QCheckBox;
class QLabel;

namespace pf::ui {

/// Asks what to do about a destination that already exists (§7.4).
///
/// "Conflict resolution offers: overwrite, overwrite if newer, skip, rename
/// (auto-suffix ` (2)`), and each with an 'apply to all remaining' checkbox.
/// Show both files' size and mtime."
///
/// Showing both sides is the requirement that matters. A dialog that says only
/// "file exists, overwrite?" asks the user to decide without the one piece of
/// information — which is newer, which is larger — that the decision turns on.
class ConflictModal : public Modal
{
    Q_OBJECT

public:
    explicit ConflictModal(QWidget *parent);

    void present(const QString &source, const QString &destination, const fs::ConflictInfo &info);

Q_SIGNALS:
    void resolved(const pf::fs::ConflictResolution &resolution);

protected:
    void accept() override;

private:
    void choose(fs::ConflictAction action);

    QLabel *m_question = nullptr;
    QLabel *m_sourceDetail = nullptr;
    QLabel *m_destinationDetail = nullptr;
    QCheckBox *m_applyToAll = nullptr;

    /// Whether a choice has been made for the current conflict. Escape closes
    /// any modal, and this one has a worker blocked on its answer.
    bool m_answered = false;
};

} // namespace pf::ui
