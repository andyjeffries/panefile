#include "input/Keymap.h"

#include "core/Logging.h"

namespace pf::input {

/// One node of the chord trie. `actionId` is set when a binding ends here;
/// `children` are the chords that continue a longer binding.
struct Keymap::Node {
    QString actionId;
    std::unordered_map<Chord, std::unique_ptr<Node>> children;

    bool isTerminal() const { return !actionId.isEmpty(); }
};

struct Keymap::Layer {
    Node root;
    /// Kept alongside the trie so the help modal can list an action's bindings
    /// without walking it. Insertion-ordered, because §6.2 renders them in the
    /// order they were declared.
    QList<QPair<QString, Binding>> bindings;
};

Keymap::Keymap() = default;
Keymap::~Keymap() = default;

Keymap::Layer &Keymap::layerFor(KeymapLayer layer)
{
    auto &slot = m_layers[static_cast<int>(layer)];
    if (!slot) {
        slot = std::make_unique<Layer>();
    }
    return *slot;
}

const Keymap::Layer *Keymap::findLayer(KeymapLayer layer) const
{
    const auto found = m_layers.find(static_cast<int>(layer));
    if (found == m_layers.end() || !found->second) {
        return nullptr;
    }
    return found->second.get();
}

bool Keymap::bind(KeymapLayer layer, const Binding &binding, const QString &actionId)
{
    if (binding.isEmpty() || actionId.isEmpty()) {
        return false;
    }

    Layer &target = layerFor(layer);
    Node *node = &target.root;

    for (const Chord &chord : binding) {
        auto &child = node->children[chord];
        if (!child) {
            child = std::make_unique<Node>();
        }
        node = child.get();
    }

    if (node->isTerminal() && node->actionId != actionId) {
        // §6.2: keep the one declared first, log both ids, and surface it in
        // the help modal so the user can see what they have done. Silently
        // overwriting would leave them with a binding that does not do what
        // their config says.
        m_conflicts.append(KeymapConflict{.layer = layer,
                                          .binding = binding,
                                          .keptActionId = node->actionId,
                                          .rejectedActionId = actionId});
        qCWarning(pfKeys) << "binding conflict:" << bindingToString(binding) << "is bound to"
                          << node->actionId << "and" << actionId << "— keeping" << node->actionId;
        return false;
    }

    if (!node->isTerminal()) {
        node->actionId = actionId;
        target.bindings.append({actionId, binding});
    }
    return true;
}

void Keymap::removeBinding(KeymapLayer layer, const Binding &binding)
{
    const auto slot = m_layers.find(static_cast<int>(layer));
    if (slot == m_layers.end() || !slot->second || binding.isEmpty()) {
        return;
    }
    Layer *target = slot->second.get();

    // Walk down remembering the path, then prune upward: a node stops being
    // useful once it is neither terminal nor a prefix of anything.
    QList<QPair<Node *, Chord>> path;
    Node *node = &target->root;
    for (const Chord &chord : binding) {
        const auto found = node->children.find(chord);
        if (found == node->children.end()) {
            return;
        }
        path.append({node, chord});
        node = found->second.get();
    }

    node->actionId.clear();

    for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {
        Node *parent = path[i].first;
        const Chord &chord = path[i].second;
        const auto found = parent->children.find(chord);
        if (found == parent->children.end() || found->second->isTerminal() ||
            !found->second->children.empty()) {
            break;
        }
        parent->children.erase(found);
    }

    target->bindings.removeIf(
        [&binding](const QPair<QString, Binding> &entry) { return entry.second == binding; });
}

void Keymap::unbind(KeymapLayer layer, const QString &actionId)
{
    const Layer *target = findLayer(layer);
    if (target == nullptr) {
        return;
    }

    QList<Binding> toRemove;
    for (const auto &[boundAction, binding] : target->bindings) {
        if (boundAction == actionId) {
            toRemove.append(binding);
        }
    }
    for (const Binding &binding : toRemove) {
        removeBinding(layer, binding);
    }
}

void Keymap::clear()
{
    m_layers.clear();
    m_conflicts.clear();
}

void Keymap::clearLayer(KeymapLayer layer)
{
    m_layers.erase(static_cast<int>(layer));
    m_conflicts.removeIf(
        [layer](const KeymapConflict &conflict) { return conflict.layer == layer; });
}

Keymap::Match Keymap::lookup(KeymapLayer layer, const Binding &pending) const
{
    const Layer *target = findLayer(layer);
    if (target == nullptr || pending.isEmpty()) {
        return {};
    }

    const Node *node = &target->root;
    for (const Chord &chord : pending) {
        const auto found = node->children.find(chord);
        if (found == node->children.end()) {
            return {};
        }
        node = found->second.get();
    }

    if (node->isTerminal()) {
        return Match{.type = MatchType::ExactMatch,
                     .actionId = node->actionId,
                     // §6.2: `g` bound while `g h` also exists. The caller
                     // starts the ambiguity timer rather than firing at once.
                     .hasLongerBinding = !node->children.empty()};
    }

    if (!node->children.empty()) {
        return Match{.type = MatchType::PartialMatch};
    }

    return {};
}

Keymap::Match Keymap::lookup(const QList<KeymapLayer> &layers, const Binding &pending) const
{
    // First match wins, layer by layer, which is §6.2's precedence rule stated
    // directly. It matters that a *partial* match in a higher-precedence layer
    // also wins: otherwise binding `g h` in a modal would be shadowed by a
    // global `g`, and the modal's sequence could never reach its second chord.
    for (const KeymapLayer layer : layers) {
        if (const Match match = lookup(layer, pending); match.type != MatchType::NoMatch) {
            return match;
        }
    }
    return {};
}

QList<Binding> Keymap::bindingsFor(KeymapLayer layer, const QString &actionId) const
{
    QList<Binding> result;
    const Layer *target = findLayer(layer);
    if (target == nullptr) {
        return result;
    }
    for (const auto &[boundAction, binding] : target->bindings) {
        if (boundAction == actionId) {
            result.append(binding);
        }
    }
    return result;
}

QStringList Keymap::boundActions(KeymapLayer layer) const
{
    QStringList result;
    const Layer *target = findLayer(layer);
    if (target == nullptr) {
        return result;
    }
    for (const auto &[boundAction, binding] : target->bindings) {
        if (!result.contains(boundAction)) {
            result.append(boundAction);
        }
    }
    return result;
}

const QList<KeymapConflict> &Keymap::conflicts() const
{
    return m_conflicts;
}

int Keymap::bindingCount(KeymapLayer layer) const
{
    const Layer *target = findLayer(layer);
    return target == nullptr ? 0 : static_cast<int>(target->bindings.size());
}

} // namespace pf::input
