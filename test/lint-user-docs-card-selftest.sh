#!/usr/bin/env bash
# Self-test of test/lint-user-docs-card.sh (GH #458). Prerequisite of `make unit-test`.
# Works on a temporary copy of the files the lint scopes; the real tree is never touched.
#
#   LUDC-01        the real tree passes
#   LUDC-02a..l    each banned pattern, planted alone in a user-guide page, fails (exit 1)
#                  and the report names the pattern
#   LUDC-03        a scoped path that is missing: exit 2
#   LUDC-04        a required 10.4 statement removed: fails, naming it
#   LUDC-05        the same patterns planted in the developer guide do NOT fail, and no
#                  developer-guide path is in scope
#
# Row IDs are literals (see test/row_id.h's reason): every row is spelt out.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
LINT="$HERE/lint-user-docs-card.sh"
EXPECTED_ROWS=16
TARGET=src/doc/user-guide/10-web-page/01-a-page-with-a-game.md
DEV=src/doc/developer-guide/05-building/06-the-browser-build.md

total=0; passed=0; failed=0
row() {  # row ID OK(0|1) TEXT
    total=$((total + 1))
    if [[ "$2" == 1 ]]; then echo "  PASS $1 $3"; passed=$((passed + 1))
    else echo "  FAIL $1 $3"; failed=$((failed + 1)); fi
}

W=$(mktemp -d "${TMPDIR:-/tmp}/ludc.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT
PRISTINE=$W/pristine
mkdir -p "$PRISTINE"
( cd "$ROOT" && bash "$LINT" --list ) >"$W/list" || { echo "FAIL: --list failed"; exit 1; }
while IFS= read -r f; do
    mkdir -p "$PRISTINE/$(dirname "$f")"; cp "$ROOT/$f" "$PRISTINE/$f"
done <"$W/list"
mkdir -p "$PRISTINE/$(dirname "$DEV")"; cp "$ROOT/$DEV" "$PRISTINE/$DEV"

fresh() { rm -rf "$W/t"; cp -r "$PRISTINE" "$W/t"; }
run() { OUT=$(bash "$LINT" "$W/t" 2>&1); RC=$?; }

# LUDC-01
( cd "$ROOT" && bash "$LINT" >/dev/null 2>&1 ); rc=$?
row LUDC-01 $([[ $rc -eq 0 ]] && echo 1 || echo 0) "the real tree passes (exit $rc)"

plant() {  # plant ID NAME TEXT
    fresh; printf '\n%s\n' "$3" >>"$W/t/$TARGET"; run
    row "$1" $([[ $RC -eq 1 && "$OUT" == *"[$2]"* ]] && echo 1 || echo 0) "planted '$3' fails and names [$2] (exit $RC)"
}
plant LUDC-02a make-minimal     'Run tools/make-minimal-card in.zip out.img'
plant LUDC-02b fix-sdcard-image 'Run tools/fix-sdcard-image.sh on it'
plant LUDC-02c http-server      'then python3 -m http.server 8000'
plant LUDC-02d data-image       '<div data-sha256="00"></div>'
plant LUDC-02e query-image      'open page.html?image=x&file=y'
plant LUDC-02f image-object     'jnext.mount(el, { image: { url: x } })'
plant LUDC-02g image-url        'use options.image.url here'
plant LUDC-02h card-img         'a file called card.img'
plant LUDC-02i example-card-url 'fetch https://example.com/files/x.zip'
plant LUDC-02j jnextdev         'set window.jnextDev first'
plant LUDC-02k dollar-card      'pass $card to it'
plant LUDC-02l distro-url-env   'export JNEXT_SDCARD_DISTRO_URL=x'

# LUDC-03
fresh; rm "$W/t/FEATURES.md"; run
row LUDC-03 $([[ $RC -eq 2 ]] && echo 1 || echo 0) "a scoped path that is missing: exit $RC (want 2)"

# LUDC-04
fresh; G=$(ls "$W"/t/src/doc/user-guide/10-web-page/04-*.md)
tr '\n' ' ' <"$G" | sed "s/JNEXT does not ship, host or serve/JNEXT does nothing/" >"$G.new" && mv "$G.new" "$G"; run
row LUDC-04 $([[ $RC -eq 1 && "$OUT" == *"missing statement: JNEXT does not ship, host or serve"* ]] && echo 1 || echo 0) "a removed 10.4 statement fails and is named (exit $RC)"

# LUDC-05
fresh
{ echo 'tools/make-minimal-card x y'; echo 'python3 -m http.server'; echo 'window.jnextDev = {}'; echo 'card.img $card data-image'; } >>"$W/t/$DEV"
run
inscope=$(bash "$LINT" --list "$W/t" 2>/dev/null | grep -c 'developer-guide' || true)
row LUDC-05 $([[ $RC -eq 0 && "$inscope" == 0 ]] && echo 1 || echo 0) "recipes in the developer guide do not fail; developer-guide files in scope: $inscope (exit $RC)"

printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
if [[ $total -ne $EXPECTED_ROWS ]]; then echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1; fi
[[ $failed -eq 0 ]]
