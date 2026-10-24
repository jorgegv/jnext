#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #402 (owner, 2026-10-23): ONE fast/real-time setting, the Tape > Fast Load
# toggle. Tape > Open, File > Load, --load of a .tap/.tzx and a .jns restore all
# read it; --tape-realtime sets it for the session only and wins over the saved
# preference (the same CLI-over-saved merge as --tape-auto-play), and is never
# written back. Before this the Qt GUI had two: Tape > Open followed the toggle,
# while File > Load and --load followed --tape-realtime and ignored the toggle's
# saved value. The SDL frontend has no toggle and no preferences: the flag is its
# whole setting, and a .jns restore reads it too.
#
# The oracle is the loader's own log line, "attached ... mode: fast|realtime"
# (TAP) and "reattached (TAP, fast|realtime, ..." (a .jns), read per frontend:
#   Qt   saved preference tape_fast_load=false, no flag      -> realtime
#   Qt   saved preference tape_fast_load=true,  no flag      -> fast
#   Qt   saved true, --tape-realtime                         -> realtime, and the
#        preference file still says true afterwards (not persisted)
#   Qt   a .jns of a fast tape, saved preference false       -> restored converted to real time
#   SDL  no flag -> fast, --tape-realtime -> realtime, for a .tap and for a .jns
if want tape-mode-unified-func; then
    begin_func tape-mode-unified-func
    tm_dir="$TMP_DIR/tm"
    mkdir -p "$tm_dir"
    tm_tap="$PROJECT_DIR/test/00regression/tap/slow_gap.tap"
    tm_faults=()
    tm_conf="$JNEXT_CONFIG_DIR/jnext.conf"

    # tm_conf_set <true|false>: the saved preference.
    tm_conf_set() { printf '[startup]\ntape_fast_load=%s\n' "$1" > "$tm_conf"; }
    # tm_qt <log> <args...>; tm_sdl <log> <args...>
    tm_qt() { local log=$1; shift
        QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s "$JNEXT" --silent --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" --delayed-automatic-exit-frames 40 >"$log" 2>&1; }
    tm_sdl() { local log=$1; shift
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 120s "$JNEXT_SDL" --silent --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" --delayed-automatic-exit-frames 40 >"$log" 2>&1; }
    # tm_expect <log> <mode> <what>
    tm_expect() { grep -qE "tape attached.*mode: $2|reattached \(TAP, $2" "$1" || tm_faults+=("$3: not $2"); }

    # --- a .jns of a FAST tape, from the SDL frontend (no preferences) ---
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" \
        --rewind-buffer-size 0 --load "$tm_tap" --delayed-snapshot "$tm_dir/fast.jns" \
        --delayed-snapshot-frames 20 --delayed-automatic-exit-frames 22 >"$tm_dir/mk.log" 2>&1 \
        || tm_faults+=("could not make the fast snapshot")
    # the SDL frontend: the flag is the setting
    tm_sdl "$tm_dir/sdl-def.log" --load "$tm_tap" || tm_faults+=("sdl default run failed")
    tm_expect "$tm_dir/sdl-def.log" fast "SDL .tap without the flag"
    tm_sdl "$tm_dir/sdl-rt.log" --tape-realtime --load "$tm_tap" || tm_faults+=("sdl realtime run failed")
    tm_expect "$tm_dir/sdl-rt.log" realtime "SDL .tap with --tape-realtime"
    tm_sdl "$tm_dir/sdl-jns-def.log" --load "$tm_dir/fast.jns" || tm_faults+=("sdl jns run failed")
    tm_expect "$tm_dir/sdl-jns-def.log" fast "SDL .jns without the flag"
    tm_sdl "$tm_dir/sdl-jns-rt.log" --tape-realtime --load "$tm_dir/fast.jns" || tm_faults+=("sdl jns realtime run failed")
    tm_expect "$tm_dir/sdl-jns-rt.log" realtime "SDL .jns with --tape-realtime (a fast tape converted to the session's mode)"

    # the Qt GUI: the saved toggle is the setting
    tm_conf_set false
    tm_qt "$tm_dir/qt-pref-rt.log" --load "$tm_tap" || tm_faults+=("qt run failed")
    tm_expect "$tm_dir/qt-pref-rt.log" realtime "Qt .tap, saved Fast Load off, no flag"
    tm_qt "$tm_dir/qt-pref-rt-jns.log" --load "$tm_dir/fast.jns" || tm_faults+=("qt jns run failed")
    tm_expect "$tm_dir/qt-pref-rt-jns.log" realtime "Qt .jns, saved Fast Load off"
    tm_conf_set true
    tm_qt "$tm_dir/qt-pref-fast.log" --load "$tm_tap" || tm_faults+=("qt run failed")
    tm_expect "$tm_dir/qt-pref-fast.log" fast "Qt .tap, saved Fast Load on, no flag"
    tm_qt "$tm_dir/qt-cli.log" --tape-realtime --load "$tm_tap" || tm_faults+=("qt cli run failed")
    tm_expect "$tm_dir/qt-cli.log" realtime "Qt .tap, saved Fast Load on, --tape-realtime"
    grep -qE "tape_fast_load=true" "$tm_conf" || tm_faults+=("--tape-realtime was written to the preferences: $(cat "$tm_conf")")

    if [[ ${#tm_faults[@]} -eq 0 ]]; then
        pass_row " (one setting: Qt follows the saved toggle for .tap and .jns, --tape-realtime wins for the session and is not saved; SDL follows the flag for .tap and .jns)"
    else
        fail_row " ($(IFS=';'; echo "${tm_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
