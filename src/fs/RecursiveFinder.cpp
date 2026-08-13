#include "fs/RecursiveFinder.h"

#include "core/Logging.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>

#include <algorithm>
#include <atomic>

namespace pf::fs {
namespace {

/// A dedicated pool with one thread. The walk is IO-bound and there is only
/// ever one of them: a second would compete with the first for the same disk
/// and deliver results no sooner.
QThreadPool *finderPool()
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(1);
        pool.setObjectName(QStringLiteral("pf-finder"));
        return true;
    }();
    Q_UNUSED(configured)
    return &pool;
}

/// Directories never worth walking, whatever .gitignore says.
///
/// `.git` is the important one: it holds tens of thousands of files that no
/// user is ever searching for by name, and walking it can be most of the cost
/// of searching a repository.
bool isAlwaysSkipped(const QString &name)
{
    return name == QLatin1String(".git") || name == QLatin1String(".hg") ||
           name == QLatin1String(".svn");
}

/// The subset of .gitignore this understands: one pattern per line, comments
/// and blanks skipped, negations ignored.
///
/// Deliberately not a full implementation. §7.8 asks the finder to "respect
/// .gitignore", and the value of that is not walking `node_modules` and
/// `target` — which simple patterns cover. A complete implementation would need
/// precedence between nested files, negation, and `**` semantics, and getting
/// those subtly wrong would hide files from a search, which is worse than
/// occasionally walking one directory too many.
class GitignoreRules
{
public:
    void addFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return;
        }

        while (!file.atEnd()) {
            QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) ||
                line.startsWith(QLatin1Char('!'))) {
                continue;
            }
            // A trailing slash means "directory only", which is how it is
            // matched here anyway.
            if (line.endsWith(QLatin1Char('/'))) {
                line.chop(1);
            }
            // A leading slash anchors to the repository root; without a full
            // implementation the anchoring is dropped and the name matched
            // anywhere, which over-matches rather than under-matches.
            if (line.startsWith(QLatin1Char('/'))) {
                line.remove(0, 1);
            }
            if (!line.isEmpty() && !line.contains(QLatin1Char('/'))) {
                m_patterns.append(line);
            }
        }
    }

    bool ignores(const QString &name) const
    {
        return std::ranges::any_of(m_patterns, [&name](const QString &pattern) {
            if (!pattern.contains(QLatin1Char('*'))) {
                return name == pattern;
            }
            const QRegularExpression expression{
                QRegularExpression::wildcardToRegularExpression(pattern)};
            return expression.match(name).hasMatch();
        });
    }

private:
    QStringList m_patterns;
};

} // namespace

/// The cancellation flag and the result sink, shared with the worker.
class RecursiveFinder::Channel : public QObject
{
    Q_OBJECT

public:
    std::atomic<bool> cancelled{false};

Q_SIGNALS:
    void batch(const QList<pf::fs::FindResult> &results);
    void done(int total, bool truncated);
};

namespace {

class FindTask : public QRunnable
{
public:
    FindTask(std::shared_ptr<RecursiveFinder::Channel> channel, QString root, QString query,
             int maxResults, bool respectGitignore, bool fuzzy, bool includeHidden)
        : m_channel(std::move(channel)), m_root(std::move(root)), m_query(std::move(query)),
          m_maxResults(maxResults), m_respectGitignore(respectGitignore), m_fuzzy(fuzzy),
          m_includeHidden(includeHidden)
    {
        setAutoDelete(true);
    }

    /// The query against one candidate, under whichever rule is configured.
    /// Separated out because expressing it inline needs a conditional inside a
    /// conditional, which reads as a puzzle rather than as two rules.
    FuzzyMatch matchQuery(const QString &candidate) const
    {
        if (m_query.isEmpty()) {
            return FuzzyMatch{.matched = true};
        }
        return m_fuzzy ? FuzzyMatcher::match(m_query, candidate)
                       : FuzzyMatcher::matchSubstring(m_query, candidate);
    }

