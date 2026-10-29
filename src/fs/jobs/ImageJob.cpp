#include "fs/jobs/ImageJob.h"

#include "core/Format.h"
#include "fs/UniquePath.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QTransform>

namespace pf::fs {
namespace {

/// JPEG and most edits are written at this quality; Qt's default of 75 would
/// visibly degrade a photograph that is only being turned the right way up.
constexpr int kJpegQuality = 95;

QImage flattenOnWhite(const QImage &image)
{
    QImage flat(image.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter painter(&flat);
    painter.drawImage(0, 0, image);
    return flat;
}

} // namespace

ImageJob::ImageJob(Op op, QStringList paths, QObject *parent)
    : Job(parent), m_op(op), m_paths(std::move(paths))
{}

void ImageJob::setTrash(Trash trash)
{
    m_trash = std::move(trash);
}

bool ImageJob::isConversion(Op op)
{
    return op == Op::ConvertPng || op == Op::ConvertJpeg || op == Op::ConvertWebp ||
           op == Op::ConvertAvif;
}

QString ImageJob::extensionFor(Op op)
{
    switch (op) {
    case Op::ConvertPng:
        return QStringLiteral("png");
    case Op::ConvertJpeg:
        return QStringLiteral("jpg");
    case Op::ConvertWebp:
        return QStringLiteral("webp");
    case Op::ConvertAvif:
        return QStringLiteral("avif");
    default:
        return {};
    }
}

QByteArray ImageJob::formatFor(Op op)
{
    switch (op) {
    case Op::ConvertPng:
        return "png";
    case Op::ConvertJpeg:
        return "jpeg";
    case Op::ConvertWebp:
        return "webp";
    case Op::ConvertAvif:
        return "avif";
    default:
        return {};
    }
}

QString ImageJob::description() const
{
    const int count = m_filesTotal > 0 ? m_filesTotal : static_cast<int>(m_paths.size());
    const QString what = counted(count, tr("image"), tr("images"));
    switch (m_op) {
    case Op::ConvertPng:
    case Op::ConvertJpeg:
    case Op::ConvertWebp:
    case Op::ConvertAvif:
        return tr("Converting %1 to %2").arg(what, extensionFor(m_op).toUpper());
    case Op::RotateLeft:
    case Op::RotateRight:
        return tr("Rotating %1").arg(what);
    case Op::FlipHorizontal:
    case Op::FlipVertical:
        return tr("Flipping %1").arg(what);
    }
    return {};
}

QStringList ImageJob::createdPaths() const
{
    return m_created;
}

QList<QPair<QString, TrashedItem>> ImageJob::replacedItems() const
{
    return m_replaced;
}

bool ImageJob::enumerate()
{
    for (const QString &path : std::as_const(m_paths)) {
        const QFileInfo info(path);
        if (!info.isFile()) {
            addError(path, tr("Not a file"));
            continue;
        }
        ++m_filesTotal;
        m_bytesTotal += static_cast<quint64>(info.size());
    }
    return m_filesTotal > 0;
}

void ImageJob::execute()
{
    for (const QString &path : std::as_const(m_paths)) {
        if (isCancelled()) {
            return;
        }
        if (!QFileInfo(path).isFile()) {
            continue;
        }
        if (isConversion(m_op)) {
            convertOne(path);
        } else {
            transformOne(path);
        }
        m_bytesDone += static_cast<quint64>(QFileInfo(path).size());
        ++m_filesDone;
        reportProgress(path);
    }
}

void ImageJob::convertOne(const QString &path)
{
    const QFileInfo source(path);
    QString destination = source.absolutePath() + QLatin1Char('/') + source.completeBaseName() +
                          QLatin1Char('.') + extensionFor(m_op);

    if (QFileInfo(destination) == source) {
        addError(path, tr("Already in that format"));
        return;
    }

    // A dangling symlink counts as taken: exists() follows it and says no.
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) {
        const QFileInfo target(destination);
        const ConflictResolution choice =
            askAboutConflict(path, destination,
                             ConflictInfo{.sourceSize = static_cast<quint64>(source.size()),
                                          .destinationSize = static_cast<quint64>(target.size()),
                                          .sourceModified = source.lastModified(),
                                          .destinationModified = target.lastModified(),
                                          .destinationIsDirectory = target.isDir()});
        switch (choice.action) {
        case ConflictAction::Cancel:
            cancel();
            return;
        case ConflictAction::Skip:
            return;
        case ConflictAction::Rename:
            destination = uniquePath(destination);
            if (destination.isEmpty()) {
                addError(path, tr("No free name for the converted file"));
                return;
            }
            break;
        case ConflictAction::OverwriteIfNewer:
            if (target.lastModified() >= source.lastModified()) {
                return;
            }
            break;
        case ConflictAction::Overwrite:
            break;
        }
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        addError(path, reader.errorString());
        return;
    }
    if (m_op == Op::ConvertJpeg && image.hasAlphaChannel()) {
        image = flattenOnWhite(image);
    }

