#include "fs/WatchCoalescer.h"

#include "core/Logging.h"

#include <QMap>
#include <QSet>

namespace pf::fs {

bool WatchDelta::isEmpty() const
{
    return created.isEmpty() && deleted.isEmpty() && modified.isEmpty() && renamed.isEmpty() &&
           !needsFullRescan && !selfGone;
}

WatchDelta coalesce(const QList<WatchEvent> &events)
{
    WatchDelta delta;

    if (events.size() > WatchCoalescer::kRescanThreshold) {
        // §7.3's threshold. Beyond it, applying events one at a time is both
        // slower than a rescan and less trustworthy: a burst that large usually
        // means something is rewriting the directory wholesale, and the events
        // describing it are likely already incomplete.
        delta.needsFullRescan = true;
        return delta;
    }

    // The last state each name ended in. A file created and then deleted within
    // one window has not changed as far as the model is concerned, and emitting
    // both would make a row appear and vanish for no reason.
    enum class Final { None, Created, Deleted, Modified, MovedFrom, MovedTo };
    QMap<QString, Final> states;
    QStringList order;

    const auto note = [&states, &order](const QString &name, Final state) {
        if (!states.contains(name)) {
            order.append(name);
        }
        states[name] = state;
    };

    for (const WatchEvent &event : events) {
        switch (event.kind) {
        case WatchEvent::Kind::SelfGone:
            delta.selfGone = true;
            // Nothing else matters: the directory the other events describe is
            // gone, and the panel is about to walk up to an ancestor.
            return delta;

        case WatchEvent::Kind::Overflow:
            delta.needsFullRescan = true;
            return delta;

        case WatchEvent::Kind::Created:
            // A name deleted and then created again is a replacement, which the
            // model sees as a modification rather than as a row appearing.
            note(event.name, states.value(event.name, Final::None) == Final::Deleted
                                 ? Final::Modified
                                 : Final::Created);
            break;

        case WatchEvent::Kind::Deleted:
            // Created then deleted within one window cancels out entirely.
            if (states.value(event.name, Final::None) == Final::Created) {
                states.remove(event.name);
                order.removeAll(event.name);
            } else {
                note(event.name, Final::Deleted);
            }
            break;

        case WatchEvent::Kind::Modified:
        case WatchEvent::Kind::AttributesChanged:
            // A modification to something created in the same window is still
            // just a creation; the row has not been drawn yet.
            if (states.value(event.name, Final::None) != Final::Created) {
                note(event.name, Final::Modified);
            }
            break;

        case WatchEvent::Kind::MovedFrom:
            note(event.name, Final::MovedFrom);
            break;

        case WatchEvent::Kind::MovedTo:
            note(event.name, Final::MovedTo);
            break;
        }
    }

    // Pair the halves of a rename. Both arrive in the same window when a file
    // is renamed within the watched directory; only one arrives when it is
    // moved in from, or out to, somewhere else — and those are a creation and a
    // deletion respectively, which is exactly how they should appear.
    QStringList movedFrom;
    QStringList movedTo;
    for (const QString &name : std::as_const(order)) {
        if (states.value(name) == Final::MovedFrom) {
            movedFrom.append(name);
        } else if (states.value(name) == Final::MovedTo) {
            movedTo.append(name);
        }
    }

    // In arrival order. inotify supplies a cookie tying the two halves together
    // and FSEvents does not, so pairing by order is the rule that works on both
    // — and within one 150 ms window it is right in every case but simultaneous
    // unrelated renames, where the result is still a correct set of rows.
    const int pairs = static_cast<int>(std::min(movedFrom.size(), movedTo.size()));
    for (int i = 0; i < pairs; ++i) {
        delta.renamed.append({movedFrom.at(i), movedTo.at(i)});
    }
    for (int i = pairs; i < movedFrom.size(); ++i) {
        delta.deleted.append(movedFrom.at(i));
    }
    for (int i = pairs; i < movedTo.size(); ++i) {
        delta.created.append(movedTo.at(i));
    }

    for (const QString &name : std::as_const(order)) {
        switch (states.value(name)) {
        case Final::Created:
            delta.created.append(name);
            break;
        case Final::Deleted:
            delta.deleted.append(name);
            break;
        case Final::Modified:
            delta.modified.append(name);
            break;
        case Final::MovedFrom:
        case Final::MovedTo:
        case Final::None:
            break;
        }
    }

    return delta;
}

WatchCoalescer::WatchCoalescer(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(kDefaultDebounceMs);
    connect(&m_timer, &QTimer::timeout, this, &WatchCoalescer::flush);
}

void WatchCoalescer::setDebounceInterval(int milliseconds)
{
    m_timer.setInterval(std::max(0, milliseconds));
}

int WatchCoalescer::debounceInterval() const
{
    return m_timer.interval();
}

void WatchCoalescer::add(const WatchEvent &event)
{
    m_pending.append(event);

    // A directory that has gone is not worth waiting 150 ms to report: the
    // panel is showing a listing of something that no longer exists.
    if (event.kind == WatchEvent::Kind::SelfGone) {
        flush();
        return;
    }

    // The timer restarts on every event, so a burst is reported once it stops
    // rather than every 150 ms while it continues. An extraction or a `git
    // checkout` produces exactly such a burst, and reporting mid-burst would
    // make the panel flicker through intermediate states.
    m_timer.start();
}

void WatchCoalescer::flush()
{
    m_timer.stop();

    if (m_pending.isEmpty()) {
        return;
    }

    const QList<WatchEvent> events = m_pending;
    m_pending.clear();

    const WatchDelta result = coalesce(events);
    if (result.isEmpty()) {
        // Everything cancelled out — a temporary file that came and went. There
        // is nothing for the model to do.
        return;
    }

    qCDebug(pfFs) << "watch delta:" << result.created.size() << "created," << result.deleted.size()
                  << "deleted," << result.modified.size() << "modified," << result.renamed.size()
                  << "renamed, rescan:" << result.needsFullRescan;

    Q_EMIT delta(result);
}

void WatchCoalescer::reset()
{
    m_timer.stop();
    m_pending.clear();
}

int WatchCoalescer::pendingCount() const
{
    return static_cast<int>(m_pending.size());
}

} // namespace pf::fs
