#pragma once

#include <QObject>
#include <QString>

namespace pf::input {
class ActionRegistry;
}

namespace pf::ui {
class FilePanel;
class MainWindow;
class PanelStrip;
class Sidebar;
} // namespace pf::ui

namespace pf {

/// The composition root for panel behaviour (§3.1's app layer).
///
/// Every action in §6.3 that touches a panel is registered here, as a closure
/// over the panel strip. That is the whole reason this class exists: §6.2
/// forbids the UI from calling behaviour functions directly, so something has
/// to bind action ids to what they actually do, and the binding needs access to
/// widgets that the input layer must not know about.
///
/// Registering handlers rather than exposing methods also keeps the "current
/// panel" question in one place. Each handler asks the strip which panel has
/// focus at the moment it runs, so an action fired from a keystroke, from the
/// `>` prompt or from a menu all act on the same panel.
class PanelController : public QObject
{
    Q_OBJECT

public:
    PanelController(ui::MainWindow *window, ui::PanelStrip *strip, ui::Sidebar *sidebar,
                    input::ActionRegistry *registry, QObject *parent = nullptr);

    /// Registers every panel, movement and view action with the registry.
    void registerActions();

    /// Opens a path according to the placement rules the caller has already
    /// resolved: in the focused panel, or in a new one (§10.2).
    void openPath(const QString &path, bool inNewPanel);

Q_SIGNALS:
    void statusMessage(const QString &message);

    /// The focused panel changed mode, so the dispatcher can update which
    /// keymap layers are active (§6.1: mode is per panel).
    void panelModeChanged();

private:
    /// §6.3's `o` menu: the sort key, plus whether it runs backwards.
    void showSortMenu(ui::FilePanel *panel);

    ui::FilePanel *focused() const;

    ui::MainWindow *m_window = nullptr;
    ui::PanelStrip *m_strip = nullptr;
    ui::Sidebar *m_sidebar = nullptr;
    input::ActionRegistry *m_registry = nullptr;
};

} // namespace pf
