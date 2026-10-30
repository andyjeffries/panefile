#include "model/FolderIconRenderer.h"

#include "model/FolderIconStore.h"

#include <QBuffer>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPaintDevice>
#include <QPainter>
#include <QPixmapCache>

#include <algorithm>

namespace pf {
namespace {

QColor mix(const QColor &from, const QColor &towards, double amount)
{
    return QColor::fromRgbF(from.redF() + (towards.redF() - from.redF()) * amount,
                            from.greenF() + (towards.greenF() - from.greenF()) * amount,
                            from.blueF() + (towards.blueF() - from.blueF()) * amount);
}

QString hex(const QColor &colour)
{
    return colour.name(QColor::HexRgb);
}

// The 64-unit master, from the design generator (design/_build/common.py).
const char *const kBack64 =
    "M11 9H53A7 7 0 0 1 60 16V50A7 7 0 0 1 53 57H11A7 7 0 0 1 4 50V16A7 7 0 0 1 11 9Z";
const char *const kPaper64 = "M11.5 12.5H52.5A2 2 0 0 1 54.5 14.5V40H9.5V14.5A2 2 0 0 1 11.5 12.5Z";
constexpr double kOpen64 = 23;
constexpr double kClosed64 = 12.5;

QString front64(double top)
{
    const double y = top + 5;
    return QStringLiteral(
               "M4 %1A4.5 4.5 0 0 1 8.5 %2H21.6A4 4 0 0 1 24.6 %3L26.6 %4A4 4 0 0 0 29.6 "
               "%5H55.5A4.5 4.5 0 0 1 60 %6V50A7 7 0 0 1 53 57H11A7 7 0 0 1 4 50Z")
        .arg(top + 4.5)
        .arg(top)
        .arg(top + 1.4)
        .arg(top + 3.6)
        .arg(y)
        .arg(y + 4.5);
}

QString front64Top(double top)
{
    const double t = top + 0.4;
    const double y = top + 5.4;
    return QStringLiteral("M4.4 %1A4.1 4.1 0 0 1 8.5 %2H21.6A3.6 3.6 0 0 1 24.3 %3L26.3 %4A4.4 4.4 "
                          "0 0 0 29.6 %5H55.5A4.1 4.1 0 0 1 59.6 %6")
        .arg(t + 4.1)
        .arg(t)
        .arg(t + 1.3)
        .arg(t + 3.5)
        .arg(y)
        .arg(y + 4.1);
}

// The 16-unit master for list rows.
const char *const kBack16 = "M2.5 2H13.5A1.5 1.5 0 0 1 15 3.5V12.5A1.5 1.5 0 0 1 13.5 14H2.5A1.5 "
                            "1.5 0 0 1 1 12.5V3.5A1.5 1.5 0 0 1 2.5 2Z";
const char *const kPaper16 = "M3 3.3H13V8H3Z";
constexpr double kOpen16 = 5;
constexpr double kClosed16 = 3.2;

QString front16(double top)
{
    const double y = top + 1.4;
    return QStringLiteral("M1 %1A1 1 0 0 1 2 %2H5.4A1 1 0 0 1 6.1 %3L6.9 %4A1 1 0 0 0 7.6 %5H14A1 "
                          "1 0 0 1 15 %6V12.5A1.5 1.5 0 0 1 13.5 14H2.5A1.5 1.5 0 0 1 1 12.5Z")
        .arg(top + 1)
        .arg(top)
        .arg(top + 0.3)
        .arg(top + 1.1)
        .arg(y)
        .arg(y + 1);
}

QString front16Top(double top)
{
    const double t = top + 0.5;
    const double y = top + 1.9;
    return QStringLiteral("M1.5 %1A.6 .6 0 0 1 2 %2H5.4L6.6 %3A1 1 0 0 0 7.6 %4H14.5")
        .arg(t + 0.4)
        .arg(t)
        .arg(t + 1.1)
        .arg(y);
}

/// A Heroicons file's content between its <svg> tags.
QByteArray innerOf(const QByteArray &file)
{
    const qsizetype open = file.indexOf("<svg");
    if (open < 0) {
        return {};
    }
    const qsizetype start = file.indexOf('>', open);
    const qsizetype end = file.lastIndexOf("</svg>");
    if (start < 0 || end < start) {
        return {};
    }
    return file.mid(start + 1, end - start - 1).trimmed();
}

/// Light themes get whiter paper.
bool isLight(const QColor &fade)
{
    return fade.isValid() && fade.lightnessF() > 0.5;
}

QString cacheKey(const FolderIconSpec &spec, const QSize &deviceSize, QIcon::Mode mode)
{
    return QStringLiteral("pf-folder:%1:%2:%3:%4%5:%6x%7:%8")
        .arg(spec.glyphId, spec.tint.name(QColor::HexArgb),
             spec.fade.isValid() ? spec.fade.name(QColor::HexArgb) : QStringLiteral("-"),
             spec.empty ? QStringLiteral("e") : QStringLiteral("f"),
             spec.linked ? QStringLiteral("l") : QStringLiteral("n"))
        .arg(deviceSize.width())
        .arg(deviceSize.height())
        .arg(mode == QIcon::Disabled ? 1 : 0);
}

QImage renderResource(const QString &resource, const QSize &size)
{
    QImageReader reader(resource, "svg");
    reader.setScaledSize(size);
    return reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

class FolderIconEngine final : public QIconEngine
{
public:
    explicit FolderIconEngine(FolderIconSpec spec) : m_spec(std::move(spec)) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State /*state*/) override
    {
        const qreal ratio =
            painter->device() != nullptr ? painter->device()->devicePixelRatio() : 1.0;
        const QPixmap rendered = FolderIconRenderer::pixmap(m_spec, rect.size(), ratio, mode);
        if (!rendered.isNull()) {
            painter->drawPixmap(rect.topLeft(), rendered);
        }
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State /*state*/) override
    {
        return FolderIconRenderer::pixmap(m_spec, size, 1.0, mode);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State /*state*/,
                         qreal scale) override
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        if (scale > 0) {
            return FolderIconRenderer::pixmap(m_spec, size / scale, scale, mode);
        }
#endif
        return FolderIconRenderer::pixmap(m_spec, size, scale, mode);
    }

    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override
    {
        const int side = qMin(size.width(), size.height());
        return {side, side};
    }

    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override
    {
        return {QSize(16, 16), QSize(32, 32)};
    }

