#include "ui/PanelView.h"

#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>

namespace pf::ui {
namespace {

/// Width of the drag pixmap. Wide enough for a readable filename, narrow enough
/// that it does not cover the panel being dragged into.
constexpr int kDragPixmapWidth = 220;
constexpr int kDragRowHeight = 22;
constexpr int kDragPadding = 8;

} // namespace

PanelView::PanelView(QWidget *parent) : QListView(parent)
{
    // QListView's own drag machinery is deliberately left off: it would build
    // the payload from the view's selection model, and §6.1's Selection mode is
    // a separate thing the panel owns.
    setDragEnabled(false);
    setDragDropMode(QAbstractItemView::NoDragDrop);

    // *After* setDragDropMode, which is not a detail.
    //
    // QAbstractItemView::setDragDropMode(NoDragDrop) calls setAcceptDrops(false)
    // as part of applying the mode, so accepting drops first and setting the
    // mode second — which is how this was written — left acceptDrops false and
    // the panel deaf to every drag event Qt could have sent it. Dropping a file
    // between panels did nothing whatsoever, and no test caught it because
    // synthetic events cannot reach a widget that has not asked for them.
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
}

int PanelView::dropTargetRow() const
{
    return m_dropTargetRow;
}

void PanelView::setDropTargetRow(int row)
{
    if (m_dropTargetRow == row) {
        return;
    }
    m_dropTargetRow = row;
    viewport()->update();
}

Qt::DropAction PanelView::actionFor(Qt::KeyboardModifiers modifiers)
{
    // §7.12: "Default action is copy; Shift forces move, Ctrl forces copy".
    //
    // Copy is the default rather than move because the two fail differently: a
    // copy the user did not want leaves a file to delete, and a move they did
    // not want has already taken the original away from where they expected it.
    if ((modifiers & Qt::ShiftModifier) != 0) {
        return Qt::MoveAction;
    }
    return Qt::CopyAction;
}

void PanelView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPosition = event->position().toPoint();
        m_pressed = true;
    }
    QListView::mousePressEvent(event);
}

void PanelView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed || (event->buttons() & Qt::LeftButton) == 0) {
        QListView::mouseMoveEvent(event);
        return;
    }

    // §7.12: "on Wayland, a drag must originate from a genuine pointer
    // press-and-move — start it from mouseMoveEvent past
    // QApplication::startDragDistance(), not from a timer." A compositor that
    // sees a drag begin without a preceding motion event refuses to start it,
    // and the failure is silent.
    const int travelled = (event->position().toPoint() - m_pressPosition).manhattanLength();
    if (travelled < QApplication::startDragDistance()) {
        QListView::mouseMoveEvent(event);
        return;
    }

    m_pressed = false;
    beginPanelDrag();
}

void PanelView::beginPanelDrag()
{
    QStringList paths;
    Q_EMIT dragPathsRequested(&paths);

    if (paths.isEmpty()) {
        return;
    }

    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &path : paths) {
        urls.append(QUrl::fromLocalFile(path));
    }

    auto *mime = new QMimeData;
    // §7.12: both formats. text/uri-list is what every file manager reads;
    // text/plain is what a terminal or an editor pastes.
    mime->setUrls(urls);
    mime->setText(paths.join(QLatin1Char('\n')));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(dragPixmap(paths));
    drag->setHotSpot(QPoint(kDragPadding, kDragPadding));

    // §7.12: "Set Qt::CopyAction | Qt::MoveAction." The default is copy, for
    // the same reason the drop's default is.
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

QPixmap PanelView::dragPixmap(const QStringList &paths) const
{
    const ThemePalette &theme = currentPalette();

    const int shown = std::min<int>(kDragPixmapRows, static_cast<int>(paths.size()));
    const int extra = static_cast<int>(paths.size()) - shown;
    const int rows = shown + (extra > 0 ? 1 : 0);

    const qreal ratio = devicePixelRatioF();
    const int width = kDragPixmapWidth;
    const int height = (rows * kDragRowHeight) + (2 * kDragPadding);

    QPixmap pixmap(QSize(width, height) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor background = theme.surface;
    background.setAlpha(235);
    painter.setPen(theme.borderFocused);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(0.5, 0.5, width - 1.0, height - 1.0), theme.borderRadius,
                            theme.borderRadius);

    const QFontMetrics metrics = painter.fontMetrics();
    int y = kDragPadding;

    painter.setPen(theme.text);
    for (int i = 0; i < shown; ++i) {
        const QRect row(kDragPadding, y, width - (2 * kDragPadding), kDragRowHeight);
        painter.drawText(
            row, Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText(QFileInfo(paths.at(i)).fileName(), Qt::ElideMiddle, row.width()));
        y += kDragRowHeight;
    }

    if (extra > 0) {
        // §7.12's "+N" badge.
        painter.setPen(theme.subtext);
        const QRect row(kDragPadding, y, width - (2 * kDragPadding), kDragRowHeight);
        painter.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, tr("+%n more", nullptr, extra));
    }

    return pixmap;
}

QString PanelView::destinationFor(const QPoint &position) const
{
    // §7.12: "Dropping onto a directory row targets that directory; dropping
    // onto empty space targets the panel's cwd." A file row is empty space for
    // this purpose — dropping onto a file plainly means the directory it is in.
    const QModelIndex index = indexAt(position);
    if (index.isValid()) {
        const QVariant value = index.data(DirectoryModel::EntryRole);
        if (value.canConvert<FileEntry>() && value.value<FileEntry>().isDir) {
            QString directory;
            Q_EMIT const_cast<PanelView *>(this)->currentDirectoryRequested(&directory);
            return QDir(directory).absoluteFilePath(value.value<FileEntry>().name);
        }
    }

    QString directory;
    Q_EMIT const_cast<PanelView *>(this)->currentDirectoryRequested(&directory);
    return directory;
}

void PanelView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        return;
    }
    event->setDropAction(actionFor(event->modifiers()));
    event->accept();
}

void PanelView::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        return;
    }

    const QModelIndex index = indexAt(event->position().toPoint());

    // Only a directory row is a target in its own right; over anything else the
    // whole panel is the target, and highlighting a row would be a lie about
    // where the files are going.
    int row = -1;
    if (index.isValid()) {
        const QVariant value = index.data(DirectoryModel::EntryRole);
        if (value.canConvert<FileEntry>() && value.value<FileEntry>().isDir) {
            row = index.row();
        }
    }
    setDropTargetRow(row);

    event->setDropAction(actionFor(event->modifiers()));
    event->accept();
}

void PanelView::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropTargetRow(-1);
    QListView::dragLeaveEvent(event);
}

void PanelView::dropEvent(QDropEvent *event)
{
    setDropTargetRow(-1);

    if (!event->mimeData()->hasUrls()) {
        return;
    }

    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        // Only local files. A drop of an http:// URL from a browser is a
        // download request, which is not something a file manager should
        // silently reinterpret as a copy.
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }

    if (paths.isEmpty()) {
        return;
    }

    const QString destination = destinationFor(event->position().toPoint());
    if (destination.isEmpty()) {
        return;
    }

    const Qt::DropAction action = actionFor(event->modifiers());
    event->setDropAction(action);
    event->accept();

    Q_EMIT filesDropped(paths, destination, action);
}

} // namespace pf::ui
