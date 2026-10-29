#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "config/TomlWriter.h"
#include "ui/MainWindow.h"
#include "ui/modals/SettingsWindow.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace pf;

/// The settings window, and the one invariant that would quietly ruin a config
/// file if it broke.
class TestSettings : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());

        // The window reads and writes through platform::configDir(), which
        // honours this.
        qputenv("PANEFILE_CONFIG_DIR", m_dir->path().toUtf8());

        m_window = std::make_unique<ui::MainWindow>();
        m_registry = std::make_unique<input::ActionRegistry>();
        m_keymap = std::make_unique<input::Keymap>();
        installDefaultKeymap(*m_keymap);

        m_settings = new ui::SettingsWindow(m_registry.get(), m_keymap.get(), m_window.get());
    }

    void cleanup()
    {
        m_settings = nullptr;
        m_keymap.reset();
        m_registry.reset();
        m_window.reset();
        m_dir.reset();
        qunsetenv("PANEFILE_CONFIG_DIR");
    }

    /// Opening the window must not write anything.
    ///
    /// Every control writes when it changes, and populating them is changing
    /// them. Without a guard, merely looking at the settings would author a
    /// fully-populated config.toml for someone who has never configured
    /// anything — and would rewrite the file of someone who had, replacing
    /// their carefully partial file with every key at its current value.
    void openingTheWindowWritesNothing()
    {
        const QString config = m_dir->filePath(QStringLiteral("config.toml"));
        const QString theme = m_dir->filePath(QStringLiteral("theme.toml"));
        QVERIFY(!QFile::exists(config));
        QVERIFY(!QFile::exists(theme));

        m_settings->present();
        // Opening's one piece of work off the GUI thread: once its answer is
        // shown, everything opening does has happened.
        if (const auto *status = m_settings->findChild<QLabel *>(
                QStringLiteral("settingsDefaultFileManagerStatus"))) {
            QTRY_VERIFY_WITH_TIMEOUT(!status->text().isEmpty(), 5000);
        }
        QCoreApplication::processEvents();

        QVERIFY2(!QFile::exists(config), "opening settings must not create config.toml");
        QVERIFY2(!QFile::exists(theme), "opening settings must not create theme.toml");
    }

    /// And changing a control does write, to the right key.
    void changingAControlWritesItsKey()
    {
        m_settings->present();

        // Found by the key it writes. It defaults to true, so unchecking it is
        // a real change rather than a no-op the writer would skip.
        auto *restore =
            m_settings->findChild<QCheckBox *>(QStringLiteral("setting_general_restore_session"));
        QVERIFY(restore != nullptr);
        QVERIFY(restore->isChecked());

        restore->setChecked(false);

        const QString config = m_dir->filePath(QStringLiteral("config.toml"));
        QVERIFY2(QFile::exists(config), "changing a setting should write config.toml");

        QFile file(config);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY2(text.contains(QStringLiteral("restore_session = false")), qPrintable(text));
    }

    /// The keys tab lists every registered action, so it is a way to find out
    /// what Panefile can do rather than only what it is bound to.
    void theKeysTabListsEveryAction()
    {
        m_registry->registerAction(QStringLiteral("a_test_action"), QStringLiteral("Does a thing"),
                                   input::ActionCategory::General, [] {});

        m_settings->present();

        auto *table = m_settings->findChild<QTreeWidget *>();
        QVERIFY(table != nullptr);

        bool found = false;
        for (int i = 0; i < table->topLevelItemCount(); ++i) {
            if (table->topLevelItem(i)->text(0) == QStringLiteral("a_test_action")) {
                found = true;
                QCOMPARE(table->topLevelItem(i)->text(2), QStringLiteral("Does a thing"));
            }
        }
        QVERIFY(found);
    }

    /// The theme list offers every bundled theme, which is the discovery
    /// problem this window exists to solve: there was previously no way to find
    /// out that "nord" was a valid value.
    void theThemeListOffersTheBundledThemes()
    {
        m_settings->present();

        auto *list = m_settings->findChild<QListWidget *>();
        QVERIFY(list != nullptr);
        QVERIFY2(list->count() > 5, qPrintable(QString::number(list->count())));

        QStringList names;
        for (int i = 0; i < list->count(); ++i) {
            names << list->item(i)->data(Qt::UserRole).toString();
        }
        QVERIFY2(names.contains(QStringLiteral("nord")), qPrintable(names.join(u',')));
        QVERIFY2(names.contains(QStringLiteral("macos-dark")), qPrintable(names.join(u',')));
    }

    // ======================================== the command palette's shortcut

    /// Clicks the recorder and presses a chord into it.
    QPushButton *recordChord(Qt::Key key, Qt::KeyboardModifiers modifiers)
    {
        m_registry->registerAction(QStringLiteral("open_panefile_prompt"),
                                   QStringLiteral("Open the command palette"),
                                   input::ActionCategory::General, [] {});
        m_registry->registerAction(QStringLiteral("open_fuzzy_find"), QStringLiteral("Find"),
                                   input::ActionCategory::General, [] {});
        m_settings->present();
        auto *recorder =
            m_settings->findChild<QPushButton *>(QStringLiteral("settingsKeyRecorder"));
        if (recorder != nullptr) {
            recorder->click();
            QTest::keyClick(recorder, key, modifiers);
        }
        return recorder;
    }

    QString hotkeys() const
    {
        QFile file(m_dir->filePath(QStringLiteral("hotkeys.toml")));
        return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll())
                                                                : QString();
    }

    void theRecorderShowsTheCurrentShortcut()
    {
        m_settings->present();
        auto *recorder =
            m_settings->findChild<QPushButton *>(QStringLiteral("settingsKeyRecorder"));
        QVERIFY(recorder != nullptr);
        QCOMPARE(recorder->text(), QStringLiteral("Ctrl+K"));
    }

    void recordingAChordWritesItToHotkeysToml()
    {
        QSignalSpy changed(m_settings, &ui::SettingsWindow::settingsChanged);
        QVERIFY(recordChord(Qt::Key_P, Qt::AltModifier) != nullptr);

        const QString text = hotkeys();
        QVERIFY2(text.contains(QStringLiteral("[global]")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("open_panefile_prompt = [\"Alt+P\"]")),
                 qPrintable(text));
        QVERIFY(!changed.isEmpty());
        QVERIFY(changed.last().first().toString().contains(QStringLiteral("Alt+P")));
    }

    void resetRemovesTheOverride()
    {
        recordChord(Qt::Key_P, Qt::AltModifier);
        QVERIFY(hotkeys().contains(QStringLiteral("Alt+P")));

        auto *reset = m_settings->findChild<QPushButton *>(QStringLiteral("settingsKeyReset"));
        QVERIFY(reset != nullptr);
        reset->click();
        QVERIFY2(!hotkeys().contains(QStringLiteral("open_panefile_prompt")),
                 qPrintable(hotkeys()));
        auto *recorder =
            m_settings->findChild<QPushButton *>(QStringLiteral("settingsKeyRecorder"));
        QCOMPARE(recorder->text(), QStringLiteral("Ctrl+K"));
    }

    /// A bare letter would hijack typing, so it is refused and nothing is
    /// written.
    void aBareLetterIsRejected()
    {
        auto *recorder = recordChord(Qt::Key_P, Qt::NoModifier);
        QVERIFY(recorder != nullptr);
        QVERIFY(hotkeys().isEmpty());
        auto *problem = m_settings->findChild<QLabel *>(QStringLiteral("modalProblem"));
        QVERIFY(problem != nullptr);
        QVERIFY(!problem->isHidden());
    }

    void escapeCancelsRecording()
    {
        auto *recorder = recordChord(Qt::Key_Escape, Qt::NoModifier);
        QVERIFY(recorder != nullptr);
        QVERIFY(hotkeys().isEmpty());
        QCOMPARE(recorder->text(), QStringLiteral("Ctrl+K"));
    }

    /// While recording, the key presses are marked as the recorder's own so the
    /// application's dispatcher leaves them alone.
    void recordingMarksTheRecorderAsCapturing()
    {
        m_settings->present();
        auto *recorder =
            m_settings->findChild<QPushButton *>(QStringLiteral("settingsKeyRecorder"));
        QVERIFY(recorder != nullptr);
        QVERIFY(!recorder->property("pfCapturesKeys").toBool());
        recorder->click();
        QVERIFY(recorder->property("pfCapturesKeys").toBool());
        QTest::keyClick(recorder, Qt::Key_Escape);
        QVERIFY(!recorder->property("pfCapturesKeys").toBool());
    }

    /// A chord another action holds asks first, and Replace takes it from that
    /// action too.
    void aChordHeldElsewhereAsksBeforeReplacing()
    {
        // Ctrl+F is open_fuzzy_find by default.
        QVERIFY(recordChord(Qt::Key_F, Qt::ControlModifier) != nullptr);
        QVERIFY2(hotkeys().isEmpty(), "nothing is written until the conflict is resolved");

        auto *bar = m_settings->findChild<QWidget *>(QStringLiteral("settingsKeyConflict"));
        QVERIFY(bar != nullptr);
        QVERIFY(!bar->isHidden());
        auto *text = m_settings->findChild<QLabel *>(QStringLiteral("settingsKeyConflictText"));
        QVERIFY2(text->text().contains(QStringLiteral("open_fuzzy_find")),
                 qPrintable(text->text()));

        m_settings->findChild<QPushButton *>(QStringLiteral("settingsKeyConflictReplace"))->click();
        const QString written = hotkeys();
        QVERIFY2(written.contains(QStringLiteral("open_panefile_prompt = [\"Ctrl+F\"]")),
                 qPrintable(written));
        QVERIFY2(written.contains(QStringLiteral("open_fuzzy_find = []")), qPrintable(written));
    }

    void cancellingAConflictWritesNothing()
    {
        QVERIFY(recordChord(Qt::Key_F, Qt::ControlModifier) != nullptr);
        m_settings->findChild<QPushButton *>(QStringLiteral("settingsKeyConflictCancel"))->click();
        QVERIFY(hotkeys().isEmpty());
        QVERIFY(
            m_settings->findChild<QWidget *>(QStringLiteral("settingsKeyConflict"))->isHidden());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ui::MainWindow> m_window;
    std::unique_ptr<input::ActionRegistry> m_registry;
    std::unique_ptr<input::Keymap> m_keymap;
    ui::SettingsWindow *m_settings = nullptr;
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
