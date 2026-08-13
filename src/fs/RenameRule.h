#pragma once

#include <QList>
#include <QString>

namespace pf::fs {

/// The three modes of the rename sheet.
///
/// These are macOS Finder's "Rename Finder Items" modes, chosen over §7.9's
/// `$EDITOR` round trip. The spec's flow — write basenames to a temp file,
/// block on an editor, diff the result — asks the user to leave the
/// application, and every rename it can express in a text editor is a rename
/// one of these three does directly and reversibly, with a live preview instead
/// of a diff after the fact.
///
/// What is kept from §7.9 is everything that matters underneath: the cycle
/// detection of step 4, the confirmation of step 5, and the single undoable job
/// of step 6.
enum class RenameMode {
    ReplaceText, ///< find and replace within each name
    AddText,     ///< prefix or suffix
    Format,      ///< name plus an index, counter or date
};

/// Where added text goes.
enum class AddPosition {
    Before,
    After,
};

/// The shape of the generated name in Format mode.
enum class NameFormat {
    NameAndIndex,   ///< Custom Format 1, Custom Format 2…
    NameAndCounter, ///< Custom Format 00001…
    NameAndDate,    ///< Custom Format 2026-08-13 at 09.41
};

/// Where the format's number or date goes, relative to the custom text.
enum class FormatPosition {
    AfterName,
    BeforeName,
};

/// One rename rule, as the sheet's controls describe it.
///
/// A value type with no UI in it, so §14 can test the naming rules — which is
/// where every off-by-one in a bulk rename lives — without a widget.
struct RenameRule {
    RenameMode mode = RenameMode::ReplaceText;

    // -------------------------------------------------------- replace text
    QString find;
    QString replaceWith;

    /// Finder's replacement is case-insensitive; the sheet exposes the choice
    /// because a case-only rename is otherwise impossible to express.
    bool caseSensitive = false;

    // ------------------------------------------------------------ add text
    QString addText;
    AddPosition addPosition = AddPosition::Before;

    // -------------------------------------------------------------- format
    NameFormat nameFormat = NameFormat::NameAndIndex;
    FormatPosition formatPosition = FormatPosition::AfterName;
    QString customText;
    int startNumber = 1;

    /// Applies the rule to one name. `index` is the item's zero-based position
    /// in the selection, which only Format mode uses.
    ///
    /// The extension is never treated specially: Finder renames the whole
    /// visible name, and a user who wanted to keep `.txt` can see in the
    /// preview whether they have.
    QString apply(const QString &name, int index) const;

    /// Applies the rule to a whole selection, in order.
    QList<QString> applyAll(const QList<QString> &names) const;

    /// A one-line description of what the rule will do, for the sheet's
    /// example row.
    QString exampleFor(const QString &name) const;
};

} // namespace pf::fs
