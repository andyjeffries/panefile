#include "ui/modals/HelpModal.h"

#include "input/ActionRegistry.h"
#include "input/Keymap.h"
#include "ui/ThemePalette.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pf::ui {
namespace {

using input::ActionCategory;
using input::KeymapLayer;

/// §8.2: "rendered as `Ctrl+C  ·  Super+C  ·  y y`".
///
/// QStringLiteral rather than QLatin1String: the separator is U+00B7, and
/// QLatin1String over a UTF-8 source literal reinterprets its two bytes as two
/// Latin-1 characters — which renders as "Â·".
/// A function rather than a namespace-scope QString: §3.4 rules out non-trivial
/// static initialisers, and a QString at namespace scope is exactly one.
QString bindingSeparator()
{
    return QStringLiteral("  \u00B7  ");
}

/// Layers searched for an action's bindings, and the order they are shown in.
/// An action bound in several layers — `confirm` is on Return globally and `l`
/// in Normal mode — should list both, because both work.
constexpr KeymapLayer kSearchedLayers[] = {KeymapLayer::Global, KeymapLayer::Normal,
                                           KeymapLayer::Selection};

} // namespace

HelpModal::HelpModal(const input::ActionRegistry &registry, const input::Keymap &keymap,
                     QWidget *parent)
    : Modal(parent), m_registry(&registry), m_keymap(&keymap),
      m_filter(new QLineEdit(contentWidget())), m_tree(new QTreeWidget(contentWidget()))
{
    setSizePercent(70, 80);

    auto *layout = new QVBoxLayout(contentWidget());
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("Keyboard reference"), contentWidget());
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    m_filter->setPlaceholderText(tr("Filter actions…"));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter);

    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Action"), tr("Keys"), tr("Description")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    layout->addWidget(m_tree, 1);

    auto *hint = new QLabel(tr("Esc closes"), contentWidget());
    hint->setForegroundRole(QPalette::WindowText);
    QPalette hintPalette = hint->palette();
    hintPalette.setColor(QPalette::WindowText, currentPalette().overlay);
    hint->setPalette(hintPalette);
    layout->addWidget(hint);

    connect(m_filter, &QLineEdit::textChanged, this, &HelpModal::applyFilter);

    refresh();
}

void HelpModal::refresh()
{
    m_tree->clear();
    if (m_registry == nullptr || m_keymap == nullptr) {
        return;
    }

    for (const ActionCategory category :
         {ActionCategory::General, ActionCategory::Panels, ActionCategory::Movement,
          ActionCategory::Selection, ActionCategory::FileOperations, ActionCategory::View}) {

        const QList<const input::Action *> actions = m_registry->byCategory(category);
        if (actions.isEmpty()) {
            continue;
        }

        auto *group = new QTreeWidgetItem(m_tree);
        group->setFirstColumnSpanned(true);
        group->setText(0, input::ActionRegistry::categoryTitle(category));
        QFont groupFont = group->font(0);
        groupFont.setBold(true);
        group->setFont(0, groupFont);
        group->setExpanded(true);

        for (const input::Action *action : actions) {
            QStringList rendered;
            for (const KeymapLayer layer : kSearchedLayers) {
                const QList<input::Binding> bindings = m_keymap->bindingsFor(layer, action->id);
                for (const input::Binding &binding : bindings) {
                    const QString text = input::bindingToString(binding);
                    if (!rendered.contains(text)) {
                        rendered << text;
                    }
                }
            }

            auto *item = new QTreeWidgetItem(group);
            item->setText(0, action->id);
            // An action with no binding is still listed. That is deliberate:
            // it is reachable from the `>` prompt, and a user looking for it
            // needs to see that it exists and has no key rather than conclude
            // the feature is missing.
            item->setText(1,
                          rendered.isEmpty() ? tr("unbound") : rendered.join(bindingSeparator()));
            item->setText(2, action->description);

            if (rendered.isEmpty()) {
                item->setForeground(1, currentPalette().overlay);
            }
        }
    }

    // §6.2: conflicts are surfaced here so the user can see what their config
    // has done, rather than only in a log they will never read.
    const QList<input::KeymapConflict> &conflicts = m_keymap->conflicts();
    if (!conflicts.isEmpty()) {
        auto *group = new QTreeWidgetItem(m_tree);
        group->setFirstColumnSpanned(true);
        group->setText(0, tr("Binding conflicts"));
        QFont groupFont = group->font(0);
        groupFont.setBold(true);
        group->setFont(0, groupFont);
        group->setExpanded(true);

        for (const input::KeymapConflict &conflict : conflicts) {
            auto *item = new QTreeWidgetItem(group);
            item->setText(0, conflict.rejectedActionId);
            item->setText(1, input::bindingToString(conflict.binding));
            item->setText(2, tr("ignored — already bound to %1").arg(conflict.keptActionId));
            item->setForeground(2, currentPalette().warning);
        }
    }

    applyFilter(m_filter->text());
}

void HelpModal::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();

    for (int groupIndex = 0; groupIndex < m_tree->topLevelItemCount(); ++groupIndex) {
        QTreeWidgetItem *group = m_tree->topLevelItem(groupIndex);
        int visibleChildren = 0;

        for (int childIndex = 0; childIndex < group->childCount(); ++childIndex) {
            QTreeWidgetItem *child = group->child(childIndex);
            const bool matches = needle.isEmpty() ||
                                 child->text(0).contains(needle, Qt::CaseInsensitive) ||
                                 child->text(1).contains(needle, Qt::CaseInsensitive) ||
                                 child->text(2).contains(needle, Qt::CaseInsensitive);

            child->setHidden(!matches);
            visibleChildren += matches ? 1 : 0;
        }

        // A heading with nothing under it is noise.
        group->setHidden(visibleChildren == 0);
    }
}

} // namespace pf::ui
