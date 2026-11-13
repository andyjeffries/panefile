#include "model/DirectoryModel.h"

#include "core/Logging.h"
#include "core/WorkerPools.h"
#include "fs/DirectoryScanner.h"
#include "fs/DirectoryWatcher.h"
#include "model/FolderIconStore.h"
#include "model/ThumbnailCache.h"

#include <QCache>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace pf {
namespace {

/// Whether a rescan found anything new about an entry. Ignores mimeName, which
/// is filled in lazily and so is never part of what the scanner reports.
bool sameListing(const FileEntry &a, const FileEntry &b)
{
    return a.name == b.name && a.size == b.size && a.modified == b.modified && a.mode == b.mode &&
           a.uid == b.uid && a.gid == b.gid && a.isDir == b.isDir && a.isSymlink == b.isSymlink &&
           a.isBroken == b.isBroken && a.isHidden == b.isHidden &&
           a.isExecutable == b.isExecutable && a.linkTarget == b.linkTarget &&
           a.statFailed == b.statFailed;
}

} // namespace

DirectoryModel::DirectoryModel(QObject *parent)
    : QAbstractListModel(parent), m_scanner(std::make_unique<fs::DirectoryScanner>())
{
    connect(m_scanner.get(), &fs::DirectoryScanner::entriesReady, this,
            &DirectoryModel::onEntriesReady);
    connect(m_scanner.get(), &fs::DirectoryScanner::finished, this,
            &DirectoryModel::onScanFinished);
    connect(m_scanner.get(), &fs::DirectoryScanner::failed, this, &DirectoryModel::onScanFailed);

    // A folder icon chosen (or reset) for one of this directory's children
    // repaints just that row.
    connect(&FolderIconStore::instance(), &FolderIconStore::changed, this,
            [this](const QString &path) {
                if (QFileInfo(path).absolutePath() != m_path) {
                    return;
                }
                if (const int row = indexOfName(QFileInfo(path).fileName()); row >= 0) {
                    const QModelIndex changed = index(row, 0);
                    Q_EMIT dataChanged(changed, changed, {FolderGlyphRole});
                }
            });
}

DirectoryModel::~DirectoryModel()
{
    cancelSizeProbes();
}

QString DirectoryModel::path() const
{
    return m_path;
}

namespace {

struct CachedSize {
    qint64 modified = 0;
    QSize size;
};

/// Sizes by absolute path, so a folder visited again lays out at once. GUI
/// thread only.
QCache<QString, CachedSize> &sizeCache()
{
    static QCache<QString, CachedSize> cache(50000);
    return cache;
}

constexpr qreal kUnprobedAspect = 1.5;

} // namespace

void DirectoryModel::setPath(const QString &path)
{
    m_path = path;
    ++m_sizeGeneration;
    m_imageSizes.clear();
    cancelSizeProbes();
    m_emptyKnown.clear();
    m_emptyPending.clear();
    // Read here, with the scan, so painting never touches the disk.
    FolderIconStore::instance().ensureLoaded();
    refresh();
}

void DirectoryModel::probeEmpty(const QString &absolutePath) const
{
    if (m_emptyPending.contains(absolutePath)) {
        return;
    }
    m_emptyPending.insert(absolutePath);

    // One readdir, on a single worker, for a directory that has been painted.
    static QThreadPool *pool = WorkerPools::acquire("pf-folder-empty", 1);
    const QPointer<DirectoryModel> self(const_cast<DirectoryModel *>(this));
    pool->start([self, absolutePath] {
        const bool empty = !QDirIterator(absolutePath, QDir::AllEntries | QDir::NoDotAndDotDot |
                                                           QDir::Hidden | QDir::System)
                                .hasNext();
        if (self.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, absolutePath, empty] {
                if (self.isNull() || !self->m_emptyPending.remove(absolutePath)) {
                    return;
                }
                self->m_emptyKnown.insert(absolutePath, empty);
                if (!empty) {
                    return;
                }
                DirectoryModel *model = self.data();
                if (QFileInfo(absolutePath).absolutePath() != model->m_path) {
                    return;
                }
                if (const int row = model->indexOfName(QFileInfo(absolutePath).fileName());
                    row >= 0) {
                    const QModelIndex changed = model->index(row, 0);
                    Q_EMIT model->dataChanged(changed, changed, {FolderEmptyRole});
                }
            },
            Qt::QueuedConnection);
    });
}

