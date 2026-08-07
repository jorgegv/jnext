# 6.3 House style

There is no formatter config here, and no lint enforces most of what follows.
This page is what reviewers actually look for. Some of it is written down in
`CLAUDE.md`; the rest is derived from reading the code and the commit history,
and each section says which.

## Match the surrounding code

*(Derived from `CLAUDE.md`'s "surgical changes" and "scope restriction" rules,
and visible throughout the tree.)*

Touch only what the task requires. Do not reformat adjacent lines, rename things
you happened to read on the way past, or "improve" a comment you did not come
there to change. Match the style of the file you are in rather than importing
your own into it.

The reason is entirely practical: a diff that mixes one real change with thirty
cosmetic ones costs the reviewer the ability to see the real one, and that
reviewer is the gate the whole process depends on.

If you do notice a genuine second problem, say so — in a note, in the pull
request, or as a new issue. Just do not fix it in the same change.

## Comments explain WHY

*(Derived from reading the sources; the pattern is consistent enough to count as
a convention rather than a habit.)*

The code already says what it does, so comments here are used almost exclusively
for the reason it does it — and that reason is very often a hardware fact which
is not inferable from the C++ in front of you. `src/video/lores.h`, for example,
opens by explaining that LoRes is *not* a compositor layer: it substitutes its
pixel inside the ULA's slot. A reader who assumed the obvious thing instead
would go on to design the wrong thing. The Makefile applies the same discipline
in a different medium, where the one-line `# ` description above a target is the
documentation and every paragraph of rationale sits inside the recipe as `@#`
lines.

The comments that pay for themselves are the ones recording a decision that
looks wrong until you know its history: why LTO is disabled for exactly one
build, why a check is a prerequisite of one target and not another, why a test
concurrency cap is not a politeness setting. Several of the longest comment
blocks in the tree are of that kind, and they are long because the same mistake
was made twice before someone wrote it down.

## Cite the VHDL

*(Derived from `CLAUDE.md`, which makes the FPGA VHDL sources the authoritative
hardware spec, and confirmed by reading the code: 94 files under `src/` carry
`.vhd` citations.)*

Where behaviour is derived from the ZX Next FPGA core, name the file **and the
line range**:

```cpp
// VHDL sprites.vhd:655-657 — a port-0x303B write pulses attr_num_change,
```

Class-level documentation goes a step further and names the entity being
modelled, its size, and the test plan that covers it.

This is not decoration. A citation is what lets the next reader — or the
traceability tooling — check the model against the specification rather than
against your intent, and it is the reason "the VHDL says so" is an argument that
wins here while "the other emulator does it this way" is not. When a fix could
be either a plausible hack or the faithful reading of the hardware, be faithful.

## Commit messages: terse but insightful

*(`CLAUDE.md` states the rule; the shape below is read off the recent history.)*

One line, lower case, a short type prefix, and — where there is room for it —
the reason rather than only the change:

```
release: any packaging error now blocks the release
ci: actually build the flatpak bundle, instead of skipping it
fix(ci): runner.temp is not available in job-level env — it rejects the file
doc(guide): chapter 8 links to the tracker instead of listing issues
```

Note what the third one manages to do in one line: it names the mechanism, not
just the symptom. That is the "insightful" half of the rule, and it is the
difference between a log you can bisect against and a list of the word "fix".

**No `Co-Authored-By` trailers.** That is an explicit project rule.

## Keep the change scoped

*(`CLAUDE.md`, and the reason the merge protocol works at all.)*

One branch, one independent change. If a task turns out to contain two unrelated
changes, split it. Parallel branches are the normal state of this repository
rather than an exception, and a reviewer can give a binary verdict on one change
far more usefully than on two tangled together.

When a change alters an interface or fixes a bug in a subsystem, the
corresponding test plan is part of that change: add the cases covering the new
or fixed behaviour in the same branch, and let the independent review cover the
new test code too.

## Documentation is part of the change

*(`CLAUDE.md`; also enforced mechanically, for the generated outputs.)*

The man page, `USAGE.md`, the user guide and this developer guide are all
generated from committed sources, and a stale committed output fails the next
test run rather than waiting for a reviewer to notice. So edit the source,
re-render, and commit both in the same change.

The half that no gate can see is whether the prose is still *true*. When you
change a subsystem, check whether this guide still describes it correctly — a
stale paragraph here is the same class of defect as a stale man page, with the
important difference that nothing will tell you about it.
