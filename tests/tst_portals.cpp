// The portals.conf side of "Panefile answers Open and Save".

#include "platform/DefaultFileManager.h"
#include "platform/linux/XdgPortals.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace pf::platform::xdg;

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

const QByteArray kHyprland = "[preferred]\n"
                             "default=hyprland;gtk;\n"
                             "org.freedesktop.impl.portal.FileChooser=gtk;\n"
                             "org.freedesktop.impl.portal.Screencast=hyprland;\n";

} // namespace

class TestPortals : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    Environment env() const
    {
        const QString root = m_dir.path();
        Environment e;
        e.configHome = root + QStringLiteral("/config");
        e.configDirs = {root + QStringLiteral("/xdg")};
        e.dataHome = root + QStringLiteral("/data");
        e.dataDirs = {root + QStringLiteral("/share")};
        e.desktops = {QStringLiteral("hyprland")};
        return e;
    }

private Q_SLOTS:
    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path());
    }

    void desktopSpecificFileBeatsPortalsConfAndUserBeatsSystem()
    {
        const Environment e = env();
        write(e.configDirs.first() + "/xdg-desktop-portal/hyprland-portals.conf", kHyprland);
        write(e.configDirs.first() + "/xdg-desktop-portal/portals.conf", "[preferred]\n");
        QCOMPARE(effectivePortalConfig(e),
                 e.configDirs.first() + "/xdg-desktop-portal/hyprland-portals.conf");

        write(e.configHome + "/xdg-desktop-portal/portals.conf", "[preferred]\n");
        // Within one directory the desktop's file wins, but a higher
        // directory wins outright.
        QCOMPARE(effectivePortalConfig(e), e.configHome + "/xdg-desktop-portal/portals.conf");
    }

    void aSystemFileIsSeededIntoTheUsersConfigUnderTheSameName()
    {
        const Environment e = env();
        write(e.configDirs.first() + "/xdg-desktop-portal/hyprland-portals.conf", kHyprland);

        const PortalConfigTarget target = userPortalConfigTarget(e);
        QCOMPARE(target.path, e.configHome + "/xdg-desktop-portal/hyprland-portals.conf");
        QCOMPARE(target.content, kHyprland);
    }

    void theLineIsEditedInPlaceAndTheOldOneRestoredExactly()
    {
        const QByteArray edited = withFileChooser(kHyprland);
        QCOMPARE(edited,
                 QByteArray("[preferred]\n"
                            "default=hyprland;gtk;\n"
                            "# Panefile replaced: org.freedesktop.impl.portal.FileChooser=gtk;\n"
                            "org.freedesktop.impl.portal.FileChooser=panefile;gtk;\n"
                            "org.freedesktop.impl.portal.Screencast=hyprland;\n"));
        QCOMPARE(withFileChooser(edited), edited);
        QCOMPARE(withoutFileChooser(edited), kHyprland);
        QCOMPARE(fileChooserBackends(edited), (QStringList{"panefile", "gtk"}));
    }

    void withoutAnOldLineTheFallbackComesFromDefaultAndRestoreRemovesTheLine()
    {
        const QByteArray original = "[preferred]\ndefault=hyprland;gtk;\n\n[other]\nx=1\n";
        const QByteArray edited = withFileChooser(original);
        QCOMPARE(fileChooserBackends(edited), (QStringList{"panefile", "hyprland", "gtk"}));
        QVERIFY(edited.contains("[other]\nx=1\n"));
        QCOMPARE(withoutFileChooser(edited), original);

        QCOMPARE(withoutFileChooser(withFileChooser("")), QByteArray("[preferred]\n"));
    }

    void crlfSurvives()
    {
        const QByteArray original =
            "[preferred]\r\norg.freedesktop.impl.portal.FileChooser=gtk;\r\n";
        QCOMPARE(withoutFileChooser(withFileChooser(original)), original);
    }

    void aBackendWithoutAFileChooserIsNotOffered()
    {
        const Environment e = env();
        write(e.dataDirs.first() + "/xdg-desktop-portal/portals/hyprland.portal",
              "[portal]\nInterfaces=org.freedesktop.impl.portal.Screencast;\n");
        write(e.dataDirs.first() + "/xdg-desktop-portal/portals/gtk.portal",
              "[portal]\nInterfaces=org.freedesktop.impl.portal.FileChooser;"
              "org.freedesktop.impl.portal.Print;\n");
        const char *chooser = kFileChooserInterface;
        QVERIFY(!portalImplements(e, "hyprland", chooser));
        QVERIFY(portalImplements(e, "gtk", chooser));
        QVERIFY(!portalImplements(e, "missing", chooser));

        write(e.dataHome + "/xdg-desktop-portal/portals/gtk.portal", "[portal]\nInterfaces=x;\n");
        QVERIFY(!portalImplements(e, "gtk", chooser));

        pf::platform::FileChooserStatus status;
        status.backends = {"panefile", "hyprland", "gtk"};
        status.withoutChooser = {"hyprland"};
        QCOMPARE(status.fallback(), QStringLiteral("gtk"));
        status.backends = {"hyprland", "gtk"};
        QCOMPARE(status.fallback(), QStringLiteral("gtk"));
    }

    void aCopiedSystemConfigIsRedundantOnlyWhileItMatches()
    {
        const Environment e = env();
        const QString system = e.configDirs.first() + "/xdg-desktop-portal/hyprland-portals.conf";
        const QString copy = e.configHome + "/xdg-desktop-portal/hyprland-portals.conf";
        write(system, kHyprland);

        const QByteArray roundTrip = withoutFileChooser(withFileChooser(kHyprland));
        write(copy, roundTrip);
        QVERIFY(isRedundantUserConfig(e, copy, roundTrip));

        const QByteArray edited = roundTrip + "extra=1\n";
        write(copy, edited);
        QVERIFY(!isRedundantUserConfig(e, copy, edited));

        QVERIFY(!isRedundantUserConfig(e, system, kHyprland));

        QFile::remove(copy);
        QVERIFY(QFile::link(system, copy));
        QVERIFY(!isRedundantUserConfig(e, copy, kHyprland));
    }

    void aCopyMadeFromNothingIsRedundant()
    {
        const Environment e = env();
        const QString file = e.configHome + "/xdg-desktop-portal/portals.conf";
        for (const QString &other : portalConfigSearchPaths(e)) {
            if (QFileInfo(other).isFile()) {
                QSKIP("this machine has a system portals.conf the test can't hide");
            }
        }
        write(file, "[preferred]\n");
        QVERIFY(isRedundantUserConfig(e, file, "[preferred]\n"));
        QVERIFY(!isRedundantUserConfig(e, file, "[preferred]\ndefault=gtk;\n"));
    }

    void portalFilesNameTheBackend()
    {
        const Environment e = env();
        QVERIFY(
            portalFileContents().contains("DBusName=org.freedesktop.impl.portal.desktop.panefile"));
        QVERIFY(!portalFileContents().contains("UseIn"));
        QCOMPARE(portalFilePath(e), e.dataHome + "/xdg-desktop-portal/portals/panefile.portal");
        QVERIFY(portalServiceFileContents("/usr/bin/pf").contains("Exec=/usr/bin/pf --portal\n"));
        QVERIFY(!writePortalServiceFile(e, "pf").ok);
        QVERIFY(writePortalServiceFile(e, "/usr/bin/pf").ok);
        QCOMPARE(executableInServiceFile(readFile(portalServiceFilePath(e))),
                 QStringLiteral("/usr/bin/pf"));
    }
};

QTEST_MAIN(TestPortals)
#include "tst_portals.moc"
