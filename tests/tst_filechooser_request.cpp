// The request and result documents, and the portal's D-Bus types on either side
// of them.

#include "platform/FileChooserRequest.h"
#include "platform/linux/FileChooserPortal_linux.h"

#include <QTest>

using namespace pf::platform;

class TestFileChooserRequest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void requestAndResultSurviveJson()
    {
        FileChooserRequest request;
        request.mode = FileChooserRequest::Mode::SaveFiles;
        request.appId = "org.example.App";
        request.title = "Save Files";
        request.multiple = true;
        request.filters = {{"PDF", {{0, "*.pdf"}, {1, "image/png"}}}};
        request.currentFilter = 0;
        request.choices = {{"enc", "Encoding", {{"u", "UTF-8"}}, "u"}};
        request.files = {"a.txt", "b.txt"};
        const FileChooserRequest back = FileChooserRequest::fromJson(request.toJson());
        QCOMPARE(back.mode, request.mode);
        QCOMPARE(back.filters, request.filters);
        QCOMPARE(back.choices, request.choices);
        QCOMPARE(back.files, request.files);
        QCOMPARE(back.currentFilter, 0);

        FileChooserResult result{{"/a", "/b"}, 2, {{"enc", "u"}}};
        FileChooserResult parsed;
        QVERIFY(FileChooserResult::fromJson(result.toJson(), &parsed));
        QCOMPARE(parsed.paths, result.paths);
        QCOMPARE(parsed.filterIndex, 2);
        QCOMPARE(parsed.choices, result.choices);
        QVERIFY(!FileChooserResult::fromJson("not json", &parsed));
    }

    void filtersBecomeNamePatterns()
    {
        QCOMPARE(globsForFilter({"x", {{0, "*.PDF"}}}), QStringList{"*.PDF"});
        QVERIFY(globsForFilter({"x", {{1, "image/png"}}}).contains("*.png"));
        const QStringList images = globsForFilter({"x", {{1, "image/*"}}});
        QVERIFY(images.contains("*.png"));
        QVERIFY(images.contains("*.jpg") || images.contains("*.jpeg"));
    }

    void mimeFiltersNobodyKnowsMatchNothingAndAllFilesMatchEverything()
    {
        QVERIFY(globsForFilter({"x", {{1, "application/x-does-not-exist"}}}).isEmpty());
        QCOMPARE(globsForFilter({"x", {{1, "application/octet-stream"}}}), QStringList{"*"});
        QVERIFY(globsForFilter({"x", {{1, "text/plain"}}}).contains("*.c"));
    }

    void optionsMapToARequest()
    {
        QVariantMap options;
        options["accept_label"] = "_Pick";
        options["multiple"] = true;
        options["current_name"] = "report.pdf";
        options["current_folder"] = QByteArray("/tmp/x\0", 7);
        options["files"] = QVariant::fromValue(QByteArrayList{QByteArray("a\0", 2), "b"});
        const PortalFilter pdf{"PDF", {{0, "*.pdf"}}};
        options["filters"] = QVariant::fromValue(QList<PortalFilter>{pdf});
        options["current_filter"] = QVariant::fromValue(pdf);
        options["choices"] = QVariant::fromValue(
            QList<PortalChoice>{{"enc", "Encoding", {{"u", "UTF-8"}, {"l", "Latin-1"}}, "l"}});

        const FileChooserRequest request =
            requestFromDBus(FileChooserRequest::Mode::Save, "app", "Save", options);
        QCOMPARE(request.acceptLabel, QString("_Pick"));
        QVERIFY(request.multiple);
        QCOMPARE(request.currentFolder, QString("/tmp/x"));
        QCOMPARE(request.files, (QStringList{"a", "b"}));
        QCOMPARE(request.filters.size(), 1);
        QCOMPARE(request.currentFilter, 0);
        QCOMPARE(request.filters.first().patterns.first().second, QString("*.pdf"));
        QCOMPARE(request.choices.first().initial, QString("l"));
        QCOMPARE(request.choices.first().options.size(), 2);

        FileChooserResult result{{"/tmp/a b.pdf"}, 0, {{"enc", "l"}}};
        const QVariantMap results = resultsToDBus(request, result);
        QCOMPARE(results.value("uris").toStringList(), QStringList{"file:///tmp/a%20b.pdf"});
        QVERIFY(results.contains("current_filter"));
        QVERIFY(results.contains("choices"));
    }
};

QTEST_MAIN(TestFileChooserRequest)
#include "tst_filechooser_request.moc"
