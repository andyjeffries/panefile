#pragma once

#include "fs/Job.h"

#include <QObject>
#include <QThread>

#include <memory>
#include <unordered_map>

namespace pf::fs {

/// Runs jobs on worker threads, capped at §3.3's concurrency limit.
///
/// "A dedicated QThread per active job group, capped at 4 concurrent groups;
/// further jobs queue." A thread each rather than a pool because a job can
/// block for a long time waiting for a conflict answer, and a blocked pool
/// thread would starve jobs behind it.
///
/// The engine owns the jobs. A job stays alive after finishing so the process
/// bar can show its result for the five seconds §7.4 asks for.
class JobEngine : public QObject
{
    Q_OBJECT

public:
    /// §3.3: four concurrent job groups.
    static constexpr int kMaxConcurrentJobs = 4;

    explicit JobEngine(QObject *parent = nullptr);
    ~JobEngine() override;

    /// Takes ownership and starts the job, or queues it if four are running.
    /// Returns the id the process bar and cancel() use.
    int submit(std::unique_ptr<Job> job);

    void cancel(int jobId);
    void cancelAll();

    Job *job(int jobId) const;
    QList<int> activeJobIds() const;
    int activeCount() const;
    int queuedCount() const;

    /// Discards a finished job. Called when the process bar's five seconds are
    /// up, so results linger exactly as long as they are being shown.
    void forget(int jobId);

Q_SIGNALS:
    void jobSubmitted(int jobId, const QString &description);
    void jobStarted(int jobId);
    void jobProgress(int jobId, quint64 bytesDone, quint64 bytesTotal, int filesDone,
                     int filesTotal, const QString &currentPath);
    void jobConflict(int jobId, const QString &source, const QString &destination,
                     const pf::fs::ConflictInfo &info);
    void jobFinished(int jobId, const pf::fs::JobResult &result);

    /// The aggregate the process bar shows when it is collapsed (§5.1).
    void aggregateProgress(quint64 bytesDone, quint64 bytesTotal, int activeJobs);

private:
    struct Entry;

    void startNext();
    void startEntry(Entry &entry);
    void onJobFinished(int jobId, const JobResult &result);
    void emitAggregate();

    // std::unordered_map rather than QHash: an Entry owns its Job through a
    // unique_ptr, and QHash requires a copyable mapped type.
    std::unordered_map<int, std::unique_ptr<Entry>> m_entries;
    QList<int> m_queue;
    QList<int> m_running;
    int m_nextId = 1;
};

} // namespace pf::fs
