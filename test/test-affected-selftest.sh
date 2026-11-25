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
#   TAF-14  a depfile written with paths relative to the target's build directory (as ccache
#           with a base_dir writes them) places a file like an absolute one
#   TAF-15..30 review round 1: a build file is FULL; docs plus a row or a make target; the browser
#           action; an undeclared row script is FULL; std::filesystem and the harness flag the
#           platform runs; a file a suite reads through a compile-definition path, or under a
#           definition directory, selects it (also a docs file, which leaves docs mode, and a file
#           nothing else places, which is not UNMAPPED); the whole-root definition plus a literal
#           mention; browser-only sources; in a git fixture: a gitlink is FULL, a conf line plans
#           its row, a removed _WIN32 arm still flags windows; --explain-miss (the stateless MISS, also for a docs-only plan)
#   TAF-32  test/ci-gate.sh and test/regression-package.sh plan package-contract-test, not FULL
#   TAF-31  Qt's QT_TESTCASE_* test-data define counts only for a suite that calls QFINDTESTDATA
#   TAF-33..74 GH #564, in a git fixture: a changed pin, a new or removed suite line, a changed ? marker
#           or directive, a pin plus another suite's source, and a line of no known shape (FULL);
#           test/CMakeLists.txt statements (a new block, a changed link, an if() context, a statement
#           naming no suite = FULL); the matrix and its generator plan the traceability check
#   TAF-78..79 GH #564 review: UNIT_TEST_LINTS names lint-test-scripts; the target fails when a lint does
#   TAF-77  each of ci-gate.sh and regression-package.sh alone plans both makes
#   TAF-75..76 GH #564: a changed test shell script plans make lint-test-scripts (the preflight lints
#           a branch never runs); that target runs the four test-script lints
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
sedi()        { sed "$1" "$2" >"$2.new" && mv "$2.new" "$2"; }   # sedi <script> <file>: edit in place without sed -i (BSD sed)

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
{ echo '# expect: 5'; grep -v '^# expect' "$W/unit-tests.conf"; echo 's4_test 1'; } >"$W/unit-tests.conf.new"; mv "$W/unit-tests.conf.new" "$W/unit-tests.conf"
run src/a/y.cpp
row TAF-12 $([[ $RC -eq 0 && "$(plan_suites)" == *s4_test* && "$(plan_suites)" == *s2_test* && "$(plan_suites)" != *s1_test* && "$OUT" == *"no linker map for: s4_test"* ]] && echo 1 || echo 0) \
    "a suite without a map is selected on the archive's whole membership and says so (got: $(plan_suites))"

run test/fixtures/data/blob.bin
r1=$([[ "$(plan_get mode)" == subset && "$(plan_suites)" == *s2_test* ]] && echo 1 || echo 0)
run test/fixtures/orphan.bin
row TAF-13 $([[ $r1 == 1 && "$(plan_get mode)" == full && "$OUT" == *orphan.bin* ]] && echo 1 || echo 0) \
    "a fixture is selected by the suite naming its directory (ok=$r1); one nothing names is FULL (mode $(plan_get mode))"

# TAF-14: the depfile of s3's object, rewritten relative (the fixture build dir is $B/test)
echo '// z' >"$R/src/a/z.h"
{ printf 'x: \\\n'; printf ' ../../src/test/s3_test.cpp \\\n ../../src/src/a/z.h\n'; } >"$B/test/CMakeFiles/s3_test.dir/s3_test.cpp.o.d"
run src/a/z.h
row TAF-14 $([[ $RC -eq 0 && "$(plan_suites)" == *s3_test* && "$(plan_suites)" != *s1_test* ]] && echo 1 || echo 0) \
    "a relative depfile path (ccache base_dir form) is resolved against the target's build dir (got: $(plan_suites))"

