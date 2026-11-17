#!/usr/bin/env bash
# Self-test of test/docs-tree-same.sh, the site comparison of docs-userguide-check and
# docs-devguide-check. Prerequisite of `make unit-test`. Fixture trees only.
#
#   DTS-01  identical trees are the same
#   DTS-02  sitemap.xml.gz with the same content but different gzip bytes (another
#           compression level and header mtime, as another zlib writes) is the same
#   DTS-03  sitemap.xml.gz whose decompressed content differs is NOT the same, named
#   DTS-04  sitemap.xml.gz missing from the committed tree is NOT the same
#   DTS-05  sitemap.xml.gz that does not decompress is NOT the same
#   DTS-06  any other file differing (the uncompressed sitemap.xml included) is NOT the same
#   DTS-07  a file present in one tree only is NOT the same
#   DTS-08  a wrong argument count is a usage error (exit 2)
#   DTS-09  the REAL `make docs-userguide-check`, pointed (GUIDE_OUT=) at a copy of the
#           committed guide whose sitemap.xml.gz is re-compressed (other bytes, same
#           content), passes; the same copy with a changed sitemap content fails as stale
#   DTS-10  the same for the real `make docs-devguide-check` (DEVGUIDE_OUT=)
#   DTS-09/10 prove the Makefile recipes use this comparison: with a plain `diff -r -q`
#   in either recipe its row fails. They need mkdocs and graphviz, as docs-check does.
#
# Row IDs are literals (see test/row_id.h's reason): every row is spelt out.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SAME="$HERE/docs-tree-same.sh"
EXPECTED_ROWS=10

total=0; passed=0; failed=0
row() {  # row ID OK(0|1) TEXT
    total=$((total + 1))
    if [[ "$2" == 1 ]]; then echo "  PASS $1 $3"; passed=$((passed + 1))
    else echo "  FAIL $1 $3"; failed=$((failed + 1)); fi
}

W=$(mktemp -d "${TMPDIR:-/tmp}/dts.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT

SITEMAP='<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
</urlset>'
# fresh: two identical trees, $W/a (rendered) and $W/b (committed)
fresh() {
    rm -rf "$W/a" "$W/b"
    mkdir -p "$W/a/sub"
    printf '<html>page</html>\n' >"$W/a/index.html"
    printf '<html>sub</html>\n' >"$W/a/sub/page.html"
    printf '%s\n' "$SITEMAP" >"$W/a/sitemap.xml"
    printf '%s\n' "$SITEMAP" | gzip -n -9 >"$W/a/sitemap.xml.gz"
    cp -a "$W/a" "$W/b"
}
run() { OUT=$(bash "$SAME" "$W/a" "$W/b" 2>&1); RC=$?; }

fresh; run
row DTS-01 $([[ $RC -eq 0 ]] && echo 1 || echo 0) "identical trees are the same (exit $RC)"

fresh; printf '%s\n' "$SITEMAP" | gzip -1 >"$W/b/sitemap.xml.gz"
cmp -s "$W/a/sitemap.xml.gz" "$W/b/sitemap.xml.gz"; bytes_differ=$?
run
row DTS-02 $([[ $bytes_differ -ne 0 && $RC -eq 0 ]] && echo 1 || echo 0) "same sitemap content, different gzip bytes (bytes differ: $([[ $bytes_differ -ne 0 ]] && echo yes || echo no)) is the same (exit $RC)"

fresh; printf '%s\n<!-- x -->\n' "$SITEMAP" | gzip -n -9 >"$W/b/sitemap.xml.gz"; run
row DTS-03 $([[ $RC -eq 1 && "$OUT" == *"sitemap.xml.gz differs"* ]] && echo 1 || echo 0) "different decompressed sitemap content is not the same, and is named (exit $RC)"

fresh; rm "$W/b/sitemap.xml.gz"; run
row DTS-04 $([[ $RC -eq 1 && "$OUT" == *"one tree only"* ]] && echo 1 || echo 0) "sitemap.xml.gz missing from the committed tree is not the same (exit $RC)"

fresh; printf 'not gzip\n' >"$W/b/sitemap.xml.gz"; run
row DTS-05 $([[ $RC -eq 1 && "$OUT" == *"does not decompress"* ]] && echo 1 || echo 0) "a sitemap.xml.gz that does not decompress is not the same (exit $RC)"

fresh; printf '<html>edited</html>\n' >"$W/b/sub/page.html"; run; rc_page=$RC
fresh; printf '%s\n<!-- x -->\n' "$SITEMAP" >"$W/b/sitemap.xml"; run; rc_xml=$RC
row DTS-06 $([[ $rc_page -eq 1 && $rc_xml -eq 1 ]] && echo 1 || echo 0) "a differing page (exit $rc_page) or uncompressed sitemap.xml (exit $rc_xml) is not the same"

fresh; printf 'x\n' >"$W/a/sub/extra.html"; run
row DTS-07 $([[ $RC -eq 1 ]] && echo 1 || echo 0) "a file present in one tree only is not the same (exit $RC)"

bash "$SAME" "$W/a" >/dev/null 2>&1; rc_usage=$?
row DTS-08 $([[ $rc_usage -eq 2 ]] && echo 1 || echo 0) "a missing argument is a usage error (exit $rc_usage)"

# real_check <target> <OUTVAR> <committed-dir>: run the real check against a copy of the
# committed render, first with its sitemap.xml.gz re-compressed (must pass), then with the
# sitemap content changed (must fail, naming the copy as stale). Sets RC_SAME, RC_DIFF, OUT_DIFF.
real_check() {
    local target=$1 var=$2 src=$3 copy=$W/$1
    rm -rf "$copy"; cp -a "$ROOT/$src" "$copy"
    gzip -dc "$ROOT/$src/sitemap.xml.gz" | gzip -1 >"$copy/sitemap.xml.gz"
    if cmp -s "$ROOT/$src/sitemap.xml.gz" "$copy/sitemap.xml.gz"; then RC_SAME=99; else
        (cd "$ROOT" && timeout --kill-after=5s 300s make --no-print-directory "$target" "$var=$copy") >/dev/null 2>&1; RC_SAME=$?
    fi
    { gzip -dc "$ROOT/$src/sitemap.xml.gz"; printf '<!-- changed -->\n'; } | gzip -n -9 >"$copy/sitemap.xml.gz"
    OUT_DIFF=$(cd "$ROOT" && timeout --kill-after=5s 300s make --no-print-directory "$target" "$var=$copy" 2>&1); RC_DIFF=$?
}
real_check docs-userguide-check GUIDE_OUT doc/user-guide
row DTS-09 $([[ $RC_SAME -eq 0 && $RC_DIFF -ne 0 && "$OUT_DIFF" == *"is stale"* ]] && echo 1 || echo 0) "make docs-userguide-check: a re-compressed sitemap.xml.gz passes (exit $RC_SAME), a changed one is stale (exit $RC_DIFF)"
real_check docs-devguide-check DEVGUIDE_OUT doc/developer-guide
row DTS-10 $([[ $RC_SAME -eq 0 && $RC_DIFF -ne 0 && "$OUT_DIFF" == *"is stale"* ]] && echo 1 || echo 0) "make docs-devguide-check: a re-compressed sitemap.xml.gz passes (exit $RC_SAME), a changed one is stale (exit $RC_DIFF)"

printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
if [[ $total -ne $EXPECTED_ROWS ]]; then echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1; fi
[[ $failed -eq 0 ]]
