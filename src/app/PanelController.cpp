#include "app/PanelController.h"

#include "input/ActionRegistry.h"
#include "core/Logging.h"
#include "fs/Trash.h"
#include "model/FilterSortProxy.h"
#include "platform/Launcher.h"
#include "platform/Paths.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/Sidebar.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMenu>

#include <array>

namespace pf {

using input::ActionCategory;

PanelController::PanelController(ui::MainWindow *window, ui::PanelStrip *strip,
                                 ui::Sidebar *sidebar, input::ActionRegistry *registry,
                                 QObject *parent)
    : QObject(parent), m_window(window), m_strip(strip), m_sidebar(sidebar), m_registry(registry)
{}

ui::FilePanel *PanelController::focused() const
{
    return m_strip == nullptr ? nullptr : m_strip->focusedPanel();
}

void PanelController::openPath(const QString &path, bool inNewPanel)
{
    if (m_strip == nullptr) {
        return;
    }

    if (inNewPanel || m_strip->focusedPanel() == nullptr) {
        m_strip->addPanel(path);
        return;
    }

    // §10.2: navigating the focused panel pushes history, so go_back returns
    // the user to where they were.
    m_strip->focusedPanel()->navigateTo(path);
}

void PanelController::showSortMenu(ui::FilePanel *panel)
{
    // A menu rather than a modal: there are five choices and a checkmark says
    // which one is current, which is the entire content of the decision.
    QMenu menu(m_window);

    struct Option {
        SortKey key;
        QString label;
    };
    const std::array<Option, 5> options{{
        {.key = SortKey::Name, .label = tr("Name")},
        {.key = SortKey::Size, .label = tr("Size")},
        {.key = SortKey::Modified, .label = tr("Date Modified")},
        {.key = SortKey::Type, .label = tr("Kind")},
        {.key = SortKey::Random, .label = tr("Random")},
    }};

    for (const Option &option : options) {
        QAction *action = menu.addAction(option.label);
        action->setCheckable(true);
        action->setChecked(panel->sortKey() == option.key);
        connect(action, &QAction::triggered, panel,
                [panel, key = option.key] { panel->setSortKey(key); });
    }

    menu.addSeparator();
    QAction *reverse = menu.addAction(tr("Reversed"));
    reverse->setCheckable(true);
    reverse->setChecked(panel->reverseSort());
    connect(reverse, &QAction::triggered, panel,
            [panel] { panel->setReverseSort(!panel->reverseSort()); });

    // Under the panel's own header, so it is obvious which panel is being
    // sorted when several are open.
    menu.exec(panel->mapToGlobal(QPoint(panel->width() / 4, 28)));
}

void PanelController::registerActions()
{
    if (m_registry == nullptr || m_strip == nullptr) {
        return;
    }

    // Each handler resolves the focused panel when it runs rather than
    // capturing one, so a binding fired after the user switches panels acts on
    // the panel they are actually looking at.
    const auto onPanel = [this](auto &&action) {
        return [this, action]() {
            if (ui::FilePanel *panel = focused(); panel != nullptr) {
                action(panel);
            }
        };
    };

    const auto reg = [this](const char *id, const QString &description, ActionCategory category,
                            std::function<void()> handler, std::function<bool()> enabled = {}) {
        m_registry->registerAction(QString::fromLatin1(id), description, category,
                                   std::move(handler), std::move(enabled));
    };

    // ---------------------------------------------------------------- movement
    reg("list_down", tr("Move the cursor down"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->moveCursor(1); }));
    reg("list_up", tr("Move the cursor up"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->moveCursor(-1); }));
    reg("page_down", tr("Move down one screen"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->movePage(1); }));
    reg("page_up", tr("Move up one screen"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->movePage(-1); }));
    reg("list_top", tr("Move to the first entry"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->moveCursorToStart(); }));
    reg("list_bottom", tr("Move to the last entry"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->moveCursorToEnd(); }));

    reg("confirm", tr("Enter the directory, or open the file"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->activateCursorItem(); }));
    reg("parent_directory", tr("Go to the parent directory"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->goToParent(); }));
    reg("go_back", tr("Go back in this panel's history"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->goBack(); }));
    reg("go_forward", tr("Go forward in this panel's history"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->goForward(); }));

    reg("go_home", tr("Go to the home directory"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->navigateTo(QDir::homePath()); }));
    reg("go_root", tr("Go to the filesystem root"), ActionCategory::Movement,
        onPanel([](ui::FilePanel *panel) { panel->navigateTo(QStringLiteral("/")); }));
    reg("go_config", tr("Go to the configuration directory"), ActionCategory::Movement,
        onPanel([this](ui::FilePanel *panel) {
            const QString directory = platform::configDir();
            if (!QDir(directory).exists()) {
                // Navigating to a directory that has never been created would
                // show an error rather than an empty listing, which reads as a
                // fault rather than as "you have no config yet".
                Q_EMIT statusMessage(
                    tr("No configuration directory yet — run 'pf --print-default-config'"));
                return;
            }
            panel->navigateTo(directory);
        }));

    reg("go_previous", tr("Go to the last directory this panel visited"), ActionCategory::Movement,
        onPanel([this](ui::FilePanel *panel) {
            // §6.3 lists go_previous and go_back separately, and they are the
            // same journey: the previous directory *is* the last entry in this
            // panel's history. Sharing the implementation means they can never
            // disagree about what "previous" means.
            if (!panel->goBack()) {
                Q_EMIT statusMessage(tr("Nowhere to go back to in this panel"));
            }
        }));

    reg("go_trash", tr("Open the trash"), ActionCategory::Movement,
        onPanel([this](ui::FilePanel *panel) {
            const QString directory = fs::Trash().filesDirectory();
            if (directory.isEmpty() || !QDir(directory).exists()) {
                // Nothing has ever been trashed, so the directory does not
                // exist yet. An empty trash is not an error.
                Q_EMIT statusMessage(tr("The trash is empty"));
                return;
            }
            panel->navigateTo(directory);
        }));

    // -------------------------------------------------------- handing off
    reg("open_with_default_app", tr("Open with the default application"), ActionCategory::General,
        onPanel([this](ui::FilePanel *panel) {
            const QString path = panel->cursorPath();
            if (path.isEmpty()) {
                return;
            }
            if (!platform::Launcher::openWithDefaultApplication(path)) {
                Q_EMIT statusMessage(tr("Could not open %1").arg(QFileInfo(path).fileName()));
            }
        }));

    reg("open_terminal_here", tr("Open a terminal in this directory"), ActionCategory::General,
        onPanel([this](ui::FilePanel *panel) {
            if (!platform::Launcher::openTerminal(panel->path())) {
                Q_EMIT statusMessage(tr("No terminal found — set $TERMINAL"));
            }
        }));

    reg("open_file_with_editor", tr("Open the cursor item in $EDITOR"), ActionCategory::General,
        onPanel([this](ui::FilePanel *panel) {
            const QString path = panel->cursorPath();
            if (path.isEmpty()) {
                return;
            }
            if (!platform::Launcher::openInEditor(path)) {
                Q_EMIT statusMessage(tr("Could not open an editor — set $EDITOR"));
            }
        }));

    reg("open_current_directory_with_editor", tr("Open this directory in $EDITOR"),
        ActionCategory::General, onPanel([this](ui::FilePanel *panel) {
            if (!platform::Launcher::openInEditor(panel->path())) {
                Q_EMIT statusMessage(tr("Could not open an editor — set $EDITOR"));
            }
        }));

    reg("toggle_dot_file", tr("Show or hide dotfiles"), ActionCategory::View,
        onPanel([](ui::FilePanel *panel) { panel->toggleShowHidden(); }));
    reg("toggle_reverse_sort", tr("Reverse the sort order"), ActionCategory::View,
        onPanel([](ui::FilePanel *panel) { panel->setReverseSort(!panel->reverseSort()); }));

    reg("open_sort_options_menu", tr("Choose how this panel is sorted"), ActionCategory::View,
        onPanel([this](ui::FilePanel *panel) { showSortMenu(panel); }));

    // ------------------------------------------------------------------ panels
    reg(
        "create_new_file_panel", tr("Open a new panel at home"), ActionCategory::Panels,
        [this] { m_strip->addPanel(QDir::homePath()); },
        [this] { return m_strip->count() < ui::PanelStrip::kMaxPanels; });

    reg(
        "split_file_panel", tr("Duplicate the focused panel"), ActionCategory::Panels,
        [this] { m_strip->splitFocusedPanel(); },
        [this] { return m_strip->count() < ui::PanelStrip::kMaxPanels; });

    reg(
        "close_file_panel", tr("Close the focused panel"), ActionCategory::Panels,
        [this] { m_strip->closeFocusedPanel(); },
        [this] { return m_strip->count() > ui::PanelStrip::kMinPanels; });

    reg(
        "next_file_panel", tr("Focus the next panel"), ActionCategory::Panels,
        [this] { m_strip->focusNext(); }, [this] { return m_strip->count() > 1; });
    reg(
        "previous_file_panel", tr("Focus the previous panel"), ActionCategory::Panels,
        [this] { m_strip->focusPrevious(); }, [this] { return m_strip->count() > 1; });
    reg("equalise_panels", tr("Reset the panels to equal widths"), ActionCategory::Panels,
        [this] { m_strip->equalise(); });

    reg("focus_on_sidebar", tr("Focus the sidebar"), ActionCategory::Panels, [this] {
        if (m_sidebar != nullptr && m_sidebar->isVisible()) {
            // §3.4 and §7.11: "Connect to udisks2 the first time the sidebar's
            // Devices section becomes visible." Focusing it is that moment —
            // the section is drawn, so the bus connection is finally earned.
            m_sidebar->startWatchingDevices();
            m_sidebar->setFocus(Qt::OtherFocusReason);
        }
    });

    reg("unmount_device", tr("Unmount the device under the sidebar cursor"), ActionCategory::Panels,
        [this] {
            if (m_sidebar != nullptr) {
                m_sidebar->unmountCurrentVolume();
            }
        });

    reg("pinned_directory", tr("Pin or unpin this directory in the sidebar"),
        ActionCategory::Panels, onPanel([this](ui::FilePanel *panel) {
            if (m_sidebar == nullptr) {
                return;
            }
            const bool pinned = m_sidebar->togglePin(panel->path());
            Q_EMIT statusMessage(pinned ? tr("Pinned %1").arg(panel->path())
                                        : tr("Unpinned %1").arg(panel->path()));
        }));

    // -------------------------------------------------------------------- misc
    reg("copy_present_working_directory", tr("Copy this panel's path to the clipboard"),
        ActionCategory::FileOperations, onPanel([this](ui::FilePanel *panel) {
            QApplication::clipboard()->setText(panel->path());
            Q_EMIT statusMessage(tr("Copied %1").arg(panel->path()));
        }));

    reg("copy_path", tr("Copy the cursor item's absolute path"), ActionCategory::FileOperations,
        onPanel([this](ui::FilePanel *panel) {
            const QString name = panel->cursorName();
            if (name.isEmpty()) {
                return;
            }
            const QString absolute = panel->path() + QLatin1Char('/') + name;
            QApplication::clipboard()->setText(absolute);
            Q_EMIT statusMessage(tr("Copied %1").arg(absolute));
        }));

    reg("toggle_footer", tr("Show or hide the footer"), ActionCategory::View,
        [this] { m_window->toggleFooter(); });

    qCDebug(pfKeys) << "registered" << m_registry->ids().size() << "actions";
}

} // namespace pf
