#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# GH #281 WP-5 (gdb-rsp-frontend.md §7.2) — the ACCEPTANCE row: the REAL
# z88dk-gdb client against a live headless jnext serving `--gdb-port 0`,
# debugging a real z88dk program with the program's own linker map. Every
# assertion reads the CLIENT's log — what the user sees — plus jnext's detach
# line. The rows that drive the protocol with an independent client of our own
# are gdb-cli/sdl/qt-func; this one is the client the feature exists for.
#
# THE CLIENT. test/provision-z88dk-gdb.sh --print-path resolves it (the one
# place the order lives: `$Z88DK_GDB`, `z88dk-gdb` on PATH, the conventional
# source-tree install `$HOME/src/spectrum/z88dk/bin/z88dk-gdb`, then the user
# cache). z88dk is not packaged, so `make regression` runs `make z88dk-gdb`
# first, which builds z88dk v2.4's client into that cache when none resolves —
# locally and in CI alike. A client still absent here means the suite was run
# without the Makefile; the row SKIPS, saying so, and a SKIP fails the run.
#
# THE PROGRAM. test/00regression/nex/magic_bp_demo.nex (our own demo,
# demo/magic_bp_demo) and magic_bp_demo.map, its z88dk linker map (`zcc … -m`),
# built from the same source into a NEX byte-identical to the checked-in one.
# The map is trimmed to what the client reads — the demo module's own symbols
# and the non-empty sections' __*_head/_tail/_size — and the z88dk install
# prefix in its comment fields is rewritten to `z88dk/`; nothing the client
# resolves changed. `_print_str` = 0x812E, and its CALL to `_print_char` sits
# at 0x8152.
#
# THE SESSION, scripted through the client's stdin. jnext runs the demo to its
# final HALT loop before the client connects (headless runs at full speed), so
# the session restarts `main` itself:
#   set pc 0x816a          G — the register write (only PC changes)
#   break _print_str       Z0 at an address the client resolved from the map
#   cont                   c — stops there: "Hit breakpoint 1: @812e (_print_str)"
#   reg                    g — pc=812E
#   stepi                  s
#   break 0x8152 ; cont    the CALL _print_char
#   nexti                  i3 — z88dk-gdb's step over a CALL; stops at 0x8155
#   reg                    g — pc=8155
#   monitor mmu / nextreg  qRcmd — only a client that has `monitor` (upstream
#                          z88dk-gdb; v2.4 does not) sends these, so they are
#                          asserted when the client sent them, and the PASS line
#                          says which it was
#   quit, y                z0 ×2, D — the detach releases the client's pause
if want gdb-z88dk-func; then
    begin_func gdb-z88dk-func
    zgdb=$(bash "$PROJECT_DIR/test/provision-z88dk-gdb.sh" --print-path) || zgdb=""
    nex="$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex"
    map="$PROJECT_DIR/test/00regression/nex/magic_bp_demo.map"
    log="$TMP_DIR/gdb-z88dk-func.jnext.log"
    clog="$TMP_DIR/gdb-z88dk-func.client.log"
    if [[ -z "$zgdb" || ! -x "$zgdb" ]]; then
        skip_row " (z88dk-gdb not found: run 'make z88dk-gdb' (make regression does), or set Z88DK_GDB)"
    elif ! gdb_launch_cmd "$log" "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --gdb-port 0 \
            --load "$nex"; then
        gdb_stop
        fail_row " (jnext never logged 'gdb: listening on': $(tail -n 3 "$log" | tr '\n' ' '))"
    else
        rc=0
        # In $TMP_DIR, in a subshell: z88dk-gdb writes its command history
        # (.ticks_history.txt) into its current directory, which must not be the
        # checkout — and this file is sourced, so a bare `cd` would move the
        # harness itself.
        (
            cd "$TMP_DIR"
            printf '%s\n' "set pc 0x816a" "break _print_str" "cont" "reg" "stepi" \
                "break 0x8152" "cont" "nexti" "reg" "monitor mmu" "monitor nextreg 0x07" \
                "quit" "y" |
                LANG=C timeout --foreground --kill-after=5s 60s "$zgdb" -v -h 127.0.0.1 \
                    -p "$GDB_PORT" -x "$map" >"$clog" 2>&1
        ) || rc=$?
        # The client's `quit` detached; give jnext's pump the moment it needs
        # to log the detach, bounded, then end it.
        for _ in $(seq 1 50); do
            grep -q "DETACH client" "$log" 2>/dev/null && break
            sleep 0.1
        done
        gdb_stop
        fails=()
        # Exit status: 0. The one tolerated exception is upstream z88dk-gdb's
        # own race at `quit`: after `D` its network thread sees the socket end
        # and calls remote_closed() -> exit(1) ("Connection to remote closed."),
        # racing the main thread's exit(0) (upstream debugger_gdb.c,
        # network_read_thread / gdb_remote_closed). It does that after a
        # successful detach whatever the server does, so status 1 is accepted
        # only with the D answered OK and that line printed after it.
        if [[ $rc -ne 0 ]] && ! { [[ $rc -eq 1 ]] &&
                grep -qF 'Connection to remote closed.' "$clog" &&
                grep -qF 'r: $OK#9a' <<<"$(sed -n '/w: D$/,$p' "$clog")"; }; then
            fails+=("z88dk-gdb exited $rc")
        fi
        grep -qF "Registers:  af bc de hl af' bc' de' hl' ix iy sp pc clockl clockh" "$clog" ||
            fails+=("target.xml register list not accepted")
        grep -qF "Adding breakpoint at '_print_str' \$812e (_print_str)" "$clog" ||
            fails+=("break _print_str not resolved from the map to 812e")
        grep -qF "Hit breakpoint 1: @812e (_print_str)" "$clog" ||
            fails+=("cont did not stop at _print_str")
        grep -q "^pc=812E" "$clog" || fails+=("reg after the stop is not pc=812E")
        grep -qF "Hit breakpoint 2: @8152" "$clog" || fails+=("second breakpoint not hit")
        grep -q "w: i3$" "$clog" || fails+=("nexti over the CALL did not send i3")
        grep -q "^pc=8155" "$clog" || fails+=("nexti did not stop at 8155")
        grep -qE "incorrect amount of data|Warning:" "$clog" &&
            fails+=("the client warned: $(grep -m1 -E 'incorrect amount of data|Warning:' "$clog")")
        grep -q "w: D$" "$clog" || fails+=("the client never sent D")
        grep -q "DETACH client [0-9]* (released its pause)" "$log" ||
            fails+=("jnext did not release the client's pause on D")
        if grep -q "w: qRcmd," "$clog"; then
            mon="monitor answered (upstream client)"
            # Each monitor line is its own line on the client's console: the
            # first and the last slot, the paging-port line, and the NextREG.
            grep -q "^slot 0: page" "$clog" && grep -q "^slot 7: page" "$clog" &&
                grep -q "^7FFD=" "$clog" || fails+=("monitor mmu output missing or run together")
            grep -qE "^NR 07 = [0-9A-F]{2}$" "$clog" ||
                fails+=("monitor nextreg 0x07 output missing or run together")
        else
            mon="client has no monitor (v2.4)"
        fi
        if (( ${#fails[@]} == 0 )); then
            pass_row " (z88dk-gdb: map symbols, break/cont/stepi/nexti(i3)/set pc/reg, D; $mon)"
        else
            fail_row " (${fails[*]}; client log: $clog)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
