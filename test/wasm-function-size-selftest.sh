#!/usr/bin/env bash
# Self-test of test/wasm-function-size.pl (the function-size guard of the WebAssembly unit suites).
# Prerequisite of `make unit-test`; needs no emsdk. The modules it checks are synthetic ones
# written here, so the real build tree is never read.
#
# Oracle: the WebAssembly binary format (the code section is a vector of size-prefixed function
# bodies; the function index space puts imports first; name subsection 1 maps an index to a
# name) and the bound, 262144 bytes of one function body. The bound is pinned HERE, independently
# of the checker: raising it is a deliberate edit in both places, like a pinned row count.
#
#   WFS-01   a body of exactly the bound passes
#   WFS-02   a body one byte over fails, and the report names the file, the size and the bound
#   WFS-03   the offender is named by function index = function imports + code index (f3, not f1), with
#            other import kinds interleaved and decoy global/data names at the same index
#   WFS-04   the largest body is found wherever it sits in the section (middle, last)
#   WFS-05   every offending module is reported, none dropped, a good one not blamed
#   WFS-06   the largest BODY is measured, not the section (3 x 100000 + 1 MiB of data pass)
#   WFS-07   a directory with no .wasm: exit 2 (cannot check)
#   WFS-08a  a file with a bad magic: exit 2
#   WFS-08b  a module truncated inside its code section: exit 2
#   WFS-08c  a trailing byte in the import section, or in the code section, is exit 2 (not consumed exactly)
#   WFS-09   a module without a name section: exit 2, and the report says so
#   WFS-10   the Makefile wires it: unit-test-wasm needs the check, the check runs the script on
#            the wasm test tree, unit-test needs the self-test
#
# Row IDs are literals (see test/row_id.h's reason): every row is spelt out.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
CHECK="$HERE/wasm-function-size.pl"
BOUND=262144
EXPECTED_ROWS=12

total=0; passed=0; failed=0
row() {  # row ID OK(0|1) TEXT
    total=$((total + 1))
    if [[ "$2" == 1 ]]; then echo "  PASS $1 $3"; passed=$((passed + 1))
    else echo "  FAIL $1 $3"; failed=$((failed + 1)); fi
}

