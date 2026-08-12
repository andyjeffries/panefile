#pragma once

#include "input/Chord.h"
#include "input/Keymap.h"

#include <QObject>
#include <QTimer>

class QKeyEvent;

namespace pf::input {
class ActionRegistry;
}

namespace pf {

/// Resolves key events to actions (§6.2).
///
/// Installed as a single application-level event filter. §6.2 rules out
/// QShortcut and QAction shortcuts explicitly: they cap out at four elements,
/// resolve ambiguity in ways the application cannot control, and have no notion
/// of a mode. All three matter here.
///
/// The interesting part is the pending-chord buffer and its two timers, which
/// are different things and are often conflated:
///
///   * The **sequence timer** bounds how long a started sequence may stay
///     unfinished. Press `g` and wander off, and after a second the buffer
///     clears rather than waiting forever to interpret the next key as the
///     second half of something.
///
///   * The **ambiguity timer** handles a buffer that is *both* a complete
///     binding and a prefix of a longer one — `g` bound while `g h` also
///     exists. Firing immediately would make `g h` unreachable; waiting
///     forever would make `g` unreachable. §6.2 waits briefly, and fires the
///     shorter action if nothing else arrives.
class KeyDispatcher : public QObject
{
    Q_OBJECT

public:
    KeyDispatcher(input::ActionRegistry *registry, input::Keymap *keymap,
                  QObject *parent = nullptr);

    /// Layers consulted, in precedence order (§6.2). The composition root
    /// updates this as modals open and panel modes change.
    void setActiveLayers(const QList<input::KeymapLayer> &layers);
    QList<input::KeymapLayer> activeLayers() const;

    void setSequenceTimeout(int milliseconds);
    void setAmbiguityTimeout(int milliseconds);

    /// Handles one key press. Returns true when the event was consumed and must
    /// not reach the widget underneath.
    bool handleKeyPress(QKeyEvent *event);

    /// §6.2 step 5: any pointer click, focus change or panel switch clears the
    /// buffer. A half-typed sequence surviving a click would fire against a
    /// panel the user is no longer looking at.
    void clearPending();

    /// The pending prefix, rendered for the footer as `g-` (§6.2 step 3).
    QString pendingText() const;

    bool hasPending() const;

Q_SIGNALS:
    /// Emitted whenever the pending buffer changes, so the footer can show it.
    void pendingChanged(const QString &text);

    /// An action fired. The footer uses this to clear any stale message.
    void actionInvoked(const QString &actionId);

private:
    void fire(const QString &actionId);
    void onSequenceTimeout();
    void onAmbiguityTimeout();

    input::ActionRegistry *m_registry = nullptr;
    input::Keymap *m_keymap = nullptr;

    QList<input::KeymapLayer> m_layers;
    input::Binding m_pending;

    /// The action an ambiguous buffer would fire if nothing further arrives.
    QString m_ambiguousActionId;

    QTimer m_sequenceTimer;
    QTimer m_ambiguityTimer;
};

} // namespace pf
