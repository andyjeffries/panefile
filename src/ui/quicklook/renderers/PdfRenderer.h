#pragma once

#include "ui/quicklook/renderers/PluginBackedRenderer.h"

namespace pf::ui {

/// PDFs (§7.6).
///
/// §7.6 asks for paged rendering via QtPdf or poppler-qt6, with page navigation
/// and zoom. That lives in the pf-pdf plugin, for the reason §3.4 gives: QtPdf
/// and poppler must not be link-time dependencies of the main binary, because
/// a DT_NEEDED entry is paid at every launch by every user whether or not they
/// ever open a PDF.
///
/// On Arch there is a second reason to keep this at arm's length. Qt6Pdf is not
/// packaged separately there — it ships inside qt6-webengine — so the plugin
/// prefers poppler-qt6, which is the backend the AUR package depends on.
class PdfRenderer : public PluginBackedRenderer
{
    Q_DECLARE_TR_FUNCTIONS(PdfRenderer)

public:
    PdfRenderer();

    QString id() const override { return QStringLiteral("pdf"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 25; }

protected:
    QStringList cardLines(const QuickLookContent &content) const override;
    QString missingPluginNote() const override;
};

} // namespace pf::ui
