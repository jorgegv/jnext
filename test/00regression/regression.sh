#!/usr/bin/env bash
# Automated regression test suite for JNEXT emulator — the driver.
# Runs screenshot tests + a few functional/integration tests, all of whose
# logic lives in scripts/: one script per declared functional test, plus the
# three group scripts (00-preflight-lint.sh, 01-sdcard-provision.sh,
# screenshots.sh). This file only parses arguments, validates the declared-test
# manifests, sources the test scripts in declared order, and enforces the
# end-of-run completeness accounting.
# (FUSE Z80 + Z80N opcode coverage lives in `make unit-test`.)
#
# Usage: bash test/00regression/regression.sh [--update] [--preflight-only] [test_name...]
#   --update          Update reference screenshots instead of comparing
#   --preflight-only  Run only the harness preflight checks and exit (no tests).
#                     This is the seam test/harness-selftest.sh drives.
#   test_name         Run only specified tests (default: all)
#
# Env: JNEXT_REGRESSION_CONF / JNEXT_REGRESSION_FUNC_CONF override the
#      manifests; JNEXT_REGRESSION_SCRIPTS_DIR overrides the test-script
#      directory (the self-test uses these to inject a truncated or unpinned
#      manifest, a declared test with no script, or a stray undeclared script).
#      JNEXT_REGRESSION_SCRIPTS_DIR is usable with --preflight-only ONLY: a
#      full run sources the override's scripts, which resolve
#      ../test-functions.inc relative to their own directory and die loudly.

set -euo pipefail

# Shared helpers/constants and the one-time environment setup (locale, jnext
# binary resolution, SD args, manifest paths, TMP_DIR + EXIT trap, counters,
# row helpers) live in the suite library; sourcing it initializes them once.
# shellcheck source=test/00regression/test-functions.inc
source "$(dirname "$0")/test-functions.inc"

# Test scripts: scripts/<name>.sh for every functional test declared in
# functional_tests.conf, plus the three group scripts.
SCRIPTS_DIR="${JNEXT_REGRESSION_SCRIPTS_DIR:-$SCRIPT_DIR/scripts}"

# Parse arguments
UPDATE_MODE=false
PREFLIGHT_ONLY=false
FILTER_TESTS=()
for arg in "$@"; do
    if [[ "$arg" == "--update" ]]; then
        UPDATE_MODE=true
    elif [[ "$arg" == "--preflight-only" ]]; then
        PREFLIGHT_ONLY=true
    else
        FILTER_TESTS+=("$arg")
    fi
done
# Membership is an in-shell hash lookup, NEVER `printf ... | grep -qx`: under
# `set -o pipefail` grep -q exits the instant it matches, the printf subshell
# can then die of SIGPIPE (141), and pipefail promotes 141 to the pipeline's
# status — a name that IS present reports as absent.
declare -A IS_FILTERED
for arg in "${FILTER_TESTS[@]+"${FILTER_TESTS[@]}"}"; do IS_FILTERED["$arg"]=1; done

# Check prerequisites
if [[ ! -x "$JNEXT" ]]; then
    echo -e "${RED}ERROR: jnext binary not found at $JNEXT — build first${RESET}"
    exit 1
fi

# No SD-card fixture existence check here: the suite self-provisions the
# fallback image (the "[sdcard-provision]" row, scripts/01-sdcard-provision.sh)
# after the lint row and before the screenshot launch, once the
# manifest/preflight checks below have passed. --preflight-only never needs an
# SD image at all.

# A manifest that is not there must say so, not be diagnosed as "missing its pin".
for conf in "$CONF" "$FUNC_CONF"; do
    [[ -f "$conf" ]] || harness_fault "Test manifest not found: ${BOLD}$conf${RESET}"
done
echo -e "  manifests: $(basename "$CONF") + $(basename "$FUNC_CONF")"