# ---- review round 1 (GH #547): run-time edges (compile-definition paths), classes, git mode, recipe actions
mkdir -p "$R"/{test/fixtures/dd,packaging,web,src/platform/web}
echo 'doc b' >"$R/doc/b.md"; echo 'rt' >"$R/src/core/runtime.txt"; echo 'bl' >"$R/test/fixtures/dd/blob2.bin"
echo '// h' >"$R/src/a/q.h"; echo 'x' >"$R/packaging/x.sh"; echo '<html>' >"$R/web/index.html"
echo '// web' >"$R/src/platform/web/web_controller.cpp"
echo 'int f() { return std::filesystem::exists("x"); }' >"$R/src/core/fs.cpp"
echo '# cmake' >"$R/CMakeLists.txt"; echo '#!/bin/sh' >"$R/test/run-unit-tests.sh"
echo 'echo row' >"$R/test/00regression/scripts/nosuch-func.sh"
dep "$B/src/a/CMakeFiles/a.dir" x.cpp.o src/a/x.cpp src/a/x.h src/a/q.h
defs() {  # defs <suite> <name=path>...: the CXX_DEFINES of the suite's flags.make (paths baked in, as CMake writes them)
    local n=$1 line='CXX_DEFINES = -DNDEBUG'; shift
    local kv; for kv in "$@"; do line="$line -D${kv%%=*}=\\\"${kv#*=}\\\""; done
    printf '%s\n' "$line" >"$B/test/CMakeFiles/$n.dir/flags.make"
}
suite s5_test test/s5_test.cpp "" zz.cpp.o; defs s5_test "JNEXT_DOC=$R/doc/b.md" "JNEXT_RT=$R/src/core/runtime.txt"
suite s6_test test/s6_test.cpp "" zz.cpp.o; defs s6_test "JNEXT_DIR=$R/test/fixtures/dd"
suite s7_test test/s7_test.cpp "" zz.cpp.o; defs s7_test "JNEXT_SRC_DIR=$R"
for n in s5 s6 s7; do echo "// $n" >"$R/test/${n}_test.cpp"; done
echo 'void g() { read("src/a/q.h"); }' >>"$R/test/s7_test.cpp"; echo 'void h() { read("src/a/q.h"); }' >>"$R/test/s6_test.cpp"
{ cat "$W/unit-tests.conf"; echo 's5_test 1'; echo 's6_test 1'; echo 's7_test 1'; } >"$W/unit-tests.conf.new"; mv "$W/unit-tests.conf.new" "$W/unit-tests.conf"
suite s8_test test/s8_test.cpp "" zz.cpp.o; defs s8_test "QT_TESTCASE_SOURCEDIR=$R/test"
suite s9_test test/s9_test.cpp "" zz.cpp.o; defs s9_test "QT_TESTCASE_SOURCEDIR=$R/test"
echo 'void a() { auto p = QFINDTESTDATA("x"); }' >"$R/test/s8_test.cpp"; echo '// s9' >"$R/test/s9_test.cpp"; echo d >"$R/test/other.dat"
{ cat "$W/unit-tests.conf"; echo 's8_test 1'; echo 's9_test 1'; } >"$W/unit-tests.conf.new"; mv "$W/unit-tests.conf.new" "$W/unit-tests.conf"
run test/other.dat
row TAF-31 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == *s8_test* && "$(plan_suites)" != *s9_test* ]] && echo 1 || echo 0) \
    "Qt's test-data root define selects a suite only when its sources call QFINDTESTDATA (got $(plan_suites))"

run CMakeLists.txt
row TAF-15 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *CMakeLists.txt* ]] && echo 1 || echo 0) \
    "a build-file change (action full) makes the plan FULL, named (mode $(plan_get mode))"
run doc/a.md test/00regression/scripts/foo-func.sh
row TAF-16 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_get rows)" == foo-func && "$(plan_get docs)" == 1 ]] && echo 1 || echo 0) \
    "a docs file plus a row script is a subset plan with that row and docs-check, not docs-only (mode $(plan_get mode), rows $(plan_get rows))"
run doc/a.md packaging/x.sh
row TAF-17 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_get makes)" == package-contract-test ]] && echo 1 || echo 0) \
    "a docs file plus a packaging file plans package-contract-test (makes: $(plan_get makes), mode $(plan_get mode))"
run web/index.html
row TAF-18 $([[ $RC -eq 0 && "$OUT" == *"HEAVY: web/index.html"*"wasm-browser-test"* ]] && echo 1 || echo 0) \
    "a web file reports the heavy browser run it needs"
run test/00regression/scripts/nosuch-func.sh
row TAF-20 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"names no declared row"* ]] && echo 1 || echo 0) \
    "a row script that names no declared row makes the plan FULL, saying so (mode $(plan_get mode))"
run src/core/fs.cpp
row TAF-21 $([[ $RC -eq 0 && "$OUT" == *"unit-test-win NEEDED"*"std::filesystem"* ]] && echo 1 || echo 0) \
    "std::filesystem in a changed file: unit-test-win needed, with the reason"
run test/run-unit-tests.sh
row TAF-22 $([[ $RC -eq 0 && "$OUT" == *"unit-test-sdl NEEDED"* && "$OUT" == *"unit-test-win NEEDED"* && "$OUT" == *"unit-test-wasm NEEDED"* ]] && echo 1 || echo 0) \
    "a change to the unit harness needs unit-test-sdl, -win and -wasm"
