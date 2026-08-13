#pragma once

#include "core/FuzzyMatcher.h"
#include "ui/ThemePalette.h"

#include <QStyledItemDelegate>

namespace pf {
struct FileEntry;
}

namespace pf::ui {

/// Paints one row of a file panel (§5.3).
///
/// Left to right: selection marker, icon or thumbnail, name with fuzzy-match
/// spans in the accent colour, symlink arrow and target, then right-aligned
/// size and modification time.
///
/// This is the hot path of the entire application. §11 asks for 60 fps
/// scrolling in a 100,000-entry directory, which at a 24-pixel row height is
/// roughly forty rows painted per frame — so nothing here allocates a
/// QFontMetrics, resolves an icon theme or consults QMimeDatabase. Anything of
/// that kind belongs in IconProvider's cache, not in paint().
class FileItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FileItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// Marks rows the panel considers selected (§6.1's Selection mode). The
    /// view's own selection model tracks the cursor; this is the separate,
    /// explicit multi-selection the file operations act on.
    void setSelectedNames(const QSet<QString> *names);

private:
    static QColor colourFor(const FileEntry &entry);

    /// §7.8: "Return both the score and the matched character spans so the
    /// delegate can highlight them."
    static void paintMatchSpans(QPainter *painter, const QRect &nameRect, const QString &elidedName,
                                const QString &fullName, const QModelIndex &index,
                                const QFontMetrics &metrics, const ThemePalette &palette);

    const QSet<QString> *m_selectedNames = nullptr;
};

} // namespace pf::ui
