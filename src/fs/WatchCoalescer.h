#pragma once

#include "platform/WatchEvent.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace pf::fs {

using platform::WatchEvent;

/// What a burst of events amounts to, once coalesced.
struct WatchDelta {
    QStringList created;
    QStringList deleted;
    QStringList modified;

    /// Renames, paired where both halves arrived in the same window.
    QList<QPair<QString, QString>> renamed;

    /// §7.3: "Fall back to a full rescan if more than 200 events arrive in one
    /// debounce window." Also set on an overflow from the backend.
    bool needsFullRescan = false;

    /// §7.3: "Detect IN_DELETE_SELF / IN_MOVE_SELF and walk the panel up to the
    /// nearest existing ancestor."
    bool selfGone = false;

    bool isEmpty() const;
};

/// Turns a stream of raw events into model deltas (§7.3).
///
/// Separated from the platform backends on purpose. Everything §7.3 actually
/// specifies — the 150 ms debounce, coalescing a create-then-delete into
/// nothing, pairing the two halves of a rename, the 200-event rescan threshold
/// — is decision logic with no system calls in it, and keeping it here means it
/// is tested identically on both platforms by feeding it synthetic events. The
/// inotify and FSEvents backends are then thin enough to read in one sitting.
class WatchCoalescer : public QObject
{
    Q_OBJECT

public:
    /// §7.3's debounce window.
    static constexpr int kDefaultDebounceMs = 150;

    /// §7.3: more events than this in one window means a rescan is cheaper than
    /// applying them one by one — and more reliable, since a burst that large
    /// usually means something is rewriting the directory wholesale.
    static constexpr int kRescanThreshold = 200;

    explicit WatchCoalescer(QObject *parent = nullptr);

    void setDebounceInterval(int milliseconds);
    int debounceInterval() const;

    /// Feeds one event in, on this object's own thread.
    void add(const WatchEvent &event);

    /// Emits whatever has accumulated, without waiting for the timer.
    void flush();

    /// Discards everything pending. Used when the watched path changes.
    void reset();

    int pendingCount() const;

Q_SIGNALS:
    void delta(const pf::fs::WatchDelta &delta);

private:
    QList<WatchEvent> m_pending;
    QTimer m_timer;
};

/// Coalesces a burst of raw events into a single delta.
///
/// A pure function, which is what makes the interesting cases — a file created
/// and deleted within one window, a rename whose halves arrive separately —
/// testable without a filesystem or a timer.
WatchDelta coalesce(const QList<WatchEvent> &events);

} // namespace pf::fs
