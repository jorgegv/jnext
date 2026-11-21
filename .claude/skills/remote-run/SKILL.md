---
name: remote-run
description: Run heavy builds and tests (the batch gate, full unit-test targets, regression rows, wasm browser test) on a remote host through rootless podman, in the same container as CI, and read the result as a log plus exit status. Use when a heavy run is wanted and JNEXT_REMOTE_RUN_HOST is set; falls back to a local run when the remote cannot be used.
---

# remote-run

Runs `make` targets on a remote host in a `fedora:45` container whose packages come from
`test/ci-deps.sh` (the same script `ci.yml` calls). Design and decisions D1-D21:
`doc/design/REMOTE-RUN-SKILL-DESIGN.md`. The driver, `remote-run.sh`, names no project; the jnext
specifics are in `project.conf`.

## Settings (per session, environment)

| variable | meaning | default |
|----------|---------|---------|
| `JNEXT_REMOTE_RUN_HOST` | SSH alias (`~/.ssh/config` has host, port, user). Unset: remote runs are off, the driver exits 75 | none |
| `JNEXT_REMOTE_RUN_DIR` | the ONE directory on the remote that holds everything remote-run writes: mirror clone, per-run checkouts, temp files, ccache, tool caches, the SD-image cache, podman's image storage and the lane lock. A leading `~/` is expanded on the remote | `~/remote-jnext-work` |

Nothing is written on the remote outside that directory, except podman's own rootless runtime
state under `$XDG_RUNTIME_DIR` (a tmpfs, rebuilt at login). Removing the directory removes the
installation: `podman unshare rm -rf <dir>` (the storage holds files of sub-uids).

## When to use it (D14)

Heavy runs only: the batch gate steps, full `make unit-test` / `unit-test-sdl` / `unit-test-win` /
`unit-test-wasm`, `wasm-browser-test`, regression rows. Single-suite rebuilds and mutation cycles
stay local (the network round trip costs more than they do). The remote lane is in addition to the
two local slots.

## How

Choose the host by load before each heavy run (owner, 2026-11-21): this host free (slots free, load
below `nproc`) → run here, it is faster; busy → the remote, with `--fallback-local`. An agent may push
its own issue branch (never force, never `main`; owner, 2026-11-21, superseding D9's orchestrator-only
push) and then run the driver itself. The remote clones from GitHub, so the
branch must be on `origin`; the batch gate pushes a temporary `gate-v<version>` branch (D8).

The remote is ADDITIONAL capacity, never a queue and never a bottleneck: a heavy run goes to whichever
is free. The normal call therefore always carries `--fallback-local <worktree>` with
`REMOTE_RUN_LOCAL_WRAP` set to the local slot wrapper (`slot.sh`): lane free, the run goes to the remote;
lane busy past `LANE_WAIT` (30 s), alias unset, unreachable or preflight failing, it runs locally at
once through the slot wrapper and the log says `RAN LOCALLY`. Only `--gate` waits long for the lane
(`GATE_LANE_WAIT`, 3 h), because its steps must run in one place.

```bash
export REMOTE_RUN_LOCAL_WRAP=<run dir>/slot.sh
S=.claude/skills/remote-run
$S/remote-run.sh --warm                              # once, and after test/ci-deps.sh changes
$S/remote-run.sh <branch> $(git rev-parse <branch>) unit-test unit-test-sdl
$S/remote-run.sh --fallback-local <worktree> <branch> <sha> unit-test    # THE NORMAL CALL: remote if free, else here
$S/remote-run.sh <branch> <sha> 'sh:make regression-rows ROWS="a b"'     # words with spaces: sh:
```

### The batch gate

Push main's tip to origin as the temporary branch `gate-v<version>` (D8, never force), then:

```bash
$S/remote-run.sh --gate gate-v<version> <sha> [--deps-ref <branch>]
```

and delete `gate-v<version>` on origin right after, green or red. `--gate` runs the project's
`GATE_STEPS` (CLAUDE.md's merge protocol step 2 list, in order, fail-fast) and waits for the lane up to
`GATE_LANE_WAIT`. **Green means the exit status is 0 AND the summary ends with `--- verdict: CLEAN`**:
each step's own `totals` line (`Total: ... Failed: 0 Skipped: 0`, or regression's `Pass: .. Fail: 0
Skip: 0`) is shown, and exactly one result line per step must exist. A step that exits 0 but reports a
failure or a skip, a step that never ran, or a step during which the container's memory limit killed a
process (an `=== remote-run oom:` line) is NOT CLEAN and the driver exits 1. A green `make regression`
stamp is copied back only when the whole run is green (exit 0 and CLEAN), then `make regression-stamp-check`
here sees it; a gate that fails after the regression step returns no stamp.

