// Platform-independent parts of the path resolution declared in Paths.h.
// The platform-specific roots live in linux/Paths_linux.cpp and
// darwin/Paths_darwin.cpp.

#include "platform/Paths.h"
#include "platform/PathsInternal.h"

#include "core/Version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <unistd.h>

namespace pf::platform {

QString envOverride(const char *name)
{
    const QByteArray raw = qgetenv(name);
    if (raw.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QString::fromLocal8Bit(raw));
}

QString envDir(const char *name)
{
    QString value = envOverride(name);
    // The XDG spec says a relative value must be ignored, not resolved.
    if (value.isEmpty() || !QDir::isAbsolutePath(value)) {
        return {};
    }
    return value;
}

QString homeDir()
{
    return QDir::homePath();
}

QString thumbnailCacheDir()
{
    return cacheDir() + QStringLiteral("/thumbnails");
}

QString singleInstanceSocketPath()
{
    return QStringLiteral("%1/panefile-%2.sock").arg(runtimeDir()).arg(::getuid());
}

QStringList dataSearchPaths()
{
    QStringList paths;

    const QString override = envDir("PANEFILE_DATA_DIR");
    if (!override.isEmpty()) {
        paths << override;
    }

    // Relative to the binary. Covers three layouts with one rule each:
    // a build tree (bin/../../data), a Unix install (bin/../share/panefile),
    // and a macOS bundle (MacOS/../Resources).
    const QString binDir = QCoreApplication::applicationDirPath();
    if (!binDir.isEmpty()) {
        for (const char *relative :
             {"/../share/" PF_APPLICATION_NAME, "/../Resources", "/../../data"}) {
            const QString candidate = QDir::cleanPath(binDir + QLatin1String(relative));
            if (QFileInfo::exists(candidate)) {
                paths << candidate;
            }
        }
    }

    const QString installed = QStringLiteral(PF_INSTALL_DATADIR);
    if (!installed.isEmpty() && !paths.contains(installed)) {
        paths << installed;
    }

    paths.removeDuplicates();
    return paths;
}

QStringList themeSearchPaths()
{
    QStringList paths;
    paths << configDir() + QStringLiteral("/themes");
    const QStringList dataPaths = dataSearchPaths();
    for (const QString &base : dataPaths) {
        paths << base + QStringLiteral("/themes");
    }
    paths.removeDuplicates();
    return paths;
}

} // namespace pf::platform
