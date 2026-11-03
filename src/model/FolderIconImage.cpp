#include "model/FolderIconImage.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QPainter>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace pf::FolderIconImage {
namespace {

/// Refused before decoding: either side beyond this many pixels.
constexpr int kMaxSide = 8192;
/// Qt's decode allocation limit, in megabytes.
constexpr int kAllocationLimitMb = 128;
/// SVGs have no pixel size; they are rasterised to this square.
constexpr int kSvgSide = 512;
/// The picture is scaled to fit this square before analysis.
constexpr int kWorkSide = 512;
/// A border pixel with less alpha than this counts as transparent.
constexpr int kBorderTransparentAlpha = 32;
/// A border ring this transparent means the picture is transparent-backed.
constexpr double kTransparentBorderShare = 0.5;
/// Only pixels at least this opaque decide whether the logo is light or dark.
constexpr int kOpaqueAlpha = 128;
/// A mean luminance above this means a light logo, which gets a black backdrop.
constexpr double kLightLogoLuminance = 0.6;
/// Coverage ramps from 0 to 1 between these two colour differences (0..1).
constexpr double kEdgeLow = 0.10;
constexpr double kEdgeHigh = 0.25;
/// Pixels with more coverage than this count towards the bounding box.
constexpr double kBoundsCoverage = 0.1;
/// A plate is looked for within this share of the picture's side from each edge.
constexpr int kPlateReachDivisor = 8;
/// Plate samples differing by more than this in any channel (0..1) are not one plate.
constexpr double kPlateSpread = 0.30;
/// Empty margin on each side of the trimmed shape, as a share of the mask.
constexpr double kMargin = 1.0 / 12.0;

double smoothstep(double low, double high, double x)
{
    const double t = std::clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

/// Decodes with the size guards. Null when refused or undecodable.
QImage decode(QImageReader &reader, bool svg)
{
    reader.setAutoTransform(true);
    reader.setAllocationLimit(kAllocationLimitMb);
    if (svg) {
        reader.setScaledSize({kSvgSide, kSvgSide});
    } else {
        const QSize size = reader.size();
        if (size.isValid() && (size.width() > kMaxSide || size.height() > kMaxSide)) {
            return {};
        }
        if (!size.isValid()) {
            return {};
        }
    }
    return reader.read();
}

QImage decodeFile(const QString &path)
{
    QImageReader reader(path);
    if (!reader.canRead()) {
        return {};
    }
    return decode(reader, reader.format() == "svg" || reader.format() == "svgz");
}

QImage decodeBytes(const QByteArray &bytes, const QByteArray &mimeFormat)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    if (!reader.canRead()) {
        return {};
    }
    return decode(reader, mimeFormat.contains("svg"));
}

struct Rgb {
    double r;
    double g;
    double b;
};

double median(std::vector<int> &values)
{
    std::ranges::nth_element(values, values.begin() + values.size() / 2);
    return values[values.size() / 2];
}

/// An opaque, roughly single-colour shape behind the logo (an app-icon plate),
/// modelled as a linear gradient through samples at the four edge centres.
struct Plate {
    Rgb mean;
    Rgb gx;
    Rgb gy;
    double cx;
    double cy;

