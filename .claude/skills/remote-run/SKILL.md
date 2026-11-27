---
name: remote-run
description: Run heavy builds and tests (the batch gate, full unit-test targets, regression rows, wasm browser test) on a remote host through rootless podman, in the same container as CI, and read the result as a log plus exit status. Use when a heavy run is wanted and JNEXT_REMOTE_RUN_HOST is set; falls back to a local run when the remote cannot be used.
---

# remote-run

Runs `make` targets on a remote host in a `fedora:45` container whose packages come from
`test/ci-deps.sh` (the same script `ci.yml` calls). Design and decisions D1-D23:
`doc/design/REMOTE-RUN-SKILL-DESIGN.md`. The driver, `remote-run.sh`, names no project; the jnext
specifics are in `project.conf`.

## Settings (per session, environment)

| variable | meaning | default |
|----------|---------|---------|
| `JNEXT_REMOTE_RUN_HOST` | SSH alias (`~/.ssh/config` has host, port, user). Unset: remote runs are off, the driver exits 75 | none |
| `JNEXT_REMOTE_RUN_HOSTS` | Several hosts: SSH aliases separated by spaces, in preference order. `slot.sh` and `pick-host.sh` read it (see "Several hosts"); the driver itself is one host per call and reads only `JNEXT_REMOTE_RUN_HOST`. Unset: the single `JNEXT_REMOTE_RUN_HOST` is a one-host list | none |
| `JNEXT_REMOTE_RUN_DIR` | the ONE directory on the remote that holds everything remote-run writes: mirror clone, per-run checkouts, temp files, ccache, tool caches, the SD-image cache, podman's image storage and the lane locks. A leading `~/` is expanded on the remote | `~/remote-jnext-work` |

Nothing is written on the remote outside that directory, except podman's own rootless runtime
state under `$XDG_RUNTIME_DIR` (a tmpfs, rebuilt at login). Removing the directory removes the
installation: `podman unshare rm -rf <dir>` (the storage holds files of sub-uids).

## When to use it (D14)

Heavy runs only: the batch gate steps, full `make unit-test` / `unit-test-sdl` / `unit-test-win` /
`unit-test-wasm`, `wasm-browser-test`, regression rows. Single-suite rebuilds stay local (the network round
trip costs more than they do). Mutation runs (a reviewer's or implementer's mutant: an uncommitted edit of a pushed
commit) go remote too, as a patch on the pushed commit through `slot.sh --patch` (D23, "Patch runs" below). The two
remote lanes are in addition to the local slots.

## How

In an autonomous run `slot.sh` (`.claude/skills/autonomous-run/slot.sh`) makes this choice itself and agents just call it (see that skill); what follows is the manual form.

Remote first (owner, 2026-11-23: fill the remote workers before building locally, unless needed): before each
heavy run pick a free lane (`pick-host.sh`) and run there, with `--fallback-local`; run here only when every lane
of every host is busy, or when the run needs this host: uncommitted worktree state that is not a patch run (below), a
measurement, or material that must not leave it (a reporter's game, the user's own card). An agent may push
its own issue branch (never force, never `main`; owner, 2026-11-21, superseding D9's orchestrator-only
push) and then run the driver itself. The remote clones from GitHub, so the
branch must be on `origin`; the batch gate pushes a temporary `gate-v<version>` branch (D8).

