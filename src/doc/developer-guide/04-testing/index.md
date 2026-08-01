# 4. Testing

JNEXT is a hardware emulator, so most of what it does is invisible until it is
wrong. The test system exists to make wrongness loud, and it is unusually
strict about one thing in particular: **a green result is only as trustworthy
as its denominator.** A suite that quietly stopped running, a row that quietly
stopped asserting, a document that quietly stopped matching its source — each
of those makes a passing run mean less while looking exactly the same. Every
gate described in this chapter was added after one of them actually happened.

So the suites are **declared** — in manifests, with exact expected counts — and
the harness proves it ran precisely what was declared before it is allowed to
report anything at all. A missing test is a loud failure, never a silent skip.

The parts, one sentence each:

- **The test triplet** is the three layers a change must clear: the unit
  suites, the FUSE Z80 opcode suite, and the screenshot/functional regression.
- **The manifests** (`test/unit-tests.conf`, `test/00regression/regression_tests.conf`,
  `test/00regression/functional_tests.conf`) declare which suites exist and how
  many rows each must report, and the harnesses refuse to run when reality
  disagrees.
- **The regression suite** boots the real binary headless, compares whole
  frames against committed reference images, and runs the functional rows that
  need a real process rather than a linked library.
- **Traceability** maps every test row back to the VHDL line that justifies it,
  in a document that is generated from the test sources and gated against
  staleness.
- **The documentation and CLI gates** prove that the committed generated
  documents match their sources, and that the documented flag set matches the
  one the parser implements.
- **The harness self-tests** inject each fault into the harnesses themselves
  and assert the refusal, because a guard that cannot be shown to fire is not a
  guard.

None of this is optional tooling you run when you remember to. `make unit-test`
and `make regression` pull the lints, the documentation checks and the
traceability gates in as prerequisites, so they run in the inner loop where
they can still be cheap to fix.

Pages in this chapter:

- [4.1 The test triplet](01-the-test-triplet.md)
- [4.2 Declared suites and pinned counts](02-declared-suites-and-pinned-counts.md)
- [4.3 The regression suite](03-the-regression-suite.md)
- [4.4 Traceability](04-traceability.md)
- [4.5 The documentation and CLI gates](05-documentation-and-cli-gates.md)
- [4.6 The harness self-test](06-the-harness-selftest.md)
- [4.7 Writing a new test](07-writing-a-new-test.md)
