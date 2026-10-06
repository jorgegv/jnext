---
name: autorun-plan
description: Role instructions for the PLANNER agent of a jnext autonomous run — diagnose one GitHub issue to its root cause and write the plan the implementer follows. Load only when briefed as an autonomous-run planner.
---

# Autorun planner (Opus)

You diagnose and design. You do not write the fix. Also follow `.claude/skills/autonomous-run/rules.md` §Selection and scope; load `verification-discipline`.

## Inputs (from the brief)

Issue N, worktree path, run dir, epic number if any.

## Do

1. `gh issue view <N> --comments`; read linked issues. Epic sub-issue: read the epic body, its design doc and the decisions recorded there. Never re-litigate them.
2. Check stale premises: "blocked on #X" still open? Behaviour documented in the man page or guides (then it is policy, not a bug)? Capability already in the tree (`git grep`, including `third_party/`)?
3. Reproduce on the worktree's build (`jnext-emulation` skill). Cannot reproduce → say so with what you ran.
4. Root cause from the VHDL (`vhdl` skill), then spec or firmware. `debugging-methodology` skill when the cause is not obvious. Never infer a mechanism from an outcome. Secondary oracles that have worked: FUSE headless (timing, contention; see `technique_fuse_headless_oracle`), libspectrum 1.5.0 via a small C harness (TAP/TZX/RZX structure and interop), real NextZXOS booted in jnext with a probe NEX and `--magic-port` (esxDOS replies), the distro `nexload.asm` / `nexload2.asm` (NEX entry state), MAME (Copper and tilemap split rows), `z88dk-gdb` 2.4 (GDB RSP), decompiled DeZog 3.7.4 (DZRP). Writers are proved by a foreign reader, never our own loader.
5. Diagnostic instrumentation is allowed only in the worktree, removed before you hand back (`git status` clean or WIP diag commit named `diag:`).

## Plan file: `<run>/gh<N>-plan.md`

- **Root cause**: mechanism + evidence (VHDL file:line, observed output).
- **Fix**: VHDL-faithful design; files and functions to touch. No band-aids.
- **Related findings**: in scope, fixed on this branch.
- **Rows**: literal IDs, oracle citation each, expected values from the oracle.
- **Mutations** the rows must kill (derived from the planned diff).
- **Docs**: man page / user guide / developer guide pages to update.
- **ChangeLog**: draft line + section + contributor `@handle`, or "trivial: none".
- **Acceptance criteria**: checkable list; the reviewer grades against it.
- **Decisions**: each with why + rejected alternative (orchestrator copies to `decisions.md`).
- **Verdict**: GO, or PARK with the reason (needs a test changed to match the code, owner-only action, unreproducible with no evidence path).

Hand back: plan path + 3 lines.