run src/core/runtime.txt
row TAF-23 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == *s5_test* ]] && echo 1 || echo 0) \
    "a file no depfile and no glob places, but a suite is given its path as a compile definition, selects that suite and is not UNMAPPED (mode $(plan_get mode), got $(plan_suites))"
run test/fixtures/dd/blob2.bin
row TAF-24 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == *s6_test* ]] && echo 1 || echo 0) \
    "a file under a directory given to a suite as a compile definition selects that suite (got $(plan_suites))"
run doc/b.md
row TAF-25 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == *s5_test* && "$(plan_get docs)" == 1 ]] && echo 1 || echo 0) \
    "a docs file a suite reads through a compile definition leaves docs mode: subset with that suite and docs-check (mode $(plan_get mode))"
run src/a/q.h
row TAF-26 $([[ $RC -eq 0 && "$(plan_suites)" == "s1_test,s3_test,s4_test,s7_test,smoke_test" ]] && echo 1 || echo 0) \
    "a compiled file is also selected for a suite given the whole source root that names its path in its sources, and not for a suite that is not; s4 has no map and takes the whole archive (got $(plan_suites))"
run src/platform/web/web_controller.cpp
row TAF-28 $([[ $RC -eq 0 && "$OUT" == *"HEAVY: src/platform/web/web_controller.cpp"* && "$OUT" == *"unit-test-wasm NEEDED"* && "$OUT" == *"unit-test-sdl not needed"* ]] && echo 1 || echo 0) \
    "a source only the browser build compiles reports the heavy browser run and unit-test-wasm, not unit-test-sdl"

# TAF-27 / TAF-19 / TAF-29: git mode, in the fixture as a repository
G() { git -C "$R" -c user.name=t -c user.email=t@t "$@" >/dev/null 2>&1; }
G init -q; G add -A; G commit -q -m base
grun() {  # grun [base]: the planner in git mode against HEAD (or the given base)
    OUT=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "$CONF" --base "${1:-HEAD}" --smoke smoke_test --plan "$W/plan" 2>&1); RC=$?
}
G update-index --add --cacheinfo "160000,$(printf 'a%.0s' {1..40}),third_party/sub"
G commit -q -m "bump the submodule"
grun HEAD~1
row TAF-27 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"submodule pointer"* && "$OUT" == *third_party/sub* ]] && echo 1 || echo 0) \
    "a submodule pointer change (a gitlink) makes the plan FULL, named (mode $(plan_get mode))"
G reset -q --hard HEAD~1
printf 'bar-func quiet\n' >>"$R/test/00regression/functional_tests.conf"
grun
row TAF-19 $([[ $RC -eq 0 && "$(plan_get rows)" == bar-func && "$(plan_get mode)" == subset ]] && echo 1 || echo 0) \
    "git mode: a line added to functional_tests.conf plans exactly the row it names (rows: $(plan_get rows), mode $(plan_get mode))"
G checkout -q -- test/00regression/functional_tests.conf
printf 'int w() { return 0; }\n' >"$R/src/core/net.cpp"
grun
row TAF-29 $([[ $RC -eq 0 && "$OUT" == *"unit-test-win NEEDED"*"src/core/net.cpp"* ]] && echo 1 || echo 0) \
    "git mode: removing the last _WIN32 arm still reports unit-test-win needed (the removed lines count)"
G checkout -q -- src/core/net.cpp

# TAF-30: --explain-miss names a failing suite the plan would not have selected, and only that one
printf 'src/a/y.cpp\n' >"$W/files"
OUT=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "$CONF" --files "$W/files" --smoke smoke_test --explain-miss s1_test,s2_test 2>&1); RC=$?
printf 'CMakeLists.txt\n' >"$W/files"
OUT_FULL=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "$CONF" --files "$W/files" --smoke smoke_test --explain-miss s1_test 2>&1)
printf 'doc/a.md\n' >"$W/files"
OUT_DOCS=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "$CONF" --files "$W/files" --smoke smoke_test --explain-miss s1_test 2>&1)
row TAF-30 $([[ $RC -eq 0 && "$OUT" == *"TEST-AFFECTED MISS: s1_test was not selected for src/a/y.cpp"* && "$OUT" != *"MISS: s2_test"* && "$OUT_FULL" != *MISS* && "$OUT_DOCS" == *"plan: DOCS"* && "$OUT_DOCS" == *"MISS: s1_test was not selected for doc/a.md"* ]] && echo 1 || echo 0) \
    "--explain-miss: s1 (not pulled y) is named, s2 (selected) is not, a FULL plan names nothing, a docs-only plan (no suite ran) names the failing suite"

