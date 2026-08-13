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
pf --startup-trace --quit-after-paint <fixture>
```

prints one line per phase of §3.4's critical path. `first-paint` is taken from a
real `paintEvent`, not from `show()` returning, because the gap between those
two is where compositor and driver costs actually land.

`scripts/check-startup-budget.sh` takes the median of 20 such runs and fails the
build on a regression of more than 15% from the committed baseline. Two details
of how it measures were arrived at the hard way:

- **It reads `first-paint` from the trace rather than timing the process.**
  Those were the same number until a directory scan was running at exit, at
  which point process lifetime started including the wait for the scanner thread
  to notice it had been cancelled. That is teardown, and §11's criterion is
  "cold start to first painted window".

- **It fixes the platform plugin, and the baseline is only valid under that
  plugin.** The script forces `QT_QPA_PLATFORM=offscreen`, both so that
  twenty-three launches do not open twenty-three windows over whatever the
  developer is doing, and so the measurement does not depend on which compositor
  or display is attached. Offscreen is markedly *slower* than cocoa here —
  software rendering, no font or graphics acceleration — so the two numbers are
  not interchangeable. Comparing a cocoa measurement against an offscreen
  baseline reports a 2.5× regression that does not exist; that happened once,
  which is why it is written down here.

- **It benchmarks a generated fixture, never `$HOME`.** §3.4 asks for "a
  fixed-size fixture directory" and the reason is measurable: two consecutive
  runs against `$HOME` here differed by 25%, wider than the threshold the check
  exists to enforce. `scripts/make-fixture.sh` builds a deterministic 2,200-entry
  tree with the mixture of kinds, hidden entries and symlinks that exercises the
  scanner's branches.

## Recorded baselines

### macOS 26.3, Apple M-series, Qt 6.10.2, Release build

| Milestone | First paint | Plugin | What changed |
| --- | --- | --- | --- |
| M0 | 159 ms | cocoa | Empty window |
| M1 | 292 ms | cocoa | One panel listing a 2,200-entry fixture |
| M3 | 255 ms | cocoa | Sidebar, three panels, theme and stylesheet |
| M3 | 648 ms | offscreen | The same build, measured the way CI measures it |

Reading config.toml and theme.toml and compiling the stylesheet costs about
1 ms of the total, measured between the `argv` and `stylesheet` phases — which
is the answer to whether §3.4 was right to put them on the critical path.

The M1 figure is measured against the fixture rather than `$HOME`; the two are
not comparable, which is the point of having a fixture at all.

Where M1's time goes:

| Phase | Cumulative | Delta |
| --- | --- | --- |
| `argv` | 0.01 ms | 0.01 ms |
| `window` | 77 ms | 77 ms |
| `scan-start` | 77 ms | 0.07 ms |
| `shown` | 214 ms | 137 ms |
| `first-paint` | 246 ms | 32 ms |

Two costs dominate, and neither is Panefile's own work:

- **77 ms constructing `QApplication`** — loading the Cocoa platform plugin and
  initialising `NSApplication`. Nothing of ours runs before it.
- **~92 ms inside `show()`, populating Qt's font database.** Measured by forcing
  a `QFontMetrics::height()` call before `show()` and watching the cost move.
  M0 paid none of it because an empty window lays out no text; M1 pays it
  because a file listing is text. It cannot be deferred — text is the product —
  and it cannot usefully be moved to a worker thread, because there is under a
  millisecond of GUI-thread work available to overlap it with.

That leaves ~120 ms of the 292 attributable to anything we control, and the
regression guard is what watches *that* number move.

**This is why macOS is held to its own baseline rather than to §11's numbers.**
A macOS `.app` launch is structurally more expensive than an ELF exec against a
warm page cache, and most of the cost above is Qt's rather than ours. Holding
both platforms to one absolute number would either make the target meaningless
on Linux or unreachable on macOS. The delta as milestones land is the thing
worth guarding, and it is what the baseline comparison measures.

### Arch Linux, GCC, Release, in CI

| Milestone | First paint | Plugin |
| --- | --- | --- |
| M3 | 11.8 ms | offscreen |

Against §11's 80 ms target, so the budget is met with a wide margin on the
platform the target is about — and by a factor large enough that the two
caveats do not threaten it. Those caveats: the offscreen plugin excludes the
compositor round trip a real Wayland session pays, and a CI runner is not a
desktop.

The contrast with macOS is the whole reason this file separates them. The same
code reaches first paint in 11.8 ms on Linux and 648 ms on macOS under the same
plugin, because a `.app` launch pays for dyld, `NSApplication` and Qt's font
database before anything of ours runs. Holding both to one number would either
make the Linux target meaningless or the macOS one unreachable.

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
