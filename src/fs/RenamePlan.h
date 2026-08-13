#pragma once

#include <QList>
#include <QString>

namespace pf::fs {

/// One rename, as the user asked for it.
struct RenamePair {
    QString from; ///< basename
    QString to;   ///< basename

    bool operator==(const RenamePair &other) const = default;
};

/// One step of the executable plan, after cycles have been broken.
struct RenameStep {
    QString from;
    QString to;

    /// True when `to` is a temporary name that a later step renames again.
    /// Shown differently in the confirmation, because a user seeing
    /// `a → a.pf-rename-tmp-0` with no explanation would reasonably panic.
    bool viaTemporary = false;
};

/// Why a plan could not be built.
enum class RenameProblem {
    None,
    EmptyName,        ///< a new name is empty or only whitespace
    PathSeparator,    ///< a new name contains `/`, which would move the file
    DuplicateTarget,  ///< two entries were renamed to the same name
    CollidesExisting, ///< a new name is an existing file not itself renamed
    ReservedName,     ///< `.` or `..`
};

/// The result of planning a bulk rename (§7.9 steps 3–5).
struct RenamePlan {
    QList<RenameStep> steps;

    /// The pairs that actually change something, for the confirmation modal.
    /// §7.9 step 5: "Show a confirmation modal listing every old → new pair".
    QList<RenamePair> changes;

    RenameProblem problem = RenameProblem::None;

    /// The name the problem is about, for the message.
    QString offendingName;

    bool isValid() const { return problem == RenameProblem::None; }

    /// Human-readable description of `problem`.
    QString problemText() const;
};

/// Turns a set of requested renames into an executable, cycle-free sequence
/// (§7.9).
///
/// §7.9 step 4: "Compute the diff, detect cycles (a→b, b→a) and resolve them
/// via temporary names."
///
/// The cycle case is not exotic. Swapping two files' names is something people
/// do deliberately, and a naive implementation either refuses it or destroys
/// one of the files. Breaking the cycle with a temporary is the whole reason
/// this is a planner rather than a loop.
///
/// Pure and free of any filesystem access except the existing-name set it is
/// given, so §14 can test the ordering and the cycle breaking without a disk.
class RenamePlanner
{
public:
    /// The infix of the temporary names used to break cycles. Includes the
    /// application name so a crash mid-rename leaves something identifiable
    /// rather than an inscrutable temp file.
    static QString temporaryNameFor(const QString &original, int index);

    /// Builds the plan.
    ///
    /// `existingNames` is every name currently in the directory, which is what
    /// lets a rename onto an unrelated existing file be rejected while a
    /// rename onto a name that something else is vacating is allowed.
    static RenamePlan plan(const QList<RenamePair> &requested, const QList<QString> &existingNames);
};

} // namespace pf::fs