void DirectoryModel::refresh()
{
    // Clearing before the scan starts, rather than swapping the contents in
    // when it completes, is what makes a slow directory feel like it is loading
    // rather than like the application has frozen on the previous one.
    m_rescanning = false;
    // A fresh listing is ground truth.
    m_pendingRemovals.clear();
    m_finishAfterRescan.clear();
    beginResetModel();
    m_entries.clear();
    m_error.clear();
    endResetModel();

    m_scanning = true;
    Q_EMIT scanStarted(m_path);
    m_scanner->scan(m_path);

    // The watch is taken alongside the scan rather than after it. A file
    // created while the scan is running would otherwise be missed entirely:
    // too late for the scan to see, too early for the watch to report.
    startWatching();
}

void DirectoryModel::rescan()
{
    // Unlike refresh(), the rows stay where they are while the scan runs and
    // are reconciled as it reports in, so the view keeps its scroll position
    // and the user keeps their selection and cursor.
    m_rescanning = true;
    m_rescanSeen.clear();
    m_scanning = true;
    m_scanner->scan(m_path);
}

void DirectoryModel::mergeEntries(const QList<FileEntry> &entries)
{
    // One name-to-row index per batch keeps the merge linear. It is rebuilt
    // each time because watcher deltas may add or remove rows between batches.
    QHash<QString, int> rows;
    rows.reserve(static_cast<qsizetype>(m_entries.size() + entries.size()));
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        rows.insert(m_entries[i].name, static_cast<int>(i));
    }

    std::vector<FileEntry> added;
    for (const FileEntry &entry : entries) {
        m_rescanSeen.insert(entry.name);
        if (m_pendingRemovals.contains(entry.name)) {
            continue;
        }
        const auto it = rows.constFind(entry.name);
        if (it != rows.constEnd()) {
            const int row = it.value();
            if (std::cmp_less(row, m_entries.size())) {
                // Unchanged rows are left alone: no dataChanged, no repaint, and
                // the resolved mimeName survives.
                if (!sameListing(m_entries[static_cast<std::size_t>(row)], entry)) {
                    if (m_entries[static_cast<std::size_t>(row)].modified != entry.modified) {
                        m_imageSizes.remove(entry.name);
                    }
                    m_entries[static_cast<std::size_t>(row)] = entry;
                    const QModelIndex changed = index(row, 0);
                    Q_EMIT dataChanged(changed, changed);
                }
            } else {
                added[static_cast<std::size_t>(row) - m_entries.size()] = entry;
            }
            continue;
        }
        rows.insert(entry.name, static_cast<int>(m_entries.size() + added.size()));
        added.push_back(entry);
    }

    if (!added.empty()) {
        const int first = static_cast<int>(m_entries.size());
        beginInsertRows({}, first, first + static_cast<int>(added.size()) - 1);
        m_entries.insert(m_entries.end(), added.begin(), added.end());
        endInsertRows();
    }
    Q_EMIT scanProgress(static_cast<int>(m_entries.size()));
}

void DirectoryModel::onEntriesReady(const QString &path, const QList<FileEntry> &entries)
{
    if (path != m_path || entries.isEmpty()) {
        return;
    }

    if (m_rescanning) {
        mergeEntries(entries);
        return;
    }

    // §4.2: append with begin/endInsertRows so the view stays responsive rather
    // than resetting — a reset would throw away the user's scroll position and
    // selection on every one of the 200 batches of a 100,000-entry directory.
    const int first = static_cast<int>(m_entries.size());
    const int last = first + static_cast<int>(entries.size()) - 1;

    beginInsertRows({}, first, last);
    m_entries.reserve(m_entries.size() + static_cast<std::size_t>(entries.size()));
    for (const FileEntry &entry : entries) {
        m_entries.push_back(entry);
    }
    endInsertRows();

    Q_EMIT scanProgress(static_cast<int>(m_entries.size()));
}

