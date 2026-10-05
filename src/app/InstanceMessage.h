#pragma once

#include "app/CommandLine.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace pf {

/// One panel's worth of a request: a directory to open, and the entries in it
/// to put the cursor on and, for "Show in folder", to select.
struct FolderRequest {
    QString directory;
    QStringList names;

    bool operator==(const FolderRequest &other) const = default;
};

/// The IPC payload of §10.3.
///
/// "Message format is a single JSON object: `{ cwd, paths[], flags{},
/// activation_token, desktop_startup_id }`. Version it with a `v` field so a
/// future change doesn't break against an older running instance — on version
/// mismatch, the client starts its own instance rather than sending something
/// the server might misread."
///
/// Encoding and decoding are pure functions of the struct and the bytes, so
/// §14 can check the round trip and — more importantly — the version rejection,
/// without a socket.
struct InstanceMessage {
    /// Bumped whenever the meaning of a field changes. An older running
    /// instance seeing a higher version, or a newer one seeing a lower, refuses
    /// rather than guessing.
    static constexpr int kVersion = 1;

    int version = kVersion;

    /// The *client's* working directory. §10.1: relative paths resolve against
    /// this, not against the running instance's, which may be anywhere.
    QString cwd;

    QStringList paths;

    PlacementOverride placement = PlacementOverride::None;

    /// "Show in folder": open each path's *parent* with the path selected,
    /// rather than opening the path. Set by org.freedesktop.FileManager1's
    /// ShowItems; never by the command line.
    ///
    /// A new key inside `flags`, which an older instance ignores rather than
    /// misreads — so it does not need a version bump.
    bool selectItems = false;

    /// §10.4: "The launching process usually has XDG_ACTIVATION_TOKEN in its
    /// environment… The client must forward it in the IPC message and then
    /// unset it locally, since a token is single-use."
    QString activationToken;

    /// The X11 equivalent, forwarded for the same reason.
    QString desktopStartupId;

    QByteArray toJson() const;

    /// Decodes a message. Returns false when the bytes are not valid JSON or
    /// carry a version this build does not speak.
    static bool fromJson(const QByteArray &bytes, InstanceMessage *out);

    /// Resolves `paths` against `cwd` into absolute paths, decoding `file://`
    /// URIs. Pure, so §10.2's resolution rules are testable.
    QStringList absolutePaths() const;

    /// The panels a request opens, in order, from absolute paths.
    ///
    /// Without `selectItems`, §10.2's rule: one panel per path, a directory
    /// opened as itself and a file as its parent with the cursor on it. With
    /// it, every path is shown in its parent, and paths sharing a parent share
    /// a panel — revealing three downloads is one folder with three files
    /// selected, not three copies of the same folder.
    static QList<FolderRequest> folderRequests(const QStringList &absolutePaths, bool selectItems);
};

} // namespace pf
