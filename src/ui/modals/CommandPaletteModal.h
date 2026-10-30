#pragma once

#include "core/FuzzyMatcher.h"
#include "ui/modals/Modal.h"

class QAction;
class QLineEdit;
class QListWidget;

namespace pf::ui {

/// One row of the palette.
struct PaletteItem {
    /// An action id, for actions; empty for places.
    QString id;
    QString title;
    /// Faint text before the keys: the category, or a place's path.
    QString detail;
    /// A rendered binding such as "Ctrl+F", drawn as keycaps.
    QString keys;
    /// 0 for actions that fit the current selection, 1 for the rest, 2 for
    /// sidebar places. Groups stay in this order whatever the query scores.
    int group = 1;
    /// A place's absolute path.
    QString path;
    /// A SymbolicIcon glyph name, drawn before the title.
    QString icon;
};

/// A ranked row: the item and where the query matched its title.
struct PaletteRow {
    PaletteItem item;
    QList<MatchSpan> spans;
    int score = 0;
};

/// The command palette (Ctrl+K): a fuzzy-searchable list of what can be done
/// right now, with the keys that do it.
///
/// Modelled on FindModal — a query box that keeps focus over a list it moves
/// with Up and Down — but over a fixed list, so it filters on every keystroke
/// rather than restarting a walk.
class CommandPaletteModal : public Modal
{
    Q_OBJECT

public:
    explicit CommandPaletteModal(QWidget *parent);

    /// Shows the palette over `items`.
    void open(QList<PaletteItem> items);

    /// The rows `query` leaves, best first within each group. An empty query
    /// keeps everything in its given order. Pure, so the ordering is testable
    /// without a widget.
    static QList<PaletteRow> rank(const QList<PaletteItem> &items, const QString &query);

    /// What is listed now, in order; for tests.
    QList<PaletteItem> visibleItems() const;

    void setQuery(const QString &query);

Q_SIGNALS:
    void actionChosen(const QString &id);
    void placeChosen(const QString &path);

protected:
    void accept() override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    QWidget *initialFocusWidget() override;

private:
    void refresh();
    void moveSelection(int delta);

    QLineEdit *m_query = nullptr;
    QListWidget *m_list = nullptr;
    QAction *m_searchAction = nullptr;

    QList<PaletteItem> m_items;
    QList<PaletteRow> m_rows;
};

} // namespace pf::ui
