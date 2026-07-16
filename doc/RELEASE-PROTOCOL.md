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
| `make bump-patch` | After **every** merge to `main` (per the merge protocol) | **No** — private history tag | **Never** |
| `make bump-minor` | A deliberate release with new user-facing features | Your choice | **Asks** you `y/N` |
| `make bump-major` | A deliberate release with breaking / milestone changes | Your choice | **Asks** you `y/N` |

All three, in order:
1. Refuse if the working tree is dirty (commit/stash first).
2. Compute the new version and write `version.yaml`.
3. Run `packaging/sync-version.sh <newver>` to propagate the version into every
   hand-maintained packaging file (§3).
4. **`bump-minor`/`bump-major` only:** prompt
   `Add vX.Y.0 to releases.yaml (build a public release)? [y/N]`. On `y`, add the
   new tag to `releases.yaml` **now, before tagging**, so the tag's commit lists
   itself. On `N` (or a non-interactive shell), leave `releases.yaml` untouched.
5. Stage `version.yaml`, the synced packaging files, and (if added)
   `releases.yaml`; commit `chore: bump version to <newver>`; create tag
   `v<newver>`.

Every step is `&&`-chained — if the sync fails, **nothing is committed or
tagged** (no half-synced release).

---

## 3. Keeping packaging files in sync — `packaging/sync-version.sh`

`version.yaml` is the source of truth; this script writes the version into the
files a person maintains by hand (the CPack path needs none of this):

- `packaging/rpm/jnext.spec` — `Version:` + a matching `%changelog` entry
- `packaging/flatpak/*.jnext.yml` — the `tag: vX.Y.Z` the build checks out
- `packaging/assets/*.metainfo.xml` — the AppStream `<releases>` history
- `packaging/debian/changelog` — the Debian changelog

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
| `make package-win` | Windows `.zip` — MinGW cross-build, Qt6/SDL2/SDL3 DLLs + `qwindows` plugin bundled by `packaging/windows/bundle-dlls.sh` (`build/gui-release-win/`) |
| `make package-flatpak` | Flatpak bundle (needs `flatpak-builder` + `org.kde.Sdk//6.8`) |
| `make package-macos` | macOS `.dmg` (Darwin only) |
| `make package-test` | build every package (except macOS) and assert each artifact |

CI runs these same targets — one build path, no CI-only divergence (§5).

---

## 5. CI / CD workflows

- **`.github/workflows/ci.yml`** — tests. Triggers on **push to `main` and PRs**
  (not on tags). Runs the full triplet (unit + FUSE + screenshot regression),
  self-provisioning the SD image.

- **`.github/workflows/release.yml`** — one tag-triggered workflow (it replaced
  the old `packaging.yml` + `release.yml` `workflow_run` hand-off). Triggers on
  a **`v*` tag push**. Jobs:
  1. **`gate`** — reads `releases.yaml` from the tag's own commit and checks
     whether `$GITHUB_REF_NAME` is listed → outputs `build` / `publish`.
  2. **`linux` / `rpm` / `src` / `windows` / `flatpak` / `macos`** — run only
     when `build == true`. `linux` builds the DEB via CPack on Ubuntu (so its
     shlibdeps are Debian-native); `rpm` builds the RPM via CPack inside a
     `fedora:44` container (so its deps are Fedora-native, not the Ubuntu
     `CURL_OPENSSL_4` libcurl node); `src` runs `make package-src` to emit the
     submodule-aware `jnext-<ver>-src.zip`; Windows via `make package-win` in a
     `fedora:44` container; `flatpak` via `flatpak-builder` in the KDE 6.8
     container (`continue-on-error`); macOS native on `macos-latest`
     (`continue-on-error`, UNTESTED — never blocks a Linux+Windows release).
     Each uploads its packages as an artifact.
  3. **`publish`** — runs only when `publish == true`; downloads the artifacts
     and creates a **GitHub Release**.

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

- The **Flatpak** build checks out the app from its git tag, so a Flatpak
  release requires that tag to be pushed to the origin.

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
