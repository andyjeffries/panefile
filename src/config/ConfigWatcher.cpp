#include "config/ConfigWatcher.h"

#include "core/Logging.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>

#include <array>

namespace pf::config {
namespace {

/// §8's four files.
constexpr std::array<QLatin1String, 4> kConfigFiles{
    QLatin1String("config.toml"), QLatin1String("hotkeys.toml"), QLatin1String("theme.toml"),
    QLatin1String("themes")};

/// Long enough to coalesce the several events one save produces, short enough
/// that a user editing a theme sees the result while still looking at it.
constexpr int kDefaultDebounceMs = 150;

} // namespace

ConfigWatcher::ConfigWatcher(QObject *parent)
    : QObject(parent), m_watcher(new QFileSystemWatcher(this))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDefaultDebounceMs);

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ConfigWatcher::onPathChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &ConfigWatcher::onPathChanged);
    connect(&m_debounce, &QTimer::timeout, this, &ConfigWatcher::flush);
}

ConfigWatcher::~ConfigWatcher() = default;

void ConfigWatcher::setDebounceInterval(int milliseconds)
{
    m_debounce.setInterval(std::max(0, milliseconds));
}

void ConfigWatcher::watchConfigDirectory(const QString &directory)
{
    m_directory = directory;

    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }

    if (!QFileInfo::exists(directory)) {
        qCDebug(pfConfig) << "no config directory to watch at" << directory;
        return;
    }

    // The directory is watched as well as the files. Two reasons: a file that
    // does not exist yet cannot be watched directly, and an editor that saves
    // by rename replaces the inode the file watch was holding.
    m_watcher->addPath(directory);

    for (const QLatin1String &name : kConfigFiles) {
        const QString path = directory + QLatin1Char('/') + name;
        if (QFileInfo::exists(path)) {
            m_watcher->addPath(path);
        }
    }
}

QStringList ConfigWatcher::watchedFiles() const
{
    return m_watcher->files();
}

void ConfigWatcher::onPathChanged(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (!m_pending.contains(name)) {
        m_pending.append(name);
    }

    // Re-adding covers the save-by-rename case: the watch was on an inode that
    // no longer has this name, so without this the *second* save of a file
    // would go unnoticed.
    if (QFileInfo::exists(path) && !m_watcher->files().contains(path) &&
        !m_watcher->directories().contains(path)) {
        m_watcher->addPath(path);
    }

    // A directory event means a file may have appeared, so pick up any of the
    // four that have started existing.
    if (path == m_directory) {
        for (const QLatin1String &fileName : kConfigFiles) {
            const QString filePath = m_directory + QLatin1Char('/') + fileName;
            if (QFileInfo::exists(filePath) && !m_watcher->files().contains(filePath)) {
                m_watcher->addPath(filePath);
                if (!m_pending.contains(QString(fileName))) {
                    m_pending.append(QString(fileName));
                }
            }
        }
    }

    m_debounce.start();
}

void ConfigWatcher::flush()
{
    if (m_pending.isEmpty()) {
        return;
    }
    const QStringList changed = m_pending;
    m_pending.clear();

    qCDebug(pfConfig) << "config changed:" << changed;
    Q_EMIT configChanged(changed);
}

} // namespace pf::config
