#include "platform/linux/FileChooserPortal_linux.h"

#include "core/Logging.h"
#include "platform/DefaultFileManager.h"
#include "platform/linux/XdgPortals.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace pf::platform {
namespace {

const char *const kPortalObjectPath = "/org/freedesktop/portal/desktop";

QByteArray withoutNul(QByteArray bytes)
{
    const qsizetype nul = bytes.indexOf('\0');
    return nul < 0 ? bytes : bytes.left(nul);
}

/// A path from an `ay`. The bytes are a file name as the application has it,
/// which need not be valid UTF-8.
QString pathFromBytes(const QByteArray &bytes)
{
    return QFile::decodeName(withoutNul(bytes));
}

QString pathOption(const QVariantMap &options, const QString &key)
{
    return pathFromBytes(options.value(key).toByteArray());
}

QList<QByteArray> readByteArrays(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QByteArrayList>()) {
        return value.value<QByteArrayList>();
    }
    QList<QByteArray> list;
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        QDBusArgument arg = value.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            QByteArray item;
            arg >> item;
            list.append(item);
        }
        arg.endArray();
    }
    return list;
}

PortalFilter toPortal(const FileFilter &filter)
{
    PortalFilter result{.name = filter.name, .patterns = {}};
    for (const auto &[kind, text] : filter.patterns) {
        result.patterns.append({.kind = kind, .text = text});
    }
    return result;
}

FileFilter fromPortal(const PortalFilter &filter)
{
    FileFilter result{.name = filter.name, .patterns = {}};
    for (const PortalPattern &pattern : filter.patterns) {
        result.patterns.append({pattern.kind, pattern.text});
    }
    return result;
}

} // namespace

void registerPortalTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalPattern>();
        qDBusRegisterMetaType<QList<PortalPattern>>();
        qDBusRegisterMetaType<PortalFilter>();
        qDBusRegisterMetaType<QList<PortalFilter>>();
        qDBusRegisterMetaType<PortalPair>();
        qDBusRegisterMetaType<QList<PortalPair>>();
        qDBusRegisterMetaType<PortalChoice>();
        qDBusRegisterMetaType<QList<PortalChoice>>();
        return true;
    }();
    Q_UNUSED(registered)
}

QDBusArgument &operator<<(QDBusArgument &arg, const PortalPattern &pattern)
{
    arg.beginStructure();
    arg << pattern.kind << pattern.text;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalPattern &pattern)
{
    arg.beginStructure();
    arg >> pattern.kind >> pattern.text;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const PortalFilter &filter)
{
    arg.beginStructure();
    arg << filter.name << filter.patterns;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalFilter &filter)
{
    arg.beginStructure();
    arg >> filter.name >> filter.patterns;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const PortalPair &pair)
{
    arg.beginStructure();
    arg << pair.first << pair.second;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalPair &pair)
{
    arg.beginStructure();
    arg >> pair.first >> pair.second;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const PortalChoice &choice)
{
    arg.beginStructure();
    arg << choice.id << choice.label << choice.options << choice.initial;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalChoice &choice)
{
    arg.beginStructure();
    arg >> choice.id >> choice.label >> choice.options >> choice.initial;
    arg.endStructure();
    return arg;
}

FileChooserRequest requestFromDBus(FileChooserRequest::Mode mode, const QString &appId,
                                   const QString &title, const QVariantMap &options)
{
    registerPortalTypes();
    FileChooserRequest request;
    request.mode = mode;
    request.appId = appId;
    request.title = title;
    request.acceptLabel = options.value(QStringLiteral("accept_label")).toString();
    request.multiple = options.value(QStringLiteral("multiple")).toBool();
    request.directory = options.value(QStringLiteral("directory")).toBool();
    request.currentName = options.value(QStringLiteral("current_name")).toString();
    request.currentFolder = pathOption(options, QStringLiteral("current_folder"));
    request.currentFile = pathOption(options, QStringLiteral("current_file"));

    for (const QByteArray &name : readByteArrays(options.value(QStringLiteral("files")))) {
        request.files << pathFromBytes(name);
    }

    for (const PortalFilter &filter :
         qdbus_cast<QList<PortalFilter>>(options.value(QStringLiteral("filters")))) {
        request.filters.append(fromPortal(filter));
    }

    if (options.contains(QStringLiteral("current_filter"))) {
        const FileFilter current =
            fromPortal(qdbus_cast<PortalFilter>(options.value(QStringLiteral("current_filter"))));
        request.currentFilter = static_cast<int>(request.filters.indexOf(current));
        if (request.currentFilter < 0) {
            request.filters.append(current);
            request.currentFilter = static_cast<int>(request.filters.size()) - 1;
        }
    }

    for (const PortalChoice &item :
         qdbus_cast<QList<PortalChoice>>(options.value(QStringLiteral("choices")))) {
        FileChoice choice{
            .id = item.id, .label = item.label, .options = {}, .initial = item.initial};
        for (const PortalPair &option : item.options) {
            choice.options.append({option.first, option.second});
        }
        request.choices.append(choice);
    }
    return request;
}

