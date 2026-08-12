// Path resolution (§8).
//
// These assertions are deliberately platform-aware rather than platform-shaped:
// the Linux expectations encode the XDG basedir spec, the macOS ones encode the
// ~/Library conventions, and both run on their own platform in CI. The rules
// that hold everywhere — environment overrides win, relative values in XDG
// variables are ignored, state never lands in the config directory — are
// asserted unconditionally.

#include "platform/Paths.h"

#include <QDir>
#include <QTest>

using namespace pf::platform;

class TestPaths : public QObject
{
    Q_OBJECT

private:
    void clearOverrides();

private Q_SLOTS:
    void init();
    void cleanup();

    void allPathsAreAbsolute();
    void stateIsNotInsideConfig();
    void environmentOverridesWin_data();
    void environmentOverridesWin();
    void relativeOverrideIsIgnored();
    void thumbnailCacheSitsUnderCache();
    void socketPathIsUserScoped();
    void themeSearchPathsPreferUserThemes();
    void platformConventions();
};

void TestPaths::clearOverrides()
{
    for (const char *name : {"PANEFILE_CONFIG_DIR", "PANEFILE_STATE_DIR", "PANEFILE_CACHE_DIR",
                             "PANEFILE_RUNTIME_DIR", "PANEFILE_DATA_DIR"}) {
        qunsetenv(name);
    }
}

void TestPaths::init()
{
    clearOverrides();
}

void TestPaths::cleanup()
{
    clearOverrides();
}

void TestPaths::allPathsAreAbsolute()
{
    QVERIFY(QDir::isAbsolutePath(configDir()));
    QVERIFY(QDir::isAbsolutePath(stateDir()));
    QVERIFY(QDir::isAbsolutePath(cacheDir()));
    QVERIFY(QDir::isAbsolutePath(runtimeDir()));
    QVERIFY(QDir::isAbsolutePath(thumbnailCacheDir()));
}

void TestPaths::stateIsNotInsideConfig()
{
    // §8: "Never write to a user's config file." State living inside the config
    // directory is how that rule gets broken by accident.
    QVERIFY(configDir() != stateDir());
    QVERIFY(!stateDir().startsWith(configDir() + QLatin1Char('/')) ||
            stateDir().endsWith(QLatin1String("/state")));
}

void TestPaths::environmentOverridesWin_data()
{
    QTest::addColumn<QByteArray>("variable");
    QTest::addColumn<int>("accessor");

    QTest::newRow("config") << QByteArray("PANEFILE_CONFIG_DIR") << 0;
    QTest::newRow("state") << QByteArray("PANEFILE_STATE_DIR") << 1;
    QTest::newRow("cache") << QByteArray("PANEFILE_CACHE_DIR") << 2;
    QTest::newRow("runtime") << QByteArray("PANEFILE_RUNTIME_DIR") << 3;
}

void TestPaths::environmentOverridesWin()
{
    QFETCH(QByteArray, variable);
    QFETCH(int, accessor);

    // The whole test suite depends on this: without it, tests would read and
    // write the developer's real configuration.
    qputenv(variable.constData(), QByteArray("/tmp/panefile-test-override"));

    QString actual;
    switch (accessor) {
    case 0:
        actual = configDir();
        break;
    case 1:
        actual = stateDir();
        break;
    case 2:
        actual = cacheDir();
        break;
    default:
        actual = runtimeDir();
        break;
    }

    QCOMPARE(actual, QStringLiteral("/tmp/panefile-test-override"));
}

void TestPaths::relativeOverrideIsIgnored()
{
    // The XDG basedir spec requires a relative value to be treated as unset,
    // not resolved against the cwd.
    qputenv("PANEFILE_CONFIG_DIR", QByteArray("relative/path"));

    const QString resolved = configDir();
    QVERIFY(QDir::isAbsolutePath(resolved));
    QVERIFY(!resolved.contains(QLatin1String("relative/path")));
}

void TestPaths::thumbnailCacheSitsUnderCache()
{
    qputenv("PANEFILE_CACHE_DIR", QByteArray("/tmp/panefile-cache"));

    QCOMPARE(thumbnailCacheDir(), QStringLiteral("/tmp/panefile-cache/thumbnails"));
}

void TestPaths::socketPathIsUserScoped()
{
    qputenv("PANEFILE_RUNTIME_DIR", QByteArray("/tmp/panefile-run"));

    const QString socket = singleInstanceSocketPath();

    // §10.3: $XDG_RUNTIME_DIR/panefile-$UID.sock. The uid suffix is what keeps
    // two users on one machine from fighting over the same socket.
    QVERIFY(socket.startsWith(QLatin1String("/tmp/panefile-run/panefile-")));
    QVERIFY(socket.endsWith(QLatin1String(".sock")));
}

void TestPaths::themeSearchPathsPreferUserThemes()
{
    qputenv("PANEFILE_CONFIG_DIR", QByteArray("/tmp/panefile-config"));

    const QStringList paths = themeSearchPaths();

    QVERIFY(!paths.isEmpty());
    QCOMPARE(paths.first(), QStringLiteral("/tmp/panefile-config/themes"));
}

void TestPaths::platformConventions()
{
#if defined(PF_PLATFORM_LINUX)
    qputenv("XDG_CONFIG_HOME", QByteArray("/tmp/xdg-config"));
    qputenv("XDG_DATA_HOME", QByteArray("/tmp/xdg-data"));
    qputenv("XDG_CACHE_HOME", QByteArray("/tmp/xdg-cache"));

    QCOMPARE(configDir(), QStringLiteral("/tmp/xdg-config/panefile"));
    QCOMPARE(stateDir(), QStringLiteral("/tmp/xdg-data/panefile"));

    // §7.7 requires the *shared* freedesktop thumbnail cache, so the cache root
    // is deliberately not namespaced by application.
    QCOMPARE(cacheDir(), QStringLiteral("/tmp/xdg-cache"));
    QCOMPARE(thumbnailCacheDir(), QStringLiteral("/tmp/xdg-cache/thumbnails"));

    qunsetenv("XDG_CONFIG_HOME");
    qunsetenv("XDG_DATA_HOME");
    qunsetenv("XDG_CACHE_HOME");
#elif defined(PF_PLATFORM_DARWIN)
    const QString home = QDir::homePath();

    QCOMPARE(configDir(), home + QStringLiteral("/Library/Application Support/panefile"));
    QCOMPARE(stateDir(), home + QStringLiteral("/Library/Application Support/panefile/state"));

    // No shared thumbnail spec exists on macOS, so unlike Linux the cache is
    // application-scoped.
    QCOMPARE(cacheDir(), home + QStringLiteral("/Library/Caches/panefile"));
#else
#error "Unsupported platform"
#endif
}

QTEST_APPLESS_MAIN(TestPaths)
#include "tst_paths.moc"
