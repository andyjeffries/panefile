#pragma once

#include <QString>

#include <sys/stat.h>
#include <sys/types.h>

class QDateTime;

namespace pf {

/// Human-readable file size, e.g. "4.2 KiB".
///
/// Binary units, because a file manager reports what the filesystem reports and
/// every other tool a user will cross-check against (ls -lh, du -h, stat) uses
/// the same. Precision drops as magnitude rises: bytes are exact, and above
/// that one decimal place is as much as anyone reads.
QString formatSize(quint64 bytes);

/// Compact modification time for a list row.
///
/// Today's files show a time, this year's show a day and month, older ones show
/// the year. The column is scanned, not read, so it optimises for telling
/// entries apart at a glance rather than for completeness — the footer shows
/// the full timestamp.
QString formatListTime(const QDateTime &when);

/// Full timestamp for the footer, e.g. "2026-08-11 14:02".
QString formatFullTime(const QDateTime &when);

/// Unix permission string, e.g. "-rw-r--r--" or "drwxr-xr-x".
QString formatPermissions(mode_t mode);

} // namespace pf
