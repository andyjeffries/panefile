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

    /// Insets the list body from the view's edges.
    ///
    /// QAbstractScrollArea::setViewportMargins is protected, and this is the
    /// subclass, so the panel asks for the inset rather than reaching for it.
    void setBodyInset(int horizontal, int vertical);

    /// The action a drop should perform.
    ///
    /// `sameFilesystem` is what makes the default sensible rather than merely
    /// consistent: within one filesystem a move is a rename — instant, and what
    /// dragging a file between two views of the same disk plainly means. Across
    /// filesystems the same gesture would be a full copy followed by deleting
    /// the original, which is slow and destroys the source, so there the
    /// default is a copy.
    ///
    /// Static and pure so the whole modifier table can be tested without a drag
    /// manager, which is the part no synthetic event can supply.
    static Qt::DropAction actionFor(Qt::KeyboardModifiers modifiers, bool sameFilesystem);

    /// Whether the modifiers ask to be shown the choice instead of having one
    /// picked (Alt, per §7.12's "Alt shows a menu").
    static bool wantsMenu(Qt::KeyboardModifiers modifiers);

Q_SIGNALS:
    /// Files were dropped. `destinationDirectory` is the directory row under
    /// the pointer, or the panel's own directory when the drop was on empty
    /// space or on a file.
    void filesDropped(const QStringList &paths, const QString &destinationDirectory,
                      Qt::DropAction action);

    /// A row was clicked, with whatever modifiers were held. The panel decides
    /// what that means for the selection, because the selection is the panel's
    /// (§6.1) and not the view's selection model.
    ///
    /// `name` is empty for a click on empty space.
    void rowClicked(const QString &name, Qt::KeyboardModifiers modifiers);

    /// The button came back up on the same row it went down on, with no drag in
    /// between — so a plain click really was a click, and can now clear the
    /// selection it could not safely clear on the way down.
    void clickCompleted(const QString &name, Qt::KeyboardModifiers modifiers);

    /// A drag is starting; the panel supplies the paths, because the selection
    /// belongs to it rather than to the view.
    void dragPathsRequested(QStringList *paths);

    /// The directory this view is showing, for resolving a drop on empty space.
    void currentDirectoryRequested(QString *path);

private:
    /// The local file paths in a drag payload, ignoring anything that is not a
    /// file — a browser's http:// URL is a download request, not a copy.
    static QStringList localPathsIn(const QMimeData *mime);

    /// `actionFor` with the filesystem question answered from a live drag.
    Qt::DropAction actionForDrag(const QMimeData *mime, const QPoint &position,
                                 Qt::KeyboardModifiers modifiers) const;

    /// Shows the copy-or-move menu and emits the answer. Called queued, after
    /// the drag manager has let go of the pointer.
    void askAndDrop(const QStringList &paths, const QString &destination, Qt::DropAction suggested,
                    const QPoint &globalPosition);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

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

    /// Grows the bottom inset by whatever is left over after a whole number of
    /// rows, so the list never ends in a sliced one.
    void absorbPartialRow();

    /// The entry name at a viewport position, or empty for empty space.
    QString nameAt(const QPoint &position) const;

    /// Renders up to three rows plus a "+N" badge.
    QPixmap dragPixmap(const QStringList &paths) const;

    void setDropTargetRow(int row);

    QPoint m_pressPosition;
    bool m_pressed = false;
    int m_dropTargetRow = -1;
    int m_bodyInsetHorizontal = 0;
    int m_bodyInsetVertical = 0;
    int m_appliedBottomInset = -1;
};

} // namespace pf::ui
