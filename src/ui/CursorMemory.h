#pragma once

#include <QHash>
#include <QString>

#include <list>

namespace pf::ui {

/// Remembers which entry the cursor was on, per directory (§5.2).
///
/// "When you navigate out of a directory and back in, the cursor must land on
/// the directory you came from." That single behaviour is most of what makes
/// keyboard navigation feel like moving around a filesystem rather than
/// operating a list widget — without it, every `h` to go up dumps the cursor at
/// the top and the user has to find their place again.
///
/// Bounded to 256 entries with least-recently-used eviction, per §5.2. The
/// bound matters: a long session that walks a large tree would otherwise
/// accumulate an entry per directory visited, forever.
class CursorMemory
{
public:
    static constexpr int kCapacity = 256;

    static CursorMemory &instance();

    void remember(const QString &directory, const QString &entryName);

    /// The remembered entry for a directory, or empty. Recalling counts as a
    /// use, so a directory you keep returning to is not evicted by directories
    /// you passed through once.
    QString recall(const QString &directory);

    void forget(const QString &directory);
    void clear();

    int size() const;

private:
    CursorMemory() = default;

    /// Iteration order is most-recently-used first, so eviction is a pop_back.
    std::list<std::pair<QString, QString>> m_order;
    QHash<QString, std::list<std::pair<QString, QString>>::iterator> m_index;
};

} // namespace pf::ui
