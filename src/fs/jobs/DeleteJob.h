#pragma once

#include "fs/Job.h"
#include "fs/Trash.h"

namespace pf::fs {

/// Moves paths to the trash, or deletes them permanently (§7.5, §6.3).
///
/// One class for both, because the enumeration and the error handling are
/// identical and only the per-item verb differs. Which one it is matters a
/// great deal to the user, though, so the mode is explicit at construction and
/// reflected in description() — a process bar reading "Deleting" when it means
/// "Moving to trash" would be a lie about whether the work is reversible.
class DeleteJob : public Job
{
    Q_OBJECT

public:
    enum class Mode {
        Trash,
        Permanent,
    };

    DeleteJob(Mode mode, QStringList paths, QObject *parent = nullptr);

    /// Uses a specific trash, for testing against a temporary directory.
    void setTrash(Trash trash);

    QString description() const override;

    /// Where each trashed item ended up, in the order given. Undo needs these.
    QList<TrashedItem> trashedItems() const;

protected:
    bool enumerate() override;
    void execute() override;

private:
    void deleteOne(const QString &path);

    Mode m_mode;
    QStringList m_paths;
    Trash m_trash;
    QList<TrashedItem> m_trashedItems;

    /// Files found while enumerating, so a permanent delete of a tree can
    /// report real progress rather than a spinner.
    QStringList m_files;
    QStringList m_directories;
};

} // namespace pf::fs
