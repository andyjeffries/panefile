#include "model/FileEntry.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/quicklook/QuickLookDock.h"
#include "ui/quicklook/QuickLookLoader.h"
#include "ui/quicklook/QuickLookRegistry.h"
#include "ui/quicklook/QuickLookView.h"
#include "ui/quicklook/renderers/ArchiveRenderer.h"
#include "ui/quicklook/renderers/DirectoryRenderer.h"
#include "ui/quicklook/renderers/HexRenderer.h"
#include "ui/quicklook/renderers/ImageRenderer.h"
#include "ui/quicklook/renderers/TextRenderer.h"

#include <QApplication>
#include <QDir>
#include <QMimeDatabase>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pf;
using namespace pf::ui;

namespace {

FileEntry fileEntry(const QString &name, quint64 size = 0)
{
    FileEntry entry;
    entry.name = name;
    entry.size = size;
    return entry;
}

QString write(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write(bytes);
    file.close();
    return path;
}

} // namespace

/// §7.6 and §7.7.
class TestQuickLook : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ------------------------------------------------------------- registry

    /// §7.6: "picks the highest-priority renderer whose canRender returns
    /// true".
    void picksByPriority()
    {
        QuickLookRegistry registry;
        registry.add(std::make_unique<HexRenderer>());
        registry.add(std::make_unique<TextRenderer>());

        QMimeDatabase database;
        const QMimeType plain = database.mimeTypeForName(QStringLiteral("text/plain"));

        QuickLookRenderer *chosen = registry.rendererFor(plain, fileEntry(QStringLiteral("a.txt")));
        QVERIFY(chosen != nullptr);
        QCOMPARE(chosen->id(), QStringLiteral("text"));
    }

    /// The hex renderer accepts everything, and must never win against a
    /// renderer that also accepts — otherwise every preview would be a hex
    /// dump.
    void hexIsOnlyEverTheFallback()
    {
        std::unique_ptr<QuickLookRegistry> registry = QuickLookRegistry::createDefault();

        QMimeDatabase database;
        QCOMPARE(registry->fallback()->id(), QStringLiteral("hex"));

        const QMimeType png = database.mimeTypeForName(QStringLiteral("image/png"));
        QCOMPARE(registry->rendererFor(png, fileEntry(QStringLiteral("a.png")))->id(),
                 QStringLiteral("image"));

        // A type nothing else claims still gets a renderer: §7.6's guarantee
        // that Quick Look is never opened on a file and shows nothing.
        const QMimeType unknown =
            database.mimeTypeForName(QStringLiteral("application/x-nonexistent-type"));
        QCOMPARE(registry->rendererFor(unknown, fileEntry(QStringLiteral("a.bin")))->id(),
                 QStringLiteral("hex"));
    }

    /// Directories go to the directory renderer whatever their MIME type says.
    void directoriesUseTheDirectoryRenderer()
    {
        std::unique_ptr<QuickLookRegistry> registry = QuickLookRegistry::createDefault();

        QMimeDatabase database;
        FileEntry entry = fileEntry(QStringLiteral("somewhere"));
        entry.isDir = true;

        QCOMPARE(
            registry
                ->rendererFor(database.mimeTypeForName(QStringLiteral("inode/directory")), entry)
                ->id(),
            QStringLiteral("directory"));
    }

    /// §7.6's MIME breadth: JSON and shell scripts are `application/*` but are
    /// plainly text to a reader.
    void textRendererClaimsCodeMimeTypes()
    {
        QMimeDatabase database;
        QVERIFY(TextRenderer::isTextual(database.mimeTypeForName(QStringLiteral("text/plain"))));
        QVERIFY(
            TextRenderer::isTextual(database.mimeTypeForName(QStringLiteral("application/json"))));
        QVERIFY(TextRenderer::isTextual(
            database.mimeTypeForName(QStringLiteral("application/x-shellscript"))));
        QVERIFY(!TextRenderer::isTextual(database.mimeTypeForName(QStringLiteral("image/png"))));
    }

    // ----------------------------------------------------------------- hex

    /// The alignment is the whole point of a hex dump.
    void hexDumpIsAligned()
    {
        const QString dump = HexRenderer::formatDump(QByteArray("ABC", 3));
        const QStringList lines = dump.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines.first().startsWith(QStringLiteral("00000000")), qPrintable(lines.first()));
        QVERIFY2(lines.first().endsWith(QStringLiteral("|ABC|")), qPrintable(lines.first()));
    }

    void hexDumpShowsNonPrintablesAsDots()
    {
        const QString dump = HexRenderer::formatDump(QByteArray("\x01\x02", 2));
        QVERIFY(dump.contains(QStringLiteral("01 02")));
        QVERIFY2(dump.trimmed().endsWith(QStringLiteral("|..|")), qPrintable(dump));
    }

    // -------------------------------------------------------------- loader

    /// §7.6: "Debounce cursor changes by 120 ms so holding `j` doesn't queue a
    /// hundred loads. Cancel any in-flight load when the cursor moves again."
    void debounceCollapsesRapidRequests()
    {
        QTemporaryDir dir;
        const QString a = write(dir, QStringLiteral("a.txt"), QByteArray("alpha"));
        const QString b = write(dir, QStringLiteral("b.txt"), QByteArray("bravo"));
        const QString c = write(dir, QStringLiteral("c.txt"), QByteArray("charlie"));

        TextRenderer renderer;
        QuickLookLoader loader;
        loader.setDebounceInterval(40);

        QSignalSpy loaded(&loader, &QuickLookLoader::loaded);

        loader.request(a, fileEntry(QStringLiteral("a.txt"), 5), &renderer);
        loader.request(b, fileEntry(QStringLiteral("b.txt"), 5), &renderer);
        loader.request(c, fileEntry(QStringLiteral("c.txt"), 7), &renderer);

        QTRY_VERIFY_WITH_TIMEOUT(loaded.count() >= 1, 3000);

        // Three requests inside one debounce window produce one load, and it is
        // the last one — the file the cursor actually landed on.
        QCOMPARE(loaded.count(), 1);
        const auto content = loaded.first().at(0).value<QuickLookContent>();
        QCOMPARE(content.path, c);
        QCOMPARE(content.text, QStringLiteral("charlie"));
    }

    /// §7.6: "Cache the last 5 rendered contents keyed on path plus mtime."
    void cacheHitsOnTheSamePathAndMtime()
    {
        QTemporaryDir dir;
        const QString path = write(dir, QStringLiteral("a.txt"), QByteArray("alpha"));

        TextRenderer renderer;
        QuickLookLoader loader;
        loader.setDebounceInterval(0);

        QSignalSpy loaded(&loader, &QuickLookLoader::loaded);

        FileEntry entry = fileEntry(QStringLiteral("a.txt"), 5);
        entry.modified = QFileInfo(path).lastModified();

        loader.request(path, entry, &renderer);
        QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 1, 3000);
        QCOMPARE(loader.cacheSize(), 1);

        // A second request for the same path and mtime is answered from the
        // cache, synchronously enough that no worker runs.
        loader.request(path, entry, &renderer);
        QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 2, 3000);
        QCOMPARE(loader.cacheSize(), 1);
    }

    /// A changed mtime invalidates the entry: §7.6 keys the cache on both.
    void cacheMissesWhenTheFileChanges()
    {
        QTemporaryDir dir;
        const QString path = write(dir, QStringLiteral("a.txt"), QByteArray("alpha"));

        TextRenderer renderer;
        QuickLookLoader loader;
        loader.setDebounceInterval(0);

        QSignalSpy loaded(&loader, &QuickLookLoader::loaded);

        FileEntry entry = fileEntry(QStringLiteral("a.txt"), 5);
        entry.modified = QFileInfo(path).lastModified();

        loader.request(path, entry, &renderer);
        QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 1, 3000);

        write(dir, QStringLiteral("a.txt"), QByteArray("rewritten"));
        entry.modified = entry.modified.addSecs(5);

        loader.request(path, entry, &renderer);
        QTRY_VERIFY_WITH_TIMEOUT(loaded.count() == 2, 3000);

        QCOMPARE(loaded.last().at(0).value<QuickLookContent>().text, QStringLiteral("rewritten"));
        QCOMPARE(loader.cacheSize(), 2);
    }

    /// A cancelled load delivers nothing at all.
    void cancelDropsAnInFlightLoad()
    {
        QTemporaryDir dir;
        const QString path = write(dir, QStringLiteral("a.txt"), QByteArray("alpha"));

        TextRenderer renderer;
        QuickLookLoader loader;
        loader.setDebounceInterval(50);

        QSignalSpy loaded(&loader, &QuickLookLoader::loaded);
        loader.request(path, fileEntry(QStringLiteral("a.txt"), 5), &renderer);
        loader.cancel();

        QTest::qWait(300);
        QCOMPARE(loaded.count(), 0);
    }

    // ---------------------------------------------------------------- docks

    void dockNamesRoundTrip()
    {
        for (const QuickLookDock dock :
             {QuickLookDock::Float, QuickLookDock::Right, QuickLookDock::Left,
              QuickLookDock::Bottom, QuickLookDock::Panel, QuickLookDock::Full}) {
            QCOMPARE(parseDock(dockName(dock)), dock);
        }

        // §8: an unrecognised value falls back to the documented default rather
        // than to whatever the enum's zero happens to be.
        QCOMPARE(parseDock(QStringLiteral("sideways")), QuickLookDock::Float);
    }

    /// §7.6: `Ctrl+Space` cycles, and full is not in the cycle — it has its own
    /// toggle and "returns to the previous mode when dismissed".
    void cycleVisitsEveryDockAndExcludesFull()
    {
        QuickLookDock dock = QuickLookDock::Float;
        QList<QuickLookDock> seen{dock};

        for (int i = 0; i < 5; ++i) {
            dock = nextDock(dock);
            QVERIFY(dock != QuickLookDock::Full);
            seen.append(dock);
        }

        QCOMPARE(dock, QuickLookDock::Float);
        QCOMPARE(seen.size(), 6);
    }

    // ----------------------------------------------------------------- view

    /// The end-to-end path: a file goes in, the right renderer's page comes up,
    /// and the header describes it.
    void viewShowsTextThroughTheTextRenderer()
    {
        QTemporaryDir dir;
        const QString path = write(dir, QStringLiteral("notes.txt"), QByteArray("hello"));

        QuickLookView view;
        view.loader()->setDebounceInterval(0);

        FileEntry entry = fileEntry(QStringLiteral("notes.txt"), 5);
        entry.modified = QFileInfo(path).lastModified();

        view.showFile(path, entry);

        QTRY_VERIFY_WITH_TIMEOUT(view.currentRenderer() != nullptr, 3000);
        QCOMPARE(view.currentRenderer()->id(), QStringLiteral("text"));
        QCOMPARE(view.currentPath(), path);
    }

    // ----------------------------------------------------------- placement

    /// Every dock mode re-parents the same pane, and no transition may leave it
    /// behind in the container the previous mode used.
    void dockTransitionsMoveTheSameWidget()
    {
        MainWindow window;
        window.panelStrip()->addPanel(QDir::tempPath());

        auto *view = new QuickLookView;
        window.setQuickLookWidget(view);
        window.setQuickLookVisible(true);

        const QList<QuickLookDock> order{
            QuickLookDock::Float, QuickLookDock::Right, QuickLookDock::Left, QuickLookDock::Bottom,
            QuickLookDock::Panel, QuickLookDock::Full,  QuickLookDock::Float};

        for (const QuickLookDock dock : order) {
            window.setQuickLookDock(dock, 70, 35);
            QCOMPARE(window.quickLookDock(), dock);
            QCOMPARE(window.quickLookWidget(), view);

            // The pane is always somewhere in the window, never orphaned.
            QVERIFY2(view->parentWidget() != nullptr, qPrintable(dockName(dock)));
            QVERIFY2(window.isAncestorOf(view), qPrintable(dockName(dock)));

            // §7.6: only `panel` mode occupies a slot in the strip.
            QCOMPARE(window.panelStrip()->quickLookSlot() == view, dock == QuickLookDock::Panel);
        }
    }

    /// §7.6: the `panel` dock "counts toward panels.max_count".
    void panelDockCountsTowardTheLimit()
    {
        MainWindow window;
        auto *view = new QuickLookView;

        for (int i = 0; i < PanelStrip::kMaxPanels; ++i) {
            window.panelStrip()->addPanel(QDir::tempPath());
        }
        QCOMPARE(window.panelStrip()->count(), PanelStrip::kMaxPanels);

        window.setQuickLookWidget(view);
        window.setQuickLookDock(QuickLookDock::Panel, 70, 35);

        // The strip is already full, so no further panel may be created while
        // the pane holds one of the slots.
        QCOMPARE(window.panelStrip()->addPanel(QDir::tempPath()), nullptr);
    }

    /// §7.6: an unreadable file degrades to hex with a note, "never crash or
    /// blank".
    void unreadableFileDegradesToHex()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("missing.txt"));

        QuickLookView view;
        view.loader()->setDebounceInterval(0);

        view.showFile(path, fileEntry(QStringLiteral("missing.txt"), 5));

        QTRY_VERIFY_WITH_TIMEOUT(view.currentRenderer() != nullptr, 3000);
        QCOMPARE(view.currentRenderer()->id(), QStringLiteral("hex"));
    }
};

QTEST_MAIN(TestQuickLook)
#include "tst_quicklook.moc"
