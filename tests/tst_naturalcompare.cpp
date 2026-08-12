// Natural ordering (§4.4, §14).
//
// §14 asks for "natural sort ordering, including numbers, locale and case".
// This exists as a separate test from tst_sorting because the ordering is a
// pure function and deserves to be pinned down directly — and because the bug
// that prompted it was invisible through the proxy: QCollator's numeric mode is
// implemented by ICU, so under LC_ALL=C, or with a Qt built without ICU, it is
// accepted and then silently ignored. Every listing quietly reverts to byte
// order. The tests below therefore assert numeric ordering *without* relying on
// the collator to provide it.

#include "core/NaturalCompare.h"

#include <QCollator>
#include <QTest>

using namespace pf;

class TestNaturalCompare : public QObject
{
    Q_OBJECT

private:
    QCollator m_collator;

    /// Sorts with naturalCompare, so a whole ordering can be asserted at once.
    QStringList sorted(QStringList input) const
    {
        std::sort(input.begin(), input.end(), [this](const QString &a, const QString &b) {
            return naturalCompare(a, b, m_collator) < 0;
        });
        return input;
    }

private Q_SLOTS:
    void initTestCase();

    void digitsCompareByValue();
    void leadingZerosDoNotChangeValue();
    void equalValuesOrderByWidth();
    void veryLongDigitRunsDoNotOverflow();
    void multipleNumberGroups();
    void numbersAtTheStart();
    void caseIsFoldedButStillDeterministic();
    void prefixSortsBeforeLongerName();
    void identicalStringsCompareEqual();
    void emptyStrings();
    void isAStrictWeakOrdering();
    void realisticListing();
};

void TestNaturalCompare::initTestCase()
{
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
}

void TestNaturalCompare::digitsCompareByValue()
{
    // The canonical case, and the one a byte-wise comparison gets wrong.
    QVERIFY(naturalCompare(QStringLiteral("file2"), QStringLiteral("file10"), m_collator) < 0);
    QVERIFY(naturalCompare(QStringLiteral("file10"), QStringLiteral("file2"), m_collator) > 0);

    QCOMPARE(sorted({"file10", "file2", "file1", "file20", "file3"}),
             (QStringList{"file1", "file2", "file3", "file10", "file20"}));
}

void TestNaturalCompare::leadingZerosDoNotChangeValue()
{
    QCOMPARE(sorted({"img10.png", "img009.png", "img1.png", "img007.png"}),
             (QStringList{"img1.png", "img007.png", "img009.png", "img10.png"}));
}

void TestNaturalCompare::equalValuesOrderByWidth()
{
    // 1 and 001 are the same number, so something has to break the tie or the
    // order depends on which pair the sort happened to compare first.
    QVERIFY(naturalCompare(QStringLiteral("v1"), QStringLiteral("v001"), m_collator) < 0);
    QVERIFY(naturalCompare(QStringLiteral("v001"), QStringLiteral("v1"), m_collator) > 0);
}

void TestNaturalCompare::veryLongDigitRunsDoNotOverflow()
{
    // A checksum in a filename is not a number anyone wants truncated. This is
    // 40 digits — well past what any integer type holds — and the comparison
    // must still be by value.
    const QString small = QStringLiteral("x1111111111111111111111111111111111111111");
    const QString large = QStringLiteral("x1111111111111111111111111111111111111112");

    QVERIFY(naturalCompare(small, large, m_collator) < 0);
    QVERIFY(naturalCompare(large, small, m_collator) > 0);

    // Longer runs of significant digits are larger, whatever the digits are.
    QVERIFY(naturalCompare(QStringLiteral("x999999999999999999999999"),
                           QStringLiteral("x1000000000000000000000000"), m_collator) < 0);
}

void TestNaturalCompare::multipleNumberGroups()
{
    QCOMPARE(sorted({"v1.10.2", "v1.2.10", "v1.2.2", "v1.10.1"}),
             (QStringList{"v1.2.2", "v1.2.10", "v1.10.1", "v1.10.2"}));
}

void TestNaturalCompare::numbersAtTheStart()
{
    QCOMPARE(sorted({"10-late.log", "2-early.log", "1-first.log"}),
             (QStringList{"1-first.log", "2-early.log", "10-late.log"}));
}

void TestNaturalCompare::caseIsFoldedButStillDeterministic()
{
    // §4.4 wants case-insensitive ordering, so Apple sorts before banana rather
    // than every capitalised name sorting above every lowercase one.
    QCOMPARE(sorted({"banana", "Apple", "cherry", "Blueberry"}),
             (QStringList{"Apple", "banana", "Blueberry", "cherry"}));

    // But names differing only in case must still have a stable order, not
    // compare equal and land wherever the sort algorithm leaves them.
    QVERIFY(naturalCompare(QStringLiteral("README"), QStringLiteral("readme"), m_collator) != 0);
}

void TestNaturalCompare::prefixSortsBeforeLongerName()
{
    QVERIFY(naturalCompare(QStringLiteral("file"), QStringLiteral("file2"), m_collator) < 0);
    QVERIFY(naturalCompare(QStringLiteral("file2"), QStringLiteral("file"), m_collator) > 0);
}

void TestNaturalCompare::identicalStringsCompareEqual()
{
    QCOMPARE(naturalCompare(QStringLiteral("same.txt"), QStringLiteral("same.txt"), m_collator), 0);
}

void TestNaturalCompare::emptyStrings()
{
    QCOMPARE(naturalCompare({}, {}, m_collator), 0);
    QVERIFY(naturalCompare({}, QStringLiteral("a"), m_collator) < 0);
    QVERIFY(naturalCompare(QStringLiteral("a"), {}, m_collator) > 0);
}

void TestNaturalCompare::isAStrictWeakOrdering()
{
    // std::sort has undefined behaviour on a comparator that is not a strict
    // weak ordering, so this is a correctness requirement rather than a
    // tidiness one: irreflexive, antisymmetric, and transitive.
    const QStringList names{"file1", "file01", "file2", "file10", "File2", "",       "a1b2",
                            "a1b10", "10",     "2",     "z",      "A",     "v1.2.3", "v1.2.10"};

    for (const QString &a : names) {
        QCOMPARE(naturalCompare(a, a, m_collator), 0);

        for (const QString &b : names) {
            const int forward = naturalCompare(a, b, m_collator);
            const int backward = naturalCompare(b, a, m_collator);
            QVERIFY2((forward < 0) == (backward > 0),
                     qPrintable(QStringLiteral("asymmetry between '%1' and '%2'").arg(a, b)));

            for (const QString &c : names) {
                if (naturalCompare(a, b, m_collator) < 0 && naturalCompare(b, c, m_collator) < 0) {
                    QVERIFY2(naturalCompare(a, c, m_collator) < 0,
                             qPrintable(
                                 QStringLiteral("intransitive: '%1' < '%2' < '%3'").arg(a, b, c)));
                }
            }
        }
    }
}

void TestNaturalCompare::realisticListing()
{
    QCOMPARE(sorted({"IMG_20260811_120000.jpg", "IMG_20260811_09000.jpg", "notes.md", "Makefile",
                     "part10.tar.gz", "part2.tar.gz", ".gitignore"}),
             (QStringList{".gitignore", "IMG_20260811_09000.jpg", "IMG_20260811_120000.jpg",
                          "Makefile", "notes.md", "part2.tar.gz", "part10.tar.gz"}));
}

QTEST_APPLESS_MAIN(TestNaturalCompare)
#include "tst_naturalcompare.moc"
