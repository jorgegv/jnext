#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# bug-report-nosd-func (GH #492): --bug-report on a --no-sdcard run, the one session in which
# RAM ships. No SD card is used at all.
#
#   A  headless --no-sdcard --load im1.nex --bug-report DIR/ exits 0 and writes exactly one
#      file, named PROGRAM.YYYYMMDDhhmm.jnext-bug.zip (local time)
#   B  the report has memory/ram.bin, manifest.memory.included = true, program/im1.nex equal
#      to the NEX, the pictures, machine.json and debugger.json, and no member contains the
#      home directory or the user name
#   C  --bug-report FILE (not a directory) writes exactly that file
#      and the viewer (tools/jnext-bug-view.py, python3 -I) shows that report: exit 0, the PC machine.json has
#   D  the Qt window (offscreen) and the SDL-only build (dummy drivers) call the same writer at
#      exit: each writes a report whose producer.frontend is qt / sdl, and only the Qt one carries the settings file (config_file_read)
if want bug-report-nosd-func; then
    begin_func bug-report-nosd-func

    br_dir="$TMP_DIR/reports"
    mkdir -p "$br_dir"
    python3 "$SCRIPT_DIR/no-sdcard-probe.py" "$TMP_DIR/im1.nex"
    check_py="$SCRIPT_DIR/bug-report-check.py"
    br_faults=()

    a_rc=0
    a_out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --no-sdcard \
        --load "$TMP_DIR/im1.nex" --bug-report "$br_dir/" --delayed-automatic-exit-frames 150 2>&1) || a_rc=$?
    [[ "$a_rc" -eq 0 ]] || br_faults+=("A: exit status $a_rc, want 0")
    mapfile -t br_files < <(find "$br_dir" -maxdepth 1 -type f -name '*.zip' -exec basename {} \;)
    [[ "${#br_files[@]}" -eq 1 ]] || br_faults+=("A: ${#br_files[@]} files in the directory, want 1")
    br_name="${br_files[0]:-none}"
    if ! [[ "$br_name" =~ ^im1\.[0-9]{12}\.jnext-bug\.zip$ ]]; then
        br_faults+=("A: file name '$br_name' is not im1.YYYYMMDDhhmm.jnext-bug.zip")
    fi

    if [[ -f "$br_dir/$br_name" ]]; then
        chk=$(python3 -I "$check_py" "$br_dir/$br_name" \
            --manifest kind jnext-bug-report --manifest format_version 1 \
            --manifest memory.included true --manifest machine next \
            --manifest producer.frontend headless \
            --has memory/ram.bin --has memory/cpu.bin --has machine.json --has debugger.json \
            --has screen/composite.png --has screen/layer2-active.png --has log.txt \
            --equals program/im1.nex "$TMP_DIR/im1.nex" \
            --viewer "$PROJECT_DIR/tools/jnext-bug-view.py" \
            --forbid "$HOME" --forbid "$(id -un)" 2>&1) || br_faults+=("B: $chk")
    else
        br_faults+=("B: no report file to check")
    fi

    c_rc=0
    timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --no-sdcard \
        --load "$TMP_DIR/im1.nex" --bug-report "$TMP_DIR/exact.zip" --delayed-automatic-exit-frames 60 \
        >/dev/null 2>&1 || c_rc=$?
    [[ "$c_rc" -eq 0 && -s "$TMP_DIR/exact.zip" ]] || br_faults+=("C: --bug-report FILE exit $c_rc, file missing or empty")

    # D: the other two loop owners.
    for fe in qt sdl; do
        fe_dir="$TMP_DIR/fe-$fe"
        mkdir -p "$fe_dir"
        fe_rc=0
        if [[ "$fe" == qt ]]; then
            env QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" --silent --no-sdcard \
                --load "$TMP_DIR/im1.nex" --bug-report "$fe_dir/" --delayed-automatic-exit-frames 60 \
                >/dev/null 2>&1 || fe_rc=$?
        elif [[ -x "$JNEXT_SDL" ]]; then
            env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 60s "$JNEXT_SDL" --silent --no-sdcard \
                --load "$TMP_DIR/im1.nex" --bug-report "$fe_dir/" --delayed-automatic-exit-frames 60 \
                >/dev/null 2>&1 || fe_rc=$?
        else
            br_faults+=("D: SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release'")
            continue
        fi
        mapfile -t fe_files < <(find "$fe_dir" -maxdepth 1 -type f -name '*.jnext-bug.zip')
        if [[ "$fe_rc" -ne 0 || "${#fe_files[@]}" -ne 1 ]]; then
            br_faults+=("D: $fe exit $fe_rc, ${#fe_files[@]} report files, want 0 and 1")
        else
            # The Qt window reads jnext.conf and says so; the SDL-only build reads none.
            want_read=false; [[ "$fe" == qt ]] && want_read=true
            chk=$(python3 -I "$check_py" "${fe_files[0]}" --manifest producer.frontend "$fe" \
                --manifest memory.included true --has memory/ram.bin \
                --json settings.json config_file_read "$want_read" 2>&1) || br_faults+=("D: $fe: $chk")
        fi
    done

    if [[ "${#br_faults[@]}" -eq 0 ]]; then
        pass_row " (named PROGRAM.time.jnext-bug.zip in the directory, RAM and the program shipped, nothing of the host in it, an exact file name honoured, the Qt and SDL owners write one too)"
    else
        fail_row " (${br_faults[*]})"
        show_output "jnext" "$a_out"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
