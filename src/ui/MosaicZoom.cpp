#include "ui/MosaicZoom.h"

#include "core/Logging.h"
#include "platform/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace pf::ui {
namespace {

QString storagePath()
{
    return platform::stateDir() + QStringLiteral("/mosaic.json");
}

} // namespace

MosaicZoom &MosaicZoom::instance()
{
    static MosaicZoom zoom;
    return zoom;
}

int MosaicZoom::rowHeight()
{
    ensureLoaded();
    return m_rowHeight;
}

void MosaicZoom::setRowHeight(int height)
{
    ensureLoaded();
    height = std::clamp(height, kMin, kMax);
    if (height == m_rowHeight) {
        return;
    }
    m_rowHeight = height;
    save();
    Q_EMIT rowHeightChanged(height);
}

void MosaicZoom::zoomBy(int steps)
{
    setRowHeight(rowHeight() + (steps * kStep));
}

void MosaicZoom::clear()
{
    m_rowHeight = kDefault;
    m_loaded = true;
}

QByteArray MosaicZoom::toJson(int rowHeight)
{
    QJsonObject root;
    root.insert(QStringLiteral("row_height"), rowHeight);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

int MosaicZoom::fromJson(const QByteArray &bytes)
{
    const QJsonValue value =
        QJsonDocument::fromJson(bytes).object().value(QStringLiteral("row_height"));
    if (!value.isDouble()) {
        return kDefault;
    }
    return std::clamp(value.toInt(kDefault), kMin, kMax);
}

void MosaicZoom::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    QFile file(storagePath());
    if (file.open(QIODevice::ReadOnly)) {
        m_rowHeight = fromJson(file.readAll());
    }
}

void MosaicZoom::save() const
{
    QDir().mkpath(platform::stateDir());

    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(pfUi) << "could not write the mosaic zoom to" << storagePath();
        return;
    }
    file.write(toJson(m_rowHeight));
    if (!file.commit()) {
        qCWarning(pfUi) << "could not commit the mosaic zoom file";
    }
}

} // namespace pf::ui
