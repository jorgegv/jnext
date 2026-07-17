# Static Binary Plan — LTO-clean Flatpak via a self-built static jnext

> **Status:** design note / future work (2026-07-17). Not implemented. Captures
> the analysis behind *why* a fully-static, self-compiled binary is the right way
> to get LTO back on the Flatpak build, and how to do it without reintroducing the
> Flatpak-SDK LTO bug. See also [../FLATPAK-LTO-PROBLEMS.md](../FLATPAK-LTO-PROBLEMS.md)
> (the shipped LTO-off workaround) and `project_flatpak_sigsegv_lto_rootcause.md`
> in auto-memory.

---

## 1. Problem recap

The published Flatpak SIGSEGVs at GUI launch when built with whole-program LTO
under the `org.kde.Sdk` toolchain. The shipped fix (v0.98.26) disables LTO for
the Flatpak build only (`-DJNEXT_ENABLE_LTO=OFF` in the Flatpak manifest);
native / DEB / RPM / `gui-release` keep full LTO. Cost of the workaround:

- **~32 % headless/turbo throughput** (171 → 117 fps headless turbo on the dev box).
- **0 %** at normal interactive speed (100 %). Normal play is unaffected.

So the *user-visible* payoff of recovering LTO on Flatpak is **small and
turbo-only**. This whole plan is worth doing only if batch/turbo/headless
throughput matters. It is **not** urgent.

---

## 2. The key insight: runtime ≠ toolchain, and the bug is in *compiling jnext*

Two facts that reframe every "just build Qt ourselves" idea:

1. **Flatpak compiles inside the Flatpak build sandbox, with the SDK's compiler
   — not your host's.** `flatpak-builder` downloads the runtime
   (`org.kde.Platform`) *and* the SDK (`org.kde.Sdk`), spins up a sandbox on the
   SDK (the "base image", Docker-analogy), and builds every module
   (`buildsystem: cmake-ninja`, our `jnext` and `sdl2` modules) with the **SDK's
   GCC/binutils/cmake**. Your Fedora `/usr/bin/gcc` never touches it.

2. **`org.kde.Sdk` is built *on top of* `org.freedesktop.Sdk`** (of the matching
   year), inheriting the same base GCC. GNOME's SDK likewise. So there is **no
   generic peer SDK with a different compiler** — switching KDE → Freedesktop
   gives you the *same* base GCC and, almost certainly, the *same* LTO bug. The
   generic base SDK for "a program + some library, no Qt" is
   `org.freedesktop.Platform` / `org.freedesktop.Sdk` — a real, standard Linux
   dev environment — but it is **Freedesktop's own toolchain, pinned per release,
   not Fedora's**, and it shares GCC with the KDE SDK.

3. **The miscompilation is in *jnext's* code compiled under LTO, not in Qt.** Qt
   (KDE-runtime, self-built, or Docker-precompiled) is a *shared library* linked
   at the end; our whole-program LTO runs across *jnext's* translation units. The
   crash was bisected to jnext's `QApplication`-init path as compiled by the SDK
   GCC under LTO (a `QApplication`-first-in-`main()` probe still crashed →
   pre-`main`/build cause; per-target LTO bisection — per-lib, exe-only,
   libs-only, `-fno-devirtualize`, `-ipa-icf`, `-O2` — all still crashed).

   **Consequence:** where Qt comes from is a red herring. Self-building or
   Docker-precompiling Qt changes *which Qt you link*, not *which compiler
   compiles jnext*. To escape the bug you must change **the compiler that
   compiles jnext**.

The only things that actually change that compiler:

- **A different SDK version** (newer `org.kde.Sdk`/`org.freedesktop.Sdk` whose GCC
  fixes the LTO defect) — cheapest experiment, one-line manifest bump.
- **The Clang/LLVM SDK extension** (`org.freedesktop.Sdk.Extension.llvm<NN>`) —
  build jnext with clang → genuinely different compiler, and it unlocks
  **ThinLTO** (Clang-only). Real "keep LTO inside Flatpak" lever.
- **Leaving the Flatpak build model** — compile jnext with *your* toolchain
  (Docker/Fedora GCC) and wrap the resulting binary. This is the static-binary
  plan below.

---

## 3. Verification status of the "SDK LTO bug" claim (HONEST)

We do **not** have external verification. What exists:

- **Known (internal, reproducible):** jnext + whole-program LTO + `org.kde.Sdk`
  → SIGSEGV; `-DJNEXT_ENABLE_LTO=OFF` fixes it; native Fedora GCC + LTO is fine.
- **Inferred, NOT proven:** that it's the *Freedesktop* SDK (we only tested KDE),
  and that it breaks LTO for *any* program (we only saw *this* Qt app break). No
  minimal reproducer was built; no upstream issue was filed or found.

So "the SDK GCC miscompiles under LTO" is our **working hypothesis + workaround**,
not an externally-referenced fact. If a real reference is ever needed, it must be
*created*: minimal Qt-LTO reproducer in the SDK → search/file on the
**freedesktop-sdk** tracker and/or **GCC Bugzilla**.

**Why this plan doesn't need that verification:** the Docker path below uses a
Fedora-class GCC that *already* builds jnext LTO-clean today. So it sidesteps the
unknown by construction — it works whether or not the SDK bug is a true compiler
defect. That de-risks the whole plan.

---

## 4. Can a locally-built (prebuilt) binary go inside a Flatpak? YES.

