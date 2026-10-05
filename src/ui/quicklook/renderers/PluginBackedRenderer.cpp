#include "ui/quicklook/renderers/PluginBackedRenderer.h"

#include "plugins/PluginInterfaces.h"
#include "core/Format.h"

#include <QFileInfo>
#include <QLabel>
#include <QStackedWidget>

namespace pf::ui {

PluginBackedRenderer::PluginBackedRenderer(platform::Plugin plugin) : m_plugin(plugin) {}

PluginBackedRenderer::~PluginBackedRenderer() = default;

QWidget *PluginBackedRenderer::createWidget(QWidget *parent)
{
    if (m_stack == nullptr) {
        m_stack = new QStackedWidget(parent);

        m_card = new QLabel(m_stack);
        m_card->setAlignment(Qt::AlignCenter);
        m_card->setWordWrap(true);
        m_card->setTextFormat(Qt::PlainText);
        m_stack->addWidget(m_card);
    }
    return m_stack;
}

QuickLookRenderer *PluginBackedRenderer::delegate()
{
    if (!m_askedForPlugin) {
        m_askedForPlugin = true;

        auto *factory = platform::plugin<plugins::RendererPlugin>(m_plugin);
        if (factory != nullptr) {
            m_delegate = factory->createRenderer();
        }
        if (m_delegate) {
            m_delegatePage = m_delegate->createWidget(m_stack);
            m_stack->addWidget(m_delegatePage);
            m_delegate->setStatusChangedHandler([this] {
                if (m_usingPlugin) {
                    notifyStatusChanged();
                }
            });
        }
    }
    return m_delegate.get();
}

void PluginBackedRenderer::setContent(QuickLookContent &&content)
{
    if (m_stack == nullptr) {
        return;
    }

    if (!content.error.isEmpty()) {
        m_card->setText(content.error);
        m_stack->setCurrentWidget(m_card);
        m_status.clear();
        return;
    }

    QuickLookRenderer *target = delegate();
    if (target == nullptr || content.metadataOnly) {
        if (m_usingPlugin && target != nullptr) {
            target->clear();
        }
        m_usingPlugin = false;
        showCard(content, target == nullptr);
        return;
    }

    m_usingPlugin = true;
    m_stack->setCurrentWidget(m_delegatePage);
    target->setContent(std::move(content));
}

void PluginBackedRenderer::showCard(const QuickLookContent &content, bool pluginMissing)
{
    QStringList lines = cardLines(content);
    lines << QString();
    if (pluginMissing) {
        lines << missingPluginNote();
    } else {
        // §7.6: above max_decode_mb the file is described, not decoded.
        lines << tr("Too large to preview.\nPress Enter to open in the default application.");
    }

    m_card->setText(lines.join(QLatin1Char('\n')));
    m_stack->setCurrentWidget(m_card);
    m_status =
        QStringLiteral("%1 · %2").arg(content.mimeType.comment(), formatSize(content.entry.size));
}

void PluginBackedRenderer::clear()
{
    if (m_delegate) {
        // Stops playback, among other things: moving the cursor off a video
        // must not leave its audio running behind the next file.
        m_delegate->clear();
    }
    if (m_card != nullptr) {
        m_card->clear();
    }
    m_usingPlugin = false;
    m_status.clear();
}

QString PluginBackedRenderer::statusText() const
{
    return m_usingPlugin ? m_delegate->statusText() : m_status;
}

bool PluginBackedRenderer::handleKey(QKeyEvent *event)
{
    return m_usingPlugin && m_delegate->handleKey(event);
}

bool PluginBackedRenderer::isUsingPlugin() const
{
    return m_usingPlugin;
}

} // namespace pf::ui
