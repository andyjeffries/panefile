#include "config/Hotkeys.h"

#include "input/Keymap.h"
#include "core/Logging.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <toml++/toml.hpp>

namespace pf::config {
namespace {

using input::KeymapLayer;

/// §8.2's section names. `[normal]` and `[selection]` map to the panel modes of
/// §6.1; `[global]` is for bindings that work regardless of mode.
struct Section {
    const char *name;
    KeymapLayer layer;
};

constexpr Section kSections[] = {
    {.name = "normal", .layer = KeymapLayer::Normal},
    {.name = "selection", .layer = KeymapLayer::Selection},
    {.name = "global", .layer = KeymapLayer::Global},
    {.name = "typing", .layer = KeymapLayer::Typing},
};

int lineOf(const toml::node *node)
{
    return node == nullptr ? 0 : static_cast<int>(node->source().begin.line);
}

} // namespace

HotkeysLoadResult applyHotkeys(const QString &text, input::Keymap &keymap, Settings::Keys *keys,
                               const QString &fileNameForIssues)
{
    HotkeysLoadResult result;

    if (text.trimmed().isEmpty()) {
        return result;
    }

    const toml::parse_result parsed = toml::parse(text.toStdString());
    if (!parsed) {
        result.issues.append(ConfigIssue{
            .file = fileNameForIssues,
            .line = static_cast<int>(parsed.error().source().begin.line),
            .column = static_cast<int>(parsed.error().source().begin.column),
            .key = {},
            .message = QString::fromStdString(std::string(parsed.error().description()))});
        // The defaults stand. §8.3: never crash, never silently produce garbage
        // — and an application with no keybindings at all would be garbage.
        return result;
    }

    const toml::table &table = parsed.table();

    // Chords this file has claimed, so that a user binding can take a chord
    // away from a *default* without that counting as a conflict, while two
    // bindings within the user's own file still do.
    QSet<QString> claimed;

    if (keys != nullptr) {
        if (const auto value = table["keys"]["sequence_timeout_ms"].value<int64_t>()) {
            keys->sequenceTimeoutMs = std::clamp(static_cast<int>(*value), 100, 10000);
        }
        if (const auto value = table["keys"]["ambiguity_timeout_ms"].value<int64_t>()) {
            keys->ambiguityTimeoutMs = std::clamp(static_cast<int>(*value), 0, 5000);
        }
    }

    for (const Section &section : kSections) {
        const auto sectionNode = table[section.name];
        const toml::table *actions = sectionNode.as_table();
        if (actions == nullptr) {
            continue;
        }

        for (const auto &[key, value] : *actions) {
            const QString actionId = QString::fromStdString(std::string(key.str()));

            const toml::array *bindings = value.as_array();
            if (bindings == nullptr) {
                result.issues.append(ConfigIssue{
                    .file = fileNameForIssues,
                    .line = lineOf(&value),
                    .column = 0,
                    .key = QStringLiteral("%1.%2").arg(QLatin1String(section.name), actionId),
                    .message =
                        QStringLiteral("expected a list of bindings, e.g. [\"j\", \"Down\"]")});
                continue;
            }

            // Mentioning an action replaces its bindings. An empty list is
            // therefore an unbind, which is what §8.2 specifies, without
            // needing to be a special case.
            keymap.unbind(section.layer, actionId);
            if (bindings->empty()) {
                ++result.actionsUnbound;
                continue;
            }

            for (const toml::node &entry : *bindings) {
                const auto spelling = entry.value<std::string>();
                if (!spelling) {
                    result.issues.append(ConfigIssue{
                        .file = fileNameForIssues,
                        .line = lineOf(&entry),
                        .column = 0,
                        .key = QStringLiteral("%1.%2").arg(QLatin1String(section.name), actionId),
                        .message = QStringLiteral("expected a string")});
                    continue;
                }

                QString error;
                const auto binding = input::parseBinding(QString::fromStdString(*spelling), &error);
                if (!binding.has_value()) {
                    result.issues.append(ConfigIssue{
                        .file = fileNameForIssues,
                        .line = lineOf(&entry),
                        .column = 0,
                        .key = QStringLiteral("%1.%2").arg(QLatin1String(section.name), actionId),
                        .message = error});
                    continue;
                }

                const QString claimKey = QStringLiteral("%1/%2")
                                             .arg(static_cast<int>(section.layer))
                                             .arg(input::bindingToString(*binding));

                if (claimed.contains(claimKey)) {
                    // Two bindings for the same chord *within this file*. §6.2:
                    // keep the one declared first, and make the loser visible.
                    result.issues.append(ConfigIssue{
                        .file = fileNameForIssues,
                        .line = lineOf(&entry),
                        .column = 0,
                        .key = QStringLiteral("%1.%2").arg(QLatin1String(section.name), actionId),
                        .message = QStringLiteral("'%1' is already bound earlier in this file")
                                       .arg(QString::fromStdString(*spelling))});
                    continue;
                }

                // Take the chord from whatever default holds it. Without this a
                // remap loses to the default it was written to replace —
                // binding list_down to "s" would be rejected because `s` is
                // focus_on_sidebar by default, which is precisely backwards:
                // the user asked for this and the default did not.
                keymap.removeBinding(section.layer, *binding);
                claimed.insert(claimKey);

                if (keymap.bind(section.layer, *binding, actionId)) {
                    ++result.bindingsApplied;
                }
            }
        }
    }

    qCDebug(pfKeys) << "applied" << result.bindingsApplied << "custom bindings,"
                    << result.actionsUnbound << "actions unbound";
    return result;
}

HotkeysLoadResult loadHotkeys(const QString &path, input::Keymap &keymap, Settings::Keys *keys)
{
    QFile file(path);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        HotkeysLoadResult result;
        result.issues.append(ConfigIssue{.file = QFileInfo(path).fileName(),
                                         .line = 0,
                                         .column = 0,
                                         .key = {},
                                         .message = file.errorString()});
        return result;
    }

    return applyHotkeys(QString::fromUtf8(file.readAll()), keymap, keys,
                        QFileInfo(path).fileName());
}

} // namespace pf::config
