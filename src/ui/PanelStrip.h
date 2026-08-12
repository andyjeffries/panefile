#pragma once

#include <QList>
#include <QWidget>

class QSplitter;

namespace pf::ui {

class FilePanel;

/// The horizontal strip of panels (§5.1, §7.1).
///
/// A QSplitter of FilePanels, each independent of the others. Panels are peers:
/// creating, splitting, closing and cycling them are the operations, and there
/// is no notion of one panel owning another.
class PanelStrip : public QWidget
{
    Q_OBJECT

public:
    /// §7.1: minimum 1, maximum 10.
    static constexpr int kMinPanels = 1;
    static constexpr int kMaxPanels = 10;

    explicit PanelStrip(QWidget *parent = nullptr);

    /// Adds a panel at `path`, focuses it, and returns it. Returns nullptr when
    /// the maximum is already reached — §7.1 wants a transient footer message
    /// rather than a modal, which the caller emits from panelLimitReached().
    FilePanel *addPanel(const QString &path);

    /// Duplicates the focused panel's path, sort order and filter, but not its
    /// selection (§7.1).
    FilePanel *splitFocusedPanel();

    /// Closes a panel. §7.1: never the last one; focus moves to the panel on
    /// its left, or to the right if it was leftmost.
    bool closePanel(FilePanel *panel);
    bool closeFocusedPanel();

    void focusNext();
    void focusPrevious();
    void focusPanelAt(int index);
    void setFocusedPanel(FilePanel *panel);

    FilePanel *focusedPanel() const;
    int focusedIndex() const;
    int count() const;
    FilePanel *panelAt(int index) const;
    QList<FilePanel *> panels() const;

    /// §5.1: resets the splitter to equal widths.
    void equalise();

    /// §7.1: below 400 px of total width, only the focused panel is shown.
    /// Applied on resize.
    void applyResponsiveLayout(int availableWidth);

Q_SIGNALS:
    void focusedPanelChanged(FilePanel *panel);
    void panelCountChanged(int count);
    void panelLimitReached();
    void statusMessage(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void connectPanel(FilePanel *panel) const;

    QSplitter *m_splitter = nullptr;
    QList<FilePanel *> m_panels;
    FilePanel *m_focused = nullptr;
    bool m_compact = false;
};

} // namespace pf::ui
