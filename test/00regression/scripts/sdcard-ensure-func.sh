#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #476: tools/sd-master-ensure.sh, which `make gui-release` (tools/pgo-train.sh) and the
# unit-test targets (make sdcard-image) call, runs the regression row's own gate
# (sd_master_gate) so a host left with the previous distribution's master gets the right card
# before anything reads it. The v1.2.39 batch gate failed on exactly such a host: the PGO
# training and sd_rom_extractor_test ran on a 24.11 card.
#
# As in sdcard-provision-func, the assertions are about what the tool DID, measured with a stub
# in place of jnext (one call = one derive) and a scratch HOME: no network, no 1 GB image.
# Oracle: the contract above (stale distro or recipe: derived once; current: untouched).
if want sdcard-ensure-func; then
    begin_func sdcard-ensure-func

    TOOL="$PROJECT_DIR/tools/sd-master-ensure.sh"
    W="$TMP_DIR/sdensure.$$"
    faults=()
    WANT_RECIPE=$(sd_expected_recipe) || WANT_RECIPE=""
    WANT_DISTRO=$(sd_expected_distro) || WANT_DISTRO=""
    [[ -n "$WANT_RECIPE" && -n "$WANT_DISTRO" ]] || faults+=("sd_expected_recipe/distro could not read the provisioner")

    rm -rf "$W"; H="$W/home"; mkdir -p "$W/bin" "$H/.jnext/sdcard"
    cat > "$W/bin/jnext-stub" <<STUB
#!/usr/bin/env bash
echo x >> "$W/calls"
printf 'PRISTINE-IMAGE-CONTENT\n' > "$H/.jnext/sdcard/cspect-next-1gb-fixed.img"
printf '%s\n' '$WANT_RECIPE' > "$H/.jnext/sdcard/cspect-next-1gb-fixed.img.recipe"
printf '%s\n' '$WANT_DISTRO' > "$H/.jnext/sdcard/cspect-next-1gb-fixed.img.distro"
exit 0
STUB
    chmod +x "$W/bin/jnext-stub"
    IMG="$H/.jnext/sdcard/cspect-next-1gb-fixed.img"

    # run_tool <args...>: run the tool on the scratch HOME; echoes the stub's call count and leaves
    # the exit status in $W/rc (the function runs in a command substitution, so no variable survives).
    run_tool() {
        local r=0
        : > "$W/calls"
        HOME="$H" JNEXT_REGRESSION_SD_STATE_DIR="" bash "$TOOL" "$@" >"$W/out" 2>&1 || r=$?
        echo "$r" > "$W/rc"
        wc -l < "$W/calls" | tr -d ' '
    }
    rcof() { cat "$W/rc"; }

    # 1 - no master: derived once.
    n=$(run_tool "$W/bin/jnext-stub")
    [[ "$n" == 1 && -f "$IMG" && -f "$IMG.sha256" ]] || faults+=("no master: stub called $n times (want 1) or no card/witness made")
    [[ "$(rcof)" == 0 ]] || faults+=("no master: exit $(rcof)")

    # 2 - current master: probe says nothing to do; the tool derives nothing and leaves the card as it was.
    before=$(stat -c '%i:%Y:%s' "$IMG")
    n=$(run_tool --probe); prc=$(rcof)
    [[ "$prc" == 0 ]] || faults+=("probe on a current master exited $prc, want 0")
    n=$(run_tool "$W/bin/jnext-stub")
    [[ "$n" == 0 && "$(rcof)" == 0 ]] || faults+=("current master: stub called $n times (want 0), exit $(rcof)")
    [[ "$(stat -c '%i:%Y:%s' "$IMG")" == "$before" ]] || faults+=("current master: inode/mtime/size changed")
    grep -q "is current" "$W/out" || faults+=("current master: not reported as current")

    # 3 - a master made from another distribution (the v1.2.39 host: 24.11): the probe says 10 and
    #     writes nothing; the tool then derives it once, and the next run touches nothing.
    printf 'sn-emulator-24.11.zip\n' > "$IMG.distro"
    before=$(stat -c '%i:%Y:%s' "$IMG")
    n=$(run_tool --probe); prc=$(rcof)
    [[ "$prc" == 10 && "$n" == 0 ]] || faults+=("probe on a stale-distro master: exit $prc (want 10), stub called $n times")
    [[ "$(stat -c '%i:%Y:%s' "$IMG")" == "$before" && "$(head -n1 "$IMG.distro")" == sn-emulator-24.11.zip ]] \
        || faults+=("probe on a stale-distro master changed the card")
    n=$(run_tool "$W/bin/jnext-stub")
    [[ "$n" == 1 && "$(rcof)" == 0 && "$(head -n1 "$IMG.distro")" == "$WANT_DISTRO" ]] \
        || faults+=("stale-distro master: stub called $n times (want 1), exit $(rcof), distro now '$(head -n1 "$IMG.distro")'")
    grep -q "made from sn-emulator-24.11.zip" "$W/out" || faults+=("stale-distro master: the reason was not reported")
    n=$(run_tool "$W/bin/jnext-stub")
    [[ "$n" == 0 ]] || faults+=("after a distro re-derive the next run called the stub $n times, want 0")

    # 4 - a master of an older image recipe: derived once.
    printf '%s\n' "$(( ${WANT_RECIPE:-1} - 1 ))" > "$IMG.recipe"
    n=$(run_tool "$W/bin/jnext-stub")
    [[ "$n" == 1 && "$(rcof)" == 0 && "$(head -n1 "$IMG.recipe")" == "$WANT_RECIPE" ]] \
        || faults+=("stale-recipe master: stub called $n times (want 1), exit $(rcof)")

    # 5 - a jnext that cannot derive: exit non-zero, never success.
    printf '#!/usr/bin/env bash\nexit 1\n' > "$W/bin/jnext-bad"; chmod +x "$W/bin/jnext-bad"
    rm -f "$IMG" "$IMG.sha256"
    n=$(run_tool "$W/bin/jnext-bad")
    [[ "$(rcof)" == 1 ]] || faults+=("a failed derive exited $(rcof), want 1")

    # 6 - the candidate-card seam: the tool does nothing at all.
    : > "$W/calls"; rc=0
    HOME="$H" JNEXT_REGRESSION_SD_STATE_DIR="$W/cand" bash "$TOOL" "$W/bin/jnext-stub" >"$W/out" 2>&1 || rc=$?
    [[ "$rc" == 0 && ! -s "$W/calls" && ! -e "$W/cand" ]] || faults+=("with the candidate-card seam the tool acted (exit $rc)")

    # 7 - the callers use it: the Makefile's sdcard-image target and the PGO training.
    grep -q 'tools/sd-master-ensure.sh --probe' "$PROJECT_DIR/Makefile" || faults+=("make sdcard-image does not probe with tools/sd-master-ensure.sh")
    grep -q 'tools/sd-master-ensure.sh" "$BIN"' "$PROJECT_DIR/tools/pgo-train.sh" || faults+=("tools/pgo-train.sh does not call tools/sd-master-ensure.sh")
    grep -qE '^unit-test:.* sdcard-image$' "$PROJECT_DIR/Makefile" || faults+=("make unit-test does not depend on sdcard-image")

    rm -rf "$W"

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (derives a missing, other-distribution or older-recipe master once; leaves a current one untouched)"
    else
        fail_row " (${#faults[@]} fault(s) in tools/sd-master-ensure.sh)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
