#!/usr/bin/env bash
# User-card lint (GH #555): no test, tool or workflow may name the USER'S SD card.
#
# The user's card is ~/.jnext/sdcard/cspect-next-1gb-fixed.img: the card jnext boots when it is
# given no --sdcard, and the one a user keeps their files on. The suites boot their OWN card,
# the test master under ~/.jnext/sdcard/test-suite/ (test/00regression/test-functions.inc,
# sd_test_state_dir; test/test_sd_image.h). The master gate DELETES and re-derives its card
# whenever the distribution or the recipe moves, so a suite that reads or provisions the user's
# card puts the user's saved files at risk: that is the defect this lint keeps out.
#
# Banned, in the tracked test/, the SD-master tools (tools/sd-master-ensure.sh, tools/pgo-*.sh),
# the Makefile and the workflows: the literal  .jnext/sdcard/cspect-next-1gb  (the card, or the
# raw distribution beside it). The test master's spelling  .jnext/sdcard/test-suite/sdcard/cspect-...
# does not contain it.
#
# Declared exceptions (path:fixed text), each a reason and not a glob:
#   - this file (its own self-test fixtures are literal instances);
#   - test/lint-hardcoded-paths.sh: a case of ITS table, a string it classifies and never opens;
#   - test/sdcard_provisioner/sdcard_provisioner_test.cpp lines that build the path under g_tmpdir:
#     the suite exercises the provisioner's default-path rule inside a throw-away HOME.
#
# Usage:
#   bash test/lint-user-card.sh              # scan the tree; exit 1 on any offender
#   bash test/lint-user-card.sh --selftest   # prove the matcher, both ways
#   JNEXT_LINT_USER_CARD_DIR=<dir>           # scan <dir>/* instead (the harness self-test's fixtures)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PATTERN='\.jnext/sdcard/cspect-next-1gb'
SCOPE='^(test/|tools/sd-master-ensure\.sh$|tools/pgo-[a-z-]+\.sh$|Makefile$|\.github/workflows/)'
# A fake-HOME line of the provisioner's own unit suite.
TMPDIR_LINE='g_tmpdir[ ]*\+'

# is_excepted <file> <text>: 0 when the (file, line) pair is a declared exception.
is_excepted() {
    case "$1" in
        test/lint-user-card.sh|test/lint-hardcoded-paths.sh) return 0 ;;
        test/sdcard_provisioner/sdcard_provisioner_test.cpp) grep -qE "$TMPDIR_LINE" <<<"$2" && return 0 ;;
    esac
    return 1
}

scan_file() {   # scan_file <label> <path>: prints offending lines, returns the count in $hits
    local label=$1 path=$2 line n text
    while IFS= read -r line; do
        n=${line%%:*}; text=${line#*:}
        is_excepted "$label" "$text" && continue
        printf '  %s:%s:%s\n' "$label" "$n" "$text"
        hits=$((hits + 1))
    done < <(grep -nE -- "$PATTERN" "$path" 2>/dev/null || true)
}

selftest() {
    local pass=0 fail=0
    t() {   # t <want 1|0> <desc> <text>
        local got=0
        grep -qE -- "$PATTERN" <<<"$3" && got=1
        if [[ $got == "$1" ]]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "[lint-user-card] SELFTEST FAIL: $2 (want $1, got $got): $3" >&2; fi
    }
    t 1 'the user card'                    'SD="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    t 1 'the raw distribution'             'DIST=~/.jnext/sdcard/cspect-next-1gb.img'
    t 1 'a quoted C++ literal'             '"/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    t 0 'the test master'                  'SD="$HOME/.jnext/sdcard/test-suite/sdcard/cspect-next-1gb-fixed.img"'
    t 0 'the cache directory alone'        'path: ~/.jnext/sdcard'
    t 0 'a candidate card dir'             'JNEXT_REGRESSION_SD_STATE_DIR="$W/cand"'
    local fix; fix=$(mktemp -d)
    printf 'x="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"\n' > "$fix/dirty.sh"
    printf 'x="$HOME/.jnext/sdcard/test-suite/sdcard/cspect-next-1gb-fixed.img"\n' > "$fix/clean.sh"
    local hits=0; scan_file clean.sh "$fix/clean.sh" >/dev/null
    [[ $hits == 0 ]] && pass=$((pass + 1)) || { fail=$((fail + 1)); echo "[lint-user-card] SELFTEST FAIL: a clean file was flagged" >&2; }
    hits=0; scan_file dirty.sh "$fix/dirty.sh" >/dev/null
    [[ $hits == 1 ]] && pass=$((pass + 1)) || { fail=$((fail + 1)); echo "[lint-user-card] SELFTEST FAIL: a dirty file was not flagged" >&2; }
    # Plain removal of the scratch directory's two files, then the directory.
    rm -f "$fix/dirty.sh" "$fix/clean.sh"; rmdir "$fix"
    if is_excepted test/sdcard_provisioner/sdcard_provisioner_test.cpp 'x = g_tmpdir + "/.jnext/sdcard/cspect-next-1gb-fixed.img"'; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "[lint-user-card] SELFTEST FAIL: the fake-HOME exception does not apply" >&2; fi
    if is_excepted test/sdcard_provisioner/sdcard_provisioner_test.cpp 'x = home + "/.jnext/sdcard/cspect-next-1gb-fixed.img"'; then fail=$((fail + 1)); echo "[lint-user-card] SELFTEST FAIL: the fake-HOME exception is too wide" >&2; else pass=$((pass + 1)); fi
    echo "[lint-user-card] selftest: pass=$pass fail=$fail"
    [[ $fail == 0 ]]
}

if [[ "${1:-}" == --selftest ]]; then selftest; exit $?; fi
selftest >/dev/null || { selftest; echo "[lint-user-card] the matcher's own self-test failed" >&2; exit 1; }

hits=0; scanned=0
if [[ -n "${JNEXT_LINT_USER_CARD_DIR:-}" ]]; then
    [[ -d "$JNEXT_LINT_USER_CARD_DIR" ]] || { echo "ERROR: JNEXT_LINT_USER_CARD_DIR is not a directory: $JNEXT_LINT_USER_CARD_DIR" >&2; exit 2; }
    for f in "$JNEXT_LINT_USER_CARD_DIR"/*; do
        [[ -f "$f" ]] || continue
        scanned=$((scanned + 1)); scan_file "$(basename "$f")" "$f"
    done
else
    cd "$PROJECT_DIR"
    while IFS= read -r f; do
        [[ -f "$f" ]] || continue
        # Binary reference images and fixtures cannot hold a path worth a verdict.
        [[ "$f" =~ \.(png|nex|tap|tzx|bin|img|jsdw)$ ]] && continue
        scanned=$((scanned + 1)); scan_file "$f" "$f"
    done < <(git ls-files | grep -E "$SCOPE" || true)
fi
echo "[lint-user-card] scanned: $scanned file(s), $hits offending line(s)"
[[ $hits == 0 ]]
