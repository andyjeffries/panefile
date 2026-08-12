#include "model/IconProvider.h"

#include "model/FileEntry.h"

#include <QMimeDatabase>
#include <QMimeType>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <array>

namespace pf {
namespace {

/// Suffix tables for kindOf().
///
/// Matching on the name rather than the MIME type is deliberate: kindOf() runs
/// once per painted row, and a QMimeDatabase lookup there would put a glob-table
/// search into the frame budget of §11 for no visible benefit — the icon column
/// cannot show more than a handful of distinct pictures anyway.
constexpr std::array<QLatin1String, 14> kArchiveSuffixes{
    QLatin1String(".zip"), QLatin1String(".tar"), QLatin1String(".gz"),  QLatin1String(".bz2"),
    QLatin1String(".xz"),  QLatin1String(".zst"), QLatin1String(".7z"),  QLatin1String(".rar"),
    QLatin1String(".tgz"), QLatin1String(".tbz"), QLatin1String(".lz4"), QLatin1String(".jar"),
    QLatin1String(".deb"), QLatin1String(".rpm")};

constexpr std::array<QLatin1String, 10> kImageSuffixes{
    QLatin1String(".png"),  QLatin1String(".jpg"),  QLatin1String(".jpeg"), QLatin1String(".gif"),
    QLatin1String(".bmp"),  QLatin1String(".webp"), QLatin1String(".svg"),  QLatin1String(".avif"),
    QLatin1String(".tiff"), QLatin1String(".heic")};

constexpr std::array<QLatin1String, 20> kCodeSuffixes{
    QLatin1String(".c"),   QLatin1String(".h"),     QLatin1String(".cpp"),
    QLatin1String(".hpp"), QLatin1String(".cc"),    QLatin1String(".rs"),
    QLatin1String(".go"),  QLatin1String(".py"),    QLatin1String(".js"),
    QLatin1String(".ts"),  QLatin1String(".rb"),    QLatin1String(".sh"),
    QLatin1String(".lua"), QLatin1String(".zig"),   QLatin1String(".java"),
    QLatin1String(".kt"),  QLatin1String(".swift"), QLatin1String(".php"),
    QLatin1String(".sql"), QLatin1String(".cmake")};

constexpr std::array<QLatin1String, 8> kTextSuffixes{
    QLatin1String(".txt"),  QLatin1String(".md"),  QLatin1String(".rst"),  QLatin1String(".log"),
    QLatin1String(".toml"), QLatin1String(".ini"), QLatin1String(".conf"), QLatin1String(".json")};

template<std::size_t N>
bool endsWithAny(const QString &name, const std::array<QLatin1String, N> &suffixes)
{
    return std::ranges::any_of(suffixes, [&name](QLatin1String suffix) {
        return name.endsWith(suffix, Qt::CaseInsensitive);
    });
}

QLatin1String bundledFileFor(IconProvider::Kind kind)
{
    switch (kind) {
    case IconProvider::Kind::Directory:
        return QLatin1String("folder");
    case IconProvider::Kind::Archive:
        return QLatin1String("archive");
    case IconProvider::Kind::Image:
        return QLatin1String("image");
    case IconProvider::Kind::Executable:
        return QLatin1String("executable");
    case IconProvider::Kind::Symlink:
        return QLatin1String("symlink");
    case IconProvider::Kind::Code:
        return QLatin1String("code");
    case IconProvider::Kind::Text:
        return QLatin1String("text");
    case IconProvider::Kind::Generic:
        break;
    }
    return QLatin1String("file");
}

/// Icon theme names to try for a kind, most specific first.
QStringList themeNamesFor(IconProvider::Kind kind, const QString &mimeIcon,
                          const QString &genericIcon)
{
    QStringList names;
    if (kind == IconProvider::Kind::Directory) {
        names << QStringLiteral("folder") << QStringLiteral("inode-directory");
        return names;
    }
    if (!mimeIcon.isEmpty()) {
        names << mimeIcon;
    }
    if (!genericIcon.isEmpty()) {
        names << genericIcon;
    }
    return names;
}

} // namespace

IconProvider &IconProvider::instance()
{
    // Function-local static: constructed on the first icon request, which is
    // after the first paint, never at load time (§3.4).
    static IconProvider provider;
    return provider;
}

void IconProvider::clear()
{
    m_themeCache.clear();
    m_bundledCache.clear();
}

IconProvider::Kind IconProvider::kindOf(const FileEntry &entry)
{
    // Order matters, and mirrors the delegate's colour order: a directory is a
    // directory before it is executable, because every directory has its
    // execute bit set.
    if (entry.isDir) {
        return Kind::Directory;
    }
    if (entry.isSymlink) {
        return Kind::Symlink;
    }
    if (endsWithAny(entry.name, kArchiveSuffixes)) {
        return Kind::Archive;
    }
    if (endsWithAny(entry.name, kImageSuffixes)) {
        return Kind::Image;
    }
    if (endsWithAny(entry.name, kCodeSuffixes)) {
        return Kind::Code;
    }
    if (endsWithAny(entry.name, kTextSuffixes)) {
        return Kind::Text;
    }
    if (entry.isExecutable) {
        return Kind::Executable;
    }
    return Kind::Generic;
}

QString IconProvider::mimeNameFor(const QString &directory, const FileEntry &entry)
{
    static const QMimeDatabase database;

    if (entry.isDir) {
        return QStringLiteral("inode/directory");
    }

    // MatchExtension never opens the file. For the common case — a directory
    // full of files with extensions — that is the difference between listing a
    // directory and reading all of it.
    QMimeType type = database.mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension);
    if (type.isValid() && !type.isDefault()) {
        return type.name();
    }

