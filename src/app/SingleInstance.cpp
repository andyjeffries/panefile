#include "app/SingleInstance.h"

#include "core/Logging.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSocketNotifier>

#include <array>
#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace pf {
namespace {

/// A file descriptor that closes itself.
///
/// Small enough to write out rather than reach for a library: the whole point
/// of this file is that it depends on nothing beyond libc.
class Descriptor
{
public:
    Descriptor() = default;
    explicit Descriptor(int fd) : m_fd(fd) {}

    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    Descriptor(Descriptor &&other) noexcept : m_fd(other.release()) {}
    Descriptor &operator=(Descriptor &&other) noexcept
    {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~Descriptor() { reset(); }

    void reset(int fd = -1)
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
        m_fd = fd;
    }

    int get() const { return m_fd; }
    bool isValid() const { return m_fd >= 0; }

    /// Hands the descriptor to the caller, who becomes responsible for it.
    int release()
    {
        const int fd = m_fd;
        m_fd = -1;
        return fd;
    }

private:
    int m_fd = -1;
};

/// Fills a sockaddr_un, or returns false when the path will not fit.
bool makeAddress(const QString &path, sockaddr_un *address)
{
    const QByteArray encoded = QFile::encodeName(path);

    // Strictly less than: sun_path must hold a terminating NUL as well. bind(2)
    // truncates silently, which would produce a socket at a path nobody will
    // ever connect to.
    if (encoded.size() >= static_cast<qsizetype>(sizeof(address->sun_path))) {
        qCWarning(pfIpc) << "socket path is too long for this platform:" << path;
        return false;
    }

    std::memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    std::memcpy(address->sun_path, encoded.constData(), static_cast<size_t>(encoded.size()));
    return true;
}

/// Waits for `events` on `fd`. Returns true when it became ready.
bool waitFor(int fd, short events, int timeoutMs)
{
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = events;

    // EINTR is not a failure: a signal arriving mid-wait says nothing about the
    // socket, and treating it as a timeout would open a second window because
    // the process happened to be resized.
    while (true) {
        const int ready = ::poll(&descriptor, 1, timeoutMs);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        return ready > 0;
    }
}

/// Writes everything, or fails. write(2) is permitted to write less than asked.
bool writeAll(int fd, const QByteArray &bytes)
{
    qsizetype written = 0;
    while (written < bytes.size()) {
        const ssize_t result =
            ::write(fd, bytes.constData() + written, static_cast<size_t>(bytes.size() - written));
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        written += result;
    }
    return true;
}

/// Connects to `path`. Returns an invalid descriptor when nothing is listening.
Descriptor connectTo(const QString &path)
{
    sockaddr_un address{};
    if (!makeAddress(path, &address)) {
        return {};
    }

    Descriptor socket(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (!socket.isValid()) {
        return {};
    }

    // A Unix-domain connect to a listening socket completes immediately — the
    // kernel queues it into the backlog without waiting for accept(2) — so this
    // does not block even though the socket is a blocking one. There is nothing
    // to wait for.
    if (::connect(socket.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        return {};
    }

    return socket;
}

} // namespace

int SingleInstance::maximumPathLength()
{
    return static_cast<int>(sizeof(sockaddr_un::sun_path)) - 1;
}

bool SingleInstance::sendToRunningInstance(const QString &socketPath,
                                           const InstanceMessage &message)
{
    // §10.3: "If a socket doesn't exist yet because another instance is
    // mid-startup (a genuine but rare race), the client retries once after
    // 50 ms before giving up and starting its own instance. Two windows is an
    // acceptable worst case; a hang is not."
    //
    // The retry is conditional on the socket file existing, which the spec's
    // wording does not say and its intent requires. Retrying unconditionally
    // put a 50 ms sleep in front of *every cold start* — the case where no
    // instance is running at all, which is the common one — and took Linux
    // first paint from 13.8 ms to 63.8 ms. The startup guard caught it.
    //
    // The race being guarded against cannot happen without the file: an
    // instance binds its socket before it shows a window, so an instance far
    // enough along to be worth waiting for has already created the path.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const Descriptor socket = connectTo(socketPath);

        if (!socket.isValid()) {
            if (attempt == 0 && QFile::exists(socketPath)) {
                ::usleep(static_cast<useconds_t>(kRetryDelayMs) * 1000);
                continue;
            }
            return false;
        }

        if (!writeAll(socket.get(), message.toJson())) {
            return false;
        }

        // §10.3: "wait for a short ack (50 ms cap)". Waiting at all is what
        // makes the exit code mean something — without it the client could
        // report success for a message the instance never read.
        if (!waitFor(socket.get(), POLLIN, kAckTimeoutMs)) {
            return false;
        }

        std::array<char, 32> reply{};
        const ssize_t read = ::read(socket.get(), reply.data(), reply.size() - 1);
        if (read <= 0) {
            return false;
        }

        return QByteArray(reply.data(), read).startsWith("ok");
    }

    return false;
}

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {}

SingleInstance::~SingleInstance()
{
    closeServer();
}

void SingleInstance::closeServer()
{
    m_notifier.reset();

    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;

        // The socket file outlives the process that made it, so its owner
        // removes it on the way out. A crash leaves it behind, which is exactly
        // the stale socket listen() knows how to reclaim.
        if (!m_socketPath.isEmpty()) {
            ::unlink(QFile::encodeName(m_socketPath).constData());
        }
    }
}

