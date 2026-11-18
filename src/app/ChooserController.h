#pragma once

#include "platform/FileChooserRequest.h"

#include <QObject>

namespace pf::ui {
class ChooserBar;
class ConfirmModal;
class FilePanel;
class MainWindow;
} // namespace pf::ui

namespace pf {

/// What a file dialog does, on top of the one panel it has: which folder to
/// start in, what Enter on a file means, what the accept button returns and
/// when it is allowed to.
///
/// It owns no process or I/O. The result leaves through finished(), and the
/// composition root decides what that means — print it and exit, in the
/// chooser process; record it, in a test.
class ChooserController : public QObject
{
    Q_OBJECT

public:
    /// `window` must have its one panel already, at `startFolder()`.
    ChooserController(const platform::FileChooserRequest &request, ui::MainWindow *window,
                      QObject *parent = nullptr);

    /// The folder to open in: the application's suggestion, the folder of the
    /// file it named, the last folder used for this application, then home.
    /// Folders that don't exist are skipped.
    static QString startFolder(const platform::FileChooserRequest &request);

    /// The title for the window: the application's, or Open File / Save File.
    static QString windowTitle(const platform::FileChooserRequest &request);

    ui::ChooserBar *bar() const;

    /// Puts the focus where a dialog starts: the Name field of a Save dialog,
    /// the list otherwise.
    void focusInitial();

    /// The Name field, in a Save dialog. Does nothing elsewhere.
    void focusName();

    /// Enter or a double-click on a file. True when the dialog has taken it.
    bool claimActivation(const QString &path);

    void accept();
    void cancel();

    bool isFinished() const;

    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void finished(bool accepted, const pf::platform::FileChooserResult &result);

private:
    struct SaveTarget {
        QString path;
        QString error;      ///< why it can't be saved, or empty
        bool blank = false; ///< nothing typed yet, which is not an error
        bool isFolder = false;
    };

    bool savesOne() const;
    bool choosesFolder() const;

    /// What Save would write, and whether it may. `name` is as typed.
    SaveTarget resolveSaveTarget(const QString &name) const;

    /// Save's enabled state and the footer's reason, from the folder and the
    /// name as they are now.
    void refresh();

    void acceptOpen();
    void acceptSave();
    void acceptSaveFiles();
    void finishWith(const QStringList &paths);
    void applyFilter(int index);
    ui::ConfirmModal *confirmModal();

    platform::FileChooserRequest m_request;
    ui::MainWindow *m_window = nullptr;
    ui::FilePanel *m_panel = nullptr;
    ui::ChooserBar *m_bar = nullptr;
    ui::ConfirmModal *m_confirm = nullptr;
    QStringList m_saveFileNames; ///< SaveFiles names, reduced to a last segment
    QString m_saveFilesError;    ///< why the app's names can't be used, or empty
    bool m_finished = false;
};

} // namespace pf
