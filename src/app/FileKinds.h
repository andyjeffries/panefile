#pragma once

#include <QString>
#include <QStringList>

namespace pf {

/// What kind of file a path is, for the actions that depend on it (§7.14).
enum class FileKind { Other, Image, Video, Audio, Archive };

/// By extension only (QMimeDatabase::MatchExtension): cheap enough to ask of
/// every selected file each time the command palette opens, and never reads the
/// file. An image counts only when Qt can read its format.
FileKind kindOf(const QString &path);

/// True for a non-empty list in which every path is a regular file of `kind`.
bool allOfKind(const QStringList &paths, FileKind kind);

/// The kind's name for the palette's heading: "Image", "Video"…
QString kindLabel(FileKind kind);

/// The `ffmpeg` executable, or empty when it is not installed. Looked up once
/// and remembered (§3.4: not at startup, only when something asks).
///
/// `PANEFILE_FFMPEG` overrides the search — to a path, or to nothing at all to
/// say "not installed" — which is how the tests exercise both cases.
QString ffmpegPath();

} // namespace pf
