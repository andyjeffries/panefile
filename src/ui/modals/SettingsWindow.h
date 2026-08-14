#pragma once

#include "config/Config.h"
#include "ui/modals/Modal.h"

#include <QStringList>

#include <memory>

class QButtonGroup;
class QStackedWidget;
class QWidget;

namespace pf::input {
class ActionRegistry;
class Keymap;
} // namespace pf::input

namespace pf::ui {

/// Settings, in the shape macOS puts them in.
///
/// A row of icon-and-label tabs across the top and the chosen tab's content
/// beneath it — Terminal.app's preferences, and every other first-party
/// application's. Not a sidebar list, which is a web idiom, and not a property
/// sheet.
///
/// It exists because configuring Panefile previously required knowing three
/// things nobody is told: where the config lives, what the files are called,
/// and that none of them exist until you create one. There was no way to
/// discover that `nord` was a valid theme, or that themes were a thing at all.
///
/// The files stay authoritative. This writes to them through TomlWriter, which
/// edits the assignment and leaves every comment in place, and then lets
/// ConfigWatcher notice and apply the change — so hand-editing and this window
/// take exactly the same path, and there is only one code path that applies
/// configuration rather than two that can disagree.
class SettingsWindow : public Modal
{
    Q_OBJECT

public:
    SettingsWindow(input::ActionRegistry *registry, input::Keymap *keymap, QWidget *parent);

    /// Out of line because Controls is only defined in the .cpp: a unique_ptr's
    /// deleter has to see the complete type, and an implicit destructor here
    /// would be instantiated where it cannot.
    ~SettingsWindow() override;

    /// Loads the current values and shows the window.
    void present();

Q_SIGNALS:
    /// A theme was picked. The composition root applies it immediately —
    /// appearance is judged by looking at it, so it has to change under the
    /// pointer rather than when the window closes.
    void themePreviewRequested(const QString &themeName);

    /// Something was written. Not strictly needed, since ConfigWatcher will
    /// notice, but it lets the status bar say so at the moment it happened.
    void settingsChanged(const QString &description);

private:
    static QWidget *buildToolbar();
    QWidget *buildAppearanceTab();
    QWidget *buildGeneralTab();
    QWidget *buildQuickLookTab();
    QWidget *buildKeysTab();

    void addTab(const QString &title, const QString &glyph, QWidget *page);

    /// Reads the config and theme files into the controls, without any of the
    /// controls' signals writing them straight back out again.
    void loadValues();

    /// Writes one key, and reports the failure rather than swallowing it: a
    /// setting that silently did not save is worse than one that refused.
    void writeConfig(const QString &table, const QString &key, const QString &value,
                     const QString &description);
    void writeTheme(const QString &table, const QString &key, const QString &value,
                    const QString &description);

    static QString configPath();
    static QString themePath();

    input::ActionRegistry *m_registry = nullptr;
    input::Keymap *m_keymap = nullptr;

    QWidget *m_toolbar = nullptr;
    QButtonGroup *m_tabs = nullptr;
    QStackedWidget *m_pages = nullptr;

    /// True while loadValues() is populating the controls. Every control writes
    /// on change, and without this the act of showing the window would rewrite
    /// every key in the file with the value it already had.
    bool m_loading = false;

    struct Controls;
    std::unique_ptr<Controls> m_controls;
};

} // namespace pf::ui
