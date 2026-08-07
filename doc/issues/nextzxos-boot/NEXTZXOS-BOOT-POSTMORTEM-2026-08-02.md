# Why NextZXOS suddenly booted — a post-mortem

**Written**: 2026-08-02 · **Scope**: the 2026-03→2026-07 NextZXOS native-boot effort,
the 2026-07-10 breakthrough, and the two external factors that coincided with it
(the `zx_go` reference emulator and the model change to Fable 5).

**Method**: git history (all refs), the in-repo investigation corpus
(`doc/issues/nextzxos-boot/`, 190 files / 20 MB), the project auto-memory
(≈45 G46(b) session files), the daily prompt files (`.prompts/`), and the
Claude Code session transcripts on this machine. Every claim below is
traceable to one of those; the places where evidence does **not** exist are
called out explicitly rather than filled with a plausible story.

---

## 1. Short answer

Three separable questions, three separate answers.

**What was the bug?** One line. `Mmu::to_sram_page` exempted logical page
`0x0E` (bank 7 lower) from the VHDL `+0x20` SRAM shift, which aliased the
bank-7 BRAM onto **physical SRAM page `0x0E` — the upper half of the alt-ROM
area**. NextZXOS's own alt-ROM install then overwrote its own MMU-page-14
workspace, corrupting the saved-SP variable at `$DA35`; the next
`LD SP,($DA35)` loaded garbage, a later `RET` popped `$0000`, and the CPU
landed on `enNextZX.rom` bank 2's `NOP; JR $0000` sentinel. Gray screen, no
interrupts, dead.

**Did `zx_go` matter?** **Yes, decisively — but not in the way you might
assume.** The answer was not in `zx_go`'s source. `zx_go` was the *instrument*:
it was the only available emulator that runs the **authentic boot chain**
(the byte-identical `nextboot.rom` → TBBLUE.FW → NextZXOS) and could therefore
be aligned instruction-for-instruction with jnext across the staging soft
reset. That symmetric trace diff is what localised the fault to a *memory*
divergence at `$DA35`, which nothing else had ever done. CSpect and ZEsarUX
are architecturally incapable of that comparison. Separately, five of the six
secondary fixes that shipped in the same commit were taken directly from
`zx_go`'s documented war stories. **Without `zx_go`, this would not have been
solved that week.**

**Did the model change matter?** The evidence is a **strong correlation with
no proof of causation**, and I'd rather say that than overclaim. Hard fact:
at `2026-07-09 21:57:55` local you ran `/model claude-fable-5[1m]`, and three
minutes later handed over `zx_go`. Every minute of the analysis, the hunt and
the fix ran on **Fable 5** with a 1M-context window; the immediately preceding
work in the same session ran on Opus 4.8. What I **cannot** establish is what
model ran the April/May campaign — the transcript archive on this machine only
goes back to **2026-07-03** (retention boundary), so there is simply no record.
Anyone claiming "the new model solved it" is asserting something the local
evidence cannot support.

The honest synthesis is in §7.

---

## 2. Timeline

All times Europe/Madrid (UTC+2 in July); transcript timestamps converted from UTC.

### 2.1 The long campaign

| Date | Event |
|---|---|
| 2026-03-19 | First plan entry assuming NextZXOS ("remove snapshot/tape, handled by NextZXOS") |
| 2026-03-29 | First boot work: `9a6423d5` "NextZXOS boot — boot ROM, DivMMC automap fixes" |
| 2026-04-01 | `3a790d5b` "comprehensive NextZXOS boot investigation — root cause analysis" — the (wrong) config-page/DivMMC conflict thesis |
| 2026-04-01 | `1f6eff4f` "defer NextZXOS to v1.1, ship without it first" — first strategic retreat |
| **2026-04-18** | **`97376409` "Task 12c — centralise VHDL +0x20 shift in `to_sram_page` helper" — THE BUG IS INTRODUCED HERE**, as part of a fix that genuinely unblocked tbblue.fw |
| 2026-05-03 → 05-11 | The G46(b) grind. Peak intensity: **183 commits on 2026-05-10**, 131 on 05-09, 102 on 05-11. 169 boot-related commits in May alone |
| 2026-05-17 | `--bypass-tbblue-fw` built as a workaround (Task 18); `25607d57` symmetric jnext-vs-**CSpect** trace attempted; `ad40b1e7` **"park bypass investigation"** |
| 2026-05-29/30 | Two evenings on the T-state profiler (Task 21) |
| **2026-05-30 → 2026-07-08** | **39 days, zero commits.** No daily prompt files either — the gap runs `.prompts/2026-05-29.md` → `.prompts/2026-07-10.md` |

