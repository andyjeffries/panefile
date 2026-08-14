#include "config/TomlWriter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using pf::config::TomlWriter;

namespace {

QString read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool write(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

} // namespace

/// Editing a config file without destroying what the user wrote in it.
class TestTomlWriter : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_path = m_dir->filePath(QStringLiteral("config.toml"));
    }

    void cleanup() { m_dir.reset(); }

    /// The whole reason this is not a parse-and-serialise round trip.
    ///
    /// Panefile's default config is mostly comments explaining what each key
    /// does. Serialising a parsed table back would drop every one of them, so
    /// the first use of a settings dialog would silently strip a file the user
    /// may have annotated further.
    void commentsAndLayoutSurvive()
    {
        const QString original = QStringLiteral(
            "# Panefile configuration.\n"
            "#\n"
            "# Delete a line to keep the default.\n"
            "\n"
            "[general]\n"
            "# Where a new panel opens.\n"
            "new_panel_path       = \"~\"\n"
            "restore_session      = true\n"
            "\n"
            "[panels]\n"
            "default_sort         = \"name\"       # name | size | modified | type\n");
        QVERIFY(write(m_path, original));

        const auto result =
            TomlWriter::setValue(m_path, QStringLiteral("general"),
                                 QStringLiteral("restore_session"), TomlWriter::boolean(false));
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.changed);

        const QString updated = read(m_path);
        QVERIFY2(updated.contains(QStringLiteral("# Panefile configuration.")),
                 qPrintable(updated));
        QVERIFY2(updated.contains(QStringLiteral("# Where a new panel opens.")),
                 qPrintable(updated));
        QVERIFY2(updated.contains(QStringLiteral("restore_session      = false")),
                 qPrintable(updated));
        QVERIFY2(updated.contains(QStringLiteral("new_panel_path       = \"~\"")),
                 qPrintable(updated));
    }

    /// A trailing comment explains the value it sits beside. Losing it on the
    /// first edit is the same failure as losing the block comments.
    void trailingCommentsSurvive()
    {
        QVERIFY(write(
            m_path,
            QStringLiteral("[panels]\n"
                           "default_sort = \"name\"       # name | size | modified | type\n")));

        QVERIFY(TomlWriter::setValue(m_path, QStringLiteral("panels"),
                                     QStringLiteral("default_sort"),
                                     TomlWriter::quote(QStringLiteral("size")))
                    .ok);

        const QString updated = read(m_path);
        QVERIFY2(updated.contains(QStringLiteral("# name | size | modified | type")),
                 qPrintable(updated));
        QVERIFY2(updated.contains(QStringLiteral("\"size\"")), qPrintable(updated));
        QVERIFY2(!updated.contains(QStringLiteral("\"name\"")), qPrintable(updated));
    }

    /// A `#` inside a string is not a comment, and treating it as one would
    /// truncate the value.
    void aHashInsideAStringIsNotAComment()
    {
        QVERIFY(write(m_path, QStringLiteral("[general]\nnew_panel_path = \"/tmp/a#b\"\n")));

        QVERIFY(TomlWriter::setValue(m_path, QStringLiteral("general"),
                                     QStringLiteral("new_panel_path"),
                                     TomlWriter::quote(QStringLiteral("/tmp/c")))
                    .ok);

        QCOMPARE(read(m_path), QStringLiteral("[general]\nnew_panel_path = \"/tmp/c\"\n"));
    }

    /// A key that is not there yet is added to its table rather than to the end
    /// of the file, where it would belong to whichever table came last.
    void aMissingKeyIsAddedToItsTable()
    {
        QVERIFY(write(m_path, QStringLiteral("[general]\nrestore_session = true\n"
                                             "\n"
                                             "[panels]\ndefault_count = 1\n")));

        QVERIFY(TomlWriter::setValue(m_path, QStringLiteral("general"),
                                     QStringLiteral("confirm_on_quit"), TomlWriter::boolean(true))
                    .ok);

        const QString updated = read(m_path);
        const qsizetype added = updated.indexOf(QStringLiteral("confirm_on_quit"));
        const qsizetype panels = updated.indexOf(QStringLiteral("[panels]"));
        QVERIFY2(added > 0 && added < panels, qPrintable(updated));
    }

    /// A table that is not there yet is appended whole.
    void aMissingTableIsAppended()
    {
        QVERIFY(write(m_path, QStringLiteral("[general]\nrestore_session = true\n")));

        QVERIFY(TomlWriter::setValue(m_path, QStringLiteral("thumbnails"),
                                     QStringLiteral("enabled"), TomlWriter::boolean(false))
                    .ok);

        const QString updated = read(m_path);
        QVERIFY2(updated.contains(QStringLiteral("[thumbnails]")), qPrintable(updated));
        QVERIFY2(updated.contains(QStringLiteral("enabled = false")), qPrintable(updated));
    }

    /// Writing to a file that does not exist yet creates it. A fresh install
    /// has no config, which is the case the settings dialog meets first.
    void anAbsentFileIsCreated()
    {
        QVERIFY(!QFile::exists(m_path));

        QVERIFY(TomlWriter::setValue(m_path, QStringLiteral("general"),
                                     QStringLiteral("restore_session"), TomlWriter::boolean(false))
                    .ok);

        const QString written = read(m_path);
        QVERIFY2(written.contains(QStringLiteral("[general]")), qPrintable(written));
        QVERIFY2(written.contains(QStringLiteral("restore_session = false")), qPrintable(written));
    }

    /// Setting a value the file already has changes nothing, and says so.
    /// Rewriting identical bytes still moves the mtime, and ConfigWatcher is
    /// watching that.
    void anUnchangedValueIsNotRewritten()
    {
        QVERIFY(write(m_path, QStringLiteral("[general]\nrestore_session = true\n")));

        const auto result =
            TomlWriter::setValue(m_path, QStringLiteral("general"),
                                 QStringLiteral("restore_session"), TomlWriter::boolean(true));
        QVERIFY(result.ok);
        QVERIFY(!result.changed);
    }

    /// Removing a key puts the built-in default back.
    void removingAKeyLeavesTheRest()
    {
        QVERIFY(write(m_path, QStringLiteral("# keep me\n[general]\nrestore_session = true\n"
                                             "confirm_on_quit = true\n")));

        QVERIFY(TomlWriter::removeValue(m_path, QStringLiteral("general"),
                                        QStringLiteral("restore_session"))
                    .ok);

        const QString updated = read(m_path);
        QVERIFY2(!updated.contains(QStringLiteral("restore_session")), qPrintable(updated));
        QVERIFY2(updated.contains(QStringLiteral("confirm_on_quit = true")), qPrintable(updated));
        QVERIFY2(updated.contains(QStringLiteral("# keep me")), qPrintable(updated));
    }

    /// A key of the same name in another table is not the key being asked for.
    void theTableIsPartOfTheAddress()
    {
        QVERIFY(write(m_path, QStringLiteral("[general]\nenabled = true\n"
                                             "\n"
                                             "[thumbnails]\nenabled = true\n")));

        QVERIFY(TomlWriter::setValue(m_path, QStringLiteral("thumbnails"),
                                     QStringLiteral("enabled"), TomlWriter::boolean(false))
                    .ok);

        const QString updated = read(m_path);
        const qsizetype general = updated.indexOf(QStringLiteral("[general]"));
        const qsizetype thumbnails = updated.indexOf(QStringLiteral("[thumbnails]"));
        const qsizetype changed = updated.indexOf(QStringLiteral("enabled = false"));

        QVERIFY2(changed > thumbnails, qPrintable(updated));
        QVERIFY2(updated.indexOf(QStringLiteral("enabled = true")) > general, qPrintable(updated));
        QVERIFY2(updated.indexOf(QStringLiteral("enabled = true")) < thumbnails,
                 qPrintable(updated));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_path;
};

QTEST_MAIN(TestTomlWriter)
#include "tst_tomlwriter.moc"
