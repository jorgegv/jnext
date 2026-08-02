# 4.4 Traceability

The emulator's specification is the VHDL of the FPGA core, so a test row is
only meaningful if you can say which lines of that VHDL it was derived from.
The traceability matrix is where that link is written down. For every test row
in the project, `doc/testing/TRACEABILITY-MATRIX.md` records what the row
proves, the VHDL line that justifies it, its live status, and where the
assertion lives. Read one way it is a coverage map; read the other way it is
the honest list of what nothing yet asserts.

Crucially, it is **generated** rather than written — `test/refresh-traceability-matrix.pl`
builds it out of the test sources themselves — and it is **gated** exactly the
way the man page is. `make traceability-check`, a prerequisite of
`make unit-test`, regenerates the matrix and fails when the committed copy
differs. The generator is idempotent, so regenerate-and-diff is an exact
staleness test. It needs a built test tree, because a row's `Status` comes from
actually running its suite; that dependency is why it could not exist while
refreshing the matrix was a manual step of the version bump.

## Where each column comes from

- **Test ID and description** come from the row's own
  `check("ID", "description", ...)` or `skip("ID", "reason")` call. The
  description's position varies between suites, so the generator reads each
  file's own declaration of `check()` instead of assuming an argument order.
- **Status** is `fail` if the binary printed the row in its FAIL set;
  otherwise `skip` for a `skip()` call, `pass` for a `check()` call, and
  `missing` when the ID appears in no source at all. A section resolves an ID
  against its own source files first and then against the other suites of the
  same `##` subsystem, and the widening stops dead at that boundary.
- **VHDL file:line** is recovered from **row-local** evidence, tried in tiers:
  the row's own call, then a comment block naming the ID, then the first call
  after a table-driven ID literal, then the plan doc's row. Every citation that
  results is validated against the real FPGA source tree.
- **`missing` rows** are planned-but-unimplemented rows, read from the
  subsystem's `*-TEST-PLAN-DESIGN.md`. They are an honest backlog, and the only
  remaining hand-made claim in the document.

Two further citation tiers were prototyped and then **rejected**: banner
comments, and the nearest unrelated comment. Both of them end up attributing a
neighbouring row's VHDL lines to this one, and a plausible-but-wrong citation
is worse than an honest `—`.

## Editing it by hand does nothing

Any hand edit is overwritten by the next run, and fails the gate in the
meantime. That is also why the older bookkeeping-drift classes no longer
exist — there is no hand-written side left for the generated side to disagree
with. The one report that survives is a plan doc and a test source citing
*different* VHDL for the same row, and that one is worth keeping because it is
signal about the spec rather than about bookkeeping.

## A row ID must be a literal

An ID assembled at run time, as in `check((std::string(c.id) + "-35").c_str(), ...)`,
emits a row that no reader of the source can see. When that actually happened,
the matrix ended up carrying two IDs that were not rows and none of the six
that were. **Spell every ID out.**

## An ID is a global name

`make unit-test` runs `test/traceability-dup-ids.pl`, which **refuses** with
exit 2 when two suites assert the same ID. That kind of reuse is how
manufactured coverage happened once: a row read `pass` because an
identically-named row in another subsystem was vouching for it. The 29
collisions that already existed are baselined in
`test/traceability-dup-ids.conf`, so anything new fails, and the baseline
shrinks only by renaming one side in the plan doc and the test source together.

The checker enumerates suites from `test/unit-tests.conf` — all 90 of them —
and deliberately not from the matrix's own sections, because 49 suites are
tombstoned and have no section at all, so a matrix-derived audit would never
see them.

## The exceptions file

`test/traceability-exceptions.conf` is the **single** hand-maintained input to
the generator: three `|`-separated fields per record, giving section, row ID and
description. It exists for the one case that neither a test source nor a plan
doc can answer — a planned row belonging to a suite that has *no* plan doc,
because it has no VHDL counterpart at all. Those are `rewind_test` and
`sdcard_test`, tombstoned as `(jnext-internal)` and `(SD SPI spec)`
respectively. The file is parsed strictly: a malformed record or a duplicate
`(section, id)` pair is a refusal, never a quietly dropped row. It is also not a
place to assert coverage, since every row in it emits as `missing` — nothing
runs those rows.

## What the generator refuses to do

Its exit codes are a vocabulary rather than decoration: `0` is clean, `1` means
rewritten but still under-recording, `2` is a refusal with nothing written, and
`3` is an internal error.

- **It refuses when a declared suite is unaccounted for.** Every suite in
  `test/unit-tests.conf` must be either *traced* — mapped to a `##` section,
  which is the one genuinely editorial fact left in the document — or
  *tombstoned* with a written reason. That refusal replaced an advisory
  warning which had saturated to uselessness: the traced-suite count sat at 28
  for a whole release series while the manifest grew from 49 suites to 80, and
  each new suite arrived as one more name on a warning line that already listed
  fifty. `make traceability-accounting-check` runs just this half, in about
  0.01 s.
- **It refuses to invent a row.** A row that the tests assert but the matrix
  does not record is *reported*, never auto-added. The payload of a matrix row
  is its human-readable description, and the script has no honest source for
  one; a row whose only readable field was `—` would make the count look right
  while recording nothing, and would then be self-consistent forever after.
- **It refuses to guess a source path.** Suite sources are read from CMake
  rather than inferred from a naming convention, because 80 suite names match
  their source basename and 7 do not, and the two ESP-01 suites live under
  `src/esp01/test/` entirely. A convention that is right 92% of the time is the
  worst kind.
- **It leaves hand-written citations alone**, reporting disagreements instead
  of overwriting them, and leaves rows marked `<!-- protected -->` untouched.

The extractor is itself pinned by `make traceability-selftest`, described in
[4.6](06-the-harness-selftest.md). In day-to-day work the thing to remember is
short: after changing a test, run `make unit-test`. It regenerates the matrix
and fails if your committed copy is stale, so commit the regenerated file
alongside your change — and if a row you touched now reads `missing`, the fix
belongs in the test source, not in the document.
