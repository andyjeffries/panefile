#include "input/ActionRegistry.h"

#include "core/Logging.h"

#include <QCoreApplication>

namespace pf::input {

void ActionRegistry::registerAction(Action action)
{
    if (action.id.isEmpty()) {
        qCWarning(pfKeys) << "refusing to register an action with no id";
        return;
    }
    // The id is copied out first. Writing insert(action.id, std::move(action))
    // leaves the order of evaluation unspecified, so the move may happen before
    // the key is read — and then the key is read from a moved-from string.
    const QString id = action.id;
    if (!m_actions.contains(id)) {
        m_order.append(id);
    }
    // operator[] then move-assign, rather than insert(): QHash::insert takes its
    // value by const reference, so a std::move into it does nothing at all.
    m_actions[id] = std::move(action);
}

void ActionRegistry::registerAction(const QString &id, const QString &description,
                                    ActionCategory category, std::function<void()> handler,
                                    std::function<bool()> isEnabled)
{
    registerAction(Action{.id = id,
                          .description = description,
                          .category = category,
                          .handler = std::move(handler),
                          .isEnabled = std::move(isEnabled)});
}

bool ActionRegistry::invoke(const QString &id)
{
    const auto found = m_actions.constFind(id);
    if (found == m_actions.constEnd()) {
        qCWarning(pfKeys) << "no such action:" << id;
        return false;
    }

    const Action &action = found.value();
    if (action.isEnabled && !action.isEnabled()) {
        qCDebug(pfKeys) << "action is currently disabled:" << id;
        return false;
    }
    if (!action.handler) {
        return false;
    }

    qCDebug(pfKeys) << "invoking" << id;
    action.handler();
    return true;
}

bool ActionRegistry::contains(const QString &id) const
{
    return m_actions.contains(id);
}

bool ActionRegistry::isEnabled(const QString &id) const
{
    const auto found = m_actions.constFind(id);
    if (found == m_actions.constEnd()) {
        return false;
    }
    return !found.value().isEnabled || found.value().isEnabled();
}

const Action *ActionRegistry::find(const QString &id) const
{
    const auto found = m_actions.constFind(id);
    return found == m_actions.constEnd() ? nullptr : &found.value();
}

QStringList ActionRegistry::ids() const
{
    return m_order;
}

QList<const Action *> ActionRegistry::byCategory(ActionCategory category) const
{
    QList<const Action *> result;
    for (const QString &id : m_order) {
        const auto found = m_actions.constFind(id);
        if (found != m_actions.constEnd() && found.value().category == category) {
            result.append(&found.value());
        }
    }
    return result;
}

QString ActionRegistry::categoryTitle(ActionCategory category)
{
    switch (category) {
    case ActionCategory::General:
        return QCoreApplication::translate("actions", "General");
    case ActionCategory::Panels:
        return QCoreApplication::translate("actions", "Panel management");
    case ActionCategory::Movement:
        return QCoreApplication::translate("actions", "Movement");
    case ActionCategory::Selection:
        return QCoreApplication::translate("actions", "Selection");
    case ActionCategory::FileOperations:
        return QCoreApplication::translate("actions", "File operations");
    case ActionCategory::View:
        return QCoreApplication::translate("actions", "View");
    }
    return {};
}

void ActionRegistry::clear()
{
    m_actions.clear();
    m_order.clear();
}

} // namespace pf::input
