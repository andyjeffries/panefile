#pragma once

#include "model/FilterSortProxy.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QListView;

namespace pf {
class DirectoryModel;
}

namespace pf::ui {

class FileItemDelegate;

/// One independent directory panel (§5.2).
///
/// Panels are peers: none is the parent of another, and each keeps its own
/// working directory, cursor, selection, history, sort order and filter. That
/// independence is the whole design, so nothing here reaches for a "current
/// panel" global or asks its parent what to display.
class FilePanel : public QWidget
{
    Q_OBJECT

public:
    explicit FilePanel(QWidget *parent = nullptr);
    ~FilePanel() override;

    void navigateTo(const QString &path);
    QString path() const;

    /// Navigates to the parent, leaving the cursor on the directory just left.
    void goToParent();

    /// Enters the directory under the cursor, or emits fileActivated for a file.
    void activateCursorItem();

    bool goBack();
    bool goForward();

    QString cursorName() const;
    void setCursorName(const QString &name);

    void moveCursor(int delta);
    void moveCursorToStart();
    void moveCursorToEnd();
    void movePage(int direction);

    void setShowHidden(bool show);
    bool showHidden() const;
    void toggleShowHidden();

    void setSortKey(SortKey key);
    SortKey sortKey() const;
    void setReverseSort(bool reverse);
    bool reverseSort() const;

    void setFilterText(const QString &text);

    /// True when this panel is the focused one. Drives the border and
    /// background treatment that §9 calls the single most important visual
    /// affordance in the application.
    void setActive(bool active);
    bool isActive() const;

    QListView *view() const;

Q_SIGNALS:
    void pathChanged(const QString &path);
    void cursorChanged(const QString &name);
    void fileActivated(const QString &absolutePath);

    /// Transient message for the footer — "nothing to go back to", and similar.
    void statusMessage(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyPalette();
    void applyHeaderElision();
    void onScanFinished(const QString &path, int count);
    void onScanFailed(const QString &path, const QString &reason);
    void updateHeader();
    void restoreCursor();
    void rememberCursor();
    void setPathInternal(const QString &path, bool pushHistory);

    // Declaration order is initialisation order, and these are constructed in
    // the member initialiser list, so it has to match it.
    DirectoryModel *m_model = nullptr;
    FilterSortProxy *m_proxy = nullptr;
    QListView *m_view = nullptr;
    FileItemDelegate *m_delegate = nullptr;
    QLabel *m_header = nullptr;
    QLabel *m_status = nullptr;

    QString m_path;
    QSet<QString> m_selection;
    QStringList m_backStack;
    QStringList m_forwardStack;

    /// The name to place the cursor on once the current scan delivers rows.
    /// Cleared when applied — a scan that finds nothing must not leave a stale
    /// target that hijacks the cursor after the next navigation.
    QString m_pendingCursorName;

    /// The header's full text before elision. The label shows an elided copy
    /// sized to whatever width the panel currently has, so the untruncated
    /// version has to be kept to re-elide on resize.
    QString m_headerText;

    bool m_active = false;
};

} // namespace pf::ui
