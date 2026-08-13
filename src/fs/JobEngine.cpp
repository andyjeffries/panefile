#include "fs/JobEngine.h"

#include "core/Logging.h"

#include <QPointer>
#include <QThread>

namespace pf::fs {

struct JobEngine::Entry {
    std::unique_ptr<Job> job;

    /// QPointer, not a raw pointer, because the thread deletes itself.
    ///
    /// startEntry connects QThread::finished to deleteLater, so a job that has
    /// run to completion leaves this entry pointing at freed memory — and the
    /// entry outlives it, because an entry is only erased by forget(). Calling
    /// quit() on that corpse is not a crash so much as a hang: QThread::quit
    /// locks the thread's own mutex, and locking a mutex in freed memory parks
    /// the caller in __ulock_wait2 forever. On the GUI thread, that is the
    /// whole application beachballing.
    ///
    /// A QPointer reads as null the moment the QThread is destroyed, so every
    /// null check below starts telling the truth. It is safe here because the
    /// thread object lives in this object's thread — `new QThread(this)` — and
    /// deleteLater is delivered there too.
    QPointer<QThread> thread;

    QString description;

    quint64 bytesDone = 0;
    quint64 bytesTotal = 0;
    bool finished = false;
};

JobEngine::JobEngine(QObject *parent) : QObject(parent) {}

JobEngine::~JobEngine()
{
    // Every worker has to stop before the engine's members are destroyed, or a
    // running job emits into a half-torn-down object. Cancellation is
    // cooperative, so this asks and then waits.
    cancelAll();

    for (const auto &[id, entry] : m_entries) {
        if (!entry->thread.isNull()) {
            entry->thread->quit();
            entry->thread->wait(5000);
        }
    }
}

int JobEngine::submit(std::unique_ptr<Job> job)
{
    if (!job) {
        return 0;
    }

    const int id = m_nextId++;
    auto entry = std::make_unique<Entry>();
    entry->description = job->description();
    entry->job = std::move(job);

    const Job *raw = entry->job.get();
    const QString description = entry->description;
    m_entries.emplace(id, std::move(entry));

    connect(raw, &Job::progress, this,
            [this, id](quint64 bytesDone, quint64 bytesTotal, int filesDone, int filesTotal,
                       const QString &currentPath) {
                if (const auto found = m_entries.find(id); found != m_entries.end()) {
                    found->second->bytesDone = bytesDone;
                    found->second->bytesTotal = bytesTotal;
                }
                Q_EMIT jobProgress(id, bytesDone, bytesTotal, filesDone, filesTotal, currentPath);
                emitAggregate();
            });

    connect(
        raw, &Job::conflict, this,
        [this, id](const QString &source, const QString &destination, const ConflictInfo &info) {
            Q_EMIT jobConflict(id, source, destination, info);
        });

    connect(raw, &Job::finished, this,
            [this, id](const JobResult &result) { onJobFinished(id, result); });

    Q_EMIT jobSubmitted(id, description);

    m_queue.append(id);
    startNext();
    return id;
}

void JobEngine::startNext()
{
    while (m_running.size() < kMaxConcurrentJobs && !m_queue.isEmpty()) {
        const int id = m_queue.takeFirst();
        const auto found = m_entries.find(id);
        if (found == m_entries.end()) {
            continue;
        }
        startEntry(*found->second);
        m_running.append(id);
        Q_EMIT jobStarted(id);
    }
}

void JobEngine::startEntry(Entry &entry)
{
    // A thread each, per §3.3, rather than a pool: a job blocked waiting for a
    // conflict answer holds its thread for as long as the user takes to
    // decide, and in a pool that would stall every job queued behind it.
    entry.thread = new QThread(this);
    Job *job = entry.job.get();
    job->moveToThread(entry.thread);

    connect(entry.thread, &QThread::started, job, &Job::run);
    connect(entry.thread, &QThread::finished, entry.thread, &QThread::deleteLater);

    entry.thread->start();
}

void JobEngine::onJobFinished(int jobId, const JobResult &result)
{
    if (const auto found = m_entries.find(jobId); found != m_entries.end()) {
        found->second->finished = true;
        if (!found->second->thread.isNull()) {
            found->second->thread->quit();
        }
    }

    m_running.removeAll(jobId);
    qCDebug(pfJobs) << "job" << jobId << "finished;" << result.errors.size() << "errors";

    Q_EMIT jobFinished(jobId, result);
    emitAggregate();

    // A slot freed, so whatever was waiting for one can start.
    startNext();
}

void JobEngine::emitAggregate()
{
    quint64 done = 0;
    quint64 total = 0;
    int active = 0;

    for (const auto &[id, entry] : m_entries) {
        if (entry->finished) {
            continue;
        }
        done += entry->bytesDone;
        total += entry->bytesTotal;
        ++active;
    }

    Q_EMIT aggregateProgress(done, total, active);
}

void JobEngine::cancel(int jobId)
{
    const auto found = m_entries.find(jobId);
    if (found == m_entries.end()) {
        return;
    }

    // A job still in the queue has no thread to interrupt; cancelling it means
    // taking it out of the queue and reporting it as cancelled, or the process
    // bar would show it forever.
    if (m_queue.removeAll(jobId) > 0) {
        found->second->finished = true;
        Q_EMIT jobFinished(jobId, JobResult{.cancelled = true});
        emitAggregate();
        return;
    }

    found->second->job->cancel();
}

void JobEngine::cancelAll()
{
    QList<int> ids;
    ids.reserve(static_cast<qsizetype>(m_entries.size()));
    for (const auto &[id, entry] : m_entries) {
        ids.append(id);
    }
    for (const int id : ids) {
        cancel(id);
    }
}

Job *JobEngine::job(int jobId) const
{
    const auto found = m_entries.find(jobId);
    return found == m_entries.end() ? nullptr : found->second->job.get();
}

QList<int> JobEngine::activeJobIds() const
{
    return m_running;
}

int JobEngine::activeCount() const
{
    return static_cast<int>(m_running.size());
}

int JobEngine::queuedCount() const
{
    return static_cast<int>(m_queue.size());
}

void JobEngine::forget(int jobId)
{
    const auto found = m_entries.find(jobId);
    if (found == m_entries.end()) {
        return;
    }

    if (!found->second->thread.isNull()) {
        found->second->thread->quit();
        found->second->thread->wait(5000);
    }
    m_entries.erase(found);
}

} // namespace pf::fs
