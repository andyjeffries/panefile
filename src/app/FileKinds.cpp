#include "app/FileKinds.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QStandardPaths>

#include <algorithm>
#include <array>

namespace pf {
namespace {

bool isArchiveMime(const QString &name)
{
    static constexpr std::array<QLatin1String, 16> kTypes{
        QLatin1String("application/zip"),
        QLatin1String("application/x-tar"),
        QLatin1String("application/gzip"),
        QLatin1String("application/x-bzip2"),
        QLatin1String("application/x-xz"),
        QLatin1String("application/zstd"),
        QLatin1String("application/x-7z-compressed"),
        QLatin1String("application/vnd.rar"),
        QLatin1String("application/x-rar-compressed"),
        QLatin1String("application/x-compressed-tar"),
        QLatin1String("application/x-bzip-compressed-tar"),
        QLatin1String("application/x-xz-compressed-tar"),
        QLatin1String("application/x-zstd-compressed-tar"),
        QLatin1String("application/x-cpio"),
        QLatin1String("application/x-archive"),
        QLatin1String("application/java-archive")};
    return std::ranges::any_of(kTypes, [&name](QLatin1String type) { return name == type; });
}

} // namespace

FileKind kindOf(const QString &path)
{
    const QString mime =
        QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();

    if (mime.startsWith(QLatin1String("image/"))) {
        // Qt's own list: an image type nothing here can decode is not one the
        // image actions can act on.
        static const QList<QByteArray> supported = QImageReader::supportedMimeTypes();
        return supported.contains(mime.toLatin1()) ? FileKind::Image : FileKind::Other;
    }
    if (mime.startsWith(QLatin1String("video/"))) {
        return FileKind::Video;
    }
    if (mime.startsWith(QLatin1String("audio/"))) {
        return FileKind::Audio;
    }
    if (isArchiveMime(mime)) {
        return FileKind::Archive;
    }
    return FileKind::Other;
}

bool allOfKind(const QStringList &paths, FileKind kind)
{
    if (paths.isEmpty()) {
        return false;
    }
    return std::ranges::all_of(paths, [kind](const QString &path) {
        return QFileInfo(path).isFile() && kindOf(path) == kind;
    });
}

QString kindLabel(FileKind kind)
{
    switch (kind) {
    case FileKind::Image:
        return QCoreApplication::translate("FileKinds", "Image");
    case FileKind::Video:
        return QCoreApplication::translate("FileKinds", "Video");
    case FileKind::Audio:
        return QCoreApplication::translate("FileKinds", "Audio");
    case FileKind::Archive:
        return QCoreApplication::translate("FileKinds", "Archive");
    case FileKind::Other:
        break;
    }
    return {};
}

QString ffmpegPath()
{
    if (qEnvironmentVariableIsSet("PANEFILE_FFMPEG")) {
        return qEnvironmentVariable("PANEFILE_FFMPEG");
    }
    static const QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    return found;
}

} // namespace pf
