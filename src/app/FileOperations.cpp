#include "app/FileOperations.h"

#include "input/ActionRegistry.h"
#include "core/Logging.h"
#include "fs/JobEngine.h"
#include "fs/UndoStack.h"
#include "fs/jobs/ArchiveJob.h"
#include "fs/jobs/DeleteJob.h"
#include "fs/jobs/ExtractJob.h"
#include "fs/jobs/RenameJob.h"
#include "fs/jobs/TransferJob.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"

#include "ui/modals/CompressModal.h"
#include "ui/modals/ConflictModal.h"
#include "ui/modals/InputModal.h"
#include "ui/modals/RenameModal.h"
#include <QDir>
#include <QFile>
#include <QKeySequence>

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>

namespace pf {

using input::ActionCategory;

namespace {

/// Marks a clipboard payload as a cut rather than a copy.
///
/// There is no cross-desktop standard for this. GNOME and KDE both use a
/// `x-special/gnome-copied-files` payload whose first line is "cut" or "copy",
/// so writing it as well as `text/uri-list` means a cut in Panefile is
/// understood as a cut by Nautilus and Dolphin, and vice versa. Anything that
/// does not recognise it still sees the URIs and treats the operation as a copy
/// — a degradation that loses no data.
constexpr QLatin1String kCutMimeType{"x-special/gnome-copied-files"};

} // namespace

FileOperations::FileOperations(ui::MainWindow *window, ui::PanelStrip *strip,
                               input::ActionRegistry *registry, fs::JobEngine *engine,
                               fs::UndoStack *undoStack, QObject *parent)
    : QObject(parent), m_window(window), m_strip(strip), m_registry(registry), m_engine(engine),
      m_undoStack(undoStack)
{}

FileOperations::~FileOperations() = default;

void FileOperations::setSettings(const config::Settings &settings)
{
    m_settings = settings;
}

ui::ConflictModal *FileOperations::conflictModal()
{
    if (m_conflictModal == nullptr) {
        // §3.4: created on first use, then cached.
        m_conflictModal = new ui::ConflictModal(m_window);
    }
    return m_conflictModal;
}

ui::CompressModal *FileOperations::compressModal()
{
    if (m_compressModal == nullptr) {
        m_compressModal = new ui::CompressModal(m_window);

        connect(m_compressModal, &ui::CompressModal::compressRequested, this,
                [this](const QStringList &sources, const QString &destination,
                       fs::ArchiveFormat format) {
                    auto job = std::make_unique<fs::ArchiveJob>(sources, destination, format);
                    auto *raw = job.get();

                    const int jobId = m_engine->submit(std::move(job));

                    connect(m_engine, &fs::JobEngine::jobFinished, this,
                            [this, jobId, raw](int id, const fs::JobResult &result) {
                                if (id != jobId) {
                                    return;
                                }
                                if (!result.errors.isEmpty()) {
                                    Q_EMIT statusMessage(result.errors.first().reason);
                                    return;
                                }
                                if (result.cancelled) {
                                    return;
                                }
                                // §7.13 does not make archive creation undoable
                                // — it creates a file rather than changing one —
                                // so the cursor landing on the result is the
                                // whole of the follow-through.
                                if (ui::FilePanel *panel = m_strip->focusedPanel();
                                    panel != nullptr) {
                                    panel->setCursorName(QFileInfo(raw->destination()).fileName());
                                }
                                Q_EMIT statusMessage(
                                    tr("Created %1").arg(QFileInfo(raw->destination()).fileName()));
                            });
                });
    }
    return m_compressModal;
}

void FileOperations::compressSelection()
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    const QStringList sources = panel->selectedPaths();
    if (sources.isEmpty()) {
        Q_EMIT statusMessage(tr("Nothing selected to archive"));
        return;
    }

    compressModal()->start(sources, panel->path());
}

