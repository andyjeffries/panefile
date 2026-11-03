#include "model/FolderIconImage.h"

#include <QBuffer>
#include <QDir>
#include <QMimeData>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace pf;

namespace {

QImage canvas(const QColor &background, int side = 200)
{
    QImage image(side, side, QImage::Format_ARGB32);
    image.fill(background);
    return image;
}

int at(const QImage &mask, int x, int y)
{
    return qGray(mask.pixel(x, y)) & 0xff;
}

QByteArray pngBytes(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QImage disc(const QColor &background, const QColor &shape)
{
    QImage image = canvas(background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(shape);
    painter.drawEllipse(QRect(50, 50, 100, 100));
    return image;
}

QImage plate(const QColor &top, const QColor &bottom, int inset, int radius, bool logo = true)
{
    QImage image = canvas(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    QLinearGradient gradient(0, inset, 0, 200 - inset);
    gradient.setColorAt(0, top);
    gradient.setColorAt(1, bottom);
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRect(inset, inset, 200 - (2 * inset), 200 - (2 * inset)), radius,
                            radius);
    if (logo) {
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRect(50, 50, 100, 100));
    }
    return image;
}

} // namespace

class TestFolderIconImage : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void darkDiscOnTransparent()
    {
        const QImage mask = FolderIconImage::toMask(disc(Qt::transparent, Qt::black), false);
        QCOMPARE(mask.size(), QSize(128, 128));
        QCOMPARE(mask.format(), QImage::Format_Grayscale8);
        QVERIFY(at(mask, 64, 64) > 240);
        QVERIFY(at(mask, 2, 2) < 10);
        // Trimmed and centred, with a margin.
        QVERIFY(at(mask, 64, 14) > 200);
        QVERIFY(at(mask, 64, 4) < 10);
    }

    void whiteDiscOnTransparent()
    {
        const QImage mask = FolderIconImage::toMask(disc(Qt::transparent, Qt::white), false);
        QVERIFY(at(mask, 64, 64) > 240);
        QVERIFY(at(mask, 2, 2) < 10);
    }

    void holeInsideAColouredSquare()
    {
        QImage image = canvas(Qt::transparent);
        QPainter painter(&image);
        painter.fillRect(QRect(40, 40, 120, 120), QColor(30, 90, 200));
        painter.fillRect(QRect(85, 85, 30, 30), Qt::white);
        painter.end();
        const QImage mask = FolderIconImage::toMask(image, false);
        QVERIFY(at(mask, 20, 20) > 240);
        QVERIFY(at(mask, 64, 64) < 10);
    }

    void logoOnTransparentPlate()
    {
        const QImage mask =
            FolderIconImage::toMask(plate(QColor(200, 100, 60), QColor(200, 100, 60), 12, 30),
                                    false);
        QVERIFY(at(mask, 64, 64) > 240);
        QVERIFY(at(mask, 14, 14) < 10);
    }

    void gradientPlateLeavesNoOutline()
    {
        const QImage mask =
            FolderIconImage::toMask(plate(QColor(31, 196, 111), QColor(0, 135, 63), 12, 30),
                                    false);
        QVERIFY(at(mask, 64, 64) > 240);
        QVERIFY(at(mask, 14, 14) < 10);
        QVERIFY(at(mask, 64, 14) > 200);
        QVERIFY(at(mask, 64, 4) < 10);
    }

    void plateFlushToEdges()
    {
        const QImage mask =
            FolderIconImage::toMask(plate(QColor(40, 60, 160), QColor(40, 60, 160), 0, 50),
                                    false);
        QVERIFY(at(mask, 64, 64) > 240);
        QVERIFY(at(mask, 14, 14) < 10);
        QVERIFY(at(mask, 2, 2) < 10);
    }

    void emptyPlateFallsBack()
    {
        const QImage mask = FolderIconImage::toMask(
            plate(QColor(200, 100, 60), QColor(200, 100, 60), 12, 30, false), false);
        QVERIFY(!mask.isNull());
        QVERIFY(at(mask, 64, 64) > 240);
    }

    void invertOnPlateKeepsSurroundEmpty()
    {
        const QImage mask = FolderIconImage::toMask(
            plate(QColor(200, 100, 60), QColor(200, 100, 60), 12, 30), true);
        QVERIFY(at(mask, 64, 64) < 10);
        QVERIFY(at(mask, 20, 64) > 240);
        QVERIFY(at(mask, 2, 2) < 10);
    }

    void shapeOnOpaqueBackgrounds()
    {
        for (const QColor &background : {QColor(Qt::white), QColor(200, 30, 30)}) {
            const QImage mask = FolderIconImage::toMask(disc(background, Qt::black), false);
            QVERIFY(at(mask, 64, 64) > 240);
            QVERIFY(at(mask, 2, 2) < 10);
        }
    }