# rewind-func runs a unit-test binary that `make clean` deletes. Check it HERE,
# in the first second, not five minutes into the run: an incomplete build is a
# harness fault, not a code regression — and never a silently absent row.
if [[ ${#FILTER_TESTS[@]} -eq 0 || -n "${IS_FILTERED[rewind-func]:-}" ]]; then
    if [[ ! -x "$REWIND_TEST" ]]; then
        harness_fault "rewind_test is not built: ${BOLD}$REWIND_TEST${RESET}" \
                      "The suite runs it, so it cannot report a rewind result without it." \
                      "Build it with: ${BOLD}make unit-test-build${RESET}  (or use ${BOLD}make regression${RESET}, which does)"
    fi
fi

# sdl-keypress-func drives the SDL-ONLY frontend, which is a different binary
# from the one every other row uses: SdlApp is instantiated only when
# ENABLE_QT_UI=OFF (src/main.cpp:944-951). Same rule as rewind_test above — it
# is a build artifact the Makefile guarantees, so a missing one is a harness
# fault in the first second, never a row that quietly reports nothing.
if [[ ${#FILTER_TESTS[@]} -eq 0 || -n "${IS_FILTERED[sdl-keypress-func]:-}" ]]; then
    if [[ ! -x "$PROJECT_DIR/build/sdl-release/jnext" ]]; then
        harness_fault "SDL-only jnext is not built: ${BOLD}$PROJECT_DIR/build/sdl-release/jnext${RESET}" \
                      "It is the only build that runs SdlApp, so the suite cannot report an SDL keypress result without it." \
                      "Build it with: ${BOLD}make sdl-release${RESET}  (or use ${BOLD}make regression${RESET}, which does)"
    fi
fi

# --- The declared counts, pinned ---
# `# expect: N` in each manifest. Without it, deleting a test from a conf
# shrinks both sides of the completeness check below in lockstep and the suite
# reports a smaller number as a clean pass. The pin makes the denominator a
# claim the file has to make out loud, exactly like test/unit-tests.conf's
# per-suite row counts.
for conf in "$CONF" "$FUNC_CONF"; do
    pin=$(pinned_count "$conf")
    have=$(declared_count "$conf")
    [[ -n "$pin" ]] || harness_fault "No '# expect: N' pin in ${BOLD}$conf${RESET}" \
                                     "The manifest must state how many tests it declares."
    [[ "$pin" -eq "$have" ]] || harness_fault \
        "${BOLD}$conf${RESET} declares ${BOLD}$have${RESET} tests but pins ${BOLD}# expect: $pin${RESET}" \
        "A test was added or removed without updating the pin. If deliberate, update it."
done

# --- The screenshot manifest needs an INDEPENDENT witness ---
# The completeness check at the end compares the rows reported against the
# rows declared — but for screenshots both sides come from
# regression_tests.conf, so on its own that term is a tautology.
# img/<name>-reference.png is the independent witness: it is checked in, one
# per screenshot test, and it does not disappear when the conf is truncated.
# A reference with no conf entry means a test was dropped from the manifest.
declare -A IS_DECLARED_SCREENSHOT
while read -r name _; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    IS_DECLARED_SCREENSHOT["$name"]=1
done < "$CONF"
for ref in "$IMG_DIR"/*-reference.png; do
    [[ -e "$ref" ]] || continue           # no refs at all (fresh tree) — nothing to witness
    ref_name=${ref##*/}
    ref_name=${ref_name%-reference.png}
    [[ -n "${IS_DECLARED_SCREENSHOT[$ref_name]:-}" ]] \
        || harness_fault "reference image ${BOLD}img/${ref_name}-reference.png${RESET} exists, but ${BOLD}$ref_name${RESET} is NOT declared in regression_tests.conf" \
                         "A screenshot test was dropped from the manifest. If that was deliberate, delete its reference image too."
done

# --- The declared functional tests (test/00regression/functional_tests.conf) ---
# Each test script calls `begin_func <name>`, which records that the row was
# actually reported. The completeness check at the end of the run proves that
# every declared test reported exactly one row and no undeclared row appeared.
DECLARED_FUNC=()
while read -r name _; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    DECLARED_FUNC+=("$name")
done < "$FUNC_CONF"
[[ ${#DECLARED_FUNC[@]} -gt 0 ]] || harness_fault "No functional tests declared in $FUNC_CONF"
declare -A IS_DECLARED_FUNC
for name in "${DECLARED_FUNC[@]}"; do IS_DECLARED_FUNC["$name"]=1; done
REPORTED_FUNC=()

# --- The scripts directory and the conf must agree, in BOTH directions ---
# Exactly like the reference-image witness above: a declared test with no
# scripts/<name>.sh could never report its row, and a stray scripts/*.sh is a
# test that was dropped from the manifest.
for name in "${DECLARED_FUNC[@]}"; do
    [[ -f "$SCRIPTS_DIR/$name.sh" ]] \
        || harness_fault "functional test ${BOLD}$name${RESET} is declared in functional_tests.conf but has NO test script" \
                         "Expected ${BOLD}$SCRIPTS_DIR/$name.sh${RESET} — a declared test with no script can never report its row." \
                         "If the test was removed deliberately, remove its conf line and update the pin."
done
for script in "$SCRIPTS_DIR"/*.sh; do
    [[ -e "$script" ]] || continue        # no scripts at all — caught above
    script_name=${script##*/}
    case "$script_name" in
        00-preflight-lint.sh|01-sdcard-provision.sh|screenshots.sh) continue ;;
    esac
    test_name=${script_name%.sh}
    [[ -n "${IS_DECLARED_FUNC[$test_name]:-}" ]] \
        || harness_fault "test script ${BOLD}$SCRIPTS_DIR/$script_name${RESET} exists, but ${BOLD}$test_name${RESET} is NOT declared in functional_tests.conf" \
                         "A functional test was dropped from the manifest. If that was deliberate, delete its script too."
done

# Every preflight guard has now run. --preflight-only exists so the self-test
# can drive each of them in a second instead of a five-minute suite: these
# guards are what make the denominator trustworthy.
if $PREFLIGHT_ONLY; then
    echo -e "${GREEN}preflight OK${RESET}: $(declared_count "$CONF") screenshot + ${#DECLARED_FUNC[@]} functional tests declared, pins agree"
    exit 0
fi

if ! $HAS_COMPARE; then
    echo -e "${YELLOW}WARNING: ImageMagick 'compare' not found — pixel comparison disabled${RESET}"
fi

echo -e "${BOLD}=== JNEXT Regression Test Suite ===${RESET}"
echo ""

# Group rows: the two preflight lints and the SD-image provisioning, then the
# whole screenshot suite. Every test script is SOURCED (never exec'd) so all of
# them share this one shell's counters, REPORTED_FUNC and ORDERED_TESTS.
# shellcheck source=test/00regression/scripts/00-preflight-lint.sh
source "$SCRIPTS_DIR/00-preflight-lint.sh"
# shellcheck source=test/00regression/scripts/01-sdcard-provision.sh
source "$SCRIPTS_DIR/01-sdcard-provision.sh"

# FUSE Z80 + Z80N opcode tests run in `make unit-test` (fuse_z80_test +
# z80n_test), not here.

# shellcheck source=test/00regression/scripts/screenshots.sh
source "$SCRIPTS_DIR/screenshots.sh"

# --- Functional tests ---
echo ""
echo -e "${BOLD}Running functional tests...${RESET}"
echo ""

# One script per declared functional test, sourced in conf order. Each script
# want-guards itself, so name filters behave exactly as before.
for func_name in "${DECLARED_FUNC[@]}"; do
    # shellcheck source=/dev/null
    source "$SCRIPTS_DIR/$func_name.sh"
done

echo ""
echo -e "${BOLD}=== Results ===${RESET}"
echo -e "  ${GREEN}Pass: $pass${RESET}  ${RED}Fail: $fail${RESET}  ${YELLOW}Skip: $skip${RESET}"

# --- Completeness: prove the suite ran everything it declares ---
# A green result is only as trustworthy as its denominator. On a full run, every
# declared functional test must have reported exactly one row, no undeclared row
# may appear, and the grand total must equal 2 (preflight lints) +
# 1 (sdcard-provision) + screenshots + functional. Anything else means a test
# went missing, which is a harness fault, not a pass.
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] && ! $UPDATE_MODE; then
    faults=()
    # Per-name row counts and membership come from pure-bash hashes over the
    # arrays — never a pipe/subshell (`printf ... | grep` can misreport under
    # `set -o pipefail`; see the note above IS_FILTERED).
    declare -A REPORTED_COUNT
    for name in "${REPORTED_FUNC[@]+"${REPORTED_FUNC[@]}"}"; do
        REPORTED_COUNT["$name"]=$(( ${REPORTED_COUNT["$name"]:-0} + 1 ))
    done
    for name in "${DECLARED_FUNC[@]}"; do
        n=${REPORTED_COUNT["$name"]:-0}
        [[ "$n" -eq 1 ]] || faults+=("declared in functional_tests.conf but reported $n rows: ${BOLD}$name${RESET}")
    done
    for name in "${REPORTED_FUNC[@]}"; do
        [[ -n "${IS_DECLARED_FUNC[$name]:-}" ]] \
            || faults+=("reported a row but is NOT declared in functional_tests.conf: ${BOLD}$name${RESET}")
    done
    expected=$(( 2 + 1 + ${#ORDERED_TESTS[@]} + ${#DECLARED_FUNC[@]} ))
    actual=$(( pass + fail + skip ))
    [[ "$actual" -eq "$expected" ]] \
        || faults+=("row count is ${BOLD}$actual${RESET}, but 2 lint + 1 sdcard-provision + ${#ORDERED_TESTS[@]} screenshot + ${#DECLARED_FUNC[@]} functional = ${BOLD}$expected${RESET} were declared")
    if [[ ${#faults[@]} -gt 0 ]]; then
        harness_fault "${faults[@]}" "" \
            "The suite did not run what it says it ran. Treat this as RED, not as a pass."
    fi
    echo -e "  ${BOLD}$actual/$expected declared tests reported${RESET}"
fi

if [[ $fail -gt 0 ]]; then
    exit 1
fi
exit 0
