// The parts of the launcher seam that are identical on both platforms.

#include "platform/Launcher.h"

#include <QFileInfo>
#include <QProcessEnvironment>

namespace pf::platform {

QString editorName()
{
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const char *name : {"VISUAL", "EDITOR"}) {
        const QString value = environment.value(QString::fromLatin1(name));
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString Launcher::editorName()
{
    return pf::platform::editorName();
}

} // namespace pf::platform
