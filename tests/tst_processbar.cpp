#include "fs/JobEngine.h"
#include "fs/Trash.h"
#include "fs/jobs/DeleteJob.h"
#include "ui/ProcessBar.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

using namespace pf;

namespace {

/// A job that is still running when the appear delay elapses: it holds until
/// the test opens its gate, which it does once the bar has appeared, rather
/// than sleeping for some multiple of the delay and hoping.
class SlowJob : public fs::Job
{
    Q_OBJECT

public:
    explicit SlowJob(std::shared_ptr<QSemaphore> gate) : m_gate(std::move(gate)) {}

    QString description() const override { return QStringLiteral("slow"); }

protected:
    bool enumerate() override
    {
        m_filesTotal = 1;
        m_bytesTotal = 100;
        return true;
    }
    void execute() override
    {
        // Bounded, so a bar that never appears fails the test instead of
        // leaving the job running for ever.
        m_gate->tryAcquire(1, 5000);
        m_bytesDone = 100;
        m_filesDone = 1;
    }

private:
    std::shared_ptr<QSemaphore> m_gate;
};

/// Whether the bar could still appear: the delay that leads to it is armed.
bool appearancePending(const ui::ProcessBar &bar)
{
    const auto *timer = bar.findChild<QTimer *>(QStringLiteral("processAppearTimer"));
    return timer == nullptr || timer->isActive();
}

} // namespace

/// When the progress bar shows itself, and how long it stays.
class TestProcessBar : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Work that is over before the delay elapses never puts the bar on screen.
    ///
    /// Copying three small files finished in a few milliseconds and the bar
    /// appeared anyway — a flash of progress for something that had already
    /// stopped being true, which draws the eye for no reason.
    void shortJobsNeverShowTheBar()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QStringList paths;
        for (int n = 0; n < 3; ++n) {
            const QString path = dir.filePath(QStringLiteral("small%1.txt").arg(n));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
            file.close();
            paths << path;
        }

        fs::JobEngine engine;
        ui::ProcessBar bar(&engine);
        QSignalSpy appeared(&bar, &ui::ProcessBar::shouldAppear);
        QSignalSpy finished(&engine, &fs::JobEngine::jobFinished);

        engine.submit(std::make_unique<fs::DeleteJob>(fs::DeleteJob::Mode::Permanent, paths));
        QVERIFY(finished.wait(5000));

        // The delay is the only way the bar appears. Once it is no longer
        // armed, a bar that was going to appear would have.
        QTRY_VERIFY_WITH_TIMEOUT(!appearancePending(bar), 5000);
        QCOMPARE(appeared.count(), 0);
    }

    /// And once it is over, the bar says so and goes away promptly.
    ///
    /// It used to sit at 100% for five seconds saying "Idle" — a full bar with
    /// no word for it reads as something still running, and five seconds is
    /// several times longer than the work most people do in a file manager.
    void aFinishedJobSaysDoneAndHidesPromptly()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("gone.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        fs::JobEngine engine;
        ui::ProcessBar bar(&engine);
        QSignalSpy idle(&bar, &ui::ProcessBar::becameIdle);

        QElapsedTimer elapsed;
        elapsed.start();
        engine.submit(
            std::make_unique<fs::DeleteJob>(fs::DeleteJob::Mode::Permanent, QStringList{path}));

        QVERIFY(idle.wait(5000));

        // The linger, not the old five seconds. Generous upper bound: the point
        // is that it is nearer one second than five.
        QVERIFY2(elapsed.elapsed() < 3000, qPrintable(QString::number(elapsed.elapsed())));

        const auto *summary = bar.findChild<QLabel *>(QStringLiteral("processSummary"));
        QVERIFY(summary != nullptr);
        QCOMPARE(summary->text(), QStringLiteral("Done"));
    }

    /// Moving to the trash is quiet: the panel already shows the result, so a
    /// trash job never puts the bar up, even one that outlasts the delay, and
    /// it does not count towards a visible job's aggregate.
    void trashJobsNeverShowTheBar()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("doomed.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        fs::JobEngine engine;
        ui::ProcessBar bar(&engine);
        QSignalSpy appeared(&bar, &ui::ProcessBar::shouldAppear);
        QSignalSpy finished(&engine, &fs::JobEngine::jobFinished);

        auto trashJob =
            std::make_unique<fs::DeleteJob>(fs::DeleteJob::Mode::Trash, QStringList{path});
        trashJob->setTrash(fs::Trash(dir.filePath(QStringLiteral("trash"))));
        const int id = engine.submit(std::move(trashJob));
        QVERIFY(engine.isQuiet(id));
        QCOMPARE(engine.visibleCount(), 0);

        QVERIFY(finished.wait(5000));
        QTRY_VERIFY_WITH_TIMEOUT(!appearancePending(bar), 5000);
        QCOMPARE(appeared.count(), 0);
    }

    void aVisibleJobStillShowsTheBarAlongsideATrash()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("doomed.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        fs::JobEngine engine;
        ui::ProcessBar bar(&engine);
        QSignalSpy appeared(&bar, &ui::ProcessBar::shouldAppear);
        QSignalSpy aggregate(&engine, &fs::JobEngine::aggregateProgress);

        const auto gate = std::make_shared<QSemaphore>();
        connect(&bar, &ui::ProcessBar::shouldAppear, this, [gate] { gate->release(); });
        engine.submit(std::make_unique<SlowJob>(gate));
        auto trashJob =
            std::make_unique<fs::DeleteJob>(fs::DeleteJob::Mode::Trash, QStringList{path});
        trashJob->setTrash(fs::Trash(dir.filePath(QStringLiteral("trash"))));
        engine.submit(std::move(trashJob));
        QCOMPARE(engine.visibleCount(), 1);

        QTRY_COMPARE_WITH_TIMEOUT(appeared.count(), 1, 2000);

        // However many were counted, the trash was never one of them.
        for (const QList<QVariant> &args : aggregate) {
            QVERIFY(args.at(2).toInt() <= 1);
        }
    }
};

QTEST_MAIN(TestProcessBar)
#include "tst_processbar.moc"