run test/ci-gate.sh test/regression-package.sh
row TAF-32 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_get makes)" == "lint-test-scripts package-contract-test" ]] && echo 1 || echo 0) \
    "test/ci-gate.sh and test/regression-package.sh plan package-contract-test, not FULL (makes: $(plan_get makes), mode $(plan_get mode))"

# ---- GH #564: test/unit-tests.conf and test/CMakeLists.txt are planned by what changed (git mode)
G reset -q --hard HEAD
defs s8_test "QT_TESTCASE_SOURCEDIR=$R/weird"   # a Qt suite whose data root is test/ would be selected by every file there
mkdir -p "$R/test"
cp "$W/unit-tests.conf" "$R/test/unit-tests.conf"
cat >"$R/test/CMakeLists.txt" <<'EOF'
# fixture
if(NOT EMSCRIPTEN)
    add_executable(s1_test s1_test.cpp)
    target_link_libraries(s1_test PRIVATE liba)
    add_test(NAME s1_tests COMMAND s1_test)
endif()
add_executable(s2_test s2_test.cpp)
add_executable(s3_test s3_test.cpp) # trailing comment
add_test(NAME s3_tests COMMAND s3_test)
set(SHARED "a;b")
EOF
G add -A; G commit -q -m "conf fixtures"
TOK=(--smoke smoke_test --plan "$W/plan" --root "$R" --build "$B" --conf "$CONF" --manifest "$R/test/unit-tests.conf")
trun() { OUT=$(perl "$TOOL" "${TOK[@]}" --base HEAD 2>&1); RC=$?; }   # the planner in git mode, manifest = the fixture's own
reset_fx() { G checkout -q -- test/unit-tests.conf test/CMakeLists.txt; }
both_makes() { [[ "$(plan_get makes)" == *traceability-check-subset* && "$(plan_get makes)" == *traceability-selftest* ]] && echo 1 || echo 0; }

sedi 's/^s2_test 1 /s2_test 2 /' "$R/test/unit-tests.conf"
trun
row TAF-33 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s2_test,smoke_test" && $(both_makes) == 1 ]] && echo 1 || echo 0) \
    "a changed row-count pin in unit-tests.conf plans that suite, the smoke set and the traceability checks, not FULL (got $(plan_get mode) $(plan_suites) / $(plan_get makes))"
reset_fx

sedi 's/^# expect: 5$/# expect: 6/; s/^s9_test 1$/s9_test 1\ns10_test 1/' "$R/test/unit-tests.conf"
trun
row TAF-34 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s10_test,smoke_test" && "$OUT" == *"# expect: total changed"* ]] && echo 1 || echo 0) \
    "a new suite line (and the bumped # expect: total) plans the new suite only and says the total is proven by the manifest checks (got $(plan_get mode) $(plan_suites))"
reset_fx

sedi '/^s9_test 1$/d' "$R/test/unit-tests.conf"
trun
row TAF-35 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == smoke_test ]] && echo 1 || echo 0) \
    "a removed suite line is not FULL (the manifest checks prove the registry both ways) (got $(plan_get mode) $(plan_suites))"
reset_fx

sedi 's/^s3_test 1$/?s3_test 1/' "$R/test/unit-tests.conf"
sedi '1a # gate: qt\n# os: native' "$R/test/unit-tests.conf"
trun
row TAF-36 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s3_test,smoke_test" && "$OUT" == *"directive"* && "$OUT" == *"unit-test-wasm NEEDED"* ]] && echo 1 || echo 0) \
    "a changed ? marker plans its suite, a changed # gate:/# os: directive adds no suite, says so, and still names the platform runs (got $(plan_get mode) $(plan_suites))"
reset_fx

sedi 's/^s2_test 1 /s2_test 2 /' "$R/test/unit-tests.conf"
echo '// s1 changed' >>"$R/test/s1_test.cpp"
trun
row TAF-37 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s1_test,s2_test,smoke_test" ]] && echo 1 || echo 0) \
    "a pin change of one suite and a source change of another are both planned, the union (got $(plan_get mode) $(plan_suites))"
reset_fx; G checkout -q -- test/s1_test.cpp

echo 'bogus line without a count' >>"$R/test/unit-tests.conf"
trun
row TAF-38 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"could not read which suites changed"* ]] && echo 1 || echo 0) \
    "a changed manifest line of no known shape is FULL, saying so (got $(plan_get mode))"
reset_fx

