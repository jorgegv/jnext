#!/usr/bin/env bash
#
# Contract tests for packaging/flatpak/verify-permissions.sh (GH #271).
#
# That script is the gate standing between "the manifest says --share=network"
# and "the thing we shipped actually has it". A gate is only worth having if it
# can FAIL, so both directions are pinned here, and the failing direction is
# pinned against the shapes a naive check would wave through:
#
#   * `network` present as a substring somewhere else in the file
#     (filesystems=/srv/network) — a bare `grep network` passes, the app still
#     has no network namespace;
#   * `shared=networking;` — a prefix/substring match passes, flatpak grants
#     nothing;
#   * `shared=network;` in a DIFFERENT section — a file-wide match passes, only
#     [Context] is what `flatpak run` enforces.
#
# Hermetic: pure bash on fabricated `metadata` files plus a stubbed `flatpak`
# on PATH for the installed-app-id target, in the same spirit as
# verify-bundle-test.sh. It never builds a flatpak and never touches the
# caller's flatpak installation. The REAL bundle is checked by the
# package-flatpak row of packaging-test.sh and by `make package-flatpak` itself.
#
set -u

cd "$(dirname "$0")/../.." || exit 2   # repo root

SCRIPT=packaging/flatpak/verify-permissions.sh
RED='\033[0;31m'; GREEN='\033[0;32m'; BOLD='\033[1m'; RESET='\033[0m'

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0; fail=0
ok()  { printf "  ${GREEN}PASS${RESET} %-10s %s\n" "$1" "$2"; pass=$((pass+1)); }
bad() { printf "  ${RED}FAIL${RESET} %-10s %s\n" "$1" "$2"; fail=$((fail+1)); }

# mkmeta <name> <context-body...> — build an app dir holding a metadata file
# whose [Context] section is the given lines. Returns the dir path on stdout.
mkmeta() {
    local name=$1; shift
    local d="$WORK/$name"
    mkdir -p "$d"
    {
        echo "[Application]"
        echo "name=io.github.zxjogv.jnext"
        echo "runtime=org.kde.Platform/x86_64/6.10"
        echo ""
        echo "[Context]"
        printf '%s\n' "$@"
    } > "$d/metadata"
    printf '%s' "$d"
}

# run_case <id> <desc> <want_rc> <target> [<pattern>...]
#   pattern  — fixed substring that must appear in the combined output
#   !pattern — fixed substring that must NOT appear
run_case() {
    local id=$1 desc=$2 want_rc=$3 target=$4; shift 4
    local out rc p okrow=1 why=""
    out=$(bash "$SCRIPT" "$target" 2>&1); rc=$?
    [ "$rc" -eq "$want_rc" ] || { okrow=0; why="exit $rc, wanted $want_rc"; }
    for p in "$@"; do
        case "$p" in
            '!'*) grep -qF -- "${p#!}" <<<"$out" && { okrow=0; why="${why:+$why; }present '${p#!}'"; } ;;
            *)    grep -qF -- "$p"    <<<"$out" || { okrow=0; why="${why:+$why; }missing '$p'"; } ;;
        esac
    done
    if [ "$okrow" -eq 1 ]; then
        ok "$id" "$desc"
    else
        bad "$id" "$desc [$why]"
        printf '%s\n' "$out" | sed -e 's/^/        | /' | head -20
    fi
}

printf "${BOLD}=== verify-permissions.sh contract (GH #271) ===${RESET}\n\n"

# ---------------------------------------------------------------- accepting
# FPKP-01  The shape the FIXED manifest produces: network present among the
#          shared tokens. Must be accepted, and must say what it read.
d=$(mkmeta ok-network "shared=network;ipc;" "sockets=pulseaudio;wayland;x11;")
run_case FPKP-01 "app dir with shared=network;ipc; is accepted" 0 "$d" \
    "flatpak permissions OK" "shared=network;ipc;"

# FPKP-02  Token order must not matter — flatpak writes the list sorted, and a
#          future permission would change the neighbours of `network`.
d=$(mkmeta ok-order "shared=ipc;network;")
run_case FPKP-02 "token order is irrelevant (ipc;network;)" 0 "$d" \
    "flatpak permissions OK"

# FPKP-03  The metadata FILE itself, not only its directory (a caller may hold
#          the path flatpak-builder wrote).
d=$(mkmeta ok-file "shared=network;ipc;")
run_case FPKP-03 "a 'metadata' file path is accepted directly" 0 "$d/metadata" \
    "flatpak permissions OK"

# ---------------------------------------------------------------- refusing
# FPKP-04  THE GH #271 SHAPE, byte-for-byte what jnext 1.0.1 shipped: every
#          other permission, no network. This is the row that had to fail and
#          did not exist.
d=$(mkmeta gh271 "shared=ipc;" "sockets=pulseaudio;wayland;x11;" \
                 "devices=dri;" "filesystems=home;")
run_case FPKP-04 "shared=ipc; (the shipped 1.0.1 shape) is REFUSED" 1 "$d" \
    "MISSING required permission" "network" "GH #271" \
    '!flatpak permissions OK'

