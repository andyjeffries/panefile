#include "ui/modals/FolderIconModal.h"

#include "core/FuzzyMatcher.h"
#include "model/FolderIconRenderer.h"
#include "model/FolderIconStore.h"
#include "model/StandardFolders.h"
#include "ui/ThemePalette.h"

#include <QAbstractListModel>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

namespace pf::ui {
namespace {

constexpr int kGlyphRole = Qt::UserRole;
constexpr int kCurrentRole = Qt::UserRole + 1;

constexpr int kCellWidth = 76;
constexpr int kCellHeight = 86;
constexpr int kIconPixels = 48;

/// Every Heroicons name, sorted, read once.
const QStringList &heroNames()
{
    static const QStringList names = [] {
        QStringList result;
        const QString directory = FolderIconStore::heroiconsDirectory();
        if (!directory.isEmpty()) {
            for (const QString &file :
                 QDir(directory).entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name)) {
                const QString name = QFileInfo(file).completeBaseName();
                if (FolderIconStore::isValidGlyphId(QStringLiteral("hero:") + name)) {
                    result.append(name);
                }
            }
        }
        return result;
    }();
    return names;
}

QPixmap previewFor(const QString &glyph, int logicalSize, qreal ratio)
{
    return FolderIconRenderer::pixmap(
        FolderIconSpec{.glyphId = glyph, .tint = currentPalette().directory},
        {logicalSize, logicalSize}, ratio);
}

} // namespace

/// The glyphs of one view. The glyph id is the data; the title is what is drawn.
class GlyphListModel : public QAbstractListModel
{
public:
    struct Item {
        QString glyph;
        QString title;
    };

    using QAbstractListModel::QAbstractListModel;

    void setItems(QList<Item> items)
    {
        beginResetModel();
        m_items = std::move(items);
        endResetModel();
    }

    void setCurrentGlyph(const QString &glyph) { m_currentGlyph = glyph; }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_items.size());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_items.size()) {
            return {};
        }
        const Item &item = m_items.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
            return item.title;
        case kGlyphRole:
            return item.glyph;
        case kCurrentRole:
            return item.glyph == m_currentGlyph;
        default:
            return {};
        }
    }

    QList<Item> items() const { return m_items; }

private:
    QList<Item> m_items;
    QString m_currentGlyph;
};

namespace {

class GlyphDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {kCellWidth, kCellHeight};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const ThemePalette &palette = currentPalette();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRectF cell = QRectF(option.rect).adjusted(2, 2, -2, -2);
        const bool cursor = (option.state & QStyle::State_Selected) != 0;
        const bool current = index.data(kCurrentRole).toBool();
        if (cursor) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(palette.focusedCursorBackground());
            painter->drawRoundedRect(cell, 6, 6);
        }
        if (current) {
            painter->setPen(QPen(palette.accent, 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(cell.adjusted(0.75, 0.75, -0.75, -0.75), 6, 6);
        }

        // Glyph bytes come from FolderIconStore's cache, read once each, and
        // only for cells that are painted: this is a modal, not a panel.
        const qreal ratio = painter->device()->devicePixelRatio();
        const QPixmap icon = previewFor(index.data(kGlyphRole).toString(), kIconPixels, ratio);
        painter->drawPixmap(QPointF(cell.center().x() - (kIconPixels / 2.0), cell.top() + 6), icon);

        QFont font = option.font;
        if (font.pixelSize() > 0) {
            font.setPixelSize(std::max(1, font.pixelSize() - 2));
        } else if (font.pointSize() > 1) {
            font.setPointSize(font.pointSize() - 1);
        }
        painter->setFont(font);
        painter->setPen(palette.text);

        // Two lines: break at a dash, elide the second in the middle.
        const QFontMetrics metrics(font);
        const QString title = index.data(Qt::DisplayRole).toString();
        const int width = static_cast<int>(cell.width()) - 4;
        QString first = title;
        QString second;
        if (metrics.horizontalAdvance(title) > width) {
            qsizetype cut = -1;
            for (qsizetype i = 0; i < title.size(); ++i) {
                if (title.at(i) == QLatin1Char('-') &&
                    metrics.horizontalAdvance(title.left(i + 1)) <= width) {
                    cut = i + 1;
                }
            }
            if (cut < 0) {
                cut = title.size();
                while (cut > 1 && metrics.horizontalAdvance(title.left(cut)) > width) {
                    --cut;
                }
            }
            first = title.left(cut);
            second = metrics.elidedText(title.mid(cut), Qt::ElideMiddle, width);
        }
        const QRectF line1(cell.left() + 2, cell.top() + 6 + kIconPixels + 3, width,
                           metrics.height());
        painter->drawText(line1, Qt::AlignHCenter | Qt::AlignTop, first);
        if (!second.isEmpty()) {
            painter->drawText(line1.translated(0, metrics.height()),
                              Qt::AlignHCenter | Qt::AlignTop, second);
        }
        painter->restore();
    }
};

