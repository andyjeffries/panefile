// inotify backend (§7.3).

#include "platform/WatchBackend.h"

#include "core/Logging.h"

#include <QFile>
#include <QSocketNotifier>

#include <array>
#include <cerrno>

#include <sys/inotify.h>
#include <unistd.h>

namespace pf::platform {
namespace {

/// §7.3's event set, plus the two self-events it asks for.
constexpr uint32_t kWatchMask = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB |
                                IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;

/// Room for a good burst of events per read. Reading one at a time would turn
/// an extraction of ten thousand files into ten thousand syscalls.
constexpr size_t kBufferSize = static_cast<size_t>(64) * 1024;

WatchEvent::Kind kindOf(uint32_t mask)
{
    if ((mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0U) {
        return WatchEvent::Kind::SelfGone;
    }
    if ((mask & IN_Q_OVERFLOW) != 0U) {
        return WatchEvent::Kind::Overflow;
    }
    if ((mask & IN_CREATE) != 0U) {
        return WatchEvent::Kind::Created;
    }
    if ((mask & IN_DELETE) != 0U) {
        return WatchEvent::Kind::Deleted;
    }
    if ((mask & IN_MOVED_FROM) != 0U) {
        return WatchEvent::Kind::MovedFrom;
    }
    if ((mask & IN_MOVED_TO) != 0U) {
        return WatchEvent::Kind::MovedTo;
    }
    if ((mask & IN_ATTRIB) != 0U) {
        return WatchEvent::Kind::AttributesChanged;
    }
    return WatchEvent::Kind::Modified;
}

class InotifyBackend : public WatchBackend
{
public:
    explicit InotifyBackend(QObject *parent) : WatchBackend(parent) {}

    // stop() is virtual, and calling a virtual from a destructor dispatches to
    // *this* class rather than to any override — so the release is spelled out
    // here instead. It is the same work, without the pretence that a subclass's
    // stop() would run.
    ~InotifyBackend() override { release(); }

    bool isSupported() const override { return true; }

    QString watchedPath() const override { return m_path; }

    bool watch(const QString &path) override
    {
        stop();

        // IN_CLOEXEC so a shell command launched from the `:` prompt does not
        // inherit the descriptor, and IN_NONBLOCK so the read below never
        // blocks the GUI thread when the notifier fires spuriously.
        m_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_fd < 0) {
            qCWarning(pfFs) << "inotify_init1 failed:" << ::strerror(errno);
            return false;
        }

        m_watch = ::inotify_add_watch(m_fd, QFile::encodeName(path).constData(), kWatchMask);
        if (m_watch < 0) {
            qCDebug(pfFs) << "cannot watch" << path << ":" << ::strerror(errno);
            ::close(m_fd);
            m_fd = -1;
            return false;
        }

        m_path = path;
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &InotifyBackend::readEvents);
        return true;
    }

    void stop() override { release(); }

private:
    void release()
    {
        if (m_notifier != nullptr) {
            m_notifier->setEnabled(false);
            m_notifier->deleteLater();
            m_notifier = nullptr;
        }
        if (m_fd >= 0) {
            if (m_watch >= 0) {
                ::inotify_rm_watch(m_fd, m_watch);
            }
            ::close(m_fd);
        }
        m_fd = -1;
        m_watch = -1;
        m_path.clear();
    }

    void readEvents()
    {
        std::array<char, kBufferSize> buffer{};

        while (true) {
            const ssize_t length = ::read(m_fd, buffer.data(), buffer.size());
            if (length <= 0) {
                if (length < 0 && errno == EINTR) {
                    continue;
                }
                // EAGAIN: the queue is drained, which is the normal way out.
                return;
            }

            // The buffer holds a packed sequence of variable-length records,
            // each with its name inline. Walking it by the length field is the
            // only correct way to iterate.
            for (ssize_t offset = 0; offset < length;) {
                const auto *raw =
                    reinterpret_cast<const struct inotify_event *>(buffer.data() + offset);
                WatchEvent event;
                event.kind = kindOf(raw->mask);
                if (raw->len > 0) {
                    event.name = QFile::decodeName(raw->name);
                }

                // A directory being watched is not itself an entry in the
                // listing, so a nameless event that is not a self-event tells
                // the model nothing.
                if (!event.name.isEmpty() || event.kind == WatchEvent::Kind::SelfGone ||
                    event.kind == WatchEvent::Kind::Overflow) {
                    Q_EMIT rawEvent(event);
                }

                offset += static_cast<ssize_t>(sizeof(struct inotify_event) + raw->len);
            }
        }
    }

    int m_fd = -1;
    int m_watch = -1;
    QString m_path;
    QSocketNotifier *m_notifier = nullptr;
};

} // namespace

std::unique_ptr<WatchBackend> WatchBackend::create(QObject *parent)
{
    return std::make_unique<InotifyBackend>(parent);
}

} // namespace pf::platform
