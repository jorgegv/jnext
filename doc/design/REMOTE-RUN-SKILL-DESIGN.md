# remote-run skill: design (draft for review)

Status: DRAFT, iterating with the owner (2026-11-20). Not implemented. Not committed.

## Goal

Run CPU-heavy work (builds, test suites, the batch gate) on a remote host instead of this
development host, with no software installed on the remote beyond what it already has (SSH,
rootless podman, git access to GitHub).

The parameters are an **SSH alias**, given per session in `JNEXT_REMOTE_RUN_HOST`
(`~/.ssh/config` holds host, port, user), and the remote **work directory**, `JNEXT_REMOTE_RUN_DIR` (D21). Unset means remote-run is off and everything runs
locally as today.

## Decisions

| # | Decision |
|---|----------|
| D1 | Dedicated to jnext for now; every jnext specific lives in one config file, so a later project needs only its own config |
| D2 | Scope: branch work **and the batch gate** |
| D3 | Code reaches the remote through **GitHub**: the branch is pushed to `origin`, the remote clone fetches it |
| D4 | The container's package list comes from **one shared script**, `test/ci-deps.sh <leg>`, called by both `ci.yml` and the remote image build |
| D5 | jnext's SD-image cache lives in a **podman volume** on the remote, owned by the dedicated user; it never leaves that host |
| D6 | The remote runs the **same make targets** as CI and as a local run, in the same `fedora:45` container |
| D7 | Remote host: **12 cores, 16 GB RAM**; rootless podman, no `--privileged` |
| D8 | The gate's commit (main's tip, unpushed before a green gate) goes to a **temporary branch `gate-v<version>`**, deleted right after its gate, green or red |
| D9 | **Only the orchestrator pushes.** Agents ask; the main session pushes their `gh<N>-<slug>` branch (never force) and starts the run. Agents still never push |
| D10 | The **remote run's result is the verdict**: exit status plus the fetched log (fail=0, skip=0). The regression stamp file is copied back, so `make regression-stamp-check` here sees it for that content key |
| D11 | Remote unreachable or preflight fails: **fall back to local `slot.sh`**; the log says it ran locally |
| D12 | A real-time-paced row that FAILs remotely is **re-run solo on the remote** (`make regression-confirm` there, same tree, quiet-host wait), exactly the local rule |
| D13 | **One lane** on the remote; the skill caps make's `-j` and `JNEXT_TEST_JOBS` so a link cannot exhaust 16 GB; the first gate run measures peak memory to tune the caps |
| D14 | **Heavy runs only** go remote: the batch gate, full `make unit-test` / `-sdl` / `-win` / `-wasm`, `wasm-browser-test`, regression rows. Single-suite rebuilds and mutation cycles stay local |
| D15 | The remote lane is **in addition** to the 2 local slots (nice 19): up to 3 heavy runs at once |
| D16 | Merged branches are deleted locally and on `origin` **at issue close**, with the existing cleanup step |
| D17 | **Caps start conservative, then tune:** `-j8` compiles, `-j4` for LTO/PGO links, `JNEXT_TEST_JOBS=8`; the first gate logs peak memory (cgroup / `podman stats`) and the caps in `project.conf` are raised if there is headroom |
| D18 | **Explicit warm-up:** `remote-run --warm` builds the image, provisions every tool cache and the SD image, and builds main once to fill ccache. Run once, and after a deps change; a normal run refuses with "run --warm" when the image for the current deps hash is missing |
| D19 | **Logs:** the full log stays in `~/tmp/remote-run/<id>.log`, kept 7 days then deleted; the caller gets a summary (per-target result lines, FAIL/SKIP names, totals) and greps the file for detail |
| D20 | **Orphans:** the ssh session carries the run; if it drops, the remote side kills the container, frees the lane and removes the run's worktree |
| D21 | **One remote work directory**, set per session in `JNEXT_REMOTE_RUN_DIR`: the mirror, per-run worktrees, temp files, logs, caches (bind-mounted directories, not named volumes), the SD-image cache, and podman's image storage if rootless podman allows it. `--warm` and cleanup touch nothing outside it |

## Architecture

