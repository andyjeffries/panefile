#pragma once

#include <QCollator>
#include <QSortFilterProxyModel>

namespace pf {

/// Sort key for a panel (§4.4).
enum class SortKey {
    Name,
    Size,
    Modified,
    Type,
    Random,
};

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

    /// Substring filter over the current directory (§7.8's in-panel filter).
    /// Fuzzy matching arrives in M7 and replaces the matching rule, not the
    /// plumbing.
    void setFilterText(const QString &text);
    QString filterText() const;

    /// Re-seeds the ordering used by SortKey::Random. Without this a random
    /// sort would reshuffle on every filter change, which is disorienting
    /// rather than random.
    void reshuffle();

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
    quint32 m_randomSeed = 1;

    /// §4.4 requires natural, locale-aware name sorting — file2 before file10.
    /// QCollator is expensive to construct and is consulted once per
    /// comparison, so it is built once and kept. Mutable because lessThan() is
    /// const and QCollator::compare() is not.
    mutable QCollator m_collator;
};

} // namespace pf
