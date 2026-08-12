#pragma once

#include <string>
#include <string_view>

namespace pf::config {

/// The default config.toml of §8.1, verbatim.
///
/// This is the single source of truth for defaults: `--print-default-config`
/// writes it, and Config parses it as the base layer that a user's file is
/// merged over. Keeping one copy means a default can never drift from what the
/// documentation and the printed template claim it is.
///
/// A constexpr string_view over a string literal is constant-initialised, so
/// this costs nothing at load time (§3.4).
inline constexpr std::string_view kDefaultConfigToml = R"TOML(# Panefile configuration.
#
# Every value below is a default; delete a line to keep the default, or the
# whole file to reset. Keys that are missing or malformed fall back to the
# default individually — a mistake in one section never discards the rest.
#
# Keybindings live in hotkeys.toml, colours in theme.toml.

[general]
new_panel_path       = "~"
restore_session      = true
confirm_on_quit      = false
single_instance      = true

[panels]
default_count        = 1
max_count            = 10
directories_first    = true
default_sort         = "name"       # name | size | modified | type
show_hidden          = false

[quicklook]
dock                 = "float"      # float | right | left | bottom | panel | full
float_size_percent   = 70           # of window, in float mode
dock_size_percent    = 35           # of window, in right/left/bottom modes
chrome               = true         # show header and hint bars
debounce_ms          = 120
max_read_bytes       = 67108864     # text and hex renderers
max_decode_mb        = 500          # above this, metadata card only
follow_cursor        = true         # false = snapshot on open, don't track
close_on_panel_switch = true        # float mode only

[thumbnails]
enabled              = true
video                = true
max_file_size_mb     = 200

[search]
fuzzy                = true
respect_gitignore    = true
max_results          = 10000

[operations]
confirm_delete       = true
confirm_trash        = false
default_conflict     = "ask"        # ask | overwrite | skip | rename
follow_symlinks      = false

[cli]
file_action          = "select"     # select | quicklook | launch
on_focused           = "current_panel"
on_unfocused         = "new_panel"

[external]
editor               = ""           # empty = $EDITOR, then $VISUAL, then nano
terminal             = ""           # empty = $TERMINAL, then heuristic
)TOML";

/// Parses kDefaultConfigToml and reports whether it is well-formed, writing the
/// parser's complaint to `errorOut` if not.
///
/// Exists so the test suite can assert that the template Panefile ships is
/// valid TOML. A typo in it would otherwise stay invisible until a user ran
/// --print-default-config and fed the result back in.
bool defaultConfigParses(std::string *errorOut = nullptr);

} // namespace pf::config