void FileOperations::extractCursorItem()
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    const QString archivePath = panel->cursorPath();
    if (archivePath.isEmpty() || QFileInfo(archivePath).isDir()) {
        Q_EMIT statusMessage(tr("Put the cursor on an archive to extract it"));
        return;
    }

    // §7.10: "extracts into <archive-basename>/ in the panel's cwd", with the
    // tarbomb rule deciding whether that nesting actually happens.
    auto job = std::make_unique<fs::ExtractJob>(archivePath, panel->path());
    auto *raw = job.get();

    const int jobId = m_engine->submit(std::move(job));

    connect(m_engine, &fs::JobEngine::jobFinished, this,
            [this, jobId, raw](int id, const fs::JobResult &result) {
                if (id != jobId) {
                    return;
                }
                if (!result.errors.isEmpty()) {
                    // §7.10: "fail with a clear message rather than partially
                    // extracting". The message is the job's own, which names
                    // what it refused and why.
                    Q_EMIT statusMessage(result.errors.first().reason);
                    return;
                }
                if (result.cancelled) {
                    return;
                }
                Q_EMIT statusMessage(
                    tr("Extracted to %1").arg(QFileInfo(raw->extractedTo()).fileName()));
            });
}

ui::InputModal *FileOperations::inputModal()
{
    if (m_inputModal == nullptr) {
        m_inputModal = new ui::InputModal(m_window);
    }
    return m_inputModal;
}

ui::RenameModal *FileOperations::renameModal()
{
    if (m_renameModal == nullptr) {
        m_renameModal = new ui::RenameModal(m_window);
    }
    return m_renameModal;
}

void FileOperations::createItem()
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    const QString directory = panel->path();
    ui::InputModal *modal = inputModal();

    // Disconnected first: the modal is cached and reused, so a previous
    // invocation's handler would still be attached and would create the file in
    // whichever directory that one was for.
    disconnect(modal, &ui::InputModal::submitted, nullptr, nullptr);

    connect(modal, &ui::InputModal::submitted, this, [this, modal, directory](const QString &name) {
        // §6.3: "Trailing / creates a directory".
        const bool wantsDirectory = name.endsWith(QLatin1Char('/'));
        QString basename = name;
        if (wantsDirectory) {
            basename.chop(1);
        }

        if (basename.isEmpty() || basename.contains(QLatin1Char('/'))) {
            modal->setProblem(tr("A name cannot contain “/”"));
            return;
        }

        const QString target = QDir(directory).absoluteFilePath(basename);
        if (QFileInfo::exists(target)) {
            modal->setProblem(tr("“%1” already exists").arg(basename));
            return;
        }

        bool created = false;
        if (wantsDirectory) {
            created = QDir(directory).mkpath(basename);
        } else {
            QFile file(target);
            created = file.open(QIODevice::WriteOnly);
            file.close();
        }

        if (!created) {
            modal->setProblem(tr("Could not create “%1”").arg(basename));
            return;
        }

        modal->dismiss();

        // The watcher will bring the row in; putting the cursor on it is what
        // makes creating a file and immediately doing something to it work.
        if (ui::FilePanel *panel = m_strip->focusedPanel(); panel != nullptr) {
            panel->setCursorName(basename);
        }
        Q_EMIT statusMessage(wantsDirectory ? tr("Created %1/").arg(basename)
                                            : tr("Created %1").arg(basename));
    });

    modal->ask(tr("New item"), tr("End the name with “/” to create a directory."), QString());
}

void FileOperations::renameCursorItem()
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr || panel->cursorName().isEmpty()) {
        return;
    }

    const QString directory = panel->path();
    const QString original = panel->cursorName();

    ui::InputModal *modal = inputModal();
    disconnect(modal, &ui::InputModal::submitted, nullptr, nullptr);

    connect(
        modal, &ui::InputModal::submitted, this,
        [this, modal, directory, original](const QString &name) {
            if (name == original) {
                modal->dismiss();
                return;
            }

            const fs::RenamePlan plan = fs::RenamePlanner::plan(
                {fs::RenamePair{.from = original, .to = name}},
                QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));

            if (!plan.isValid()) {
                modal->setProblem(plan.problemText());
                return;
            }

            modal->dismiss();
            runRenamePlan(directory, plan);
        });

    // The stem is preselected so typing replaces the name and keeps the
    // extension, which is what every file manager does and what a user renaming
    // "photo.jpg" almost always wants.
    const qsizetype dot = original.lastIndexOf(QLatin1Char('.'));
    const int stemLength = dot > 0 ? static_cast<int>(dot) : static_cast<int>(original.size());

    modal->ask(tr("Rename"), QString(), original, 0, stemLength);
}

