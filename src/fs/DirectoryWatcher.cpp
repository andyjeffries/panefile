#include "fs/DirectoryWatcher.h"

#include "core/Logging.h"
#include "platform/WatchBackend.h"

#include <QDir>

#include <map>

namespace pf::fs {
namespace {

/// The shared watchers, keyed on cleaned path.
///
/// weak_ptr rather than shared_ptr: the registry must not be what keeps a
/// watcher alive, or every directory ever visited would hold an inotify watch
/// for the lifetime of the process.
std::map<QString, std::weak_ptr<DirectoryWatcher>> &registry()
{
    // Function-local static: nothing is constructed until the first directory
    // is watched, which is after the first paint (§3.4).
    static std::map<QString, std::weak_ptr<DirectoryWatcher>> instance;
    return instance;
}

} // namespace

DirectoryWatcher::DirectoryWatcher(const QString &path)
    : m_path(QDir::cleanPath(path)), m_backend(platform::WatchBackend::create(nullptr))
{
    m_backend->setParent(this);

    connect(m_backend.get(), &platform::WatchBackend::rawEvent, &m_coalescer,
            [this](const WatchEvent &event) { m_coalescer.add(event); });

    connect(&m_coalescer, &WatchCoalescer::delta, this, &DirectoryWatcher::changed);

    if (!m_backend->watch(m_path)) {
        // Not an error worth showing the user: a panel that is not watched
        // still lists correctly, it simply will not notice changes made from
        // elsewhere. The most common cause is the directory disappearing
        // between the scan and the watch.
        qCDebug(pfFs) << "not watching" << m_path;
        return;
    }
    m_active = true;
}

DirectoryWatcher::~DirectoryWatcher()
{
    if (m_backend) {
        m_backend->stop();
    }
    registry().erase(m_path);
}

std::shared_ptr<DirectoryWatcher> DirectoryWatcher::acquire(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);

    if (const auto found = registry().find(cleaned); found != registry().end()) {
        if (auto existing = found->second.lock()) {
            return existing;
        }
    }

    // Not make_shared: the constructor is private, and exposing it publicly to
    // save one allocation would let a caller create an unshared watcher and
    // quietly defeat the refcounting.
    std::shared_ptr<DirectoryWatcher> watcher(new DirectoryWatcher(cleaned));
    registry()[cleaned] = watcher;
    return watcher;
}

int DirectoryWatcher::watchedPathCount()
{
    int alive = 0;
    for (const auto &[path, weak] : registry()) {
        if (!weak.expired()) {
            ++alive;
        }
    }
    return alive;
}

QString DirectoryWatcher::path() const
{
    return m_path;
}

bool DirectoryWatcher::isActive() const
{
    return m_active;
}

void DirectoryWatcher::setDebounceInterval(int milliseconds)
{
    m_coalescer.setDebounceInterval(milliseconds);
}

} // namespace pf::fs
