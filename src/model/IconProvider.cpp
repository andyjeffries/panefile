#include "model/IconProvider.h"

#include "model/FileEntry.h"
#include "model/TintedIcon.h"

#include <QMimeDatabase>
#include <QMimeType>

#include <array>

namespace pf {
namespace {

using Kind = IconProvider::Kind;

/// One row of the classification tables: a kind and the names that map to it,
/// space-separated, lower case.
struct KindNames {
    Kind kind;
    const char *names;
};

/// Suffixes, matched on the text after the last dot.
///
/// A table rather than a MIME lookup because kindOf() runs once per painted
/// row, and a glob search there would put shared-mime-info into the frame
/// budget of §11 for no visible gain — the icon column cannot show more than
/// twenty pictures anyway.
///
/// Where a suffix is ambiguous the developer's reading wins, because the people
/// who live in a keyboard-driven file manager are mostly reading source trees:
/// `.ts` is TypeScript, not an MPEG transport stream, and `.m` is Objective-C.
// NOLINTBEGIN(modernize-use-designated-initializers): lookup tables, read as rows.
constexpr std::array kSuffixes{
    KindNames{Kind::Archive, "zip tar gz tgz bz2 tbz tbz2 xz txz zst tzst lz lz4 lzma lzo z 7z rar "
                             "cab cpio ar jar war ear"},
    KindNames{Kind::DiskImage, "iso img dmg vhd vhdx vmdk vdi qcow qcow2 toast sparseimage"},
    KindNames{Kind::Package, "deb rpm pkg mpkg appimage flatpak flatpakref snap apk msi msix "
                             "whl gem crate xpi"},
    KindNames{Kind::Image, "png jpg jpeg jpe jfif gif bmp webp svg svgz avif tif tiff heic heif "
                           "ico icns jxl psd xcf kra ora cr2 cr3 nef arw dng orf rw2 raf exr hdr "
                           "tga ppm pgm pbm pnm qoi"},
    KindNames{Kind::Audio, "mp3 flac ogg oga opus wav wave aac m4a m4b wma aif aiff aifc alac mid "
                           "midi ape wv mka ac3 dts amr caf"},
    KindNames{Kind::Video, "mp4 m4v mkv webm mov qt avi wmv flv mpg mpeg mpe m2ts vob 3gp 3g2 ogv "
                           "divx rm rmvb asf"},
    KindNames{Kind::Pdf, "pdf xps oxps"},
    KindNames{Kind::Document,
              "doc docx docm dot dotx odt ott fodt rtf pages wpd abw epub mobi azw3 "
              "djvu"},
    KindNames{Kind::Spreadsheet, "xls xlsx xlsm xlsb xlt xltx ods ots fods numbers csv tsv"},
    KindNames{Kind::Presentation, "ppt pptx pptm pps ppsx pot potx odp otp fodp"},
    KindNames{Kind::Font, "ttf otf ttc otc woff woff2 pfa pfb afm pcf bdf fon fnt"},
    KindNames{Kind::Web, "html htm xhtml shtml mhtml mht webloc url"},
    KindNames{Kind::Database, "db sqlite sqlite3 db3 sql mdb accdb dbf kdbx frm ibd realm"},
    KindNames{Kind::Config, "toml ini cfg conf config json jsonc json5 yaml yml env properties "
                            "plist desktop service socket timer mount target rules lock "
                            "editorconfig gitignore gitattributes gitmodules dockerignore "
                            "npmrc prettierrc eslintrc babelrc"},
    KindNames{Kind::Markdown, "md markdown mdown mkd mkdn mdx"},
    KindNames{Kind::Text, "txt text log rst adoc asciidoc org nfo asc srt vtt sub ass"},
    // Shell and batch scripts are run rather than read, so they get the
    // terminal rather than the brackets.
    KindNames{Kind::Executable, "sh bash zsh fish ksh csh tcsh command bat cmd ps1 psm1 exe com "
                                "bin run out elf"},
    KindNames{
        Kind::Code,
        "c h cc cpp cxx c++ hh hpp hxx h++ ipp inl tpp m mm rs go py pyi pyw pyx ipynb js "
        "mjs cjs jsx ts mts cts tsx rb erb rake lua zig java kt kts swift php pl pm r jl "
        "scala sc clj cljs cljc edn ex exs erl hrl hs lhs ml mli fs fsi fsx cs vb dart nim cr "
        "v sv vhd vhdl d groovy gradle cmake mk mak make s asm css scss sass less styl vue "
        "svelte astro xml xsl xslt xsd dtd qml qrc ui glsl hlsl wgsl vert frag comp metal cu "
        "tex sty cls bib diff patch el vim lisp scm ss rkt awk sed proto thrift graphql gql "
        "tf tfvars hcl nix dhall cue rego sol wasm wat ino pde f f90 f95 for pas pp adb ads "
        "cob cbl coffee elm purs re rei odin gd gdshader"},
};

/// Whole names, matched before the suffix. The cases a suffix gets wrong or
/// cannot see: `CMakeLists.txt` is source, not prose; a Makefile has no suffix.
constexpr std::array kFileNames{
    KindNames{Kind::Code, "makefile gnumakefile cmakelists.txt dockerfile containerfile justfile "
                          "rakefile gemfile podfile vagrantfile brewfile procfile jenkinsfile "
                          "meson.build build.gradle sconstruct snakefile tiltfile pkgbuild "
                          "apkbuild kbuild kconfig"},
    KindNames{Kind::Text, "readme license licence copying authors contributors changelog changes "
                          "news notice todo install thanks history"},
    KindNames{Kind::Config, "config .env .bashrc .bash_profile .bash_logout .zshrc .zshenv "
                            ".zprofile .profile .vimrc .inputrc .xinitrc .xprofile .tmux.conf "
                            ".gitconfig .clang-format .clang-tidy .npmrc .yarnrc .curlrc .wgetrc "
                            "go.mod go.sum cargo.lock"},
};
// NOLINTEND(modernize-use-designated-initializers)

/// Builds a lookup table once, on the first painted row (§3.4: no
/// namespace-scope object with a non-trivial constructor).
template<std::size_t N>
QHash<QString, Kind> buildTable(const std::array<KindNames, N> &rows)
{
    QHash<QString, Kind> table;
    for (const KindNames &row : rows) {
        const QStringList names =
            QString::fromLatin1(row.names).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &name : names) {
            // First mention wins, so the order of the table is the precedence
            // for any name listed twice.
            if (!table.contains(name)) {
                table.insert(name, row.kind);
            }
        }
    }
    return table;
}

const QHash<QString, Kind> &suffixTable()
{
    static const QHash<QString, Kind> table = buildTable(kSuffixes);
    return table;
}

const QHash<QString, Kind> &fileNameTable()
{
    static const QHash<QString, Kind> table = buildTable(kFileNames);
    return table;
}

QString resourceFor(QLatin1String name)
{
    return QStringLiteral(":/icons/files/%1.svg").arg(name);
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
    m_cache.clear();
}

QLatin1String IconProvider::iconNameFor(Kind kind)
{
    switch (kind) {
    case Kind::Directory:
        return QLatin1String("folder");
    case Kind::Text:
        return QLatin1String("text");
    case Kind::Code:
        return QLatin1String("code");
    case Kind::Markdown:
        return QLatin1String("markdown");
    case Kind::Image:
        return QLatin1String("image");
    case Kind::Audio:
        return QLatin1String("audio");
    case Kind::Video:
        return QLatin1String("video");
    case Kind::Pdf:
        return QLatin1String("pdf");
    case Kind::Document:
        return QLatin1String("document");
    case Kind::Spreadsheet:
        return QLatin1String("spreadsheet");
    case Kind::Presentation:
        return QLatin1String("presentation");
    case Kind::Archive:
        return QLatin1String("archive");
    case Kind::DiskImage:
        return QLatin1String("disk-image");
    case Kind::Package:
        return QLatin1String("package");
    case Kind::Executable:
        return QLatin1String("executable");
    case Kind::Font:
        return QLatin1String("font");
    case Kind::Web:
        return QLatin1String("web");
    case Kind::Database:
        return QLatin1String("database");
    case Kind::Config:
        return QLatin1String("config");
    case Kind::Generic:
        break;
    }
    return QLatin1String("file");
}

IconProvider::Kind IconProvider::kindOf(const FileEntry &entry)
{
    // Order matters, and mirrors the delegate's colour order: a directory is a
    // directory before it is executable, because every directory has its
    // execute bit set.
    if (entry.isDir) {
        return Kind::Directory;
    }

    if (const Kind byName = kindForName(entry.name); byName != Kind::Generic) {
        return byName;
    }

    // A link is drawn as what it points at, and its own name often says
    // nothing about that — `python3 -> python3.12`, `current -> notes.txt`.
    // The target's name is already in the entry, so asking it costs nothing.
    if (entry.isSymlink && !entry.linkTarget.isEmpty()) {
        const QString target = entry.linkTarget.section(QLatin1Char('/'), -1);
        if (const Kind byTarget = kindForName(target); byTarget != Kind::Generic) {
            return byTarget;
        }
    }

    // Only when something has already resolved it: kindOf() never asks.
    if (!entry.mimeName.isEmpty()) {
        if (const Kind byMime = kindForMimeType(entry.mimeName); byMime != Kind::Generic) {
            return byMime;
        }
    }

    // After the name, so that an executable bit on a .txt — every file on a
    // FAT or NTFS mount has one — does not turn a listing of documents into a
    // listing of programs.
    //
    // Not for a broken link, whose mode is the link's own `rwxrwxrwx`: it says
    // nothing about a target that does not exist.
    if (entry.isExecutable && !entry.isBroken) {
        return Kind::Executable;
    }

    // A dotfile with no other dot is, in a home directory, almost always some
    // program's settings or state.
    if (entry.name.lastIndexOf(QLatin1Char('.')) == 0) {
        return Kind::Config;
    }

    return Kind::Generic;
}

IconProvider::Kind IconProvider::kindForName(const QString &name)
{
    const QString lower = name.toLower();
    if (const auto byName = fileNameTable().constFind(lower);
        byName != fileNameTable().constEnd()) {
        return byName.value();
    }

    // The text after the last dot. A leading dot alone is a hidden file, not a
    // suffix — but ".gitignore" is still worth looking up as one, which is why
    // the table lists those stems too.
    const qsizetype dot = lower.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < lower.size()) {
        if (const auto bySuffix = suffixTable().constFind(lower.mid(dot + 1));
            bySuffix != suffixTable().constEnd()) {
            return bySuffix.value();
        }
    }

