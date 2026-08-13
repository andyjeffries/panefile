#include "app/SearchController.h"

#include "input/ActionRegistry.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/modals/FindModal.h"

#include <QFileInfo>

using pf::input::ActionCategory;

namespace pf {

SearchController::SearchController(ui::MainWindow *window, ui::PanelStrip *strip,
                                   input::ActionRegistry *registry, QObject *parent)
    : QObject(parent), m_window(window), m_strip(strip), m_registry(registry)
{}

void SearchController::setSettings(const config::Settings &settings)
{
    m_settings = settings;

    for (ui::FilePanel *panel : m_strip->panels()) {
        configurePanel(panel);
    }

    if (m_findModal != nullptr) {
        m_findModal->setMaxResults(settings.search.maxResults);
        m_findModal->setRespectGitignore(settings.search.respectGitignore);
        m_findModal->setFuzzyMatching(settings.search.fuzzy);
    }
}

void SearchController::configurePanel(ui::FilePanel *panel) const
{
    if (panel != nullptr) {
        panel->setFuzzyMatching(m_settings.search.fuzzy);
    }
}

ui::FindModal *SearchController::findModal()
{
    if (m_findModal == nullptr) {
        // §3.4: on first invocation, not at startup. The finder owns a worker
        // and a matcher, neither of which a user who never presses Ctrl+F
        // should pay for.
        m_findModal = new ui::FindModal(m_window);
        m_findModal->setMaxResults(m_settings.search.maxResults);
        m_findModal->setRespectGitignore(m_settings.search.respectGitignore);
        m_findModal->setFuzzyMatching(m_settings.search.fuzzy);

        connect(m_findModal, &ui::FindModal::resultChosen, this, [this](const QString &path) {
            ui::FilePanel *panel = m_strip->focusedPanel();
            if (panel == nullptr) {
                return;
            }

            // §7.8: "Enter navigates the panel to the result's directory and
            // puts the cursor on it." The directory, not the result — opening a
            // file's *parent* with the cursor on the file is what lets the next
            // keystroke act on it.
            const QFileInfo info(path);
            panel->navigateTo(info.absolutePath());
            panel->setCursorName(info.fileName());
        });
    }
    return m_findModal;
}

void SearchController::openFilter()
{
    if (ui::FilePanel *panel = m_strip->focusedPanel(); panel != nullptr) {
        configurePanel(panel);
        panel->openFilterBar();
    }
}

void SearchController::openFinder()
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    ui::FindModal *modal = findModal();
    modal->setIncludeHidden(panel->showHidden());
    modal->setSearchRoot(panel->path());
    modal->start();
}

void SearchController::registerActions()
{
    m_registry->registerAction(QStringLiteral("search_bar"),
                               tr("Filter this directory as you type"), ActionCategory::View,
                               [this] { openFilter(); });

    m_registry->registerAction(QStringLiteral("open_fuzzy_find"),
                               tr("Find a file anywhere below this directory"),
                               ActionCategory::General, [this] { openFinder(); });
}

} // namespace pf