`--deps-ref <branch>` takes the dependency script (the image's package set) from that branch instead of the
commit under test, for a commit that has no `test/ci-deps.sh` yet; `--warm --ref <branch>` warms that
branch. The log and the summary name the ref that supplied the script.

The container also gets the ZX Next FPGA core (the VHDL oracle of the traceability check): a partial, sparse
clone under `<dir>/cache/extra-repo` at the commit pinned in `ci.yml` (read from the commit under test, never
copied), mounted read-only with `JNEXT_FPGA_SRC` set. A run is refused (75) if it cannot be fetched.

**Do not edit the skill's files while a run is live; commit them instead.** The driver snapshots them at
start, but an edit half-written before the run's remote side starts still breaks that run.

### Steps

Targets run one after another, in order, each as its own `make JOBS=<n> <target>` (`sh:<cmd>` runs a
shell command as written), and the run stops at the first failure unless `--keep-going`. Options:
`--timeout SECS`, `--stream` (also print the log live), `--id`, `--log-dir`, `--conf`.

Local builds still go through the local slot wrapper: `--fallback-local` runs the targets in the given
directory, in front of `REMOTE_RUN_LOCAL_WRAP` (for example the run's `slot.sh`) when that is set.

## What it guarantees

- **The verdict is the exit status plus the log.** `0` green; `1` the run ran and failed, was not CLEAN, or was cut off
  (the log says "connection lost"); `124` it hit the timeout; `2` usage;
  `3` refused: the branch is not on `origin`, or `origin` has another commit than the `<sha>` given
  (the push did not land, or the branch moved); `75` the remote cannot be used: alias unset,
  unreachable, preflight failed (podman missing or not rootless, not enough memory or disk, memory
  cgroup controller not delegated), no image for the current deps (run `--warm`), lane busy past
  `LANE_WAIT` (a plain run) or `GATE_LANE_WAIT` (`--gate`). On `75` run locally; with `--fallback-local` the driver does it and the log says
  `RAN LOCALLY`. A non-green run is never retried silently.
- **The caller gets a summary, the full log stays in a file** (`~/tmp/remote-run/<id>.log`, kept 7
  days): per-target result lines (rc, seconds, peak container memory), the FAIL / SKIP lines (first 30),
  each step's own `totals` line and the verdict (`CLEAN`, or why not). Grep the file for detail. A SKIP is a failure, as everywhere.
- **Same targets, same container as CI.** The image is `docker.io/library/fedora:45` plus
  `test/ci-deps.sh test mingw mingw-qt`, tagged by a hash of base, set names and the script at the run's
  commit; it is rebuilt only by `--warm`. `LANG=C LC_ALL=C` in the container.
- **A green `make regression` stamp comes back**: the container writes it to a per-run directory, the
  driver copies it to `~/.cache/jnext/regression-stamps`, so `make regression-stamp-check` here sees
  it for that content key. A loaded-host timing FAIL is re-run solo on the remote
  (`make regression-confirm`), the same rule as locally; that is a separate target in a second call.
- **One lane**: an `flock`; a plain run waits for it only `LANE_WAIT` (30 s) and then falls back, `--gate`
  waits `GATE_LANE_WAIT`. Caps: `JOBS` 11 (6 for LTO/PGO link targets), `JNEXT_TEST_JOBS` 12, a hard
  container memory limit (the configured 11g, or 80% of the host's memory when smaller; swap off) and 1 GB
  `/dev/shm`. The preflight reports MemAvailable and the ZFS ARC size and counts reclaimable ARC as
  available. Every result line logs `peak=` (the cgroup's cumulative high-water mark, page cache included)
  and `anon=` (the step's own peak of anonymous memory): tune the caps on `anon=`, not `peak=`. A process
  killed by the limit is logged as `oom` and makes the run NOT CLEAN.
- **Orphans are killed**: while a run is live the remote sends a heartbeat down the ssh session; if the
  session drops (or the driver gets TERM/INT) the remote kills the container, frees the lane and removes
  the run's checkout. Leftovers older than an hour are swept at the next run's start.
- **ssh never opens forwards**: the driver passes `-o BatchMode=yes -o ClearAllForwardings=yes`, so an
  alias that carries `LocalForward` lines neither binds ports nor prints their errors into the log.
- **Podman storage is under the work directory** (`--root`, `--runroot`). The driver tries the default
  (overlay), then fuse-overlayfs, then vfs, and records the first that starts, the default too, in
  `containers/driver`. It probes only while holding the lane, because a failed probe wipes the store.
  Every podman call carries `--root`/`--runroot`: a bare call would create a default store in the
  remote HOME.

## The SD-image rule

The NextZXOS image is copyrighted: it NEVER leaves the remote work directory and is never committed,
posted or copied anywhere else. It lives in `<dir>/cache/jnext-home/sdcard` (the container's `~/.jnext` is that one mount, so the master and every run's clone and scratch share a filesystem and the clone can be a reflink), provisioned there by jnext's own
download (`make sdcard-image`, part of `--warm`). Logs may name it, never contain it. The driver copies
back only logs and stamp files.

## Remote prerequisites

SSH, `bash`, `git`, rootless `podman`, `flock`, `timeout`, `tar`, `sha256sum`, and git access to GitHub
over https. Nothing else is installed there.
