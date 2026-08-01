# 4.4 Traceability

`doc/testing/TRACEABILITY-MATRIX.md` maps every test row to what it proves, the
VHDL line that justifies it, its live status, and where the assertion lives. It
is **generated** by `test/refresh-traceability-matrix.pl` and **gated** the same
way the man page is: `make traceability-check` — a prerequisite of
`make unit-test` — regenerates it and fails when the committed copy differs. The
generator is idempotent, so regenerate-and-diff is an exact staleness test. It
needs a built test tree, because a row's `Status` comes from actually running
its suite; that is why it could not exist while the refresh was a manual
version-bump step.

## Where each column comes from

- **Test ID and description** — the row's own `check("ID", "description", ...)`
  or `skip("ID", "reason")` call. The description position varies between
  suites, so the generator reads each file's own declaration of `check()` rather
  than assuming an argument order.
- **Status** — `fail` if the binary printed the row in its FAIL set; otherwise
  `skip` for a `skip()` call, `pass` for a `check()` call, and `missing` if the
  ID appears in no source at all. A section resolves an ID against its own
  source files first, then against the other suites of the same `##` subsystem;
  the widening stops dead at that boundary.
- **VHDL file:line** — recovered from **row-local** evidence, in tiers: the
  row's own call, a comment block naming the ID, the first call after a
  table-driven ID literal, and the plan doc's row. Every citation is validated
  against the real FPGA source tree.
- **`missing` rows** — planned-but-unimplemented rows, read from the
  subsystem's `*-TEST-PLAN-DESIGN.md`. An honest backlog, and the only remaining
  hand-made claim in the document.

Two citation tiers were prototyped and **rejected**: banner comments and the
nearest unrelated comment. Both attribute a neighbouring row's VHDL lines to
this one, and a plausible-but-wrong citation is worse than an honest `—`.

## Editing it by hand does nothing

Nothing in the file is hand-written. An edit is overwritten by the next run, and
fails the gate in the meantime. That is why the older bookkeeping-drift classes
no longer exist — there is no hand-written side left to disagree with. The one
report that survives is a plan doc and a test source citing *different* VHDL for
the same row, which is signal about the spec rather than about bookkeeping.

## A row ID must be a literal

Building one at run time — `check((std::string(c.id) + "-35").c_str(), ...)` —
emits a row no source reader can see. When that happened, the matrix carried two
IDs that were not rows and none of the six that were. **Spell every ID out.**

## An ID is a global name

`make unit-test` runs `test/traceability-dup-ids.pl`, which **refuses** (exit 2)
when two suites assert the same ID. That reuse is how manufactured coverage
happened once: a row read `pass` because an identically-named row in another
subsystem was vouching for it. The 29 collisions that already existed are
baselined in `test/traceability-dup-ids.conf`; anything new fails, and the
baseline shrinks only by renaming one side — plan doc and test source together.

It enumerates suites from `test/unit-tests.conf` — all 90 — and deliberately not
from the matrix's sections, because 49 suites are tombstoned and have no section
at all, so a matrix-derived audit cannot see them.

## The exceptions file

`test/traceability-exceptions.conf` is the **single** hand-maintained input:
three `|`-separated fields per record — section, row ID, description. It exists
for the one case neither a test source nor a plan doc can answer for, a planned
row of a suite that has *no* plan doc because it has no VHDL counterpart. Two
suites are in that position, `rewind_test` and `sdcard_test`, tombstoned as
`(jnext-internal)` and `(SD SPI spec)`; without this file their backlog would
simply vanish when the matrix became generated.

It is parsed strictly: a malformed record or a duplicate `(section, id)` is a
refusal, not a dropped row. It is not a place to assert coverage — every row in
it emits as `missing`, because nothing runs it.

## What the generator refuses to do

Its exit codes are a vocabulary, not decoration: `0` clean, `1` the matrix was
rewritten and still under-records rows, `2` refusal with nothing written, `3`
internal error.

- **It refuses (exit 2) when a declared suite is unaccounted for.** Every suite
  in `test/unit-tests.conf` must be either *traced* — mapped to a `##` section,
  the one genuinely editorial fact left — or *tombstoned* with a written reason.
  Anything else stops the run and the matrix is untouched. That replaced an
  advisory warning which had saturated: the traced-suite count sat at 28 for a
  whole release series while the manifest grew 49 → 80, because each of ~31
  additions arrived as one more name on a warning line that already listed
  fifty. `make traceability-accounting-check` runs just this half, in ~0.01 s
  with no build prerequisite.
- **It refuses to invent a row.** A row the tests assert but the matrix does not
  record is *reported*, never auto-added: the payload of a matrix row is its
  human-readable description, and the script has no honest source for one. A row
  whose only readable field is `—` would make the count look right while
  recording nothing, and would be self-consistent on every later run, so it
  would never surface again.
- **It refuses to guess a source path.** Suite sources are read from CMake, not
  from a naming convention: 80 suite names match their source basename and 7 do
  not, and the two ESP-01 suites live under `src/esp01/test/`. A convention that
  is right 92% of the time is the worst kind.
- **It leaves hand-written citations alone**, reporting disagreements rather
  than overwriting them, and leaves rows marked `<!-- protected -->`
  byte-identical.

The extractor is itself pinned by `make traceability-selftest` — see
[4.6](06-the-harness-selftest.md). After changing a test, run `make unit-test`:
it regenerates the matrix and fails if your committed copy is stale, so commit
the regenerated file with your change. If a row you touched now reads `missing`,
the fix is in the test source, not in the document.
