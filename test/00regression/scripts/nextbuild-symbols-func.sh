#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# A matching <stem>.Memory.txt attaches when the program starts, without
# opening the debugger or using a file dialog.
if want nextbuild-symbols-func; then
    begin_func nextbuild-symbols-func
    symbol_nex="$TMP_DIR/symbol-sidecar.nex"
    symbol_file="$TMP_DIR/symbol-sidecar.Memory.txt"
    cp "$PROJECT_DIR/test/00regression/nex/menu.nex" "$symbol_nex"
    printf '8000: ._Main\n8123: ._PlayerTick\n' > "$symbol_file"
    symbol_output=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 15s \
        "$JNEXT" --machine next "${SD_CARD_ARGS[@]}" --silent \
        --load "$symbol_nex" --delayed-automatic-exit-frames 10 2>&1) || true
    if echo "$symbol_output" | grep -Fq \
        "Loaded 2 NextBuild symbols from '$symbol_file'"; then
        pass_row " (program-specific Memory.txt loaded automatically)"
    else
        fail_row " (NextBuild symbol sidecar was not loaded)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
