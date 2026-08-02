# 1. Orientation

Three pages of ground truth, to be read before any code. The first says what
the program is and what accuracy it actually delivers, which is not quite what
you would guess from the phrase "Spectrum emulator". The second walks every
directory and says what it is responsible for, so that a bug report about
sprite scaling or tape loading turns into a path. The third collects the
project-wide invariants — the ones that are not visible from any single file,
and therefore cannot be discovered by reading the file you happen to be
changing.

The third page is the one that matters most on a first visit. JNEXT has several
mechanisms that look like bureaucracy until you know what each of them caught:
generated documents that fail the test run when they go stale, test manifests
that pin an exact row count per suite, a command-line option table that is data
rather than control flow. In every case the failure being prevented had already
happened once, and — this is the part that made the mechanism worth building —
nobody noticed at the time. Skip that page and you will meet at least one of
them on your first change, as a red test run that appears to have nothing to do
with what you touched.

## What is in this chapter

- [1.1 What JNEXT is, in one page](01-what-jnext-is.md) — the machines it
  emulates, the accuracy model as implemented, what a frame means here, and
  the three frontends.
- [1.2 Repository layout](02-repository-layout.md) — every top-level directory
  and every `src/` subdirectory, with its responsibility.
- [1.3 The rules that shape the code](03-the-rules-that-shape-the-code.md) —
  VHDL as specification, generated-and-committed documents, declared test
  suites, and the frontend/core separation.
