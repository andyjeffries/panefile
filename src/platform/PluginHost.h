#pragma once

#include <QObject>
#include <QString>

namespace pf::platform {

/// The optional-feature plugins of §3.4.
///
/// "Optional heavy dependencies (KSyntaxHighlighting, QtMultimedia, QtPdf,
/// poppler, libffmpegthumbnailer) must **not** be direct link-time dependencies
/// of the main binary. Build each as a small plugin `.so` loaded with
/// `QPluginLoader` on first use."
///
/// This is a fixed set, not a plugin system: §1 rules that out. Each entry is
/// built from src/plugins/ in the same build as the binary, implements one of
/// the private interfaces in plugins/PluginInterfaces.h, and is found only in
/// the directories pluginSearchPaths() names.
enum class Plugin {
    Syntax,          ///< pf-syntax: KSyntaxHighlighting for the text renderer
    Media,           ///< pf-media: QtMultimedia playback for the media renderer
    Pdf,             ///< pf-pdf: poppler-qt6 or QtPdf for the PDF renderer
    VideoThumbnails, ///< pf-video-thumb: libffmpegthumbnailer for §7.7
};

/// The plugin's base file name, without prefix or suffix: "pf-syntax".
QString pluginBaseName(Plugin which);

/// Loads the plugin on first call and returns its root object, or nullptr when
/// it is not installed or will not load. Either answer is remembered for the
/// life of the process, so a missing plugin costs one directory probe, once.
///
/// Thread-safe: the thumbnail pool asks for pf-video-thumb from a worker, and
/// two threads asking at once get the same instance from a single load.
QObject *pluginInstance(Plugin which);

/// pluginInstance() cast to the interface the plugin implements.
template<class Interface>
Interface *plugin(Plugin which)
{
    return qobject_cast<Interface *>(pluginInstance(which));
}

} // namespace pf::platform
