// Directory watching (§7.3, §14).
//
// §14 asks that "create/delete/rename events produce correct model deltas".
// Everything §7.3 specifies about *behaviour* — the 150 ms debounce, coalescing
// a create-then-delete into nothing, pairing the halves of a rename, the
// 200-event rescan threshold, walking up when the directory disappears — is in
// WatchCoalescer, which takes synthetic events and needs no filesystem. That is
// the point of the split: these run identically on Linux and macOS, and the
// per-platform backends are left thin enough to read.
//
// The backends themselves are exercised at the end against a real directory,
// which is as far as an automated test can honestly go: what inotify and
// FSEvents actually deliver is theirs to decide.

#include "fs/DirectoryWatcher.h"
#include "fs/WatchCoalescer.h"
#include "model/DirectoryModel.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>

using namespace pf::fs;
using pf::platform::WatchEvent;

class TestWatcher : public QObject
{
    Q_OBJECT

private:
    static WatchEvent created(const QString &name)
    {
        return {.kind = WatchEvent::Kind::Created, .name = name};
    }
    static WatchEvent deleted(const QString &name)
    {
        return {.kind = WatchEvent::Kind::Deleted, .name = name};
    }
    static WatchEvent modified(const QString &name)
    {
        return {.kind = WatchEvent::Kind::Modified, .name = name};
    }
    static WatchEvent movedFrom(const QString &name)
    {
        return {.kind = WatchEvent::Kind::MovedFrom, .name = name};
    }
    static WatchEvent movedTo(const QString &name)
    {
        return {.kind = WatchEvent::Kind::MovedTo, .name = name};
    }

private Q_SLOTS:
    // Coalescing
    void singleCreate();
    void singleDelete();
    void modificationIsReportedAsModified();
    void attributeChangeIsAModification();
    void createThenDeleteCancelsOut();
    void deleteThenCreateIsAModification();
    void createThenModifyIsStillJustACreate();
    void renameWithinTheDirectoryIsPaired();
    void moveInIsACreate();
    void moveOutIsADelete();
    void severalRenamesPairInOrder();
    void selfGoneDiscardsEverythingElse();
    void overflowRequestsARescan();
    void aLargeBurstRequestsARescan();
    void duplicateEventsForOneNameCollapse();

    // Debouncing
    void burstIsReportedOnceAfterTheDebounce();
    void theTimerRestartsOnEachEvent();
    void selfGoneIsReportedImmediately();
    void nothingIsEmittedWhenEverythingCancels();
    void resetDiscardsPendingEvents();

    // The watcher itself
    void watchersAreSharedPerPath();
    void watchersAreReleasedWhenDropped();
    void watchingAMissingDirectoryFailsQuietly();
    void realChangesArriveFromTheBackend();
    void aRescanDoesNotResetTheModel();

    // Optimistic removal
    void aLargeDeletionBurstNamesWhatWentAway();
    void expectRemovalHidesRowsImmediately();
    void finishRemovalRestoresSurvivors();
    void deletionEchoDoesNotRescanOrRepaint();
    void rescanLeavesUnchangedRowsAlone();

private:
    static void touch(const QString &path)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
};

void TestWatcher::singleCreate()
{
    const WatchDelta delta = coalesce({created(QStringLiteral("a.txt"))});

    QCOMPARE(delta.created, QStringList{"a.txt"});
    QVERIFY(delta.deleted.isEmpty());
    QVERIFY(!delta.needsFullRescan);
}

void TestWatcher::singleDelete()
{
    const WatchDelta delta = coalesce({deleted(QStringLiteral("a.txt"))});

    QCOMPARE(delta.deleted, QStringList{"a.txt"});
    QVERIFY(delta.created.isEmpty());
}

void TestWatcher::modificationIsReportedAsModified()
{
    const WatchDelta delta = coalesce({modified(QStringLiteral("a.txt"))});

    QCOMPARE(delta.modified, QStringList{"a.txt"});
}