# FPKP-05  `network` as a substring of an unrelated value: a bare grep passes,
#          the sandbox still has no network.
d=$(mkmeta substring "shared=ipc;" "filesystems=home;/srv/network;")
run_case FPKP-05 "network only inside filesystems= is REFUSED" 1 "$d" \
    "MISSING required permission" \
    '!flatpak permissions OK'

# FPKP-06  A near-miss token: `networking` is not `network`, and flatpak grants
#          nothing for it. A prefix match would pass this.
d=$(mkmeta nearmiss "shared=networking;ipc;")
run_case FPKP-06 "shared=networking; (near-miss token) is REFUSED" 1 "$d" \
    "MISSING required permission" \
    '!flatpak permissions OK'

# FPKP-07  The right token in the WRONG section. Only [Context] is enforced by
#          flatpak, so a file-wide match must not count this.
d="$WORK/wrongsection"; mkdir -p "$d"
cat > "$d/metadata" <<'EOF'
[Application]
name=io.github.zxjogv.jnext
shared=network;

[Context]
shared=ipc;
EOF
run_case FPKP-07 "shared=network; outside [Context] is REFUSED" 1 "$d" \
    "MISSING required permission" \
    '!flatpak permissions OK'

# FPKP-08  No [Context] section at all (a truncated or wrong-format file). The
#          failure direction is toward refusing, never toward a silent pass.
d="$WORK/nocontext"; mkdir -p "$d"
printf '[Application]\nname=io.github.zxjogv.jnext\n' > "$d/metadata"
run_case FPKP-08 "metadata with no [Context] is REFUSED" 1 "$d" \
    "MISSING required permission" "<absent>" \
    '!flatpak permissions OK'

# ------------------------------------------------------- unusable targets
# FPKP-09  A directory that is not a flatpak-builder app dir must be a hard
#          error, not a pass. (A stale/renamed build dir is the realistic way
#          this happens.)
mkdir -p "$WORK/notanappdir"
run_case FPKP-09 "a directory without 'metadata' is a hard error" 1 "$WORK/notanappdir" \
    "holds no 'metadata'" \
    '!flatpak permissions OK'

# FPKP-10  Too many arguments: usage error (exit 2), distinct from a verdict.
out=$(bash "$SCRIPT" a b 2>&1); rc=$?
if [ "$rc" -eq 2 ] && grep -qF "usage:" <<<"$out"; then
    ok FPKP-10 "two arguments is a usage error (exit 2)"
else
    bad FPKP-10 "two arguments should exit 2 with usage — got rc=$rc"
fi

# ------------------------------------------- installed-app-id target (stub)
# The app-id branch asks `flatpak info --show-permissions`. Stub flatpak so
# both verdicts AND the "app not installed" case are pinned without installing
# anything. PATH is prepended for these three rows only.
mkdir -p "$WORK/bin"
cat > "$WORK/bin/flatpak" <<'STUB'
#!/usr/bin/env bash
# args: info --show-permissions <id>
case "${STUB_MODE:-}" in
    net)     printf '[Context]\nshared=network;ipc;\nsockets=x11;\n' ;;
    nonet)   printf '[Context]\nshared=ipc;\nsockets=x11;\n' ;;
    missing) echo "error: io.github.zxjogv.jnext not installed" >&2; exit 1 ;;
esac
STUB
chmod +x "$WORK/bin/flatpak"

stub_case() {
    local id=$1 desc=$2 mode=$3 want_rc=$4; shift 4
    local out rc p okrow=1 why=""
    out=$(PATH="$WORK/bin:$PATH" STUB_MODE="$mode" \
          bash "$SCRIPT" io.github.zxjogv.jnext 2>&1); rc=$?
    [ "$rc" -eq "$want_rc" ] || { okrow=0; why="exit $rc, wanted $want_rc"; }
    for p in "$@"; do
        case "$p" in
            '!'*) grep -qF -- "${p#!}" <<<"$out" && { okrow=0; why="${why:+$why; }present '${p#!}'"; } ;;
            *)    grep -qF -- "$p"    <<<"$out" || { okrow=0; why="${why:+$why; }missing '$p'"; } ;;
        esac
    done
    if [ "$okrow" -eq 1 ]; then ok "$id" "$desc"
    else bad "$id" "$desc [$why]"; printf '%s\n' "$out" | sed -e 's/^/        | /' | head -20; fi
}

# FPKP-11  An installed app that HAS the permission.
stub_case FPKP-11 "installed app id with network is accepted" net 0 \
    "flatpak permissions OK" "installed app io.github.zxjogv.jnext"

# FPKP-12  An installed app that does NOT — the state a user left behind with
#          `flatpak override --unshare=network`, and the state every 1.0.1
#          install is in.
stub_case FPKP-12 "installed app id without network is REFUSED" nonet 1 \
    "MISSING required permission" \
    '!flatpak permissions OK'

# FPKP-13  Not installed / flatpak errors: a hard error, never a pass.
stub_case FPKP-13 "an unreadable app id is a hard error" missing 1 \
    "neither an existing path nor an installed flatpak" \
    '!flatpak permissions OK'

