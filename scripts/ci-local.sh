#!/usr/bin/env bash
#
# Runs the same gates as .github/workflows/ci.yml, locally.
#
# The point of this script is that "it builds and the tests pass" is not the
# bar CI applies — formatting and clang-tidy are gates too, and finding that
# out from a red build after pushing wastes a round trip every time. Run this
# before committing.
#
# Usage:
#   scripts/ci-local.sh              everything
#   scripts/ci-local.sh format       just clang-format
#   scripts/ci-local.sh build        configure, build, test (Debug, -Werror)
#   scripts/ci-local.sh tidy         just clang-tidy
#   scripts/ci-local.sh release      release build, dependency and startup guards
#
# Every stage runs even if an earlier one fails, so one invocation reports
# everything that is wrong rather than only the first thing.

set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

stage="${1:-all}"
failures=()

bold=$'\033[1m'
red=$'\033[31m'
green=$'\033[32m'
dim=$'\033[2m'
reset=$'\033[0m'

announce() { printf '\n%s==> %s%s\n' "$bold" "$1" "$reset"; }
ok()       { printf '%s  ok%s  %s\n' "$green" "$reset" "$1"; }
fail()     { printf '%s  FAIL%s  %s\n' "$red" "$reset" "$1"; failures+=("$1"); }

# --- tool discovery ---------------------------------------------------------
# Homebrew keeps clang-format and clang-tidy out of the default PATH on macOS,
# and Apple's toolchain does not ship them at all.
find_tool() {
    local name="$1"
    for candidate in "$name" "/opt/homebrew/opt/llvm/bin/$name" "/usr/local/opt/llvm/bin/$name"; do
        if command -v "$candidate" >/dev/null 2>&1; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

# clang-tidy needs to be told where the SDK is when it is a Homebrew build
# reading Apple's headers, otherwise every translation unit fails on <type_traits>.
tidy_extra_args=()
if [[ "$(uname -s)" == "Darwin" ]] && command -v xcrun >/dev/null 2>&1; then
    tidy_extra_args=(--extra-arg=-isysroot "--extra-arg=$(xcrun --show-sdk-path)")
fi

sources() {
    find src tests \( -name '*.cpp' -o -name '*.h' \) -not -path '*/build/*'
}

# --- stages -----------------------------------------------------------------

run_format() {
    announce "clang-format"
    local tool
    if ! tool="$(find_tool clang-format)"; then
        printf '%s  clang-format not found, skipping%s\n' "$dim" "$reset"
        return
    fi

    local unformatted
    unformatted="$(sources | xargs "$tool" --dry-run --Werror 2>&1 || true)"
    if [[ -n "$unformatted" ]]; then
        echo "$unformatted" | head -40
        fail "clang-format (run: sources | xargs $tool -i)"
    else
        ok "clang-format"
    fi
}

run_build() {
    announce "configure + build + test (dev, -Werror)"

    if ! cmake --preset dev >/dev/null; then
        fail "cmake configure"
        return
    fi
    ok "configure"

    if ! cmake --build --preset dev; then
        fail "build"
        return
    fi
    ok "build"

    if ctest --preset dev; then
        ok "tests"
    else
        fail "tests"
    fi
}

run_tidy() {
    announce "clang-tidy"
    local tool
    if ! tool="$(find_tool clang-tidy)"; then
        printf '%s  clang-tidy not found, skipping%s\n' "$dim" "$reset"
        return
    fi
    if [[ ! -f build/dev/compile_commands.json ]]; then
        fail "clang-tidy (no compile_commands.json — run the build stage first)"
        return
    fi

    local output=""
    local file
    while IFS= read -r file; do
        # Only files the build knows about; a source not yet added to a
        # CMakeLists has no compile command and would report a spurious error.
        if ! grep -q "\"$PWD/$file\"" build/dev/compile_commands.json 2>/dev/null; then
            continue
        fi
        output+="$("$tool" -p build/dev "${tidy_extra_args[@]}" \
            --warnings-as-errors='*' "$file" 2>/dev/null | grep -E '(error|warning):' || true)"
    done < <(find src -name '*.cpp' -not -path '*/build/*')

    if [[ -n "$output" ]]; then
        echo "$output" | head -40
        fail "clang-tidy"
    else
        ok "clang-tidy"
    fi
}

run_release() {
    announce "release build + startup and dependency guards"

    if ! cmake --preset release >/dev/null || ! cmake --build --preset release >/dev/null; then
        fail "release build"
        return
    fi
    ok "release build"

    if ctest --preset release >/dev/null; then
        ok "release tests"
    else
        fail "release tests"
    fi

    local binary platform
    if [[ "$(uname -s)" == "Darwin" ]]; then
        binary="build/release/bin/pf.app/Contents/MacOS/pf"
        platform="darwin"
    else
        binary="build/release/bin/pf"
        platform="linux"
    fi

    if scripts/check-dependencies.sh "$binary" "docs/dependencies-$platform.txt"; then
        ok "dynamic dependencies"
    else
        fail "dynamic dependencies"
    fi

    if scripts/check-startup-budget.sh "$binary" "docs/startup-baseline-$platform.txt"; then
        ok "startup budget"
    else
        fail "startup budget"
    fi
}

case "$stage" in
format)  run_format ;;
build)   run_build ;;
tidy)    run_tidy ;;
release) run_release ;;
all)     run_format; run_build; run_tidy; run_release ;;
*)
    echo "unknown stage '$stage'; expected one of: all format build tidy release" >&2
    exit 64
    ;;
esac

printf '\n'
if (( ${#failures[@]} > 0 )); then
    printf '%s%d stage(s) failed:%s\n' "$red" "${#failures[@]}" "$reset"
    printf '  - %s\n' "${failures[@]}"
    exit 1
fi
printf '%sall gates passed%s\n' "$green" "$reset"