void TestWatcher::attributeChangeIsAModification()
{
    // A chmod changes what the delegate paints — the executable colour, the
    // permission string in the footer — so the row has to be re-read.
    const WatchDelta delta = coalesce(
        {WatchEvent{.kind = WatchEvent::Kind::AttributesChanged, .name = QStringLiteral("a.sh")}});

    QCOMPARE(delta.modified, QStringList{"a.sh"});
}

void TestWatcher::createThenDeleteCancelsOut()
{
    // The case that makes coalescing worth having. Editors write a temporary
    // file and remove it; without this the panel flickers a row in and out for
    // every save.
    const WatchDelta delta =
        coalesce({created(QStringLiteral(".swp")), deleted(QStringLiteral(".swp"))});

    QVERIFY2(delta.isEmpty(), "a file that came and went produced a delta");
}

void TestWatcher::deleteThenCreateIsAModification()
{
    // A replacement — the atomic write pattern of writing a new file over an
    // old one. The row is already on screen, so it changes rather than
    // disappearing and coming back.
    const WatchDelta delta =
        coalesce({deleted(QStringLiteral("a.txt")), created(QStringLiteral("a.txt"))});

    QCOMPARE(delta.modified, QStringList{"a.txt"});
    QVERIFY(delta.created.isEmpty());
    QVERIFY(delta.deleted.isEmpty());
}

void TestWatcher::createThenModifyIsStillJustACreate()
{
    // The row has not been drawn yet, so reporting a modification as well would
    // make the model do the same work twice.
    const WatchDelta delta =
        coalesce({created(QStringLiteral("a.txt")), modified(QStringLiteral("a.txt")),
                  modified(QStringLiteral("a.txt"))});

    QCOMPARE(delta.created, QStringList{"a.txt"});
    QVERIFY(delta.modified.isEmpty());
}

void TestWatcher::renameWithinTheDirectoryIsPaired()
{
    // §7.3 wants targeted updates rather than a rescan, and a rename is where
    // that pays: one row changes its name in place.
    const WatchDelta delta =
        coalesce({movedFrom(QStringLiteral("old.txt")), movedTo(QStringLiteral("new.txt"))});

    QCOMPARE(delta.renamed.size(), 1);
    QCOMPARE(delta.renamed.first().first, QStringLiteral("old.txt"));
    QCOMPARE(delta.renamed.first().second, QStringLiteral("new.txt"));
    QVERIFY(delta.created.isEmpty());
    QVERIFY(delta.deleted.isEmpty());
}

void TestWatcher::moveInIsACreate()
{
    // A file moved in from another directory produces only the second half, and
    // that is exactly a creation as far as this listing is concerned.
    const WatchDelta delta = coalesce({movedTo(QStringLiteral("arrived.txt"))});

    QCOMPARE(delta.created, QStringList{"arrived.txt"});
    QVERIFY(delta.renamed.isEmpty());
}

void TestWatcher::moveOutIsADelete()
{
    const WatchDelta delta = coalesce({movedFrom(QStringLiteral("departed.txt"))});

    QCOMPARE(delta.deleted, QStringList{"departed.txt"});
    QVERIFY(delta.renamed.isEmpty());
}

void TestWatcher::severalRenamesPairInOrder()
{
    const WatchDelta delta =
        coalesce({movedFrom(QStringLiteral("a")), movedTo(QStringLiteral("b")),
                  movedFrom(QStringLiteral("c")), movedTo(QStringLiteral("d"))});

    QCOMPARE(delta.renamed.size(), 2);
    QCOMPARE(delta.renamed.at(0).first, QStringLiteral("a"));
    QCOMPARE(delta.renamed.at(0).second, QStringLiteral("b"));
    QCOMPARE(delta.renamed.at(1).first, QStringLiteral("c"));
    QCOMPARE(delta.renamed.at(1).second, QStringLiteral("d"));
}

