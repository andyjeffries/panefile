#include "app/SingleInstance.h"

#include "core/Logging.h"

#include <QElapsedTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

namespace pf {

bool SingleInstance::sendToRunningInstance(const QString &socketPath,
                                           const InstanceMessage &message)
{
    // Two attempts, per §10.3: "If a socket doesn't exist yet because another
    // instance is mid-startup (a genuine but rare race), the client retries
    // once after 50 ms before giving up and starting its own instance. Two
    // windows is an acceptable worst case; a hang is not."
    for (int attempt = 0; attempt < 2; ++attempt) {
        QLocalSocket socket;
        socket.connectToServer(socketPath);

        // waitForConnected with a zero timeout still completes a connection
        // that succeeds immediately, which on a Unix domain socket is the
        // normal case: connect(2) to a listening socket does not block.
        if (!socket.waitForConnected(kConnectTimeoutMs)) {
            if (attempt == 0) {
                QThread::msleep(kRetryDelayMs);
                continue;
            }
            return false;
        }

        socket.write(message.toJson());
        if (!socket.waitForBytesWritten(kAckTimeoutMs)) {
            return false;
        }

        // §10.3: "wait for a short ack (50 ms cap)". Waiting at all is what
        // makes the exit code mean something — without it the client could
        // report success for a message the instance never read.
        if (!socket.waitForReadyRead(kAckTimeoutMs)) {
            return false;
        }

        const QByteArray ack = socket.readAll();
        socket.disconnectFromServer();

        return ack.startsWith("ok");
    }

    return false;
}

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::listen(const QString &socketPath)
{
    m_socketPath = socketPath;

    // §10.3: "If listen() fails because a stale socket exists (previous process
    // killed), connect to it first — if that fails, unlink and retry once."
    //
    // The probe happens *before* the bind, not after a failed one, which is not
    // what the spec's wording suggests and is what it means. Qt's
    // QLocalServer::listen unlinks an existing socket file itself and then
    // succeeds — so a bind-first implementation never sees a failure to
    // recover from, and quietly steals the socket of a running instance. That
    // instance stays alive and unreachable, and every launch afterwards opens
    // another window.
    //
    // Asking whether anything answers, first, is the only order that can tell
    // a corpse from a live process.
    if (QFile::exists(socketPath)) {
        QLocalSocket probe;
        probe.connectToServer(socketPath);

        if (probe.waitForConnected(kProbeTimeoutMs)) {
            probe.disconnectFromServer();
            qCDebug(pfIpc) << "another instance already owns" << socketPath;
            return false;
        }

        qCDebug(pfIpc) << "removing stale socket" << socketPath;
        QLocalServer::removeServer(socketPath);
    }

    m_server = std::make_unique<QLocalServer>();

    // Refuse connections from other users. The socket lives in the per-user
    // runtime directory, but saying so explicitly costs nothing and does not
    // rely on that directory's permissions being what they should be.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    if (m_server->listen(socketPath)) {
        return true;
    }

    qCWarning(pfIpc) << "could not listen on" << socketPath << m_server->errorString();
    m_server.reset();
    return false;
}

void SingleInstance::startServing()
{
    if (m_server == nullptr || m_serving) {
        return;
    }
    m_serving = true;

    connect(m_server.get(), &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);

    // A connection that arrived between listen() and here is still queued, so
    // it is drained rather than lost. Without this, a launch during the few
    // milliseconds of startup would hang until its 50 ms ack timeout and then
    // open a second window.
    onNewConnection();
}

void SingleInstance::onNewConnection()
{
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (socket == nullptr) {
            return;
        }

        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);

        // Read synchronously with a short cap rather than asynchronously. The
        // message is one small JSON object written immediately after connect,
        // and the client is already blocked waiting for the ack — an
        // asynchronous read would add a round trip to the fastest path in the
        // program to save a few milliseconds that are not being spent anyway.
        QByteArray buffer;
        QElapsedTimer timer;
        timer.start();

        while (!buffer.contains('\n') && timer.elapsed() < kAckTimeoutMs) {
            if (!socket->waitForReadyRead(static_cast<int>(kAckTimeoutMs - timer.elapsed()))) {
                break;
            }
            buffer += socket->readAll();
        }

        InstanceMessage message;
        if (!InstanceMessage::fromJson(buffer, &message)) {
            // No ack: §10.3 has the client start its own instance when the
            // message is not understood, and an ack would tell it the opposite.
            qCWarning(pfIpc) << "ignoring an unreadable or wrong-version request";
            socket->disconnectFromServer();
            continue;
        }

        socket->write(kAck);
        socket->flush();
        socket->disconnectFromServer();

        Q_EMIT messageReceived(message);
    }
}

bool SingleInstance::isListening() const
{
    return m_server != nullptr && m_server->isListening();
}

QString SingleInstance::socketPath() const
{
    return m_socketPath;
}

} // namespace pf
