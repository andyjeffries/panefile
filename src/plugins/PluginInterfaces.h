#pragma once

// The private interfaces between pf and its optional-feature plugins (§3.4).
//
// Header-only on purpose. A plugin includes this and the header-only types it
// names — QuickLookRenderer.h and model/FileEntry.h — and links nothing from
// pf's own layers: a plugin that linked pf_core statically would carry its own
// copies of every function-local static in it, from the logging categories to
// the worker pools, and they would silently disagree with the binary's.
//
// The IIDs carry a version. Plugins are built in the same tree as the binary
// and installed beside it, so a mismatch means a stale plugin left behind by an
// older install; QPluginLoader's IID check refuses it instead of calling into a
// vtable laid out differently.

#include "ui/quicklook/QuickLookRenderer.h"

#include <QImage>
#include <QString>
#include <QtPlugin>

#include <memory>

class QSyntaxHighlighter;
class QTextDocument;

namespace pf::plugins {

/// pf-syntax: KSyntaxHighlighting for the text renderer (§2, §7.6).
class SyntaxPlugin
{
public:
    virtual ~SyntaxPlugin() = default;

    /// Attaches a highlighter for the file to `document` and returns it,
    /// parented to the document. Returns nullptr when no syntax definition
    /// matches the name or the MIME type, leaving the document plain.
    virtual QSyntaxHighlighter *attach(QTextDocument *document, const QString &fileName,
                                       const QString &mimeName, bool dark) = 0;
};

/// pf-media and pf-pdf: a Quick Look renderer the built-in one delegates to.
///
/// The built-in renderer keeps its place in the registry, its id and its
/// priority; it asks for this the first time it has a file to show and falls
/// back to its own metadata card when the plugin is absent.
class RendererPlugin
{
public:
    virtual ~RendererPlugin() = default;

    virtual std::unique_ptr<ui::QuickLookRenderer> createRenderer() = 0;
};

/// pf-video-thumb: libffmpegthumbnailer for §7.7.
class VideoThumbnailPlugin
{
public:
    virtual ~VideoThumbnailPlugin() = default;

    /// A frame from the video, scaled to fit `sizePx` square. Returns a null
    /// image and sets `error` when the file cannot be decoded.
    ///
    /// Called from the thumbnail pool, several threads at once: an
    /// implementation must be safe to call concurrently.
    virtual QImage thumbnail(const QString &path, int sizePx, QString *error) const = 0;
};

} // namespace pf::plugins

#define PF_SYNTAX_PLUGIN_IID "org.panefile.SyntaxPlugin/1"
#define PF_RENDERER_PLUGIN_IID "org.panefile.RendererPlugin/1"
#define PF_VIDEO_THUMBNAIL_PLUGIN_IID "org.panefile.VideoThumbnailPlugin/1"

Q_DECLARE_INTERFACE(pf::plugins::SyntaxPlugin, PF_SYNTAX_PLUGIN_IID)
Q_DECLARE_INTERFACE(pf::plugins::RendererPlugin, PF_RENDERER_PLUGIN_IID)
Q_DECLARE_INTERFACE(pf::plugins::VideoThumbnailPlugin, PF_VIDEO_THUMBNAIL_PLUGIN_IID)
