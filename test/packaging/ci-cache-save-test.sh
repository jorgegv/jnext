#!/usr/bin/env bash
#
# Contract test: ci.yml restores its caches everywhere and saves them only from a push to main (GH #547).
#
# Why: one full CI run saves ~6.5 GB of ccache entries. A branch, pull-request or gate run saves them under
# its own ref, which main cannot read, and GitHub's 10 GB repository limit then evicts main's entries, so
# main ran with a cold cache (59 min against 40 warm, 2026-11-21). Every `actions/cache` step of ci.yml is
# therefore a `restore` plus, at the end of the job, a `save` whose `if:` demands a push to main.
# Oracle: that rule, read from the workflow file. No VHDL: this is tooling.
#
#   CICS-01  ci.yml holds no combined `actions/cache@` step (it would save on every ref)
#   CICS-02  every `actions/cache/save` step's `if:` requires a push to refs/heads/main, and so does
#            the flatpak-builder action's `save-cache` in flatpak-build.yml
#   CICS-03  every `actions/cache/restore` has a save with the same key and path, and every save a restore
#   CICS-04  the checker refuses fixtures: the save condition is PARSED (top-level && conjuncts), so ||, a
#            negation and a trailing || term are refused; a commented-out or `true` flatpak save-cache;
#            a combined cache, a save with no (or a wrong) condition, a
#            restore with no save, a save with no restore, a workflow with no cache step at all
#   CICS-05  flatpak-build.yml (GH #583): no combined cache step, every cache save needs a main push, every
#            restore is fed by a save of the same key and path or by the builder action's save
#            (path .flatpak-builder, key <cache-key>-x86_64), and every save has a restore
#   CICS-06  the checker refuses flatpak-build fixtures: a save without the main condition, a save whose key
#            differs from its restore, a restore of the builder state whose key is not the action's save key,
#            a restore with no feeder
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
WF=${CI_CACHE_SAVE_WF:-$ROOT/.github/workflows/ci.yml}   # the override is for mutation runs
EXPECTED_ROWS=6
total=0; passed=0; failed=0
row() {
    local id=$1 ok=$2 text=$3
    total=$((total + 1))
    if [[ "$ok" == 1 ]]; then echo "  PASS $id $text"; passed=$((passed + 1))
    else echo "  FAIL $id $text"; failed=$((failed + 1)); fi
}
finish() {
    printf 'Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n' "$total" "$passed" "$failed" 0
    [[ $total -eq $EXPECTED_ROWS ]] || { echo "FAIL: reported $total rows, expected $EXPECTED_ROWS"; exit 1; }
    [[ $failed -eq 0 ]]
    exit
}
command -v python3 >/dev/null 2>&1 || { echo "  missing tool: python3"; for i in 1 2 3 4 5 6; do row "CICS-0$i" 0 "not run: python3 missing"; done; finish; }

# check <workflow> <rule>: print the offending steps, one per line; exit 1 when any, 2 when the file has no cache step
check() {
    python3 -I -c '
import sys, yaml
d = yaml.safe_load(open(sys.argv[1]))
rule = sys.argv[2]
MAIN = ("github.event_name == \x27push\x27", "github.ref == \x27refs/heads/main\x27")
def conjuncts(c):
    # strip ${{ }}, split on top-level && (parentheses and quotes respected)
    c = str(c).strip()
    if c.startswith("${{") and c.endswith("}}"): c = c[3:-2].strip()
    parts, depth, q, cur, i = [], 0, None, "", 0
    while i < len(c):
        ch = c[i]
        if q:
            if ch == q: q = None
        elif ch in "\x27\"": q = ch
        elif ch == "(": depth += 1
        elif ch == ")": depth -= 1
        if not q and depth == 0 and c[i:i+2] == "&&":
            parts.append(cur.strip()); cur = ""; i += 2; continue
        cur += ch; i += 1
    parts.append(cur.strip())
    return parts
def cond_ok(c):
    # the two conjuncts must stand alone at the top level: an `||` or a negation would
    # make them part of a larger conjunct and so absent from the list
    parts = conjuncts(c)
    return all(m in parts for m in MAIN)
bad = []; restores = []; saves = []; n = 0
for jn, j in (d.get("jobs") or {}).items():
    for st in j.get("steps") or []:
        u = st.get("uses", "")
        if not u.startswith("actions/cache"): continue
        n += 1
        w = st.get("with") or {}
        ident = "%s/%s" % (jn, st.get("name", "?"))
        if u.startswith("actions/cache@"): bad.append("combined cache step " + ident)
        elif u.startswith("actions/cache/restore@"): restores.append((jn, str(w.get("key")), str(w.get("path")), ident))
        elif u.startswith("actions/cache/save@"):
            saves.append((jn, str(w.get("key")), str(w.get("path")), ident))
            c = str(st.get("if", ""))
            if rule in ("cond", "fpwf") and not cond_ok(c): bad.append("save without a main-push condition: " + ident)
if rule == "flatpak":
    n = 0
    for jn, j in (d.get("jobs") or {}).items():
        for st in j.get("steps") or []:
            if str(st.get("uses", "")).startswith("flatpak/flatpak-github-actions/flatpak-builder"):
                n += 1
                if not cond_ok((st.get("with") or {}).get("save-cache", "")): bad.append("flatpak-builder step without a main-push save-cache: %s/%s" % (jn, st.get("name", st.get("uses"))))
    if n == 0: sys.exit(2)
    print("\n".join(bad)); sys.exit(1 if bad else 0)
if n == 0: sys.exit(2)
if rule == "fpwf":
    # restores must be fed by a save of the same key and path, or by the flatpak-builder action'"'"'s own save
    ksave = []
    for jn, j in (d.get("jobs") or {}).items():
        for st in j.get("steps") or []:
            if str(st.get("uses", "")).startswith("flatpak/flatpak-github-actions/flatpak-builder"):
                w = st.get("with") or {}
                if cond_ok(w.get("save-cache", "")): ksave.append((jn, str(w.get("cache-key")) + "-x86_64", ".flatpak-builder"))
    for r in restores:
        if not any(s[:3] == r[:3] for s in saves) and not any(k == r[:3] for k in ksave): bad.append("restore without a feeding save: " + r[3])
    for s in saves:
        if not any(s[:3] == r[:3] for r in restores): bad.append("save without a restore: " + s[3])
    print("\n".join(bad)); sys.exit(1 if bad else 0)
if rule == "pair":
    for r in restores:
        if not any(s[:3] == r[:3] for s in saves): bad.append("restore without a save of the same key and path: " + r[3])
    for s in saves:
        if not any(s[:3] == r[:3] for r in restores): bad.append("save without a restore: " + s[3])
if rule == "plain":
    bad = [b for b in bad if b.startswith("combined")]
elif rule != "pair" and rule != "cond": sys.exit(3)
if rule == "cond": bad = [b for b in bad if b.startswith("save")]
if rule == "pair": bad = [b for b in bad if not b.startswith("combined") and not b.startswith("save without a main")]
print("\n".join(bad)); sys.exit(1 if bad else 0)
' "$1" "$2"
}