void DirectoryModel::setWatchingEnabled(bool enabled)
{
    if (m_watchingEnabled == enabled) {
        return;
    }
    m_watchingEnabled = enabled;

    if (!enabled) {
        m_watcher.reset();
        return;
    }
    startWatching();
}

void DirectoryModel::setThumbnailsEnabled(bool enabled)
{
    if (m_thumbnailsEnabled == enabled) {
        return;
    }
    m_thumbnailsEnabled = enabled;

    if (!enabled && m_thumbnailsConnected) {
        for (const QString &path : std::as_const(m_thumbnailWindow)) {
            ThumbnailCache::instance().cancel(path);
        }
        m_thumbnailWindow.clear();
    }
}

bool DirectoryModel::isImageEntry(const FileEntry &entry) const
{
    if (entry.isDir || entry.statFailed || (!entry.isSymlink && !S_ISREG(entry.mode))) {
        return false;
    }
    static QHash<QString, bool> bySuffix;
    const QString suffix = QFileInfo(entry.name).suffix().toLower();
    if (suffix.isEmpty()) {
        return false;
    }
    auto found = bySuffix.find(suffix);
    if (found == bySuffix.end()) {
        const QMimeType type =
            QMimeDatabase().mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension);
        found = bySuffix.insert(suffix, type.name().startsWith(QLatin1String("image/")));
    }
    return found.value();
}

void DirectoryModel::setImageSizesWanted(bool wanted)
{
    if (!wanted && m_imageSizesWanted) {
        cancelSizeProbes();
    }
    m_imageSizesWanted = wanted;
}

void DirectoryModel::cancelSizeProbes()
{
    m_sizeCancel->store(true);
    m_sizeCancel = std::make_shared<std::atomic<bool>>(false);
    m_sizePending.clear();
    m_sizeQueue.clear();
}

void DirectoryModel::queueSizeProbe(int row, const FileEntry &entry) const
{
    if (!m_imageSizesWanted || m_sizePending.contains(entry.name)) {
        return;
    }
    if (const CachedSize *cached = sizeCache().object(absolutePathFor(entry));
        cached != nullptr && cached->modified == entry.modified.toSecsSinceEpoch()) {
        const_cast<DirectoryModel *>(this)->m_imageSizes.insert(entry.name, cached->size);
        return;
    }
    m_sizePending.insert(entry.name);
    m_sizeQueue.append({row, entry.name});
    auto *self = const_cast<DirectoryModel *>(this);
    if (!self->m_sizeFlushScheduled) {
        self->m_sizeFlushScheduled = true;
        QTimer::singleShot(0, self, [self] { self->flushSizeProbes(); });
    }
}

namespace {

/// Reads an image's dimensions without ever blocking on a FIFO or device that
/// merely has an image-like name.
QSize probeImageSize(const QString &path)
{
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return {};
    }
    QFile file(path);
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return {};
    }
    QImageReader reader(&file);
    QSize size = reader.size();
    if (size.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90)) {
        size.transpose();
    }
    return size;
}

} // namespace

