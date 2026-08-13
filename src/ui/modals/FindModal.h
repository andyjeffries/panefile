#pragma once

#include "fs/RecursiveFinder.h"
#include "ui/modals/Modal.h"

class QLabel;
class QLineEdit;
class QListWidget;

namespace pf::ui {

/// §7.8's recursive fuzzy finder.
///
/// "A modal that walks the panel's subtree on a worker thread, streaming
/// candidates into a scored list. Enter navigates the panel to the result's
/// directory and puts the cursor on it."
///
/// The query restarts the walk rather than filtering what the last one found.
/// Filtering would be faster and would be wrong: the previous walk stopped at
/// max_results, so anything it never reached could not be filtered back in.
class FindModal : public Modal
{
    Q_OBJECT

public:
    /// Rescored and re-sorted this often while results stream in. A re-sort per
    /// batch would make the list jump under the cursor several times a second.
    static constexpr int kRefreshIntervalMs = 100;

    /// How long after the last keystroke the walk restarts. Longer than Quick
    /// Look's 120 ms: restarting a subtree walk costs far more than a file read.
    static constexpr int kSearchDebounceMs = 200;

    explicit FindModal(QWidget *parent);

    void setSearchRoot(const QString &root);
    void setMaxResults(int maximum);
    void setRespectGitignore(bool respect);
    void setFuzzyMatching(bool fuzzy);
    void setIncludeHidden(bool include);

    /// Clears the previous search and shows the modal.
    void start();

Q_SIGNALS:
    /// §7.8: "Enter navigates the panel to the result's directory and puts the
    /// cursor on it."
    void resultChosen(const QString &absolutePath);

protected:
    void accept() override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void restartSearch();
    void onResults(const QList<fs::FindResult> &results);
    void onFinished(int total, bool truncated);
    void refreshList();
    void moveSelection(int delta);

    fs::RecursiveFinder m_finder;

    QLineEdit *m_query = nullptr;
    QListWidget *m_results = nullptr;
    QLabel *m_status = nullptr;

    QString m_root;

    /// Everything the current walk has produced, unsorted. Sorted into the list
    /// on the refresh tick rather than on arrival.
    QList<fs::FindResult> m_collected;

    QTimer *m_debounce = nullptr;
    QTimer *m_refresh = nullptr;

    bool m_dirty = false;
    bool m_truncated = false;
    int m_total = 0;
};

} // namespace pf::ui
