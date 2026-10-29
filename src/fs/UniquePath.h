#pragma once

#include <QFileInfo>
#include <QString>

namespace pf::fs {

/// Appends " (2)", " (3)" … until the name is free (§7.4's rename resolution).
/// The counter goes before the extension, not after it: "notes (2).txt" stays a
/// text file, "notes.txt (2)" does not. Empty when nothing under 10000 is free.
inline QString uniquePath(const QString &path)
{
    const QFileInfo info(path);
    const QString directory = info.absolutePath();
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int counter = 2; counter < 10000; ++counter) {
        const QString candidate =
            suffix.isEmpty()
                ? QStringLiteral("%1/%2 (%3)").arg(directory, base).arg(counter)
                : QStringLiteral("%1/%2 (%3).%4").arg(directory, base).arg(counter).arg(suffix);
        if (!QFileInfo::exists(candidate) && !QFileInfo(candidate).isSymLink()) {
            return candidate;
        }
    }
    return {};
}

} // namespace pf::fs
