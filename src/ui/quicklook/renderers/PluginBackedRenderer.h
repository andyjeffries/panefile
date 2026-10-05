#pragma once

#include "platform/PluginHost.h"
#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

#include <memory>

class QLabel;
class QStackedWidget;

namespace pf::ui {

/// A renderer whose real implementation lives in an optional plugin (§3.4).
///
/// The renderer itself stays in pf, registered as usual, so selection by MIME
/// type and priority never depends on what is installed. It asks the plugin
/// host for its plugin the first time it has a file to show — not when Quick
/// Look opens, so looking at a text file never loads QtMultimedia — and
/// forwards everything to the renderer the plugin creates.
///
/// Without the plugin, or for a file above §7.6's `max_decode_mb`, it shows a
/// metadata card instead: name, type, size and whatever facts the loader found.
/// That is §2's graceful degradation, and it is better than a hex dump.
class PluginBackedRenderer : public QuickLookRenderer
{
    Q_DECLARE_TR_FUNCTIONS(PluginBackedRenderer)

public:
    explicit PluginBackedRenderer(platform::Plugin plugin);
    ~PluginBackedRenderer() override;

    QWidget *createWidget(QWidget *parent) final;
    void setContent(QuickLookContent &&content) final;
    void clear() final;
    QString statusText() const final;
    bool handleKey(QKeyEvent *event) final;

    /// Whether the plugin loaded and is rendering, rather than the card.
    bool isUsingPlugin() const;

protected:
    /// The card's opening lines for this file, before the closing note.
    virtual QStringList cardLines(const QuickLookContent &content) const = 0;

    /// The card's closing note when the plugin is not installed.
    virtual QString missingPluginNote() const = 0;

private:
    QuickLookRenderer *delegate();
    void showCard(const QuickLookContent &content, bool pluginMissing);

    platform::Plugin m_plugin;

    QStackedWidget *m_stack = nullptr;
    QLabel *m_card = nullptr;

    std::unique_ptr<QuickLookRenderer> m_delegate;
    QWidget *m_delegatePage = nullptr;
    bool m_askedForPlugin = false;
    bool m_usingPlugin = false;

    QString m_status;
};

} // namespace pf::ui
