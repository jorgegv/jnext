#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #555 (review of stage S1): a jnext the SUITES spawn must never fall back to the DEFAULT card,
# which is the user's own and is absent on a clean host. No literal lint can see that (the path is
# jnext's own default, never spelled out), so this is the guard for the CLASS: the suite that spawns
# jnext (cli_options_native_test) is run in a scratch HOME that holds the suites' test master and
#   A  a sentinel USER card (mode 000, fixed mtime: ANY open of it, even a read, fails the suite's own run), the suite run BARE as a hand run would: every row
#      passes, and the sentinel's bytes, mode, mtime and sidecars are unchanged afterwards;
#   B  NO user card at all, the same run: every row passes (a fallback to the default card would
#      fail 13 of 30 rows there);
#   C  no user card, through the real entry point (test/run-unit-tests.sh --only): every row passes;
# and the cli-check recipe still goes through that entry point (a bare run in the Makefile is the
# way this class came back once). Needs the built suite: regression depends on unit-test-build.
if want sdcard-no-default-card-func; then
    begin_func sdcard-no-default-card-func

    faults=()
    W="$RUN_DIR/private/sdcard-no-default-card"
    H="$W/home"
    SUITE="$PROJECT_DIR/build/test/cli_options_native_test"
    TM="$H/.jnext/sdcard/test-suite/sdcard/cspect-next-1gb-fixed.img"
    UC="$H/.jnext/sdcard/cspect-next-1gb-fixed.img"
    rm -rf "$W"; mkdir -p "$(dirname "$TM")" "$H/.jnext/runs"
    if [[ ! -x "$SUITE" ]]; then
        faults+=("$SUITE is not built (regression depends on unit-test-build)")
    else
        cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" "$TM"
        printf 'USER-CARD\n' > "$UC"; printf 'sn-emulator-24.11.zip\n' > "$UC.distro"; printf '1\n' > "$UC.recipe"
        chmod 0000 "$UC"; touch -d '2020-01-02 03:04:05' "$UC"
        sig() { stat -c '%s:%a:%Y' "$UC"; cat "$UC.distro" "$UC.recipe"; }
        before=$(sig)

        suite_run() {   # suite_run <label> <command...>: the suite's own Total line must read Failed: 0
            local label=$1 rc=0; shift
            env -u JNEXT_CONFIG_DIR -u JNEXT_TEST_SD_IMAGE -u JNEXT_REGRESSION_SD_STATE_DIR HOME="$H" \
                timeout --foreground --kill-after=5s 600s "$@" >"$W/$label.log" 2>&1 || rc=$?
            local line
            line=$(grep -E 'Total: +[0-9]+ +Passed: +[0-9]+ +Failed: +[0-9]+' "$W/$label.log" | tail -n 1 || true)
            if [[ $rc -ne 0 || ! "$line" =~ Failed:\ +0([^0-9]|$) || "$line" =~ Total:\ +0\  ]]; then
                local why
                why=$({ grep -h 'FAIL' "$W/$label.log" || true; } | head -n 3 | cut -c1-300 | tr '\n' ' ')
                faults+=("$label: exit $rc, '${line:-no Total line}': $why")
            fi
        }
        # A: a hand run, with a sentinel user card present
        suite_run bare-with-user-card "$SUITE"
        chmod 0400 "$UC"; uc_content=$(cat "$UC"); chmod 0000 "$UC"   # the row's own look, after the run
        [[ "$(sig)" == "$before" && "$uc_content" == USER-CARD ]] || faults+=("A: the user's card was opened, changed or replaced")
        # B: the same with no user card at all
        rm -f "$UC" "$UC.distro" "$UC.recipe"
        suite_run bare-no-user-card "$SUITE"
        [[ ! -e "$UC" ]] || faults+=("B: a user card appeared at $UC")
        # C: through the unit harness, the entry point cli-check uses
        suite_run harness-no-user-card bash "$PROJECT_DIR/test/run-unit-tests.sh" --only cli_options_native_test build
        [[ ! -e "$UC" ]] || faults+=("C: a user card appeared at $UC")
    fi
    # the cli-check recipe keeps its entry point
    recipe=$(sed -n '/^cli-check:/,/^[a-z][a-z-]*:/p' "$PROJECT_DIR/Makefile")
    if ! grep -qF 'run-unit-tests.sh --only cli_options_test,cli_options_native_test' <<<"$recipe"; then
        faults+=("the cli-check recipe no longer runs the CLI suites through test/run-unit-tests.sh --only")
    fi
    if grep -qE '^\s*@?\./build/test/cli_options_native_test' <<<"$recipe"; then
        faults+=("the cli-check recipe runs cli_options_native_test bare")
    fi

    rm -rf "$W"
    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (the suite that spawns jnext passes with and without a user card, bare and through the unit harness, and never touches a user card; cli-check keeps the harness entry point)"
    else
        fail_row " (${#faults[@]} fault(s): a spawned jnext reached the default card)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
