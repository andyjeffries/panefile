#include "model/StandardFolders.h"

#include <QDir>
#include <QHash>
#include <QStandardPaths>

namespace pf {
namespace {

struct Spec {
    const char *title;
    const char *hero;
    QStandardPaths::StandardLocation location; ///< unused for Home
    bool home;
};

constexpr Spec kSpecs[]{
    {.title = "Home", .hero = "home", .location = QStandardPaths::HomeLocation, .home = true},
    {.title = "Desktop",
     .hero = "computer-desktop",
     .location = QStandardPaths::DesktopLocation,
     .home = false},
    {.title = "Documents",
     .hero = "document-text",
     .location = QStandardPaths::DocumentsLocation,
     .home = false},
    {.title = "Downloads",
     .hero = "arrow-down-tray",
     .location = QStandardPaths::DownloadLocation,
     .home = false},
    {.title = "Music",
     .hero = "musical-note",
     .location = QStandardPaths::MusicLocation,
     .home = false},
    {.title = "Pictures",
     .hero = "photo",
     .location = QStandardPaths::PicturesLocation,
     .home = false},
    {.title = "Videos", .hero = "film", .location = QStandardPaths::MoviesLocation, .home = false},
    {.title = "Public",
     .hero = "globe-alt",
     .location = QStandardPaths::PublicShareLocation,
     .home = false},
    {.title = "Templates",
     .hero = "document-duplicate",
     .location = QStandardPaths::TemplatesLocation,
     .home = false},
};

QString glyphIdFor(const char *hero)
{
    return QStringLiteral("hero:") + QString::fromLatin1(hero);
}

} // namespace

const QList<StandardFolder> &StandardFolders::all()
{
    static const QList<StandardFolder> folders = [] {
        QList<StandardFolder> result;
        const QString home = QDir::cleanPath(QDir::homePath());
        for (const Spec &spec : kSpecs) {
            const QString path =
                spec.home ? home : QDir::cleanPath(QStandardPaths::writableLocation(spec.location));
            // XDG falls back to $HOME for a directory the user has not set.
            if (path.isEmpty() || (!spec.home && path == home)) {
                continue;
            }
            result.append({.title = QString::fromLatin1(spec.title),
                           .glyph = glyphIdFor(spec.hero),
                           .path = path});
        }
        return result;
    }();
    return folders;
}

QString StandardFolders::glyphFor(const QString &path)
{
    static const QHash<QString, QString> byPath = [] {
        QHash<QString, QString> table;
        for (const StandardFolder &folder : all()) {
            table.insert(folder.path, folder.glyph);
        }
        return table;
    }();
    return byPath.value(path);
}

QString StandardFolders::heroNameFor(const QString &path)
{
    const QString glyph = glyphFor(QDir::cleanPath(path));
    return glyph.isEmpty() ? QString() : glyph.mid(5);
}

QList<StandardFolder> StandardFolders::presets()
{
    QList<StandardFolder> result;
    for (const Spec &spec : kSpecs) {
        result.append(
            {.title = QString::fromLatin1(spec.title), .glyph = glyphIdFor(spec.hero), .path = {}});
    }
    return result;
}

} // namespace pf
