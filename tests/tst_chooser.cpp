// The file dialog: Application in chooser mode with an injected request.

#include "input/ActionRegistry.h"
#include "app/Application.h"
#include "config/StyleSheetBuilder.h"
#include "app/ChooserController.h"
#include "platform/FileChooserRequest.h"
#include "ui/ChooserBar.h"
#include "ui/ElidingComboBox.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"
#include "ui/ThemePalette.h"
#include "ui/Sidebar.h"
#include "ui/modals/ConfirmModal.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using Mode = platform::FileChooserRequest::Mode;

namespace {

void touch(const QString &path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
}

} // namespace

class TestChooser : public QObject
{
    Q_OBJECT

public:
    TestChooser(Application *app) : m_app(app) {}

private:
    Application *m_app;
    QTemporaryDir m_state;
    QTemporaryDir m_root;

    // The one dialog these tests have open at a time.
    struct Outcome {
        bool accepted = false;
        platform::FileChooserResult result;
        int count = 0;
    };
    Outcome m_outcome;

    ui::FilePanel *panel() const { return m_app->mainWindow()->panelStrip()->focusedPanel(); }

    /// Opens a dialog for `request` in the fixture folder.
    void open(platform::FileChooserRequest request)
    {
        request.currentFolder = m_root.path();
        m_outcome = {};
        m_app->startUpChooser(request);
        connect(m_app->chooser(), &ChooserController::finished, this,
                [this](bool accepted, const platform::FileChooserResult &result) {
                    m_outcome.accepted = accepted;
                    m_outcome.result = result;
                    ++m_outcome.count;
                });
        QTRY_VERIFY(!panel()->isLoading());
    }

    /// Shows the window at `width` wide, so the bar lays out at a real size.
    void showAt(int width)
    {
        ui::MainWindow *window = m_app->mainWindow();
        window->resize(width, 640);
        if (!window->isVisible()) {
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
        }
        QCoreApplication::processEvents();
    }

