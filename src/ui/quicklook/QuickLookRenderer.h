#pragma once

#include "model/FileEntry.h"

#include <QByteArray>
#include <QImage>
#include <QMimeType>
#include <QString>

class QKeyEvent;
class QWidget;

namespace pf::ui {

/// Content loaded off the GUI thread and handed to a renderer (§7.6).
///
/// "Content loading is **always** off the GUI thread, into a QuickLookContent
/// value type handed back by queued signal." So this is a value type, and it
/// holds whatever the loader could produce without touching a widget.
///
/// Which fields are filled depends on the renderer that asked. A text renderer
/// wants `text`; an image renderer wants `image`; several want neither and
/// work from the path alone, because QMediaPlayer and QPdfDocument do their own
/// streaming and would only be hindered by having the bytes read for them.
struct QuickLookContent {
    QString path;
    QMimeType mimeType;
    FileEntry entry;

    QString text;
    QImage image;
    QByteArray bytes;

    /// Free-form facts the renderer displays — dimensions, duration, codec,
    /// entry count. Ordered, because the order they are shown in is chosen.
    QList<QPair<QString, QString>> facts;

    /// Set when loading failed or was refused. A renderer showing this falls
    /// back to hex with the note attached (§7.6).
    QString error;

    /// True when the file was too large to decode and only metadata was read
    /// (§7.6's `max_decode_mb`).
    bool metadataOnly = false;
};

/// Renders one kind of file in Quick Look (§7.6).
///
/// §7.6: "Do not write one giant switch statement. Define an interface and
/// register implementations." The registry picks the highest-priority renderer
/// whose canRender() returns true, falling back to hex.
///
/// Renderer widgets are created once and reused, because "creating a QPdfView
/// per cursor movement will be visibly slow".
class QuickLookRenderer
{
public:
    virtual ~QuickLookRenderer() = default;

    virtual QString id() const = 0;

    virtual bool canRender(const QMimeType &mime, const FileEntry &entry) const = 0;

    /// Higher wins ties (§7.6).
    virtual int priority() const { return 0; }

    /// Whether this renderer wants the file's bytes read for it, and how many.
    /// Zero means it opens the file itself — which is what the video, audio and
    /// PDF renderers do, since their libraries stream far better than a
    /// wholesale read would.
    virtual qint64 desiredReadBytes() const { return 0; }

    /// Whether the loader should decode an image for it.
    virtual bool wantsImage() const { return false; }

    virtual QWidget *createWidget(QWidget *parent) = 0;

    /// Called on the GUI thread with content already loaded off-thread.
    virtual void setContent(QuickLookContent &&content) = 0;

    virtual void clear() = 0;

    /// Renderer-specific hints for the footer — page numbers, zoom, encoding.
    virtual QString statusText() const { return {}; }

    /// Returns true when the key was consumed (§7.6's per-renderer keys).
    virtual bool handleKey(QKeyEvent *event)
    {
        Q_UNUSED(event)
        return false;
    }
};

} // namespace pf::ui
