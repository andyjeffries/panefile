#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QObject>
#include <QTimer>

#include <memory>

namespace pf::ui {

/// Loads Quick Look content off the GUI thread (§7.6).
///
/// §7.6's loading rules, all of which are about not making the user wait:
///
///   * "Content loading is **always** off the GUI thread."
///   * "Debounce cursor changes by 120 ms so holding `j` doesn't queue a
///     hundred loads. Cancel any in-flight load when the cursor moves again."
///   * "Cache the last 5 rendered contents keyed on path plus mtime, so moving
///     back and forth between two files is instant."
///   * "Show a skeleton/spinner state after 200 ms of loading, never before —
///     instant content must not flash a spinner."
///
/// The last one is the subtlest: a spinner that appears for 30 ms and vanishes
/// is worse than no spinner, because the flash reads as a glitch.
class QuickLookLoader : public QObject
{
    Q_OBJECT

public:
    /// The worker-to-GUI handoff. Public only because the QRunnable that fills
    /// it lives in the .cpp's anonymous namespace and cannot be befriended.
    class Channel;

    static constexpr int kDefaultDebounceMs = 120;
    static constexpr int kSpinnerDelayMs = 200;
    static constexpr int kCacheSize = 5;

    explicit QuickLookLoader(QObject *parent = nullptr);
    ~QuickLookLoader() override;

    void setDebounceInterval(int milliseconds);

    /// §7.6's `max_read_bytes` and `max_decode_mb`.
    void setMaxReadBytes(qint64 bytes);
    void setMaxDecodeMegabytes(int megabytes);

    /// Requests content for a file. Supersedes any request in flight.
    void request(const QString &path, const FileEntry &entry, QuickLookRenderer *renderer);

    /// Abandons the current request and clears the debounce.
    void cancel();

    /// Drops the cache. Called when Quick Look closes, so a decoded image is
    /// not held for the rest of the session.
    void clearCache();

    int cacheSize() const;

Q_SIGNALS:
    void loaded(const pf::ui::QuickLookContent &content, pf::ui::QuickLookRenderer *renderer);

    /// The load has been running long enough to be worth acknowledging.
    void loadingSlowly(const QString &path);

private:
    struct CacheKey {
        QString path;
        qint64 modifiedSeconds = 0;

        bool operator==(const CacheKey &other) const = default;
    };

    void start();

    QTimer m_debounce;
    QTimer m_spinnerDelay;

    QString m_pendingPath;
    FileEntry m_pendingEntry;
    QuickLookRenderer *m_pendingRenderer = nullptr;

    std::shared_ptr<Channel> m_channel;

    /// Most recently used last, so eviction takes from the front.
    QList<QPair<CacheKey, QuickLookContent>> m_cache;

    qint64 m_maxReadBytes = 64LL * 1024 * 1024;
    int m_maxDecodeMegabytes = 500;
};

} // namespace pf::ui