The remote is ADDITIONAL capacity, never a queue and never a bottleneck: a heavy run goes to whichever
is free. **Call the driver from the main checkout**
(`/home/jorgegv/src/spectrum/jnext/.claude/skills/remote-run/remote-run.sh`), never from an older worktree:
the remote side comes from the driver's own copy, and an old one kills every `rr-jnext-*` container
in its orphan sweep, a lane 1 run's included (lane 2's name does not match). **`--id` must be unique among live runs** (the log
file and the run's directories are named by it). The normal call therefore always carries `--fallback-local <worktree>` with
`REMOTE_RUN_LOCAL_WRAP` set to the local slot wrapper (`slot.sh`): a lane free, the run goes to the remote;
every lane busy past `LANE_WAIT` (30 s), alias unset, unreachable or preflight failing, it runs locally at
once through the slot wrapper and the log says `RAN LOCALLY`. Only `--gate` waits long for a lane
(`GATE_LANE_WAIT`, 3 h), because its steps must run in one place.

```bash
export REMOTE_RUN_LOCAL_WRAP=<run dir>/slot.sh
S=/home/jorgegv/src/spectrum/jnext/.claude/skills/remote-run   # the MAIN checkout's copy, never a worktree's
$S/remote-run.sh --warm                              # once, and after test/ci-deps.sh changes
$S/remote-run.sh <branch> $(git rev-parse <branch>) unit-test unit-test-sdl
$S/remote-run.sh --fallback-local <worktree> <branch> <sha> unit-test    # THE NORMAL CALL: remote if free, else here
$S/remote-run.sh <branch> <sha> 'sh:make regression-rows ROWS="a b"'     # words with spaces: sh:
```

### Patch runs (mutants, D23)

A mutant is an uncommitted edit in a worktree whose HEAD is a commit already on `origin`. The remote runs only
committed state, so the driver carries the edit as a PATCH on that commit:

```bash
$S/remote-run.sh --make-patch <worktree> > /path/to/m1.patch     # exit 2, nothing on stdout, if it cannot be carried
$S/remote-run.sh --patch /path/to/m1.patch --fallback-local <worktree> <branch> <sha> unit-test
```

`--make-patch` prints the canonical patch of the work tree against its HEAD: a first line
`# remote-run patch: base <sha> tree <tree>`, then a `git diff --binary --full-index --no-renames` of a temporary
index (tracked and untracked files, ignore rules applied, the real index never touched; every format choice is a flag,
so the caller's git configuration cannot change it). It refuses (exit 2, reason on stderr) a clean tree, a changed
submodule (a moved one, one with changes inside, an embedded repository): a patch cannot carry them. Ignored files
(build output) are not carried, by design.

`--patch` is for plain runs (not `--warm`, `--gate`, `--probe`; an empty value is refused), and `<sha>` must be the patch's base. The remote applies the
patch to a fresh checkout of `<sha>` as UNCOMMITTED changes, but only after proving it: applied to a fresh index of
`<sha>` the patch must write exactly the tree id the header names, so another base, an edited patch or an offset
application is refused; and it may not change `test/ci-deps.sh` or the pinned-core file (the remote reads those from
the commit). The log shows `=== remote-run patch applied: <name> sha256=<h> tree=<t> files=<n>` before the run starts and
the summary names the patch (`patch=<name>`, a `--- patch` block). **Any doubt is exit 2, `PATCH REFUSED`: no verdict,
no local fallback, nothing run** (a refusal read as a kill, or a pass read as a survivor, would be a wrong answer): a
bad header, name or base, a patch that does not apply or whose tree differs, a remote side that does not log the applied
line (the run's own status is discarded), or a `--fallback-local` directory whose tree is not the patch's (clean, or
edited since). No stamp is copied back from a patch run. The payload travels as one argument (the kernel limits one
argument to 128 KiB; the script itself takes about 30 KiB and the base64 patch is a third larger than the patch), so a
patch of the order of 70 KB or more makes the transport fail, which is the usual "remote did not start" fallback, taken
only after the tree check.

`slot.sh --patch` does all of this for an agent (`autonomous-run` skill): `<run>/slot.sh --patch make -C <worktree>
<heavy target> JNEXT_SUITE_VMEM_KB=3145728`. Read its result as exit 0 = the mutant SURVIVED the target; non-zero with a
FAIL row, `Failed` > 0, a crash or no `Total:` line = KILLED; any `PATCH REFUSED` = no verdict, fix and re-run. A patch run whose session is lost after the start, whose harness refuses to run (its banner `TEST HARNESS REFUSES TO RUN` alone on a line, exit 2 of make; the words quoted in a FAIL listing do not count) or that hits the run-level timeout is also exit 2 `PATCH REFUSED ... no verdict`, never exit 1: for a mutant a red exit is the "good" answer, so a status that says nothing about the mutant must not look like one.

### The batch gate

The batch gate is a CI run (CLAUDE.md merge protocol step 2); `--gate` here is its fallback.

Push main's tip to origin as the temporary branch `gate-v<version>` (D8, never force), then:

```bash
$S/remote-run.sh --gate gate-v<version> <sha> [--deps-ref <branch>]
```

and delete `gate-v<version>` on origin right after, green or red. `--gate` runs the project's
`GATE_STEPS` (CLAUDE.md's merge protocol step 2 list, in order, fail-fast) and waits for a lane up to
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
directory, in front of `REMOTE_RUN_LOCAL_WRAP` (for example the run's `slot.sh`) when that is set. The wrapper's environment carries `REMOTE_RUN_FALLBACK=1`, which `slot.sh` reads as "never overflow again".

## Several hosts (GH #547, owner 2026-11-22)

The driver is ONE host per call. A caller with several hosts picks one and passes its alias in
`JNEXT_REMOTE_RUN_HOST` for that call. Two helpers do the choosing, both over `remote-run.sh --probe`
(one ssh round trip: exit 0 = usable and a lane is free right now, 75 = every lane busy, host unreachable or
preflight refused; nothing is started; a busy host therefore costs a moment, never `LANE_WAIT`):

- `slot.sh` (autonomous runs): tries the aliases of `JNEXT_REMOTE_RUN_HOSTS` in order, probe first; the run goes to
  the first host with a free lane. A driver status 75 after a free probe (a lane lost in between, no image: nothing
  ran) moves to the next host; a run that STARTED and failed is never retried elsewhere. Every host busy: the run
  waits for a local slot and, while it waits, the hosts are probed again quietly every `JNEXT_SLOT_REPROBE`-th poll
  (default 3, about a minute), so a lane that frees up takes it. The last host of the list carries `--fallback-local`,
  as with one host.
- `pick-host.sh [--or-first]` (the batch gate and manual runs): prints the first host with a free lane; 75 when none;
  `--or-first` prints the first host of the list instead, for a caller that will wait for a lane there:
  `H=$(pick-host.sh --or-first) && JNEXT_REMOTE_RUN_HOST=$H remote-run.sh --gate <branch> <sha>`.

Each host is a complete installation of its own: its own work directory, mirror, image, caches, SD master, lanes,
locks and podman store. One `JNEXT_REMOTE_RUN_DIR` value serves every host (a leading `~/` is expanded by each host in
its own home); a host that needs another path needs another call with its own value. The orphan sweep and the
`rr-lane` label work on the host's own podman store, so a run never touches another host's containers. The
per-lane memory cap is computed from each host's own `MemTotal` (80% divided by `LANES`, at most `CONTAINER_MEMORY`):
a 24 GB host gets about 9.3 GB per lane against 10 GB on the 32 GB one, so `--warm` and the first real runs there
must be read for `anon=` and `oom` lines. The SD image never leaves the host's own work directory. `--warm` is run
once per host.

## What it guarantees

- **The verdict is the exit status plus the log.** `0` green; `1` the run ran and failed, was not CLEAN, or was cut off
  (the log says "connection lost"); `124` it hit the timeout; `2` usage, or a patch run (`--patch`) whose patch was refused
  or whose application the log does not prove (`PATCH REFUSED`: no verdict, never a fallback);
  `3` refused: the branch is not on `origin`, or `origin` has another commit than the `<sha>` given
  (the push did not land, or the branch moved); `75` the remote cannot be used: alias unset,
  unreachable, preflight failed (podman missing or not rootless, not enough memory or disk, memory
  cgroup controller not delegated), no image for the current deps (run `--warm`), every lane busy past
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
- **Two lanes** (`LANES=2`, D22): one `flock` each (`lane.lock`, `lane-L2.lock`); a run takes the first free
  one and logs `lane n of 2`. A plain run waits for a lane only `LANE_WAIT` (30 s) and then falls back,
  `--gate` waits `GATE_LANE_WAIT` and runs beside the other lane. Lane 1 keeps the one-lane names; lane 2
  has its own `runs-L2/`, `ctl-L2/`, container name `rr-L2-jnext-<id>` (not matching the `rr-jnext-` prefix an older remote side sweeps), label `rr-lane=2`, SD home
  `cache/jnext-home-L2` (provisioned by its first run through jnext's own download: the SD image never
  leaves the work directory) and FPGA clone `cache/extra-repo-L2`. The ccache, the tool caches (which
  hold the regression host lock, so two full regressions serialize), the mirror, the image and the
  podman store are shared; a setup lock serializes the mirror fetch, image build and checkout clone. Caps, per lane:
  `JOBS` 11 (6 for LTO/PGO link targets), `JNEXT_TEST_JOBS` 12 (halved, rounded up, for a lane that starts while the other is busy), a hard container memory
  limit (the configured 10g, or 80% of the host's memory divided by `LANES` when smaller; swap off) and 1 GB
  `/dev/shm`. The preflight reports MemAvailable and the ZFS ARC size and counts reclaimable ARC as
  available. Every result line logs `peak=` (the cgroup's cumulative high-water mark, page cache included)
  and `anon=` (the step's own peak of anonymous memory): tune the caps on `anon=`, not `peak=`. A process
  killed by the limit is logged as `oom` and makes the run NOT CLEAN.
- **Orphans are killed**: while a run is live the remote sends a heartbeat down the ssh session; if the
  session drops (or the driver gets TERM/INT) the remote kills the container, frees the lane and removes
  the run's checkout. Leftovers older than an hour in the same lane are swept at the next run's start;
  another lane's live run is never touched.
- **ssh never opens forwards**: the driver passes `-o BatchMode=yes -o ClearAllForwardings=yes`, so an
  alias that carries `LocalForward` lines neither binds ports nor prints their errors into the log.
- **Podman storage is under the work directory** (`--root`, `--runroot`). The driver tries the default
  (overlay), then fuse-overlayfs, then vfs, and records the first that starts, the default too, in
  `containers/driver`. It probes only while holding every lane, because a failed probe wipes the store (a run that finds no
  recorded driver while another lane is live exits 75).
  Every podman call carries `--root`/`--runroot`: a bare call would create a default store in the
  remote HOME.

## The SD-image rule

The NextZXOS image is copyrighted: it NEVER leaves the remote work directory and is never committed,
posted or copied anywhere else. It lives in `<dir>/cache/jnext-home/sdcard` (lane 2: `jnext-home-L2`; the container's `~/.jnext` is that one mount, so the master and every run's clone and scratch share a filesystem and the clone can be a reflink), provisioned there by jnext's own
download (`make sdcard-image`, part of `--warm`). Logs may name it, never contain it. The driver copies
back only logs and stamp files.

## Remote prerequisites

SSH, `bash`, `git`, rootless `podman`, `flock`, `timeout`, `tar`, `sha256sum`, `base64`, and git access to GitHub
over https. Nothing else is installed there.
