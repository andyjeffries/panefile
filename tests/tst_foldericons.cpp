#include "model/DirectoryModel.h"
#include "model/FolderIconStore.h"
#include "model/StandardFolders.h"
#include "ui/ThemePalette.h"
#include "ui/modals/FolderIconModal.h"

#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>

using namespace pf;

/// The folder icon store, the model's glyph role, and the picker.
class TestFolderIcons : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_state;

private Q_SLOTS:
    void initTestCase()
    {
        // Never the developer's real state directory.
        qputenv("PANEFILE_STATE_DIR", m_state.path().toUtf8());
    }

    void init() { FolderIconStore::instance().reset(); }

    void storeRoundTrips()
    {
        const QHash<QString, QString> icons{
            {QStringLiteral("/a/b"), QStringLiteral("hero:rocket-launch")},
            {QStringLiteral("/c d/e"), QStringLiteral("hero:home")}};
        QCOMPARE(FolderIconStore::fromJson(FolderIconStore::toJson(icons)), icons);
    }

    void fromJsonSkipsInvalidEntries()
    {
        const auto icons = FolderIconStore::fromJson(
            R"({"/ok":"hero:home","/bad":"hero:../x","/num":3,"":"hero:home"})");
        QCOMPARE(icons.size(), 1);
        QVERIFY(icons.contains(QStringLiteral("/ok")));
    }

    void setPersistsAndClearForgets()
    {
        auto &store = FolderIconStore::instance();
        QSignalSpy spy(&store, &FolderIconStore::changed);
        store.set(QStringLiteral("/tmp/x/folder"), QStringLiteral("hero:rocket-launch"));
        QCOMPARE(store.glyphFor(QStringLiteral("/tmp/x/folder")),
                 QStringLiteral("hero:rocket-launch"));
        QCOMPARE(spy.count(), 1);

        QFile file(m_state.path() + QStringLiteral("/folder-icons.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("rocket-launch"));

        store.clear(QStringLiteral("/tmp/x/folder"));
        QVERIFY(store.glyphFor(QStringLiteral("/tmp/x/folder")).isEmpty());
        QCOMPARE(spy.count(), 2);
    }

    void aMissingGlyphIsNotStored()
    {
        auto &store = FolderIconStore::instance();
        store.set(QStringLiteral("/tmp/x/folder"), QStringLiteral("hero:no-such-icon-at-all"));
        QVERIFY(store.glyphFor(QStringLiteral("/tmp/x/folder")).isEmpty());
        store.set(QStringLiteral("/tmp/x/folder"), QStringLiteral("hero:../../x"));
        QVERIFY(store.glyphFor(QStringLiteral("/tmp/x/folder")).isEmpty());
    }

    void customChoiceBeatsTheStandardFolder()
    {
        auto &store = FolderIconStore::instance();
        const QString home = QDir::cleanPath(QDir::homePath());
        QCOMPARE(store.effectiveGlyph(home), QStringLiteral("hero:home"));
        store.set(home, QStringLiteral("hero:star"));
        QCOMPARE(store.effectiveGlyph(home), QStringLiteral("hero:star"));
        store.clear(home);
        QCOMPARE(store.effectiveGlyph(home), QStringLiteral("hero:home"));
    }

    void renamedRewritesDescendants()
    {
        auto &store = FolderIconStore::instance();
        store.set(QStringLiteral("/w/old"), QStringLiteral("hero:star"));
        store.set(QStringLiteral("/w/old/inner"), QStringLiteral("hero:film"));
        store.set(QStringLiteral("/w/older"), QStringLiteral("hero:photo"));
        store.renamed(QStringLiteral("/w/old"), QStringLiteral("/w/new"));

        QVERIFY(store.glyphFor(QStringLiteral("/w/old")).isEmpty());
        QVERIFY(store.glyphFor(QStringLiteral("/w/old/inner")).isEmpty());
        QCOMPARE(store.glyphFor(QStringLiteral("/w/new")), QStringLiteral("hero:star"));
        QCOMPARE(store.glyphFor(QStringLiteral("/w/new/inner")), QStringLiteral("hero:film"));
        // A sibling that merely shares a prefix is not a descendant.
        QCOMPARE(store.glyphFor(QStringLiteral("/w/older")), QStringLiteral("hero:photo"));
    }

    void removedDropsDescendants()
    {
        auto &store = FolderIconStore::instance();
        store.set(QStringLiteral("/w/gone"), QStringLiteral("hero:star"));
        store.set(QStringLiteral("/w/gone/inner"), QStringLiteral("hero:film"));
        store.set(QStringLiteral("/w/kept"), QStringLiteral("hero:photo"));
        store.removed(QStringLiteral("/w/gone"));
        QVERIFY(store.glyphFor(QStringLiteral("/w/gone")).isEmpty());
        QVERIFY(store.glyphFor(QStringLiteral("/w/gone/inner")).isEmpty());
        QVERIFY(!store.glyphFor(QStringLiteral("/w/kept")).isEmpty());
    }

    void changedRepaintsTheRightRow()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("a")));
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("b")));

        DirectoryModel model;
        model.setPath(dir.path());
        QTRY_COMPARE(model.rowCount(), 2);
        QVERIFY(!model.isScanning());

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        const QString path = QDir::cleanPath(dir.path() + QStringLiteral("/b"));
        FolderIconStore::instance().set(path, QStringLiteral("hero:star"));

        QCOMPARE(spy.count(), 1);
        const int row = model.indexOfName(QStringLiteral("b"));
        QCOMPARE(spy.first().at(0).toModelIndex().row(), row);
        QCOMPARE(model.index(row).data(DirectoryModel::FolderGlyphRole).toString(),
                 QStringLiteral("hero:star"));
        QVERIFY(model.index(model.indexOfName(QStringLiteral("a")))
                    .data(DirectoryModel::FolderGlyphRole)
                    .toString()
                    .isEmpty());
    }

    void emptyFoldersAreFoundInTheBackground()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("empty")));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("full/inner")));

        DirectoryModel model;
        model.setPath(dir.path());
        QTRY_COMPARE(model.rowCount(), 2);

        const QModelIndex empty = model.index(model.indexOfName(QStringLiteral("empty")));
        const QModelIndex full = model.index(model.indexOfName(QStringLiteral("full")));
        // Drawn open until the answer arrives.
        QVERIFY(!empty.data(DirectoryModel::FolderEmptyRole).toBool());
        QVERIFY(!full.data(DirectoryModel::FolderEmptyRole).toBool());
        QTRY_VERIFY(empty.data(DirectoryModel::FolderEmptyRole).toBool());
        QTRY_VERIFY(!model.index(model.indexOfName(QStringLiteral("full")))
                         .data(DirectoryModel::FolderEmptyRole)
                         .toBool());
    }

    void pickerFiltersAndApplies()
    {
        QWidget host;
        host.resize(1000, 800);
        ui::FolderIconModal modal(&host);
        host.show();
        modal.open(QDir::homePath(), {});

        // Default and the nine presets, then (when installed) the whole set.
        QStringList glyphs = modal.visibleGlyphs();
        QVERIFY(glyphs.size() >= 10);
        QVERIFY(glyphs.first().isEmpty());

        const bool installed = !FolderIconStore::heroiconsDirectory().isEmpty();
        if (!installed) {
            QSKIP("Heroicons set is not staged");
        }
        QVERIFY(glyphs.size() > 300);

        modal.setQuery(QStringLiteral("arrow d"));
        glyphs = modal.visibleGlyphs();
        QVERIFY(glyphs.contains(QStringLiteral("hero:arrow-down-tray")));
        QVERIFY(!glyphs.contains(QStringLiteral("hero:rocket-launch")));

        // The best match is under the cursor, and Enter applies it.
        QSignalSpy chosen(&modal, &ui::FolderIconModal::chosen);
        const QString cursor = modal.cursorGlyph();
        QVERIFY(!cursor.isEmpty());
        QTest::keyClick(&modal, Qt::Key_Return);
        QCOMPARE(chosen.count(), 1);
        QCOMPARE(chosen.first().first().toString(), cursor);
    }

    void pickerDefaultEmitsEmpty()
    {
        QWidget host;
        host.resize(1000, 800);
        ui::FolderIconModal modal(&host);
        host.show();
        modal.open(QDir::homePath() + QStringLiteral("/x"), QStringLiteral("hero:star"));
        // Opens on the current choice.
        QCOMPARE(modal.cursorGlyph(), QStringLiteral("hero:star"));

        QSignalSpy chosen(&modal, &ui::FolderIconModal::chosen);
        // Clearing a query puts the cursor back on the first cell: Default.
        modal.setQuery(QStringLiteral("zz"));
        modal.setQuery({});
        QVERIFY(modal.cursorGlyph().isEmpty());
        QTest::keyClick(&modal, Qt::Key_Return);
        QCOMPARE(chosen.count(), 1);
        QVERIFY(chosen.first().first().toString().isEmpty());
    }

    void pickerEscapeCancels()
    {
        QWidget host;
        host.resize(1000, 800);
        ui::FolderIconModal modal(&host);
        host.show();
        modal.open(QDir::homePath(), {});
        QSignalSpy chosen(&modal, &ui::FolderIconModal::chosen);
        QTest::keyClick(&modal, Qt::Key_Escape);
        QCOMPARE(chosen.count(), 0);
    }
};

QTEST_MAIN(TestFolderIcons)
#include "tst_foldericons.moc"