cat >>"$R/test/CMakeLists.txt" <<'EOF'
if(NOT EMSCRIPTEN)
    add_executable(s10_test s10_test.cpp)
    add_test(NAME s10_tests COMMAND s10_test)
endif()
EOF
echo 's10_test 1' >>"$R/test/unit-tests.conf"
trun
row TAF-39 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s10_test,smoke_test" && $(both_makes) == 1 ]] && echo 1 || echo 0) \
    "a new add_executable/add_test block (and its manifest line) plans the new suite, not FULL (got $(plan_get mode) $(plan_suites))"
reset_fx

sedi 's/target_link_libraries(s1_test PRIVATE liba)/target_link_libraries(s1_test PRIVATE liba libb)/' "$R/test/CMakeLists.txt"
trun
row TAF-40 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s1_test,smoke_test" && $(both_makes) == 1 ]] && echo 1 || echo 0) \
    "a changed target_link_libraries of one suite plans that suite only, and the traceability checks (the matrix reads CMake) (got $(plan_get mode) $(plan_suites))"
reset_fx

echo 'include_directories(/x)' >>"$R/test/CMakeLists.txt"
trun
row TAF-41 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"names no declared suite: include_directories(/x)"* ]] && echo 1 || echo 0) \
    "a changed CMake statement that names no suite is FULL and is quoted (got $(plan_get mode))"
reset_fx
sedi 's/^set(SHARED "a;b")$/set(SHARED "a;b;c")/' "$R/test/CMakeLists.txt"
trun
row TAF-42 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"names no declared suite: set(SHARED"* ]] && echo 1 || echo 0) \
    "a changed shared variable is FULL (got $(plan_get mode))"
reset_fx

sedi 's/^if(NOT EMSCRIPTEN)$/if(NOT WIN32)/' "$R/test/CMakeLists.txt"
trun
row TAF-43 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s1_test,smoke_test" ]] && echo 1 || echo 0) \
    "a changed if() condition plans the suites inside that block (their context changed), not FULL (got $(plan_get mode) $(plan_suites))"
reset_fx

sedi 's/^add_executable(s2_test s2_test.cpp)$/if(WIN32)\nadd_executable(s2_test s2_test.cpp)\nendif()/' "$R/test/CMakeLists.txt"
trun
row TAF-44 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s2_test,smoke_test" ]] && echo 1 || echo 0) \
    "wrapping an existing suite in a new if() plans that suite (context is part of the statement's identity) (got $(plan_get mode) $(plan_suites))"
reset_fx

sedi 's/^add_executable(s2_test s2_test.cpp)$/add_executable(s2_test s2_test.cpp)\nadd_executable(s2_test s2_test.cpp)/' "$R/test/CMakeLists.txt"
trun
row TAF-47 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s2_test,smoke_test" ]] && echo 1 || echo 0) \
    "a statement repeated in the same context (its count changed) plans the suite it names (got $(plan_get mode) $(plan_suites))"
reset_fx

# ---- GH #564 round 2: order, quoting, parse anomalies, else arms, removed suites, non-target commands
cmk="$R/test/CMakeLists.txt"
variant() {  # variant: commit the CMakeLists.txt currently in the tree as the new base (undone by unvariant)
    G commit -q -am "variant"
}
unvariant() { G reset -q --hard HEAD~1; }

sedi '/^set(SHARED "a;b")$/d' "$cmk"; sedi '1a set(SHARED "a;b")' "$cmk"
trun
row TAF-48 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"names no declared suite: set(SHARED"* ]] && echo 1 || echo 0) \
    "a shared statement moved to another place in test/CMakeLists.txt is FULL (CMake is order-dependent) (got $(plan_get mode))"
reset_fx

sedi '/^add_executable(s2_test s2_test.cpp)$/d' "$cmk"; sedi 's/^add_test(NAME s3_tests COMMAND s3_test)$/add_test(NAME s3_tests COMMAND s3_test)\nadd_executable(s2_test s2_test.cpp)/' "$cmk"
trun
row TAF-49 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == *s2_test* ]] && echo 1 || echo 0) \
    "a suite's statement moved past other suites' statements plans that suite, not FULL (got $(plan_get mode) $(plan_suites))"
reset_fx

sedi '/^add_executable(s3_test s3_test.cpp)/d; /^add_test(NAME s3_tests COMMAND s3_test)$/d' "$cmk"
sedi 's/^set(SHARED "a;b")$/set(SHARED "a;b")\nadd_executable(s3_test s3_test.cpp)\nadd_test(NAME s3_tests COMMAND s3_test)/' "$cmk"
trun
row TAF-50 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"moved past shared statements"* ]] && echo 1 || echo 0) \
    "a suite's statements moved past a shared statement are FULL (directory options reach later targets only) (got $(plan_get mode))"
