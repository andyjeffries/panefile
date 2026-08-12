#pragma once

#include <QtGlobal>

class QString;

namespace pf {

/// Phases of the startup critical path described in §3.4, in the order they
/// are expected to occur. Recorded unconditionally (a clock read is a few
/// nanoseconds); only the reporting is gated on --startup-trace.
enum class StartupPhase {
    ProcessStart, ///< first statement of main()
    ArgvParsed,
    ConfigLoaded,      ///< config.toml and theme.toml read and parsed
    StylesheetApplied, ///< QSS set on the application, before any widget exists
    WindowConstructed,
    ScanStarted,    ///< directory scan dispatched for the initial path
    Shown,          ///< show() returned
    FirstPaint,     ///< paintEvent on the panel, not show() returning
    ScanFirstBatch, ///< first batch of entries reached the model
    Count
};

/// Records monotonic timestamps for the startup phases.
///
/// Deliberately free of allocation, locking and static construction: it is
/// touched on the hottest path in the program, and §3.4 makes that path an
/// acceptance criterion rather than a nice-to-have.
class StartupTrace
{
public:
    /// Marks a phase as reached. Recording a phase twice keeps the first
    /// timestamp, so a paintEvent firing repeatedly does not skew FirstPaint.
    static void mark(StartupPhase phase);

    static bool hasReached(StartupPhase phase);

    /// Nanoseconds from ProcessStart to `phase`, or -1 if not reached.
    static qint64 nanosSinceStart(StartupPhase phase);

    /// Enables the human-readable report emitted by dump().
    static void setReportingEnabled(bool enabled);
    static bool isReportingEnabled();

    /// Writes one line per reached phase to stderr. No-op unless reporting is
    /// enabled. Safe to call more than once.
    static void dump();

    static const char *phaseName(StartupPhase phase);

    /// Formats the same report as dump() writes. Exposed for tests.
    static QString formatReport();
};

} // namespace pf
