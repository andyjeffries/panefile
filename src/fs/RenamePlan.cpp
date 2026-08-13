#include "fs/RenamePlan.h"

#include <QCoreApplication>
#include <QSet>

namespace pf::fs {
namespace {

/// Whether `name` is usable as a basename.
RenameProblem validate(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        return RenameProblem::EmptyName;
    }
    // §7.9 renames within a directory. A separator would make it a move, which
    // is a different operation with different conflict rules, and accepting it
    // here would let a bulk rename quietly write outside the directory.
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        return RenameProblem::PathSeparator;
    }
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return RenameProblem::ReservedName;
    }
    return RenameProblem::None;
}

} // namespace

QString RenamePlan::problemText() const
{
    switch (problem) {
    case RenameProblem::None:
        return {};
    case RenameProblem::EmptyName:
        return QCoreApplication::translate("RenamePlan", "“%1” would be left with no name")
            .arg(offendingName);
    case RenameProblem::PathSeparator:
        return QCoreApplication::translate("RenamePlan",
                                           "“%1” contains a path separator — a rename cannot "
                                           "move a file to another directory")
            .arg(offendingName);
    case RenameProblem::DuplicateTarget:
        return QCoreApplication::translate("RenamePlan", "Two items would both be named “%1”")
            .arg(offendingName);
    case RenameProblem::CollidesExisting:
        return QCoreApplication::translate("RenamePlan", "“%1” already exists").arg(offendingName);
    case RenameProblem::ReservedName:
        return QCoreApplication::translate("RenamePlan", "“%1” is not a usable name")
            .arg(offendingName);
    }
    return {};
}

QString RenamePlanner::temporaryNameFor(const QString &original, int index)
{
    return original + QStringLiteral(".pf-rename-%1").arg(index);
}

RenamePlan RenamePlanner::plan(const QList<RenamePair> &requested,
                               const QList<QString> &existingNames)
{
    RenamePlan result;

    const QSet<QString> existing(existingNames.constBegin(), existingNames.constEnd());

    QSet<QString> sources;
    QSet<QString> targets;

    // ------------------------------------------------------------ validation
    for (const RenamePair &pair : requested) {
        if (pair.from == pair.to) {
            // Unchanged entries are not errors and are not work. They still
            // count as sources, because a *different* entry renaming onto this
            // name is a genuine collision.
            sources.insert(pair.from);
            continue;
        }

        if (const RenameProblem problem = validate(pair.to); problem != RenameProblem::None) {
            result.problem = problem;
            result.offendingName = pair.to.trimmed().isEmpty() ? pair.from : pair.to;
            return result;
        }

        if (targets.contains(pair.to)) {
            result.problem = RenameProblem::DuplicateTarget;
            result.offendingName = pair.to;
            return result;
        }

        sources.insert(pair.from);
        targets.insert(pair.to);
        result.changes.append(pair);
    }

    for (const RenamePair &pair : result.changes) {
        // A target that exists is only a collision if nothing is vacating it.
        // Without this, renaming a→b while b→c would be rejected even though
        // it is perfectly well defined once ordered.
        if (existing.contains(pair.to) && !sources.contains(pair.to)) {
            result.problem = RenameProblem::CollidesExisting;
            result.offendingName = pair.to;
            return result;
        }
    }

    if (result.changes.isEmpty()) {
        return result;
    }

    // ------------------------------------------------------------- ordering
    //
    // Rename a→b only once nothing still occupies b. Repeatedly emitting every
    // step whose target is free resolves every chain; whatever is left when no
    // step is free is a cycle, and a cycle needs a temporary to break it.

    QList<RenamePair> pending = result.changes;
    QSet<QString> occupied = sources;

    // Every name a step has already produced. A chain c→d, b→c, a→b must not
    // let the later steps think their targets are free before the earlier ones
    // have run.
    for (const RenamePair &pair : result.changes) {
        if (existing.contains(pair.to)) {
            occupied.insert(pair.to);
        }
    }

    int temporaryIndex = 0;

    while (!pending.isEmpty()) {
        QList<RenamePair> stillPending;
        bool progressed = false;

        for (const RenamePair &pair : std::as_const(pending)) {
            if (occupied.contains(pair.to)) {
                stillPending.append(pair);
                continue;
            }

            result.steps.append(RenameStep{.from = pair.from, .to = pair.to});
            occupied.remove(pair.from);
            occupied.insert(pair.to);
            progressed = true;
        }

        if (progressed) {
            pending = stillPending;
            continue;
        }

        // Nothing could move: every remaining step is part of a cycle. Move one
        // of them aside, which frees its source name and lets the rest unwind.
        const RenamePair victim = pending.constFirst();
        const QString temporary = temporaryNameFor(victim.from, temporaryIndex++);

        result.steps.append(RenameStep{.from = victim.from, .to = temporary, .viaTemporary = true});
        occupied.remove(victim.from);

        pending.removeFirst();
        pending.append(RenamePair{.from = temporary, .to = victim.to});
    }

    return result;
}

} // namespace pf::fs
