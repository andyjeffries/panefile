#include "core/StartupTrace.h"

#include <QString>
#include <QTextStream>

#include <array>
#include <chrono>

namespace pf {
namespace {

constexpr int kPhaseCount = static_cast<int>(StartupPhase::Count);

/// -1 marks a phase as not reached, which has to be distinguishable from a
/// phase that was reached at time zero.
constexpr std::array<qint64, kPhaseCount> makeUnreachedStamps()
{
    std::array<qint64, kPhaseCount> stamps{};
    stamps.fill(-1);
    return stamps;
}

struct TraceState {
    std::array<qint64, kPhaseCount> stamps = makeUnreachedStamps();
    bool reportingEnabled = false;
};

// Function-local static: constructed on first mark(), never at load time (§3.4).
TraceState &state()
{
    static TraceState s;
    return s;
}

qint64 nowNanos()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

void StartupTrace::mark(StartupPhase phase)
{
    const int index = static_cast<int>(phase);
    if (index < 0 || index >= kPhaseCount) {
        return;
    }
    auto &stamps = state().stamps;
    if (stamps[index] < 0) {
        stamps[index] = nowNanos();
    }
}

bool StartupTrace::hasReached(StartupPhase phase)
{
    const int index = static_cast<int>(phase);
    return index >= 0 && index < kPhaseCount && state().stamps[index] >= 0;
}

qint64 StartupTrace::nanosSinceStart(StartupPhase phase)
{
    const int index = static_cast<int>(phase);
    if (index < 0 || index >= kPhaseCount) {
        return -1;
    }
    const auto &stamps = state().stamps;
    const qint64 start = stamps[static_cast<int>(StartupPhase::ProcessStart)];
    if (start < 0 || stamps[index] < 0) {
        return -1;
    }
    return stamps[index] - start;
}

void StartupTrace::setReportingEnabled(bool enabled)
{
    state().reportingEnabled = enabled;
}

bool StartupTrace::isReportingEnabled()
{
    return state().reportingEnabled;
}

const char *StartupTrace::phaseName(StartupPhase phase)
{
    switch (phase) {
    case StartupPhase::ProcessStart:
        return "process-start";
    case StartupPhase::ArgvParsed:
        return "argv";
    case StartupPhase::ConfigLoaded:
        return "config";
    case StartupPhase::StylesheetApplied:
        return "stylesheet";
    case StartupPhase::WindowConstructed:
        return "window";
    case StartupPhase::ScanStarted:
        return "scan-start";
    case StartupPhase::Shown:
        return "shown";
    case StartupPhase::FirstPaint:
        return "first-paint";
    case StartupPhase::ScanFirstBatch:
        return "scan-first-batch";
    case StartupPhase::Count:
        break;
    }
    return "unknown";
}

QString StartupTrace::formatReport()
{
    QString report;
    QTextStream out(&report);

    qint64 previous = 0;
    for (int i = 0; i < kPhaseCount; ++i) {
        const auto phase = static_cast<StartupPhase>(i);
        const qint64 elapsed = nanosSinceStart(phase);
        if (elapsed < 0) {
            continue;
        }
        const double totalMs = static_cast<double>(elapsed) / 1e6;
        const double deltaMs = static_cast<double>(elapsed - previous) / 1e6;
        previous = elapsed;

        out << "startup " << qSetFieldWidth(18) << Qt::left << phaseName(phase) << qSetFieldWidth(0)
            << Qt::right << ' ' << qSetRealNumberPrecision(3) << Qt::fixed << qSetFieldWidth(9)
            << totalMs << qSetFieldWidth(0) << " ms  (+" << qSetFieldWidth(7) << deltaMs
            << qSetFieldWidth(0) << " ms)\n";
    }
    return report;
}

void StartupTrace::dump()
{
    if (!isReportingEnabled()) {
        return;
    }
    QTextStream err(stderr);
    err << formatReport();
    err.flush();
}

} // namespace pf