QVariantMap resultsToDBus(const FileChooserRequest &request, const FileChooserResult &result)
{
    registerPortalTypes();

    QStringList uris;
    for (const QString &path : result.paths) {
        uris << QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    }

    QList<PortalPair> choices;
    for (const auto &[id, value] : result.choices) {
        choices.append({.first = id, .second = value});
    }

    QVariantMap results{{QStringLiteral("uris"), uris},
                        {QStringLiteral("choices"), QVariant::fromValue(choices)}};
    if (result.filterIndex >= 0 && result.filterIndex < request.filters.size()) {
        results.insert(QStringLiteral("current_filter"),
                       QVariant::fromValue(toPortal(request.filters.at(result.filterIndex))));
    }
    return results;
}

/// One call in flight: the chooser process, and the object at the call's
/// handle that lets the portal withdraw it.
class FileChooserPortal::Request : public QObject
{
    Q_OBJECT

public:
    Request(const FileChooserRequest &request, const QDBusMessage &message, const QString &handle,
            QObject *parent)
        : QObject(parent), m_request(request), m_message(message), m_handle(handle),
          m_process(new QProcess(this))
    {}

    QProcess *process() const { return m_process; }
    const QString &handle() const { return m_handle; }

    void run(const QStringList &command)
    {
        connect(m_process, &QProcess::readyReadStandardError, this, [this] {
            qCWarning(pfIpc) << "file chooser:" << m_process->readAllStandardError().trimmed();
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                qCWarning(pfIpc) << "the file chooser didn't start:" << m_process->errorString();
                reply(2, {});
            }
        });
        connect(m_process, &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus status) {
                    const QByteArray output = m_process->readAllStandardOutput();
                    if (m_closed || (status == QProcess::NormalExit && code == 1)) {
                        reply(1, {});
                        return;
                    }
                    FileChooserResult result;
                    if (status != QProcess::NormalExit || code != 0 ||
                        !FileChooserResult::fromJson(output, &result)) {
                        qCWarning(pfIpc)
                            << "the file chooser ended without a result (exit" << code << ")";
                        reply(2, {});
                        return;
                    }
                    reply(0, resultsToDBus(m_request, result));
                });

        m_process->start(command.constFirst(), command.mid(1));
        m_process->write(m_request.toJson());
        m_process->closeWriteChannel();
    }

    /// The portal withdrew the request.
    void close()
    {
        m_closed = true;
        if (m_process->state() == QProcess::NotRunning) {
            reply(1, {});
            return;
        }
        m_process->terminate();
        QTimer::singleShot(3000, m_process, &QProcess::kill);
    }

Q_SIGNALS:
    void answered(Request *request);

private:
    void reply(uint response, const QVariantMap &results)
    {
        if (m_replied) {
            return;
        }
        m_replied = true;
        QDBusConnection::sessionBus().send(m_message.createReply(QVariantList{response, results}));
        Q_EMIT answered(this);
    }

    FileChooserRequest m_request;
    QDBusMessage m_message;
    QString m_handle;
    QProcess *m_process;
    bool m_closed = false;
    bool m_replied = false;
};

/// org.freedesktop.impl.portal.Request, exported at each call's handle.
class RequestAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
    explicit RequestAdaptor(QObject *object) : QDBusAbstractAdaptor(object) {}

public Q_SLOTS:
    // NOLINTNEXTLINE(readability-identifier-naming): the D-Bus method name.
    void Close() { QMetaObject::invokeMethod(parent(), "close"); }
};

