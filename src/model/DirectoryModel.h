#pragma once

#include "model/FileEntry.h"

#include <QAbstractListModel>
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
    };
    Q_ENUM(Roles)

    /// §7.7: "Only request thumbnails for rows in or within 20 rows of the
    /// viewport; cancel requests for rows that scroll away."
    static constexpr int kThumbnailOvershoot = 20;

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

    /// Queues thumbnails for the visible rows and the overshoot either side,
    /// and cancels anything queued outside that window. Called by the panel on
    /// scroll and after a scan.
    void requestThumbnailRange(int firstVisibleRow, int lastVisibleRow);
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

    /// Re-reads one entry from disk and updates its row, or inserts it.
    /// Returns false when the entry has gone.
    bool refreshEntry(const QString &name);

    void removeEntry(const QString &name);

    void startWatching();

    QString absolutePathFor(const FileEntry &entry) const;

    void onEntriesReady(const QString &path, const QList<FileEntry> &entries);
    void onScanFinished(const QString &path, int total);
    void onScanFailed(const QString &path, const QString &reason);

    std::unique_ptr<fs::DirectoryScanner> m_scanner;
    std::shared_ptr<fs::DirectoryWatcher> m_watcher;
    bool m_watchingEnabled = false;
    bool m_thumbnailsEnabled = false;

    /// Paths currently inside the request window, so a scroll can cancel only
    /// what left it rather than cancelling and requeueing everything.
    QSet<QString> m_thumbnailWindow;
    std::vector<FileEntry> m_entries;
    QString m_path;
    QString m_error;
    bool m_scanning = false;
};

} // namespace pf
