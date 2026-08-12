# Startup budget

§11 makes cold start an acceptance criterion, and §16 requires it to be checked
at **every** milestone rather than deferred to M10. This file records the
baselines those checks run against, and — more usefully — what each number is
made of, so that a regression can be attributed rather than merely detected.

## Targets (§11)

| Metric | Target |
| --- | --- |
| Cold start to first painted window | < 80 ms |
| Cold start to first directory listing visible | < 120 ms |
| Warm start (page cache hot) | < 50 ms |
| Forwarding a path to a running instance (client process lifetime) | < 10 ms |

**These are Linux targets.** They are the platform the application is designed
for, and they are what CI enforces. macOS is measured too, but against its own
recorded baseline rather than against these numbers — see below for why.

## Measuring

```
pf --startup-trace --quit-after-paint ~
```

prints one line per phase of §3.4's critical path. `first-paint` is taken from a
real `paintEvent` on the panel, not from `show()` returning, because the gap
between those two is where compositor and driver costs actually land.

For a distribution rather than a single sample:

```
hyperfine --warmup 3 'pf --quit-after-paint <fixture>'
```

CI fails the build if the mean regresses more than 15% from the committed
baseline.

## Recorded baselines

### macOS 26.3, Apple M-series, Qt 6.10.2, Release build

Measured at M0 with an empty window (no panel, no scan) — the floor that later
milestones are measured against.

| Phase | Cumulative | Delta |
| --- | --- | --- |
| `argv` | 0.001 ms | 0.001 ms |
| `window` | 80.4 ms | 80.4 ms |
| `shown` | 118.5 ms | 38.1 ms |
| `first-paint` | 159.1 ms | 40.6 ms |

The 80 ms before `window` is almost entirely `QApplication` construction:
loading the Cocoa platform plugin, initialising `NSApplication`, and building
the font database. The two ~40 ms steps after it are AppKit window realisation
and the first frame going through the compositor.

**This is why macOS is held to its own baseline rather than to §11's numbers.**
None of that 159 ms is Panefile's own work — the same measurement on an empty
Qt application costs the same — and none of it is under our control. What *is*
under our control is the delta as milestones land, which is what the baseline
comparison measures. A macOS `.app` launch is structurally more expensive than
an ELF exec against a warm page cache, and pretending otherwise would either
make the target meaningless on Linux or unreachable on macOS.

### Linux

Not yet recorded. Populated from the M10 verification pass and from CI, both of
which run on Arch with a Release build. Until then the Linux figures in §11
stand unverified, and this file says so rather than implying otherwise.

## Dynamic dependencies

§3.4 identifies a silently added `DT_NEEDED` entry as the most common cause of
startup regression, so the dependency list is itself under test:

```
scripts/check-dependencies.sh <path-to-pf> docs/dependencies-<platform>.txt
```

Every optional feature — syntax highlighting, media playback, PDF rendering,
video thumbnails — is a plugin opened on first use. If one of them appears in
this list, it has become a cost paid at every launch by every user, including
the ones who never open Quick Look.