void DirectoryModel::flushSizeProbes()
{
    m_sizeFlushScheduled = false;
    if (m_sizeQueue.isEmpty()) {
        return;
    }
    const auto queue = std::exchange(m_sizeQueue, {});
    const int generation = m_sizeGeneration;
    const QString directory = m_path;
    const std::shared_ptr<std::atomic<bool>> cancel = m_sizeCancel;

    static QThreadPool *pool = WorkerPools::acquire("pf-image-size", 1);
    const QPointer<DirectoryModel> self(this);
    constexpr qsizetype kChunk = 64;
    for (qsizetype start = 0; start < queue.size(); start += kChunk) {
        const auto chunk = queue.mid(start, kChunk);
        pool->start([self, chunk, generation, directory, cancel] {
            QList<int> rows;
            QStringList names;
            QList<QSize> sizes;
            for (const auto &[row, name] : chunk) {
                if (cancel->load() || self.isNull()) {
                    return;
                }
                rows.append(row);
                names.append(name);
                sizes.append(probeImageSize(directory + QLatin1Char('/') + name));
            }
            if (cancel->load() || self.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(
                self.data(),
                [self, generation, rows, names, sizes] {
                    if (!self.isNull()) {
                        self->applyProbedSizes(generation, rows, names, sizes);
                    }
                },
                Qt::QueuedConnection);
        });
    }
}

void DirectoryModel::applyProbedSizes(int generation, const QList<int> &rows,
                                      const QStringList &names, const QList<QSize> &sizes)
{
    if (generation != m_sizeGeneration) {
        return;
    }
    for (qsizetype i = 0; i < names.size(); ++i) {
        const QString &name = names.at(i);
        m_sizePending.remove(name);
        const int hint = rows.at(i);
        const int row = (hint >= 0 && static_cast<size_t>(hint) < m_entries.size() &&
                         m_entries[hint].name == name)
                            ? hint
                            : indexOfName(name);
        if (row < 0) {
            continue;
        }
        m_imageSizes.insert(name, sizes.at(i));
        sizeCache().insert(absolutePathFor(m_entries[row]),
                           new CachedSize{m_entries[row].modified.toSecsSinceEpoch(), sizes.at(i)});
        const QModelIndex changed = index(row, 0);
        Q_EMIT dataChanged(changed, changed, {ImageSizeRole, AspectRatioRole});
    }
}

void DirectoryModel::connectThumbnailCache()
{
    if (m_thumbnailsConnected) {
        return;
    }
    m_thumbnailsConnected = true;

    // One connection, not one per request: a generated thumbnail arrives by
    // path, and the model turns that back into a row.
    connect(&ThumbnailCache::instance(), &ThumbnailCache::ready, this,
            [this](const QString &path, const QImage &) {
                const int row = indexOfName(QFileInfo(path).fileName());
                if (row < 0 || QFileInfo(path).absolutePath() != m_path) {
                    return;
                }
                const QModelIndex changed = index(row, 0);
                Q_EMIT dataChanged(changed, changed, {ThumbnailRole});
            });
}

bool DirectoryModel::thumbnailsEnabled() const
{
    return m_thumbnailsEnabled;
}

void DirectoryModel::setThumbnailSize(ThumbnailCache::Size size)
{
    if (m_thumbnailSize == size) {
        return;
    }
    m_thumbnailSize = size;
    // Whatever was queued is at the wrong size; the next request replaces it.
    m_thumbnailWindow.clear();
}

ThumbnailCache::Size DirectoryModel::thumbnailSize() const
{
    return m_thumbnailSize;
}

void DirectoryModel::requestThumbnails(const QList<int> &visibleSourceRows,
                                       const QList<int> &overshootSourceRows)
{
    if (!m_thumbnailsEnabled) {
        return;
    }

    connectThumbnailCache();

    const auto pathAt = [this](int row) -> QString {
        if (row < 0 || row >= static_cast<int>(m_entries.size())) {
            return {};
        }
        const FileEntry &entry = m_entries[static_cast<std::size_t>(row)];
        return entry.isDir ? QString() : absolutePathFor(entry);
    };

    QStringList visible;
    QStringList overshoot;
    QSet<QString> window;
    for (const int row : visibleSourceRows) {
        if (const QString path = pathAt(row); !path.isEmpty() && !window.contains(path)) {
            window.insert(path);
            // Already in the window on the previous call (visible or
            // overshoot): its request is pending or done, and repeating it
            // costs stats, a redundant ready signal and a repaint per tile.
            if (!m_thumbnailWindow.contains(path)) {
                visible.append(path);
            }
        }
    }
    for (const int row : overshootSourceRows) {
        if (const QString path = pathAt(row); !path.isEmpty() && !window.contains(path)) {
            window.insert(path);
            overshoot.append(path);
        }
    }

    // §7.7: "cancel requests for rows that scroll away". Only what left the
    // window is cancelled — re-requesting everything on every scroll event
    // would cancel work that is about to finish.
    for (const QString &path : std::as_const(m_thumbnailWindow)) {
        if (!window.contains(path)) {
            ThumbnailCache::instance().cancel(path);
        }
    }

    // Visible tiles first, and at a higher pool priority so they also pass
    // work queued earlier for rows that were only the overshoot.
    for (const QString &path : std::as_const(visible)) {
        ThumbnailCache::instance().request(path, m_thumbnailSize, 1);
    }
    for (const QString &path : std::as_const(overshoot)) {
        if (!m_thumbnailWindow.contains(path)) {
            ThumbnailCache::instance().request(path, m_thumbnailSize, 0);
        }
    }

    m_thumbnailWindow = std::move(window);
}

QString DirectoryModel::absolutePathFor(const FileEntry &entry) const
{
    return m_path + QLatin1Char('/') + entry.name;
}

bool DirectoryModel::isWatchingEnabled() const
{
    return m_watchingEnabled;
}

void DirectoryModel::startWatching()
{
    if (!m_watchingEnabled || m_path.isEmpty()) {
        return;
    }

    // The old watcher is dropped before the new one is taken. Holding both
    // across a navigation would keep a watch on every directory visited until
    // the panel closed, and inotify watches are a limited per-user resource.
    m_watcher.reset();
    m_watcher = fs::DirectoryWatcher::acquire(m_path);

    connect(m_watcher.get(), &fs::DirectoryWatcher::changed, this, &DirectoryModel::applyDelta);
}

bool DirectoryModel::refreshEntry(const QString &name)
{
    const QFileInfo info(m_path + QLatin1Char('/') + name);
    if (!info.exists() && !info.isSymLink()) {
        return false;
    }

    FileEntry entry;
    entry.name = name;
    entry.isHidden = name.startsWith(QLatin1Char('.'));
    entry.size = static_cast<quint64>(info.size());
    entry.modified = info.lastModified();
    entry.isSymlink = info.isSymLink();
    entry.isDir = info.isDir();
    entry.isBroken = info.isSymLink() && !info.exists();
    entry.isExecutable = info.isExecutable();
    if (entry.isSymlink) {
        entry.linkTarget = info.symLinkTarget();
    }

    if (const int row = indexOfName(name); row >= 0) {
        if (m_entries[static_cast<std::size_t>(row)].modified != entry.modified) {
            m_imageSizes.remove(name);
        }
        m_entries[static_cast<std::size_t>(row)] = entry;
        const QModelIndex changed = index(row, 0);
        Q_EMIT dataChanged(changed, changed);
        return true;
    }

    // Appended rather than inserted in sorted position: the proxy owns the
    // ordering, and having the model maintain one too would mean two sort
    // implementations that have to agree.
    const int last = static_cast<int>(m_entries.size());
    beginInsertRows({}, last, last);
    m_entries.push_back(entry);
    endInsertRows();
    return true;
}

void DirectoryModel::removeEntry(const QString &name)
{
    const int row = indexOfName(name);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
}

void DirectoryModel::expectRemoval(const QStringList &names)
{
    QList<int> rows;
    for (const QString &name : names) {
        m_pendingRemovals.insert(name);
    }
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (m_pendingRemovals.contains(m_entries[i].name)) {
            rows.append(static_cast<int>(i));
        }
    }

    // One removal per contiguous run, from the bottom up so earlier rows keep
    // their numbers. Removing a thousand scattered rows one at a time costs the
    // proxy a pass over its rows each.
    int end = static_cast<int>(rows.size()) - 1;
    while (end >= 0) {
        int begin = end;
        while (begin > 0 && rows.at(begin - 1) == rows.at(begin) - 1) {
            --begin;
        }
        const int first = rows.at(begin);
        const int last = rows.at(end);
        beginRemoveRows({}, first, last);
        m_entries.erase(m_entries.begin() + first, m_entries.begin() + last + 1);
        endRemoveRows();
        end = begin - 1;
    }
}