void TestWatcher::selfGoneDiscardsEverythingElse()
{
    // §7.3: the panel is about to walk up to an ancestor, so what happened to
    // the entries of a directory that no longer exists is not worth reporting.
    const WatchDelta delta = coalesce({created(QStringLiteral("a.txt")),
                                       WatchEvent{.kind = WatchEvent::Kind::SelfGone, .name = {}},
                                       created(QStringLiteral("b.txt"))});

    QVERIFY(delta.selfGone);
    QVERIFY(delta.created.isEmpty());
}

void TestWatcher::overflowRequestsARescan()
{
    // The backend lost events, so the deltas cannot describe the directory.
    const WatchDelta delta = coalesce({created(QStringLiteral("a.txt")),
                                       WatchEvent{.kind = WatchEvent::Kind::Overflow, .name = {}}});

    QVERIFY(delta.needsFullRescan);
}

void TestWatcher::aLargeBurstRequestsARescan()
{
    // §7.3: "Fall back to a full rescan if more than 200 events arrive in one
    // debounce window."
    QList<WatchEvent> events;
    for (int i = 0; i <= WatchCoalescer::kRescanThreshold; ++i) {
        events.append(created(QStringLiteral("f%1").arg(i)));
    }

    const WatchDelta delta = coalesce(events);

    QVERIFY(delta.needsFullRescan);
    QVERIFY2(delta.created.isEmpty(), "a rescan delta should not also list every event");

    // One under the threshold is still applied as targeted updates.
    events.removeLast();
    const WatchDelta smaller = coalesce(events);
    QVERIFY(!smaller.needsFullRescan);
    QCOMPARE(smaller.created.size(), WatchCoalescer::kRescanThreshold);
}

void TestWatcher::duplicateEventsForOneNameCollapse()
{
    // Writing a file produces a stream of modify events; the model needs to
    // re-read the row once, not forty times.
    QList<WatchEvent> events;
    for (int i = 0; i < 40; ++i) {
        events.append(modified(QStringLiteral("big.bin")));
    }

    const WatchDelta delta = coalesce(events);
    QCOMPARE(delta.modified, QStringList{"big.bin"});
}

void TestWatcher::burstIsReportedOnceAfterTheDebounce()
{
    WatchCoalescer coalescer;
    coalescer.setDebounceInterval(30);
    QSignalSpy spy(&coalescer, &WatchCoalescer::delta);

    for (int i = 0; i < 10; ++i) {
        coalescer.add(created(QStringLiteral("f%1").arg(i)));
    }

    QCOMPARE(spy.count(), 0); // nothing yet — still within the window
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);

    const auto delta = spy.first().first().value<WatchDelta>();
    QCOMPARE(delta.created.size(), 10);
}

void TestWatcher::theTimerRestartsOnEachEvent()
{
    // An extraction produces events continuously for seconds. Reporting every
    // 150 ms through it would make the panel flicker through intermediate
    // states; the window is meant to close once the burst *stops*.
    //
    // A window far longer than the test, so a slow machine cannot close it
    // between events: each event is added once the window has visibly run
    // down, and must wind it back up to the full length.
    constexpr int window = 60000;
    WatchCoalescer coalescer;
    coalescer.setDebounceInterval(window);
    QSignalSpy spy(&coalescer, &WatchCoalescer::delta);
    auto *timer = coalescer.findChild<QTimer *>(QStringLiteral("watchDebounce"));
    QVERIFY(timer != nullptr);
    // A coarse timer rounds its deadline, so a restart inside the same rounding
    // step would read back the same remaining time as no restart at all.
    timer->setTimerType(Qt::PreciseTimer);

    coalescer.add(created(QStringLiteral("f0")));
    for (int i = 1; i < 5; ++i) {
        QTRY_VERIFY_WITH_TIMEOUT(timer->remainingTime() < window - 100, 2000);
        const int before = timer->remainingTime();
        coalescer.add(created(QStringLiteral("f%1").arg(i)));
        QVERIFY2(timer->remainingTime() > before,
                 qPrintable(QStringLiteral("%1 ms left before, %2 after")
                                .arg(before)
                                .arg(timer->remainingTime())));
    }
    QCOMPARE(spy.count(), 0);

    // The burst is over; close the window now rather than in a minute.
    coalescer.setDebounceInterval(0);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QCOMPARE(spy.first().first().value<WatchDelta>().created.size(), 5);
}

