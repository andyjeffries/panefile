// macOS has no default file manager to become: Finder cannot be replaced, and
// "Reveal in Finder" is not something another application can answer. Every
// entry point says so and does nothing.

#include "platform/DefaultFileManager.h"
#include "platform/FileManagerService.h"

#include <QCoreApplication>

namespace pf::platform {
namespace {

class UnavailableFileManagerService : public FileManagerService
{
public:
    explicit UnavailableFileManagerService(QObject *parent) : FileManagerService(parent) {}

    bool isAvailable() const override { return false; }
    bool ownsName() const override { return false; }
    void requestName() override {}
};

} // namespace

bool defaultFileManagerSupported()
{
    return false;
}

QString panefileExecutable()
{
    return QCoreApplication::applicationFilePath();
}

DefaultFileManagerStatus queryDefaultFileManager()
{
    return {};
}

MakeDefaultResult makePanefileDefaultFileManager()
{
    MakeDefaultResult result;
    result.error = QStringLiteral("Finder can't be replaced as the file manager on macOS");
    return result;
}

bool fileChooserSupported()
{
    return false;
}

QString FileChooserStatus::fallback() const
{
    return {};
}

QString FileChooserStatus::displayName(const QString &backend)
{
    return backend;
}

FileChooserStatus queryFileChooser()
{
    return {};
}

FileChooserSwitchResult makePanefileFileChooser()
{
    FileChooserSwitchResult result;
    result.error = QStringLiteral("Open and Save dialogs can't be replaced on macOS");
    return result;
}

int runFileChooserPortal()
{
    return 1;
}

FileChooserSwitchResult restorePanefileFileChooser()
{
    return makePanefileFileChooser();
}

QString fileManagerNameOwner()
{
    return {};
}

std::unique_ptr<FileManagerService> FileManagerService::create(QObject *parent)
{
    return std::make_unique<UnavailableFileManagerService>(parent);
}

} // namespace pf::platform
