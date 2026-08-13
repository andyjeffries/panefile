#include "platform/WatchBackend.h"

namespace pf::platform {

// The base class holds nothing of its own: create() and every override live in
// the per-platform files. This translation unit exists so the moc output for
// the Q_OBJECT in the header has somewhere to be compiled, and so that a build
// which somehow reaches neither platform file still fails at link rather than
// at configure with a confusing message.

WatchBackend::WatchBackend(QObject *parent) : QObject(parent) {}

WatchBackend::~WatchBackend() = default;

} // namespace pf::platform
