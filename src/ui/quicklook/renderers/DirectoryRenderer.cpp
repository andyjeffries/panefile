#include "ui/quicklook/renderers/DirectoryRenderer.h"

#include "core/Format.h"

#include <QHeaderView>
#include <QTreeWidget>

namespace pf::ui {

bool DirectoryRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    Q_UNUSED(mime)
    // On the entry rather than the MIME type: a symlink to a directory has
    // isDir set and resolves to inode/directory, but a broken one has neither,
    // and it is still a directory-shaped thing as far as the user is concerned.
    return entry.isDir;
}

QWidget *DirectoryRenderer::createWidget(QWidget *parent)
{
    if (m_tree == nullptr) {
        m_tree = new QTreeWidget(parent);
        m_tree->setColumnCount(2);
        m_tree->setHeaderLabels({tr("Name"), tr("Size")});
        m_tree->setRootIsDecorated(false);
        m_tree->setUniformRowHeights(true);
        m_tree->setSelectionMode(QAbstractItemView::NoSelection);
        m_tree->setFocusPolicy(Qt::NoFocus);
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    }
    return m_tree;
}

void DirectoryRenderer::setContent(QuickLookContent &&content)
{
    if (m_tree == nullptr) {
        return;
    }

    m_tree->clear();

    if (!content.error.isEmpty()) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, content.error);
        m_status.clear();
        return;
    }

    // The loader put the listing in `facts` as name/size pairs, largest first
    // for the leading rows and then in name order — see QuickLookLoader.
    for (const auto &[name, size] : content.facts) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, name);
        item->setText(1, size);
    }

    m_status = content.text;
}

void DirectoryRenderer::clear()
{
    if (m_tree != nullptr) {
        m_tree->clear();
    }
    m_status.clear();
}

QString DirectoryRenderer::statusText() const
{
    return m_status;
}

} // namespace pf::ui