    return Kind::Generic;
}

IconProvider::Kind IconProvider::kindForMimeType(const QString &mimeName)
{
    const auto is = [&mimeName](const char *name) { return mimeName == QLatin1String(name); };
    const auto contains = [&mimeName](const char *part) {
        return mimeName.contains(QLatin1String(part));
    };

    if (is("inode/directory")) {
        return Kind::Directory;
    }
    if (mimeName.startsWith(QLatin1String("image/"))) {
        return Kind::Image;
    }
    if (mimeName.startsWith(QLatin1String("audio/"))) {
        return Kind::Audio;
    }
    if (mimeName.startsWith(QLatin1String("video/"))) {
        return Kind::Video;
    }
    if (mimeName.startsWith(QLatin1String("font/")) || contains("font-")) {
        return Kind::Font;
    }
    if (is("application/pdf") || is("application/oxps") || is("application/vnd.ms-xpsdocument")) {
        return Kind::Pdf;
    }
    if (is("text/markdown") || is("text/x-markdown")) {
        return Kind::Markdown;
    }
    if (is("text/html") || is("application/xhtml+xml")) {
        return Kind::Web;
    }
    if (contains("shellscript") || is("application/x-executable") ||
        is("application/x-pie-executable") || is("application/x-msdownload") ||
        is("application/x-ms-dos-executable") || is("application/x-mach-binary")) {
        return Kind::Executable;
    }
    if (is("application/vnd.debian.binary-package") || is("application/x-rpm") ||
        is("application/vnd.appimage") || is("application/x-iso9660-appimage") ||
        is("application/vnd.flatpak") || is("application/x-msi") ||
        is("application/vnd.android.package-archive")) {
        return Kind::Package;
    }
    if (contains("cd-image") || contains("disk-image") || contains("diskimage") ||
        is("application/x-raw-disk-image")) {
        return Kind::DiskImage;
    }
    if (contains("spreadsheet") || contains("ms-excel") || is("text/csv") ||
        is("text/tab-separated-values")) {
        return Kind::Spreadsheet;
    }
    if (contains("presentation") || contains("ms-powerpoint")) {
        return Kind::Presentation;
    }
    if (contains("wordprocessing") || contains("opendocument.text") || is("application/msword") ||
        is("application/rtf") || is("text/rtf") || is("application/epub+zip")) {
        return Kind::Document;
    }
    if (is("application/zip") || contains("compressed") || contains("-tar") ||
        is("application/gzip") || contains("bzip") || is("application/x-xz") ||
        is("application/zstd") || is("application/vnd.rar") || is("application/x-rar") ||
        is("application/java-archive") || is("application/x-cpio") || is("application/x-lzma")) {
        return Kind::Archive;
    }
    if (contains("sqlite") || is("application/sql") || is("application/x-sql")) {
        return Kind::Database;
    }
    if (is("application/json") || is("application/toml") || contains("yaml") || is("text/x-ini") ||
        is("application/x-desktop") || is("application/x-plist")) {
        return Kind::Config;
    }
    if (is("text/plain")) {
        return Kind::Text;
    }
    // Every other text type in shared-mime-info is some language's source:
    // text/x-c++src, text/x-python, text/css, application/javascript's peers.
    if (mimeName.startsWith(QLatin1String("text/")) || contains("javascript") ||
        contains("typescript") || is("application/xml")) {
        return Kind::Code;
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

QIcon IconProvider::iconFor(Kind kind, const QColor &tint, bool linked)
{
    const QLatin1String name = iconNameFor(kind);
    const QString key = name + QLatin1Char(linked ? '@' : ':') + tint.name(QColor::HexArgb);

    if (const auto cached = m_cache.constFind(key); cached != m_cache.constEnd()) {
        return cached.value();
    }

    QList<TintedLayer> layers{TintedLayer{.resource = resourceFor(name)}};
    if (linked) {
        // A halo is erased first, then the arrow drawn into it, so the badge
        // reads over a glyph of any density without a plate behind it.
        layers.append(
            TintedLayer{.resource = resourceFor(QLatin1String("link-cutout")), .erase = true});
        layers.append(TintedLayer{.resource = resourceFor(QLatin1String("link-badge"))});
    }

    const QIcon icon = TintedIcon::icon(layers, tint);
    m_cache.insert(key, icon);
    return icon;
}

QIcon IconProvider::iconFor(const FileEntry &entry, const QColor &tint)
{
    return iconFor(kindOf(entry), tint, entry.isSymlink);
}

} // namespace pf
