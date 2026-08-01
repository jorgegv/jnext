# 5.4 Continuous integration

Two workflows. `ci.yml` tests, on every push to `main` and every pull request.
`release.yml` builds and publishes packages, on a `v*` tag push.

## The hard rule: every step is a plain make target

Every `run:` in `ci.yml` is a target a human types locally. CI and local must
never diverge. If CI appears to need something the local flow lacks, the answer
is almost never a CI-only step: either the project is missing a target — add it
to the Makefile, so local runs get it too — or the need is imaginary. There is
exactly one declared exception, the Flatpak job in `release.yml`, written down
in that job's comment and in `doc/RELEASE-PROTOCOL.md` so it stays a decision
rather than drift.

The corollary is **never pipe a build or test command**. GitHub's default
`bash -e` does not set `pipefail`, so a piped step takes the *pipe's* exit
status and a failing `make` reports success. That is not hypothetical: a run
here once printed `62 pass, 1 fail` and `UNIT TESTS FAILED` in bold and went
green.

## Why a Fedora container

Four of the five `ci.yml` jobs run in `container: fedora:44` — the same image
`release.yml` builds the rpm and Windows artifacts in, and the distro the
maintainer develops on. This is load-bearing rather than cosmetic. The man page,
`USAGE.md`, the rendered user guide and the rendered developer guide (including
its Graphviz SVGs) are all generated and committed, and pandoc, mkdocs-material
and graphviz emit byte-different output across versions. A runner on a
different distro therefore reports a *version gap* as staleness: Ubuntu shipped
pandoc 3.1.3 against the maintainer's 3.7.0.2 and failed `docs-check` on every
run from the day the man page landed. Matching the distro removes the whole
class instead of pinning tool by tool.

## The jobs in `ci.yml`

**`test`** is the main one. It installs the dependency set, checks out the
repository with submodules, then clones the ZX Next FPGA core — sparsely, at a
pinned commit, into the runner's temp directory outside the workspace — because
the traceability generator validates every VHDL citation against the real
source tree and produces different bytes without it. It then runs, in order:
`make clean && make gui-release`, `make build-matrix`,
`JNEXT_TEST_JOBS=4 make regression`, and `make unit-test`. On failure it
uploads the screenshot diffs and the test summary.

That order matters. `make regression` self-provisions the SD image as one of
its own rows, and `make unit-test` needs the same image for
`sd_rom_extractor_test`, which finds it already there.

**`qt5-guard`** runs `make qt5-guard-build`: the same GUI and debugger sources
compiled against native Linux Qt5. Nothing else in CI compiles that
combination, so a Qt6-only API creeping into the GUI would otherwise surface
only at release time, inside the MinGW cross job. Build-only by design.

**`package`** runs `make package-test` in its own parallel job — roughly four
minutes of package builds with a toolchain the test job has no other use for.
In CI a missing packaging tool is a **FAIL, not a SKIP**, so a row that quietly
stopped running cannot read as a pass.

**`macos`** runs `make package-macos` on a real `macos-latest` runner, and
**`flatpak`** actually builds the bundle in the KDE runtime container. Both
exist because a platform whose failures are invisible until release time fails
too late — the v0.98.34 `.dmg` referenced Homebrew paths and aborted in dyld on
every Mac but the builder's. Neither is `continue-on-error`: the entire point is
to go red when the thing stops working.

## The SD-card image is provisioned, never faked

`roms/` is git-ignored, so a fresh checkout has no NextZXOS SD image, and
jnext's harness treats a missing image as a fatal fault rather than a silent
skip. Either `make unit-test` or `make regression` therefore fails outright
without one — there is no honest way to run most of the suite and still call
the job green.

So CI runs **jnext's own provisioner**: the same code path an end user hits
with `--sdcard-download-confirm`, downloading and FAT32-patching the official
public NextZXOS distribution image into its default location, cached on the
runner. Reimplementing that download in YAML would be exactly the mistake the
make-target rule exists to prevent — and because the image is always the
pristine distribution copy rather than a locally-contaminated fixture, every
declared regression row runs, including the one whose reference screenshot is
an SD-card directory listing.

## The tag-gated release workflow

`release.yml` triggers on a `v*` tag push. Its first job, **`gate`**, reads
`releases.yaml` **from the tag's own commit** and decides whether this tag is
public. If it is not, nothing builds and the tag stays a private history
marker.

When it is, six artifact jobs run: `deb` (a matrix over pinned `ubuntu:24.04`
and `ubuntu:26.04` containers, because `dpkg-shlibdeps` bakes in the
container's library package names and a single 24.04-built deb is
uninstallable on 26.04), `rpm` in `fedora:44`, `src`, `windows` (three zips
from one Fedora container), `flatpak`, and `macos`. Finally **`publish`**
attaches everything to a GitHub Release, gated on
`if: success() && needs.gate.outputs.publish == 'true'`.

That explicit `success()` is what makes any failed artifact job withhold the
release instead of silently omitting a platform. **Every artifact job is
blocking** as of v0.99.110 — the accepted cost is that a hiccup on one platform
holds up the others, because a release either covers the platforms it claims or
it stops and says so. Taking a deliberately partial release means restoring
`continue-on-error` on that one job in that one commit: an explicit, visible
decision each time, never a standing one.
