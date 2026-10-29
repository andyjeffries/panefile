#include "fs/jobs/MediaJob.h"

#include "core/Format.h"
#include "fs/UniquePath.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace pf::fs {
namespace {

bool isVideo(MediaJob::Target target)
{
    return target == MediaJob::Target::VideoMp4 || target == MediaJob::Target::VideoWebm ||
           target == MediaJob::Target::VideoGif || target == MediaJob::Target::VideoAudioMp3;
}

} // namespace

MediaJob::MediaJob(Target target, QStringList paths, QString ffmpegPath, QObject *parent)
    : Job(parent), m_target(target), m_paths(std::move(paths)), m_ffmpeg(std::move(ffmpegPath))
{}

QString MediaJob::extensionFor(Target target)
{
    switch (target) {
    case Target::VideoMp4:
        return QStringLiteral("mp4");
    case Target::VideoWebm:
        return QStringLiteral("webm");
    case Target::VideoGif:
        return QStringLiteral("gif");
    case Target::VideoAudioMp3:
    case Target::AudioMp3:
        return QStringLiteral("mp3");
    case Target::AudioFlac:
        return QStringLiteral("flac");
    case Target::AudioWav:
        return QStringLiteral("wav");
    case Target::AudioOgg:
        return QStringLiteral("ogg");
    }
    return {};
}

QString MediaJob::labelFor(Target target)
{
    return extensionFor(target).toUpper();
}

QStringList MediaJob::buildArguments(Target target, const QString &source,
                                     const QString &destination)
{
    QStringList codec;
    switch (target) {
    case Target::VideoMp4:
        codec = {"-c:v", "libx264", "-crf", "23",   "-preset",   "medium",
                 "-c:a", "aac",     "-b:a", "160k", "-movflags", "+faststart"};
        break;
    case Target::VideoWebm:
        codec = {"-c:v", "libvpx-vp9", "-crf", "32", "-b:v", "0", "-c:a", "libopus"};
        break;
    case Target::VideoGif:
        codec = {"-vf", "fps=12,scale=480:-1:flags=lanczos"};
        break;
    case Target::VideoAudioMp3:
        codec = {"-vn", "-c:a", "libmp3lame", "-q:a", "2"};
        break;
    case Target::AudioMp3:
        codec = {"-c:a", "libmp3lame", "-q:a", "2"};
        break;
    case Target::AudioFlac:
        codec = {"-c:a", "flac"};
        break;
    case Target::AudioWav:
        codec = {"-c:a", "pcm_s16le"};
        break;
    case Target::AudioOgg:
        codec = {"-c:a", "libvorbis", "-q:a", "5"};
        break;
    }

    QStringList arguments{"-hide_banner",
                          "-nostdin",
                          "-loglevel",
                          "error",
                          "-y",
                          "-i",
                          QStringLiteral("file:") + source};
    arguments += codec;
    arguments += {"-progress", "pipe:1", QStringLiteral("file:") + destination};
    return arguments;
}

double MediaJob::parseDuration(const QString &output)
{
    static const QRegularExpression pattern(
        QStringLiteral("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)"));
    const QRegularExpressionMatch match = pattern.match(output);
    if (!match.hasMatch()) {
        return -1;
    }
    return match.captured(1).toInt() * 3600.0 + match.captured(2).toInt() * 60.0 +
           match.captured(3).toDouble();
}

QString MediaJob::partialPathFor(const QString &destination)
{
    const QFileInfo info(destination);
    return info.absolutePath() + QStringLiteral("/.") + info.completeBaseName() +
           QStringLiteral(".pf-partial.") + info.suffix();
}

QString MediaJob::description() const
{
    const int count = m_filesTotal > 0 ? m_filesTotal : static_cast<int>(m_paths.size());
    if (m_target == Target::VideoAudioMp3) {
        return tr("Extracting audio from %1").arg(counted(count, tr("video"), tr("videos")));
    }
    return tr("Converting %1 to %2")
        .arg(isVideo(m_target) ? counted(count, tr("video"), tr("videos"))
                               : counted(count, tr("audio file"), tr("audio files")),
             labelFor(m_target));
}

