#pragma once

#include "app/InstanceMessage.h"

#include <QObject>
#include <QString>

#include <memory>

class QSocketNotifier;

namespace pf {

/// The single-instance socket of §10.3.
///
/// The two halves are deliberately different shapes, because they have very
/// different constraints:
///
///   * **The client** is on the startup critical path and must never construct
///     a MainWindow, a QApplication, or anything else. It is a free function
///     taking a socket path and a message, and §10.3 wants it to complete "in a
///     couple of milliseconds".
///
///   * **The server** is a QObject that outlives the process's startup and
///     hands each request to the composition root.
///
/// Written against POSIX sockets rather than QLocalServer, which §10.3 names.
///
/// The measurement decided it. Linking Qt6::Network added twenty-eight shared
/// libraries to the binary's load-time dependencies on Arch — OpenSSL,
/// Kerberos, libcurl, nghttp2, and libproxy, which itself embeds a JavaScript
/// interpreter — every one of them mapped and relocated at every launch, for a
/// Unix domain socket that never speaks TCP, TLS or HTTP. §3.4 is explicit that
/// "a silent new DT_NEEDED entry is the most common way startup time
/// regresses", and the dependency guard caught this one loudly.
///
/// What is left needs nothing but libc. The client is four syscalls, which is a
/// better answer to §10.3's "a couple of milliseconds" than QLocalSocket was,
/// and the server keeps its event-loop integration through QSocketNotifier,
/// which lives in QtCore.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    /// §10.3's timings. The connect attempt is non-blocking; the ack wait is
    /// capped so a wedged instance cannot hang a new launch.
    static constexpr int kConnectTimeoutMs = 0;
    static constexpr int kAckTimeoutMs = 50;
    static constexpr int kRetryDelayMs = 50;

    /// How long the stale-socket probe waits before concluding that nothing is
    /// listening.
    ///
    /// Deliberately not the client's zero. The client's cost of being wrong is
    /// one extra window; this probe's cost of being wrong is unlinking a live
    /// instance's socket, after which that instance is unreachable for the rest
    /// of its life and every later launch opens another window. The two are not
    /// the same decision and must not share a constant.
    static constexpr int kProbeTimeoutMs = 200;

    /// The reply the server sends. Short and fixed: the client only needs to
    /// know it was heard.
    static constexpr const char *kAck = "ok\n";

    /// Tries to hand `message` to a running instance.
    ///
    /// Returns true when a running instance accepted it, in which case the
    /// caller must exit 0 without starting up. Returns false when there is no
    /// instance, when it did not answer, or when it speaks a different protocol
    /// version — all of which mean "start your own".
    ///
    /// Static and free of any Qt GUI type, because §10.3 puts this before
    /// everything else in main().
    static bool sendToRunningInstance(const QString &socketPath, const InstanceMessage &message);

    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    /// Binds the socket. §10.3: "listen() is two syscalls, so bind it before
    /// show(); only the connection-handling wiring is deferred."
    ///
    /// Handles the stale-socket case the spec describes, in the only order that
    /// works: probe first, then bind. See the comment in listen().
    bool listen(const QString &socketPath);

    /// The longest socket path this platform accepts.
    ///
    /// `sockaddr_un::sun_path` is 104 bytes on macOS and 108 on Linux, and a
    /// path longer than that is silently truncated by bind(2) — which produces
    /// a socket at a path nobody will ever connect to. Better to refuse.
    static int maximumPathLength();

    /// Wires up connection handling. Called from the deferred startup queue.
    void startServing();

    bool isListening() const;
    QString socketPath() const;

Q_SIGNALS:
    /// A running instance received a request. Emitted on the GUI thread.
    void messageReceived(const pf::InstanceMessage &message);

private:
    void onNewConnection();

    void closeServer();

    /// The listening socket, or -1. A raw descriptor rather than a QObject,
    /// because that is all it is.
    int m_listenFd = -1;

    /// Owns nothing but the readiness callback; the descriptor's lifetime is
    /// this object's.
    std::unique_ptr<QSocketNotifier> m_notifier;

    QString m_socketPath;
    bool m_serving = false;
};

} // namespace pf
