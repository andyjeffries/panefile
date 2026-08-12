#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <functional>

namespace pf::input {

/// Grouping used by the help modal, matching the headings of §6.3.
enum class ActionCategory {
    General,
    Panels,
    Movement,
    Selection,
    FileOperations,
    View,
};

struct Action {
    QString id;          ///< stable, e.g. "list_down"
    QString description; ///< human-readable, shown in the help modal
    ActionCategory category = ActionCategory::General;
    std::function<void()> handler;

    /// False when the action exists but cannot run right now — closing the last
    /// panel, pasting with an empty clipboard. The help modal still lists it;
    /// invoking it is a no-op with a footer message rather than nothing at all.
    std::function<bool()> isEnabled;
};

/// id → callable, and the only dispatch path in the application (§6.2).
///
/// "Nothing in the UI may call a behaviour function directly — menu items,
/// keybindings and the command prompt all resolve through the registry. This is
/// what makes remapping and the help modal work for free."
///
/// The registry deliberately knows nothing about keys. Bindings live in the
/// Keymap and are resolved to an id before anything reaches here, which is what
/// lets the same action be invoked from a keystroke, the `>` prompt or a menu
/// without three code paths.
class ActionRegistry
{
public:
    /// Registers an action. An id registered twice replaces the first — the
    /// composition root rebuilds handlers when panels change, and rejecting the
    /// second registration would leave stale captures behind.
    void registerAction(Action action);

    void registerAction(const QString &id, const QString &description, ActionCategory category,
                        std::function<void()> handler, std::function<bool()> isEnabled = {});

    /// Runs an action. Returns false when the id is unknown or the action is
    /// currently disabled; the caller decides whether that deserves a message.
    bool invoke(const QString &id);

    bool contains(const QString &id) const;
    bool isEnabled(const QString &id) const;

    const Action *find(const QString &id) const;

    /// All ids, in registration order, so the help modal groups by category
    /// while keeping a stable order within each.
    QStringList ids() const;

    QList<const Action *> byCategory(ActionCategory category) const;

    static QString categoryTitle(ActionCategory category);

    void clear();

private:
    QHash<QString, Action> m_actions;
    QStringList m_order;
};

} // namespace pf::input
