#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# 01-sdcard-provision.sh re-derives the NextZXOS SD image when it has drifted,
# and — critically — does NOTHING when it has not.
#
# That second half is the whole safety property. CLAUDE.md has reviewers running
# regression cycles concurrently in separate worktrees; they share $HOME, hence
# one 1 GB image. Today that is safe only because after provisioning the file is
# nothing but READ. If the row rewrote it every run, it would truncate the image
# under another run's parallel screenshot rows.
#
# WHY THIS TEST EXISTS: the property was invisible without it. An independent
# review mutated the hash comparison from == to != — so the row ALWAYS rewrites,
# defeating the entire protection — and the full triplet stayed green
# (5340/5340, 1356/1356, 96/96), because the re-derived image is byte-identical
# and every screenshot therefore still matched. "The suite runs this code every
# time" is not the same as "the suite tests this code".
#
# So the assertions are about WHAT THE ROW DID, not what the image looks like
# afterwards: with a stub in place of jnext, the only honest question is how
# many times it was invoked.
#
# Runs against a scratch HOME and a stub $JNEXT: no network, no 1 GB image, no
# dependency on the real machine-wide card.
if want sdcard-provision-func; then
    begin_func sdcard-provision-func

    ROW="$SCRIPT_DIR/scripts/01-sdcard-provision.sh"
    W="$TMP_DIR/sdprov.$$"
    faults=()

    # A stub standing in for jnext: records each invocation and produces an
    # "image" whose content is fixed, so a re-derivation is byte-identical to
    # the previous one — exactly like the real FF_FS_NORTC=1 build, and exactly
    # the condition that hid the mutation from every screenshot row.
    mk_stub() {
        mkdir -p "$W/bin" "$1/.jnext/sdcard"
        cat > "$W/bin/jnext-stub" <<STUB
#!/usr/bin/env bash
echo x >> "$W/calls"
printf 'PRISTINE-IMAGE-CONTENT\n' > "$1/.jnext/sdcard/cspect-next-1gb-fixed.img"
exit 0
STUB
        chmod +x "$W/bin/jnext-stub"
    }

    # run_row <home> — drive the row with a scratch HOME and the stub, and echo
    # how many times the stub was called.
    run_row() {
        : > "$W/calls"
        HOME="$1" JNEXT="$W/bin/jnext-stub" bash "$ROW" >"$W/out" 2>&1 || true
        wc -l < "$W/calls" | tr -d ' '
    }

    rm -rf "$W"; H="$W/home"; mk_stub "$H"
    IMG="$H/.jnext/sdcard/cspect-next-1gb-fixed.img"
    WIT="$IMG.sha256"

    # 1 — nothing present: must derive (exactly one invocation) and record a witness.
    n=$(run_row "$H")
    [[ "$n" == "1" ]]  || faults+=("cold start invoked jnext $n times, expected 1")
    [[ -f "$WIT" ]]    || faults+=("cold start recorded no witness")

    # 2 — THE ONE THAT MATTERS: unchanged image must invoke jnext ZERO times and
    #     leave the file untouched. A row that rewrites here is the concurrency
    #     hazard this design exists to avoid, and it is invisible downstream
    #     because the rewrite produces identical bytes.
    before=$(stat -c '%i:%Y:%s' "$IMG")
    n=$(run_row "$H")
    after=$(stat -c '%i:%Y:%s' "$IMG")
    [[ "$n" == "0" ]]            || faults+=("fast path invoked jnext $n times, expected 0 (it rewrites when it should not)")
    [[ "$before" == "$after" ]]  || faults+=("fast path modified the image (inode/mtime/size changed)")

    # 3 — contaminated image: must re-derive, exactly once.
    printf 'TAMPERED\n' >> "$IMG"
    n=$(run_row "$H")
    [[ "$n" == "1" ]] || faults+=("a modified image invoked jnext $n times, expected 1")
    grep -q "MODIFIED" "$W/out" || faults+=("repair did not report that the image had been modified")

    # 4 — missing witness must RE-DERIVE, never adopt what is on disk. Adopting
    #     would permanently bless an already-corrupt image while looking like
    #     protection.
    printf 'CORRUPT\n' >> "$IMG"
    rm -f "$WIT"
    n=$(run_row "$H")
    [[ "$n" == "1" ]] || faults+=("a missing witness invoked jnext $n times, expected 1 (it adopted the on-disk image)")

    # 5 — provisioning failure must FAIL the row, not pass silently.
    cat > "$W/bin/jnext-stub" <<'STUB'
#!/usr/bin/env bash
exit 1
STUB
    chmod +x "$W/bin/jnext-stub"
    rm -f "$IMG" "$WIT"
    run_row "$H" >/dev/null
    grep -q "FAIL" "$W/out" || faults+=("provisioning failure did not fail the row")

    rm -rf "$W"

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (derives when absent, repairs when drifted, does NOTHING when unchanged)"
    else
        fail_row " (${#faults[@]} fault(s) in SD-image provisioning)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
