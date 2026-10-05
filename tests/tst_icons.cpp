#include "model/FileEntry.h"
#include "model/IconProvider.h"
#include "model/SymbolicIcon.h"
#include "model/TintedIcon.h"

#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

namespace {

using Kind = IconProvider::Kind;

FileEntry fileNamed(const QString &name)
{
    FileEntry entry;
    entry.name = name;
    return entry;
}

QImage imageOf(const QIcon &icon, int size, qreal ratio, QIcon::Mode mode = QIcon::Normal)
{
    return icon.pixmap(QSize(size, size), ratio, mode)
        .toImage()
        .convertToFormat(QImage::Format_ARGB32);
}

/// The colour and alpha of the most opaque pixel.
QColor strongestPixel(const QImage &image)
{
    QColor strongest(Qt::transparent);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor colour = image.pixelColor(x, y);
            if (colour.alpha() > strongest.alpha()) {
                strongest = colour;
            }
        }
    }
    return strongest;
}

int opaquePixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += image.pixelColor(x, y).alpha() > 0 ? 1 : 0;
        }
    }
    return count;
}

} // namespace

/// §4.3's bundled file-type icons and the chrome's symbolic glyphs.
class TestIcons : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Every kind resolves to a compiled-in glyph that renders and takes the
    /// tint, on every platform. This is what lets the delegate's colour rules —
    /// the tint, the unfocused dimming, white on the pill — always apply.
    void everyKindHasATintedIcon()
    {
        const QColor tint(0x33, 0x66, 0xcc);
        for (const Kind kind : IconProvider::kAllKinds) {
            const QString name = IconProvider::iconNameFor(kind);
            QVERIFY2(QFile::exists(QStringLiteral(":/icons/files/%1.svg").arg(name)),
                     qPrintable(name));

            const QIcon icon = IconProvider::instance().iconFor(kind, tint);
            QVERIFY2(!icon.isNull(), qPrintable(name));

            const QImage image = imageOf(icon, 16, 1.0);
            QVERIFY2(opaquePixels(image) > 20, qPrintable(name));

            QColor strongest = strongestPixel(image);
            QCOMPARE(strongest.alpha(), 255);
            strongest.setAlpha(255);
            QCOMPARE(strongest, tint);
        }
    }

    /// Kinds are told apart by their glyphs, not only by the tint the delegate
    /// gives them: no two kinds share a picture.
    void everyKindLooksDifferent()
    {
        QHash<QByteArray, QString> seen;
        for (const Kind kind : IconProvider::kAllKinds) {
            const QImage image =
                imageOf(IconProvider::instance().iconFor(kind, Qt::black), 16, 1.0);
            const QByteArray pixels(reinterpret_cast<const char *>(image.constBits()),
                                    image.sizeInBytes());
            const QString name = IconProvider::iconNameFor(kind);
            QVERIFY2(!seen.contains(pixels),
                     qPrintable(name + QStringLiteral(" looks like ") + seen.value(pixels)));
            seen.insert(pixels, name);
        }
    }

    /// Rendered at the ratio it is painted at, not scaled from one size: at 2x
    /// the pixmap has twice the device pixels and says so.
    void rendersAtTheDevicePixelRatio()
    {
        const QIcon icon = IconProvider::instance().iconFor(Kind::Directory, Qt::black);

        const QPixmap single = icon.pixmap(QSize(16, 16), 1.0);
        QCOMPARE(single.size(), QSize(16, 16));

        const QPixmap doubled = icon.pixmap(QSize(16, 16), 2.0);
        QCOMPARE(doubled.size(), QSize(32, 32));
        QCOMPARE(doubled.devicePixelRatio(), 2.0);
    }

    /// The desktop icon theme is never consulted for a file type. A theme
    /// whose folder is solid red is installed and made current; the folder
    /// still comes out in the tint it was asked for.
    void ignoresTheDesktopIconTheme()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QDir dir(root.path());
        QVERIFY(dir.mkpath(QStringLiteral("red/scalable/places")));
        {
            QFile index(dir.filePath(QStringLiteral("red/index.theme")));
            QVERIFY(index.open(QIODevice::WriteOnly));
            index.write("[Icon Theme]\nName=red\nDirectories=scalable/places\n\n"
                        "[scalable/places]\nSize=16\nMinSize=8\nMaxSize=512\nType=Scalable\n");
        }
        for (const char *name : {"folder", "inode-directory", "text-x-generic"}) {
            QFile svg(dir.filePath(
                QStringLiteral("red/scalable/places/%1.svg").arg(QLatin1String(name))));
            QVERIFY(svg.open(QIODevice::WriteOnly));
            svg.write("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
                      "<rect width='16' height='16' fill='#ff0000'/></svg>");
        }

        const QStringList oldPaths = QIcon::themeSearchPaths();
        const QString oldTheme = QIcon::themeName();
        QIcon::setThemeSearchPaths({root.path()});
        QIcon::setThemeName(QStringLiteral("red"));

        IconProvider::instance().clear();
        FileEntry folder = fileNamed(QStringLiteral("src"));
        folder.isDir = true;
        const QColor tint(0x00, 0x00, 0xff);
        QColor strongest =
            strongestPixel(imageOf(IconProvider::instance().iconFor(folder, tint), 16, 1.0));
        strongest.setAlpha(255);

        QIcon::setThemeSearchPaths(oldPaths);
        QIcon::setThemeName(oldTheme);

        QCOMPARE(strongest, tint);
    }

    /// A symlink is drawn as what it points at, with a badge: the badge must
    /// actually change the picture, and must not replace it.
    void symlinksGetABadge()
    {
        FileEntry plain = fileNamed(QStringLiteral("notes.txt"));
        FileEntry link = plain;
        link.isSymlink = true;
        link.linkTarget = QStringLiteral("/elsewhere/notes.txt");

        QCOMPARE(IconProvider::kindOf(link), Kind::Text);

        // A link whose own name says nothing is drawn as its target's kind.
        FileEntry bare = fileNamed(QStringLiteral("current"));
        bare.isSymlink = true;
        bare.linkTarget = QStringLiteral("releases/v2/paper.pdf");
        QCOMPARE(IconProvider::kindOf(bare), Kind::Pdf);

        // A dangling link's mode is the link's own rwxrwxrwx, which says
        // nothing about a target that does not exist.
        FileEntry broken = fileNamed(QStringLiteral("dangling"));
        broken.isSymlink = true;
        broken.isBroken = true;
        broken.isExecutable = true;
        broken.linkTarget = QStringLiteral("missing");
        QCOMPARE(IconProvider::kindOf(broken), Kind::Generic);

        const QImage plainImage =
            imageOf(IconProvider::instance().iconFor(plain, Qt::black), 32, 1.0);
        const QImage linkImage =
            imageOf(IconProvider::instance().iconFor(link, Qt::black), 32, 1.0);
        QVERIFY(plainImage != linkImage);
        QVERIFY(opaquePixels(linkImage) > 100);
    }

    /// QIcon::Disabled, which the delegate uses for a broken symlink, is the
    /// same glyph at lower opacity.
    void disabledIsDimmer()
    {
        const QIcon icon = IconProvider::instance().iconFor(Kind::Text, Qt::black);
        const int normal = strongestPixel(imageOf(icon, 16, 1.0)).alpha();
        const int disabled = strongestPixel(imageOf(icon, 16, 1.0, QIcon::Disabled)).alpha();
        QVERIFY(disabled < normal);
        QVERIFY(disabled > 0);
    }

    void kindOfClassifies_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("isDir");
        QTest::addColumn<bool>("isExecutable");
        QTest::addColumn<int>("kind");

        const auto row = [](const char *tag, const char *name, Kind kind, bool isDir = false,
                            bool isExecutable = false) {
            QTest::newRow(tag) << QString::fromUtf8(name) << isDir << isExecutable
                               << static_cast<int>(kind);
        };

        row("directory", "src", Kind::Directory, true);
        // A directory is a directory whatever it is called, and every one has
        // its execute bit set.
        row("directory named like an archive", "backup.zip", Kind::Directory, true, true);
        row("generic", "blob.xyz123", Kind::Generic);
        row("text", "notes.txt", Kind::Text);
        row("readme", "README", Kind::Text);
        row("markdown", "README.md", Kind::Markdown);
        row("c++", "main.cpp", Kind::Code);
        row("typescript, not mpeg-ts", "index.ts", Kind::Code);
        row("makefile", "Makefile", Kind::Code);
        row("cmakelists is source, not prose", "CMakeLists.txt", Kind::Code);
        row("image, any case", "Photo.JPG", Kind::Image);
        row("audio", "track.flac", Kind::Audio);
        row("video", "clip.mkv", Kind::Video);
        row("pdf", "paper.pdf", Kind::Pdf);
        row("document", "letter.docx", Kind::Document);
        row("spreadsheet", "budget.ods", Kind::Spreadsheet);
        row("csv", "data.csv", Kind::Spreadsheet);
        row("presentation", "deck.pptx", Kind::Presentation);
        row("archive", "src.tar.gz", Kind::Archive);
        row("disk image", "arch.iso", Kind::DiskImage);
        row("package", "app.deb", Kind::Package);
        row("appimage", "Tool.AppImage", Kind::Package);
        row("shell script", "build.sh", Kind::Executable);
        row("bare executable", "pf", Kind::Executable, false, true);
        // The executable bit every file on a FAT mount has does not override a
        // suffix that says what the file is.
        row("executable bit on text", "notes.txt", Kind::Text, false, true);
        row("font", "Inter.woff2", Kind::Font);
        row("web", "index.html", Kind::Web);
        row("database", "app.sqlite3", Kind::Database);
        row("config", "Cargo.toml", Kind::Config);
        row("json", "package.json", Kind::Config);
        row("known dotfile", ".bashrc", Kind::Config);
        row("unknown dotfile", ".somethingrc_history", Kind::Config);
        row("gitignore", ".gitignore", Kind::Config);
    }

    void kindOfClassifies()
    {
        QFETCH(QString, name);
        QFETCH(bool, isDir);
        QFETCH(bool, isExecutable);
        QFETCH(int, kind);

        FileEntry entry = fileNamed(name);
        entry.isDir = isDir;
        entry.isExecutable = isExecutable;
        QCOMPARE(static_cast<int>(IconProvider::kindOf(entry)), kind);
    }

    /// A MIME type is used when the entry already carries one and the name
    /// said nothing — an extensionless file that was sniffed.
    void kindOfUsesAKnownMimeType()
    {
        FileEntry entry = fileNamed(QStringLiteral("cover"));
        entry.mimeName = QStringLiteral("image/jpeg");
        QCOMPARE(IconProvider::kindOf(entry), Kind::Image);

        QCOMPARE(IconProvider::kindForMimeType(QStringLiteral("application/pdf")), Kind::Pdf);
        QCOMPARE(IconProvider::kindForMimeType(QStringLiteral("application/x-shellscript")),
                 Kind::Executable);
        QCOMPARE(IconProvider::kindForMimeType(QStringLiteral("text/x-c++src")), Kind::Code);
        QCOMPARE(IconProvider::kindForMimeType(QStringLiteral("application/x-compressed-tar")),
                 Kind::Archive);
        QCOMPARE(IconProvider::kindForMimeType(QStringLiteral(
                     "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet")),
                 Kind::Spreadsheet);
        QCOMPARE(IconProvider::kindForMimeType(QStringLiteral("application/octet-stream")),
                 Kind::Generic);
    }

    /// The chrome glyphs the sidebar, settings and Quick Look ask for by name.
    void symbolicGlyphsExist_data()
    {
        QTest::addColumn<QString>("name");
        for (const char *name : {"home",
                                 "computer-desktop",
                                 "arrow-down-tray",
                                 "document-text",
                                 "photo",
                                 "musical-note",
                                 "film",
                                 "folder",
                                 "folder-open",
                                 "star",
                                 "server",
                                 "server-stack",
                                 "eject",
                                 "trash",
                                 "magnifying-glass",
                                 "x-mark",
                                 "cog-6-tooth",
                                 "swatch",
                                 "eye",
                                 "command-line",
                                 "exclamation-triangle",
                                 "arrow-path",
                                 "chevron-right"}) {
            QTest::newRow(name) << QString::fromLatin1(name);
        }
    }

    void symbolicGlyphsExist()
    {
        QFETCH(QString, name);
        QVERIFY(SymbolicIcon::exists(name));

        const QColor tint(0xcc, 0x33, 0x66);
        const QPixmap pixmap = SymbolicIcon::pixmap(name, tint, 16, 2.0);
        QCOMPARE(pixmap.size(), QSize(32, 32));
        QCOMPARE(pixmap.devicePixelRatio(), 2.0);

        const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(opaquePixels(image) > 10);
        QColor strongest = strongestPixel(image);
        strongest.setAlpha(255);
        QCOMPARE(strongest, tint);

        QVERIFY(!SymbolicIcon::icon(name, tint).isNull());
    }

    void unknownSymbolicGlyphIsNull()
    {
        QVERIFY(!SymbolicIcon::exists(QStringLiteral("no-such-glyph")));
        QVERIFY(SymbolicIcon::icon(QStringLiteral("no-such-glyph"), Qt::black).isNull());
        QVERIFY(SymbolicIcon::pixmap(QStringLiteral("no-such-glyph"), Qt::black, 16, 1.0).isNull());
    }
};

QTEST_MAIN(TestIcons)
#include "tst_icons.moc"