void TestWatcher::selfGoneIsReportedImmediately()
{
    // The panel is showing a listing of something that no longer exists; making
    // the user look at it for another 150 ms serves nobody.
    WatchCoalescer coalescer;
    coalescer.setDebounceInterval(5000);
    QSignalSpy spy(&coalescer, &WatchCoalescer::delta);

    coalescer.add(WatchEvent{.kind = WatchEvent::Kind::SelfGone, .name = {}});

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.first().first().value<WatchDelta>().selfGone);
}

void TestWatcher::nothingIsEmittedWhenEverythingCancels()
{
    WatchCoalescer coalescer;
    coalescer.setDebounceInterval(20);
    QSignalSpy spy(&coalescer, &WatchCoalescer::delta);

    coalescer.add(created(QStringLiteral(".tmp")));
    coalescer.add(deleted(QStringLiteral(".tmp")));

    // The window closes and finds nothing left to say.
    const auto *window = coalescer.findChild<QTimer *>(QStringLiteral("watchDebounce"));
    QVERIFY(window != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!window->isActive(), 2000);
    QCOMPARE(spy.count(), 0);
}

void TestWatcher::resetDiscardsPendingEvents()
{
    WatchCoalescer coalescer;
    coalescer.setDebounceInterval(30);
    QSignalSpy spy(&coalescer, &WatchCoalescer::delta);

    coalescer.add(created(QStringLiteral("a.txt")));
    QCOMPARE(coalescer.pendingCount(), 1);

    // Navigating away: the pending events describe the directory just left.
    coalescer.reset();
    QCOMPARE(coalescer.pendingCount(), 0);

    // Nothing left to close the window on: the debounce is not armed.
    const auto *window = coalescer.findChild<QTimer *>(QStringLiteral("watchDebounce"));
    QVERIFY(window != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(!window->isActive(), 2000);
    QCOMPARE(spy.count(), 0);
}

void TestWatcher::watchersAreSharedPerPath()
{
    // §7.3: "refcounted — two panels on the same path share one watch". inotify
    // watches are a limited per-user kernel resource, and ten panels on one
    // directory taking ten of them would waste nine.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto first = DirectoryWatcher::acquire(directory.path());
    const auto second = DirectoryWatcher::acquire(directory.path());

    QCOMPARE(first.get(), second.get());
    QCOMPARE(first->path(), QDir::cleanPath(directory.path()));
}

void TestWatcher::watchersAreReleasedWhenDropped()
{
    // The registry holds weak references, so it must not be what keeps a
    // watcher alive — otherwise every directory ever visited would hold a watch
    // for the lifetime of the process.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const int before = DirectoryWatcher::watchedPathCount();
    {
        const auto watcher = DirectoryWatcher::acquire(directory.path());
        QCOMPARE(DirectoryWatcher::watchedPathCount(), before + 1);
    }
    QCOMPARE(DirectoryWatcher::watchedPathCount(), before);
}

void TestWatcher::watchingAMissingDirectoryFailsQuietly()
{
    // A panel that is not watched still lists correctly; it simply will not
    // notice changes made elsewhere. That is a degradation, not a failure, and
    // must not throw or crash.
    const auto watcher =
        DirectoryWatcher::acquire(QStringLiteral("/nonexistent/directory/somewhere"));

    QVERIFY(watcher != nullptr);
    QVERIFY(!watcher->isActive());
}

void TestWatcher::realChangesArriveFromTheBackend()
{
    // The one test that exercises the actual platform backend. It asserts only
    // that *something* arrives describing the change — precisely which events
    // inotify and FSEvents emit is theirs to decide, and pinning that down
    // would be testing the kernel rather than this code.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto watcher = DirectoryWatcher::acquire(directory.path());
    if (!watcher->isActive()) {
        QSKIP("no watch backend available here");
    }
    watcher->setDebounceInterval(50);

    QSignalSpy spy(watcher.get(), &DirectoryWatcher::changed);

    QFile file(directory.path() + QStringLiteral("/created.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("hello");
    file.close();

    QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 5000);

    bool sawIt = false;
    for (const QList<QVariant> &emission : spy) {
        const auto delta = emission.first().value<WatchDelta>();
        if (delta.created.contains(QStringLiteral("created.txt")) ||
            delta.modified.contains(QStringLiteral("created.txt")) || delta.needsFullRescan) {
            sawIt = true;
        }
    }
    QVERIFY2(sawIt, "the backend reported a change that did not mention the new file");
}

void TestWatcher::aRescanDoesNotResetTheModel()
{
    // A burst past the threshold falls back to a rescan. The view must keep its
    // scroll position through it, which a model reset would throw away.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QFile(directory.path() + QStringLiteral("/old.txt")).open(QIODevice::WriteOnly));

    pf::DirectoryModel model;
    model.setWatchingEnabled(true);
    model.setPath(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.isScanning(), 5000);
    if (!pf::fs::DirectoryWatcher::acquire(directory.path())->isActive()) {
        QSKIP("no watch backend available here");
    }
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy resets(&model, &QAbstractItemModel::modelAboutToBeReset);

    QFile::remove(directory.path() + QStringLiteral("/old.txt"));
    for (int i = 0; i <= WatchCoalescer::kRescanThreshold + 10; ++i) {
        QFile file(directory.path() + QStringLiteral("/f%1.txt").arg(i));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    const int expected = WatchCoalescer::kRescanThreshold + 11;
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), expected, 5000);
    QCOMPARE(model.indexOfName(QStringLiteral("old.txt")), -1);
    QCOMPARE(resets.count(), 0);
}

void TestWatcher::aLargeDeletionBurstNamesWhatWentAway()
{
    QList<WatchEvent> events;
    for (int i = 0; i <= WatchCoalescer::kRescanThreshold; ++i) {
        events.append(i % 2 == 0 ? deleted(QStringLiteral("f%1").arg(i))
                                 : WatchEvent{.kind = WatchEvent::Kind::MovedFrom,
                                              .name = QStringLiteral("f%1").arg(i)});
    }
    // A repeat of the first name is listed once.
    events.append(deleted(QStringLiteral("f0")));

    const WatchDelta delta = coalesce(events);

    QVERIFY(delta.needsFullRescan);
    QCOMPARE(delta.deleted.size(), WatchCoalescer::kRescanThreshold + 1);
    QCOMPARE(delta.deleted.first(), QStringLiteral("f0"));
    QVERIFY(delta.created.isEmpty());

    // A single creation in the mix and the names are withheld again.
    events.append(created(QStringLiteral("new")));
    QVERIFY(coalesce(events).deleted.isEmpty());
}

void TestWatcher::expectRemovalHidesRowsImmediately()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const char *name : {"a", "b", "c", "keep", "d", "e"}) {
        touch(directory.path() + QLatin1Char('/') + QLatin1String(name));
    }

    pf::DirectoryModel model;
    model.setPath(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.isScanning(), 5000);
    QCOMPARE(model.rowCount(), 6);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.expectRemoval({"a", "b", "c", "d", "e"});

    // Synchronous, and the rows went in runs rather than one signal each: the
    // scan order is not guaranteed, so only bound the count.
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.entryAt(0)->name, QStringLiteral("keep"));
    QVERIFY(removed.count() <= 5);
    QVERIFY(removed.count() >= 1);
}

