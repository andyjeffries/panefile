// Performance probe (cycle 3): what icon view costs against the list in a large
// folder — the scan landing, a toggle, scrolling a page at a time, and the
// cursor walking across the grid. Not a pass/fail gate; it prints timings.

#include "ui/FilePanel.h"
#include "ui/FolderViewMemory.h"
#include "ui/PanelView.h"
#include "ui/ViewMode.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using pf::ui::ViewMode;

Q_DECLARE_METATYPE(pf::ui::ViewMode)

class TestPerfIconView : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;

    QString makeFolder(int count)
    {
        const QString dir = m_root.path() + QStringLiteral("/n%1").arg(count);
        if (QDir(dir).exists()) {
            return dir;
        }
        QDir().mkpath(dir);
        for (int i = 0; i < count; ++i) {
            QFile file(dir + QStringLiteral("/file-%1.txt").arg(i, 6, 10, QLatin1Char('0')));
            if (!file.open(QIODevice::WriteOnly)) {
                qFatal("cannot create %s", qPrintable(file.fileName()));
            }
        }
        return dir;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        qputenv("PANEFILE_STATE_DIR", QFile::encodeName(m_root.path() + "/.state"));
    }

    void costs_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<ViewMode>("mode");
        for (const int n : {10000, 100000}) {
            QTest::addRow("list-%d", n) << n << ViewMode::List;
            QTest::addRow("icons-%d", n) << n << ViewMode::Icons;
            QTest::addRow("mosaic-%d", n) << n << ViewMode::Mosaic;
        }
    }

    void costs()
    {
        QFETCH(int, count);
        QFETCH(ViewMode, mode);
        const QString dir = makeFolder(count);

        ui::FolderViewMemory::instance().clear();
        ui::FilePanel panel;
        panel.resize(900, 700);
        panel.show();
        panel.setDefaultViewMode(mode);
        QVERIFY(QTest::qWaitForWindowExposed(&panel));

        QElapsedTimer timer;
        timer.start();
        panel.navigateTo(dir);
        QTRY_VERIFY_WITH_TIMEOUT(!panel.isLoading(), 120000);
        QCoreApplication::processEvents();
        const qint64 scan = timer.elapsed();

        timer.restart();
        const ViewMode other = mode == ViewMode::List ? ViewMode::Icons : ViewMode::List;
        panel.setViewMode(other);
        QCoreApplication::processEvents();
        panel.setViewMode(mode);
        QCoreApplication::processEvents();
        const qint64 toggle = timer.elapsed();

        QScrollBar *bar = panel.view()->verticalScrollBar();
        timer.restart();
        constexpr int kPages = 200;
        for (int i = 0; i < kPages; ++i) {
            bar->setValue(bar->value() + bar->pageStep());
            panel.view()->viewport()->repaint();
        }
        const double perPage = static_cast<double>(timer.nsecsElapsed()) / 1e6 / kPages;

        panel.moveCursorToStart();
        timer.restart();
        constexpr int kMoves = 2000;
        for (int i = 0; i < kMoves; ++i) {
            panel.moveCursorRows(1);
        }
        QCoreApplication::processEvents();
        const double perMove = static_cast<double>(timer.nsecsElapsed()) / 1e6 / kMoves;

        qInfo().noquote() << QStringLiteral(
                                 "%1 n=%2 scan=%3ms toggle-twice=%4ms page=%5ms move=%6ms")
                                 .arg(viewModeName(mode), -6)
                                 .arg(count)
                                 .arg(scan)
                                 .arg(toggle)
                                 .arg(perPage, 0, 'f', 3)
                                 .arg(perMove, 0, 'f', 4);
    }
};

QTEST_MAIN(TestPerfIconView)
#include "tst_perf_iconview.moc"