    // QSaveFile commits through a symlink to its target, which would write
    // outside this folder. Replace the link itself instead.
    if (QFileInfo(destination).isSymLink() && !QFile::remove(destination)) {
        addError(path, tr("Cannot replace %1").arg(destination));
        return;
    }

    // QSaveFile, so a failed or cancelled write leaves any file already at the
    // destination untouched rather than half-written.
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly)) {
        addError(path, file.errorString());
        return;
    }
    QImageWriter writer(&file, formatFor(m_op));
    if (m_op == Op::ConvertJpeg) {
        writer.setQuality(kJpegQuality);
    }
    if (!writer.write(image)) {
        file.cancelWriting();
        addError(path, writer.errorString());
        return;
    }
    if (!file.commit()) {
        addError(path, file.errorString());
        return;
    }
    m_created.append(destination);
}

void ImageJob::transformOne(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format();
    QImage image = reader.read();
    if (image.isNull()) {
        addError(path, reader.errorString());
        return;
    }
    if (format.isEmpty() || !QImageWriter::supportedImageFormats().contains(format)) {
        addError(path, tr("Cannot write %1 images").arg(QString::fromLatin1(format)));
        return;
    }

    switch (m_op) {
    case Op::RotateLeft:
        image = image.transformed(QTransform().rotate(-90));
        break;
    case Op::RotateRight:
        image = image.transformed(QTransform().rotate(90));
        break;
    case Op::FlipHorizontal:
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        image = image.flipped(Qt::Horizontal);
#else
        image = image.mirrored(true, false);
#endif
        break;
    case Op::FlipVertical:
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        image = image.flipped(Qt::Vertical);
#else
        image = image.mirrored(false, true);
#endif
        break;
    default:
        return;
    }

    // Written beside the original but not yet in place.
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        addError(path, file.errorString());
        return;
    }
    const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
    QImageWriter writer(&file, format);
    if (format == "jpeg" || format == "jpg") {
        writer.setQuality(kJpegQuality);
    }
    if (!writer.write(image)) {
        file.cancelWriting();
        addError(path, writer.errorString());
        return;
    }
    file.setPermissions(permissions);

    // The original goes to the trash before the new file takes its name. If
    // that fails nothing has changed; if the commit fails the original is put
    // back.
    QString error;
    const QString trashed = m_trash.moveToTrash(path, &error);
    if (trashed.isEmpty()) {
        file.cancelWriting();
        addError(path, error);
        return;
    }
    const TrashedItem item{.trashedPath = trashed,
                           .originalPath = QFileInfo(path).absoluteFilePath(),
                           .deletedAt = QDateTime::currentDateTime(),
                           .size = 0,
                           .isDirectory = false};
    if (!file.commit()) {
        m_trash.restore(item, &error);
        addError(path, file.errorString());
        return;
    }
    m_replaced.append({path, item});
}

} // namespace pf::fs
