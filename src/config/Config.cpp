#include "config/Config.h"

#include "config/DefaultConfig.h"
#include "core/Logging.h"

#include <QFile>
#include <QFileInfo>

#include <toml++/toml.hpp>

namespace pf::config {
namespace {

/// Collects issues while reading a table, so that one bad key does not stop the
/// rest of the file being applied (§8.3).
class Reader
{
public:
    Reader(const toml::table &table, QString file, QList<ConfigIssue> *issues)
        : m_table(table), m_file(std::move(file)), m_issues(issues)
    {}

    void readBool(const char *section, const char *key, bool &target)
    {
        const auto node = m_table[section][key];
        if (!node) {
            return;
        }
        if (const auto value = node.value<bool>()) {
            target = *value;
            return;
        }
        reject(section, key, node, QStringLiteral("expected true or false"));
    }

    void readString(const char *section, const char *key, QString &target)
    {
        const auto node = m_table[section][key];
        if (!node) {
            return;
        }
        if (const auto value = node.value<std::string>()) {
            target = QString::fromStdString(*value);
            return;
        }
        reject(section, key, node, QStringLiteral("expected a string"));
    }

    /// Reads a string constrained to a set of permitted values.
    ///
    /// An out-of-range value is rejected with the list of what is allowed. §8.1
    /// documents these enumerations in comments; repeating them in the error is
    /// the difference between a user fixing a typo and a user guessing.
    void readEnum(const char *section, const char *key, QString &target,
                  const QStringList &permitted)
    {
        QString candidate = target;
        readString(section, key, candidate);
        if (candidate == target) {
            return;
        }
        if (permitted.contains(candidate)) {
            target = candidate;
            return;
        }
        reject(section, key, m_table[section][key],
               QStringLiteral("'%1' is not one of: %2")
                   .arg(candidate, permitted.join(QStringLiteral(", "))));
    }

    template<typename T>
    void readNumber(const char *section, const char *key, T &target, T minimum, T maximum)
    {
        const auto node = m_table[section][key];
        if (!node) {
            return;
        }
        const auto value = node.value<int64_t>();
        if (!value) {
            reject(section, key, node, QStringLiteral("expected a number"));
            return;
        }
        // Clamping rather than rejecting: a max_count of 500 is a user asking
        // for more panels than the design supports, not a typo, and giving them
        // the maximum is friendlier than ignoring the line entirely. The issue
        // still tells them what happened.
        const auto clamped = static_cast<T>(std::clamp<int64_t>(
            *value, static_cast<int64_t>(minimum), static_cast<int64_t>(maximum)));
        if (clamped != static_cast<T>(*value)) {
            reject(section, key, node,
                   QStringLiteral("%1 is outside %2–%3; using %4")
                       .arg(*value)
                       .arg(minimum)
                       .arg(maximum)
                       .arg(clamped));
        }
        target = clamped;
    }

private:
    void reject(const char *section, const char *key, const toml::node_view<const toml::node> &node,
                const QString &message)
    {
        ConfigIssue issue;
        issue.file = m_file;
        issue.key = QStringLiteral("%1.%2").arg(QLatin1String(section), QLatin1String(key));
        issue.message = message;
        if (node) {
            issue.line = static_cast<int>(node.node()->source().begin.line);
            issue.column = static_cast<int>(node.node()->source().begin.column);
        }
        m_issues->append(issue);
    }

