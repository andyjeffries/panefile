#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <optional>

namespace pf::fs {

/// What a conflicting destination looks like, so the modal can show both sides
/// (§7.4: "Show both files' size and mtime").
struct ConflictInfo {
    quint64 sourceSize = 0;
    quint64 destinationSize = 0;
    QDateTime sourceModified;
    QDateTime destinationModified;
    bool destinationIsDirectory = false;
};

/// §7.4's conflict choices.
enum class ConflictAction {
    Overwrite,
    OverwriteIfNewer,
    Skip,
    Rename, ///< auto-suffix " (2)"
    Cancel, ///< abandon the whole job
};

struct ConflictResolution {
    ConflictAction action = ConflictAction::Skip;
    /// §7.4: "each with an 'apply to all remaining' checkbox".
    bool applyToAll = false;
};

/// One failure within a job that did not stop it (§12).
struct JobError {
    QString path;
    QString reason;
};

struct JobResult {
    bool cancelled = false;
    int filesProcessed = 0;
    quint64 bytesProcessed = 0;
    /// §12: "Non-fatal job errors (one file of 500 failed) accumulate into a
    /// summary shown at job end, listing failures with reasons — the job
    /// continues."
    QList<JobError> errors;

    bool succeeded() const { return !cancelled && errors.isEmpty(); }
};

/// A mutating filesystem operation (§7.4).
///
/// Every job is two-phase: enumerate first to count files and bytes, then
/// execute. That is what gives real progress instead of a spinner, and it is
/// also when a job discovers it cannot proceed — copying a directory into its
/// own descendant, say — while nothing has been written yet.
///
/// Jobs run on a worker thread. Everything crossing back to the GUI thread is a
/// queued signal carrying value types, per §3.3.
class Job : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Pending,
        Enumerating,
        Running,
        AwaitingConflictResolution,
        Finished,
    };

    explicit Job(QObject *parent = nullptr);
    ~Job() override;

    /// Human-readable, for the process bar: "Copying 128 files".
    virtual QString description() const = 0;

    /// Runs the job to completion. Called on a worker thread.
    void run();

    State state() const;
    JobResult result() const;

    /// §7.4: cancellation is cooperative. Safe to call from any thread.
    void cancel();
    bool isCancelled() const;

    /// Answers a conflict() signal. Safe to call from the GUI thread; the
    /// worker is blocked waiting for it.
    void resolveConflict(const ConflictResolution &resolution);

Q_SIGNALS:
    void started();

    /// §7.4: real progress, from the counts the enumeration pass produced.
    void progress(quint64 bytesDone, quint64 bytesTotal, int filesDone, int filesTotal,
                  const QString &currentPath);

    /// Blocks the worker until resolveConflict() is called.
    void conflict(const QString &source, const QString &destination,
                  const pf::fs::ConflictInfo &info);

    void finished(const pf::fs::JobResult &result);

protected:
    /// Counts what is to be done. Sets m_filesTotal and m_bytesTotal.
    virtual bool enumerate() = 0;

    /// Does it. Called only if enumerate() returned true.
    virtual void execute() = 0;

    /// Asks the GUI thread what to do about a conflicting destination, and
    /// blocks until it answers.
    ///
    /// Once "apply to all" has been chosen, later conflicts are answered from
    /// the remembered choice without troubling the user again.
    ConflictResolution askAboutConflict(const QString &source, const QString &destination,
                                        const ConflictInfo &info);

    void reportProgress(const QString &currentPath);
    void addError(const QString &path, const QString &reason);

    quint64 m_bytesTotal = 0;
    quint64 m_bytesDone = 0;
    int m_filesTotal = 0;
    int m_filesDone = 0;

    JobResult m_result;

private:
    std::atomic<bool> m_cancelled{false};
    std::atomic<State> m_state{State::Pending};

    class ConflictChannel;
    std::unique_ptr<ConflictChannel> m_conflictChannel;

    /// Set once the user chooses "apply to all remaining".
    std::optional<ConflictAction> m_blanketAction;
};

} // namespace pf::fs
