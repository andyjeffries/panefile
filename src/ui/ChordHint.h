#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

namespace pf::ui {

/// What can follow a half-typed key sequence: press `g`, pause, and a card
/// lists `h` Go to the home directory, `r` Go to the filesystem root, and so
/// on, until the next key, Escape or a click.
///
/// A child of the main window rather than a popup window, for §5.4's reason: a
/// tiling compositor would tile a top-level window instead of floating it. It
/// takes no focus and no input, so the keys keep going to the dispatcher that
/// asked for it.
class ChordHint : public QWidget
{
    Q_OBJECT

public:
    /// One continuation: the keys still to press, rendered ("h", or "d d"), and
    /// what they do.
    using Row = QPair<QString, QString>;

    explicit ChordHint(QWidget *parent);

    /// Shows the card for `pending` (rendered, e.g. "g-") anchored to the
    /// bottom-right of `area`, in the parent's coordinates.
    void present(const QString &pending, const QList<Row> &rows, const QRect &area);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_pending;
    QList<Row> m_rows;
};

} // namespace pf::ui
