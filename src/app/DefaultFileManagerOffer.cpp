#include "app/DefaultFileManagerOffer.h"

#include "input/ActionRegistry.h"
#include "config/TomlWriter.h"
#include "core/Logging.h"
#include "core/WorkerPools.h"
#include "platform/FileManagerService.h"
#include "platform/Paths.h"
#include "ui/DefaultFileManagerBar.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QGuiApplication>
#include <QPointer>
#include <QTextStream>
#include <QThreadPool>

namespace pf {
namespace {

QThreadPool *pool()
{
    return WorkerPools::acquire("pf-default-fm", 1);
}

QString tildePath(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home + QLatin1Char('/')) ? QStringLiteral("~") + path.mid(home.size())
                                                    : path;
}

} // namespace

DefaultFileManagerOffer::DefaultFileManagerOffer(ui::MainWindow *window,
                                                 platform::FileManagerService *service,
                                                 QObject *parent)
    : QObject(parent), m_window(window), m_service(service)
{
    if (m_service == nullptr) {
        return;
    }

    connect(m_service, &platform::FileManagerService::nameQueued, this,
            [this](const QString &owner) {
                // Never a fight: the name is queued, and comes to Panefile when
                // the owner exits. Saying which process that is turns "it didn't
                // work" into something the user can act on.
                const QString note =
                    tr("“Show in folder” takes effect once %1 exits, or after your next login.")
                        .arg(owner);
                if (m_reportClaim && m_bar != nullptr && m_bar->isShowingResult()) {
                    m_bar->showResult(m_bar->text() + QStringLiteral(". ") + note, true);
                } else {
                    Q_EMIT statusMessage(note);
                }
                m_reportClaim = false;
            });

    connect(m_service, &platform::FileManagerService::nameAcquired, this,
            [this] { m_reportClaim = false; });

    connect(m_service, &platform::FileManagerService::nameUnavailable, this,
            [this](const QString &reason) {
                qCWarning(pfIpc) << "could not take org.freedesktop.FileManager1:" << reason;
                if (m_reportClaim) {
                    Q_EMIT statusMessage(tr("“Show in folder” is unavailable: %1").arg(reason));
                }
                m_reportClaim = false;
            });
}

DefaultFileManagerOffer::~DefaultFileManagerOffer() = default;

void DefaultFileManagerOffer::registerActions(input::ActionRegistry *registry)
{
    const auto offering = [this] { return isOffering(); };
    registry->registerAction(
        QLatin1String(kYesAction), tr("Make Panefile the default file manager (when offered)"),
        input::ActionCategory::General, [this] { accept(); }, offering);
    registry->registerAction(
        QLatin1String(kNotNowAction),
        tr("Dismiss the default file manager offer until next launch"),
        input::ActionCategory::General, [this] { notNow(); }, offering);
    registry->registerAction(
        QLatin1String(kNeverAction), tr("Never offer to become the default file manager"),
        input::ActionCategory::General, [this] { never(); }, offering);
}

bool DefaultFileManagerOffer::shouldOffer(const DefaultOfferConditions &conditions)
{
    return conditions.supported && conditions.optedIn && !conditions.alreadyDefault &&
           conditions.desktopEntryInstalled && !conditions.dbusActivated && !conditions.headless &&
           !conditions.dismissedThisRun;
}

bool DefaultFileManagerOffer::isHeadless()
{
    const QString platform = QGuiApplication::platformName().isEmpty()
                                 ? qEnvironmentVariable("QT_QPA_PLATFORM")
                                 : QGuiApplication::platformName();
    return platform == QLatin1String("offscreen") || platform == QLatin1String("minimal");
}

bool DefaultFileManagerOffer::isDbusActivated(bool dbusServiceFlag)
{
    // DBUS_STARTER_BUS_TYPE is set by the bus in the environment of every
    // process it activates, so this holds even for a service file someone
    // wrote by hand without --dbus-service.
    return dbusServiceFlag || qEnvironmentVariableIsSet("DBUS_STARTER_BUS_TYPE");
}

void DefaultFileManagerOffer::setContext(bool optedIn, bool dbusActivated, bool headless,
                                         bool mayClaimName)
{
    m_optedIn = optedIn;
    m_dbusActivated = dbusActivated;
    m_headless = headless;
    m_mayClaimName = mayClaimName;
}

void DefaultFileManagerOffer::start(bool optedIn, bool dbusActivated, bool mayClaimName)
{
    setContext(optedIn, dbusActivated, isHeadless(), mayClaimName);

    // A headless run never touches the user's session bus or files: it is the
    // test suite or the startup benchmark, and neither should find itself
    // owning "Show in folder" on the developer's desktop.
    if (!platform::defaultFileManagerSupported() || m_headless) {
        return;
    }

    const QPointer<DefaultFileManagerOffer> self(this);
    pool()->start([self] {
        const platform::DefaultFileManagerStatus status = platform::queryDefaultFileManager();
        QMetaObject::invokeMethod(
            self,
            [self, status] {
                if (!self.isNull()) {
                    self->applyStatus(status);
                }
            },
            Qt::QueuedConnection);
    });
}

