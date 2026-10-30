#pragma once

#include "ui/modals/Modal.h"

#include <QList>
#include <QString>

class QLabel;
class QLineEdit;
class QListView;

namespace pf::ui {

class GlyphListModel;

/// The folder icon picker: a searchable grid of Heroicons, with Default and
/// the standard folders' glyphs above it.
///
/// The query box keeps focus; the arrow keys move a cursor through the strip
/// and the grid as one surface (Up from the grid's top row enters the strip).
/// Enter applies, Escape cancels. The header shows the folder being changed
/// and a live preview of the highlighted choice at three sizes.
///
/// The Heroicons directory is read when the picker first opens, not at start-up.
/// Without it only Default and the presets are offered, with a note.
class FolderIconModal : public Modal
{
    Q_OBJECT

public:
    explicit FolderIconModal(QWidget *parent);

    /// Opens for the folder at `path`. `currentGlyph` is its present choice
    /// (empty for Default), and where the cursor starts.
    void open(const QString &path, const QString &currentGlyph);

    void setQuery(const QString &query);

    /// Glyph ids offered now, strip first then grid; empty string is Default.
    QStringList visibleGlyphs() const;

    /// The highlighted choice's glyph id.
    QString cursorGlyph() const;

    /// Forwards the cursor keys from the query box to the grid. Public because
    /// QObject declares it so.
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    /// The user applied a choice. Empty means "reset to the default".
    void chosen(const QString &glyphId);

protected:
    void accept() override;
    void keyPressEvent(QKeyEvent *event) override;
    QWidget *initialFocusWidget() override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Cell {
        QString glyph; ///< empty for Default
        QString title;
        QString name; ///< what the query matches
    };

    void refresh();
    void layoutViews();
    void moveCursor(int dRow, int dColumn);
    void select(bool strip, int index);
    void updatePreview();
    int columns() const;

    QLabel *m_title = nullptr;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_preview96 = nullptr;
    QLabel *m_preview32 = nullptr;
    QLabel *m_preview16 = nullptr;
    QLabel *m_current = nullptr;
    QLineEdit *m_query = nullptr;
    QLabel *m_stripHeading = nullptr;
    QLabel *m_gridHeading = nullptr;
    QLabel *m_gridCount = nullptr;
    QLabel *m_notInstalled = nullptr;
    QListView *m_stripView = nullptr;
    QListView *m_gridView = nullptr;
    GlyphListModel *m_stripModel = nullptr;
    GlyphListModel *m_gridModel = nullptr;

    QList<Cell> m_all;
    QString m_currentGlyph;
    bool m_cursorInStrip = true;
};

} // namespace pf::ui
