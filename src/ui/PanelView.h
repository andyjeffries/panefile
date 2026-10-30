#pragma once

#include <QListView>
#include <QString>
#include <QTimer>

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

    /// Whether the panel this view belongs to is the focused one.
    ///
    /// The delegate's idea of "focused" used to be QStyle::State_Active, which
    /// Qt sets from the *window's* activation, not the panel's. Open a modal,
    /// focus the filter field or switch to another application and the focused
    /// panel lost its cursor colour and dimmed its names like an unfocused one,
    /// while its header and accent edge still said it was focused. The panel
    /// knows which it is; it says so here.
    void setPanelActive(bool active);
    bool isPanelActive() const;

    /// Drawn centred in the list when it has no rows: a faint symbolic icon, a
    /// heading and one muted line under it. An empty title draws nothing.
    ///
    /// An empty folder was a blank pane, which reads the same as a pane that
    /// has not finished loading or one that failed to.
    void setPlaceholder(const QString &title, const QString &detail = {},
                        const QString &iconName = {});

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

    /// Type-to-select: typing a name moves the cursor to the first entry that
    /// starts with it.
    ///
    /// Replaces Qt's own search, which skips the current row, compares the
    /// whole display text and forgets what was typed after 400 ms. Here a name
    /// is compared without its leading dots — `.bashrc` is found by typing
    /// `ba` — matching ignores case, and the buffer lasts
    /// QApplication::keyboardInputInterval(), which the `type_ahead_timeout_ms`
    /// setting controls. Typing one letter repeatedly cycles through the
    /// entries starting with it, unless a name starts with the repeat itself.
    void keyboardSearch(const QString &search) override;

    /// Whether a name is being typed: the buffer is non-empty and has not timed
    /// out. While it is, Space and `.` belong to the name rather than to Quick
    /// Look and hidden files, Backspace edits it and Escape abandons it, so the
    /// application routes those keys here ahead of its key bindings.
    bool isTypingAhead() const;
    QString typeAheadText() const;
    void clearTypeAhead();

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

    /// A right-click on a row, or the Menu key with a cursor row. The cursor
    /// is on `name`. Never emitted for empty space, where there is nothing to
    /// act on.
    void contextMenuRequested(const QString &name);

    /// A row was double-clicked with the left button; the cursor is on it.
    ///
    /// Its own signal rather than QAbstractItemView::activated, which Qt only
    /// emits for a double-click when its private record of the pressed index
    /// still matches the row — and between the two clicks the panel's own
    /// click handling has run. Double-clicking a file did nothing.
    void rowDoubleClicked();

    /// A drag is starting; the panel supplies the paths, because the selection
    /// belongs to it rather than to the view.
    void dragPathsRequested(QStringList *paths);

    /// The directory this view is showing, for resolving a drop on empty space.
    void currentDirectoryRequested(QString *path);

    /// The type-ahead buffer changed; empty once it is cleared or expires.
    void typeAheadChanged(const QString &text);

    /// Type-ahead moved the cursor to the entry matching the typed name.
    void cursorMovedByTyping();

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
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;

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

    /// The first row, counting from `from` and wrapping, whose name without
    /// its leading dots starts with `typed`; -1 when there is none.
    int typeAheadRow(const QString &typed, int from) const;

    void setTypeAhead(const QString &text);

    QPoint m_pressPosition;
    bool m_pressed = false;
    int m_dropTargetRow = -1;
    bool m_panelActive = false;
    QString m_placeholderTitle;
    QString m_placeholderDetail;
    QString m_placeholderIcon;
    int m_bodyInsetHorizontal = 0;
    int m_bodyInsetVertical = 0;
    int m_appliedBottomInset = -1;
    QString m_typeAhead;
    QTimer m_typeAheadTimer;
    /// Set while type-ahead itself moves the cursor, so that the move does not
    /// count as the user leaving the name they are typing.
    bool m_typeAheadMoving = false;
};

} // namespace pf::ui
