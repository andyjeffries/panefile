#include "input/Chord.h"

#include <QHashFunctions>
#include <QKeySequence>
#include <QRegularExpression>

namespace pf::input {
namespace {

/// Modifier names users type that Qt does not recognise, mapped to ones it
/// does. §8.2: "Accept Super+C as an alias for Meta+C, because that's what
/// users will type."
QString normaliseModifierNames(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\b(super|cmd|command|win|windows)\\s*\\+"),
        QRegularExpression::CaseInsensitiveOption);

    QString normalised = text;
    normalised.replace(pattern, QStringLiteral("Meta+"));
    return normalised;
}

bool isModifierKey(int key)
{
    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return true;
    default:
        return false;
    }
}

/// Whether a piece of text is a single character that a key press can produce
/// and that a config file can name directly.
///
/// Whitespace is excluded on purpose. Space, Tab, Return and Backspace all have
/// text — " ", "\t", "\r", "\b" — but a config names them `Space`, `Tab`,
/// `Return` and `Backspace`, which are key-form chords. Letting them take the
/// text form would mean the two spellings never matched each other.
bool isTextChordCandidate(const QString &text)
{
    if (text.size() != 1) {
        return false;
    }
    const QChar character = text.at(0);
    return character.isPrint() && !character.isSpace();
}

/// Modifiers that change which binding is meant, as opposed to which character
/// was produced.
///
/// Shift is excluded: on a text-form chord it is already expressed by the
/// character itself. `J` is Shift+j on every layout, and recording the Shift
/// separately would make `J` and `Shift+J` different bindings that look
/// identical in a config file.
Qt::KeyboardModifiers significantModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

} // namespace

std::optional<Chord> parseChord(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    // A literal comma is Qt's own sequence separator, so a chord containing one
    // would parse as several elements. Rejecting it gives a clear error rather
    // than a binding that silently means something else.
    if (trimmed.contains(QLatin1Char(','))) {
        return std::nullopt;
    }

    // A bare printable character is a text-form chord, and must not go through
    // QKeySequence — that is what would collapse `j` and `J` into one binding.
    if (isTextChordCandidate(trimmed)) {
        return Chord{.modifiers = Qt::NoModifier, .key = 0, .text = trimmed};
    }

    const QKeySequence sequence =
        QKeySequence::fromString(normaliseModifierNames(trimmed), QKeySequence::PortableText);
    if (sequence.count() != 1) {
        return std::nullopt;
    }

    const QKeyCombination combination = sequence[0];
    if (combination.key() == Qt::Key_unknown || combination.key() == 0) {
        return std::nullopt;
    }

    // A modifier plus a printable key still uses the key form, because the
    // character a modified key produces is not dependable: Ctrl+C yields a
    // control character, or nothing at all, depending on the platform.
    return Chord{
        .modifiers = combination.keyboardModifiers(), .key = combination.key(), .text = {}};
}

std::optional<Binding> parseBinding(const QString &text, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<Binding> {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };

    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList parts = text.trimmed().split(whitespace, Qt::SkipEmptyParts);

    if (parts.isEmpty()) {
        return fail(QStringLiteral("binding is empty"));
    }
    if (parts.size() > kMaxChordsPerBinding) {
        return fail(QStringLiteral("binding has %1 chords; the maximum is %2")
                        .arg(parts.size())
                        .arg(kMaxChordsPerBinding));
    }

    Binding binding;
    binding.reserve(parts.size());

    for (const QString &part : parts) {
        const std::optional<Chord> chord = parseChord(part);
        if (!chord.has_value()) {
            return fail(QStringLiteral("'%1' is not a key").arg(part));
        }
        binding.append(*chord);
    }

    // §8.2: reject a sequence whose first chord is Escape. Escape always clears
    // the pending buffer (§6.2 step 4), so it could never reach a second chord.
    if (binding.size() > 1 && binding.constFirst().key == Qt::Key_Escape) {
        return fail(QStringLiteral("a sequence cannot start with Escape, which always "
                                   "clears the pending keys"));
    }

    return binding;
}

QString chordToString(const Chord &chord)
{
    if (!chord.isValid()) {
        return {};
    }
    if (chord.isTextForm()) {
        return chord.text;
    }
    // NativeText renders ⌘, ⌥ and ⇧ on macOS and Ctrl/Alt/Shift elsewhere, so
    // the help modal shows what is printed on the user's own keyboard.
    return QKeySequence(QKeyCombination(chord.modifiers, static_cast<Qt::Key>(chord.key)))
        .toString(QKeySequence::NativeText);
}

QString bindingToString(const Binding &binding)
{
    QStringList parts;
    parts.reserve(binding.size());
    for (const Chord &chord : binding) {
        parts << chordToString(chord);
    }
    return parts.join(QLatin1Char(' '));
}

std::optional<Chord> chordFromKeyEvent(int key, Qt::KeyboardModifiers modifiers,
                                       const QString &eventText)
{
    // §6.2 step 1: a bare modifier press never advances the buffer. Without
    // this, reaching for Ctrl before pressing C would clear a pending sequence.
    if (key == 0 || isModifierKey(key)) {
        return std::nullopt;
    }

    // The keypad modifier says which physical key produced the event rather
    // than what the user pressed, and is never written in a config file.
    modifiers &= ~Qt::KeypadModifier;

    const Qt::KeyboardModifiers significant = significantModifiers(modifiers);

    if (significant == Qt::NoModifier && isTextChordCandidate(eventText)) {
        return Chord{.modifiers = Qt::NoModifier, .key = 0, .text = eventText};
    }

    return Chord{.modifiers = modifiers, .key = key, .text = {}};
}

size_t qHash(const Chord &chord, size_t seed) noexcept
{
    return qHashMulti(seed, static_cast<int>(chord.modifiers), chord.key, chord.text);
}

} // namespace pf::input
