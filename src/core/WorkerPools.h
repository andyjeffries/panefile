#pragma once

class QThreadPool;

namespace pf {

/// The worker pools the application owns, and the one place that drains them.
///
/// Every pool here is a function-local static, so that a `--version` invocation
/// creates none of them (§3.4). That much was right. What was missing is the
/// other end: a `QThreadPool` held in a function-local static is destroyed
/// during static destruction, in an order the standard leaves unspecified
/// relative to Qt's own globals — and its destructor is the *only* thing that
/// waits for its tasks.
///
/// So a task still running when `main()` returned could reach a Qt global that
/// had already been torn down. It did:
///
///     Thread 0  exit → __cxa_finalize_ranges → ~QMimeDatabasePrivate
///     Thread 3  pf-scanner → QMimeDatabase::mimeTypeForFile → SIGSEGV
///
/// Draining every pool while the event loop is still alive fixes it for all of
/// them at once, and keeps the fix in one place rather than in four.
class WorkerPools
{
public:
    /// A pool registered for draining. `name` is the thread name, which is what
    /// a profiler and a crash report show.
    ///
    /// Returns a pool that lives for the rest of the process. Registration is
    /// the whole point: a pool created any other way is one this cannot wait
    /// for.
    static QThreadPool *acquire(const char *name, int maxThreadCount);

    /// Waits for every registered pool to finish, then refuses further work.
    ///
    /// Called from the composition root on `aboutToQuit`, which is early enough
    /// that the event loop is still running and late enough that nothing new is
    /// being started.
    ///
    /// Safe to call more than once, and safe to call when no pool was ever
    /// created — which is the `--version` case.
    static void drainAll();

    /// How many pools have been created. For the tests.
    static int registeredCount();
};

} // namespace pf
