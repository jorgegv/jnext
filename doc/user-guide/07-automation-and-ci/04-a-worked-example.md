# 7.4 A worked example

Three files: a manifest of tests, a runner, and a directory of references.
This mirrors JNEXT's own layout — `test/00regression/regression_tests.conf`,
`test/00regression/scripts/screenshots.sh`, and `test/00regression/img/`.

## The manifest

`tests.conf` — one line per capture. JNEXT's own manifest has exactly these
columns:

```
# name          machine  program      frames  [extra jnext args]
title-screen    next     myprog.nex   150
level-two       next     myprog.nex   250     --delayed-keypress-frames 100 space
```

## The runner

`screentest.sh`:

```bash
#!/usr/bin/env bash
# Screenshot regression runner. Usage:
#   ./screentest.sh            compare against the references
#   ./screentest.sh --update   (re)generate the references
set -euo pipefail

JNEXT=${JNEXT:-jnext}
CONF=tests.conf
REF_DIR=ref
OUT_DIR=out
TOLERANCE=${TOLERANCE:-0}
UPDATE=false
[[ "${1:-}" == "--update" ]] && UPDATE=true

mkdir -p "$REF_DIR" "$OUT_DIR"
fail=0

while read -r name machine program frames extra; do
    [[ -z "$name" || "$name" == \#* ]] && continue

    out="$OUT_DIR/$name.png"
    ref="$REF_DIR/$name-reference.png"
    rm -f "$out"

    # shellcheck disable=SC2086  # extra args are deliberately word-split
    timeout --kill-after=5s 120s \
        "$JNEXT" --headless \
            --machine "$machine" \
            --load "$program" \
            --rtc "2026-01-01 00:00:00" \
            --delayed-screenshot "$out" \
            --delayed-screenshot-frames "$frames" \
            --delayed-automatic-exit-frames $(( frames + 50 )) \
            $extra >/dev/null 2>&1

    printf '  %-20s ' "[$name]"

    if $UPDATE; then
        cp "$out" "$ref"; echo "UPDATED"; continue
    fi

    diff_px=$(compare -metric AE "$out" "$ref" /dev/null 2>&1 || true)
    diff_px=$(awk '{printf "%d", $1+0}' <<< "$diff_px")

    if [[ "$diff_px" -le "$TOLERANCE" ]]; then
        echo "PASS (identical)"
    else
        echo "FAIL (differs, AE=$diff_px)"
        compare "$out" "$ref" "$OUT_DIR/$name-diff.png" 2>/dev/null || true
        fail=$(( fail + 1 ))
    fi
done < "$CONF"

exit $(( fail > 0 ))
```

`compare` is ImageMagick. The awk step exists because `compare -metric AE`
prints its count in scientific notation on some builds; JNEXT's own suite uses
the same guard (`png_diff` in `test/00regression/test-functions.inc`).

If the emulator produced no PNG at all, `compare` fails and the run is counted
as a failure — which is exactly right, and is why the never-silently-missed
contract of §7.3 matters.

## Using it

Capture the references once, from a build you have looked at and believe:

```console
$ ./screentest.sh --update
  [title-screen]       UPDATED
```

Commit `ref/` alongside your source. From then on, every run compares:

```console
$ ./screentest.sh
  [title-screen]       PASS (identical)
$ echo $?
0
```

And when the rendering changes:

```console
$ ./screentest.sh
  [title-screen]       FAIL (differs, AE=2734140000)
$ echo $?
1
```

`out/title-screen-diff.png` is written on failure, with the differing pixels
in red:

![Diff image: differing pixels in red over a faded original](../img/07-screenshot-diff.png)

**Never regenerate a reference to make a test pass.** Regenerate only when you
have decided the new output is correct, and look at the diff first. JNEXT
treats this as a hard rule for its own suite; the `--update` mode exists for
deliberate changes, not for silencing failures.

## Wiring it into a build

A make target:

```make
# Run the screenshot tests
test: myprog.nex
	./screentest.sh
```

And a GitHub Actions job:

```yaml
jobs:
  screenshots:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get install -y imagemagick
      # ...install jnext, build myprog.nex...
      - run: ./screentest.sh
      - uses: actions/upload-artifact@v4
        if: failure()
        with:
          name: screenshot-diffs
          path: out/
```

Uploading `out/` on failure is worth the two lines: a red build then comes
with the actual picture of what changed.

## One thing CI needs first

Tests that run the `next` machine need a NextZXOS SD-card image. On a fresh
runner there is none, and the first test would race to fetch it. Provision it
once, before any test:

```bash
jnext --headless --sdcard-download-confirm --delayed-automatic-exit 2
```

This is what JNEXT's suite does in
`test/00regression/scripts/01-sdcard-provision.sh`. Tests for the `48k`,
`128k` and `plus3` machines still need the image for their ROMs, so provision
it regardless of which machine you target.