```
dev host                                   remote host ($JNEXT_REMOTE_RUN_HOST)
--------                                   -------------------------------------
git push origin <branch>      ──GitHub──►  ~/remote-run/jnext/mirror    (bare clone; git fetch)
                                           ~/remote-run/jnext/runs/<id>  (git worktree at <sha>)
remote-run <branch> <sha> <targets>
   └─ ssh $JNEXT_REMOTE_RUN_HOST ─────────► flock lane (1)
        stdout/stderr streamed back          podman run --rm jnext-f45:<deps-hash>
        into ~/tmp/remote-run/<id>.log         -v runs/<id>:/src
   exit status = remote make's                 -v jnext-ccache:/root/.cache/ccache
   stamp copied back on green regression       -v jnext-tools:/root/.cache/jnext
                                               -v jnext-sdcard:/root/.jnext/sdcard
                                               env LANG=C JNEXT_TEST_JOBS=<cap> make -j<cap> <targets>
```

## Components

1. **`.claude/skills/remote-run/SKILL.md`**: when to use it (D14), how to call it, what it guarantees,
   the rules: no SD image leaves the remote; logs come back; the exit status is the verdict; local
   fallback (D11).
2. **`.claude/skills/remote-run/remote-run.sh`**: the generic driver; it names no project.
   - **Preflight:** `ssh $JNEXT_REMOTE_RUN_HOST true`, `podman --version`, rootless check, `nproc`,
     free memory, free disk, load. On any failure it prints why and returns the "fall back" code.
   - **Mirror:** clone once from GitHub, then `git fetch origin <branch>`. It refuses if the fetched
     ref is not at the expected `<sha>`: the push did not land, or the branch moved.
   - **Worktree:** one per run at `<sha>`, submodules from GitHub, then the project's bootstrap command.
   - **Image:** `podman build` from `fedora:45` plus the deps script, tagged with a hash of that script.
     It is rebuilt only when the hash changes.
   - **Run:** `podman run --rm` with the project's volumes, under `timeout --kill-after`, with the
     `-j` and job caps (D13).
   - **Log:** streamed to `~/tmp/remote-run/<id>.log`, with a summary line at the end.
   - **Stamp:** after a green `make regression`, the stamp file is copied back (D10).
   - **Lane:** `flock` on the remote, so a second caller queues there (D13).
   - **Cleanup:** remove the run's worktree; keep the mirror, the image and the volumes.
3. **`.claude/skills/remote-run/project.conf`**, the jnext specifics:
   - the repo URL, the deps script and its leg names, the bootstrap command (`make worktree-bootstrap`);
   - the cache volumes and their mount points;
   - the default timeout and the `-j` / `JNEXT_TEST_JOBS` caps;
   - the list of heavy targets (D14).
4. **`test/ci-deps.sh <leg>`** (a repo change, reviewed as usual): the `dnf install` lists now inline in
   `ci.yml` move here. `ci.yml` calls it per leg, and the image build calls it too.

## Flow: an agent's branch

1. The agent commits on `gh<N>-<slug>` and asks the orchestrator for a remote run.
2. The orchestrator runs `git push origin gh<N>-<slug>` (never force), then
   `remote-run gh<N>-<slug> <sha> unit-test`.
3. The result is an exit status and a log path. The agent reads the log as it would a local one.
4. At issue close the branch is deleted locally and on `origin` (D16).

## Flow: the batch gate

1. Main's tip is pushed as `gate-v<version>` (D8).
2. `remote-run gate-v<version> <sha> <gate steps>`. The gate's steps run in one call or a few, one per
   step for readable logs.
3. Green: the stamp is copied back, the gate branch is deleted, then main and its tags are pushed under
   the existing standing authorization.
4. Red: the gate branch is deleted, and the existing "red batch gate" procedure applies.

## CLAUDE.md changes this needs

The owner writes these; Claude never edits authorization text.

1. **Issue-branch pushes for remote runs.** The standing authorization already covers pushing
   `gh<N>-<slug>` (never force). Confirm it covers "to run it on the remote-run host", or say so.
2. **Temporary gate branches.** Allow pushing `gate-v<version>` (never force) and deleting it after its
   gate.
3. **Deleting merged branches on `origin`** at issue close (`git push origin --delete gh<N>-<slug>`).
   This is not covered today.
4. **The batch gate may run on the remote-run host** (merge protocol step 2), and a remote green
   `make regression` may supply the stamp (D10).
5. **SD-image rule:** add "a cache volume on the remote-run host" to where images may live (D5).

## Still open

Nothing. Next step: the owner writes the five CLAUDE.md changes; then `test/ci-deps.sh` (a normal reviewed change), then the skill itself.
