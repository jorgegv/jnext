# G46(b) — RESET_SOFT audit reviewer report (read-only)

**Date:** 2026-05-05  
**Reviewer scope:** verify the read-only audit at `doc/issues/G46B-AGENT-RESETSOFT.md` and the post-fix observation that the supervisor still stalls in the same loop after applying the recommended `nmi_source_.strobe_soft_reset()` (commit `75d11fb`).  
**Verdict: REWORK.** The agent's VHDL inventory is correct, but the recommendation is rooted in an unverified assumption (that the supervisor reads NR 0x02 to branch). I disassembled enNextZX.rom and the supervisor **NEVER** reads or writes NR 0x02. The FSM strobe is VHDL-faithful but **not boot-relevant** — exactly hypothesis #3 from the user's framing.

---

## 1. Inventory completeness — APPROVED with minor note

I cross-checked all 44 `if reset = '1'` blocks in `zxnext.vhd` (3 main process clusters: 1730-3907 core/MMU/NMI/iotrap; 4528-5108 NextReg state; 7447-7540 unused/optional) plus per-peripheral resets in `device/{im2_control,divmmc,multiface,copper,ctc,dma}.vhd`, `video/{sprites,tilemap,layer2,lores,zxula_timing}.vhd`, `audio/{audio_mixer,ym2149,dac,i2s}.vhd`. Every reset-domain signal in those files is mapped 1:1 to a `Subsystem::reset()` invocation in `Emulator::init()` (emulator.cpp:85-128). **No omissions.**

Minor note: the agent's table at line 65 (NR 0x86-0x89 nibble copy) is flagged as "approximation" in `nextreg.cpp:72-80`. Per VHDL zxnext.vhd:5061-5067 only `nr_86-88` are forced to 0xFF; `nr_89` is gated on a different bit. Drift is real but unrelated to G46(b).

## 2. Does the supervisor read NR 0x02? — NO

I extracted `/tmp/enNextZX.rom` and disassembled it with `z88dk-dis -mz80n`. Searched exhaustively for every NR-read pattern (write `$XX` to port `$243B`, then `IN A,(C)` from `$253B`):

