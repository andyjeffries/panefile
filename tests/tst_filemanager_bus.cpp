// org.freedesktop.FileManager1, end to end on a private session bus.
//
// ctest runs this under `dbus-run-session`, which starts a bus for this
// process alone and points DBUS_SESSION_BUS_ADDRESS at it. Without that it
// skips: owning "Show in folder" on the developer's own desktop is the last
// thing a test should do.

#include "app/Application.h"
#include "app/CommandLine.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace pf;

namespace {

const QString kName = QStringLiteral("org.freedesktop.FileManager1");

bool touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly);
}

QString uri(const QString &path)
{
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

} // namespace

class TestFileManagerBus : public QObject
{
    Q_OBJECT

public:
    TestFileManagerBus(Application *app, QString fixtureRoot)
        : m_app(app), m_root(std::move(fixtureRoot))
    {}

private Q_SLOTS:

    void initTestCase()
    {
        if (!qEnvironmentVariableIsSet("PF_TEST_PRIVATE_BUS")) {
            QSKIP("needs dbus-run-session, which provides a private session bus");
        }
        QVERIFY(QDBusConnection::sessionBus().isConnected());

        // Exactly what the bus does on activation: pf --dbus-service.
        CommandLineOptions options;
        options.dbusService = true;
        m_app->startUp(options);

        QTRY_VERIFY_WITH_TIMEOUT(
            QDBusConnection::sessionBus().interface()->isServiceRegistered(kName).value(), 5000);

        // A window is held back until a call says what to show.
        QVERIFY(!m_app->mainWindow()->isVisible());
        QVERIFY(m_app->mainWindow()->panelStrip()->panels().isEmpty());

        // A second connection, so calls go through the bus daemon the way a
        // browser's do, rather than being short-circuited inside this one.
        m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                 QStringLiteral("tst-client"));
        QVERIFY(m_client.isConnected());
    }

    /// The acceptance check: ShowItems on a file opens its folder, with the
    /// file selected and the cursor on it, and shows the window.
    void showItemsSelectsTheItem()
    {
        const QString file = m_root + QStringLiteral("/etc/hosts");
        QVERIFY(touch(file));

        QVERIFY(call(QStringLiteral("ShowItems"), {uri(file)}));

        ui::FilePanel *panel = nullptr;
        QTRY_VERIFY((panel = focusedPanel()) != nullptr);
        QCOMPARE(panel->path(), m_root + QStringLiteral("/etc"));
        QTRY_COMPARE(panel->cursorName(), QStringLiteral("hosts"));
        QCOMPARE(panel->selectedPaths(), QStringList{file});
        QTRY_VERIFY(m_app->mainWindow()->isVisible());
    }

    /// Several items in one folder are one panel with all of them selected,
    /// and an item with an awkward name survives the percent-encoding.
    void showItemsGroupsByFolder()
    {
        const QString first = m_root + QStringLiteral("/Downloads/report 100%.pdf");
        const QString second = m_root + QStringLiteral("/Downloads/été.txt");
        QVERIFY(touch(first));
        QVERIFY(touch(second));

        // Whether this lands in the focused panel or a new one is §10.2's
        // focused-window rule, which the offscreen platform answers its own
        // way. What matters is that one panel shows the folder.
        QVERIFY(call(QStringLiteral("ShowItems"), {uri(first), uri(second)}));

        ui::FilePanel *panel = nullptr;
        QTRY_VERIFY((panel = panelShowing(m_root + QStringLiteral("/Downloads"))) != nullptr);
        QTRY_COMPARE(panel->cursorName(), QStringLiteral("report 100%.pdf"));
        QStringList selected = panel->selectedPaths();
        selected.sort();
        QCOMPARE(selected, QStringList({first, second}));
    }

    void showFoldersOpensTheFolder()
    {
        const QString folder = m_root + QStringLiteral("/Pictures");
        QVERIFY(QDir().mkpath(folder));

        QVERIFY(call(QStringLiteral("ShowFolders"), {uri(folder + QLatin1Char('/'))}));

        ui::FilePanel *panel = nullptr;
        QTRY_VERIFY((panel = panelShowing(folder)) != nullptr);
        QCOMPARE(panel->selectionCount(), 0);
    }

    /// Not a file:// URI: answered, and ignored.
    void otherSchemesAreIgnored()
    {
        const qsizetype before = panelCount();
        const QString path = focusedPanel()->path();
        QVERIFY(call(QStringLiteral("ShowItems"), {QStringLiteral("https://example.com/a.txt")}));
        QTest::qWait(100);
        QCOMPARE(panelCount(), before);
        QCOMPARE(focusedPanel()->path(), path);
    }

private:
    bool call(const QString &method, const QStringList &uris)
    {
        QDBusMessage message = QDBusMessage::createMethodCall(
            kName, QStringLiteral("/org/freedesktop/FileManager1"), kName, method);
        message << uris << QString();

        // Asynchronous, with this thread's event loop spinning: the service
        // lives on this thread, and a blocking call would wait on itself.
        QDBusPendingCall pending = m_client.asyncCall(message, 5000);
        QElapsedTimer timer;
        timer.start();
        while (!pending.isFinished() && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        if (pending.isError()) {
            qWarning() << method << "failed:" << pending.error().message();
        }
        return pending.isFinished() && !pending.isError();
    }

    ui::FilePanel *focusedPanel() const
    {
        return m_app->mainWindow()->panelStrip()->focusedPanel();
    }

    ui::FilePanel *panelShowing(const QString &path) const
    {
        for (ui::FilePanel *panel : m_app->mainWindow()->panelStrip()->panels()) {
            if (panel->path() == path) {
                return panel;
            }
        }
        return nullptr;
    }

    qsizetype panelCount() const { return m_app->mainWindow()->panelStrip()->panels().size(); }

    Application *m_app;
    QString m_root;
    QDBusConnection m_client{QString()};
};

int main(int argc, char **argv)
{
    // Everything Panefile would write or listen on, in a sandbox: the config,
    // the session, the single-instance socket — a real one belonging to a
    // running Panefile must not answer for this test.
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) {
        return 1;
    }
    for (const char *variable : {"PANEFILE_CONFIG_DIR", "PANEFILE_STATE_DIR", "PANEFILE_CACHE_DIR",
                                 "PANEFILE_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME"}) {
        qputenv(variable, QFile::encodeName(sandbox.path() + QLatin1Char('/') +
                                            QString::fromLatin1(variable).toLower()));
        QDir().mkpath(qEnvironmentVariable(variable));
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // Canonical, because the panels report canonical paths and /tmp may not
    // be one.
    const QString fixtures =
        QFileInfo(sandbox.path()).canonicalFilePath() + QStringLiteral("/fixtures");

    Application app(argc, argv);
    TestFileManagerBus test(&app, fixtures);
    return QTest::qExec(&test, argc, argv);
}

#include "tst_filemanager_bus.moc"
