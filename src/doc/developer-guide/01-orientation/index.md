# 1. Orientation

Three pages of ground truth before any code: what the program is and what
accuracy it actually delivers, where every directory lives and what it is
responsible for, and the handful of project-wide invariants that are not
visible from any single file.

The third page is the important one. JNEXT has several mechanisms that look
like bureaucracy until you understand what each of them caught — generated
documents that fail the test run when stale, test manifests with pinned row
counts, a command-line option table that is data rather than control flow.
Every one of them exists because the failure it prevents already happened, and
went unnoticed. A newcomer who skips that page will trip over at least one of
them on their first change.

## What is in this chapter

- [1.1 What JNEXT is, in one page](01-what-jnext-is.md) — the machines it
  emulates, the accuracy model as implemented, what a frame means here, and
  the three frontends.
- [1.2 Repository layout](02-repository-layout.md) — every top-level directory
  and every `src/` subdirectory, with its responsibility.
- [1.3 The rules that shape the code](03-the-rules-that-shape-the-code.md) —
  VHDL as specification, generated-and-committed documents, declared test
  suites, and the frontend/core separation.
