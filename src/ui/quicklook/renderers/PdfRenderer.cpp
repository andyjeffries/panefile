#include "ui/quicklook/renderers/PdfRenderer.h"

#include "core/Format.h"

#include <QFileInfo>

namespace pf::ui {

PdfRenderer::PdfRenderer() : PluginBackedRenderer(platform::Plugin::Pdf) {}

bool PdfRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    return !entry.isDir && mime.isValid() && mime.name() == QLatin1String("application/pdf");
}

QStringList PdfRenderer::cardLines(const QuickLookContent &content) const
{
    QStringList lines;
    lines << QFileInfo(content.path).fileName();
    lines << QString();
    lines << formatSize(content.entry.size);
    for (const auto &[key, value] : content.facts) {
        lines << QStringLiteral("%1: %2").arg(key, value);
    }
    return lines;
}

QString PdfRenderer::missingPluginNote() const
{
    return tr("PDF rendering is not installed (the pf-pdf plugin).\nPress Enter to open in "
              "the default application.");
}

} // namespace pf::ui
