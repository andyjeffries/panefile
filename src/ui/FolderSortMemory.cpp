#include "ui/FolderSortMemory.h"

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
    return platform::stateDir() + QStringLiteral("/folder-sorts.json");
}

} // namespace

FolderSortMemory &FolderSortMemory::instance()
{
    // Function-local static, and the file is read on the first recall rather
    // than here: nothing touches the disk at load time (§3.4).
    static FolderSortMemory memory;
    return memory;
}

std::optional<SortOrder> FolderSortMemory::recall(const QString &directory)
{
    ensureLoaded();
    const auto found = m_orders.constFind(directory);
    if (found == m_orders.constEnd()) {
        return std::nullopt;
    }
    return found.value();
}

void FolderSortMemory::remember(const QString &directory, SortOrder order)
{
    if (directory.isEmpty()) {
        return;
    }
    ensureLoaded();
    if (const auto found = m_orders.constFind(directory);
        found != m_orders.constEnd() && found.value() == order) {
        return;
    }
    m_orders.insert(directory, order);
    save();
}

void FolderSortMemory::forget(const QString &directory)
{
    ensureLoaded();
    if (m_orders.remove(directory)) {
        save();
    }
}

void FolderSortMemory::clear()
{
    m_orders.clear();
    m_loaded = true;
}

int FolderSortMemory::size() const
{
    return static_cast<int>(m_orders.size());
}

QByteArray FolderSortMemory::toJson(const QHash<QString, SortOrder> &orders)
{
    // JSON rather than the session's hand-written INI: the keys are arbitrary
    // paths, and JSON already knows how to quote a path containing `=`, `]` or
    // a newline.
    QJsonObject root;
    for (auto it = orders.constBegin(); it != orders.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("sort"), sortKeyName(it.value().key));
        if (it.value().reverse) {
            entry.insert(QStringLiteral("reverse"), true);
        }
        root.insert(it.key(), entry);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QHash<QString, SortOrder> FolderSortMemory::fromJson(const QByteArray &bytes)
{
    QHash<QString, SortOrder> orders;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        if (it.key().isEmpty() || !entry.contains(QStringLiteral("sort"))) {
            continue;
        }
        orders.insert(
            it.key(),
            SortOrder{.key = sortKeyFromName(entry.value(QStringLiteral("sort")).toString()),
                      .reverse = entry.value(QStringLiteral("reverse")).toBool()});
    }
    return orders;
}

void FolderSortMemory::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    QFile file(storagePath());
    if (file.open(QIODevice::ReadOnly)) {
        m_orders = fromJson(file.readAll());
    }
}

void FolderSortMemory::save() const
{
    QDir().mkpath(platform::stateDir());

    // QSaveFile, as for the session: a crash mid-write must not leave a
    // truncated file that loses every order the user has chosen.
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(pfUi) << "could not write folder sort orders to" << storagePath();
        return;
    }
    file.write(toJson(m_orders));
    if (!file.commit()) {
        qCWarning(pfUi) << "could not commit the folder sort orders file";
    }
}

} // namespace pf::ui
