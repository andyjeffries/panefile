// Cursor memory (§5.2).
//
// "When you navigate out of a directory and back in, the cursor must land on
// the directory you came from." The bound on the cache is part of the
// requirement, not an implementation detail: a long session walking a large
// tree would otherwise keep an entry per directory visited, forever.

#include "ui/CursorMemory.h"

#include <QTest>

using namespace pf::ui;

class TestCursorMemory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    void recallsWhatWasRemembered();
    void unknownDirectoryRecallsNothing();
    void rememberingAgainReplaces();
    void emptyArgumentsAreIgnored();
    void forgetRemoves();
    void evictsLeastRecentlyUsed();
    void recallCountsAsUse();
    void staysWithinCapacity();
};

void TestCursorMemory::init()
{
    CursorMemory::instance().clear();
}

void TestCursorMemory::recallsWhatWasRemembered()
{
    CursorMemory::instance().remember(QStringLiteral("/home/andy"), QStringLiteral("Developer"));

    QCOMPARE(CursorMemory::instance().recall(QStringLiteral("/home/andy")),
             QStringLiteral("Developer"));
}

void TestCursorMemory::unknownDirectoryRecallsNothing()
{
    // Empty means "no memory", which the panel turns into "put the cursor on
    // the first row" rather than "put it nowhere".
    QVERIFY(CursorMemory::instance().recall(QStringLiteral("/never/visited")).isEmpty());
}

void TestCursorMemory::rememberingAgainReplaces()
{
    auto &memory = CursorMemory::instance();
    memory.remember(QStringLiteral("/tmp"), QStringLiteral("first"));
    memory.remember(QStringLiteral("/tmp"), QStringLiteral("second"));

    QCOMPARE(memory.recall(QStringLiteral("/tmp")), QStringLiteral("second"));
    QCOMPARE(memory.size(), 1);
}

void TestCursorMemory::emptyArgumentsAreIgnored()
{
    auto &memory = CursorMemory::instance();
    memory.remember(QString(), QStringLiteral("x"));
    memory.remember(QStringLiteral("/tmp"), QString());

    QCOMPARE(memory.size(), 0);
}

void TestCursorMemory::forgetRemoves()
{
    auto &memory = CursorMemory::instance();
    memory.remember(QStringLiteral("/a"), QStringLiteral("x"));
    memory.remember(QStringLiteral("/b"), QStringLiteral("y"));

    memory.forget(QStringLiteral("/a"));

    QVERIFY(memory.recall(QStringLiteral("/a")).isEmpty());
    QCOMPARE(memory.recall(QStringLiteral("/b")), QStringLiteral("y"));
    QCOMPARE(memory.size(), 1);
}

void TestCursorMemory::evictsLeastRecentlyUsed()
{
    auto &memory = CursorMemory::instance();

    for (int i = 0; i < CursorMemory::kCapacity; ++i) {
        memory.remember(QStringLiteral("/dir%1").arg(i), QStringLiteral("entry%1").arg(i));
    }
    QCOMPARE(memory.size(), CursorMemory::kCapacity);

    // One more than capacity: the oldest goes.
    memory.remember(QStringLiteral("/overflow"), QStringLiteral("last"));

    QCOMPARE(memory.size(), CursorMemory::kCapacity);
    QVERIFY(memory.recall(QStringLiteral("/dir0")).isEmpty());
    QCOMPARE(memory.recall(QStringLiteral("/overflow")), QStringLiteral("last"));
}

void TestCursorMemory::recallCountsAsUse()
{
    auto &memory = CursorMemory::instance();

    for (int i = 0; i < CursorMemory::kCapacity; ++i) {
        memory.remember(QStringLiteral("/dir%1").arg(i), QStringLiteral("entry%1").arg(i));
    }

    // Touch the oldest entry. A directory the user keeps returning to should
    // not age out at the same rate as one they passed through once.
    QCOMPARE(memory.recall(QStringLiteral("/dir0")), QStringLiteral("entry0"));

    memory.remember(QStringLiteral("/overflow"), QStringLiteral("last"));

    QCOMPARE(memory.recall(QStringLiteral("/dir0")), QStringLiteral("entry0"));
    QVERIFY(memory.recall(QStringLiteral("/dir1")).isEmpty());
}

void TestCursorMemory::staysWithinCapacity()
{
    auto &memory = CursorMemory::instance();

    for (int i = 0; i < CursorMemory::kCapacity * 4; ++i) {
        memory.remember(QStringLiteral("/dir%1").arg(i), QStringLiteral("entry%1").arg(i));
    }

    QCOMPARE(memory.size(), CursorMemory::kCapacity);
}

QTEST_APPLESS_MAIN(TestCursorMemory)
#include "tst_cursormemory.moc"
