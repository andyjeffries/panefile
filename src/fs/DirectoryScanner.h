#pragma once

#include "model/FileEntry.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QThreadPool;

namespace pf::fs {

/// Enumerates a directory on a worker thread and delivers entries in batches.
///
/// §3.3 puts directory enumeration and stat()ing on a shared pool of four
/// threads, with results reaching the model by queued signal. §7.2 adds two
/// requirements that shape the design more than the threading does:
///
///   * A scan must be cancellable, and a scan that has been superseded must
///     *abandon* its results rather than deliver them. Delivering stale entries
///     into a model that has moved on is worse than delivering nothing.
///
///   * Batches keep the view responsive on huge directories: the first 512
///     entries of a 100,000-entry directory are visible long before the last.
///
/// One scanner belongs to one panel and lives on the GUI thread. All signals
/// are emitted there.
class DirectoryScanner : public QObject
{
    Q_OBJECT

public:
    explicit DirectoryScanner(QObject *parent = nullptr);
    ~DirectoryScanner() override;

    /// §4.2: batches of 512.
    static constexpr int kBatchSize = 512;

    /// Cancels any scan in flight and starts a new one. The signals of the
    /// abandoned scan are never emitted.
    void scan(const QString &path);

    /// Abandons the current scan. No further signals are emitted for it.
    void cancel();

    bool isScanning() const;

    /// The path of the scan in flight, or the last one started.
    QString path() const;

    /// The pool used for enumeration, shared by every scanner in the process
    /// (§3.3: maxThreadCount = 4).
    static QThreadPool *pool();

    /// Carries one scan's signals across the thread boundary. Declared here
    /// only because the worker task in the .cpp has to name it; it is defined
    /// there, so no caller can do anything with it.
    class Channel;

Q_SIGNALS:
    void started(const QString &path);

    /// A batch of up to kBatchSize entries, in readdir order. Sorting is the
    /// proxy's job, not the scanner's.
    void entriesReady(const QString &path, const QList<pf::FileEntry> &entries);

    void finished(const QString &path, int totalCount);

    /// §7.2: the panel shows an inline error state with this reason, and keeps
    /// its previous listing reachable through go_back.
    void failed(const QString &path, const QString &reason);

private:
    void detachCurrentScan();

    std::shared_ptr<Channel> m_channel;
    QString m_path;
    bool m_scanning = false;
};

} // namespace pf::fs
