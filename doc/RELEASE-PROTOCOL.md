# JNEXT Release Protocol

This is the authoritative process for versioning, tagging, and publishing
JNEXT releases. **Read it whenever the user asks to "release" or "bump" a
version.** It complements — does not replace — the "Merging a completed
feature/fix to `main`" and "Version bumping" sections in
[CLAUDE.md](../CLAUDE.md).

---

## 1. Core concepts

- **`version.yaml` is the single source of truth for the version.** Everything
  else is derived from it. CMake reads it into `PROJECT_VERSION`, so every
  CPack-generated package (rpm/deb/tgz/zip/dmg) already carries the right
  version. The hand-maintained packaging files are kept in lockstep by
  `packaging/sync-version.sh` (see §3).

- **A git tag is NOT the same as a public release.** Every merge to `main`
  gets its own `vX.Y.Z` tag (per-merge patch bump). Those tags are *local
  history markers*. Only the small, curated subset listed in **`releases.yaml`**
  becomes a public GitHub Release.

- **`releases.yaml`** (repo root) is the allowlist that gates public releases:

  ```yaml
  # Tags listed here get a public GitHub Release built + published by CI when
  # the tag is pushed. Everything else is a private history tag.
  releases:
    - v0.99.0
    - v1.0.0
  ```

  The CI reads it **from the tag's own commit** — so a tag must already list
  *itself* in `releases.yaml` for CI to release it. `bump-minor`/`bump-major`
  handle that automatically (§2).

---

## 2. Versioning & tagging — `make bump-*`

| Target | When | Public release? | Touches `releases.yaml`? |
|--------|------|-----------------|--------------------------|
| `make bump-patch` | After **every** merge to `main` (per the merge protocol) | **Asks** you `y/N` (default No) | **Asks** you `y/N` |
| `make bump-minor` | A deliberate release with new user-facing features | **Asks** you `y/N` | **Asks** you `y/N` |
| `make bump-major` | A deliberate release with breaking / milestone changes | **Asks** you `y/N` | **Asks** you `y/N` |

All three, in order:
1. Refuse if the working tree is dirty (commit/stash first).
2. Compute the new version.
3. **All three** prompt `Add vX.Y.Z to releases.yaml (build a public GitHub
   Release)? [y/N]`. On `y`, add the new tag to `releases.yaml` **now, before
   tagging**, so the tag's commit lists itself (and `sync-version.sh` will add
   the version to the AppStream metainfo — §3). On `N` (or a non-interactive
   shell), leave `releases.yaml` untouched — a private history tag.
   **Before answering `y`, the ChangeLog MUST be up to date — see §2.1.**
4. Write `version.yaml`, then run `packaging/sync-version.sh <newver>` to
   propagate the version into every hand-maintained packaging file (§3).
5. Stage `version.yaml`, the synced packaging files, and (if added)
   `releases.yaml`; commit `chore: bump version to <newver>`; create tag
   `v<newver>`.

### 2.1 A public release MUST update the ChangeLog

Whenever a bump is going to be **public** (you answer `y` to the prompt above,
i.e. the tag is added to `releases.yaml`), the **`ChangeLog` MUST be updated
before the bump commit**, so the released tag carries its own ChangeLog entry.

- The new entry is **differential**: it describes only what changed **since the
  previous ChangeLog record** (the last public release), not the whole history.
- Follow the ChangeLog rules in `CLAUDE.md` (4 sections — User Features,
  Developer Features, Bug Fixes, Internal JNEXT Development; no trivial fixes;
  no commit IDs; coalesce similar items).
- Private patch bumps (answered `N`) do **not** require a ChangeLog entry.

**Every item is ONE line. No exceptions.** One bullet, one physical line in the
file, 10-20 words. If it wraps, it is too long — cut it, do not reflow it onto a
second line. Same for the section describing the headline feature: it gets one
line like everything else, not a paragraph.

```markdown
### User Features
- New nine-chapter user guide, shipped with the packages and readable offline   ← yes
- New jnext(1) man page installed by every package; USAGE.md generated from it   ← yes

- **New: a complete JNEXT user guide** — nine chapters covering everything      ← NO
  from first run to automation, shipped with the packages and readable offline
```

This is not formatting pedantry. The ChangeLog exists to give a USER a scannable
overview of what changed — not a development diary, not an exhaustive list, and
not a place to explain mechanism. Detail belongs in the issue, the commit
message or the design doc. A reader who wants the mechanism can follow the
issue number; a reader skimming twenty items cannot skim twenty paragraphs.

The failure mode is real and recent: the v0.98.71 entry shipped with
multi-line, three-clause bullets explaining *why* things happened, which is
precisely what this rule forbids.

Every step is `&&`-chained — if the sync fails, **nothing is committed or
tagged** (no half-synced release).

### 2.2 A public release MUST have its documentation checked

The generated-document gates (`docs-check`) prove only that the committed
outputs regenerate from their sources. They cannot tell you whether those
sources still describe the product. That seam has failed twice — five flags
once entirely undocumented, and v0.98.60 shipping a man page with a wrong scale
range, two missing GUI menus and a status-bar indicator that does not exist,
with every gate green. Both were found by a human reading the running product.

