# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Purpose

This repository contains the code for a ZX Spectrum Next emulator based on the official VHDL sources for the ZX Next FPGA core.

## Reference Files

- Emulator design plan: `doc/design/EMULATOR-DESIGN-PLAN.md` — SUPERSEDED (2026-10-06): historical record only, not loaded; every pending item is tracked in GitHub issues.
- FPGA code analysis: @doc/analysis/FPGA-REPO-ANALYSIS.md
- FPGA VHDL source (authoritative hardware spec): `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`
- Design plans in directory `doc/design`

### External references

**All external URLs live in [REFERENCES.md](doc/REFERENCES.md)** — Next wiki pages
(boot sequence, NextREG, NEX file format), TBBlue firmware source, and the
emulators used as oracles. Add new external links there, not here or inline in
code comments.

## General guidance

- Use an Agent Team when working on different tasks
- Try to parallelize work on different agents for independent tasks (e.g. emulator, peripherals, GUI, tests, documentation, etc.)
- Skills needed:
  - C/C++ expert developer
  - VHDL expert
  - GUI developer expert in SDL/QT6
- When a new feature or bugfix is developed, ALWAYS schedule an additional agent for code review, with the same expertise as the original one. The code review should NEVER be done by the same agent that created the code in the first place. Make the reviewer agent be very critic with the code created, ensuring that code passes tests and that no regressions are introduced. Also review code style and conformance to our best practices.

## Constraints for development

- Do not include Co-Authored-by headers in commit messages
- Keep commit messages terse but insightful
- When reading daily prompt files (in directory `.prompts`, they contains tasks for the daily work), always keep a Task Completion Status section in each of them. Update this section whenever a task is finished.
- When launching Agent Teams, the Manager agent should NOT write or touch any code
- When launching Agent Teams, each independent function should be worked on in a different branch, to avoid code trashing between agents. When code is ready on each branch, they should be merged to main. If merge problems occur, the agent responsible for fixing them is the one that tried to merge last, and it should try to fix them on their own branch.
- Agents should NOT write to the main branch, ever. Only on their own branches and worktrees!
- **Git worktrees live OUTSIDE the repository directory.** Canonical location: `/home/jorgegv/tmp/worktrees/<name>` (2026-08-03; the previous sibling `/home/jorgegv/src/spectrum/jnext-worktrees/` is retired, as is the older in-repo `.claude/worktrees/` from 2026-07-19). Never create a worktree checkout inside the repo — even gitignored: anything walking the repository file list also walks the worktrees, which is unneeded work and loads the machine. The traceability generator finds the FPGA checkout from any worktree location on its own — it resolves the worktree's main checkout from its `.git` file and walks up from there — so no `JNEXT_FPGA_SRC` export is needed in worktrees (the env var remains for machines with no sibling checkout, e.g. CI).
- **NEVER push to origin without explicit user authorization.** This applies to the manager AND every spawned agent. Local commits, rebases, and merges on owned branches/worktrees are fine; `git push`, `git push -u`, `git push --force`, `gh pr create`, and any equivalent are all forbidden unless the user explicitly says "push" or "open a PR". **Standing authorization (owner, 2026-09-29 and 2026-10-06) — exactly this, nothing more:** after a green, independently APPROVED merge to `main` and its `make bump-patch`, push `main` and its new tags (explicit refspecs, ≤3 tags per push, only when no CI run on `main` is in progress); push epic branches (`epic<E>-<slug>`, never force); comment on, file and edit issues and close them — during an autonomous run; and, after any approved merge is pushed, close the finished issue with its close note; once an issue is closed and its branch merged into its target, remove its clean worktrees and delete its merged local branch (`worktree-launch` §Cleanup). Other branches, force pushes, PRs, unmerged or dirty work, and public releases stay per-message.
- `doc/design/EMULATOR-DESIGN-PLAN.md` and the `doc/design/TASK*-PLAN.md` family are FROZEN historical artifacts (owner, 2026-08-12): never edit them or offer to. Task status lives in GitHub issues; a superseded plan line is recorded in the issue, or in the developer guide if it describes current behaviour.
- **Findings an issue surfaces are fixed on that issue's branch**, with their own tests (owner, 2026-09-22, restated 2026-10-03): no follow-up issues, no TODOs, no "fix or file?". "Minimal" means the smallest change per bug, not fewer bugs fixed. File separately only work that is both large AND unrelated, with the evidence. In this repo this overrides the global "mention a secondary issue, do not touch it" rule.
- When the user tells you to prepare for a session handvover, immediately save your memories
- When a commit is made, check that the FEATURES.md file is updated to include the new feature if it's a significant one. Ask the user if in doubt of the relevance of the change meriting an update. Pending features and known bugs are NOT tracked in the repo — they live in GitHub issues (https://github.com/jorgegv/jnext/issues); `TODO.md` is only a pointer to that page.
- When a new development is made that changes any interface in any subsystem, make sure there are enough test cases in that subsystem's test  plan to fully test that new code/interface. Modify the plan if needed and do an independent code review for the new code.
- When a bug is fixed in any subsystem, make sure there are enough test cases in that subsystem's test  plan to fully test the fixed new code/interface. Modify the plan if needed and do an independent code review for the new test code.
- **Every external process jnext spawns runs under `LANG=C`** — set in the
  CHILD's environment, never by changing jnext's own. A child's output is
  otherwise a function of the user's locale, and this project's own development
  host runs Spanish: `ping` prints `tiempo=` for `time=`, and a comma-decimal
  locale turns `12.3 ms` into `12,3 ms`, which `strtod` reads as 12. Forcing
  the locale makes the whole output deterministic instead of making the parser
  guess which fields happen not to translate. Prefer the child's **exit status**
  to its text either way; `LANG=C` makes parsing safe, not preferable.
  - Spawn with an **explicit, minimal `envp`** wherever the output is parsed.
    POSIX ranks `LC_ALL` above `LANG`, so a child that INHERITS the environment
    is not fully protected by `LANG=C` alone — an inherited `LC_ALL` outranks
    it. A child that inherits nothing has no such problem.
  - The FFmpeg spawn (`src/core/video_recorder.cpp`) inherits and therefore
    carries that residual; it is documented at the call site.
  - Its Windows twin (`src/core/win_process.h`) is **exempt for cause, not
    skipped**, and the reasons are independent: `LANG` is not how Windows
    localises a console tool, the call passes a null `lpEnvironment` so there is
    no block to set a variable in, and **nothing reads ffmpeg's output on
    either platform** — POSIX redirects it with `>/dev/null 2>&1` in the
    command string, Windows with NUL `STARTUPINFOA` handles, and only the exit
    status is consulted. The PROPERTY is what the exemption rests on; the two
    mechanisms that achieve it differ, so do not read one platform's spelling
    as the rule. Recorded at that call site too, so this does not read as
    violated by code it does not reach.
  - **The strongest form of this rule is not to spawn at all.** `AT+PING` was
    first built by running `ping(8)` and is now an in-process ICMP socket
    (`src/esp01/src/esp_ping.cpp`), which deletes the locale question along
    with the output parsing and the argv surface. Where a platform API exists,
    prefer it to a command whose output you would have to read.
