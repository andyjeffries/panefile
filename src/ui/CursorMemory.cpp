#include "ui/CursorMemory.h"

namespace pf::ui {

CursorMemory &CursorMemory::instance()
{
    // Function-local static: nothing is constructed until the first navigation,
    // never at load time (§3.4).
    static CursorMemory memory;
    return memory;
}

void CursorMemory::remember(const QString &directory, const QString &entryName)
{
    if (directory.isEmpty() || entryName.isEmpty()) {
        return;
    }

    if (const auto existing = m_index.constFind(directory); existing != m_index.constEnd()) {
        // Splice rather than erase-and-insert: the iterator stays valid, so the
        // index does not need updating.
        m_order.splice(m_order.begin(), m_order, existing.value());
        existing.value()->second = entryName;
        return;
    }

    m_order.emplace_front(directory, entryName);
    m_index.insert(directory, m_order.begin());

    if (m_order.size() > kCapacity) {
        m_index.remove(m_order.back().first);
        m_order.pop_back();
    }
}

QString CursorMemory::recall(const QString &directory)
{
    const auto found = m_index.constFind(directory);
    if (found == m_index.constEnd()) {
        return {};
    }

    // A recall is a use. Without this, a directory the user keeps returning to
    // ages out just as fast as one they passed through once.
    m_order.splice(m_order.begin(), m_order, found.value());
    return found.value()->second;
}

void CursorMemory::forget(const QString &directory)
{
    const auto found = m_index.constFind(directory);
    if (found == m_index.constEnd()) {
        return;
    }
    m_order.erase(found.value());
    m_index.erase(found);
}

void CursorMemory::clear()
{
    m_order.clear();
    m_index.clear();
}

int CursorMemory::size() const
{
    return static_cast<int>(m_order.size());
}

} // namespace pf::ui
