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
    /// How long a finished job stays visible before the bar hides itself.
    ///
    /// §7.4 says five seconds. That is a long time to watch a bar that has
    /// nothing left to say, and for the small copies that make up most of what
    /// anyone does it is several times longer than the work took. One second is
    /// enough to see that it finished.
    static constexpr int kLingerMs = 1000;

    /// How long a job must run before the bar appears at all.
    ///
    /// Copying three small files is over before this elapses, and a progress
    /// bar that flashes up and vanishes is worse than no progress bar: it draws
    /// the eye to something that has already stopped being true. Work that
    /// outlasts the delay is work worth reporting on.
    static constexpr int kAppearDelayMs = 250;

    ProcessBar(fs::JobEngine *engine, QWidget *parent = nullptr);

    void setExpanded(bool expanded);
    bool isExpanded() const;
    void toggleExpanded();

Q_SIGNALS:
    /// Emitted when the last job finishes and the linger elapses, so the window
    /// can hide the bar.
    void becameIdle();

    /// A job has been running long enough to be worth showing. The composition
    /// root puts the bar on screen in response — the bar itself does not know
    /// where it lives in the window.
    void shouldAppear();

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
    QTimer *m_appearTimer = nullptr;

    QHash<int, QTreeWidgetItem *> m_items;
    bool m_expanded = false;
};

} // namespace pf::ui