- For git commands that run against another directory (e.g. a worktree), always use `git -C /abs/path <cmd> ...` instead of `cd /abs/path && git <cmd> ...`. The `-C` flag avoids shell-state side effects and keeps the current working directory stable across tool calls. It also avoids needless permission prompts to the user.

### Pull requests

> **Read [doc/PULL-REQUEST-PROTOCOL.md](doc/PULL-REQUEST-PROTOCOL.md) whenever a
> pull request is being reviewed or merged — especially an external one from a
> third-party contributor.** It is the authoritative, strictly-enforced gate:
> Bugfix vs Feature flows, the tests-included / no-existing-test-modification /
> license-clean-fixtures / no-new-dependency rules, and the design-doc + use-case
> requirement for features. A non-compliant PR is not merged.

### Merging a completed feature/fix to `main`

The single authoritative protocol for landing any implemented change on `main`:

1. **Dedicated branch + worktree** off current `main` — never edit `main` directly. Each independent feature gets its own branch (so parallel agents don't trash each other). A standalone issue gets its own branch and merges to `main` when done. An **epic** gets an `epic<E>-<slug>` branch; its sub-issues branch off it and merge back into it, never into `main`, and the epic reaches `main` in one merge when finished (owner, 2026-10-06). A multi-stage issue lives on one branch until the whole issue is done and merges once; each stage is still reviewed on that branch (owner, 2026-09-24). Merge `main` into long-lived branches (epic, multi-stage) from time to time, so the final merge is not a cliff. A precedent set by an earlier session is not a decision.
2. **Full test triplet green on the branch, plus the SDL-only unit run and its Windows twin** before review: `make clean && make gui-release`, then `make unit-test`, **`make unit-test-sdl`**, **`make unit-test-win`**, the FUSE Z80 suite (`./build/test/fuse_z80_test build/test/fuse` → 1356/1356), `make regression`, and **`make regression-win`**. `make gui-release` is the PGO build (GH #297), so `make regression` tests the shipped binary and also runs FUSE against the PGO build's CPU core (`make fuse-pgo`, a prerequisite). No FAIL and no SKIP anywhere.
   - `make unit-test-sdl` applies to **every** branch, not only GUI-touching ones (owner decision, 2026-09-25). Its 108 suites are the core emulator plus the platform decision-logic both frontends share, minus Qt and the debugger — and they INCLUDE `host_key_latch_test`, which drives the real `SdlInput::poll()` (GH #268) precisely because an SDL-only build is the only place that coverage survives. So an SDL-frontend change needs this run just as much as a core one does; do not read “the non-Qt set” as “no frontends”. Cost on a branch that actually changed code: ~17 s with a warm ccache (a no-op re-run of just the suites is ~9 s). See the two-configuration rule under **Testing**.
   - `make unit-test-win` applies to **every** code branch too (owner, 2026-10-10, GH #214): the same SDL-only suites cross-built for Windows with the Fedora MinGW toolchain and run under wine (`dnf install mingw64-gcc mingw64-gcc-c++ mingw64-SDL3 mingw64-zlib mingw64-libpng mingw64-winpthreads wine-core wine-common`). It is the only way a branch sees a Windows compile break, a Win32 code path or a path-separator bug before review, and it runs on the development host (~30 s warm, ~3 min with a cold ccache). What wine cannot stand in for (symlink creation, a real console attach, NTFS short names and streams) stays untested; the suites that need it are `# os: posix`.
   - `make regression-win` applies to **every** code branch too (owner, 2026-10-10, GH #319): the regression suite's screenshots and the functional rows Windows can run, against the Release + LTO (non-PGO) Qt `jnext.exe` and the SDL-only `jnext.exe` under wine (extra: `dnf install mingw64-qt6-qtbase`). It is the only way a branch sees a Windows regression before review; ~6 min under the host lock on top of warm builds, 0 FAIL and 0 SKIP, absent rows printed by name (declared by `os=` in `functional_tests.conf`, never skipped), not stamped. macOS runs in CI only (`macos-regression`).
   - Use **`make regression`**, never bare `bash test/00regression/regression.sh`: the suite's `sdl-keypress-func` row needs `build/sdl-release`, which only the make target builds, so the bare script aborts as a harness fault. Two separate agents lost a run to this on 2026-09-25. Targeted rows likewise go through **`make regression-rows ROWS="<row> ..."`**, which builds the same binaries first (the bare script with row names is fine only when they are already built).
   - **A DOCUMENTATION-ONLY change runs NO code gate** (owner rule, 2026-09-27). If the branch or
     the set of changes to merge touches only documentation, run only the gates the documentation
     itself needs — normally `make docs-check` (man page + user guide + developer guide staleness),
     plus `make docs-devguide-diagrams` when a `.dot` source changed — and skip the triplet, the
     SDL run, FUSE and the regression suite entirely. Nothing they cover can have changed. A
     change that is doc-only *except* for one code or test edit is NOT doc-only. Files that gate
     nothing (`CLAUDE.md`, `doc/design/**`, `ChangeLog`, `README.md`) need no gate at all; the
     generated-and-committed trees (`doc/man/`, `doc/user-guide/`, `doc/developer-guide/`) are
     exactly what `docs-check` exists for, so edit the source, re-render, commit both.
   - **Run the full regression only when it is needed** (GH #295, owner decision 2026-10-04).
     A green `make regression` on a tree with no uncommitted non-doc change writes a **stamp**
     keyed on that tree's content, docs excluded (`test/regression-stamp.sh` defines the keyed
     paths, in one place). `make regression-stamp-check` prints the stamp covering the current
     tree or exits non-zero saying why there is none; **a matching green stamp satisfies the
     regression gate.** Only a run with **fail=0 and skip=0** is stamped: a SKIP is a row that
     was not tested. Hence:
     - the author runs it once, on the final non-doc state, and commits before running (a dirty
       tree is not stamped — the run says so);
     - the **reviewer does not re-run it**: they run `make regression-stamp-check` in their own
       worktree of the branch (stamps are per user, shared across worktrees) and run the
       **targeted rows** the change touches (`make regression-rows ROWS="<row> ..."`);
     - a fix round re-runs it **only if non-doc content changed** — otherwise the stamp still
       matches;
     - **no re-run after a clean merge of `main` into the branch** (the merge result is not
       re-gated locally; CI on `main` stays a full run and is the safety net);
     - **mutation testing and diagnosis use targeted rows only**, never full runs;
     - only ONE full run per host at a time: `regression.sh` takes a host lock and waits
       (saying who holds it); it also waits, bounded, while the 1-minute load is above `nproc`.
       Targeted runs never lock;
     - **the owner's timing-row rule has a path to the stamp**: a timing row that fails under
       load and passes SOLO counts as a pass. A full run whose ONLY FAILs are functional rows
       that failed with the load at or above `nproc` (the harness flags each) records them as
       PENDING; `make regression-confirm` re-runs exactly those rows, one lane, under the lock
       and the quiet-host wait, on the same tree, and only if every one passes is the run
       stamped — the stamp names them (`confirmed_solo=`). An idle-host FAIL, a screenshot or
       lint FAIL, or any SKIP has no such path: it is real until fixed.
3. **Independent code review** by an agent/person that did NOT write the change — never self-review. The reviewer works in its own worktree, never the author's. Verdict is binary APPROVE / REJECT; on REJECT, fix and re-review.
4. **Merge on green APPROVE**, one branch at a time. The manager (not the authoring agent) does the merge. If a merge conflicts, the agent who merged last fixes it on their own branch. After any merge that touched `test/unit-tests.conf`, `functional_tests.conf` or a pinned row count, recount from the file. Two branches that each bump `# expect:` from N to N+1 auto-merge to N+1 with no conflict. Resolve conflicts in those files line by line, never by taking a side (list the overlap with `comm -12` of the two branches' `git diff --name-only`); then regenerate the generated files (traceability matrix, guides) from a real run.
5. **Immediately after each merge to `main`, bump the patch version: `JNEXT_ALLOW_MAIN_WRITE=1 make bump-patch`** (bumps `version.yaml`, commits, and creates the git tag). Every feature/fix that lands on `main` gets its own patch bump — per merge, not batched. This is separate from the deliberate minor/major release flow in "Version bumping" below.
6. **Push only as the push rule above allows**: under the standing authorization, `main` and its new tags after the bump (explicit refspecs, ≤3 tags, CI on `main` idle) and epic branches; anything the push rule does not cover stays local until the user says push.

## ChangeLog file

- A ChangeLog file should exist at the root of the repository
- It should contain entries for the different tagged versions, in reverse chronological order (most recent at the top of the file)
- **Version headers correspond to PUBLIC RELEASE tags only** (the tags listed in `releases.yaml`), NEVER intermediate/private `make bump-patch` tags. A private patch bump does NOT get its own ChangeLog entry. Accumulate all changes since the last public release under a single top `## Unreleased (YYYY-MM-DD)` header; when a public release is actually cut, rename that `Unreleased` header to the released version + date. So most feature/fix bumps land under `Unreleased` and only coalesce into a versioned header at the next public release.
- Each entry should consist of the version tag, and below it, an extremely terse description of the new features and fixes of that version, up to the previous version. There should be these sections:
  - User Features: new features oriented to users who just run games and programs: GUI, emulation features, main menu, etc.
  - Developer Features: new features oriented to developers: in general, all debugger and instrospection features
  - Bug Fixes
  - Internal JNEXT Development: new plans, enhancements to test results, big architectural changes or enhancements, etc.
  - Contributors in this release (owner decision 2026-10-04, from v1.1.0 on): a single `- ` bullet with a comma-separated list of the handles (`@handle`) of everyone other than the owner who CONTRIBUTED since the previous public release: opened an issue or PR, commented on one, reviewed a PR, or tested and reported back (owner, 2026-10-05: "they all contribute"). Every handle is written `@handle` whatever its origin (GitHub, a forum, elsewhere), with no note of where it comes from (owner, 2026-10-07: `@vmorilla, @MickeyG`). No per-person details.
- Descriptions for each feature/fix should never be more than one line, and should be about 10-20 words maximum
- Trivial fixes, syntax, reformats, documentation, project plan updates, etc. should not appear on the ChangeLog. Only significative features and fixes.
- The file should only be updated when the user requests it
- The initial version (v0.91.0) should contains a short list of the current features at that time
- If there are commits after the last tag, and the user requests a ChangeLog update, it should be updated up to the current date, and using "(current date)" as the version identifier in the ChangeLog
- Don't be overly confident about features: never put an ongoing feature in the ChangeLog if it's still not tested or has known bugs
- Don't put commit IDs in the ChangeLog
- Try to coalesce similar features or fixes in a single description if possible
- The goal os this file is to give the emulator USERS an overview of the most important things happened since the last version. It's NOT meant to be an exhaustive list of changes at all. It's not meant to be a development diary for the emulator itself either.

## Version bumping

> **Read [doc/RELEASE-PROTOCOL.md](doc/RELEASE-PROTOCOL.md) whenever the user
> asks to release or bump a version.** It is the authoritative process:
> `version.yaml` as single source of truth, the `make bump-patch/minor/major`
> semantics (ALL three prompt `y/N` whether to make the tag a public release by
> adding it to `releases.yaml`; default No = private history tag), the rule that
> **a public release MUST update the ChangeLog first** (differential from the
> previous record), `packaging/sync-version.sh` (which adds the version to the
> AppStream metainfo `<releases>` only for public releases), the `releases.yaml`
> allowlist that gates public GitHub Releases, the `make package-*` targets, the
> CI release gate, and the push rules (incl. the GitHub ≤3-tags-per-push limit).
>
> **Its §8 is also the authoritative format for release announcements** — read
> it whenever the user asks to announce a release or write a forum post: the
> current-to-previous-public-release span, the mandatory pair of outputs (rich
> text + phpBB for spectrumcomputing.co.uk and z88dk.org), one line per feature,
> and the rule that trivia (keyboard changes, bug fixes, internal work) is not
> advertised.

When the user asks to bump the version, follow these steps in order:

1. Run all unit tests (`make unit-test`) and regression tests (`make regression`) — none must have any FAIL or SKIP. The regression is satisfied without a local run by `make regression-ci-check` (a green CI run on the same content key, checked before the bump) or `make regression-stamp-check` — see doc/RELEASE-PROTOCOL.md §7
2. ~~Update the traceability matrix~~ — **no longer a manual step (GH #196).** It is generated and staleness-gated; `make unit-test` regenerates and fails if the committed copy differs. Commit the regenerated file if it changed.
3. Update the unit test status report
4. Update the DEVELOPMENT-SESSIONS document (`doc/DEVELOPMENT-SESSIONS.md`)
5. Update the ChangeLog: add to the top `## Unreleased (YYYY-MM-DD)` section; only for a public release rename it to `## vX.Y.Z (YYYY-MM-DD)` for the version being cut (`changelog-update` skill)
6. Commit all the above changes
7. Bump the version by running `JNEXT_ALLOW_MAIN_WRITE=1 make bump-<bump_type>` (where bump_type is `patch`, `minor`, or `major`) — this bumps `version.yaml`, runs `packaging/sync-version.sh` (re-renders the two committed guides; for a public release also adds the AppStream `<release>` entry), stages them, commits, and creates the git tag

**`version.yaml` is the single source of truth for the version, and the only file a bump
edits by hand.** Everything else READS it when it is built: CMake (`PROJECT_VERSION`, so
the binary and every CPack package), the `package-*` recipes (artifact names), both mkdocs
configs (`src/doc/version_hook.py` sets `extra.doc_release`, the guides' "This version"),
the native rpm spec (`rpmbuild --define "jnext_version X.Y.Z"`; it refuses to parse
without it) and the Debian changelog (generated at build time by
`packaging/gen-debian-changelog.sh`). So a private bump commit is `version.yaml` plus the
re-rendered `doc/user-guide` and `doc/developer-guide` (committed on purpose: they show
the version and are read offline from a clone); a public one adds `releases.yaml` and the
AppStream metainfo `<release>` entry, whose release date nothing else records.
**Do not add a hard-coded copy of the version anywhere** — read `version.yaml` at build
time instead. `test/packaging/sync-version-test.sh` (part of `make unit-test`) fails if a
copy appears in a packaging, CI, CMake or mkdocs file.

## Building

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

The build uses CMake with Qt6 UI enabled (`-DENABLE_QT_UI=ON`). The executable is at `build/jnext`.

`build/jnext` is a **RelWithDebInfo dev binary** (the CMake default when no
`-DCMAKE_BUILD_TYPE` is given — Task 27 T0). Any **performance measurement or
benchmark must use a Release build**, never `build/jnext` — and which one
depends on the question (GH #297):

- **A/B between two code changes: `build/gui-release-non-pgo/jnext`**
  (`make gui-release-non-pgo`; `make bench` uses it). Its speed does not move
  with a retrained PGO profile, so a difference is the code's.
- **"How fast is what we ship": `build/gui-release/jnext`** (`make
  gui-release`, the PGO build; `make bench BENCH_BUILD=gui-release`).

`make gui-release` is a gcc **PGO build**: instrument, train
(`tools/pgo-train.sh`, needs the SD image — provisioned through jnext's own
download if missing), rebuild with the profile. Training is skipped when
nothing changed; a missing or mismatched profile is a build error.

## Testing

> **Before authoring or rewriting any subsystem unit test plan, or
> implementing one of its planned rows**, read [doc/testing/UNIT-TEST-PLAN-EXECUTION.md](doc/testing/UNIT-TEST-PLAN-EXECUTION.md).
> It documents the VHDL-as-oracle rule, the pass/fail distinction (a SKIP is
> a failure, below), the 1:1:1 emulator-fix-plus-row process, the independent-review
> requirement, and why all of that exists (the coverage-theatre audit).
> The process is mandatory for every test plan rewrite and every emulator
> fix that touches subsystem tests.

### A SKIP is a failure of the gate — HARD RULE (owner, 2026-10-06)

A row that was not tested is not a pass. **Every harness exits non-zero on any
SKIP**, in every mode: `make unit-test` / `unit-test-sdl` (`run-unit-tests.sh`,
a suite with `Skipped > 0`), `make regression` / `regression-rows` /
`regression-confirm` / `--update` / a standalone row (`regression.sh`,
`standalone_summary`), `make package-test`, and every contract suite. A missing
test tool or a docs renderer whose version differs from the committed render
FAILS the check instead of skipping it, on every machine, `$CI` or not. A row
whose feature does not exist yet is PLANNED in its `*-TEST-PLAN-DESIGN.md`,
never a `skip()` in a suite. The tools the suite needs are provisioned, not
assumed: `make regression` runs `make z88dk-gdb` (a sha256-pinned z88dk v2.4
client built into `~/.cache/jnext/tools`, like the SD image); the rest are the
`test` job's dnf list in `ci.yml`. `skip()` survives only as an ENVIRONMENT
fallback arm of a row that also has a `check()`; when it fires, the run is red.

### CI runs the EXACT same commands as a local run — HARD RULE

Every `run:` in `.github/workflows/ci.yml` is a **plain make target**, the same
one a human types locally. CI and local must never diverge.

If CI appears to need something the local flow lacks, the answer is almost never
a CI-only step: either the project is missing a target (add it to the Makefile,
so local runs get it too) or the need is imaginary. **Reuse the program's own
mechanisms** — jnext downloads and caches its SD image itself, and
`sd_rom_extractor_test` already takes a `JNEXT_TEST_SD_IMAGE` override.
Reimplementing either in YAML is the error.

**Never pipe a build or test command** (`| tail`, `| head`, `| grep`): GitHub's
default `bash -e` does not set `pipefail`, so the step takes the *pipe's* exit
status and a failing `make` reports success. This is not hypothetical — CI once
printed `62 pass, 1 fail` and `UNIT TESTS FAILED` in bold and went **green**.

Do not add a step for something the Makefile already declares as a prerequisite
(`docs-check` is a prerequisite of both `unit-test` and `regression`).

The job runs in `container: fedora:44` — the same image `release.yml` builds the
rpm and Windows artifacts in, and the distro the maintainer develops on. That is
load-bearing: distros ship different pandoc / mkdocs-material versions, and
those emit byte-different generated documentation, so a different runner reports
a *version gap* as staleness.

### Documentation is checked by every test run

`make unit-test` and `make regression` both depend on **`make docs-check`**, so a
stale generated document fails the test run itself rather than waiting for CI or
a reviewer. This is deliberate: `doc/man/jnext.1` and `USAGE.md` are GENERATED
from `doc/man/jnext.1.md` and COMMITTED, so a stale committed output is a silent
lie no other gate can see. Edit the source, run `make docs-man`, commit the
regenerated outputs. On a host without pandoc, or with a different pandoc than the
committed outputs were made with, the check fails (it cannot verify; CI is the reference).

**Know exactly what this proves and what it does not.** `docs-check` proves the
two generated outputs match `jnext.1.md`. It does NOT prove `jnext.1.md`
describes the CLI `src/main.cpp` parses. **`make cli-check` does** (issue #43):
`src/core/cli_options.h` holds the flag set as a DATA table, `main.cpp`
dispatches from it, and `cli_options_test` diffs the table against the man page
OPTIONS section both ways — implemented-but-undocumented and
documented-but-unimplemented are both hard failures, as is an argument count
that disagrees. It runs as a prerequisite of `make regression` and as a declared
suite of `make unit-test`. Deliberate exceptions (`--sd-card`, an undocumented
back-compat alias) are declared IN the table, never as a checker exclusion.

That seam had failed twice before the check existed: five flags entirely
undocumented, and v0.98.60's man page with a wrong scale range, two missing GUI
menus and a status-bar indicator that does not exist — all found by reading the
running product while writing the user guide. **`cli-check` covers the flag set,
not the prose**: the v0.98.60 defects were GUI descriptions in the man page's
narrative sections, which nothing checks. Keep reading the running product.

**`doc/testing/TRACEABILITY-MATRIX.md` is generated too, and gated the same way**
(GH #196). `make traceability-check` — a prerequisite of `make unit-test` —
regenerates it and fails if the committed copy differs, exactly as `docs-check`
does for the man page. It needs a built test tree, because a row's `Status`
comes from actually running its suite.

**Nothing in that file is hand-written.** A row's description and VHDL citation
come from its own `check()`/`skip()` call; planned-but-unimplemented rows come
from the subsystem's `*-TEST-PLAN-DESIGN.md`; and the single exceptions file
`test/traceability-exceptions.conf` covers the one case neither can answer — a
planned row of a suite that has no plan doc because it has no VHDL counterpart
(`rewind_test`, `sdcard_test`). Editing the matrix by hand does nothing: the
next run overwrites it, and the gate fails in the meantime.

That is why `frozen`, doc-vs-computed `drift` and `unrecorded` no longer exist
as classes — there is no hand-written side left to disagree with. The one
report that survives is a plan doc and a test source citing DIFFERENT VHDL for
the same row, which is signal about the spec rather than bookkeeping.

**A row ID must be a LITERAL.** Building one at run time
(`check((std::string(c.id) + "-35").c_str(), ...)`) emits a row no source
reader can see: the matrix then carried two IDs that are not rows and none of
the six that are. Spell every ID out. `run-unit-tests.sh` enforces it: every ID a
suite reports must appear verbatim as a string literal in its sources, with
`fuse_z80_test` and `z80n_test` the declared exceptions (their IDs are the case
names of a checked-in fixture file).

**An ID is a GLOBAL name.** `make unit-test` runs `traceability-dup-ids.pl`,
which refuses when two suites (the `?`-gated GUI ones included) assert the same
ID — that reuse is how #190's manufactured coverage happened — when a PLANNED
row in a `*-TEST-PLAN-DESIGN.md` carries an ID asserted by a suite the matrix
does not read that row's status from, and when a declared suite cannot be
resolved to its source (GH #243). The baseline file
`test/traceability-dup-ids.conf` is EMPTY: GH #243 renamed one side of all 29
pre-existing collisions. A new collision is fixed by renaming, never by adding
a baseline line, and an entry that no longer collides is itself a refusal.
Within ONE suite the rule is enforced at run time: every row reports its ID
(`test/row_id.h`), and `run-unit-tests.sh` fails a suite that reports an ID twice
or a number of IDs other than its row count — so a loop asserting N rows gives
each its own literal ID in its case table.

The rendered user guide under `doc/user-guide` is also generated (from
`src/doc/user-guide`, via `make docs-userguide`) and committed, and it IS
staleness-checked: `docs-userguide-check` is the second half of `docs-check`, so
it runs on every `make unit-test` and `make regression` exactly like the man
page. If you edit a guide source, re-render and commit it in the same change —
otherwise the next test run fails. On a host without mkdocs, or with a different
mkdocs-material than the render was made with, the check fails; CI is the reference.

**The DEVELOPER guide works the same way, with one extra generated stage**
(GH #44). Source: `src/doc/developer-guide` + `mkdocs-devguide.yml`. Render:
`make docs-devguide`. Committed output: `doc/developer-guide`. Gate:
`docs-devguide-check`, the third part of `docs-check`, so it too runs on every
`make unit-test` and `make regression`. The extra stage is the **figures**: the
diagram sources are Graphviz `.dot` files under
`src/doc/developer-guide/diagrams/`, rendered by `make docs-devguide-diagrams`
into **committed** SVGs under `src/doc/developer-guide/img/`. Both the SVGs and
the rendered site are byte-diffed, so hand-editing an SVG fails the gate exactly
as hand-editing the man page would — edit the `.dot`, re-render, commit both.
`make read-devguide` serves it locally.

Graphviz, not Mermaid, and the reason is the same one that makes the guide
committed at all: mkdocs-material's Mermaid integration fetches
`mermaid.min.js` from unpkg.com at page load, so diagrams would be blank for
exactly the offline reader the committed render exists for. Self-hosting means
vendoring a 3.5 MB blob; pre-rendering Mermaid means a Node + headless-Chromium
dependency. `dot` is a small packaged offline tool of the same class as pandoc
and mkdocs. It is installed in CI alongside them.

**When you change a subsystem, check whether the developer guide still
describes it correctly.** That guide is a description of the current system, not
a roadmap — a stale paragraph in it is the same class of defect as a stale man
page, with the difference that no gate can detect it. `doc/design/EMULATOR-DESIGN-PLAN.md`
is a superseded historical record (GitHub issues are the roadmap) and is explicitly NOT a source for it: writing the user guide
proved that plan wrong about the debugger in five separate ways.

### The test manifests — a missing test is a LOUD FAILURE, never a silent skip

The suites are **declared**, and the harness proves it ran exactly what was declared.
A green triplet is only as trustworthy as its denominator (Tasks 32/35/37: three suites
had vanished from the counts, all found by accident).

**`test/unit-tests.conf`** — every unit suite, with its **exact expected row count**.
`test/run-unit-tests.sh` **refuses to run** (exit 2) if the manifest and the suites CMake
registered via `add_test()` disagree in either direction, if a declared binary is not
built, or if a suite is declared twice. It **FAILS** (exit 1) if a suite reports a row
count other than the pinned one (in either direction), prints no parseable `Total:` line,
crashes, or times out. `make unit-test` **exits non-zero** when a suite fails.

> **Adding or removing a test row means updating its count in the manifest.** That edit is
> the point: the number is the project's claim about how much it tests, and it is made
> deliberately. The CMake side is not a second hand-kept list — it is read from the
> generated `build/test/CTestTestfile.cmake`.

### TWO build configurations run their suites (GH #273)

`ENABLE_QT_UI` × `ENABLE_DEBUGGER` gives four combinations. **`make build-matrix`
builds all four** — it catches link rot, which only appears at build time — but it
never ran a suite in any of them, so `make unit-test` and CI only ever exercised
the default one and a suite could stay red in a supported configuration
indefinitely. That is not hypothetical: `host_hotkey_test` had three rows failing
in `ENABLE_QT_UI=ON / ENABLE_DEBUGGER=OFF` and nothing noticed.

**Two configurations are worth RUNNING** (owner decision, 2026-09-25):

| target | configuration | build dir | suites |
|--------|---------------|-----------|--------|
| `make unit-test`     | Qt + debugger (the shipped one) | `build/`              | 140 |
| `make unit-test-sdl` | SDL-only, no Qt, no debugger    | `build/sdl-unit-test` | 108 |

**The SDL-only set also runs on other operating systems** (GH #214, owner decisions
2026-10-10), and the table's rows are per platform:

| target | platform | build dir | suites | where |
|--------|----------|-----------|--------|-------|
| `make unit-test-sdl` | Linux (native)                  | `build/sdl-unit-test`     | 108 | local gate, CI `test (unit)` |
| `make unit-test-sdl` | macOS (native)                  | `build/sdl-unit-test`     | 107 | CI `macos-unit` (Homebrew `bash coreutils grep` required); one fewer than Linux because `sdcard_file_add_linux_test` is `# os: linux` (SDFA-T26 needs a case-sensitive filesystem; APFS is not) |
| `make unit-test-win` | Windows, MinGW cross under wine | `build/win-sdl-unit-test` | 103 | local gate, CI `test (unit-win)` |
| `make regression-win` | Windows, MinGW exes under wine | `build/win-release-non-pgo`, `build/win-sdl-release` | 1 + 66 + 128 rows | local gate, CI `test (regression-win)` |
| `make regression-macos` | macOS (native) | `build/gui-release-non-pgo`, `build/sdl-release` | 1 + 66 + 132 rows | CI `macos-regression` |

**The regression suite also runs off Linux** (GH #319, owner decisions 2026-10-10): `regression.sh --platform` runs the screenshots and the functional rows an OS can run. A row's line in `functional_tests.conf` opts out with `os=<list>` (a subset of `linux,macos,windows`, `linux` mandatory, no tag = all three) and a `# os:` reason above it; `# expect-macos:` / `# expect-windows:` pin the counts; the absent rows are printed by name, never SKIPs. The 6 lints and the stamp stay Linux-only. The wine runner (`test/wine-run.sh --jnext`) folds CRLF, translates `QT_*`/`SDL_*`, and needs a persistent wineserver; the Qt test exe bundles `qoffscreen.dll`, in the test tree only, never in a package. See the developer guide, section 4.3.

Both new CI jobs are BLOCKING. The `# os: all | posix | linux` directive in
`test/unit-tests.conf` declares which target OSes own a suite — read from the build
tree's own `CMakeCache.txt` (`JNEXT_TARGET_OS`) and cross-checked both ways exactly
like `# gate:`; a row that cannot exist on a platform moves, ID and assertion
unchanged, into an os-gated sibling suite, never into a SKIP. The Qt, debugger,
docs and traceability gates stay Linux-only (the regression suite's portable subset is the two rows above). On macOS the harness needs
Homebrew's `bash`, `coreutils` and `grep` (`make unit-test-sdl` puts them first on
`PATH` for its own recipe) and refuses with the install line when they are missing.

The other two (Qt without the debugger; SDL with it) are not used in practice and
stay **build-only**. CI runs both targets (and `make unit-test-win` and macOS's `unit-test-sdl`, see below) — the same commands a human types —
and `make unit-test` deliberately does **not** pull the second one in, so the
everyday inner loop does not pay for a second build and suite run. The SDL tree
gets its own build directory: `build/` must stay the Qt tree that `unit-test-build`
guards. `make clean` takes both.

**A gated suite's absence is CHECKED, not excused.** `test/unit-tests.conf` carries
`# gate: none | qt | dbg | qt+dbg` directives; each names the CMake options a suite
needs to exist, and the `?` marker on the suite line must agree with the gate in
force. `run-unit-tests.sh` reads `ENABLE_QT_UI` / `ENABLE_DEBUGGER` out of the build
tree's **own `CMakeCache.txt`**, so the configuration comes from the build and never
from the caller, and it **refuses** (exit 2) when a suite is missing from a
configuration whose gate is satisfied, or present in one the gate excludes. Before
this, `?` meant "skip it quietly if CMake did not register it" — a suite that
stopped being registered in the configuration that owns it printed a NOTICE and the
run stayed green, which is the same silent shrinking the manifest exists to forbid.

**`test/00regression/regression_tests.conf`** (screenshots) + **`functional_tests.conf`**
(functional). At the end of a full run, `regression.sh` asserts every declared functional
test reported exactly one row, no undeclared row appeared, and the total equals
`6 lint + 1 sdcard-provision + screenshots + functional`. Screenshots additionally get an
*independent* witness: every checked-in `img/<name>-reference.png` must have a conf entry, so
truncating the conf cannot silently shrink the suite. Any mismatch is a **harness fault** (exit 2).

**Functional rows run in parallel, each in a process of its own** (GH #295). The driver
starts `test/00regression/row-runner.sh` per row, with the row's own `$TMP_DIR` and
`$RUN_DIR` (= `$JNEXT_CONFIG_DIR`), and merges the results in declared order (per phase)
with every check above intact; a row that dies or outlives its bound is a named FAIL, a row
that reports twice/nothing or writes into another row's directory is a harness fault.
Words after a row's name in `functional_tests.conf` place it: untagged = the parallel phase
(`JNEXT_TEST_JOBS`, default every CPU); `quiet` = a low-concurrency phase after it, for rows
**measured** to fail under parallel load (`JNEXT_TEST_QUIET_JOBS`, default 1); `serial` =
alone, last; `private-sd` = the row writes to the SD card and gets a copy of its own.
Untagged rows boot the run's clone through a hard link, and the clone is fingerprinted
before and after the run — a row that writes to it without the tag is a harness fault.
A new row that fails only in the parallel phase gets `quiet`, with the evidence recorded.
A row that needs an X server starts it with **`xvfb-run -d`** (Xvfb picks the display
itself): `xvfb-run -a` chooses by scanning lock files, two parallel rows got the same
display and one silently SKIPPED; `-a`/`-n` are banned (harness-selftest HS-72).
A row that drives the app with `xdotool` also passes **`-noreset`** in `--server-args`:
Xvfb resets when its last client disconnects, so the row's own `xdotool` polling can
land the app's connection in a reset and the app exits without a window (GH #318,
harness-selftest HS-89).

**No row script may install a `trap`** (GH #153). Every row is still SOURCED — now into its
row-runner shell, which holds the library's one `trap regression_cleanup EXIT/INT/TERM` that
deletes the row's directories and, in the driver, the per-run 1-2 GB SD clone; a second trap
silently replaces it, and only the *successful* run leaks (INT/TERM survive, so an interrupted
run still cleans up). The driver also catches the leak after the fact (a row that exits with
its directories still there is a harness fault), but the lint stops it before it runs. `test/00regression/lint-traps.sh`
— row 2 of the suite, inside `scripts/00-preflight-lint.sh` — bans `trap` in `scripts/*.sh` for
every signal and at any depth, including behind `builtin`/`command`, inside `eval`, and via a
heredoc fed to `source`/`.`/`eval` (which runs in *this* shell). Put scratch files under
`$TMP_DIR`, which the harness trap already removes; a shell that truly needs its own trap goes
in a file run with `bash` — heredoc bodies with a non-sourcing consumer are exempt.
Its comment stripper is a bash-exact three-state quote scanner, not a quote counter (a counter
cannot express escaping), and it dequotes before matching, since bash concatenates word
fragments and `tr''ap` runs the builtin without containing the word.
**Scope: it catches the ACCIDENTAL trap — the failure that actually happened — and does not
try to stop deliberate obfuscation, which no static grep can.** `t=trap; $t 'c' EXIT` defeats
it in eight characters, and that is accepted, not a backlog item. The lint's header gives
EXAMPLES of what it cannot catch, explicitly not an exhaustive list (two "exhaustive" lists
have already been proved incomplete), and a MAY WRONGLY FLAG list for the other direction —
a subshell `( trap … )`, or a live `eval`/heredoc head that merely mentions the word. Matching
is done on a SYNTAX SKELETON in which every quoted string collapses to one inert token, so a
live string whose contents look like syntax (`fail_row "… eval … trap …"`) stays clean; an
earlier whole-line dequote flagged exactly those, which blocks correct rows. Its self-test
pins 86 cases both ways, and cross-checks its own documented case table against the fixture
files so the two cannot drift. Command position is derived from bash's CLOSED set of reserved
words (`! coproc do elif else if then time until while`), so `if trap …; then` is caught by
construction rather than by having thought of it.

**Every `timeout` in a test script must escalate to SIGKILL.** `timeout N cmd` sends only
SIGTERM, which a command may ignore — the bound is then decorative and the 124 status lies.
Write `timeout --foreground --kill-after=5s Ns`. `--kill-after` (or `--signal=KILL`) is the
requirement; `--foreground` is optional and sometimes wrong to add — without it the command
gets its own process group and the KILL reaches its children, which is what you want when it
spawns a process tree. `test/lint-timeouts.sh`, row 3 of the preflight, enforces this across
every tracked `*.sh` under `test/` — a wider scope than `lint-traps.sh`, since the hazard has
nothing to do with being sourced. There is no exception list: whether a program handles
SIGTERM is not statically decidable.

**No pipeline in a test script may end in a quiet grep** (`producer | grep -q`). Under
`pipefail` grep exits on its match, the producer dies of SIGPIPE, and 141 becomes the
pipeline's status, so a line that IS there reads as missing (load-dependent; it made
`warm-start-func` flap). Ask grep alone: `grep -q P <<<"$out"`, or `<<<"$(producer)"`.
`test/lint-pipe-grepq.sh`, row 6 of the preflight, enforces it in every tracked test
script that runs under pipefail.

**The harness is itself under test.** `make harness-selftest` (also run every regression as
`harness-selftest-func`) injects each fault against stub suites and asserts the refusal. It
exists because the harness shipped once with a bug that appeared *only when a suite failed*
— the one path nobody exercises while everything is green.

`make regression` depends on `unit-test-build`: the suite runs `build/test/rewind_test`,
and `make clean` deletes it.

**Agent worktrees: run `make worktree-bootstrap` first.** `roms/*` is git-ignored, so a
fresh worktree has no SD-card image and cannot run the tests at all.

### FUSE Z80 opcode test suite

```bash
./build/test/fuse_z80_test build/test/fuse
```

Result: 1356/1356 pass (100%). That is the `build/` tree's CPU core; `make fuse-pgo`
(run by `make regression`) runs the same suite against the PGO `gui-release` one.

### Full regression test suite

Run the complete automated test suite (screenshot + functional rows; FUSE is a
prerequisite of the make target):

```bash
make regression                      # the gate: prerequisites, host lock, stamp
make regression-stamp-check          # is this tree covered by a green run?
make regression-rows ROWS="<row> ..."   # targeted rows: builds the binaries; no lock, no stamp
make regression-confirm              # re-run SOLO a full run's loaded-host FAILs; stamp if all pass
```

This runs all tests in headless mode and compares screenshots to reference images.
See [doc/testing/REGRESSION-TEST-SUITE.md](doc/testing/REGRESSION-TEST-SUITE.md) for full details.

To update reference screenshots after intentional rendering changes:

```bash
bash test/00regression/generate-references.sh
```

### Test-cycle performance (Task 39)

Two ways to keep the build/test/review loop fast. Neither weakens a test: the
identical work runs, it just runs faster. **Speed is never traded for test
rigour** — see the `JNEXT_TEST_JOBS` note below for the one place that
temptation arises, and why we decline it.

**1. ccache is wired into the build.** `CMakeLists.txt` auto-detects `ccache`
and uses it as `CMAKE_{C,CXX}_COMPILER_LAUNCHER` (guarded — a machine without
ccache builds exactly as before; `-DUSE_CCACHE=OFF` opts out). This is what
makes the mandatory `make clean` + full-rebuild discipline cheap: a clean
rebuild of `gui-release` + `build/` drops from ~65 s to ~8 s on a warm cache
(100% hit rate). Reverting a fix and rebuilding — the core reviewer move — is a
*pure* cache hit, because the source is byte-identical to a state already
compiled.

Give ccache room, once per machine (this is a user-level config, **not**
captured in the repo — re-apply it on any new machine):

```bash
ccache -M 20G     # the 5G default thrashes on a tree this size
```

**2. Reviewers: run mutation cycles in parallel, not serially.** Reviewer
mutations are independent by construction (reverting a stencil gate has nothing
to do with reverting a tab order), yet they are usually run one after another in
a single build dir. Give each mutation its own build directory (or its own
worktree) and run them concurrently — ccache is global, so the second and third
builds are almost free. Mutations are checked with **targeted rows**
(`make regression-rows ROWS="<row> ..."`), never full runs (GH #295).

**`JNEXT_TEST_JOBS` caps the suite's lanes; it is no longer a speed trade-off**
(GH #295, replacing the Task 39 rule "`JNEXT_TEST_JOBS=4` on every run"). The
suite's real-time-paced rows — `audio-underrun-func` reports underruns on a
loaded box, `screenshot-paused-func`'s control run takes ~55 s against a 60 s
timeout — are tagged `quiet` and run in their own low-concurrency phase after
the parallel one, so the screenshot and parallel functional phases use every CPU
by default. Pass `JNEXT_TEST_JOBS=N` only to leave CPUs to something else; do
not raise `JNEXT_TEST_QUIET_JOBS` to buy speed — that is the trade Task 39
measured and rejected. The suite itself now takes ~4 min on the 12-CPU dev host
(172-175 s with nothing else running, 223-253 s with other agents active, ~410 s
with 6 extra busy loops; it was ~13.5 min serial).

**Those two rows are examples, not the list** (GH #245). Contention also fails
rows that merely spawn short-lived processes — `subsystem-gain-func` failed
under a three-agent load on 2026-08-09 and never reproduced solo — and a
concurrent duplicate build once produced an `undefined reference to main`. So:
a single regression FAIL on a loaded host is unconfirmed until that row is
re-run SOLO (`make regression-rows ROWS=<row>`), and it is not
dismissed either until the solo run passes — a row that also fails solo is
real. **Always record the row name.** The harness does the bookkeeping: it
prints the 1-minute load at the start and the end of the run, flags each FAIL
that happened with the load at or above `nproc`, and lists every failed row by
name after the results. It never changes a verdict.

### Headless mode

The `--headless` option runs without display/audio for automated testing:

```bash
./build/jnext --headless --machine 48k \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```

Key options:
- `--machine TYPE` — `48k`, `128k`, `plus3`, `next` (default)
- `--headless` — no display, no audio, runs at max speed
- `--sdcard FILE` — SD image with ROMs at `/MACHINES/NEXT/`. Optional; if omitted, falls back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img` (the patched image; offers to download the canonical distribution `cspect-next-1gb.img` and produce that patched copy). `--sdcard-download-confirm` / `--sdcard-download-force` control that provisioning
- `--delayed-screenshot FILE` — save PNG screenshot after delay
- `--delayed-screenshot-time N` — delay in seconds (default 10)
- `--delayed-screenshot-frames N` — delay in frames (overrides `--delayed-screenshot-time`)
- `--delayed-screenshot-layers LIST` — layers composed into the screenshot: comma-separated `ula`, `layer2`, `sprites`, `tiles`, `all` (default `all`). An excluded layer is composed as if its hardware enable bit were clear, so the rest still follow NR 0x15 priority and the NR 0x4A fallback colour shows through. Excluding `ula` also removes the **border** (the ULA emits it)
- `--delayed-automatic-exit N` — exit emulator after N seconds. It is a hard bound: it always fires. If it (or a window close) arrives while a `--delayed-screenshot` is still outstanding — the capture came due but no frame was rendered for it, e.g. the debugger was paused, or the exit delay is simply shorter than the screenshot delay — jnext logs an **error** and **exits non-zero** rather than quietly writing nothing, or writing a stale frame with the wrong layers in it
- `--delayed-automatic-exit-frames N` — exit after N frames (overrides `--delayed-automatic-exit` when both are given). Same hard-bound contract as the seconds form, but deterministic: a capture due at frame N is still taken, one due at frame N+1 is not (and errors + exits non-zero)
- `--load FILE` — load a NEX, TAP, or TZX file at startup
- `--rtc "YYYY-MM-DD HH:MM:SS"` — pin the RTC to a fixed date/time (deterministic boot screenshots; ISO `T` form also accepted)

Always use `timeout --kill-after=5s` when running non-headless for safety.

### Building demo/test programs (z88dk)

Test programs are in `demo/` and built with z88dk:

```bash
# Build all demos (NEX + TAP)
make -C demo all

# Build only NEX or TAP
make -C demo nex
make -C demo tap
```

### ROMs

Wave 0.3 (Task 8 Multiface plan, 2026-05-04) made the SD-card image the
canonical source for **all** ROMs jnext needs at runtime, mirroring real
ZX Spectrum Next hardware. There are two parts:

1. **FPGA boot ROM (`nextboot.rom`, 8 KB)** is silicon-baked: embedded
   into the jnext binary as a generated C byte array (`src/core/embed_rom.cmake`
   invoked from `src/core/CMakeLists.txt` — portable across Linux/Windows/macOS,
   no objcopy). No CLI flag, no SD lookup. Mirrors the on-FPGA flash IPL of real
   Next hardware.

2. **All other ROMs** are extracted from the SD image (supplied via
   `--sdcard`, or the `~/.jnext/sdcard/` fallback) at canonical TBBlue
   paths via the host-side FAT32 reader in `src/core/sd_rom_extractor.{h,cpp}`:
   - `/MACHINES/NEXT/48.rom` (16 KB) — 48K BASIC
   - `/MACHINES/NEXT/128.rom` (32 KB combined) — 128K BASIC (split into 2 banks)
   - `/MACHINES/NEXT/plus3.rom` (64 KB combined) — +3 BASIC (split into 4 banks)
   - `/MACHINES/NEXT/enNxtmmc.rom` (8 KB) — DivMMC firmware
   - `/MACHINES/NEXT/enNextMf.rom` (8 KB) — Multiface firmware (Wave 1)

The runtime SPI/SD path (`src/peripheral/sd_card.cpp`) is independent
from the host-side extractor — it serves Z80 software at runtime via
block-level access. Wave 1 adds a Multiface ROM read of the same SD
image at init time.

### ZX Spectrum Next boot assets

jnext needs a NextZXOS SD image. Mount one explicitly via `--sdcard`, or omit
it and let jnext fall back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img` (the
patched image; offering to download the canonical distribution image, kept as
`cspect-next-1gb.img`, and produce that FAT32-patched copy — see Task 27).

**`roms/` holds ONLY `nextboot.rom`.** No SD-card image lives there, and
nothing links one in — the two 1 GB `.img` fixtures that used to sit there
were deleted 2026-07-22 (GH #75/#77). The canonical image is the one jnext
provisions and caches for itself at
`~/.jnext/sdcard/cspect-next-1gb-fixed.img`; every suite resolves it from
there, and no test row passes `--sdcard` at all.

A cluster-count caveat still applies to any image you supply yourself: a
1 GB partition with 32 KB clusters yields only 32 758 data clusters, below
the FAT32 spec minimum of 65 525, so tbblue.fw's FatFs (correctly, per spec)
rejects it as "not an FAT filesystem" (see `project_nextzxos_task9_stagec.md`
in memory for the full trace). CSpect's built-in SD driver tolerates the
under-clustered variant; ours doesn't, and there is no reason to relax it —
firmware-faithful is the right posture. `tools/fix-sdcard-image.sh` re-clusters
such an image.

**jnext opens the SD image read-write and persists guest writes**, so a run
that boots NextZXOS mutates it. Test runs are isolated — the regression suite
and `make unit-test` each clone the master per run — but a MANUAL run is not:
give it its own copy (`cp --reflink=auto`) whenever the result has to be
reproducible. Typical boot invocation:

```bash
./build/jnext --machine next
```