    QString path(const QString &name) const { return m_root.filePath(name); }

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("PANEFILE_STATE_DIR", m_state.path().toUtf8());
        QDir(m_root.path()).mkdir("Docs");
        touch(path("a.pdf"));
        touch(path("b.PDF"));
        touch(path("c.txt"));
        touch(path("existing.txt"));
    }

    void openReturnsTheFileOnEnterAndOpensFoldersInstead()
    {
        open({});
        QCOMPARE(m_app->mainWindow()->panelStrip()->count(), 1);

        QVERIFY(m_app->chooser()->claimActivation(path("a.pdf")));
        QCOMPARE(m_outcome.count, 1);
        QVERIFY(m_outcome.accepted);
        QCOMPARE(m_outcome.result.paths, QStringList{path("a.pdf")});
    }

    void openOnAFolderEntersItAndCancelReturnsNothing()
    {
        open({});
        panel()->setCursorName("Docs");
        m_app->chooser()->accept();
        QTRY_COMPARE(panel()->path(), path("Docs"));
        QCOMPARE(m_outcome.count, 0);

        m_app->chooser()->cancel();
        QVERIFY(!m_outcome.accepted);
        QCOMPARE(m_outcome.count, 1);
    }

    void multipleReturnsTheSelection()
    {
        platform::FileChooserRequest request;
        request.multiple = true;
        open(request);
        panel()->selectNames({"a.pdf", "c.txt"});
        QTRY_COMPARE(panel()->selectionCount(), 2);

        m_app->chooser()->accept();
        QCOMPARE(m_outcome.result.paths, (QStringList{path("a.pdf"), path("c.txt")}));
    }

    void folderModeReturnsTheCurrentFolderAndNamesIt()
    {
        platform::FileChooserRequest request;
        request.directory = true;
        open(request);
        QTRY_COMPARE(m_app->chooser()->bar()->acceptLabel(),
                     QStringLiteral("Select “%1”").arg(QFileInfo(m_root.path()).fileName()));

        m_app->chooser()->accept();
        QCOMPARE(m_outcome.result.paths, QStringList{QDir::cleanPath(m_root.path())});
    }

    void filtersHideFilesButNotFoldersAndTheChosenOneIsReturned()
    {
        platform::FileChooserRequest request;
        request.filters = {{"All", {}}, {"PDF", {{0, "*.pdf"}}}};
        request.currentFilter = 1;
        request.choices = {{"enc", "Encoding", {{"u", "UTF-8"}, {"l", "Latin-1"}}, "l"},
                           {"flag", "Flag", {}, "true"}};
        open(request);

        QCOMPARE(panel()->headerCountText(), QStringLiteral("3 of 5"));

        QVERIFY(m_app->chooser()->claimActivation(path("a.pdf")));
        QCOMPARE(m_outcome.result.filterIndex, 1);
        QCOMPARE(m_outcome.result.choices,
                 (QList<QPair<QString, QString>>{{"enc", "l"}, {"flag", "true"}}));
    }

    void saveReturnsFolderPlusNameAndSelectsTheStem()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Save;
        request.currentName = "report.final.pdf";
        open(request);

        ui::ChooserBar *bar = m_app->chooser()->bar();
        QCOMPARE(bar->acceptLabel(), QStringLiteral("Save"));
        QCOMPARE(bar->nameField()->selectedText(), QStringLiteral("report.final"));

        m_app->chooser()->accept();
        QCOMPARE(m_outcome.result.paths, QStringList{path("report.final.pdf")});
    }

    void savingOverAnExistingFileAsksFirst()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Save;
        request.currentName = "existing.txt";
        open(request);

        m_app->chooser()->accept();
        QCOMPARE(m_outcome.count, 0);
        auto *modal = m_app->mainWindow()->findChild<ui::ConfirmModal *>();
        QVERIFY(modal != nullptr);
        QVERIFY(modal->isVisible());

        modal->dismiss();
        QCOMPARE(m_outcome.count, 0);

        m_app->chooser()->accept();
        modal->findChild<QPushButton *>(QStringLiteral("destructiveButton"))->click();
        QCOMPARE(m_outcome.result.paths, QStringList{path("existing.txt")});
    }

    void badNamesDisableSaveWithAReason()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Save;
        request.currentName = "x.txt";
        open(request);
        ui::ChooserBar *bar = m_app->chooser()->bar();

        bar->setName("");
        QVERIFY(!bar->isAcceptEnabled());
        bar->setName("..");
        QVERIFY(!bar->isAcceptEnabled());
        bar->setName("missing-folder/x.txt");
        QVERIFY(!bar->isAcceptEnabled());
        bar->setName("fine.txt");
        QVERIFY(bar->isAcceptEnabled());

        bar->setName("..");
        m_app->chooser()->accept();
        QCOMPARE(m_outcome.count, 0);
    }

    void saveFilesChecksEachNameInTheFolder()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::SaveFiles;
        request.files = {"new.txt", "existing.txt"};
        open(request);

        m_app->chooser()->accept();
        QCOMPARE(m_outcome.count, 0);
        auto *modal = m_app->mainWindow()->findChild<ui::ConfirmModal *>();
        QVERIFY(modal->isVisible());
        modal->findChild<QPushButton *>(QStringLiteral("destructiveButton"))->click();
        QCOMPARE(m_outcome.result.paths, (QStringList{path("new.txt"), path("existing.txt")}));
    }

    void saveFilesNamesTheCountAndFolderOnTheButtonAndInTheReplaceCard()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::SaveFiles;
        request.files = {"existing.txt", "n1.txt", "n2.txt"};
        open(request);
        const QString root = QFileInfo(m_root.path()).fileName();
        ui::ChooserBar *bar = m_app->chooser()->bar();
        QCOMPARE(bar->acceptLabel(), QStringLiteral("Save 3 files in “%1”").arg(root));

        panel()->navigateTo(path("Docs"));
        QTRY_COMPARE(bar->acceptLabel(), QStringLiteral("Save 3 files in “Docs”"));
        panel()->navigateTo(m_root.path());
        QTRY_COMPARE(bar->acceptLabel(), QStringLiteral("Save 3 files in “%1”").arg(root));

        m_app->chooser()->accept();
        auto *modal = m_app->mainWindow()->findChild<ui::ConfirmModal *>();
        QCOMPARE(modal->findChild<QLabel *>(QStringLiteral("modalTitle"))->text(),
                 QStringLiteral("Replace 1 file in “%1”?").arg(root));
        QVERIFY(modal->findChild<QLabel *>(QStringLiteral("modalMessage"))
                    ->text()
                    .endsWith(QStringLiteral("Replacing it overwrites what is there.")));
        modal->dismiss();
        m_app->chooser()->cancel();

        touch(path("n1.txt"));
        open(request);
        m_app->chooser()->accept();
        modal = m_app->mainWindow()->findChild<ui::ConfirmModal *>();
        QVERIFY(modal->findChild<QLabel *>(QStringLiteral("modalMessage"))
                    ->text()
                    .endsWith(QStringLiteral("Replacing them overwrites what is there.")));
        modal->dismiss();
        QFile::remove(path("n1.txt"));
    }

    void saveFilesKeepsTheAppsOwnAcceptLabel()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::SaveFiles;
        request.files = {"n1.txt"};
        request.acceptLabel = "_Export";
        open(request);
        QCOMPARE(m_app->chooser()->bar()->acceptLabel(), QStringLiteral("Export"));
    }

    void saveFilesRefusesDotNamesAndEmptyNames()
    {
        for (const QStringList &files : {QStringList{"ok.txt", ".."}, QStringList{"dir/"}}) {
            platform::FileChooserRequest request;
            request.mode = Mode::SaveFiles;
            request.files = files;
            open(request);

            QVERIFY(!m_app->chooser()->bar()->isAcceptEnabled());
            m_app->chooser()->accept();
            QCOMPARE(m_outcome.count, 0);
            QVERIFY(m_app->mainWindow()
                        ->findChild<QLabel *>(QStringLiteral("footer"))
                        ->text()
                        .contains(QStringLiteral("Cancel and try again from the app")));
            m_app->chooser()->cancel();
        }
    }

    void appSuppliedNameIsANameNotAPath()
    {
        const QString spaces(60, QChar(0x2003));
        for (const QString &sent : QStringList{QStringLiteral("a") + spaces + "/../../escape.txt",
                                    QStringLiteral("/etc/escape.txt"),
                                    QStringLiteral("~/escape.txt")}) {
            platform::FileChooserRequest request;
            request.mode = Mode::Save;
            request.currentName = sent;
            open(request);
            QCOMPARE(m_app->chooser()->bar()->name(), QStringLiteral("escape.txt"));
            m_app->chooser()->accept();
            QCOMPARE(m_outcome.result.paths, QStringList{path("escape.txt")});
        }
        for (const QString &sent : {QStringLiteral(".."), QStringLiteral("~")}) {
            platform::FileChooserRequest request;
            request.mode = Mode::Save;
            request.currentName = sent;
            open(request);
            QVERIFY(m_app->chooser()->bar()->name().isEmpty());
            QVERIFY(!m_app->chooser()->bar()->isAcceptEnabled());
            m_app->chooser()->cancel();
        }
    }

    void choiceLabelsArePlainText()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Save;
        request.choices = {{"x", "<b>x</b>", {{"a", "A"}}, "a"}};
        open(request);
        QLabel *label = nullptr;
        for (QLabel *candidate : m_app->chooser()->bar()->findChildren<QLabel *>()) {
            if (candidate->text() == QLatin1String("<b>x</b>")) {
                label = candidate;
            }
        }
        QVERIFY(label != nullptr);
        QCOMPARE(label->textFormat(), Qt::PlainText);
    }

    void appStringsInToolTipsAreEscaped()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Open;
        request.acceptLabel = "<b>Safe</b>" + QString(60, QLatin1Char('x'));
        request.filters = {{"<b>x</b>", {{0, "*.txt"}}}};
        open(request);
        auto *bar = m_app->chooser()->bar();
        auto *type = bar->findChild<QComboBox *>(QStringLiteral("chooserType"));
        auto *accept = bar->findChild<QPushButton *>(QStringLiteral("chooserAccept"));
        QVERIFY(type != nullptr && accept != nullptr);
        QVERIFY(type->itemData(0, Qt::ToolTipRole).toString().contains(QStringLiteral("&lt;b&gt;")));
        QVERIFY(accept->toolTip().contains(QStringLiteral("&lt;b&gt;")));
        m_app->chooser()->cancel();
    }

    void appOptionsShareTheRowWhenThereIsRoom()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Save;
        request.currentName = "x.txt";
        request.filters = {{"All files", {{0, "*"}}}};
        request.choices = {
            {"enc", "Character encoding", {{"u", "Unicode (UTF-8)"}, {"l", "Western (ISO-8859-1)"}}, "u"},
            {"eol", "Line ending", {{"u", "Unix/Linux"}, {"w", "Windows"}}, "u"}};
        open(request);
        ui::ChooserBar *bar = m_app->chooser()->bar();
        auto rowOf = [bar](QWidget *w) { return w->mapTo(bar, QPoint()).y(); };
        auto combos = bar->findChildren<QComboBox *>();
        auto *type = bar->findChild<QComboBox *>(QStringLiteral("chooserType"));
        QComboBox *encoding = combos.at(0) == type ? combos.at(1) : combos.at(0);
        QComboBox *eol = nullptr;
        for (auto *combo : combos) {
            if (combo != type && combo != encoding) {
                eol = combo;
            }
        }
        QVERIFY(type != nullptr && eol != nullptr);

        showAt(1000);
        QCOMPARE(rowOf(encoding), rowOf(type));
        QLabel *encodingLabel = nullptr;
        for (auto *label : bar->findChildren<QLabel *>()) {
            if (label->buddy() == encoding) {
                encodingLabel = label;
            }
        }
        QVERIFY(encodingLabel != nullptr);
        auto *choices = bar->findChild<QWidget *>(QStringLiteral("chooserChoices"));
        for (int width : {700, 820}) {
            showAt(width);
            QVERIFY(rowOf(encoding) < rowOf(type));
            QVERIFY(encoding->geometry().left() - encodingLabel->geometry().right() <= 9);
            QVERIFY(eol->width() <= eol->sizeHint().width());
            QVERIFY(choices->width() <= choices->sizeHint().width());
        }
        showAt(1000);
        QCOMPARE(rowOf(encoding), rowOf(type));
        QVERIFY(eol->sizeHint().width() < encoding->sizeHint().width());
        m_app->chooser()->cancel();
    }

    void longTypeNamesShowWhenThereIsRoomAndElideWhenNot()
    {
        platform::FileChooserRequest request;
        request.filters = {{"Markdown documents (*.md, *.markdown)", {{0, "*.md"}}}};
        open(request);
        showAt(1000);
        auto *type = m_app->chooser()->bar()->findChild<ui::ElidingComboBox *>(
            QStringLiteral("chooserType"));
        QVERIFY(type != nullptr);
        QCOMPARE(type->displayText(), QStringLiteral("Markdown documents (*.md, *.markdown)"));
        type->resize(130, type->height());
        QVERIFY(type->displayText().endsWith(QChar(0x2026)));
        m_app->chooser()->cancel();
    }

    void acceptLabelIsMeasuredInItsOwnFont()
    {
        const QString folder = QStringLiteral(
            "Supplier agreements and statements of work for the 2026 financial year");
        QDir(m_root.path()).mkdir(folder);
        qApp->setStyleSheet(config::buildStyleSheet(ui::currentPalette()));
        platform::FileChooserRequest request;
        request.directory = true;
        open(request);
        panel()->navigateTo(path(folder));
        auto *accept = m_app->chooser()->bar()->findChild<QPushButton *>(
            QStringLiteral("chooserAccept"));
        QVERIFY(accept != nullptr);
        QTRY_VERIFY(accept->text().contains(QChar(0x2026)));
        showAt(1000);
        QFont semibold = accept->font();
        semibold.setWeight(QFont::DemiBold);
        QVERIFY(QFontMetrics(semibold).horizontalAdvance(accept->text()) <= 260);
        QTRY_VERIFY(accept->width() >= accept->sizeHint().width());
        m_app->chooser()->cancel();
        qApp->setStyleSheet({});
    }

    void aMimeFilterNobodyKnowsListsNoFiles()
    {
        platform::FileChooserRequest request;
        request.filters = {{"X", {{1, "application/x-does-not-exist"}}}};
        open(request);
        QTRY_COMPARE(panel()->headerCountText(), QStringLiteral("1 of 5"));
    }

    void longFilterNamesAndManyChoicesFitAHalfTile()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Save;
        request.currentName = "x.md";
        request.filters = {{"Markdown documents (*.md, *.markdown)", {{0, "*.md"}}}};
        request.choices = {{"a", "Encoding", {{"u", "Unicode (UTF-8)"}, {"l", "Latin-1"}}, "u"},
                           {"b", "Line endings", {{"u", "Unix (LF)"}, {"w", "Windows"}}, "u"},
                           {"c", "Compression", {{"n", "None"}, {"z", "Gzip"}}, "n"},
                           {"bom", "Add byte order mark", {}, "false"}};
        open(request);
        showAt(900);
        ui::ChooserBar *bar = m_app->chooser()->bar();
        QVERIFY2(bar->minimumSizeHint().width() <= 900,
                 qPrintable(QString::number(bar->minimumSizeHint().width())));
        QVERIFY(m_app->mainWindow()->minimumSizeHint().width() <= 900);
        m_app->chooser()->cancel();
    }

    void escapeCancelsAndTabMovesBetweenListAndName()
    {
        platform::FileChooserRequest request;
        request.mode = Mode::Save;
        request.currentName = "x.txt";
        open(request);
        QLineEdit *name = m_app->chooser()->bar()->nameField();
        QTRY_VERIFY(name->hasFocus());

        QTest::keyClick(name, Qt::Key_Tab);
        QTRY_VERIFY(panel()->view()->hasFocus());
        QTest::keyClick(panel()->view(), Qt::Key_Tab);
        QTRY_VERIFY(name->hasFocus());
        QTest::keyClick(name, Qt::Key_Down);
        QTRY_VERIFY(panel()->view()->hasFocus());
        QTest::keyClick(panel()->view(), Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(name->hasFocus());

        QTest::keyClick(name, Qt::Key_Escape);
        QVERIFY(!m_outcome.accepted);
        QCOMPARE(m_outcome.count, 1);
    }

    void thereIsExactlyOnePanelAndNoPanelActions()
    {
        open({});
        ui::PanelStrip *strip = m_app->mainWindow()->panelStrip();
        QTest::keyClick(panel()->view(), Qt::Key_T, Qt::ControlModifier);
        QTest::keyClick(panel()->view(), Qt::Key_Backslash, Qt::ControlModifier);
        QTest::keyClick(panel()->view(), Qt::Key_W, Qt::ControlModifier);
        QCOMPARE(strip->count(), 1);

        for (const char *id : {"create_new_file_panel", "split_file_panel", "close_file_panel",
                               "next_file_panel", "previous_file_panel"}) {
            QVERIFY(!m_app->registry()->contains(QLatin1String(id)));
        }

        // A sidebar place navigates the one list.
        m_app->mainWindow()->sidebar()->placeActivated(path("Docs"));
        QTRY_COMPARE(panel()->path(), path("Docs"));
        QCOMPARE(strip->count(), 1);
    }

    void closingTheWindowCancels()
    {
        open({});
        m_app->mainWindow()->close();
        QVERIFY(!m_outcome.accepted);
        QCOMPARE(m_outcome.count, 1);
    }

    void theSessionIsNeverWritten()
    {
        open({});
        m_app->chooser()->cancel();
        QVERIFY(!QFile::exists(m_state.filePath("session.json")));
        QVERIFY(!QFile::exists(m_state.filePath("session.toml")));
    }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    Application app(argc, argv);
    TestChooser test(&app);
    return QTest::qExec(&test, argc, argv);
}

#include "tst_chooser.moc"
