#include "app/InstanceMessage.h"
#include "app/Session.h"
#include "app/SingleInstance.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <thread>

using namespace pf;

namespace {

/// Runs the client hand-off on another thread and pumps this one meanwhile.
///
/// The client blocks waiting for an ack, and the server needs an event loop to
/// accept the connection at all — so in one thread they deadlock until the
/// client's 50 ms cap expires. In the real program they are separate
/// processes; a separate thread is the smallest faithful stand-in.
bool handOff(const QString &socketPath, const InstanceMessage &message)
{
    std::atomic<bool> finished{false};
    bool accepted = false;

    std::thread client([&] {
        accepted = SingleInstance::sendToRunningInstance(socketPath, message);
        finished.store(true, std::memory_order_release);
    });

    // Spins this thread's event loop, which is what lets the server accept.
    // Written out rather than with QTRY_VERIFY, because the QTest macros
    // return on failure and so cannot appear in a function that returns a
    // value.
    QElapsedTimer timer;
    timer.start();
    while (!finished.load(std::memory_order_acquire) && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    client.join();
    return accepted;
}

} // namespace

/// §10.2, §10.3 and §8's session file.
class TestInstance : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =========================================================== the message

    void roundTripsEveryField()
    {
        InstanceMessage sent;
        sent.cwd = QStringLiteral("/home/andy/work");
        sent.paths = {QStringLiteral("src"), QStringLiteral("/etc")};
        sent.placement = PlacementOverride::NewPanel;
        sent.activationToken = QStringLiteral("token-abc");
        sent.desktopStartupId = QStringLiteral("startup-1");

        InstanceMessage received;
        QVERIFY(InstanceMessage::fromJson(sent.toJson(), &received));

        QCOMPARE(received.cwd, sent.cwd);
        QCOMPARE(received.paths, sent.paths);
        QCOMPARE(received.placement, sent.placement);
        QCOMPARE(received.activationToken, sent.activationToken);
        QCOMPARE(received.desktopStartupId, sent.desktopStartupId);
    }

    void everyPlacementSurvivesTheRoundTrip()
    {
        for (const PlacementOverride placement :
             {PlacementOverride::None, PlacementOverride::Here, PlacementOverride::NewPanel,
              PlacementOverride::NewWindow}) {
            InstanceMessage sent;
            sent.placement = placement;

            InstanceMessage received;
            QVERIFY(InstanceMessage::fromJson(sent.toJson(), &received));
            QCOMPARE(received.placement, placement);
        }
    }

    /// §10.3: "on version mismatch, the client starts its own instance rather
    /// than sending something the server might misread."
    void rejectsAnotherProtocolVersion()
    {
        InstanceMessage message;
        QByteArray json = message.toJson();
        json.replace("\"v\":1", "\"v\":2");

        InstanceMessage received;
        QVERIFY(!InstanceMessage::fromJson(json, &received));

        // And in the other direction: an older client talking to this build.
        json = message.toJson();
        json.replace("\"v\":1", "\"v\":0");
        QVERIFY(!InstanceMessage::fromJson(json, &received));
    }

    void rejectsRubbish()
    {
        InstanceMessage received;
        QVERIFY(!InstanceMessage::fromJson(QByteArray("not json at all"), &received));
        QVERIFY(!InstanceMessage::fromJson(QByteArray("[1,2,3]"), &received));
        QVERIFY(!InstanceMessage::fromJson(QByteArray(), &received));
        QVERIFY(!InstanceMessage::fromJson(QByteArray("{}"), &received));
    }

    /// §10.1: relative paths resolve "against the **client's** working
    /// directory, not the running instance's".
    void resolvesPathsAgainstTheClientDirectory()
    {
        InstanceMessage message;
        message.cwd = QStringLiteral("/home/andy/work");
        message.paths = {QStringLiteral("src"), QStringLiteral("../other"),
                         QStringLiteral("/absolute")};

        QCOMPARE(message.absolutePaths(),
                 QStringList({QStringLiteral("/home/andy/work/src"),
                              QStringLiteral("/home/andy/other"), QStringLiteral("/absolute")}));
    }

    /// §10.1: "may be `file://` URIs so that `%U` in the .desktop file works."
    void decodesFileUris()
    {
        InstanceMessage message;
        message.cwd = QStringLiteral("/tmp");
        message.paths = {QStringLiteral("file:///home/andy/My%20Documents")};

        QCOMPARE(message.absolutePaths(), QStringList({QStringLiteral("/home/andy/My Documents")}));
    }

    /// A filename with a colon in it is not a URL, and must not be treated as
    /// one — which is exactly what QUrl::fromUserInput would do.
    void doesNotMistakeAColonForAScheme()
    {
        InstanceMessage message;
        message.cwd = QStringLiteral("/tmp");
        message.paths = {QStringLiteral("notes:draft.md")};

        QCOMPARE(message.absolutePaths(), QStringList({QStringLiteral("/tmp/notes:draft.md")}));
    }

    // ============================================================ the socket

