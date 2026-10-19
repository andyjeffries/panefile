#include "model/DirectoryModel.h"

#include "core/Logging.h"
#include "fs/DirectoryScanner.h"
#include "fs/DirectoryWatcher.h"
#include "model/ThumbnailCache.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
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
}

DirectoryModel::~DirectoryModel() = default;

QString DirectoryModel::path() const
{
    return m_path;
}

void DirectoryModel::setPath(const QString &path)
{
    m_path = path;
    refresh();
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

void DirectoryModel::requestThumbnailRange(int firstVisibleRow, int lastVisibleRow)
{
    if (!m_thumbnailsEnabled) {
        return;
    }

    connectThumbnailCache();

    // §7.7: "rows in or within 20 rows of the viewport".
    const int first = std::max(0, firstVisibleRow - kThumbnailOvershoot);
    const int last =
        std::min(static_cast<int>(m_entries.size()) - 1, lastVisibleRow + kThumbnailOvershoot);

    QSet<QString> window;
    for (int row = first; row <= last; ++row) {
        const FileEntry &entry = m_entries[static_cast<std::size_t>(row)];
        if (entry.isDir) {
            continue;
        }
        window.insert(absolutePathFor(entry));
    }

    // §7.7: "cancel requests for rows that scroll away". Only what left the
    // window is cancelled — re-requesting everything on every scroll event
    // would cancel work that is about to finish.
    for (const QString &path : std::as_const(m_thumbnailWindow)) {
        if (!window.contains(path)) {
            ThumbnailCache::instance().cancel(path);
        }
    }

    for (const QString &path : std::as_const(window)) {
        if (!m_thumbnailWindow.contains(path)) {
            ThumbnailCache::instance().request(path, ThumbnailCache::Size::Normal);
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
    case ThumbnailRole: {
        if (!m_thumbnailsEnabled || !m_thumbnailsConnected || entry->isDir) {
            return {};
        }
        // A memory-tier hit or nothing: §5.3 forbids IO in the paint path, and
        // this is called from it. Generation is driven by requestThumbnailRange
        // instead, which knows which rows are actually on screen.
        const QImage image = ThumbnailCache::instance().lookup(absolutePathFor(*entry),
                                                               ThumbnailCache::Size::Normal);
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
