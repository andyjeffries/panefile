#include "platform/Thumbnailers.h"

namespace pf::platform {

// TODO: QLThumbnailGenerator needs Objective-C++; until then macOS has no
// external thumbnailers and the registry stays empty.
Thumbnailers &Thumbnailers::instance()
{
    static Thumbnailers registry;
    return registry;
}

} // namespace pf::platform
