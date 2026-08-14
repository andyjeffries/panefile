#pragma once

#include <QString>

namespace pf::config {

/// Changes one key in a TOML file without rewriting the rest of it.
///
/// The obvious implementation — parse to a table, set the value, serialise the
/// table back — throws away every comment in the file. Panefile's default
/// config is mostly comments explaining what each key does, so the first time
/// anyone opened a settings dialog their annotated file would come back as a
/// bare list of assignments. That is a straight downgrade, and it would happen
/// silently.
///
/// So nothing is serialised. The file is edited as text: the assignment is
/// found, and only its value is replaced. Comments, key order, blank lines,
/// alignment and anything the user added all survive, because nothing else is
/// touched.
///
/// This is deliberately a small, dumb text editor rather than a general TOML
/// writer. It handles the shapes Panefile's own files use — top-level tables,
/// one key per line, string, integer and boolean values — and refuses anything
/// it does not recognise rather than guessing.
class TomlWriter
{
public:
    /// The result of an edit. `changed` is false when the file already said
    /// this, which is worth knowing: rewriting a file to the same bytes still
    /// updates its mtime, and something is watching that.
    struct Result {
        bool ok = false;
        bool changed = false;
        QString error;
    };

    /// Sets `table.key` to `value`, which is written verbatim — the caller has
    /// already decided whether it needs quoting. Use the helpers below.
    ///
    /// An empty `table` means the top level, above the first `[table]` header.
    /// theme.toml keeps `name` and `follow_system` there, so this is not an
    /// edge case.
    ///
    /// Creates the file if it does not exist, and appends the table or the key
    /// if either is missing. A key that appears more than once in the same
    /// table has its *first* occurrence changed, which is the one TOML itself
    /// would honour.
    static Result setValue(const QString &filePath, const QString &table, const QString &key,
                           const QString &value);

    /// Removes `table.key` entirely, so the built-in default applies again.
    /// Absent keys are not an error — the file already says what was wanted.
    static Result removeValue(const QString &filePath, const QString &table, const QString &key);

    /// Formats a value for setValue. Strings are quoted and escaped; the others
    /// are written bare.
    static QString quote(const QString &text);
    static QString boolean(bool value);
    static QString number(long long value);
};

} // namespace pf::config
