#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

class QLabel;

namespace pf::ui {

/// PDFs (§7.6).
///
/// §7.6 asks for paged rendering via QtPdf or poppler-qt6, with page navigation
/// and zoom. Like MediaRenderer, this build shows a metadata card and leaves the
/// rendering to the plugin host, for the reason §3.4 gives: QtPdf and poppler
/// must not be link-time dependencies of the main binary, because a DT_NEEDED
/// entry is paid at every launch by every user whether or not they ever open a
/// PDF.
///
/// On Arch there is a second reason to keep this at arm's length. Qt6Pdf is not
/// packaged separately there — it ships inside qt6-webengine — so linking it
/// would put Chromium in a file manager's dependency tree. poppler-qt6 is the
/// backend the AUR package depends on for that reason.
class PdfRenderer : public QuickLookRenderer
{
    // tr() without QObject: a renderer implements an interface and has no
    // need of the meta-object system otherwise.
    Q_DECLARE_TR_FUNCTIONS(PdfRenderer)

public:
    QString id() const override { return QStringLiteral("pdf"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 25; }

    QWidget *createWidget(QWidget *parent) override;
    void setContent(QuickLookContent &&content) override;
    void clear() override;
    QString statusText() const override;

private:
    QLabel *m_label = nullptr;
    QString m_status;
};

} // namespace pf::ui
