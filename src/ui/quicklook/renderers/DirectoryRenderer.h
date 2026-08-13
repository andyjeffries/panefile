#pragma once

#include "ui/quicklook/QuickLookRenderer.h"

#include <QCoreApplication>

class QTreeWidget;

namespace pf::ui {

/// Directories (§7.6).
///
/// "Child listing (first 200), item count, aggregate size computed lazily and
/// cancellably, du-style top-5 largest children."
///
/// The word doing the work is *cancellably*. Summing a directory tree can take
/// minutes on a large one, and the cursor may well have moved on within a
/// second — so the total is computed on a worker that is abandoned the moment
/// the preview changes, and the pane shows what it has meanwhile.
class DirectoryRenderer : public QuickLookRenderer
{
    // tr() without QObject: a renderer implements an interface and has no
    // need of the meta-object system otherwise.
    Q_DECLARE_TR_FUNCTIONS(DirectoryRenderer)

public:
    /// §7.6: "Child listing (first 200)".
    static constexpr int kMaxChildren = 200;

    /// §7.6: "du-style top-5 largest children".
    static constexpr int kTopChildren = 5;

    QString id() const override { return QStringLiteral("directory"); }

    bool canRender(const QMimeType &mime, const FileEntry &entry) const override;
    int priority() const override { return 50; }

    QWidget *createWidget(QWidget *parent) override;
    void setContent(QuickLookContent &&content) override;
    void clear() override;
    QString statusText() const override;

private:
    QTreeWidget *m_tree = nullptr;
    QString m_status;
};

} // namespace pf::ui