void FileOperations::bulkRenameSelection()
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    const QString directory = panel->path();

    QList<QString> names;
    for (const QString &path : panel->selectedPaths()) {
        names.append(QFileInfo(path).fileName());
    }

    if (names.isEmpty()) {
        Q_EMIT statusMessage(tr("Nothing selected to rename"));
        return;
    }

    ui::RenameModal *modal = renameModal();
    disconnect(modal, &ui::RenameModal::renameRequested, nullptr, nullptr);

    connect(modal, &ui::RenameModal::renameRequested, this,
            [this, directory](const fs::RenamePlan &plan) { runRenamePlan(directory, plan); });

    modal->setNames(names);
    modal->setExistingNames(
        QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
    modal->start();
}

void FileOperations::runRenamePlan(const QString &directory, const fs::RenamePlan &plan)
{
    auto job = std::make_unique<fs::RenameJob>(directory, plan);
    auto *raw = job.get();

    const int jobId = m_engine->submit(std::move(job));

    connect(m_engine, &fs::JobEngine::jobFinished, this,
            [this, jobId, raw](int id, const fs::JobResult &result) {
                if (id != jobId) {
                    return;
                }

                if (!result.cancelled && !raw->completedRenames().isEmpty()) {
                    // §7.9 step 6, and §7.13's Rename/BulkRename kinds: one
                    // entry for the whole thing, so Ctrl+Z reverses the rename
                    // the user made rather than one file of it.
                    const bool bulk = raw->requestedChanges().size() > 1;
                    m_undoStack->push(
                        fs::UndoEntry{.kind = bulk ? fs::UndoEntry::Kind::BulkRename
                                                   : fs::UndoEntry::Kind::Rename,
                                      .description = bulk ? tr("Bulk rename") : tr("Rename"),
                                      .movedPairs = raw->completedRenames(),
                                      .trashedItems = {}});
                }

                if (!result.errors.isEmpty()) {
                    Q_EMIT statusMessage(tr("Rename failed: %1").arg(result.errors.first().reason));
                    return;
                }

                Q_EMIT statusMessage(tr("Renamed %n item(s)", nullptr,
                                        static_cast<int>(raw->requestedChanges().size())));
            });
}

QStringList FileOperations::clipboardPaths()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime == nullptr || !mime->hasUrls()) {
        return {};
    }

    QStringList paths;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        const QString local = url.toLocalFile();
        // Remote URLs are out of scope for v1 (§1's non-goals), and silently
        // treating one as a path would produce a nonsensical copy.
        if (!local.isEmpty()) {
            paths << local;
        }
    }
    return paths;
}

void FileOperations::setClipboardPaths(const QStringList &paths, bool cut)
{
    auto *mime = new QMimeData;

    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &path : paths) {
        urls << QUrl::fromLocalFile(path);
    }
    mime->setUrls(urls);

    QStringList lines;
    lines << (cut ? QStringLiteral("cut") : QStringLiteral("copy"));
    for (const QUrl &url : urls) {
        lines << url.toString();
    }
    mime->setData(kCutMimeType, lines.join(QLatin1Char('\n')).toUtf8());

    // text/plain as well, so pasting into a terminal or an editor gives the
    // paths rather than nothing.
    mime->setText(paths.join(QLatin1Char('\n')));

    QApplication::clipboard()->setMimeData(mime);
    m_clipboardIsCut = cut;
}

bool FileOperations::clipboardIsCut() const
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (mime != nullptr && mime->hasFormat(kCutMimeType)) {
        return mime->data(kCutMimeType).startsWith("cut");
    }
    // Nothing on the clipboard says otherwise, so fall back to what this
    // process last did — which covers the case of a clipboard implementation
    // that drops unknown types.
    return m_clipboardIsCut;
}

