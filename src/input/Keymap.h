#pragma once

#include "input/Chord.h"

#include <QList>
#include <QString>

#include <memory>
#include <unordered_map>

namespace pf::input {

/// Layers a binding can live in, resolved in the order of §6.2's precedence
/// rules: an active modal's own bindings first, then the panel's mode
/// (Selection before Normal), then global.
enum class KeymapLayer {
    Modal,
    Selection,
    Normal,
    Global,
    Typing, ///< active while a text input has focus; §6.1
};

/// Two bindings competing for the same chord sequence within one layer (§6.2).
struct KeymapConflict {
    KeymapLayer layer = KeymapLayer::Normal;
    Binding binding;
    QString keptActionId;
    QString rejectedActionId;
};

/// Maps chord sequences to action ids (§6.2).
///
/// Stored as a trie keyed on chords, not a flat hash. That is what makes
/// arbitrary-length sequences work, and — more importantly — it is what lets a
/// lookup distinguish "no such binding" from "a prefix of something longer".
/// Without that distinction a mistyped `g` followed by a stray key would leak
/// the stray key into the widget underneath.
class Keymap
{
public:
    enum class MatchType {
        NoMatch,
        PartialMatch, ///< a prefix of at least one longer binding
        ExactMatch,
    };

    struct Match {
        MatchType type = MatchType::NoMatch;
        QString actionId;

        /// True when the buffer both completes a binding *and* prefixes a
        /// longer one — `g` bound while `g h` also exists. §6.2 resolves this
        /// with the ambiguity timer rather than by picking one.
        bool hasLongerBinding = false;
    };

    Keymap();
    ~Keymap();

    Keymap(const Keymap &) = delete;
    Keymap &operator=(const Keymap &) = delete;

    /// Binds a sequence in a layer.
    ///
    /// An action may have any number of bindings, all active at once (§6.2), so
    /// binding the same action again adds to it rather than replacing.
    ///
    /// Two actions bound to the identical sequence within one layer is a config
    /// error: §6.2 says keep the one declared first, log both ids, and show the
    /// conflict in the help modal. Returns false in that case, having kept the
    /// existing binding, and records the conflict.
    bool bind(KeymapLayer layer, const Binding &binding, const QString &actionId);

    /// Removes every binding for an action in a layer. §8.2's `unbind`
    /// pseudo-action and `open_zoxide = []` both land here.
    void unbind(KeymapLayer layer, const QString &actionId);

    /// Removes one specific binding.
    void removeBinding(KeymapLayer layer, const Binding &binding);

    void clear();
    void clearLayer(KeymapLayer layer);

    /// Resolves a pending chord buffer against one layer.
    Match lookup(KeymapLayer layer, const Binding &pending) const;

    /// Resolves against several layers in order, returning the first exact or
    /// partial match. Layers are searched in the order given, which is the
    /// caller's expression of §6.2's precedence.
    Match lookup(const QList<KeymapLayer> &layers, const Binding &pending) const;

    /// Every binding for an action in a layer, in the order they were added, so
    /// the help modal can render `Ctrl+C  ·  Super+C  ·  y y`.
    QList<Binding> bindingsFor(KeymapLayer layer, const QString &actionId) const;

    /// Every action id that has at least one binding in a layer.
    QStringList boundActions(KeymapLayer layer) const;

    /// Conflicts found while binding, for the help modal to display (§6.2).
    const QList<KeymapConflict> &conflicts() const;

    int bindingCount(KeymapLayer layer) const;

private:
    struct Node;
    struct Layer;

    Layer &layerFor(KeymapLayer layer);
    const Layer *findLayer(KeymapLayer layer) const;

    // std::unordered_map rather than QHash: the trie owns its nodes through
    // unique_ptr, and QHash::value() returns by value, which a move-only
    // mapped type cannot satisfy.
    std::unordered_map<int, std::unique_ptr<Layer>> m_layers;
    QList<KeymapConflict> m_conflicts;
};

} // namespace pf::input