bool SingleInstance::listen(const QString &socketPath)
{
    m_socketPath = socketPath;

    // §10.3: "If listen() fails because a stale socket exists (previous process
    // killed), connect to it first — if that fails, unlink and retry once."
    //
    // The probe happens *before* the bind, not after a failed one. bind(2) on
    // an existing path fails with EADDRINUSE whether the socket is live or a
    // corpse and cannot tell them apart — so unlinking on failure would steal a
    // running instance's socket, leaving it alive and unreachable while every
    // later launch opened another window.
    //
    // Asking whether anything answers, first, is the only order that can tell a
    // corpse from a running process.
    if (QFile::exists(socketPath)) {
        if (connectTo(socketPath).isValid()) {
            qCDebug(pfIpc) << "another instance already owns" << socketPath;
            return false;
        }

        qCDebug(pfIpc) << "removing stale socket" << socketPath;
        ::unlink(QFile::encodeName(socketPath).constData());
    }

    sockaddr_un address{};
    if (!makeAddress(socketPath, &address)) {
        return false;
    }

    Descriptor socket(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (!socket.isValid()) {
        qCWarning(pfIpc) << "could not create a socket:" << std::strerror(errno);
        return false;
    }

    if (::bind(socket.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
        qCWarning(pfIpc) << "could not bind" << socketPath << std::strerror(errno);
        return false;
    }

    // Only this user. The socket lives in the per-user runtime directory, but
    // saying so explicitly costs one syscall and does not rely on that
    // directory's permissions being what they should be.
    ::chmod(QFile::encodeName(socketPath).constData(), S_IRUSR | S_IWUSR);

    // A short backlog: requests are answered in microseconds, and anything
    // deeper than this means something is very wrong.
    if (::listen(socket.get(), 8) != 0) {
        qCWarning(pfIpc) << "could not listen on" << socketPath << std::strerror(errno);
        return false;
    }

    m_listenFd = socket.release();
    return true;
}

void SingleInstance::startServing()
{
    if (m_listenFd < 0 || m_serving) {
        return;
    }
    m_serving = true;

    // QSocketNotifier is QtCore, so the event-loop integration costs nothing
    // beyond what the application already links.
    m_notifier = std::make_unique<QSocketNotifier>(m_listenFd, QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &SingleInstance::onNewConnection);

    // A connection that arrived between listen() and here is still in the
    // backlog, so it is drained rather than lost. Without this, a launch during
    // the few milliseconds of startup would hang until its 50 ms ack timeout
    // and then open a second window.
    onNewConnection();
}

void SingleInstance::onNewConnection()
{
    while (true) {
        // A zero-timeout poll says whether accept would block, so the drain
        // loop ends without one.
        if (!waitFor(m_listenFd, POLLIN, 0)) {
            return;
        }

        const Descriptor client(::accept(m_listenFd, nullptr, nullptr));
        if (!client.isValid()) {
            return;
        }

        // Read synchronously with a short cap rather than asynchronously. The
        // message is one small JSON object written immediately after connect,
        // and the client is already blocked waiting for the ack — an
        // asynchronous read would add a round trip to the fastest path in the
        // program to save a few milliseconds that are not being spent anyway.
        QByteArray buffer;
        QElapsedTimer timer;
        timer.start();

        while (!buffer.contains('\n') && timer.elapsed() < kAckTimeoutMs) {
            if (!waitFor(client.get(), POLLIN, static_cast<int>(kAckTimeoutMs - timer.elapsed()))) {
                break;
            }

            std::array<char, 4096> chunk{};
            const ssize_t read = ::read(client.get(), chunk.data(), chunk.size());
            if (read <= 0) {
                break;
            }
            buffer.append(chunk.data(), read);
        }

        InstanceMessage message;
        if (!InstanceMessage::fromJson(buffer, &message)) {
            // No ack: §10.3 has the client start its own instance when the
            // message is not understood, and an ack would tell it the opposite.
            qCWarning(pfIpc) << "ignoring an unreadable or wrong-version request";
            continue;
        }

        writeAll(client.get(), QByteArray(kAck));

        Q_EMIT messageReceived(message);
    }
}

bool SingleInstance::isListening() const
{
    return m_listenFd >= 0;
}

QString SingleInstance::socketPath() const
{
    return m_socketPath;
}

} // namespace pf