reset_fx

cat >>"$cmk" <<'EOF'
target_compile_definitions(s1_test PRIVATE RX="^#" Y=1)
target_link_libraries(s2_test PRIVATE
    liba # note one
    libb)
if(WIN32)
    target_link_libraries(s1_test PRIVATE liba)
else()
    target_link_libraries(s2_test PRIVATE libb)
endif()
set(SHARED2 "a)" b)
set(SHARED3 s1_test)
EOF
variant
sedi 's/Y=1)/Y=2)/' "$cmk"
trun
row TAF-51 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s1_test,smoke_test" ]] && echo 1 || echo 0) \
    "a change after a quoted # in an argument is seen, and selects the suite (got $(plan_get mode) $(plan_suites))"
reset_fx
sedi 's/# note one/# note two/' "$cmk"
trun
row TAF-52 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == smoke_test ]] && echo 1 || echo 0) \
    "a comment-only change inside a multi-line statement selects no suite and is not FULL (got $(plan_get mode) $(plan_suites))"
reset_fx
sedi '/^if(WIN32)$/,/^endif()$/c if(WIN32)\n    target_link_libraries(s1_test PRIVATE liba)\n    target_link_libraries(s2_test PRIVATE libb)\nendif()' "$cmk"
trun
row TAF-53 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == *s2_test* ]] && echo 1 || echo 0) \
    "a statement moved from the else() arm into the if() arm plans its suite (the arm is part of the context) (got $(plan_get mode) $(plan_suites))"
reset_fx
sedi 's/^set(SHARED2 "a)" b)$/set(SHARED2 "a)" c)/' "$cmk"
trun
row TAF-54 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"set(SHARED2"* ]] && echo 1 || echo 0) \
    "a changed shared statement holding a quoted ) is FULL (quotes are respected when the statement ends) (got $(plan_get mode))"
reset_fx
sedi 's/^set(SHARED3 s1_test)$/set(SHARED3 s1_test s2_test)/' "$cmk"
trun
row TAF-55 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"set(SHARED3"* ]] && echo 1 || echo 0) \
    "a changed non-target command that merely holds a suite name is FULL, not that suite (got $(plan_get mode))"
reset_fx
unvariant

cat >>"$cmk" <<'EOF'
target_compile_definitions(s1_test PRIVATE Q=\"x\")
find_package(Foo QUIET)
EOF
variant
sedi 's/^find_package(Foo QUIET)$/find_package(Foo REQUIRED)/' "$cmk"
trun
row TAF-56 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"find_package(Foo"* ]] && echo 1 || echo 0) \
    "an escaped quote outside quotes does not swallow later statements: a shared change after it is FULL (got $(plan_get mode))"
reset_fx
unvariant

printf '#[[ a bracket comment (unbalanced\n]]\n' >>"$cmk"
trun
row TAF-57 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"cannot read with confidence"* ]] && echo 1 || echo 0) \
    "bracket syntax in test/CMakeLists.txt is a parse anomaly: FULL (got $(plan_get mode))"
reset_fx
printf '#[[ a one-line bracket comment ]]\n' >>"$cmk"
trun
row TAF-60 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"cannot read with confidence"* ]] && echo 1 || echo 0) \
    "a one-line bracket comment is a parse anomaly: FULL (got $(plan_get mode))"
reset_fx
printf 'set(BR [[x]])\n' >>"$cmk"
trun
row TAF-61 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"cannot read with confidence"* ]] && echo 1 || echo 0) \
    "a bracket argument is a parse anomaly: FULL (got $(plan_get mode))"
reset_fx
printf 'add_executable(s9x s9x.cpp\n' >>"$cmk"
trun
row TAF-58 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"cannot read with confidence"* ]] && echo 1 || echo 0) \
    "a statement left unterminated at the end of the file is a parse anomaly: FULL (got $(plan_get mode))"
reset_fx

sedi '/^add_executable(s3_test s3_test.cpp)/d; /^add_test(NAME s3_tests COMMAND s3_test)$/d' "$cmk"
sedi '/^s3_test 1$/d' "$R/test/unit-tests.conf"
trun
row TAF-59 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == smoke_test ]] && echo 1 || echo 0) \
    "removing a suite from the manifest and from CMake together is not FULL (the base manifest names it) (got $(plan_get mode) $(plan_suites))"
reset_fx

