# 4.7 Writing a new test

The authority for this is `doc/testing/UNIT-TEST-PLAN-EXECUTION.md`. Read it
before authoring, rewriting or un-skipping any subsystem test plan: it documents
the VHDL-as-oracle rule, the pass/fail/skip distinction, the 1:1:1
emulator-fix-plus-un-skip process, the mandatory independent review, and the
coverage-theatre audit all of it exists to prevent. This page is the practical
walkthrough, not a replacement.

## Where suites live

One directory per subsystem under `test/` — `test/copper/copper_test.cpp`,
`test/mmu/mmu_test.cpp`, and so on. Several subsystems also have a companion
`*_integration_test` driving the same area through a more assembled machine.
Two ESP-01 suites live under `src/esp01/test/`, declared by that module's own
CMakeLists.

## The row idiom

Suites are standalone binaries with a small local harness — no GoogleTest.
`test/copper/copper_test.cpp` is the canonical reference:

```cpp
void check(const char* id, const char* desc, bool cond, const std::string& detail = {});
void skip (const char* id, const char* reason);
```

`check()` bumps the totals and, on failure, prints `  FAIL <id>: <desc>` — the
one output format every tool in the project relies on. `skip()` records the row
and prints it in the trailing summary **without touching the pass/fail
counters**. Every suite ends with the line the unit harness parses:

```
Total:   82  Passed:   82  Failed:    0  Skipped:    0
```

Four rules about the ID:

- **It must be a string literal.** An ID assembled at run time is invisible to
  every source reader and vanishes from the traceability matrix.
- **It must be globally unique.** `traceability-dup-ids.pl` refuses when two
  suites assert the same ID.
- The description is what the matrix publishes. Write one worth reading.
- **Cite the VHDL in the same call** — file and line range. A citation written
  only in the plan doc still works, but the row-local one is what gets validated
  against the real FPGA tree, and a disagreement between the two is reported for
  a human to resolve.

`skip()` means the facility does not exist in `src/` at all, or is genuinely not
observable through the public API. It does not mean the assertion is awkward.
`check(x, true, ...)` as a placeholder is banned outright — it pollutes the pass
count and can pin a wrong value once the facility lands. The assertion lint
rejects it, along with `|| true` and `a == b || a != b`; it matches raw text, so
those substrings must not appear in comments either.

## The rule that matters most

**A test derives from the specification, never from the existing C++.** The
specification is the VHDL at `cores/zxnext/src/` in the FPGA repository; read
`src/` only to discover the public API surface. If your assertion matches
whatever the emulator happens to do today, you have written a tautology that
will pass forever and prove nothing.

The corollary: **a failing unit test does not mean the test is wrong.** The
default assumption is that the emulator is wrong and the test is a correct
reading of the VHDL, and only an independent review comparing assertion,
citation and C++ may change a test. Read the surrounding VHDL process, not just
the cited line — an oracle can cite a real line and still be unreachable under
the stimulus the test applies, which is how several "confirmed emulator bugs"
survived four reviews each.

## Registering and pinning it

In `test/CMakeLists.txt`:

```cmake
add_executable(mysub_test mysub/mysub_test.cpp)
target_link_libraries(mysub_test PRIVATE jnext_peripheral jnext_port)
target_include_directories(mysub_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME mysub_tests COMMAND mysub_test)
```

Link the narrowest set of libraries that works. The `add_test()` name is
conventionally the binary name plus `s`; the harness reads the **binary path**,
not the test name.

Then add the suite to `test/unit-tests.conf` with its exact row total and bump
that file's `# expect: N` suite pin. Registering in CMake without declaring it
here — or the reverse — makes the harness refuse to run; see
[4.2](02-declared-suites-and-pinned-counts.md) for every refusal and failure
condition. **Adding or removing a row later means editing that number by hand**,
and the harness fails in both directions, so you cannot skip it.

If the subsystem has a `*-TEST-PLAN-DESIGN.md`, planned-but-unimplemented rows
belong there — that is where the traceability generator reads them from, and
they emit as `missing`, an honest backlog.

## Before anyone reviews it: mutation-test your own rows

Break the thing the row protects, rebuild, and confirm the row **fails**. Then
revert the mutation from a backup copy of the file — never with `git checkout`,
which is how a real fix gets thrown away along with the mutation. Mutate the
behavioural branch you actually added, not a shared helper: reverting a helper
turns everything red and proves nothing about your row.

A row that cannot be made to fail is not a test. Not a formality — the project's
history contains six subsystem plans reporting "100% passing" while carrying
tautologies, anti-tests that pinned wrong behaviour as expected, and 34 plan
rows silently dropped from a 95-row plan.

## Then: never review your own work

Every change gets an independent review by someone — or some agent — that did
not write it, working in its own worktree. The verdict is binary, APPROVE or
REJECT. The reviewer samples VHDL citations at random and traces the stimulus
through the cited process, classifies every failure (harness bug / wrong
expected value / real emulator bug / plan bug), checks that each `skip()` is
genuinely unreachable, and counts rows against the plan. Missing rows are the
canonical failure mode.

## Running it

```console
$ make unit-test-build && ./build/test/mysub_test
$ make unit-test
```

`make unit-test` regenerates the traceability matrix and fails if the committed
copy is stale, so commit the regenerated file with your change. If the change
touches rendering or CLI behaviour, run the full regression too — see
[4.1](01-the-test-triplet.md).
