#!/usr/bin/env bash
#
# Enforces the cold-start budget of §11 against a committed baseline.
#
# §16 requires this at every milestone, not just at the end: "If a milestone
# pushes cold start past §11's target, the fix is to make that milestone's work
# lazy before moving on — never to defer the problem."
#
# Usage: check-startup-budget.sh <path-to-pf> [baseline-file] [fixture-dir]
#
# With no baseline the measurement is printed and the absolute target is
# checked, which is how a new baseline is recorded.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

binary="${1:?usage: check-startup-budget.sh <path-to-pf> [baseline-file] [fixture-dir]}"
baseline="${2:-}"
fixture="${3:-}"

# §3.4 asks for "a fixed-size fixture directory", and it matters more than it
# looks: benchmarking against $HOME measures a different directory on every
# machine, and a different one on the same machine week to week. Two consecutive
# runs against $HOME differed by 25% here — wider than the regression threshold
# this check exists to enforce, which would have made it useless in both
# directions.
if [[ -z "$fixture" ]]; then
    fixture="$("$here/make-fixture.sh" "$here/../build/benchmark-fixture")"
fi

# §11: cold start to first painted window.
target_ms=80
runs=20
warmups=3
# A regression larger than this fraction of the baseline fails the build.
regression_tolerance=1.15

# The metric is time to first paint, read from the binary's own trace, rather
# than the process lifetime hyperfine would report. They stopped being the same
# number once a directory scan was running at exit: process lifetime then
# includes waiting for the scanner thread to notice it has been cancelled, which
# is teardown cost and has nothing to do with how quickly a window appears.
# §11's criterion is "cold start to first painted window", and the trace
# measures exactly that, from a real paintEvent.
# QT_QPA_PLATFORM=offscreen matters for more than tidiness: this launches the
# real application twenty-three times to take a median, and without it that is
# twenty-three windows opening and closing over whatever the developer is doing.
# A window that reappears the moment it is closed looks like a bug in Panefile
# rather than a benchmark in progress. Offscreen still performs a real
# paintEvent, which is what is being measured.
measure_once() {
    QT_QPA_PLATFORM=offscreen "$binary" --startup-trace --quit-after-paint "$fixture" 2>&1 \
        | awk '/first-paint/ { print $3 }'
}

for ((i = 0; i < warmups; ++i)); do
    measure_once >/dev/null 2>&1 || true
done

samples=()
for ((i = 0; i < runs; ++i)); do
    sample="$(measure_once || true)"
    if [[ -n "$sample" ]]; then
        samples+=("$sample")
    fi
done

if (( ${#samples[@]} == 0 )); then
    echo "check-startup-budget: the binary produced no first-paint measurement." >&2
    echo "Run it by hand to see why:" >&2
    echo "  $binary --startup-trace --quit-after-paint $fixture" >&2
    exit 1
fi

# The median, not the mean: a single scheduling hiccup in twenty runs should not
# move the number the build is gated on.
read -r median spread < <(printf '%s\n' "${samples[@]}" | python3 -c "
import statistics, sys
values = sorted(float(line) for line in sys.stdin if line.strip())
print(f'{statistics.median(values):.1f} {values[0]:.1f}-{values[-1]:.1f}')
")

echo "check-startup-budget: first paint ${median} ms (range ${spread}, n=${#samples[@]}, target ${target_ms} ms)"

status=0

if [[ -n "$baseline" && -f "$baseline" ]]; then
    previous="$(grep -vE '^\s*(#|$)' "$baseline" | head -1)"
    limit="$(python3 -c "print(f'{$previous * $regression_tolerance:.1f}')")"
    if (( $(python3 -c "print(1 if $median > $limit else 0)") )); then
        echo "check-startup-budget: regression — ${median} ms exceeds ${limit} ms" >&2
        echo "(baseline ${previous} ms plus a ${regression_tolerance}x tolerance)" >&2
        echo "Make the most recent milestone's startup work lazy rather than" >&2
        echo "deferring the problem (§16)." >&2
        status=1
    else
        echo "check-startup-budget: within tolerance of the ${previous} ms baseline"
    fi
fi

# The absolute target is a Linux acceptance criterion. On macOS the floor is set
# by NSApplication, the Cocoa plugin and Qt's font database rather than by
# anything Panefile does — see docs/startup-budget.md.
#
# It is reported everywhere but enforced only when asked for, because the
# machine matters: 80 ms is a claim about a real desktop, and a shared,
# containerised CI runner with cold caches cannot falsify it. Enforcing it there
# would produce red builds that say nothing about the code. The regression check
# above is the one that belongs in CI — it compares like with like.
if [[ "$(uname -s)" == "Linux" ]]; then
    if (( $(python3 -c "print(1 if $median > $target_ms else 0)") )); then
        if [[ "${PF_STARTUP_BUDGET_STRICT:-0}" == "1" ]]; then
            echo "check-startup-budget: over the ${target_ms} ms budget of §11" >&2
            status=1
        else
            echo "check-startup-budget: over the ${target_ms} ms target of §11." \
                 "Not failing the build — set PF_STARTUP_BUDGET_STRICT=1 to enforce" \
                 "on hardware where the number is meaningful."
        fi
    fi
fi

exit $status
