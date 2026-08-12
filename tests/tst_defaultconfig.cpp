// The bundled default configuration (§8.1).
//
// This template is the single source of truth for defaults: --print-default-
// config writes it and Config parses it as the base layer. A typo in it would
// otherwise stay invisible until a user redirected the output back into their
// config directory and found Panefile refusing to read it.

#include "config/DefaultConfig.h"

#include <QTest>

#include <toml++/toml.hpp>

using namespace pf::config;

class TestDefaultConfig : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void templateIsValidToml();
    void everyDocumentedSectionIsPresent();
    void valuesMatchTheSpecifiedDefaults();
    void isCommentedForHumans();
};

void TestDefaultConfig::templateIsValidToml()
{
    std::string error;
    QVERIFY2(defaultConfigParses(&error), error.c_str());
}

void TestDefaultConfig::everyDocumentedSectionIsPresent()
{
    const auto table = toml::parse(kDefaultConfigToml);
    QVERIFY(table.succeeded());

    for (const char *section : {"general", "panels", "quicklook", "thumbnails", "search",
                                "operations", "cli", "external"}) {
        QVERIFY2(table.table().contains(section),
                 qPrintable(QStringLiteral("missing [%1]").arg(QLatin1String(section))));
    }
}

void TestDefaultConfig::valuesMatchTheSpecifiedDefaults()
{
    const auto result = toml::parse(kDefaultConfigToml);
    QVERIFY(result.succeeded());
    const toml::table &table = result.table();

    // A representative value from each section. These are the numbers §8.1
    // documents; if one changes, the documentation changes with it.
    QCOMPARE(table["general"]["single_instance"].value_or(false), true);
    QCOMPARE(table["panels"]["max_count"].value_or(0), 10);
    QCOMPARE(table["panels"]["default_sort"].value_or(std::string_view{}),
             std::string_view{"name"});
    QCOMPARE(table["quicklook"]["dock"].value_or(std::string_view{}), std::string_view{"float"});
    QCOMPARE(table["quicklook"]["debounce_ms"].value_or(0), 120);
    QCOMPARE(table["quicklook"]["max_read_bytes"].value_or(0), 67108864);
    QCOMPARE(table["thumbnails"]["max_file_size_mb"].value_or(0), 200);
    QCOMPARE(table["search"]["max_results"].value_or(0), 10000);
    QCOMPARE(table["operations"]["default_conflict"].value_or(std::string_view{}),
             std::string_view{"ask"});

    // §7.4 and §13: following symlinks during recursive operations is how a
    // copy escapes its tree. It must default to off.
    QCOMPARE(table["operations"]["follow_symlinks"].value_or(true), false);
}

void TestDefaultConfig::isCommentedForHumans()
{
    // The template is what a user gets from --print-default-config, so it has
    // to explain the enumerated values rather than just list keys.
    const std::string_view toml = kDefaultConfigToml;

    QVERIFY(toml.find("# name | size | modified | type") != std::string_view::npos);
    QVERIFY(toml.find("float | right | left | bottom | panel | full") != std::string_view::npos);
    QVERIFY(toml.find("$EDITOR") != std::string_view::npos);
}

QTEST_APPLESS_MAIN(TestDefaultConfig)
#include "tst_defaultconfig.moc"
