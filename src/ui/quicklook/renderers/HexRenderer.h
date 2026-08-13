#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

class QPlainTextEdit;

namespace pf::ui {

/// The fallback renderer (§7.6).
///
/// "Hex and ASCII dump of the first 4 KiB, plus MIME, size and file(1)-style
/// description."
///
/// It exists as much for what it guarantees as for what it shows: because it
/// accepts everything, Quick Look can never be opened on a file and show
/// nothing. A renderer that fails degrades to this with its error attached
/// rather than leaving a blank pane.
class HexRenderer : public QuickLookRenderer
{
    // tr() without QObject: a renderer implements an interface and has no
    // need of the meta-object system otherwise.
    Q_DECLARE_TR_FUNCTIONS(HexRenderer)

public:
    /// §7.6: "the first 4 KiB".
    static constexpr qint64 kDumpBytes = 4096;

    QString id() const override { return QStringLiteral("hex"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;

    /// Lowest, so any renderer that also accepts a file wins.
    int priority() const override { return -100; }

    qint64 desiredReadBytes() const override { return kDumpBytes; }

    QWidget *createWidget(QWidget *parent) override;
    void setContent(QuickLookContent &&content) override;
    void clear() override;
    QString statusText() const override;

    /// Formats a hex and ASCII dump the way xxd does. Exposed for testing,
    /// because the alignment is the whole point of a hex dump and a test can
    /// check it without a widget.
    static QString formatDump(const QByteArray &bytes, qint64 offset = 0);

private:
    QPlainTextEdit *m_view = nullptr;
    QString m_status;
};

} // namespace pf::ui
