#pragma once

#include "fs/Job.h"

namespace pf::fs {

/// Converts video and audio with the `ffmpeg` command (§7.14).
///
/// ffmpeg is a subprocess, never a library: it is large, optional, and its
/// licensing is the user's business. Arguments are passed as a list — there is
/// no shell — and file names carry a `file:` prefix, so a name that starts with
/// `-` or looks like a URL is read as a file name and nothing else.
///
/// Output goes to a hidden sibling file and is renamed into place when ffmpeg
/// succeeds, so a failed or cancelled conversion leaves nothing behind.
class MediaJob : public Job
{
    Q_OBJECT

public:
    enum class Target {
        VideoMp4,
        VideoWebm,
        VideoGif,
        VideoAudioMp3, ///< the audio track of a video, as MP3
        AudioMp3,
        AudioFlac,
        AudioWav,
        AudioOgg,
    };

    MediaJob(Target target, QStringList paths, QString ffmpegPath, QObject *parent = nullptr);

    static QString extensionFor(Target target);
    static QString labelFor(Target target);

    /// The whole ffmpeg command line for one file. Pure, so it is tested
    /// without ffmpeg.
    static QStringList buildArguments(Target target, const QString &source,
                                      const QString &destination);

    /// Seconds from ffmpeg's "Duration: 00:01:02.50" line, or -1.
    static double parseDuration(const QString &ffmpegOutput);

    /// The hidden file ffmpeg writes to before it is renamed into place.
    static QString partialPathFor(const QString &destination);

    QString description() const override;

    QStringList createdPaths() const;

protected:
    bool enumerate() override;
    void execute() override;

private:
    void convertOne(const QString &path);
    double probeDuration(const QString &path) const;

    Target m_target;
    QStringList m_paths;
    QString m_ffmpeg;
    QStringList m_created;
};

} // namespace pf::fs
