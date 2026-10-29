#include "ui/modals/CommandPaletteModal.h"

#include "ui/Keycaps.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace pf::ui {
namespace {

constexpr int kRowRole = Qt::UserRole;       ///< Index into the visible rows, or -1 for a heading.
constexpr int kSpansRole = Qt::UserRole + 1; ///< Flat [start, length, …] of the title's match.
constexpr int kDetailRole = Qt::UserRole + 2;
constexpr int kKeysRole = Qt::UserRole + 3;
constexpr int kHeadingRole = Qt::UserRole + 4;

constexpr int kPad = 10;

class PaletteDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int height = index.data(kHeadingRole).toBool() ? option.fontMetrics.height() + 14
                                                             : option.fontMetrics.height() + 14;
        return {0, height};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const ThemePalette &palette = currentPalette();
        painter->save();

        if (index.data(kHeadingRole).toBool()) {
            QFont font = option.font;
            font.setBold(true);
            font.setCapitalization(QFont::AllUppercase);
            if (font.pixelSize() > 0) {
                font.setPixelSize(std::max(1, font.pixelSize() - 2));
            }
            painter->setFont(font);
            painter->setPen(palette.overlay);
            painter->drawText(option.rect.adjusted(kPad, 4, -kPad, 0),
                              Qt::AlignLeft | Qt::AlignBottom,
                              index.data(Qt::DisplayRole).toString());
            painter->restore();
            return;
        }

        if ((option.state & QStyle::State_Selected) != 0) {
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(palette.selectionBackground);
            painter->drawRoundedRect(QRectF(option.rect).adjusted(2, 1, -2, -1), 6, 6);
        }

        int right = option.rect.right() - kPad;
        const QString keys = index.data(kKeysRole).toString();
        if (!keys.isEmpty()) {
            const qreal width = keycaps::width(keys, option.font);
            keycaps::paint(painter, QPointF(right - width, option.rect.center().y()), keys,
                           option.font);
            right -= static_cast<int>(width) + kPad;
        }

        const QFontMetrics metrics(option.font);
        const QString detail = index.data(kDetailRole).toString();
        QString title = index.data(Qt::DisplayRole).toString();
        const QList<QVariant> spans = index.data(kSpansRole).toList();

        const int detailWidth = detail.isEmpty() ? 0 : metrics.horizontalAdvance(detail);
        const int titleRoom = right - option.rect.left() - 2 * kPad -
                              (detailWidth > 0 ? std::min(detailWidth, right / 3) + kPad : 0);
        if (metrics.horizontalAdvance(title) > titleRoom) {
            // Match spans index the full title; an elided one is drawn plain.
            title = metrics.elidedText(title, Qt::ElideRight, titleRoom);
        }

        const QRect textRect(option.rect.left() + kPad, option.rect.top(), titleRoom,
                             option.rect.height());
        painter->setFont(option.font);
        painter->setPen(palette.text);
        if (spans.isEmpty() || title != index.data(Qt::DisplayRole).toString()) {
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);
        } else {
            // Matched runs in bold and the accent, the rest as is.
            QFont bold = option.font;
            bold.setBold(true);
            int x = textRect.left();
            int at = 0;
            const auto draw = [&](const QString &piece, bool matched) {
                if (piece.isEmpty()) {
                    return;
                }
                painter->setFont(matched ? bold : option.font);
                painter->setPen(matched ? palette.accent : palette.text);
                painter->drawText(
                    QPoint(x, textRect.center().y() + metrics.ascent() / 2 - metrics.descent() / 2),
                    piece);
                x += QFontMetrics(painter->font()).horizontalAdvance(piece);
            };
            for (qsizetype i = 0; i + 1 < spans.size(); i += 2) {
                const int start = spans.at(i).toInt();
                const int length = spans.at(i + 1).toInt();
                draw(title.mid(at, start - at), false);
                draw(title.mid(start, length), true);
                at = start + length;
            }
            draw(title.mid(at), false);
        }

        if (detailWidth > 0) {
            painter->setFont(option.font);
            painter->setPen(palette.overlay);
            const QRect detailRect(option.rect.left() + kPad, option.rect.top(),
                                   right - option.rect.left() - kPad, option.rect.height());
            painter->drawText(detailRect, Qt::AlignRight | Qt::AlignVCenter,
                              metrics.elidedText(detail, Qt::ElideMiddle, right / 3));
        }
        painter->restore();
    }
};

} // namespace

CommandPaletteModal::CommandPaletteModal(QWidget *parent)
    : Modal(parent), m_query(new QLineEdit), m_list(new QListWidget), m_footer(new QLabel),
      m_count(new QLabel)
{
    setSizePercent(55, 70);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(10);

    m_query->setObjectName(QStringLiteral("findQuery"));
    m_query->setPlaceholderText(tr("Search actions and places"));
    layout->addWidget(m_query);

    m_list->setObjectName(QStringLiteral("findResults"));
    m_list->setItemDelegate(new PaletteDelegate(m_list));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_list, 1);

    auto *footer = new QHBoxLayout;
    m_footer->setObjectName(QStringLiteral("findStatus"));
    m_footer->setText(tr("↵ runs · ↑↓ moves · Esc closes"));
    m_count->setObjectName(QStringLiteral("findStatus"));
    footer->addWidget(m_footer, 1);
    footer->addWidget(m_count);
    layout->addLayout(footer);

    connect(m_query, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_list, &QListWidget::itemActivated, this, [this] { accept(); });
}

QWidget *CommandPaletteModal::initialFocusWidget()
{
    return m_query;
}

