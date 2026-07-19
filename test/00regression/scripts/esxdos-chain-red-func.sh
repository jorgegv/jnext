#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# esxdos-chain-{red,blue,return}-func: the --esxdos-stub M_EXECCMD
# ".RUN <name>.nex" path lets a selector NEX chain-load a SIBLING NEX (same
# directory) without booting NextZXOS. menu.nex shows white and, on key 1/2,
# runs red.nex/blue.nex; red/blue on key M run menu.nex back. Driven headlessly
# with --delayed-keypress-frames and asserted on the stub's .RUN request + the
# resulting NEX load. The keypress schedule and frame counter live in the
# frontend and survive the cold boot the chain triggers, so a second hop (the
# return) is observable in the same run.
MENU_NEX="$PROJECT_DIR/test/00regression/nex/menu.nex"

if want esxdos-chain-red-func; then
    begin_func esxdos-chain-red-func
    out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --esxdos-stub --load "$MENU_NEX" \
        --delayed-keypress-frames 100 1 \
        --delayed-automatic-exit-frames 220 2>&1) || true
    req=$(echo "$out" | grep -cE "requested \.RUN '.*red\.nex'" || true)
    got=$(echo "$out" | grep -cE "NEX: loaded '.*red\.nex'" || true)
    if [[ "$req" -ge 1 && "$got" -ge 1 ]]; then
        pass_row " (menu -> key 1 -> chain-loaded red.nex)"
    else
        fail_row " (red .RUN=$req want>=1, red load=$got want>=1)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
