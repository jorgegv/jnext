#!/usr/bin/env bash
# Structural lint: no detached threads in our own sources (GH #328).
#
# MinGW winpthreads (the library behind std::thread on our Windows builds)
# recycles a thread's descriptor when pthread_detach runs on a thread that has
# already ended, while that thread's DLL_THREAD_DETACH can still reach it
# through its TLS slot. The NEXT thread created gets the recycled descriptor
# and can be wiped: it never runs its body, or calls a null function. Any
# `.detach()` anywhere in the Windows process can bring that back for whichever
# thread comes next, and no unit row can force the window, so the rule is
# structural: detached threads go through esp::net::start_detached, whose
# Windows twin uses a Win32 thread.
#
# The rule: no `.detach()` / `->detach()` call and no `pthread_detach(` in
# src/ or test/ (third_party/ excluded), except in the one sanctioned POSIX
# twin, src/esp01/src/esp_socket_posix.cpp. `//` comments are ignored.
#
# MAY WRONGLY FLAG a non-thread detach(), e.g. Qt's QString::detach(). None
# exist in the tree today; such a call would need this lint changed, on purpose.
#
# Usage: bash test/lint-thread-detach.sh [root]   (root defaults to the repo)
# Exit: 0 clean, 1 violation, 2 self-test failure or missing ripgrep.
# Self-tests on every invocation, like lint-assertions.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="${1:-$(cd "$SCRIPT_DIR/.." && pwd)}"
ALLOWED="src/esp01/src/esp_socket_posix.cpp"

if ! command -v rg &>/dev/null; then
    echo "ERROR: ripgrep (rg) is required" >&2
    exit 2
fi

PATTERN='(\.|->)\s*detach\s*\(\s*\)|\bpthread_detach\s*\('

# scan <dir>: print "file:line:text" for every offending line under <dir>,
# comments stripped before matching.
scan() {
    local dir="$1"
    { rg -n --no-heading --no-messages \
          --glob '*.cpp' --glob '*.cc' --glob '*.h' --glob '*.hpp' --glob '*.c' \
          '.' "$dir" || true; } \
      | sed -E 's://.*$::' \
      | { rg -e "$PATTERN" || true; }
}

self_test() {
    local dir bad=0 n
    dir=$(mktemp -d)
    mkdir -p "$dir/must" "$dir/mustnot"
    cat >"$dir/must/flag.cpp" <<'FIXTURE'
void f() { std::thread(f).detach(); }
void g() { t.detach(); }
void h() { pthread_detach(th); }
void i() { worker_ . detach ( ); }
void j() { p->detach(); }
FIXTURE
    cat >"$dir/mustnot/clean.cpp" <<'FIXTURE'
// t.detach();
void f() { detached_ = true; }  // x.detach();
void g() { thread.join(); }
FIXTURE
    n=$(scan "$dir/must" | wc -l)
    if [[ "$n" -ne 5 ]]; then
        echo "ERROR: self-test: expected 5 must-flag lines, matched $n" >&2; bad=1
    fi
    n=$(scan "$dir/mustnot" | wc -l)
    if [[ "$n" -ne 0 ]]; then
        echo "ERROR: self-test: $n must-not-flag line(s) matched" >&2; bad=1
    fi
    rm -rf "$dir"
    if [[ "$bad" -ne 0 ]]; then
        echo "[lint-thread-detach] SELF-TEST FAILED: refusing to report on the tree." >&2
        exit 2
    fi
}

self_test

hits=""
for d in src test; do
    [[ -d "$ROOT/$d" ]] || continue
    hits+="$(cd "$ROOT" && scan "$d")"$'\n'
done
hits=$(grep -v "^$ALLOWED:" <<<"$hits" | grep -v '^$' || true)

if [[ -n "$hits" ]]; then
    echo "[lint-thread-detach] detached thread outside $ALLOWED (GH #328):" >&2
    printf '%s\n' "$hits" >&2
    echo "Use esp::net::start_detached (src/esp01/include/esp01/esp_socket_platform.h)." >&2
    exit 1
fi
echo "[lint-thread-detach] clean"
