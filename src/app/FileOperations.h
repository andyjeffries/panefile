#pragma once

#include "config/Config.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace pf::fs {
class JobEngine;
class UndoStack;
} // namespace pf::fs

namespace pf::input {
class ActionRegistry;
}

namespace pf::ui {
class ConflictModal;
class MainWindow;
class PanelStrip;
class ProcessBar;
} // namespace pf::ui

namespace pf {

/// Registers and runs §6.3's file operations.
///
/// Kept apart from PanelController because the two have different jobs: that
/// one moves a cursor around, this one changes the filesystem. The distinction
/// matters most in what each has to worry about — nothing here happens without
/// either a confirmation or an undo entry.
class FileOperations : public QObject
{
    Q_OBJECT

public:
    FileOperations(ui::MainWindow *window, ui::PanelStrip *strip, input::ActionRegistry *registry,
                   fs::JobEngine *engine, fs::UndoStack *undoStack, QObject *parent = nullptr);
    ~FileOperations() override;

    void registerActions();

    /// The settings that govern confirmations and conflict defaults (§8.1).
    void setSettings(const config::Settings &settings);

Q_SIGNALS:
    void statusMessage(const QString &message);

private:
    void copySelection(bool cut);
    void pasteIntoFocusedPanel();
    void deleteSelection(bool permanent);
    void undoLast();

    /// The clipboard's paths, whether they were put there by Panefile or by
    /// another application.
    ///
    /// §7.12 has Panefile put `text/uri-list` on the clipboard, and every other
    /// file manager does the same, so a paste works between applications
    /// without either knowing about the other.
    static QStringList clipboardPaths();
    void setClipboardPaths(const QStringList &paths, bool cut);
    bool clipboardIsCut() const;

    ui::ConflictModal *conflictModal();

    ui::MainWindow *m_window = nullptr;
    ui::PanelStrip *m_strip = nullptr;
    input::ActionRegistry *m_registry = nullptr;
    fs::JobEngine *m_engine = nullptr;
    fs::UndoStack *m_undoStack = nullptr;

    ui::ConflictModal *m_conflictModal = nullptr;
    config::Settings m_settings;

    /// Whether the last copy_items/cut_items was a cut. The clipboard carries
    /// the paths; this carries what to do with them.
    bool m_clipboardIsCut = false;
};

} // namespace pf