### 2.2 The restart — and it was not your idea

| Date/time | Event |
|---|---|
| 2026-07-07 23:39 | **`danboid` files GitHub issue #4**: "Fails to build under Debian 12 / LMDE 6" |
| 2026-07-08 21:52 | You return to the project. First message of the session: *"I just received this issue, fix it"* — a build fix, nothing to do with booting |
| 2026-07-08 → 07-09 17:44 | Three build/packaging commits (`ce5c6e19`, `3ee47cbb`, `086f89e1`). Model: **Opus 4.8** |
| **2026-07-09 21:57:55** | **You run `/model claude-fable-5[1m]`** → "Set model to claude-fable-5" |
| **2026-07-09 22:01:01** | **You hand over `zx_go`**: *"I have a new reference for you to check… Is open source and BOOTS NextZXOS. Analyze that emulator, and analyze JNext, compare what it does to successfully boot"* |

An external contributor's build report is what reopened the project. The boot
question came 26 hours later, and only because you brought a new reference to it.

### 2.3 The breakthrough — ~11 hours wall clock

| Time | Event |
|---|---|
| 2026-07-09 22:01 | Analysis starts. `zx_go` cloned; agents dispatched at jnext, `zx_go` and the VHDL in parallel |
| 22:45 | **Empirical proof**: `zx_go` cold-boots NextZXOS headless to the menu using *our* SD image and a bootrom **sha256-identical** to jnext's (`ee0b99c5…4798`) |
| 22:45–23:05 | Two long-standing G46(b) conclusions **refuted** (see §4). Delta table of 16 behaviours built; 5 divergences confirmed |
| 23:05 | `ZXGO-COMPARISON-2026-07-09.md` written: delta table, prioritised plan P0–P6 |
| 23:05–23:15 | Independent reviewer confirms all five divergences but **disciplines the causal story** — insists on P0 (a runtime trace) before crediting any fix |
| 23:39 | You: *"go with all tasks, don't stop unless you find something I need to solve"* |
| ~00:07 | **Monthly spend limit hit.** An agent dies mid-task; work stops |
| 2026-07-10 07:19 | You: *"go"*. Limit reset; work resumes |
| 07:19–08:49 | **The hunt** (≈90 minutes, 540+ messages) |
| **08:49** | **NextZXOS boots.** Welcome screen + main menu, live RTC, 1792K |
| 09:26 | *"ok I verified NextZXOS boot successfully!"* — plus the mid-boot glitch you spotted |
| 09:26–10:50 | Three-round independent review: APPROVE-WITH-NITS → **REJECT** (real blocker) → APPROVE-WITH-NITS → **APPROVE** |
| 11:49 | Merged to `main` (`742269a7`), v0.94.0 tagged |
| Same evening | The **sibling bug** (bank 5) found and fixed — your mid-boot glitch (§3.3) |

### 2.4 The aftermath

Commits per day after the fix: 32 (07-10), 64, 91, 109, 79, 110, 109, 77, 89,
104, 85… The project has not had a quiet day since. Your "non-stop since then"
is visible directly in the commit histogram.

### 2.5 The hunt itself

From the 08:49 announcement, verbatim:

> trap tracer → post-reset trace windows (**first 150k instructions were
> byte-identical to zx_go**) → SD-content mismatch discovered and fixed (zx_go
> now serves our exact `.img`) → SP divergence at `LD SP,($DA35)` → logical
> write-watch (**nothing**) → physical byte-watch (**nothing!**) → the aliasing.

The fork instruction: `LD SP,($DA35)` at `$1BEC` loaded **`$5F2F` in jnext vs
`$5BF5` in zx_go**. Both write-watches coming back empty is the signature of an
*aliasing* bug rather than a stray write — nobody wrote to that address; two
different logical memories were resolving to the same physical bytes. That is
the deduction that produced the answer, and it was only possible because a
reference emulator had supplied the correct value to diff against.

---

## 3. What was actually fixed

### 3.1 The killer — one conditional

```c
// before (src/memory/mmu.h)
if (logical == 0x0A || logical == 0x0B || logical == 0x0E) return logical;
return static_cast<uint8_t>(logical + 0x20);

// after
if (logical == 0x0A || logical == 0x0B) return logical;
return static_cast<uint8_t>(logical + 0x20);
```

