# 4. Testing

JNEXT is a hardware emulator, so most of what it does is invisible until it is
wrong. The test system exists to make wrongness loud, and it is unusually
strict about one thing in particular: **a green result is only as trustworthy
as its denominator.** A suite that quietly stopped running, a row that quietly
stopped asserting, a generated document that quietly stopped matching its
source — each of those makes a passing run mean less while looking exactly the
same as it did before. Every gate described in this chapter was added after one
of them actually happened.

The response to that is to make the suites **declared**. Instead of running
whatever test binaries happen to be lying around and believing whatever they
happen to print, the project keeps manifests that name every suite and pin the
exact number of rows each one must report. The harness compares that
declaration against reality before it is allowed to report anything at all, and
refuses to run when the two disagree. A missing test is a loud failure, never a
silent skip.

The rest of the chapter works through the pieces:

- **The test triplet** is the three layers every change has to clear before it
  can land — the unit suites, the FUSE Z80 opcode corpus, and the screenshot
  and functional regression.
- **The manifests** are where the pinned counts live: `test/unit-tests.conf`
  for the unit suites, `test/00regression/regression_tests.conf` and
  `functional_tests.conf` for the regression. Each harness treats a
  disagreement with its manifest as a fault in itself, not as a test result.
- **The regression suite** boots the real binary headless, compares whole
  frames against committed reference images, and runs the rows that need a
  real process rather than a linked library.
- **Traceability** maps every test row back to the VHDL line that justifies it,
  in a document that is generated from the test sources and gated against going
  stale.
- **The documentation and CLI gates** prove that the committed generated
  documents still match their sources, and that the flag set the man page
  describes is the one the parser actually implements.
- **The harness self-tests** inject each fault into the harnesses themselves
  and assert that the refusal happens, because a guard nobody has ever seen
  fire is not a guard.

None of this is optional tooling that you run when you remember to.
`make unit-test` and `make regression` pull the lints, the documentation checks
and the traceability gates in as prerequisites, so they run in the inner loop,
where a failure is still cheap to fix.

Pages in this chapter:

- [4.1 The test triplet](01-the-test-triplet.md)
- [4.2 Declared suites and pinned counts](02-declared-suites-and-pinned-counts.md)
- [4.3 The regression suite](03-the-regression-suite.md)
- [4.4 Traceability](04-traceability.md)
- [4.5 The documentation and CLI gates](05-documentation-and-cli-gates.md)
- [4.6 The harness self-test](06-the-harness-selftest.md)
- [4.7 Writing a new test](07-writing-a-new-test.md)