    void run() override
    {
        const QDir rootDir(m_root);
        QList<FindResult> batch;
        int total = 0;
        bool truncated = false;

        // An explicit stack rather than QDirIterator::Subdirectories, because
        // the pruning is the point: a recursive iterator has already descended
        // into node_modules by the time you could tell it not to.
        QStringList pending{m_root};

        while (!pending.isEmpty()) {
            if (m_channel->cancelled.load(std::memory_order_relaxed)) {
                return;
            }

            const QString directory = pending.takeFirst();

            GitignoreRules rules;
            if (m_respectGitignore) {
                rules.addFile(directory + QStringLiteral("/.gitignore"));
            }

            QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
            if (m_includeHidden) {
                filters |= QDir::Hidden;
            }

            QDirIterator iterator(directory, filters);
            while (iterator.hasNext()) {
                if (m_channel->cancelled.load(std::memory_order_relaxed)) {
                    return;
                }

                const QString path = iterator.next();
                const QFileInfo info = iterator.fileInfo();
                const QString name = info.fileName();

                if (isAlwaysSkipped(name) || (m_respectGitignore && rules.ignores(name))) {
                    continue;
                }

                const bool isDir = info.isDir() && !info.isSymLink();
                if (isDir) {
                    pending.append(path);
                }

                const QString relative = rootDir.relativeFilePath(path);

                // Scored against the relative path, not the basename, so typing
                // part of a directory name narrows the way a user expects.
                const FuzzyMatch match = matchQuery(relative);
                if (!match.matched) {
                    continue;
                }

                batch.append(FindResult{.absolutePath = path,
                                        .relativePath = relative,
                                        .isDir = isDir,
                                        .score = match.score,
                                        .spans = match.spans});
                ++total;

                if (batch.size() >= RecursiveFinder::kBatchSize) {
                    Q_EMIT m_channel->batch(batch);
                    batch.clear();
                }

                if (total >= m_maxResults) {
                    // §7.8: "Cap the recursive walk at config.search.max_results".
                    truncated = true;
                    pending.clear();
                    break;
                }
            }
        }

        if (!batch.isEmpty()) {
            Q_EMIT m_channel->batch(batch);
        }
        if (!m_channel->cancelled.load(std::memory_order_relaxed)) {
            Q_EMIT m_channel->done(total, truncated);
        }
    }

private:
    std::shared_ptr<RecursiveFinder::Channel> m_channel;
    QString m_root;
    QString m_query;
    int m_maxResults;
    bool m_respectGitignore;
    bool m_fuzzy;
    bool m_includeHidden;
};

} // namespace

RecursiveFinder::RecursiveFinder(QObject *parent) : QObject(parent) {}

RecursiveFinder::~RecursiveFinder()
{
    cancel();
}

void RecursiveFinder::setMaxResults(int maximum)
{
    m_maxResults = maximum;
}

void RecursiveFinder::setRespectGitignore(bool respect)
{
    m_respectGitignore = respect;
}

void RecursiveFinder::setFuzzyMatching(bool fuzzy)
{
    m_fuzzy = fuzzy;
}

void RecursiveFinder::setIncludeHidden(bool include)
{
    m_includeHidden = include;
}

bool RecursiveFinder::isRunning() const
{
    return m_channel != nullptr;
}

void RecursiveFinder::cancel()
{
    if (!m_channel) {
        return;
    }
    m_channel->cancelled.store(true, std::memory_order_relaxed);
    // Dropped rather than waited on: the worker holds the other reference and
    // tears the channel down when it notices, so a new search starts at once
    // instead of blocking the GUI thread on a walk that is mid-syscall.
    m_channel.reset();
}

void RecursiveFinder::search(const QString &root, const QString &query)
{
    cancel();

    m_channel = std::make_shared<Channel>();

    connect(m_channel.get(), &Channel::batch, this, &RecursiveFinder::resultsReady,
            Qt::QueuedConnection);
    connect(
        m_channel.get(), &Channel::done, this,
        [this](int total, bool truncated) {
            m_channel.reset();
            Q_EMIT finished(total, truncated);
        },
        Qt::QueuedConnection);

    finderPool()->start(new FindTask(m_channel, root, query, m_maxResults, m_respectGitignore,
                                     m_fuzzy, m_includeHidden));
}

} // namespace pf::fs

#include "RecursiveFinder.moc"
