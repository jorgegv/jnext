#!/usr/bin/env bash
# Self-test of test/test-affected.pl (`make test-affected`, GH #547) on a hand-written fixture
# build tree and source root: no compiler, no real build. Prerequisite of `make unit-test`.
# The oracle is the map's definition (a suite is selected when a file it really linked reads the
# changed file), spelt out in the fixture, not read back from the tool.
#
#   TAF-01  a header change selects exactly the suites whose own or pulled objects' depfiles list it
#   TAF-02  a .cpp in an archive selects the suite whose map pulled that member, NOT a suite that
#           lists the archive and did not pull it
#   TAF-03  a changed file in no depfile and matching no glob makes the plan FULL and names it
#   TAF-04  a docs-only change plans docs-check and no suite
#   TAF-05  a regression row script plans that row
#   TAF-06  _WIN32 in a changed file reports unit-test-win needed, with the file
#   TAF-07  __EMSCRIPTEN__ reports unit-test-wasm needed
#   TAF-08  ENABLE_QT_UI reports unit-test-sdl needed
#   TAF-09  the smoke set is in every plan; a smoke name the manifest does not declare is a
#           refusal (exit 2)
#   TAF-10  --check fails on a tracked file no depfile reads and no glob matches, and passes once
#           the file is classified
#   TAF-12  a suite with no linker map is selected on its archives' whole membership
#   TAF-13  a fixture file is selected by the suite whose sources name its directory, and is FULL
#           when nothing names it
# (The MISS line of run-unit-tests.sh is TAF-11's subject; its rows are HS-194d/e in
# test/harness-selftest.sh, which owns the stub-suite machinery.)
#
# Row IDs are literals (see test/row_id.h's reason): every row is spelt out.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TOOL="${TEST_AFFECTED_TOOL:-$HERE/test-affected.pl}"
CONF="$HERE/test-affected.conf"

total=0; passed=0; failed=0
row() {  # row ID OK(0|1) TEXT
    total=$((total + 1))
    if [[ "$2" == 1 ]]; then echo "  PASS $1 $3"; passed=$((passed + 1))
    else echo "  FAIL $1 $3"; failed=$((failed + 1)); fi
}

W=$(mktemp -d "${TMPDIR:-/tmp}/taf.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT
R="$W/src"; B="$W/build"
mkdir -p "$R"/{src/a,src/core,test/fixtures/data,test/00regression/scripts,doc,weird} \
         "$B"/{src/a/CMakeFiles/a.dir,test/CMakeFiles}

# sources
echo '// header' >"$R/src/a/x.h"
echo '#include "x.h"' >"$R/src/a/x.cpp"
echo '// y' >"$R/src/a/y.cpp"
echo '// s1' >"$R/test/s1_test.cpp"
echo '// s2' >"$R/test/s2_test.cpp"
echo '// s3' >"$R/test/s3_test.cpp"
echo '// smoke' >"$R/test/smoke_test.cpp"
echo '// uses test/fixtures/data/ at run time' >>"$R/test/s2_test.cpp"
echo 'x' >"$R/test/fixtures/data/blob.bin"
echo 'x' >"$R/test/fixtures/orphan.bin"
echo '# doc' >"$R/doc/a.md"
echo 'echo row' >"$R/test/00regression/scripts/foo-func.sh"
printf 'foo-func\n' >"$R/test/00regression/functional_tests.conf"
echo 'int w() {
#ifdef _WIN32
return 1;
#endif
return 0; }' >"$R/src/core/net.cpp"
echo '#ifdef __EMSCRIPTEN__
#endif' >"$R/src/core/em.cpp"
echo '#if ENABLE_QT_UI
#endif' >"$R/src/core/qt.cpp"
echo 'dat' >"$R/weird/thing.dat"

# manifest
cat >"$W/unit-tests.conf" <<'EOF'
# expect: 4
s1_test 1
s2_test 1 test/fixtures/data/x
s3_test 1
smoke_test 1
EOF

