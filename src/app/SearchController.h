#pragma once

#include "config/Config.h"

#include <QObject>

namespace pf::input {
class ActionRegistry;
}

namespace pf::ui {
class FilePanel;
class FindModal;
class MainWindow;
class PanelStrip;
} // namespace pf::ui

namespace pf {

/// §7.8's two searches.
///
/// "Two distinct things, don't conflate them": the in-panel filter (`/`) narrows
/// what the focused panel is already showing, and the recursive finder
/// (`Ctrl+F`) walks its subtree. They share a matcher and nothing else — the
/// filter never leaves the directory, and the finder never changes what the
/// panel is displaying until a result is chosen.
class SearchController : public QObject
{
    Q_OBJECT

public:
    SearchController(ui::MainWindow *window, ui::PanelStrip *strip, input::ActionRegistry *registry,
                     QObject *parent = nullptr);

    void registerActions();

    /// Applies `[search]`. Also applied to each panel as it is created, so a
    /// panel opened later matches the same way.
    void setSettings(const config::Settings &settings);

    /// Applies the search settings to one panel. Called from the composition
    /// root's panelCreated handler.
    void configurePanel(ui::FilePanel *panel) const;

Q_SIGNALS:
    void statusMessage(const QString &message);

private:
    void openFilter();
    void openFinder();

    /// §3.4: built on first `Ctrl+F`, then cached.
    ui::FindModal *findModal();

    ui::MainWindow *m_window = nullptr;
    ui::PanelStrip *m_strip = nullptr;
    input::ActionRegistry *m_registry = nullptr;
    ui::FindModal *m_findModal = nullptr;

    config::Settings m_settings;
};

} // namespace pf
