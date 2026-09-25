#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #27 S9 — the INDEPENDENT, SPEC-WRITTEN reader (design §13.2, §13.3).
#
# WHAT THIS ANSWERS. §13.1: jnext's `.szx` saver once wrote RAM pages 0-111 and
# its own loader accepted them, so save/load/compare passed byte-exact with
# discriminative, mutation-tested assertions — all green, all worthless,
# because libspectrum rejects any page > 63 and so **every `.szx` jnext could
# produce** failed to load in real FUSE. Saver and loader shared the blind
# spot, which made the defect structurally invisible to the suite as written.
#
# `test/snapshot/jns_reader.py` is a second implementation built from
# `doc/design/NEXT-SNAPSHOT-FORMAT.md` and nothing else. It imports no jnext
# code. Its value is exactly that it can disagree — and on its first run it
# did, refusing every file jnext produces because §7.4 said "every 64-bit field
# is a string, unconditionally" while §8's own manifest example showed
# `"frame": 41291` as a number. The spec now states the scope (§6.2).
#
# WHAT IT IS NOT (§13.3, kept honest here rather than implied): this is a
# SECOND reader in the same repository, not a foreign one. An error in the
# DOCUMENT propagates into both it and the writer. It narrows the hole; it does
# not close it. `snapshot-foreign-fuse-func` is the genuinely foreign
# adjudication, and it reaches only the CPU, the 64 KB map and 128K paging.
if want snapshot-spec-reader-func; then
    begin_func snapshot-spec-reader-func
    READER="$PROJECT_DIR/test/snapshot/jns_reader.py"
    if ! command -v python3 >/dev/null 2>&1; then
        skip_row " (no python3 — the independent reader cannot run)"
    elif [[ ! -f "$READER" ]]; then
        fail_row " (test/snapshot/jns_reader.py missing — the spec-written reader IS this row)"
    else
    nx="$TMP_DIR/spec-next.jns"; k48="$TMP_DIR/spec-48k.jns"
    plain="$TMP_DIR/spec-stored.jns"; broken="$TMP_DIR/spec-broken.jns"
    rm -f "$nx" "$k48" "$plain" "$broken"

    # A Next carrying a real workload, and a 48K — the two machines whose
    # manifests differ in the one structural way §4.3(2) names (the Multiface
    # blob is present iff the machine is not a Next), which the reader checks.
    next_rc=1
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
        --load "$PROJECT_DIR/test/00regression/nex/beast.nex" \
        --delayed-snapshot "$nx" --delayed-snapshot-frames 120 \
        --delayed-automatic-exit 30 >/dev/null 2>&1 && next_rc=0

    k48_rc=1
    timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
        --delayed-snapshot "$k48" --delayed-snapshot-frames 60 \
        --delayed-automatic-exit 20 >/dev/null 2>&1 && k48_rc=0

    # --snapshot-uncompressed: every member STORED. The reader must not care,
    # because the compression method is the container's business and none of
    # the spec's rules mention it.
    plain_rc=1
    timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --snapshot-uncompressed \
        --delayed-snapshot "$plain" --delayed-snapshot-frames 60 \
        --delayed-automatic-exit 20 >/dev/null 2>&1 && plain_rc=0

    accept_next=1;  python3 "$READER" "$nx"    --expect-machine next >/dev/null 2>&1 || accept_next=0
    accept_48k=1;   python3 "$READER" "$k48"   --expect-machine 48k  >/dev/null 2>&1 || accept_48k=0
    accept_plain=1; python3 "$READER" "$plain" --expect-machine 48k  >/dev/null 2>&1 || accept_plain=0

    # THE ROW THAT MAKES THE OTHERS MEAN SOMETHING. A reader that returns 0 for
    # everything would pass all three above. Corrupt one byte of a member's
    # payload: Python's own `zipfile.testzip()` — not our code — must catch the
    # CRC, and the reader must exit non-zero.
    rejects=0
    if [[ -s "$k48" ]]; then
        cp "$k48" "$broken"
        # Flip a byte late in the file, inside a member's compressed data
        # rather than in the central directory at the very end.
        sz=$(stat -c%s "$broken")
        off=$(( sz / 2 ))
        printf '\xff' | dd of="$broken" bs=1 seek="$off" conv=notrunc status=none
        python3 "$READER" "$broken" >/dev/null 2>&1 || rejects=1
    fi

    # And the refusal must NAME THE MEMBER, and must be a REFUSAL rather than
    # a traceback — G9 is a testable property here too, not only in the C++.
    #
    # A traceback names a line of Python, which is not the offending thing.
    # This row is how that was found: `zipfile.testzip()` itself raises on a
    # damaged DEFLATE stream instead of returning the member's name, and the
    # reader did not catch it. The length check the first version of this row
    # used would have passed on the traceback.
    named=0
    if [[ "$rejects" -eq 1 ]]; then
        msg=$(python3 "$READER" "$broken" 2>&1 || true)
        if ! grep -q 'Traceback' <<<"$msg" \
           && grep -q '^REFUSED: ' <<<"$msg" \
           && grep -qE "(state|mem|meta)/[A-Za-z0-9_.-]+" <<<"$msg"; then
            named=1
        fi
    fi

    if [[ "$next_rc" -eq 0 && "$k48_rc" -eq 0 && "$plain_rc" -eq 0 ]] \
       && [[ "$accept_next" -eq 1 && "$accept_48k" -eq 1 && "$accept_plain" -eq 1 ]] \
       && [[ "$rejects" -eq 1 && "$named" -eq 1 ]]; then
        pass_row " (spec-written reader accepts Next + 48K + STORED, and rejects a byte-corrupted archive with a named reason)"
    else
        fail_row " (next_rc=$next_rc k48_rc=$k48_rc plain_rc=$plain_rc accept=$accept_next/$accept_48k/$accept_plain rejects=$rejects named=$named msg='${msg:-<unset>}')"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
