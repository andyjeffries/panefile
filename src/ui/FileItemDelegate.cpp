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
constexpr int kHorizontalPadding = 6;
constexpr int kColumnGap = 12;
constexpr int kSelectionMarkerWidth = 3;

/// Width reserved for the right-hand size and time columns. Fixed rather than
/// measured per row: a column whose width depends on its widest visible value
/// shifts as you scroll, and §11's frame budget does not have room for a
/// QFontMetrics pass over every row anyway.
constexpr int kSizeColumnWidth = 74;
constexpr int kTimeColumnWidth = 84;

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
    return palette.text;
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

    // Background. The cursor row and an explicitly selected row are different
    // states and must look different: in Selection mode the cursor moves
    // through rows that are already selected, and a user needs to see both.
    if (isCursor) {
        painter->fillRect(row, palette.cursorBackground);
    } else if (isSelected) {
        painter->fillRect(row, palette.selectionBackground);
    }

    int x = row.left() + kHorizontalPadding;

    // §7.12: "Highlight the drop target row clearly." Drawn before everything
    // else so the row's own content sits on top of it, and with the focused
    // border colour rather than the selection colour so it cannot be mistaken
    // for a selection the user made.
    // Both sides speak in proxy rows: dragMoveEvent got its row from
    // indexAt(), which the view answers in its own coordinates, and so is this
    // index. Comparing a proxy row against a source row would highlight a
    // different file whenever a filter or sort was active.
    if (const auto *view = qobject_cast<const PanelView *>(option.widget);
        view != nullptr && index.row() == view->dropTargetRow()) {
        QColor highlight = palette.accent;
        highlight.setAlpha(70);
        painter->fillRect(row, highlight);
        painter->setPen(palette.borderFocused);
        painter->drawRect(row.adjusted(0, 0, -1, -1));
    }

    // Selection marker.
    if (isSelected) {
        const QRect marker(row.left(), row.top(), kSelectionMarkerWidth, row.height());
        painter->fillRect(marker, palette.accent);
    }

    // Icon, tinted to the same colour the name is painted in, so the row reads
    // as one thing rather than as a picture next to some text.
    const QColor nameColour = colourFor(entry);
    const QRect iconRect(x, row.top() + ((row.height() - kIconSize) / 2), kIconSize, kIconSize);

    // §7.7: a generated thumbnail replaces the icon for the row it belongs to.
    // Served from the model's memory tier, which is why this stays a hash
    // lookup and not the file read a thumbnail would otherwise imply.
    const QVariant thumbnail = index.data(DirectoryModel::ThumbnailRole);
    const auto image = thumbnail.canConvert<QImage>() ? thumbnail.value<QImage>() : QImage();

    if (!image.isNull()) {
        const QSize scaled = image.size().scaled(iconRect.size(), Qt::KeepAspectRatio);
        painter->drawImage(QRect(iconRect.x() + ((iconRect.width() - scaled.width()) / 2),
                                 iconRect.y() + ((iconRect.height() - scaled.height()) / 2),
                                 scaled.width(), scaled.height()),
                           image);
    } else if (const QIcon icon = IconProvider::instance().iconFor(entry, nameColour);
               !icon.isNull()) {
        icon.paint(painter, iconRect, Qt::AlignCenter,
                   entry.isBroken ? QIcon::Disabled : QIcon::Normal);
    }

    x += kIconSize + kHorizontalPadding;

    // Right-hand columns are laid out first so the name knows how much room it
    // has left, rather than being elided against the full row width and then
    // overdrawn.
    const int timeLeft = row.right() - kHorizontalPadding - kTimeColumnWidth;
    const int sizeLeft = timeLeft - kColumnGap - kSizeColumnWidth;

    const QFontMetrics metrics = option.fontMetrics;

    if (!entry.statFailed) {
        painter->setPen(palette.subtext);

        if (!entry.isDir) {
            const QRect sizeRect(sizeLeft, row.top(), kSizeColumnWidth, row.height());
            painter->drawText(sizeRect, Qt::AlignRight | Qt::AlignVCenter, formatSize(entry.size));
        }

        const QRect timeRect(timeLeft, row.top(), kTimeColumnWidth, row.height());
        painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter,
                          formatListTime(entry.modified));
    }

    // Name, plus the symlink target if there is room for it.
    const int nameRight = sizeLeft - kColumnGap;
    const QRect nameRect(x, row.top(), nameRight - x, row.height());

    QFont nameFont = option.font;
    if (entry.isBroken) {
        nameFont.setStrikeOut(true);
    }
    painter->setFont(nameFont);
    painter->setPen(nameColour);

    QString name = entry.name;
    if (entry.isDir) {
        name += QLatin1Char('/');
    }

    const QString elidedName = metrics.elidedText(name, Qt::ElideMiddle, nameRect.width());
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    // §7.8: the matched characters, in the accent colour. Drawn over the name
    // rather than instead of it, so the elision above still governs the layout
    // and a highlight can never make a row wider than its column.
    paintMatchSpans(painter, nameRect, elidedName, name, index, metrics, palette);

    if (entry.isSymlink && !entry.linkTarget.isEmpty()) {
        const int nameWidth = metrics.horizontalAdvance(elidedName);
        const int targetLeft = nameRect.left() + nameWidth + kHorizontalPadding;
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
