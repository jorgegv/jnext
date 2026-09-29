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
    # the condition that hid the mutation from every screenshot row. Like the
    # real jnext it also records the recipe that made the image (GH #284):
    # mk_stub <home> [recipe], defaulting to the one this tree builds.
    WANT_RECIPE=$(sd_expected_recipe) || WANT_RECIPE=""
    mk_stub() {
        mkdir -p "$W/bin" "$1/.jnext/sdcard"
        cat > "$W/bin/jnext-stub" <<STUB
#!/usr/bin/env bash
echo x >> "$W/calls"
printf 'PRISTINE-IMAGE-CONTENT\n' > "$1/.jnext/sdcard/cspect-next-1gb-fixed.img"
printf '%s\n' '${2:-$WANT_RECIPE}' > "$1/.jnext/sdcard/cspect-next-1gb-fixed.img.recipe"
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
    REC="$IMG.recipe"
    [[ -n "$WANT_RECIPE" ]] || faults+=("sd_expected_recipe could not read kFixedImageRecipe")

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

    # 5 — GH #284, THE RECIPE: an image that still matches its witness but was
    #     made by an older recipe must be re-derived, exactly once. Without
    #     this the witness blesses it forever: it was written for that image.
    #     5a is every master made before recipes existed (no sidecar at all),
    #     5b one made by an older recipe that recorded itself.
    run_row "$H" >/dev/null                      # converge: fresh image + witness
    rm -f "$REC"
    n=$(run_row "$H")
    [[ "$n" == "1" ]] || faults+=("an image with NO recipe sidecar invoked jnext $n times, expected 1")
    grep -q "re-derived with recipe" "$W/out" || faults+=("a missing recipe was not reported as the reason")
    printf '%s\n' "$(( ${WANT_RECIPE:-1} - 1 ))" > "$REC"
    n=$(run_row "$H")
    [[ "$n" == "1" ]] || faults+=("an image of an OLDER recipe invoked jnext $n times, expected 1")
    grep -q "made by image recipe $(( ${WANT_RECIPE:-1} - 1 ))" "$W/out" \
        || faults+=("an older recipe was not reported as the reason")
    #   5c — ...and the re-derive converges: the next run touches nothing.
    n=$(run_row "$H")
    [[ "$n" == "0" ]] || faults+=("after a recipe re-derive the next run invoked jnext $n times, expected 0")

    # 6 — a jnext that derives an image of ANOTHER recipe than this tree
    #     builds is not the binary under test (a stale build): FAIL the row,
    #     and leave no witness, so the image is never adopted.
    mk_stub "$H" "$(( ${WANT_RECIPE:-1} - 1 ))"
    rm -f "$REC"
    run_row "$H" >/dev/null
    grep -q "FAIL" "$W/out" || faults+=("an image of the wrong recipe did not fail the row")
    [[ ! -f "$WIT" ]] || faults+=("an image of the wrong recipe was given a witness")
    mk_stub "$H"

    # 7 — provisioning failure must FAIL the row, not pass silently.
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
        pass_row " (derives when absent, repairs when drifted or of an old recipe, does NOTHING when unchanged)"
    else
        fail_row " (${#faults[@]} fault(s) in SD-image provisioning)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
