// Icon view (the panel's second layout): switching, memory, grid movement, the
// tile's metrics and name wrapping, and a guard against the slow layout path.

#include "input/ActionRegistry.h"
#include "config/Theme.h"
#include "ui/FileItemDelegate.h"
#include "ui/FilePanel.h"
#include "ui/FolderViewMemory.h"
#include "ui/PanelView.h"
#include "ui/ThemePalette.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontMetrics>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using pf::ui::ViewMode;

class TestViewMode : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    ui::FilePanel *m_panel = nullptr;

    QString path(const QString &relative) const
    {
        return m_root.path() + QLatin1Char('/') + relative;
    }

    void touchFiles(const QString &directory, int count)
    {
        QDir().mkpath(directory);
        for (int i = 0; i < count; ++i) {
            QFile file(directory + QStringLiteral("/f%1.txt").arg(i, 6, 10, QLatin1Char('0')));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
    }

    bool settled() const { return !m_panel->isLoading(); }

    int cursorRow() const { return m_panel->view()->currentIndex().row(); }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        qputenv("PANEFILE_STATE_DIR", QFile::encodeName(path(QStringLiteral(".state"))));
        touchFiles(path(QStringLiteral("many")), 50);
        touchFiles(path(QStringLiteral("other")), 3);
    }

    void init()
    {
        ui::FolderViewMemory::instance().clear();
        m_panel = new ui::FilePanel;
        m_panel->resize(700, 500);
        m_panel->show();
        m_panel->navigateTo(path(QStringLiteral("many")));
        QTRY_VERIFY(settled());
        QCoreApplication::processEvents();
    }

    void cleanup()
    {
        delete m_panel;
        m_panel = nullptr;
    }

    void togglingSetsTheGridAndBack()
    {
        QCOMPARE(m_panel->viewMode(), ViewMode::List);
        QVERIFY(!m_panel->view()->gridSize().isValid());

        m_panel->setViewMode(ViewMode::Icons);
        QCOMPARE(m_panel->viewMode(), ViewMode::Icons);
        QVERIFY(m_panel->view()->gridSize().isValid());
        QVERIFY(m_panel->view()->isWrapping());
        QCOMPARE(m_panel->view()->flow(), QListView::LeftToRight);
        QCOMPARE(m_panel->view()->viewMode(), QListView::ListMode);

        m_panel->toggleViewMode();
        QCOMPARE(m_panel->viewMode(), ViewMode::List);
        QVERIFY(!m_panel->view()->gridSize().isValid());
        QVERIFY(!m_panel->view()->isWrapping());
        QCOMPARE(m_panel->view()->flow(), QListView::TopToBottom);
    }

    void itemsHaveOneSizeInBothLayoutsAfterToggling()
    {
        const QModelIndex first = m_panel->view()->model()->index(0, 0);
        const int rowHeight = ui::currentPalette().rowHeight;
        QCOMPARE(m_panel->view()->visualRect(first).height(), rowHeight);

        m_panel->setViewMode(ViewMode::Icons);
        const QSize tile = m_panel->view()->gridSize();
        QCOMPARE(m_panel->view()->visualRect(first).size(), tile);
        QCOMPARE(m_panel->view()->visualRect(m_panel->view()->model()->index(7, 0)).size(), tile);

        m_panel->setViewMode(ViewMode::List);
        QCOMPARE(m_panel->view()->visualRect(first).height(), rowHeight);
    }

    void itemsPerRowMatchesTheLayout()
    {
        QCOMPARE(m_panel->itemsPerRow(), 1);
        m_panel->setViewMode(ViewMode::Icons);

        const int columns = m_panel->itemsPerRow();
        QVERIFY(columns > 1);
        auto *view = m_panel->view();
        const QModelIndex a = view->model()->index(0, 0);
        const QModelIndex last = view->model()->index(columns - 1, 0);
        const QModelIndex next = view->model()->index(columns, 0);
        QCOMPARE(view->visualRect(last).y(), view->visualRect(a).y());
        QVERIFY(view->visualRect(next).y() > view->visualRect(a).y());
    }

    void theChoiceIsRememberedPerFolder()
    {
        m_panel->setViewMode(ViewMode::Icons);
        m_panel->navigateTo(path(QStringLiteral("other")));
        QTRY_VERIFY(settled());
        QCOMPARE(m_panel->viewMode(), ViewMode::List);

        m_panel->goBack();
        QTRY_VERIFY(settled());
        QCOMPARE(m_panel->viewMode(), ViewMode::Icons);
    }

    void settingTheDefaultBackForgetsTheEntry()
    {
        m_panel->setViewMode(ViewMode::Icons);
        QCOMPARE(ui::FolderViewMemory::instance().size(), 1);
        m_panel->setViewMode(ViewMode::List);
        QCOMPARE(ui::FolderViewMemory::instance().size(), 0);
    }

    void theDefaultViewAppliesToFoldersWithoutAChoice()
    {
        m_panel->setDefaultViewMode(ViewMode::Icons);
        QCOMPARE(m_panel->viewMode(), ViewMode::Icons);

        m_panel->navigateTo(path(QStringLiteral("other")));
        QTRY_VERIFY(settled());
        QCOMPARE(m_panel->viewMode(), ViewMode::Icons);

        // Choosing the list here is a departure from that default.
        m_panel->setViewMode(ViewMode::List);
        QCOMPARE(ui::FolderViewMemory::instance().size(), 1);
    }

    void aSecondPanelOnTheFolderInheritsTheMode()
    {
        m_panel->setViewMode(ViewMode::Icons);
        ui::FilePanel other;
        other.resize(700, 500);
        other.navigateTo(path(QStringLiteral("many")));
        QCOMPARE(other.viewMode(), ViewMode::Icons);
    }

    void memoryRoundTripsThroughItsFile()
    {
        QHash<QString, ViewMode> modes;
        modes.insert(QStringLiteral("/a b/c"), ViewMode::Icons);
        const auto read = ui::FolderViewMemory::fromJson(ui::FolderViewMemory::toJson(modes));
        QCOMPARE(read, modes);
        // Garbage is skipped rather than fatal.
        QVERIFY(ui::FolderViewMemory::fromJson("[1,2").isEmpty());
    }

    void downAndUpMoveAWholeRow()
    {
        m_panel->setViewMode(ViewMode::Icons);
        const int columns = m_panel->itemsPerRow();
        m_panel->moveCursorToStart();
        m_panel->moveCursorRows(1);
        QCOMPARE(cursorRow(), columns);
        m_panel->moveCursorRows(-1);
        QCOMPARE(cursorRow(), 0);
        // And does not leave the top.
        m_panel->moveCursorRows(-1);
        QCOMPARE(cursorRow(), 0);
    }

    void rightMovesOneTileAndWrapsOntoTheNextRow()
    {
        m_panel->setViewMode(ViewMode::Icons);
        const int columns = m_panel->itemsPerRow();
        m_panel->moveCursorToStart();
        m_panel->moveCursorColumns(1);
        QCOMPARE(cursorRow(), 1);

        m_panel->setCursorName(
            m_panel->view()->model()->index(columns - 1, 0).data(Qt::DisplayRole).toString());
        m_panel->moveCursorColumns(1);
        QCOMPARE(cursorRow(), columns);
        m_panel->moveCursorColumns(-1);
        QCOMPARE(cursorRow(), columns - 1);
    }

    void downFromTheLastFullRowLandsOnTheLastTile()
    {
        m_panel->setViewMode(ViewMode::Icons);
        const int columns = m_panel->itemsPerRow();
        const int count = m_panel->view()->model()->rowCount();
        QVERIFY2(count % columns != 0, "the fixture needs a short last row");

        const int lastRowStart = ((count - 1) / columns) * columns;
        m_panel->view()->setCurrentIndex(m_panel->view()->model()->index(lastRowStart - 1, 0));
        m_panel->moveCursorRows(1);
        QCOMPARE(cursorRow(), count - 1);

        // Already on the last row: stays.
        m_panel->moveCursorRows(1);
        QCOMPARE(cursorRow(), count - 1);
    }

    void pageDownMovesByRows()
    {
        m_panel->setViewMode(ViewMode::Icons);
        const int columns = m_panel->itemsPerRow();
        m_panel->moveCursorToStart();
        m_panel->movePage(1);
        QVERIFY(cursorRow() >= columns);
        QCOMPARE(cursorRow() % columns, 0);
    }

    void shiftRightExtendsTheSelection()
    {
        m_panel->setViewMode(ViewMode::Icons);
        m_panel->moveCursorToStart();
        m_panel->extendSelectionByColumns(1);
        m_panel->extendSelectionByColumns(1);
        QCOMPARE(m_panel->selectionCount(), 3);
    }

    void leftAndRightDoNothingInAList()
    {
        m_panel->moveCursorToStart();
        m_panel->moveCursorColumns(1);
        QCOMPARE(cursorRow(), 0);
        m_panel->extendSelectionByColumns(1);
        QCOMPARE(m_panel->selectionCount(), 0);
    }

    void tileMetricsFollowTheIconSize()
    {
        const QFontMetrics metrics{QFont()};
        auto palette = ui::currentPalette();
        palette.iconSize = 64;
        palette.panelPadding = 10;
        const auto tile = ui::FileItemDelegate::tileMetricsFor(palette, metrics);
        QCOMPARE(tile.tile.width(), 102);
        QCOMPARE(tile.tile.height(), 10 + 64 + 6 + (2 * metrics.lineSpacing()) + 10);

        palette.iconSize = 32;
        const auto small = ui::FileItemDelegate::tileMetricsFor(palette, metrics);
        QVERIFY(small.tile.width() < tile.tile.width());
        QVERIFY(small.tile.width() >= 32 + 20);
    }

    void namesWrapOntoTwoLinesAndKeepTheirExtension()
    {
        const QFontMetrics metrics{QFont()};
        const int width = 90;

        QCOMPARE(ui::FileItemDelegate::wrapName(QStringLiteral("a.txt"), metrics, width),
                 QStringList{QStringLiteral("a.txt")});

        const auto words = ui::FileItemDelegate::wrapName(
            QStringLiteral("quarterly budget review final"), metrics, width);
        QCOMPARE(words.size(), 2);
        QVERIFY(metrics.horizontalAdvance(words.at(0)) <= width);
        QVERIFY(metrics.horizontalAdvance(words.at(1)) <= width);

        // One long word: breaks before the dot, not mid-word.
        const auto camera = ui::FileItemDelegate::wrapName(
            QStringLiteral("DSC_0498_longer_than_a_tile.heic"), metrics, width);
        QCOMPARE(camera.size(), 2);
        QVERIFY(camera.at(1).endsWith(QStringLiteral("heic")));

        const auto huge = ui::FileItemDelegate::wrapName(
            QString(200, QLatin1Char('x')) + QStringLiteral(".png"), metrics, width);
        QCOMPARE(huge.size(), 2);
        QVERIFY(huge.at(1).endsWith(QStringLiteral("png")));
        QVERIFY(metrics.horizontalAdvance(huge.at(1)) <= width);
    }

    void aLargeFolderSwitchesAndScrollsQuickly()
    {
        const int count = qEnvironmentVariableIntValue("PF_PERF_N") > 0
                              ? qEnvironmentVariableIntValue("PF_PERF_N")
                              : 20000;
        touchFiles(path(QStringLiteral("huge")), count);
        m_panel->navigateTo(path(QStringLiteral("huge")));
        QTRY_VERIFY_WITH_TIMEOUT(settled(), 60000);

        // IconMode would lay out every item; a fixed grid in ListMode does not.
        QElapsedTimer timer;
        timer.start();
        m_panel->setViewMode(ViewMode::Icons);
        QCoreApplication::processEvents();
        m_panel->moveCursorToEnd();
        QCoreApplication::processEvents();
        m_panel->moveCursorToStart();
        m_panel->setViewMode(ViewMode::List);
        QCoreApplication::processEvents();
        QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
    }
};

QTEST_MAIN(TestViewMode)
#include "tst_viewmode.moc"
