# JNEXT Developer Guide

This guide describes how JNEXT is built: what the code does, where it lives,
why it is arranged the way it is, and which invariants you will break if you
do not know about them.

It is written for two readers. The first is a new contributor who wants to fix
a bug or add a feature, and needs to find the right file without reading a
hundred thousand lines to get there. The second is the maintainer coming back
to a subsystem after six months, who needs the shape of it back in their head
before touching it.

## Why the project looks the way it does

JNEXT started as an experiment, and the experiment is the reason the
repository has the shape it has. The question being asked is how far a large,
complex piece of software can be taken using AI coding tools — so essentially
all of the code here is AI-generated, with the project owner directing,
reviewing and accepting the work rather than typing it. That experiment is
still running; this guide is a snapshot of where it has got to.

Knowing that explains several things about the repository that otherwise look
like eccentricity:

- **The conventions are written down, in unusual density.** `CLAUDE.md` at the
  repository root is close to five hundred lines of rules about branches,
  tests, versioning, documentation and house style. A human contributor absorbs
  that sort of thing gradually and keeps it in their head; an assistant starts
  every session with none of it, so it has to exist as text and be re-read each
  time. `.claude/` carries the same idea further, with reusable agent
  definitions and task recipes for the jobs that recur.
- **Explanation sits next to the thing it explains.** Scripts, headers and test
  files routinely open with several paragraphs describing not just what the
  code does but which failure made it necessary. The next reader is frequently
  a fresh session with no memory of the decision, and a comment is the only
  channel that reaches it.
- **Every generated artefact is committed and gated.** The man page, both
  guides, the diagrams and the traceability matrix are all produced by a tool,
  checked into git, and re-derived by the test run so that a stale copy fails
  loudly. [1.3 The rules that shape the code](01-orientation/03-the-rules-that-shape-the-code.md)
  explains the mechanism; the motivation is this one. Generated content that
  nobody re-derives drifts silently, and a reader — human or otherwise —
  cannot tell drifted documentation from correct documentation.
- **The test apparatus is larger than the emulator.** `src/` is around 86 000
  lines of C and C++; `test/` is around 131 000 lines of tests, harness and
  tooling. [Chapter 4](04-testing/index.md) is about that machinery, and its
  size is not accidental — it is the mechanism by which generated work gets
  accepted or rejected.

What the experiment is *not* is code taken on trust. Every change goes through
the protocol in [chapter 6](06-contributing/index.md) — its own branch and
worktree, the full test triplet green, and an independent review by someone,
or something, that did not write it. This guide is part of the same posture:
generated code still needs a description a person can read, and no generator
produces one as a side effect.

### A practical suggestion

Read this repository with an AI coding assistant to hand. That is a remark
about size and shape rather than an endorsement of anything: the codebase is
large, the conventions are numerous, and most of them are enforced
mechanically, so the cost of not knowing one is a red test run whose message
points somewhere other than your change. An assistant that has read `CLAUDE.md`
and this guide will surface the relevant rule when you touch the code it
governs. Working by hand, you will find the same rules — one at a time, by
tripping over them.

## What this guide is not

It is not the user guide. Installing JNEXT, running programs, using the
debugger's panels and automating screenshots are all covered there, under
`doc/user-guide` (rendered from `src/doc/user-guide`). Nothing here repeats it.

It is not the command-line reference either. The manual page —
`doc/man/jnext.1`, also rendered as `USAGE.md` — is generated from a single
source and gated against the actual flag table in the code, so it is always
right. This guide explains the *mechanism* that keeps it right; it does not
restate the flags.

And it is not a roadmap. That is `doc/design/EMULATOR-DESIGN-PLAN.md`, which
records what was intended and in what order, much of it years ago. **The design
plan is a plan; this guide is a description.** Where the two disagree, the code
is right — and they do disagree: the plan still describes a `DebuggerInterface`
class that does not exist, a `render_scanline(vc)` call inside the scanline
loop that is not how frames are composited, and a Pentagon machine type that
was removed in 2026-05.

Every claim on the following pages was checked against the source. If you find
one that no longer holds, that is a defect in this guide — and it is the one
class of defect no automated gate can catch, so fix it in the same change that
made it stale.

## Chapters

| | Chapter | |
|---|---|---|
| 1 | [Orientation](01-orientation/index.md) | What JNEXT is, where things live, the rules that shape the code |
| 2 | [Architecture](02-architecture/index.md) | Startup, the core, a frame end to end, video, save state |
| 3 | [Subsystems](03-subsystems/index.md) | CPU, memory, video, audio, ports, peripherals, input, media, debug |
| 4 | [Testing](04-testing/index.md) | The triplet, declared suites, regression, traceability, the gates |
| 5 | [Building and packaging](05-building/index.md) | Make targets, build configurations, packaging, CI |
| 6 | [Contributing](06-contributing/index.md) | Issues, pull requests, branches and worktrees, house style |

If you are about to make a change and have time for only one page, read
[1.3 The rules that shape the code](01-orientation/03-the-rules-that-shape-the-code.md).
If what you want is to understand how the emulator actually runs, start
instead with [2.3 A frame, end to end](02-architecture/03-a-frame-end-to-end.md).
