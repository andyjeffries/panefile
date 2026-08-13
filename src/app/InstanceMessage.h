#pragma once

#include "app/CommandLine.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace pf {

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
};

} // namespace pf
