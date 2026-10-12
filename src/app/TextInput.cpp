#include "app/TextInput.h"

#include <QAbstractSpinBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

#include <algorithm>
#include <array>

namespace pf {

bool isTextInput(const QWidget *widget)
{
    if (widget == nullptr) {
        return false;
    }
    // A spin box keeps the focus itself rather than handing it to the line
    // edit inside, so it has to be named: digits typed into it, and `.` above
    // all, would otherwise reach the keymap.
    if (qobject_cast<const QLineEdit *>(widget) != nullptr ||
        qobject_cast<const QAbstractSpinBox *>(widget) != nullptr) {
        return true;
    }
    if (const auto *plain = qobject_cast<const QPlainTextEdit *>(widget)) {
        return !plain->isReadOnly();
    }
    if (const auto *rich = qobject_cast<const QTextEdit *>(widget)) {
        return !rich->isReadOnly();
    }
    return false;
}

bool isTextEditingChord(const QKeyEvent *event)
{
    // QKeySequence's standard keys rather than a list of chords, so that each
    // platform's own spelling applies: ⌘← moves to the start of the line on
    // macOS, Ctrl+U clears it on Linux.
    static constexpr std::array kEditingKeys{
        QKeySequence::SelectAll,
        QKeySequence::Copy,
        QKeySequence::Cut,
        QKeySequence::Paste,
        QKeySequence::Undo,
        QKeySequence::Redo,
        QKeySequence::MoveToNextWord,
        QKeySequence::MoveToPreviousWord,
        QKeySequence::MoveToStartOfLine,
        QKeySequence::MoveToEndOfLine,
        QKeySequence::MoveToStartOfDocument,
        QKeySequence::MoveToEndOfDocument,
        QKeySequence::SelectNextWord,
        QKeySequence::SelectPreviousWord,
        QKeySequence::SelectStartOfLine,
        QKeySequence::SelectEndOfLine,
        QKeySequence::SelectStartOfDocument,
        QKeySequence::SelectEndOfDocument,
        QKeySequence::DeleteStartOfWord,
        QKeySequence::DeleteEndOfWord,
        QKeySequence::DeleteEndOfLine,
        QKeySequence::DeleteCompleteLine,
    };
    return std::ranges::any_of(
        kEditingKeys, [event](QKeySequence::StandardKey key) { return event->matches(key); });
}

} // namespace pf
