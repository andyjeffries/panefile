#include "input/ActionRegistry.h"
#include "input/DefaultKeymap.h"
#include "input/Keymap.h"
#include "config/TomlWriter.h"
#include "ui/MainWindow.h"
#include "ui/modals/SettingsWindow.h"

#include <QCheckBox>
#include <QFile>
#include <QListWidget>
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
        QTest::qWait(50);

        QVERIFY2(!QFile::exists(config), "opening settings must not create config.toml");
        QVERIFY2(!QFile::exists(theme), "opening settings must not create theme.toml");
    }

    /// And changing a control does write, to the right key.
    void changingAControlWritesItsKey()
    {
        m_settings->present();
        QTest::qWait(50);

        // Found by the key it writes. It defaults to true, so unchecking it is
        // a real change rather than a no-op the writer would skip.
        auto *restore =
            m_settings->findChild<QCheckBox *>(QStringLiteral("setting_general_restore_session"));
        QVERIFY(restore != nullptr);
        QVERIFY(restore->isChecked());

        restore->setChecked(false);
        QTest::qWait(50);

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
        QTest::qWait(50);

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
        QTest::qWait(50);

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

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ui::MainWindow> m_window;
    std::unique_ptr<input::ActionRegistry> m_registry;
    std::unique_ptr<input::Keymap> m_keymap;
    ui::SettingsWindow *m_settings = nullptr;
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
