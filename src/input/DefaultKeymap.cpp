#include "input/DefaultKeymap.h"

#include "input/Keymap.h"
#include "core/Logging.h"

namespace pf::input {
namespace {

struct DefaultBinding {
    KeymapLayer layer;
    const char *actionId;
    const char *binding;
};

/// §6.3's tables, verbatim, in the order they appear there.
///
/// An action may appear more than once: §6.2 allows any number of bindings per
/// action, all active simultaneously, so copy_items really is reachable from
/// Ctrl+C, Super+C and `y y` at the same time.
// NOLINTBEGIN(modernize-use-designated-initializers): this is a lookup table,
// and its three columns are named by the struct immediately above it. Spelling
// out .layer/.actionId/.binding on all ninety rows would triple its height and
// make the one thing worth seeing at a glance — which key does what — harder to
// read, not easier.
constexpr DefaultBinding kDefaults[] = {
    // General
    {KeymapLayer::Global, "confirm", "Return"},
    {KeymapLayer::Global, "confirm", "Right"},
    {KeymapLayer::Normal, "confirm", "l"},
    // §6.3 binds `quit` to both `q` and `Esc`. Escape is bound to `cancel`
    // instead — see the note in Application::registerGlobalActions. Quitting
    // takes the chord the platform already uses: Cmd+Q on macOS, Ctrl+Q on
    // Linux, which Qt spells the same way.
    {KeymapLayer::Global, "cancel", "Escape"},
    {KeymapLayer::Global, "quit", "Ctrl+Q"},
    {KeymapLayer::Normal, "quit", "q"},
    {KeymapLayer::Normal, "open_help_menu", "?"},
    {KeymapLayer::Normal, "open_command_line", ":"},
    {KeymapLayer::Normal, "open_panefile_prompt", ">"},
    {KeymapLayer::Global, "open_fuzzy_find", "Ctrl+F"},
    {KeymapLayer::Global, "toggle_theme_dark_light", "Ctrl+T"},

    // Panel management
    {KeymapLayer::Normal, "create_new_file_panel", "n"},
    {KeymapLayer::Normal, "split_file_panel", "N"},
    {KeymapLayer::Normal, "close_file_panel", "w"},
    // The platform's own "close" chord, for the same reason quit takes its own.
    {KeymapLayer::Global, "close_file_panel", "Ctrl+W"},
    {KeymapLayer::Global, "next_file_panel", "Tab"},
    {KeymapLayer::Normal, "next_file_panel", "L"},
    {KeymapLayer::Global, "previous_file_panel", "Shift+Tab"},
    {KeymapLayer::Normal, "previous_file_panel", "H"},
    {KeymapLayer::Global, "quick_look", "Space"},
    {KeymapLayer::Global, "quick_look_cycle_dock", "Ctrl+Space"},
    {KeymapLayer::Global, "quick_look_fullscreen", "Ctrl+Shift+Space"},
    {KeymapLayer::Normal, "toggle_footer", "F"},
    {KeymapLayer::Normal, "focus_on_sidebar", "s"},

    // §4's component table: "`s` focuses, `Ctrl+S` toggles visibility". The
    // toggle was registered but never bound, so the sidebar could be hidden
    // only by shrinking the window past 600 px.
    {KeymapLayer::Global, "toggle_sidebar", "Ctrl+S"},
    // §7.11: "`u` on a mounted device unmounts."
    {KeymapLayer::Normal, "unmount_device", "u"},
    {KeymapLayer::Normal, "focus_on_process_bar", "p"},
    {KeymapLayer::Normal, "open_sort_options_menu", "o"},
    {KeymapLayer::Normal, "toggle_reverse_sort", "R"},
    {KeymapLayer::Global, "equalise_panels", "Ctrl+="},

    // Movement
    {KeymapLayer::Normal, "list_up", "k"},
    {KeymapLayer::Global, "list_up", "Up"},
    {KeymapLayer::Normal, "list_down", "j"},
    {KeymapLayer::Global, "list_down", "Down"},
    {KeymapLayer::Global, "page_up", "PgUp"},
    {KeymapLayer::Global, "page_up", "Ctrl+U"},
    {KeymapLayer::Global, "page_down", "PgDown"},
    {KeymapLayer::Global, "page_down", "Ctrl+D"},
    {KeymapLayer::Normal, "list_top", "g g"},
    {KeymapLayer::Normal, "list_bottom", "G"},
    {KeymapLayer::Normal, "go_home", "g h"},
    {KeymapLayer::Global, "go_home", "Alt+Home"},
    {KeymapLayer::Normal, "go_root", "g r"},
    {KeymapLayer::Normal, "go_config", "g c"},
    {KeymapLayer::Normal, "go_trash", "g t"},
    {KeymapLayer::Normal, "go_previous", "g p"},
    {KeymapLayer::Normal, "parent_directory", "h"},
    {KeymapLayer::Global, "parent_directory", "Left"},
    {KeymapLayer::Global, "parent_directory", "Backspace"},
    {KeymapLayer::Global, "go_back", "Alt+Left"},
    {KeymapLayer::Global, "go_forward", "Alt+Right"},
    {KeymapLayer::Normal, "toggle_dot_file", "."},
    {KeymapLayer::Normal, "search_bar", "/"},
    // Esc already clears an active filter, ahead of its other meanings. This is
    // the explicit one, for anyone who wants a key that only ever does this —
    // and so the action appears in the help modal with a key beside it.
    {KeymapLayer::Normal, "clear_filter", "\\"},
    {KeymapLayer::Normal, "change_panel_mode", "v"},
    {KeymapLayer::Selection, "change_panel_mode", "v"},
    {KeymapLayer::Selection, "select_all", "A"},

    // Also outside Selection mode, where it is just as useful — selecting
    // everything in order to rename or move it does not need a mode first, and
    // Ctrl+A is not available for it because §7.10 spends that on compress.
    {KeymapLayer::Normal, "select_all", "A"},
    // §6.1: in Selection mode "movement extends the selection" — so the
    // ordinary movement keys do, not only the shifted ones. The Selection layer
    // is consulted before Normal and Global, so these take `j`, `k` and the
    // arrows away from plain movement for as long as the mode is on.
    {KeymapLayer::Selection, "select_down", "j"},
    {KeymapLayer::Selection, "select_down", "Down"},
    {KeymapLayer::Selection, "select_up", "k"},
    {KeymapLayer::Selection, "select_up", "Up"},

    // And the shifted forms keep working, so the habit transfers from a file
    // manager where they are the only way to do it.
    {KeymapLayer::Selection, "select_up", "K"},
    {KeymapLayer::Selection, "select_up", "Shift+Up"},
    {KeymapLayer::Selection, "select_down", "J"},
    {KeymapLayer::Selection, "select_down", "Shift+Down"},
    {KeymapLayer::Normal, "pinned_directory", "P"},

    // File operations
    {KeymapLayer::Global, "file_panel_item_create", "Ctrl+N"},
    {KeymapLayer::Global, "file_panel_item_rename", "Ctrl+R"},
    {KeymapLayer::Global, "bulk_rename", "Ctrl+B"},
    {KeymapLayer::Global, "copy_items", "Ctrl+C"},
    {KeymapLayer::Global, "cut_items", "Ctrl+X"},

    // §7.13: "Ctrl+Z undoes the last." The action was registered and reachable
    // from the command palette, but never bound — so the one keystroke standing
    // between a mistaken move and losing track of the files did nothing at all.
    // It is Cmd+Z on macOS, as Qt maps Qt::ControlModifier there.
    {KeymapLayer::Global, "undo", "Ctrl+Z"},
    {KeymapLayer::Global, "paste_items", "Ctrl+V"},
    // §6.3 lists Ctrl+D twice: for page_down in the Movement table and for
    // delete_items in the File operations table. Only one of them can have it.
    //
    // Movement keeps it, on the safer-failure argument. A superfile user who
    // presses Ctrl+D expecting a delete gets a page down, notices, and uses
    // Delete; a vim user who presses it expecting a half-page scroll and gets a
    // deletion prompt has been given a fright by their own muscle memory. It
    // also keeps Ctrl+U and Ctrl+D symmetrical, which is most of why anyone
    // reaches for them.
    //
    // delete_items keeps Delete, which is the discoverable binding and the one
    // every other file manager uses. Rebinding either is one line of
    // hotkeys.toml.
    {KeymapLayer::Global, "delete_items", "Delete"},
    {KeymapLayer::Normal, "permanently_delete_items", "D"},
    {KeymapLayer::Global, "permanently_delete_items", "Shift+Delete"},
    {KeymapLayer::Global, "copy_path", "Ctrl+P"},
    {KeymapLayer::Normal, "copy_present_working_directory", "c"},
    {KeymapLayer::Global, "compress_file", "Ctrl+A"},
    {KeymapLayer::Global, "extract_file", "Ctrl+E"},
    {KeymapLayer::Normal, "open_file_with_editor", "e"},
    {KeymapLayer::Normal, "open_current_directory_with_editor", "E"},
    {KeymapLayer::Global, "open_with_default_app", "Ctrl+Return"},
    {KeymapLayer::Normal, "open_terminal_here", "T"},
};
// NOLINTEND(modernize-use-designated-initializers)

} // namespace

void installDefaultKeymap(Keymap &keymap)
{
    for (const DefaultBinding &entry : kDefaults) {
        QString error;
        const std::optional<Binding> binding =
            parseBinding(QString::fromLatin1(entry.binding), &error);

        if (!binding.has_value()) {
            // A malformed default is a programming error rather than a user
            // one, so it is worth shouting about: the table above is the
            // application's own.
            qCWarning(pfKeys) << "default binding" << entry.binding << "for" << entry.actionId
                              << "is unusable:" << error;
            continue;
        }

        keymap.bind(entry.layer, *binding, QString::fromLatin1(entry.actionId));
    }

    // Selection mode inherits Normal's movement and operations; §6.1 says only
    // that movement *extends the selection* there, not that everything else
    // stops working. Copying the Normal layer under Selection would duplicate
    // the trie, so the dispatcher is given both layers in precedence order
    // instead (Selection first), and this is where that expectation is written
    // down.
}

} // namespace pf::input
