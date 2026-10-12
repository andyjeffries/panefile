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

/// §6.3's tables, reworked so that letters belong to type-to-select.
///
/// The rule: a default's first chord carries Ctrl (⌘ on macOS) or Alt, or is a
/// key that types nothing — arrows, Return, Tab, Delete, F-keys and the like.
/// Typing a name in a panel moves the cursor to it, which a bare `j` bound to
/// list_down would steal. Two plain keys are kept on purpose, `.` and Space,
/// and the panel takes both back while a name is being typed. A test holds the
/// table to this.
///
/// The "go to" bindings sit behind a Ctrl+G leader, whose second key is plain:
/// `Ctrl+G h` goes home. The leader is what makes the plain key safe — it is
/// consumed as the end of a sequence and never reaches the list.
///
/// An action may appear more than once: §6.2 allows any number of bindings per
/// action, all active simultaneously.
// NOLINTBEGIN(modernize-use-designated-initializers): this is a lookup table,
// and its three columns are named by the struct immediately above it. Spelling
// out .layer/.actionId/.binding on every row would triple its height and make
// the one thing worth seeing at a glance — which key does what — harder to
// read, not easier.
constexpr DefaultBinding kDefaults[] = {
    // General
    {KeymapLayer::Global, "confirm", "Return"},
    {KeymapLayer::Global, "confirm", "Right"},
    // §6.3 binds `quit` to both `q` and `Esc`. Escape is bound to `cancel`
    // instead — see the note in Application::registerGlobalActions. Quitting
    // takes the chord the platform already uses: Cmd+Q on macOS, Ctrl+Q on
    // Linux, which Qt spells the same way.
    {KeymapLayer::Global, "cancel", "Escape"},
    {KeymapLayer::Global, "quit", "Ctrl+Q"},
    // F1 is help everywhere; Ctrl+? is GTK's "keyboard shortcuts" window.
    {KeymapLayer::Global, "open_help_menu", "F1"},
    {KeymapLayer::Global, "open_help_menu", "Ctrl+?"},
    {KeymapLayer::Global, "open_command_line", "Ctrl+Shift+R"},
    // The command-palette chord, which VS Code's palette also opens with `>`.
    {KeymapLayer::Global, "open_panefile_prompt", "Ctrl+Shift+P"},
    {KeymapLayer::Global, "open_fuzzy_find", "Ctrl+F"},

    // The default-file-manager bar's answers. Not plain `y` and `n`, which
    // would be typed names, and not chords anything else holds; enabled only
    // while the bar is asking, so outside it these keys do nothing.
    {KeymapLayer::Global, "default_file_manager_yes", "Alt+Y"},
    {KeymapLayer::Global, "default_file_manager_not_now", "Alt+N"},
    {KeymapLayer::Global, "default_file_manager_never", "Alt+Shift+N"},

    // Panel management. Panels behave like a browser's tabs, so they take the
    // tab chords; splitting takes VS Code's split-editor chord.
    {KeymapLayer::Global, "create_new_file_panel", "Ctrl+T"},
    {KeymapLayer::Global, "split_file_panel", "Ctrl+\\"},
    {KeymapLayer::Global, "close_file_panel", "Ctrl+W"},
    {KeymapLayer::Global, "next_file_panel", "Tab"},
    {KeymapLayer::Global, "next_file_panel", "Ctrl+PgDown"},
    {KeymapLayer::Global, "previous_file_panel", "Shift+Tab"},
    {KeymapLayer::Global, "previous_file_panel", "Ctrl+PgUp"},
    {KeymapLayer::Global, "quick_look", "Space"},
    {KeymapLayer::Global, "quick_look_cycle_dock", "Ctrl+Space"},
    {KeymapLayer::Global, "quick_look_fullscreen", "Ctrl+Shift+Space"},
    // VS Code's "toggle the bottom panel".
    {KeymapLayer::Global, "toggle_footer", "Ctrl+J"},
    {KeymapLayer::Normal, "focus_on_sidebar", "Ctrl+G s"},

    // §4's component table: "`s` focuses, `Ctrl+S` toggles visibility". The
    // toggle was registered but never bound, so the sidebar could be hidden
    // only by shrinking the window past 600 px.
    {KeymapLayer::Global, "toggle_sidebar", "Ctrl+S"},

    // Ctrl+, is what Qt maps to Cmd+, on macOS and is what every other desktop
    // application uses.
    {KeymapLayer::Global, "open_settings", "Ctrl+,"},
    // §7.11's unmount. Finder ejects with ⌘E, which Panefile spends on extract.
    {KeymapLayer::Global, "unmount_device", "Ctrl+Shift+E"},
    {KeymapLayer::Normal, "focus_on_process_bar", "Ctrl+G j"},
    {KeymapLayer::Global, "open_sort_options_menu", "Ctrl+O"},
    {KeymapLayer::Global, "toggle_reverse_sort", "Ctrl+Shift+O"},
    {KeymapLayer::Global, "equalise_panels", "Ctrl+="},

    // Movement
    {KeymapLayer::Global, "list_up", "Up"},
    {KeymapLayer::Global, "list_down", "Down"},
    {KeymapLayer::Global, "page_up", "PgUp"},
    {KeymapLayer::Global, "page_up", "Ctrl+U"},
    {KeymapLayer::Global, "page_down", "PgDown"},
    {KeymapLayer::Global, "page_down", "Ctrl+D"},
    {KeymapLayer::Global, "list_top", "Home"},
    {KeymapLayer::Normal, "list_top", "Ctrl+G g"},
    {KeymapLayer::Global, "list_bottom", "End"},
    {KeymapLayer::Normal, "list_bottom", "Ctrl+G G"},
    {KeymapLayer::Normal, "go_home", "Ctrl+G h"},
    {KeymapLayer::Global, "go_home", "Alt+Home"},
    {KeymapLayer::Normal, "go_root", "Ctrl+G r"},
    {KeymapLayer::Normal, "go_config", "Ctrl+G c"},
    {KeymapLayer::Normal, "go_trash", "Ctrl+G t"},
    // The wastebasket, as GNOME calls it; `t` for anyone who says trash.
    {KeymapLayer::Normal, "go_trash", "Ctrl+G w"},
    {KeymapLayer::Normal, "go_previous", "Ctrl+G p"},
    {KeymapLayer::Global, "parent_directory", "Left"},
    {KeymapLayer::Global, "parent_directory", "Backspace"},
    {KeymapLayer::Global, "go_back", "Alt+Left"},
    {KeymapLayer::Global, "go_forward", "Alt+Right"},
    // One of the two plain keys kept. Names are typed without their leading
    // dot, so `.` never needs to start one.
    {KeymapLayer::Normal, "toggle_dot_file", "."},
    {KeymapLayer::Global, "search_bar", "Ctrl+/"},
    {KeymapLayer::Global, "change_panel_mode", "Ctrl+Shift+V"},
    {KeymapLayer::Global, "select_all", "Ctrl+A"},

    // §6.1: in Selection mode "movement extends the selection" — so the
    // ordinary movement keys do, not only the shifted ones. The Selection layer
    // is consulted before Normal and Global, so these take the arrows away from
    // plain movement for as long as the mode is on.
    {KeymapLayer::Selection, "select_down", "Down"},
    {KeymapLayer::Selection, "select_up", "Up"},

    // And Shift+arrows extend in either mode, as in every other file manager.
    {KeymapLayer::Global, "select_up", "Shift+Up"},
    {KeymapLayer::Global, "select_down", "Shift+Down"},
    // A browser's bookmark chord, shifted because Ctrl+D pages down.
    {KeymapLayer::Global, "pinned_directory", "Ctrl+Shift+D"},

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
    {KeymapLayer::Global, "permanently_delete_items", "Shift+Delete"},
    {KeymapLayer::Global, "copy_path", "Ctrl+P"},
    {KeymapLayer::Global, "copy_present_working_directory", "Ctrl+Shift+C"},
    // §7.10 put compress on Ctrl+A; select_all has the better claim to it.
    {KeymapLayer::Global, "compress_file", "Ctrl+Shift+A"},
    {KeymapLayer::Global, "extract_file", "Ctrl+E"},
    // Midnight Commander's and Total Commander's edit key.
    {KeymapLayer::Global, "open_file_with_editor", "F4"},
    {KeymapLayer::Global, "open_current_directory_with_editor", "Shift+F4"},
    {KeymapLayer::Global, "open_with_default_app", "Ctrl+Return"},
    {KeymapLayer::Global, "open_terminal_here", "Ctrl+Shift+T"},
};
// NOLINTEND(modernize-use-designated-initializers)

} // namespace

QStringList unusableDefaultBindings()
{
    QStringList problems;
    for (const DefaultBinding &entry : kDefaults) {
        QString error;
        if (!parseBinding(QString::fromLatin1(entry.binding), &error).has_value()) {
            problems << QStringLiteral("%1: %2 — %3")
                            .arg(QLatin1String(entry.actionId), QLatin1String(entry.binding),
                                 error);
        }
    }
    return problems;
}

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
