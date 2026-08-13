#include "model/FilterSortProxy.h"

#include "core/NaturalCompare.h"
#include "model/DirectoryModel.h"
#include "model/FileEntry.h"

namespace pf {

SortKey sortKeyFromName(const QString &name)
{
    if (name == QLatin1String("size")) {
        return SortKey::Size;
    }
    if (name == QLatin1String("modified")) {
        return SortKey::Modified;
    }
    if (name == QLatin1String("type")) {
        return SortKey::Type;
    }
    if (name == QLatin1String("random")) {
        return SortKey::Random;
    }
    return SortKey::Name;
}

QString sortKeyName(SortKey key)
{
    switch (key) {
    case SortKey::Size:
        return QStringLiteral("size");
    case SortKey::Modified:
        return QStringLiteral("modified");
    case SortKey::Type:
        return QStringLiteral("type");
    case SortKey::Random:
        return QStringLiteral("random");
    case SortKey::Name:
        break;
    }
    return QStringLiteral("name");
}
namespace {

/// A stable pseudo-random ordering key.
///
/// SortKey::Random has to be a *function* of the name and a seed rather than a
/// call to a generator: QSortFilterProxyModel may compare the same pair more
/// than once, and an ordering that answers differently each time is not a
/// strict weak ordering. Feeding std::sort one of those is undefined behaviour,
/// not merely an odd-looking list.
quint32 randomKey(const QString &name, quint32 seed)
{
    quint32 hash = seed * 2654435761U;
    for (const QChar character : name) {
        hash ^= character.unicode();
        hash *= 16777619U;
    }
    return hash;
}

/// The extension used for type sorting. Not QMimeType — that would mean a MIME
/// lookup per comparison, and what a user means by "sort by type" is almost
/// always "group the .cpp files together".
///
/// A leading dot is not an extension: .gitignore sorts as a name, not as a file
/// of type "gitignore".
QString typeKey(const QString &name)
{
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1) {
        return {};
    }
    return name.mid(dot + 1).toLower();
}

} // namespace

void FilterSortProxy::refilter()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    endFilterChange();
#else
    invalidateFilter();
#endif
}

FilterSortProxy::FilterSortProxy(QObject *parent) : QSortFilterProxyModel(parent)
{
    // §4.4: natural, case-insensitive, locale-aware.
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);

    setDynamicSortFilter(true);
    sort(0);
}

void FilterSortProxy::setShowHidden(bool show)
{
    if (m_showHidden == show) {
        return;
    }
    m_showHidden = show;
    refilter();
}

bool FilterSortProxy::showHidden() const
{
    return m_showHidden;
}

void FilterSortProxy::setDirectoriesFirst(bool first)
{
    if (m_directoriesFirst == first) {
        return;
    }
    m_directoriesFirst = first;
    invalidate();
}

bool FilterSortProxy::directoriesFirst() const
{
    return m_directoriesFirst;
}

void FilterSortProxy::setSortKey(SortKey key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    invalidate();
}

SortKey FilterSortProxy::sortKey() const
{
    return m_sortKey;
}

void FilterSortProxy::setReverseSort(bool reverse)
{
    if (m_reverseSort == reverse) {
        return;
    }
    m_reverseSort = reverse;
    invalidate();
}

bool FilterSortProxy::reverseSort() const
{
    return m_reverseSort;
}

void FilterSortProxy::setFilterText(const QString &text)
{
    if (m_filterText == text) {
        return;
    }
    m_filterText = text;
    refilter();
}

QString FilterSortProxy::filterText() const
{
    return m_filterText;
}

void FilterSortProxy::reshuffle()
{
    m_randomSeed += 2654435761U;
    if (m_sortKey == SortKey::Random) {
        invalidate();
    }
}

bool FilterSortProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!index.isValid()) {
        return false;
    }

    const QString name = index.data(DirectoryModel::NameRole).toString();

    // 1. Hidden-file filter.
    if (!m_showHidden && name.startsWith(QLatin1Char('.'))) {
        return false;
    }

    // 2. Search filter (§7.8).
    if (!m_filterText.isEmpty() && !matchFor(name).matched) {
        return false;
    }

    return true;
}

