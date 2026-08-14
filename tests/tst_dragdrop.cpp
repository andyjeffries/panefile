#include "model/DirectoryModel.h"
#include "ui/FilePanel.h"
#include "ui/PanelView.h"

#include <QApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::ui;

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write("x");
    file.close();
}

} // namespace

/// §7.12.
class TestDragDrop : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// The panel has to be willing to receive a drop at all.
    ///
    /// It was not. QAbstractItemView::setDragDropMode(NoDragDrop) calls
    /// setAcceptDrops(false) while applying the mode, and the constructor
    /// accepted drops *before* setting the mode — so the flag was cleared again
    /// on the next line and no drag event ever reached the panel. Dropping a
    /// file between panels did nothing at all.
    ///
    /// Both the view and its viewport are checked: a QAbstractScrollArea routes
    /// drag events through the viewport, so the view alone accepting them is
    /// not enough.
    void thePanelAcceptsDrops()
    {
        QVERIFY2(m_panel->view()->acceptDrops(), "the view must accept drops");
        QVERIFY2(m_panel->view()->viewport()->acceptDrops(), "so must its viewport");
    }

    void initTestCase()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        touch(m_dir->filePath(QStringLiteral("alpha.txt")));
        touch(m_dir->filePath(QStringLiteral("beta.txt")));
        touch(m_dir->filePath(QStringLiteral("target/keep.txt")));

        m_panel = std::make_unique<FilePanel>();
        m_panel->resize(400, 400);
        m_panel->navigateTo(m_dir->path());

        QSignalSpy scanned(m_panel.get(), &FilePanel::cursorChanged);
        QTest::qWait(300);
    }

    void cleanupTestCase()
    {
        m_panel.reset();
        m_dir.reset();
    }

    /// §7.12: "Dropping onto empty space targets the panel's cwd."
    void dropOnEmptySpaceTargetsTheWorkingDirectory()
    {
        // Far below the last row.
        QCOMPARE(m_panel->view()->destinationFor(QPoint(10, 380)), m_dir->path());
    }

    /// §7.12: "Dropping onto a directory row targets that directory."
    void dropOnADirectoryRowTargetsThatDirectory()
    {
        const QModelIndex row = rowFor(QStringLiteral("target"));
        QVERIFY(row.isValid());

        QCOMPARE(m_panel->view()->destinationFor(m_panel->view()->visualRect(row).center()),
                 m_dir->filePath(QStringLiteral("target")));
    }

    /// Selection mode must not change where a drop lands.
    ///
    /// A file dropped onto a pane in Selection mode was reported as vanishing.
    /// The mode is cosmetic — a header badge and a paint style — so if the
    /// destination ever depended on it, that would be the whole explanation.
    void selectionModeDoesNotChangeTheDestination()
    {
        const QModelIndex directory = rowFor(QStringLiteral("target"));
        const QModelIndex file = rowFor(QStringLiteral("alpha.txt"));
        QVERIFY(directory.isValid());
        QVERIFY(file.isValid());

        const QString onEmpty = m_panel->view()->destinationFor(QPoint(10, 380));
        const QString onDirectory =
            m_panel->view()->destinationFor(m_panel->view()->visualRect(directory).center());
        const QString onFile =
            m_panel->view()->destinationFor(m_panel->view()->visualRect(file).center());

        m_panel->setSelectionMode(true);
        m_panel->toggleSelectionAt(QStringLiteral("beta.txt"));

        QCOMPARE(m_panel->view()->destinationFor(QPoint(10, 380)), onEmpty);
        QCOMPARE(m_panel->view()->destinationFor(m_panel->view()->visualRect(directory).center()),
                 onDirectory);
        QCOMPARE(m_panel->view()->destinationFor(m_panel->view()->visualRect(file).center()),
                 onFile);

        m_panel->setSelectionMode(false);
        m_panel->clearSelection();
    }

    /// A file row is not a target of its own: dropping on it plainly means the
    /// directory the file is in.
    void dropOnAFileRowTargetsTheWorkingDirectory()
    {
        const QModelIndex row = rowFor(QStringLiteral("alpha.txt"));
        QVERIFY(row.isValid());

        QCOMPARE(m_panel->view()->destinationFor(m_panel->view()->visualRect(row).center()),
                 m_dir->path());
    }

    /// The default follows the filesystem, because the two cases mean
    /// different things: within one disk a drag is a rename, across two it
    /// would copy every byte and then delete the original.
    void theDefaultFollowsTheFilesystem()
    {
        QCOMPARE(PanelView::actionFor(Qt::NoModifier, true), Qt::MoveAction);
        QCOMPARE(PanelView::actionFor(Qt::NoModifier, false), Qt::CopyAction);
    }

    /// A held modifier is a decision, so it wins over the filesystem in both
    /// directions — otherwise the override would be useless exactly where it
    /// matters.
    void modifiersOverrideTheFilesystem()
    {
        QCOMPARE(PanelView::actionFor(Qt::ShiftModifier, false), Qt::MoveAction);
        QCOMPARE(PanelView::actionFor(Qt::ControlModifier, true), Qt::CopyAction);

        // Shift wins when both are held: it is the one the user had to reach
        // for deliberately.
        QCOMPARE(PanelView::actionFor(Qt::ShiftModifier | Qt::ControlModifier, true),
                 Qt::MoveAction);
        QCOMPARE(PanelView::actionFor(Qt::ShiftModifier | Qt::ControlModifier, false),
                 Qt::MoveAction);
    }

    /// Alt asks rather than decides, and does so whatever else is held — the
    /// menu still opens with a sensible default action preselected.
    void altAsksInsteadOfDeciding()
    {
        QVERIFY(PanelView::wantsMenu(Qt::AltModifier));
        QVERIFY(PanelView::wantsMenu(Qt::AltModifier | Qt::ShiftModifier));

        QVERIFY(!PanelView::wantsMenu(Qt::NoModifier));
        QVERIFY(!PanelView::wantsMenu(Qt::ShiftModifier));
        QVERIFY(!PanelView::wantsMenu(Qt::ControlModifier));
    }

    /// The paths carried by a drag come from the panel's selection (§6.1's
    /// Selection mode), not the view's own selection model.
    void dragPayloadComesFromThePanelSelection()
    {
        m_panel->clearSelection();
        m_panel->setCursorName(QStringLiteral("alpha.txt"));

        QStringList paths;
        Q_EMIT m_panel->view()->dragPathsRequested(&paths);

        // With nothing selected it is the cursor item, which is what makes
        // every file operation work without a selection first.
        QCOMPARE(paths.size(), 1);
        QCOMPARE(QFileInfo(paths.first()).fileName(), QStringLiteral("alpha.txt"));

        m_panel->toggleSelectionAt(QStringLiteral("beta.txt"));
        paths.clear();
        Q_EMIT m_panel->view()->dragPathsRequested(&paths);

        QCOMPARE(paths.size(), 1);
        QCOMPARE(QFileInfo(paths.first()).fileName(), QStringLiteral("beta.txt"));
    }

    /// Dragging a row that is not part of the selection drags *that row*.
    ///
    /// This is the case that loses files if it is wrong. With a selection left
    /// over from earlier — easy to accumulate in Selection mode, which no longer
    /// clears on exit — grabbing some other file and dragging it would carry the
    /// old selection instead: the file the user was pointing at stays put, and
    /// files they had forgotten about move somewhere else. Now that a drag
    /// within one filesystem moves rather than copies, that is not a nuisance,
    /// it is data going missing.
    ///
    /// It works because pressing on an unselected row clears the selection
    /// before the drag can start, which is the press half of the click handling.
    void draggingAnUnselectedRowLeavesTheOldSelectionBehind()
    {
        m_panel->setSelectionMode(true);

        // Entering the mode now selects the cursor row, so start from a known
        // state: exactly beta.txt selected, and the cursor elsewhere.
        m_panel->clearSelection();
        m_panel->toggleSelectionAt(QStringLiteral("beta.txt"));
        QCOMPARE(m_panel->selectionCount(), 1);

        // The press that begins a drag on a different row.
        m_panel->handleClickPress(QStringLiteral("alpha.txt"), Qt::NoModifier);
        m_panel->setCursorName(QStringLiteral("alpha.txt"));

        QStringList paths;
        Q_EMIT m_panel->view()->dragPathsRequested(&paths);

        QCOMPARE(paths.size(), 1);
        QCOMPARE(QFileInfo(paths.constFirst()).fileName(), QStringLiteral("alpha.txt"));

        m_panel->setSelectionMode(false);
        m_panel->clearSelection();
    }

    /// And dragging a row that *is* selected carries the whole selection, which
    /// is the reason the press does not clear it.
    void draggingASelectedRowCarriesTheSelection()
    {
        m_panel->clearSelection();
        m_panel->toggleSelectionAt(QStringLiteral("alpha.txt"));
        m_panel->toggleSelectionAt(QStringLiteral("beta.txt"));

        m_panel->handleClickPress(QStringLiteral("beta.txt"), Qt::NoModifier);

        QStringList paths;
        Q_EMIT m_panel->view()->dragPathsRequested(&paths);

        QCOMPARE(paths.size(), 2);

        m_panel->clearSelection();
    }

private:
    QModelIndex rowFor(const QString &name) const
    {
        const QAbstractItemModel *model = m_panel->view()->model();
        for (int i = 0; i < model->rowCount(); ++i) {
            const QModelIndex index = model->index(i, 0);
            if (index.data(DirectoryModel::NameRole).toString() == name) {
                return index;
            }
        }
        return {};
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FilePanel> m_panel;
};

QTEST_MAIN(TestDragDrop)
#include "tst_dragdrop.moc"
