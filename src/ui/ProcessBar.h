#pragma once

#include "fs/Job.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace pf::fs {
class JobEngine;
}

namespace pf::ui {

/// Progress for the running jobs (§5.1, §7.4).
///
/// "auto-shows when jobs active; `p` focuses. Aggregate progress, expandable to
/// per-job list… Completed jobs linger for 5 s then fade."
///
/// §3.4 makes this lazy: it is constructed when the first job starts, not at
/// startup, because a user who copies nothing should never pay for it.
class ProcessBar : public QWidget
{
    Q_OBJECT

public:
    /// §7.4: how long a finished job stays visible.
    static constexpr int kLingerMs = 5000;

    ProcessBar(fs::JobEngine *engine, QWidget *parent = nullptr);

    void setExpanded(bool expanded);
    bool isExpanded() const;
    void toggleExpanded();

Q_SIGNALS:
    /// Emitted when the last job finishes and the linger elapses, so the window
    /// can hide the bar.
    void becameIdle();

    void cancelRequested(int jobId);

private:
    void onSubmitted(int jobId, const QString &description);
    void onProgress(int jobId, quint64 bytesDone, quint64 bytesTotal, int filesDone, int filesTotal,
                    const QString &currentPath);
    void onFinished(int jobId, const fs::JobResult &result);
    void onAggregate(quint64 bytesDone, quint64 bytesTotal, int activeJobs);
    void scheduleIdleCheck();

    fs::JobEngine *m_engine = nullptr;

    QLabel *m_summary = nullptr;
    QProgressBar *m_progress = nullptr;
    QTreeWidget *m_jobs = nullptr;
    QTimer *m_lingerTimer = nullptr;

    QHash<int, QTreeWidgetItem *> m_items;
    bool m_expanded = false;
};

} // namespace pf::ui
