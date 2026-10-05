#pragma once

#include <QStringList>

namespace pf::input {

class Keymap;

/// Installs the default bindings of §6.3, derived from superfile's so muscle
/// memory carries over. Every one of them is remappable in hotkeys.toml (M3),
/// which merges over this rather than replacing it.
///
/// §3.4 wants a minimal hardcoded map bound before the first keypress can
/// arrive, with the parsed hotkeys.toml swapped in afterwards, so that the very
/// first key is never dropped. This is that map, and it is complete rather than
/// minimal: building it is a few hundred hash insertions with no file I/O, and
/// the alternative — two maps that can disagree — is worse than the microsecond
/// it costs.
void installDefaultKeymap(Keymap &keymap);

/// The default bindings that do not parse, as "action: binding — reason".
///
/// Empty in a correct build. installDefaultKeymap() skips a default it cannot
/// parse with a warning nobody reads, which is how Settings shipped with no
/// working key: both of its bindings contain a comma, and commas were rejected
/// outright. A test asserts this list is empty.
QStringList unusableDefaultBindings();

} // namespace pf::input
