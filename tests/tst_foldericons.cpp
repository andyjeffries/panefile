#include "model/DirectoryModel.h"
#include "model/FolderIconImage.h"
#include "model/FolderIconRenderer.h"
#include "model/FolderIconStore.h"
#include "model/IconProvider.h"
#include "model/StandardFolders.h"
#include "ui/FileItemDelegate.h"
#include "ui/ThemePalette.h"
#include "ui/modals/FolderIconModal.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
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

    void delegateDrawsEmptyFoldersClosed()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("empty")));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("full/inner")));

        DirectoryModel model;
        model.setPath(dir.path());
        QTRY_COMPARE(model.rowCount(), 2);

        const QColor colour(Qt::blue);
        const auto iconOf = [&](const QString &name) {
            const QModelIndex index = model.index(model.indexOfName(name));
            const auto entry = index.data(DirectoryModel::EntryRole).value<FileEntry>();
            return ui::FileItemDelegate::iconFor(index, entry, colour).pixmap(64).toImage();
        };
        const QImage closed =
            IconProvider::instance().folderIcon({}, colour, false, true).pixmap(64).toImage();
        const QImage open = IconProvider::instance().folderIcon({}, colour).pixmap(64).toImage();
        QVERIFY(closed != open);

        // Painting is what asks; the closed folder follows once the probe answers.
        iconOf(QStringLiteral("empty"));
        iconOf(QStringLiteral("full"));
        QTRY_VERIFY(iconOf(QStringLiteral("empty")) == closed);
        QCOMPARE(iconOf(QStringLiteral("full")), open);
    }

    static QImage testMask(int inset = 30)
    {
        QImage image(200, 200, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.fillRect(QRect(inset, inset, 200 - (2 * inset), 200 - (2 * inset)), Qt::black);
        painter.end();
        return FolderIconImage::toMask(image, false);
    }

    void fromJsonAcceptsImageIds()
    {
        const QString good = QStringLiteral("img:") + QString(32, QLatin1Char('a'));
        QVERIFY(FolderIconStore::isImageGlyph(good));
        const QString prefix = QStringLiteral("img:");
        for (const QString &bad : QStringList{prefix + QStringLiteral("../x"),
                                              prefix + QString(32, QLatin1Char('A')),
                                              prefix + QString(31, QLatin1Char('a')),
                                              prefix + QString(33, QLatin1Char('a'))}) {
            QVERIFY(!FolderIconStore::isValidGlyphId(bad));
        }
        const auto icons = FolderIconStore::fromJson(
            QStringLiteral(R"({"/ok":"%1","/bad":"img:../../x"})").arg(good).toUtf8());
        QCOMPARE(icons.size(), 1);
    }

    void imageMasksArePersistedAndPruned()
    {
        auto &store = FolderIconStore::instance();
        const QString id = store.addImage(testMask());
        const QString file = FolderIconStore::imagesDirectory() + QLatin1Char('/') +
                             id.mid(4) + QStringLiteral(".png");
        // Nothing is written until a folder takes it.
        QVERIFY(!QFile::exists(file));

        QVERIFY(store.set(QStringLiteral("/tmp/x/one"), id));
        QVERIFY(store.set(QStringLiteral("/tmp/x/two"), id));
        QVERIFY(QFile::exists(file));

        // A fresh store has no mask in memory; the file restores it.
        store.reset();
        QVERIFY(store.maskFor(id).isNull());
        QVERIFY(store.set(QStringLiteral("/tmp/x/one"), id));
        QVERIFY(!store.maskFor(id).isNull());
        QVERIFY(store.set(QStringLiteral("/tmp/x/two"), id));

        store.clear(QStringLiteral("/tmp/x/one"));
        QVERIFY(QFile::exists(file));
        store.clear(QStringLiteral("/tmp/x/two"));
        QVERIFY(!QFile::exists(file));
    }

    void imageEmblemPaintsInsideTheEmblemOnly()
    {
        auto &store = FolderIconStore::instance();
        const QString id = store.addImage(testMask(10));
        const QColor tint(60, 110, 180);
        const auto render = [&](const QString &glyph) {
            return FolderIconRenderer::pixmap(FolderIconSpec{.glyphId = glyph, .tint = tint},
                                              {96, 96}, 1.0)
                .toImage();
        };
        const QImage plain = render({});
        const QImage with = render(id);
        QVERIFY(plain != with);
        // The top-left corner of the folder is untouched.
        QCOMPARE(with.pixel(8, 8), plain.pixel(8, 8));
        QVERIFY(with.pixel(48, 66) != plain.pixel(48, 66));
    }

    void pickerPastesAnImageFile()
    {
        QTemporaryDir dir;
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter(&image).fillRect(QRect(8, 8, 48, 48), Qt::black);
        const QString file = dir.filePath(QStringLiteral("logo.png"));
        QVERIFY(image.save(file));

        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(file)});
        mime->setText(file);
        QGuiApplication::clipboard()->setMimeData(mime);

        QWidget host;
        host.resize(1000, 800);
        ui::FolderIconModal modal(&host);
        host.show();
        modal.open(QDir::homePath(), {});
        QTest::keyClick(&modal, Qt::Key_V, Qt::ControlModifier);
        const QString pasted = modal.cursorGlyph();
        QVERIFY(FolderIconStore::isImageGlyph(pasted));
        QVERIFY(modal.visibleGlyphs().contains(pasted));

        // Ctrl+I inverts into a different mask.
        QTest::keyClick(&modal, Qt::Key_I, Qt::ControlModifier);
        QVERIFY(FolderIconStore::isImageGlyph(modal.cursorGlyph()));
        QVERIFY(modal.cursorGlyph() != pasted);

        QSignalSpy chosen(&modal, &ui::FolderIconModal::chosen);
        QTest::keyClick(&modal, Qt::Key_Return);
        QCOMPARE(chosen.count(), 1);
        QVERIFY(FolderIconStore::isImageGlyph(chosen.first().first().toString()));
        // The clipboard is left alone.
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasUrls());
    }

    void pickerPasteOfBlankImageDropsThePreviousPaste()
    {
        QTemporaryDir dir;
        QImage logo(64, 64, QImage::Format_ARGB32);
        logo.fill(Qt::transparent);
        QPainter(&logo).fillRect(QRect(8, 8, 48, 48), Qt::black);
        QImage blank(64, 64, QImage::Format_ARGB32);
        blank.fill(Qt::white);
        const QString logoFile = dir.filePath(QStringLiteral("logo.png"));
        const QString blankFile = dir.filePath(QStringLiteral("blank.png"));
        QVERIFY(logo.save(logoFile));
        QVERIFY(blank.save(blankFile));
        const auto copy = [](const QString &file) {
            auto *mime = new QMimeData;
            mime->setUrls({QUrl::fromLocalFile(file)});
            QGuiApplication::clipboard()->setMimeData(mime);
        };

        QWidget host;
        host.resize(1000, 800);
        ui::FolderIconModal modal(&host);
        host.show();
        modal.open(QDir::homePath(), {});
        copy(logoFile);
        QTest::keyClick(&modal, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(FolderIconStore::isImageGlyph(modal.cursorGlyph()));

        copy(blankFile);
        QTest::keyClick(&modal, Qt::Key_V, Qt::ControlModifier);
        for (const QString &glyph : modal.visibleGlyphs()) {
            QVERIFY(!FolderIconStore::isImageGlyph(glyph));
        }
        QVERIFY(!FolderIconStore::isImageGlyph(modal.cursorGlyph()));
    }

    void pickerPasteWithoutImageKeepsPathsOutOfTheSearchBox()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("notes.txt"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.close();
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(file)});
        mime->setText(file);
        QGuiApplication::clipboard()->setMimeData(mime);

        QWidget host;
        host.resize(1000, 800);
        ui::FolderIconModal modal(&host);
        host.show();
        modal.open(QDir::homePath(), {});
        const int before = modal.visibleGlyphs().size();
        QTest::keyClick(&modal, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(modal.visibleGlyphs().size(), before);
    }

    void pickerPastesPlainTextIntoTheQuery()
    {
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("zzqq"));
        QGuiApplication::clipboard()->setMimeData(mime);

        QWidget host;
        host.resize(1000, 800);
        ui::FolderIconModal modal(&host);
        host.show();
        modal.open(QDir::homePath(), {});
        QTest::keyClick(&modal, Qt::Key_V, Qt::ControlModifier);
        QVERIFY(modal.visibleGlyphs().size() < 10);
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
