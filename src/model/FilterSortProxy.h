#pragma once

#include "core/FuzzyMatcher.h"

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QString>

namespace pf {

/// Sort key for a panel (§4.4).
enum class SortKey {
    Name,
    Size,
    Modified,
    Type,
    Random,
};

/// Parses `panels.default_sort` and the `o` menu's names. Anything
/// unrecognised is Name, which is §8's documented default.
SortKey sortKeyFromName(const QString &name);
QString sortKeyName(SortKey key);

/// Per-panel filtering and sorting (§4.4).
///
/// Applied in order: the hidden-file filter, then the search string, then the
/// sort. Each panel owns one of these, which is what lets two panels show the
/// same directory with different sort orders — they share a DirectoryModel but
/// not a proxy.
class FilterSortProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit FilterSortProxy(QObject *parent = nullptr);

    void setShowHidden(bool show);
    bool showHidden() const;

    void setDirectoriesFirst(bool first);
    bool directoriesFirst() const;

    void setSortKey(SortKey key);
    SortKey sortKey() const;

    void setReverseSort(bool reverse);
    bool reverseSort() const;

    /// §7.8's in-panel filter. Substring by default, fuzzy when enabled.
    void setFilterText(const QString &text);
    QString filterText() const;

    /// §7.8's `config.search.fuzzy`. Changing it refilters, because the two
    /// rules accept different sets and a stale filter would show the wrong one.
    void setFuzzyMatching(bool fuzzy);
    bool fuzzyMatching() const;

    /// The match for one source row, so the delegate can highlight the spans
    /// §7.8 asks it to. Empty when there is no filter.
    ///
    /// Recomputed rather than cached: the alternative is a parallel map keyed
    /// on row that has to be invalidated on every model change, and the matcher
    /// is cheap enough that a visible row costing one match per paint is not
    /// where §11's frame budget goes.
    FuzzyMatch matchFor(const QString &name) const;

    /// §7.8: fuzzy results are ranked by score, not by name. Only while a
    /// fuzzy filter is active — an unfiltered listing keeps §4.4's ordering.
    bool isRankingByScore() const;

    /// Re-seeds the ordering used by SortKey::Random. Without this a random
    /// sort would reshuffle on every filter change, which is disorienting
    /// rather than random.
    void reshuffle();

    /// Serves MatchSpansRole (§4.2) from the active filter. The spans belong to
    /// the proxy rather than the model because the model has no idea a filter
    /// exists, and two panels showing the same directory can be filtering
    /// differently.
    QVariant data(const QModelIndex &index, int role) const override;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    /// Qt 6.9 replaced invalidateFilter() with a begin/end pair and deprecated
    /// the old call in 6.10, but §2 sets the floor at 6.7 — so both spellings
    /// have to be available. invalidate() would paper over this, at the cost of
    /// re-sorting every row on each keystroke of a filter; in a 100,000-entry
    /// directory that is exactly the stutter §11 rules out.
    void refilter();

    bool m_showHidden = false;
    bool m_directoriesFirst = true;
    bool m_reverseSort = false;
    SortKey m_sortKey = SortKey::Name;
    QString m_filterText;
    bool m_fuzzy = false;
    quint32 m_randomSeed = 1;

    /// Handles the locale-aware half of §4.4's ordering; naturalCompare() drives
    /// it and handles the numeric half itself, because QCollator's numeric mode
    /// depends on ICU and is silently ignored without it. Expensive to
    /// construct and consulted once per comparison, so it is built once and
    /// kept. Mutable because lessThan() is const and QCollator::compare() is
    /// not.
    mutable QCollator m_collator;
};

} // namespace pf
