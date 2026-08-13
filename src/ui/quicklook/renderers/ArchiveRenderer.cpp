#include "ui/quicklook/renderers/ArchiveRenderer.h"

#include "fs/ArchiveReader.h"

#include <QHeaderView>
#include <QTreeWidget>

#include <algorithm>
#include <array>

namespace pf::ui {

bool ArchiveRenderer::isArchive(const QMimeType &mime)
{
    if (!mime.isValid()) {
        return false;
    }

    static constexpr std::array<QLatin1String, 16> kTypes{
        QLatin1String("application/zip"),
        QLatin1String("application/x-tar"),
        QLatin1String("application/gzip"),
        QLatin1String("application/x-bzip2"),
        QLatin1String("application/x-xz"),
        QLatin1String("application/zstd"),
        QLatin1String("application/x-7z-compressed"),
        QLatin1String("application/vnd.rar"),
        QLatin1String("application/x-rar-compressed"),
        QLatin1String("application/x-compressed-tar"),
        QLatin1String("application/x-bzip-compressed-tar"),
        QLatin1String("application/x-xz-compressed-tar"),
        QLatin1String("application/x-zstd-compressed-tar"),
        QLatin1String("application/x-cpio"),
        QLatin1String("application/x-archive"),
        QLatin1String("application/java-archive")};

    const QString name = mime.name();
    if (std::ranges::any_of(kTypes, [&name](QLatin1String type) { return name == type; })) {
        return true;
    }

    // Ancestry catches the rest: shared-mime-info makes the compressed-tar
    // types inherit from application/x-tar, and a format this list has never
    // heard of is still an archive if it says so.
    return mime.inherits(QStringLiteral("application/x-tar")) ||
           mime.inherits(QStringLiteral("application/zip"));
}

bool ArchiveRenderer::canRender(const QMimeType &mime, const FileEntry &entry) const
{
    return !entry.isDir && fs::ArchiveReader::isAvailable() && isArchive(mime);
}

QWidget *ArchiveRenderer::createWidget(QWidget *parent)
{
    if (m_tree == nullptr) {
        m_tree = new QTreeWidget(parent);
        m_tree->setColumnCount(3);
        m_tree->setHeaderLabels({tr("Entry"), tr("Size"), tr("Ratio")});
        m_tree->setRootIsDecorated(false);
        m_tree->setUniformRowHeights(true);
        m_tree->setSelectionMode(QAbstractItemView::NoSelection);
        m_tree->setFocusPolicy(Qt::NoFocus);
        m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    }
    return m_tree;
}

void ArchiveRenderer::setContent(QuickLookContent &&content)
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

    // The loader put the listing in `facts` as (entry, "size · ratio") pairs.
    for (const auto &[name, detail] : content.facts) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, name);

        const QStringList parts = detail.split(QLatin1Char('\x1f'));
        item->setText(1, parts.value(0));
        item->setText(2, parts.value(1));
    }

    m_status = content.text;
}

void ArchiveRenderer::clear()
{
    if (m_tree != nullptr) {
        m_tree->clear();
    }
    m_status.clear();
}

QString ArchiveRenderer::statusText() const
{
    return m_status;
}

} // namespace pf::ui
