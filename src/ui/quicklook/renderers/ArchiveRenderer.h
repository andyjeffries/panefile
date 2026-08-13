#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

class QTreeWidget;

namespace pf::ui {

/// Archives (§7.6).
///
/// "Entry tree with sizes and compression ratios. No extraction."
///
/// The last three words are the requirement. A preview that extracted to a
/// temporary directory to show a listing would write files as a side effect of
/// moving the cursor, and a malicious archive would be extracted before anyone
/// had chosen to extract it.
class ArchiveRenderer : public QuickLookRenderer
{
    // tr() without QObject: a renderer implements an interface and has no
    // need of the meta-object system otherwise.
    Q_DECLARE_TR_FUNCTIONS(ArchiveRenderer)

public:
    QString id() const override { return QStringLiteral("archive"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 30; }

    QWidget *createWidget(QWidget *parent) override;
    void setContent(QuickLookContent &&content) override;
    void clear() override;
    QString statusText() const override;

    /// Whether a MIME type names an archive libarchive can open.
    static bool isArchive(const QMimeType &mime);

private:
    QTreeWidget *m_tree = nullptr;
    QString m_status;
};

} // namespace pf::ui
