# 5.4 Continuous integration

There are three workflows. `ci.yml` tests, and runs on every push to `main` and
every pull request. `release.yml` builds and publishes packages, and runs on a
`v*` tag push. `macos-build.yml` builds, packages and verifies the macOS `.dmg`
on demand — it has only a `workflow_dispatch` trigger, so it never runs by
itself; it exists so that the macOS leg can be exercised without having to cut
a release to do it.

## The hard rule: every step is a plain make target

The organising idea behind `ci.yml` is worth stating before the details,
because it is not how most projects are set up. **Every `run:` in the workflow
is a plain make target — the same one a human types locally.** There is no
CI-only build script, no YAML that knows how to do something the Makefile
cannot. The purpose is that CI and a local run can never quietly diverge: if
the suite is green on your machine it is green for the same reasons it will be
green on the runner, and when it goes red you reproduce it by typing the target
from the log.

The rule has a corollary for when CI seems to need something the local flow
lacks. The answer is almost never a CI-only step. Either the project is missing
a target — in which case add it to the Makefile, so local runs get it too — or
the need is imaginary. There is exactly one declared exception, the Flatpak job
in `release.yml`, and it is written down both in that job's comment and in
`doc/RELEASE-PROTOCOL.md` so that it stays a decision rather than becoming
drift.

The second corollary is **never pipe a build or test command**. GitHub's
default `bash -e` does not set `pipefail`, so a piped step takes the *pipe's*
exit status, and a failing `make` therefore reports success. That is not a
hypothetical worry: a run here once printed `62 pass, 1 fail` and
`UNIT TESTS FAILED` in bold, and went green.

## Why a Fedora container

Three of the five `ci.yml` jobs — `test`, `qt5-guard` and `package` — run in a
pinned Fedora container: the same image `release.yml` builds the rpm and Windows
artifacts in, and the same distribution family the maintainer develops on. (The
remaining two cannot: `macos` needs a `macos-latest` runner, and `flatpak` runs
in the Flathub `flatpak-github-actions` image, because that is where
`flatpak-builder` and the KDE SDK live.)

Matching the maintainer's distribution is load-bearing rather than cosmetic. The
man page, `USAGE.md`, the rendered user guide and the rendered developer guide —
including its Graphviz SVGs — are all generated and committed, and pandoc,
mkdocs-material and graphviz all emit byte-different output from one version to
the next. A runner on a different distribution therefore reports a *version gap*
as staleness, which is exactly what happened: a runner whose distribution
shipped an older pandoc than the maintainer's failed `docs-check` on every run
from the day the man page landed. Matching the distribution removes the whole
class of problem, instead of pinning tool by tool and waiting for the next one.

## The jobs in `ci.yml`

**`test`** is the main job. It installs the dependency set, checks out the
repository with submodules, and then clones the ZX Next FPGA core — sparsely, at
a pinned commit, into the runner's temp directory outside the workspace. That
clone is needed because the traceability generator validates every VHDL citation
against the real source tree, and produces different bytes without it. The job
then runs, in order: `make clean && make gui-release`, `make build-matrix`,
`JNEXT_TEST_JOBS=4 make regression`, and `make unit-test`. On failure it uploads
the screenshot diffs and the test summary.

That order is not arbitrary. `make regression` self-provisions the SD image as
one of its own rows, and `make unit-test` needs the same image for
`sd_rom_extractor_test` — running regression first means the image is already
there when the unit tests ask for it.

**`qt5-guard`** runs `make qt5-guard-build`, which is the same GUI and debugger
sources compiled against native Linux Qt5. Nothing else in CI compiles that
combination, so a Qt6-only API creeping into the GUI would otherwise surface
only at release time, inside the MinGW cross job, which is a slow and confusing
place to find out. It is build-only by design.

**`package`** runs `make package-test` in its own parallel job, because it is
roughly four minutes of package builds using a toolchain the test job has no
other use for. One detail matters here: in CI a missing packaging tool is a
**FAIL, not a SKIP**, so a row that has quietly stopped running cannot read as
a pass.

**`macos`** runs `make package-macos` on a real `macos-latest` runner, and
**`flatpak`** actually builds the bundle inside the KDE runtime container. Both
exist because a platform whose failures are invisible until release time fails
too late to be useful — the v0.98.34 `.dmg` referenced Homebrew paths and
aborted in dyld on every Mac except the one that built it. Neither job is
`continue-on-error`, since going red when the thing stops working is the entire
point of having them.

## The SD-card image is provisioned, never faked

`roms/` is git-ignored, so a fresh checkout has no NextZXOS SD image, and
jnext's harness treats a missing image as a fatal fault rather than as a silent
skip. Either `make unit-test` or `make regression` will therefore fail outright
without one; there is no honest way to run most of the suite and still call the
job green.

So CI runs **jnext's own provisioner** — the same code path an end user reaches
with `--sdcard-download-confirm`, downloading and FAT32-patching the official
public NextZXOS distribution image into its default location, cached on the
runner. Reimplementing that download in YAML would be exactly the mistake the
make-target rule exists to prevent. It also has a second benefit: because the
image is always the pristine distribution copy rather than a locally
contaminated fixture, every declared regression row runs, including the one
whose reference screenshot is an SD-card directory listing.

## The tag-gated release workflow

`release.yml` triggers on a `v*` tag push. Its first job, **`gate`**, reads
`releases.yaml` **from the tag's own commit** and decides whether this tag is
public — the allowlist described in [5.3](03-packaging-and-release.md). When it
is not, nothing builds, and the tag remains a private history marker.

When it is, six artifact jobs run. `deb` is a matrix over two pinned Ubuntu LTS
containers, one per supported release, because `dpkg-shlibdeps` bakes the
container's own library package names into the dependency list, so a deb built
in one release's container is uninstallable on the next. `rpm` builds in the
pinned Fedora container, so that its dependencies are Fedora-native. Then `src`,
`windows` (three zips from one Fedora container), `flatpak`, and `macos`.
Finally **`publish`** attaches everything to a GitHub Release, gated on
`if: success() && needs.gate.outputs.publish == 'true'`.

That explicit `success()` is what makes a failed artifact job withhold the
release rather than silently omit a platform. **Every artifact job is blocking**
as of v0.99.110. The accepted cost is that a hiccup on one platform holds up the
others, and it is accepted because a release should either cover the platforms
it claims to cover or stop and say so. Shipping a deliberately partial release
is still possible — it means restoring `continue-on-error` on that one job in
that one commit, which makes it an explicit and visible decision each time
rather than a standing one.
