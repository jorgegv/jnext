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
#
# Row IDs are literals (see test/row_id.h's reason): every row is spelt out.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SAME="$HERE/docs-tree-same.sh"
EXPECTED_ROWS=8

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

printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
if [[ $total -ne $EXPECTED_ROWS ]]; then echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1; fi
[[ $failed -eq 0 ]]