QLabel *headingLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("findStatus"));
    QFont font = label->font();
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    label->setFont(font);
    return label;
}

QListView *makeView(GlyphListModel *model, QWidget *parent)
{
    auto *view = new QListView(parent);
    view->setObjectName(QStringLiteral("findResults"));
    view->setModel(model);
    view->setItemDelegate(new GlyphDelegate(view));
    view->setViewMode(QListView::ListMode);
    view->setFlow(QListView::LeftToRight);
    view->setWrapping(true);
    view->setResizeMode(QListView::Adjust);
    view->setGridSize({kCellWidth, kCellHeight});
    view->setUniformItemSizes(true);
    view->setSpacing(0);
    view->setFrameShape(QFrame::NoFrame);
    view->setFocusPolicy(Qt::NoFocus);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setMouseTracking(true);
    return view;
}

} // namespace

FolderIconModal::FolderIconModal(QWidget *parent)
    : Modal(parent), m_title(new QLabel), m_pathLabel(new QLabel), m_preview96(new QLabel),
      m_preview32(new QLabel), m_preview16(new QLabel), m_current(new QLabel),
      m_query(new QLineEdit), m_stripModel(new GlyphListModel(this)),
      m_gridModel(new GlyphListModel(this))
{
    setSizePercent(60, 82);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(10);

    // Header: live preview, the folder's name, and the choice's small sizes.
    auto *header = new QHBoxLayout;
    header->setSpacing(16);
    m_preview96->setFixedSize(112, 112);
    m_preview96->setAlignment(Qt::AlignCenter);
    m_preview96->setObjectName(QStringLiteral("folderIconPreview"));
    header->addWidget(m_preview96);

    auto *titles = new QVBoxLayout;
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    m_title->setFont(titleFont);
    m_title->setTextFormat(Qt::PlainText);
    m_pathLabel->setTextFormat(Qt::PlainText);
    m_pathLabel->setObjectName(QStringLiteral("findStatus"));
    auto *small = new QHBoxLayout;
    small->setSpacing(8);
    small->addWidget(m_preview16);
    small->addWidget(m_preview32);
    small->addWidget(m_current, 1);
    titles->addStretch(1);
    titles->addWidget(m_title);
    titles->addWidget(m_pathLabel);
    titles->addLayout(small);
    titles->addStretch(1);
    header->addLayout(titles, 1);
    layout->addLayout(header);

    m_query->setObjectName(QStringLiteral("findQuery"));
    m_query->setPlaceholderText(tr("Search icons"));
    m_query->installEventFilter(this);
    layout->addWidget(m_query);

    m_stripHeading = headingLabel(tr("Default and standard folders"), this);
    layout->addWidget(m_stripHeading);
    m_stripView = makeView(m_stripModel, this);
    m_stripView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_stripView);

    auto *gridHeader = new QHBoxLayout;
    m_gridHeading = headingLabel(tr("Heroicons"), this);
    m_gridCount = new QLabel(this);
    m_gridCount->setObjectName(QStringLiteral("findStatus"));
    gridHeader->addWidget(m_gridHeading, 1);
    gridHeader->addWidget(m_gridCount);
    layout->addLayout(gridHeader);

    m_notInstalled = new QLabel(tr("Icon set not installed"), this);
    m_notInstalled->setObjectName(QStringLiteral("findStatus"));
    m_notInstalled->setVisible(false);
    layout->addWidget(m_notInstalled);

    m_gridView = makeView(m_gridModel, this);
    layout->addWidget(m_gridView, 1);

    auto *footer = new QLabel(tr("↵ applies · ←↑↓→ moves · Esc cancels"), this);
    footer->setObjectName(QStringLiteral("findStatus"));
    layout->addWidget(footer);

    connect(m_query, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_stripView, &QListView::clicked, this,
            [this](const QModelIndex &index) { select(true, index.row()); });
    connect(m_gridView, &QListView::clicked, this,
            [this](const QModelIndex &index) { select(false, index.row()); });
    connect(m_stripView, &QListView::doubleClicked, this, [this] { accept(); });
    connect(m_gridView, &QListView::doubleClicked, this, [this] { accept(); });

    // Built once; the strip and grid are views over these.
    m_all.append({.glyph = {}, .title = tr("Default"), .name = {}});
    for (const StandardFolder &preset : StandardFolders::presets()) {
        m_all.append({.glyph = preset.glyph, .title = preset.title, .name = preset.title});
    }
}