# ------------------------------------------------- the gate has to be WIRED
# Three rows in the same spirit as package-recipe-guard-test.sh (GH #148),
# which reads the Makefile: a gate nobody calls is not a gate, and this one has
# callers that can drift apart. `make package-flatpak` is the local path
# (FPKP-14). The CI path is .github/workflows/flatpak-build.yml, which
# deliberately builds through the upstream flatpak-builder action instead of
# that make target (a declared exception documented in its header), so its call
# to the gate has to be named separately (FPKP-15) — and release.yml has to
# still be delegating to that file rather than carrying its own copy of the
# steps (FPKP-16). Dropping any of the three leaves a build that can ship the
# GH #271 bug again with every other row here still green.

# All three rows reduce their file to the lines that ACTUALLY RUN and match only
# there. A plain substring grep is not enough, and this is not a theoretical
# worry — review mutated both call sites into comments and both rows stayed
# green:
#
#     Makefile      @# $(MAKE) verify-flatpak-permissions   (disabled)
#     the workflow  # - name: Verify sandbox permissions ...
#                   #   run: make verify-flatpak-permissions ...
#
# "Comment it out while chasing something else" is an utterly ordinary edit,
# and it silently reships GH #271. So: recipe lines whose command text begins
# with `#` (with or without make's `@`/`-`/`+` prefixes) do not count, and
# neither do YAML comment lines.

