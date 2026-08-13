#include "fs/Job.h"

#include "core/Logging.h"

#include <QMutex>
#include <QWaitCondition>

namespace pf::fs {

/// Carries a conflict question to the GUI thread and the answer back.
///
/// §7.4 says conflict() "blocks until resolved", which means the worker must
/// wait for a decision made on another thread. A mutex and a condition variable
/// are the whole mechanism; the alternative — suspending and resuming the job
/// as a state machine — would spread one question across three call sites.
class Job::ConflictChannel
{
public:
    void ask() { m_answered = false; }

    ConflictResolution wait(const std::atomic<bool> &cancelled)
    {
        const QMutexLocker locker(&m_mutex);
        while (!m_answered) {
            if (cancelled.load(std::memory_order_relaxed)) {
                return ConflictResolution{.action = ConflictAction::Cancel, .applyToAll = true};
            }
            // A bounded wait rather than an indefinite one, so a cancel that
            // arrives while the user is staring at the modal is noticed rather
            // than leaving the worker parked forever.
            m_condition.wait(&m_mutex, 100);
        }
        return m_resolution;
    }

    void answer(const ConflictResolution &resolution)
    {
        const QMutexLocker locker(&m_mutex);
        m_resolution = resolution;
        m_answered = true;
        m_condition.wakeAll();
    }

private:
    QMutex m_mutex;
    QWaitCondition m_condition;
    ConflictResolution m_resolution;
    bool m_answered = false;
};

Job::Job(QObject *parent) : QObject(parent), m_conflictChannel(std::make_unique<ConflictChannel>())
{}

Job::~Job() = default;

Job::State Job::state() const
{
    return m_state.load(std::memory_order_relaxed);
}

JobResult Job::result() const
{
    return m_result;
}

void Job::cancel()
{
    m_cancelled.store(true, std::memory_order_relaxed);
}

bool Job::isCancelled() const
{
    return m_cancelled.load(std::memory_order_relaxed);
}

void Job::resolveConflict(const ConflictResolution &resolution)
{
    m_conflictChannel->answer(resolution);
}

ConflictResolution Job::askAboutConflict(const QString &source, const QString &destination,
                                         const ConflictInfo &info)
{
    // §7.4's "apply to all remaining": once chosen, later conflicts are
    // answered from the remembered decision. Asking again would make the
    // checkbox meaningless.
    if (m_blanketAction.has_value()) {
        return ConflictResolution{.action = *m_blanketAction, .applyToAll = true};
    }

    m_state.store(State::AwaitingConflictResolution, std::memory_order_relaxed);
    m_conflictChannel->ask();
    Q_EMIT conflict(source, destination, info);

    const ConflictResolution resolution = m_conflictChannel->wait(m_cancelled);
    m_state.store(State::Running, std::memory_order_relaxed);

    if (resolution.applyToAll) {
        m_blanketAction = resolution.action;
    }
    if (resolution.action == ConflictAction::Cancel) {
        cancel();
    }
    return resolution;
}

void Job::reportProgress(const QString &currentPath)
{
    Q_EMIT progress(m_bytesDone, m_bytesTotal, m_filesDone, m_filesTotal, currentPath);
}

void Job::addError(const QString &path, const QString &reason)
{
    // §12: accumulate and continue. One unreadable file in five hundred must
    // not abandon the other four hundred and ninety-nine.
    qCDebug(pfJobs) << "job error:" << path << reason;
    m_result.errors.append(JobError{.path = path, .reason = reason});
}

void Job::run()
{
    Q_EMIT started();

    // §7.4: "Two-phase: enumerate first (count files and bytes), then execute.
    // This gives real progress instead of a spinner."
    m_state.store(State::Enumerating, std::memory_order_relaxed);
    const bool canProceed = enumerate();

    if (!canProceed || isCancelled()) {
        m_result.cancelled = isCancelled();
        m_state.store(State::Finished, std::memory_order_relaxed);
        Q_EMIT finished(m_result);
        return;
    }

    qCDebug(pfJobs) << description() << "—" << m_filesTotal << "files," << m_bytesTotal << "bytes";

    m_state.store(State::Running, std::memory_order_relaxed);
    execute();

    m_result.cancelled = isCancelled();
    m_result.filesProcessed = m_filesDone;
    m_result.bytesProcessed = m_bytesDone;

    m_state.store(State::Finished, std::memory_order_relaxed);
    Q_EMIT finished(m_result);
}

} // namespace pf::fs
