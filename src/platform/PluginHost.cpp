#include "platform/PluginHost.h"

#include "core/Logging.h"
#include "platform/Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>
#include <QThread>

namespace pf::platform {
namespace {

QObject *load(Plugin which)
{
    const QString base = pluginBaseName(which);

    // CMake names a MODULE library .so on macOS as well as Linux; .dylib is
    // accepted too in case a packager's toolchain says otherwise.
    const QStringList dirs = pluginSearchPaths();
    for (const QString &dir : dirs) {
        for (const char *suffix : {".so", ".dylib"}) {
            const QString file = QDir(dir).filePath(base + QLatin1String(suffix));
            if (!QFileInfo::exists(file)) {
                continue;
            }

            // Never deleted: a plugin is never unloaded. Its code backs
            // widgets and highlighters whose lifetimes the host does not track,
            // and unloading under them would be a crash for no saving.
            auto *loader = new QPluginLoader(file);
            QObject *instance = loader->instance();
            if (instance == nullptr) {
                qCWarning(pfPlugins).noquote()
                    << "could not load" << file << "-" << loader->errorString();
                delete loader;
                continue;
            }

            // Owned by the instance, which lives for the rest of the process,
            // so the loader stays reachable instead of reading to LeakSanitizer
            // as a leak.
            loader->setParent(instance);

            // The root object is created on whichever thread asked first, and
            // for pf-video-thumb that is a pool thread that may later exit.
            // Moving it to the application thread keeps its affinity valid.
            // moveToThread() must be called from the object's own thread,
            // which this is.
            if (const QCoreApplication *app = QCoreApplication::instance();
                app != nullptr && instance->thread() != app->thread()) {
                instance->moveToThread(app->thread());
            }

            qCDebug(pfPlugins).noquote() << "loaded" << file;
            return instance;
        }
    }

    qCDebug(pfPlugins).noquote() << base << "is not installed; searched" << dirs;
    return nullptr;
}

} // namespace

QString pluginBaseName(Plugin which)
{
    switch (which) {
    case Plugin::Syntax:
        return QStringLiteral("pf-syntax");
    case Plugin::Media:
        return QStringLiteral("pf-media");
    case Plugin::Pdf:
        return QStringLiteral("pf-pdf");
    case Plugin::VideoThumbnails:
        return QStringLiteral("pf-video-thumb");
    }
    return {};
}

QObject *pluginInstance(Plugin which)
{
    // One function-local static per plugin: construction is on first use
    // (§3.4 forbids registry objects at namespace scope) and C++ guarantees it
    // happens once even when two threads race to it.
    switch (which) {
    case Plugin::Syntax: {
        static QObject *const instance = load(Plugin::Syntax);
        return instance;
    }
    case Plugin::Media: {
        static QObject *const instance = load(Plugin::Media);
        return instance;
    }
    case Plugin::Pdf: {
        static QObject *const instance = load(Plugin::Pdf);
        return instance;
    }
    case Plugin::VideoThumbnails: {
        static QObject *const instance = load(Plugin::VideoThumbnails);
        return instance;
    }
    }
    return nullptr;
}

} // namespace pf::platform