QWidget *FolderIconModal::initialFocusWidget()
{
    return m_query;
}

void FolderIconModal::open(const QString &path, const QString &currentGlyph)
{
    m_currentGlyph = currentGlyph;
    m_stripModel->setCurrentGlyph(currentGlyph);
    m_gridModel->setCurrentGlyph(currentGlyph);

    const QFileInfo info(path);
    m_title->setText(tr("Folder icon for “%1”").arg(info.fileName()));
    const QString home = QDir::homePath();
    m_pathLabel->setText(path == home || path.startsWith(home + QLatin1Char('/'))
                             ? QStringLiteral("~") + path.mid(home.size())
                             : path);
    m_current->setText(currentGlyph.isEmpty() ? tr("Currently: Default")
                                              : tr("Currently: %1").arg(currentGlyph.mid(5)));

    m_query->clear();
    refresh();

    // The cursor starts on the folder's present choice.
    bool placed = false;
    for (int i = 0; i < m_stripModel->rowCount() && !placed; ++i) {
        if (m_stripModel->index(i).data(kGlyphRole).toString() == currentGlyph) {
            select(true, i);
            placed = true;
        }
    }
    for (int i = 0; i < m_gridModel->rowCount() && !placed; ++i) {
        if (m_gridModel->index(i).data(kGlyphRole).toString() == currentGlyph) {
            select(false, i);
            m_gridView->scrollTo(m_gridModel->index(i), QAbstractItemView::PositionAtCenter);
            placed = true;
        }
    }
    if (!placed) {
        select(true, 0);
    }

    showModal();
    layoutViews();
    m_query->setFocus(Qt::ShortcutFocusReason);
}

void FolderIconModal::setQuery(const QString &query)
{
    m_query->setText(query);
}

void FolderIconModal::refresh()
{
    const QString needle = m_query->text().trimmed();

    // Dashes read as spaces, so "arrow d" finds arrow-down-tray.
    struct Scored {
        GlyphListModel::Item item;
        int score;
    };
    QList<GlyphListModel::Item> strip;
    if (needle.isEmpty()) {
        for (const Cell &cell : std::as_const(m_all)) {
            strip.append({.glyph = cell.glyph, .title = cell.title});
        }
    } else {
        QList<Scored> scored;
        for (const Cell &cell : std::as_const(m_all)) {
            if (cell.glyph.isEmpty()) {
                continue;
            }
            const FuzzyMatch match = FuzzyMatcher::match(needle, cell.name);
            if (match.matched) {
                scored.append({{cell.glyph, cell.title}, match.score});
            }
        }
        std::ranges::stable_sort(
            scored, [](const Scored &a, const Scored &b) { return a.score > b.score; });
        for (const Scored &entry : scored) {
            strip.append(entry.item);
        }
    }

    QList<GlyphListModel::Item> grid;
    QList<Scored> scoredGrid;
    for (const QString &name : heroNames()) {
        if (needle.isEmpty()) {
            grid.append({.glyph = QStringLiteral("hero:") + name, .title = name});
            continue;
        }
        QString spaced = name;
        spaced.replace(QLatin1Char('-'), QLatin1Char(' '));
        const FuzzyMatch match = FuzzyMatcher::match(needle, spaced);
        if (match.matched) {
            scoredGrid.append({{QStringLiteral("hero:") + name, name}, match.score});
        }
    }
    if (!needle.isEmpty()) {
        std::ranges::stable_sort(
            scoredGrid, [](const Scored &a, const Scored &b) { return a.score > b.score; });
        for (const Scored &entry : std::as_const(scoredGrid)) {
            grid.append(entry.item);
        }
    }

    m_stripModel->setItems(strip);
    m_gridModel->setItems(grid);

    m_stripHeading->setVisible(!strip.isEmpty());
    m_stripView->setVisible(!strip.isEmpty());
    m_notInstalled->setVisible(heroNames().isEmpty());
    m_gridView->setVisible(!heroNames().isEmpty());
    m_gridCount->setText(tr("%n icon(s)", nullptr, static_cast<int>(grid.size())));

    layoutViews();
    if (!strip.isEmpty()) {
        select(true, 0);
    } else if (!grid.isEmpty()) {
        select(false, 0);
    } else {
        updatePreview();
    }
}

int FolderIconModal::columns() const
{
    const int width = m_gridView->viewport()->width();
    return std::max(1, width / kCellWidth);
}

