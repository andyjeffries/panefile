#include "ui/modals/FindModal.h"

#include "ui/ThemePalette.h"

#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace pf::ui {
namespace {

/// How many results the list widget holds. The rest are found and counted but
/// not built into widgets: nobody scrolls to the eight thousandth hit, and
/// constructing that many QListWidgetItems is measurable.
constexpr int kVisibleResultLimit = 500;

} // namespace

FindModal::FindModal(QWidget *parent)
    : Modal(parent), m_query(new QLineEdit), m_results(new QListWidget), m_status(new QLabel),
      m_debounce(new QTimer(this)), m_refresh(new QTimer(this))
{
    setSizePercent(60, 70);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_query->setObjectName(QStringLiteral("findQuery"));
    m_query->setPlaceholderText(tr("Find in this directory tree…"));
    m_query->setClearButtonEnabled(true);
    layout->addWidget(m_query);

    m_results->setObjectName(QStringLiteral("findResults"));
    m_results->setUniformItemSizes(true);
    m_results->setAlternatingRowColors(false);
    // The query box keeps focus so typing never has to be interrupted to move
    // the selection; Up and Down are forwarded from there.
    m_results->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(m_results, 1);

    m_status->setObjectName(QStringLiteral("findStatus"));
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kSearchDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &FindModal::restartSearch);

    m_refresh->setInterval(kRefreshIntervalMs);
    connect(m_refresh, &QTimer::timeout, this, [this] {
        if (m_dirty) {
            refreshList();
        }
    });

    connect(m_query, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });

    connect(&m_finder, &fs::RecursiveFinder::resultsReady, this, &FindModal::onResults);
    connect(&m_finder, &fs::RecursiveFinder::finished, this, &FindModal::onFinished);

    connect(m_results, &QListWidget::itemActivated, this, [this] { accept(); });
}

void FindModal::setSearchRoot(const QString &root)
{
    m_root = root;
}

void FindModal::setMaxResults(int maximum)
{
    m_finder.setMaxResults(maximum);
}

void FindModal::setRespectGitignore(bool respect)
{
    m_finder.setRespectGitignore(respect);
}

void FindModal::setFuzzyMatching(bool fuzzy)
{
    m_finder.setFuzzyMatching(fuzzy);
}

void FindModal::setIncludeHidden(bool include)
{
    m_finder.setIncludeHidden(include);
}

void FindModal::start()
{
    m_query->clear();
    m_results->clear();
    m_collected.clear();
    m_total = 0;
    m_truncated = false;
    m_status->setText(tr("Searching %1").arg(m_root));

    showModal();
    m_query->setFocus(Qt::ShortcutFocusReason);

    m_refresh->start();
    restartSearch();
}

void FindModal::restartSearch()
{
    m_results->clear();
    m_collected.clear();
    m_total = 0;
    m_truncated = false;

    if (m_root.isEmpty()) {
        return;
    }
    m_finder.search(m_root, m_query->text());
}

void FindModal::onResults(const QList<fs::FindResult> &results)
{
    m_collected.append(results);
    m_dirty = true;
}

void FindModal::onFinished(int total, bool truncated)
{
    m_total = total;
    m_truncated = truncated;
    m_dirty = true;
    refreshList();
    m_refresh->stop();
}

void FindModal::refreshList()
{
    m_dirty = false;

    // Best first. Ties on relative path so the order is stable between
    // refreshes — a list that reshuffles its equal-scoring rows every 100 ms is
    // unusable even though every individual ordering is defensible.
    std::ranges::sort(m_collected, [](const fs::FindResult &a, const fs::FindResult &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.relativePath < b.relativePath;
    });

    const QString selected = m_results->currentItem() != nullptr
                                 ? m_results->currentItem()->data(Qt::UserRole).toString()
                                 : QString();

    m_results->clear();

    const int shown = std::min<int>(kVisibleResultLimit, static_cast<int>(m_collected.size()));
    for (int i = 0; i < shown; ++i) {
        const fs::FindResult &result = m_collected.at(i);
        auto *item = new QListWidgetItem(
            result.isDir ? result.relativePath + QLatin1Char('/') : result.relativePath, m_results);
        item->setData(Qt::UserRole, result.absolutePath);
    }

    // The selection follows the path it was on, not the row index: the row a
    // path sits at changes on every re-sort.
    if (!selected.isEmpty()) {
        for (int i = 0; i < m_results->count(); ++i) {
            if (m_results->item(i)->data(Qt::UserRole).toString() == selected) {
                m_results->setCurrentRow(i);
                break;
            }
        }
    }
    if (m_results->currentRow() < 0 && m_results->count() > 0) {
        m_results->setCurrentRow(0);
    }

    const int found = std::max<int>(m_total, static_cast<int>(m_collected.size()));
    if (m_truncated) {
        // §7.8 caps the walk; saying so is the difference between "there are no
        // more" and "we stopped looking".
        m_status->setText(tr("%n match(es), search stopped at the limit", nullptr, found));
    } else if (m_finder.isRunning()) {
        m_status->setText(tr("%n match(es), still searching…", nullptr, found));
    } else {
        m_status->setText(tr("%n match(es)", nullptr, found));
    }
}

void FindModal::moveSelection(int delta)
{
    if (m_results->count() == 0) {
        return;
    }
    const int row = qBound(0, m_results->currentRow() + delta, m_results->count() - 1);
    m_results->setCurrentRow(row);
}

void FindModal::keyPressEvent(QKeyEvent *event)
{
    // §6.1: the query box has focus, so this is Typing mode and single-key
    // bindings are suspended. Movement still has to work without leaving the
    // box, which is what these forward.
    switch (event->key()) {
    case Qt::Key_Down:
        moveSelection(1);
        return;
    case Qt::Key_Up:
        moveSelection(-1);
        return;
    case Qt::Key_PageDown:
        moveSelection(10);
        return;
    case Qt::Key_PageUp:
        moveSelection(-10);
        return;
    default:
        break;
    }

    Modal::keyPressEvent(event);
}

void FindModal::accept()
{
    if (const QListWidgetItem *item = m_results->currentItem(); item != nullptr) {
        Q_EMIT resultChosen(item->data(Qt::UserRole).toString());
    }

    m_finder.cancel();
    m_refresh->stop();
    Modal::accept();
}

} // namespace pf::ui
