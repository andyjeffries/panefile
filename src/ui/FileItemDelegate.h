#pragma once

#include "core/FuzzyMatcher.h"
#include "ui/ThemePalette.h"

#include <QCache>
#include <QStringList>
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
    /// Where a row's right-hand columns go, and whether there is room for them.
    ///
    /// Pure, and public, because this is the part that decides whether a
    /// filename is readable — and it got that wrong in two ways at once: a
    /// directory reserved space for a size it never prints, and nothing stopped
    /// the name's width going negative in a narrow panel. Testing it through
    /// painted pixels would be testing QPainter.
    struct RowColumns {
        bool showSize = false;
        bool showTime = false;
        int sizeLeft = 0;
        int timeLeft = 0;
        int nameRight = 0;
    };

    /// `nameLeft` is where the name starts, `rowRight` where the row's content
    /// ends. `hasSize` is false for directories, which print no size.
    static RowColumns columnsFor(int nameLeft, int rowRight, int columnGap, bool hasSize,
                                 bool hasTime);

    /// What a tile (icon view) is made of. Pure, and shared with the panel so
    /// the view's grid and the delegate's painting cannot disagree.
    struct TileMetrics {
        QSize tile;       ///< the whole cell
        int iconSize = 0; ///< side of the icon or thumbnail box
        int padding = 0;  ///< cell edge to icon, and name to cell edge
        int nameGap = 0;  ///< icon to name
        int lineHeight = 0;
        static constexpr int kNameLines = 2;
    };
    static TileMetrics tileMetricsFor(const ThemePalette &palette, const QFontMetrics &metrics);

    /// A name broken for a tile: at most two lines, each within `width`.
    ///
    /// Breaks at a space or after a hyphen; failing that, before the
    /// extension's dot (`DSC_0498` / `.heic`); only as a last resort anywhere.
    /// The second line is elided in the middle so the extension survives.
    static QStringList wrapName(const QString &name, const QFontMetrics &metrics, int width);

    /// A folder drawn closed when empty and with its glyph where it has one,
    /// else the type icon. Both layouts draw their icon through here; public
    /// so the empty and glyph roles can be tested without painting.
    static QIcon iconFor(const QModelIndex &index, const FileEntry &entry, const QColor &colour);

    enum class Layout { Row, Tile };

    explicit FileItemDelegate(QObject *parent = nullptr);

    /// Rows (the list) or tiles (the icon view). The view's item size changes
    /// with it, so the caller re-lays the view out.
    void setLayout(Layout layout);
    Layout layout() const;

    /// Drops cached name wrapping, for a font or size change.
    void clearCaches();

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// Marks rows the panel considers selected (§6.1's Selection mode). The
    /// view's own selection model tracks the cursor; this is the separate,
    /// explicit multi-selection the file operations act on.
    void setSelectedNames(const QSet<QString> *names);

private:
    void paintTile(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index,
                   const FileEntry &entry) const;

    static QColor colourFor(const FileEntry &entry);

    /// §7.8: "Return both the score and the matched character spans so the
    /// delegate can highlight them."
    static void paintMatchSpans(QPainter *painter, const QRect &nameRect, const QString &elidedName,
                                const QString &fullName, const QModelIndex &index,
                                const QFontMetrics &metrics, const ThemePalette &palette);

    const QSet<QString> *m_selectedNames = nullptr;
    Layout m_layout = Layout::Row;

    /// Wrapped names by (width, font, name): wrapping measures text many times,
    /// and a scroll repaints the same names constantly.
    mutable QCache<QString, QStringList> m_wrapCache{1024};
};

} // namespace pf::ui
