#pragma once

#include "platform/FileChooserRequest.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class QTimer;

namespace pf::platform {

/// The portal's compound types, as QtDBus streams them: `(us)`, `(sa(us))`,
/// `(ss)` and `(ssa(ss)s)`. Registered on first use.
struct PortalPattern {
    uint kind = 0;
    QString text;
    bool operator==(const PortalPattern &) const = default;
};
struct PortalFilter {
    QString name;
    QList<PortalPattern> patterns;
    bool operator==(const PortalFilter &) const = default;
};
struct PortalPair {
    QString first;
    QString second;
};
struct PortalChoice {
    QString id;
    QString label;
    QList<PortalPair> options;
    QString initial;
};

/// Registers the compound types with QtDBus. Called by the functions below;
/// a client that sends them calls it too.
void registerPortalTypes();

QDBusArgument &operator<<(QDBusArgument &arg, const PortalPattern &pattern);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalPattern &pattern);
QDBusArgument &operator<<(QDBusArgument &arg, const PortalFilter &filter);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalFilter &filter);
QDBusArgument &operator<<(QDBusArgument &arg, const PortalPair &pair);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalPair &pair);
QDBusArgument &operator<<(QDBusArgument &arg, const PortalChoice &choice);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalChoice &choice);

/// A request in the portal's own terms, from the options of a call.
///
/// The portal's types are `a(sa(us))` for filters, `a(ssa(ss)s)` for choices
/// and NUL-terminated byte arrays for paths; all of them arrive wrapped in a
/// QDBusArgument inside the variant.
FileChooserRequest requestFromDBus(FileChooserRequest::Mode mode, const QString &appId,
                                   const QString &title, const QVariantMap &options);

/// The `results` dictionary of the portal's Response: `uris`, `choices`, and
/// `current_filter` when one was chosen.
QVariantMap resultsToDBus(const FileChooserRequest &request, const FileChooserResult &result);

/// org.freedesktop.impl.portal.FileChooser, version 4: the backend
/// xdg-desktop-portal asks to show an Open or Save dialog.
///
/// It draws nothing and never touches the display. Each call starts a chooser
/// process, writes the request to its stdin as JSON and answers the call when
/// it exits: 0 with a result is a pick, 1 is a cancel, anything else is an
/// error. Closing the request ends that process.
class FileChooserPortal : public QObject
{
    Q_OBJECT

public:
    /// How long the service waits with nothing to do before it says it is idle.
    static constexpr int kIdleMs = 120000;

    /// `chooserCommand` is the program and its arguments.
    explicit FileChooserPortal(QStringList chooserCommand, QObject *parent = nullptr);
    ~FileChooserPortal() override;

    /// Exports the objects and takes the bus name `busName`. False when the
    /// name is already owned.
    bool start(const QString &busName);

    void setIdleTimeout(int ms);
    int activeRequests() const;

    /// What the adaptor's methods do. Answers the call later, through `message`.
    void handleCall(FileChooserRequest::Mode mode, const QString &handle, const QString &appId,
                    const QString &title, const QVariantMap &options, const QDBusMessage &message);

Q_SIGNALS:
    /// No request for the idle timeout; the process may exit.
    void idle();

private:
    class Request;
    void requestFinished(Request *request);

    QStringList m_command;
    QList<Request *> m_requests;
    QTimer *m_idleTimer = nullptr;
};

} // namespace pf::platform

Q_DECLARE_METATYPE(pf::platform::PortalPattern)
Q_DECLARE_METATYPE(pf::platform::PortalFilter)
Q_DECLARE_METATYPE(pf::platform::PortalPair)
Q_DECLARE_METATYPE(pf::platform::PortalChoice)
Q_DECLARE_METATYPE(QList<pf::platform::PortalFilter>)
Q_DECLARE_METATYPE(QList<pf::platform::PortalPair>)
Q_DECLARE_METATYPE(QList<pf::platform::PortalChoice>)
