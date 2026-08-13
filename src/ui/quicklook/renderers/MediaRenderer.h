#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

class QLabel;

namespace pf::ui {

/// Video and audio (§7.6).
///
/// §7.6 asks for QMediaPlayer playback with a scrub bar, and for the video
/// renderer to fall back "to a static thumbnail if QtMultimedia is unavailable".
///
/// This build takes the fallback in every case, and says so rather than
/// pretending otherwise. The reason is §3.4: "Optional heavy dependencies
/// (KSyntaxHighlighting, QtMultimedia, QtPdf, poppler, libffmpegthumbnailer)
/// must **not** be direct link-time dependencies of the main binary. Build each
/// as a small plugin `.so` loaded with `QPluginLoader` on first use." Linking
/// QtMultimedia here would add a DT_NEEDED entry paid for at every launch by
/// every user, including the ones who never open a video — exactly what the
/// dependency guard in CI exists to prevent.
///
/// So playback waits for the plugin host, and what is here now is the metadata
/// card: name, type, size, duration and dimensions where a thumbnail or the
/// container's own header can supply them. A user gets something informative
/// instead of a hex dump of an MP4.
class MediaRenderer : public QuickLookRenderer
{
    // tr() without QObject: a renderer implements an interface and has no
    // need of the meta-object system otherwise.
    Q_DECLARE_TR_FUNCTIONS(MediaRenderer)

public:
    QString id() const override { return QStringLiteral("media"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 25; }

    QWidget *createWidget(QWidget *parent) override;
    void setContent(QuickLookContent &&content) override;
    void clear() override;
    QString statusText() const override;

private:
    QLabel *m_label = nullptr;
    QString m_status;
};

} // namespace pf::ui
