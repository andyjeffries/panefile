#include "app/ChooserController.h"

#include "core/Format.h"
#include "platform/Paths.h"
#include "ui/ChooserBar.h"
#include "ui/FilePanel.h"
#include "ui/MainWindow.h"
#include "ui/PanelStrip.h"
#include "ui/PanelView.h"
#include "ui/modals/ConfirmModal.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>
#include <optional>

namespace pf {
namespace {

using Mode = platform::FileChooserRequest::Mode;

constexpr int kListedNames = 5;

QString lastFolderFile()
{
    return platform::stateDir() + QStringLiteral("/chooser.ini");
}

QString lastFolderKey(const QString &appId)
{
    QString key = appId.isEmpty() ? QStringLiteral("default") : appId;
    key.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QStringLiteral("lastFolder/") + key;
}

QString folderLabel(const QString &path)
{
    return path == QLatin1String("/") ? path : QFileInfo(path).fileName();
}

QString withoutMnemonics(const QString &label)
{
    QString text;
    for (qsizetype i = 0; i < label.size(); ++i) {
        if (label.at(i) == QLatin1Char('_')) {
            if (i + 1 < label.size() && label.at(i + 1) == QLatin1Char('_')) {
                text += QLatin1Char('_');
                ++i;
            }
            continue;
        }
        text += label.at(i);
    }
    return text;
}

QString cleanPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// What an app sends as a name is a name, never a path: only the last segment is
// kept, and "." / ".." / "~" are not names.
QString appName(const QString &sent)
{
    const QString name = QFileInfo(sent).fileName();
    if (name == QLatin1String("~") || name == QLatin1String(".") || name == QLatin1String("..")) {
        return {};
    }
    return name;
}

QString readOnlyMessage(const QString &folder)
{
    return ChooserController::tr("“%1” is read-only. Choose a folder you can save in.")
        .arg(folderLabel(folder));
}

} // namespace

ChooserController::ChooserController(const platform::FileChooserRequest &request,
                                     ui::MainWindow *window, QObject *parent)
    : QObject(parent), m_request(request), m_window(window),
      m_panel(window->panelStrip()->focusedPanel())
{
    if (m_request.currentName.isEmpty() && !m_request.currentFile.isEmpty()) {
        m_request.currentName = m_request.currentFile;
    }
    m_request.currentName = appName(m_request.currentName);

    for (const QString &file : m_request.files) {
        const QString name = appName(file);
        if (name.isEmpty()) {
            const QString sent = QFileInfo(file).fileName();
            m_saveFilesError = sent.isEmpty() ? tr("The app asked to save a file with no name. Cancel and try again "
                                                 "from the app.")
                                              : tr("The app asked to save a file called “%1”, which "
                                                   "isn't allowed. Cancel and try again from the app.")
                                                    .arg(sent);
            break;
        }
        m_saveFileNames << name;
    }

    m_bar = new ui::ChooserBar(m_request, window);
    window->setBottomBar(m_bar);
    window->setFixedTitle(windowTitle(m_request));
    window->installEventFilter(this);

    const QString label = withoutMnemonics(m_request.acceptLabel);
    if (!label.isEmpty()) {
        m_bar->setAcceptLabel(label);
    } else if (choosesFolder()) {
        m_bar->setAcceptLabel(tr("Select"));
    } else {
        m_bar->setAcceptLabel(m_request.mode == Mode::Open ? tr("Open") : tr("Save"));
    }

    m_panel->setDirectoriesOnly(choosesFolder() || m_request.mode == Mode::SaveFiles);
    applyFilter(m_bar->filterIndex());

    connect(m_bar, &ui::ChooserBar::acceptRequested, this, &ChooserController::accept);
    connect(m_bar, &ui::ChooserBar::cancelRequested, this, &ChooserController::cancel);
    connect(m_bar, &ui::ChooserBar::filterChanged, this, &ChooserController::applyFilter);
    connect(m_bar, &ui::ChooserBar::nameEdited, this, &ChooserController::refresh);
    connect(m_bar, &ui::ChooserBar::listFocusRequested, this,
            [this] { m_panel->view()->setFocus(Qt::OtherFocusReason); });
    connect(m_panel, &ui::FilePanel::pathChanged, this, &ChooserController::refresh);
    connect(m_panel, &ui::FilePanel::selectionChanged, this, &ChooserController::refresh);
    refresh();
}

QString ChooserController::startFolder(const platform::FileChooserRequest &request)
{
    const auto isFolder = [](const QString &path) {
        return !path.isEmpty() && QFileInfo(path).isDir();
    };

    if (isFolder(request.currentFolder)) {
        return cleanPath(request.currentFolder);
    }
    if (!request.currentFile.isEmpty() && isFolder(QFileInfo(request.currentFile).absolutePath())) {
        return cleanPath(QFileInfo(request.currentFile).absolutePath());
    }
    const QString last = QSettings(lastFolderFile(), QSettings::IniFormat)
                             .value(lastFolderKey(request.appId))
                             .toString();
    if (isFolder(last)) {
        return last;
    }
    return QDir::homePath();
}

QString ChooserController::windowTitle(const platform::FileChooserRequest &request)
{
    if (!request.title.isEmpty()) {
        return request.title;
    }
    return request.mode == Mode::Open ? tr("Open File") : tr("Save File");
}

ui::ChooserBar *ChooserController::bar() const
{
    return m_bar;
}

bool ChooserController::savesOne() const
{
    return m_request.mode == Mode::Save;
}

bool ChooserController::choosesFolder() const
{
    return m_request.mode == Mode::Open && m_request.directory;
}

void ChooserController::focusInitial()
{
    if (savesOne()) {
        m_bar->focusName();
    } else {
        m_panel->view()->setFocus(Qt::OtherFocusReason);
    }
}

void ChooserController::focusName()
{
    if (savesOne()) {
        m_bar->focusName();
    }
}

bool ChooserController::isFinished() const
{
    return m_finished;
}

ChooserController::SaveTarget ChooserController::resolveSaveTarget(const QString &typed) const
{
    SaveTarget target;
    const QString name = typed.trimmed();
    if (name.isEmpty()) {
        target.blank = true;
        return target;
    }
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        target.error = tr("“%1” can't be used as a file name.").arg(name);
        return target;
    }

