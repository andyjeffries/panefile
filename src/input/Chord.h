#pragma once

#include <QList>
#include <QString>
#include <Qt>

#include <functional>
#include <optional>

namespace pf::input {

/// A set of modifiers plus one key: `Ctrl+C`, `Super+C`, `g`, `Shift+G` (§6.2).
///
/// A chord is stored in one of two forms, and which one matters:
///
///   * **Text form** — `text` holds the character the key produces, and `key`
///     is zero. Used for bare printable keys with no Ctrl/Alt/Meta.
///   * **Key form** — `key` holds a Qt key code and `text` is empty. Used for
///     everything else: named keys, and anything with a modifier.
///
/// The split exists because QKeySequence cannot represent what §6.3's default
/// keymap requires. It parses `j` and `J` to the same value — Key_J — yet the
/// defaults bind `j` to list_down and `J` to select_down, which have to stay
/// distinct. Matching bare keys on the character they produce also makes
/// non-US layouts work: `?` is Shift+/ on one keyboard and its own key on
/// another, and in both cases the text is "?".
struct Chord {
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    int key = 0;
    QString text;

    bool operator==(const Chord &other) const = default;

    bool isValid() const { return key != 0 || !text.isEmpty(); }
    bool isTextForm() const { return !text.isEmpty(); }
};

/// A binding is a sequence of one or more chords. Size 1 is the simple case;
/// anything longer must be pressed in order (§6.2).
using Binding = QList<Chord>;

/// §8.2 rejects sequences longer than this.
inline constexpr int kMaxChordsPerBinding = 5;

/// Parses a single chord.
///
/// §8.2 asks for QKeySequence::fromString, and it is used for the key form,
/// rejecting anything that comes back as more than one element — Qt's own
/// comma-separated syntax is not used here, because whitespace is the sequence
/// separator and the two would collide.
///
/// `Super`, `Cmd` and `Command` are accepted as aliases for `Meta` and
/// normalised, because that is what users type. On macOS Qt maps
/// ControlModifier to Command and MetaModifier to Control, so a config written
/// as `Ctrl+C` gives Cmd+C there — which is what a Mac user expects.
std::optional<Chord> parseChord(const QString &text);

/// Parses a whole binding: chords separated by whitespace.
///
/// Returns nothing, and sets `error` if given, when the binding is unparseable,
/// longer than kMaxChordsPerBinding, or is a sequence beginning with Escape —
/// §8.2 reserves Escape for clearing the pending buffer (§6.2 step 4), so such
/// a sequence could never reach its second chord.
std::optional<Binding> parseBinding(const QString &text, QString *error = nullptr);

/// Renders a chord the way the help modal shows it, using the platform's own
/// conventions — `⌘C` on macOS, `Ctrl+C` elsewhere.
QString chordToString(const Chord &chord);

/// Renders a binding, e.g. "g h" or "Ctrl+C".
QString bindingToString(const Binding &binding);

/// The chord a key event represents, or nothing when the event is a bare
/// modifier press — §6.2 step 1: modifiers alone never advance the buffer.
///
/// Produces the same form parseChord() would for the equivalent config text,
/// which is what lets the two be compared at all.
std::optional<Chord> chordFromKeyEvent(int key, Qt::KeyboardModifiers modifiers,
                                       const QString &eventText);

size_t qHash(const Chord &chord, size_t seed = 0) noexcept;

} // namespace pf::input

template<>
struct std::hash<pf::input::Chord> {
    size_t operator()(const pf::input::Chord &chord) const noexcept
    {
        return pf::input::qHash(chord);
    }
};