void DirectoryModel::finishRemoval(const QStringList &names)
{
    if (m_rescanning) {
        m_finishAfterRescan.append(names);
        return;
    }
    releaseRemovals(names);
}

void DirectoryModel::releaseRemovals(const QStringList &names)
{
    for (const QString &name : names) {
        if (m_pendingRemovals.remove(name)) {
            // Puts the row back only if the file is still there.
            refreshEntry(name);
        }
    }
}

void DirectoryModel::applyDelta(const fs::WatchDelta &delta)
{
    if (delta.selfGone) {
        // §7.3: the panel walks up to the nearest existing ancestor. The model
        // reports what happened and lets the panel decide where to go, because
        // "up" is a navigation and navigation is the panel's business.
        Q_EMIT directoryVanished(m_path);
        return;
    }

    if (delta.needsFullRescan && delta.created.isEmpty() && delta.modified.isEmpty() &&
        delta.renamed.isEmpty() && !delta.deleted.isEmpty()) {
        // A burst of nothing but deletions. If none of those names has a row,
        // the listing already shows the result (expectRemoval got there first),
        // and rescanning a large folder would only repaint it for nothing.
        QSet<QString> current;
        current.reserve(static_cast<qsizetype>(m_entries.size()));
        for (const FileEntry &entry : m_entries) {
            current.insert(entry.name);
        }
        const bool echo =
            std::none_of(delta.deleted.cbegin(), delta.deleted.cend(),
                         [&current](const QString &n) { return current.contains(n); });
        if (echo) {
            qCDebug(pfFs) << "ignoring the echo of" << delta.deleted.size() << "deletions";
            return;
        }
    }

    if (delta.needsFullRescan) {
        // §7.3's threshold was crossed, or the backend lost events. Either way
        // the deltas can no longer be trusted to describe the directory.
        qCDebug(pfFs) << "rescanning" << m_path << "after a large or lossy event burst";
        rescan();
        return;
    }

    // Deletions first, then renames, then creations: a rename within the
    // directory produces a delete and a create for the same row, and applying
    // them in the other order would insert the new name before removing the old
    // one, leaving both visible for a frame.
    for (const QString &name : delta.deleted) {
        removeEntry(name);
    }
    for (const auto &[from, to] : delta.renamed) {
        removeEntry(from);
        if (!m_pendingRemovals.contains(to)) {
            refreshEntry(to);
        }
    }
    // Names being deleted are skipped: finishRemoval() re-stats them anyway.
    for (const QString &name : delta.created) {
        if (!m_pendingRemovals.contains(name)) {
            refreshEntry(name);
        }
    }
    for (const QString &name : delta.modified) {
        if (!m_pendingRemovals.contains(name)) {
            refreshEntry(name);
        }
    }
}

