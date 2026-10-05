#include "ui/FileItemDelegate.h"

#include "core/Format.h"
#include "core/FuzzyMatcher.h"
#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "model/IconProvider.h"
#include "ui/PanelView.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <array>

namespace pf::ui {
namespace {

constexpr int kIconSize = 16;

/// The cursor and selection pill: inset from the row's edges and rounded, the
/// way a macOS list selection is drawn.
constexpr int kPillInset = 4;
constexpr int kPillRadius = 6;

/// The row's metadata face: a step smaller than the name, with figures of equal
/// width.
///
/// Proportional digits give a right-aligned column a ragged edge — "1" is
/// narrower than "0" in most UI faces — which is most of why a list of dates
/// looks untidy even when every entry is correctly aligned.
QFont metadataFont(const QFont &base)
{
    QFont font = base;
    if (base.pointSize() > 0) {
        font.setPointSize(std::max(1, base.pointSize() - 1));
    } else if (base.pixelSize() > 0) {
        font.setPixelSize(std::max(1, base.pixelSize() - 1));
    }
    font.setFeature(QFont::Tag("tnum"), 1);
    return font;
}

QColor blend(const QColor &from, const QColor &towards, double amount)
{
    return config::mixColours(from, towards, amount);
}

constexpr int kSelectionMarkerWidth = 3;

/// The row's horizontal rhythm, derived from the theme's panel padding rather
/// than fixed.
///
/// Finder's spacing is not one number: the gutter before the icon, the gap
/// after it, and the gap between the size and time columns are all different,
/// and all of them scale together when the density changes. Deriving them from
/// one theme value keeps that relationship intact, and means a user who wants a
/// tighter list changes one number instead of recompiling.
struct Metrics {
    int gutter;      ///< left edge to icon
    int afterIcon;   ///< icon to filename
    int columnGap;   ///< between the size and time columns
    int rightMargin; ///< time column to right edge
};

Metrics metricsFor(const ThemePalette &palette)
{
    const int base = std::max(4, palette.panelPadding);
    return Metrics{
        .gutter = base, .afterIcon = base - 2, .columnGap = base + 4, .rightMargin = base};
}

/// Width reserved for the right-hand size and time columns. Fixed rather than
/// measured per row: a column whose width depends on its widest visible value
/// shifts as you scroll, and §11's frame budget does not have room for a
/// QFontMetrics pass over every row anyway.
/// The narrowest a name may be squeezed before a column is dropped instead.
///
/// Roughly ten characters at the default size: enough to tell two entries apart,
/// which is the least a filename can usefully do.
constexpr int kMinimumNameWidth = 96;

/// Below this row height a thumbnail is smaller than the glyph it replaces and
/// says less than it.
constexpr int kThumbnailMinimumRowHeight = 40;

constexpr int kSizeColumnWidth = 64;
constexpr int kTimeColumnWidth = 78;

bool isArchiveName(const QString &name)
{
    static constexpr std::array<QLatin1String, 12> kSuffixes{
        QLatin1String(".zip"), QLatin1String(".tar"), QLatin1String(".gz"),  QLatin1String(".bz2"),
        QLatin1String(".xz"),  QLatin1String(".zst"), QLatin1String(".7z"),  QLatin1String(".rar"),
        QLatin1String(".tgz"), QLatin1String(".tbz"), QLatin1String(".lz4"), QLatin1String(".jar")};

    return std::ranges::any_of(kSuffixes, [&name](QLatin1String suffix) {
        return name.endsWith(suffix, Qt::CaseInsensitive);
    });
}

bool isImageName(const QString &name)
{
    static constexpr std::array<QLatin1String, 9> kSuffixes{
        QLatin1String(".png"), QLatin1String(".jpg"),  QLatin1String(".jpeg"),
        QLatin1String(".gif"), QLatin1String(".bmp"),  QLatin1String(".webp"),
        QLatin1String(".svg"), QLatin1String(".avif"), QLatin1String(".tiff")};

    return std::ranges::any_of(kSuffixes, [&name](QLatin1String suffix) {
        return name.endsWith(suffix, Qt::CaseInsensitive);
    });
}

} // namespace

FileItemDelegate::FileItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void FileItemDelegate::setSelectedNames(const QSet<QString> *names)
{
    m_selectedNames = names;
}

QSize FileItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    // §5.3: fixed per theme. The view sets uniformItemSizes, which means this is
    // asked once and applied to every row — it must not depend on the content.
    return {0, currentPalette().rowHeight};
}