# ---- GH #564 round 3: a failing child the planner reads is FULL; every parse-anomaly arm has its row
STUB="$W/stub"; mkdir -p "$STUB"
printf '#!/bin/sh\nexit 2\n' >"$STUB/diff"; chmod +x "$STUB/diff"
sedi 's/^set(SHARED "a;b")$/set(SHARED "a;b;c")/' "$cmk"
OUT=$(PATH="$STUB:$PATH" perl "$TOOL" "${TOK[@]}" --base HEAD 2>&1); RC=$?
row TAF-62 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"could not diff"* ]] && echo 1 || echo 0) \
    "a diff that exits 2 makes a CMake change FULL, never an empty change list (got $(plan_get mode))"
NOD="$W/nodiff"; mkdir -p "$NOD"; for t in perl git env; do ln -sf "$(command -v $t)" "$NOD/$t"; done
OUT=$(PATH="$NOD" "$(command -v perl)" "$TOOL" "${TOK[@]}" --base HEAD 2>&1); RC=$?
row TAF-63 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"could not diff"* ]] && echo 1 || echo 0) \
    "with no diff on PATH a CMake change is FULL (got $(plan_get mode))"
reset_fx
printf '#!/bin/sh\nfor a in "$@"; do [ "$a" = diff ] && exit 128; done\nexec %s "$@"\n' "$(command -v git)" >"$STUB/git"; chmod +x "$STUB/git"; rm -f "$STUB/diff"
sedi 's/^set(SHARED "a;b")$/set(SHARED "a;b;c")/' "$cmk"
OUT=$(PATH="$STUB:$PATH" perl "$TOOL" "${TOK[@]}" --base HEAD 2>&1); RC=$?
row TAF-64 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"git command that reads the change failed"* ]] && echo 1 || echo 0) \
    "a failing git diff makes the plan FULL, not an empty change set (got $(plan_get mode))"
reset_fx; rm -f "$STUB/git"

sedi '/^set(SHARED "a;b")$/d' "$cmk"
trun
row TAF-65 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"names no declared suite: set(SHARED"* ]] && echo 1 || echo 0) \
    "deleting a shared statement is FULL (the old side is attributed too) (got $(plan_get mode))"
reset_fx

anomaly() {  # anomaly ID TEXT-TO-APPEND DESCRIPTION: appended text only, so the sole reason for FULL is the parse
    printf '%b' "$2" >>"$cmk"
    trun
    row "$1" $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"cannot read with confidence"* ]] && echo 1 || echo 0) "$3 (got $(plan_get mode))"
    reset_fx
}
anomaly TAF-66 ']\n' "a stray top-level token is a parse anomaly: FULL"
anomaly TAF-67 'word\n' "an identifier not followed by ( is a parse anomaly: FULL"
anomaly TAF-68 'endif()\n' "an end*() with nothing open is a parse anomaly: FULL"
anomaly TAF-69 'if(WIN32)\n' "an if() still open at the end of the file is a parse anomaly: FULL"
anomaly TAF-70 'target_link_libraries(s2_test PRIVATE liba #[[ note ]]\n    libb)\n' "a bracket comment inside a statement is a parse anomaly: FULL"

anomaly TAF-72 'else()\n' "an else() with nothing open is a parse anomaly: FULL"
anomaly TAF-73 'elseif(WIN32)\n' "an elseif() with nothing open is a parse anomaly: FULL"
printf '#!/bin/sh\nfor a in "$@"; do [ "$a" = -s ] && exit 128; done\nexec %s "$@"\n' "$(command -v git)" >"$STUB/git"; chmod +x "$STUB/git"
sedi 's/^s2_test 1 /s2_test 2 /' "$R/test/unit-tests.conf"
OUT=$(PATH="$STUB:$PATH" perl "$TOOL" "${TOK[@]}" --base HEAD 2>&1); RC=$?
row TAF-74 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"git command that reads the change failed"* ]] && echo 1 || echo 0) \
    "a failing git ls-files -s (the submodule pointer probe) makes the plan FULL, not a skipped probe (got $(plan_get mode))"
reset_fx; rm -f "$STUB/git"

echo 'target_compile_definitions(s1_test PRIVATE X="a\")" Y=1)' >>"$cmk"
variant
sedi 's/Y=1)/Y=2)/' "$cmk"
trun
row TAF-71 $([[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == "s1_test,smoke_test" ]] && echo 1 || echo 0) \
    "an escaped quote inside a quoted argument does not end it: a change after \")\" selects only the suite (got $(plan_get mode) $(plan_suites))"
reset_fx
unvariant

