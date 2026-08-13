#include "fs/jobs/DeleteJob.h"

#include "core/Logging.h"
#include "fs/FsError.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cerrno>

#include <unistd.h>

namespace pf::fs {

DeleteJob::DeleteJob(Mode mode, QStringList paths, QObject *parent)
    : Job(parent), m_mode(mode), m_paths(std::move(paths))
{}

void DeleteJob::setTrash(Trash trash)
{
    m_trash = std::move(trash);
}

QString DeleteJob::description() const
{
    const int count = m_filesTotal > 0 ? m_filesTotal : static_cast<int>(m_paths.size());
    // The wording distinguishes the reversible operation from the irreversible
    // one. §7.13 makes trashing undoable and permanent deletion not, and the
    // process bar is the last place a user sees which one is running.
    return m_mode == Mode::Trash ? tr("Moving %n item(s) to the trash", nullptr, count)
                                 : tr("Deleting %n item(s)", nullptr, count);
}

QList<TrashedItem> DeleteJob::trashedItems() const
{
    return m_trashedItems;
}

bool DeleteJob::enumerate()
{
    for (const QString &path : std::as_const(m_paths)) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink()) {
            addError(path, describeErrno(ENOENT));
            continue;
        }

        // Trashing a directory is one rename, however much is inside it, so
        // walking the tree to count would be work whose only product is a more
        // precise progress bar for an operation that finishes instantly.
        if (m_mode == Mode::Trash || info.isSymLink() || !info.isDir()) {
            m_files.append(path);
            ++m_filesTotal;
            m_bytesTotal += static_cast<quint64>(info.size());
            continue;
        }

        // A permanent delete does have to walk, both for progress and because
        // the contents must go before the directory. §12: iterative, with an
        // explicit stack.
        QList<QString> pending{path};
        while (!pending.isEmpty()) {
            if (isCancelled()) {
                return false;
            }
            const QString current = pending.takeLast();
            m_directories.append(current);

            const QFileInfoList entries = QDir(current).entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

            for (const QFileInfo &entry : entries) {
                // Never descend through a symlink: §7.4's rule applies with
                // more force here, because following one would delete the
                // target's contents rather than the link.
                if (entry.isDir() && !entry.isSymLink()) {
                    pending.append(entry.absoluteFilePath());
                } else {
                    m_files.append(entry.absoluteFilePath());
                    ++m_filesTotal;
                    m_bytesTotal += static_cast<quint64>(entry.size());
                }
            }
        }
    }

    return !m_files.isEmpty() || !m_directories.isEmpty();
}

void DeleteJob::deleteOne(const QString &path)
{
    if (m_mode == Mode::Trash) {
        QString error;
        const QString destination = m_trash.moveToTrash(path, &error);
        if (destination.isEmpty()) {
            addError(path, error);
            return;
        }

        m_trashedItems.append(TrashedItem{.trashedPath = destination,
                                          .originalPath = QFileInfo(path).absoluteFilePath(),
                                          .deletedAt = QDateTime::currentDateTime(),
                                          .size = 0,
                                          .isDirectory = QFileInfo(destination).isDir()});
        ++m_filesDone;
        reportProgress(path);
        return;
    }

    const QByteArray native = QFile::encodeName(path);
    if (::unlink(native.constData()) != 0) {
        addError(path, describeErrno(errno));
        return;
    }
    ++m_filesDone;
    reportProgress(path);
}

void DeleteJob::execute()
{
    for (const QString &path : std::as_const(m_files)) {
        if (isCancelled()) {
            return;
        }
        deleteOne(path);
    }

    if (m_mode == Mode::Trash) {
        return;
    }

    // Deepest first, so a directory is empty by the time it is removed.
    std::ranges::reverse(m_directories);
    for (const QString &directory : std::as_const(m_directories)) {
        if (isCancelled()) {
            return;
        }
        if (::rmdir(QFile::encodeName(directory).constData()) != 0 && errno != ENOENT) {
            addError(directory, describeErrno(errno));
        }
    }
}

} // namespace pf::fs