void TestWatcher::finishRemovalRestoresSurvivors()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    touch(directory.path() + QStringLiteral("/gone.txt"));
    touch(directory.path() + QStringLiteral("/stays.txt"));

    pf::DirectoryModel model;
    model.setPath(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.isScanning(), 5000);

    model.expectRemoval({"gone.txt", "stays.txt"});
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(QFile::remove(directory.path() + QStringLiteral("/gone.txt")));
    model.finishRemoval({"gone.txt", "stays.txt"});

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.entryAt(0)->name, QStringLiteral("stays.txt"));
}

void TestWatcher::deletionEchoDoesNotRescanOrRepaint()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const int count = WatchCoalescer::kRescanThreshold + 20;
    QStringList names;
    for (int i = 0; i < count; ++i) {
        names << QStringLiteral("f%1.txt").arg(i);
        touch(directory.path() + QLatin1Char('/') + names.last());
    }
    touch(directory.path() + QStringLiteral("/keep.txt"));

    pf::DirectoryModel model;
    model.setWatchingEnabled(true);
    model.setPath(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.isScanning(), 5000);
    if (!pf::fs::DirectoryWatcher::acquire(directory.path())->isActive()) {
        QSKIP("no watch backend available here");
    }
    QCOMPARE(model.rowCount(), count + 1);

    model.expectRemoval(names);
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy started(&model, &pf::DirectoryModel::scanStarted);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy resets(&model, &QAbstractItemModel::modelAboutToBeReset);
    bool rescanned = false;
    connect(&model, &pf::DirectoryModel::scanProgress, this,
            [&rescanned](int) { rescanned = true; });

    // The model's own watcher — they are shared per path — connected after the
    // model, so by the time this sees a delta the model has already acted on it.
    QSignalSpy echoes(pf::fs::DirectoryWatcher::acquire(directory.path()).get(),
                      &pf::fs::DirectoryWatcher::changed);
    const auto echoed = [&echoes, &names] {
        QSet<QString> deleted;
        for (const QList<QVariant> &arguments : std::as_const(echoes)) {
            for (const QString &name : arguments.first().value<WatchDelta>().deleted) {
                deleted.insert(name);
            }
        }
        return std::ranges::all_of(
            names, [&deleted](const QString &name) { return deleted.contains(name); });
    };

    for (const QString &name : names) {
        QVERIFY(QFile::remove(directory.path() + QLatin1Char('/') + name));
    }
    model.finishRemoval(names);

    // Every deletion has come back as an echo and been handled. A rescan it
    // started would show as one in progress, or as the signals below.
    QTRY_VERIFY_WITH_TIMEOUT(echoed(), 5000);

    QVERIFY(!model.isRescanning());
    QVERIFY(!rescanned);
    QCOMPARE(started.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(model.rowCount(), 1);
}

void TestWatcher::rescanLeavesUnchangedRowsAlone()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (int i = 0; i < 5; ++i) {
        touch(directory.path() + QStringLiteral("/f%1").arg(i));
    }

    pf::DirectoryModel model;
    model.setWatchingEnabled(true);
    model.setPath(directory.path());
    QTRY_VERIFY_WITH_TIMEOUT(!model.isScanning(), 5000);
    if (!pf::fs::DirectoryWatcher::acquire(directory.path())->isActive()) {
        QSKIP("no watch backend available here");
    }

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy finished(&model, &pf::DirectoryModel::scanFinished);

    // An overflow-style delta: nothing actually changed on disk.
    pf::fs::WatchDelta overflow;
    overflow.needsFullRescan = true;
    Q_EMIT pf::fs::DirectoryWatcher::acquire(directory.path())->changed(overflow);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);

    QCOMPARE(changed.count(), 0);
    QCOMPARE(model.rowCount(), 5);
}

QTEST_MAIN(TestWatcher)
#include "tst_watcher.moc"
