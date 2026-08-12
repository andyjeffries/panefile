// DefaultConfig is header-only, but the config layer needs at least one
// translation unit to exist as a library target. This also gives the default
// config a compile-time guarantee that it is valid TOML: a typo in the template
// would otherwise only surface when a user ran --print-default-config and fed
// the result back in.

#include "config/DefaultConfig.h"

#include <toml++/toml.hpp>

namespace pf::config {

bool defaultConfigParses(std::string *errorOut)
{
    const toml::parse_result result = toml::parse(kDefaultConfigToml);
    if (!result) {
        if (errorOut != nullptr) {
            *errorOut = std::string(result.error().description());
        }
        return false;
    }
    return true;
}

} // namespace pf::config
