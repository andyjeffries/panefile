#pragma once

#include <QListView>
#include <QString>

namespace pf::ui {

/// The list view inside a panel, with drag and drop (§7.12).
///
/// A subclass rather than an event filter because §7.12 needs three things
/// QListView's own drag support cannot give: a drag payload built from the
/// panel's *selection* rather than the view's, a drop target that depends on
/// which row the pointer is over, and a highlight drawn on that row.
///
/// §7.12's Wayland note is why the drag starts where it does: "on Wayland, a
/// drag must originate from a genuine pointer press-and-move — start it from
/// mouseMoveEvent past QApplication::startDragDistance(), not from a timer."
class PanelView : public QListView
{
    Q_OBJECT

public:
    /// §7.12: "a rendered pixmap of up to 3 rows plus a '+N' badge".
    static constexpr int kDragPixmapRows = 3;

    explicit PanelView(QWidget *parent = nullptr);

    /// The row the pointer is over during a drag, or -1. The delegate paints it
    /// as the drop target.
    int dropTargetRow() const;

    /// Where a drop at `position` would land: the directory row under the
    /// pointer, or the panel's own directory for empty space or a file row.
    ///
    /// Public and pure of side effects because §7.12's two placement rules are
    /// what is worth testing, and a synthetic QDropEvent cannot reach a
    /// QAbstractScrollArea — it refuses drag events sent to itself, and routes
    /// real ones through machinery only the platform's drag manager drives.
    /// Testing Qt's event plumbing was never the point; testing where the files
    /// go is.
    QString destinationFor(const QPoint &position) const;

    /// The action the modifiers ask for (§7.12).
    static Qt::DropAction actionFor(Qt::KeyboardModifiers modifiers);

Q_SIGNALS:
    /// Files were dropped. `destinationDirectory` is the directory row under
    /// the pointer, or the panel's own directory when the drop was on empty
    /// space or on a file.
    void filesDropped(const QStringList &paths, const QString &destinationDirectory,
                      Qt::DropAction action);

    /// A drag is starting; the panel supplies the paths, because the selection
    /// belongs to it rather than to the view.
    void dragPathsRequested(QStringList *paths);

    /// The directory this view is showing, for resolving a drop on empty space.
    void currentDirectoryRequested(QString *path);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// QAbstractItemView declares a `startDrag(Qt::DropActions)` of its own,
    /// which this deliberately does not use — the payload comes from the
    /// panel's selection, not the view's. Named apart so the two cannot be
    /// confused for an override that never fires.
    void beginPanelDrag();

    /// Renders up to three rows plus a "+N" badge.
    QPixmap dragPixmap(const QStringList &paths) const;

    void setDropTargetRow(int row);

    QPoint m_pressPosition;
    bool m_pressed = false;
    int m_dropTargetRow = -1;
};

} // namespace pf::ui