QVariant FilterSortProxy::data(const QModelIndex &index, int role) const
{
    if (role != DirectoryModel::MatchSpansRole) {
        return QSortFilterProxyModel::data(index, role);
    }

    if (m_filterText.isEmpty()) {
        return {};
    }

    const QString name = QSortFilterProxyModel::data(index, DirectoryModel::NameRole).toString();
    return QVariant::fromValue(matchFor(name).spans);
}

FuzzyMatch FilterSortProxy::matchFor(const QString &name) const
{
    if (m_filterText.isEmpty()) {
        return {};
    }
    return m_fuzzy ? FuzzyMatcher::match(m_filterText, name)
                   : FuzzyMatcher::matchSubstring(m_filterText, name);
}

void FilterSortProxy::setFuzzyMatching(bool fuzzy)
{
    if (m_fuzzy == fuzzy) {
        return;
    }
    m_fuzzy = fuzzy;
    if (!m_filterText.isEmpty()) {
        refilter();
    }
}

bool FilterSortProxy::fuzzyMatching() const
{
    return m_fuzzy;
}

bool FilterSortProxy::isRankingByScore() const
{
    return m_fuzzy && !m_filterText.isEmpty();
}

bool FilterSortProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const QString leftName = left.data(DirectoryModel::NameRole).toString();
    const QString rightName = right.data(DirectoryModel::NameRole).toString();

    // §7.8: a fuzzy filter ranks by score. Best first, which is descending, and
    // ties fall through to the ordinary ordering below so the result is stable
    // rather than arbitrary.
    if (isRankingByScore()) {
        const int leftScore = matchFor(leftName).score;
        const int rightScore = matchFor(rightName).score;
        if (leftScore != rightScore) {
            return leftScore > rightScore;
        }
    }

    const bool leftIsDir = left.data(DirectoryModel::IsDirRole).toBool();
    const bool rightIsDir = right.data(DirectoryModel::IsDirRole).toBool();

    // 3. Directories first, and deliberately outside the reverse toggle:
    // reversing the sort should flip the order of the files, not move the
    // directories to the bottom. Nobody wants "reverse by name" to bury the
    // directories they were about to navigate into.
    if (m_directoriesFirst && leftIsDir != rightIsDir) {
        // Returns directly, so the reverse toggle applied at the end of this
        // function never sees it — which is the point. Reversing flips the
        // order within each group; it does not send the directories to the
        // bottom, because nobody reverses a sort in order to make the
        // directories they were about to open harder to reach.
        return leftIsDir;
    }

    bool result = false;
    switch (m_sortKey) {
    case SortKey::Name:
        result = naturalCompare(leftName, rightName, m_collator) < 0;
        break;

    case SortKey::Size: {
        const quint64 leftSize = left.data(DirectoryModel::SizeRole).toULongLong();
        const quint64 rightSize = right.data(DirectoryModel::SizeRole).toULongLong();
        if (leftSize != rightSize) {
            result = leftSize < rightSize;
        } else {
            // Equal sizes are extremely common — every empty file, every
            // directory. Falling back to the name keeps the order stable and
            // predictable instead of readdir-dependent.
            result = naturalCompare(leftName, rightName, m_collator) < 0;
        }
        break;
    }

    case SortKey::Modified: {
        const QDateTime leftTime = left.data(DirectoryModel::ModifiedRole).toDateTime();
        const QDateTime rightTime = right.data(DirectoryModel::ModifiedRole).toDateTime();
        if (leftTime != rightTime) {
            result = leftTime < rightTime;
        } else {
            result = naturalCompare(leftName, rightName, m_collator) < 0;
        }
        break;
    }

    case SortKey::Type: {
        const QString leftType = typeKey(leftName);
        const QString rightType = typeKey(rightName);

        if (leftType != rightType) {
            result = naturalCompare(leftType, rightType, m_collator) < 0;
        } else {
            result = naturalCompare(leftName, rightName, m_collator) < 0;
        }
        break;
    }

    case SortKey::Random:
        result = randomKey(leftName, m_randomSeed) < randomKey(rightName, m_randomSeed);
        break;
    }

    return m_reverseSort ? !result : result;
}

} // namespace pf
