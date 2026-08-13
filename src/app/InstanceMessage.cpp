#include "app/InstanceMessage.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace pf {
namespace {

QString placementName(PlacementOverride placement)
{
    switch (placement) {
    case PlacementOverride::Here:
        return QStringLiteral("here");
    case PlacementOverride::NewPanel:
        return QStringLiteral("panel");
    case PlacementOverride::NewWindow:
        return QStringLiteral("window");
    case PlacementOverride::None:
        break;
    }
    return QStringLiteral("auto");
}

PlacementOverride placementFromName(const QString &name)
{
    if (name == QLatin1String("here")) {
        return PlacementOverride::Here;
    }
    if (name == QLatin1String("panel")) {
        return PlacementOverride::NewPanel;
    }
    if (name == QLatin1String("window")) {
        return PlacementOverride::NewWindow;
    }
    return PlacementOverride::None;
}

} // namespace

QByteArray InstanceMessage::toJson() const
{
    QJsonArray pathArray;
    for (const QString &path : paths) {
        pathArray.append(path);
    }

    const QJsonObject flags{
        {QStringLiteral("placement"), placementName(placement)},
    };

    const QJsonObject object{
        {QStringLiteral("v"), version},
        {QStringLiteral("cwd"), cwd},
        {QStringLiteral("paths"), pathArray},
        {QStringLiteral("flags"), flags},
        {QStringLiteral("activation_token"), activationToken},
        {QStringLiteral("desktop_startup_id"), desktopStartupId},
    };

    // Compact, and newline-terminated: the reader needs a frame boundary, and a
    // socket delivers bytes rather than messages.
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

bool InstanceMessage::fromJson(const QByteArray &bytes, InstanceMessage *out)
{
    if (out == nullptr) {
        return false;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonObject object = document.object();

    // §10.3: "on version mismatch, the client starts its own instance rather
    // than sending something the server might misread." Exact equality rather
    // than a floor, because the mismatch can point either way — a new client
    // talking to an old instance is the same problem in reverse.
    const int version = object.value(QStringLiteral("v")).toInt(-1);
    if (version != kVersion) {
        return false;
    }

    out->version = version;
    out->cwd = object.value(QStringLiteral("cwd")).toString();
    out->activationToken = object.value(QStringLiteral("activation_token")).toString();
    out->desktopStartupId = object.value(QStringLiteral("desktop_startup_id")).toString();
    out->placement = placementFromName(object.value(QStringLiteral("flags"))
                                           .toObject()
                                           .value(QStringLiteral("placement"))
                                           .toString());

    out->paths.clear();
    for (const auto &value : object.value(QStringLiteral("paths")).toArray()) {
        out->paths.append(value.toString());
    }

    return true;
}

QStringList InstanceMessage::absolutePaths() const
{
    QStringList resolved;
    resolved.reserve(paths.size());

    const QDir base(cwd.isEmpty() ? QDir::currentPath() : cwd);

    for (const QString &path : paths) {
        // §10.1: "may be `file://` URIs so that `%U` in the .desktop file
        // works." Checked with a prefix rather than by asking QUrl, because
        // QUrl::fromUserInput would happily turn a relative path into an http
        // URL and a filename containing a colon into a scheme.
        if (path.startsWith(QLatin1String("file://"))) {
            const QString local = QUrl(path).toLocalFile();
            if (!local.isEmpty()) {
                resolved.append(QDir::cleanPath(local));
                continue;
            }
        }

        // §10.1: "resolved against the *client's* working directory, not the
        // running instance's".
        resolved.append(QDir::cleanPath(base.absoluteFilePath(path)));
    }

    return resolved;
}

} // namespace pf