QColor FileItemDelegate::colourFor(const FileEntry &entry)
{
    const ThemePalette &palette = currentPalette();

    // Order matters: a broken symlink is broken before it is anything else, and
    // a directory is a directory before it is executable — every directory has
    // its execute bit set, so testing executable first would paint the entire
    // listing green.
    if (entry.isBroken) {
        return palette.broken;
    }
    if (entry.isSymlink) {
        return palette.symlink;
    }
    if (entry.isDir) {
        return palette.directory;
    }
    if (entry.isExecutable) {
        return palette.executable;
    }
    if (isArchiveName(entry.name)) {
        return palette.archive;
    }
    if (isImageName(entry.name)) {
        return palette.image;
    }
    // Secondary, not primary: a column of near-black document glyphs is
    // heavier than the names beside them, and the icon is the quieter half of
    // the row.
    return palette.subtext;
}

FileItemDelegate::RowColumns FileItemDelegate::columnsFor(int nameLeft, int rowRight, int columnGap,
                                                          bool hasSize, bool hasTime)
{
    // Which columns there is room for is decided per row, and the name wins.
    //
    // Both used to be reserved unconditionally, so a directory — which prints
    // no size — still had 64px held back for one, and "Applications" came out
    // as "Ap…ns" in a panel with room to spare. Narrower still and the name's
    // width went negative, which is why five panels showed no filenames at all
    // rather than short ones.
    //
    // A file manager whose columns are legible and whose filenames are not has
    // its priorities backwards. The size column goes first, then the date,
    // before the name is squeezed below what it takes to tell two entries
    // apart.
    RowColumns columns;
    columns.showSize = hasSize;
    columns.showTime = hasTime;
    columns.timeLeft = rowRight - kTimeColumnWidth;
    columns.sizeLeft = columns.timeLeft - columnGap - kSizeColumnWidth;

    if (columns.showSize && (columns.sizeLeft - columnGap - nameLeft) < kMinimumNameWidth) {
        columns.showSize = false;
    }
    if (columns.showTime && (columns.timeLeft - columnGap - nameLeft) < kMinimumNameWidth) {
        columns.showTime = false;
    }

    if (!columns.showTime) {
        columns.timeLeft = rowRight;
    }
    if (!columns.showSize) {
        columns.sizeLeft = columns.timeLeft;
    }

    columns.nameRight =
        columns.showSize || columns.showTime ? columns.sizeLeft - columnGap : rowRight;
    return columns;
}

void FileItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    const QVariant entryValue = index.data(DirectoryModel::EntryRole);
    if (!entryValue.canConvert<FileEntry>()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    const auto entry = entryValue.value<FileEntry>();
    const ThemePalette &palette = currentPalette();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    const QRect row = option.rect;
    const bool isCursor = (option.state & QStyle::State_HasFocus) != 0 ||
                          (option.state & QStyle::State_Selected) != 0;
    const bool isSelected = m_selectedNames != nullptr && m_selectedNames->contains(entry.name);

    // Row banding, under everything else. Finder's most quietly useful habit:
    // across a wide row the eye loses the line between a filename on the left
    // and a date on the right, and a few percent of tint is enough to keep it.
    //
    // Banded on the *view's* row, not the model's, so the stripes stay
    // alternating through a filter rather than developing gaps where rows were
    // removed.
    //
    // Only in the focused panel. Banding is a reading aid for the list you are
    // working in, and running it everywhere makes two panels compete for the
    // eye; switching it off with focus turns it into a third quiet signal of
    // where you are, alongside the cursor's tint and the top edge.
    //
    // Banding is off in every bundled theme now, but remains a theme option.
    //
    // "Focused" is the panel's own answer, not QStyle::State_Active: that is the
    // window's activation, and it went false whenever a modal opened or the
    // filter field took the keyboard — see PanelView::setPanelActive.
    const auto *view = qobject_cast<const PanelView *>(option.widget);
    const bool panelActive =
        view != nullptr ? view->isPanelActive() : (option.state & QStyle::State_Active) != 0;
    const QColor panelBackground = panelActive ? palette.surface : palette.background;

    if (palette.alternatingRows && panelActive && (index.row() % 2) == 1) {
        painter->fillRect(row, palette.effectiveAlternateRowBackground());
    }

    // Background. The cursor row and an explicitly selected row are different
    // states and must look different: in Selection mode the cursor moves
    // through rows that are already selected, and a user needs to see both.
    //
    // A filled pill rather than a full-bleed band: a 6px radius inset from the
    // row's edges is what macOS uses for a list selection, and it reads as a
    // thing the cursor is *on* rather than as a stripe painted across the panel.
    //
    // In a focused panel the cursor pill is the focus colour's soft tint; in an
    // unfocused one it is the neutral cursor colour. That difference is the
    // cheapest possible answer to "which panel am I typing into", and it works
    // even when the panel border is off the edge of your attention.
    //
    // A tint rather than a solid fill, so the row keeps its own text and icon
    // colours. A solid accent pill with inverted text was the loudest thing in
    // the window and pulled the eye off the content to itself.
    //
    // The pointer gets the same shape a few percent off the panel, so a mouse
    // user can see which row a click will land on.
    const bool isHovered = (option.state & QStyle::State_MouseOver) != 0;
    QColor pillFill;
    if (isCursor) {
        pillFill = panelActive ? palette.focusedCursorBackground() : palette.cursorBackground;
    } else if (isSelected) {
        pillFill = palette.selectionBackground;
    } else if (isHovered) {
        pillFill = palette.hoverBackground(panelBackground);
    }

    if (pillFill.isValid()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(pillFill);
        painter->drawRoundedRect(QRectF(row.adjusted(kPillInset, 1, -kPillInset, -1)), kPillRadius,
                                 kPillRadius);
        painter->restore();
    }

    const Metrics spacing = metricsFor(palette);
    int x = row.left() + spacing.gutter;

    // §7.12: "Highlight the drop target row clearly." Drawn before everything
    // else so the row's own content sits on top of it, and with the focused
    // border colour rather than the selection colour so it cannot be mistaken
    // for a selection the user made.
    // Both sides speak in proxy rows: dragMoveEvent got its row from
    // indexAt(), which the view answers in its own coordinates, and so is this
    // index. Comparing a proxy row against a source row would highlight a
    // different file whenever a filter or sort was active.
    //
    // The pill's shape, outlined: the same "this row" the cursor draws, so the
    // target reads as a place rather than as a slab of colour across the panel.
    if (view != nullptr && index.row() == view->dropTargetRow()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(palette.borderFocused, 1.5));
        painter->setBrush(palette.focusedCursorBackground());
        painter->drawRoundedRect(
            QRectF(row.adjusted(kPillInset, 1, -kPillInset, -1)).adjusted(0.75, 0.75, -0.75, -0.75),
            kPillRadius, kPillRadius);
        painter->restore();
    }

    // Selection marker: a rounded bar in the gutter, beside the pill rather
    // than inside it, so a marked row under the cursor still shows both.
    if (isSelected) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.accent);
        const QRectF marker(row.left() + 0.5, row.top() + 6, kSelectionMarkerWidth,
                            row.height() - 12);
        painter->drawRoundedRect(marker, kSelectionMarkerWidth / 2.0, kSelectionMarkerWidth / 2.0);
        painter->restore();
    }

    // The icon carries the entry's colour; the name does not.
    //
    // Both used to be tinted, so a panel of directories was a panel of blue
    // text — which reads as a page of hyperlinks, and spends contrast on
    // something the glyph beside it already says. Names now sit in the primary
    // label colour, and the folder, archive or image tint lives in the icon.
    //
    // A broken symlink is the exception, because there is no icon state that
    // says "this points nowhere" as plainly as the name being struck through in
    // the error colour.
    QColor iconColour = colourFor(entry);
    if (!panelActive) {
        // The accent belongs to the panel being worked in. A pane full of
        // saturated blue folders pulls the eye whether or not it has focus,
        // which is the opposite of what the colour is for.
        iconColour = blend(iconColour, palette.background, 0.30);
    }

    QColor nameColour = palette.text;
    if (entry.isBroken) {
        nameColour = palette.broken;
    } else if (!panelActive) {
        // A step quieter in a panel that is not focused. Two panels of
        // identically-weighted text make the eye choose between them; this is
        // the same signal the accent edge and the cursor pill give, said a
        // third time for anyone reading the list rather than its frame.
        nameColour = blend(palette.text, palette.background, 0.22);
    }
    const QRect iconRect(x, row.top() + ((row.height() - kIconSize) / 2), kIconSize, kIconSize);

    // §7.7: a generated thumbnail replaces the icon for the row it belongs to.
    // Served from the model's memory tier, which is why this stays a hash
    // lookup and not the file read a thumbnail would otherwise imply.
    //
    // Only when a row is tall enough to show one. At 28 pixels a photograph
    // scaled to 16 is an unreadable smudge that reads as a fourth icon style
    // competing with the glyphs beside it — it costs the row's legibility and
    // returns nothing. A taller row mode earns them back.
    const bool thumbnailsFit = palette.rowHeight >= kThumbnailMinimumRowHeight;
    const QVariant thumbnail =
        thumbnailsFit ? index.data(DirectoryModel::ThumbnailRole) : QVariant();
    const auto image = thumbnail.canConvert<QImage>() ? thumbnail.value<QImage>() : QImage();

    if (!image.isNull()) {
        const QSize scaled = image.size().scaled(iconRect.size(), Qt::KeepAspectRatio);
        painter->drawImage(QRect(iconRect.x() + ((iconRect.width() - scaled.width()) / 2),
                                 iconRect.y() + ((iconRect.height() - scaled.height()) / 2),
                                 scaled.width(), scaled.height()),
                           image);
    } else if (const QIcon icon = IconProvider::instance().iconFor(entry, iconColour);
               !icon.isNull()) {
        icon.paint(painter, iconRect, Qt::AlignCenter,
                   entry.isBroken ? QIcon::Disabled : QIcon::Normal);
    }

    x += kIconSize + spacing.afterIcon;

    // Right-hand columns are laid out first so the name knows how much room it
    // has left, rather than being elided against the full row width and then
    // overdrawn.
    //
    // Which columns there is room for is decided per row, and the name wins.
    // Both used to be reserved unconditionally: a directory, which prints no
    // size, still had 64px held back for one — so "Applications" came out as
    // "Ap…ns" in a panel with room to spare. Narrower still and the name's
    // width went negative, which is why five panels showed no filenames at all
    // rather than short ones.
    //
    // A file manager whose columns are legible and whose filenames are not has
    // its priorities backwards. The date goes before the name is squeezed, and
    // the size goes before that.
    const int rowRight = row.right() - spacing.rightMargin;
    const RowColumns columns =
        columnsFor(x, rowRight, spacing.columnGap, !entry.isDir, !entry.statFailed);
    const bool showSize = columns.showSize;
    const bool showTime = columns.showTime;
    const int sizeLeft = columns.sizeLeft;
    const int timeLeft = columns.timeLeft;

    const QFontMetrics metrics = option.fontMetrics;

    if (!entry.statFailed) {
        // A step quieter than the name, and quieter again in a panel that is
        // not focused.
        QColor metaColour = palette.subtext;
        if (!panelActive) {
            metaColour = blend(palette.subtext, palette.background, 0.25);
        }
        painter->setPen(metaColour);

        // A step smaller than the name, and figures of equal width. Proportional
        // digits give a right-aligned column a ragged edge — "11" is narrower
        // than "00" in most UI faces — which is most of why a list of dates
        // looks untidy even when every one of them is correctly aligned.
        painter->setFont(metadataFont(option.font));

        if (showSize) {
            const QRect sizeRect(sizeLeft, row.top(), kSizeColumnWidth, row.height());
            painter->drawText(sizeRect, Qt::AlignRight | Qt::AlignVCenter, formatSize(entry.size));
        }

        if (showTime) {
            const QRect timeRect(timeLeft, row.top(), kTimeColumnWidth, row.height());
            painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter,
                              formatListTime(entry.modified));
        }
    }

    painter->setFont(option.font);

    // Name, plus the symlink target if there is room for it.
    const QRect nameRect(x, row.top(), std::max(0, columns.nameRight - x), row.height());

    QFont nameFont = option.font;
    if (entry.isBroken) {
        nameFont.setStrikeOut(true);
    }
    painter->setFont(nameFont);
    painter->setPen(nameColour);

    // No trailing slash. The icon has already said it is a directory, and the
    // slash is a second answer to a question nobody asked twice.
    const QString name = entry.name;

    const QString elidedName = metrics.elidedText(name, Qt::ElideMiddle, nameRect.width());
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    // §7.8: the matched characters, in the accent colour. Drawn over the name
    // rather than instead of it, so the elision above still governs the layout
    // and a highlight can never make a row wider than its column.
    paintMatchSpans(painter, nameRect, elidedName, name, index, metrics, palette);

    if (entry.isSymlink && !entry.linkTarget.isEmpty()) {
        const int nameWidth = metrics.horizontalAdvance(elidedName);
        const int targetLeft = nameRect.left() + nameWidth + spacing.afterIcon;
        const int available = nameRect.right() - targetLeft;

        // Only when it genuinely fits. A symlink target elided down to "…" is
        // noise in a column that is already tight.
        if (available > 40) {
            const QRect targetRect(targetLeft, row.top(), available, row.height());
            painter->setFont(option.font);
            painter->setPen(palette.overlay);
            painter->drawText(targetRect, Qt::AlignLeft | Qt::AlignVCenter,
                              metrics.elidedText(QStringLiteral("→ ") + entry.linkTarget,
                                                 Qt::ElideMiddle, available));
        }
    }

    painter->restore();
}