r45=1
for tf in doc/testing/TRACEABILITY-MATRIX.md test/refresh-traceability-matrix.pl; do
    printf '%s\n' "$tf" >"$W/files"
    OUT=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "$CONF" --files "$W/files" --smoke smoke_test --plan "$W/plan" 2>&1); RC=$?
    [[ $RC -eq 0 && "$(plan_get mode)" == subset && "$(plan_suites)" == smoke_test && $(both_makes) == 1 ]] || { r45=0; echo "  (TAF-45: $tf gave $(plan_get mode) $(plan_suites) / $(plan_get makes))"; }
done
row TAF-45 $r45 "the traceability matrix, and its generator, each alone plan the traceability check and its self-test, not FULL"

printf 'test/unit-tests.conf\n' >"$W/files"
OUT=$(perl "$TOOL" --root "$R" --build "$B" --manifest "$W/unit-tests.conf" --conf "$CONF" --files "$W/files" --smoke smoke_test --plan "$W/plan" 2>&1); RC=$?
row TAF-46 $([[ $RC -eq 0 && "$(plan_get mode)" == full && "$OUT" == *"could not read which suites changed"* ]] && echo 1 || echo 0) \
    "with no git diff to read (--files) a manifest change stays FULL (got $(plan_get mode))"

r75=1
for tf in test/zz-helper.sh test/00regression/lint-zz.sh test/packaging/zz.sh test/test-affected-selftest.sh test/00regression/scripts/foo-func.sh; do
    run "$tf"
    [[ "$(plan_get makes)" == *lint-test-scripts* ]] || { r75=0; echo "  (TAF-75: $tf plans makes=[$(plan_get makes)])"; }
done
run test/zz-helper.sh
[[ "$(plan_get mode)" == full ]] || r75=0
run test/00regression/scripts/foo-func.sh
[[ "$(plan_get rows)" == foo-func ]] || r75=0
row TAF-75 $r75 "a changed test shell script (generic, harness dir, packaging, the selftest, a row script) plans make lint-test-scripts, the preflight lints a branch otherwise never runs (GH #564)"

r77=1
for tf in test/ci-gate.sh test/regression-package.sh; do
    run "$tf"
    [[ "$(plan_get makes)" == "lint-test-scripts package-contract-test" ]] || { r77=0; echo "  (TAF-77: $tf plans makes=[$(plan_get makes)])"; }
done
row TAF-77 $r77 "test/ci-gate.sh and test/regression-package.sh EACH plan package-contract-test and lint-test-scripts (TAF-32 runs them together, which hides a rule that lost one)"

lts=$(awk '/^lint-test-scripts:/{f=1} f&&/^$/{exit} f' "$HERE/../Makefile")
r76=1
for l in test/00regression/lint-traps.sh test/lint-timeouts.sh test/lint-pipe-grepq.sh test/lint-sed-inplace.sh; do
    [[ "$lts" == *"$l"* ]] || { r76=0; echo "  (TAF-76: lint-test-scripts does not run $l)"; }
    [[ -f "$HERE/../$l" ]] || r76=0
done
row TAF-76 $r76 "the lint-test-scripts recipe runs all four test-script lints (traps, timeouts, quiet pipe grep, bare sed -i), each an existing script"

ul=$(sed -n 's/^UNIT_TEST_LINTS *:= *//p' "$HERE/../Makefile")
row TAF-78 $([[ " $ul " == *" lint-test-scripts "* ]] && echo 1 || echo 0) \
    "UNIT_TEST_LINTS names lint-test-scripts, so make unit-test and make test-affected lint every tracked test script whatever the plan (GH #564 review: 7 scripts no conf rule reached)"

PL=$(mktemp -d "$W/plant.XXXXXX"); printf '#!/bin/bash\ntimeout 5 true\n' >"$PL/bad.sh"
LTS_OUT=$(JNEXT_LINT_TIMEOUTS_DIR="$PL" LANG=C make -C "$HERE/.." --no-print-directory lint-test-scripts 2>&1); LTS_RC=$?
row TAF-79 $([[ $LTS_RC -ne 0 && "$LTS_OUT" == *"[lint-timeouts]"*"offenders: 1"* && "$LTS_OUT" == *"[lint-pipe-grepq] scanned"* && "$LTS_OUT" == *"[lint-sed-i] scanned"* ]] && echo 1 || echo 0) \
    "make lint-test-scripts exits non-zero when one lint finds a planted offender, and the lints after it still ran (rc=$LTS_RC)"

echo ""
echo "Total: $total  Passed: $passed  Failed: $failed  Skipped: 0"
[[ "$total" -eq 78 ]] || { echo "  FAIL test-affected-selftest: ran $total rows, expected 78"; exit 1; }
[[ "$failed" -eq 0 ]]
