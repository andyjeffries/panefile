#include "ui/quicklook/renderers/MediaRenderer.h"

#include "core/Format.h"

#include <QFileInfo>
#include <QLabel>

namespace pf::ui {

bool MediaRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    if (entry.isDir || !mime.isValid()) {
        return false;
    }
    return mime.name().startsWith(QLatin1String("video/")) ||
           mime.name().startsWith(QLatin1String("audio/"));
}

QWidget *MediaRenderer::createWidget(QWidget *parent)
{
    if (m_label == nullptr) {
        m_label = new QLabel(parent);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setWordWrap(true);
        m_label->setTextFormat(Qt::PlainText);
    }
    return m_label;
}

void MediaRenderer::setContent(QuickLookContent &&content)
{
    if (m_label == nullptr) {
        return;
    }

    if (!content.error.isEmpty()) {
        m_label->setText(content.error);
        m_status.clear();
        return;
    }

    QStringList lines;
    lines << QFileInfo(content.path).fileName();
    lines << QString();
    lines << content.mimeType.comment();
    lines << formatSize(content.entry.size);

    for (const auto &[key, value] : content.facts) {
        lines << QStringLiteral("%1: %2").arg(key, value);
    }

    lines << QString();
    lines << tr("Playback is not built into this binary.\nPress Enter to open in the default "
                "application.");

    m_label->setText(lines.join(QLatin1Char('\n')));
    m_status =
        QStringLiteral("%1 · %2").arg(content.mimeType.comment(), formatSize(content.entry.size));
}

void MediaRenderer::clear()
{
    if (m_label != nullptr) {
        m_label->clear();
    }
    m_status.clear();
}

QString MediaRenderer::statusText() const
{
    return m_status;
}

} // namespace pf::ui