    // §4.3: sniff only for extensionless files, and only once visible. A caller
    // reaching this point is already painting the row.
    if (!entry.name.contains(QLatin1Char('.'))) {
        const QString fullPath = directory + QLatin1Char('/') + entry.name;
        type = database.mimeTypeForFile(fullPath, QMimeDatabase::MatchContent);
        if (type.isValid()) {
            return type.name();
        }
    }

    return QStringLiteral("application/octet-stream");
}

QIcon IconProvider::bundledIcon(Kind kind, const QColor &tint)
{
    const QLatin1String file = bundledFileFor(kind);
    const QString key = file + QLatin1Char('\x1f') + tint.name(QColor::HexArgb);

    if (const auto cached = m_bundledCache.constFind(key); cached != m_bundledCache.constEnd()) {
        return cached.value();
    }

    // Rendered larger than the row needs and left for QIcon to scale down, so
    // the glyph stays crisp on a HiDPI display without the cache holding a
    // separate pixmap per device ratio.
    constexpr int kRenderSize = 32;

    QPixmap pixmap(kRenderSize, kRenderSize);
    pixmap.fill(Qt::transparent);
    {
        const QIcon source(QStringLiteral(":/icons/fallback/%1.svg").arg(file));
        QPainter painter(&pixmap);
        source.paint(&painter, QRect(0, 0, kRenderSize, kRenderSize));
        // The glyphs are drawn in one colour at varying opacity, so compositing
        // the tint through SourceIn recolours them while keeping the shading
        // that distinguishes, say, a page from its folded corner.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), tint);
    }

    const QIcon icon(pixmap);
    m_bundledCache.insert(key, icon);
    return icon;
}

QIcon IconProvider::iconFor(const FileEntry &entry, const QColor &tint)
{
    const Kind kind = kindOf(entry);

    QString mimeIcon;
    QString genericIcon;
    if (kind != Kind::Directory) {
        static const QMimeDatabase database;
        const QMimeType type = database.mimeTypeForFile(entry.name, QMimeDatabase::MatchExtension);
        mimeIcon = type.iconName();
        genericIcon = type.genericIconName();
    }

    const QStringList candidates = themeNamesFor(kind, mimeIcon, genericIcon);
    const QString cacheKey = candidates.join(QLatin1Char('\x1f'));

    if (const auto cached = m_themeCache.constFind(cacheKey); cached != m_themeCache.constEnd()) {
        if (!cached.value().isNull()) {
            return cached.value();
        }
        return bundledIcon(kind, tint);
    }

    QIcon themed;
    for (const QString &name : candidates) {
        themed = QIcon::fromTheme(name);
        if (!themed.isNull()) {
            break;
        }
    }

    // A null result is cached too. Without that, every row of every scroll would
    // re-walk the icon theme's directories to rediscover the same nothing — and
    // on macOS, where there is no icon theme at all, that is every row.
    m_themeCache.insert(cacheKey, themed);

    if (!themed.isNull()) {
        return themed;
    }
    return bundledIcon(kind, tint);
}

} // namespace pf