    QString path;
    if (name == QLatin1String("~") || name.startsWith(QLatin1String("~/"))) {
        path = QDir::homePath() + name.mid(1);
    } else if (QDir::isAbsolutePath(name)) {
        path = name;
    } else {
        path = m_panel->path() + QLatin1Char('/') + name;
    }
    path = QDir::cleanPath(path);
    target.path = path;

    const QFileInfo info(path);
    if (info.isDir()) {
        target.isFolder = true;
        return target;
    }

    const QFileInfo folder(info.absolutePath());
    if (!folder.isDir()) {
        target.error = tr("“%1” isn't a folder.").arg(folderLabel(folder.absoluteFilePath()));
    } else if (!folder.isWritable()) {
        target.error = readOnlyMessage(folder.absoluteFilePath());
    } else if (info.exists() && !info.isWritable()) {
        target.error = tr("“%1” can't be replaced.").arg(info.fileName());
    }
    return target;
}

void ChooserController::refresh()
{
    QString error;
    bool enabled = true;

    if (savesOne()) {
        const SaveTarget target = resolveSaveTarget(m_bar->name());
        error = target.error;
        enabled = !target.blank && target.error.isEmpty();
    } else if (m_request.mode == Mode::SaveFiles) {
        if (!m_saveFilesError.isEmpty()) {
            error = m_saveFilesError;
            enabled = false;
        } else if (!QFileInfo(m_panel->path()).isWritable()) {
            error = readOnlyMessage(m_panel->path());
            enabled = false;
        }
        if (m_request.acceptLabel.isEmpty()) {
            m_bar->setAcceptLabel(
                tr("Save %1 in “%2”")
                    .arg(counted(static_cast<int>(m_request.files.size()), tr("file"), tr("files")),
                         folderLabel(m_panel->path())));
        }
    } else if (choosesFolder() && m_request.acceptLabel.isEmpty()) {
        const QStringList chosen =
            m_panel->selectionCount() > 0 ? m_panel->selectedPaths() : QStringList{m_panel->path()};
        m_bar->setAcceptLabel(
            chosen.size() > 1 ? tr("Select %n folders", nullptr, static_cast<int>(chosen.size()))
                              : tr("Select “%1”").arg(folderLabel(chosen.constFirst())));
    }

    m_bar->setAcceptEnabled(enabled);
    m_window->setStatusError(error);
}

void ChooserController::applyFilter(int index)
{
    std::optional<QStringList> globs;
    if (index >= 0 && index < m_request.filters.size()) {
        globs = platform::globsForFilter(m_request.filters.at(index));
    }
    m_panel->setNameFilters(globs);
}

ui::ConfirmModal *ChooserController::confirmModal()
{
    if (m_confirm == nullptr) {
        m_confirm = new ui::ConfirmModal(m_window);
    }
    return m_confirm;
}

bool ChooserController::claimActivation(const QString &path)
{
    if (m_finished) {
        return true;
    }
    if (savesOne()) {
        m_bar->setName(QFileInfo(path).fileName());
        m_bar->focusName();
        return true;
    }
    if (m_request.mode != Mode::Open || m_request.directory) {
        return true;
    }

    QStringList picks{path};
    if (m_request.multiple && m_panel->selectedPaths().contains(path) &&
        m_panel->selectionCount() > 1) {
        picks = m_panel->selectedPaths();
    }
    finishWith(picks);
    return true;
}

