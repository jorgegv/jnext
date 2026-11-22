# shellcheck shell=bash disable=SC2088,SC2153,SC2329
# remote-side.sh: the part of remote-run that runs ON the remote host. The driver sends it
# over ssh stdin, after a prelude that sets every RR_* variable; nothing is installed there.
# Modes (RR_MODE): run | warm | stamps | cleanup. Needs bash, git, podman (rootless), flock,
# timeout, tar, sha256sum. All work data lives under RR_DIR: nothing is written outside it
# except podman's rootless runtime state, which podman keeps under $XDG_RUNTIME_DIR.
#
# Result protocol, plain lines in the log: "=== remote-run exit: N" (the container's status),
# "=== remote-run fallback: <why>" (exit 75: the caller runs locally),
# "=== remote-run refused: <why>" (exit 3: the pushed ref is not what the caller expects).
set -uo pipefail
export LC_ALL=C LANG=C

say()      { printf '[remote-run %s] %s\n' "$(date +%H:%M:%S)" "$*"; }
fallback() { echo "=== remote-run fallback: $*"; exit 75; }
refuse()   { echo "=== remote-run refused: $*"; exit 3; }

case "$RR_DIR" in
    '~/'*) RR_DIR="$HOME/${RR_DIR#\~/}" ;;
    '~')   RR_DIR="$HOME" ;;
    *)     ;;
esac
case "$RR_DIR" in /*) ;; *) fallback "the work directory '$RR_DIR' is not an absolute path" ;; esac

MIRROR="$RR_DIR/mirror"
STAMPS="$RR_DIR/stamps"
CACHE="$RR_DIR/cache"
export TMPDIR="$RR_DIR/tmp"

# Every podman call keeps its storage under RR_DIR.
# The storage driver is detected once (pick_driver) and kept in $RR_DIR/containers/driver.
DRV_FILE="$RR_DIR/containers/driver"
PM_CMD=()
set_pm() {
    PM_CMD=(podman --root "$RR_DIR/containers/storage" --runroot "$RR_DIR/containers/run")
    if [[ -s "$DRV_FILE" ]]; then PM_CMD+=(--storage-driver "$(cat "$DRV_FILE")"); fi
}
pm() { "${PM_CMD[@]}" "$@"; }
set_pm

# Modes that need no lane or preflight.
if [[ "$RR_MODE" == stamps ]]; then
    [[ -d "$STAMPS/$RR_ID" ]] || exit 4
    exec tar -C "$STAMPS/$RR_ID" -cf - .
fi
if [[ "$RR_MODE" == cleanup ]]; then
    # the run's lane is not known here: its checkout is in runs/ or runs-L<n>/, ctl likewise
    rm -rf "$RR_DIR"/runs*/"${RR_ID:?}" "$RR_DIR"/ctl*/"${RR_ID:?}" "${STAMPS:?}/$RR_ID"
    exit 0
fi

mkdir -p "$RR_DIR" "$TMPDIR" "$STAMPS" "$RR_DIR/containers/storage" "$RR_DIR/containers/run" \
    || fallback "cannot create the work directory $RR_DIR"

# ---- preflight --------------------------------------------------------------
for c in git podman flock timeout tar sha256sum; do
    command -v "$c" >/dev/null 2>&1 || fallback "required command not found on the remote: $c"
done

# Memory: MemAvailable leaves out the ZFS ARC, which the kernel gives back under pressure, so
# the gate counts the ARC above its minimum as available; both are reported.
MEMINFO=${RR_MEMINFO:-/proc/meminfo}   # test seam
mem_avail_mb=$(awk '/^MemAvailable:/ {print int($2/1024)}' "$MEMINFO" 2>/dev/null || echo 0)
mem_total_mb=$(awk '/^MemTotal:/ {print int($2/1024)}' "$MEMINFO" 2>/dev/null || echo 0)
arc_mb=0; arc_reclaim_mb=0
if [[ -r /proc/spl/kstat/zfs/arcstats ]]; then
    arc_mb=$(awk '$1=="size" {print int($3/1048576)}' /proc/spl/kstat/zfs/arcstats)
    arc_reclaim_mb=$(awk '$1=="size" {s=$3} $1=="c_min" {m=$3} END {r=s-m; if (r<0) r=0; print int(r/1048576)}' /proc/spl/kstat/zfs/arcstats)
