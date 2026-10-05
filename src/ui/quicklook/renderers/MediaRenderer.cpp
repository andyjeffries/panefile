#include "ui/quicklook/renderers/MediaRenderer.h"

#include "core/Format.h"

#include <QFileInfo>

namespace pf::ui {

MediaRenderer::MediaRenderer() : PluginBackedRenderer(platform::Plugin::Media) {}

bool MediaRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    if (entry.isDir || !mime.isValid()) {
        return false;
    }
    return mime.name().startsWith(QLatin1String("video/")) ||
           mime.name().startsWith(QLatin1String("audio/"));
}

QStringList MediaRenderer::cardLines(const QuickLookContent &content) const
{
    QStringList lines;
    lines << QFileInfo(content.path).fileName();
    lines << QString();
    lines << content.mimeType.comment();
    lines << formatSize(content.entry.size);

    for (const auto &[key, value] : content.facts) {
        lines << QStringLiteral("%1: %2").arg(key, value);
    }
    return lines;
}

QString MediaRenderer::missingPluginNote() const
{
    return tr("Playback is not installed (the pf-media plugin).\nPress Enter to open in the "
              "default application.");
}

} // namespace pf::ui
