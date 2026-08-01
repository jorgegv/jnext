# 6.3 House style

There is no formatter config and no lint that enforces most of this. What
follows is what reviewers actually look for, and where each item comes from:
some is written down in `CLAUDE.md`, the rest is derived from reading the code
and the commit history.

## Match the surrounding code

*(Derived from `CLAUDE.md`'s "surgical changes" and "scope restriction" rules,
and visible throughout the tree.)*

Touch only what the task requires. Do not reformat adjacent lines, rename
things you happened to read, or "improve" a comment you did not come to change.
Match the style of the file you are in rather than importing your own. A diff
that mixes one real change with thirty cosmetic ones costs the reviewer the
ability to see the real one.

If you notice a genuine second problem, say so — in a note, in the pull request,
or as a new issue. Do not fix it in the same change.

## Comments explain WHY

*(Derived from reading the sources; the pattern is consistent enough to be a
convention rather than a habit.)*

The code says what it does. Comments here are used almost exclusively for the
reason it does it, and the reason is very often a hardware fact that is not
inferable from the C++. `src/video/lores.h`, for instance, opens by explaining
that LoRes is *not* a compositor layer — it substitutes its pixel inside the
ULA's slot — because a reader who assumed otherwise would design the wrong
thing. The Makefile uses the same discipline in a different medium: the
one-line `# ` description above a target is documentation, and every paragraph
of rationale sits inside the recipe as `@#` lines.

The comments that pay for themselves are the ones recording a decision that
looks wrong until you know the history: why LTO is disabled for exactly one
build, why a check is a prerequisite of one target and not another, why a test
cap is not a politeness setting. Several of the longest comment blocks in the
tree are of that kind, and they exist because the same mistake was made twice.

## Cite the VHDL

*(Derived from `CLAUDE.md`, which makes the FPGA VHDL sources the authoritative
hardware spec, and confirmed by reading the code: 94 files under `src/` carry
`.vhd` citations.)*

Where behaviour is derived from the ZX Next FPGA core, name the file **and the
line range**:

```cpp
// VHDL sprites.vhd:655-657 — a port-0x303B write pulses attr_num_change,
```

Class-level documentation goes further and names the entity being modelled,
its size, and the test plan that covers it. This is not decoration. It is what
lets the next reader — or the traceability tooling — check the model against
the spec instead of against your intent, and it is why "the VHDL says so" is an
argument that wins here while "the other emulator does it this way" is not.
When a fix could be either a plausible hack or the faithful reading, be
faithful.

## Commit messages: terse but insightful

*(`CLAUDE.md` states the rule; the shape below is read off the recent history.)*

One line, lower case, a short type prefix, and — where there is room — the
reason rather than only the change:

```
release: any packaging error now blocks the release
ci: actually build the flatpak bundle, instead of skipping it
fix(ci): runner.temp is not available in job-level env — it rejects the file
doc(guide): chapter 8 links to the tracker instead of listing issues
```

Note what the third one does: it names the mechanism, not just the symptom.
That is the "insightful" half, and it is the difference between a log you can
bisect against and a list of the word "fix".

**No `Co-Authored-By` trailers.** That is an explicit project rule.

## Keep the change scoped

*(`CLAUDE.md`, and the reason the merge protocol works.)*

One branch, one independent change. If a task turns out to contain two
unrelated changes, split it — parallel branches are the normal state of this
repository, not an exception, and a reviewer can give a binary verdict on one
change far more usefully than on two.

When a change alters an interface or fixes a bug in a subsystem, the
corresponding test plan is part of the change: add the cases that cover the new
or fixed behaviour, in the same branch, and let the independent review cover
the new test code too.

## Documentation is part of the change

*(`CLAUDE.md`; also enforced mechanically for the generated outputs.)*

The man page, `USAGE.md`, the user guide and this developer guide are all
generated from committed sources, and stale committed output fails the next
test run rather than waiting for a reviewer. Edit the source, re-render, and
commit both in the same change.

The half no gate can see is whether the prose is still *true*. When you change
a subsystem, check whether this guide still describes it correctly — a stale
paragraph here is the same class of defect as a stale man page, with the
difference that nothing will tell you.
