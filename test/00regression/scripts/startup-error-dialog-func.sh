#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #555 - a start-up error a desktop user can see. A launch from a desktop file has no
# terminal, so an error that only reaches stderr looks like jnext doing nothing. A windowed,
# attended run that fails before its loop starts shows the reason in the frontend's message box
# (stderr is unchanged). Nobody can dismiss a box on the offscreen platforms, so there the box
# logs a debug line instead (the SD download failure's existing precedent), which is what this
# row reads: "dialog not shown on the offscreen platform: jnext could not start: <the reason>".
#
#   qt, sdl  an unknown option and a refused option combination (--sdcard-upgrade with --sdcard): exit 1,
#            the box line carries the first line jnext printed on stderr. The SDL build with no card
#            and no answer to the download question fails the same way (the reason, "download declined").
#   none     --headless, an unattended run (--delayed-automatic-exit) and a terminal command
#            (--sdcard-file-add) show no box: a script never blocks on one.
# Oracle: the issue (errors in a dialog, "warnings only when about to exit") and its plan.
if want startup-error-dialog-func; then
    begin_func startup-error-dialog-func

    se_dir="$TMP_DIR/startup-error-dialog"
    rm -rf "$se_dir"
    mkdir -p "$se_dir"
    se_fails=()
    se_box='dialog not shown on the offscreen platform: jnext could not start: '

    # se_run <name> <extra env...> -- <jnext> <args...>: a config dir of its own (no card), stdin
    # closed; output in $se_dir/<name>.out, the log in $se_dir/<name>.log (CRs removed); prints the status.
    se_run() {
        local name=$1 rc=0
        shift
        local -a se_env=()
        while [[ $1 != -- ]]; do se_env+=("$1"); shift; done
        shift
        local se_bin=$1
        shift
        mkdir -p "$se_dir/$name-cfg"
        # --log-level comes first: an unknown option ends the parse loop before a later one is read.
        timeout --foreground --kill-after=5s 60s \
            env JNEXT_CONFIG_DIR="$se_dir/$name-cfg" "${se_env[@]}" \
            "$se_bin" --log-level platform=debug --log-file "$se_dir/$name.log" "$@" \
            </dev/null >"$se_dir/$name.out" 2>&1 || rc=$?
        tr -d '\r' <"$se_dir/$name.out" >"$se_dir/$name.out.lf"
        if [[ -f "$se_dir/$name.log" ]]; then tr -d '\r' <"$se_dir/$name.log" >"$se_dir/$name.log.lf"; else : >"$se_dir/$name.log.lf"; fi
        echo "$rc"
    }
    # se_box_has <name> <fixed text>: the box line is in the log and carries the text.
    se_box_has() {
        local line
        line=$(grep -F -- "$se_box" "$se_dir/$1.log.lf" | head -n 1 || true)
        [[ -n "$line" ]] || { se_fails+=("$1: no box line in the log; stderr: $(head -c 200 "$se_dir/$1.out.lf" | tr '\n' ' ') | log: $(tail -n 3 "$se_dir/$1.log.lf" | cut -c1-200 | tr '\n' ' ')"); return 0; }
        [[ "$line" == *"$2"* ]] || se_fails+=("$1: the box line does not carry '$2': $line")
    }
    se_no_box() {
        ! grep -qF -- "$se_box" "$se_dir/$1.log.lf" || se_fails+=("$1: a box was raised")
        [[ -s "$se_dir/$1.log.lf" ]] || se_fails+=("$1: the log is empty or missing (it proves nothing)")
    }
    se_stderr_has() {   # <name> <fixed text>: stderr kept the message (unchanged)
        grep -qF -- "$2" "$se_dir/$1.out.lf" || se_fails+=("$1: stderr does not carry '$2'")
    }

    frontends=("qt|QT_QPA_PLATFORM=offscreen|$JNEXT")
    if [[ -x "${JNEXT_SDL:-}" ]]; then
        frontends+=("sdl|SDL_VIDEODRIVER=dummy|$JNEXT_SDL")
    else
        se_fails+=("the SDL-only binary is not built: ${JNEXT_SDL:-unset}; run 'make sdl-release'")
    fi
    for fe in "${frontends[@]}"; do
        IFS='|' read -r fe_name fe_env fe_bin <<<"$fe"
        # an unknown option
        rc=$(se_run "$fe_name-opt" "$fe_env" SDL_AUDIODRIVER=dummy -- "$fe_bin" --bogus-option)
        [[ "$rc" == 1 ]] || se_fails+=("$fe_name-opt: exit $rc, want 1")
        se_stderr_has "$fe_name-opt" "Unknown option"
        se_box_has "$fe_name-opt" "Unknown option"
        # a refused option combination (past the parse loop: a validation, not a usage error)
        printf junk > "$se_dir/junk.img"
        rc=$(se_run "$fe_name-sd" "$fe_env" SDL_AUDIODRIVER=dummy -- "$fe_bin" --sdcard-upgrade yes --sdcard "$se_dir/junk.img")
        [[ "$rc" == 1 ]] || se_fails+=("$fe_name-sd: exit $rc, want 1")
        se_box_has "$fe_name-sd" "--sdcard-upgrade cannot be used with --sdcard"
    done

    # the SDL build with no card at all and a question nobody answers (stdin closed): a failure the
    # logger/stderr carry, not a parse error
    if [[ -x "${JNEXT_SDL:-}" ]]; then
        rc=$(se_run sdl-nocard SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy -- "$JNEXT_SDL")
        [[ "$rc" == 1 ]] || se_fails+=("sdl-nocard: exit $rc, want 1")
        se_box_has sdl-nocard "download declined"
    fi

    # headless, unattended, terminal command: no box, for the same errors
    rc=$(se_run headless "QT_QPA_PLATFORM=offscreen" SDL_AUDIODRIVER=dummy -- "$JNEXT" --headless --bogus-option)
    [[ "$rc" == 1 ]] || se_fails+=("headless: exit $rc, want 1")
    se_no_box headless
    rc=$(se_run unattended "QT_QPA_PLATFORM=offscreen" SDL_AUDIODRIVER=dummy -- "$JNEXT" --delayed-automatic-exit 1 --bogus-option)
    [[ "$rc" == 1 ]] || se_fails+=("unattended: exit $rc, want 1")
    se_no_box unattended
    rc=$(se_run fileadd "QT_QPA_PLATFORM=offscreen" SDL_AUDIODRIVER=dummy -- "$JNEXT" --sdcard "$se_dir/no-such-card.img" --sdcard-file-add "$se_dir/no-such-file.bin")
    [[ "$rc" != 0 ]] || se_fails+=("fileadd: exit 0, want a failure")
    se_no_box fileadd

    # the Qt SD download failure has a dialog of its own: exactly that one, the start-up box must not
    # stack a second on it (GH #555 review S3-1: startup_report::note_dialog_shown)
    rc=$(se_run qt-dl QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy JNEXT_SDCARD_DISTRO_URL=file:///nonexistent/x.zip -- "$JNEXT" --sdcard-download-confirm)
    [[ "$rc" == 1 ]] || se_fails+=("qt-dl: exit $rc, want 1")
    dl_n=$(grep -cF 'dialog not shown on the offscreen platform' "$se_dir/qt-dl.log.lf" || true)
    dl_box=$(grep -cF -- "$se_box" "$se_dir/qt-dl.log.lf" || true)
    [[ "$dl_n" == 1 && "$dl_box" == 0 ]] || se_fails+=("qt-dl: $dl_n dialog line(s) and $dl_box start-up box line(s), want 1 and 0 (the box stacked on the download dialog)")

    # stderr a TERMINAL (a pty): the box is shown all the same (owner, 2026-11-23)
    mkdir -p "$se_dir/tty-cfg"
    rc=0
    timeout --foreground --kill-after=5s 60s \
        env JNEXT_CONFIG_DIR="$se_dir/tty-cfg" QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
        python3 -I -c 'import os, pty, sys; sys.exit(os.waitstatus_to_exitcode(pty.spawn(sys.argv[1:])))' \
        "$JNEXT" --log-level platform=debug --log-file "$se_dir/tty.log" --bogus-option \
        </dev/null >"$se_dir/tty.out" 2>&1 || rc=$?
    if [[ -f "$se_dir/tty.log" ]]; then tr -d '\r' <"$se_dir/tty.log" >"$se_dir/tty.log.lf"; else : >"$se_dir/tty.log.lf"; fi
    [[ "$rc" == 1 ]] || se_fails+=("tty: exit $rc, want 1")
    se_box_has tty "Unknown option"

    if [[ ${#se_fails[@]} -eq 0 ]]; then
        pass_row " (an unknown option and a refused option combination raise the start-up box with the reason on Qt and SDL; also with stderr on a terminal; headless, unattended and --sdcard-file-add raise none; stderr keeps the message)"
    else
        fail_row " (${#se_fails[@]} fault(s) in the start-up error box)"
        printf '      %s\n' "${se_fails[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
