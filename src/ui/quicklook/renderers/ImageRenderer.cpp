#include "ui/quicklook/renderers/ImageRenderer.h"

#include "core/Format.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMovie>
#include <QScrollArea>
#include <QScrollBar>

namespace pf::ui {
namespace {

constexpr double kZoomStep = 1.25;
constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 32.0;

} // namespace

bool ImageRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    return !entry.isDir && mime.isValid() && mime.name().startsWith(QLatin1String("image/"));
}

QWidget *ImageRenderer::createWidget(QWidget *parent)
{
    if (m_scroll == nullptr) {
        m_scroll = new QScrollArea(parent);
        m_scroll->setWidgetResizable(false);
        m_scroll->setAlignment(Qt::AlignCenter);
        m_scroll->setFrameShape(QFrame::NoFrame);

        m_label = new QLabel(m_scroll);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setBackgroundRole(QPalette::Base);
        m_scroll->setWidget(m_label);
    }
    return m_scroll;
}

void ImageRenderer::setContent(QuickLookContent &&content)
{
    if (m_scroll == nullptr) {
        return;
    }

    if (m_movie != nullptr) {
        m_movie->stop();
        m_movie->deleteLater();
        m_movie = nullptr;
    }

    if (!content.error.isEmpty()) {
        m_label->setText(content.error);
        m_status.clear();
        return;
    }

    if (content.metadataOnly) {
        // §7.6: "files above quicklook.max_decode_mb show a metadata-only card
        // with an 'open anyway' action." Showing the facts beats spending ten
        // seconds decoding something the user is only glancing at.
        QStringList lines;
        for (const auto &[key, value] : content.facts) {
            lines << QStringLiteral("%1: %2").arg(key, value);
        }
        m_label->setText(tr("Too large to decode\n\n%1").arg(lines.join(QLatin1Char('\n'))));
        m_status = formatSize(content.entry.size);
        return;
    }

    m_image = content.image;

    // §7.6 asks for "animated GIF/WebP playback". QMovie streams the file
    // itself, so an animated format gets the file path rather than the single
    // frame the loader decoded.
    const bool animated = content.mimeType.name() == QLatin1String("image/gif") ||
                          content.mimeType.name() == QLatin1String("image/webp") ||
                          content.mimeType.name() == QLatin1String("image/apng");

    if (animated) {
        m_movie = new QMovie(content.path, {}, m_label);
        if (m_movie->isValid() && m_movie->frameCount() > 1) {
            m_label->setMovie(m_movie);
            m_movie->start();
            m_status = tr("%1 × %2 · %3 frames · %4")
                           .arg(m_image.width())
                           .arg(m_image.height())
                           .arg(m_movie->frameCount())
                           .arg(formatSize(content.entry.size));
            m_label->adjustSize();
            return;
        }
        // A single-frame GIF is just an image; fall through rather than
        // animating one frame forever.
        m_movie->deleteLater();
        m_movie = nullptr;
    }

    m_fitted = true;
    rescale();

    QStringList facts;
    facts << QStringLiteral("%1 × %2").arg(m_image.width()).arg(m_image.height());
    facts << formatSize(content.entry.size);
    for (const auto &[key, value] : content.facts) {
        facts << QStringLiteral("%1 %2").arg(key, value);
    }
    m_status = facts.join(QStringLiteral(" · "));
}

void ImageRenderer::rescale()
{
    if (m_label == nullptr || m_image.isNull()) {
        return;
    }

    QSize target;
    if (m_fitted) {
        // Scaled down to fit, never up: enlarging a small icon to fill the pane
        // makes it blurry and tells the viewer nothing they could not already
        // see.
        target = m_image.size().scaled(m_scroll->viewport()->size(), Qt::KeepAspectRatio);
        if (target.width() > m_image.width()) {
            target = m_image.size();
        }
        m_zoom = m_image.width() > 0 ? static_cast<double>(target.width()) / m_image.width() : 1.0;
    } else {
        target = m_image.size() * m_zoom;
    }

    m_label->setPixmap(
        QPixmap::fromImage(m_image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_label->adjustSize();
}

void ImageRenderer::setZoom(double zoom)
{
    m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    m_fitted = false;
    rescale();
}

void ImageRenderer::fitToWindow()
{
    m_fitted = true;
    rescale();
}

bool ImageRenderer::handleKey(QKeyEvent *event)
{
    // §7.6: "`+` / `-` / `0` | Zoom in / out / fit — image and PDF renderers".
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        setZoom(m_zoom * kZoomStep);
        return true;
    case Qt::Key_Minus:
        setZoom(m_zoom / kZoomStep);
        return true;
    case Qt::Key_0:
        fitToWindow();
        return true;
    default:
        return false;
    }
}

void ImageRenderer::clear()
{
    if (m_movie != nullptr) {
        m_movie->stop();
        m_movie->deleteLater();
        m_movie = nullptr;
    }
    if (m_label != nullptr) {
        m_label->clear();
    }
    // The image is dropped explicitly: holding a decoded 40-megapixel photo
    // after Quick Look closes would keep 150 MB alive for nothing.
    m_image = QImage();
    m_status.clear();
    m_fitted = true;
    m_zoom = 1.0;
}

QString ImageRenderer::statusText() const
{
    return m_status;
}

} // namespace pf::ui
