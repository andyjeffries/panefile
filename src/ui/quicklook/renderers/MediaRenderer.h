#pragma once

#include "ui/quicklook/renderers/PluginBackedRenderer.h"

namespace pf::ui {

/// Video and audio (§7.6).
///
/// §7.6 asks for QMediaPlayer playback with a scrub bar. That lives in the
/// pf-media plugin, because §3.4 forbids QtMultimedia from being a link-time
/// dependency of the main binary: "A DT_NEEDED entry costs relocation and
/// page-in time at every launch whether or not the code is called."
///
/// Without the plugin this shows the metadata card — name, type, size and any
/// facts the loader found — rather than a hex dump of an MP4.
class MediaRenderer : public PluginBackedRenderer
{
    Q_DECLARE_TR_FUNCTIONS(MediaRenderer)

public:
    MediaRenderer();

    QString id() const override { return QStringLiteral("media"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 25; }

protected:
    QStringList cardLines(const QuickLookContent &content) const override;
    QString missingPluginNote() const override;
};

} // namespace pf::ui