    Rgb at(int x, int y) const
    {
        const double dx = x - cx;
        const double dy = y - cy;
        const auto channel = [&](double m, double ax, double ay) {
            return std::clamp(m + (ax * dx) + (ay * dy), 0.0, 1.0);
        };
        return {channel(mean.r, gx.r, gy.r), channel(mean.g, gx.g, gy.g),
                channel(mean.b, gx.b, gy.b)};
    }
};

struct Sample {
    int x;
    int y;
    Rgb colour;
};

/// Steps inward from an edge centre past transparent pixels, then a little
/// further to clear the anti-aliased rim. Empty when no opaque pixel is found.
std::optional<Sample> samplePlate(const QImage &image, int x, int y, int dx, int dy)
{
    const int side = std::min(image.width(), image.height());
    const int reach = side / kPlateReachDivisor;
    const int nudge = std::min(std::max(2, side / 64), side / 4);
    int steps = 0;
    while (qAlpha(image.pixel(x, y)) < kOpaqueAlpha) {
        if (steps++ >= reach) {
            return std::nullopt;
        }
        x += dx;
        y += dy;
    }
    x = std::clamp(x + (dx * nudge), 0, image.width() - 1);
    y = std::clamp(y + (dy * nudge), 0, image.height() - 1);
    const QRgb p = image.pixel(x, y);
    if (qAlpha(p) < kOpaqueAlpha) {
        return std::nullopt;
    }
    return Sample{x, y, {qRed(p) / 255.0, qGreen(p) / 255.0, qBlue(p) / 255.0}};
}

std::optional<Plate> findPlate(const QImage &image)
{
    const int w = image.width();
    const int h = image.height();
    const auto top = samplePlate(image, w / 2, 0, 0, 1);
    const auto bottom = samplePlate(image, w / 2, h - 1, 0, -1);
    const auto left = samplePlate(image, 0, h / 2, 1, 0);
    const auto right = samplePlate(image, w - 1, h / 2, -1, 0);
    if (!top || !bottom || !left || !right) {
        return std::nullopt;
    }
    const std::array samples{*top, *bottom, *left, *right};
    for (const Sample &a : samples) {
        for (const Sample &b : samples) {
            const double spread = std::max({std::abs(a.colour.r - b.colour.r),
                                            std::abs(a.colour.g - b.colour.g),
                                            std::abs(a.colour.b - b.colour.b)});
            if (spread > kPlateSpread) {
                return std::nullopt;
            }
        }
    }

    const double dxSpan = std::max(1, right->x - left->x);
    const double dySpan = std::max(1, bottom->y - top->y);
    Plate plate{};
    plate.mean = {(top->colour.r + bottom->colour.r + left->colour.r + right->colour.r) / 4,
                  (top->colour.g + bottom->colour.g + left->colour.g + right->colour.g) / 4,
                  (top->colour.b + bottom->colour.b + left->colour.b + right->colour.b) / 4};
    plate.gx = {(right->colour.r - left->colour.r) / dxSpan,
                (right->colour.g - left->colour.g) / dxSpan,
                (right->colour.b - left->colour.b) / dxSpan};
    plate.gy = {(bottom->colour.r - top->colour.r) / dySpan,
                (bottom->colour.g - top->colour.g) / dySpan,
                (bottom->colour.b - top->colour.b) / dySpan};
    plate.cx = (left->x + right->x) / 2.0;
    plate.cy = (top->y + bottom->y) / 2.0;
    return plate;
}

struct Coverage {
    QImage image;
    int minX;
    int minY;
    int maxX;
    int maxY;

    bool empty() const { return maxX < minX || maxY < minY; }
};

/// How far each pixel differs from `backdrop(x, y)` once composited over it.
/// `invertAlpha` flips coverage within the opaque part only, so a transparent
/// surround stays empty; otherwise `invert` flips everything.
template <typename Backdrop>
Coverage coverageOf(const QImage &image, Backdrop backdrop, bool invert, bool invertAlpha)
{
    const int w = image.width();
    const int h = image.height();
    Coverage result{QImage(w, h, QImage::Format_Grayscale8), w, h, -1, -1};
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        uchar *out = result.image.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const Rgb back = backdrop(x, y);
            const double a = qAlpha(line[x]) / 255.0;
            const double r = (qRed(line[x]) / 255.0 * a) + (back.r * (1 - a));
            const double g = (qGreen(line[x]) / 255.0 * a) + (back.g * (1 - a));
            const double b = (qBlue(line[x]) / 255.0 * a) + (back.b * (1 - a));
            const double d =
                std::max({std::abs(r - back.r), std::abs(g - back.g), std::abs(b - back.b)});
            double c = smoothstep(kEdgeLow, kEdgeHigh, d);
            if (invert) {
                c = invertAlpha ? a * (1 - c) : 1 - c;
            }
            out[x] = static_cast<uchar>(std::lround(c * 255));
            if (c > kBoundsCoverage) {
                result.minX = std::min(result.minX, x);
                result.maxX = std::max(result.maxX, x);
                result.minY = std::min(result.minY, y);
                result.maxY = std::max(result.maxY, y);
            }
        }
    }
    return result;
}

