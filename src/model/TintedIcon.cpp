#include "model/TintedIcon.h"

#include <QFile>
#include <QIconEngine>
#include <QImage>
#include <QImageReader>
#include <QPaintDevice>
#include <QPainter>
#include <QPixmapCache>

#include <utility>

namespace pf {
namespace {

QString cacheKey(const QList<TintedLayer> &layers, const QColor &colour, const QSize &logicalSize,
                 qreal ratio, QIcon::Mode mode)
{
    // The ratio is part of the key even though 16px at 2x and 32px at 1x are
    // the same pixels: a pixmap's ratio is set once, before it is cached, and
    // changing it on the way out would detach — copying the pixels on every
    // paint to save one render per size.
    QString key = QStringLiteral("pf-tinted:%1:%2x%3@%4:%5")
                      .arg(colour.name(QColor::HexArgb))
                      .arg(logicalSize.width())
                      .arg(logicalSize.height())
                      .arg(ratio)
                      .arg(mode == QIcon::Disabled ? 1 : 0);
    for (const TintedLayer &layer : layers) {
        key += QLatin1Char(layer.erase ? '-' : '+');
        key += layer.resource;
    }
    return key;
}

/// One layer at exactly `deviceSize` pixels.
///
/// QImageReader rather than QIcon or QSvgRenderer: it reaches the svg plugin
/// without linking QtSvg (see the header), and setScaledSize() makes the
/// plugin rasterise at the target size rather than at the SVG's nominal one —
/// which is the whole point of the 16px grid the file glyphs are drawn on.
QImage renderLayer(const QString &resource, const QSize &deviceSize)
{
    QImageReader reader(resource, "svg");
    reader.setScaledSize(deviceSize);
    QImage image = reader.read();
    if (image.isNull()) {
        return image;
    }
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

/// Backs the QIcon returned by TintedIcon::icon(), so that it renders at the
/// size and ratio it is actually painted at instead of being scaled from a
/// pixmap rendered for some other one.
class TintedIconEngine final : public QIconEngine
{
public:
    TintedIconEngine(QList<TintedLayer> layers, QColor colour)
        : m_layers(std::move(layers)), m_colour(colour)
    {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State /*state*/) override
    {
        // The painter's device knows the real ratio: a HiDPI screen, or a
        // pixmap being rendered at 2x for a screenshot. Asking it, rather than
        // qApp, is what keeps an icon painted into an offscreen buffer crisp.
        const qreal ratio =
            painter->device() != nullptr ? painter->device()->devicePixelRatio() : 1.0;
        const QPixmap rendered = render(rect.size(), mode, ratio);
        if (!rendered.isNull()) {
            painter->drawPixmap(rect.topLeft(), rendered);
        }
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State /*state*/) override
    {
        return render(size, mode, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State /*state*/,
                         qreal scale) override
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
        // Before 6.8, QIcon::pixmap() hands the engine the size already
        // multiplied by the ratio; from 6.8 on it is the logical size.
        if (scale > 0) {
            return render(size / scale, mode, scale);
        }
#endif
        return render(size, mode, scale);
    }

    QSize actualSize(const QSize &size, QIcon::Mode /*mode*/, QIcon::State /*state*/) override
    {
        // Square and scalable: whatever square fits is the size.
        const int side = qMin(size.width(), size.height());
        return {side, side};
    }

    QList<QSize> availableSizes(QIcon::Mode /*mode*/, QIcon::State /*state*/) override
    {
        // The two the glyphs are drawn for. Anything else still renders; this
        // is only what a caller asking "which sizes are native" should hear.
        return {QSize(16, 16), QSize(32, 32)};
    }

    QString key() const override { return QStringLiteral("pf-tinted"); }

    QIconEngine *clone() const override { return new TintedIconEngine(m_layers, m_colour); }

    bool isNull() override { return m_layers.isEmpty(); }

private:
    QPixmap render(const QSize &logicalSize, QIcon::Mode mode, qreal scale) const
    {
        return TintedIcon::pixmap(m_layers, m_colour, logicalSize, scale, mode);
    }

    QList<TintedLayer> m_layers;
    QColor m_colour;
};

} // namespace

QPixmap TintedIcon::pixmap(const QList<TintedLayer> &layers, const QColor &colour,
                           const QSize &logicalSize, qreal devicePixelRatio, QIcon::Mode mode)
{
    if (layers.isEmpty() || logicalSize.isEmpty() || devicePixelRatio <= 0) {
        return {};
    }

    // Rounded rather than truncated, so a fractional ratio — 16px at 1.25 is
    // 20 — does not lose a device pixel and paint a hair small.
    const QSize deviceSize(qRound(logicalSize.width() * devicePixelRatio),
                           qRound(logicalSize.height() * devicePixelRatio));

    const QString key = cacheKey(layers, colour, logicalSize, devicePixelRatio, mode);
    if (QPixmap cached; QPixmapCache::find(key, &cached)) {
        return cached;
    }

    QImage canvas(deviceSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        for (const TintedLayer &layer : layers) {
            const QImage rendered = renderLayer(layer.resource, deviceSize);
            if (rendered.isNull()) {
                // A missing layer is a packaging bug, and half an icon hides
                // it better than none does. Nothing is cached, so it is not
                // remembered either.
                return {};
            }
            painter.setCompositionMode(layer.erase ? QPainter::CompositionMode_DestinationOut
                                                   : QPainter::CompositionMode_SourceOver);
            painter.drawImage(0, 0, rendered);
        }

        // SourceIn with a translucent colour scales every pixel's alpha by the
        // colour's, which is how Disabled dims without a second pass.
        QColor fill = colour;
        if (mode == QIcon::Disabled) {
            fill.setAlphaF(fill.alphaF() * kDisabledOpacity);
        }
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(canvas.rect(), fill);
    }

    QPixmap result = QPixmap::fromImage(std::move(canvas));
    result.setDevicePixelRatio(devicePixelRatio);
    QPixmapCache::insert(key, result);
    return result;
}

QIcon TintedIcon::icon(const QList<TintedLayer> &layers, const QColor &colour)
{
    if (layers.isEmpty() || !QFile::exists(layers.first().resource)) {
        return {};
    }
    return QIcon(new TintedIconEngine(layers, colour));
}

} // namespace pf