The reasoning behind the original code was not careless — it was *half* right.
VHDL `zxnext.vhd:2961-2962` genuinely does exempt bank 5 and bank-7-lower from
the `mmu_A21_A13` shift, because on silicon **those banks are separate dual-port
BRAMs, not external SRAM at all**. The error was modelling "separate silicon" as
"a different index into the same array". Bank 7's stand-in landed on physical
SRAM page `0x0E`, which is the alt-ROM upper half — a real, actively-used region
(config-mode `NR $04` window, `NR $8C` write-over, `enAltZX.rom` seeding). Two
distinct memories, one buffer.

The final fix (after review, `910102d7`/`395b81e7`) is not the one-liner: `Mmu`
now owns a **dedicated `bank7_bram_` buffer**, matching the VHDL truth
(`bank7_ram: entity work.dpram2`, `zxnext.vhd:6670`), gated on `rom_in_sram_`
so standalone 128K/+3 bank-7 RAM is untouched. The intermediate "relocate to
SRAM page `0x2E`, it's dead space" idea was **refuted by the reviewer** —
config-mode `NR $04=$17` reaches every SRAM page, so no `ram_` page is safe.

### 3.2 The passengers

Five further fixes shipped in `ee2d8910`, all traceable to `zx_go`'s documented
findings, none of which turned out to be the killer:

| Fix | Origin | Was it on the boot path? |
|---|---|---|
| `NR $00` machine ID `$08` → **`$0A`** | zx_go hit the identical bug: `$08` sent NextZXOS down an "emulator" branch | Not the killer; jnext's source comment had asserted the **opposite** and was wrong |
| Soft reset **preserves** BC/DE/HL/IX/IY | `t80n.vhd:429-447` has an empty register-file reset branch; zx_go documents NextZXOS dereferencing `(IX+$1F)` right after | Real bug, fixed regardless |
| **DivMMC RAM = physical SRAM pages 16-31** | "zx_go gap #1" — makes tbblue.fw's config-window installs visible to the runtime overlay | Prerequisite for the `$3D00` trampoline |
| **CMD18 stream survives CS deassert** | zx_go `spi.go` | **Yes** — NextZXOS closes a pre-reset stream with CMD12 after the reset |
| SD card on **socket 0 only** | zx_go documented a phantom-second-card infinite mount loop | Latent here |

Note the pattern: a reviewer had already ranked `NR $00` as **P1, most likely
root cause**. It wasn't. The discipline of running P0 (the trace) *before*
crediting any candidate is what stopped the team from declaring victory on the
wrong fix — and is the single most transferable process lesson here.

### 3.3 The sibling, same day

