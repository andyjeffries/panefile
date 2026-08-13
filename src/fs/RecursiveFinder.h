#pragma once

#include "core/FuzzyMatcher.h"

#include <QObject>
#include <QString>

#include <memory>

namespace pf::fs {

/// One hit from the recursive finder.
struct FindResult {
    QString absolutePath;

    /// Relative to the search root — what the list shows, and what the query
    /// is scored against, so that typing a directory name narrows usefully.
    QString relativePath;

    bool isDir = false;
    int score = 0;
    QList<MatchSpan> spans;
};

/// The recursive fuzzy finder's worker (§7.8).
///
/// §7.8: "a modal that walks the panel's subtree on a worker thread, streaming
/// candidates into a scored list."
///
/// Streaming is the requirement that shapes this. A finder that walks the whole
/// tree and then sorts is unusable on a home directory; one that emits batches
/// as it goes is usable from the first tenth of a second, which is the entire
/// difference between a finder people reach for and one they avoid.
///
/// The walk itself is separate from the modal so §14 can drive it against a
/// fixture directory without a window.
class RecursiveFinder : public QObject
{
    Q_OBJECT

public:
    /// The worker-to-GUI handoff. Public only because the QRunnable that fills
    /// it lives in the .cpp's anonymous namespace and cannot be befriended.
    class Channel;

    /// §7.8's `config.search.max_results` (default 10,000).
    static constexpr int kDefaultMaxResults = 10000;

    /// How many hits accumulate before a batch is emitted. Small enough that
    /// the first results appear immediately, large enough that a matching
    /// directory does not cost one queued signal per file.
    static constexpr int kBatchSize = 64;

    explicit RecursiveFinder(QObject *parent = nullptr);
    ~RecursiveFinder() override;

    void setMaxResults(int maximum);
    void setRespectGitignore(bool respect);
    void setFuzzyMatching(bool fuzzy);
    void setIncludeHidden(bool include);

    /// Starts a walk of `root` for `query`. Supersedes anything in flight.
    /// An empty query lists everything, capped the same way.
    void search(const QString &root, const QString &query);

    /// Abandons the walk. Safe to call when nothing is running.
    void cancel();

    bool isRunning() const;

Q_SIGNALS:
    /// A batch of hits, best-effort ordered within the batch. The receiver
    /// merges and re-sorts; ordering across batches is not this class's job.
    void resultsReady(const QList<pf::fs::FindResult> &results);

    /// The walk ended. `truncated` is true when it stopped at max_results.
    void finished(int total, bool truncated);

private:
    std::shared_ptr<Channel> m_channel;

    int m_maxResults = kDefaultMaxResults;
    bool m_respectGitignore = true;
    bool m_fuzzy = true;
    bool m_includeHidden = false;
};

} // namespace pf::fs

Q_DECLARE_METATYPE(pf::fs::FindResult)
