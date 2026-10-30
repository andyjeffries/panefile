#include "model/FolderIconStore.h"

#include "core/Logging.h"
#include "model/FolderIconImage.h"
#include "model/StandardFolders.h"
#include "model/TintedIcon.h"
#include "platform/Paths.h"

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSaveFile>

namespace pf {
namespace {

QString storagePath()
{
    return platform::stateDir() + QStringLiteral("/folder-icons.json");
}

QString nameOf(const QString &glyphId)
{
    return glyphId.mid(glyphId.indexOf(QLatin1Char(':')) + 1); // after "hero:" or "img:"
}

QString maskPath(const QString &glyphId)
{
    return FolderIconStore::imagesDirectory() + QLatin1Char('/') + nameOf(glyphId) +
           QStringLiteral(".png");
}

} // namespace

FolderIconStore &FolderIconStore::instance()
{
    static FolderIconStore store;
    return store;
}

bool FolderIconStore::isValidGlyphId(const QString &glyphId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(hero:[a-z0-9-]+|img:[0-9a-f]{32})$"));
    return pattern.match(glyphId).hasMatch();
}

bool FolderIconStore::isImageGlyph(const QString &glyphId)
{
    return glyphId.startsWith(QLatin1String("img:")) && isValidGlyphId(glyphId);
}

QString FolderIconStore::displayName(const QString &glyphId)
{
    if (glyphId.isEmpty()) {
        return tr("Default");
    }
    return isImageGlyph(glyphId) ? tr("pasted image") : nameOf(glyphId);
}

QString FolderIconStore::imagesDirectory()
{
    return platform::stateDir() + QStringLiteral("/folder-icons");
}

bool FolderIconStore::hasArtwork(const QString &glyphId) const
{
    return !m_svgs.value(glyphId).isEmpty() || !m_images.value(glyphId).isNull();
}

QString FolderIconStore::addImage(const QImage &mask)
{
    if (mask.isNull()) {
        return {};
    }
    const QImage grey = mask.convertToFormat(QImage::Format_Grayscale8);
    const QString id = FolderIconImage::idFor(grey);
    m_images.insert(id, grey);
    return id;
}

QImage FolderIconStore::maskFor(const QString &glyphId) const
{
    return m_images.value(glyphId);
}

QString FolderIconStore::heroiconsDirectory()
{
    for (const QString &root : platform::dataSearchPaths()) {
        const QString candidate = root + QStringLiteral("/heroicons/24/solid");
        if (QDir(candidate).exists()) {
            return candidate;
        }
    }
    return {};
}

void FolderIconStore::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;

    // Orphaned masks are only pruned when the file is absent or parsed, so a
    // damaged state file cannot wipe them.
    bool prune = true;
    QFile file(storagePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = file.readAll();
        prune = QJsonDocument::fromJson(bytes).isObject();
        m_icons = fromJson(bytes);
    }
    for (const StandardFolder &folder : StandardFolders::all()) {
        preload(folder.glyph);
    }
    for (auto it = m_icons.begin(); it != m_icons.end();) {
        preload(it.value());
        it = hasArtwork(it.value()) ? std::next(it) : m_icons.erase(it);
    }
    if (prune) {
        pruneImages();
    }
}

void FolderIconStore::preload(const QString &glyphId)
{
    if (m_svgs.contains(glyphId) || m_images.contains(glyphId) || !isValidGlyphId(glyphId)) {
        return;
    }
    const QString name = nameOf(glyphId);

    if (isImageGlyph(glyphId)) {
        QImageReader reader(maskPath(glyphId), "png");
        reader.setAllocationLimit(16);
        QImage mask;
        if (reader.size() == QSize(FolderIconImage::kMaskSize, FolderIconImage::kMaskSize)) {
            mask = reader.read();
        }
        if (mask.isNull()) {
            // Remembered as null, so a bad file is logged once and not per paint.
            qCWarning(pfApp) << "folder icon mask not usable:" << glyphId;
        } else {
            mask = mask.convertToFormat(QImage::Format_Grayscale8);
        }
        m_images.insert(glyphId, mask);
        return;
    }

    // The nine emblems are compiled in; the rest come from the data directory.
    QFile emblem(QStringLiteral(":/icons/emblems/%1.svg").arg(name));
    if (emblem.open(QIODevice::ReadOnly)) {
        m_svgs.insert(glyphId, emblem.readAll());
        return;
    }
    const QString directory = heroiconsDirectory();
    QFile file(directory + QLatin1Char('/') + name + QStringLiteral(".svg"));
    if (!directory.isEmpty() && file.open(QIODevice::ReadOnly)) {
        m_svgs.insert(glyphId, file.readAll());
        return;
    }
    // Remembered as empty, so a missing file is logged once and not per paint.
    qCWarning(pfApp) << "folder icon glyph not found:" << glyphId;
    m_svgs.insert(glyphId, QByteArray());
}

