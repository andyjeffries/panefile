// The mosaic view: the pure justified layout, the zoom, the image-size probe,
// and the panel agreeing with Qt about where every tile is.

#include "model/DirectoryModel.h"
#include "ui/FileItemDelegate.h"
#include "ui/FilePanel.h"
#include "ui/FolderViewMemory.h"
#include "ui/MainWindow.h"
#include "ui/MosaicLayout.h"
#include "ui/MosaicZoom.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QScrollBar>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace pf;
using pf::ui::MosaicLayout;
using pf::ui::MosaicZoom;
using pf::ui::ViewMode;

namespace {

QList<qreal> mixedAspects(int count)
{
    static const QList<qreal> kShapes{1.5, 0.75, 1.0, 2.0, 0.66, 1.33, 3.0, 0.5};
    QList<qreal> aspects;
    for (int i = 0; i < count; ++i) {
        aspects.append(kShapes.at(i % kShapes.size()));
    }
    return aspects;
}

} // namespace

class TestMosaic : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    ui::FilePanel *m_panel = nullptr;

    QString path(const QString &relative) const
    {
        return m_root.path() + QLatin1Char('/') + relative;
    }

    void writePng(const QString &file, int width, int height)
    {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QVERIFY(image.save(file));
    }

    ui::FileItemDelegate *delegate() const
    {
        return qobject_cast<ui::FileItemDelegate *>(m_panel->view()->itemDelegate());
    }

    bool settled() const { return !m_panel->isLoading(); }

    void openMosaic(const QString &folder)
    {
        m_panel->navigateTo(folder);
        QTRY_VERIFY(settled());
        m_panel->setViewMode(ViewMode::Mosaic);
        QCoreApplication::processEvents();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        qputenv("PANEFILE_STATE_DIR", QFile::encodeName(path(QStringLiteral(".state"))));

        QDir().mkpath(path(QStringLiteral("photos")));
        static const QList<QSize> kSizes{{300, 200}, {200, 300}, {400, 400}, {600, 200},
                                         {240, 320}, {320, 240}, {800, 300}};
        for (int i = 0; i < 40; ++i) {
            const QSize size = kSizes.at(i % kSizes.size());
            writePng(path(QStringLiteral("photos/p%1.png").arg(i, 2, 10, QLatin1Char('0'))),
                     size.width(), size.height());
        }
        QDir().mkpath(path(QStringLiteral("photos/sub")));
        QFile note(path(QStringLiteral("photos/notes.txt")));
        QVERIFY(note.open(QIODevice::WriteOnly));
    }

    void init()
    {
        ui::FolderViewMemory::instance().clear();
        MosaicZoom::instance().clear();
        m_panel = new ui::FilePanel;
        m_panel->resize(900, 600);
        m_panel->show();
    }

    void cleanup()
    {
        delete m_panel;
        m_panel = nullptr;
    }

    void computeJustifiesFullRows()
    {
        const int width = 1000;
        const int target = 200;
        const int gap = 6;
        const MosaicLayout layout = MosaicLayout::compute(mixedAspects(40), width, target, gap);

        QVERIFY(layout.rows.size() > 2);
        for (int r = 0; r < layout.rows.size() - 1; ++r) {
            const auto &row = layout.rows.at(r);
            int total = 0;
            for (int i = row.first; i < row.first + row.count; ++i) {
                total += layout.rects.at(i).width();
                QCOMPARE(layout.rects.at(i).height(), row.height);
                QCOMPARE(layout.rects.at(i).top(), row.top);
            }
            QCOMPARE(total, width);
            QVERIFY(row.height >= target / 2 && row.height <= target * 2);
        }
        for (int i = 1; i < layout.rects.size(); ++i) {
            if (layout.rects.at(i).top() == layout.rects.at(i - 1).top()) {
                QCOMPARE(layout.rects.at(i).left(), layout.rects.at(i - 1).right() + 1);
            }
        }
    }

    void lastRowIsNotStretched()
    {
        const MosaicLayout layout = MosaicLayout::compute(mixedAspects(7), 1000, 200, 6);
        const auto &last = layout.rows.last();
        QCOMPARE(last.height, 200);
        QCOMPARE(layout.rects.at(last.first).left(), 0);
        const QRect end = layout.rects.at(last.first + last.count - 1);
        QVERIFY(end.right() < 1000);
    }

    void extremeAspectsAreClamped()
    {
        const MosaicLayout layout =
            MosaicLayout::compute({100.0, 0.001, -1.0, 0.0, 1.0}, 800, 200, 6);
        QCOMPARE(layout.rects.size(), 5);
        for (const QRect &rect : layout.rects) {
            QVERIFY(rect.width() >= 1);
            QVERIFY(rect.width() <= 800);
        }
        QVERIFY(MosaicLayout::compute({}, 800, 200, 6).rects.isEmpty());
    }

    void verticalNeighbourPicksNearestCentre()
    {
        const MosaicLayout layout = MosaicLayout::compute(mixedAspects(30), 1000, 200, 6);
        QVERIFY(layout.rows.size() >= 3);

        const auto &second = layout.rows.at(1);
        const int item = second.first;
        const int down = layout.verticalNeighbour(item, 1);
        QCOMPARE(layout.rowOf(down), 2);
        for (int i = layout.rows.at(2).first; i < layout.rows.at(2).first + layout.rows.at(2).count;
             ++i) {
            QVERIFY(
                std::abs(layout.rects.at(down).center().x() - layout.rects.at(item).center().x()) <=
                std::abs(layout.rects.at(i).center().x() - layout.rects.at(item).center().x()));
        }
        QCOMPARE(layout.rowOf(layout.verticalNeighbour(0, -1)), 0);
        QCOMPARE(layout.rowOf(layout.verticalNeighbour(29, 1)), layout.rows.size() - 1);
        QCOMPARE(MosaicLayout().verticalNeighbour(0, 1), -1);
    }

    void itemsInBandCoversTheViewport()
    {
        const MosaicLayout layout = MosaicLayout::compute(mixedAspects(60), 1000, 200, 6);
        const auto band = layout.itemsInBand(250, 700);
        QVERIFY(band.first >= 0);
        QVERIFY(layout.rects.at(band.first).bottom() >= 250);
        QVERIFY(band.first == 0 || layout.rects.at(band.first - 1).bottom() < 250);
        QVERIFY(layout.rects.at(band.second).top() < 700);
        QVERIFY(band.second == layout.rects.size() - 1 ||
                layout.rects.at(band.second + 1).top() >= 700);
        QCOMPARE(layout.itemsInBand(100000, 100500).first, -1);
    }

    void mosaicRectsMatchTheView()
    {
        openMosaic(path(QStringLiteral("photos")));
        auto *view = m_panel->view();
        QVERIFY(view->model()->rowCount() >= 40);

        // Every image shape has to land before the layout is final.
        const QModelIndex lastImage = view->model()->index(view->model()->rowCount() - 1, 0);
        QTRY_VERIFY_WITH_TIMEOUT(
            lastImage.data(DirectoryModel::ImageSizeRole).isValid() ||
                lastImage.data(Qt::DisplayRole).toString() == QLatin1String("sub") ||
                lastImage.data(Qt::DisplayRole).toString() == QLatin1String("notes.txt"),
            10000);

        const auto check = [&] {
            QCoreApplication::processEvents();
            const MosaicLayout &layout = delegate()->mosaicLayout(view->model());
            QCOMPARE(layout.rects.size(), view->model()->rowCount());
            const int scroll = view->verticalScrollBar()->value();
            for (int row = 0; row < layout.rects.size(); ++row) {
                const QRect expected = layout.rects.at(row).translated(0, -scroll);
                const QRect actual = view->visualRect(view->model()->index(row, 0));
                // The view's contents start at its left edge, so only the
                // rect's own geometry is compared.
                QCOMPARE(actual.size(), expected.size());
                QCOMPARE(actual.topLeft(), expected.topLeft());
            }
        };
        QTest::qWait(300);
        check();

        m_panel->resize(700, 600);
        QTest::qWait(300);
        check();
        m_panel->resize(1100, 600);
        QTest::qWait(300);
        check();
    }

    void imageSizeRoleReportsDimensions()
    {
        openMosaic(path(QStringLiteral("photos")));
        auto *view = m_panel->view();
        const int row = [&] {
            for (int i = 0; i < view->model()->rowCount(); ++i) {
                if (view->model()->index(i, 0).data(Qt::DisplayRole).toString() ==
                    QLatin1String("p00.png")) {
                    return i;
                }
            }
            return -1;
        }();
        QVERIFY(row >= 0);
        const QModelIndex index = view->model()->index(row, 0);
        QTRY_COMPARE(index.data(DirectoryModel::ImageSizeRole).toSize(), QSize(300, 200));
        QTRY_COMPARE(index.data(DirectoryModel::AspectRatioRole).toReal(), 1.5);
    }

    void nonImagesAreSquare()
    {
        openMosaic(path(QStringLiteral("photos")));
        auto *view = m_panel->view();
        for (int i = 0; i < view->model()->rowCount(); ++i) {
            const QModelIndex index = view->model()->index(i, 0);
            const QString name = index.data(Qt::DisplayRole).toString();
            if (name == QLatin1String("sub") || name == QLatin1String("notes.txt")) {
                QCOMPARE(index.data(DirectoryModel::AspectRatioRole).toReal(), 1.0);
                QVERIFY(!index.data(DirectoryModel::ImageSizeRole).isValid());
            }
        }
    }

    void nonRegularFilesDoNotBlockTheProbe()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString fifo = dir.filePath(QStringLiteral("a-trap.png"));
        QVERIFY(::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0);
        QVERIFY(QFile::link(fifo, dir.filePath(QStringLiteral("b-link.png"))));
        writePng(dir.filePath(QStringLiteral("c-real.png")), 300, 200);

        {
            DirectoryModel model;
            model.setImageSizesWanted(true);
            model.setPath(dir.path());
            QTRY_VERIFY(!model.isScanning());
            QCoreApplication::processEvents();

            int realRow = -1;
            for (int i = 0; i < model.rowCount(); ++i) {
                const QModelIndex index = model.index(i, 0);
                const QString name = index.data(Qt::DisplayRole).toString();
                (void)index.data(DirectoryModel::AspectRatioRole);
                if (name == QLatin1String("c-real.png")) {
                    realRow = i;
                } else if (name == QLatin1String("a-trap.png")) {
                    QCOMPARE(index.data(DirectoryModel::AspectRatioRole).toReal(), 1.0);
                }
            }
            QVERIFY(realRow >= 0);
            QTRY_COMPARE_WITH_TIMEOUT(
                model.index(realRow, 0).data(DirectoryModel::ImageSizeRole).toSize(),
                QSize(300, 200), 3000);
            for (int i = 0; i < model.rowCount(); ++i) {
                const QModelIndex index = model.index(i, 0);
                if (index.data(Qt::DisplayRole).toString() == QLatin1String("b-link.png")) {
                    QVERIFY(!index.data(DirectoryModel::ImageSizeRole).isValid());
                    QCOMPARE(index.data(DirectoryModel::AspectRatioRole).toReal(), 1.0);
                }
            }
        }

        const int fd = ::open(QFile::encodeName(fifo).constData(), O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            ::close(fd);
        }
    }

    void sizesArriveAfterMosaicToggledMidProbe()
    {
        DirectoryModel model;
        model.setImageSizesWanted(true);
        model.setPath(path(QStringLiteral("photos")));
        QTRY_VERIFY(!model.isScanning());
        QCoreApplication::processEvents();

        const auto readAll = [&] {
            for (int i = 0; i < model.rowCount(); ++i) {
                (void)model.index(i, 0).data(DirectoryModel::AspectRatioRole);
            }
        };
        readAll();
        model.setImageSizesWanted(false);
        model.setImageSizesWanted(true);
        readAll();
        QTRY_VERIFY([&] {
            for (int i = 0; i < model.rowCount(); ++i) {
                const QModelIndex index = model.index(i, 0);
                if (index.data(Qt::DisplayRole).toString().endsWith(QLatin1String(".png")) &&
                    !index.data(DirectoryModel::ImageSizeRole).toSize().isValid()) {
                    return false;
                }
            }
            return true;
        }());
    }

    void exifRotatedImageIsPortrait()
    {
        QDir().mkpath(path(QStringLiteral("exif")));
        QImage image(300, 200, QImage::Format_RGB32);
        image.fill(Qt::red);
        QImageWriter writer(path(QStringLiteral("exif/rot.jpg")), "jpeg");
        if (!writer.supportsOption(QImageIOHandler::ImageTransformation)) {
            QSKIP("the JPEG plugin cannot write an EXIF orientation");
        }
        writer.setTransformation(QImageIOHandler::TransformationRotate90);
        QVERIFY(writer.write(image));

        openMosaic(path(QStringLiteral("exif")));
        const QModelIndex index = m_panel->view()->model()->index(0, 0);
        QTRY_COMPARE(index.data(DirectoryModel::ImageSizeRole).toSize(), QSize(200, 300));
    }

    void zoomChangesRowHeight()
    {
        QCOMPARE(MosaicZoom::instance().rowHeight(), 192);
        MosaicZoom::instance().setRowHeight(10);
        QCOMPARE(MosaicZoom::instance().rowHeight(), MosaicZoom::kMin);
        MosaicZoom::instance().setRowHeight(5000);
        QCOMPARE(MosaicZoom::instance().rowHeight(), MosaicZoom::kMax);

        QCOMPARE(MosaicZoom::fromJson(MosaicZoom::toJson(300)), 300);
        QCOMPARE(MosaicZoom::fromJson("not json"), MosaicZoom::kDefault);
        QCOMPARE(MosaicZoom::fromJson("{\"row_height\": \"big\"}"), MosaicZoom::kDefault);

        openMosaic(path(QStringLiteral("photos")));
        MosaicZoom::instance().setRowHeight(300);
        QTest::qWait(300);
        const MosaicLayout &layout = delegate()->mosaicLayout(m_panel->view()->model());
        QCOMPARE(layout.targetHeight, 300);
        QVERIFY(std::abs(layout.rows.first().height - 300) <= 150);
    }

    void ctrlWheelZooms()
    {
        openMosaic(path(QStringLiteral("photos")));
        auto *viewport = m_panel->view()->viewport();
        const int before = MosaicZoom::instance().rowHeight();

        QWheelEvent zoom(QPointF(50, 50), viewport->mapToGlobal(QPointF(50, 50)), {},
                         QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase,
                         false);
        QCoreApplication::sendEvent(viewport, &zoom);
        QCOMPARE(MosaicZoom::instance().rowHeight(), before + MosaicZoom::kStep);

        QWheelEvent plain(QPointF(50, 50), viewport->mapToGlobal(QPointF(50, 50)), {},
                          QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(viewport, &plain);
        QCOMPARE(MosaicZoom::instance().rowHeight(), before + MosaicZoom::kStep);
    }

    void arrowKeysMoveThroughTheMosaic()
    {
        openMosaic(path(QStringLiteral("photos")));
        QTest::qWait(300);
        m_panel->moveCursorToStart();
        const auto &layout = delegate()->mosaicLayout(m_panel->view()->model());
        QVERIFY(layout.rows.size() > 2);
        const auto row = [&] { return layout.rowOf(m_panel->view()->currentIndex().row()); };

        m_panel->moveCursorColumns(1);
        QCOMPARE(m_panel->view()->currentIndex().row(), 1);
        m_panel->moveCursorColumns(-1);
        QCOMPARE(m_panel->view()->currentIndex().row(), 0);

        m_panel->moveCursorRows(1);
        QCOMPARE(row(), 1);
        m_panel->moveCursorRows(-1);
        QCOMPARE(row(), 0);
        m_panel->moveCursorRows(-1);
        QCOMPARE(row(), 0);

        m_panel->moveCursorToEnd();
        m_panel->moveCursorRows(1);
        QCOMPARE(row(), layout.rows.size() - 1);

        m_panel->moveCursorToStart();
        m_panel->movePage(1);
        QVERIFY(row() > 0);
    }

    void dropTargetIsAFolderTile()
    {
        openMosaic(path(QStringLiteral("photos")));
        auto *view = m_panel->view();
        for (int i = 0; i < view->model()->rowCount(); ++i) {
            const QModelIndex index = view->model()->index(i, 0);
            if (index.data(Qt::DisplayRole).toString() == QLatin1String("sub")) {
                view->scrollTo(index);
                const QPoint centre = view->visualRect(index).center();
                QCOMPARE(view->destinationFor(centre), path(QStringLiteral("photos/sub")));
                return;
            }
        }
        QFAIL("no sub folder tile");
    }

    void mosaicIsRememberedPerFolder()
    {
        m_panel->navigateTo(path(QStringLiteral("photos")));
        QTRY_VERIFY(settled());
        m_panel->setViewMode(ViewMode::Mosaic);
        m_panel->navigateTo(path(QStringLiteral("exif")));
        QTRY_VERIFY(settled());
        QCOMPARE(m_panel->viewMode(), ViewMode::List);
        m_panel->navigateTo(path(QStringLiteral("photos")));
        QTRY_VERIFY(settled());
        QCOMPARE(m_panel->viewMode(), ViewMode::Mosaic);

        QCOMPARE(ui::viewModeFromName(ui::viewModeName(ViewMode::Mosaic)), ViewMode::Mosaic);
        const auto restored = ui::FolderViewMemory::fromJson(
            ui::FolderViewMemory::toJson({{QStringLiteral("/x"), ViewMode::Mosaic}}));
        QCOMPARE(restored.value(QStringLiteral("/x")), ViewMode::Mosaic);
        QCOMPARE(ui::viewModeFromName(QStringLiteral("grid")), ViewMode::List);
    }

    void sliderOnlyShowsForMosaic()
    {
        ui::MainWindow window;
        window.show();
        ui::FilePanel *panel = window.panelStrip()->addPanel(path(QStringLiteral("photos")));
        QVERIFY(panel != nullptr);
        auto *slider = window.findChild<QSlider *>(QStringLiteral("mosaicZoom"));
        QVERIFY(slider != nullptr);
        QCOMPARE(window.activePanel(), panel);

        panel->setViewMode(ViewMode::List);
        QVERIFY(!slider->isVisible());
        panel->setViewMode(ViewMode::Mosaic);
        QVERIFY(slider->isVisible());
        panel->setViewMode(ViewMode::Icons);
        QVERIFY(!slider->isVisible());

        panel->setViewMode(ViewMode::Mosaic);
        slider->setValue(288);
        QCOMPARE(MosaicZoom::instance().rowHeight(), 288);
        MosaicZoom::instance().zoomBy(1);
        QCOMPARE(slider->value(), 288 + MosaicZoom::kStep);
    }
};

QTEST_MAIN(TestMosaic)
#include "tst_mosaic.moc"