void FileOperations::copySelection(bool cut)
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    const QStringList paths = panel->selectedPaths();
    if (paths.isEmpty()) {
        Q_EMIT statusMessage(tr("Nothing to copy"));
        return;
    }

    // Copying accumulates, the way superfile does it: walk the list pressing
    // Ctrl+C on each file you want and they gather into one set, then paste
    // them together. That removes the need to enter Selection mode for the
    // common case of picking a handful of files that are not adjacent.
    //
    // Three things end the accumulation, and between them they stop the list
    // growing when nobody meant it to:
    //
    //   * a paste — the set has been used, so the next copy starts a new one;
    //   * switching between copy and cut, which are not mixable;
    //   * an explicit multi-file selection, which plainly means "these, and
    //     only these".
    const bool replaces =
        m_pastedSinceCopy || cut != m_clipboardIsCut || panel->selectionCount() > 1;

    if (replaces) {
        m_pendingPaths.clear();
    }

    for (const QString &path : paths) {
        // Pressing it twice on the same file is a repeat, not a second copy.
        if (!m_pendingPaths.contains(path)) {
            m_pendingPaths.append(path);
        }
    }

    m_pastedSinceCopy = false;

    setClipboardPaths(m_pendingPaths, cut);

    const int count = static_cast<int>(m_pendingPaths.size());
    Q_EMIT statusMessage(cut ? tr("Cut %n item(s) — Ctrl+V to move them", nullptr, count)
                             : tr("Copied %n item(s) — Ctrl+V to paste them", nullptr, count));
}

void FileOperations::clearClipboard()
{
    if (m_pendingPaths.isEmpty()) {
        return;
    }

    m_pendingPaths.clear();
    m_pastedSinceCopy = false;
    setClipboardPaths({}, false);

    Q_EMIT statusMessage(tr("Copy list cleared"));
}

bool FileOperations::hasPendingClipboard() const
{
    return !m_pendingPaths.isEmpty();
}

void FileOperations::pasteIntoFocusedPanel()
{
    const ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    const QStringList paths = clipboardPaths();
    if (paths.isEmpty()) {
        Q_EMIT statusMessage(tr("Nothing copied yet — Ctrl+C on a file adds it to the list"));
        return;
    }

    const bool cut = clipboardIsCut();
    runTransfer(paths, panel->path(), cut);

    // Pasting empties the list. It has been used, and leaving it filled is how
    // a copy made an hour later quietly drags along everything from before it.
    m_pendingPaths.clear();
    m_pastedSinceCopy = true;
    setClipboardPaths({}, cut);
}

void FileOperations::onFilesDropped(const QStringList &paths, const QString &destination,
                                    Qt::DropAction action)
{
    // A drop of a file onto the directory it already lives in is a gesture that
    // slipped, not an instruction. Acting on it would either duplicate the file
    // as "x (2)" or, worse for a move, ask about a conflict with itself.
    QStringList transferable;
    for (const QString &path : paths) {
        if (QFileInfo(path).absolutePath() != QDir(destination).absolutePath()) {
            transferable.append(path);
        }
    }

    if (transferable.isEmpty()) {
        return;
    }

    runTransfer(transferable, destination, action == Qt::MoveAction);
}

namespace {

/// The undo shortcut in the platform's own notation — ⌘Z on macOS, Ctrl+Z
/// elsewhere. Qt renders the standard sequence, so this needs no #ifdef and no
/// second place to keep in step.
QString undoShortcut()
{
    return QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText);
}

} // namespace