fi
mem_mb=$((mem_avail_mb + arc_reclaim_mb))
disk_gb=$(df -Pk "$RR_DIR" | awk 'NR==2 {print int($4/1048576)}')
say "preflight: $(podman --version), nproc=$(nproc), mem-total=${mem_total_mb}MiB mem-available=${mem_avail_mb}MiB zfs-arc=${arc_mb}MiB (reclaimable ${arc_reclaim_mb}MiB), disk-free=${disk_gb}GiB, load=$(cut -d' ' -f1-3 /proc/loadavg), dir=$RR_DIR"
[[ "$mem_mb" -ge "$RR_MIN_MEM_MB" ]] || fallback "only ${mem_mb} MiB memory available incl. reclaimable ARC (need $RR_MIN_MEM_MB)"
[[ "$disk_gb" -ge "$RR_MIN_DISK_GB" ]] || fallback "only ${disk_gb} GiB disk free (need $RR_MIN_DISK_GB)"

# The container gets a hard memory limit: the configured one, or 80% of the host's memory shared
# between the lanes when that is smaller. That needs the memory controller delegated to the user (cgroup v2).
mem_limit=$RR_MEMORY
case "$RR_MEMORY" in
    *g) cfg_mb=$(( ${RR_MEMORY%g} * 1024 )) ;;
    *m) cfg_mb=${RR_MEMORY%m} ;;
    *)  fallback "RR_MEMORY must end in g or m: '$RR_MEMORY'" ;;
esac
cap_mb=$((mem_total_mb * 8 / 10 / RR_LANES))
if [[ "$mem_total_mb" -gt 0 && "$cfg_mb" -gt "$cap_mb" ]]; then mem_limit="${cap_mb}m"; fi
if [[ -z "${RR_SKIP_CGROUP_CHECK:-}" ]]; then
    uid=$(id -u)
    ctrl=$(cat "/sys/fs/cgroup/user.slice/user-$uid.slice/user@$uid.service/cgroup.controllers" 2>/dev/null || true)
    [[ " $ctrl " == *" memory "* ]] || fallback "the memory cgroup controller is not delegated to this user (controllers: '${ctrl:-none}'); a limit cannot be set"
fi
say "container memory limit: $mem_limit (swap off)"


# ---- lane: RR_LANES runs at once, one lock each ----------------------------------
# Lane 1 keeps the names of the one-lane layout (lane.lock, runs/, ctl/, rr-<project>-<id>); lane n
# adds -L<n> to them (SFX). Only what two concurrent runs would trample is per lane (the lock, runs/
# and ctl/, the container's name and label, the LANE_CACHES directories, the FPGA clone); the other
# caches, the podman store, the mirror and the stamps are shared.
lane_lock() { if [[ "$1" -eq 1 ]]; then echo "$RR_DIR/lane.lock"; else echo "$RR_DIR/lane-L$1.lock"; fi; }
if [[ "$RR_MODE" == probe ]]; then
    # a lane is free right now: take its lock and let go at once (the real run takes it again, atomically)
    for n in $(seq 1 "$RR_LANES"); do
        exec 9>"$(lane_lock "$n")"
        if flock -n 9; then echo "probe: lane $n of $RR_LANES is free"; exit 0; fi
    done
    echo "probe: every lane of $RR_LANES is busy"; exit 75
fi
lane=0
deadline=$((SECONDS + RR_LANE_WAIT)); queued=0
while :; do
    for n in $(seq 1 "$RR_LANES"); do
        exec 9>"$(lane_lock "$n")"
        if flock -n 9; then lane=$n; break; fi
    done
    [[ $lane -eq 0 ]] || break
    if [[ $queued -eq 0 ]]; then
        say "every lane is busy: queued (waiting up to ${RR_LANE_WAIT}s)"; queued=1
    fi
    [[ $SECONDS -lt $deadline ]] || fallback "every lane stayed busy for ${RR_LANE_WAIT}s"
    sleep 1
