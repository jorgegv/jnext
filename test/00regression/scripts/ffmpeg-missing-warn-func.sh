#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# ffmpeg-missing-warn-func: jnext shells out to ffmpeg for --record /
# File > Record MPEG4 Video. At startup it probes for
# ffmpeg and, if absent, warns once (in EVERY mode, headless included) so the
# user is not surprised only when a recording silently fails. Discriminative
# both ways: with ffmpeg masked from PATH the warning MUST fire; with ffmpeg
# present it MUST NOT. The masked run points PATH at a directory with no
# executables (so `ffmpeg -version` cannot resolve); the control run uses the
# real PATH. Both use --headless so no display is needed.
if want ffmpeg-missing-warn-func; then
    begin_func ffmpeg-missing-warn-func
    warn_line="ffmpeg not found in PATH"
    # jnext is addressed by absolute path, so a stripped PATH does not stop it
    # launching — only its own ffmpeg probe fails. `env` sets PATH for the jnext
    # child ONLY; it must come AFTER timeout, or `env PATH=... timeout` would
    # leave `timeout` itself unresolvable on the stripped PATH.
    ff_masked=$(timeout --foreground --kill-after=5s 20s \
        env PATH=/nonexistent-jnext-ffmpeg-test "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 5 2>&1) || true
    ff_present=$(timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 5 2>&1) || true
    masked_hit=$(echo "$ff_masked" | grep -cF "$warn_line" || true)
    present_hit=$(echo "$ff_present" | grep -cF "$warn_line" || true)
    if ! command -v ffmpeg &>/dev/null; then
        # ffmpeg genuinely absent on this host — the control run cannot prove the
        # negative, so only assert the masked run warns. Loud, not silent.
        if [[ "$masked_hit" -ge 1 ]]; then
            pass_row " (warns when ffmpeg absent; control skipped — no ffmpeg on host)"
        else
            fail_row " (masked run did not warn: masked_hit=$masked_hit want>=1)"
        fi
    elif [[ "$masked_hit" -ge 1 && "$present_hit" -eq 0 ]]; then
        pass_row " (warns when ffmpeg masked from PATH; silent when present)"
    else
        fail_row " (masked_hit=$masked_hit want>=1, present_hit=$present_hit want0)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
