#include "model/DirectoryModel.h"

#include "core/Logging.h"
#include "fs/DirectoryScanner.h"
#include "fs/DirectoryWatcher.h"
#include "model/ThumbnailCache.h"

#include <QDir>
#include <QFileInfo>

#include <utility>

namespace pf {

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

void DirectoryModel::onEntriesReady(const QString &path, const QList<FileEntry> &entries)
{
    if (path != m_path || entries.isEmpty()) {
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

    m_thumbnailWindow = window;
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

void DirectoryModel::applyDelta(const fs::WatchDelta &delta)
{
    if (delta.selfGone) {
        // §7.3: the panel walks up to the nearest existing ancestor. The model
        // reports what happened and lets the panel decide where to go, because
        // "up" is a navigation and navigation is the panel's business.
        Q_EMIT directoryVanished(m_path);
        return;
    }

    if (delta.needsFullRescan) {
        // §7.3's threshold was crossed, or the backend lost events. Either way
        // the deltas can no longer be trusted to describe the directory.
        qCDebug(pfFs) << "rescanning" << m_path << "after a large or lossy event burst";
        refresh();
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
        refreshEntry(to);
    }
    for (const QString &name : delta.created) {
        refreshEntry(name);
    }
    for (const QString &name : delta.modified) {
        refreshEntry(name);
    }
}

void DirectoryModel::onScanFinished(const QString &path, int total)
{
    if (path != m_path) {
        return;
    }
    m_scanning = false;
    qCDebug(pfFs) << "scan finished" << path << total << "entries";
    Q_EMIT scanFinished(path, total);
}

void DirectoryModel::onScanFailed(const QString &path, const QString &reason)
{
    if (path != m_path) {
        return;
    }
    m_scanning = false;
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
        return entry->name;
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

QString DirectoryModel::errorMessage() const
{
    return m_error;
}

} // namespace pf