Your mid-boot glitch was **the same bug class one bank over**: jnext modelled
bank 5 as physical SRAM pages `0x0A/0x0B` and the ULA read those same pages.
During "Loading ROM: enNextZX.rom", tbblue.fw streams data through the
config-mode `NR $04` window, which legally reaches `0x0A/0x0B` — invisible on
real hardware (plain SRAM), but in jnext it painted the visible screen. Fixed
the same night (`968914c3`, Task 25) with a dedicated 16K `bank5_vram_` buffer.
The oracle for *that* one was **you, from real hardware** ("that boot phase
shows ONLY white text on black"). Fixing it also revealed a line of text that
had been buried under the garbage all along.

---

## 4. Why four months missed a one-line bug

This is the uncomfortable part, and it's worth recording precisely.

**The offending line was audited — repeatedly — and passed.** The May
enumeration audits looked *directly at it* and marked it conformant:

> `NEXTZXOS-BOOT-SUBSYSTEM-VERIFY25-MEMORY.md`, row 71:
> `` `mmu.h:1093-1097` `to_sram_page` — `+0x20` except bank 5 (0x0A/0x0B) and bank 7 lo (0x0E) `` | `:2961-2962 dual-port bypass + :2964 shift` | **✓**

> `NEXTZXOS-BOOT-SUBSYSTEM-VERIFY12-MEMORY.md`:
> "`to_sram_page()` **faithfully implements this** with the bank-5 (0x0A/0x0B)
> and bank-7-low (0x0E) carve-outs per VHDL :2961-2962."

The audit compared the code against the *right* VHDL lines and drew the *wrong*
conclusion, because both the code and the audit shared the same unexamined
premise: that "bypasses the shift" and "is a separate memory" are the same
statement. A citation check cannot catch an error that lives in the
interpretation of the citation. This is a genuinely hard failure mode and not
one that more audit passes would have fixed — there were already 25 of them.

**The symptom was seen and misattributed.** `G46B-INVESTIGATION-LIVE.md`
records the investigation noticing bank-7 content was wrong, and concluding the
gap was in *hardware knowledge* rather than in jnext:

> "bank 7 high half (page 0x2F) needs sprite descriptor data — **how real Next
> hardware populates this region post-RAM-test is unclear**"
> … "reads zeros from bank 7 high half (page 0x2F) where it expects sprite
> descriptors" … "Wait for a NextZXOS supervisor source code reference to
> understand what's expected in bank 7"

The bank was split in half — low at physical `0x0E` (colliding with alt-ROM),
high at `0x2F` (correct). The investigation saw the resulting incoherence and
filed it as an open question about real hardware. That framing pointed away
from the code for two more months.

**The wrong conclusions were load-bearing.** Two G46(b) conclusions were
formally refuted within 45 minutes of `zx_go` arriving:

- *Layer 7 — "`nextboot.rom` is the wrong/outdated asset."* Refuted: `zx_go`
  boots with the **byte-identical** ROM (its `LICENSES/tbblue_loader-NOTICE.md`
  even cites jnext by name).
- *Layer 6 — "VHDL says machine_type is ZX48 after tbblue.fw's `$B0`; CSpect
  must be deviating to boot."* Refuted: VHDL powers on `nr_03_machine_type :=
  "011"` (+3) at `zxnext.vhd:1099-1103` and never resets it. **jnext** was the
  deviation. CSpect and `zx_go` had been faithful all along.

Both had been steering the investigation. One of them had it hunting a phantom
asset problem; the other had it treating a correct reference emulator as the
unreliable one.

**The right method had been tried — against the wrong reference.** On
2026-05-17, `25607d57` implemented exactly the technique that eventually worked:
a symmetric jnext-vs-CSpect instruction trace, with a custom CSpect plugin. It
achieved 249,547 instructions of perfect lockstep and then diverged on an
**interrupt-rate** discrepancy — a real but different bug — and the trail went
cold there. The method was sound. The reference was not (see §5.2).

---

## 5. The `zx_go` question

You asked whether there was a correlation, or whether the problem was solved
independently. The correlation is direct and causal, and the split is worth
stating precisely because both halves are true.

### 5.1 What `zx_go` actually contributed

**As an existence proof (within 45 minutes).** `zx_go` booting NextZXOS with
jnext's *own* bootrom bytes and *our* SD image collapsed the search space
immediately: the assets were fine, the firmware was fine, the SD image was fine.
The bug was in jnext's emulation. That alone killed two months of accumulated
misdirection.

**As the trace oracle (the decisive contribution).** The root cause was found by
aligning both emulators at the staging soft reset (`PC=$6D31`) and walking
forward. The first 150k post-reset instructions were byte-identical; then a
single 16-bit value diverged. **You cannot do that without a second emulator
that executes the same code path.** This is what "reference emulator" means in
the strong sense: not a source of answers, but a source of *correct
observations* to subtract from your own.

**As a source of concrete fixes.** Five of the six secondary changes came
straight from `zx_go`'s `VHDL_CONFORMANCE.md` and `CHANGELOG.md` war stories
(§3.2). Its `VHDL_CONFORMANCE.md` was explicitly noted as "an enumeration-table
conformance audit in exactly our Task-2 style — directly reusable as a checklist
source."

### 5.2 Why CSpect and ZEsarUX could not have done this

Not a matter of effort — an architectural impossibility:

- **CSpect does not run the authentic boot chain.** Its slot-0 boot code is a
  CSpect-specific DivMMC-ROM-overlay design; the May trace work found *"CSpect's
  boot ROM is only 9 IPL instructions long before it converges to the same
  enNextZX.rom code."* CSpect never executes `nextboot.rom` or TBBLUE.FW, so it
  cannot be aligned with jnext across the bootrom→firmware→staging-reset
  window — which is precisely where the bug lived.
- **Both fake esxDOS host-side.** CSpect moves all `RST $08` into `esxDOS.dll`,
  always on; ZEsarUX uses `esxdos_handler`. Neither exercises the DivMMC automap
  path (recorded separately in memory as
  `technique_cspect_not_oracle_for_esxdos`).

`zx_go` was the first available emulator that boots NextZXOS **through the real
firmware**, which is the only configuration in which jnext's bug is observable.

### 5.3 What was *not* taken from `zx_go`

- **No source code.** The only traces of `zx_go` in the codebase are **five
  comments** citing it as a reference (`nextreg.cpp:220`, `sd_card.cpp:144`,
  `emulator.cpp:1392`, `emulator.cpp:5967`,
  `nextreg_integration_test.cpp:157`). No vendored files, no ported functions.
  (Licensing note for the record: `zx_go` is MIT with a GPLv3 bundled loader,
  jnext is GPLv3 — but nothing was copied, so the question doesn't arise.)
- **The root cause itself.** `zx_go` did not identify jnext's aliasing bug and
  could not have — it's a jnext-internal modelling error with no counterpart in
  their design. The write-watch cascade, the aliasing deduction, and the fix
  were jnext-side work against the VHDL.
- **The correct fix.** The shipped design (dedicated `bank7_bram_` per
  `zxnext.vhd:6670`, gated on `rom_in_sram_`) came from the VHDL and from three
  rounds of adversarial review — including a **REJECT** that caught the first
  attempt breaking standalone 128K/+3 bank-7 RAM, reproduced empirically by
  compiling a probe against the built library.
- **The sibling bank-5 fix.** Found from your real-hardware observation, by
  analogy to the bank-7 fix (§3.3).

### 5.4 Verdict

**Not independent.** `zx_go` was necessary. But it was necessary as an
*instrument*, not as an answer key — it supplied the one thing four months of
solo investigation could not manufacture: a trustworthy second observation of
the same execution. The diagnosis and the fix were yours.

---

## 6. The model question

### 6.1 What the evidence shows

| Fact | Evidence |
|---|---|
| At 2026-07-09 **21:57:55** you ran `/model claude-fable-5[1m]` | Transcript `2e772552`, `<command-name>/model</command-name> … claude-fable-5[1m]` → *"Set model to claude-fable-5"* |
| 3 minutes later you handed over `zx_go` | Same transcript, 22:01:01 |
| **All** zx_go analysis, the hunt, the fix and the review ran on **Fable 5** | Model spans in `2e772552`: `claude-opus-4-8` 07-08 21:52 → 07-09 17:44; `claude-fable-5` 07-09 22:01 → 07-10 12:17 |
| The preceding build-fix work ran on **Opus 4.8** | Same |
| The follow-on session that landed Tasks 23/24/25/28 also ran on Fable 5 | Session `e409db6d`, 07-10 13:33 → 00:10 |
| Fable was rationed for you | Your own message, 2026-07-15: *"we will do a handover and switch model (Fable usage is almost depleted for me)"* |
| A monthly spend limit interrupted the work overnight | Agent failure at 00:07, *"You've hit your monthly spend limit"*; resumed 07:19 |

The `[1m]` matters and is easy to overlook: the task required holding
**two emulator codebases plus the VHDL** in one context, and that is exactly
what the session did — jnext mapping, `zx_go` mapping, VHDL oracle and delta
verification ran as four parallel agents feeding one synthesis.

### 6.2 What the evidence cannot show

**There is no record of what model ran the April/May campaign.** The transcript
archive on this machine starts at **2026-07-03T06:35** across *all* 1,027
sessions and 814 MB — a retention boundary, not a project boundary. The April
and May jnext sessions are gone. Consequently:

- I cannot show the failed campaign ran on an older/weaker model.
- I cannot date the public launch of Opus 4.8 or Fable from local evidence. The
  earliest appearance of either **anywhere on this machine** is 2026-07-03,
  which is a lower bound on *your* access, not a release date. I'm deliberately
  not asserting release dates from memory.
- No A/B test exists. Nobody re-ran the May investigation on Fable, or the July
  investigation on the older model.

### 6.3 Honest assessment

What the model plausibly contributed: the July session ran **four parallel
analysis agents** over two codebases and the VHDL, built a 16-row delta table,
submitted its own findings to an adversarial reviewer that **refuted the
headline claim**, and then executed a six-step diagnostic cascade to a
single 16-bit divergence — inside ~4 active hours. Capability and context
budget are clearly relevant to work of that shape.

What undercuts a simple "the new model solved it" story:

1. **The decisive input was external and human.** You found `zx_go` and brought
   it. No model produced that.
2. **The prior campaign was not obviously under-powered.** It correctly built
   the trace-diff methodology in May, wrote a custom CSpect plugin, and achieved
   249k instructions of lockstep. It failed for a *reference* reason, not an
   analysis-capability reason.
3. **The refutations that unlocked everything were cheap once the reference
   existed.** Comparing two sha256 hashes killed Layer 7. Reading four VHDL
   lines killed Layer 6. Neither needed a frontier model — they needed a reason
   to look, which `zx_go` supplied.
4. **Adversarial review caught two real defects the authoring model shipped** —
   a false "dead space" justification and a blocker that broke standalone 128K.
   The process caught what the model missed.

**Conclusion**: the model change is a genuine correlation and probably a real
accelerant on the ~11-hour timeline. It is not the explanation. The
counterfactual I'd defend is that `zx_go` + the May methodology would have
cracked this on Opus 4.8 too, more slowly. The counterfactual I would *not*
defend is that any model would have cracked it without `zx_go`.

---

## 7. Synthesis — what actually made the difference

Ranked by how much each mattered:

1. **A reference emulator that runs the same code path** (`zx_go`). Necessary
   and irreplaceable. The generalisable rule: *an oracle is only an oracle for
   the code path it actually executes* — CSpect boots NextZXOS and was still
   worthless for this, because it boots it a different way.
2. **The discipline of demanding a runtime trace before crediting a fix** (P0).
   Five confirmed divergences were on the table, one ranked most-likely-cause.
   All five were wrong about the stall. Without P0, the likely outcome is
   shipping `NR $00 = $0A`, seeing no boot, and concluding the reference was
   irrelevant.
3. **Adversarial independent review.** Three rounds, two real defects,
   one **REJECT** with an empirical repro. The first "fix" was wrong about why
   it worked; the second broke a machine type nobody was testing.
4. **The model + 1M context.** A real accelerant for holding two codebases and
   the VHDL simultaneously, and for running the analysis in parallel. Not the
   cause.
5. **An external bug report.** `danboid`'s issue #4 is what reopened the
   project at all. Without it the 39-day gap plausibly continues.

And one durable lesson, recorded because it cost four months:

> **A conformance audit that cites the right VHDL can still bless the wrong
> code.** Row 71 of the memory audit compared `to_sram_page` against
> `zxnext.vhd:2961-2962` — the correct citation — and marked it ✓. Both the code
> and the audit shared an unexamined premise: that "bypasses the address shift"
> and "is a physically separate memory" are the same statement. They are not,
> and no amount of re-auditing against the same citation would have surfaced it.
> What surfaced it was an **observation from outside the system**.

---

## Appendix — evidence index

**In-repo**
- `doc/issues/nextzxos-boot/ZXGO-COMPARISON-2026-07-09.md` — the primary
  document: delta table, P0–P6 plan, review verdicts, and the RESOLUTION section
- `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-INVESTIGATION.md` — the chronological journal
- `doc/issues/nextzxos-boot/G46B-INVESTIGATION-LIVE.md` (354 KB) — the May grind
- `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY{12,25}-MEMORY.md` — the audit rows that blessed the bug
- `.prompts/2026-07-10.md` — the boot-day task file (Tasks 1, 22, 23, 24, 25)
- `ChangeLog` v0.94.0

**Git**
- `97376409` (2026-04-18) — bug introduced
- `ee2d8910` (2026-07-10) — the boot fix; `910102d7`, `395b81e7`, `8148e958` — review fixes
- `968914c3` (2026-07-10) — the bank-5 sibling fix
- `25607d57` (2026-05-17) — the CSpect symmetric-trace attempt
- Commit-per-day histogram: 2026-05-30 → 2026-07-08 gap

**Auto-memory**
- `project_zxgo_comparison_2026-07-09.md`
- `technique_cspect_not_oracle_for_esxdos.md`
- `project_nextzxos_boot_progress.md`, 44 × `project_g46b_2026_05_*.md`

**Session transcripts** (`~/.claude/projects/-home-jorgegv-src-spectrum-jnext/`)
- `2e772552-…` — the boot session (5.6 MB): model switch, zx_go handover, the hunt, the review
- `e409db6d-…` — same-day follow-on (bank 5, RTC, regression tests)
- Archive-wide model scan: 1,027 files, retention boundary 2026-07-03

**GitHub**
- Issue #4 (`danboid`, 2026-07-07) — what reopened the project
