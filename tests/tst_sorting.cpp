// Filtering and sorting (§4.4, §14).
//
// §14 asks for "natural sort ordering, including numbers, locale and case".
// Natural ordering is the one users notice immediately when it is wrong: a
// directory of log files listing file10 before file2 looks broken even though
// it is what a byte-wise comparison gives you.

#include "model/DirectoryModel.h"
#include "model/FileEntry.h"
#include "model/FilterSortProxy.h"

#include <QAbstractListModel>
#include <QTest>

using namespace pf;

namespace {

/// A model of fixed entries, so the sort can be tested without a filesystem.
class StubModel : public QAbstractListModel
{
public:
    void setEntries(QList<FileEntry> entries)
    {
        beginResetModel();
        m_entries = std::move(entries);
        endResetModel();
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (index.row() < 0 || index.row() >= m_entries.size()) {
            return {};
        }
        const FileEntry &entry = m_entries.at(index.row());
        switch (role) {
        case Qt::DisplayRole:
        case DirectoryModel::NameRole:
            return entry.name;
        case DirectoryModel::EntryRole:
            return QVariant::fromValue(entry);
        case DirectoryModel::SizeRole:
            return entry.size;
        case DirectoryModel::ModifiedRole:
            return entry.modified;
        case DirectoryModel::IsDirRole:
            return entry.isDir;
        default:
            return {};
        }
    }

private:
    QList<FileEntry> m_entries;
};

FileEntry makeFile(const QString &name, quint64 size = 0, const QDateTime &modified = {})
{
    FileEntry entry;
    entry.name = name;
    entry.size = size;
    entry.modified = modified;
    entry.isHidden = name.startsWith(QLatin1Char('.'));
    return entry;
}

FileEntry makeDir(const QString &name)
{
    FileEntry entry = makeFile(name);
    entry.isDir = true;
    return entry;
}

} // namespace

class TestSorting : public QObject
{
    Q_OBJECT

private:
    StubModel m_model;
    FilterSortProxy m_proxy;

    QStringList visibleNames() const
    {
        QStringList names;
        for (int row = 0; row < m_proxy.rowCount(); ++row) {
            names << m_proxy.index(row, 0).data(DirectoryModel::NameRole).toString();
        }
        return names;
    }

private Q_SLOTS:
    void initTestCase();
    void init();

    void namesSortNaturally();
    void namesSortCaseInsensitively();
    void numbersWithLeadingZerosSortNumerically();
    void directoriesComeFirst();
    void directoriesStayFirstWhenReversed();
    void directoriesFirstCanBeDisabled();
    void hiddenFilesAreFilteredByDefault();
    void showHiddenRevealsThem();
    void filterTextIsCaseInsensitiveSubstring();
    void filterAndHiddenCombine();
    void sizeSortFallsBackToName();
    void modifiedSortFallsBackToName();
    void typeSortGroupsExtensions();
    void dotfilesAreNotTreatedAsExtensions();
    void randomSortIsAStableOrdering();
};

void TestSorting::initTestCase()
{
    m_proxy.setSourceModel(&m_model);
}

void TestSorting::init()
{
    m_proxy.setShowHidden(false);
    m_proxy.setDirectoriesFirst(true);
    m_proxy.setReverseSort(false);
    m_proxy.setSortKey(SortKey::Name);
    m_proxy.setFilterText({});
}

void TestSorting::namesSortNaturally()
{
    m_model.setEntries({makeFile("file10"), makeFile("file2"), makeFile("file1"),
                        makeFile("file20"), makeFile("file3")});

    QCOMPARE(visibleNames(), (QStringList{"file1", "file2", "file3", "file10", "file20"}));
}

void TestSorting::namesSortCaseInsensitively()
{
    // §4.4 requires case-insensitive collation. Byte order would put every
    // capitalised name above every lowercase one, so a directory of Makefile,
    // README and src would list the source directory last.
    m_model.setEntries(
        {makeFile("banana"), makeFile("Apple"), makeFile("cherry"), makeFile("Blueberry")});

    QCOMPARE(visibleNames(), (QStringList{"Apple", "banana", "Blueberry", "cherry"}));
}

void TestSorting::numbersWithLeadingZerosSortNumerically()
{
    m_model.setEntries({makeFile("img009.png"), makeFile("img10.png"), makeFile("img1.png"),
                        makeFile("img007.png")});

    QCOMPARE(visibleNames(), (QStringList{"img1.png", "img007.png", "img009.png", "img10.png"}));
}

void TestSorting::directoriesComeFirst()
{
    m_model.setEntries({makeFile("aaa.txt"), makeDir("zzz"), makeFile("mmm.txt"), makeDir("bbb")});

    QCOMPARE(visibleNames(), (QStringList{"bbb", "zzz", "aaa.txt", "mmm.txt"}));
}

void TestSorting::directoriesStayFirstWhenReversed()
{
    // Reversing should flip the order *within* each group, not bury the
    // directories at the bottom — nobody reverses a sort in order to make the
    // directories harder to reach.
    m_model.setEntries({makeFile("aaa.txt"), makeDir("zzz"), makeFile("mmm.txt"), makeDir("bbb")});
    m_proxy.setReverseSort(true);

    QCOMPARE(visibleNames(), (QStringList{"zzz", "bbb", "mmm.txt", "aaa.txt"}));
}

