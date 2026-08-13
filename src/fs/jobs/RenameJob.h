#pragma once

#include "fs/Job.h"
#include "fs/RenamePlan.h"

namespace pf::fs {

/// Executes a rename plan as one job (§7.9 step 6).
///
/// "Execute as a single undoable job." Single is the operative word: a bulk
/// rename of forty files that half-succeeded and left twenty renamed would be
/// worse than one that failed outright, so the steps that did succeed are
/// tracked and handed to the undo stack as one entry.
///
/// The plan arrives already ordered and cycle-free from RenamePlanner. This
/// class does no planning of its own — it renames, in order, and reports.
class RenameJob : public Job
{
    Q_OBJECT

public:
    RenameJob(QString directory, RenamePlan plan, QObject *parent = nullptr);

    QString description() const override;

    /// The renames that actually happened, oldest first, as absolute paths.
    /// Undo walks these backwards.
    ///
    /// Temporary steps are included: a plan broken by a temporary is only
    /// reversible if undo knows the file went through it.
    QList<QPair<QString, QString>> completedRenames() const;

    /// The user-facing pairs, without the temporaries, for the undo entry's
    /// description and the summary.
    QList<RenamePair> requestedChanges() const;

protected:
    bool enumerate() override;
    void execute() override;

private:
    QString m_directory;
    RenamePlan m_plan;
    QList<QPair<QString, QString>> m_completed;
};

} // namespace pf::fs
