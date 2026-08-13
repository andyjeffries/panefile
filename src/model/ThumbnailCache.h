#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <memory>

class QThreadPool;

namespace pf {

/// The freedesktop thumbnail cache (§7.7).
///
/// §7.7 asks for the shared spec rather than a private cache, "so the cache is
/// shared with other applications": `$XDG_CACHE_HOME/thumbnails/{normal,large}`
/// keyed on the MD5 of the file URI, with `Thumb::URI` and `Thumb::MTime` PNG
/// text chunks, and failures recorded under `fail/panefile/` so a file that
/// cannot be thumbnailed is not retried on every scroll.
///
/// The MD5 is not a security choice and does not need to be one — the spec
/// names it, and interoperability is the entire point of implementing someone
/// else's cache layout.
class ThumbnailCache : public QObject
{
    Q_OBJECT

public:
    /// §7.7: "`normal` = 128 px, `large` = 256 px".
    enum class Size {
        Normal,
        Large,
    };

    static int pixelsFor(Size size);
    static QString directoryNameFor(Size size);

    /// The `file://` URI the spec hashes. Percent-encoded the way the spec
    /// requires, which is why this is not just a string concatenation.
    static QString fileUri(const QString &absolutePath);

    /// `<md5-of-uri>.png`.
    static QString cacheFileName(const QString &uri);

    /// Absolute path a thumbnail would live at. Pure, so the layout can be
    /// checked against the spec by a test without touching the disk.
    static QString thumbnailPathFor(const QString &absolutePath, Size size,
                                    const QString &cacheRoot);

    explicit ThumbnailCache(QObject *parent = nullptr);
    ~ThumbnailCache() override;

    /// Overrides the cache root. Injectable for the same reason Trash's root
    /// is: the round-trip is the thing worth testing, and a test must not write
    /// into the developer's real thumbnail cache.
    void setCacheRoot(const QString &root);
    QString cacheRoot() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    /// §7.7's `thumbnails.max_file_size_mb` (default 200).
    void setMaxFileSizeMb(int megabytes);

    /// §7.7's `thumbnails.video`. Video thumbnailing needs ffmpegthumbnailer,
    /// which §3.4 keeps behind the optional plugin host; until that exists this
    /// only records the preference.
    void setVideoEnabled(bool enabled);

    /// Reads a cached thumbnail, or returns a null image when there is none or
    /// the cached one is stale. Cheap enough to call from a delegate: it is one
    /// stat plus, on a hit, a small PNG decode.
    QImage lookup(const QString &absolutePath, Size size) const;

    /// §7.7: "Write failures to fail/panefile/ so you don't retry a file that
    /// can't be thumbnailed."
    bool hasFailed(const QString &absolutePath) const;

    /// Queues generation off the GUI thread. A path already queued or already
    /// known to have failed is ignored, so a delegate may call this on every
    /// paint without flooding the pool.
    void request(const QString &absolutePath, Size size);

    /// §7.7: "cancel requests for rows that scroll away".
    void cancel(const QString &absolutePath);
    void cancelAll();

    int pendingCount() const;

    /// The process-wide cache. Panels share one, because the disk cache is
    /// shared anyway and a per-panel memory tier would decode the same PNG
    /// several times over for a directory open in two panels.
    static ThumbnailCache &instance();

    /// Whether a thumbnail is worth attempting at all: enabled, a regular file,
    /// under the size cap, and of a type something in this build can decode.
    bool canThumbnail(const QString &absolutePath) const;

Q_SIGNALS:
    /// A generated thumbnail, on the GUI thread.
    void ready(const QString &absolutePath, const QImage &image);

    /// Generation failed and has been recorded in the fail cache.
    void failed(const QString &absolutePath);

private:
    void writeFailMarker(const QString &absolutePath) const;
    QString failMarkerPath(const QString &absolutePath) const;

    QString m_root;

    /// One cancellation flag per in-flight request, shared with its worker so a
    /// cancelled decode stops as soon as it notices. Erasing the entry is the
    /// cancellation: the worker holds the last reference and checks it.
    QHash<QString, std::shared_ptr<std::atomic<bool>>> m_pendingFlags;

    /// The memory tier. lookup() is called from the delegate's paint path,
    /// which §5.3 requires to do no IO at all — so a disk hit is decoded once
    /// and answered from here afterwards. The recorded mtime is what makes an
    /// entry verifiable rather than merely present, exactly as on disk.
    struct MemoryEntry {
        QImage image;
        qint64 modifiedSeconds = 0;
    };

    mutable QHash<QString, MemoryEntry> m_memory;

    QThreadPool *m_pool = nullptr;

    int m_maxFileSizeMb = 200;
    bool m_enabled = true;
    bool m_video = true;
};

} // namespace pf