    const toml::table &m_table;
    QString m_file;
    QList<ConfigIssue> *m_issues;
};

void applyTable(const toml::table &table, Settings &settings, const QString &file,
                QList<ConfigIssue> *issues)
{
    Reader reader(table, file, issues);

    reader.readString("general", "new_panel_path", settings.general.newPanelPath);
    reader.readBool("general", "restore_session", settings.general.restoreSession);
    reader.readBool("general", "confirm_on_quit", settings.general.confirmOnQuit);
    reader.readBool("general", "single_instance", settings.general.singleInstance);

    reader.readNumber("panels", "default_count", settings.panels.defaultCount, 1, 10);
    reader.readNumber("panels", "max_count", settings.panels.maxCount, 1, 10);
    reader.readBool("panels", "directories_first", settings.panels.directoriesFirst);
    reader.readEnum("panels", "default_sort", settings.panels.defaultSort,
                    {QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("modified"),
                     QStringLiteral("type"), QStringLiteral("random")});
    reader.readBool("panels", "show_hidden", settings.panels.showHidden);

    reader.readEnum("quicklook", "dock", settings.quicklook.dock,
                    {QStringLiteral("float"), QStringLiteral("right"), QStringLiteral("left"),
                     QStringLiteral("bottom"), QStringLiteral("panel"), QStringLiteral("full")});
    reader.readNumber("quicklook", "float_size_percent", settings.quicklook.floatSizePercent, 20,
                      100);
    reader.readNumber("quicklook", "dock_size_percent", settings.quicklook.dockSizePercent, 10, 90);
    reader.readBool("quicklook", "chrome", settings.quicklook.chrome);
    reader.readNumber("quicklook", "debounce_ms", settings.quicklook.debounceMs, 0, 5000);
    reader.readNumber<qint64>("quicklook", "max_read_bytes", settings.quicklook.maxReadBytes, 0,
                              1LL << 34);
    reader.readNumber("quicklook", "max_decode_mb", settings.quicklook.maxDecodeMb, 1, 100000);
    reader.readBool("quicklook", "follow_cursor", settings.quicklook.followCursor);
    reader.readBool("quicklook", "close_on_panel_switch", settings.quicklook.closeOnPanelSwitch);

    reader.readBool("thumbnails", "enabled", settings.thumbnails.enabled);
    reader.readBool("thumbnails", "video", settings.thumbnails.video);
    reader.readNumber("thumbnails", "max_file_size_mb", settings.thumbnails.maxFileSizeMb, 1,
                      100000);

    reader.readBool("search", "fuzzy", settings.search.fuzzy);
    reader.readBool("search", "respect_gitignore", settings.search.respectGitignore);
    reader.readNumber("search", "max_results", settings.search.maxResults, 1, 1000000);

    reader.readBool("operations", "confirm_delete", settings.operations.confirmDelete);
    reader.readBool("operations", "confirm_trash", settings.operations.confirmTrash);
    reader.readEnum("operations", "default_conflict", settings.operations.defaultConflict,
                    {QStringLiteral("ask"), QStringLiteral("overwrite"), QStringLiteral("skip"),
                     QStringLiteral("rename")});
    reader.readBool("operations", "follow_symlinks", settings.operations.followSymlinks);

    reader.readEnum(
        "cli", "file_action", settings.cli.fileAction,
        {QStringLiteral("select"), QStringLiteral("quicklook"), QStringLiteral("launch")});
    reader.readString("cli", "on_focused", settings.cli.onFocused);
    reader.readString("cli", "on_unfocused", settings.cli.onUnfocused);

    reader.readString("external", "editor", settings.external.editor);
    reader.readString("external", "terminal", settings.external.terminal);

    // §8.2 puts the key timeouts in hotkeys.toml, but a user who writes them
    // into config.toml has made an understandable mistake rather than a
    // meaningless one, so both files are accepted.
    reader.readNumber("keys", "sequence_timeout_ms", settings.keys.sequenceTimeoutMs, 100, 10000);
    reader.readNumber("keys", "ambiguity_timeout_ms", settings.keys.ambiguityTimeoutMs, 0, 5000);
}

} // namespace

QString ConfigIssue::toString() const
{
    QString location = file.isEmpty() ? QStringLiteral("config") : file;
    if (line > 0) {
        location += QStringLiteral(":%1").arg(line);
    }
    if (!key.isEmpty()) {
        return QStringLiteral("%1: %2 — %3").arg(location, key, message);
    }
    return QStringLiteral("%1: %2").arg(location, message);
}

ConfigLoadResult parseConfig(const QString &text, const QString &fileNameForIssues)
{
    ConfigLoadResult result;

    // The shipped defaults are applied first, so an unparseable user file still
    // leaves a fully populated Settings rather than an empty one.
    const toml::parse_result defaults = toml::parse(kDefaultConfigToml);
    if (defaults) {
        applyTable(defaults.table(), result.settings, {}, &result.issues);
    }
    // An issue in our own template is a bug in the application, not the user's
    // problem, so it is not reported to them.
    result.issues.clear();

    if (text.trimmed().isEmpty()) {
        return result;
    }

    const std::string utf8 = text.toStdString();
    const toml::parse_result parsed = toml::parse(utf8);

    if (!parsed) {
        const toml::parse_error &error = parsed.error();
        result.issues.append(
            ConfigIssue{.file = fileNameForIssues,
                        .line = static_cast<int>(error.source().begin.line),
                        .column = static_cast<int>(error.source().begin.column),
                        .key = {},
                        .message = QString::fromStdString(std::string(error.description()))});
        // The defaults stand. §8.3 requires the application to start with a
        // banner rather than refuse to start.
        return result;
    }

    applyTable(parsed.table(), result.settings, fileNameForIssues, &result.issues);
    return result;
}

ConfigLoadResult loadConfig(const QString &path)
{
    QFile file(path);
    if (!file.exists()) {
        // Not an issue. This is what every new installation looks like, and
        // reporting it would train users to ignore the banner.
        qCDebug(pfConfig) << "no config file at" << path << "— using defaults";
        return parseConfig({});
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ConfigLoadResult result = parseConfig({});
        result.issues.append(ConfigIssue{.file = QFileInfo(path).fileName(),
                                         .line = 0,
                                         .column = 0,
                                         .key = {},
                                         .message = file.errorString()});
        return result;
    }

    return parseConfig(QString::fromUtf8(file.readAll()), QFileInfo(path).fileName());
}

} // namespace pf::config