void DefaultFileManagerOffer::applyStatus(const platform::DefaultFileManagerStatus &status)
{
    m_status = status;
    m_statusKnown = true;

    qCDebug(pfApp) << "default file manager:" << status.handlerDescription() << "from"
                   << status.handlerSource << "service file" << status.serviceFile
                   << (status.serviceFileExists ? "present" : "missing");

    if (status.isDefault()) {
        claimName();
    }
    showOfferIfWanted();
}

DefaultOfferConditions DefaultFileManagerOffer::conditions() const
{
    return {.supported = m_status.supported,
            .optedIn = m_optedIn,
            .alreadyDefault = m_status.isDefault(),
            .desktopEntryInstalled = m_status.panefileInstalled,
            .dbusActivated = m_dbusActivated,
            .headless = m_headless,
            .dismissedThisRun = m_dismissed};
}

void DefaultFileManagerOffer::showOfferIfWanted()
{
    if (!m_statusKnown || m_working || !shouldOffer(conditions())) {
        return;
    }
    if (m_bar != nullptr && (m_bar->isOffering() || m_bar->isShowingResult())) {
        return;
    }
    ensureBar()->showOffer();
}

void DefaultFileManagerOffer::setOptedIn(bool optedIn)
{
    if (m_optedIn == optedIn) {
        return;
    }
    m_optedIn = optedIn;
    if (!optedIn) {
        if (m_bar != nullptr && m_bar->isOffering()) {
            m_bar->hide();
        }
        return;
    }
    showOfferIfWanted();
}

void DefaultFileManagerOffer::setKeyHints(const QString &yes, const QString &notNow,
                                          const QString &never)
{
    m_yesHint = yes;
    m_notNowHint = notNow;
    m_neverHint = never;
    if (m_bar != nullptr) {
        m_bar->setKeyHints(yes, notNow, never);
    }
}

ui::DefaultFileManagerBar *DefaultFileManagerOffer::ensureBar()
{
    if (m_bar == nullptr) {
        // Constructed on first use, so a user who is never asked never pays
        // for the widget — and launch latency is unchanged for everyone.
        m_bar = new ui::DefaultFileManagerBar(m_window);
        m_bar->setKeyHints(m_yesHint, m_notNowHint, m_neverHint);
        if (m_window != nullptr) {
            m_window->setTopBar(m_bar);
        }
        connect(m_bar, &ui::DefaultFileManagerBar::accepted, this,
                &DefaultFileManagerOffer::accept);
        connect(m_bar, &ui::DefaultFileManagerBar::dismissed, this,
                &DefaultFileManagerOffer::notNow);
        connect(m_bar, &ui::DefaultFileManagerBar::declined, this, &DefaultFileManagerOffer::never);
    }
    return m_bar;
}

bool DefaultFileManagerOffer::isOffering() const
{
    return m_bar != nullptr && m_bar->isOffering();
}

ui::DefaultFileManagerBar *DefaultFileManagerOffer::bar() const
{
    return m_bar;
}

void DefaultFileManagerOffer::accept()
{
    if (!isOffering()) {
        return;
    }
    makeDefault();
}

void DefaultFileManagerOffer::notNow()
{
    // For this run only. Nothing is written, so the next launch asks again.
    m_dismissed = true;
    if (m_bar != nullptr) {
        m_bar->hide();
    }
}

void DefaultFileManagerOffer::never()
{
    m_dismissed = true;
    if (m_bar != nullptr) {
        m_bar->hide();
    }

    // Through TomlWriter, which edits the one assignment and leaves every
    // comment, blank line and hand-made alignment in the file as it was.
    // ConfigWatcher then reloads it like any other edit.
    const auto result = config::TomlWriter::setValue(
        platform::configDir() + QStringLiteral("/config.toml"), QStringLiteral("general"),
        QStringLiteral("offer_default_file_manager"), config::TomlWriter::boolean(false));
    if (!result.ok) {
        Q_EMIT statusMessage(tr("Could not save “never ask”: %1").arg(result.error));
        return;
    }
    m_optedIn = false;
    Q_EMIT statusMessage(tr("Panefile won't ask again. Settings can still make it the default."));
}

