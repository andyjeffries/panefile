#include "input/ActionRegistry.h"
#include "app/DefaultFileManagerOffer.h"
#include "config/Config.h"
#include "platform/DefaultFileManager.h"
#include "platform/FileManagerService.h"
#include "platform/linux/XdgMimeApps.h"
#include "ui/DefaultFileManagerBar.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace pf;
namespace xdg = pf::platform::xdg;

namespace {

bool write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(bytes) == bytes.size();
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// readlink(2), raw: the text of the link, not where it resolves to.
QByteArray readLink(const QString &path)
{
    QByteArray buffer(4096, '\0');
    const ssize_t length =
        ::readlink(QFile::encodeName(path).constData(), buffer.data(), buffer.size());
    return length < 0 ? QByteArray() : buffer.left(length);
}

QByteArray desktopEntry(const QByteArray &name, const QByteArray &extra = {})
{
    return "[Desktop Entry]\nType=Application\nName=" + name + "\nExec=true\n" + extra;
}

const QString kMime = QStringLiteral("inode/directory");
const QString kPanefile = QStringLiteral("panefile.desktop");
const QString kNautilus = QStringLiteral("org.gnome.Nautilus.desktop");
const QString kDolphin = QStringLiteral("org.kde.dolphin.desktop");

platform::DefaultFileManagerStatus nautilusIsDefault()
{
    platform::DefaultFileManagerStatus status;
    status.supported = true;
    status.handlerId = kNautilus;
    status.handlerName = QStringLiteral("Files");
    status.panefileInstalled = true;
    return status;
}

} // namespace

