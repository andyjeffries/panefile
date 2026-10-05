#include "model/SymbolicIcon.h"

#include "model/TintedIcon.h"

#include <QFile>
#include <QSize>

namespace pf {

QString SymbolicIcon::resourcePath(const QString &name)
{
    return QStringLiteral(":/icons/symbolic/%1.svg").arg(name);
}

bool SymbolicIcon::exists(const QString &name)
{
    return !name.isEmpty() && QFile::exists(resourcePath(name));
}

QIcon SymbolicIcon::icon(const QString &name, const QColor &colour)
{
    if (!exists(name)) {
        return {};
    }
    return TintedIcon::icon({TintedLayer{.resource = resourcePath(name)}}, colour);
}

QPixmap SymbolicIcon::pixmap(const QString &name, const QColor &colour, int size,
                             qreal devicePixelRatio)
{
    if (!exists(name)) {
        return {};
    }
    return TintedIcon::pixmap({TintedLayer{.resource = resourcePath(name)}}, colour,
                              QSize(size, size), devicePixelRatio);
}

} // namespace pf
