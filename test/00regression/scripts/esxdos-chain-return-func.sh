#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Sibling of esxdos-chain-red-func — see that script for the chain-load design.
MENU_NEX="$PROJECT_DIR/test/00regression/nex/menu.nex"

if want esxdos-chain-return-func; then
    begin_func esxdos-chain-return-func
    out=$(timeout --foreground --kill-after=5s 40s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$MENU_NEX" \
        --delayed-keypress-frames 80 2 --delayed-keypress-frames 200 m \
        --delayed-automatic-exit-frames 320 2>&1) || true
    to_blue=$(echo "$out" | grep -cE "requested \.RUN '.*blue\.nex'" || true)
    back=$(echo "$out" | grep -cE "requested \.RUN '.*menu\.nex'" || true)
    # menu.nex loads once at the initial --load and again on the return hop, so
    # a count of >=2 is what proves the round-trip actually chained back.
    menu_loads=$(echo "$out" | grep -cE "NEX: loaded '.*menu\.nex'" || true)
    if [[ "$to_blue" -ge 1 && "$back" -ge 1 && "$menu_loads" -ge 2 ]]; then
        pass_row " (menu -> blue -> menu round-trip via .RUN)"
    else
        fail_row " (blue .RUN=$to_blue want>=1, menu .RUN=$back want>=1, menu loads=$menu_loads want>=2)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