/// The default file manager offer: XDG MIME resolution, editing
/// mimeapps.list without damaging it, the FileManager1 service file, and when
/// the bar is allowed to appear.
///
/// Nothing here may touch the real home directory. initTestCase points every
/// XDG variable, the config directory and the session bus at a sandbox before
/// any test runs, so that even the code paths that read the process
/// environment cannot reach the developer's own files or desktop.
class TestDefaultFileManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_sandbox.isValid());
        const QString root = m_sandbox.path();

        qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + QStringLiteral("/config-home")));
        qputenv("XDG_DATA_HOME", QFile::encodeName(root + QStringLiteral("/data-home")));
        qputenv("XDG_CONFIG_DIRS", QFile::encodeName(root + QStringLiteral("/etc-xdg")));
        qputenv("XDG_DATA_DIRS", QFile::encodeName(root + QStringLiteral("/usr-share")));
        qputenv("XDG_CURRENT_DESKTOP", "Hyprland");
        qputenv("PANEFILE_CONFIG_DIR", QFile::encodeName(root + QStringLiteral("/panefile")));

        // A bus that is not there, so a ReloadConfig fails fast and harmlessly
        // instead of reaching the session the tests happen to run in.
        qputenv("DBUS_SESSION_BUS_ADDRESS",
                QFile::encodeName(QStringLiteral("unix:path=") + root + QStringLiteral("/no-bus")));
        qunsetenv("DBUS_STARTER_BUS_TYPE");

        // A `pf` first on PATH, so the service file the end-to-end test writes
        // names something that exists and is called pf.
        m_fakePf = root + QStringLiteral("/bin/pf");
        QVERIFY(write(m_fakePf, "#!/bin/sh\n"));
        QVERIFY(
            QFile::setPermissions(m_fakePf, QFile::permissions(m_fakePf) | QFileDevice::ExeOwner));
        const QByteArray path = QFile::encodeName(root + QStringLiteral("/bin:")) + qgetenv("PATH");
        qputenv("PATH", path);
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        const QString root = m_dir->path();
        m_env.configHome = root + QStringLiteral("/home/.config");
        m_env.dataHome = root + QStringLiteral("/home/.local/share");
        m_env.configDirs = {root + QStringLiteral("/etc/xdg")};
        m_env.dataDirs = {root + QStringLiteral("/usr/local/share"),
                          root + QStringLiteral("/usr/share")};
        m_env.desktops = {QStringLiteral("hyprland")};
    }

    void cleanup() { m_dir.reset(); }

    // ======================================================== environment

    void theEnvironmentComesFromTheXdgVariables()
    {
        qputenv("XDG_CURRENT_DESKTOP", "Hyprland:GNOME");
        qputenv("XDG_DATA_DIRS", "/one:relative/ignored:/two");
        const xdg::Environment env = xdg::Environment::fromProcess();
        qputenv("XDG_CURRENT_DESKTOP", "Hyprland");
        qputenv("XDG_DATA_DIRS",
                QFile::encodeName(m_sandbox.path() + QStringLiteral("/usr-share")));

        QCOMPARE(env.configHome, m_sandbox.path() + QStringLiteral("/config-home"));
        QCOMPARE(env.dataHome, m_sandbox.path() + QStringLiteral("/data-home"));
        QCOMPARE(env.desktops, QStringList({QStringLiteral("hyprland"), QStringLiteral("gnome")}));
        QCOMPARE(env.dataDirs, QStringList({QStringLiteral("/one"), QStringLiteral("/two")}));
    }

    void thePrecedenceOrderIsTheSpecifications()
    {
        const QString root = m_dir->path();
        QCOMPARE(
            xdg::mimeappsListPaths(m_env),
            QStringList({
                root + QStringLiteral("/home/.config/hyprland-mimeapps.list"),
                root + QStringLiteral("/home/.config/mimeapps.list"),
                root + QStringLiteral("/etc/xdg/hyprland-mimeapps.list"),
                root + QStringLiteral("/etc/xdg/mimeapps.list"),
                root + QStringLiteral("/home/.local/share/applications/hyprland-mimeapps.list"),
                root + QStringLiteral("/home/.local/share/applications/mimeapps.list"),
                root + QStringLiteral("/usr/local/share/applications/hyprland-mimeapps.list"),
                root + QStringLiteral("/usr/local/share/applications/mimeapps.list"),
                root + QStringLiteral("/usr/share/applications/hyprland-mimeapps.list"),
                root + QStringLiteral("/usr/share/applications/mimeapps.list"),
            }));
    }

    // ========================================================= resolution

    void aDesktopSpecificFileBeatsMimeappsList()
    {
        installSystem(kNautilus, "Files");
        installSystem(kDolphin, "Dolphin");
        QVERIFY(write(configFile("mimeapps.list"),
                      "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n"));
        QVERIFY(write(configFile("hyprland-mimeapps.list"),
                      "[Default Applications]\ninode/directory=org.kde.dolphin.desktop\n"));

        const xdg::Resolution resolution = xdg::resolveDefault(m_env, kMime);
        QCOMPARE(resolution.desktopId, kDolphin);
        QCOMPARE(resolution.sourceFile, configFile("hyprland-mimeapps.list"));
    }

    void theUsersFileBeatsTheSystems()
    {
        installSystem(kNautilus, "Files");
        installSystem(kDolphin, "Dolphin");
        QVERIFY(write(m_dir->filePath(QStringLiteral("etc/xdg/mimeapps.list")),
                      "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n"));
        QVERIFY(write(configFile("mimeapps.list"),
                      "[Default Applications]\ninode/directory=org.kde.dolphin.desktop\n"));

        QCOMPARE(xdg::resolveDefault(m_env, kMime).desktopId, kDolphin);
    }

    void aListGivesTheFirstInstalledEntry()
    {
        installSystem(kNautilus, "Files");
        QVERIFY(write(configFile("mimeapps.list"),
                      "[Default Applications]\n"
                      "inode/directory=gone.desktop;org.gnome.Nautilus.desktop;\n"));

        QCOMPARE(xdg::resolveDefault(m_env, kMime).desktopId, kNautilus);
        QCOMPARE(xdg::associationsIn(read(configFile("mimeapps.list")),
                                     QLatin1String(xdg::kDefaultApplicationsGroup), kMime),
                 QStringList({QStringLiteral("gone.desktop"), kNautilus}));
    }

    /// An entry for something no longer installed is skipped, and the search
    /// carries on into lower-precedence files rather than stopping there.
    void anUninstalledEntryIsSkipped()
    {
        installSystem(kNautilus, "Files");
        QVERIFY(write(configFile("mimeapps.list"),
                      "[Default Applications]\ninode/directory=uninstalled.desktop\n"));
        QVERIFY(write(m_dir->filePath(QStringLiteral("etc/xdg/mimeapps.list")),
                      "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n"));

        const xdg::Resolution resolution = xdg::resolveDefault(m_env, kMime);
        QCOMPARE(resolution.desktopId, kNautilus);
        QCOMPARE(resolution.sourceFile, m_dir->filePath(QStringLiteral("etc/xdg/mimeapps.list")));
    }

    void otherGroupsAndTypesAreNotTheDefault()
    {
        installSystem(kNautilus, "Files");
        QVERIFY(write(configFile("mimeapps.list"),
                      "[Added Associations]\ninode/directory=org.gnome.Nautilus.desktop;\n"
                      "[Default Applications]\ntext/plain=org.gnome.Nautilus.desktop\n"));

        QVERIFY(xdg::resolveDefault(m_env, kMime).desktopId.isEmpty());
    }

    /// `kde4-dolphin.desktop` may live at `applications/kde4/dolphin.desktop`.
    void aDashCanBeASubdirectory()
    {
        QVERIFY(
            write(m_dir->filePath(QStringLiteral("usr/share/applications/kde4/dolphin.desktop")),
                  desktopEntry("Dolphin")));
        QVERIFY(!xdg::desktopFilePath(m_env, QStringLiteral("kde4-dolphin.desktop")).isEmpty());
        QVERIFY(xdg::desktopFilePath(m_env, QStringLiteral("kde5-dolphin.desktop")).isEmpty());
    }

    /// A user's Hidden=true copy is how an application is uninstalled for one
    /// user, so it counts as not installed even with the system's copy there.
    void aHiddenEntryIsNotInstalled()
    {
        installSystem(kNautilus, "Files");
        QVERIFY(write(m_env.dataHome + QStringLiteral("/applications/") + kNautilus,
                      desktopEntry("Files", "Hidden=true\n")));
        QVERIFY(xdg::desktopFilePath(m_env, kNautilus).isEmpty());
    }

    // ============================================================ writing

    void aMissingFileIsCreated()
    {
        const xdg::WriteResult result = xdg::setDefaultApplication(m_env, kMime, kPanefile);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.path, configFile("mimeapps.list"));
        QCOMPARE(read(result.path), QByteArray("[Default Applications]\n"
                                               "inode/directory=panefile.desktop\n"
                                               "\n"
                                               "[Added Associations]\n"
                                               "inode/directory=panefile.desktop;\n"));
    }

    void aMissingGroupIsAppended()
    {
        const QByteArray before = "# mine\n[Added Associations]\ntext/plain=gvim.desktop;\n";
        const QByteArray after = xdg::withDefaultApplication(before, kMime, kPanefile);
        QCOMPARE(after, QByteArray("# mine\n[Added Associations]\ntext/plain=gvim.desktop;\n"
                                   "inode/directory=panefile.desktop;\n"
                                   "\n"
                                   "[Default Applications]\n"
                                   "inode/directory=panefile.desktop\n"));
    }

    void anExistingKeyIsReplaced()
    {
        const QByteArray before = "[Default Applications]\n"
                                  "text/html=firefox.desktop\n"
                                  "inode/directory=org.gnome.Nautilus.desktop\n"
                                  "image/png=imv.desktop\n";
        const QByteArray after = xdg::withDefaultApplication(before, kMime, kPanefile);
        QVERIFY2(after.startsWith("[Default Applications]\n"
                                  "text/html=firefox.desktop\n"
                                  "inode/directory=panefile.desktop\n"
                                  "image/png=imv.desktop\n"),
                 after.constData());
    }

    /// Comments, ordering, other groups and keys this does not understand all
    /// come back exactly as they were. The only lines that differ are the two
    /// it was asked to change.
    void everythingElseSurvivesByteForByte()
    {
        const QByteArray before = "# Managed by stow — edit in ~/dotfiles\n"
                                  "\n"
                                  "[X-Unknown Group]\n"
                                  "whatever = this means\n"
                                  "\n"
                                  "[Default Applications]\n"
                                  "# folders\n"
                                  "inode/directory = org.gnome.Nautilus.desktop\n"
                                  "x-scheme-handler/http=firefox.desktop\n"
                                  "\n"
                                  "\n"
                                  "[Added Associations]\n"
                                  "inode/directory=org.gnome.Nautilus.desktop;panefile.desktop;\n"
                                  "text/plain=nvim.desktop;\n"
                                  "\n"
                                  "[Removed Associations]\n"
                                  "image/png=gimp.desktop;\n";
        const QByteArray expected = "# Managed by stow — edit in ~/dotfiles\n"
                                    "\n"
                                    "[X-Unknown Group]\n"
                                    "whatever = this means\n"
                                    "\n"
                                    "[Default Applications]\n"
                                    "# folders\n"
                                    "inode/directory = panefile.desktop\n"
                                    "x-scheme-handler/http=firefox.desktop\n"
                                    "\n"
                                    "\n"
                                    "[Added Associations]\n"
                                    "inode/directory=panefile.desktop;org.gnome.Nautilus.desktop;\n"
                                    "text/plain=nvim.desktop;\n"
                                    "\n"
                                    "[Removed Associations]\n"
                                    "image/png=gimp.desktop;\n";
        QCOMPARE(xdg::withDefaultApplication(before, kMime, kPanefile), expected);
    }

    void crlfLineEndingsSurvive()
    {
        const QByteArray before = "[Default Applications]\r\n"
                                  "inode/directory=org.gnome.Nautilus.desktop\r\n"
                                  "text/plain=gvim.desktop\r\n";
        QCOMPARE(xdg::withDefaultApplication(before, kMime, kPanefile),
                 QByteArray("[Default Applications]\r\n"
                            "inode/directory=panefile.desktop\r\n"
                            "text/plain=gvim.desktop\r\n"
                            "\r\n"
                            "[Added Associations]\r\n"
                            "inode/directory=panefile.desktop;\r\n"));
    }

    void aFileWithoutAFinalNewlineKeepsItsLastLine()
    {
        const QByteArray before = "[Default Applications]\ntext/plain=gvim.desktop";
        const QByteArray after = xdg::withDefaultApplication(before, kMime, kPanefile);
        QVERIFY2(after.startsWith("[Default Applications]\ntext/plain=gvim.desktop\n"
                                  "inode/directory=panefile.desktop\n"),
                 after.constData());
    }

    void writingTwiceChangesNothingTheSecondTime()
    {
        installPanefile();
        const QByteArray before =
            "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n";
        const QByteArray once = xdg::withDefaultApplication(before, kMime, kPanefile);
        QCOMPARE(xdg::withDefaultApplication(once, kMime, kPanefile), once);

        QVERIFY(write(configFile("mimeapps.list"), before));
        QVERIFY(xdg::setDefaultApplication(m_env, kMime, kPanefile).changed);
        const xdg::WriteResult second = xdg::setDefaultApplication(m_env, kMime, kPanefile);
        QVERIFY(second.ok);
        QVERIFY(!second.changed);
        QCOMPARE(read(configFile("mimeapps.list")), once);
    }

    /// A trailing `;` on the default is the same list, not a reason to edit.
    void anEquivalentValueIsLeftAlone()
    {
        const QByteArray before =
            "[Default Applications]\ninode/directory=panefile.desktop;\n"
            "[Added Associations]\ninode/directory=panefile.desktop;b.desktop\n";
        QCOMPARE(xdg::withDefaultApplication(before, kMime, kPanefile), before);
    }

    /// The case this was written for: `~/.config/mimeapps.list` is a stow
    /// symlink into a dotfiles repository. `xdg-mime` replaces the link with a
    /// plain file; this must change the repository's file and leave the link.
    void aSymlinkStaysASymlink()
    {
        const QString target = m_dir->filePath(QStringLiteral("home/dotfiles/xdg/mimeapps.list"));
        QVERIFY(
            write(target, "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n"));
        QVERIFY(QDir().mkpath(m_env.configHome));
        const QString link = configFile("mimeapps.list");
        const QByteArray relative = "../dotfiles/xdg/mimeapps.list";
        QCOMPARE(::symlink(relative.constData(), QFile::encodeName(link).constData()), 0);

        const xdg::WriteResult result = xdg::setDefaultApplication(m_env, kMime, kPanefile);
        QVERIFY2(result.ok, qPrintable(result.error));

        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(readLink(link), relative);
        QVERIFY2(read(target).contains("inode/directory=panefile.desktop\n"),
                 read(target).constData());
        // And nothing was left behind beside the link.
        QCOMPARE(QDir(m_env.configHome).entryList(QDir::Files | QDir::System | QDir::Hidden),
                 QStringList({QStringLiteral("mimeapps.list")}));
    }

    void permissionsSurvive()
    {
        const QString path = configFile("mimeapps.list");
        QVERIFY(write(path, "[Default Applications]\n"));
        const QFileDevice::Permissions wanted = QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                QFileDevice::ReadGroup | QFileDevice::ReadOther;
        QVERIFY(QFile::setPermissions(path, wanted));

        QVERIFY(xdg::setDefaultApplication(m_env, kMime, kPanefile).ok);
        // Owner, group and other; Qt's "user" bits describe the current user
        // rather than the file's mode.
        const QFileDevice::Permissions mode(0x7077);
        QCOMPARE(QFile::permissions(path) & mode, wanted & mode);
    }

    /// The desktop-specific file is the one that counts, so it is the one
    /// edited — and mimeapps.list, which would lose to it, is left alone.
    void theFileThatSetsItIsTheOneEdited()
    {
        QVERIFY(
            write(configFile("mimeapps.list"), "[Default Applications]\ntext/plain=a.desktop\n"));
        QVERIFY(write(configFile("hyprland-mimeapps.list"),
                      "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n"));

        const xdg::WriteResult result = xdg::setDefaultApplication(m_env, kMime, kPanefile);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.path, configFile("hyprland-mimeapps.list"));
        QCOMPARE(read(configFile("mimeapps.list")),
                 QByteArray("[Default Applications]\ntext/plain=a.desktop\n"));
    }

    /// A higher-precedence file that cannot be written would beat whatever
    /// this wrote. Saying so beats pretending.
    void anUnwritableOverrideIsReported()
    {
        if (::geteuid() == 0) {
            QSKIP("root can write a read-only file");
        }
        const QString blocker = configFile("hyprland-mimeapps.list");
        QVERIFY(
            write(blocker, "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n"));
        QVERIFY(QFile::setPermissions(blocker, QFileDevice::ReadOwner));

        const xdg::WriteResult result = xdg::setDefaultApplication(m_env, kMime, kPanefile);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("hyprland-mimeapps.list overrides it and "
                                                      "isn't writable")),
                 qPrintable(result.error));
        QVERIFY(!QFile::exists(configFile("mimeapps.list")));
    }

    /// A system file that sets it loses to the user's own mimeapps.list, so
    /// that is where the default goes; the system file is never touched.
    void aSystemDefaultIsOverriddenFromTheUsersFile()
    {
        const QString system = m_dir->filePath(QStringLiteral("etc/xdg/mimeapps.list"));
        const QByteArray original =
            "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n";
        QVERIFY(write(system, original));

        const xdg::WriteResult result = xdg::setDefaultApplication(m_env, kMime, kPanefile);
        QVERIFY(result.ok);
        QCOMPARE(result.path, configFile("mimeapps.list"));
        QCOMPARE(read(system), original);
    }

    // ======================================================= service file

    void theServiceFileNamesAnAbsolutePf()
    {
        const xdg::WriteResult result =
            xdg::writeFileManagerServiceFile(m_env, QStringLiteral("/opt/panefile/bin/pf"));
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.path,
                 m_env.dataHome +
                     QStringLiteral("/dbus-1/services/org.freedesktop.FileManager1.service"));

        const QByteArray contents = read(result.path);
        QVERIFY2(contents.contains("[D-BUS Service]\n"), contents.constData());
        QVERIFY2(contents.contains("\nName=org.freedesktop.FileManager1\n"), contents.constData());
        QVERIFY2(contents.contains("\nExec=/opt/panefile/bin/pf --dbus-service\n"),
                 contents.constData());
        QCOMPARE(xdg::executableInServiceFile(contents), QStringLiteral("/opt/panefile/bin/pf"));

        // And writing it again is not a change.
        QVERIFY(!xdg::writeFileManagerServiceFile(m_env, QStringLiteral("/opt/panefile/bin/pf"))
                     .changed);
    }

    /// The bus's activation environment may not have ~/.local/bin on PATH, so
    /// a bare `pf` would start nothing.
    void aRelativeExecIsRefused()
    {
        const xdg::WriteResult result =
            xdg::writeFileManagerServiceFile(m_env, QStringLiteral("pf"));
        QVERIFY(!result.ok);
        QVERIFY(!QFile::exists(result.path));
    }

    void anAwkwardPathIsQuoted()
    {
        const QString path = QStringLiteral("/home/me/my builds/it's/pf");
        const QByteArray contents = xdg::fileManagerServiceFileContents(path);
        QVERIFY2(contents.contains("Exec='/home/me/my builds/it'\\''s/pf' --dbus-service"),
                 contents.constData());
        QCOMPARE(xdg::executableInServiceFile(contents), path);
    }

    void thePfToRunIsAbsolute()
    {
        QVERIFY(QDir::isAbsolutePath(platform::panefileExecutable()));
        QCOMPARE(platform::panefileExecutable(), m_fakePf);
    }

    // ========================================================= end to end

    /// Both halves, through the process environment the real code reads —
    /// which initTestCase pointed at the sandbox.
    void makingTheDefaultSetsBothHalves()
    {
        const QString dataHome = m_sandbox.path() + QStringLiteral("/data-home");
        const QString dataDir = m_sandbox.path() + QStringLiteral("/usr-share");
        const QString configHome = m_sandbox.path() + QStringLiteral("/config-home");
        QVERIFY(
            write(dataDir + QStringLiteral("/applications/") + kNautilus, desktopEntry("Files")));
        QVERIFY(write(configHome + QStringLiteral("/mimeapps.list"),
                      "[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n"));

        // Not installed: nothing can be pointed at it.
        platform::DefaultFileManagerStatus before = platform::queryDefaultFileManager();
        QVERIFY(!before.panefileInstalled);
        QCOMPARE(before.handlerDescription(), QStringLiteral("Files (org.gnome.Nautilus.desktop)"));
        const platform::MakeDefaultResult refused = platform::makePanefileDefaultFileManager();
        QVERIFY(!refused.ok);
        QVERIFY2(refused.error.contains(QStringLiteral("panefile.desktop")),
                 qPrintable(refused.error));

        QVERIFY(write(dataHome + QStringLiteral("/applications/panefile.desktop"),
                      desktopEntry("Panefile")));
        before = platform::queryDefaultFileManager();
        QVERIFY(before.panefileInstalled);
        QVERIFY(!before.isDefault());

        const platform::MakeDefaultResult made = platform::makePanefileDefaultFileManager();
        QVERIFY2(made.ok, qPrintable(made.error));

        const platform::DefaultFileManagerStatus after = platform::queryDefaultFileManager();
        QCOMPARE(after.handlerId, kPanefile);
        QCOMPARE(after.handlerSource, configHome + QStringLiteral("/mimeapps.list"));
        QVERIFY(after.serviceFileExists);
        QCOMPARE(after.serviceExecutable, m_fakePf);
        QVERIFY(after.isDefault());
        QCOMPARE(after.handlerDescription(), QStringLiteral("Panefile"));

        // A service file whose pf has gone is not "the default".
        QFile::remove(m_fakePf);
        QVERIFY(!platform::queryDefaultFileManager().isDefault());
        QVERIFY(write(m_fakePf, "#!/bin/sh\n"));
        QVERIFY(
            QFile::setPermissions(m_fakePf, QFile::permissions(m_fakePf) | QFileDevice::ExeOwner));
    }

    // ================================================================ URIs

    void urisBecomePaths()
    {
        QStringList rejected;
        const QStringList paths = platform::FileManagerService::localPathsFromUris(
            {QStringLiteral("file:///tmp/a%20b"), QStringLiteral("file:///home/me/Downloads/"),
             QStringLiteral("file://localhost/etc/hosts"),
             QStringLiteral("file:///tmp/%C3%A9t%C3%A9%25.txt"), QStringLiteral("file:///")},
            &rejected);

        QCOMPARE(paths,
                 QStringList({QStringLiteral("/tmp/a b"), QStringLiteral("/home/me/Downloads"),
                              QStringLiteral("/etc/hosts"), QStringLiteral("/tmp/été%.txt"),
                              QStringLiteral("/")}));
        QVERIFY(rejected.isEmpty());
    }

    void onlyLocalFileUrisAreAccepted()
    {
        const QStringList offered{
            QStringLiteral("https://example.com/x"), QStringLiteral("smb://server/share/x"),
            QStringLiteral("file://elsewhere/etc/hosts"), QStringLiteral("/etc/hosts"), QString()};
        QStringList rejected;
        QVERIFY(platform::FileManagerService::localPathsFromUris(offered, &rejected).isEmpty());
        QCOMPARE(rejected, offered);
    }

    // ===================================================== bar visibility

    void theRule_data()
    {
        QTest::addColumn<int>("flip");
        QTest::newRow("nothing rules it out") << -1;
        QTest::newRow("not Linux") << 0;
        QTest::newRow("opted out") << 1;
        QTest::newRow("already the default") << 2;
        QTest::newRow("no panefile.desktop") << 3;
        QTest::newRow("started by D-Bus") << 4;
        QTest::newRow("headless") << 5;
        QTest::newRow("not now") << 6;
    }

    /// Each condition on its own is enough to keep the bar away.
    void theRule()
    {
        QFETCH(int, flip);
        DefaultOfferConditions conditions{.supported = true,
                                          .optedIn = true,
                                          .alreadyDefault = false,
                                          .desktopEntryInstalled = true,
                                          .dbusActivated = false,
                                          .headless = false,
                                          .dismissedThisRun = false};
        bool *fields[] = {&conditions.supported,       &conditions.optedIn,
                          &conditions.alreadyDefault,  &conditions.desktopEntryInstalled,
                          &conditions.dbusActivated,   &conditions.headless,
                          &conditions.dismissedThisRun};
        if (flip >= 0) {
            *fields[flip] = !*fields[flip];
        }
        QCOMPARE(DefaultFileManagerOffer::shouldOffer(conditions), flip < 0);
    }

    void theBarAppearsWhenItShould()
    {
        ui::MainWindow window;
        DefaultFileManagerOffer offer(&window, nullptr);
        offer.setContext(true, false, false, false);
        offer.applyStatus(nautilusIsDefault());

        QVERIFY(offer.isOffering());
        QVERIFY(offer.bar() != nullptr);
        QCOMPARE(offer.bar()->text(), QStringLiteral("Make Panefile your default file manager?"));
    }

    void theBarStaysAwayWhenItShould_data()
    {
        QTest::addColumn<QString>("why");
        QTest::newRow("already the default") << "default";
        QTest::newRow("no panefile.desktop") << "uninstalled";
        QTest::newRow("not Linux") << "unsupported";
        QTest::newRow("opted out") << "optedOut";
        QTest::newRow("started by D-Bus") << "dbus";
        QTest::newRow("headless") << "headless";
    }

    void theBarStaysAwayWhenItShould()
    {
        QFETCH(QString, why);
        platform::DefaultFileManagerStatus status = nautilusIsDefault();
        if (why == QLatin1String("default")) {
            status.handlerId = kPanefile;
            status.serviceFileExists = true;
            status.serviceExecutableExists = true;
        } else if (why == QLatin1String("uninstalled")) {
            status.panefileInstalled = false;
        } else if (why == QLatin1String("unsupported")) {
            status = {};
        }

        ui::MainWindow window;
        DefaultFileManagerOffer offer(&window, nullptr);
        offer.setContext(why != QLatin1String("optedOut"), why == QLatin1String("dbus"),
                         why == QLatin1String("headless"), false);
        offer.applyStatus(status);

        QVERIFY(!offer.isOffering());
        // Nothing was even built for it.
        QVERIFY(offer.bar() == nullptr);
    }

    /// The test suite runs offscreen, and an offscreen run must never look —
    /// not at the files, and not at the bus.
    void anOffscreenRunDoesNothingAtAll()
    {
        QVERIFY(DefaultFileManagerOffer::isHeadless());

        ui::MainWindow window;
        DefaultFileManagerOffer offer(&window, nullptr);
        offer.start(true, false, true);
        QTest::qWait(50);
        QVERIFY(offer.bar() == nullptr);
    }

    void busActivationIsRecognised()
    {
        QVERIFY(DefaultFileManagerOffer::isDbusActivated(true));
        QVERIFY(!DefaultFileManagerOffer::isDbusActivated(false));
        qputenv("DBUS_STARTER_BUS_TYPE", "session");
        QVERIFY(DefaultFileManagerOffer::isDbusActivated(false));
        qunsetenv("DBUS_STARTER_BUS_TYPE");
    }

    /// "Never" writes the key — through the registry, as a keypress would —
    /// and leaves the rest of a hand-edited config.toml exactly as it was.
    void neverPersists()
    {
        const QString config = configToml();
        const QByteArray handEdited = "# my settings\n"
                                      "[general]\n"
                                      "restore_session = false   # start clean\n"
                                      "\n"
                                      "[panels]\n"
                                      "show_hidden = true\n";
        QVERIFY(write(config, handEdited));

        ui::MainWindow window;
        input::ActionRegistry registry;
        DefaultFileManagerOffer offer(&window, nullptr);
        offer.registerActions(&registry);
        offer.setContext(true, false, false, false);
        offer.applyStatus(nautilusIsDefault());
        QVERIFY(offer.isOffering());

        QVERIFY(registry.invoke(QLatin1String(DefaultFileManagerOffer::kNeverAction)));

        QVERIFY(!offer.isOffering());
        QCOMPARE(read(config), QByteArray("# my settings\n"
                                          "[general]\n"
                                          "restore_session = false   # start clean\n"
                                          "offer_default_file_manager = false\n"
                                          "\n"
                                          "[panels]\n"
                                          "show_hidden = true\n"));
        QCOMPARE(config::loadConfig(config).settings.general.offerDefaultFileManager, false);
        QFile::remove(config);
    }

    /// "Not now" is for this run: nothing is written, the bar does not come
    /// back in this run, and the next launch asks again.
    void notNowDoesNotPersist()
    {
        QFile::remove(configToml());

        ui::MainWindow window;
        input::ActionRegistry registry;
        {
            DefaultFileManagerOffer offer(&window, nullptr);
            offer.registerActions(&registry);
            offer.setContext(true, false, false, false);
            offer.applyStatus(nautilusIsDefault());
            QVERIFY(offer.isOffering());

            QVERIFY(registry.invoke(QLatin1String(DefaultFileManagerOffer::kNotNowAction)));
            QVERIFY(!offer.isOffering());
            QVERIFY(!QFile::exists(configToml()));

            offer.applyStatus(nautilusIsDefault());
            QVERIFY(!offer.isOffering());
        }

        DefaultFileManagerOffer nextLaunch(&window, nullptr);
        nextLaunch.setContext(true, false, false, false);
        nextLaunch.applyStatus(nautilusIsDefault());
        QVERIFY(nextLaunch.isOffering());
    }

    /// The keys do nothing unless the bar is asking.
    void theAnswersAreEnabledOnlyWhileAsking()
    {
        ui::MainWindow window;
        input::ActionRegistry registry;
        DefaultFileManagerOffer offer(&window, nullptr);
        offer.registerActions(&registry);

        const QString yes = QLatin1String(DefaultFileManagerOffer::kYesAction);
        QVERIFY(registry.contains(yes));
        QVERIFY(!registry.isEnabled(yes));

        offer.setContext(true, false, false, false);
        offer.applyStatus(nautilusIsDefault());
        QVERIFY(registry.isEnabled(yes));
        QVERIFY(registry.isEnabled(QLatin1String(DefaultFileManagerOffer::kNeverAction)));

        offer.notNow();
        QVERIFY(!registry.isEnabled(yes));
    }

    /// Turning the key off by hand, or in Settings, takes the bar down now.
    void optingOutLiveHidesTheBar()
    {
        ui::MainWindow window;
        DefaultFileManagerOffer offer(&window, nullptr);
        offer.setContext(true, false, false, false);
        offer.applyStatus(nautilusIsDefault());
        QVERIFY(offer.isOffering());

        offer.setOptedIn(false);
        QVERIFY(!offer.isOffering());
        offer.setOptedIn(true);
        QVERIFY(offer.isOffering());
    }

    void theButtonsShowTheirKeysAndNeverTakeFocus()
    {
        ui::MainWindow window;
        DefaultFileManagerOffer offer(&window, nullptr);
        offer.setKeyHints(QStringLiteral("Alt+Y"), QStringLiteral("Alt+N"), QString());
        offer.setContext(true, false, false, false);
        offer.applyStatus(nautilusIsDefault());

        const QList<QPushButton *> buttons = offer.bar()->findChildren<QPushButton *>();
        QCOMPARE(buttons.size(), 3);
        QStringList labels;
        for (const QPushButton *button : buttons) {
            QCOMPARE(button->focusPolicy(), Qt::NoFocus);
            labels << button->text();
        }
        QCOMPARE(labels, QStringList({QStringLiteral("Yes   Alt+Y"),
                                      QStringLiteral("Not now   Alt+N"), QStringLiteral("Never")}));
    }

private:
    QString configFile(const char *name) const
    {
        return m_env.configHome + QLatin1Char('/') + QLatin1String(name);
    }

    QString configToml() const
    {
        return m_sandbox.path() + QStringLiteral("/panefile/config.toml");
    }

    void installSystem(const QString &id, const QByteArray &name)
    {
        QVERIFY(write(m_dir->filePath(QStringLiteral("usr/share/applications/") + id),
                      desktopEntry(name)));
    }

    void installPanefile()
    {
        QVERIFY(write(m_env.dataHome + QStringLiteral("/applications/panefile.desktop"),
                      desktopEntry("Panefile")));
    }

    QTemporaryDir m_sandbox;
    QString m_fakePf;
    std::unique_ptr<QTemporaryDir> m_dir;
    xdg::Environment m_env;
};

QTEST_MAIN(TestDefaultFileManager)
#include "tst_defaultfilemanager.moc"