    QString key() const override { return QStringLiteral("pf-folder"); }
    QIconEngine *clone() const override { return new FolderIconEngine(m_spec); }
    bool isNull() override { return false; }

private:
    FolderIconSpec m_spec;
};

} // namespace

QByteArray FolderIconRenderer::svg(const FolderIconSpec &spec, int devicePixels)
{
    const bool large = devicePixels > 20;
    const QColor tint = spec.tint;
    const auto fadeTo = [&spec](const QColor &colour) {
        return hex(spec.fade.isValid() ? mix(colour, spec.fade, 0.30) : colour);
    };
    const QColor black(Qt::black);
    const QColor white(Qt::white);
    const bool light = isLight(spec.fade);

    const double u = large ? 64.0 : 16.0;
    const double top =
        large ? (spec.empty ? kClosed64 : kOpen64) : (spec.empty ? kClosed16 : kOpen16);
    const QString back = QString::fromLatin1(large ? kBack64 : kBack16);
    const QString paper = QString::fromLatin1(large ? kPaper64 : kPaper16);
    const QString front = large ? front64(top) : front16(top);
    const QString frontTop = large ? front64Top(top) : front16Top(top);
    const double bodyTop = top + (large ? 5.0 : 1.4);
    const double bodyH = (large ? 57.0 : 14.0) - bodyTop;
    const double bodyCy = bodyTop + (bodyH / 2);
    const double one = u / std::max(1, devicePixels);

    const QString paperTop =
        fadeTo(QColor(light ? QStringLiteral("#ffffff") : QStringLiteral("#f6f5f1")));
    const QString paperBottom =
        fadeTo(QColor(light ? QStringLiteral("#e9e9e4") : QStringLiteral("#dddbd4")));

    QString s;
    s += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %1 %1\">").arg(u);
    s += QStringLiteral(
             "<defs>"
             "<linearGradient id=\"b\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" "
             "stop-color=\"%1\"/><stop offset=\"1\" stop-color=\"%2\"/></linearGradient>"
             "<linearGradient id=\"p\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\"0\" "
             "stop-color=\"%3\"/><stop offset=\"1\" stop-color=\"%4\"/></linearGradient>"
             "<radialGradient id=\"f\" cx=\".34\" cy=\".38\" r=\".85\"><stop offset=\"0\" "
             "stop-color=\"%5\"/><stop offset=\".55\" stop-color=\"%6\"/><stop offset=\"1\" "
             "stop-color=\"%7\"/></radialGradient>"
             "<linearGradient id=\"s\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\"><stop offset=\".6\" "
             "stop-color=\"#000\" stop-opacity=\"0\"/><stop offset=\"1\" stop-color=\"#000\" "
             "stop-opacity=\".16\"/></linearGradient>"
             "</defs>")
             .arg(fadeTo(mix(tint, black, 0.12)), fadeTo(mix(tint, black, 0.30)), paperTop,
                  paperBottom, fadeTo(mix(tint, white, 0.24)), fadeTo(tint),
                  fadeTo(mix(tint, black, 0.10)));

    s += QStringLiteral("<path d=\"%1\" fill=\"url(#b)\"/>").arg(back);
    if (large) {
        s += QStringLiteral(
                 "<path d=\"M6 13.5A5 5 0 0 1 11 9.6H53A5 5 0 0 1 58 13.5\" fill=\"none\" "
                 "stroke=\"%1\" stroke-width=\"1\" stroke-opacity=\".55\"/>")
                 .arg(fadeTo(mix(tint, white, 0.35)));
    }
    if (!spec.empty) {
        s += QStringLiteral(
                 "<path d=\"%1\" fill=\"#000\" fill-opacity=\".18\" transform=\"translate(%2 "
                 "%2)\"/>")
                 .arg(paper)
                 .arg(one * 0.6);
        s += QStringLiteral("<path d=\"%1\" fill=\"url(#p)\"/>").arg(paper);
    }
    // The flap's own shape, stepped upwards, is the shadow it casts on the sheet.
    const auto shadow = [&](double dy, double opacity) {
        s += QStringLiteral(
                 "<path d=\"%1\" fill=\"#000\" fill-opacity=\"%2\" transform=\"translate(0 "
                 "%3)\"/>")
                 .arg(front)
                 .arg(opacity)
                 .arg(-dy);
    };
    if (large) {
        shadow(1, 0.14);
        shadow(2, 0.09);
        shadow(3, 0.05);
    } else {
        shadow(one, 0.22);
    }
    s += QStringLiteral("<path d=\"%1\" fill=\"url(#f)\"/><path d=\"%1\" fill=\"url(#s)\"/>")
             .arg(front);
    if (large) {
        s += QStringLiteral("<path d=\"%1\" fill=\"none\" stroke=\"%2\" stroke-width=\"%3\" "
                            "stroke-opacity=\".85\" stroke-linecap=\"round\"/>")
                 .arg(frontTop, fadeTo(mix(tint, white, 0.55)))
                 .arg(std::max(0.8, one));
        s += QStringLiteral("<path d=\"M5 52A6 6 0 0 0 11 56.5H53A6 6 0 0 0 59 52\" fill=\"none\" "
                            "stroke=\"%1\" stroke-width=\"%2\" stroke-opacity=\".6\"/>")
                 .arg(fadeTo(mix(tint, black, 0.35)))
                 .arg(std::max(0.8, one));
    } else {
        s += QStringLiteral("<path d=\"%1\" fill=\"none\" stroke=\"%2\" stroke-width=\"%3\" "
                            "stroke-opacity=\".9\"/>")
                 .arg(frontTop, fadeTo(mix(tint, white, 0.5)))
                 .arg(one);
    }

    if (!spec.glyphId.isEmpty()) {
        const QByteArray inner = innerOf(FolderIconStore::instance().svgFor(spec.glyphId));
        if (!inner.isEmpty()) {
            const double px = std::max(7.0, devicePixels * (large ? 0.40 : 0.44));
            const double units = std::min(px * u / std::max(1, devicePixels), bodyH * 0.82);
            const double scale = units / 24.0;
            const double x = (u / 2) - (units / 2);
            const double y = bodyCy - (units / 2);
            const auto glyph = [&](double dy, const QColor &colour, double opacity) {
                s += QStringLiteral("<g transform=\"translate(%1 %2) scale(%3)\" fill=\"%4\" "
                                    "opacity=\"%5\">")
                         .arg(x)
                         .arg(y + dy)
                         .arg(scale)
                         .arg(fadeTo(colour))
                         .arg(opacity);
                s += QString::fromUtf8(inner);
                s += QStringLiteral("</g>");
            };
            // The flap is opaque, so one recipe serves light and dark themes.
            glyph(one, mix(tint, white, 0.45), 0.55);
            glyph(0, mix(tint, black, 0.38), 0.80);
        }
    }

    s += QStringLiteral("</svg>");
    return s.toUtf8();
}