void FolderIconModal::layoutViews()
{
    const int cols = columns();
    const int rows = std::max<int>(1, (m_stripModel->rowCount() + cols - 1) / cols);
    m_stripView->setFixedHeight(rows * kCellHeight + 2);
}

void FolderIconModal::resizeEvent(QResizeEvent *event)
{
    Modal::resizeEvent(event);
    layoutViews();
}

bool FolderIconModal::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_query && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        // The box keeps focus, so the cursor keys it would use for its own
        // caret drive the grid instead.
        switch (key->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            keyPressEvent(const_cast<QKeyEvent *>(key));
            return true;
        default:
            break;
        }
    }
    return Modal::eventFilter(watched, event);
}

void FolderIconModal::select(bool strip, int index)
{
    m_cursorInStrip = strip;
    QListView *active = strip ? m_stripView : m_gridView;
    QListView *other = strip ? m_gridView : m_stripView;
    other->selectionModel()->clearSelection();
    other->setCurrentIndex({});
    const QModelIndex target = (strip ? m_stripModel : m_gridModel)->index(index);
    if (target.isValid()) {
        active->setCurrentIndex(target);
        active->scrollTo(target);
    }
    updatePreview();
}

void FolderIconModal::moveCursor(int dRow, int dColumn)
{
    const int cols = columns();
    const int stripCount = m_stripModel->rowCount();
    const int gridCount = m_gridModel->rowCount();

    const int current = (m_cursorInStrip ? m_stripView : m_gridView)->currentIndex().row();
    if (current < 0) {
        select(stripCount > 0, 0);
        return;
    }

    if (dColumn != 0) {
        int next = current + dColumn;
        if (m_cursorInStrip) {
            if (next >= stripCount && gridCount > 0) {
                select(false, 0);
            } else {
                select(true, std::clamp(next, 0, std::max(0, stripCount - 1)));
            }
        } else if (next < 0 && stripCount > 0) {
            select(true, stripCount - 1);
        } else {
            select(false, std::clamp(next, 0, std::max(0, gridCount - 1)));
        }
        return;
    }

    const int delta = dRow;
    if (m_cursorInStrip) {
        if (delta < 0) {
            return;
        }
        const int below = current + cols;
        if (below < stripCount) {
            select(true, below);
        } else if (gridCount > 0) {
            select(false, std::min(current % cols, gridCount - 1));
        }
        return;
    }

    const int next = current + (delta * cols);
    if (next < 0) {
        if (stripCount > 0) {
            const int lastRowStart = ((stripCount - 1) / cols) * cols;
            select(true, std::min(lastRowStart + (current % cols), stripCount - 1));
        }
        return;
    }
    // Down from a short last row lands on the last item, as in the panels.
    select(false, std::min(next, gridCount - 1));
}

void FolderIconModal::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        moveCursor(0, -1);
        return;
    case Qt::Key_Right:
        moveCursor(0, 1);
        return;
    case Qt::Key_Up:
        moveCursor(-1, 0);
        return;
    case Qt::Key_Down:
        moveCursor(1, 0);
        return;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
        const int rows = std::max(1, m_gridView->viewport()->height() / kCellHeight - 1);
        moveCursor(event->key() == Qt::Key_PageUp ? -rows : rows, 0);
        return;
    }
    default:
        break;
    }
    Modal::keyPressEvent(event);
}

QString FolderIconModal::cursorGlyph() const
{
    const QListView *view = m_cursorInStrip ? m_stripView : m_gridView;
    return view->currentIndex().data(kGlyphRole).toString();
}

QStringList FolderIconModal::visibleGlyphs() const
{
    QStringList glyphs;
    for (int i = 0; i < m_stripModel->rowCount(); ++i) {
        glyphs << m_stripModel->index(i).data(kGlyphRole).toString();
    }
    for (int i = 0; i < m_gridModel->rowCount(); ++i) {
        glyphs << m_gridModel->index(i).data(kGlyphRole).toString();
    }
    return glyphs;
}

void FolderIconModal::updatePreview()
{
    const QString glyph = cursorGlyph();
    const qreal ratio = devicePixelRatioF();
    m_preview96->setPixmap(previewFor(glyph, 96, ratio));
    m_preview32->setPixmap(previewFor(glyph, 32, ratio));
    m_preview16->setPixmap(previewFor(glyph, 16, ratio));
}

void FolderIconModal::accept()
{
    const bool hasChoice = (m_cursorInStrip ? m_stripView : m_gridView)->currentIndex().isValid();
    const QString glyph = cursorGlyph();
    Modal::accept();
    if (hasChoice) {
        Q_EMIT chosen(glyph);
    }
}

} // namespace pf::ui
