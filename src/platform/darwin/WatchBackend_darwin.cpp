// FSEvents backend (§7.3, and the macOS half of the platform seam).

#include "platform/WatchBackend.h"

#include "core/Logging.h"

#include <QDir>
#include <QFileInfo>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

namespace pf::platform {
namespace {

/// How long FSEvents may sit on events before delivering them.
///
/// Zero, deliberately. FSEvents has its own coalescing latency, and using it
/// would mean two debounce windows stacked on top of each other — this one and
/// WatchCoalescer's 150 ms — with no way to reason about the total. All the
/// coalescing §7.3 specifies happens in WatchCoalescer, identically on both
/// platforms, so the backend's job is to deliver events promptly and nothing
/// else.
constexpr CFAbsoluteTime kLatency = 0.0;

class FSEventsBackend : public WatchBackend
{
public:
    explicit FSEventsBackend(QObject *parent) : WatchBackend(parent) {}

    // stopStream() rather than stop(): calling a virtual during destruction
    // bypasses virtual dispatch, and this class is the one that owns the
    // stream so it can tear it down directly.
    ~FSEventsBackend() override { stopStream(); }

    bool isSupported() const override { return true; }

    QString watchedPath() const override { return m_path; }

    bool watch(const QString &path) override
    {
        stop();

        if (!QFileInfo(path).isDir()) {
            return false;
        }

        CFStringRef cfPath = CFStringCreateWithCString(
            kCFAllocatorDefault, path.toUtf8().constData(), kCFStringEncodingUTF8);
        if (cfPath == nullptr) {
            return false;
        }

        CFArrayRef paths =
            CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void **>(&cfPath), 1,
                          &kCFTypeArrayCallBacks);
        CFRelease(cfPath);
        if (paths == nullptr) {
            return false;
        }

        FSEventStreamContext context{};
        context.info = this;

        // FileEvents rather than directory-level events: without it FSEvents
        // reports "something under this directory changed" and the model would
        // have to rescan for every keystroke in a text editor. WatchRoot adds
        // notification of the watched directory itself being moved or deleted,
        // which is §7.3's IN_MOVE_SELF and IN_DELETE_SELF.
        m_stream = FSEventStreamCreate(kCFAllocatorDefault, &FSEventsBackend::callback, &context,
                                       paths, kFSEventStreamEventIdSinceNow, kLatency,
                                       kFSEventStreamCreateFlagFileEvents |
                                           kFSEventStreamCreateFlagWatchRoot |
                                           kFSEventStreamCreateFlagNoDefer);
        CFRelease(paths);

        if (m_stream == nullptr) {
            qCWarning(pfFs) << "FSEventStreamCreate failed for" << path;
            return false;
        }

        // A private serial queue rather than the main one.
        //
        // The main dispatch queue is only drained while a CFRunLoop is running,
        // which is true under Qt's Cocoa plugin and *not* true under the
        // offscreen plugin, which uses a poll-based dispatcher. Depending on the
        // main queue therefore made watching work in the application and fail
        // silently in every headless run — including the tests. A private queue
        // always runs, and the callback hops to this object's thread itself.
        m_queue = dispatch_queue_create("org.panefile.fsevents", DISPATCH_QUEUE_SERIAL);
        FSEventStreamSetDispatchQueue(m_stream, m_queue);
        if (FSEventStreamStart(m_stream) == 0U) {
            qCWarning(pfFs) << "FSEventStreamStart failed for" << path;
            FSEventStreamInvalidate(m_stream);
            FSEventStreamRelease(m_stream);
            m_stream = nullptr;
            return false;
        }

        m_path = QDir::cleanPath(path);

        // FSEvents reports *canonical* paths, with every symlink resolved. On
        // macOS that matters constantly rather than in some edge case: /tmp and
        // /var are symlinks into /private, so a watch on anything below them
        // would compare its events against the wrong parent and discard every
        // one of them — a panel that silently never updates.
        m_canonicalPath = QFileInfo(m_path).canonicalFilePath();
        if (m_canonicalPath.isEmpty()) {
            m_canonicalPath = m_path;
        }
        return true;
    }

    void stop() override { stopStream(); }

private:
    void stopStream()
    {
        if (m_stream != nullptr) {
            FSEventStreamStop(m_stream);
            FSEventStreamInvalidate(m_stream);
            FSEventStreamRelease(m_stream);
            m_stream = nullptr;
        }
        if (m_queue != nullptr) {
            dispatch_release(m_queue);
            m_queue = nullptr;
        }
        m_path.clear();
        m_canonicalPath.clear();
    }

