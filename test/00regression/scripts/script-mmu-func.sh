#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — #279's MMU0/MMU1 consistency on NR 0x51 (ChaseTheBug, §3(b)):
# test/scripts/dsl/nextreg.jds against the dsl_demo NEX, five ways (§8):
#   1. good build: every NR 0x51 write follows MMU0 — exit 0;
#   2. buggy build: MMU1 = 0x24 after MMU0 = 0x22 — exit 3, the logged PC is
#      0x818B, @page_in_level_mmu1, the NEXTREG 0x51 instruction itself;
#   3. good build + a script fault injected UPSTREAM of the watched write:
#      `set nextreg[0x50] = 0xFF` at @page_in_level_mmu1 (MMU0 already
#      committed, MMU1 not yet written) — the guest's own NEXTREG 0x51,0x23
#      trips the guard: exit 3;
#   4. the same injection at the watched register itself (`set nextreg[0x51]`):
#      a script write raises no event (§2.7) — exit 0;
#   5. the injection one instruction earlier, at @page_in_level: the guest's
#      NEXTREG 0x50,0x22 then overwrites it — exit 0.
if want script-mmu-func; then
    begin_func script-mmu-func
    script_mmu_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    printf 'on execute @page_in_level_mmu1 when mem16[@magic] == 0xD5D5 do set nextreg[0x50] = 0xFF end\n' \
        > "$TMP_DIR/mmu-upstream.jds"
    printf 'on execute @page_in_level_mmu1 when mem16[@magic] == 0xD5D5 do set nextreg[0x51] = 0x40 end\n' \
        > "$TMP_DIR/mmu-watched.jds"
    printf 'on execute @page_in_level when mem16[@magic] == 0xD5D5 do set nextreg[0x50] = 0xFF end\n' \
        > "$TMP_DIR/mmu-early.jds"
    s="$PROJECT_DIR/test/scripts/dsl/nextreg.jds"
    out=$(script_mmu_func_run dsl_demo --script "$s") && rc=0 || rc=$?
    bad=$(script_mmu_func_run dsl_demo_buggy --script "$s") && brc=0 || brc=$?
    inj=$(script_mmu_func_run dsl_demo --script "$s" --script "$TMP_DIR/mmu-upstream.jds") && irc=0 || irc=$?
    wat=$(script_mmu_func_run dsl_demo --script "$s" --script "$TMP_DIR/mmu-watched.jds") && wrc=0 || wrc=$?
    ear=$(script_mmu_func_run dsl_demo --script "$s" --script "$TMP_DIR/mmu-early.jds") && erc=0 || erc=$?
    if [[ $rc -eq 0 && $brc -eq 3 && $irc -eq 3 && $wrc -eq 0 && $erc -eq 0 ]] &&
       grep -qF "PASS nextreg: MMU1 followed MMU0 on every NR 0x51 write" <<<"$out" &&
       grep -qF "MMU0 is 22 whereas MMU1 write is 24 (src 0, PC 818B)" <<<"$bad" &&
       grep -qF "SCRIPT STOP: MMU1 inconsistent with MMU0 at PC=818B" <<<"$bad" &&
       grep -qF "MMU0 is FF whereas MMU1 write is 23 (src 0, PC 818B)" <<<"$inj" &&
       grep -qF "MUTATE" <<<"$inj" &&
       grep -qF "PASS nextreg" <<<"$wat" && grep -qF "PASS nextreg" <<<"$ear"; then
        pass_row " (good 0; buggy 3 at PC 818B; injected upstream 3; at the watched register 0; one instruction early 0)"
    else
        fail_row " (exits good $rc buggy $brc upstream $irc watched $wrc early $erc: $(grep -E 'MMU0 is|STOP|ASSERT|SCRIPT ERROR' <<<"$out$bad$inj$wat$ear" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
