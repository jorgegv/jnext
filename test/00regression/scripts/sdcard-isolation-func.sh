#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #65 — every run boots a PRIVATE clone of the SD image, never the shared
# master. NextZXOS writes back to the card it boots, so before this the suite
# mutated the very artefact its reference screenshots were generated against; a
# drift once turned a green branch into 91 pass / 5 FAIL, indistinguishable from
# a rendering regression. It also forced runs to be serialised.
#
# WHY THESE PARTICULAR ASSERTIONS. The lesson from sdcard-provision-func is that
# an artefact-shaped assertion proves nothing here: a reviewer inverted that
# row's hash comparison so it ALWAYS rewrote the image, defeating the whole
# protection, and the entire triplet stayed green because the rewritten image is
# byte-identical. So every row below asserts what the harness DID — which file
# jnext opened, which device the clone is on, whether the master's inode/mtime
# moved, whether the directory still exists after a kill — never what the image
# looks like afterwards, which is identical either way.
#
# Each row is written so that ONE specific way of breaking the design fails it:
#   1. the provisioner stops honouring $JNEXT_CONFIG_DIR (rows fall back to the
#      shared master)                                        -> A, B
#   2. the clone is never made, or JNEXT_CONFIG_DIR does not point at it -> C
#   3. the run directory moves to $TMP_DIR / /tmp, where the reflink cannot
#      happen and a 1 GB copy lands in RAM instead           -> D, E
#   4. a run writes to the master anyway                     -> F
#   5. the EXIT trap is removed or turned into a final step  -> G, H
if want sdcard-isolation-func; then
    begin_func sdcard-isolation-func

    W="$TMP_DIR/sdiso.$$"
    mkdir -p "$W"
    faults=()

    # --- A/B: does jnext ACTUALLY resolve its image through $JNEXT_CONFIG_DIR?
    # Point it at a scratch directory holding a file with the fallback image's
    # name. jnext logs "sdcard: using default image <path>" for whichever file
    # it picked, so the log names the file it opened. If the provisioner ignored
    # the variable it would name the master instead — and every screenshot row
    # in this suite would silently be booting the shared card.
    mkdir -p "$W/cfg/sdcard"
    printf 'NOT-A-REAL-IMAGE\n' > "$W/cfg/sdcard/cspect-next-1gb-fixed.img"
    # A bogus image cannot boot; --delayed-automatic-exit bounds it either way
    # and the exit status is deliberately not asserted — only the resolution is.
    JNEXT_CONFIG_DIR="$W/cfg" timeout 60 "$JNEXT" --headless --machine next \
        --delayed-automatic-exit 1 > "$W/resolve.log" 2>&1 || true
    grep -qF "$W/cfg/sdcard/cspect-next-1gb-fixed.img" "$W/resolve.log" \
        || faults+=("A: jnext did not resolve its SD image through \$JNEXT_CONFIG_DIR")
    grep -qF "$SD_MASTER_IMAGE" "$W/resolve.log" \
        && faults+=("B: jnext fell back to the shared master image $SD_MASTER_IMAGE")

    # --- C: this run's clone exists, at exactly the path jnext will resolve,
    # and is a DISTINCT file from the master (not a symlink or a hard link, both
    # of which would still let a write reach the master).
    CLONE="$JNEXT_CONFIG_DIR/sdcard/cspect-next-1gb-fixed.img"
    if [[ ! -f "$CLONE" ]]; then
        faults+=("C: no per-run clone at $CLONE — the run would boot the master")
    else
        m_ino=$(stat -c '%i' "$SD_MASTER_IMAGE")
        c_ino=$(stat -c '%i' "$CLONE")
        [[ "$m_ino" != "$c_ino" ]] \
            || faults+=("C: the clone shares the master's inode ($c_ino) — writes reach the master")
        [[ "$(stat -c '%s' "$CLONE")" == "$(stat -c '%s' "$SD_MASTER_IMAGE")" ]] \
            || faults+=("C: the clone is not the same size as the master (truncated copy)")

        # --- D: same FILESYSTEM as the master. This is the assertion that
        # catches "put the run dir in $TMP_DIR": reflink is a filesystem-
        # internal operation, so across a boundary `cp --reflink=auto` silently
        # degrades to a real 1 GB copy — and on this host /tmp is tmpfs, i.e.
        # a gigabyte of RAM per concurrent run.
        [[ "$(stat -c '%d' "$CLONE")" == "$(stat -c '%d' "$SD_MASTER_IMAGE")" ]] \
            || faults+=("D: the clone is on a different filesystem from the master — reflink is impossible there")

        # --- E: where the filesystem CAN reflink, the clone must be reflinked
        # and not bulk-copied. Probed rather than assumed, so the row is honest
        # on a filesystem (CI's overlayfs, ext4 without extents) that cannot.
        probe="$JNEXT_CONFIG_DIR/sdcard/.reflink-probe"
        printf 'probe\n' > "$probe"
        if cp --reflink=always "$probe" "$probe.copy" 2>/dev/null; then
            [[ "$SD_CLONE_MODE" == "reflink" ]] \
                || faults+=("E: this filesystem supports reflink but the clone was made by $SD_CLONE_MODE")
        fi
        rm -f "$probe" "$probe.copy"
    fi

    # --- F: nothing in this run touched the master. One more NextZXOS boot
    # first — the exact operation that used to dirty the shared card, and it
    # also proves the clone is a bootable image and not a plausible-looking
    # 1 GB file. Then compare the master's inode/mtime/size against the stamp
    # taken right after the provisioning gate, so the assertion covers EVERY
    # boot this run performed (all the screenshot rows included), not just this
    # one. This row is declared LAST in functional_tests.conf so that its own
    # boot cannot perturb a later row.
    #
    # Content is deliberately NOT hashed: hashing 1 GB costs a second and proves
    # less, because a rewrite with byte-identical content is precisely the
    # failure mode that already fooled this suite once. inode+mtime catches
    # that; a hash does not.
    timeout 90 "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
        --delayed-automatic-exit 6 > "$W/boot.log" 2>&1 || true
    m_after=$(stat -c '%i:%Y:%s' "$SD_MASTER_IMAGE" 2>/dev/null || echo absent)
    [[ "$SD_MASTER_STAMP" != "absent" ]] \
        || faults+=("F: no master stamp was taken — the assertion below proves nothing")
    [[ "$SD_MASTER_STAMP" == "$m_after" ]] \
        || faults+=("F: the run modified the MASTER image ($SD_MASTER_STAMP -> $m_after)")

    # --- G/H: cleanup is a TRAP, not a final step. Drive a child suite shell
    # that initializes the lib (creating its own run directory) and then dies
    # from a signal, exactly like a timed-out or Ctrl-C'd run. A `rm -rf` placed
    # at the end of a script would never execute here, leaking a gigabyte per
    # kill; the trap does.
    cat > "$W/child.sh" <<CHILD
#!/usr/bin/env bash
set -euo pipefail
source "$SCRIPT_DIR/test-functions.inc"
echo "\$RUN_DIR" > "$W/child_run_dir"
kill -TERM \$\$
sleep 30
CHILD
    chmod +x "$W/child.sh"
    # The outer redirect silences the shell's own "Terminated" notice about the
    # signal-killed child — which is the expected outcome here, not a problem.
    { bash "$W/child.sh" >/dev/null 2>&1 || true; } 2>/dev/null
    if [[ ! -s "$W/child_run_dir" ]]; then
        faults+=("G: the child run never reported a run directory")
    else
        child_dir=$(< "$W/child_run_dir")
        [[ "$child_dir" != "$RUN_DIR" ]] \
            || faults+=("G: the child run reused THIS run's directory — run dirs are not unique")
        [[ ! -d "$child_dir" ]] \
            || faults+=("H: a SIGTERM-killed run leaked its directory $child_dir (cleanup is not a trap)")
    fi

    rm -rf "$W"

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (boots a private $SD_CLONE_MODE clone; master untouched; run dir trap-removed)"
    else
        fail_row " (${#faults[@]} fault(s) in per-run SD-image isolation)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
