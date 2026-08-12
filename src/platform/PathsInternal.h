#pragma once

#include <QString>

// Helpers shared between Paths.cpp and the per-platform implementations.
// Not part of the public interface of the platform layer.

namespace pf::platform {

/// Value of `name` from the environment, cleaned, or empty if unset.
QString envOverride(const char *name);

/// As envOverride(), but returns empty for a relative path. The XDG basedir
/// spec requires a relative value to be treated as unset rather than resolved.
QString envDir(const char *name);

} // namespace pf::platform
