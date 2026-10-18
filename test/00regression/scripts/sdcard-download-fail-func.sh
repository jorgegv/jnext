#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable - the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdcard-download-fail-func: a failed SD-card download names the address that
# failed, says why in plain words and what happens next, in every frontend
# (GH #385, reported by @Nitrowing in #384: "show the URL ... so it can be
# checked by other means", "show a clear reason for the failure").
#
# WHY A FUNCTIONAL ROW. The message itself is pinned by sdcard_provisioner_test
# (PROV-MSG-*, PROV-REASON-*) and the dialog's wiring by sdcard_dialog_test, but
# only the real binary shows that main.cpp hands the message to the places a
# user looks: stderr AND the log file in a headless run and in the SDL-only
# build (which logged nothing before: the failure went to stderr alone), and the
# log plus the warning dialog in the Qt GUI. The dialog itself cannot be shown on
# the offscreen platform (nobody could dismiss it), so SdcardGuiProvisioner::warn()
# writes the title and the text it WOULD have shown to the platform log at debug
# level; that line is what the third run reads.
#
# THE SCENARIO needs no network and no SD image: the distribution URL
# ($JNEXT_SDCARD_DISTRO_URL, the provisioner's test seam) is a port nothing
# listens on, so the connection is refused at once, and the run's config directory
# is empty, so there is nothing to boot and the download is attempted. The
# download is confirmed on the command line.
#   1. headless: exit 1; stderr and the log file both carry the reason in plain
#      words, the address, and that nothing was kept;
#   2. the SDL-only build: the same;
#   3. the Qt GUI on the offscreen platform: exit 1, the log carries the error and
#      the dialog's text with the address;
#   4. the opposite side: the user DECLINES the download (no --sdcard-download-confirm,
#      stdin closed, so the question is answered No): exit 1, stderr says so as it
#      always did, no download is attempted, and the log carries no SD-card error,
#      because declining is the user's own answer and needs no report (main.cpp).
#   5. a server that is reachable and SLOW (slow-http-peer.py answers the zip's request with
#      a 404 after 8 s; the backend gives up after 30 s, so the answer comes first): with no
#      dialog to show it, the console and the log say what the GUI's dialog would, the stalled
#      note, 5 s in and before the failure it ends in. Run for both frontends that have no
#      dialog, headless and the SDL-only build: each has its own wiring in main.cpp.
# The text is matched with the carriage returns removed: spdlog writes CRLF on
# Windows, to the log file as well.
if want sdcard-download-fail-func; then
    begin_func sdcard-download-fail-func

    sf_dir="$TMP_DIR/sdcard-download-fail"
    rm -rf "$sf_dir"
    mkdir -p "$sf_dir"
    sf_url="http://127.0.0.1:1/sn-emulator.zip"
    sf_fails=()

    # sf_run <name> <extra env...> -- <jnext> <args...>: one run on a config
    # directory of its own (empty: no SD image), stdin closed (a question is
    # answered No), output in $sf_dir/<name>.out (stdout and stderr, CR removed),
    # the log in $sf_dir/<name>.log (CR removed); prints the exit status. The
    # download is confirmed by the caller, with --sdcard-download-confirm.
    sf_run() {
        local name=$1 rc=0
        shift
        local -a sf_env=()
        while [[ $1 != -- ]]; do sf_env+=("$1"); shift; done
        shift
        rm -rf "$sf_dir/$name-cfg" "$sf_dir/$name.out" "$sf_dir/$name.log"
        mkdir -p "$sf_dir/$name-cfg"
        timeout --foreground --kill-after=5s 60s \
            env JNEXT_CONFIG_DIR="$sf_dir/$name-cfg" JNEXT_SDCARD_DISTRO_URL="$sf_url" \
            no_proxy=127.0.0.1,localhost NO_PROXY=127.0.0.1,localhost "${sf_env[@]}" \
            "$@" --log-file "$sf_dir/$name.log" \
            </dev/null >"$sf_dir/$name.out" 2>&1 || rc=$?
        # Windows writes CRLF; the match is on the text.
        tr -d '\r' <"$sf_dir/$name.out" >"$sf_dir/$name.out.lf"
        if [[ -f "$sf_dir/$name.log" ]]; then
            tr -d '\r' <"$sf_dir/$name.log" >"$sf_dir/$name.log.lf"
        else
            : >"$sf_dir/$name.log.lf"
        fi
        echo "$rc"
    }
    # sf_has <name> <out|log> <fixed text> <what>: the text is in that output.
    sf_has() {
        grep -qF -- "$3" "$sf_dir/$1.$2.lf" || sf_fails+=("$1: $4 is not in the $2 ('$3')")
    }
    # sf_once <name>: the failure is logged ONCE (the Qt GUI logs it itself and must not log it again).
    sf_once() {
        local n
        n=$(grep -cF '[error] SD-card image: ' "$sf_dir/$1.log.lf" || true)
        [[ "$n" == 1 ]] || sf_fails+=("$1: the failure is logged $n times in the log, want once")
    }

    # sf_absent <name> <out|log> <fixed text> <what>: the text is NOT in that output.
    sf_absent() {
        ! grep -qF -- "$3" "$sf_dir/$1.$2.lf" || sf_fails+=("$1: $4 is in the $2 ('$3')")
    }
    # sf_not_logged <name>: nothing was logged as an SD-card error, in a log that is not empty
    # (a log file that was never written would pass the count without proving anything).
    sf_not_logged() {
        local n
        n=$(grep -cF '[error] SD-card image: ' "$sf_dir/$1.log.lf" || true)
        [[ "$n" == 0 ]] || sf_fails+=("$1: $n SD-card errors in the log, want none")
        [[ -s "$sf_dir/$1.log.lf" ]] || sf_fails+=("$1: the log file is empty or missing")
    }

    # sf_re <name> <out|log> <ERE> <what>: a line of that output matches.
    sf_re() {
        grep -qE -- "$3" "$sf_dir/$1.$2.lf" || sf_fails+=("$1: $4 is not in the $2 (/$3/)")
    }
    # sf_before <name> <ERE a> <ERE b> <what>: in the output, a line matching a comes before one matching b.
    sf_before() {
        local la lb
        la=$(grep -nE -- "$2" "$sf_dir/$1.out.lf" | head -n 1 | cut -d: -f1 || true)
        lb=$(grep -nE -- "$3" "$sf_dir/$1.out.lf" | head -n 1 | cut -d: -f1 || true)
        [[ -n "$la" && -n "$lb" && "$la" -lt "$lb" ]] \
            || sf_fails+=("$1: $4 (lines '${la:-none}' and '${lb:-none}')")
    }

    sf_reason='download failed: could not connect to the server ('
    sf_url_line="  URL: $sf_url"
    sf_next='  nothing was kept, so the next start downloads it from the beginning'

    # 1. headless
    rc=$(sf_run headless -- "$JNEXT" --headless --sdcard-download-confirm)
    [[ "$rc" == 1 ]] || sf_fails+=("headless exited $rc, want 1")
    sf_has headless out "error: SD-card image: $sf_reason" "the reason in plain words"
    sf_has headless out "$sf_url_line" "the address"
    sf_has headless out "$sf_next" "what happens next"
    sf_has headless log "SD-card image: $sf_reason" "the reason in plain words"
    sf_has headless log "$sf_url_line" "the address"
    sf_has headless log "$sf_next" "what happens next"
    sf_once headless

    # 2. the SDL-only build
    if [[ ! -x "$JNEXT_SDL" ]]; then
        sf_fails+=("SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release'")
    else
        rc=$(sf_run sdl SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy -- "$JNEXT_SDL" --sdcard-download-confirm)
        [[ "$rc" == 1 ]] || sf_fails+=("the SDL-only build exited $rc, want 1")
        sf_has sdl out "error: SD-card image: $sf_reason" "the reason in plain words"
        sf_has sdl out "$sf_url_line" "the address"
        sf_has sdl out "$sf_next" "what happens next"
        sf_has sdl log "SD-card image: $sf_reason" "the reason in plain words"
        sf_has sdl log "$sf_url_line" "the address"
        sf_has sdl log "$sf_next" "what happens next"
        sf_once sdl
    fi

    # 3. the Qt GUI, offscreen: the dialog is not shown, its text is logged at debug level
    rc=$(sf_run qt QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy -- "$JNEXT" --log-level platform=debug --sdcard-download-confirm)
    [[ "$rc" == 1 ]] || sf_fails+=("the Qt GUI exited $rc, want 1")
    sf_has qt out "error: SD-card image: $sf_reason" "the reason in plain words"
    sf_has qt log "SD-card image: $sf_reason" "the reason in plain words"
    sf_has qt log "dialog not shown on the offscreen platform: jnext - SD card image: The SD-card image could not be installed:" "the dialog's title and text"
    sf_has qt log "$sf_url_line" "the address"
    sf_has qt log "$sf_next" "what happens next"
    sf_once qt

    # 4. declined: the question is answered No (stdin is closed), so nothing is downloaded and
    # nothing is a failure to report. stderr keeps saying so; the log gets no SD-card error.
    rc=$(sf_run declined -- "$JNEXT" --headless)
    [[ "$rc" == 1 ]] || sf_fails+=("the declined run exited $rc, want 1")
    sf_has declined out "error: SD-card image: download declined by user" "the decline"
    sf_absent declined out "download failed" "a failed download"
    sf_not_logged declined

    # 5. a slow server. The peer is bounded (it ends by itself after 60 s); no trap: the suite
    # library owns the one EXIT handler.
    sf_port_file="$sf_dir/slow.port"
    sf_peer_log="$sf_dir/slow.peer.log"
    python3 "$SCRIPT_DIR/slow-http-peer.py" "$sf_port_file" 8 404 >"$sf_peer_log" 2>&1 &
    sf_peer=$!
    for _ in $(seq 1 100); do
        [[ -s "$sf_port_file" ]] && break
        kill -0 "$sf_peer" 2>/dev/null || break
        sleep 0.1
    done
    if [[ ! -s "$sf_port_file" ]]; then
        sf_fails+=("the slow server never became ready: $(tr -d '\n' <"$sf_peer_log")")
    else
        sf_url="http://127.0.0.1:$(tr -d '\n' <"$sf_port_file")/sn-emulator.zip"
        sf_stalled='Stalled: no data received for [0-9]+ s\. The network or the server may be down; jnext gives up after 30 s\.'
        # sf_check_slow <name> <exit status>: the checks of one slow run.
        sf_check_slow() {
            [[ "$2" == 1 ]] || sf_fails+=("$1 exited $2, want 1")
            sf_re "$1" out "^$sf_stalled\$" "the stalled note, the dialog's words, on a line of its own"
            sf_re "$1" log "\[warning\] SD-card image: $sf_stalled" "the stalled note, as a warning"
            sf_has "$1" out "error: SD-card image: download failed: the server does not have this file (HTTP 404)" "the failure the wait ends in"
            sf_before "$1" "^$sf_stalled\$" "^error: SD-card image: download failed" "the stalled note comes before the failure it ends in"
        }
        rc=$(sf_run slowhl -- "$JNEXT" --headless --sdcard-download-confirm)
        sf_check_slow slowhl "$rc"
        if [[ -x "$JNEXT_SDL" ]]; then   # a missing binary was reported by run 2
            rc=$(sf_run slowsdl SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy -- "$JNEXT_SDL" --sdcard-download-confirm)
            sf_check_slow slowsdl "$rc"
        fi
    fi
    kill "$sf_peer" 2>/dev/null || true
    wait "$sf_peer" 2>/dev/null || true

    if [[ ${#sf_fails[@]} -eq 0 ]]; then
        pass_row " (a refused SD-card download exits 1 naming the address, the reason in plain words and that nothing was kept: on stderr and in the log file of a headless run and of the SDL-only build, and in the log and the dialog's text of the Qt GUI; a declined download exits 1 saying so on stderr and logs no SD-card error; a slow server's stall is said on the console and in the log of both frontends without a dialog)"
    else
        fail_row " (${sf_fails[*]})"
        for sf_n in headless sdl qt declined slowhl slowsdl; do
            [[ -f "$sf_dir/$sf_n.out.lf" ]] && show_output "$sf_n output" "$(cat "$sf_dir/$sf_n.out.lf")"
        done
    fi
    rm -rf "$sf_dir"
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
