#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace pf::platform {

/// org.freedesktop.FileManager1 — the D-Bus interface behind "Show in folder".
///
/// Browsers use it to reveal a download, Electron applications to reveal a
/// file, and xdg-desktop-portal to answer the OpenURI portal's
/// OpenDirectory. Owning `inode/directory` in `mimeapps.list` does not reach
/// any of them: they ask whoever owns this bus name, and on most desktops that
/// is Nautilus.
///
/// The same rules as VolumeMonitor: nothing touches the bus until
/// requestName() is called, which the composition root does after the first
/// paint and only when Panefile is the default; and nothing blocks the GUI
/// thread waiting for the bus.
class FileManagerService : public QObject
{
    Q_OBJECT

public:
    /// The implementation for this platform. Never null; on macOS it is never
    /// available and does nothing.
    static std::unique_ptr<FileManagerService> create(QObject *parent = nullptr);

    explicit FileManagerService(QObject *parent = nullptr) : QObject(parent) {}
    ~FileManagerService() override = default;

    virtual bool isAvailable() const = 0;

    /// Exports the object and asks for the name, queued behind any current
    /// owner rather than taking it from them. Asynchronous: reports through
    /// nameAcquired(), nameQueued() or nameUnavailable(). Idempotent.
    virtual void requestName() = 0;

    virtual bool ownsName() const = 0;

    /// Turns `file://` URIs into local paths. Anything else — another scheme,
    /// another host, something that is not a URI — goes into `rejected`,
    /// because a file manager cannot show it and guessing would be worse.
    static QStringList localPathsFromUris(const QStringList &uris, QStringList *rejected = nullptr);

Q_SIGNALS:
    /// A call arrived. `selectItems` is false for ShowFolders, which opens the
    /// folders themselves, and true for ShowItems and ShowItemProperties, which
    /// open each item's folder with the item selected. `startupId` is the
    /// caller's activation token, when it sent one.
    void showRequested(const QStringList &paths, bool selectItems, const QString &startupId);

    /// The name is ours; calls will reach this process.
    void nameAcquired();

    /// Someone else owns the name and we are queued behind them. We get it
    /// when they exit. `owner` names the process, for telling the user.
    void nameQueued(const QString &owner);

    /// The bus refused, or there is no session bus.
    void nameUnavailable(const QString &reason);
};

} // namespace pf::platform