    void uniformImageHasNoShape()
    {
        QVERIFY(FolderIconImage::toMask(canvas(Qt::white), false).isNull());
        QVERIFY(FolderIconImage::toMask(canvas(Qt::white, 1), false).isNull());
    }

    void invertFlipsCoverage()
    {
        const QImage image = disc(Qt::transparent, Qt::black);
        const QImage plain = FolderIconImage::toMask(image, false);
        const QImage flipped = FolderIconImage::toMask(image, true);
        QCOMPARE(flipped.size(), QSize(128, 128));
        QVERIFY(at(plain, 64, 64) > 240);
        QVERIFY(at(flipped, 64, 64) < 10);
        QVERIFY(FolderIconImage::idFor(plain) != FolderIconImage::idFor(flipped));
    }

    void idIsContentAddressed()
    {
        const QImage mask = FolderIconImage::toMask(disc(Qt::transparent, Qt::black), false);
        const QString id = FolderIconImage::idFor(mask);
        QVERIFY(QRegularExpression(QStringLiteral("^img:[0-9a-f]{32}$")).match(id).hasMatch());
        QCOMPARE(FolderIconImage::idFor(mask.copy()), id);
    }

    void clipboardPicksTheLastImageFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("sub")));
        const auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(bytes);
        };
        write(QStringLiteral("a.txt"), "hello");
        write(QStringLiteral("b.png"), pngBytes(disc(Qt::transparent, Qt::black)));
        write(QStringLiteral("c.png"), pngBytes(disc(Qt::transparent, Qt::black)));

        QMimeData mime;
        QList<QUrl> urls;
        for (const QString &name : {QStringLiteral("sub"), QStringLiteral("a.txt"),
                                    QStringLiteral("b.png"), QStringLiteral("c.png")}) {
            urls << QUrl::fromLocalFile(dir.filePath(name));
        }
        mime.setUrls(urls);
        QString origin;
        QVERIFY(!FolderIconImage::sourceFromClipboard(&mime, &origin).isNull());
        QCOMPARE(origin, QStringLiteral("c.png"));
    }

    void clipboardWithoutImagesIsNull()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("a.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(file.fileName()), QUrl(QStringLiteral("https://x/y.png"))});
        QVERIFY(FolderIconImage::sourceFromClipboard(&mime).isNull());
        QVERIFY(FolderIconImage::sourceFromClipboard(nullptr).isNull());
    }

    void clipboardRawPng()
    {
        QMimeData mime;
        mime.setData(QStringLiteral("image/png"), pngBytes(disc(Qt::transparent, Qt::black)));
        QString origin;
        const QImage image = FolderIconImage::sourceFromClipboard(&mime, &origin);
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(200, 200));
        QCOMPARE(origin, QStringLiteral("clipboard image"));
    }

    void wideSvgKeepsItsAspectRatio()
    {
        const QByteArray svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 200 100\">"
            "<rect x=\"20\" y=\"10\" width=\"160\" height=\"80\" fill=\"black\"/></svg>";
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("wide.svg")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(svg);
        file.close();

        QMimeData fromFile;
        fromFile.setUrls({QUrl::fromLocalFile(file.fileName())});
        QMimeData fromBytes;
        fromBytes.setData(QStringLiteral("image/svg+xml"), svg);
        for (const QMimeData *mime : {&fromFile, &fromBytes}) {
            const QImage image = FolderIconImage::sourceFromClipboard(mime);
            QVERIFY(!image.isNull());
            QCOMPARE(image.width(), 2 * image.height());

            // Trimmed and centred, a 2:1 shape spans the mask's width but
            // only half its height.
            const QImage mask = FolderIconImage::toMask(image, false);
            QVERIFY(at(mask, 64, 64) > 240);
            QVERIFY(at(mask, 64, 20) < 10);
            QVERIFY(at(mask, 64, 108) < 10);
        }
    }

    void oversizedHeaderIsRefused()
    {
        QByteArray bytes = pngBytes(canvas(Qt::white, 8));
        // IHDR width and height sit at bytes 16..23; the CRC is not checked on
        // the header by the size probe.
        const char big[8] = {0, 1, char(0x86), char(0xa0), 0, 1, char(0x86), char(0xa0)};
        bytes.replace(16, 8, QByteArray(big, 8));
        QMimeData mime;
        mime.setData(QStringLiteral("image/png"), bytes);
        QVERIFY(FolderIconImage::sourceFromClipboard(&mime).isNull());
    }
};

QTEST_MAIN(TestFolderIconImage)
#include "tst_foldericonimage.moc"