void DefaultFileManagerOffer::makeDefault()
{
    if (m_working || !platform::defaultFileManagerSupported()) {
        return;
    }
    m_working = true;

    if (m_bar != nullptr && !m_bar->isHidden()) {
        m_bar->showBusy(tr("Making Panefile your default file manager…"));
    }

    // Writing two files and asking the bus to reload is I/O, and none of it
    // belongs on the thread that paints.
    const QPointer<DefaultFileManagerOffer> self(this);
    pool()->start([self] {
        const platform::MakeDefaultResult result = platform::makePanefileDefaultFileManager();
        QMetaObject::invokeMethod(
            self,
            [self, result] {
                if (!self.isNull()) {
                    self->onMadeDefault(result);
                }
            },
            Qt::QueuedConnection);
    });
}

void DefaultFileManagerOffer::onMadeDefault(const platform::MakeDefaultResult &result)
{
    m_working = false;
    const bool barInUse = m_bar != nullptr && !m_bar->isHidden();

    if (!result.ok) {
        qCWarning(pfApp) << "could not become the default file manager:" << result.error;
        const QString message = tr("Couldn't make Panefile the default: %1").arg(result.error);
        if (barInUse) {
            m_bar->showResult(message, false);
        }
        Q_EMIT madeDefault(false, message);
        return;
    }

    qCInfo(pfApp) << "now the default file manager:" << result.mimeappsFile << "and"
                  << result.serviceFile;

    m_status.handlerId = QLatin1String(platform::kPanefileDesktopId);
    m_status.handlerSource = result.mimeappsFile;
    m_status.serviceFile = result.serviceFile;
    m_status.serviceFileExists = true;
    m_status.serviceExecutableExists = true;

    const QString message = tr("Panefile is now your default file manager");
    if (barInUse) {
        m_bar->showResult(message, true);
    }
    Q_EMIT madeDefault(true, message);

    m_reportClaim = barInUse;
    claimName();
}

void DefaultFileManagerOffer::claimName()
{
    // Only the instance that owns the single-instance socket answers "Show in
    // folder": a second, --new-instance window holding the name would take the
    // requests from the window the user thinks of as Panefile.
    if (m_service == nullptr || !m_mayClaimName || !m_service->isAvailable()) {
        return;
    }
    if (m_service->ownsName()) {
        m_reportClaim = false;
        return;
    }
    m_service->requestName();
}

int runMakeDefaultCommand(QTextStream &out, QTextStream &err)
{
    if (!platform::defaultFileManagerSupported()) {
        err << "pf: " << platform::makePanefileDefaultFileManager().error << '\n';
        return 1;
    }

    const platform::MakeDefaultResult result = platform::makePanefileDefaultFileManager();
    if (!result.ok) {
        err << "pf: couldn't make Panefile the default file manager: " << result.error << '\n';
        return 1;
    }

    out << "inode/directory → panefile.desktop in " << tildePath(result.mimeappsFile) << '\n'
        << "org.freedesktop.FileManager1 → " << tildePath(result.serviceFile) << '\n';

    const QString owner = platform::fileManagerNameOwner();
    if (!owner.isEmpty() && !owner.startsWith(QLatin1String("pf "))) {
        out << "\"Show in folder\" reaches Panefile once " << owner.section(QLatin1Char(' '), 0, 0)
            << " exits, or after your next login.\n";
    }
    return 0;
}

int runDefaultStatusCommand(QTextStream &out)
{
    if (!platform::defaultFileManagerSupported()) {
        out << "Finder is the file manager on macOS; Panefile can't replace it.\n";
        return 0;
    }

    const platform::DefaultFileManagerStatus status = platform::queryDefaultFileManager();

    out << "inode/directory:  "
        << (status.handlerId.isEmpty() ? QStringLiteral("not set") : status.handlerId);
    if (!status.handlerName.isEmpty()) {
        out << " (" << status.handlerName << ')';
    }
    out << '\n';
    if (!status.handlerSource.isEmpty()) {
        out << "  set in:         " << tildePath(status.handlerSource) << '\n';
    }

    out << "service file:     " << tildePath(status.serviceFile)
        << (status.serviceFileExists ? "" : " (missing)") << '\n';
    if (status.serviceFileExists) {
        out << "  runs:           "
            << (status.serviceExecutable.isEmpty() ? QStringLiteral("(no Exec=)")
                                                   : status.serviceExecutable)
            << (status.serviceExecutableExists ? "" : " (not a pf that exists)") << '\n';
    }

    const QString owner = platform::fileManagerNameOwner();
    out << "FileManager1:     " << (owner.isEmpty() ? QStringLiteral("no owner") : owner) << '\n';

    if (!status.panefileInstalled) {
        out << "panefile.desktop is not installed, so Panefile can't be the default.\n";
    }
    out << (status.isDefault() ? "Panefile is the default file manager.\n"
                               : "Panefile is not the default file manager.\n");
    return 0;
}

} // namespace pf