QList<PaletteRow> CommandPaletteModal::rank(const QList<PaletteItem> &items, const QString &query)
{
    const QString needle = query.trimmed();

    struct Scored {
        PaletteRow row;
        int order;
    };
    QList<Scored> scored;
    for (int i = 0; i < items.size(); ++i) {
        const PaletteItem &item = items.at(i);
        PaletteRow row{.item = item, .spans = {}, .score = 0};
        if (!needle.isEmpty()) {
            const FuzzyMatch byTitle = FuzzyMatcher::match(needle, item.title);
            const FuzzyMatch byId =
                item.id.isEmpty() ? FuzzyMatch{} : FuzzyMatcher::match(needle, item.id);
            if (!byTitle.matched && !byId.matched) {
                continue;
            }
            // The better score wins, but only a title match can be highlighted.
            if (byTitle.matched && (!byId.matched || byTitle.score >= byId.score)) {
                row.score = byTitle.score;
                row.spans = byTitle.spans;
            } else {
                row.score = byId.score;
            }
        }
        scored.append({row, i});
    }

    std::ranges::stable_sort(scored, [](const Scored &a, const Scored &b) {
        if (a.row.item.group != b.row.item.group) {
            return a.row.item.group < b.row.item.group;
        }
        if (a.row.score != b.row.score) {
            return a.row.score > b.row.score;
        }
        return a.order < b.order;
    });

    QList<PaletteRow> rows;
    rows.reserve(scored.size());
    for (const Scored &entry : scored) {
        rows.append(entry.row);
    }
    return rows;
}

void CommandPaletteModal::open(QList<PaletteItem> items, const QString &contextHeader)
{
    m_items = std::move(items);
    m_contextHeader = contextHeader;
    m_query->clear();
    refresh();
    showModal();
    m_query->setFocus(Qt::ShortcutFocusReason);
}

void CommandPaletteModal::setQuery(const QString &query)
{
    m_query->setText(query);
}

QList<PaletteItem> CommandPaletteModal::visibleItems() const
{
    QList<PaletteItem> items;
    for (const PaletteRow &row : m_rows) {
        items.append(row.item);
    }
    return items;
}

void CommandPaletteModal::refresh()
{
    m_rows = rank(m_items, m_query->text());
    m_list->clear();

    const bool grouped = m_query->text().trimmed().isEmpty();
    int lastGroup = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const PaletteRow &row = m_rows.at(i);
        if (grouped && row.item.group != lastGroup) {
            lastGroup = row.item.group;
            QString heading;
            if (lastGroup == 0) {
                heading = m_contextHeader;
            } else if (lastGroup == 1) {
                heading = tr("Actions");
            } else {
                heading = tr("Places");
            }
            if (!heading.isEmpty()) {
                auto *item = new QListWidgetItem(heading, m_list);
                item->setData(kHeadingRole, true);
                item->setData(kRowRole, -1);
                item->setFlags(Qt::NoItemFlags);
            }
        }
        auto *item = new QListWidgetItem(row.item.title, m_list);
        item->setData(kRowRole, i);
        item->setData(kDetailRole, row.item.detail);
        item->setData(kKeysRole, row.item.keys);
        QList<QVariant> spans;
        for (const MatchSpan &span : row.spans) {
            spans << span.start << span.length;
        }
        item->setData(kSpansRole, spans);
    }

    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kRowRole).toInt() >= 0) {
            m_list->setCurrentRow(i);
            break;
        }
    }
    m_count->setText(tr("%n command(s)", nullptr, static_cast<int>(m_rows.size())));
}

void CommandPaletteModal::moveSelection(int delta)
{
    int row = m_list->currentRow();
    const int step = delta < 0 ? -1 : 1;
    for (int remaining = std::abs(delta); remaining > 0; --remaining) {
        int next = row + step;
        while (next >= 0 && next < m_list->count() &&
               m_list->item(next)->data(kRowRole).toInt() < 0) {
            next += step;
        }
        if (next < 0 || next >= m_list->count()) {
            break;
        }
        row = next;
    }
    if (row >= 0) {
        m_list->setCurrentRow(row);
    }
}

void CommandPaletteModal::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Down:
        moveSelection(1);
        return;
    case Qt::Key_Up:
        moveSelection(-1);
        return;
    case Qt::Key_PageDown:
        moveSelection(8);
        return;
    case Qt::Key_PageUp:
        moveSelection(-8);
        return;
    default:
        break;
    }
    Modal::keyPressEvent(event);
}

void CommandPaletteModal::accept()
{
    const QListWidgetItem *current = m_list->currentItem();
    const int index = current != nullptr ? current->data(kRowRole).toInt() : -1;

    // What to emit is settled before the palette is dismissed, and emitted
    // after: an action that opens its own modal must not be dismissed with us.
    QString actionId;
    QString placePath;
    if (index >= 0 && index < m_rows.size()) {
        const PaletteItem &item = m_rows.at(index).item;
        if (item.group == 2) {
            placePath = item.path;
        } else {
            actionId = item.id;
        }
    } else if (m_list->count() == 0 && !m_query->text().trimmed().isEmpty()) {
        // Nothing matched: the text may still be an exact action id, as the
        // prompt this replaces took. The application says if it is not.
        actionId = m_query->text().trimmed();
    }

    Modal::accept();

    if (!placePath.isEmpty()) {
        Q_EMIT placeChosen(placePath);
    } else if (!actionId.isEmpty()) {
        Q_EMIT actionChosen(actionId);
    }
}

} // namespace pf::ui
