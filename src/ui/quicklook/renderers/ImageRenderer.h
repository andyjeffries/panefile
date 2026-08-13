#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

#include <QImage>

class QLabel;
class QMovie;
class QScrollArea;

namespace pf::ui {

/// Images (§7.6).
///
/// "Full-resolution decode off-thread, zoom and pan, EXIF summary (dimensions,
/// camera, date), animated GIF/WebP playback."
///
/// The decode happens in the loader, not here: this receives a QImage that is
/// already in memory, because decoding a 40-megapixel photograph on the GUI
/// thread would freeze the window for as long as it took.
class ImageRenderer : public QuickLookRenderer
{
    // tr() without QObject: a renderer implements an interface and has no
    // need of the meta-object system otherwise.
    Q_DECLARE_TR_FUNCTIONS(ImageRenderer)

public:
    QString id() const override { return QStringLiteral("image"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 20; }

    bool wantsImage() const override { return true; }

    QWidget *createWidget(QWidget *parent) override;
    void setContent(QuickLookContent &&content) override;
    void clear() override;
    QString statusText() const override;
    bool handleKey(QKeyEvent *event) override;

private:
    void rescale();
    void setZoom(double zoom);
    void fitToWindow();

    QScrollArea *m_scroll = nullptr;
    QLabel *m_label = nullptr;
    QMovie *m_movie = nullptr;

    QImage m_image;
    QString m_status;
    double m_zoom = 1.0;

    /// True while the image is scaled to fit rather than to a chosen zoom.
    /// Kept separate from the zoom factor because a resize must rescale a
    /// fitted image and leave a deliberately zoomed one alone.
    bool m_fitted = true;
};

} // namespace pf::ui
