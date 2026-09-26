#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-play-route-func: every way of starting RZX playback starts from the same
# machine, so one recording replays identically whichever way it is played.
#
# WHY THIS ROW EXISTS. Starting a playback is `init()` followed by
# Emulator::load_rzx(), and EmulatorConfig::load_file changes what init()
# builds: on the Next an empty one arms the boot-ROM overlay. --load x.rzx, a
# bare x.rzx, a cold-boot load (the route the GUI's File > Open takes) set it;
# --rzx-play did not, and the GUI's Play RZX item played on the running
# machine with no reboot at all. A Next program that runs through the ROM then
# replayed its recording under the boot ROM — a different picture from the
# same file. rzx-frontends-func could not see it: it records a 48K machine,
# where load_file changes nothing.
#
# GH #274 — THIS ROW RUNS ITS MATRIX TWICE, on a 128K recording and on a
# NEXT-TYPED one, and the second is the one that guards the original bug.
#
# A first cut of #274 moved the row to the 128K and retired the Next case on the
# grounds that it was "unreachable rather than untested". THAT WAS WRONG, and
# the way it was wrong is worth keeping. Recording on a Next is refused now —
# true — but PLAYING a Next-typed recording is still supported, deliberately, by
# the same change (older jnext versions wrote them, so load_rzx() warns and
# plays). The combination the bug lived in is therefore still live and still
# user-reachable; retiring the case removed the coverage rather than following
# the code.
#
# It cannot be reached from a 128K or a 48K, either. The gate is Next-only:
#
#     src/core/emulator.cpp — if (cfg.type == MachineType::ZXN_ISSUE2 &&
#                                 !cfg.sd_card_image.empty() &&
#                                 cfg.load_file.empty())  -> boot-ROM overlay
#
# and the fix it guards is in main.cpp: `if (cfg.load_file.empty()) cfg.load_file
# = rzx_play_file;`, whose own comment records the symptom — "--rzx-play ... used
# to leave load_file empty, which on the Next armed the boot-ROM overlay that a
# --load skips — the same recording then replayed differently depending on how it
# was named on the command line". Only a Next-typed recording driven through more
# than one route can see that.
#
# THE NEXT-TYPED FIXTURE is built by patching the 128K recording's machine
# marker, `machine=128k` -> `machine=next`: the same length, so it is a 12-byte
# in-place substitution with no block-length or offset to fix, and it needs
# nothing beyond coreutils. jnext can no longer record one, and this is what a
# recording an older jnext wrote looks like to `rzx::recorded_machine()`, which
# is what the routing depends on.
#
# THE MEASUREMENT. A 128K session of a program loaded from a snapshot is
# recorded headless and screenshotted at frame 100: the truth. Then
# each of these must give a picture identical to it at the same point of the
# replay (png_diff 0):
#   --rzx-play, --load x.rzx, bare x.rzx     in headless, the Qt GUI and SDL
#   a cold-boot load of x.rzx after 5 frames (headless; the shared cold boot
#                                             the GUI's menu items use)
# and the truth must DIFFER from a plain Next boot at frame 100, or the
# equalities would prove nothing. (The GUI menu items reaching that cold boot
# is rzx_menu_test RZXGUI-10's business.)
if want rzx-play-route-func; then
    begin_func rzx-play-route-func

    pr_dir="$TMP_DIR/rzx-play-route"
    rm -rf "$pr_dir"; mkdir -p "$pr_dir"
    pr_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    # The loaded program is a 128K .szx of a running session, built below: the
    # row needs a picture that is NOT a bare boot, and a NEX cannot load on the
    # 128K this row moved to.
    pr_prog="$pr_dir/session.szx"
    pr_rzx="$pr_dir/session.rzx"
    pr_faults=()

    # pr_run <frontend> <tag> <frames> <jnext args...>: one 128K run that
    # screenshots <dir>/<tag>.png after <frames> frames; prints the status.
    pr_run() {
        local fe=$1 tag=$2 n=$3 rc=0; shift 3
        local -a shot=(--delayed-screenshot "$pr_dir/$tag.png" --delayed-screenshot-frames "$n"
                       --delayed-automatic-exit-frames "$((n + 1))")
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine 128k "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 128k "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$pr_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 128k "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }

    # pr_auto <tag> <frames> <jnext args...>: headless, with NO --machine, so
    # the RECORDING decides the machine — which is the whole point of the
    # Next-typed matrix below. pr_run forces --machine 128k, and an explicit
    # --machine deliberately WINS over the recording, so it cannot be used here.
    pr_auto() {
        local tag=$1 n=$2 rc=0; shift 2
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" "$@" \
            --delayed-screenshot "$pr_dir/$tag.png" --delayed-screenshot-frames "$n" \
            --delayed-automatic-exit-frames "$((n + 1))" \
            >"$pr_dir/$tag.log" 2>&1 || rc=$?
        echo "$rc"
    }
    # pr_same <tag> <rc>: the run exited 0 and its picture is the truth.
    pr_same() {
        local d
        [[ "$2" == 0 ]] || { pr_faults+=("$1: rc=$2"); return; }
        d=$(png_diff "$pr_dir/$1.png" "$pr_dir/truth.png")
        [[ "$d" == 0 ]] || pr_faults+=("$1: differs from the recording (png_diff=$d)")
    }

    if [[ ! -x "$pr_sdl" ]]; then
        fail_row " (SDL-only binary not built: $pr_sdl; run 'make sdl-release')"
    elif ! $HAS_COMPARE; then
        skip_row " (no ImageMagick — cannot compare the replayed pictures)"
    else
        # Fixture: a 128K mid-run .szx. The injected program (DI; LD BC,7FFD;
        # LD A,1F; OUT (C),A — bank 7 at 0xC000, ROM 1, shadow screen shown;
        # fill the shadow screen with 0x47; EI; HALT; JR -3) gives a picture no
        # plain boot produces, and a .szx carries the paging that makes it.
        printf '\xf3\x01\xfd\x7f\x3e\x1f\xed\x79\x21\x00\xc0\x11\x01\xc0\x01\xff\x1a\x36\x47\xed\xb0\xfb\x76\x18\xfd' \
            > "$pr_dir/prog.bin"
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine 128k \
            "${SD_CARD_ARGS[@]}" --inject "$pr_dir/prog.bin" --inject-delay 100 \
            --delayed-snapshot "$pr_prog" --delayed-snapshot-frames 150 \
            --delayed-automatic-exit-frames 151 >"$pr_dir/mk-szx.log" 2>&1 || true
        if [[ ! -s "$pr_prog" ]]; then
            fail_row " (could not build the 128K .szx fixture)"
            return 2>/dev/null || true
        fi
        rc=$(pr_run headless truth 100 --load "$pr_prog" --rzx-record "$pr_rzx")
        rc_boot=$(pr_run headless boot 100)
        if [[ "$rc" != 0 || ! -s "$pr_rzx" ]]; then
            fail_row " (could not record the ground truth: rc=$rc)"
        else
            c=$(png_diff "$pr_dir/truth.png" "$pr_dir/boot.png")
            [[ "$rc_boot" == 0 && "$c" -gt 0 && "$c" -lt 999999 ]] \
                || pr_faults+=("the truth equals a plain 128K boot (png_diff=$c, rc=$rc_boot) — proves nothing")
            for fe in headless qt sdl; do
                pr_same "$fe-rzx-play" "$(pr_run "$fe" "$fe-rzx-play" 100 --rzx-play "$pr_rzx")"
                pr_same "$fe-load"     "$(pr_run "$fe" "$fe-load" 100 --load "$pr_rzx")"
                pr_same "$fe-bare"     "$(pr_run "$fe" "$fe-bare" 100 "$pr_rzx")"
            done
            # The cold boot: 5 frames of a plain boot, then the frontend's own
            # cold boot loads the recording, which then runs 100 frames.
            rc=$(JNEXT_DELAYED_RESET_FRAMES=5 JNEXT_DELAYED_RESET_TYPE="loadnex:$pr_rzx" \
                    pr_run headless cold-boot 105)
            pr_same cold-boot "$rc"

            # ── THE NEXT-TYPED MATRIX (the original bug's home) ──────────
            # Same file, machine marker patched to `next` in place. Every route
            # is compared against the FIRST ROUTE'S OWN PICTURE, not against a
            # fixed expectation: the invariant is that the routes AGREE, and an
            # expectation all four could satisfy while disagreeing with each
            # other would not be this test.
            # ITS OWN FIXTURE, and the reason is the whole point of the case.
            # The 128K recording above is of a program running in RAM at
            # 0x8000, and the gate this guards overlays the boot ROM at
            # 0x0000-0x1FFF — which such a program never executes, so its
            # picture is identical whether the overlay armed or not. A fixture
            # like that cannot see the bug: measured, by restoring the bug and
            # watching the row pass. This one is a BARE BOOT, executing ROM,
            # so the overlay is exactly what it renders.
            pr_next="$pr_dir/next.rzx"
            pr_next_src="$pr_dir/next-src.rzx"
            rc=$(pr_run headless next-mk 30 --rzx-record "$pr_next_src")
            if [[ "$rc" != 0 || ! -s "$pr_next_src" ]]; then
                pr_faults+=("next: could not record the bare-boot 128K fixture (rc=$rc)")
            fi
            cp "$pr_next_src" "$pr_next" 2>/dev/null || true
            pr_off=$(grep -abo "machine=128k" "$pr_next" 2>/dev/null | head -1 | cut -d: -f1)
            if [[ -z "$pr_off" ]]; then
                pr_faults+=("next: no machine=128k marker to patch — has the marker format changed?")
            else
                printf 'machine=next' | dd of="$pr_next" bs=1 seek="$pr_off" \
                    conv=notrunc status=none
                grep -aq "machine=next" "$pr_next" \
                    || pr_faults+=("next: the marker patch did not take")

                # Route 1 is the reference the others are compared against.
                rc=$(pr_auto next-rzx-play 100 --rzx-play "$pr_next")
                [[ "$rc" == 0 ]] || pr_faults+=("next-rzx-play: rc=$rc")
                # It must really have built a Next, or the Next-only gate below
                # was never reached and the agreement proves nothing.
                grep -qF "machine_type=0[ZX Next]" "$pr_dir/next-rzx-play.log" \
                    || pr_faults+=("next-rzx-play: the boot did not build a Next")
                # ...and it must not be a plain Next boot, for the same reason.
                rc_nboot=$(pr_auto next-boot 100 --machine next)
                d=$(png_diff "$pr_dir/next-rzx-play.png" "$pr_dir/next-boot.png")
                [[ "$rc_nboot" == 0 && "$d" -gt 0 && "$d" -lt 999999 ]] \
                    || pr_faults+=("next: the replay equals a plain Next boot (png_diff=$d) — proves nothing")

                # pr_agree <tag> <rc>: this route's picture equals route 1's.
                pr_agree() {
                    local d
                    [[ "$2" == 0 ]] || { pr_faults+=("$1: rc=$2"); return; }
                    d=$(png_diff "$pr_dir/$1.png" "$pr_dir/next-rzx-play.png")
                    [[ "$d" == 0 ]] \
                        || pr_faults+=("$1: differs from --rzx-play of the same file (png_diff=$d)")
                }
                pr_agree next-load "$(pr_auto next-load 100 --load "$pr_next")"
                pr_agree next-bare "$(pr_auto next-bare 100 "$pr_next")"
                pr_agree next-cold "$(JNEXT_DELAYED_RESET_FRAMES=5 \
                    JNEXT_DELAYED_RESET_TYPE="loadnex:$pr_next" \
                    pr_auto next-cold 105)"
            fi

            if [[ ${#pr_faults[@]} -gt 0 ]]; then
                fail_row " ($(IFS=';'; echo "${pr_faults[*]}"))"
            else
                pass_row " (a 128K recording of a loaded program replays pixel-exact via --rzx-play, --load and bare .rzx in headless/Qt/SDL, and via a cold-boot load; and a NEXT-TYPED recording — which only playback can reach now — gives the same picture by all four routes, on a machine the boot really built as a Next and a picture that is not a plain boot)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