done
SFX=""; [[ $lane -eq 1 ]] || SFX="-L$lane"
say "lane $lane of $RR_LANES"
# A lane that starts while another is busy runs at half the CPU caps (rounded up): measured no slower than
# full caps for the pair (project.conf), and it keeps two compiling lanes from oversubscribing the host.
for n in $(seq 1 "$RR_LANES"); do
    [[ $n -ne $lane ]] || continue
    exec {pfd}>"$(lane_lock "$n")"
    if ! flock -n "$pfd"; then
        RR_JOBS_DEFAULT=$(( (RR_JOBS_DEFAULT + 1) / 2 )); RR_JOBS_HEAVY=$(( (RR_JOBS_HEAVY + 1) / 2 )); RR_TEST_JOBS=$(( (RR_TEST_JOBS + 1) / 2 ))
        say "another lane is busy: caps halved (JOBS $RR_JOBS_DEFAULT, heavy $RR_JOBS_HEAVY, test jobs $RR_TEST_JOBS)"
        exec {pfd}>&-
        break
    fi
    exec {pfd}>&-
done
RUNS="$RR_DIR/runs$SFX"
CTL="$RR_DIR/ctl$SFX"
mkdir -p "$RUNS" "$CTL" || fallback "cannot create the work directory $RR_DIR"
# lane_dir <cache path>: the path of a per-lane cache directory (the first component is listed in LANE_CACHES)
lane_dir() {
    local first=${1%%/*} rest=""
    [[ "$1" != */* ]] || rest=/${1#*/}
    if [[ -n "$SFX" && " ${RR_LANE_CACHES:-} " == *" $first "* ]]; then echo "$first$SFX$rest"; else echo "$1"; fi
}

# The setup lock: the shared mirror, the image, the cache migration and the clone out of the mirror
# take it, one run at a time. The checkout, the submodules and the container run do not.
exec 8>"$RR_DIR/setup.lock"
if ! flock -n 8; then
    say "waiting for the setup lock (another run is fetching or building)"
    flock -w "$RR_LANE_WAIT" 8 || fallback "the setup lock stayed busy for ${RR_LANE_WAIT}s"
fi

# Storage driver: try the default (overlay) in the work directory, then fuse-overlayfs, then vfs;
# the first that can start is recorded in containers/driver (the default too, by the name podman
# reports) and used by every later call. Runs only while holding EVERY lane: a failed probe wipes
# the store, which must never be one a live run is using.
pick_driver() {
    local d out
    [[ ! -s "$DRV_FILE" ]] || return 0
    for d in "" fuse-overlayfs vfs; do
        if [[ -n "$d" ]]; then
            [[ "$d" != fuse-overlayfs ]] || command -v fuse-overlayfs >/dev/null 2>&1 || continue
            echo "$d" > "$DRV_FILE"
        else
            rm -f "$DRV_FILE"
        fi
        set_pm
        if out=$(pm info --format '{{.Store.GraphDriverName}}' 2>&1 </dev/null); then
            say "podman storage driver: ${out} (selected: ${d:-default})"
            [[ -n "$d" ]] || { echo "$out" > "$DRV_FILE"; set_pm; }
            return 0
        fi
        say "storage driver '${d:-default}' failed: $(head -n 1 <<<"$out")"
        rm -rf "$RR_DIR/containers/storage" "$RR_DIR/containers/run"
        mkdir -p "$RR_DIR/containers/storage" "$RR_DIR/containers/run"
    done
    rm -f "$DRV_FILE"; set_pm
    fallback "no podman storage driver works under $RR_DIR"
}
if [[ ! -s "$DRV_FILE" ]]; then
    # the other lanes must be idle for the probe; their locks are held (fds 10 and up) until it is done
    other_fds=()
    for n in $(seq 1 "$RR_LANES"); do
        [[ $n -ne $lane ]] || continue
        exec {ofd}>"$(lane_lock "$n")"
        flock -n "$ofd" || fallback "no storage driver recorded and another lane is live: probe later"
        other_fds+=("$ofd")
    done
    pick_driver
    for ofd in "${other_fds[@]}"; do exec {ofd}>&-; done
fi
set_pm   # a run that waited for a lane while another probed must pick up the driver recorded meanwhile
rootless=$(pm info --format '{{.Host.Security.Rootless}}' 2>/dev/null) || fallback "podman info failed"
[[ "$rootless" == true ]] || fallback "podman is not rootless (got '$rootless')"

