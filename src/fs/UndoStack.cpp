#include "fs/UndoStack.h"

#include "core/Logging.h"
#include "fs/FsError.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cerrno>

namespace pf::fs {

UndoStack::UndoStack(QObject *parent) : QObject(parent) {}

void UndoStack::setTrash(Trash trash)
{
    m_trash = std::move(trash);
}

void UndoStack::push(UndoEntry entry)
{
    m_entries.append(std::move(entry));

    // §7.13's bound. Without it a long session accumulates an entry per
    // operation forever, and the oldest are the least likely to be wanted.
    while (m_entries.size() > kCapacity) {
        m_entries.removeFirst();
    }

    Q_EMIT changed();
}

bool UndoStack::canUndo() const
{
    return !m_entries.isEmpty();
}

QString UndoStack::nextDescription() const
{
    return m_entries.isEmpty() ? QString() : m_entries.constLast().description;
}

int UndoStack::size() const
{
    return static_cast<int>(m_entries.size());
}

void UndoStack::clear()
{
    m_entries.clear();
    Q_EMIT changed();
}

bool UndoStack::undo(QString *error)
{
    if (m_entries.isEmpty()) {
        if (error != nullptr) {
            *error = QObject::tr("Nothing to undo");
        }
        return false;
    }

    const UndoEntry entry = m_entries.takeLast();
    Q_EMIT changed();

    QStringList failures;

    switch (entry.kind) {
    case UndoEntry::Kind::Trash:
        // §7.13: "Undo of a trash operation restores from trash by .trashinfo
        // path." Trash::restore refuses to overwrite, which is what makes this
        // safe to run against a directory the user has kept working in.
        for (const TrashedItem &item : entry.trashedItems) {
            QString itemError;
            if (m_trash.restore(item, &itemError).isEmpty()) {
                failures.append(QStringLiteral("%1: %2").arg(item.originalPath, itemError));
            }
        }
        break;

    case UndoEntry::Kind::Move:
    case UndoEntry::Kind::Rename:
    case UndoEntry::Kind::BulkRename:
        // In reverse, so a rename cycle that was resolved through temporary
        // names unwinds in the order that keeps each destination free.
        QList<QPair<QString, QString>> reversed = entry.movedPairs;
        std::ranges::reverse(reversed);

        for (const auto &[from, to] : reversed) {

            if (!QFileInfo::exists(to)) {
                failures.append(QObject::tr("%1 is no longer there").arg(QFileInfo(to).fileName()));
                continue;
            }
            if (QFileInfo::exists(from)) {
                // Something now occupies the original name. Overwriting it
                // would destroy a file the user created after the operation
                // being undone, which is not what undo is for.
                failures.append(QObject::tr("%1 already exists").arg(from));
                continue;
            }

            QDir().mkpath(QFileInfo(from).absolutePath());
            if (!QFile::rename(to, from)) {
                failures.append(QStringLiteral("%1: %2").arg(to, describeErrno(errno)));
            }
        }
        break;
    }

    if (!failures.isEmpty()) {
        qCWarning(pfJobs) << "undo incomplete:" << failures;
        if (error != nullptr) {
            *error = failures.join(QStringLiteral("; "));
        }
        // Partial success is still reported as failure: the user asked for the
        // operation to be reversed, and it was reversed in part. Saying it
        // worked would leave them believing something untrue about their files.
        return false;
    }

    return true;
}

} // namespace pf::fs
