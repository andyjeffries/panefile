// Becoming the default file manager on Linux: mimeapps.list for opening
// folders, and org.freedesktop.FileManager1 for "Show in folder".

#include "core/Logging.h"
#include "platform/DefaultFileManager.h"
#include "platform/FileManagerService.h"
#include "platform/linux/XdgMimeApps.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace pf::platform {
namespace {

const char *const kObjectPath = "/org/freedesktop/FileManager1";
const char *const kBusService = "org.freedesktop.DBus";
const char *const kBusPath = "/org/freedesktop/DBus";
const char *const kBusInterface = "org.freedesktop.DBus";

/// RequestName's flags and replies, from the D-Bus specification.
constexpr uint kAllowReplacement = 0x1;
constexpr uint kPrimaryOwner = 1;
constexpr uint kInQueue = 2;
constexpr uint kAlreadyOwner = 4;

/// The short name of a process, from /proc. Empty if it has gone.
QString processName(uint pid)
{
    QFile file(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLocal8Bit(file.readAll()).trimmed();
}

QDBusMessage busCall(const char *method)
{
    return QDBusMessage::createMethodCall(QLatin1String(kBusService), QLatin1String(kBusPath),
                                          QLatin1String(kBusInterface), QLatin1String(method));
}

} // namespace

bool defaultFileManagerSupported()
{
    return true;
}

QString panefileExecutable()
{
    // `pf` as the user runs it, which survives a rebuild or an upgrade in a way
    // the path of this particular binary may not. It is also what a packaged
    // install puts on PATH.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("pf"));
    return onPath.isEmpty() ? QCoreApplication::applicationFilePath() : onPath;
}

DefaultFileManagerStatus queryDefaultFileManager()
{
    const xdg::Environment env = xdg::Environment::fromProcess();

    DefaultFileManagerStatus status;
    status.supported = true;

    const xdg::Resolution resolution =
        xdg::resolveDefault(env, QLatin1String(xdg::kDirectoryMimeType));
    status.handlerId = resolution.desktopId;
    status.handlerSource = resolution.sourceFile;
    if (!resolution.desktopId.isEmpty()) {
        status.handlerName = xdg::desktopEntryName(xdg::desktopFilePath(env, resolution.desktopId));
    }

    status.panefileInstalled =
        !xdg::desktopFilePath(env, QLatin1String(kPanefileDesktopId)).isEmpty();

    status.serviceFile = xdg::fileManagerServiceFilePath(env);
    QFile service(status.serviceFile);
    if (service.open(QIODevice::ReadOnly)) {
        status.serviceFileExists = true;
        status.serviceExecutable = xdg::executableInServiceFile(service.readAll());

        // A pf that is still there. A file pointing at a build tree that has
        // since been deleted activates nothing, and saying Panefile is the
        // default would stop the bar offering to fix it.
        const QFileInfo executable(status.serviceExecutable);
        status.serviceExecutableExists = executable.isFile() && executable.isExecutable() &&
                                         executable.fileName() == QLatin1String("pf");
    }

    return status;
}

MakeDefaultResult makePanefileDefaultFileManager()
{
    const xdg::Environment env = xdg::Environment::fromProcess();
    MakeDefaultResult result;

    if (xdg::desktopFilePath(env, QLatin1String(kPanefileDesktopId)).isEmpty()) {
        result.error =
            QStringLiteral("panefile.desktop isn't installed. Install the package, or run "
                           "scripts/install-cli.sh from a build.");
        return result;
    }

    const xdg::WriteResult mimeapps = xdg::setDefaultApplication(
        env, QLatin1String(xdg::kDirectoryMimeType), QLatin1String(kPanefileDesktopId));
    result.mimeappsFile = mimeapps.path;
    if (!mimeapps.ok) {
        result.error = mimeapps.error;
        return result;
    }

    const xdg::WriteResult service = xdg::writeFileManagerServiceFile(env, panefileExecutable());
    result.serviceFile = service.path;
    if (!service.ok) {
        result.error = service.error;
        return result;
    }

    // dbus-daemon notices a new service file by itself; dbus-broker, Arch's
    // default, only on a reload. Without this, "Show in folder" would not
    // start Panefile until the next login. Failure changes nothing that was
    // written, so it is logged rather than reported.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        const QDBusMessage reply = bus.call(busCall("ReloadConfig"), QDBus::Block, 2000);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCDebug(pfIpc) << "the session bus did not reload its configuration:"
                           << reply.errorMessage();
        }
    }

    result.ok = true;
    return result;
}

QString fileManagerNameOwner()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || bus.interface() == nullptr) {
        return {};
    }
    const QString name = QLatin1String(xdg::kFileManagerBusName);
    const QDBusReply<QString> owner = bus.interface()->serviceOwner(name);
    if (!owner.isValid() || owner.value().isEmpty()) {
        return {};
    }
    const QDBusReply<uint> pid = bus.interface()->servicePid(name);
    if (!pid.isValid()) {
        return owner.value();
    }
    const QString process = processName(pid.value());
    return QStringLiteral("%1 (pid %2, %3)")
        .arg(process.isEmpty() ? QStringLiteral("unknown") : process)
        .arg(pid.value())
        .arg(owner.value());
}

class DBusFileManagerService;

/// The interface itself. QtDBus exports an adaptor's slots as its methods, and
/// derives the signatures from the parameter types: `as` and `s`.
class FileManagerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManagerAdaptor(DBusFileManagerService *service);

