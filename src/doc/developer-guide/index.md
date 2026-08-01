# JNEXT Developer Guide

This guide describes how JNEXT is built: what the code does, where it lives,
why it is arranged the way it is, and which invariants you will break if you
do not know about them.

It is written for two readers. The first is a new contributor who wants to fix
a bug or add a feature and needs to find the right file without reading a
hundred thousand lines to get there. The second is the maintainer coming back
to a subsystem after six months, who needs the shape of it back in their head
before touching it.

## What this guide is not

It is not the user guide. Installing JNEXT, running programs, using the
debugger's panels, and automating screenshots are all covered by the user
guide under `doc/user-guide` (rendered from `src/doc/user-guide`). Nothing
here repeats it.

It is not the command-line reference either. The manual page — `doc/man/jnext.1`,
also rendered as `USAGE.md` — is generated from a single source and gated
against the actual flag table in the code, so it is always right. This guide
explains the *mechanism* that keeps it right; it does not restate the flags.

And it is not a roadmap. `doc/design/EMULATOR-DESIGN-PLAN.md` is the roadmap:
it records what was intended, in the order it was intended, and much of it is
years old. **The design plan is a plan; this guide is a description.** Where
the two disagree, the code is right, and the disagreements are real — the plan
still describes a `DebuggerInterface` class that does not exist, a
`render_scanline(vc)` call inside the scanline loop that is not how frames are
composited, and a Pentagon machine type that was removed in 2026-05. Every
claim on the following pages was checked against the source. If you find one
that no longer holds, that is a defect in this guide, and no automated gate can
catch it — fix it in the same change that made it stale.

## Chapters

| | Chapter | |
|---|---|---|
| 1 | [Orientation](01-orientation/index.md) | What JNEXT is, where things live, the rules that shape the code |
| 2 | [Architecture](02-architecture/index.md) | Startup, the core, a frame end to end, video, save state |
| 3 | [Subsystems](03-subsystems/index.md) | CPU, memory, video, audio, ports, peripherals, input, media, debug |
| 4 | [Testing](04-testing/index.md) | The triplet, declared suites, regression, traceability, the gates |
| 5 | [Building and packaging](05-building/index.md) | Make targets, build configurations, packaging, CI |
| 6 | [Contributing](06-contributing/index.md) | Issues, pull requests, branches and worktrees, house style |

Start with [1.3 The rules that shape the code](01-orientation/03-the-rules-that-shape-the-code.md)
if you are about to make a change and only have time for one page. Start with
[2.3 A frame, end to end](02-architecture/03-a-frame-end-to-end.md) if you want
to understand how the emulator actually runs.