class RequestObject : public QObject
{
    Q_OBJECT

public:
    RequestObject(QObject *parent) : QObject(parent) { new RequestAdaptor(this); }

Q_SIGNALS:
    void closeRequested();

public Q_SLOTS:
    void close() { Q_EMIT closeRequested(); }
};

class FileChooserAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")
    Q_PROPERTY(uint version READ version CONSTANT)

public:
    explicit FileChooserAdaptor(FileChooserPortal *portal)
        : QDBusAbstractAdaptor(portal), m_portal(portal)
    {}

    static uint version() { return 4; }

public Q_SLOTS:
    // NOLINTBEGIN(readability-identifier-naming): the D-Bus method names.
    void OpenFile(const QDBusObjectPath &handle, const QString &appId, const QString &parent,
                  const QString &title, const QVariantMap &options, const QDBusMessage &message)
    {
        Q_UNUSED(parent)
        m_portal->handleCall(FileChooserRequest::Mode::Open, handle.path(), appId, title, options,
                             message);
    }

    void SaveFile(const QDBusObjectPath &handle, const QString &appId, const QString &parent,
                  const QString &title, const QVariantMap &options, const QDBusMessage &message)
    {
        Q_UNUSED(parent)
        m_portal->handleCall(FileChooserRequest::Mode::Save, handle.path(), appId, title, options,
                             message);
    }

    void SaveFiles(const QDBusObjectPath &handle, const QString &appId, const QString &parent,
                   const QString &title, const QVariantMap &options, const QDBusMessage &message)
    {
        Q_UNUSED(parent)
        m_portal->handleCall(FileChooserRequest::Mode::SaveFiles, handle.path(), appId, title,
                             options, message);
    }
    // NOLINTEND(readability-identifier-naming)

private:
    FileChooserPortal *m_portal;
};

FileChooserPortal::FileChooserPortal(QStringList chooserCommand, QObject *parent)
    : QObject(parent), m_command(std::move(chooserCommand)), m_idleTimer(new QTimer(this))
{
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(kIdleMs);
    connect(m_idleTimer, &QTimer::timeout, this, &FileChooserPortal::idle);
}

FileChooserPortal::~FileChooserPortal() = default;

bool FileChooserPortal::start(const QString &busName)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(pfIpc) << "there is no session bus";
        return false;
    }
    new FileChooserAdaptor(this);
    if (!bus.registerObject(QLatin1String(kPortalObjectPath), this,
                            QDBusConnection::ExportAdaptors) ||
        !bus.registerService(busName)) {
        qCWarning(pfIpc) << "couldn't take" << busName << "on the session bus";
        return false;
    }
    m_idleTimer->start();
    return true;
}

void FileChooserPortal::setIdleTimeout(int ms)
{
    m_idleTimer->setInterval(ms);
    if (m_requests.isEmpty()) {
        m_idleTimer->start();
    }
}

int FileChooserPortal::activeRequests() const
{
    return static_cast<int>(m_requests.size());
}

void FileChooserPortal::handleCall(FileChooserRequest::Mode mode, const QString &handle,
                                   const QString &appId, const QString &title,
                                   const QVariantMap &options, const QDBusMessage &message)
{
    message.setDelayedReply(true);
    m_idleTimer->stop();

    const FileChooserRequest request = requestFromDBus(mode, appId, title, options);
    auto *call = new Request(request, message, handle, this);
    m_requests.append(call);

    auto *object = new RequestObject(call);
    connect(object, &RequestObject::closeRequested, call, &Request::close);
    QDBusConnection::sessionBus().registerObject(handle, object);

    connect(call, &Request::answered, this, &FileChooserPortal::requestFinished);
    call->run(m_command);
}

void FileChooserPortal::requestFinished(Request *request)
{
    QDBusConnection::sessionBus().unregisterObject(request->handle());
    m_requests.removeAll(request);
    request->deleteLater();
    if (m_requests.isEmpty()) {
        m_idleTimer->start();
    }
}

int runFileChooserPortal()
{
    FileChooserPortal portal(
        {QCoreApplication::applicationFilePath(), QStringLiteral("--chooser")});
    QObject::connect(&portal, &FileChooserPortal::idle, qApp, &QCoreApplication::quit);
    if (!portal.start(QLatin1String(xdg::kPortalBusName))) {
        return 1;
    }
    return QCoreApplication::exec();
}

} // namespace pf::platform

#include "FileChooserPortal_linux.moc"
