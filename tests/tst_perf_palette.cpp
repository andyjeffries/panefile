// Performance probes for the command palette's selection-dependent actions and
// for dropping many files on the sidebar. They time, rather than assert, so the
// suite stays green; set PF_PERF_N to scale them up (default 300).

#include "app/FileOperations.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "input/ActionRegistry.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"
#include "ui/SidebarLayout.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

namespace {
int perfN()
{
    bool ok = false;
    const int n = qEnvironmentVariableIntValue("PF_PERF_N", &ok);
    return ok && n > 0 ? n : 300;
}
} // namespace

class TestPerfPalette : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void paletteOpenCostGrowsWithSelection()
    {
        const int n = perfN();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QByteArray png;
        {
            QImage image(8, 4, QImage::Format_ARGB32);
            image.fill(Qt::blue);
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            QVERIFY(image.save(&buffer, "PNG"));
        }
        for (int i = 0; i < n; ++i) {
            QFile file(dir.filePath(QStringLiteral("pic%1.png").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(png);
        }

        ui::MainWindow window;
        input::ActionRegistry registry;
        fs::JobEngine engine;
        fs::UndoStack undo;
        FileOperations operations(&window, window.panelStrip(), &registry, &engine, &undo);
        operations.registerActions();
        window.panelStrip()->addPanel(dir.path());
        ui::FilePanel *panel = window.panelStrip()->focusedPanel();
        QTRY_COMPARE_WITH_TIMEOUT(panel->view()->model()->rowCount(), n, 20000);
        panel->selectAll();

        // What Application::promptForAction does for each registered action.
        QElapsedTimer timer;
        timer.start();
        int enabled = 0;
        for (const QString &id : registry.ids()) {
            enabled += registry.isEnabled(id) ? 1 : 0;
        }
        const qint64 all = timer.elapsed();

        timer.restart();
        const QStringList once = panel->selectedPaths();
        const qint64 select = timer.elapsed();

        qInfo("palette predicates: %d selected images, %d actions enabled, %lld ms "
              "(one selectedPaths(): %lld ms)",
              n, enabled, all, select);
    }

    void droppingManyFilesOnTheSidebar()
    {
        const int n = perfN();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ui::SidebarLayout layout = ui::SidebarLayout::defaults();

        // What Sidebar's external drop does per URL.
        QElapsedTimer timer;
        timer.start();
        int index = 0;
        for (int i = 0; i < n; ++i) {
            const QString path = dir.filePath(QStringLiteral("f%1").arg(i));
            const ui::SidebarEntry entry = ui::SidebarLayout::entryFor(path);
            layout.insertEntry(0, index, entry);
            if (const auto landed = layout.find(QDir::cleanPath(entry.resolvedPath()))) {
                index = landed->second + 1;
            }
        }
        qInfo("sidebar drop: %d files, %lld ms", n, timer.elapsed());
    }
};

QTEST_MAIN(TestPerfPalette)
#include "tst_perf_palette.moc"
