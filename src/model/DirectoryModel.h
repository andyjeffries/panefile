#pragma once

#include "model/FileEntry.h"
#include "model/ThumbnailCache.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>

#include <memory>
#include <vector>

namespace pf::fs {
class DirectoryScanner;
class DirectoryWatcher;
struct WatchDelta;
} // namespace pf::fs

namespace pf {

/// A flat list model over one directory (§4.2).
///
/// QAbstractListModel over a std::vector<FileEntry>, fed by a DirectoryScanner.
/// The model is mutated only on the GUI thread (§3.3); the scanner hands it
/// value types by queued signal and never touches it directly.
class DirectoryModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /// §4.2. EntryRole carries the whole FileEntry, which is what the delegate
    /// paints from — one role lookup per row rather than eight.
    enum Roles {
        EntryRole = Qt::UserRole + 1,
        /// Resolved by the delegate rather than served from here: the icon is
        /// tinted with the theme colour for the entry's kind, and the theme
        /// lives in the ui layer, which the model must not depend on (§3.1).
        IconRole,
        ThumbnailRole,
        MatchSpansRole, ///< spans to highlight for a fuzzy match (§7.8)
        NameRole,
        SizeRole,
        ModifiedRole,
        IsDirRole,
        /// A directory's folder-icon glyph id (custom, else standard), or empty.
        /// A hash lookup: FolderIconStore is loaded by setPath(), not here.
        FolderGlyphRole,
        /// Whether a directory is known to be empty. False until a background
        /// readdir says so, so a folder is drawn open until it is known closed.
        FolderEmptyRole,
    };
    Q_ENUM(Roles)

    explicit DirectoryModel(QObject *parent = nullptr);
    ~DirectoryModel() override;

    /// Cancels any scan in flight, clears, and starts a new one (§4.2).
    void setPath(const QString &path);
    QString path() const;

    /// Re-runs the scan for the current path.
    void refresh();

    /// §7.3: watch the current directory and apply targeted updates. Off by
    /// default, so a model used for a one-off listing — a Quick Look directory
    /// preview, say — does not take a watch it will never use.
    void setWatchingEnabled(bool enabled);

    /// §7.7's `thumbnails.enabled`. Off by default at construction time so a
    /// panel that never scrolls costs nothing; the panel turns it on from the
    /// configuration once it has one.
    void setThumbnailsEnabled(bool enabled);
    bool thumbnailsEnabled() const;

    /// The size thumbnails are requested and served at. Icon view wants Large.
    void setThumbnailSize(ThumbnailCache::Size size);
    ThumbnailCache::Size thumbnailSize() const;

    /// Queues thumbnails for these *source* rows and cancels anything queued
    /// outside them. The panel works the rows out in view order — a sorted or
    /// filtered proxy makes the visible source rows non-contiguous — and lists
    /// the on-screen ones separately so they jump the queue (§7.7: rows in or
    /// within 20 rows of the viewport).
    void requestThumbnails(const QList<int> &visibleSourceRows,
                           const QList<int> &overshootSourceRows);
    bool isWatchingEnabled() const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const FileEntry *entryAt(int row) const;

    /// Row of the entry with this basename, or -1. Linear, but callers use it
    /// on user actions rather than in a loop — cursor restoration by name
    /// (§5.2) is the reason it exists.
    int indexOfName(const QString &name) const;

    bool isScanning() const;

    /// True while a rescan is reconciling rows that are already showing, as
    /// opposed to the initial scan that fills an empty model.
    bool isRescanning() const;

    /// Removes these rows now, ahead of the filesystem confirming it, and
    /// remembers the names so a scan or watcher event that races the delete
    /// cannot bring them back. Call finishRemoval() when the delete is over.
    void expectRemoval(const QStringList &names);

    /// Ends expectRemoval(): each name is re-checked on disk, and one that
    /// still exists (the delete failed or was cancelled) gets its row back.
    /// Deferred until a rescan in flight has ended.
    void finishRemoval(const QStringList &names);

    /// Non-empty when the last scan failed; the panel renders this inline
    /// (§7.2) rather than replacing its listing with an error.
    QString errorMessage() const;

Q_SIGNALS:
    void scanStarted(const QString &path);

    /// §7.3: the watched directory itself is gone, so the panel must "walk up
    /// to the nearest existing ancestor".
    void directoryVanished(const QString &path);
    void scanProgress(int count);
    void scanFinished(const QString &path, int count);
    void scanFailed(const QString &path, const QString &reason);

private:
    void applyDelta(const fs::WatchDelta &delta);

    /// Re-scans the current path and reconciles the rows in place, without the
    /// model reset refresh() performs.
    void rescan();
    void mergeEntries(const QList<FileEntry> &entries);

    /// Re-reads one entry from disk and updates its row, or inserts it.
    /// Returns false when the entry has gone.
    bool refreshEntry(const QString &name);

    void removeEntry(const QString &name);

    /// Forgets the pending names and restores the rows whose files remain.
    void releaseRemovals(const QStringList &names);

    void startWatching();

    QString absolutePathFor(const FileEntry &entry) const;

    /// Starts the one readdir that tells whether a directory is empty.
    void probeEmpty(const QString &absolutePath) const;

    /// Wires this model to the shared thumbnail cache, on first use.
    void connectThumbnailCache();

    void onEntriesReady(const QString &path, const QList<FileEntry> &entries);
    void onScanFinished(const QString &path, int total);
    void onScanFailed(const QString &path, const QString &reason);

    std::unique_ptr<fs::DirectoryScanner> m_scanner;
    std::shared_ptr<fs::DirectoryWatcher> m_watcher;
    bool m_watchingEnabled = false;
    bool m_rescanning = false;
    QSet<QString> m_rescanSeen;

    /// Names hidden by expectRemoval() whose delete has not finished.
    QSet<QString> m_pendingRemovals;

    /// Names whose delete finished while a rescan was running; released when
    /// the rescan ends so it cannot re-add a deleted row afterwards.
    QStringList m_finishAfterRescan;
    bool m_thumbnailsEnabled = false;
    ThumbnailCache::Size m_thumbnailSize = ThumbnailCache::Size::Normal;

    /// False until the first thumbnail request has wired this model to the
    /// shared cache. §3.4: constructing the cache resolves the thumbnail
    /// directory, which reads XDG configuration — not work for the startup
    /// path of a panel whose rows have not even been scanned yet.
    bool m_thumbnailsConnected = false;

    /// Paths currently inside the request window, so a scroll can cancel only
    /// what left it rather than cancelling and requeueing everything.
    QSet<QString> m_thumbnailWindow;
    std::vector<FileEntry> m_entries;

    /// Which directories are empty, filled in off the GUI thread for the ones
    /// that have been painted. Cleared when the path changes.
    mutable QHash<QString, bool> m_emptyKnown;
    mutable QSet<QString> m_emptyPending;
    QString m_path;
    QString m_error;
    bool m_scanning = false;
};

} // namespace pf
