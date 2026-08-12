#!/usr/bin/env bash
#
# Creates a fixed-size directory for benchmarking.
#
# §3.4 specifies the startup benchmark runs "against a fixed-size fixture
# directory", and the reason is measurable: benchmarking against $HOME compares
# a different directory on every machine and a different one on the same
# machine week to week, which turns a regression guard into a noise generator.
#
# Usage: make-fixture.sh [directory] [entry-count]
#
# Deterministic: same arguments produce the same tree, so re-creating it does
# not move the baseline. Prints the directory on stdout.

set -euo pipefail

directory="${1:-${TMPDIR:-/tmp}/panefile-fixture}"
count="${2:-2000}"

# A stamp of the parameters, so a fixture built with different ones is rebuilt
# rather than silently reused.
stamp="$directory/.fixture-stamp"
want="v1 count=$count"

if [[ -f "$stamp" ]] && [[ "$(cat "$stamp")" == "$want" ]]; then
    echo "$directory"
    exit 0
fi

rm -rf "$directory"
mkdir -p "$directory"

# A mixture rather than a flat run of identical names: the sort is natural and
# locale-aware, the delegate colours by kind, and the scanner has a separate
# path for symlinks — a fixture of 2000 files called file0001 would exercise
# none of that and would flatter every measurement taken against it.
suffixes=(txt md cpp h json png jpg tar.gz mp4 pdf "")
for ((i = 0; i < count; ++i)); do
    suffix="${suffixes[$((i % ${#suffixes[@]}))]}"
    if [[ -n "$suffix" ]]; then
        name="entry-$i.$suffix"
    else
        name="entry-$i"
    fi
    printf 'fixture %d\n' "$i" > "$directory/$name"
done

# Directories, hidden entries, a symlink and a deliberately broken one, so the
# scanner's second stat and its dangling-link branch are both on the measured
# path.
for ((i = 0; i < count / 20; ++i)); do
    mkdir -p "$directory/dir-$i"
done
for ((i = 0; i < count / 20; ++i)); do
    printf 'hidden\n' > "$directory/.hidden-$i"
done
ln -s "entry-0.txt" "$directory/link-valid"
ln -s "does-not-exist" "$directory/link-broken"

echo "$want" > "$stamp"
echo "$directory"
