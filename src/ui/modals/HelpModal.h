#pragma once

#include "ui/modals/Modal.h"

class QLineEdit;
class QTreeWidget;

namespace pf::input {
class ActionRegistry;
class Keymap;
} // namespace pf::input

namespace pf::ui {

/// The help modal of §6.3, generated entirely from the action registry.
///
/// §16: "Every new action goes in ActionRegistry with a description, or it
/// won't appear in the help modal." That is the point of generating it — the
/// help cannot drift from what the application actually does, because there is
/// no separate list to forget to update.
///
/// Bindings come from the keymap rather than from a table here, so a remapped
/// key shows its new binding without any code change, rendered as
/// `Ctrl+C  ·  Super+C  ·  y y` (§8.2).
class HelpModal : public Modal
{
    Q_OBJECT

public:
    HelpModal(const input::ActionRegistry &registry, const input::Keymap &keymap, QWidget *parent);

    /// Rebuilds from the registry and keymap. Called when either changes —
    /// after a hotkeys.toml reload, or when panels come and go and alter which
    /// actions are enabled.
    void refresh();

private:
    void applyFilter(const QString &text);

    const input::ActionRegistry *m_registry = nullptr;
    const input::Keymap *m_keymap = nullptr;

    QLineEdit *m_filter = nullptr;
    QTreeWidget *m_tree = nullptr;
};

} // namespace pf::ui