# build tree
echo "CMAKE_HOME_DIRECTORY:INTERNAL=$R" >"$B/CMakeCache.txt"
echo '/usr/bin/ar qc liba.a CMakeFiles/a.dir/x.cpp.o CMakeFiles/a.dir/y.cpp.o' >"$B/src/a/CMakeFiles/a.dir/link.txt"
dep() {  # dep <obj dir> <obj name> <file...>: <obj>.o.d naming the files
    local d=$1 o=$2; shift 2
    { echo "$d/$o: \\"; for f in "$@"; do echo " $R/$f \\"; done; echo " /usr/include/stdc-predef.h"; } >"$d/$o.d"
}
mkdir -p "$B/src/a/CMakeFiles/a.dir"
dep "$B/src/a/CMakeFiles/a.dir" x.cpp.o src/a/x.cpp src/a/x.h
dep "$B/src/a/CMakeFiles/a.dir" y.cpp.o src/a/y.cpp
suite() {  # suite <name> <src> <header?> <map pulled member|none>
    local n=$1 src=$2 hdr=$3 pulled=$4 d="$B/test/CMakeFiles/$1.dir"
    mkdir -p "$d"
    if [[ -n "$hdr" ]]; then dep "$d" "$n.cpp.o" "$src" "$hdr"; else dep "$d" "$n.cpp.o" "$src"; fi
    printf '%s: \\\n CMakeFiles/%s.dir/%s.cpp.o \\\n ../src/a/liba.a\n' "$n" "$n" "$n" >"$d/link.d"
    if [[ "$pulled" != none ]]; then
        { echo "Archive member included to satisfy reference by file (symbol)"; echo
          echo "../src/a/liba.a($pulled)"; echo "                              CMakeFiles/$n.dir/$n.cpp.o (sym)"; echo
          echo "Discarded input sections"; } >"$B/test/$n.map"
    fi
}
suite s1_test test/s1_test.cpp "" x.cpp.o
suite s2_test test/s2_test.cpp "" y.cpp.o
suite s3_test test/s3_test.cpp "" x.cpp.o
suite smoke_test test/smoke_test.cpp "" x.cpp.o

run() {  # run <files...>: the plan file in $W/plan, the report in $OUT
    printf '%s\n' "$@" >"$W/files"
    OUT=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "${CONFX:-$CONF}" \
          --files "$W/files" --smoke "${SMOKE:-smoke_test}" --plan "$W/plan" 2>&1); RC=$?
}
plan_suites() { sed -n 's/^suites=//p' "$W/plan"; }
plan_get()    { sed -n "s/^$1=//p" "$W/plan"; }

run src/a/x.h
row TAF-01 $([[ $RC -eq 0 && "$(plan_suites)" == "s1_test,s3_test,smoke_test" ]] && echo 1 || echo 0) \
    "header x.h selects the suites whose pulled objects read it, plus the smoke set (got: $(plan_suites))"

run src/a/y.cpp
row TAF-02 $([[ $RC -eq 0 && "$(plan_suites)" == "s2_test,smoke_test" ]] && echo 1 || echo 0) \
    "y.cpp selects s2 (pulled y), not s1/s3 (list the archive, pulled x only) (got: $(plan_suites))"

run weird/thing.dat src/a/x.h
row TAF-03 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"weird/thing.dat"* && "$OUT" == *UNMAPPED* ]] && echo 1 || echo 0) \
    "an unmapped file makes the plan FULL and names it (mode $(plan_get mode))"

run doc/a.md
row TAF-04 $([[ $RC -eq 0 && "$(plan_get mode)" == docs && "$(plan_get docs)" == 1 && "$OUT" == *"docs-check: yes"* ]] && echo 1 || echo 0) \
    "a docs-only change plans docs-check (mode $(plan_get mode))"

run test/00regression/scripts/foo-func.sh
row TAF-05 $([[ $RC -eq 0 && "$(plan_get rows)" == foo-func ]] && echo 1 || echo 0) \
    "a row script plans that row (rows: $(plan_get rows))"

