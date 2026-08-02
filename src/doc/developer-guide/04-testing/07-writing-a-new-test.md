# 4.7 Writing a new test

The authority for this is `doc/testing/UNIT-TEST-PLAN-EXECUTION.md`, and you
should read it before authoring, rewriting or un-skipping any subsystem test
plan. It documents the VHDL-as-oracle rule, the pass/fail/skip distinction, the
1:1:1 emulator-fix-plus-un-skip process, the mandatory independent review, and
the coverage-theatre audit that all of it exists to prevent. This page is the
practical walkthrough alongside it, not a replacement for it.

## The rule everything else serves

**A test derives from the specification, never from the existing C++.** That is
the single most important idea in this chapter, and it is worth stating as an
idea before it turns into a procedure. The specification is the VHDL at
`cores/zxnext/src/` in the FPGA repository — it is what the hardware does, and
therefore what the emulator is supposed to do. The emulator's current behaviour
is not evidence of anything; it is the thing under test. Read `src/` only to
discover the public API surface you will drive.

If an assertion is written by observing what the emulator does today and
writing that down, it will pass forever and prove nothing, and it will do so
while adding a row to the coverage count. That is what makes the mistake
expensive: it produces something that looks exactly like a test.

The corollary follows directly. **A failing unit test does not mean the test is
wrong.** The default assumption is the other way round — that the emulator is
wrong and the test is a correct reading of the VHDL — and only an independent
review that compares the assertion, the citation and the C++ may change a test.
When you read the VHDL, read the surrounding process and not just the cited
line: an oracle can cite a perfectly real line that is nevertheless unreachable
under the stimulus your test applies. That is how several "confirmed emulator
bugs" each survived four reviews before anyone noticed.

## Where suites live

There is one directory per subsystem under `test/` — `test/copper/copper_test.cpp`,
`test/mmu/mmu_test.cpp`, and so on. Several subsystems also have a companion
`*_integration_test` that drives the same area through a more fully assembled
machine. The two ESP-01 suites are the exception to the layout: they live under
`src/esp01/test/` and are declared by that module's own CMakeLists.

## The row idiom

Suites are standalone binaries with a small local harness — there is no
GoogleTest anywhere in the project. `test/copper/copper_test.cpp` is the
canonical reference, and the two functions that matter are:

```cpp
void check(const char* id, const char* desc, bool cond, const std::string& detail = {});
void skip (const char* id, const char* reason);
```

`check()` bumps the totals and, on failure, prints `  FAIL <id>: <desc>`, which
is the one output format every tool in the project relies on. `skip()` records
the row and prints it in the trailing summary **without touching the pass/fail
counters**. Every suite ends with the line the unit harness parses:

```
Total:   82  Passed:   82  Failed:    0  Skipped:    0
```

Four rules govern the ID:

- **It must be a string literal.** An ID assembled at run time is invisible to
  every reader of the source and vanishes from the traceability matrix.
- **It must be globally unique.** `traceability-dup-ids.pl` refuses when two
  suites assert the same ID.
- The description is what the matrix publishes to everyone else, so write one
  worth reading.
- **Cite the VHDL in the same call**, with file and line range. A citation
  written only in the plan doc still works, but the row-local one is what gets
  validated against the real FPGA tree, and a disagreement between the two is
  reported for a human to resolve.

Use `skip()` when the facility does not exist in `src/` at all, or is genuinely
not observable through the public API. It does not mean that the assertion is
awkward to write. Writing `check(x, true, ...)` as a placeholder is banned
outright, because it pollutes the pass count and can end up pinning a wrong
value once the facility does land. The assertion lint rejects it, along with
`|| true` and `a == b || a != b`; it matches raw text, so those substrings must
not appear in comments either.

## Registering and pinning it

In `test/CMakeLists.txt`:

```cmake
add_executable(mysub_test mysub/mysub_test.cpp)
target_link_libraries(mysub_test PRIVATE jnext_peripheral jnext_port)
target_include_directories(mysub_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME mysub_tests COMMAND mysub_test)
```

Link the narrowest set of libraries that works. By convention the `add_test()`
name is the binary name plus an `s`, but nothing depends on that — the harness
reads the **binary path**, not the test name.

Then add the suite to `test/unit-tests.conf` with its exact row total, and bump
that file's `# expect: N` suite pin. Registering it in CMake without declaring
it in the manifest, or the reverse, makes the harness refuse to run;
[4.2](02-declared-suites-and-pinned-counts.md) lists every refusal and failure
condition. **Adding or removing a row later means editing that number by
hand**, and since the harness fails in both directions you cannot quietly skip
that step.

If the subsystem has a `*-TEST-PLAN-DESIGN.md`, planned-but-unimplemented rows
belong there. That is where the traceability generator reads them from, and
they emit as `missing` — an honest backlog rather than an invisible one.

## Before anyone reviews it: mutation-test your own rows

Break the thing your row protects, rebuild, and confirm that the row **fails**.
Then revert the mutation from a backup copy of the file, never with
`git checkout`, which is how a real fix gets thrown away along with the
mutation. Mutate the behavioural branch you actually added rather than a shared
helper: reverting a helper turns everything red at once and proves nothing
about your row in particular.

A row that cannot be made to fail is not a test. This is not a formality — the
project's own history includes six subsystem plans reporting "100% passing"
while carrying tautologies, anti-tests that pinned wrong behaviour as the
expected result, and 34 plan rows silently dropped from a 95-row plan.

## Then: never review your own work

Every change gets an independent review by someone — or some agent — that did
not write it, working in its own worktree, and the verdict is binary: APPROVE
or REJECT. The reviewer samples VHDL citations at random and traces the
stimulus through the cited process, classifies every failure into one of four
buckets (harness bug, wrong expected value, real emulator bug, plan bug),
checks that each `skip()` is genuinely unreachable, and counts rows against the
plan. Missing rows are the canonical failure mode, which is why the count is
checked rather than assumed.

## Running it

```console
$ make unit-test-build && ./build/test/mysub_test
$ make unit-test
```

`make unit-test` regenerates the traceability matrix and fails if the committed
copy is stale, so commit the regenerated file together with your change. If the
change touches rendering or CLI behaviour, run the full regression as well —
see [4.1](01-the-test-triplet.md).
