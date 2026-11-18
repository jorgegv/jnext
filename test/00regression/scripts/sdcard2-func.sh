#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #505 - the second SD card (--sdcard2). The Next board has two SD sockets;
# NextZXOS maps the second card's first FAT partition to the next drive letter
# after card 0's (D:). The card is a 64 MiB image built HERE at run time (an MBR
# with one FAT32 partition at 1 MiB, mformat; no NextZXOS byte, nothing to commit)
# holding MAGIC.NEX, copied on by jnext's own --sdcard-file-add and checked with
# mdir, a foreign reader. Oracle: NextZXOS's own drive mapping, observed: the
# program prints "Hello from ZX Next!" to the magic port only if it was read off D:.
#
#   d        --sdcard2 card2: `CD "d:"` then `.nexload magic.nex` runs it.
#   control  the same keystrokes with no --sdcard2: the file is not reachable, so
#            the line must NOT appear (without it, `d` proves nothing).
#   refused  every unusable --sdcard2 / --sdcard2-* combination fails the run
#            (exit != 0) with its reason on stderr.
#
# (A live insert and the Qt probe are covered by further legs: see the stage list
# in the GH #505 plan.) Every card is a private reflink clone under $RUN_DIR; the
# harness's EXIT trap removes them. This row installs no trap of its own (GH #153).
if want sdcard2-func; then
    begin_func sdcard2-func

    faults=()
    BASE="$RUN_DIR/private/sdcard2"
    W="$BASE/work"
    mkdir -p "$W"
    MASTER="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    missing=()
    for tool in mformat mdir; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done

    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${missing[*]})"
    elif [[ ! -f "$MASTER" ]]; then
        skip_row " (no SD-card image clone for this run)"
    else
        export MTOOLS_SKIP_CHECK=1
        clone_a() {   # clone_a <name> - a config dir whose fallback card 0 is a clone of the master
            mkdir -p "$BASE/$1/sdcard"
            cp --reflink=auto "$MASTER" "$BASE/$1/sdcard/cspect-next-1gb-fixed.img"
            sd_card_sidecars "$MASTER" "$BASE/$1/sdcard/cspect-next-1gb-fixed.img"
        }
        clone_a d
        clone_a control
        clone_a writes
        clone_a warm
        clone_a warmj
        clone_a insreset
        clone_a qt
        clone_a prefs
        clone_a stale

        # Card 2: 64 MiB, an MBR with one FAT32 (type 0x0C) partition from sector
        # 2048, formatted with one-sector clusters so it has more than 65 525 of them.
        CARD2="$W/card2.img"
        OFF=1048576
        SECTORS=$(( (64 * 1024 * 1024 - OFF) / 512 ))
        truncate -s $((64 * 1024 * 1024)) "$CARD2"
        printf '\x0c' | dd of="$CARD2" bs=1 seek=$((0x1BE + 4)) conv=notrunc status=none
        printf '\x00\x08\x00\x00' | dd of="$CARD2" bs=1 seek=$((0x1BE + 8)) conv=notrunc status=none
        printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((SECTORS & 255)) $(((SECTORS >> 8) & 255)) $(((SECTORS >> 16) & 255)) $(((SECTORS >> 24) & 255)))" \
            | dd of="$CARD2" bs=1 seek=$((0x1BE + 12)) conv=notrunc status=none
        printf '\x55\xaa' | dd of="$CARD2" bs=1 seek=510 conv=notrunc status=none
        LANG=C mformat -i "$CARD2@@$OFF" -F -T "$SECTORS" -h 255 -s 63 -c 1 :: </dev/null >/dev/null 2>&1 \
            || faults+=("harness: mformat could not build card 2")
        # jnext's own --sdcard-file-add onto a card that is not NextZXOS's.
        rc=0
        timeout --foreground --kill-after=5s 120s \
            "$JNEXT" --sdcard "$CARD2" \
                     --sdcard-file-add "$PROJECT_DIR/test/00regression/nex/magic_port_demo.nex" \
                     --sdcard-file-dest /MAGIC.NEX > "$W/add.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("--sdcard-file-add onto card 2 exited $rc")
        listing=$(LANG=C mdir -i "$CARD2@@$OFF" :: </dev/null 2>/dev/null || true)
        grep -qi 'MAGIC *NEX' <<<"$listing" || faults+=("mdir does not list MAGIC.NEX on card 2")

        # Frame-based, boot-timeline frames (bf) like sdcard-swap-func: SPACE@400
        # skips the welcome tour, DOWN+ENTER reach the command line; then
        # CD "d:" ENTER and `.nexload magic.nex` ENTER (`.nexload d:/x` would not
        # work: ":" separates BASIC statements).
        keys=()
        kadd() { local v; v=$(bf "$1") || exit 2; keys+=(--delayed-keypress-frames "$v" "$2"); }
        kadd 577 space; kadd 647 down; kadd 677 enter
        f=737
        for k in c d space sym+p d : sym+p enter; do kadd "$f" "$k"; f=$((f + 15)); done
        f=$((f + 60))
        for k in . n e x l o a d space m a g i c . n e x enter; do kadd "$f" "$k"; f=$((f + 15)); done
        exit_frame=$(bf $((f + 400)))

        D_ENV=()
        d_run() {   # d_run <name> [extra args...]   (D_ENV: extra environment, as VAR=value words)
            local name=$1 rc=0; shift
            env JNEXT_CONFIG_DIR="$BASE/$name" "${D_ENV[@]}" timeout --foreground --kill-after=5s 300s \
                "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                         --magic-port 0xCAFE --magic-port-mode line \
                         "${keys[@]}" "$@" \
                         --delayed-automatic-exit-frames "$exit_frame" \
                > "$W/$name.log" 2>&1 || rc=$?
            [[ $rc -eq 0 ]] || faults+=("$name: the NextZXOS run exited $rc")
        }
        d_run d --sdcard2 "$CARD2"
        d_run control
        # insert-reset: no card at power-on; a headless insert and a hard reset in the SAME tick
        # (as sdcard-swap-func's reset leg): the change is made BEFORE the cold boot, which boots
        # NextZXOS with the new card, so the d keys find it.
        D_ENV=(JNEXT_DELAYED_RESET_FRAMES=5)
        d_run insreset --delayed-sdcard2-insert-frames 5 "$CARD2"
        D_ENV=()
        grep -qF "Second SD card inserted: '$CARD2'" "$W/insreset.log" \
            || faults+=("insert-reset: the second card was not inserted")
        grep -qF "outlived a reset" "$W/insreset.log" \
            && faults+=("insert-reset: the change was made after the cold boot, not before")
        grep -qF "Hello from ZX Next!" "$W/insreset.log" \
            || faults+=("insert-reset: after the reset NextZXOS did not run MAGIC.NEX from D:")
        grep -qF "Second SD card image mounted: '$CARD2'" "$W/d.log" \
            || faults+=("d: the second card was not mounted")
        grep -qF "Hello from ZX Next!" "$W/d.log" \
            || faults+=("d: NextZXOS did not run MAGIC.NEX from D:")
        grep -qF "Hello from ZX Next!" "$W/control.log" \
            && faults+=("control: MAGIC.NEX ran without a second card, so the d result proves nothing")

        # writes: --sdcard2-writes binds the journal to the SECOND card's image (the mount of
        # card 2 says it opened with a write journal) and the image is not written.
        rc=0
        cksum_before=$(cksum < "$CARD2")
        JNEXT_CONFIG_DIR="$BASE/writes" timeout --foreground --kill-after=5s 120s \
            "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 \
                     --sdcard2 "$CARD2" --sdcard2-writes "$W/w.jsdw" \
                     --delayed-automatic-exit-frames 20 > "$W/writes.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("writes: the run exited $rc")
        grep -qF "SD image opened with a write journal: $CARD2" "$W/writes.log" \
            || faults+=("writes: the second card was not opened with a write journal")
        grep -qF "second SD card writes are kept in $W/w.jsdw" "$W/writes.log" \
            || faults+=("writes: the journal was not bound for the second card")
        [[ "$(cksum < "$CARD2")" == "$cksum_before" ]] || faults+=("writes: the second card's image changed")

        # warm: the warm-start recording boots with the second card and is keyed on it
        # (header magic JNEXTWS4, the card's size at offset 96); a run without the card
        # refuses it and records again.
        WARM_NEX="$PROJECT_DIR/test/00regression/nex/tilemap_demo.nex"
        warm_run() {   # warm_run <label> [extra args...]
            local label=$1 rc=0; shift
            JNEXT_CONFIG_DIR="$BASE/warm" timeout --foreground --kill-after=5s 120s \
                "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" --load "$WARM_NEX" "$@" \
                         --delayed-automatic-exit-frames 10 > "$W/$label.log" 2>&1 || rc=$?
            [[ $rc -eq 0 ]] || faults+=("$label: the run exited $rc")
        }
        warm_run warm1 --sdcard2 "$CARD2"
        wfile=$(ls "$BASE"/warm/warm-start/*.jwss 2>/dev/null | head -1 || true)
        if [[ -s "$wfile" ]]; then
            [[ "$(head -c 8 "$wfile")" == "JNEXTWS4" ]] || faults+=("warm: the cache magic is not JNEXTWS4")
            wsize=$(dd if="$wfile" bs=1 skip=96 count=8 status=none | od -An -tu8 | tr -d ' ')
            [[ "$wsize" == "$(stat -c%s "$CARD2")" ]] \
                || faults+=("warm: the second card's size is not in the cache header (got '$wsize')")
            warm_run warm2
            grep -qF "recorded with a different second SD card" "$W/warm2.log" \
                || faults+=("warm: a run without the second card did not refuse the recording")
        else
            faults+=("warm: no warm-start cache was written for the run with a second card")
        fi

        # warmj: the second card's WRITE JOURNAL is part of the key. Record with an (empty)
        # journal, change the journal through jnext's own --sdcard-file-add, and the next run
        # refuses the recording and takes a new one.
        warmj_run() {   # warmj_run <label>
            local label=$1 rc=0
            JNEXT_CONFIG_DIR="$BASE/warmj" timeout --foreground --kill-after=5s 120s \
                "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" --load "$WARM_NEX" \
                         --sdcard2 "$CARD2" --sdcard2-writes "$W/wj.jsdw" \
                         --delayed-automatic-exit-frames 10 > "$W/$label.log" 2>&1 || rc=$?
            [[ $rc -eq 0 ]] || faults+=("$label: the run exited $rc")
        }
        warmj_run warmj1
        rc=0
        timeout --foreground --kill-after=5s 120s \
            "$JNEXT" --sdcard "$CARD2" --sdcard-writes "$W/wj.jsdw" \
                     --sdcard-file-add "$WARM_NEX" --sdcard-file-dest /JNOTE.NEX > "$W/warmj-add.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("warmj: changing the second card's journal exited $rc")
        warmj_run warmj2
        grep -qF "recorded with a different second SD card" "$W/warmj2.log" \
            || faults+=("warmj: a changed second-card journal did not refuse the recording")
        grep -qF "recorded a NextZXOS-resident machine" "$W/warmj2.log" \
            || faults+=("warmj: the refused recording was not replaced by a new one")

        # qt: QtApp's poll for the second slot, its order against the cold boot, its own config
        # and its report to the window, through the probe (offscreen, as sdcard-swap-func).
        rc=0
        JNEXT_CONFIG_DIR="$BASE/qt" JNEXT_HOST_PROBE="sdcard2:$CARD2" \
        QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 120s \
            "$JNEXT" --silent --machine 48k --rewind-buffer-size 0 \
                     --delayed-automatic-exit-frames 150 > "$W/qt.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("qt: the run exited $rc")
        grep -qF "HOSTPROBE sdcard2: same-tick-reset card=new" "$W/qt.log" \
            || faults+=("qt: the hard reset did not keep the new second card")
        grep -qF "Second SD card image mounted: '$CARD2'" "$W/qt.log" \
            || faults+=("qt: the same-tick hard reset did not boot with the new second card")
        grep -qF "outlived a reset" "$W/qt.log" \
            && faults+=("qt: the change was serviced after the cold boot, not before")
        grep -qF "status bar: Second SD card inserted: $CARD2" "$W/qt.log" \
            || faults+=("qt: the window was not told the change was made")

        # prefs: the saved Preferences path (jnext.conf [paths] sd_card2_path) mounts the second
        # card with no flag; a saved path that no longer exists is logged and leaves the slot
        # empty, and the run still starts.
        printf '[paths]\nsd_card2_path=%s\n' "$CARD2" > "$BASE/prefs/jnext.conf"
        printf '[paths]\nsd_card2_path=%s\n' "$W/vanished.img" > "$BASE/stale/jnext.conf"
        for pn in prefs stale; do
            rc=0
            JNEXT_CONFIG_DIR="$BASE/$pn" QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
            timeout --foreground --kill-after=5s 120s \
                "$JNEXT" --silent --machine 48k --rewind-buffer-size 0 \
                         --delayed-automatic-exit-frames 30 > "$W/$pn.log" 2>&1 || rc=$?
            [[ $rc -eq 0 ]] || faults+=("$pn: the run exited $rc")
        done
        grep -qF "Second SD card image mounted: '$CARD2'" "$W/prefs.log" \
            || faults+=("prefs: the saved second card was not mounted")
        grep -qF "Second SD card image mounted" "$W/stale.log" \
            && faults+=("stale: a vanished saved second card was mounted")
        grep -qF "Preferences: the second SD card image is not used" "$W/stale.log" \
            || faults+=("stale: the vanished saved path was not reported")

        # refused: card 0 is CARD2 where the second card is the thing under test (the
        # run never starts: every refusal is at parse time, on a 48K headless machine).
        short_run() {   # short_run <label> <expect-in-log> [jnext args...]
            local label=$1 want=$2 rc=0; shift 2
            timeout --foreground --kill-after=5s 120s \
                "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 \
                         --delayed-automatic-exit-frames 5 "$@" > "$W/$label.log" 2>&1 || rc=$?
            [[ $rc -ne 0 ]] || faults+=("$label: exited 0")
            grep -qF -- "$want" "$W/$label.log" || faults+=("$label: no '$want' in the log")
        }
        cp --reflink=auto "$CARD2" "$W/card2b.img"
        ln -sf card2.img "$W/link.img"
        printf 'PK\005\006%018d' 0 > "$W/empty.zip"
        short_run same "need two different images" --sdcard "$CARD2" --sdcard2 "$CARD2"
        short_run symlink "need two different images" --sdcard "$CARD2" --sdcard2 "$W/link.img"
        short_run dotted "need two different images" --sdcard "$CARD2" --sdcard2 "$W/./card2.img"
        short_run missing "cannot open '$W/no-such.img'" --sdcard "$CARD2" --sdcard2 "$W/no-such.img"
        short_run dir "is not a file" --sdcard "$CARD2" --sdcard2 "$W"
        short_run zip "is a ZIP archive" --sdcard "$CARD2" --sdcard2 "$W/empty.zip"
        short_run wro "cannot be combined with --sdcard2-readonly" --sdcard "$CARD2" \
            --sdcard2 "$W/card2b.img" --sdcard2-writes "$W/w.jsdw" --sdcard2-readonly
        short_run reset "--sdcard2-writes-reset needs --sdcard2-writes FILE" --sdcard "$CARD2" \
            --sdcard2 "$W/card2b.img" --sdcard2-writes-reset
        short_run rdonly "--sdcard2-readonly needs --sdcard2 FILE" --sdcard "$CARD2" --sdcard2-readonly
        short_run insdup "need two different images" --sdcard "$CARD2" --sdcard2 "$W/card2b.img" \
            --delayed-sdcard2-insert-frames 3 "$CARD2"
        short_run insfirst "need two different images" --sdcard "$CARD2" \
            --delayed-sdcard2-insert-frames 3 "$CARD2"
        windowed_run() {
            local rc=0
            timeout --foreground --kill-after=5s 120s \
                "$JNEXT" --machine 48k --delayed-sdcard2-insert-frames 5 "$W/card2b.img" \
                > "$W/windowed.log" 2>&1 || rc=$?
            [[ $rc -ne 0 ]] || faults+=("windowed: exited 0")
            grep -qF "--delayed-sdcard2-insert-frames requires --headless" "$W/windowed.log" \
                || faults+=("windowed: no 'requires --headless' in the log")
        }
        QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy windowed_run
        short_run wonly "--sdcard2-writes needs --sdcard2 FILE" --sdcard "$CARD2" \
            --sdcard2-writes "$W/w.jsdw"
    fi

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (NextZXOS ran MAGIC.NEX from D:; control did not; refusals pinned)"
    else
        fail_row " (${#faults[@]} fault(s) in the second SD card)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
