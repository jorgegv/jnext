#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Sibling of esxdos-chain-red-func — see that script for the chain-load design.
MENU_NEX="$PROJECT_DIR/test/00regression/nex/menu.nex"

if want esxdos-chain-blue-func; then
    begin_func esxdos-chain-blue-func
    out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$MENU_NEX" \
        --delayed-keypress-frames 100 2 \
        --delayed-automatic-exit-frames 220 2>&1) || true
    req=$(echo "$out" | grep -cE "requested \.RUN '.*blue\.nex'" || true)
    got=$(echo "$out" | grep -cE "NEX: loaded '.*blue\.nex'" || true)
    if [[ "$req" -ge 1 && "$got" -ge 1 ]]; then
        pass_row " (menu -> key 2 -> chain-loaded blue.nex)"
    else
        fail_row " (blue .RUN=$req want>=1, blue load=$got want>=1)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