QStringList MediaJob::createdPaths() const
{
    return m_created;
}

bool MediaJob::enumerate()
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

double MediaJob::probeDuration(const QString &path) const
{
    // ffmpeg with an input and no output prints the input's description to
    // stderr, duration included, and exits non-zero. Cheap, and the only way to
    // turn "out_time" into a fraction.
    QProcess probe;
    probe.start(m_ffmpeg, {"-hide_banner", "-nostdin", "-i", QStringLiteral("file:") + path});
    if (!probe.waitForFinished(5000)) {
        probe.kill();
        probe.waitForFinished();
        return -1;
    }
    return parseDuration(QString::fromUtf8(probe.readAllStandardError()));
}

void MediaJob::execute()
{
    for (const QString &path : std::as_const(m_paths)) {
        if (isCancelled()) {
            return;
        }
        if (!QFileInfo(path).isFile()) {
            continue;
        }
        convertOne(path);
        m_bytesDone += static_cast<quint64>(QFileInfo(path).size());
        ++m_filesDone;
        reportProgress(path);
    }
}

void MediaJob::convertOne(const QString &path)
{
    const QFileInfo source(path);
    QString destination = source.absolutePath() + QLatin1Char('/') + source.completeBaseName() +
                          QLatin1Char('.') + extensionFor(m_target);

    if (QFileInfo(destination) == source) {
        addError(path, tr("Already in that format"));
        return;
    }

    // Resolved here, before ffmpeg runs: `-y` is only safe because of this.
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

    const QString partial = partialPathFor(destination);
    QFile::remove(partial);

    const double duration = probeDuration(path);
    const quint64 size = static_cast<quint64>(source.size());
    const quint64 before = m_bytesDone;

    // Created here, on the worker thread: a QProcess belongs to the thread that
    // makes it, and waitForFinished() runs its own I/O without an event loop.
    QProcess process;
    process.start(m_ffmpeg, buildArguments(m_target, path, partial));
    if (!process.waitForStarted(5000)) {
        addError(path, tr("Could not start ffmpeg"));
        return;
    }

    QString lastError;
    QByteArray pending;
    while (!process.waitForFinished(100)) {
        if (isCancelled()) {
            process.kill();
            process.waitForFinished();
            QFile::remove(partial);
            return;
        }

        pending += process.readAllStandardOutput();
        while (true) {
            const qsizetype newline = pending.indexOf('\n');
            if (newline < 0) {
                break;
            }
            const QByteArray line = pending.left(newline).trimmed();
            pending.remove(0, newline + 1);
            if (duration > 0 && line.startsWith("out_time_us=")) {
                const double seconds = line.mid(12).toDouble() / 1e6;
                const double fraction = qBound(0.0, seconds / duration, 1.0);
                m_bytesDone = before + static_cast<quint64>(fraction * static_cast<double>(size));
                reportProgress(path);
            }
        }
        const QString errors = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (!errors.isEmpty()) {
            lastError = errors.section(QLatin1Char('\n'), -1);
        }
    }
    m_bytesDone = before;

    const QString errors = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (!errors.isEmpty()) {
        lastError = errors.section(QLatin1Char('\n'), -1);
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QFile::remove(partial);
        addError(path, lastError.isEmpty() ? tr("ffmpeg failed") : lastError);
        return;
    }

    // `-y` overwrote nothing: the destination was settled above. Replacing an
    // existing file here is the "overwrite" the user chose.
    // A link is removed too: QFile::rename falls back to a copy that would
    // write through it to a path outside this folder.
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) {
        QFile::remove(destination);
    }
    if (!QFile::rename(partial, destination)) {
        QFile::remove(partial);
        addError(path, tr("Could not write %1").arg(QFileInfo(destination).fileName()));
        return;
    }
    m_created.append(destination);
}

} // namespace pf::fs