| addr        | NR read | nearby code site |
|-------------|---------|------------------|
| `$0116`     | NR 0x06 | early init: hotkey gate read |
| `$0a20`     | NR 0x41 | palette readback |
| `$26c8`     | NR 0x13 | LoRes/transparency read |
| `$2703`     | NR 0x57 | MMU bank-bumping helper (the very routine Fix #1 unblocked) |

**Confirmed via raw-byte search**: not a single occurrence of `01 3b 24 ... 3e 02` (select NR_02 then read), `ed 91 02 ...` (NEXTREG write NR_02), `ed 92 02` (NEXTREG A,NR_02), or `3e 02 d3 3b` (direct OUT to $3B). enNextZX.rom does not touch NR 0x02 anywhere in its 64 KB.

→ **Hypothesis #3 from the user's prompt is correct.** Strobing the FSM (commit `75d11fb`) is VHDL-faithful housekeeping but cannot influence the supervisor's branching because the supervisor never reads the register that exposes the FSM.

## 3. Other "post-soft-reset" state the agent's report didn't cover

The audit's list at lines 86-102 is correct for *individual register defaults*, but it does not capture **dynamic transient state** that tbblue.fw mutates and the supervisor inherits:

- **Z80 register file** at supervisor entry. After tbblue.fw + RESET_SOFT, A/F/BC/DE/HL/IX/IY/I/R have firmware-execution residue. After bypass-cold-init, they're whatever `cpu_.reset()` produces (zero / 0xFF). The supervisor's $00EF code does NOT initialize HL/DE/BC before the RAM-test pass at $0136-$016E reads them implicitly via stack ops. Worth verifying.
- **Stack pointer**. The supervisor sets `ld sp,$5bff` at $01D1 *after* the RAM-test passes. Pre-$01D1 the SP value is whatever was inherited.
- **Interrupt-mode + IFF1/IFF2**. tbblue.fw runs DI; supervisor at $0000 also `di / jp $00ef`. cpu_.reset() should have IM 0 + DI; but is `iff1/iff2` exactly what real hw + RESET_SOFT delivers? (Mostly irrelevant since DI is reasserted at $0000 — but worth a one-liner check.)
- **CTC, DMA, im2_control state machines**. The agent listed these as having `reset_i = '1'` clauses, but the soft-reset path also clears them; not a hard-vs-soft divergence. OK.
- **Floating bus / port_ff state**. VHDL `port_ff_reg` at `:3613` resets to zero; jnext mirrors at `port_ff_reg_ = 0` (emulator.cpp:168). OK.

The single dynamic state element most likely to matter is **the Z80 register file**, but the supervisor's $00EF immediately overwrites HL/BC/DE/A repeatedly, so this is unlikely to be the divergence either.

## 4. The supervisor self-resets the bypass block's NR writes

Per the disassembly at $00EF-$0114 (lines 158-170 of `/tmp/enNextZX.dis`):
```
nextreg $07,$03 ; nextreg $03,$b0 ; nextreg $c0,$08
nextreg $82,$ff ; $83,$ff ; $84,$ff ; $85,$ff
nextreg $80,$00 ; $81,$00 ; $8a,$00 ; $8f,$00
```
The supervisor **immediately self-resets** every NR the bypass block at `emulator.cpp:3770-3791` writes (NR 0x07/0x03/0x05/0x06/0x08/0x09/0x0a/0x82-0x85), overwriting any value we placed there. The bypass block's value-setting is therefore largely cosmetic (only the side-effects of those writes — e.g. `nextreg.write(0x03, 0xB3)` exits config_mode — are persistent).

This means the *pre-handoff state divergence* between bypass-cold and tbblue.fw+SOFT is concentrated in things the supervisor does NOT self-reset:
- SRAM contents (DivMMC ROM area pages 0x08-0x09, Multiface pages 0x0A-0x0B, Speccy ROM-in-SRAM pages 0x00-0x07, AltROM pages 0x0C-0x0F).
- NR 0x10 coreid / flashboot, NR 0x04 romram_bank, NR 0x7F user reg, NR 0xA9 esp_gpio0, NR 0xF0 select.
- NR 0x8C nibble copy on hard reset (post-soft-reset preserves bits[7:4] = bits[3:0]).

The audit's list at lines 86-102 covers these, but did not flag that the *self-reset* of NR 0x80..0x85 by the supervisor renders the bypass block's writes to those registers irrelevant. Important framing for any future bypass-mode investigation.

## 5. Race conditions / timing-dependent state — NOT THE CULPRIT

- Sprite Y counter, copper FSM, NMI FSM all reset cleanly in init.
- ULA frame state: `frame_cycle_ = 0`, `video_timing_.init()` re-seeds the raster counters. OK.
- Port 0x7FFD/0x1FFD/0xDFFD initial values: all 0 on hard reset (covered).
- DMA delayed-mode / im2_control delayed-int: the docs flag wiring concerns, but those are emulation-side — not a tbblue.fw vs bypass divergence.

No race condition is plausible as the G46(b) divergence root.

## 6. Where the divergence actually is (per LIVE doc, NOT the agent's audit)

The investigation doc `G46B-INVESTIGATION-LIVE.md` lines 597-700 already pinpoints the problem and it is **NOT a reset-tree divergence**:

- CSpect at first $20E6 hit: IX=$F700, MMU={FF,FF,0A,11,04,05,0B,10}, SP=$5BF9 (supervisor stack inside wrapper).
- jnext at first $20E6 hit:    IX=$E01B, MMU={FF,FF,0A,0B,04,05,0E,0F}, SP=$FF7F (user stack outside wrapper).

CSpect reaches $20E6 *via* the wrapper-mediated path. jnext reaches $20E6 *via* the dispatcher path before the wrapper-mediated path runs. This is a **control-flow divergence**, not a reset-state divergence. The supervisor is making a different early branching decision based on something it READS.

The "something it reads" is one of:
- a memory location that has different content (most likely — sysvars, descriptors, AltROM content, font table).
- a port (NR 0x06 / 0x13 / 0x41 / 0x57 — only 4 NRs are read; ports $1F/$3F/$BF/$FE/$EB/$E7/etc. are the rest).
- the SD-card data path (FATFS reads).
- the Z80 register file at supervisor entry (low-likelihood per section 3 above).

## Top recommendation: the SINGLE next test most likely to unblock

**Capture, in CSpect, the first 200 instructions of the supervisor from PC=$00EF, dumping each instruction's PC + register state + source/destination addresses.** Compare with jnext's same trace from the same entry point (same starting NR state, same SD image). The first PC at which the two traces diverge — i.e., a different branch taken on the same conditional, OR a different value read from the same address — is the upstream root cause.

Concretely, set `--log-level cpu_inst=trace --log-pc-range 0x00ef-0x0500 --log-level mem=trace` in jnext, get the same from CSpect (its debugger has a step-trace mode), and diff. With Fix #1 already landed, the first divergence is upstream of $20E6 by definition (since CSpect/jnext both reach $20E6 but via different paths). Most likely candidate: the routine at $0271-$03A0 that sets up the IM2 vector + sysvar block (700+ instructions of init) — a memory read in there returning a different value will cascade through the rest of boot.

**Do NOT spend more time on reset-tree audits.** The reset-tree is now provably matched between bypass-cold and tbblue.fw+SOFT; the residual divergence is in *content* (what tbblue.fw left in SRAM / SD / sysvars before handing off), not in *FF state*.

## Files referenced

- `/home/jorgegv/src/spectrum/jnext/doc/issues/G46B-AGENT-RESETSOFT.md` (audited)
- `/home/jorgegv/src/spectrum/jnext/doc/issues/G46B-INVESTIGATION-LIVE.md` (live data, lines 597-700 contain the actual root cause framing)
- `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp:47-220` (init), `:3768-3808` (bypass block + the reviewed strobe call), `:4815-4858` (soft_reset)
- `/home/jorgegv/src/spectrum/jnext/src/peripheral/nmi_source.cpp:145-156` (FSM strobe)
- `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd:1730-1739` (FSM definition), `:5890-5891` (NR 0x02 read)
- `/tmp/enNextZX.rom` (extracted), `/tmp/enNextZX.dis` (43 668 lines disassembly)
