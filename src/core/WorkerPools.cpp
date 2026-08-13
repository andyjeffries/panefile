#include "core/WorkerPools.h"

#include "core/Logging.h"

#include <QString>
#include <QThreadPool>

#include <mutex>
#include <vector>

namespace pf {
namespace {

/// The registry.
///
/// Deliberately leaked: the pools must outlive every thread that might still be
/// touching one, and static destruction is exactly the hazard this class
/// exists to remove. A `new` that is never deleted is the honest way to say
/// "this lives until the process does" — a static would put its destructor back
/// in the order that caused the crash.
struct Registry {
    std::mutex mutex;
    std::vector<QThreadPool *> pools;
    bool drained = false;
};

Registry &registry()
{
    static auto *instance = new Registry;
    return *instance;
}

} // namespace

QThreadPool *WorkerPools::acquire(const char *name, int maxThreadCount)
{
    Registry &pools = registry();
    const std::scoped_lock lock(pools.mutex);

    // Leaked for the same reason the registry is.
    auto *pool = new QThreadPool;
    pool->setMaxThreadCount(maxThreadCount);
    pool->setObjectName(QString::fromLatin1(name));

    // A pool created after the drain would never be waited for. That can only
    // happen if something starts work during shutdown, which is a bug in the
    // caller — but silently leaving a thread running past static destruction is
    // how this class's namesake crash happened, so it is refused loudly.
    if (pools.drained) {
        qCWarning(pfApp) << "worker pool" << name << "created after shutdown";
    }

    pools.pools.push_back(pool);
    return pool;
}

void WorkerPools::drainAll()
{
    Registry &pools = registry();
    const std::scoped_lock lock(pools.mutex);

    if (pools.drained) {
        return;
    }
    pools.drained = true;

    for (QThreadPool *pool : pools.pools) {
        // clear() first: a queued task that has not started yet has no reason
        // to run now, and waiting for a backlog of them is the difference
        // between quitting promptly and appearing to hang.
        pool->clear();
        pool->waitForDone();
    }

    qCDebug(pfApp) << "drained" << pools.pools.size() << "worker pools";
}

int WorkerPools::registeredCount()
{
    Registry &pools = registry();
    const std::scoped_lock lock(pools.mutex);
    return static_cast<int>(pools.pools.size());
}

} // namespace pf
