#!/usr/bin/env bash
#
# Guards the dynamic dependency list of the pf binary against silent growth.
#
# §3.4: "A silent new DT_NEEDED entry is the most common way startup time
# regresses." Every optional feature is supposed to arrive as a plugin loaded on
# first use, so a new entry here means something that should have been lazy has
# become a load-time cost for every launch, including the launches that never
# touch the feature.
#
# Usage: check-dependencies.sh <path-to-pf> [baseline-file]
#
# With no baseline the current list is printed, which is how you regenerate one.

set -euo pipefail

binary="${1:?usage: check-dependencies.sh <path-to-pf> [baseline-file]}"
baseline="${2:-}"

if [[ ! -f "$binary" ]]; then
    echo "check-dependencies: no such file: $binary" >&2
    exit 1
fi

# Reduce to bare library names, sorted, so the comparison is not sensitive to
# install prefixes, version suffixes or link order.
list_dependencies() {
    case "$(uname -s)" in
    Linux)
        ldd "$binary" \
            | awk '{print $1}' \
            | sed -E 's/\.so[.0-9]*$/.so/; s|.*/||' \
            | grep -v '^linux-vdso' \
            | sort -u
        ;;
    Darwin)
        otool -L "$binary" \
            | tail -n +2 \
            | awk '{print $1}' \
            | sed -E 's/\.[0-9]+\.dylib$/.dylib/; s|.*/||' \
            | sort -u
        ;;
    *)
        echo "check-dependencies: unsupported platform $(uname -s)" >&2
        exit 1
        ;;
    esac
}

actual="$(list_dependencies)"

if [[ -z "$baseline" ]]; then
    echo "$actual"
    exit 0
fi

if [[ ! -f "$baseline" ]]; then
    echo "check-dependencies: no baseline at $baseline" >&2
    echo "Regenerate with: $0 $binary > $baseline" >&2
    exit 1
fi

expected="$(grep -vE '^\s*(#|$)' "$baseline" | sort -u)"

if [[ "$actual" == "$expected" ]]; then
    echo "check-dependencies: $(echo "$actual" | wc -l | tr -d ' ') dependencies, unchanged"
    exit 0
fi

echo "check-dependencies: the dynamic dependency list changed." >&2
echo >&2
diff -u <(echo "$expected") <(echo "$actual") | tail -n +3 >&2 || true
echo >&2
echo "A new entry means a library is now linked at load time rather than opened" >&2
echo "on first use. If that is deliberate, justify it and update $baseline." >&2
exit 1
