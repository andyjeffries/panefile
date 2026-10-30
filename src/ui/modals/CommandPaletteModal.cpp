#include "ui/modals/CommandPaletteModal.h"

#include "model/SymbolicIcon.h"
#include "ui/Keycaps.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace pf::ui {
namespace {

constexpr int kRowRole = Qt::UserRole;       ///< Index into the visible rows.
constexpr int kSpansRole = Qt::UserRole + 1; ///< Flat [start, length, …] of the title's match.
constexpr int kDetailRole = Qt::UserRole + 2;
constexpr int kKeysRole = Qt::UserRole + 3;
constexpr int kIconRole = Qt::UserRole + 4;
constexpr int kGroupRole = Qt::UserRole + 5;

constexpr int kPad = 10;
constexpr int kIconSize = 16;
constexpr int kGap = 8;

class PaletteDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(index)
        return {0, option.fontMetrics.height() + 12};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const ThemePalette &palette = currentPalette();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        if ((option.state & QStyle::State_Selected) != 0) {
            // Neutral, not the accent: the reference's highlight is a quiet grey.
            painter->setPen(Qt::NoPen);
            painter->setBrush(
                config::mixColours(palette.surface, palette.text, palette.isLight() ? 0.07 : 0.10));
            painter->drawRoundedRect(QRectF(option.rect).adjusted(0, 1, 0, -1),
                                     palette.borderRadius, palette.borderRadius);
        }

        const int group = index.data(kGroupRole).toInt();
        int left = option.rect.left() + kPad;
        const QString icon = index.data(kIconRole).toString();
        if (!icon.isEmpty()) {
            const QColor tint = group == 2 ? palette.directory : palette.subtext;
            const QPixmap pixmap = SymbolicIcon::pixmap(icon, tint, kIconSize,
                                                        painter->device()->devicePixelRatioF());
            painter->drawPixmap(QPoint(left, option.rect.center().y() - kIconSize / 2 + 1),
                                pixmap);
        }
        left += kIconSize + kGap;

        int right = option.rect.right() - kPad;
        const QString keys = index.data(kKeysRole).toString();
        if (!keys.isEmpty()) {
            const qreal width = keycaps::width(keys, option.font);
            painter->setOpacity(0.55);
            keycaps::paint(painter, QPointF(right - width, option.rect.center().y()), keys,
                           option.font);
            painter->setOpacity(1.0);
            right -= static_cast<int>(width) + kPad;
        }

        const QFontMetrics metrics(option.font);
        const QString detail = index.data(kDetailRole).toString();
        const QString fullTitle = index.data(Qt::DisplayRole).toString();
        QString title = fullTitle;
        const QList<QVariant> spans = index.data(kSpansRole).toList();

        // The title has priority up to two thirds of the row; the detail takes
        // what is left, right after it.
        const int room = std::max(0, right - left);
        const int detailWidth = detail.isEmpty() ? 0 : metrics.horizontalAdvance(detail) + kGap;
        const int titleWidth = metrics.horizontalAdvance(title);
        int titleRoom = room;
        if (detailWidth > 0) {
            titleRoom = std::min(titleWidth, std::max(room - detailWidth, room * 65 / 100));
        }
        if (titleWidth > titleRoom) {
            // Match spans index the full title; an elided one is drawn plain.
            title = metrics.elidedText(title, Qt::ElideRight, titleRoom);
        }

        const int baseline = option.rect.center().y() + metrics.ascent() / 2 -
                             metrics.descent() / 2 + 1;
        painter->setFont(option.font);
        painter->setPen(palette.text);
        if (spans.isEmpty() || title != fullTitle) {
            painter->drawText(QPoint(left, baseline), title);
        } else {
            // Matched runs in bold, the rest as is; same colour, no accent.
            QFont bold = option.font;
            bold.setBold(true);
            int x = left;
            int at = 0;
            const auto draw = [&](const QString &piece, bool matched) {
                if (piece.isEmpty()) {
                    return;
                }
                painter->setFont(matched ? bold : option.font);
                painter->drawText(QPoint(x, baseline), piece);
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

        if (!detail.isEmpty()) {
            const int detailLeft = left + metrics.horizontalAdvance(title) + kGap;
            const int detailRoom = right - detailLeft;
            if (detailRoom > 12) {
                painter->setFont(option.font);
                painter->setPen(palette.overlay);
                const Qt::TextElideMode mode = group == 2 ? Qt::ElideMiddle : Qt::ElideRight;
                painter->drawText(QPoint(detailLeft, baseline),
                                  metrics.elidedText(detail, mode, detailRoom));
            }
        }
        painter->restore();
    }
};

/// A keycap and what it does, for the footer.
class KeyHint : public QWidget
{
public:
    KeyHint(const QString &keys, const QString &label, QWidget *parent)
        : QWidget(parent), m_keys(keys), m_label(label)
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

