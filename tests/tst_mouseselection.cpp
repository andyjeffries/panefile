#include "ui/FilePanel.h"
#include "ui/PanelView.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;

/// Selecting files with the mouse: Ctrl/Cmd+click to pick, Shift+click for a
/// range. None of this did anything before — the view's own selection model was
/// never consulted, because §6.1's selection belongs to the panel, and nothing
/// connected a click to it.
class TestMouseSelection : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Pressing v selects the row you are standing on.
    ///
    /// It used to select nothing at all, and then — on the first movement —
    /// select the row you had just left. So `v` looked inert, the first `j`
    /// marked the wrong file, and the row under the cursor was never in the
    /// selection it appeared to be building.
    void enteringSelectionModeSelectsTheCursorRow()
    {
        m_panel->setCursorName(QStringLiteral("b.txt"));
        m_panel->setSelectionMode(true);

        QCOMPARE(selectedNames(), QStringList{QStringLiteral("b.txt")});
    }

    /// And movement extends a range from there, so going down and back up
    /// leaves exactly the row you started on.
    void selectionModeMovementIsARangeFromTheAnchor()
    {
        m_panel->setCursorName(QStringLiteral("b.txt"));
        m_panel->setSelectionMode(true);

        m_panel->moveCursor(1);
        m_panel->extendSelectionTo(m_panel->cursorName());
        const QStringList two{QStringLiteral("b.txt"), QStringLiteral("c.txt")};
        QCOMPARE(selectedNames(), two);

        m_panel->moveCursor(1);
        m_panel->extendSelectionTo(m_panel->cursorName());
        QCOMPARE(m_panel->selectionCount(), 3);

        // Back up, and the range narrows rather than the extra row lingering.
        m_panel->moveCursor(-1);
        m_panel->extendSelectionTo(m_panel->cursorName());
        QCOMPARE(selectedNames(), two);

        m_panel->moveCursor(-1);
        m_panel->extendSelectionTo(m_panel->cursorName());
        QCOMPARE(selectedNames(), QStringList{QStringLiteral("b.txt")});
    }

    void initTestCase()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        for (const char *name : {"a.txt", "b.txt", "c.txt", "d.txt", "e.txt"}) {
            QFile file(m_dir->filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
            file.close();
        }
    }

    void init()
    {
        m_panel = std::make_unique<ui::FilePanel>();
        m_panel->navigateTo(m_dir->path());
        QTRY_COMPARE_WITH_TIMEOUT(m_panel->view()->model()->rowCount(), 5, 5000);
        m_panel->moveCursorToStart();
    }

    void cleanup() { m_panel.reset(); }

    void cleanupTestCase() { m_dir.reset(); }

    /// Ctrl — Command on a Mac, where Qt maps Qt::ControlModifier to it — picks
    /// files out one at a time, with no Selection mode and no requirement that
    /// they be next to each other.
    void ctrlClickTogglesOneRow()
    {
        m_panel->handleClickPress(QStringLiteral("b.txt"), Qt::ControlModifier);
        QCOMPARE(m_panel->selectionCount(), 1);

        m_panel->handleClickPress(QStringLiteral("d.txt"), Qt::ControlModifier);
        QCOMPARE(m_panel->selectionCount(), 2);

        // And takes one back off again.
        m_panel->handleClickPress(QStringLiteral("b.txt"), Qt::ControlModifier);
        QCOMPARE(m_panel->selectionCount(), 1);
        QCOMPARE(selectedNames(), QStringList{QStringLiteral("d.txt")});
    }

    /// A Mac's Ctrl+click arrives as Meta. It should toggle as well, rather
    /// than being the one modifier combination that does nothing.
    void metaClickTogglesToo()
    {
        m_panel->handleClickPress(QStringLiteral("b.txt"), Qt::MetaModifier);
        QCOMPARE(m_panel->selectionCount(), 1);
    }

    /// Shift takes everything between the last click and this one, in the order
    /// the panel is sorted rather than the order they were clicked.
    void shiftClickExtendsARange()
    {
        m_panel->handleClickPress(QStringLiteral("a.txt"), Qt::NoModifier);
        m_panel->handleClickPress(QStringLiteral("d.txt"), Qt::ShiftModifier);

        const QStringList expected{QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                   QStringLiteral("c.txt"), QStringLiteral("d.txt")};
        QCOMPARE(selectedNames(), expected);
    }

    /// The range works backwards too, which is the same gesture from below.
    void shiftClickExtendsUpwards()
    {
        m_panel->handleClickPress(QStringLiteral("d.txt"), Qt::NoModifier);
        m_panel->handleClickPress(QStringLiteral("b.txt"), Qt::ShiftModifier);

        const QStringList expected{QStringLiteral("b.txt"), QStringLiteral("c.txt"),
                                   QStringLiteral("d.txt")};
        QCOMPARE(selectedNames(), expected);
    }

    /// Shift+clicking again from the same anchor narrows as well as widens, so
    /// an overshoot is corrected in place instead of needing a fresh click.
    void shiftClickNarrowsAsWellAsWidens()
    {
        m_panel->handleClickPress(QStringLiteral("a.txt"), Qt::NoModifier);
        m_panel->handleClickPress(QStringLiteral("e.txt"), Qt::ShiftModifier);
        QCOMPARE(m_panel->selectionCount(), 5);

        m_panel->handleClickPress(QStringLiteral("c.txt"), Qt::ShiftModifier);
        QCOMPARE(m_panel->selectionCount(), 3);
    }

    /// A plain press on an already-selected row must leave the selection alone,
    /// because it may be the first half of a drag — and a drag of four files
    /// that silently became a drag of one is the bug this press/release split
    /// exists to prevent.
    void aPlainPressOnASelectedRowKeepsTheSelection()
    {
        m_panel->handleClickPress(QStringLiteral("a.txt"), Qt::NoModifier);
        m_panel->handleClickPress(QStringLiteral("c.txt"), Qt::ShiftModifier);
        QCOMPARE(m_panel->selectionCount(), 3);

        m_panel->handleClickPress(QStringLiteral("b.txt"), Qt::NoModifier);
        QCOMPARE(m_panel->selectionCount(), 3);
    }

    /// Once the button comes back up with no drag in between, the click stands
    /// and the selection it was covering for goes.
    void aCompletedPlainClickCollapsesTheSelection()
    {
        m_panel->handleClickPress(QStringLiteral("a.txt"), Qt::NoModifier);
        m_panel->handleClickPress(QStringLiteral("c.txt"), Qt::ShiftModifier);

        m_panel->handleClickPress(QStringLiteral("b.txt"), Qt::NoModifier);
        m_panel->handleClickRelease(QStringLiteral("b.txt"), Qt::NoModifier);

        QCOMPARE(m_panel->selectionCount(), 0);
    }

    /// A plain click on an unselected row starts over at once, which is what
    /// makes building a new selection feel immediate.
    void aPlainClickElsewhereClearsTheSelection()
    {
        m_panel->handleClickPress(QStringLiteral("a.txt"), Qt::ControlModifier);
        m_panel->handleClickPress(QStringLiteral("b.txt"), Qt::ControlModifier);
        QCOMPARE(m_panel->selectionCount(), 2);

        m_panel->handleClickPress(QStringLiteral("d.txt"), Qt::NoModifier);
        QCOMPARE(m_panel->selectionCount(), 0);
    }

    /// With nothing anchored yet, Shift+click starts from the cursor — so it
    /// does something sensible immediately on arriving in a directory rather
    /// than nothing at all.
    void shiftClickWithNoAnchorStartsFromTheCursor()
    {
        m_panel->setCursorName(QStringLiteral("b.txt"));
        m_panel->handleClickPress(QStringLiteral("d.txt"), Qt::ShiftModifier);

        const QStringList expected{QStringLiteral("b.txt"), QStringLiteral("c.txt"),
                                   QStringLiteral("d.txt")};
        QCOMPARE(selectedNames(), expected);
    }

    /// Leaving the directory drops the anchor with the selection. Keeping it
    /// would let a later Shift+click extend from a name that means a different
    /// file here, or no file at all.
    void navigatingAwayForgetsTheAnchor()
    {
        m_panel->handleClickPress(QStringLiteral("a.txt"), Qt::ControlModifier);
        QCOMPARE(m_panel->selectionCount(), 1);

        QTemporaryDir other;
        QVERIFY(other.isValid());
        for (const char *name : {"x.txt", "y.txt", "z.txt"}) {
            QFile file(other.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
        }

        m_panel->navigateTo(other.path());
        QTRY_COMPARE_WITH_TIMEOUT(m_panel->view()->model()->rowCount(), 3, 5000);
        QCOMPARE(m_panel->selectionCount(), 0);

        // The anchor is gone with it, so this falls back to the cursor rather
        // than reaching for "a.txt", which does not exist here.
        m_panel->setCursorName(QStringLiteral("y.txt"));
        m_panel->handleClickPress(QStringLiteral("z.txt"), Qt::ShiftModifier);

        const QStringList expected{QStringLiteral("y.txt"), QStringLiteral("z.txt")};
        QCOMPARE(selectedNames(), expected);
    }

private:
    /// The selection as names, in the panel's own order — selectedPaths() falls
    /// back to the cursor when nothing is selected, so every caller here checks
    /// selectionCount() first or expects a non-empty selection.
    QStringList selectedNames() const
    {
        QStringList names;
        for (const QString &path : m_panel->selectedPaths()) {
            names << QFileInfo(path).fileName();
        }
        return names;
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ui::FilePanel> m_panel;
};

QTEST_MAIN(TestMouseSelection)
#include "tst_mouseselection.moc"