void ChooserController::accept()
{
    if (m_finished) {
        return;
    }
    switch (m_request.mode) {
    case Mode::Open:
        acceptOpen();
        break;
    case Mode::Save:
        acceptSave();
        break;
    case Mode::SaveFiles:
        acceptSaveFiles();
        break;
    }
}

void ChooserController::acceptOpen()
{
    if (choosesFolder()) {
        QStringList picks =
            m_panel->selectionCount() > 0 ? m_panel->selectedPaths() : QStringList{m_panel->path()};
        picks.erase(std::remove_if(picks.begin(), picks.end(),
                                   [](const QString &path) { return !QFileInfo(path).isDir(); }),
                    picks.end());
        if (!m_request.multiple && picks.size() > 1) {
            picks = picks.mid(0, 1);
        }
        finishWith(picks);
        return;
    }

    QStringList picks;
    if (m_request.multiple && m_panel->selectionCount() > 0) {
        picks = m_panel->selectedPaths();
    } else if (const QString cursor = m_panel->cursorPath(); !cursor.isEmpty()) {
        picks = {cursor};
    }

    // Open on a folder goes into it, as Enter does.
    if (m_panel->selectionCount() == 0 && picks.size() == 1 &&
        QFileInfo(picks.constFirst()).isDir()) {
        m_panel->activateCursorItem();
        return;
    }

    picks.erase(std::remove_if(picks.begin(), picks.end(),
                               [](const QString &path) { return QFileInfo(path).isDir(); }),
                picks.end());
    if (picks.isEmpty()) {
        m_window->showStatusMessage(tr("Choose a file first"));
        return;
    }
    finishWith(picks);
}

void ChooserController::acceptSave()
{
    const SaveTarget target = resolveSaveTarget(m_bar->name());
    if (target.blank) {
        return;
    }
    if (!target.error.isEmpty()) {
        m_window->setStatusError(target.error);
        return;
    }
    if (target.isFolder) {
        m_panel->navigateTo(target.path);
        m_bar->setName(QString());
        return;
    }
    if (!QFileInfo::exists(target.path)) {
        finishWith({target.path});
        return;
    }

    const QString name = QFileInfo(target.path).fileName();
    const QString folder = folderLabel(QFileInfo(target.path).absolutePath());
    confirmModal()->ask(
        tr("Replace “%1”?").arg(name),
        tr("A file with this name is already in %1. Replacing it overwrites what is there.")
            .arg(folder),
        tr("Replace"), [this, path = target.path] { finishWith({path}); });
}

void ChooserController::acceptSaveFiles()
{
    if (!m_saveFilesError.isEmpty()) {
        m_window->setStatusError(m_saveFilesError);
        return;
    }
    const QString folder = m_panel->path();
    if (!QFileInfo(folder).isWritable()) {
        m_window->setStatusError(readOnlyMessage(folder));
        return;
    }

    QStringList targets;
    QStringList existing;
    for (const QString &name : m_saveFileNames) {
        const QString path = folder + QLatin1Char('/') + name;
        targets << path;
        if (QFileInfo::exists(path)) {
            existing << name;
        }
    }

    if (existing.isEmpty()) {
        finishWith(targets);
        return;
    }

    QStringList listed = existing.mid(0, kListedNames);
    if (existing.size() > kListedNames) {
        listed << tr("and %n more", nullptr, static_cast<int>(existing.size()) - kListedNames);
    }
    confirmModal()->ask(
        tr("Replace %1 in “%2”?")
            .arg(counted(static_cast<int>(existing.size()), tr("file"), tr("files")),
                 folderLabel(folder)),
        listed.join(QLatin1Char('\n')) + QStringLiteral("\n\n") +
            (existing.size() == 1 ? tr("Replacing it overwrites what is there.")
                                  : tr("Replacing them overwrites what is there.")),
        tr("Replace"), [this, targets] { finishWith(targets); });
}

void ChooserController::finishWith(const QStringList &paths)
{
    if (m_finished) {
        return;
    }
    m_finished = true;

    platform::FileChooserResult result;
    for (const QString &path : paths) {
        result.paths << cleanPath(path);
    }
    result.filterIndex = m_bar->filterIndex();
    result.choices = m_bar->choiceValues();

    QSettings(lastFolderFile(), QSettings::IniFormat)
        .setValue(lastFolderKey(m_request.appId), m_panel->path());
    Q_EMIT finished(true, result);
}

void ChooserController::cancel()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    Q_EMIT finished(false, {});
}

bool ChooserController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::Close && !m_finished) {
        event->ignore();
        cancel();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace pf