    /// The whole hand-off, end to end over a real socket.
    void handsOffToAListeningInstance()
    {
        QTemporaryDir runtime;
        const QString socketPath = runtime.filePath(QStringLiteral("pf.sock"));

        SingleInstance server;
        QVERIFY(server.listen(socketPath));
        server.startServing();

        QSignalSpy received(&server, &SingleInstance::messageReceived);

        InstanceMessage message;
        message.cwd = QStringLiteral("/home/andy");
        message.paths = {QStringLiteral("Downloads")};

        QVERIFY(handOff(socketPath, message));
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 2000);

        const auto delivered = received.first().at(0).value<InstanceMessage>();
        QCOMPARE(delivered.cwd, QStringLiteral("/home/andy"));
        QCOMPARE(delivered.paths, QStringList({QStringLiteral("Downloads")}));
    }

    /// No instance means no hand-off, and the caller starts its own.
    void reportsFailureWhenNothingIsListening()
    {
        QTemporaryDir runtime;
        QVERIFY(!handOff(runtime.filePath(QStringLiteral("absent.sock")), InstanceMessage{}));
    }

    /// §10.3: "If listen() fails because a stale socket exists (previous
    /// process killed), connect to it first — if that fails, unlink and retry
    /// once."
    void reclaimsAStaleSocket()
    {
        QTemporaryDir runtime;
        const QString socketPath = runtime.filePath(QStringLiteral("stale.sock"));

        // A file where the socket should be, answering nothing — which is what
        // a killed process leaves behind.
        QFile corpse(socketPath);
        QVERIFY(corpse.open(QIODevice::WriteOnly));
        corpse.write("not a socket");
        corpse.close();

        SingleInstance server;
        QVERIFY(server.listen(socketPath));
        QVERIFY(server.isListening());
    }

    /// A live instance's socket must never be unlinked, however much it looks
    /// like a stale one from the outside.
    void refusesToStealALiveSocket()
    {
        QTemporaryDir runtime;
        const QString socketPath = runtime.filePath(QStringLiteral("live.sock"));

        SingleInstance first;
        QVERIFY(first.listen(socketPath));
        first.startServing();

        SingleInstance second;
        QVERIFY(!second.listen(socketPath));
        QVERIFY(!second.isListening());

        // And the first is still usable, which is the point.
        QVERIFY(first.isListening());
        QVERIFY(handOff(socketPath, InstanceMessage{}));
    }

    // =========================================================== the session

    void sessionRoundTrips()
    {
        Session session;
        session.windowGeometry = QRect(10, 20, 1200, 700);
        session.windowMaximised = true;
        session.focusedPanel = 1;
        session.quickLookDock = QStringLiteral("right");
        session.pinnedPaths = {QStringLiteral("/home/andy/work")};
        session.panels = {
            SessionPanel{.path = QStringLiteral("/home/andy"),
                         .cursorName = QStringLiteral("notes.md"),
                         .sortKey = QStringLiteral("modified"),
                         .reverseSort = true,
                         .showHidden = false},
            SessionPanel{.path = QStringLiteral("/tmp"),
                         .cursorName = QString(),
                         .sortKey = QStringLiteral("name"),
                         .reverseSort = false,
                         .showHidden = true},
        };

        const Session restored = Session::fromIni(session.toIni());

        QCOMPARE(restored.windowGeometry, session.windowGeometry);
        QCOMPARE(restored.windowMaximised, session.windowMaximised);
        QCOMPARE(restored.focusedPanel, session.focusedPanel);
        QCOMPARE(restored.quickLookDock, session.quickLookDock);
        QCOMPARE(restored.pinnedPaths, session.pinnedPaths);
        QCOMPARE(restored.panels, session.panels);
    }

    void ignoresAnUnreadableSessionFile()
    {
        QVERIFY(Session::fromIni(QStringLiteral("garbage\nmore garbage")).isEmpty());
        QVERIFY(Session::fromIni(QString()).isEmpty());
    }

    /// A panel entry with no path is not a panel, however many other keys it
    /// carries.
    void dropsPanelsWithoutAPath()
    {
        const Session session = Session::fromIni(QStringLiteral("[panel0]\nsort=name\n"));
        QVERIFY(session.panels.isEmpty());
    }

    /// A session pointing at a directory that has gone — an unmounted volume,
    /// usually — must not leave the user staring at an error every launch.
    void prunesDirectoriesThatHaveGone()
    {
        QTemporaryDir dir;

        Session session;
        session.panels = {SessionPanel{.path = dir.path()},
                          SessionPanel{.path = QStringLiteral("/nonexistent/volume")},
                          SessionPanel{.path = QDir::tempPath()}};
        session.focusedPanel = 2;
        session.pinnedPaths = {dir.path(), QStringLiteral("/also/gone")};

        const Session pruned = session.pruned();

        QCOMPARE(pruned.panels.size(), 2);
        QCOMPARE(pruned.pinnedPaths.size(), 1);
        // The focused index referred to the list before pruning, so it is
        // clamped rather than trusted.
        QCOMPARE(pruned.focusedPanel, 1);
    }

    void prunedEmptySessionIsSafe()
    {
        Session session;
        session.panels = {SessionPanel{.path = QStringLiteral("/nonexistent")}};
        session.focusedPanel = 0;

        const Session pruned = session.pruned();
        QVERIFY(pruned.isEmpty());
        QCOMPARE(pruned.focusedPanel, 0);
    }
};

QTEST_MAIN(TestInstance)
#include "tst_instance.moc"
