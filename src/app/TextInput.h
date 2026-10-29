#pragma once

class QKeyEvent;
class QWidget;

namespace pf {

/// Whether a widget is somewhere the user types text: a line edit, a spin box,
/// or an editable text area. Read-only text — Quick Look's text and hex
/// previews — does not count, since there is nothing in it to edit.
bool isTextInput(const QWidget *widget);

/// Whether a key press is one of the platform's text-editing chords: select
/// all, copy, cut, paste, undo and redo, and the word and line movement and
/// deletion keys.
///
/// §6.2 step 2 hands only *bare* keys to a focused text input, and lets every
/// chord reach the keymap. That made Ctrl+A in the filter box select every
/// file, and Ctrl+V in the rename box paste files into the directory. These
/// chords mean what they mean in every text field, so they belong to it.
bool isTextEditingChord(const QKeyEvent *event);

/// Whether a widget is recording a shortcut: its dynamic property
/// `pfCapturesKeys` is true. The dispatcher and Quick Look must not see its key
/// presses, or pressing Ctrl+K to record it would open the palette instead.
bool capturesKeys(const QWidget *widget);

} // namespace pf
