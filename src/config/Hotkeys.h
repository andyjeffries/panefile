#pragma once

#include "config/Config.h"

#include <QString>

namespace pf::input {
class Keymap;
}

namespace pf::config {

struct HotkeysLoadResult {
    QList<ConfigIssue> issues;
    int bindingsApplied = 0;
    int actionsUnbound = 0;
};

/// Applies a `hotkeys.toml` over a keymap that already holds the defaults (§8.2).
///
/// Over, not instead of: an action the user does not mention keeps its default
/// binding. Mentioning an action *replaces* its bindings rather than adding to
/// them, because `list_down = ["j"]` plainly means "j and nothing else" — a
/// user who wanted to add would have written both.
///
/// `open_zoxide = []` therefore unbinds, which is what §8.2 says an empty list
/// does, and falls out of the same rule rather than needing a special case.
///
/// §3.4 wants this off the critical path: the default keymap is bound before
/// the window exists so the first keypress is never dropped, and this runs on
/// idle afterwards.
HotkeysLoadResult applyHotkeys(const QString &text, input::Keymap &keymap,
                               Settings::Keys *keys = nullptr,
                               const QString &fileNameForIssues = {});

/// Reads hotkeys.toml and applies it. A missing file is not an issue.
HotkeysLoadResult loadHotkeys(const QString &path, input::Keymap &keymap,
                              Settings::Keys *keys = nullptr);

} // namespace pf::config
