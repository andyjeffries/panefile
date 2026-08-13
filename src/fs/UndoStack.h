#pragma once

#include "fs/Trash.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace pf::fs {

/// One reversible operation (§7.13).
struct UndoEntry {
    enum class Kind {
        Move,
        Rename,
        BulkRename,
        Trash,
    };

    Kind kind = Kind::Move;
    QString description;

    /// For Move and Rename: what ended up where. Undo moves each back.
    QList<QPair<QString, QString>> movedPairs;

    /// For Trash: the items, which carry where they came from.
    QList<TrashedItem> trashedItems;
};

/// A bounded stack of undoable operations (§7.13).
///
/// "Maintain a bounded stack (50 entries) of undoable operations: move, rename,
/// bulk rename, trash. Ctrl+Z undoes the last. Copy and permanent delete are
/// **not** undoable and must be labelled as such in the confirmation."
///
/// That last sentence is the reason this class refuses entries rather than
/// accepting anything offered: a copy leaves files that undo would have to
/// delete, and deleting files to undo a copy is a worse operation than the one
/// it reverses. Permanent deletion has nothing to restore from at all.
class UndoStack : public QObject
{
    Q_OBJECT

public:
    static constexpr int kCapacity = 50;

    explicit UndoStack(QObject *parent = nullptr);

    void push(UndoEntry entry);

    bool canUndo() const;

    /// A description of what undoing would do, for the menu and the footer.
    QString nextDescription() const;

    /// Undoes the most recent operation. Returns false with `error` set when it
    /// cannot — a destination now occupied, a file gone since.
    bool undo(QString *error = nullptr);

    /// Uses a specific trash, for testing.
    void setTrash(Trash trash);

    int size() const;
    void clear();

Q_SIGNALS:
    void changed();

private:
    QList<UndoEntry> m_entries;
    Trash m_trash;
};

} // namespace pf::fs
