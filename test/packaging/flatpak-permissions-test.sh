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

printf "\n"
printf "Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n" \
    "$((pass + fail))" "$pass" "$fail" 0
[ "$fail" -eq 0 ]
