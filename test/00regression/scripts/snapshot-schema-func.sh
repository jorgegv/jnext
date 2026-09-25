#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #27 S9 — §13.2(2) and §13.2(3)/(4), applied to REAL FILES.
#
# WHAT THIS ADDS OVER `make schema-check`. That gate proves the committed
# schema is a valid draft 2020-12 schema, that a manifest transcribed from the
# design document validates, and that 22 deliberate faults are rejected. All
# three use a manifest written BY HAND. Nothing in the tree validated a file
# jnext actually produced — which is §13.2(3)'s wording exactly: "an
# independent JSON Schema validator, ON EVERY WRITTEN FILE".
#
# The two halves, neither of them our code:
#
#  (2) `unzip -t` / `zipfile.testzip()` verify the central directory, the local
#      headers, the sizes and every per-member CRC-32. This removes the whole
#      CONTAINER FRAMING class — which is where the `.szx` bug of §13.1 lived,
#      a structural field our own reader was too permissive about.
#  (3)+(4) Python `jsonschema` validates the real manifest against the
#      committed schema, INCLUDING the hand-written constraint overlay. The
#      overlay is external knowledge (§13.2(4)) and is what stops (3) being
#      circular: the generated half cannot express an invariant the generator
#      does not know.
#
# THE MUTATION LEG IS NOT OPTIONAL HERE. A schema with no constraints validates
# everything and would pass both halves above forever. So the row also relabels
# a Next manifest as `48k` and requires the overlay's §4.3(2) invariant —
# `mem/multiface-ram.bin` present IFF the machine is not a Next — to reject it.
if want snapshot-schema-func; then
    begin_func snapshot-schema-func
    SCHEMA="$PROJECT_DIR/doc/formats/jns-snapshot.schema.json"
    if ! command -v python3 >/dev/null 2>&1; then
        skip_row " (no python3)"
    elif ! python3 -c 'import jsonschema' >/dev/null 2>&1; then
        skip_row " (python jsonschema absent — the independent validator IS this row; CI installs it)"
    elif [[ ! -f "$SCHEMA" ]]; then
        fail_row " (doc/formats/jns-snapshot.schema.json missing)"
    else
    nx="$TMP_DIR/schema-next.jns"; k48="$TMP_DIR/schema-48k.jns"
    rm -f "$nx" "$k48"

    nx_rc=1
    timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
        --delayed-snapshot "$nx" --delayed-snapshot-frames 120 \
        --delayed-automatic-exit 25 >/dev/null 2>&1 && nx_rc=0
    k48_rc=1
    timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
        --delayed-snapshot "$k48" --delayed-snapshot-frames 60 \
        --delayed-automatic-exit 20 >/dev/null 2>&1 && k48_rc=0

    # §13.2(2) — an independent ZIP reader, on every written file.
    unzip_ok=0
    if command -v unzip >/dev/null 2>&1; then
        if unzip -t "$nx" >/dev/null 2>&1 && unzip -t "$k48" >/dev/null 2>&1; then
            unzip_ok=1
        fi
    else
        # zipfile.testzip() is the same check by a different implementation, so
        # a host without `unzip` still gets (2) rather than silently skipping it.
        python3 - "$nx" "$k48" >/dev/null 2>&1 <<'PY' && unzip_ok=1
import sys, zipfile
for p in sys.argv[1:]:
    with zipfile.ZipFile(p) as z:
        if z.testzip() is not None:
            raise SystemExit(1)
PY
    fi

    # §13.2(3)+(4) — the real manifests against the committed schema, and the
    # overlay's invariant proved to BITE.
    schema_out=$(python3 "$PROJECT_DIR/test/snapshot/verify_written.py" \
                     "$SCHEMA" "$nx" "$k48" 2>&1 || true)
    schema_ok=0
    grep -q '^ALL-OK' <<<"$schema_out" && schema_ok=1

    if [[ "$nx_rc" -eq 0 && "$k48_rc" -eq 0 && "$unzip_ok" -eq 1 && "$schema_ok" -eq 1 ]]; then
        pass_row " (real Next + 48K archives pass an independent ZIP integrity check and validate against the committed schema + overlay; the §4.3(2) multiface invariant rejects a relabelled manifest)"
    else
        fail_row " (nx_rc=$nx_rc k48_rc=$k48_rc unzip=$unzip_ok schema=$schema_ok out='$schema_out')"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