    QSize sizeHint() const override
    {
        const QFont font = labelFont();
        return {static_cast<int>(keycaps::width(m_keys, font)) + kGap +
                    QFontMetrics(font).horizontalAdvance(m_label),
                static_cast<int>(keycaps::kHeight)};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QFont font = labelFont();
        const qreal used = keycaps::paint(&painter, QPointF(0, height() / 2.0), m_keys, font);
        painter.setFont(font);
        painter.setPen(currentPalette().overlay);
        painter.drawText(QRectF(used + kGap, 0, width() - used - kGap, height()),
                         Qt::AlignLeft | Qt::AlignVCenter, m_label);
    }

private:
    QFont labelFont() const
    {
        QFont font = this->font();
        font.setPixelSize(std::max(1, currentPalette().fontSize - 1));
        return font;
    }

    QString m_keys;
    QString m_label;
};

} // namespace

CommandPaletteModal::CommandPaletteModal(QWidget *parent)
    : Modal(parent), m_query(new QLineEdit), m_list(new QListWidget)
{
    setSizePercent(42, 50);
    setMaximumContentSize({640, 460});
    // Scopes the stylesheet's palette rules to this card alone.
    contentWidget()->setProperty("commandPalette", true);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *search = new QWidget;
    search->setObjectName(QStringLiteral("paletteSearch"));
    search->setAttribute(Qt::WA_StyledBackground, true);
    auto *searchLayout = new QHBoxLayout(search);
    searchLayout->setContentsMargins(14, 12, 14, 12);
    m_query->setObjectName(QStringLiteral("paletteQuery"));
    m_query->setPlaceholderText(tr("Search actions and places"));
    searchLayout->addWidget(m_query);
    layout->addWidget(search);

    auto *listHolder = new QWidget;
    listHolder->setObjectName(QStringLiteral("paletteListHolder"));
    auto *listLayout = new QVBoxLayout(listHolder);
    listLayout->setContentsMargins(6, 6, 6, 6);
    m_list->setObjectName(QStringLiteral("paletteResults"));
    m_list->setItemDelegate(new PaletteDelegate(m_list));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMouseTracking(true);
    m_list->viewport()->setAutoFillBackground(false);
    listLayout->addWidget(m_list);
    layout->addWidget(listHolder, 1);

    auto *footer = new QWidget;
    footer->setObjectName(QStringLiteral("paletteFooter"));
    footer->setAttribute(Qt::WA_StyledBackground, true);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 8, 12, 8);
    footerLayout->setSpacing(14);
    footerLayout->addWidget(new KeyHint(QStringLiteral("Enter"), tr("Run"), footer));
    footerLayout->addStretch(1);
    footerLayout->addWidget(new KeyHint(QStringLiteral("↑ ↓"), tr("Move"), footer));
    footerLayout->addWidget(new KeyHint(QStringLiteral("Esc"), tr("Close"), footer));
    layout->addWidget(footer);

    connect(m_query, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_list, &QListWidget::itemActivated, this, [this] { accept(); });
    connect(m_list, &QListWidget::itemEntered, this,
            [this](QListWidgetItem *item) { m_list->setCurrentItem(item); });
    connect(m_list, &QListWidget::itemClicked, this, [this] { accept(); });
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

void CommandPaletteModal::open(QList<PaletteItem> items)
{
    m_items = std::move(items);
    m_query->clear();
    // Tinted here rather than in the stylesheet, which cannot recolour a glyph.
    if (m_searchAction != nullptr) {
        m_query->removeAction(m_searchAction);
        delete m_searchAction;
    }
    m_searchAction = m_query->addAction(
        SymbolicIcon::icon(QStringLiteral("magnifying-glass"), currentPalette().overlay),
        QLineEdit::LeadingPosition);
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

    for (int i = 0; i < m_rows.size(); ++i) {
        const PaletteRow &row = m_rows.at(i);
        auto *item = new QListWidgetItem(row.item.title, m_list);
        item->setData(kRowRole, i);
        item->setData(kDetailRole, row.item.detail);
        item->setData(kKeysRole, row.item.keys);
        item->setData(kIconRole, row.item.icon);
        item->setData(kGroupRole, row.item.group);
        QList<QVariant> spans;
        for (const MatchSpan &span : row.spans) {
            spans << span.start << span.length;
        }
        item->setData(kSpansRole, spans);
    }

    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
}

void CommandPaletteModal::moveSelection(int delta)
{
    if (m_list->count() == 0) {
        return;
    }
    m_list->setCurrentRow(std::clamp(m_list->currentRow() + delta, 0, m_list->count() - 1));
}

void CommandPaletteModal::mousePressEvent(QMouseEvent *event)
{
    // A click on the backdrop closes the palette; one on the card is the card's.
    if (!contentWidget()->geometry().contains(event->position().toPoint())) {
        dismiss();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
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
