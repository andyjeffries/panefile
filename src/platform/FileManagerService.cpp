// The platform-independent parts of the default-file-manager support: turning
// the URIs a caller sends into paths, and describing the current handler.

#include "platform/FileManagerService.h"
#include "platform/DefaultFileManager.h"

#include <QDir>
#include <QUrl>

namespace pf::platform {

QString DefaultFileManagerStatus::handlerDescription() const
{
    if (handlerId.isEmpty()) {
        return QStringLiteral("none");
    }
    if (handlerId == QLatin1String(kPanefileDesktopId)) {
        return QStringLiteral("Panefile");
    }
    return handlerName.isEmpty() ? handlerId
                                 : QStringLiteral("%1 (%2)").arg(handlerName, handlerId);
}

QStringList FileManagerService::localPathsFromUris(const QStringList &uris, QStringList *rejected)
{
    QStringList paths;
    for (const QString &uri : uris) {
        QUrl url(uri);
        const bool local = url.isValid() &&
                           url.scheme().compare(QLatin1String("file"), Qt::CaseInsensitive) == 0 &&
                           (url.host().isEmpty() || url.host() == QLatin1String("localhost"));

        // `localhost` is this machine, but QUrl turns any host into a UNC
        // path — //localhost/etc — so it is dropped first.
        //
        // toLocalFile() percent-decodes, and cleanPath() drops the trailing
        // slash a folder URI often carries — without that, "the parent of
        // file:///home/me/Downloads/" would be Downloads itself.
        url.setHost(QString());
        const QString path = local ? url.toLocalFile() : QString();
        if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
            if (rejected != nullptr) {
                rejected->append(uri);
            }
            continue;
        }
        paths << QDir::cleanPath(path);
    }
    return paths;
}

} // namespace pf::platform
