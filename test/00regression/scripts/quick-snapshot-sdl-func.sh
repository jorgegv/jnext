#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# SDL frontend Quick Save / Quick Load (GitHub issue #321): Alt+Shift+F5 writes a
# snapshot into $JNEXT_CONFIG_DIR/snapshots, Alt+Shift+F9 restores the newest.
#
# WHY A FUNCTIONAL ROW. host_key_latch_test (HKL-QS-*) pins the chord
# predicate and its press/release tracker, but nothing in the unit tree runs
# SdlApp::run(), which is where the load request is serviced and where an
# unreadable newest file must be refused BEFORE the cold boot. Only the real
# binary shows that. The binary is build/sdl-release, as sdl-keypress-func.
#
# THE RUNS (all 48K, $JNEXT_CONFIG_DIR is this row's own directory):
#   empty  Alt+Shift+F9, no snapshot: the log says so and NO cold boot follows (the
#          count of "Cold boot" lines equals the save run's, which has only the
#          startup one); its border is the untouched machine's;
#   save   BORDER 1, Alt+Shift+F5, BORDER 2, Alt+Shift+F5, screenshot A: two files, named
#          jnext-YYYYMMDD-HHMMSS[-NN].jns, two "quick save: wrote" lines;
#   load   Alt+Shift+F9 on a fresh machine: the border must equal A's (the newest);
#   typeafter  Alt+Shift+F9 again, then BORDER 5 typed: the border must change.
#          The chord's Shift sits in the saved keyboard matrix and used to come
#          back held (BORDER 5 became CAPS SHIFT+5 and did nothing);
#   typeahead  Alt+Shift+F9 and BORDER 5 in ONE xdotool burst (GH #386): the keys
#          reach the app together with the chord and must still be typed into the
#          LOADED machine, so the border equals typeafter's BORDER 5 render. The
#          key queue holds them behind the chord (Router::on_host_action) and the
#          load re-asserts what the host holds (Router::reassert());
#   typeduring  the chord with its key-down and key-up SPLIT (keydown, 50 ms,
#          keyup), then BORDER 5 typed in one burst while the app is stalled on the
#          SD digest (GH #386). With the chord's releases already consumed nothing
#          is queued at the cold boot, so the keys reach the app directly, after
#          the boot and before the deferred load lands, and the load wipes the
#          matrix: only Router::reassert() (through the shared load helper) puts
#          them back. Same expectation as typeahead. Removing the re-assert fails
#          it every time (measured by the round-1 review: 3/3 on both rows). A
#          plain `xdotool key alt+shift+F9` does NOT test this: its releases are
#          queued behind the barrier and the keys line up behind them;
#   emptytype  right after `empty` (the folder is still empty), Alt+Shift+F9 then
#          BORDER 4 typed slowly: the load finds nothing and does not cold-boot,
#          so only the end of the action lifts the key queue; the border must
#          differ from the untouched machine's;
#   load2  after `touch`ing the OLDER save into the future, Alt+Shift+F9: the border
#          must differ from `load`'s and from the untouched machine's. This is
#          what proves the two saves really differ AND that "newest" is by
#          modification time: the row FAILS if the BORDER keystrokes were lost
#          and both saves are the same state (it used to pass vacuously then);
#   bad    a garbage .jns, newest of all; BORDER 3 then Alt+Shift+F9: refused with a
#          log line, no .jns cold boot, no cold boot at all, and the BORDER 3
#          session survives.
#   keep1  Alt+Shift+F5 with --quick-snapshot-keep 1: exactly one quick snapshot
#          is left (the new one) and the garbage file, which is not jnext's
#          name, is untouched (the SDL build has no config, so the flag is its
#          only way to set the retention).
#   typesave  BORDER 1 typed slowly, then BORDER 2 and Alt+Shift+F5 in ONE burst
#          (GH #386; the garbage file removed first); loadts then Quick Loads it:
#          its border must equal A's. The save waits for the keys typed before it.
# (GH #396 removed the whole-card SHA-256 and the digest count that used to be
# asserted here; the NODIG rows of sd_no_digest_linux_test measure bytes read.)
# No palette value is hard-coded: every comparison is between renders of this
# row. Border pixels, not text, because the BASIC cursor flashes.
#
# Bare function keys cannot be injected under this Xvfb (xdotool delivers them
# as Alt+F-key), so "bare F5/F9 do nothing" is pinned by HKL-QS-* instead.
# The Xvfb is started with -noreset (GH #318; HS-89 enforces it). The row is
# tagged `quiet`: under the parallel phase's load the typed BORDER commands
# were sometimes lost, and the oracle above then (correctly) fails the row.

