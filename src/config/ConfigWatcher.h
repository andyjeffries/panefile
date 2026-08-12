#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class QFileSystemWatcher;

namespace pf::config {

/// Watches the four config files and reports changes (§8.3).
///
/// "Watch all four config files and hot-reload on change — including
/// regenerating the stylesheet — without restarting."
///
/// QFileSystemWatcher is used here, unlike the directory watcher of §7.3 which
/// wraps inotify directly. The reasons §7.3 gives for avoiding it — no control
/// over batching, no targeted updates, no coalescing — are about watching
/// directories with a hundred thousand entries. Four files that change when a
/// human saves them is the case QFileSystemWatcher is actually good at, and it
/// is already cross-platform, which the inotify wrapper is not.
///
/// One thing still has to be handled: editors do not write files, they write a
/// temporary file and rename it over the target. That drops the watch, so the
/// path is re-added after every change, and the notification is debounced
/// because a single save can produce several events.
class ConfigWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ConfigWatcher(QObject *parent = nullptr);
    ~ConfigWatcher() override;

    /// Starts watching the config directory's four files. Paths that do not
    /// exist yet are still watched for creation, since writing a config file
    /// for the first time should take effect like any other change.
    void watchConfigDirectory(const QString &directory);

    QStringList watchedFiles() const;

    void setDebounceInterval(int milliseconds);

Q_SIGNALS:
    /// One or more of the watched files changed. Carries the file names that
    /// changed, so a caller can reload only what it needs — regenerating the
    /// stylesheet for a theme change but not for a hotkeys change.
    void configChanged(const QStringList &fileNames);

private:
    void onPathChanged(const QString &path);
    void flush();

    QFileSystemWatcher *m_watcher = nullptr;
    QString m_directory;
    QStringList m_pending;
    QTimer m_debounce;
};

} // namespace pf::config
