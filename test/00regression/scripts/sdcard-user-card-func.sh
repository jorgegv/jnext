#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #555: the suites boot their OWN card (the test master, ~/.jnext/sdcard/test-suite/sdcard/),
# never the user's. The SD master gate deletes and re-derives its card whenever the distribution
# or the recipe moves, so a gate aimed at the user's card would destroy the files a developer kept
# there. Measured here with the real tools in a scratch HOME holding a SENTINEL user card that
# looks CURRENT (this tree's recipe and distribution, a matching witness): a gate keyed on the
# user's card would call the master pristine and do nothing, so the only thing that can make the
# probe say "derive" is that it looked at the test master, which is missing. Then the real jnext
# writes a file into a default card and must not print the old "DEFAULT SD-card image" warning,
# which was true only while the suites shared that card.
# Oracle: the issue's requirement that no test or build step touches the user's card.
if want sdcard-user-card-func; then
    begin_func sdcard-user-card-func

    faults=()
    W="$RUN_DIR/private/sdcard-user-card"
    H="$W/home"
    rm -rf "$W"; mkdir -p "$H/.jnext/sdcard" "$W/cfg/sdcard"
    UC="$H/.jnext/sdcard/cspect-next-1gb-fixed.img"
    want_recipe=$(sd_expected_recipe) || want_recipe=""
    want_distro=$(sd_expected_distro) || want_distro=""
    [[ -n "$want_recipe" && -n "$want_distro" ]] || faults+=("sd_expected_recipe/distro could not read the provisioner")
    printf 'USER-CARD\n' > "$UC"
    printf '%s\n' "$want_recipe" > "$UC.recipe"
    printf '%s\n' "$want_distro" > "$UC.distro"
    printf '%s  cspect-next-1gb-fixed.img\n' "$(sha256sum "$UC" | cut -d' ' -f1)" > "$UC.sha256"
    chmod 0444 "$UC"; touch -d '2020-01-02 03:04:05' "$UC"
    sig() { stat -c '%s:%a:%Y' "$UC"; cat "$UC.recipe" "$UC.distro" "$UC.sha256"; }
    before=$(sig)

    probe() {   # probe: exit status of the master probe in the scratch HOME
        local r=0
        env -u JNEXT_REGRESSION_SD_STATE_DIR HOME="$H" bash "$PROJECT_DIR/tools/sd-master-ensure.sh" --probe >"$W/probe.out" 2>&1 || r=$?
        echo "$r"
    }
    # 1 - no test master yet: the probe says "would derive" (10), whatever the user's card looks like.
    rc=$(probe)
    [[ "$rc" == 10 ]] || faults+=("the probe exited $rc with no test master (want 10: it must not judge the user's card)")
    [[ "$(sig)" == "$before" ]] || faults+=("the probe changed the user's card")

    # 2 - a current test master: the probe says nothing to do (0); the user's card is still untouched.
    TM="$H/.jnext/sdcard/test-suite/sdcard/cspect-next-1gb-fixed.img"
    mkdir -p "$(dirname "$TM")"
    printf 'TEST-MASTER\n' > "$TM"
    printf '%s\n' "$want_recipe" > "$TM.recipe"
    printf '%s\n' "$want_distro" > "$TM.distro"
    printf '%s  cspect-next-1gb-fixed.img\n' "$(sha256sum "$TM" | cut -d' ' -f1)" > "$TM.sha256"
    rc=$(probe)
    [[ "$rc" == 0 ]] || faults+=("the probe exited $rc with a current test master (want 0)")
    [[ "$(sig)" == "$before" ]] || faults+=("a current test master still disturbed the user's card")

    # 3 - a drifted test master is re-derived in ITS directory, the user's card stays as it was
    #     (the stub derives; it is told where by JNEXT_CONFIG_DIR, as sd_rederive does).
    cat > "$W/jnext-stub" <<STUB
#!/usr/bin/env bash
d="\$JNEXT_CONFIG_DIR/sdcard"; mkdir -p "\$d"
printf 'DERIVED\n' > "\$d/cspect-next-1gb-fixed.img"
printf '%s\n' '$want_recipe' > "\$d/cspect-next-1gb-fixed.img.recipe"
printf '%s\n' '$want_distro' > "\$d/cspect-next-1gb-fixed.img.distro"
STUB
    chmod +x "$W/jnext-stub"
    printf 'drift\n' >> "$TM"
    env -u JNEXT_REGRESSION_SD_STATE_DIR HOME="$H" bash "$PROJECT_DIR/tools/sd-master-ensure.sh" "$W/jnext-stub" >"$W/ensure.out" 2>&1 || faults+=("the gate failed on a drifted test master")
    [[ "$(head -n1 "$TM")" == DERIVED ]] || faults+=("a drifted test master was not re-derived in the test-suite directory")
    [[ "$(sig)" == "$before" && "$(cat "$UC")" == USER-CARD ]] || faults+=("re-deriving the test master disturbed the user's card")

    # 4 - the real binary: --sdcard-file-add into a default card says nothing about a shared image.
    cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" "$W/cfg/sdcard/cspect-next-1gb-fixed.img"
    printf 'payload\n' > "$W/payload.bin"
    rc=0
    out=$(JNEXT_CONFIG_DIR="$W/cfg" timeout --foreground --kill-after=5s 120s "$JNEXT" \
          --sdcard-file-add "$W/payload.bin" --sdcard-file-dest /UCARD.BIN 2>&1) || rc=$?
    [[ "$rc" == 0 ]] || faults+=("--sdcard-file-add into a default card exited $rc")
    grep -q "DEFAULT SD-card image" <<< "$out" && faults+=("--sdcard-file-add into a default card printed the shared-image warning")
    grep -qF "Copied" <<< "$out" || faults+=("--sdcard-file-add into a default card did not report the copy")

    rm -rf "$W"
    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (the gate looks at the test master only; a current-looking user card stays byte-identical, same mode and mtime; no shared-image warning)"
    else
        fail_row " (${#faults[@]} fault(s): the suites touched the user's card or kept the old warning)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
