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
RUNS="$RR_DIR/runs"
STAMPS="$RR_DIR/stamps"
CTL="$RR_DIR/ctl"
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
    rm -rf "${RUNS:?}/$RR_ID" "${STAMPS:?}/$RR_ID" "${CTL:?}/$RR_ID"
    exit 0
fi

mkdir -p "$RR_DIR" "$TMPDIR" "$RUNS" "$STAMPS" "$CTL" "$RR_DIR/containers/storage" "$RR_DIR/containers/run" \
    || fallback "cannot create the work directory $RR_DIR"

# ---- preflight --------------------------------------------------------------
for c in git podman flock timeout tar sha256sum; do
    command -v "$c" >/dev/null 2>&1 || fallback "required command not found on the remote: $c"
done

# Memory: MemAvailable leaves out the ZFS ARC, which the kernel gives back under pressure, so
# the gate counts the ARC above its minimum as available; both are reported.
mem_avail_mb=$(awk '/^MemAvailable:/ {print int($2/1024)}' /proc/meminfo 2>/dev/null || echo 0)
mem_total_mb=$(awk '/^MemTotal:/ {print int($2/1024)}' /proc/meminfo 2>/dev/null || echo 0)
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

# The container gets a hard memory limit: the configured one, or 80% of the host's memory when
# that is smaller. That needs the memory controller delegated to the user (cgroup v2).
mem_limit=$RR_MEMORY
case "$RR_MEMORY" in
    *g) cfg_mb=$(( ${RR_MEMORY%g} * 1024 )) ;;
    *m) cfg_mb=${RR_MEMORY%m} ;;
    *)  fallback "RR_MEMORY must end in g or m: '$RR_MEMORY'" ;;
esac
cap_mb=$((mem_total_mb * 8 / 10))
if [[ "$mem_total_mb" -gt 0 && "$cfg_mb" -gt "$cap_mb" ]]; then mem_limit="${cap_mb}m"; fi
if [[ -z "${RR_SKIP_CGROUP_CHECK:-}" ]]; then
    uid=$(id -u)
    ctrl=$(cat "/sys/fs/cgroup/user.slice/user-$uid.slice/user@$uid.service/cgroup.controllers" 2>/dev/null || true)
    [[ " $ctrl " == *" memory "* ]] || fallback "the memory cgroup controller is not delegated to this user (controllers: '${ctrl:-none}'); a limit cannot be set"
fi
say "container memory limit: $mem_limit (swap off)"


# ---- lane: one run at a time ------------------------------------------------
exec 9>"$RR_DIR/lane.lock"
if ! flock -n 9; then
    say "the lane is busy: queued (waiting up to ${RR_LANE_WAIT}s)"
    flock -w "$RR_LANE_WAIT" 9 || fallback "the lane stayed busy for ${RR_LANE_WAIT}s"
fi

# Storage driver: try the default (overlay) in the work directory, then fuse-overlayfs, then vfs;
# the first that can start is recorded in containers/driver (the default too, by the name podman
# reports) and used by every later call. Runs only while holding the lane: a failed probe wipes
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
pick_driver
rootless=$(pm info --format '{{.Host.Security.Rootless}}' 2>/dev/null) || fallback "podman info failed"
[[ "$rootless" == true ]] || fallback "podman is not rootless (got '$rootless')"

# Cache directories that moved: renamed once, while holding the lane (same filesystem: instant).
for m in ${RR_CACHE_MIGRATE:-}; do
    if [[ -d "$CACHE/${m%%:*}" && ! -e "$CACHE/${m#*:}" ]]; then
        mkdir -p "$(dirname "$CACHE/${m#*:}")" && mv "$CACHE/${m%%:*}" "$CACHE/${m#*:}" \
            && say "cache migrated: ${m%%:*} -> ${m#*:}"
    fi
done

for m in ${RR_CACHE_SWEEP:-}; do
    find "$CACHE/$m" -mindepth 1 -maxdepth 1 -mmin +1440 -exec rm -rf {} + 2>/dev/null
done

cname="rr-$RR_PROJECT-$RR_ID"
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

# The lane is ours, so anything left under RUNS/CTL/STAMPS or still running is an orphan.
for c in $(pm ps -q --filter "name=^rr-$RR_PROJECT-" 2>/dev/null); do pm kill "$c" >/dev/null 2>&1; done
find "$RUNS" "$CTL" -mindepth 1 -maxdepth 1 -mmin +60 -exec rm -rf {} + 2>/dev/null
# stamps wait for the driver to fetch them in a later session: only a day-old one is an orphan
find "$STAMPS" -mindepth 1 -maxdepth 1 -mmin +1440 -exec rm -rf {} + 2>/dev/null

# ---- mirror -----------------------------------------------------------------
[[ -d "$MIRROR/objects" ]] || git init -q --bare "$MIRROR" || fallback "cannot create the mirror"
git -C "$MIRROR" remote get-url origin >/dev/null 2>&1 || git -C "$MIRROR" remote add origin "$RR_REPO_URL"
git -C "$MIRROR" remote set-url origin "$RR_REPO_URL"
say "fetching $RR_BRANCH from $RR_REPO_URL"
if ! fetch_out=$(git -C "$MIRROR" fetch --no-tags -q origin "+refs/heads/$RR_BRANCH:refs/rr/$RR_BRANCH" 2>&1 </dev/null); then
    echo "$fetch_out"
    if [[ "$fetch_out" == *"couldn't find remote ref"* ]]; then
        refuse "branch $RR_BRANCH is not on the remote repository: push it first"
    fi
    fallback "git fetch failed"
fi
tip=$(git -C "$MIRROR" rev-parse "refs/rr/$RR_BRANCH") || fallback "no tip for $RR_BRANCH after fetch"
if [[ -z "$RR_SHA" ]]; then
    RR_SHA=$tip
elif [[ "$tip" != "$RR_SHA" ]]; then
    refuse "$RR_BRANCH is at $tip on the remote repository, not at the expected $RR_SHA: the push did not land or the branch moved"
fi
say "source: $RR_BRANCH @ $RR_SHA"

# ---- image ------------------------------------------------------------------
# The dependency script comes from the commit under test, or from RR_DEPS_REF when one is given.
deps_from="$RR_SHA"
if [[ -n "${RR_DEPS_REF:-}" ]]; then
    if ! fetch_out=$(git -C "$MIRROR" fetch --no-tags -q origin "+refs/heads/$RR_DEPS_REF:refs/rr/$RR_DEPS_REF" 2>&1 </dev/null); then
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
    extra_dir="$CACHE/extra-repo"
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
git -C "$run/src" checkout -q --detach "$RR_SHA" || fallback "cannot check out $RR_SHA"
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
args=(run --rm --name "$cname" --memory "$mem_limit" --memory-swap "$mem_limit" --shm-size "$RR_SHM" --security-opt label=disable
      -v "$run/src:/src" -v "$ctl:/rr:ro" -v "$stamps:/rr-stamps" -w /src
      -e "JNEXT_TEST_JOBS=$RR_TEST_JOBS")
[[ -z "$RR_STAMP_ENV" ]] || args+=(-e "$RR_STAMP_ENV=/rr-stamps")
for m in $RR_CACHE_MOUNTS; do
    mkdir -p "$CACHE/${m%%:*}"
    args+=(-v "$CACHE/${m%%:*}:${m#*:}")
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
