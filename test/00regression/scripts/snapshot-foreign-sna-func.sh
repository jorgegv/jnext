#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #274 — A FOREIGN READER FOR `.sna`, both forms.
#
# WHY THIS EXISTS. jnext's new 128K SNA saver is written to be the exact inverse
# of jnext's OWN SnaLoader. That is precisely the pairing the `.szx` defect came
# from (see snapshot-foreign-szx-func's header): an offset, a bank order or a
# length both sides agree on round-trips byte-exact, passes every discriminative
# unit row, and interoperates with nothing. libspectrum — the library FUSE
# itself uses — is the only adjudicator here that does not consult jnext code.
#
# WHAT IS ASSERTED, and why each part is needed:
#
#  1. libspectrum ACCEPTS all three files jnext can now write: the 48K form
#     (49179 bytes), the 128K form (131103) and the 128K form's six-remaining-
#     bank variant (147487, produced when the bank paged at 0xC000 is itself
#     bank 2 or 5). The 147487 case is the one a foreign reader is most likely
#     to reject, so it is the single most valuable acceptance here.
#  2. libspectrum's view of the `.sna` EQUALS its view of a `.szx` written from
#     the same machine state — every register, the border, the 0x7FFD paging
#     byte and a checksum of each of the eight 16K banks. The `.szx` side is
#     already foreign-adjudicated by snapshot-foreign-szx-func, so agreement
#     means the `.sna` encodes the same machine in a format libspectrum reads.
#  3. THE EIGHT BANK CHECKSUMS ARE DISTINCT. Without this the comparison would
#     be worthless: on an idle 128K boot five banks are all zeros, so a saver
#     that swapped two of them would produce identical checksums and the
#     agreement above would hold while the file was wrong. The fixture injects
#     a program that pages each bank at 0xC000 in turn and stamps its number
#     into it, and this assertion PROVES the fixture did that rather than
#     assuming it.
#  4. The paged bank is 3 — the bank the injected program's last OUT selects —
#     so the 0x7FFD byte is checked against a known truth, not merely agreed on.
#  5. A determinism control: two `.sna` runs to the same frame are byte-
#     identical. The pair in (2) is written by two runs because
#     `--delayed-snapshot` takes one path, so without this the comparison would
#     rest on an assumption of determinism rather than a measurement of it.
#
# WHAT IT DOES NOT PROVE. libspectrum reads a 131103/147487-byte SNA as
# LIBSPECTRUM_MACHINE_PENT (4), not _128 (2) — measured, not assumed. That is
# the FORMAT's doing: a 128K SNA carries no machine identifier, only a TR-DOS
# flag, so a reader has to choose. Nothing jnext can write changes it, so it is
# reported in the pass line rather than asserted, and it means a 128K `.sna`
# handed to FUSE comes up as a Pentagon — correct RAM and paging, different
# timing model. Use `.szx` (which names the machine) when that matters.
if want snapshot-foreign-sna-func; then
    begin_func snapshot-foreign-sna-func
    SNA_PROBE="$PROJECT_DIR/build/test/sna_probe"
    SZX_PROBE="$PROJECT_DIR/build/test/szx_probe"
    if [[ ! -x "$SNA_PROBE" || ! -x "$SZX_PROBE" ]]; then
        skip_row " (sna_probe/szx_probe not built — libspectrum-devel absent; these are the only foreign adjudications in the tree, so their absence is worth seeing)"
    else
    fs_dir="$TMP_DIR/foreign-sna"
    rm -rf "$fs_dir"; mkdir -p "$fs_dir"
    fs_faults=()

    # The fixture program, injected at 0x8000 on a 128K:
    #   DI; LD BC,7FFD; LD HL,DFFF; XOR A
    # loop: OUT (C),A; LD (HL),A; INC A; CP 8; JR NZ,loop
    #   LD A,3; OUT (C),A; JR $
    # It pages each of the eight banks at 0xC000 in turn and stamps its number
    # at bank offset 0x1FFF — far enough from 0x8000 that it never overwrites
    # itself when bank 2 is the paged one — then leaves bank 3 paged and spins.
    printf '\xf3\x01\xfd\x7f\x21\xff\xdf\xaf\xed\x79\x77\x3c\xfe\x08\x20\xf8\x3e\x03\xed\x79\x18\xfe' \
        > "$fs_dir/stamp.bin"
    # And one that just pages bank 5 at 0xC000, so {5,2,paged} has two members
    # and the file carries six remaining banks (147487 bytes).
    printf '\xf3\x01\xfd\x7f\x3e\x05\xed\x79\x18\xfe' > "$fs_dir/page5.bin"

    # fs_save <outfile> <extra jnext args...>: one 128K run that snapshots at
    # frame 150; prints the exit status.
    fs_save() {
        local out=$1 rc=0; shift
        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless \
            --machine 128k "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" \
            --delayed-snapshot "$out" --delayed-snapshot-frames 150 \
            --delayed-automatic-exit-frames 151 >"$out.log" 2>&1 || rc=$?
        echo "$rc"
    }

    inj=(--inject "$fs_dir/stamp.bin" --inject-delay 100)
    rc_sna=$(fs_save "$fs_dir/state.sna"  "${inj[@]}")
    rc_sna2=$(fs_save "$fs_dir/state2.sna" "${inj[@]}")
    rc_szx=$(fs_save "$fs_dir/state.szx"  "${inj[@]}")
    rc_dup=$(fs_save "$fs_dir/dup.sna" --inject "$fs_dir/page5.bin" --inject-delay 100)

    # The 48K form, from a plain 48K boot.
    rc_48=0
    timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
        --delayed-snapshot "$fs_dir/c48.sna" --delayed-snapshot-frames 60 \
        --delayed-automatic-exit-frames 61 >"$fs_dir/c48.log" 2>&1 || rc_48=$?

    for f in state.sna state2.sna state.szx dup.sna c48.sna; do
        [[ -s "$fs_dir/$f" ]] || fs_faults+=("$f was not written")
    done
    [[ "$rc_sna" == 0 && "$rc_sna2" == 0 && "$rc_szx" == 0 && "$rc_dup" == 0 && "$rc_48" == 0 ]] \
        || fs_faults+=("a fixture run exited non-zero (sna=$rc_sna sna2=$rc_sna2 szx=$rc_szx dup=$rc_dup 48k=$rc_48)")

    if [[ ${#fs_faults[@]} -eq 0 ]]; then
        # (5) determinism: the pair really is one machine state.
        cmp -s "$fs_dir/state.sna" "$fs_dir/state2.sna" \
            || fs_faults+=("two runs to frame 150 produced different .sna bytes — the .szx pairing below would prove nothing")

        # (1) libspectrum accepts every form jnext can write.
        "$SNA_PROBE" "$fs_dir/state.sna" >"$fs_dir/p-sna.txt" 2>"$fs_dir/p-sna.err" \
            || fs_faults+=("libspectrum REFUSED the 128K .sna: $(cat "$fs_dir/p-sna.err")")
        "$SNA_PROBE" "$fs_dir/dup.sna" >"$fs_dir/p-dup.txt" 2>"$fs_dir/p-dup.err" \
            || fs_faults+=("libspectrum REFUSED the 147487-byte .sna: $(cat "$fs_dir/p-dup.err")")
        "$SNA_PROBE" "$fs_dir/c48.sna" >"$fs_dir/p-48.txt" 2>"$fs_dir/p-48.err" \
            || fs_faults+=("libspectrum REFUSED the 48K .sna: $(cat "$fs_dir/p-48.err")")
        "$SZX_PROBE" "$fs_dir/state.szx" >"$fs_dir/p-szx.txt" 2>"$fs_dir/p-szx.err" \
            || fs_faults+=("libspectrum REFUSED the .szx of the same state: $(cat "$fs_dir/p-szx.err")")
    fi

    if [[ ${#fs_faults[@]} -eq 0 ]]; then
        # The sizes libspectrum read back, which is also a check that the form
        # jnext chose per machine is the one it claims.
        sz_sna=$(sed -n 's/^size=//p' "$fs_dir/p-sna.txt")
        sz_dup=$(sed -n 's/^size=//p' "$fs_dir/p-dup.txt")
        sz_48=$(sed -n 's/^size=//p' "$fs_dir/p-48.txt")
        [[ "$sz_sna" == 131103 ]] || fs_faults+=("128K .sna is $sz_sna bytes, want 131103")
        [[ "$sz_dup" == 147487 ]] || fs_faults+=("bank-5-paged .sna is $sz_dup bytes, want 147487")
        [[ "$sz_48"  == 49179  ]] || fs_faults+=("48K .sna is $sz_48 bytes, want 49179")

        # (3) the fixture really does make the eight banks distinguishable.
        distinct=$(sed -n 's/^bank[0-7]=//p' "$fs_dir/p-sna.txt" | sort -u | wc -l)
        [[ "$distinct" == 8 ]] \
            || fs_faults+=("only $distinct/8 bank checksums are distinct — a swapped pair of banks would be invisible")

        # (4) the paging byte against a known truth, not just agreement.
        # `:-999` for the same reason the diff above is taken with `|| true`:
        # an empty value would make the arithmetic below a syntax error and
        # abort the row instead of failing it.
        p7ffd=$(sed -n 's/^port7ffd=//p' "$fs_dir/p-sna.txt"); p7ffd=${p7ffd:-999}
        [[ $(( p7ffd & 7 )) == 3 ]] \
            || fs_faults+=("libspectrum read port 0x7FFD=$p7ffd; the program left bank 3 paged")

        # (2) the two formats' views of one machine state must agree.
        # The diff is taken into a variable with `|| true` FIRST: `diff` exits
        # non-zero when the files differ, and under this file's `set -e` +
        # pipefail an unguarded `$(diff ... | tr ...)` inside the fault message
        # aborted the whole row instead of failing it — a harness bug that
        # appears only on the failing path, which is the one nobody exercises
        # while everything is green. Found by mutating the saver's bank order.
        grep -vE '^(machine|size)=' "$fs_dir/p-sna.txt" > "$fs_dir/cmp-sna.txt"
        grep -vE '^(machine|size)=' "$fs_dir/p-szx.txt" > "$fs_dir/cmp-szx.txt"
        fs_delta=$(diff "$fs_dir/cmp-sna.txt" "$fs_dir/cmp-szx.txt" | tr '\n' ' ' || true)
        [[ -z "$fs_delta" ]] \
            || fs_faults+=("libspectrum's view of the .sna differs from its view of the .szx of the same state: $fs_delta")
    fi

    if [[ ${#fs_faults[@]} -gt 0 ]]; then
        fail_row " ($(IFS=';'; echo "${fs_faults[*]}"))"
    else
        pass_row " (libspectrum accepts all three forms — 49179, 131103 and 147487 — and its view of the 128K .sna matches its view of the .szx of the same state in every register, the border, port 0x7FFD and all 8 bank checksums, which the fixture makes distinct; note it reads a 128K .sna as MACHINE_PENT, since the format carries no machine id)"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