/// The flat backdrop for a picture without a plate: black or white behind a
/// transparent-backed logo, otherwise the median colour of the border ring.
auto plainBackdrop(const QImage &image)
{
    const int w = image.width();
    const int h = image.height();
    // The 1-px border ring.
    std::vector<QRgb> border;
    for (int x = 0; x < w; ++x) {
        border.push_back(image.pixel(x, 0));
        border.push_back(image.pixel(x, h - 1));
    }
    for (int y = 1; y < h - 1; ++y) {
        border.push_back(image.pixel(0, y));
        border.push_back(image.pixel(w - 1, y));
    }
    const auto transparent = std::ranges::count_if(
        border, [](QRgb p) { return qAlpha(p) < kBorderTransparentAlpha; });

    Rgb backdrop{1, 1, 1};
    if (static_cast<double>(transparent) / static_cast<double>(border.size()) >=
        kTransparentBorderShare) {
        double weighted = 0;
        double weight = 0;
        for (int y = 0; y < h; ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const int a = qAlpha(line[x]);
                if (a >= kOpaqueAlpha) {
                    const double lum = (0.2126 * qRed(line[x]) + 0.7152 * qGreen(line[x]) +
                                        0.0722 * qBlue(line[x])) /
                                       255.0;
                    weighted += lum * a;
                    weight += a;
                }
            }
        }
        const bool lightLogo = weight > 0 && (weighted / weight) > kLightLogoLuminance;
        backdrop = lightLogo ? Rgb{0, 0, 0} : Rgb{1, 1, 1};
    } else {
        std::vector<int> rs;
        std::vector<int> gs;
        std::vector<int> bs;
        for (const QRgb p : border) {
            // Composited over white, so a faint border reads as near-white.
            const double a = qAlpha(p) / 255.0;
            rs.push_back(qRound(qRed(p) * a + 255 * (1 - a)));
            gs.push_back(qRound(qGreen(p) * a + 255 * (1 - a)));
            bs.push_back(qRound(qBlue(p) * a + 255 * (1 - a)));
        }
        backdrop = {median(rs) / 255.0, median(gs) / 255.0, median(bs) / 255.0};
    }

    return [backdrop](int, int) { return backdrop; };
}

} // namespace

QImage sourceFromClipboard(const QMimeData *mime, QString *origin)
{
    if (mime == nullptr) {
        return {};
    }

    const QList<QUrl> urls = mime->urls();
    for (qsizetype i = urls.size() - 1; i >= 0; --i) {
        const QString path = urls.at(i).toLocalFile();
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            continue;
        }
        if (const QImage image = decodeFile(path); !image.isNull()) {
            if (origin != nullptr) {
                *origin = QFileInfo(path).fileName();
            }
            return image;
        }
    }

    QStringList formats = mime->formats();
    std::ranges::stable_sort(formats, [](const QString &a, const QString &b) {
        return a == QLatin1String("image/png") && b != QLatin1String("image/png");
    });
    for (const QString &format : std::as_const(formats)) {
        if (!format.startsWith(QLatin1String("image/"))) {
            continue;
        }
        if (const QImage image = decodeBytes(mime->data(format), format.toLatin1());
            !image.isNull()) {
            if (origin != nullptr) {
                *origin = QObject::tr("clipboard image");
            }
            return image;
        }
    }

    // Platforms that only offer application/x-qt-image end up here, and the
    // size guard cannot run before the decode.
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull() && image.width() <= kMaxSide && image.height() <= kMaxSide) {
            if (origin != nullptr) {
                *origin = QObject::tr("clipboard image");
            }
            return image;
        }
    }
    return {};
}

QImage toMask(const QImage &source, bool invert)
{
    if (source.isNull()) {
        return {};
    }
    QImage image = source.width() > kWorkSide || source.height() > kWorkSide
                       ? source.scaled(kWorkSide, kWorkSide, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation)
                       : source;
    image = image.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    if (w < 2 || h < 2) {
        return {};
    }

    Coverage coverage{};
    const std::optional<Plate> plate = findPlate(image);
    const auto plateBackdrop = [&](int x, int y) { return plate->at(x, y); };
    if (plate) {
        // Whether the plate has anything on it must not depend on `invert`.
        coverage = coverageOf(image, plateBackdrop, false, true);
        if (!coverage.empty() && invert) {
            coverage = coverageOf(image, plateBackdrop, true, true);
        }
    }
    if (!plate || coverage.empty()) {
        coverage = coverageOf(image, plainBackdrop(image), invert, false);
    }
    if (coverage.empty()) {
        return {};
    }
    const int minX = coverage.minX;
    const int minY = coverage.minY;
    const int maxX = coverage.maxX;
    const int maxY = coverage.maxY;

    const QImage cropped = coverage.image.copy(minX, minY, maxX - minX + 1, maxY - minY + 1);
    const int side = std::max(cropped.width(), cropped.height());
    const int canvasSide = std::max(1, qRound(side / (1 - (2 * kMargin))));
    QImage canvas(canvasSide, canvasSide, QImage::Format_Grayscale8);
    canvas.fill(0);
    {
        QPainter painter(&canvas);
        painter.drawImage((canvasSide - cropped.width()) / 2, (canvasSide - cropped.height()) / 2,
                          cropped);
    }
    return canvas.scaled(kMaskSize, kMaskSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}

QString idFor(const QImage &mask)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (int y = 0; y < mask.height(); ++y) {
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(mask.constScanLine(y)),
                                    mask.width()));
    }
    return QStringLiteral("img:") + QString::fromLatin1(hash.result().left(16).toHex());
}

} // namespace pf::FolderIconImage