void FileOperations::runTransfer(const QStringList &paths, const QString &destination, bool move)
{
    auto job = std::make_unique<fs::TransferJob>(
        move ? fs::TransferJob::Mode::Move : fs::TransferJob::Mode::Copy, paths, destination);

    const fs::TransferJob *raw = job.get();
    const int jobId = m_engine->submit(std::move(job));

    connect(m_engine, &fs::JobEngine::jobConflict, this,
            [this, jobId](int id, const QString &source, const QString &conflictDestination,
                          const fs::ConflictInfo &info) {
                if (id != jobId) {
                    return;
                }
                ui::ConflictModal *modal = conflictModal();
                // A fresh connection per conflict, disconnected on the first
                // answer: the modal is reused across jobs, and a stale
                // connection would answer a later job's question with an
                // earlier job's decision.
                auto *connection = new QMetaObject::Connection;
                *connection =
                    connect(modal, &ui::ConflictModal::resolved, this,
                            [this, jobId, connection](const fs::ConflictResolution &resolution) {
                                disconnect(*connection);
                                delete connection;
                                if (fs::Job *job = m_engine->job(jobId); job != nullptr) {
                                    job->resolveConflict(resolution);
                                }
                            });
                modal->present(source, conflictDestination, info);
            });

    connect(m_engine, &fs::JobEngine::jobFinished, this,
            [this, jobId, raw, move, destination](int id, const fs::JobResult &result) {
                if (id != jobId) {
                    return;
                }

                // §7.13: "Copy and permanent delete are **not** undoable." A
                // move is, because putting the files back is exactly the
                // inverse; undoing a copy would mean deleting files, which is a
                // more destructive operation than the one being reversed.
                if (move && !result.cancelled && !raw->removedSources().isEmpty()) {
                    QList<QPair<QString, QString>> pairs;
                    const QStringList created = raw->createdPaths();
                    const QStringList removed = raw->removedSources();
                    for (int i = 0; i < std::min(created.size(), removed.size()); ++i) {
                        pairs.append({removed.at(i), created.at(i)});
                    }
                    m_undoStack->push(fs::UndoEntry{.kind = fs::UndoEntry::Kind::Move,
                                                    .description = tr("Move"),
                                                    .movedPairs = pairs,
                                                    .trashedItems = {}});
                }

                if (!result.errors.isEmpty()) {
                    Q_EMIT statusMessage(tr("%n item(s) could not be transferred: %1", nullptr,
                                            static_cast<int>(result.errors.size()))
                                             .arg(result.errors.first().reason));
                    return;
                }

                if (result.cancelled || result.filesProcessed == 0) {
                    return;
                }

                // Say where they went.
                //
                // A drag onto a directory row targets that directory, which is
                // both what §7.12 asks for and what Finder does — but it is easy
                // to do by accident, and a move that happens in silence looks
                // exactly like a file disappearing. Naming the destination is
                // what turns "where has it gone" into "oh, in there", and the
                // reminder that a move is undoable is worth more here than
                // anywhere else in the application.
                const QString where = QDir(destination).dirName();
                Q_EMIT statusMessage(
                    move
                        ? tr("Moved %n item(s) to %1 — %2 to undo", nullptr, result.filesProcessed)
                              .arg(where, undoShortcut())
                        : tr("Copied %n item(s) to %1", nullptr, result.filesProcessed).arg(where));
            });
}

