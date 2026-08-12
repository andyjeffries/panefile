#include "app/KeyDispatcher.h"

#include "input/ActionRegistry.h"
#include "core/Logging.h"

#include <QKeyEvent>

namespace pf {

using input::Chord;
using input::Keymap;
using input::KeymapLayer;

namespace {
// §8.2's defaults.
constexpr int kDefaultSequenceTimeoutMs = 1000;
constexpr int kDefaultAmbiguityTimeoutMs = 500;
} // namespace

KeyDispatcher::KeyDispatcher(input::ActionRegistry *registry, Keymap *keymap, QObject *parent)
    : QObject(parent), m_registry(registry), m_keymap(keymap)
{
    m_layers = {KeymapLayer::Normal, KeymapLayer::Global};

    m_sequenceTimer.setSingleShot(true);
    m_sequenceTimer.setInterval(kDefaultSequenceTimeoutMs);
    connect(&m_sequenceTimer, &QTimer::timeout, this, &KeyDispatcher::onSequenceTimeout);

    m_ambiguityTimer.setSingleShot(true);
    m_ambiguityTimer.setInterval(kDefaultAmbiguityTimeoutMs);
    connect(&m_ambiguityTimer, &QTimer::timeout, this, &KeyDispatcher::onAmbiguityTimeout);
}

void KeyDispatcher::setActiveLayers(const QList<KeymapLayer> &layers)
{
    if (m_layers == layers) {
        return;
    }
    m_layers = layers;
    // The buffer belonged to the old set of layers; a half-typed sequence must
    // not complete against a different one.
    clearPending();
}

QList<KeymapLayer> KeyDispatcher::activeLayers() const
{
    return m_layers;
}

void KeyDispatcher::setSequenceTimeout(int milliseconds)
{
    m_sequenceTimer.setInterval(std::max(0, milliseconds));
}

void KeyDispatcher::setAmbiguityTimeout(int milliseconds)
{
    m_ambiguityTimer.setInterval(std::max(0, milliseconds));
}

bool KeyDispatcher::hasPending() const
{
    return !m_pending.isEmpty();
}

QString KeyDispatcher::pendingText() const
{
    if (m_pending.isEmpty()) {
        return {};
    }
    // §6.2 step 3 shows the pending prefix in the footer as `g-`; the trailing
    // dash is what tells the user the application is waiting for them rather
    // than having ignored the key.
    return input::bindingToString(m_pending) + QLatin1Char('-');
}

void KeyDispatcher::clearPending()
{
    m_sequenceTimer.stop();
    m_ambiguityTimer.stop();
    m_ambiguousActionId.clear();

    if (m_pending.isEmpty()) {
        return;
    }
    m_pending.clear();
    Q_EMIT pendingChanged({});
}

void KeyDispatcher::fire(const QString &actionId)
{
    clearPending();
    if (m_registry != nullptr && m_registry->invoke(actionId)) {
        Q_EMIT actionInvoked(actionId);
    }
}

void KeyDispatcher::onSequenceTimeout()
{
    qCDebug(pfKeys) << "sequence timed out with" << input::bindingToString(m_pending) << "pending";
    clearPending();
}

void KeyDispatcher::onAmbiguityTimeout()
{
    // Nothing followed the ambiguous prefix, so the shorter binding was what
    // the user meant.
    const QString actionId = m_ambiguousActionId;
    if (!actionId.isEmpty()) {
        fire(actionId);
    }
}

bool KeyDispatcher::handleKeyPress(QKeyEvent *event)
{
    if (m_keymap == nullptr || m_registry == nullptr) {
        return false;
    }

    const std::optional<Chord> chord =
        input::chordFromKeyEvent(event->key(), event->modifiers(), event->text());
    if (!chord.has_value()) {
        // A bare modifier press. Not consumed, and it leaves the buffer alone.
        return false;
    }

    // §6.2 step 4: Escape always clears the pending buffer, and is otherwise
    // handled normally — so an Escape that cancels a half-typed sequence is
    // consumed, while an Escape with nothing pending falls through to whatever
    // binding or widget wants it.
    if (chord->key == Qt::Key_Escape && !m_pending.isEmpty()) {
        clearPending();
        return true;
    }

    // A key arriving while the ambiguity timer runs resolves the ambiguity in
    // favour of the longer binding, so the timer must stop before the lookup.
    m_ambiguityTimer.stop();
    m_ambiguousActionId.clear();

    input::Binding candidate = m_pending;
    candidate.append(*chord);

    const Keymap::Match match = m_keymap->lookup(m_layers, candidate);

    switch (match.type) {
    case Keymap::MatchType::ExactMatch:
        if (match.hasLongerBinding) {
            // §6.2: both a complete binding and a prefix of a longer one. Hold
            // it, and fire the shorter action only if nothing else arrives.
            m_pending = candidate;
            m_ambiguousActionId = match.actionId;
            m_ambiguityTimer.start();
            m_sequenceTimer.start();
            Q_EMIT pendingChanged(pendingText());
            return true;
        }
        fire(match.actionId);
        return true;

    case Keymap::MatchType::PartialMatch:
        m_pending = candidate;
        m_sequenceTimer.start();
        Q_EMIT pendingChanged(pendingText());
        return true;

    case Keymap::MatchType::NoMatch:
        break;
    }

    // §6.2 step 3: a mistyped sequence must not leak keys into the application.
    // With a buffer, the key is swallowed along with the buffer; with no
    // buffer, the event was never ours and falls through to the widget.
    if (!m_pending.isEmpty()) {
        qCDebug(pfKeys) << "no binding for" << input::bindingToString(candidate)
                        << "— discarding the sequence";
        clearPending();
        return true;
    }

    return false;
}

} // namespace pf
