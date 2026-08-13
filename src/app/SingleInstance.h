#pragma once

#include "app/InstanceMessage.h"

#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;

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
    /// Handles the stale-socket case the same way the spec describes: if the
    /// bind fails, try connecting; if that fails too, the socket is a corpse,
    /// so unlink it and retry once.
    bool listen(const QString &socketPath);

    /// Wires up connection handling. Called from the deferred startup queue.
    void startServing();

    bool isListening() const;
    QString socketPath() const;

Q_SIGNALS:
    /// A running instance received a request. Emitted on the GUI thread.
    void messageReceived(const pf::InstanceMessage &message);

private:
    void onNewConnection();

    std::unique_ptr<QLocalServer> m_server;
    QString m_socketPath;
    bool m_serving = false;
};

} // namespace pf