# Cache directories that moved: renamed once, while holding the setup lock (same filesystem: instant).
for m in ${RR_CACHE_MIGRATE:-}; do
    if [[ -d "$CACHE/${m%%:*}" && ! -e "$CACHE/${m#*:}" ]]; then
        mkdir -p "$(dirname "$CACHE/${m#*:}")" && mv "$CACHE/${m%%:*}" "$CACHE/${m#*:}" \
            && say "cache migrated: ${m%%:*} -> ${m#*:}"
    fi
done

for m in ${RR_CACHE_SWEEP:-}; do
    find "$CACHE/$(lane_dir "$m")" -mindepth 1 -maxdepth 1 -mmin +1440 -exec rm -rf {} + 2>/dev/null
done

# lane n > 1 names its container rr-L<n>-<project>-<id>, NOT rr-<project>-L<n>-<id>: a remote side from before
# the lanes kills every container matching ^rr-<project>- at its start, and must not find lane 2's.
cname="rr-${SFX:+L$lane-}$RR_PROJECT-$RR_ID"
run="$RUNS/$RR_ID"
ctl="$CTL/$RR_ID"
stamps="$STAMPS/$RR_ID"
watchdog=""
run_pid=""
cleanup() {
    local rc=$?
    trap - EXIT
    trap '' TERM HUP INT   # a second signal (a process-group kill) must not cut the cleanup short
    [[ -z "$run_pid" ]] || kill -TERM "$run_pid" 2>/dev/null
    [[ -z "$watchdog" ]] || kill "$watchdog" 2>/dev/null
    pm kill "$cname" >/dev/null 2>&1
    rm -rf "${run:?}" "${ctl:?}"
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 143' TERM HUP INT

# The lane is ours, so anything of this lane left under RUNS/CTL or still running is an orphan.
for c in $(pm ps -q --filter "label=rr-lane=$lane" 2>/dev/null); do pm kill "$c" >/dev/null 2>&1; done
find "$RUNS" "$CTL" -mindepth 1 -maxdepth 1 -mmin +60 -exec rm -rf {} + 2>/dev/null
# stamps wait for the driver to fetch them in a later session: only a day-old one is an orphan
find "$STAMPS" -mindepth 1 -maxdepth 1 -mmin +1440 -exec rm -rf {} + 2>/dev/null

# ---- mirror -----------------------------------------------------------------
[[ -d "$MIRROR/objects" ]] || git init -q --bare "$MIRROR" || fallback "cannot create the mirror"
git -C "$MIRROR" remote get-url origin >/dev/null 2>&1 || git -C "$MIRROR" remote add origin "$RR_REPO_URL"
git -C "$MIRROR" remote set-url origin "$RR_REPO_URL"
say "fetching $RR_BRANCH from $RR_REPO_URL"
FETCH_T="${RR_FETCH_TIMEOUT:-120}s"   # a stalled network must not hold the setup lock (every lane waits on it)
if ! fetch_out=$(timeout --kill-after=5s "$FETCH_T" git -C "$MIRROR" fetch --no-tags -q origin "+refs/heads/$RR_BRANCH:refs/rr/$RR_BRANCH" 2>&1 </dev/null); then
    echo "$fetch_out"
    if [[ "$fetch_out" == *"couldn't find remote ref"* ]]; then
        refuse "branch $RR_BRANCH is not on the remote repository: push it first"
    fi
    fallback "git fetch failed (or timed out after $FETCH_T)"
fi
tip=$(git -C "$MIRROR" rev-parse "refs/rr/$RR_BRANCH") || fallback "no tip for $RR_BRANCH after fetch"
if [[ -z "$RR_SHA" ]]; then
    RR_SHA=$tip
elif [[ "$tip" != "$RR_SHA" ]]; then
    refuse "$RR_BRANCH is at $tip on the remote repository, not at the expected $RR_SHA: the push did not land or the branch moved"
fi
say "source: $RR_BRANCH @ $RR_SHA"
# The run tree gets origin/main so a tool inside the run (test/test-affected.pl --explain-miss, GH #547)
# can take a merge base. Best effort: no main on the remote, or a failed fetch, is not a failure of the run.
base_main=""
if timeout --kill-after=5s "$FETCH_T" git -C "$MIRROR" fetch --no-tags -q origin "+refs/heads/main:refs/rr/main" </dev/null >/dev/null 2>&1; then
    base_main=$(git -C "$MIRROR" rev-parse "refs/rr/main" 2>/dev/null || true)
fi

# ---- image ------------------------------------------------------------------
# The dependency script comes from the commit under test, or from RR_DEPS_REF when one is given.
deps_from="$RR_SHA"
if [[ -n "${RR_DEPS_REF:-}" ]]; then
    if ! fetch_out=$(timeout --kill-after=5s "$FETCH_T" git -C "$MIRROR" fetch --no-tags -q origin "+refs/heads/$RR_DEPS_REF:refs/rr/$RR_DEPS_REF" 2>&1 </dev/null); then
        echo "$fetch_out"
        fallback "cannot fetch --deps-ref $RR_DEPS_REF"
    fi
    deps_from=$(git -C "$MIRROR" rev-parse "refs/rr/$RR_DEPS_REF") || fallback "no tip for --deps-ref $RR_DEPS_REF"
    say "dependency script $RR_DEPS_SCRIPT taken from --deps-ref $RR_DEPS_REF @ $deps_from"
else
    say "dependency script $RR_DEPS_SCRIPT taken from the commit under test @ $RR_SHA"
fi
deps_src=$(git -C "$MIRROR" show "$deps_from:$RR_DEPS_SCRIPT" 2>/dev/null) || fallback "$RR_DEPS_SCRIPT is not in $deps_from"
hash=$(printf '%s\0%s\0%s' "$RR_BASE_IMAGE" "$RR_DEPS_ARGS" "$deps_src" | sha256sum | cut -c1-12)
image="rr-$RR_PROJECT:$hash"
if ! pm image exists "$image"; then
    if [[ "$RR_MODE" != warm ]]; then
        fallback "no image for the current dependency set ($image): run remote-run --warm first"
    fi
    say "building image $image (base $RR_BASE_IMAGE, deps: $RR_DEPS_ARGS)"
    ctx="$RR_DIR/ctx/$hash"
    rm -rf "$ctx"; mkdir -p "$ctx"
    printf '%s\n' "$deps_src" > "$ctx/deps.sh"
    # shellcheck disable=SC2086
    printf 'FROM %s\nCOPY deps.sh /opt/rr/deps.sh\nRUN bash /opt/rr/deps.sh %s\n' "$RR_BASE_IMAGE" "$RR_DEPS_ARGS" > "$ctx/Containerfile"
    pm build -q -t "$image" -f "$ctx/Containerfile" "$ctx" </dev/null || fallback "image build failed"
    rm -rf "$ctx"
    # keep only the current tag of this project's image
    pm image ls "localhost/rr-$RR_PROJECT" --format '{{.Repository}}:{{.Tag}}' 2>/dev/null \
        | grep -v ":$hash\$" | while read -r old; do pm rmi "$old" >/dev/null 2>&1; done
    pm image prune -f >/dev/null 2>&1
fi
say "image: $image"

# ---- second repository (read-only input of the container) ---------------------
extra_dir=""
if [[ -n "${RR_EXTRA_URL:-}" ]]; then
    extra_ref=$(git -C "$MIRROR" show "$RR_SHA:$RR_EXTRA_REF_FILE" 2>/dev/null | sed -nE "$RR_EXTRA_REF_SED" | head -n 1)
    [[ "$extra_ref" =~ ^[0-9a-f]{40}$ ]] || fallback "no pinned commit for $RR_EXTRA_URL found in $RR_EXTRA_REF_FILE at $RR_SHA"
    extra_dir="$CACHE/extra-repo$SFX"
    mkdir -p "$CACHE"
    if [[ "$(git -C "$extra_dir" rev-parse HEAD 2>/dev/null)" == "$extra_ref" ]]; then
        say "extra repository: reusing $extra_dir @ $extra_ref (pin read from $RR_EXTRA_REF_FILE)"
    else
        say "extra repository: cloning $RR_EXTRA_URL @ $extra_ref (pin read from $RR_EXTRA_REF_FILE)"
        rm -rf "$extra_dir"
        {
            git clone -q --filter=blob:none --no-checkout "$RR_EXTRA_URL" "$extra_dir" \
                && git -C "$extra_dir" sparse-checkout init --cone \
                && git -C "$extra_dir" sparse-checkout set "$RR_EXTRA_SPARSE" \
                && git -C "$extra_dir" checkout -q "$extra_ref"
        } </dev/null 2>&1 || { rm -rf "$extra_dir"; fallback "cannot fetch $RR_EXTRA_URL @ $extra_ref"; }
    fi
    [[ -d "$extra_dir/$RR_EXTRA_SPARSE" ]] || { rm -rf "$extra_dir"; fallback "$RR_EXTRA_SPARSE is missing from $RR_EXTRA_URL @ $extra_ref"; }
    say "extra repository ready: $(find "$extra_dir/$RR_EXTRA_SPARSE" -name '*.vhd' | wc -l) .vhd files under $RR_EXTRA_SPARSE"
fi

# ---- per-run checkout -------------------------------------------------------
rm -rf "$run"; mkdir -p "$run" "$ctl" "$stamps"
git clone -q --local --no-checkout "$MIRROR" "$run/src" 2>&1 </dev/null | grep -v "remote HEAD refers" || true
exec 8>&-   # the setup lock is released: the rest is this run's own
git -C "$run/src" checkout -q --detach "$RR_SHA" || fallback "cannot check out $RR_SHA"
[[ -z "$base_main" ]] || git -C "$run/src" update-ref refs/remotes/origin/main "$base_main" 2>/dev/null || true
git -C "$run/src" submodule update --init --recursive --depth 1 -q </dev/null \
    || git -C "$run/src" submodule update --init --recursive -q </dev/null \
    || fallback "submodule fetch failed"
printf '%s' "$RR_INNER" > "$ctl/inner.sh"
{
    printf 'export RR_TARGETS=%q\n' "$RR_TARGETS"
    printf 'export RR_BOOTSTRAP=%q\n' "$RR_BOOTSTRAP"
    printf 'export RR_KEEP_GOING=%q\n' "$RR_KEEP_GOING"
    printf 'export RR_JOBS_DEFAULT=%q RR_JOBS_HEAVY=%q RR_HEAVY_RE=%q\n' "$RR_JOBS_DEFAULT" "$RR_JOBS_HEAVY" "$RR_HEAVY_RE"
} > "$ctl/env.sh"

# ---- run --------------------------------------------------------------------
args=(run --rm --name "$cname" --label "rr-lane=$lane" --memory "$mem_limit" --memory-swap "$mem_limit" --shm-size "$RR_SHM" --security-opt label=disable
      -v "$run/src:/src" -v "$ctl:/rr:ro" -v "$stamps:/rr-stamps" -w /src
      -e "JNEXT_TEST_JOBS=$RR_TEST_JOBS")
[[ -z "$RR_STAMP_ENV" ]] || args+=(-e "$RR_STAMP_ENV=/rr-stamps")
for m in $RR_CACHE_MOUNTS; do
    mkdir -p "$CACHE/$(lane_dir "${m%%:*}")"
    args+=(-v "$CACHE/$(lane_dir "${m%%:*}"):${m#*:}")
done
for e in $RR_CONTAINER_ENV; do args+=(-e "$e"); done
if [[ -n "$extra_dir" ]]; then
    args+=(-v "$extra_dir:$RR_EXTRA_MOUNT:ro")
    [[ -z "$RR_EXTRA_ENV" ]] || args+=(-e "$RR_EXTRA_ENV")
fi
args+=("$image" bash /rr/inner.sh)

# Watchdog: a heartbeat line every RR_HEARTBEAT (default 20) s; when the ssh session is gone the write fails and the
# run is stopped (the container is killed and the checkout removed by the EXIT trap).
main_pid=$$
(
    trap '' PIPE
    exec 9>&-   # the lane lock is the run's, not the watchdog's: a lingering sleep must not hold it
    while kill -0 "$main_pid" 2>/dev/null; do
        printf '\001HB\n' || { kill -TERM "$main_pid" 2>/dev/null; exit 0; }
        sleep "${RR_HEARTBEAT:-20}" >/dev/null 2>&1   # nor the session's stdout
    done
) &
watchdog=$!

say "started: container $cname, targets: $(printf '%s' "$RR_TARGETS" | tr '\n' ' ')"
echo "=== remote-run started"
# In the background with `wait`, so a TERM reaches the trap at once instead of after the container.
timeout --kill-after=30s "${RR_TIMEOUT}s" "${PM_CMD[@]}" "${args[@]}" </dev/null &
run_pid=$!
wait "$run_pid"
rc=$?
while kill -0 "$run_pid" 2>/dev/null; do wait "$run_pid"; rc=$?; done
[[ $rc -ne 124 ]] || say "TIMEOUT after ${RR_TIMEOUT}s"
echo "=== remote-run exit: $rc"
exit "$rc"