run src/core/net.cpp
row TAF-06 $([[ $RC -eq 0 && "$OUT" == *"unit-test-win NEEDED"*"src/core/net.cpp"* && "$OUT" == *"unit-test-wasm not needed"* ]] && echo 1 || echo 0) \
    "_WIN32 in a changed file: unit-test-win needed, with the file"

run src/core/em.cpp
row TAF-07 $([[ $RC -eq 0 && "$OUT" == *"unit-test-wasm NEEDED"*"src/core/em.cpp"* && "$OUT" == *"unit-test-win not needed"* ]] && echo 1 || echo 0) \
    "__EMSCRIPTEN__ in a changed file: unit-test-wasm needed, with the file"

run src/core/qt.cpp
row TAF-08 $([[ $RC -eq 0 && "$OUT" == *"unit-test-sdl NEEDED"*"src/core/qt.cpp"* && "$OUT" == *"unit-test-win not needed"* ]] && echo 1 || echo 0) \
    "ENABLE_QT_UI in a changed file: unit-test-sdl needed, with the file"

run doc/a.md
in_plan=$([[ "$(plan_suites)" == *smoke_test* || "$(plan_get mode)" == docs ]] && echo 1 || echo 0)
run src/a/y.cpp
in_plan2=$([[ "$(plan_suites)" == *smoke_test* ]] && echo 1 || echo 0)
SMOKE=smoke_test,not_a_suite run src/a/y.cpp
row TAF-09 $([[ $in_plan2 == 1 && $in_plan == 1 && $RC -eq 2 && "$OUT" == *"not_a_suite"* ]] && echo 1 || echo 0) \
    "the smoke suite is in a code plan; an undeclared smoke name is refused (exit $RC)"

printf 'src/a/x.h\nweird/thing.dat\n' >"$W/tracked"
chk() { OUT=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "$1" --tracked "$W/tracked" --check 2>&1); RC=$?; }
chk "$CONF"; rc_bad=$RC; out_bad=$OUT
{ cat "$CONF"; echo 'weird/** ignore'; } >"$W/conf2"
chk "$W/conf2"; rc_ok=$RC
row TAF-10 $([[ $rc_bad -eq 1 && "$out_bad" == *"weird/thing.dat"* && $rc_ok -eq 0 ]] && echo 1 || echo 0) \
    "--check: an unclassified tracked file fails and is named (rc $rc_bad); classified, it passes (rc $rc_ok)"

# TAF-12: s4 has no map: selected on liba's whole membership (a change to y.cpp reaches it)
suite s4_test test/s4_test.cpp "" none
echo '// s4' >"$R/test/s4_test.cpp"
sed -i 's/^# expect: 4/# expect: 5/' "$W/unit-tests.conf"; echo 's4_test 1' >>"$W/unit-tests.conf"
run src/a/y.cpp
row TAF-12 $([[ $RC -eq 0 && "$(plan_suites)" == *s4_test* && "$(plan_suites)" == *s2_test* && "$(plan_suites)" != *s1_test* && "$OUT" == *"no linker map for: s4_test"* ]] && echo 1 || echo 0) \
    "a suite without a map is selected on the archive's whole membership and says so (got: $(plan_suites))"

run test/fixtures/data/blob.bin
r1=$([[ "$(plan_get mode)" == subset && "$(plan_suites)" == *s2_test* ]] && echo 1 || echo 0)
run test/fixtures/orphan.bin
row TAF-13 $([[ $r1 == 1 && "$(plan_get mode)" == full && "$OUT" == *orphan.bin* ]] && echo 1 || echo 0) \
    "a fixture is selected by the suite naming its directory (ok=$r1); one nothing names is FULL (mode $(plan_get mode))"

echo ""
echo "Total: $total  Passed: $passed  Failed: $failed  Skipped: 0"
[[ "$total" -eq 12 ]] || { echo "  FAIL test-affected-selftest: ran $total rows, expected 12"; exit 1; }
[[ "$failed" -eq 0 ]]