Correcting a common misconception: the Flatpak *model* allows prebuilt binaries.
A Flatpak is just a filesystem tree + metadata; `flatpak-builder` accepts a
prebuilt binary as a source (`type: file`/`archive`) with a `buildsystem: simple`
module that copies it into the prefix. Many proprietary Flathub apps (Zoom,
Spotify, …) are exactly this. Two real constraints — **neither is "the model
forbids it":**

1. **ABI matching.** A Flatpak still runs against *some* runtime's
   glibc/libstdc++. A partially-dynamic binary built against a newer glibc can
   mismatch the runtime's. **Fully-static third-party libs + building against a
   glibc ≤ the runtime's** removes this. (Fully-static *glibc* is discouraged —
   breaks NSS/`dlopen`; normal practice is static Qt/SDL/libs, dynamic glibc,
   built against an old-enough base.)
2. **Flathub policy** (distinct from the technology): Flathub prefers/requires
   build-from-source for FOSS and scrutinizes prebuilt blobs. **This only applies
   if you target Flathub.** Self-hosting your own Flatpak repo (e.g. as a GitHub
   Releases asset — jnext's *current* channel) has no such rule.

---

## 5. The plan (for jnext's actual channel: self-hosted on GitHub Releases)

jnext ships its Flatpak as a **GitHub Releases asset, not on Flathub**. Therefore
the Flathub build-from-source constraint does **not** apply, and the
"clone-repo-and-build-in-Flatpak" step is both unnecessary *and* the one that
reintroduces the LTO bug. The clean pipeline:

```
Docker image  (Fedora-class GCC; static Qt + SDL + deps prebuilt, cached)
     │  LTO on, known-good compiler  →  LTO-clean by construction
     ▼
static jnext  (STATIC_BUILD=ON; static Qt/SDL/libs, dynamic glibc vs old base)
     ▼
Flatpak       (buildsystem: simple — copy the prebuilt static binary into prefix)
     ▼
publish       (GitHub Releases asset, as today)
```

### Mutually-exclusive with Flathub-source-compliance
You can have **Flathub source-compliance** *or* **LTO-via-your-own-compiler**, not
both — unless you instead fix the compiler *inside* Flatpak (SDK bump / clang
ThinLTO, §2). Since jnext self-hosts, choose the prebuilt-static path.

### Building blocks (not yet created)
- **`packaging/static/Dockerfile`** — a cached image that fetches Qt + SDL2
  sources, builds them **static**, on a Fedora-class GCC that does LTO cleanly.
  The ~2 h Qt build is paid once; the image is the cache. Could be published to a
  registry (Docker Hub / GHCR) so CI pulls instead of rebuilding.
- **`STATIC_BUILD` CMake path** — already scaffolded (Phase 6). Needs a static
  Qt/SDL to link against, plus static-plugin wiring (see below).
- **Flatpak manifest variant** — `buildsystem: simple`, one `file` source (the
  static binary), `build-commands: install -D … /app/bin/jnext`, plus the
  `.desktop`/metainfo/icon install. Runtime can be a minimal
  `org.freedesktop.Platform` (Qt no longer needed from the runtime).

### The real work item
**Static Qt plugins.** Qt platform plugins (`xcb`, `wayland`) and image-format
plugins are `dlopen`'d at runtime by default. A static Qt needs them
`Q_IMPORT_PLUGIN`'d and statically configured (a `Q_IMPORT_PLUGIN(QXcbIntegration
Plugin)` + linking the static plugin libs, `qInitResources`-style). This is the
fiddly part — well-trodden, but not "download and `make`".

### Flatpak vs AppImage once static
Once jnext is a fully-static blob, **Flatpak and AppImage are near-equivalent** —
both just wrap-and-sandbox a prebuilt static binary. Keep Flatpak for its
sandbox/desktop integration and the existing GH asset format, or switch to
AppImage for less ceremony. Either is fine; the static binary is the deliverable
that matters.

---

## 6. Recommended order (cheap before expensive)

Given the payoff is turbo-only, try the cheap levers **before** committing to the
static-Qt project:

1. **Bump the SDK version** (e.g. a newer `org.kde.Sdk`) and re-enable LTO — the
   upstream GCC LTO defect may already be fixed. One-line manifest change, ~1 h.
2. **Clang + ThinLTO** via `org.freedesktop.Sdk.Extension.llvm<NN>` — different
   compiler, ThinLTO is Clang-only; may dodge the GCC codegen bug entirely with
   far less effort than static Qt.
3. **Only if 1 & 2 fail and turbo throughput genuinely matters** → build the
   Docker static image + static jnext + prebuilt-blob Flatpak (this document).

---

## 7. TL;DR

- The Flatpak crash is a **compiler** problem (SDK GCC + LTO on *jnext's* code),
  not a runtime or Qt problem — **not externally verified**, only reproduced +
  worked around.
- Building Qt ourselves changes nothing inside Flatpak, because jnext is still
  compiled by the SDK GCC. Only changing *that compiler* helps.
- You **can** wrap a locally-built static binary in a Flatpak; the model allows
  it. Flathub's source policy would forbid it, but jnext self-hosts on GitHub, so
  that constraint doesn't apply.
- Clean path: **Docker (static libs, good GCC, LTO on) → static jnext → simple
  prebuilt-blob Flatpak → GH Releases.** It's LTO-clean *by construction* and
  needs no root-cause of the SDK bug.
- Try the SDK-bump and Clang-ThinLTO one-liners first; the static project is the
  fallback, and its payoff is turbo-only.