void FileItemDelegate::paintMatchSpans(QPainter *painter, const QRect &nameRect,
                                       const QString &elidedName, const QString &fullName,
                                       const QModelIndex &index, const QFontMetrics &metrics,
                                       const ThemePalette &palette)
{
    // Only when the name was not elided. Mapping a span through Qt's elision —
    // which removes an unknown run from the middle — would need the elision
    // algorithm reimplemented to stay correct, and a highlight one character
    // off is worse than no highlight.
    if (elidedName != fullName) {
        return;
    }

    const QVariant value = index.data(DirectoryModel::MatchSpansRole);
    if (!value.canConvert<QList<MatchSpan>>()) {
        return;
    }

    const auto spans = value.value<QList<MatchSpan>>();
    if (spans.isEmpty()) {
        return;
    }

    painter->setPen(palette.accent);

    for (const MatchSpan &span : spans) {
        if (span.start < 0 || span.start + span.length > fullName.size()) {
            continue;
        }

        const int left = nameRect.left() + metrics.horizontalAdvance(fullName.left(span.start));
        const QString text = fullName.mid(span.start, span.length);
        const QRect spanRect(left, nameRect.top(), metrics.horizontalAdvance(text),
                             nameRect.height());

        painter->drawText(spanRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}

} // namespace pf::ui
