#pragma once

#include <QWidget>

namespace pf::ui {

/// The backdrop for Quick Look's float and full modes (§7.6).
///
/// §7.6: "Centred frameless overlay at 70% of window size (configurable),
/// dimmed backdrop, drop shadow… **Never** a separate top-level window — that
/// matters on a tiling compositor." So this is a child widget covering the
/// content area, exactly as Modal is.
///
/// It differs from Modal in one deliberate way: it never takes focus. §7.6
/// requires that "while it is open the arrow keys still move the panel cursor",
/// and the simplest way to guarantee that is for the panel never to lose focus
/// in the first place — there is then nothing to forward.
class QuickLookOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit QuickLookOverlay(QWidget *parent);

    /// The widget shown inside the overlay. Not owned: the view outlives any
    /// number of dock changes and is re-parented between them.
    void setContentWidget(QWidget *content);
    QWidget *contentWidget() const;

    /// Fraction of the parent the content occupies. 100 is full mode, which
    /// also drops the dimming — there is nothing behind it left to see.
    void setSizePercent(int percent);
    int sizePercent() const;

    /// Shows the overlay over its parent and starts tracking its size.
    void showOverlay();
    void hideOverlay();

    /// Tracks the parent's resizes. Public because QObject declares it public.
    bool eventFilter(QObject *watched, QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

Q_SIGNALS:
    /// A click on the backdrop, outside the content. Dismisses, as it does in
    /// macOS Quick Look.
    void backdropClicked();

private:
    void reposition();

    QWidget *m_content = nullptr;
    int m_percent = 70;
};

} // namespace pf::ui