Before a public bump:

1. **Run `make cli-check` and fix anything it reports.** It diffs the flag table
   in `src/core/cli_options.h` against the OPTIONS section of
   `doc/man/jnext.1.md` in both directions (issue #43). `USAGE.md` is covered
   transitively — it is generated from the same source and `docs-check` fails if
   the committed copy has drifted. A failure here is a real disagreement between
   the shipped CLI and the shipped documentation; fix the code or the man page,
   never silence the check.

   Note what this does **not** cover: the man page's **prose** sections, and
   `print_usage()` (the `--help` text). Neither is checked by anything.

2. **Read the user documentation against the running product and update it.**
   The user guide sources live in `src/doc/user-guide` (rendered into the
   committed `doc/user-guide` by `make docs-userguide`). Check the chapters
   touching anything that changed this cycle — menus, panels, flags, defaults,
   on-screen indicators — **by running jnext and looking**, not by reading other
   docs. `doc/design/EMULATOR-DESIGN-PLAN.md` is a roadmap and is NOT a reliable
   source of product facts; writing the guide against it produced five wrong
   statements about the debugger alone.

   Re-render and **commit** both the source and the rendered output in the same
   change — `docs-userguide-check` (part of `docs-check`, a prerequisite of both
   `make unit-test` and `make regression`) fails the next test run otherwise.

Private patch bumps (answered `N`) do not require step 2, but `cli-check` runs
anyway as a prerequisite of `make regression`.

---

## 3. Keeping packaging files in sync — `packaging/sync-version.sh`

`version.yaml` is the source of truth; this script writes the version into the
files a person maintains by hand (the CPack path needs none of this):

- `packaging/rpm/jnext.spec` — `Version:` + a matching `%changelog` entry
- `packaging/assets/*.metainfo.xml` — the AppStream `<releases>` history
  (**public releases only** — `sync-version.sh` adds an entry only when the
  version is listed in `releases.yaml`, so private per-merge patch tags never
  appear here)
- `packaging/debian/changelog` — the Debian changelog

The flatpak manifest (`packaging/flatpak/*.yml`) is **not** in this list: it
builds from the local checkout (`type: git`, `path: ../..`, `branch: HEAD`)
and carries no version tag, so there is nothing for `sync-version.sh` to
rewrite.

It is idempotent, fails loud if a target file or its anchor is missing, and is
covered by `test/packaging/sync-version-test.sh` (run inside `make package-test`).
**When you add a new file that hard-codes the version, add it to
`sync-version.sh` too** — that script is the one place that must know them all.

---

## 4. Building packages — `make package-*`

| Target | Produces |
|--------|----------|
| `make package-src` | source tarball (vendors submodule content) |
| `make package-rpm` / `package-deb` | Fedora/RHEL `.rpm` / Debian/Ubuntu `.deb` (CPack) |
| `make package-win` | Windows `.zip` — MinGW cross-build, Qt6/SDL2/SDL3 DLLs + `qwindows` plugin bundled by `packaging/windows/bundle-dlls.sh` (`build/win-release/`) |
| `make package-flatpak` | Flatpak bundle (needs `flatpak-builder` + `org.kde.Sdk//6.8`) |
| `make package-macos` | macOS `.dmg` (Darwin only) |
| `make package-test` | build every package (except macOS) and assert each artifact |
| `make package-contract-test` | the packaging-**script** contract suites only — hermetic, ~4 s, no toolchain |

CI runs these same targets — one build path, with **exactly one declared
divergence**: the `flatpak` job in `release.yml` invokes
`flatpak/flatpak-github-actions/flatpak-builder@v6` instead of
`make package-flatpak`. It hands that action the *same* manifest and the same
`org.kde.Sdk` runtime version, and takes the runtime image, build cache and
bundle export from it; reimplementing that caching in YAML is the anti-pattern
the rule exists to prevent. Every other package is built by the plain `make`
target a developer runs locally. Do not describe this as "no divergence" — it
is one, it is deliberate, and it is written down here and in that job's comment
so it stays a decision rather than drift.

**Packaging correctness is gated automatically** (issue #61):

- `make package-contract-test` is a prerequisite of `make unit-test`, so the six
  packaging-script contract suites — including `verify-bundle`, the GH #46 gate
  — run on every local test run and every CI push.
- The full `make package-test` runs as its own parallel `package` job in
  `ci.yml` on every push to `main` and every PR. It is deliberately **not**
  duplicated into `release.yml`: by convention a release tag is cut from a
  commit that has already landed on `main`, so that commit has already had its
  packaging asserted by this job. Note that is a **process discipline, not a
  technical guarantee** — the `bump-*` targets do not check the current branch,
  so a tag cut from a commit that never reached `main` would bypass it. If that
  ever stops being merely theoretical, either gate `bump-*` on the branch or
  add a `package-test` job to `release.yml`.
- In CI a **missing packaging tool is a FAIL, not a SKIP**
  (`skp_ci_fail` in `test/packaging/packaging-test.sh`), so a row that quietly
  stopped running cannot read as a pass. Flatpak is the one deliberate SKIP
  there: it needs a multi-GB privileged `org.kde.Sdk` install.

---

## 5. CI / CD workflows

- **`.github/workflows/ci.yml`** — tests. Triggers on **push to `main` and PRs**
  (not on tags). Two jobs: **`test`** runs the full triplet (unit + FUSE +
  screenshot regression), self-provisioning the SD image; **`package`** runs
  `make package-test` in a `fedora:44` container, building every package and
  asserting its contents. They run in parallel, so packaging costs nothing on
  the critical path.

- **`.github/workflows/release.yml`** — one tag-triggered workflow (it replaced
  the old `packaging.yml` + `release.yml` `workflow_run` hand-off). Triggers on
  a **`v*` tag push**. Jobs:
  1. **`gate`** — reads `releases.yaml` from the tag's own commit and checks
     whether `$GITHUB_REF_NAME` is listed → outputs `build` / `publish`.
  2. **`deb` / `rpm` / `src` / `windows` / `flatpak` / `macos`** — run only
     when `build == true`. `deb` is a **matrix** over `ubuntu:24.04` and
     `ubuntu:26.04` pinned containers: CPack's `dpkg-shlibdeps` bakes in the
     container's library package names, so each LTS gets its own deb (the 24.04
     `t64` names are unsatisfiable on 26.04), renamed
     `jnext_<ver>_ubuntu<rel>_amd64.deb` and shipped as two assets; `rpm` builds
     the RPM via CPack inside a
     `fedora:44` container (so its deps are Fedora-native, not the Ubuntu
     `CURL_OPENSSL_4` libcurl node); `src` runs `make package-src` to emit the
     submodule-aware `jnext-<ver>-src.zip`; Windows via `make package-win` in a
     `fedora:44` container; `flatpak` via `flatpak-builder` in the KDE 6.10
     container (`continue-on-error` until its first green run on a real
     runner — the one declared divergence from the make-target rule, §4);
     macOS native on `macos-latest` via `make package-macos`, **no longer
     `continue-on-error`** since issue #61 (it carries the GH #46
     `verify-bundle` gate, and a gate that cannot fail anything is not a gate).
     Each uploads its packages as an artifact.
  3. **`publish`** — `if: success() && needs.gate.outputs.publish == 'true'`;
     downloads the artifacts and creates a **GitHub Release**. The explicit
     `success()` is what makes a failed `macos` job withhold the release rather
     than silently omitting a platform; a failed `flatpak` still publishes,
     because a `continue-on-error` job reports `success` to `needs`
     (actions/toolkit #1739). **The macos-failure half is verified on a real
     runner**: `workflow_dispatch` run 29917667433 (2026-07-22) broke `macos`
     deliberately and `publish` was skipped, gate/src/rpm/deb/windows/flatpak
     all green. **The flatpak-failure half is still unverified** — `flatpak`
     *succeeded* in that run, so the "a red `flatpak` alone still publishes"
     path has never actually been exercised; confirm it with a
     `workflow_dispatch` run that breaks only `flatpak` and checks `publish`
     still runs.

  So a tag **not** in `releases.yaml` → the gate says "private tag", nothing
  builds. A **`workflow_dispatch`** run builds all packages for testing but
  never publishes.

---

## 6. Pushing (read before every push)

- **Never `git push` without explicit user authorization.** Local commits,
  merges, and bump tags stay local until the user says "push". (CLAUDE.md rule.)

- **GitHub only fires tag events for ≤ 3 tags pushed at once.** If you push
  more than three tags in one operation, GitHub creates **zero** tag events and
  **nothing builds**. Push the release tag(s) **individually** (or ≤ 3 at a
  time). Plain `git push origin main` does not carry tags — you push tags by
  name.

- A tag that is **not** in `releases.yaml` builds/releases nothing even if
  pushed — harmless, but pointless. Push the branch, then push only the
  release tag(s) you actually intend to publish.

- The **Flatpak** build builds the app from the local checkout (`type: git`,
  `path: ../..`, `branch: HEAD`), not a pushed git tag, so it does **not**
  require the release tag to be on origin. In CI, `actions/checkout` leaves the
  release tag checked out and flatpak-builder builds exactly that tree.

---

## 7. Release checklist — when the user says "release vX.Y.Z"

1. **Green triplet** on `main`: `make unit-test`, FUSE suite, `make regression`
   — no FAIL (per CLAUDE.md "Version bumping").
2. Update the traceability matrix, unit-test status report,
   `doc/DEVELOPMENT-SESSIONS.md`, and the ChangeLog (to the future version).
   Commit those.
3. `make bump-minor` (or `bump-major`) and answer the
   `Add … to releases.yaml?` prompt **`y`** — this is the step that makes it a
   public release.
4. Get the user's explicit **"push"** authorization.
5. Push `main`, then push the single release tag (`git push origin vX.Y.Z`).
6. CI sees the tag is in `releases.yaml` → builds all packages → publishes the
   GitHub Release. (Patch bumps skip all of this by design.)
