#include "ui/FolderViewMemory.h"

#include "core/Logging.h"
#include "platform/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace pf::ui {
namespace {

QString storagePath()
{
    return platform::stateDir() + QStringLiteral("/folder-views.json");
}

} // namespace

FolderViewMemory &FolderViewMemory::instance()
{
    static FolderViewMemory memory;
    return memory;
}

std::optional<ViewMode> FolderViewMemory::recall(const QString &directory)
{
    ensureLoaded();
    const auto found = m_modes.constFind(directory);
    if (found == m_modes.constEnd()) {
        return std::nullopt;
    }
    return found.value();
}

void FolderViewMemory::remember(const QString &directory, ViewMode mode)
{
    if (directory.isEmpty()) {
        return;
    }
    ensureLoaded();
    if (const auto found = m_modes.constFind(directory);
        found != m_modes.constEnd() && found.value() == mode) {
        return;
    }
    m_modes.insert(directory, mode);
    save();
}

void FolderViewMemory::forget(const QString &directory)
{
    ensureLoaded();
    if (m_modes.remove(directory)) {
        save();
    }
}

void FolderViewMemory::clear()
{
    m_modes.clear();
    m_loaded = true;
}

int FolderViewMemory::size() const
{
    return static_cast<int>(m_modes.size());
}

QByteArray FolderViewMemory::toJson(const QHash<QString, ViewMode> &modes)
{
    QJsonObject root;
    for (auto it = modes.constBegin(); it != modes.constEnd(); ++it) {
        root.insert(it.key(), viewModeName(it.value()));
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QHash<QString, ViewMode> FolderViewMemory::fromJson(const QByteArray &bytes)
{
    QHash<QString, ViewMode> modes;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isString()) {
            continue;
        }
        modes.insert(it.key(), viewModeFromName(it.value().toString()));
    }
    return modes;
}

void FolderViewMemory::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    QFile file(storagePath());
    if (file.open(QIODevice::ReadOnly)) {
        m_modes = fromJson(file.readAll());
    }
}

void FolderViewMemory::save() const
{
    QDir().mkpath(platform::stateDir());

    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(pfUi) << "could not write folder views to" << storagePath();
        return;
    }
    file.write(toJson(m_modes));
    if (!file.commit()) {
        qCWarning(pfUi) << "could not commit the folder views file";
    }
}

} // namespace pf::ui