W=$(mktemp -d "${TMPDIR:-/tmp}/wfs.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT

# gen.pl <out> <bodies comma list> [imports=K] [mixed=1] [trail_imp=1] [trail_code=1] [names=0|1] [data=BYTES] [cut=1]
# Type section (one () -> ()), K imported functions (mixed=1: with a memory, table, global and tag import among them), function section, code section whose body i is
# 0x00 (no locals) + (size-2) x 0x01 (nop) + 0x0B (end), so its LEB size prefix is exactly `size`;
# an optional data section; an optional name section (function index i is named "f<i>").
# cut=1 chops the file 200 bytes before the end of the code section.
cat >"$W/gen.pl" <<'PERL'
use strict; use warnings;
my ($out, $bodies, @opt) = @ARGV;
my %o = (imports => 0, names => 1, data => 0, cut => 0, mixed => 0, trail_imp => 0, trail_code => 0);
for (@opt) { my ($k, $v) = split /=/; $o{$k} = $v }
sub leb { my $n = shift; my $r = ''; while (1) { my $b = $n & 0x7F; $n >>= 7; if ($n) { $r .= chr($b | 0x80) } else { $r .= chr($b); last } } $r }
sub sec { my ($id, $p) = @_; chr($id) . leb(length $p) . $p }
sub nm { my $s = shift; leb(length $s) . $s }
my @b = split /,/, $bodies;
my $m = "\0asm\1\0\0\0";
$m .= sec(1, "\1\x60\0\0");
if ($o{imports}) {
    my @imp = map { nm('e') . nm("i$_") . "\0\0" } 0 .. $o{imports} - 1;    # function imports: kind 0, type 0
    if ($o{mixed}) {    # non-function imports (they take no function index): memory with a max, table, global, tag
        my @other = (nm('e') . nm('mem') . "\2\1\1\5", nm('e') . nm('tab') . "\1\x70\1\1\2",
                     nm('e') . nm('glob') . "\3\x7F\0", nm('e') . nm('tag') . "\4\0\0");
        @imp = ($other[0], $imp[0], $other[1], $other[2], $other[3], @imp[1 .. $#imp]);
    }
    $m .= sec(2, leb(scalar @imp) . join('', @imp) . ($o{trail_imp} ? "\0" : ''));
}
$m .= sec(3, leb(scalar @b) . ("\0" x @b));
my $code = leb(scalar @b);
$code .= leb($_) . "\0" . ("\1" x ($_ - 2)) . "\x0B" for @b;
$m .= sec(10, $code . ($o{trail_code} ? "\0" : ''));
my $cut_at = length($m) - 200;
$m .= sec(11, "\1\1" . leb($o{data}) . ("\0" x $o{data})) if $o{data};
if ($o{names}) {
    my $n = $o{imports} + @b;
    my $sub = leb($n); $sub .= leb($_) . nm("f$_") for 0 .. $n - 1;
    # subsections 0 (module), 1 (functions), 7 (globals), 9 (data segments), as every real module has;
    # 7 and 9 map index 3 to decoys, which must never be taken for a function name
    my $sub0 = nm('m');
    my $sub7 = leb(1) . leb(3) . nm('g3');
    my $sub9 = leb(1) . leb(3) . nm('d3');
    $m .= sec(0, nm('name') . "\0" . leb(length $sub0) . $sub0 . "\1" . leb(length $sub) . $sub
                 . "\7" . leb(length $sub7) . $sub7 . "\x09" . leb(length $sub9) . $sub9);
}
$m = substr($m, 0, $cut_at) if $o{cut};
open(my $fh, '>:raw', $out) or die; print $fh $m; close $fh;
PERL
gen() { perl "$W/gen.pl" "$@"; }
fresh() { rm -rf "$W/d"; mkdir -p "$W/d"; }
run() { OUT=$(perl "$CHECK" "$W/d" 2>&1); RC=$?; }

# WFS-01
fresh; gen "$W/d/a.wasm" $BOUND; run
row WFS-01 $([[ $RC -eq 0 ]] && echo 1 || echo 0) "a body of exactly $BOUND bytes passes (exit $RC)"

# WFS-02
fresh; gen "$W/d/a.wasm" $((BOUND + 1)); run
row WFS-02 $([[ $RC -eq 1 && "$OUT" == *"a.wasm"* && "$OUT" == *"$((BOUND + 1))"* && "$OUT" == *"$BOUND-byte bound"* ]] && echo 1 || echo 0) "a body of $((BOUND + 1)) bytes fails naming the file, the size and the bound (exit $RC)"

# WFS-03: 2 imports, bodies 100, BOUND+1, 100 -> the big one is function index 2 + 1 = 3
fresh; gen "$W/d/a.wasm" 100,$((BOUND + 1)),100 imports=2 mixed=1; run
bad=0; [[ "$OUT" == *"function f3 has"* ]] || bad=1
[[ "$OUT" =~ function\ f[0124]\  ]] && bad=1
[[ "$OUT" == *"function g3"* || "$OUT" == *"function d3"* ]] && bad=1
row WFS-03 $([[ $RC -eq 1 && $bad -eq 0 ]] && echo 1 || echo 0) "with 2 function imports among memory/table/global/tag imports the offender is f3; no other function and no global/data decoy (g3, d3) is named (exit $RC)"

# WFS-04
fresh; gen "$W/d/mid.wasm" 1000,$((BOUND + 1)),2000; gen "$W/d/last.wasm" 1000,2000,$((BOUND + 1)); run
row WFS-04 $([[ $RC -eq 1 && "$OUT" == *"mid.wasm"* && "$OUT" == *"last.wasm"* ]] && echo 1 || echo 0) "an oversized body in the middle and one last are both found (exit $RC)"

# WFS-05
fresh; gen "$W/d/big1.wasm" $((BOUND + 1)); gen "$W/d/big2.wasm" $((BOUND + 500)); gen "$W/d/good.wasm" 1000; run
nfail=$(grep -c '^FAIL ' <<<"$OUT" || true)
goodfail=$(grep '^FAIL ' <<<"$OUT" | grep -c 'good.wasm' || true)
row WFS-05 $([[ $RC -eq 1 && "$nfail" -eq 2 && "$goodfail" -eq 0 ]] && echo 1 || echo 0) "two oversized modules give exactly two FAIL lines, the good one is not blamed (FAIL lines $nfail, blamed good $goodfail, exit $RC)"

# WFS-06
fresh; gen "$W/d/a.wasm" 100000,100000,100000 data=1048576; run
row WFS-06 $([[ $RC -eq 0 ]] && echo 1 || echo 0) "three 100000-byte bodies (code section over the bound) and a 1 MiB data section pass (exit $RC)"

# WFS-07
fresh; echo x >"$W/d/readme.txt"; run
row WFS-07 $([[ $RC -eq 2 ]] && echo 1 || echo 0) "a directory with no .wasm is exit 2 (exit $RC)"

# WFS-08a
fresh; printf 'this is not a wasm module at all' >"$W/d/a.wasm"; run
row WFS-08a $([[ $RC -eq 2 ]] && echo 1 || echo 0) "a file with a bad magic is exit 2 (exit $RC)"

# WFS-08b
fresh; gen "$W/d/a.wasm" 1000,1000 cut=1; run
row WFS-08b $([[ $RC -eq 2 && "$OUT" == *"truncated"* ]] && echo 1 || echo 0) "a module cut inside its code section is exit 2, saying truncated (exit $RC)"

# WFS-08c
fresh; gen "$W/d/a.wasm" 1000 imports=1 trail_imp=1; run; rc_i=$RC; out_i=$OUT
fresh; gen "$W/d/a.wasm" 1000 trail_code=1; run; rc_c=$RC; out_c=$OUT
row WFS-08c $([[ $rc_i -eq 2 && "$out_i" == *"malformed import section"* && $rc_c -eq 2 && "$out_c" == *"malformed code section"* ]] && echo 1 || echo 0) "one trailing byte in the import section (exit $rc_i) or in the code section (exit $rc_c) is exit 2, naming the section"

# WFS-09
fresh; gen "$W/d/a.wasm" 1000 names=0; run
row WFS-09 $([[ $RC -eq 2 && "$OUT" == *"no name section"* ]] && echo 1 || echo 0) "an in-bound module with no name section is exit 2, saying so (exit $RC)"

# WFS-10: the real Makefile, as make itself reads it (-p lists the rules and variables, runs nothing)
DB=$(cd "$ROOT" && LANG=C make -pRrq 2>/dev/null)
wasm_rule=$(grep -E '^unit-test-wasm:' <<<"$DB" | head -n1)
unit_rule=$(grep -E '^unit-test:' <<<"$DB" | head -n1)
chk_recipe=$(awk '/^wasm-function-size-check:/{f=1;next} f&&/^\t/{print;next} f&&/^[^\t#]/{exit}' <<<"$DB")
ok=1
[[ " $wasm_rule " == *" wasm-function-size-check "* ]] || ok=0
[[ " $unit_rule " == *" wasm-function-size-selftest "* ]] || ok=0
# make -p prints a recipe unexpanded: the script's argument is the wasm tree variable's /test
[[ "$chk_recipe" == *'wasm-function-size.pl $(BUILD_DIR_WASM_UNIT_TEST)/test'* ]] || ok=0
grep -qE '^BUILD_DIR_WASM_UNIT_TEST := build/wasm-unit-test$' <<<"$DB" || ok=0
row WFS-10 $ok "unit-test-wasm needs wasm-function-size-check, which runs the script on build/wasm-unit-test/test, and unit-test needs the self-test"

printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
if [[ $total -ne $EXPECTED_ROWS ]]; then echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1; fi
[[ $failed -eq 0 ]]