QPixmap FolderIconRenderer::pixmap(const FolderIconSpec &spec, const QSize &logicalSize,
                                   qreal devicePixelRatio, QIcon::Mode mode)
{
    if (logicalSize.isEmpty() || devicePixelRatio <= 0 || !spec.tint.isValid()) {
        return {};
    }
    const QSize deviceSize(qRound(logicalSize.width() * devicePixelRatio),
                           qRound(logicalSize.height() * devicePixelRatio));
    const QString key = cacheKey(spec, deviceSize, mode);
    if (QPixmap cached; QPixmapCache::find(key, &cached)) {
        return cached;
    }

    QByteArray bytes = svg(spec, std::min(deviceSize.width(), deviceSize.height()));
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "svg");
    reader.setScaledSize(deviceSize);
    QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (spec.linked) {
        // The same halo-then-arrow the file glyphs use, in the folder's colour.
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.drawImage(
            0, 0, renderResource(QStringLiteral(":/icons/files/link-cutout.svg"), deviceSize));
        QImage badge = renderResource(QStringLiteral(":/icons/files/link-badge.svg"), deviceSize);
        QPainter badgePainter(&badge);
        badgePainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        badgePainter.fillRect(badge.rect(),
                              spec.fade.isValid() ? mix(spec.tint, spec.fade, 0.3) : spec.tint);
        badgePainter.end();
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(0, 0, badge);
    }

    if (mode == QIcon::Disabled) {
        QImage dimmed(image.size(), QImage::Format_ARGB32_Premultiplied);
        dimmed.fill(Qt::transparent);
        QPainter painter(&dimmed);
        painter.setOpacity(0.45);
        painter.drawImage(0, 0, image);
        painter.end();
        image = dimmed;
    }

    QPixmap result = QPixmap::fromImage(std::move(image));
    result.setDevicePixelRatio(devicePixelRatio);
    QPixmapCache::insert(key, result);
    return result;
}

QIcon FolderIconRenderer::icon(const FolderIconSpec &spec)
{
    return QIcon(new FolderIconEngine(spec));
}

} // namespace pf