if want quick-snapshot-sdl-func; then
    begin_func quick-snapshot-sdl-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    snap_dir="$RUN_DIR/snapshots"

    # qs_run <mode> <png>: mode = empty | emptytype | save | load | typeafter | typeahead |
    #                             load2 | bad | keep1 | typesave | loadts.
    # shellcheck disable=SC2016
    qs_run() {
        local mode="$1" out="$2"
        local log="$TMP_DIR/qs_sdl_$mode.log"
        rm -f "$out" "$log"
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
        timeout --kill-after=5s 120s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24 -noreset" bash -c '
            set -uo pipefail
            bin="$1"; out="$2"; mode="$3"; log="$4"
            extra=""
            [ "$mode" = keep1 ] && extra="--quick-snapshot-keep 1"
            # typeafter waits for the load, then types: it needs a later capture;
            # typeahead types as the load is serviced, so it gets the same.
            shot_frame=700; exit_frame=760
            case "$mode" in typeafter|typeahead|typeduring) shot_frame=950; exit_frame=1010 ;; esac
            "$bin" --machine 48k --silent $extra \
                --delayed-screenshot "$out" --delayed-screenshot-frames $shot_frame \
                --delayed-automatic-exit-frames $exit_frame >"$log" 2>&1 &
            pid=$!
            wid=""
            for _ in $(seq 1 100); do
                wid=$(xdotool search --name JNEXT 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            # 48K BASIC is at its prompt by emulated frame 150; six seconds is
            # 4x that at 1x speed (see sdl-keypress-func).
            sleep 6
            if [ -n "$wid" ]; then
                xdotool windowactivate --sync "$wid" 2>/dev/null || true
                tap() { xdotool key "$1" 2>/dev/null || true; sleep 0.5; }
                case "$mode" in
                    empty|load|load2|loadts) tap alt+shift+F9 ;;
                    keep1) tap alt+shift+F5 ;;
                    typeafter) tap alt+shift+F9; sleep 5; tap b; tap 5; tap Return ;;
                    # GH #386: the chord and the keys behind it in ONE burst (no
                    # delay between xdotool events), so the host delivers them to
                    # the app together, as it does after any UI stall.
                    typeahead) xdotool key --delay 0 alt+shift+F9 b 5 Return 2>/dev/null || true; sleep 1 ;;
                    # GH #386: keys typed while the load is in progress, and nothing
                    # left in the key queue when its cold boot comes: the chord
                    # key-down and key-up are split, with the keys typed after the
                    # key-up, so the chord releases are already consumed when the
                    # stall (the SD digest) begins to end. The host queues the keys,
                    # they reach the app after the cold boot and before the load
                    # lands, and the load wipes the matrix: only the re-assert
                    # (emulator_apply_load_keeping_keys) keeps them.
                    typeduring)
                        xdotool keydown alt shift F9 2>/dev/null || true; sleep 0.05
                        xdotool keyup F9 shift alt 2>/dev/null || true; sleep 0.05
                        xdotool key --delay 0 b 5 Return 2>/dev/null || true; sleep 1 ;;
                    # GH #386: a Quick Load that finds nothing does not cold-boot, so
                    # only the end of the action lifts the key queue: typing must work.
                    emptytype) tap alt+shift+F9; tap b; tap 4; tap Return ;;
                    # GH #386: keys typed BEFORE the save chord, in one burst: the
                    # save must wait for them (BORDER 2 is in the snapshot).
                    typesave)
                        tap b; tap 1; tap Return
                        xdotool key --delay 0 b 2 Return alt+shift+F5 2>/dev/null || true; sleep 2 ;;
                    save)
                        tap b; tap 1; tap Return; tap alt+shift+F5
                        tap b; tap 2; tap Return; tap alt+shift+F5 ;;
                    bad)
                        tap b; tap 3; tap Return; tap alt+shift+F9 ;;
                esac
            fi
            wait $pid
        ' _ "$sdl_bin" "$out" "$mode" "$log" >/dev/null 2>&1 \
            && qs_rc[$mode]=0 || qs_rc[$mode]=$?
    }
    declare -A qs_rc=()

    # border_px <png>: the colour of the top-left pixel (the border).
    border_px() {
        local image_cmd=(magick); command -v magick &>/dev/null || image_cmd=(convert)
        "${image_cmd[@]}" "$1" -format '%[pixel:p{2,2}]' info: 2>/dev/null || echo "?"
    }

    shot_e="$TMP_DIR/qs_sdl_e.png"; shot_a="$TMP_DIR/qs_sdl_a.png"
    shot_l="$TMP_DIR/qs_sdl_l.png"; shot_l2="$TMP_DIR/qs_sdl_l2.png"
    shot_bad="$TMP_DIR/qs_sdl_bad.png"; shot_k="$TMP_DIR/qs_sdl_k.png"; shot_t="$TMP_DIR/qs_sdl_t.png"; shot_ta="$TMP_DIR/qs_sdl_ta.png"; shot_td="$TMP_DIR/qs_sdl_td.png"
    shot_et="$TMP_DIR/qs_sdl_et.png"; shot_ts="$TMP_DIR/qs_sdl_ts.png"; shot_lts="$TMP_DIR/qs_sdl_lts.png"

    if [[ ! -x "$sdl_bin" ]]; then
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; a windowed frontend needs a display)"
    elif ! command -v xdotool &>/dev/null; then
        skip_row " (xdotool not available; cannot inject host key events)"
    else
        rm -rf "$snap_dir"
        qs_run empty "$shot_e"
        # GH #386: typing after a Quick Load that found nothing (no cold boot).
        qs_run emptytype "$shot_et"
        qs_run save  "$shot_a"
        # A missing folder (nothing saved) is a count of 0, not an error under
        # pipefail: the verdict below reports it.
        snap_names=""
        [[ -d "$snap_dir" ]] && snap_names=$(find "$snap_dir" -maxdepth 1 -type f -name '*.jns')
        n_files=$(grep -c . <<<"$snap_names") || n_files=0
        bad_names=$(grep -vcE '/jnext-[0-9]{8}-[0-9]{6}(-[0-9]{2})?\.jns$' <<<"$snap_names") || bad_names=0
        [[ -n "$snap_names" ]] || bad_names=0
        first_saved=$(sort <<<"$snap_names" | head -1)   # names sort in save order
        qs_run load "$shot_l"
        # Typing AFTER a Quick Load: the chord's Shift is in the saved keyboard
        # matrix, and a restored CAPS SHIFT that nothing releases turns BORDER 5
        # into CAPS SHIFT+5 and the border does not move.
        qs_run typeafter "$shot_t"
        # GH #386: the load chord and the keys typed right behind it, in one burst.
        qs_run typeahead "$shot_ta"
        qs_run typeduring "$shot_td"
        # Make the OLDER save the newest by modification time: the load must now
        # restore a DIFFERENT state, which also proves the two saves differ.
        [[ -n "$first_saved" ]] && touch -d '10 minutes' "$first_saved"
        qs_run load2 "$shot_l2"
        # A garbage .jns, newest of all: Quick Load must refuse it and leave the
        # session alone (a BORDER 3 typed beforehand must survive).
        printf 'not a snapshot' >"$snap_dir/zz-bad.jns" 2>/dev/null || true
        touch -d '20 minutes' "$snap_dir/zz-bad.jns" 2>/dev/null || true
        qs_run bad "$shot_bad"
        # Retention: one more Quick Save with --quick-snapshot-keep 1 leaves only
        # the new quick snapshot; the garbage file (not jnext's name) stays.
        qs_run keep1 "$shot_k"
        keep_names=$(find "$snap_dir" -maxdepth 1 -type f -name '*.jns' 2>/dev/null)
        n_keep=$(grep -cE '/jnext-[0-9]{8}-[0-9]{6}(-[0-9]{2})?\.jns$' <<<"$keep_names") || n_keep=0
        bad_kept=no; [[ -f "$snap_dir/zz-bad.jns" ]] && bad_kept=yes
        # GH #386: keys typed BEFORE a Quick Save, in one burst with the chord.
        # Quick Load takes the newest by mtime, and the garbage file and the
        # `touch`ed older save are both dated in the future, so they go first:
        # the load must find the new snapshot.
        rm -f "$snap_dir/zz-bad.jns" "$snap_dir"/jnext-*.jns
        qs_run typesave "$shot_ts"
        qs_run loadts "$shot_lts"

        problems=()
        for m in empty emptytype save load typeafter typeahead typeduring load2 bad keep1 typesave loadts; do
            case "$m" in empty) shot="$shot_e" ;; emptytype) shot="$shot_et" ;; typeahead) shot="$shot_ta" ;; typeduring) shot="$shot_td" ;; typesave) shot="$shot_ts" ;; loadts) shot="$shot_lts" ;; save) shot="$shot_a" ;; load) shot="$shot_l" ;; typeafter) shot="$shot_t" ;; load2) shot="$shot_l2" ;; bad) shot="$shot_bad" ;; *) shot="$shot_k" ;; esac
            if [[ ! -s "$shot" || "${qs_rc[$m]}" -ne 0 ]]; then
                err_line=$(grep -m1 -E '\[(error|critical)\]' "$TMP_DIR/qs_sdl_$m.log" 2>/dev/null) || err_line="no error logged"
                problems+=("$m run: jnext exit ${qs_rc[$m]}, png $([[ -s "$shot" ]] && echo written || echo missing); $err_line")
            fi
        done
        if [[ ${#problems[@]} -gt 0 ]]; then
            fail_row " ($(IFS='|'; echo "${problems[*]}"))"
        else
            L() { echo "$TMP_DIR/qs_sdl_$1.log"; }
            saved_lines=$(grep -c 'quick save: wrote' "$(L save)" || true)
            empty_msg=$(grep -c 'quick load: no snapshot' "$(L empty)" || true)
            boots_save=$(grep -c 'Cold boot' "$(L save)" || true)
            boots_empty=$(grep -c 'Cold boot' "$(L empty)" || true)
            load_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$(L load)" || true)
            bad_boot=$(grep -c 'Cold boot.*load_file=.*\.jns' "$(L bad)" || true)
            boots_bad=$(grep -c 'Cold boot' "$(L bad)" || true)
            bad_msg=$(grep -c 'quick load: refused' "$(L bad)" || true)
            px_e=$(border_px "$shot_e"); px_a=$(border_px "$shot_a"); px_l=$(border_px "$shot_l")
            px_l2=$(border_px "$shot_l2"); px_t=$(border_px "$shot_t"); px_bad=$(border_px "$shot_bad")
            px_ta=$(border_px "$shot_ta"); px_td=$(border_px "$shot_td"); px_et=$(border_px "$shot_et"); px_lts=$(border_px "$shot_lts")
            if [[ "$n_files" -ne 2 || "$bad_names" -ne 0 || "$saved_lines" -ne 2 ]]; then
                fail_row " (save: $n_files file(s), $bad_names misnamed, $saved_lines 'quick save: wrote' line(s); want 2/0/2)"
            elif [[ "$empty_msg" -lt 1 || "$boots_empty" -ne "$boots_save" ]]; then
                fail_row " (empty folder: 'quick load: no snapshot' lines=$empty_msg, cold boots $boots_empty vs $boots_save at startup alone; want >=1 and equal)"
            elif [[ "$px_a" == "$px_e" ]]; then
                fail_row " (the saves did not change the machine: border $px_a equals the untouched machine's; BORDER keystrokes lost?)"
            elif [[ "$load_boot" -lt 1 ]]; then
                fail_row " (Alt+Shift+F9 with snapshots present did not cold-boot a .jns)"
            elif [[ "$px_l" != "$px_a" ]]; then
                fail_row " (Quick Load did not restore the newest save: border $px_l, saved $px_a, untouched $px_e)"
            elif [[ "$px_t" == "$px_l" || "$px_t" == "$px_e" ]]; then
                fail_row " (typing after a Quick Load did not work: BORDER 5 left the border at $px_t (restored $px_l, untouched $px_e); lost keystrokes (the usual cause on a loaded host: the load had not landed, or a key was dropped), or a CAPS SHIFT held in the saved keyboard matrix?)"
            elif [[ "$px_ta" != "$px_t" ]]; then
                fail_row " (keys typed with the Quick Load chord were lost: BORDER 5 typed in the same burst left the border at $px_ta, while typed after the load it gives $px_t (restored $px_l, untouched $px_e); the load wiped keys the host was holding, or the cold boot dropped the queue behind the chord)"
            elif [[ "$px_td" != "$px_t" ]]; then
                fail_row " (keys typed during the Quick Load were wiped by it: BORDER 5 typed while the app was stalled left the border at $px_td, while typed after the load it gives $px_t (restored $px_l, untouched $px_e); the load must re-assert the keys the host holds)"
            elif [[ "$px_et" == "$px_e" ]]; then
                fail_row " (typing after a Quick Load that found nothing did not work: border $px_et equals the untouched machine's; the key queue stayed blocked behind the finished action)"
            elif [[ "$px_l2" == "$px_l" || "$px_l2" == "$px_e" ]]; then
                fail_row " (the two saves are not distinguishable, or Quick Load ignored modification time: after touching the older save the border is $px_l2; newest-by-name gave $px_l, untouched $px_e)"
            elif [[ "$bad_msg" -lt 1 || "$bad_boot" -ne 0 || "$boots_bad" -ne "$boots_save" ]]; then
                fail_row " (unreadable newest .jns: 'quick load: refused' lines=$bad_msg, .jns cold boots=$bad_boot, cold boots $boots_bad vs $boots_save; want >=1/0/equal)"
            elif [[ "$px_bad" == "$px_e" ]]; then
                fail_row " (unreadable newest .jns reset the session (or BORDER 3 was lost): border $px_bad equals the untouched machine's)"
            elif [[ "$n_keep" -ne 1 || "$bad_kept" != yes ]]; then
                fail_row " (--quick-snapshot-keep 1: $n_keep quick snapshot(s) left, garbage file present=$bad_kept; want exactly 1 and the garbage file untouched)"
            elif [[ "$px_lts" != "$px_a" ]]; then
                fail_row " (the Quick Save overtook the keys typed before it: BORDER 2 typed in the same burst as the chord is not in the snapshot; after loading it the border is $px_lts, BORDER 2 is $px_a, untouched $px_e)"
            else
                pass_row " (2 distinct saves; Alt+Shift+F9 restored the newer ($px_l) and, once the older was touched, the older ($px_l2); empty and unreadable folders left the session alone; --quick-snapshot-keep 1 pruned to one and spared the foreign file)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
