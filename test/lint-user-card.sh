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
#   - test/harness-selftest.sh: only the lines rooted at its scratch fixtures ($TSD, $PG2, $MC2: the sentinel homes of
#     HS-195/196, and $ULC_FIX, the dirty fixture of HS-198b, a write of its own line) and its comments; a line naming a scratch card AND another card is not excepted;
#   - test/00regression/scripts/sdcard-user-card-func.sh and sdcard-no-default-card-func.sh: only the lines rooted at
#     their scratch home ($H/);
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

# Two spellings of the user's card: the literal path, and the state-dir default the suites' master used
# to be spelled with (the seam's default `${JNEXT_REGRESSION_SD_STATE_DIR:-$HOME/.jnext}`): a consumer that
# still spells it takes the user's directory for the suites'.
PATTERN='\.jnext/sdcard/cspect-next-1gb|JNEXT_REGRESSION_SD_STATE_DIR:-[^}]*\.jnext\}'
SCOPE='^(test/|tools/sd-master-ensure\.sh$|tools/pgo-[a-z-]+\.sh$|Makefile$|\.github/workflows/)'
# A fake-HOME line of the provisioner's own unit suite.
TMPDIR_LINE='g_tmpdir[ ]*\+'

# rooted_only <root regex> <text>: 0 when EVERY card path on the line is rooted at the scratch root (the
# root, then the rest of the same word, then the card path). The rooted spellings are cut out and the
# line must then be clean: a line that also names the HOME card is not excepted by the scratch path
# beside it.
rooted_only() {
    local rest
    rest=$(sed -E "s#($1)[^ \"';]*\.jnext/sdcard/cspect-next-1gb##g" <<<"$2")
    [[ "$rest" != "$2" ]] && ! grep -qE "$PATTERN" <<<"$rest"
}

# is_excepted <file> <text>: 0 when the (file, line) pair is a declared exception.
is_excepted() {
    case "$1" in
        test/lint-user-card.sh|test/lint-hardcoded-paths.sh) return 0 ;;
        test/harness-selftest.sh) grep -qE '^[[:space:]]*#' <<<"$2" && return 0; rooted_only '\$(TSD|PG2|MC2)' "$2" && return 0
            grep -qE "^printf '[^']*' > \"\\\$ULC_FIX/(clean|dirty)/row\\.sh\"\$" <<<"$2" && return 0 ;;   # HS-198's fixture writes, nothing else on the line
        test/00regression/scripts/sdcard-user-card-func.sh|test/00regression/scripts/sdcard-no-default-card-func.sh) rooted_only '\$H/' "$2" && return 0 ;;
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
    t 1 'the old state-dir default'        'dir="${JNEXT_REGRESSION_SD_STATE_DIR:-$HOME/.jnext}/sdcard"'
    t 0 'the test state dir'               'dir="$(sd_test_state_dir)/sdcard"'
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
    x() {   # x <want 1|0> <desc> <file> <text>: is the (file, line) pair excepted
        local got=0
        is_excepted "$3" "$4" && got=1
        if [[ $got == "$1" ]]; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "[lint-user-card] SELFTEST FAIL: $2 (want $1, got $got): $3: $4" >&2; fi
    }
    x 1 'a harness-selftest scratch-home line'   test/harness-selftest.sh 'TSD_CARD="$TSD/home/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    x 1 'the dirty fixture of HS-198b'           test/harness-selftest.sh 'printf '"'"'x="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"\n'"'"' > "$ULC_FIX/dirty/row.sh"'
    x 0 'a card access before the fixture write'  test/harness-selftest.sh 'cp ~/.jnext/sdcard/cspect-next-1gb-fixed.img /x; printf x > "$ULC_FIX/dirty/row.sh"'
    x 1 'a harness-selftest comment'             test/harness-selftest.sh '# the user card ~/.jnext/sdcard/cspect-next-1gb-fixed.img'
    x 0 'a harness-selftest line on the HOME card' test/harness-selftest.sh 'SD="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    x 1 'a user-card-func scratch-home line'     test/00regression/scripts/sdcard-user-card-func.sh 'UC="$H/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    x 0 'a user-card-func line on the HOME card' test/00regression/scripts/sdcard-user-card-func.sh 'UC="$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    x 0 'a harness-selftest line mixing a scratch path and the HOME card' test/harness-selftest.sh 'cp "$TSD/.jnext/sdcard/cspect-next-1gb-fixed.img" "$HOME/.jnext/sdcard/cspect-next-1gb-fixed.img"'
    x 0 'a user-card-func line mixing the scratch home and the HOME card' test/00regression/scripts/sdcard-user-card-func.sh 'cmp "$H/.jnext/sdcard/cspect-next-1gb-fixed.img" ~/.jnext/sdcard/cspect-next-1gb-fixed.img'
    x 1 'two scratch-rooted card paths on one line' test/harness-selftest.sh 'cmp "$TSD/a/.jnext/sdcard/cspect-next-1gb-fixed.img" "$PG2/.jnext/sdcard/cspect-next-1gb.img"'
    x 0 'any other file'                         test/run-unit-tests.sh 'm="$TSD/.jnext/sdcard/cspect-next-1gb-fixed.img"'
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