void DirectoryModel::onScanFinished(const QString &path, int total)
{
    if (path != m_path) {
        return;
    }
    m_scanning = false;
    if (m_rescanning) {
        m_rescanning = false;
        // Whatever the rescan did not report has gone since the last listing.
        for (int row = static_cast<int>(m_entries.size()) - 1; row >= 0; --row) {
            if (!m_rescanSeen.contains(m_entries[static_cast<std::size_t>(row)].name)) {
                beginRemoveRows({}, row, row);
                m_entries.erase(m_entries.begin() + row);
                endRemoveRows();
            }
        }
        m_rescanSeen.clear();
        total = static_cast<int>(m_entries.size());

        const QStringList deferred = std::exchange(m_finishAfterRescan, {});
        releaseRemovals(deferred);
    }
    qCDebug(pfFs) << "scan finished" << path << total << "entries";
    Q_EMIT scanFinished(path, total);
}

void DirectoryModel::onScanFailed(const QString &path, const QString &reason)
{
    if (path != m_path) {
        return;
    }
    m_scanning = false;
    m_rescanning = false;
    m_rescanSeen.clear();
    releaseRemovals(std::exchange(m_finishAfterRescan, {}));
    m_error = reason;
    qCDebug(pfFs) << "scan failed" << path << reason;
    Q_EMIT scanFailed(path, reason);
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    // A list model has no children below the root; without this a view can ask
    // for the row count of a valid index and get a nonsensical answer.
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

const FileEntry *DirectoryModel::entryAt(int row) const
{
    if (row < 0 || std::cmp_greater_equal(row, m_entries.size())) {
        return nullptr;
    }
    return &m_entries[static_cast<std::size_t>(row)];
}

int DirectoryModel::indexOfName(const QString &name) const
{
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    const FileEntry *entry = entryAt(index.row());
    if (entry == nullptr) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry->name;
    case EntryRole:
        return QVariant::fromValue(*entry);
    case SizeRole:
        return entry->size;
    case ModifiedRole:
        return entry->modified;
    case IsDirRole:
        return entry->isDir;
    case FolderGlyphRole:
        return entry->isDir ? FolderIconStore::instance().effectiveGlyph(absolutePathFor(*entry))
                            : QString();
    case FolderEmptyRole: {
        if (!entry->isDir) {
            return false;
        }
        const QString path = absolutePathFor(*entry);
        if (const auto known = m_emptyKnown.constFind(path); known != m_emptyKnown.constEnd()) {
            return known.value();
        }
        probeEmpty(path);
        return false;
    }
    case ImageSizeRole:
    case AspectRatioRole: {
        const bool wantRatio = role == AspectRatioRole;
        if (!isImageEntry(*entry)) {
            return wantRatio ? QVariant(1.0) : QVariant();
        }
        const QString name = entry->name;
        auto known = m_imageSizes.constFind(name);
        if (known == m_imageSizes.constEnd()) {
            queueSizeProbe(index.row(), *entry);
            known = m_imageSizes.constFind(name);
        }
        if (known == m_imageSizes.constEnd()) {
            return wantRatio ? QVariant(kUnprobedAspect) : QVariant();
        }
        const QSize size = known.value();
        if (!wantRatio) {
            return size.isValid() ? QVariant(size) : QVariant();
        }
        return size.isValid() ? static_cast<qreal>(size.width()) / size.height() : 1.0;
    }
    case ThumbnailRole: {
        if (!m_thumbnailsEnabled || !m_thumbnailsConnected || entry->isDir) {
            return {};
        }
        // A memory-tier hit or nothing: §5.3 forbids IO in the paint path, and
        // this is called from it. Generation is driven by requestThumbnailRange
        // instead, which knows which rows are actually on screen.
        const QString path = absolutePathFor(*entry);
        QImage image = ThumbnailCache::instance().lookup(path, m_thumbnailSize);
        if (image.isNull()) {
            // The other size, scaled by the delegate, until the right one lands:
            // switching views should not blank every tile.
            for (const auto other : {ThumbnailCache::Size::XLarge, ThumbnailCache::Size::Large,
                                     ThumbnailCache::Size::Normal}) {
                if (other != m_thumbnailSize && image.isNull()) {
                    image = ThumbnailCache::instance().peek(path, other);
                }
            }
        }
        return image.isNull() ? QVariant() : QVariant::fromValue(image);
    }
    case Qt::ToolTipRole:
        if (entry->isSymlink && !entry->linkTarget.isEmpty()) {
            // QT_USE_QSTRINGBUILDER makes a concatenation an expression
            // template, not a QString, so the conversion has to be explicit.
            return QString(entry->name + QStringLiteral(" → ") + entry->linkTarget);
        }
        // Nothing for an ordinary entry.
        //
        // Returning the name meant a large bordered box appeared over the list
        // repeating a filename that was already fully readable in the row the
        // pointer was resting on — four rows obscured to tell the user
        // something they were already looking at. A tooltip earns its place by
        // saying what the row cannot, which here is a symlink's target; the
        // elided-name case is handled by the delegate, which knows whether it
        // actually had to elide.
        return {};
    default:
        return {};
    }
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
    QHash<int, QByteArray> names = QAbstractListModel::roleNames();
    names[EntryRole] = "entry";
    names[IconRole] = "icon";
    names[ThumbnailRole] = "thumbnail";
    names[MatchSpansRole] = "matchSpans";
    names[NameRole] = "name";
    names[SizeRole] = "size";
    names[ModifiedRole] = "modified";
    names[IsDirRole] = "isDir";
    names[FolderGlyphRole] = "folderGlyph";
    names[FolderEmptyRole] = "folderEmpty";
    names[ImageSizeRole] = "imageSize";
    names[AspectRatioRole] = "aspectRatio";
    return names;
}

bool DirectoryModel::isScanning() const
{
    return m_scanning;
}

bool DirectoryModel::isRescanning() const
{
    return m_rescanning;
}

QString DirectoryModel::errorMessage() const
{
    return m_error;
}

} // namespace pf
