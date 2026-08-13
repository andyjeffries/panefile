#pragma once

#include <QString>

namespace pf::platform {

/// A raw filesystem event, as a backend reports it.
///
/// Lives in the platform layer rather than in fs, because it is the thing the
/// platform produces: inotify's IN_CREATE and FSEvents'
/// kFSEventStreamEventFlagItemCreated both arrive as Created, and everything
/// above knows only this vocabulary. `fs::WatchDelta` is the other half of the
/// pair — what the *model* consumes — and belongs upstairs with the coalescing
/// logic that produces it.
///
/// The split was forced by the layering check (§3.1): putting both in fs would
/// have made the platform layer depend upward on it.
struct WatchEvent {
    enum class Kind {
        Created,
        Deleted,
        Modified,
        AttributesChanged,
        MovedFrom,
        MovedTo,
        /// The watched directory itself went away or was moved.
        SelfGone,
        /// The backend lost events and the watcher must rescan.
        Overflow,
    };

    Kind kind = Kind::Modified;
    QString name; ///< basename within the watched directory; empty for SelfGone
};

} // namespace pf::platform