# --- FPKP-14: the package-flatpak RECIPE ------------------------------------
# Scoped to the recipe body — from `package-flatpak:` to the line before the
# next target — because `verify-flatpak-permissions:` is defined immediately
# below it and a range that swallowed one more line would match the target's
# own name with the call deleted. Then: recipe lines only (leading TAB),
# make's `@`/`-`/`+` line prefixes stripped, commented-out lines dropped, and
# any trailing ` #...` cut so the name cannot hide in an end-of-line comment.
mk_active=$(awk '
    /^package-flatpak:/            { inr = 1; next }
    inr && /^[a-zA-Z0-9_.-]+:/     { exit }
    inr {
        if ($0 !~ /^\t/) next               # not a recipe line at all
        line = $0
        sub(/^\t+/, "", line)
        while (line ~ /^[@+-]/) sub(/^[@+-]/, "", line)
        sub(/^[ \t]+/, "", line)
        if (line ~ /^#/) next                # commented-out recipe line
        sub(/[ \t]#.*$/, "", line)          # trailing comment
        print line
    }' Makefile)
if grep -q 'verify-flatpak-permissions' <<<"$mk_active"; then
    ok FPKP-14 "make package-flatpak invokes the permission gate"
else
    bad FPKP-14 "package-flatpak does not RUN verify-flatpak-permissions (commented out or gone) — a local build would ship without the gate"
fi

# yml_active <workflow-file> <job-name> <key> — print the value of every
# `<key>:` inside that job that the runner will ACTUALLY ACT ON, and nothing
# else. <key> is `run` (a command) or `uses` (a called action or workflow).
#
# Requiring the directive itself is the point: a step's own `name:` contains
# the target's name too, so "an active line mentioning it" would still pass
# with the step commented out down to its name. YAML comment lines are dropped.
# Handles both `run: cmd` and a `run: |` block (any deeper-indented
# continuation lines count as part of it). Scoped to the one job: the range
# ends at the next job key.
#
# Shared by FPKP-15 and FPKP-16 so every workflow that must keep the gate wired
# is held to the SAME standard — a future hardening of this extractor cannot
# land on one and miss the others.
yml_active() {
    awk -v job="$2" -v key="$3" '
        $0 ~ "^  " job ":"                   { inj = 1; next }
        inj && /^  [a-zA-Z0-9_-]+:/          { exit }
        !inj                                 { next }
        {
            raw = $0
            line = raw; sub(/^[ \t]+/, "", line)
            if (line ~ /^#/) next                       # YAML comment
            ind = match(raw, /[^ ]/) - 1
            if (in_d && ind > d_ind) { print line; next }
            in_d = 0
            if (line ~ "^(- )?" key ":[ \t]*") {
                d_ind = ind; in_d = 1
                sub("^(- )?" key ":[ \t]*", "", line)
                print line
            }
        }' "$1"
}

# --- FPKP-15: the SHARED flatpak definition ---------------------------------
# .github/workflows/flatpak-build.yml is the ONE definition of how the bundle
# is built and gated. release.yml calls it (FPKP-16) and it is also dispatchable
# by hand, which is what finally lets the gate be exercised without cutting a
# public release — every artifact job in release.yml is skipped for a private
# tag, so on the real v1.0.28 run the gate did not execute.
#
# Three things are pinned, because the file only helps while all three hold: it
# exists, both triggers are still declared (workflow_call for release.yml and
# ci.yml, workflow_dispatch for a human), and the job still RUNS the gate.
wf=.github/workflows/flatpak-build.yml
why=""
[ -f "$wf" ] || why="the shared flatpak definition is gone"
for trig in workflow_call workflow_dispatch; do
    [ -n "$why" ] && break
    grep -qE "^ *$trig:" "$wf" || why="it no longer declares $trig, so one way in is dead"
done
if [ -z "$why" ] && ! grep -q 'verify-flatpak-permissions' \
        <<<"$(yml_active "$wf" flatpak-build run)"; then
    why="its flatpak-build job does not RUN verify-flatpak-permissions (commented out, renamed or gone)"
fi
if [ -z "$why" ]; then
    ok FPKP-15 "the shared flatpak definition is callable + dispatchable and RUNS the gate"
else
    bad FPKP-15 "$wf: $why — the bundle could ship ungated again, and there would be no way to exercise the gate short of a public release"
fi

# --- FPKP-16: BOTH workflows DELEGATE to that definition --------------------
# Every path that builds a bundle has to be the gated path. An active job-level
# `uses:` of the shared file is what makes one manual run evidence about the
# release build and the push build rather than about lookalikes of them, so a
# workflow that goes back to its own inline copy of the steps fails here —
# with or without a gate in that copy.
#
# Both are checked because both had a copy, and the ci.yml one is the reason
# this matters beyond tidiness: it was release.yml's job copied near-verbatim
# MINUS the permission gate, so from the day GH #271 was fixed a bundle that
# stopped carrying --share=network would still have gone green on every push.
#
# Matched on the `uses:` value as a whole line, so the path cannot slip in as
# part of some unrelated `with:` value.
why=""
for f in release ci; do
    [ -n "$why" ] && break
    grep -qxF './.github/workflows/flatpak-build.yml' \
        <<<"$(yml_active ".github/workflows/$f.yml" flatpak uses)" \
        || why="$f.yml's flatpak job does not USE ./.github/workflows/flatpak-build.yml (commented out, or back to an inline copy)"
done
if [ -z "$why" ]; then
    ok FPKP-16 "release.yml and ci.yml both delegate to the shared definition"
else
    bad FPKP-16 "$why — that bundle would no longer be built by the definition every other row here covers"
fi

# --- FPKP-17..21: prefetch-sources.sh (GH #497) -----------------------------
# specnext.com sometimes answers with a ~12 KB page instead of the zip; the
# script must keep fetching until the manifest's sha256 matches, back off
# 2/4/8 s, and never leave a wrong file in flatpak-builder's cache. The
# oracle for "right" is the sha256 in the manifest, here a fabricated one.
PF=packaging/flatpak/prefetch-sources.sh
good="$WORK/good.bin"; printf 'the real zip\n' > "$good"
gsum=$(sha256sum "$good" | cut -d' ' -f1)
pfm="$WORK/pf.yml"
printf 'modules:\n  - sources:\n      - type: file\n        url: https://example.invalid/d/x.zip\n        sha256: %s\n' "$gsum" > "$pfm"
# fake fetcher: serves the wrong body for the first $FAKE_BAD calls, then the right one
cat > "$WORK/fakefetch" <<'FAKE'
#!/usr/bin/env bash
n=$(cat "$FAKE_STATE/n" 2>/dev/null || echo 0); n=$((n+1)); echo "$n" > "$FAKE_STATE/n"
if [ "$n" -le "$FAKE_BAD" ]; then echo '<html>busy</html>' > "$2"; else cp "$FAKE_GOOD" "$2"; fi
FAKE
printf '#!/usr/bin/env bash\necho "$1" >> "$FAKE_STATE/sleeps"\n' > "$WORK/fakesleep"
chmod +x "$WORK/fakefetch" "$WORK/fakesleep"
# pf_run <bad-count> — fresh state + cache; sets pf_rc, pf_calls, pf_sleeps, pf_cached
pf_run() {
    rm -rf "$WORK/pfs" "$WORK/pfc"; mkdir -p "$WORK/pfs"
    FAKE_STATE="$WORK/pfs" FAKE_BAD=$1 FAKE_GOOD="$good" PREFETCH_FETCH="$WORK/fakefetch" \
        PREFETCH_SLEEP="$WORK/fakesleep" bash "$PF" "$pfm" "$WORK/pfc" >/dev/null 2>&1
    pf_rc=$?
    pf_calls=$(cat "$WORK/pfs/n" 2>/dev/null || echo 0)
    pf_sleeps=$(cat "$WORK/pfs/sleeps" 2>/dev/null | tr "\n" " ")
    pf_cached=no; [ -f "$WORK/pfc/downloads/$gsum/x.zip" ] && pf_cached=yes
}
pf_run 0
if [ "$pf_rc" -eq 0 ] && [ "$pf_calls" -eq 1 ] && [ -z "$pf_sleeps" ] && [ "$pf_cached" = yes ]; then
    ok FPKP-17 "prefetch: a good first fetch is cached with one call and no wait"
else bad FPKP-17 "prefetch good first fetch: rc=$pf_rc calls=$pf_calls sleeps='$pf_sleeps' cached=$pf_cached"; fi
pf_run 2
if [ "$pf_rc" -eq 0 ] && [ "$pf_calls" -eq 3 ] && [ "$pf_sleeps" = "2 4 " ] && [ "$pf_cached" = yes ] \
   && [ "$(sha256sum "$WORK/pfc/downloads/$gsum/x.zip" | cut -d' ' -f1)" = "$gsum" ]; then
    ok FPKP-18 "prefetch: two wrong bodies then a good one is cached after waits of 2 s, 4 s"
else bad FPKP-18 "prefetch 2 bad: rc=$pf_rc calls=$pf_calls sleeps='$pf_sleeps' cached=$pf_cached"; fi
pf_run 99
if [ "$pf_rc" -eq 0 ] && [ "$pf_calls" -eq 4 ] && [ "$pf_sleeps" = "2 4 8 " ] && [ "$pf_cached" = no ] \
   && [ -z "$(find "$WORK/pfc" -type f 2>/dev/null)" ]; then
    ok FPKP-19 "prefetch: always-wrong body gives up after 1+3 attempts (2/4/8 s), exit 0, nothing cached"
else bad FPKP-19 "prefetch always bad: rc=$pf_rc calls=$pf_calls sleeps='$pf_sleeps' cached=$pf_cached"; fi
pf_run 0   # now cached; a second run must not fetch
FAKE_STATE="$WORK/pfs" FAKE_BAD=0 FAKE_GOOD="$good" PREFETCH_FETCH="$WORK/fakefetch" \
    PREFETCH_SLEEP="$WORK/fakesleep" bash "$PF" "$pfm" "$WORK/pfc" >/dev/null 2>&1
if [ "$(cat "$WORK/pfs/n")" -eq 1 ]; then
    ok FPKP-20 "prefetch: a source already cached with the right sha256 is not fetched again"
else bad FPKP-20 "prefetch re-fetched a cached source (calls=$(cat "$WORK/pfs/n"))"; fi
# --- FPKP-22: a WRONG file already in the cache is refetched and replaced
# (flatpak-builder itself accepts a corrupt cache entry, so this check is the guard).
rm -rf "$WORK/pfs" "$WORK/pfc"; mkdir -p "$WORK/pfs" "$WORK/pfc/downloads/$gsum"
echo '<html>busy</html>' > "$WORK/pfc/downloads/$gsum/x.zip"
FAKE_STATE="$WORK/pfs" FAKE_BAD=0 FAKE_GOOD="$good" PREFETCH_FETCH="$WORK/fakefetch" \
    PREFETCH_SLEEP="$WORK/fakesleep" bash "$PF" "$pfm" "$WORK/pfc" >/dev/null 2>&1
if [ "$(cat "$WORK/pfs/n" 2>/dev/null || echo 0)" -eq 1 ] \
   && [ "$(sha256sum "$WORK/pfc/downloads/$gsum/x.zip" | cut -d' ' -f1)" = "$gsum" ]; then
    ok FPKP-22 "prefetch: a wrong file already in the cache is refetched and replaced"
else bad FPKP-22 "prefetch kept a wrong cached file (calls=$(cat "$WORK/pfs/n" 2>/dev/null))"; fi
# --- FPKP-23: the real curl runs with a time bound (a stall must not hold the job), via a stub curl
rm -rf "$WORK/pfs" "$WORK/pfc" "$WORK/cbin"; mkdir -p "$WORK/pfs" "$WORK/cbin"
printf '#!/usr/bin/env bash\necho "$*" >> "$WORK_ARGS"; exit 28\n' > "$WORK/cbin/curl"; chmod +x "$WORK/cbin/curl"
WORK_ARGS="$WORK/pfs/curl-args" FAKE_STATE="$WORK/pfs" PREFETCH_SLEEP="$WORK/fakesleep" PATH="$WORK/cbin:$PATH" \
    bash "$PF" "$pfm" "$WORK/pfc" >/dev/null 2>&1; pf_rc=$?
cargs=$(cat "$WORK/pfs/curl-args" 2>/dev/null)
if [ "$pf_rc" -eq 0 ] && grep -q -- '--connect-timeout' <<<"$cargs" && grep -q -- '--speed-limit' <<<"$cargs" \
   && grep -q -- '--speed-time' <<<"$cargs" && [ "$(wc -l < "$WORK/pfs/curl-args")" -eq 4 ]; then
    ok FPKP-23 "prefetch: curl gets --connect-timeout/--speed-limit/--speed-time; a timed-out curl is retried (4 calls) then handed back (exit 0)"
else bad FPKP-23 "prefetch curl options/retry: rc=$pf_rc args='$cargs'"; fi
# --- FPKP-24: local `make package-flatpak` prefetches too (same targets as CI)
mk_pf=$(awk '/^package-flatpak:/{inr=1;next} inr && /^[a-zA-Z0-9_.-]+:/{exit} inr && /^\t/{l=$0; sub(/^\t+/,"",l); while(l ~ /^[@+-]/) sub(/^[@+-]/,"",l); if(l !~ /^#/) print l}' Makefile)
if grep -q 'flatpak-prefetch' <<<"$mk_pf" && awk '/flatpak-prefetch/{a=NR} /^flatpak-builder /{b=NR} END{exit !(a&&b&&a<b)}' <<<"$mk_pf"; then
    ok FPKP-24 "make package-flatpak runs flatpak-prefetch before flatpak-builder"
else bad FPKP-24 "package-flatpak does not run flatpak-prefetch before flatpak-builder"; fi
# --- FPKP-21: the shared workflow RUNS the prefetch, BEFORE the builder action
# (commented-out steps do not count: same extractor as FPKP-15).
wfa=$(yml_active .github/workflows/flatpak-build.yml flatpak-build run)
wfu=$(yml_active .github/workflows/flatpak-build.yml flatpak-build uses)
if grep -qx 'make flatpak-prefetch' <<<"$wfa" && grep -q 'flatpak-builder@' <<<"$wfu" \
   && awk '/^ *run: make flatpak-prefetch/{a=NR} /^ *- uses: flatpak\/flatpak-github-actions\/flatpak-builder@/{b=NR} END{exit !(a && b && a<b)}' .github/workflows/flatpak-build.yml; then
    ok FPKP-21 "flatpak-build.yml runs make flatpak-prefetch before the flatpak-builder action"
else bad FPKP-21 "flatpak-build.yml does not run 'make flatpak-prefetch' before the builder action"; fi

# --- FPKP-25: the distro zip is restored from a CI cache before the prefetch (GH #583)
# Oracle: the manifest's url/sha256 pairs, parsed as prefetch-sources.sh parses them. R1 restores by
# content (key and path carry basename and sha256), R2 runs only on an R1 miss and its restore-keys is a
# prefix of the action's save key, S1 saves only from main on an R1 miss; order checkout < R1 < R2 <
# prefetch < builder action < S1. Fixtures (mutations of the real workflow) are refused.
cat > "$WORK/srcchk.py" <<'PY'
import sys, yaml, re
wf, manifest, mut = sys.argv[1], sys.argv[2], (sys.argv[3] if len(sys.argv) > 3 else "")
pairs, u = [], None
for line in open(manifest):
    m = re.match(r"\s*(?:- )?url:\s*(\S+)", line)
    if m: u = m.group(1)
    m = re.match(r"\s*sha256:\s*(\S+)", line)
    if m and u: pairs.append((u.rsplit("/", 1)[1], m.group(1))); u = None
d = yaml.safe_load(open(wf))
steps = d["jobs"]["flatpak-build"]["steps"]
def find(pred):
    return [i for i, s in enumerate(steps) if pred(s)]
isR1 = lambda s: str(s.get("uses", "")).startswith("actions/cache/restore@") and str((s.get("with") or {}).get("path", "")).startswith(".flatpak-builder/downloads/")
isR2 = lambda s: str(s.get("uses", "")).startswith("actions/cache/restore@") and str((s.get("with") or {}).get("path", "")) == ".flatpak-builder"
isS1 = lambda s: str(s.get("uses", "")).startswith("actions/cache/save@") and str((s.get("with") or {}).get("path", "")).startswith(".flatpak-builder/downloads/")
# mutations of the parsed workflow
if mut:
    r1 = steps[find(isR1)[0]]; r2 = steps[find(isR2)[0]]; s1 = steps[find(isS1)[0]]
    if mut == "stale-sha": r1["with"]["key"] = r1["with"]["key"][:-1] + "1"
    if mut == "path-no-sha": r1["with"]["path"] = ".flatpak-builder/downloads"
    if mut == "r1-after-prefetch":
        steps.remove(r1); steps.insert(find(lambda s: s.get("run") == "make flatpak-prefetch")[0] + 1, r1)
    if mut == "r2-no-miss": del r2["if"]
    if mut == "r2-action-prefix": r2["with"]["restore-keys"] = "flatpak-builder-x86_64"
    if mut == "s1-no-main": s1["if"] = s1["if"].replace(" && github.ref == 'refs/heads/main'", "")
    if mut == "s1-no-miss": s1["if"] = s1["if"].replace(" && steps.srczip.outputs.cache-hit != 'true'", "")
    if mut == "no-r1": steps.remove(r1)
bad = []
r1i, r2i, s1i = find(isR1), find(isR2), find(isS1)
co = find(lambda s: str(s.get("uses", "")).startswith("actions/checkout@"))
pf = find(lambda s: s.get("run") == "make flatpak-prefetch")
fb = find(lambda s: str(s.get("uses", "")).startswith("flatpak/flatpak-github-actions/flatpak-builder@"))
if not (r1i and r2i and s1i and co and pf and fb): print("missing step: R1=%s R2=%s S1=%s checkout=%s prefetch=%s builder=%s" % (r1i, r2i, s1i, co, pf, fb)); sys.exit(1)
r1, r2, s1 = steps[r1i[0]], steps[r2i[0]], steps[s1i[0]]
if not (co[0] < r1i[0] < r2i[0] < pf[0] < fb[0] < s1i[0]): bad.append("order checkout<R1<R2<prefetch<builder<S1 broken")
for base, sha in pairs:
    w = r1["with"]
    if base not in str(w.get("key")) or sha not in str(w.get("key")): bad.append("R1 key lacks %s / %s" % (base, sha))
    if sha not in str(w.get("path")): bad.append("R1 path lacks " + sha)
if (s1.get("with") or {}) != r1["with"]: bad.append("S1 key/path differ from R1")
rid = r1.get("id")
if not rid or ("steps.%s.outputs.cache-hit != 'true'" % rid) not in str(r2.get("if", "")): bad.append("R2 if does not require R1 miss")
conj = [c.strip() for c in str(s1.get("if", "")).replace("${{", "").replace("}}", "").split("&&")]
for need in ("github.event_name == 'push'", "github.ref == 'refs/heads/main'", "steps.%s.outputs.cache-hit != 'true'" % rid):
    if need not in conj: bad.append("S1 if lacks " + need)
fbw = steps[fb[0]]["with"]
save_key = str(fbw["cache-key"]) + "-x86_64"
rk = str((r2.get("with") or {}).get("restore-keys", "")).strip()
if str(r2["with"].get("key")) != save_key: bad.append("R2 key is not the action's save key")
if not rk or not save_key.startswith(rk): bad.append("R2 restore-keys %r is not a prefix of the action save key" % rk)
print("\n".join(bad)); sys.exit(1 if bad else 0)
PY
WFY=.github/workflows/flatpak-build.yml; MFY=packaging/flatpak/io.github.zxjogv.jnext.yml
if ! command -v python3 >/dev/null 2>&1; then bad FPKP-25 "python3 missing: not run"
else
    why=""
    out=$(python3 -I "$WORK/srcchk.py" "$WFY" "$MFY" 2>&1) || why="real workflow refused: $out"
    for m in stale-sha path-no-sha r1-after-prefetch r2-no-miss r2-action-prefix s1-no-main s1-no-miss no-r1; do
        python3 -I "$WORK/srcchk.py" "$WFY" "$MFY" "$m" >/dev/null 2>&1 && why="$why; fixture $m accepted"
    done
    if [ -z "$why" ]; then
        ok FPKP-25 "flatpak-build.yml: R1 restores the zip by name+sha256, R2 only on a miss, S1 saves from main only, in order; 8 broken fixtures refused"
    else bad FPKP-25 "$why"; fi
fi

# --- FPKP-26..28: what a failed prefetch attempt logs (GH #583). Fake fetcher: copies $FAKE_BODY to the
# destination unless $FAKE_NOFILE, prints $FAKE_META (printf %b), exits $FAKE_RC.
cat > "$WORK/fakefetch2" <<'FAKE'
#!/usr/bin/env bash
[ -n "${FAKE_NOFILE:-}" ] || cp "$FAKE_BODY" "$2"
printf '%b' "${FAKE_META:-}"
exit "${FAKE_RC:-0}"
FAKE
chmod +x "$WORK/fakefetch2"
# pf2_run <body-file> <meta> <rc> [nofile]: stderr to $WORK/pf2.err; sets pf_rc
pf2_run() {
    rm -rf "$WORK/pfs" "$WORK/pfc"; mkdir -p "$WORK/pfs"
    FAKE_STATE="$WORK/pfs" FAKE_BODY="$1" FAKE_META="$2" FAKE_RC="$3" FAKE_NOFILE="${4:-}" PREFETCH_FETCH="$WORK/fakefetch2" \
        PREFETCH_SLEEP="$WORK/fakesleep" bash "$PF" "$pfm" "$WORK/pfc" >/dev/null 2>"$WORK/pf2.err"
    pf_rc=$?
    pf_diag=$(grep -c ' failed: curl exit ' "$WORK/pf2.err")
    pf_nfiles=$(find "$WORK/pfc" -type f 2>/dev/null | wc -l)
}
# a Cloudflare-style 12097-byte page: the first 200 bytes are the literal below (no newline in them)
h200='<!DOCTYPE html><html lang="en-US"><head><title>Just a moment...</title><meta http-equiv="Content-Type" content="text/html; charset=UTF-8"><meta name="robots" content="noindex,nofollow"></head><body><p'
printf '%s' "$h200" > "$WORK/html.bin"
head -c $((12097 - ${#h200})) /dev/zero | tr '\0' 'x' >> "$WORK/html.bin"
pf2_run "$WORK/html.bin" '200 text/html; charset=UTF-8' 0
errs=$(cat "$WORK/pf2.err"); why=""
[ "${#h200}" -eq 200 ] && [ "$(wc -c < "$WORK/html.bin")" -eq 12097 ] || why="fixture is not 200/12097 bytes"
[ "$pf_rc" -eq 0 ] && [ "$pf_diag" -eq 4 ] && [ "$pf_nfiles" -eq 0 ] || why="$why; rc=$pf_rc diag-lines=$pf_diag cached-files=$pf_nfiles"
for n in 1 2 3 4; do grep -qF " attempt $n of 4 failed: " <<<"$errs" || why="$why; no 'attempt $n of 4'"; done
hsum=$(sha256sum "$WORK/html.bin" | cut -d' ' -f1)
while IFS= read -r l; do
    case "$l" in *' failed: curl exit '*) ;; *) continue ;; esac
    for want in 'curl exit 0, HTTP 200, 12097 bytes, type text/html; charset=UTF-8, sha256 '"$hsum"' (manifest '"$gsum"')' "starts: \"$h200\""; do
        [[ "$l" == *"$want"* ]] || why="$why; line lacks '$want'"
    done
done <<<"$errs"
if [ -z "$why" ]; then ok FPKP-26 "prefetch: a wrong 12097-byte 200 page logs one line per attempt (exit, HTTP, size, type, sha256, first 200 bytes), exit 0, nothing cached"
else bad FPKP-26 "$why"; fi

# FPKP-27: hostile body and content type: no control byte, no line starting with `::`, cut at byte 200
{ printf 'AB\r\nC\033[31mD\000E\303\251F\n::error::pwned\n::add-mask::x'; head -c 204 /dev/zero | tr '\0' 'p'; printf 'MARK250'; head -c 500 /dev/zero | tr '\0' 'q'; } > "$WORK/hostile.bin"
pf2_run "$WORK/hostile.bin" '200 text/html\r\n::warning::y' 0
errs=$(cat "$WORK/pf2.err"); why=""
want27='AB..C.[31mD.E..F.::error::pwned.::add-mask::x'$(printf 'p%.0s' $(seq 1 155))
[ "${#want27}" -eq 200 ] || why="expected string is ${#want27} long"
[ "$pf_diag" -eq 4 ] || why="$why; $pf_diag diagnostic lines"
while IFS= read -r l; do
    case "$l" in *' failed: curl exit '*) [[ "$l" == *"starts: \"$want27\""$'' ]] || why="$why; excerpt differs: ${l##*starts: }"
        [[ "$l" == *'type text/html..::warning::y, sha256'* ]] || why="$why; type not sanitised" ;; esac
done <<<"$errs"
if grep -q '[^[:print:]]' <<<"$errs"; then why="$why; a non-printable byte reached the log"; fi
if grep -q '^::' <<<"$errs"; then why="$why; a log line starts with ::"; fi
if grep -q 'MARK250' <<<"$errs"; then why="$why; bytes beyond 200 were logged"; fi
# a printable content type over 100 characters is cut at 100 (marker after character 100 must not appear)
x89=$(printf 'x%.0s' $(seq 1 89)); x120=$(printf 'x%.0s' $(seq 1 120))
pf2_run "$WORK/hostile.bin" "200 text/html; ${x120}CTMARK" 0
grep -qF "type text/html; ${x89}, sha256 " "$WORK/pf2.err" || why="$why; long type not cut to exactly 100 characters"
if grep -q 'CTMARK' "$WORK/pf2.err"; then why="$why; content type beyond 100 characters was logged"; fi
if [ -z "$why" ]; then ok FPKP-27 "prefetch: hostile body/type is logged as printable ASCII, cut at byte 200 (type at 100 characters), never starting a line (no ::command::)"
else bad FPKP-27 "$why"; fi

# FPKP-28: an empty body, and a fetch that fails writing nothing, neither trips set -u
: > "$WORK/empty.bin"
pf2_run "$WORK/empty.bin" '200 text/plain' 0
e_sum=$(sha256sum "$WORK/empty.bin" | cut -d' ' -f1)
why=""
[ "$pf_rc" -eq 0 ] && [ "$pf_diag" -eq 4 ] || why="empty: rc=$pf_rc diag=$pf_diag"
grep -qF "HTTP 200, 0 bytes, type text/plain, sha256 $e_sum (manifest $gsum); starts: \"\"" "$WORK/pf2.err" || why="$why; empty-body line wrong: $(head -1 "$WORK/pf2.err")"
pf2_run "$WORK/empty.bin" '' 7 nofile
[ "$pf_rc" -eq 0 ] && [ "$pf_diag" -eq 4 ] && [ "$(grep -c 'retry [123] of 3' "$WORK/pf2.err")" -eq 3 ] || why="$why; nofile: rc=$pf_rc diag=$pf_diag"
grep -qF 'curl exit 7, HTTP ?, 0 bytes, type ?, sha256 none (manifest' "$WORK/pf2.err" || why="$why; nofile line wrong: $(head -1 "$WORK/pf2.err")"
grep -qi 'unbound' "$WORK/pf2.err" && why="$why; set -u tripped"
if [ -z "$why" ]; then ok FPKP-28 "prefetch: an empty body and a failed fetch with no file log 0 bytes / sha256 none, retry 1+3 times, exit 0"
else bad FPKP-28 "$why"; fi

# FPKP-29: the real curl path keeps the error body: --fail-with-body and --write-out, never bare --fail
rm -rf "$WORK/pfs" "$WORK/pfc" "$WORK/cbin"; mkdir -p "$WORK/pfs" "$WORK/cbin"
cat > "$WORK/cbin/curl" <<'STUB'
#!/usr/bin/env bash
echo "$*" >> "$WORK_ARGS"
while [ $# -gt 0 ]; do [ "$1" = --output ] && out=$2; shift; done
printf '<html>403 Forbidden stub body</html>' > "$out"
printf '403 text/html'
exit 22
STUB
chmod +x "$WORK/cbin/curl"
WORK_ARGS="$WORK/pfs/curl-args" PREFETCH_SLEEP="$WORK/fakesleep" FAKE_STATE="$WORK/pfs" PATH="$WORK/cbin:$PATH" \
    bash "$PF" "$pfm" "$WORK/pfc" >/dev/null 2>"$WORK/pf2.err"; pf_rc=$?
cargs=$(cat "$WORK/pfs/curl-args" 2>/dev/null)
if [ "$pf_rc" -eq 0 ] && grep -q -- '--fail-with-body' <<<"$cargs" && grep -q -- '--write-out' <<<"$cargs" \
   && ! grep -qE -- '(^| )--fail( |$)' <<<"$cargs" \
   && grep -qF 'curl exit 22, HTTP 403, 36 bytes, type text/html' "$WORK/pf2.err" \
   && grep -qF 'starts: "<html>403 Forbidden stub body</html>"' "$WORK/pf2.err"; then
    ok FPKP-29 "prefetch: real curl gets --fail-with-body and --write-out (no bare --fail); a 403 body is logged"
else bad FPKP-29 "curl 403 path: rc=$pf_rc args='$cargs' err='$(head -2 "$WORK/pf2.err")'"; fi

printf "\n"
printf "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n" \
    "$((pass + fail))" "$pass" "$fail" 0
[ "$fail" -eq 0 ]