public Q_SLOTS:
    // NOLINTBEGIN(readability-identifier-naming): the D-Bus method names.
    void ShowFolders(const QStringList &uris, const QString &startupId);
    void ShowItems(const QStringList &uris, const QString &startupId);

    /// Panefile has no properties view, so this shows the item, which is the
    /// part of the request it can honour.
    void ShowItemProperties(const QStringList &uris, const QString &startupId);
    // NOLINTEND(readability-identifier-naming)

private:
    DBusFileManagerService *m_service;
};

class DBusFileManagerService : public FileManagerService
{
    Q_OBJECT

public:
    explicit DBusFileManagerService(QObject *parent) : FileManagerService(parent) {}

    bool isAvailable() const override { return true; }

    bool ownsName() const override { return m_owns; }

    void requestName() override
    {
        if (m_owns || m_requesting) {
            return;
        }

        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) {
            Q_EMIT nameUnavailable(QStringLiteral("there is no session bus"));
            return;
        }

        if (!m_exported) {
            new FileManagerAdaptor(this);
            if (!bus.registerObject(QLatin1String(kObjectPath), this,
                                    QDBusConnection::ExportAdaptors)) {
                Q_EMIT nameUnavailable(QStringLiteral("%1 is already exported in this process")
                                           .arg(QLatin1String(kObjectPath)));
                return;
            }
            // Both arrive unicast, for names this connection gains or loses —
            // including when a queued request is finally granted because the
            // previous owner exited.
            bus.connect(QLatin1String(kBusService), QLatin1String(kBusPath),
                        QLatin1String(kBusInterface), QStringLiteral("NameAcquired"), this,
                        SLOT(onNameAcquired(QString)));
            bus.connect(QLatin1String(kBusService), QLatin1String(kBusPath),
                        QLatin1String(kBusInterface), QStringLiteral("NameLost"), this,
                        SLOT(onNameLost(QString)));
            m_exported = true;
        }

        // Queued, never replacing: if Nautilus owns the name, Panefile waits
        // its turn rather than taking it. Replacement is allowed the other way
        // round, so switching back to another file manager is never a fight
        // either.
        QDBusMessage call = busCall("RequestName");
        call << QString::fromLatin1(xdg::kFileManagerBusName) << kAllowReplacement;

        m_requesting = true;
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher *finished) {
                    finished->deleteLater();
                    m_requesting = false;

                    const QDBusPendingReply<uint> reply = *finished;
                    if (reply.isError()) {
                        Q_EMIT nameUnavailable(reply.error().message());
                        return;
                    }
                    switch (reply.value()) {
                    case kPrimaryOwner:
                    case kAlreadyOwner:
                        acquired();
                        break;
                    case kInQueue:
                        reportOwner();
                        break;
                    default:
                        Q_EMIT nameUnavailable(QStringLiteral("the bus refused the name"));
                        break;
                    }
                });
    }

    void handle(const QStringList &uris, bool selectItems, const QString &startupId)
    {
        QStringList rejected;
        const QStringList paths = localPathsFromUris(uris, &rejected);
        for (const QString &uri : std::as_const(rejected)) {
            qCWarning(pfIpc) << "FileManager1: ignoring" << uri << "— only file:// URIs are shown";
        }
        if (!paths.isEmpty()) {
            Q_EMIT showRequested(paths, selectItems, startupId);
        }
    }

private Q_SLOTS:
    void onNameAcquired(const QString &name)
    {
        if (name == QLatin1String(xdg::kFileManagerBusName)) {
            acquired();
        }
    }

    void onNameLost(const QString &name)
    {
        if (name == QLatin1String(xdg::kFileManagerBusName) && m_owns) {
            m_owns = false;
            qCInfo(pfIpc) << "another file manager took" << name;
        }
    }

private:
    void acquired()
    {
        if (m_owns) {
            return;
        }
        m_owns = true;
        qCDebug(pfIpc) << "now own" << xdg::kFileManagerBusName;
        Q_EMIT nameAcquired();
    }

    /// Asks who is ahead of us, so the user can be told what to quit.
    void reportOwner()
    {
        QDBusMessage call = busCall("GetConnectionUnixProcessID");
        call << QString::fromLatin1(xdg::kFileManagerBusName);

        auto *watcher =
            new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher *finished) {
                    finished->deleteLater();
                    const QDBusPendingReply<uint> reply = *finished;
                    QString owner = reply.isError() ? QString() : processName(reply.value());
                    if (owner.isEmpty()) {
                        owner = QStringLiteral("another file manager");
                    }
                    qCInfo(pfIpc) << xdg::kFileManagerBusName << "is owned by" << owner
                                  << "— queued behind it";
                    Q_EMIT nameQueued(owner);
                });
    }

    bool m_exported = false;
    bool m_requesting = false;
    bool m_owns = false;
};

FileManagerAdaptor::FileManagerAdaptor(DBusFileManagerService *service)
    : QDBusAbstractAdaptor(service), m_service(service)
{}

void FileManagerAdaptor::ShowFolders(const QStringList &uris, const QString &startupId)
{
    m_service->handle(uris, false, startupId);
}

void FileManagerAdaptor::ShowItems(const QStringList &uris, const QString &startupId)
{
    m_service->handle(uris, true, startupId);
}

void FileManagerAdaptor::ShowItemProperties(const QStringList &uris, const QString &startupId)
{
    m_service->handle(uris, true, startupId);
}

std::unique_ptr<FileManagerService> FileManagerService::create(QObject *parent)
{
    return std::make_unique<DBusFileManagerService>(parent);
}

} // namespace pf::platform

#include "DefaultFileManager_linux.moc"