for pair in "CICS-01:plain:holds no combined actions/cache step" \
            "CICS-02:cond:every save step requires a push to main" \
            "CICS-03:pair:every restore has a save of the same key and path, and back"; do
    id=${pair%%:*}; rest=${pair#*:}; rule=${rest%%:*}; text=${rest#*:}
    out=$(check "$WF" "$rule"); rc=$?
    # the Flatpak job caches through its action, not actions/cache: its save-cache toggle carries the same rule
    if [[ $rc -eq 0 && "$id" == CICS-02 ]]; then
        out=$(check "${CI_CACHE_SAVE_FLATPAK:-$ROOT/.github/workflows/flatpak-build.yml}" flatpak); rc=$?
    fi
    if [[ $rc -eq 0 ]]; then row "$id" 1 "ci.yml $text"
    else row "$id" 0 "ci.yml: ${out:-checker exit $rc}"; fi
done

# CICS-05: the flatpak-build.yml cache steps (GH #583)
FPW=${CI_CACHE_SAVE_FLATPAK:-$ROOT/.github/workflows/flatpak-build.yml}
out=$(check "$FPW" fpwf); rc=$?
if [[ $rc -eq 0 ]]; then row CICS-05 1 "flatpak-build.yml restores are fed, saves need a push to main and have a restore"
else row CICS-05 0 "flatpak-build.yml: ${out:-checker exit $rc}"; fi

# CICS-04: fixtures
TMP=$(mktemp -d "${TMPDIR:-/tmp}/ci-cache-save-test.XXXXXX"); trap 'rm -rf "$TMP"' EXIT
mk() {  # mk <file> <restore-block> <save-block>
    printf 'jobs:\n  j:\n    steps:\n%s%s' "$2" "$3" > "$1"
}
R=$'      - name: Cache x\n        uses: actions/cache/restore@v5\n        with:\n          path: p\n          key: k\n'
S=$'      - name: Save x\n        if: github.event_name == \'push\' && github.ref == \'refs/heads/main\'\n        uses: actions/cache/save@v5\n        with:\n          path: p\n          key: k\n'
mk "$TMP/ok.yml" "$R" "$S"
mk "$TMP/combined.yml" $'      - name: Cache x\n        uses: actions/cache@v5\n        with:\n          path: p\n          key: k\n' ""
mk "$TMP/nocond.yml" "$R" $'      - name: Save x\n        uses: actions/cache/save@v5\n        with:\n          path: p\n          key: k\n'
mk "$TMP/wrongcond.yml" "$R" $'      - name: Save x\n        if: github.event_name == \'push\'\n        uses: actions/cache/save@v5\n        with:\n          path: p\n          key: k\n'
mk "$TMP/nosave.yml" "$R" ""
mk "$TMP/norestore.yml" "" "$S"
mk "$TMP/otherkey.yml" "$R" "${S/key: k/key: other}"
printf 'jobs:\n  j:\n    steps:\n      - run: true\n' > "$TMP/none.yml"
# conditions that merely CONTAIN both conjuncts: ||, a negation, a trailing || term, a PR alternative
SAVE_IF() { printf '      - name: Save x\n        if: %s\n        uses: actions/cache/save@v5\n        with:\n          path: p\n          key: k\n' "$1"; }
P="github.event_name == 'push'"; M="github.ref == 'refs/heads/main'"
mk "$TMP/c-or.yml"  "$R" "$(SAVE_IF "$P || $M")"
mk "$TMP/c-not.yml" "$R" "$(SAVE_IF "\${{ !($P && $M) }}")"
mk "$TMP/c-tail.yml" "$R" "$(SAVE_IF "$P && $M || github.event_name == 'workflow_dispatch'")"
mk "$TMP/c-pr.yml"  "$R" "$(SAVE_IF "$P && $M || github.event_name == 'pull_request' && steps.x.outputs.cache-hit != 'true'")"
mk "$TMP/c-good.yml" "$R" "$(SAVE_IF "\${{ $P && $M && (matrix.leg == 'a' || matrix.leg == 'b') }}")"
FB() { printf 'jobs:\n  j:\n    steps:\n      - uses: flatpak/flatpak-github-actions/flatpak-builder@v6\n        with:\n          cache-key: k\n%s' "$1"; }
FB "          save-cache: \${{ $P && $M }}"$'\n' > "$TMP/fb-ok.yml"
FB "          # save-cache: \${{ $P && $M }}"$'\n' > "$TMP/fb-commented.yml"
FB "          save-cache: true"$'\n' > "$TMP/fb-true.yml"
FB "          save-cache: \${{ $P || $M }}"$'\n' > "$TMP/fb-or.yml"
msg=""
for r in plain cond pair; do check "$TMP/ok.yml" "$r" >/dev/null || msg+=" ok-refused-by-$r;"; done
check "$TMP/combined.yml" plain >/dev/null && msg+=" combined-accepted;"
check "$TMP/nocond.yml" cond >/dev/null && msg+=" no-condition-accepted;"
check "$TMP/wrongcond.yml" cond >/dev/null && msg+=" push-only-condition-accepted;"
check "$TMP/nosave.yml" pair >/dev/null && msg+=" restore-without-save-accepted;"
check "$TMP/norestore.yml" pair >/dev/null && msg+=" save-without-restore-accepted;"
check "$TMP/otherkey.yml" pair >/dev/null && msg+=" different-key-accepted;"
check "$TMP/none.yml" cond >/dev/null; [[ $? -eq 2 ]] || msg+=" no-cache-steps-not-refused;"
for f in c-or c-not c-tail c-pr; do check "$TMP/$f.yml" cond >/dev/null && msg+=" $f-condition-accepted;"; done
check "$TMP/c-good.yml" cond >/dev/null || msg+=" good-compound-condition-refused;"
check "$TMP/fb-ok.yml" flatpak >/dev/null || msg+=" good-flatpak-refused;"
for f in fb-commented fb-true fb-or; do check "$TMP/$f.yml" flatpak >/dev/null && msg+=" $f-accepted;"; done
check "$TMP/ok.yml" flatpak >/dev/null; [[ $? -eq 2 ]] || msg+=" no-flatpak-step-not-refused;"
if [[ -z "$msg" ]]; then row CICS-04 1 "the checker accepts a good fixture and refuses each broken one"
else row CICS-04 0 "$msg"; fi

# CICS-06: flatpak-build fixtures, made from the real workflow by one edit each
TMPF=$(mktemp -d "${TMPDIR:-/tmp}/ci-cache-save-fp.XXXXXX")
msg=""
check "$FPW" fpwf >/dev/null || msg+=" real-refused;"
sed "s| \&\& github.ref == 'refs/heads/main' \&\& steps| \&\& steps|" "$FPW" > "$TMPF/s1-nomain.yml"
awk '/\(S1\)/{f=1} f && /^ *key:/{$0=$0 "x"; f=0} {print}' "$FPW" > "$TMPF/s1-otherkey.yml"
sed 's|key: flatpak-builder-\${{ github.sha }}-x86_64|key: flatpak-builder-${{ github.sha }}-x86_65|' "$FPW" > "$TMPF/r2-otherkey.yml"
awk '/- name: Save the Flatpak source zip/{skip=1} skip && /^$/{skip=0; next} !skip{print}' "$FPW" > "$TMPF/nofeeder.yml"
for f in s1-nomain s1-otherkey r2-otherkey nofeeder; do
    cmp -s "$FPW" "$TMPF/$f.yml" && msg+=" $f-fixture-unchanged;"
    check "$TMPF/$f.yml" fpwf >/dev/null && msg+=" $f-accepted;"
done
rm -rf "$TMPF"
if [[ -z "$msg" ]]; then row CICS-06 1 "the checker accepts the real flatpak-build.yml and refuses each broken variant"
else row CICS-06 0 "$msg"; fi

finish
