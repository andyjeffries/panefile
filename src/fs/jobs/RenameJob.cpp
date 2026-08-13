#include "fs/jobs/RenameJob.h"

#include "core/Logging.h"
#include "fs/FsError.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>

namespace pf::fs {

RenameJob::RenameJob(QString directory, RenamePlan plan, QObject *parent)
    : Job(parent), m_directory(std::move(directory)), m_plan(std::move(plan))
{}

QString RenameJob::description() const
{
    return tr("Renaming %n item(s)", nullptr, static_cast<int>(m_plan.changes.size()));
}

QList<QPair<QString, QString>> RenameJob::completedRenames() const
{
    return m_completed;
}

QList<RenamePair> RenameJob::requestedChanges() const
{
    return m_plan.changes;
}

bool RenameJob::enumerate()
{
    if (!m_plan.isValid()) {
        addError(m_directory, m_plan.problemText());
        return false;
    }

    // The plan is already ordered and cycle-free, so this pass only has to
    // check that what it names still exists. A file removed between the sheet
    // opening and OK being pressed is a real possibility on a watched
    // directory, and it is far better found now than halfway through.
    for (const RenameStep &step : std::as_const(m_plan.steps)) {
        if (step.from.contains(QLatin1String(".pf-rename-"))) {
            // A temporary produced by an earlier step; it does not exist yet.
            continue;
        }
        if (!QFileInfo::exists(QDir(m_directory).absoluteFilePath(step.from))) {
            addError(step.from, describeErrno(ENOENT));
            return false;
        }
    }

    m_filesTotal = static_cast<int>(m_plan.steps.size());
    return m_filesTotal > 0;
}

void RenameJob::execute()
{
    const QDir directory(m_directory);

    for (const RenameStep &step : std::as_const(m_plan.steps)) {
        if (isCancelled()) {
            return;
        }

        const QString from = directory.absoluteFilePath(step.from);
        const QString to = directory.absoluteFilePath(step.to);

        reportProgress(step.from);

        if (!QFile::rename(from, to)) {
            // A failure stops the job rather than continuing. The remaining
            // steps were ordered on the assumption that this one ran, and
            // carrying on would rename files onto names that are still
            // occupied.
            addError(step.from, describeErrno(errno));
            qCWarning(pfFs) << "rename failed" << from << "->" << to;
            return;
        }

        m_completed.append({from, to});
        ++m_filesDone;
    }

    reportProgress(QString());
}

} // namespace pf::fs
