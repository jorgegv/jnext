#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# A matching <stem>.sld attaches automatically. This uses standard SLD records
# without JNEXT's optional binary-identity comments.
if want source-map-func; then
    begin_func source-map-func
    source_nex="$TMP_DIR/source-sidecar.nex"
    source_file="$TMP_DIR/source-sidecar.sld"
    cp "$PROJECT_DIR/test/00regression/nex/menu.nex" "$source_nex"
    printf '%s\n' \
        '|SLD.data.version|1' \
        'main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344' \
        'main.bas|1||0|4|32768|T|' > "$source_file"
    source_output=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 15s \
        "$JNEXT" --machine next "${SD_CARD_ARGS[@]}" --silent \
        --load "$source_nex" --delayed-automatic-exit-frames 10 2>&1) || true
    if echo "$source_output" | grep -Fq \
        "Loaded 1 SLD source traces from '$source_file'"; then
        pass_row " (program-specific SLD loaded automatically)"
    else
        fail_row " (SLD source-map sidecar was not loaded)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
