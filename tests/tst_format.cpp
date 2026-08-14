// Size, time and permission formatting (§14).
//
// These strings are read constantly and glanced at rather than studied, so the
// tests are as much about what is *not* shown — a decimal point on a byte
// count, a year on today's files — as about correctness.

#include "core/Format.h"

#include <QDateTime>
#include <QTest>

#include <sys/stat.h>

using namespace pf;

class TestFormat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sizes_data();
    void sizes();
    void sizeUnitsAreBinary();
    void sizePrecisionDropsWithMagnitude();

    void listTimeShowsTimeForToday();
    void listTimeShowsDayAndMonthWithinTheYear();
    void listTimeShowsYearForOlder();
    void invalidTimeIsEmpty();
    void fullTimeIsSortableAndUnambiguous();

    void permissions_data();
    void permissions();
    void permissionsShowSetuidAndSticky();
};

void TestFormat::sizes_data()
{
    QTest::addColumn<quint64>("bytes");
    QTest::addColumn<QString>("expected");

    QTest::newRow("zero") << quint64(0) << "0 B";
    QTest::newRow("one") << quint64(1) << "1 B";
    // Exact up to the unit boundary, with no decimal point: a byte count is a
    // whole number of bytes and "0.5 KiB" tells the reader less than "512 B".
    QTest::newRow("512") << quint64(512) << "512 B";
    QTest::newRow("1023") << quint64(1023) << "1023 B";
    QTest::newRow("1 KiB") << quint64(1024) << "1.0 KiB";
    QTest::newRow("4.2 KiB") << quint64(4300) << "4.2 KiB";
    QTest::newRow("1 MiB") << quint64(1024ULL * 1024) << "1.0 MiB";
    QTest::newRow("1 GiB") << quint64(1024ULL * 1024 * 1024) << "1.0 GiB";
    QTest::newRow("1 TiB") << quint64(1024ULL * 1024 * 1024 * 1024) << "1.0 TiB";
    QTest::newRow("1 PiB") << quint64(1024ULL * 1024 * 1024 * 1024 * 1024) << "1.0 PiB";
}

void TestFormat::sizes()
{
    QFETCH(quint64, bytes);
    QFETCH(QString, expected);

    QCOMPARE(formatSize(bytes), expected);
}

void TestFormat::sizeUnitsAreBinary()
{
    // A file manager reports what the filesystem reports, and every tool a user
    // cross-checks against — ls -lh, du -h, stat — uses binary units. Showing
    // 1.0 KiB for 1000 bytes would make Panefile the odd one out.
    QCOMPARE(formatSize(1000), QStringLiteral("1000 B"));
    QCOMPARE(formatSize(1024), QStringLiteral("1.0 KiB"));
}

void TestFormat::sizePrecisionDropsWithMagnitude()
{
    // One decimal below ten, none above: the extra digit stops carrying
    // information as the number grows, and dropping it keeps the column narrow.
    QCOMPARE(formatSize(9ULL * 1024 * 1024 + 400 * 1024), QStringLiteral("9.4 MiB"));
    QCOMPARE(formatSize(94ULL * 1024 * 1024), QStringLiteral("94 MiB"));
}

void TestFormat::listTimeShowsTimeForToday()
{
    // Built from today's date rather than by subtracting an hour from now: in
    // the hour after midnight "an hour ago" is yesterday, and the test would
    // fail once a day for reasons that have nothing to do with the code.
    const QDateTime today(QDate::currentDate(), QTime(12, 0));
    const QString text = formatListTime(today);

    QVERIFY2(text.contains(QLatin1Char(':')), qPrintable(text));
    QCOMPARE(text.size(), 5); // HH:mm
}

void TestFormat::listTimeShowsDayAndMonthWithinTheYear()
{
    // Six months ago, but clamped so the test does not cross a year boundary
    // and silently start asserting the wrong branch every January.
    const QDate today = QDate::currentDate();
    QDate target(today.year(), 1, 15);
    if (target == today) {
        target = QDate(today.year(), 2, 15);
    }
    if (target.year() != today.year()) {
        QSKIP("no other date available within the current year");
    }

    const QString text = formatListTime(QDateTime(target, QTime(9, 0)));

    QVERIFY2(!text.contains(QLatin1Char(':')), qPrintable(text));
    QVERIFY2(!text.contains(QString::number(today.year())), qPrintable(text));
}

void TestFormat::listTimeShowsYearForOlder()
{
    const QDateTime old(QDate(2019, 3, 4), QTime(11, 30));
    const QString text = formatListTime(old);

    // "4 Mar 19", not "Mar 2019": one shape for the whole column, and the day
    // survives — a four-digit year cost the day, so two files from different
    // Novembers used to render identically.
    QVERIFY2(text.contains(QStringLiteral("19")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("Mar")), qPrintable(text));
    QVERIFY2(text.startsWith(QStringLiteral("4 ")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("2019")), qPrintable(text));
}

void TestFormat::invalidTimeIsEmpty()
{
    // An entry that could not be stat'ed has no timestamp. Rendering the epoch
    // would be worse than rendering nothing.
    QVERIFY(formatListTime({}).isEmpty());
    QVERIFY(formatFullTime({}).isEmpty());
}

void TestFormat::fullTimeIsSortableAndUnambiguous()
{
    const QDateTime when(QDate(2026, 8, 11), QTime(14, 2));
    QCOMPARE(formatFullTime(when), QStringLiteral("2026-08-11 14:02"));
}

void TestFormat::permissions_data()
{
    QTest::addColumn<uint>("mode");
    QTest::addColumn<QString>("expected");

    QTest::newRow("regular 644") << uint(S_IFREG | 0644) << "-rw-r--r--";
    QTest::newRow("regular 755") << uint(S_IFREG | 0755) << "-rwxr-xr-x";
    QTest::newRow("directory 755") << uint(S_IFDIR | 0755) << "drwxr-xr-x";
    QTest::newRow("symlink 777") << uint(S_IFLNK | 0777) << "lrwxrwxrwx";
    QTest::newRow("fifo") << uint(S_IFIFO | 0644) << "prw-r--r--";
    QTest::newRow("socket") << uint(S_IFSOCK | 0644) << "srw-r--r--";
    QTest::newRow("none") << uint(S_IFREG | 0) << "----------";
}

void TestFormat::permissions()
{
    QFETCH(uint, mode);
    QFETCH(QString, expected);

    QCOMPARE(formatPermissions(static_cast<mode_t>(mode)), expected);
}

void TestFormat::permissionsShowSetuidAndSticky()
{
    // The lowercase form means the execute bit is also set, the uppercase form
    // means it is not — the distinction ls makes, and the one that tells you
    // whether a setuid bit is actually doing anything.
    QCOMPARE(formatPermissions(S_IFREG | S_ISUID | 0755), QStringLiteral("-rwsr-xr-x"));
    QCOMPARE(formatPermissions(S_IFREG | S_ISUID | 0644), QStringLiteral("-rwSr--r--"));
    QCOMPARE(formatPermissions(S_IFDIR | S_ISVTX | 0777), QStringLiteral("drwxrwxrwt"));
    QCOMPARE(formatPermissions(S_IFDIR | S_ISGID | 0755), QStringLiteral("drwxr-sr-x"));
}

QTEST_APPLESS_MAIN(TestFormat)
#include "tst_format.moc"