QIcon FolderIconStore::iconFor(const QString &glyphId, const QColor &tint)
{
    TintedLayer layer;
    layer.id = glyphId;
    if (isImageGlyph(glyphId)) {
        layer.mask = maskFor(glyphId);
    } else {
        layer.svg = svgFor(glyphId);
    }
    return TintedIcon::icon({layer}, tint);
}

QByteArray FolderIconStore::svgFor(const QString &glyphId)
{
    if (!m_svgs.contains(glyphId)) {
        preload(glyphId);
    }
    return m_svgs.value(glyphId);
}

QString FolderIconStore::glyphFor(const QString &path) const
{
    return m_icons.value(path);
}

QString FolderIconStore::effectiveGlyph(const QString &path) const
{
    if (const auto found = m_icons.constFind(path); found != m_icons.constEnd()) {
        return found.value();
    }
    return StandardFolders::glyphFor(path);
}

bool FolderIconStore::set(const QString &path, const QString &glyphId)
{
    ensureLoaded();
    if (path.isEmpty()) {
        return false;
    }
    if (glyphId.isEmpty()) {
        clear(path);
        return true;
    }
    preload(glyphId);
    if (!hasArtwork(glyphId)) {
        return false;
    }
    if (isImageGlyph(glyphId) && !QFile::exists(maskPath(glyphId))) {
        QDir().mkpath(imagesDirectory());
        QSaveFile file(maskPath(glyphId));
        if (!file.open(QIODevice::WriteOnly) || !m_images.value(glyphId).save(&file, "PNG") ||
            !file.commit()) {
            qCWarning(pfApp) << "could not write folder icon mask" << maskPath(glyphId);
            return false;
        }
    }
    m_icons.insert(QDir::cleanPath(path), glyphId);
    save();
    pruneImages();
    Q_EMIT changed(QDir::cleanPath(path));
    return true;
}

void FolderIconStore::clear(const QString &path)
{
    ensureLoaded();
    const QString cleaned = QDir::cleanPath(path);
    if (m_icons.remove(cleaned)) {
        save();
        pruneImages();
        Q_EMIT changed(cleaned);
    }
}

void FolderIconStore::renamed(const QString &oldPath, const QString &newPath)
{
    ensureLoaded();
    const QString from = QDir::cleanPath(oldPath);
    const QString to = QDir::cleanPath(newPath);
    if (from == to || from.isEmpty() || to.isEmpty()) {
        return;
    }

    const QString prefix = from + QLatin1Char('/');
    QHash<QString, QString> moved;
    for (auto it = m_icons.begin(); it != m_icons.end();) {
        if (it.key() == from) {
            moved.insert(to, it.value());
            it = m_icons.erase(it);
        } else if (it.key().startsWith(prefix)) {
            moved.insert(to + QLatin1Char('/') + it.key().mid(prefix.size()), it.value());
            it = m_icons.erase(it);
        } else {
            ++it;
        }
    }
    if (moved.isEmpty()) {
        return;
    }
    m_icons.insert(moved);
    save();
    Q_EMIT changed(from);
    Q_EMIT changed(to);
}

void FolderIconStore::removed(const QString &path)
{
    ensureLoaded();
    const QString cleaned = QDir::cleanPath(path);
    const QString prefix = cleaned + QLatin1Char('/');
    bool any = false;
    for (auto it = m_icons.begin(); it != m_icons.end();) {
        if (it.key() == cleaned || it.key().startsWith(prefix)) {
            it = m_icons.erase(it);
            any = true;
        } else {
            ++it;
        }
    }
    if (any) {
        save();
        pruneImages();
        Q_EMIT changed(cleaned);
    }
}

void FolderIconStore::reset()
{
    m_icons.clear();
    m_images.clear();
    m_loaded = true;
    for (const StandardFolder &folder : StandardFolders::all()) {
        preload(folder.glyph);
    }
}

QByteArray FolderIconStore::toJson(const QHash<QString, QString> &icons)
{
    QJsonObject root;
    for (auto it = icons.constBegin(); it != icons.constEnd(); ++it) {
        root.insert(it.key(), it.value());
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QHash<QString, QString> FolderIconStore::fromJson(const QByteArray &bytes)
{
    QHash<QString, QString> icons;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QString glyph = it.value().toString();
        if (it.key().isEmpty() || !isValidGlyphId(glyph)) {
            continue;
        }
        icons.insert(it.key(), glyph);
    }
    return icons;
}

void FolderIconStore::pruneImages() const
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{32}\\.png$"));
    QSet<QString> used;
    for (const QString &glyph : m_icons) {
        used.insert(glyph);
    }
    const QDir directory(imagesDirectory());
    for (const QString &name : directory.entryList(QDir::Files)) {
        if (pattern.match(name).hasMatch() &&
            !used.contains(QStringLiteral("img:") + name.left(32))) {
            QFile::remove(directory.filePath(name));
        }
    }
}

void FolderIconStore::save() const
{
    QDir().mkpath(platform::stateDir());
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(pfApp) << "could not write folder icons to" << storagePath();
        return;
    }
    file.write(toJson(m_icons));
    if (!file.commit()) {
        qCWarning(pfApp) << "could not commit the folder icons file";
    }
}

} // namespace pf