    static void callback(ConstFSEventStreamRef /*stream*/, void *info, size_t count,
                         void *eventPaths, const FSEventStreamEventFlags flags[],
                         const FSEventStreamEventId /*ids*/[])
    {
        auto *self = static_cast<FSEventsBackend *>(info);
        const auto *const *paths = static_cast<const char *const *>(eventPaths);

        // Copied out before hopping threads: the arrays belong to FSEvents and
        // are only valid for the duration of this callback.
        QList<QPair<QString, FSEventStreamEventFlags>> batch;
        batch.reserve(static_cast<qsizetype>(count));
        for (size_t i = 0; i < count; ++i) {
            batch.append({QString::fromUtf8(paths[i]), flags[i]});
        }

        // Onto the object's own thread, where the coalescer and the model live.
        QMetaObject::invokeMethod(
            self,
            [self, batch] {
                for (const auto &[path, flag] : batch) {
                    self->handleOne(path, flag);
                }
            },
            Qt::QueuedConnection);
    }

    void handleOne(const QString &fullPath, FSEventStreamEventFlags flags)
    {
        if ((flags & kFSEventStreamEventFlagUserDropped) != 0U ||
            (flags & kFSEventStreamEventFlagKernelDropped) != 0U) {
            Q_EMIT rawEvent(WatchEvent{.kind = WatchEvent::Kind::Overflow, .name = {}});
            return;
        }

        if ((flags & kFSEventStreamEventFlagRootChanged) != 0U) {
            Q_EMIT rawEvent(WatchEvent{.kind = WatchEvent::Kind::SelfGone, .name = {}});
            return;
        }

        const QFileInfo info(fullPath);
        const QString parent = QDir::cleanPath(info.absolutePath());

        // FileEvents reports the whole subtree, so entries in nested
        // directories arrive here too. A panel lists one directory, and a
        // change two levels down is not a row it is showing.
        if (parent != m_canonicalPath && parent != m_path) {
            return;
        }

        const QString name = info.fileName();
        if (name.isEmpty()) {
            return;
        }

        // FSEvents describes *what happened to the item* with a set of flags
        // rather than one verb, and several can be set at once — a file created
        // and written before the callback fires arrives as Created|Modified.
        // Each is emitted separately and WatchCoalescer decides what the
        // combination amounts to, which is the same logic inotify's stream goes
        // through.
        const bool exists = info.exists() || info.isSymLink();

        if ((flags & kFSEventStreamEventFlagItemRenamed) != 0U) {
            // A rename gives one event per side, and which side this is can
            // only be told from whether the path still exists. That is exactly
            // what inotify's MOVED_FROM and MOVED_TO distinguish, so the same
            // pairing logic applies.
            Q_EMIT rawEvent(
                WatchEvent{.kind = exists ? WatchEvent::Kind::MovedTo : WatchEvent::Kind::MovedFrom,
                           .name = name});
            return;
        }

        if ((flags & kFSEventStreamEventFlagItemRemoved) != 0U && !exists) {
            Q_EMIT rawEvent(WatchEvent{.kind = WatchEvent::Kind::Deleted, .name = name});
            return;
        }

        if ((flags & kFSEventStreamEventFlagItemCreated) != 0U && exists) {
            Q_EMIT rawEvent(WatchEvent{.kind = WatchEvent::Kind::Created, .name = name});
        }

        if ((flags &
             (kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemInodeMetaMod |
              kFSEventStreamEventFlagItemChangeOwner | kFSEventStreamEventFlagItemXattrMod)) !=
                0U &&
            exists) {
            Q_EMIT rawEvent(WatchEvent{.kind = WatchEvent::Kind::Modified, .name = name});
        }
    }

    FSEventStreamRef m_stream = nullptr;
    dispatch_queue_t m_queue = nullptr;
    QString m_path;

    /// The watched path with symlinks resolved, which is the form FSEvents
    /// reports paths in.
    QString m_canonicalPath;
};

} // namespace

std::unique_ptr<WatchBackend> WatchBackend::create(QObject *parent)
{
    return std::make_unique<FSEventsBackend>(parent);
}

} // namespace pf::platform
