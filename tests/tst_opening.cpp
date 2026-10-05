// Enter and double-click on a file open it with the desktop's default
// application.
//
// An application-level test, not a panel one, because the bug it guards was
// between the two: FilePanel emitted fileActivated, a panel test asserted that
// it did, and nothing in the application was connected to it — so on a real
// desktop Enter and double-click on a file did nothing at all.
//
// It also drives the `o` sort menu, which broke in the same gap: the menu
// works on its own, but the application's key dispatcher took its keys.
//
// xdg-open is replaced, through PATH, by a script that writes down what it was
// asked to open, so the test launches nothing.

#include "app/Application.h"
#include "app/CommandLine.h"
#include "model/FilterSortProxy.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace pf;

class TestOpening : public QObject
{
    Q_OBJECT

public:
    TestOpening(Application *app, QString root, QString record)
        : m_app(app), m_root(std::move(root)), m_record(std::move(record))
    {}

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(m_root));
        QFile video(m_root + QStringLiteral("/clip.mp4"));
        QVERIFY(video.open(QIODevice::WriteOnly));
        video.write("not really a video");
        video.close();

        CommandLineOptions options;
        options.paths = {m_root};
        options.newInstance = true;
        m_app->startUp(options);

        QTRY_VERIFY((m_panel = m_app->mainWindow()->panelStrip()->focusedPanel()) != nullptr);
        QTRY_COMPARE(m_panel->path(), m_root);
        m_panel->setCursorName(QStringLiteral("clip.mp4"));
        QTRY_COMPARE(m_panel->cursorName(), QStringLiteral("clip.mp4"));
    }

    void returnOpensTheFile()
    {
        QFile::remove(m_record);
        m_panel->view()->setFocus();
        QTest::keyClick(m_panel->view(), Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_record), 5000);
        QCOMPARE(recorded(), m_root + QStringLiteral("/clip.mp4"));
        // Opening a file is not navigating.
        QCOMPARE(m_panel->path(), m_root);
    }

    void doubleClickOpensTheFile()
    {
        QFile::remove(m_record);
        const QModelIndex index = m_panel->view()->currentIndex();
        const QPoint centre = m_panel->view()->visualRect(index).center();
        QTest::mouseDClick(m_panel->view()->viewport(), Qt::LeftButton, Qt::NoModifier, centre);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_record), 5000);
        QCOMPARE(recorded(), m_root + QStringLiteral("/clip.mp4"));
    }

    // The `o` menu is opened from the keyboard, so it has to be usable from
    // it: arrows move, Return chooses, Escape dismisses. The dispatcher sees
    // every key first, and must leave an open popup's keys alone.
    void sortMenuIsDrivenByTheKeyboard()
    {
        QCOMPARE(m_panel->sortKey(), SortKey::Name);

        bool sawMenu = false;
        QTimer::singleShot(100, this, [&sawMenu] {
            QWidget *popup = QApplication::activePopupWidget();
            sawMenu = qobject_cast<QMenu *>(popup) != nullptr;
            if (popup != nullptr) {
                // Name is current, so one step down is Size.
                QTest::keyClick(popup, Qt::Key_Down);
                QTest::keyClick(popup, Qt::Key_Return);
                // A menu the keys never reached would block exec() forever;
                // close it so the test fails rather than hangs.
                if (QApplication::activePopupWidget() == popup) {
                    popup->close();
                }
            }
        });
        m_panel->view()->setFocus();
        QTest::keyClick(m_panel->view(), Qt::Key_O, Qt::NoModifier);

        QVERIFY(sawMenu);
        QCOMPARE(m_panel->sortKey(), SortKey::Size);

        bool escapeClosed = false;
        QTimer::singleShot(100, this, [&escapeClosed] {
            if (QWidget *popup = QApplication::activePopupWidget(); popup != nullptr) {
                QTest::keyClick(popup, Qt::Key_Escape);
                escapeClosed = QApplication::activePopupWidget() != popup;
                if (!escapeClosed) {
                    popup->close();
                }
            }
        });
        QTest::keyClick(m_panel->view(), Qt::Key_O, Qt::NoModifier);

        QVERIFY(escapeClosed);
        QCOMPARE(m_panel->sortKey(), SortKey::Size);
    }

private:
    QString recorded() const
    {
        QFile file(m_record);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll()).trimmed();
    }

    Application *m_app;
    QString m_root;
    QString m_record;
    ui::FilePanel *m_panel = nullptr;
};

int main(int argc, char **argv)
{
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) {
        return 1;
    }
    for (const char *variable : {"PANEFILE_CONFIG_DIR", "PANEFILE_STATE_DIR", "PANEFILE_CACHE_DIR",
                                 "PANEFILE_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME"}) {
        qputenv(variable, QFile::encodeName(sandbox.path() + QLatin1Char('/') +
                                            QString::fromLatin1(variable).toLower()));
        QDir().mkpath(qEnvironmentVariable(variable));
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // A stand-in xdg-open, first on PATH, that records its argument.
    const QString bin = sandbox.path() + QStringLiteral("/bin");
    const QString record = sandbox.path() + QStringLiteral("/opened.txt");
    QDir().mkpath(bin);
    QFile script(bin + QStringLiteral("/xdg-open"));
    if (!script.open(QIODevice::WriteOnly)) {
        return 1;
    }
    script.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$1\" > '%1.tmp' && mv '%1.tmp' '%1'\n")
                     .arg(record)
                     .toUtf8());
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QByteArray path = QFile::encodeName(bin) + ':' + qgetenv("PATH");
    qputenv("PATH", path);

    const QString root =
        QFileInfo(sandbox.path()).canonicalFilePath() + QStringLiteral("/fixtures");

    Application app(argc, argv);
    TestOpening test(&app, root, record);
    return QTest::qExec(&test, argc, argv);
}

#include "tst_opening.moc"
