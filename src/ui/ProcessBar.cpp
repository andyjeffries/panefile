#include "ui/ProcessBar.h"

#include "core/Format.h"
#include "core/Logging.h"
#include "fs/JobEngine.h"
#include "ui/ThemePalette.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pf::ui {
namespace {

/// Rate is smoothed over the whole job rather than sampled between updates:
/// progress arrives every chunk, and an instantaneous rate computed from two
/// adjacent samples jitters far too much to read.
QString formatRate(quint64 bytes, qint64 elapsedMs)
{
    if (elapsedMs < 500 || bytes == 0) {
        return {};
    }
    const quint64 perSecond = bytes * 1000 / static_cast<quint64>(elapsedMs);
    return QObject::tr("%1/s").arg(formatSize(perSecond));
}

} // namespace

ProcessBar::ProcessBar(fs::JobEngine *engine, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_summary(new QLabel(this)),
      m_progress(new QProgressBar(this)), m_jobs(new QTreeWidget(this)),
      m_lingerTimer(new QTimer(this))
{
    setObjectName(QStringLiteral("processBar"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(currentPalette().panelPadding, 4, currentPalette().panelPadding, 4);
    layout->setSpacing(4);

    auto *row = new QWidget(this);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);

    m_summary->setObjectName(QStringLiteral("processSummary"));
    m_summary->setTextFormat(Qt::PlainText);
    rowLayout->addWidget(m_summary, 1);

    m_progress->setObjectName(QStringLiteral("processProgress"));
    m_progress->setTextVisible(true);
    m_progress->setFixedWidth(220);
    rowLayout->addWidget(m_progress, 0);

    layout->addWidget(row);

    m_jobs->setObjectName(QStringLiteral("processJobs"));
    m_jobs->setColumnCount(3);
    m_jobs->setHeaderLabels({tr("Job"), tr("Progress"), tr("Detail")});
    m_jobs->setRootIsDecorated(false);
    m_jobs->setUniformRowHeights(true);
    m_jobs->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_jobs->setMaximumHeight(140);
    m_jobs->hide();
    layout->addWidget(m_jobs);

    m_lingerTimer->setSingleShot(true);
    m_lingerTimer->setInterval(kLingerMs);
    connect(m_lingerTimer, &QTimer::timeout, this, [this] {
        // §7.4: "Completed jobs linger for 5 s then fade." Only once nothing is
        // running — a job that started while the timer ran keeps the bar up.
        if (m_engine->activeCount() == 0 && m_engine->queuedCount() == 0) {
            for (const int id : m_items.keys()) {
                m_engine->forget(id);
            }
            m_items.clear();
            m_jobs->clear();
            Q_EMIT becameIdle();
        }
    });

    connect(engine, &fs::JobEngine::jobSubmitted, this, &ProcessBar::onSubmitted);
    connect(engine, &fs::JobEngine::jobProgress, this, &ProcessBar::onProgress);
    connect(engine, &fs::JobEngine::jobFinished, this, &ProcessBar::onFinished);
    connect(engine, &fs::JobEngine::aggregateProgress, this, &ProcessBar::onAggregate);
}

void ProcessBar::onSubmitted(int jobId, const QString &description)
{
    auto *item = new QTreeWidgetItem(m_jobs);
    item->setText(0, description);
    item->setText(1, tr("waiting"));
    m_items.insert(jobId, item);

    m_lingerTimer->stop();
}

void ProcessBar::onProgress(int jobId, quint64 bytesDone, quint64 bytesTotal, int filesDone,
                            int filesTotal, const QString &currentPath)
{
    QTreeWidgetItem *item = m_items.value(jobId);
    if (item == nullptr) {
        return;
    }

    // Bytes where there are any, files otherwise: a directory of empty files
    // has a byte total of zero and would sit at 0% for its whole run.
    int percent = 0;
    if (bytesTotal > 0) {
        percent = static_cast<int>(bytesDone * 100 / bytesTotal);
    } else if (filesTotal > 0) {
        percent = filesDone * 100 / filesTotal;
    }

    item->setText(1, QStringLiteral("%1%").arg(percent));
    item->setText(2, QStringLiteral("%1 / %2 — %3")
                         .arg(filesDone)
                         .arg(filesTotal)
                         .arg(QFileInfo(currentPath).fileName()));
}

void ProcessBar::onFinished(int jobId, const fs::JobResult &result)
{
    QTreeWidgetItem *item = m_items.value(jobId);
    if (item != nullptr) {
        if (result.cancelled) {
            item->setText(1, tr("cancelled"));
        } else if (!result.errors.isEmpty()) {
            // §12: "Non-fatal job errors accumulate into a summary shown at job
            // end, listing failures with reasons." The count goes here; the
            // list is one click away in the expanded view.
            item->setText(1, tr("%n error(s)", nullptr, static_cast<int>(result.errors.size())));
            item->setText(2, result.errors.first().reason);
            item->setForeground(1, currentPalette().error);
        } else {
            item->setText(1, tr("done"));
        }
    }

    scheduleIdleCheck();
}

void ProcessBar::scheduleIdleCheck()
{
    if (m_engine->activeCount() == 0 && m_engine->queuedCount() == 0) {
        m_lingerTimer->start();
    }
}

void ProcessBar::onAggregate(quint64 bytesDone, quint64 bytesTotal, int activeJobs)
{
    static QElapsedTimer elapsed;
    if (activeJobs > 0 && !elapsed.isValid()) {
        elapsed.start();
    } else if (activeJobs == 0) {
        elapsed.invalidate();
    }

    if (activeJobs == 0) {
        m_summary->setText(tr("Idle"));
        m_progress->setValue(m_progress->maximum());
        return;
    }

    if (bytesTotal == 0) {
        // §7.4: "Show an indeterminate bar during enumeration." Until the
        // counting pass finishes there is no honest percentage to show, and a
        // bar sitting at zero reads as stuck rather than as counting.
        m_progress->setRange(0, 0);
        m_summary->setText(tr("Preparing %n job(s)…", nullptr, activeJobs));
        return;
    }

    m_progress->setRange(0, 100);
    m_progress->setValue(static_cast<int>(bytesDone * 100 / bytesTotal));

    const QString rate = elapsed.isValid() ? formatRate(bytesDone, elapsed.elapsed()) : QString();

    // The rate is omitted for the first half-second rather than shown as a wild
    // figure derived from too little data.
    QString summary = tr("%n job(s) · %1 of %2", nullptr, activeJobs)
                          .arg(formatSize(bytesDone), formatSize(bytesTotal));
    if (!rate.isEmpty()) {
        summary += QStringLiteral(" · ") + rate;
    }
    m_summary->setText(summary);
}

void ProcessBar::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_jobs->setVisible(expanded);
}

bool ProcessBar::isExpanded() const
{
    return m_expanded;
}

void ProcessBar::toggleExpanded()
{
    setExpanded(!m_expanded);
}

} // namespace pf::ui