void FileOperations::deleteSelection(bool permanent)
{
    ui::FilePanel *panel = m_strip->focusedPanel();
    if (panel == nullptr) {
        return;
    }

    const QStringList paths = panel->selectedPaths();
    if (paths.isEmpty()) {
        Q_EMIT statusMessage(tr("Nothing to delete"));
        return;
    }

    const bool confirm =
        permanent ? m_settings.operations.confirmDelete : m_settings.operations.confirmTrash;

    if (confirm || permanent) {
        // §6.3: permanent deletion "Confirms twice". The wording says plainly
        // that it cannot be undone, which §7.13 requires: "Copy and permanent
        // delete are not undoable and must be labelled as such in the
        // confirmation."
        const QString question =
            permanent
                ? tr("Permanently delete %n item(s)? This cannot be undone.", nullptr,
                     static_cast<int>(paths.size()))
                : tr("Move %n item(s) to the trash?", nullptr, static_cast<int>(paths.size()));

        if (QMessageBox::question(m_window, tr("Panefile"), question,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            return;
        }

        if (permanent &&
            QMessageBox::question(m_window, tr("Panefile"),
                                  tr("Really? %n item(s) will be destroyed with no way back.",
                                     nullptr, static_cast<int>(paths.size())),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    auto job = std::make_unique<fs::DeleteJob>(
        permanent ? fs::DeleteJob::Mode::Permanent : fs::DeleteJob::Mode::Trash, paths);

    const fs::DeleteJob *raw = job.get();
    const int jobId = m_engine->submit(std::move(job));

    connect(m_engine, &fs::JobEngine::jobFinished, this,
            [this, jobId, raw, permanent](int id, const fs::JobResult &result) {
                if (id != jobId) {
                    return;
                }
                if (!permanent && !result.cancelled && !raw->trashedItems().isEmpty()) {
                    m_undoStack->push(fs::UndoEntry{.kind = fs::UndoEntry::Kind::Trash,
                                                    .description = tr("Move to trash"),
                                                    .movedPairs = {},
                                                    .trashedItems = raw->trashedItems()});
                }
                if (!result.errors.isEmpty()) {
                    Q_EMIT statusMessage(result.errors.first().reason);
                }
            });

    panel->clearSelection();
}

void FileOperations::undoLast()
{
    QString error;
    if (m_undoStack->undo(&error)) {
        Q_EMIT statusMessage(tr("Undone"));
        return;
    }
    Q_EMIT statusMessage(error);
}

void FileOperations::registerActions()
{
    const auto reg = [registry = m_registry](const char *id, const QString &description,
                                             std::function<void()> handler,
                                             std::function<bool()> enabled = {}) {
        registry->registerAction(QString::fromLatin1(id), description,
                                 ActionCategory::FileOperations, std::move(handler),
                                 std::move(enabled));
    };

    reg("copy_items", tr("Copy the selection to the clipboard"), [this] { copySelection(false); });
    reg("cut_items", tr("Cut the selection to the clipboard"), [this] { copySelection(true); });
    reg(
        "paste_items", tr("Paste into this directory"), [this] { pasteIntoFocusedPanel(); },
        [] { return !clipboardPaths().isEmpty(); });

    reg("delete_items", tr("Move the selection to the trash"), [this] { deleteSelection(false); });
    reg("permanently_delete_items", tr("Delete the selection permanently"),
        [this] { deleteSelection(true); });

    reg(
        "undo", tr("Undo the last move, rename or trash"), [this] { undoLast(); },
        [this] { return m_undoStack->canUndo(); });

    // §7.10's archive support was built — CompressModal, ArchiveJob, ExtractJob,
    // the tarbomb rule, all of it tested — and then never registered as an
    // action, so no key reached any of it and the entire feature was
    // unreachable from the running application.
    reg("compress_file", tr("Compress the selection into an archive"),
        [this] { compressSelection(); });
    reg("extract_file", tr("Extract the archive under the cursor"),
        [this] { extractCursorItem(); });

    reg("file_panel_item_create", tr("Create a file, or a directory with a trailing “/”"),
        [this] { createItem(); });
    reg("file_panel_item_rename", tr("Rename the cursor item"), [this] { renameCursorItem(); });
    reg("bulk_rename", tr("Rename the selection with a rule"), [this] { bulkRenameSelection(); });

    // Selection mode and its movement bindings (§6.1, §6.3).
    const auto onPanel = [strip = m_strip](auto action) {
        return [strip, action] {
            if (ui::FilePanel *panel = strip->focusedPanel(); panel != nullptr) {
                action(panel);
            }
        };
    };

    m_registry->registerAction(QStringLiteral("change_panel_mode"),
                               tr("Switch between Normal and Selection mode"),
                               ActionCategory::Selection,
                               onPanel([](ui::FilePanel *panel) { panel->toggleSelectionMode(); }));

    m_registry->registerAction(QStringLiteral("select_all"), tr("Select every visible entry"),
                               ActionCategory::Selection,
                               onPanel([](ui::FilePanel *panel) { panel->selectAll(); }));

    // §6.1: "Movement extends the selection" in Selection mode — the entry the
    // cursor leaves is the one that gets selected, so a run of J selects the
    // run it passes over.
    m_registry->registerAction(QStringLiteral("select_down"), tr("Extend the selection downwards"),
                               ActionCategory::Selection, onPanel([](ui::FilePanel *panel) {
                                   panel->toggleSelectionAt(panel->cursorName());
                                   panel->moveCursor(1);
                               }));

    m_registry->registerAction(QStringLiteral("select_up"), tr("Extend the selection upwards"),
                               ActionCategory::Selection, onPanel([](ui::FilePanel *panel) {
                                   panel->toggleSelectionAt(panel->cursorName());
                                   panel->moveCursor(-1);
                               }));
}

} // namespace pf
