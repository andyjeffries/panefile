#pragma once

#include "config/Config.h"
#include "fs/RenamePlan.h"

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
class CompressModal;
class ConflictModal;
class InputModal;
class RenameModal;
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

public Q_SLOTS:
    /// §7.12: files dropped on a panel.
    void onFilesDropped(const QStringList &paths, const QString &destination,
                        Qt::DropAction action);

private:
    void copySelection(bool cut);
    void pasteIntoFocusedPanel();

    /// The one path every copy and move goes through: paste, and §7.12's drop.
    /// Shared so a dropped file gets the same conflict handling and the same
    /// undo entry a pasted one does.
    void runTransfer(const QStringList &paths, const QString &destination, bool move);

    void deleteSelection(bool permanent);
    void undoLast();

    /// §6.3's `file_panel_item_create` (`Ctrl+N`): "Trailing `/` creates a
    /// directory".
    void createItem();

    /// §6.3's `file_panel_item_rename` (`Ctrl+R`).
    void renameCursorItem();

    /// §7.9's bulk rename, through the sheet rather than $EDITOR.
    void bulkRenameSelection();

    /// Runs a planned rename as one undoable job (§7.9 step 6).
    void runRenamePlan(const QString &directory, const fs::RenamePlan &plan);

    /// §7.10's `Ctrl+A` and `Ctrl+E`.
    void compressSelection();
    void extractCursorItem();

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

    /// §3.4: modals are built on first invocation, then cached.
    ui::CompressModal *compressModal();
    ui::InputModal *inputModal();
    ui::RenameModal *renameModal();

    ui::MainWindow *m_window = nullptr;
    ui::PanelStrip *m_strip = nullptr;
    input::ActionRegistry *m_registry = nullptr;
    fs::JobEngine *m_engine = nullptr;
    fs::UndoStack *m_undoStack = nullptr;

    ui::ConflictModal *m_conflictModal = nullptr;
    ui::CompressModal *m_compressModal = nullptr;
    ui::InputModal *m_inputModal = nullptr;
    ui::RenameModal *m_renameModal = nullptr;
    config::Settings m_settings;

    /// Whether the last copy_items/cut_items was a cut. The clipboard carries
    /// the paths; this carries what to do with them.
    bool m_clipboardIsCut = false;
};

} // namespace pf
