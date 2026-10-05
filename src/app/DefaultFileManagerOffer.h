#pragma once

#include "platform/DefaultFileManager.h"

#include <QObject>
#include <QString>

class QTextStream;

namespace pf::input {
class ActionRegistry;
}

namespace pf::platform {
class FileManagerService;
}

namespace pf::ui {
class DefaultFileManagerBar;
class MainWindow;
} // namespace pf::ui

namespace pf {

/// Everything that decides whether the offer appears, gathered in one place so
/// each "don't ask" rule can be tested on its own.
struct DefaultOfferConditions {
    bool supported = false;             ///< Linux; macOS has nothing to offer
    bool optedIn = true;                ///< general.offer_default_file_manager
    bool alreadyDefault = false;        ///< both halves already point at Panefile
    bool desktopEntryInstalled = false; ///< a bare build tree can't be a default
    bool dbusActivated = false;         ///< started to answer "Show in folder"
    bool headless = false;              ///< tests, or QT_QPA_PLATFORM=offscreen
    bool dismissedThisRun = false;      ///< "Not now" was pressed
};

/// Offers to make Panefile the default file manager, and does it.
///
/// The work runs after the first paint and off the GUI thread: finding the
/// current default means reading half a dozen `mimeapps.list` files and
/// walking `applications/` directories, none of which a launch should wait
/// for. Only once the answer is in does anything appear — or, when Panefile is
/// already the default, does this instance claim org.freedesktop.FileManager1.
class DefaultFileManagerOffer : public QObject
{
    Q_OBJECT

public:
    /// The registry ids of the bar's three answers.
    static constexpr const char *kYesAction = "default_file_manager_yes";
    static constexpr const char *kNotNowAction = "default_file_manager_not_now";
    static constexpr const char *kNeverAction = "default_file_manager_never";

    /// Registers the three answers. Actions rather than button mnemonics, so
    /// they go through the keymap like every other key — remappable, and
    /// listed by `?` — and enabled only while the bar is asking, so outside it
    /// their keys do nothing.
    void registerActions(input::ActionRegistry *registry);

    /// `service` may be null, in which case nothing is ever claimed.
    DefaultFileManagerOffer(ui::MainWindow *window, platform::FileManagerService *service,
                            QObject *parent = nullptr);
    ~DefaultFileManagerOffer() override;

    /// The rule itself.
    static bool shouldOffer(const DefaultOfferConditions &conditions);

    /// Running somewhere nobody can answer a question: under the test suite,
    /// the startup benchmark, or any other offscreen run.
    static bool isHeadless();

    /// Started by the session bus. `--dbus-service` says so, and so does the
    /// environment the bus gives every process it activates.
    static bool isDbusActivated(bool dbusServiceFlag);

    /// The context `start()` decides in. Exposed so a test can set it without
    /// an offscreen platform deciding for it.
    void setContext(bool optedIn, bool dbusActivated, bool headless, bool mayClaimName);

    /// Reads the current state off the GUI thread, then offers or claims as
    /// it calls for. Nothing at all happens when headless or unsupported.
    void start(bool optedIn, bool dbusActivated, bool mayClaimName);

    /// What the state lookup found. Called with the worker's answer; a test
    /// calls it directly.
    void applyStatus(const platform::DefaultFileManagerStatus &status);

    /// Live reload of the config key. Turning it off hides the bar now.
    void setOptedIn(bool optedIn);

    /// Keys shown on the bar's buttons.
    void setKeyHints(const QString &yes, const QString &notNow, const QString &never);

    /// The three answers.
    void accept();
    void notNow();
    void never();

    /// Does what Yes does, without needing the bar: Settings' "Make default".
    void makeDefault();

    bool isOffering() const;

    /// Null until the bar is first needed.
    ui::DefaultFileManagerBar *bar() const;

Q_SIGNALS:
    void statusMessage(const QString &message);

    /// makeDefault() or accept() finished.
    void madeDefault(bool ok, const QString &message);

private:
    ui::DefaultFileManagerBar *ensureBar();
    DefaultOfferConditions conditions() const;
    void showOfferIfWanted();
    void claimName();
    void onMadeDefault(const platform::MakeDefaultResult &result);

    ui::MainWindow *m_window = nullptr;
    platform::FileManagerService *m_service = nullptr;
    ui::DefaultFileManagerBar *m_bar = nullptr;

    platform::DefaultFileManagerStatus m_status;
    bool m_statusKnown = false;

    bool m_optedIn = true;
    bool m_dbusActivated = false;
    bool m_headless = false;
    bool m_mayClaimName = false;
    bool m_dismissed = false;
    bool m_working = false;

    /// Set by Yes, so a "queued behind Nautilus" that arrives a moment later
    /// is added to the outcome rather than reported separately.
    bool m_reportClaim = false;

    QString m_yesHint;
    QString m_notNowHint;
    QString m_neverHint;
};

/// `pf --make-default`. Returns the exit code.
int runMakeDefaultCommand(QTextStream &out, QTextStream &err);

/// `pf --default-status`. Returns the exit code.
int runDefaultStatusCommand(QTextStream &out);

} // namespace pf
