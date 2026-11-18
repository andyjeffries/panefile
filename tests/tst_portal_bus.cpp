// The FileChooser backend over a private session bus, with a stand-in for the
// chooser window.
//
// Run under `dbus-run-session`; without it the test skips rather than take the
// FileChooser name on the developer's own desktop.

#include "platform/linux/FileChooserPortal_linux.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>

using namespace pf::platform;

namespace {

const QString kBusName = QStringLiteral("org.freedesktop.impl.portal.desktop.panefile.test");

QDBusMessage openFile(const QString &handle, const QVariantMap &options = {})
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        kBusName, QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.impl.portal.FileChooser"), QStringLiteral("OpenFile"));
    call << QVariant::fromValue(QDBusObjectPath(handle)) << QStringLiteral("org.test") << QString()
         << QStringLiteral("Open") << options;
    return call;
}

} // namespace

class TestPortalBus : public QObject
{
    Q_OBJECT

private:
    QDBusConnection m_client{QStringLiteral("none")};

    /// A chooser that ignores its stdin and prints `output`, then exits `code`.
    static QStringList fake(const QString &script) { return {"/bin/sh", "-c", script}; }

private Q_SLOTS:
    void initTestCase()
    {
        if (!qEnvironmentVariableIsSet("PF_TEST_PRIVATE_BUS")) {
            QSKIP("needs dbus-run-session, which provides a private session bus");
        }
        registerPortalTypes();
        m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                 QStringLiteral("tst-portal-client"));
        QVERIFY(m_client.isConnected());
    }

    void aPickComesBackAsUrisAndTheFilterTheUserChose()
    {
        FileChooserPortal portal(fake(
            R"(cat >/dev/null; printf '{"paths":["/tmp/a b.pdf"],"filter":0,"choices":[["enc","u"]]}')"));
        QVERIFY(portal.start(kBusName));

        QVariantMap options;
        const PortalFilter pdf{"PDF", {{0, "*.pdf"}}};
        options["filters"] = QVariant::fromValue(QList<PortalFilter>{pdf});

        QDBusMessage reply =
            m_client.call(openFile("/org/freedesktop/portal/desktop/request/t/1", options),
                          QDBus::BlockWithGui, 10000);
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().at(0).toUInt(), 0U);
        const QVariantMap results = qdbus_cast<QVariantMap>(reply.arguments().at(1));
        QCOMPARE(results.value("uris").toStringList(), QStringList{"file:///tmp/a%20b.pdf"});
        const PortalFilter chosen = qdbus_cast<PortalFilter>(results.value("current_filter"));
        QCOMPARE(chosen.name, QString("PDF"));
        QTRY_COMPARE_WITH_TIMEOUT(portal.activeRequests(), 0, 2000);
    }

    void aCancelIsResponseOneAndABrokenChooserIsResponseTwo()
    {
        FileChooserPortal cancelled(fake("exit 1"));
        QVERIFY(cancelled.start(kBusName));
        QDBusMessage reply = m_client.call(openFile("/org/freedesktop/portal/desktop/request/t/2"),
                                           QDBus::BlockWithGui, 10000);
        QCOMPARE(reply.arguments().at(0).toUInt(), 1U);
    }

    void aCrashIsResponseTwo()
    {
        FileChooserPortal broken(fake("exit 3"));
        QVERIFY(broken.start(kBusName));
        QDBusMessage reply = m_client.call(openFile("/org/freedesktop/portal/desktop/request/t/3"),
                                           QDBus::BlockWithGui, 10000);
        QCOMPARE(reply.arguments().at(0).toUInt(), 2U);
    }

    void closingTheRequestCancelsAndEndsTheChooser()
    {
        QTemporaryDir dir;
        const QString pidFile = dir.filePath("pid");
        FileChooserPortal portal(fake(QStringLiteral("echo $$ > %1; exec sleep 30").arg(pidFile)));
        QVERIFY(portal.start(kBusName));

        const QString handle = QStringLiteral("/org/freedesktop/portal/desktop/request/t/4");
        QDBusPendingCall pending = m_client.asyncCall(openFile(handle), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(portal.activeRequests(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(pidFile) && QFileInfo(pidFile).size() > 0, 5000);

        QFile file(pidFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const int pid = file.readAll().trimmed().toInt();
        QVERIFY(pid > 0);

        QDBusMessage close = QDBusMessage::createMethodCall(
            kBusName, handle, QStringLiteral("org.freedesktop.impl.portal.Request"),
            QStringLiteral("Close"));
        m_client.call(close, QDBus::BlockWithGui, 5000);

        QDBusPendingReply<uint, QVariantMap> reply = pending;
        QTRY_VERIFY_WITH_TIMEOUT(reply.isFinished(), 10000);
        QCOMPARE(reply.argumentAt<0>(), 1U);
        QTRY_VERIFY_WITH_TIMEOUT(::kill(pid, 0) != 0, 5000);
    }
};

QTEST_MAIN(TestPortalBus)
#include "tst_portal_bus.moc"
