#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable -- the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# coverage-frontends-func (GH #388): --coverage starts memory coverage in EVERY
# loop owner. The three owners each call apply_startup_coverage() after hosting the
# debugger backend (platform/coverage_startup.h) and log_coverage_summary() at exit;
# cli_options_native_test CLI-COV-01 and debugger_backend_test HOST-COV-01 reach only the
# headless one, so a Qt or SDL owner that forgot the call passed everything.
#
# THE MEASUREMENT. Each frontend runs 20 frames with --coverage and must log the
# start line and the exit summary, with executed bytes (a boot ROM that ran), and
# the SAME run without the flag must log neither. The frontends are driven as
# auto-exit-deferred-func drives them (Qt offscreen, SDL dummy drivers).
if want coverage-frontends-func; then
    begin_func coverage-frontends-func

    cf_dir="$TMP_DIR/coverage-frontends"
    rm -rf "$cf_dir"; mkdir -p "$cf_dir"
    cf_sdl="$JNEXT_SDL"
    cf_faults=()

    # cf_run <frontend> <tag> <jnext args...>; the log goes to $cf_dir/<tag>.log
    cf_run() {
        local fe=$1 tag=$2; shift 2
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" \
                    --delayed-automatic-exit-frames 20 >"$cf_dir/$tag.log" 2>&1 || true ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 60s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" \
                    --delayed-automatic-exit-frames 20 >"$cf_dir/$tag.log" 2>&1 || true ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 60s "$cf_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" \
                    --delayed-automatic-exit-frames 20 >"$cf_dir/$tag.log" 2>&1 || true ;;
        esac
    }

    if [[ ! -x "$cf_sdl" ]]; then
        fail_row " (SDL-only binary not built: $cf_sdl; run 'make sdl-release')"
    else
        for fe in headless qt sdl; do
            cf_run "$fe" "$fe-on" --coverage
            cf_run "$fe" "$fe-off"
            grep -qF "Memory coverage recording: on" "$cf_dir/$fe-on.log" \
                || cf_faults+=("$fe: no start line with --coverage")
            grep -qE "Memory coverage at exit: [1-9][0-9]* space\(s\), [1-9][0-9]* byte\(s\) executed" "$cf_dir/$fe-on.log" \
                || cf_faults+=("$fe: no exit summary with executed bytes with --coverage")
            if grep -qF "Memory coverage" "$cf_dir/$fe-off.log"; then
                cf_faults+=("$fe: coverage lines without --coverage")
            fi
        done
        if [[ ${#cf_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${cf_faults[*]}"))"
        else
            pass_row " (--coverage logs its start line and an exit summary with executed bytes in headless, Qt and SDL, and nothing without it)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
