#include "ui/PanelView.h"
#include "core/Format.h"

#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "platform/FileOps.h"
#include "ui/ThemePalette.h"

#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMenu>
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

void PanelView::setBodyInset(int horizontal, int vertical)
{
    setViewportMargins(horizontal, vertical, horizontal, vertical);
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

bool PanelView::wantsMenu(Qt::KeyboardModifiers modifiers)
{
    return (modifiers & Qt::AltModifier) != 0;
}

Qt::DropAction PanelView::actionFor(Qt::KeyboardModifiers modifiers, bool sameFilesystem)
{
    // Shift and Ctrl override, and are absolute: someone holding a modifier has
    // said what they want, and second-guessing it would make the override
    // useless exactly when it matters. On macOS Qt maps Qt::ControlModifier to
    // Command, so this one test reads as Ctrl on Linux and Cmd on a Mac, which
    // is the shortcut each platform's users already know.
    if ((modifiers & Qt::ShiftModifier) != 0) {
        return Qt::MoveAction;
    }
    if ((modifiers & Qt::ControlModifier) != 0) {
        return Qt::CopyAction;
    }

    // Otherwise the filesystem decides. Within one, a move is a rename: instant,
    // and what dragging a file from one view of a disk to another plainly means.
    // Across two, that same gesture would copy every byte and then delete the
    // original — slow, and destructive if it went to the wrong place — so a copy
    // is the safer reading of an unmodified drag.
    return sameFilesystem ? Qt::MoveAction : Qt::CopyAction;
}

QString PanelView::nameAt(const QPoint &position) const
{
    const QModelIndex index = indexAt(position);
    if (!index.isValid()) {
        return {};
    }
    return index.data(DirectoryModel::NameRole).toString();
}

void PanelView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPosition = event->position().toPoint();
        m_pressed = true;
        Q_EMIT rowClicked(nameAt(m_pressPosition), event->modifiers());
    }
    QListView::mousePressEvent(event);
}

void PanelView::mouseReleaseEvent(QMouseEvent *event)
{
    // A press that is still "pressed" here never travelled far enough to become
    // a drag, so it was a click after all. The distinction matters for a plain
    // click on an already-selected row: clearing the selection on the way down
    // would empty it just as the user started dragging it somewhere.
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        Q_EMIT clickCompleted(nameAt(event->position().toPoint()), event->modifiers());
    }
    QListView::mouseReleaseEvent(event);
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
        painter.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, tr("+%1 more").arg(extra));
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

QStringList PanelView::localPathsIn(const QMimeData *mime)
{
    QStringList paths;
    for (const QUrl &url : mime->urls()) {
        // Only local files. A drop of an http:// URL from a browser is a
        // download request, which is not something a file manager should
        // silently reinterpret as a copy.
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    return paths;
}

Qt::DropAction PanelView::actionForDrag(const QMimeData *mime, const QPoint &position,
                                        Qt::KeyboardModifiers modifiers) const
{
    const QStringList paths = localPathsIn(mime);
    const QString destination = destinationFor(position);

    // The first source stands for all of them. A drag spanning two filesystems
    // is possible but vanishingly rare, and asking the kernel once per file to
    // resolve a question the modifiers can override anyway would put a stat
    // storm inside a handler that runs on every mouse move.
    const bool same = !paths.isEmpty() && !destination.isEmpty() &&
                      platform::onSameFilesystem(paths.constFirst(), destination);

    return actionFor(modifiers, same);
}

void PanelView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        return;
    }
    event->setDropAction(
        actionForDrag(event->mimeData(), event->position().toPoint(), event->modifiers()));
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

    event->setDropAction(
        actionForDrag(event->mimeData(), event->position().toPoint(), event->modifiers()));
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

    const QStringList paths = localPathsIn(event->mimeData());
    if (paths.isEmpty()) {
        return;
    }

    const QPoint position = event->position().toPoint();
    const QString destination = destinationFor(position);
    if (destination.isEmpty()) {
        return;
    }

    const Qt::DropAction action = actionForDrag(event->mimeData(), position, event->modifiers());

    // The drag has to be answered now, whatever happens next: the platform's
    // drag manager is still holding the pointer, and it does not get it back
    // until the event is accepted.
    event->setDropAction(action);
    event->accept();

    if (wantsMenu(event->modifiers())) {
        // Queued rather than run here, because the menu's own event loop would
        // otherwise nest inside the drag manager's — on macOS that leaves the
        // cursor stuck in its drag state until the menu closes. By the time
        // this runs the drag has ended and the menu is an ordinary popup.
        const QPoint global = viewport()->mapToGlobal(position);
        QMetaObject::invokeMethod(
            this,
            [this, paths, destination, action, global] {
                askAndDrop(paths, destination, action, global);
            },
            Qt::QueuedConnection);
        return;
    }

    Q_EMIT filesDropped(paths, destination, action);
}

void PanelView::askAndDrop(const QStringList &paths, const QString &destination,
                           Qt::DropAction suggested, const QPoint &globalPosition)
{
    QMenu menu(this);

    QAction *copy = menu.addAction(tr("Copy Here"));
    QAction *move = menu.addAction(tr("Move Here"));
    menu.addSeparator();
    menu.addAction(tr("Cancel"));

    // The action the drag was already going to take is the default, so pressing
    // Return does what letting go without Alt would have done.
    menu.setDefaultAction(suggested == Qt::MoveAction ? move : copy);

    const QAction *chosen = menu.exec(globalPosition);
    if (chosen == copy) {
        Q_EMIT filesDropped(paths, destination, Qt::CopyAction);
    } else if (chosen == move) {
        Q_EMIT filesDropped(paths, destination, Qt::MoveAction);
    }
}

} // namespace pf::ui
