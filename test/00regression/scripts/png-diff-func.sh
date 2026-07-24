#!/usr/bin/env bash
# Pin png_diff() to a literal differing-pixel count. Sourced by regression.sh;
# also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

if want png-diff-func; then
    begin_func png-diff-func
    if ! $HAS_COMPARE; then
        skip_row " (no ImageMagick)"
    else
        if command -v magick &>/dev/null; then
            image_cmd=(magick)
        elif command -v convert &>/dev/null; then
            image_cmd=(convert)
        else
            fail_row " (compare exists, but no ImageMagick image command was found)"
            image_cmd=()
        fi

        if [[ ${#image_cmd[@]} -gt 0 ]]; then
            base="$TMP_DIR/png-diff-base.png"
            one="$TMP_DIR/png-diff-one.png"
            red="$TMP_DIR/png-diff-red.png"
            two="$TMP_DIR/png-diff-two.png"
            if ! "${image_cmd[@]}" -size 4x3 xc:black "$base" 2>/dev/null \
                || ! "${image_cmd[@]}" "$base" -fill white \
                    -draw 'point 1,1' "$one" 2>/dev/null \
                || ! "${image_cmd[@]}" "$base" -fill red \
                    -draw 'point 1,1' "$red" 2>/dev/null \
                || ! "${image_cmd[@]}" "$base" -fill white \
                    -draw 'point 1,1 point 2,1' "$two" 2>/dev/null; then
                fail_row " (could not create synthetic image fixtures)"
            else
                same_count=$(png_diff "$base" "$base")
                one_count=$(png_diff "$base" "$one")
                red_count=$(png_diff "$base" "$red")
                two_count=$(png_diff "$base" "$two")
                missing_count=$(png_diff "$base" "$TMP_DIR/absent.png" 424242)
                if [[ "$same_count" -eq 0 && "$one_count" -eq 1 \
                      && "$red_count" -eq 1 && "$two_count" -eq 2 \
                      && "$missing_count" -eq 424242 ]]; then
                    pass_row " (0 identical; 1 single-channel/full-colour; 2 two-pixel; failure sentinel preserved)"
                else
                    fail_row " (same=$same_count one=$one_count red=$red_count two=$two_count missing=$missing_count)"
                fi
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