void TestSorting::directoriesFirstCanBeDisabled()
{
    m_model.setEntries({makeFile("aaa.txt"), makeDir("zzz"), makeFile("mmm.txt"), makeDir("bbb")});
    m_proxy.setDirectoriesFirst(false);

    QCOMPARE(visibleNames(), (QStringList{"aaa.txt", "bbb", "mmm.txt", "zzz"}));
}

void TestSorting::hiddenFilesAreFilteredByDefault()
{
    m_model.setEntries({makeFile(".gitignore"), makeFile("README.md"), makeFile(".env")});

    QCOMPARE(visibleNames(), QStringList{"README.md"});
}

void TestSorting::showHiddenRevealsThem()
{
    m_model.setEntries({makeFile(".gitignore"), makeFile("README.md"), makeFile(".env")});
    m_proxy.setShowHidden(true);

    QCOMPARE(visibleNames(), (QStringList{".env", ".gitignore", "README.md"}));
}

void TestSorting::filterTextIsCaseInsensitiveSubstring()
{
    m_model.setEntries(
        {makeFile("Makefile"), makeFile("main.cpp"), makeFile("README.md"), makeFile("domain.h")});
    m_proxy.setFilterText(QStringLiteral("MA"));

    QCOMPARE(visibleNames(), (QStringList{"domain.h", "main.cpp", "Makefile"}));
}

void TestSorting::filterAndHiddenCombine()
{
    // §4.4 applies the hidden filter first and the search filter second; a
    // dotfile matching the search must still be hidden.
    m_model.setEntries({makeFile(".config"), makeFile("config.toml"), makeFile("other.txt")});
    m_proxy.setFilterText(QStringLiteral("config"));

    QCOMPARE(visibleNames(), QStringList{"config.toml"});

    m_proxy.setShowHidden(true);
    QCOMPARE(visibleNames(), (QStringList{".config", "config.toml"}));
}

void TestSorting::sizeSortFallsBackToName()
{
    // Equal sizes are the common case, not the edge case: every empty file has
    // one. Without the fallback the order would be whatever readdir returned.
    m_model.setEntries({makeFile("c.txt", 100), makeFile("a.txt", 100), makeFile("b.txt", 50)});
    m_proxy.setSortKey(SortKey::Size);

    QCOMPARE(visibleNames(), (QStringList{"b.txt", "a.txt", "c.txt"}));
}

void TestSorting::modifiedSortFallsBackToName()
{
    const QDateTime older(QDate(2020, 1, 1), QTime(0, 0));
    const QDateTime newer(QDate(2026, 1, 1), QTime(0, 0));

    m_model.setEntries(
        {makeFile("c.txt", 0, newer), makeFile("a.txt", 0, newer), makeFile("b.txt", 0, older)});
    m_proxy.setSortKey(SortKey::Modified);

    QCOMPARE(visibleNames(), (QStringList{"b.txt", "a.txt", "c.txt"}));
}

void TestSorting::typeSortGroupsExtensions()
{
    m_model.setEntries(
        {makeFile("b.txt"), makeFile("a.cpp"), makeFile("c.txt"), makeFile("d.cpp")});
    m_proxy.setSortKey(SortKey::Type);

    QCOMPARE(visibleNames(), (QStringList{"a.cpp", "d.cpp", "b.txt", "c.txt"}));
}

void TestSorting::dotfilesAreNotTreatedAsExtensions()
{
    // .gitignore is a name, not a file of type "gitignore".
    m_model.setEntries({makeFile(".gitignore"), makeFile("a.txt"), makeFile(".env")});
    m_proxy.setShowHidden(true);
    m_proxy.setSortKey(SortKey::Type);

    // Both dotfiles have an empty type key, so they group together ahead of
    // .txt and order by name within the group.
    QCOMPARE(visibleNames(), (QStringList{".env", ".gitignore", "a.txt"}));
}

void TestSorting::randomSortIsAStableOrdering()
{
    // A comparator that answers differently for the same pair is not a strict
    // weak ordering, and feeding one to std::sort is undefined behaviour rather
    // than merely an odd-looking list. Asking twice must give the same answer.
    m_model.setEntries({makeFile("a"), makeFile("b"), makeFile("c"), makeFile("d"), makeFile("e"),
                        makeFile("f"), makeFile("g"), makeFile("h")});
    m_proxy.setSortKey(SortKey::Random);

    const QStringList first = visibleNames();
    const QStringList second = visibleNames();
    QCOMPARE(first, second);

    QStringList sortedFirst = first;
    sortedFirst.sort();
    QCOMPARE(sortedFirst, (QStringList{"a", "b", "c", "d", "e", "f", "g", "h"}));

    // Reshuffling changes the order but keeps it self-consistent.
    m_proxy.reshuffle();
    const QStringList afterReshuffle = visibleNames();
    QCOMPARE(afterReshuffle, visibleNames());
}

QTEST_MAIN(TestSorting)
#include "tst_sorting.moc"
