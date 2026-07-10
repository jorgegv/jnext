# zx_go comparison — NextZXOS native-boot fix plan (2026-07-09)

**Reference emulator**: [zx_go](https://github.com/conorarmstrong/zx_go) (Go, MIT-licensed
emulator; GPLv3 bundled loader) — cold-boots NextZXOS **end-to-end through the authentic
FPGA boot chain**: FPGA bootrom → TBBLUE.FW splash → NextZXOS welcome → main menu.

**Empirically verified in this investigation** (headless, screenshots on file in the
session scratchpad): zx_go boots to the NextZXOS welcome screen and main menu
(Browser/Command Line/NextBASIC/…, 1792K, live RTC) using:

- the **byte-identical bootrom** jnext embeds — sha256
  `ee0b99c56b2c4cf72152662288f274483f8380865df0cbdcf3774f7974494798` for BOTH
  zx_go's `pkg/roms/data/tbblue_loader.rom` and jnext's `roms/nextboot.rom`
  (zx_go's LICENSES/tbblue_loader-NOTICE.md even cites jnext by name);
- ROMs and SD content extracted from **our canonical image**
  `roms/nextzxos-1gb-fat32fix.img` (`enNextZX.rom`, `enNxtmmc.rom`, TBBLUE.FW,
  machines/, nextzxos/, sys/ tree).

## Two prior jnext conclusions are refuted

1. **G46(b) Layer 7 ("`nextboot.rom` is the wrong/outdated asset") is refuted.**
   zx_go boots with the byte-identical ROM. CSpect's different slot-0 boot code is a
   CSpect-specific architecture (DivMMC-ROM-overlay style), not evidence our IPL is wrong.
   The bug is in jnext's emulation of what this ROM needs, not the asset.

2. **G46(b) Layer 6 ("VHDL-faithful machine_type after tbblue.fw's `$B0` write is ZX48
   for both emulators; CSpect must deviate from VHDL to boot") is refuted.**
   The VHDL powers on `nr_03_machine_type = "011"` (+3), verified:

   ```vhdl
   -- zxnext.vhd:1099-1103
   signal nr_03_machine_timing   : std_logic_vector(2 downto 0) := "011";
   ...
   signal nr_03_machine_type     : std_logic_vector(2 downto 0) := "011";
   ```

   and the `"011"` decode falls in the `machine_type_p3` branch (zxnext.vhd:5748-5754).
   None of the NR $03 fields appear in the reset block (zxnext.vhd:4930-5112), so they
   survive every reset. tbblue.fw's `NR $03 ← $B0` write (bits 2:0 = 000 = "no change")
   therefore leaves a VHDL-faithful machine in **+3 mode with 2-bit `sram_rom`** — which
   is what CSpect and zx_go both do. jnext cold-boots `--machine next` with
   `typ_sel = 0x04` (Pentagon family, 1-bit `sram_rom`) via `src/core/emulator.cpp:379-409`,
   diverging from the VHDL power-on state. This invalidates the input assumption behind
   the old Layer-5/Layer-6 analysis.

## Current native-boot stall (re-measured on main @ 086f89e1, 2026-07-09)

The native path (`./build/jnext --machine next --sd-card roms/nextzxos-1gb-fat32fix.img`)
now gets **much further** than the parked G46(b) docs describe:

1. Boot ROM (IPL) runs; SD init OK; TBBLUE.FW loads and runs.
2. tbblue.fw streams the ROMs/modules through config-mode NR $04 pages (this works —
   the write fall-through path `mmu.h:377-476` + `(nr_04 << 1) | slot` routing is correct).
3. NextZXOS commits `+3` machine type via `NR $03 ← $B3` from PC=$6D31 (this is
   NextZXOS's **staging soft-reset** site), then soft-resets via NR $02, sets 28 MHz.
4. **Death**: CPU ends at `$0000` executing `00 18 FD` = `NOP; JR $0000`, interrupts
   never dispatch, screen stays blank gray.

The trap bytes are identified: they are the **first bytes of `enNextZX.rom` bank 2**
(the ROM's own "you should never execute this" sentinel). NextZXOS paged ROM 2 and
jumped to $0000 **expecting the DivMMC automap to intercept the fetch** (post-reset EP
defaults `B8=$83 B9=$01 BA=$00` make $0000 a *delayed_on, main-path* entry point —
zxnext.vhd:2848-2909, 5087-5090) and land in esxDOS. In jnext the automap did not fire.

## How the boot is *supposed* to work (per zx_go + VHDL, the missing links)

The critical invisible machinery around the staging soft-reset:

- **NR $02 reset-type**: 3-bit shift history, power-on `100`, soft reset shifts
  `0 & old(2) & (old(1)|old(0))`, reads expose bits 1:0 → NextZXOS reads **$02** on its
  first (staging) pass and **$01** after — this decides whether it installs the menu
  engine and issues the $6D31 staging soft-reset. *(jnext: implemented and correct —
  `src/peripheral/nmi_source.{h,cpp}`.)*
- **Soft reset re-arms** MMU pages to `FF FF 0A 0B 04 05 00 01`, DivMMC EPs to
  `$83/$01/$00/$CD`, deasserts SPI CS, resets CPU speed; **preserves** all NR $03
  fields and NR $0A bit 4 (automap enable). Bootrom re-arms iff config_mode=1.
  *(jnext: matches on all of these per audit — see delta table.)*
- **NR $0A bit 4** (DivMMC automap enable) is set **by NextZXOS itself** during bank-0
  init (zx_go pins this at PC≈$023E) and must survive the staging soft-reset so that
  the post-reset `JP $0000` automaps into esxDOS.
- **Z80 soft-reset register-file semantics**: t80n resets PC/AF/AF'/I/R/SP(=FFFF)/IFF/IM
  and **preserves BC/DE/HL/BC'/DE'/HL'/IX/IY** (t80n.vhd:429-447; register-file reset
  branch is empty). zx_go documents NextZXOS/NextBASIC dereferencing `(IX+$1F)`
  immediately after the reset.
- **NR $00 machine ID**: VHDL generic default **$0A** (`zxnext_top_issue5.vhd:34-35`).
  zx_go documents (VHDL_CONFORMANCE.md, CHANGELOG v1.x) that returning $08 ("emulator")
  made NextZXOS's ROM1 check at $1E69 take an **emulator branch that broke the boot**,
  fixed by returning $0A.

## Verified delta table (jnext vs working reference, 2026-07-09)

Audited by a dedicated agent, independently reviewed. Ordered by likely boot impact.

| # | Behavior | jnext status | Evidence |
|---|----------|--------------|----------|
| 1 | NR $00 machine-ID readback | **DIFFERS** — hardcoded $08; VHDL/zx_go/real HW = $0A. NextZXOS branches on it ($1E69). jnext's comment (`nextreg.cpp:212-225`) claims $0A breaks emulation — zx_go proves the opposite | `emulator.cpp:857-865` |
| 2 | Z80 soft-reset register file | **DIFFERS** — `fuse_z80_reset(1)` zeroes BC/DE/HL/BC'/DE'/HL'/IX/IY; t80n preserves them (SP=FFFF matches) | `z80_cpu.cpp:391`, `fuse_z80_core.c:95-114`, t80n.vhd:429-447/1495 |
| 3 | Cold NR $03 machine_type | **DIFFERS** — `typ_sel=0x04` (Pentagon family, 1-bit sram_rom) vs VHDL power-on `"011"` (+3, 2-bit). Masked during config mode; corrected by guest's own $B3 commit — pre-commit fidelity + invalidates old Layer-6 analysis | `emulator.cpp:379-409`, zxnext.vhd:1099-1103 |
| 4 | SD card on both SPI CS lines | **DIFFERS** — same `SdCardDevice` attached to CS0 **and** CS1 → phantom second card; zx_go: card answering slot-1 probe made NextZXOS loop forever mounting it | `emulator.cpp:5123-5124`, zx_go spi.go:231-277 |
| 5 | SPI Ncr (idle bytes before R1) | **DIFFERS** — 1 idle byte; reference measured exactly 2. tbblue.fw uses fixed-count readers in places | `sd_card.cpp:979-985`, zx_go spi.go:549-562 |
| 6 | CMD58 OCR response bytes | **DIFFERS** — OCR $C0FF8000 contains an $FF byte; tbblue.fw's response reader *skips* $FF bytes → misalignment risk (zx_go: caused the wrong core-load branch) | `sd_card.cpp:944-952`, zx_go spi.go:448-473 |
| 7 | CMD18 stream across CS deassert | **DIFFERS** — jnext aborts multi-read on deselect; real cards (and reference) keep the stream open across driver calls | `sd_card.cpp:97-100`, zx_go spi.go:248-259 |
| 8 | Data-block CRC-16 | **DIFFERS** — dummy $00 $00 (esxDOS reads them; reference implements real CRC-16/CCITT) | `sd_card.cpp:372-375` |
| 9 | Post-reset $0000 automap decode | MATCHES (delayed_on, main path) — but gated on NR $0A bit 4, which only the guest sets | `divmmc.cpp:263-501` |
| 10 | NR $02 reset-type readback | MATCHES (100→010→001, reads $02 then $01) | `nmi_source.cpp:169-188` |
| 11 | Soft-reset NR/MMU/EP/SPI semantics | MATCHES (MMU pages, DivMMC EPs, CS deassert, NR $03 + NR $0A b4 preserved, bootrom re-arm iff config_mode) | `mmu.cpp:131-158`, `divmmc.cpp:31-48` |
| 12 | Bootrom write fall-through to NR $04 page | MATCHES | `mmu.h:377-476` |
| 13 | NR $04 write-anytime latch | MATCHES | `emulator.cpp:2580-2599` |
| 14 | NR $8E (bit3 MMU6/7 gate, bit7→$DFFD) | MATCHES | `mmu.cpp:695-763` |
| 15 | I2C DS1307 RTC ACK ($103B/$113B) | MATCHES (full slave FSM at 0x68) | `i2c.cpp:314-350`, `emulator.cpp:5108` |
| 16 | NR $10 core-id readback | MATCHES | `emulator.cpp:1450-1460` |

## Fix plan (prioritized)

> All five key findings were independently reviewed (verdicts in the Review-status
> section below). The reviewer's main discipline: none of the divergences is *proven*
> to be the proximate cause of today's $0000 trap without a runtime trace — hence P0.

### P0 — Discriminating trace at the failing fetch (diagnostic, do FIRST)

Env-gated probe capturing, at the first M1 fetch of PC=$0000 after the NR $02 soft
reset: `divmmc automap_active_`, `nr_8c_altrom_lock_rom1_/rom0_`, `machine_type_`,
`port_7ffd_`, `port_1ffd_`, NR $0A bit 4, and the resolved `rom_page` from
`current_sram_rom()`. This discriminates the two candidate mechanisms for the trap:

- **(A) DivMMC automap not firing** (NR $0A bit 4 = 0 at that moment — NextZXOS's
  enable write missing, possibly downstream of the machine-ID emulator branch), vs
- **(B) NR $8C altrom-lock residue** (reviewer-found alternative): the lock bits are
  nibble-preserved across reset (zxnext.vhd:2253-2256); `(lock_rom1,lock_rom0)=(1,0)`
  yields `sram_rom=2` → rom_page 4 → file offset $8000 — **exactly the observed
  bank-2 trap** with no DivMMC involvement.

Also log all guest NR $0A and NR $8C writes during the boot to see what the firmware
actually staged. Only after this trace should the P1/P2 fixes be credited or blamed
for the stall.

### P1 — NR $00 machine ID → $0A (one line + test)

Change the NR $00 read to return $0A (`HWID_ZXNEXT`), matching VHDL
`g_machine_id = X"0A"` and both working references. Re-run the native boot.
**Why first**: cheapest; NextZXOS demonstrably branches on it; the "emulator branch"
plausibly skips the NR $0A automap-enable write whose absence explains the $0000 trap.
The old jnext comment claiming $0A breaks emulation must be re-tested — it predates
the rest of these fixes and zx_go disproves it in context.

### P2 — Soft reset must preserve the Z80 register file

Stop calling `fuse_z80_reset(1)` semantics for NR $02 soft reset; reset only
PC/AF/AF'/I/R/IFF/IM and force SP=$FFFF, preserving BC/DE/HL/BC'/DE'/HL'/IX/IY
(t80n.vhd:429-447). NextZXOS's post-staging-reset path dereferences (IX+$1F).

### P3 — Cold-boot NR $03 = VHDL power-on state ("011"/+3, config_mode=1)

Make `--machine next` cold-init leave `nr_03_machine_type/timing` at the VHDL
power-on `"011"` instead of forcing `typ_sel=0x04`, so pre-commit `sram_rom` is
2-bit/+3 exactly as on silicon. (The guest's $B3 commit later makes this explicit.)
Re-audit `current_sram_rom()` behavior in config-mode window afterwards.

### P4 — Second SPI slot must be empty

Attach the SD card to CS0 only; CS1 returns no-card (all-$FF). Prevents the
phantom-second-card mount loop zx_go documents.

### P5 — SPI/SD byte-level conformance batch

(a) Ncr = 2 idle bytes before R1; (b) OCR response voltage bytes $00 (avoid $FF
inside R3 payload); (c) keep CMD18 stream open across CS deassert; (d) real
CRC-16/CCITT on data blocks. Each with a discriminative unit test against the
zx_go-documented byte sequences.

### P6 — Diagnostic checks (before/alongside the fixes)

1. **Log NR $0A writes** during native boot (env-gated probe): confirm whether
   NextZXOS writes bit 4 = 1 pre-staging-reset (expected at PC≈$023E). If absent under
   machine-ID $08 and present under $0A, P1 is confirmed as the root unlock.
2. **Trace the last 50k instructions before the $0000 jump** (existing `JNEXT_TRACE_*`
   infra): identify the JP/RET that lands on $0000 and the ROM page mapped.
3. **zx_go as live oracle**: the GUI-faithful headless harness recipe (scratchpad
   `boot_probe_test.go`) boots to menu in ~5 s wall; instrument it to dump PC streams /
   NR-write logs for lockstep comparison with jnext's TraceLog (same format as the
   CSpect symmetric-trace methodology, but open-source and scriptable).
4. After P1-P4, re-run the triplet + the native boot with screenshots at 5/10/30 s.

### Post-boot hardening (after the banner draws)

- Menu idle needs the RTC ACK (already OK) and Multiface ROM at NR $06 bit 3 semantics.
- zx_go's war-story list (VHDL_CONFORMANCE.md, CHANGELOG) is a ready-made regression
  checklist for the *next* phase (NEX loading, 128K personality boot, Browser).

## The bypass path (Task 18) in light of this

The `--bypass-tbblue-fw` stall (page-probe loop $0192-$01C9, banner never drawn) now has
a strong candidate explanation that is *neither* of the two parked hypotheses: the bypass
skips tbblue.fw, so **everything tbblue.fw installs is missing** — zx_go documents
concretely: the IRQ stub at DivMMC RAM bank 1 offset $2009 (NextZXOS's $0044-$0060 IRQ
handler calls into it every frame for keyboard scan/esxDOS maintenance; without it
"boot reaches IRQ idle but doesn't progress"), and valid handler code in the 8K bank at
slot 7 (post-sweep RST $20 dispatch to $ED82 “lands in cold-RAM garbage”). The **native
path is now the right horse**: once it boots, the bypass becomes unnecessary (zx_go
deleted their equivalent warm-boot machinery after their cold boot worked).

## zx_go reference recipes (for future differential debugging)

- Headless boot-to-menu: see scratchpad `zx_go/pkg/testharness/boot_probe_test.go`
  (GUI-faithful wiring; the stock testharness `newNext()` cannot boot — 13 wiring gaps
  documented in its header comment). Env: `ZX_GO_NEXT_SD_DIR=<extracted SD tree>`;
  ROMs in `<repo>/roms/next/`.
- ROM extraction from our canonical image:
  `mcopy -i 'roms/nextzxos-1gb-fat32fix.img@@32256' ::/MACHINES/NEXT/ENNEXTZX.ROM …`
- zx_go's `VHDL_CONFORMANCE.md` is an enumeration-table conformance audit in exactly
  our Task-2 style — directly reusable as a checklist source.

## Review status (independent review pass, 2026-07-09)

An independent reviewer agent attempted to refute the five key findings against jnext
HEAD, zx_go source, the VHDL, and the actual `enNextZX.rom` binary extracted from our
SD image. Verdicts:

| Finding | Verdict | Notes |
|---------|---------|-------|
| NR $00 = $08 vs VHDL $0A | **CONFIRMED** | Deliberate deviation, admitted in jnext source comments; VHDL `g_machine_id = X"0A"` in all three top-level variants; zx_go fixed the identical bug to boot. The "$1E69 fork" address could not be reproduced against our ROM build (sub-detail unverified; mechanism solid). |
| $0000 trap = bank-2 sentinel via missing automap | **PARTIALLY CONFIRMED** | Trap bytes (`00 18 FD` at file offset $8000 = bank 2) and the NR $0A gate are confirmed. Causal attribution UNPROVEN: reviewer found an equally plausible alternative — NR $8C altrom-lock bits survive reset (nibble-copy, zxnext.vhd:2253-2256) and `(1,0)` locks produce `sram_rom=2` → rom_page 4 → the same bank-2 offset. Needs the P0 runtime trace to discriminate. |
| Soft reset zeroes Z80 register file | **CONFIRMED** | `soft_reset()` → `init(preserve_memory=true)` → unconditional `cpu_.reset()` → `fuse_z80_reset(1)` zeroes BC/DE/HL/BC'/DE'/HL'/IX/IY; t80n.vhd:1493-1498 has an explicitly empty reset branch for the register file. Real bug, fix regardless — but cannot be the proximate cause of a trap on the very first post-reset fetch. |
| Cold NR $03 typ_sel=$04 vs VHDL "011" | **CONFIRMED** | Pre-commit-only: skipped on soft reset and superseded by the guest's own $B3 commit. Fidelity fix; does not explain today's stall. |
| SD card on both CS lines | **CONFIRMED** (bug) | jnext's "VHDL-faithful" justification only covers the FPGA-side MISO mux, not card behavior; zx_go documents the phantom-second-card infinite mount loop. Ordering caveat: NextZXOS's dual-slot probe runs early, and today's boot got past SD init — so this is likely LATENT here, biting at a later stage (Browser/menu SD access). Fix regardless. |

Reviewer's overall discipline, adopted in this plan: **run P0 before crediting any
single fix with the stall**; P2 (register file) and P4 (dual CS) are confirmed bugs to
fix regardless of what P0 shows; P1 (machine ID) is a deliberate design choice whose
old justification must be re-tested under the new evidence, with its own regression
coverage.

---

# RESOLUTION (2026-07-10) — NextZXOS BOOTS NATIVELY

Implemented on branch `nextzxos-boot-fixes` (commit `ee2d8910`). jnext now
cold-boots NextZXOS through the authentic chain — FPGA bootrom → TBBLUE.FW
→ **NextZXOS welcome screen → main menu** (verified by headless screenshots,
`--delayed-keypress 26 space` reaches the Browser/Command Line/NextBASIC menu
with live RTC).

## The actual root cause (found via P0 + symmetric zx_go trace)

None of the plan's P1-P5 candidates was the proximate killer. The
discriminating instrument was the **symmetric instruction-trace diff against
zx_go booting the same bootrom + the same SD image** (zx_go harness patched
to serve `nextzxos-1gb-fat32fix.img` verbatim — env `ZX_GO_NEXT_SD_IMAGE`).
Method: align both traces at the staging soft reset (PC=$6D31), uniq-collapse
HALT repeats, walk back from jnext's trap to the last common window.

The fork: `LD SP,($DA35)` at $1BEC loaded $5F2F in jnext vs $5BF5 in zx_go —
a **memory divergence** in NextZXOS's saved-SP variable. The writer hunt
(env-gated logical + physical byte watches) showed no logical writer:
**`Mmu::to_sram_page` aliased logical page 0x0E (bank-7 BRAM,
zxnext.vhd:2962 `mem_active_bank7`, `sram_pre_active` gated off at
:3039-3041/:3061) onto PHYSICAL SRAM page 0x0E — the alt-ROM upper half.**
NextZXOS's alt-ROM install (NR $8C=$C0 write-over observed at PC=$0E3B/$007F)
overwrote its own MMU-page-14 workspace; the corrupted saved SP made a later
RET pop $0000 → enNextZX bank-2 sentinel (`00 18 FD`) → the observed trap.

Fix: the bank-7 BRAM stand-in now lives at SRAM page **0x2E** — the page's
natural +0x20 shift target, which is dead space on real hardware (only MMU
page 0x0E computes address 0x2E and the BRAM answers instead of SRAM).
ULA shadow screen + tilemap bank-7 fetches updated to match
(`src/memory/mmu.h`, `src/video/ula.cpp`, `src/video/tilemap.cpp`).

## Shipped alongside (VHDL-conformance, from the plan)

- **P1** NR $00 machine ID → $0A (`emulator.cpp`, `nextreg.cpp`).
- **P2** Soft reset preserves the Z80 register file (`z80_cpu.{h,cpp}`,
  `fuse_z80_reset(0)` = exact t80n semantics).
- **P4** SD card on SPI socket 0 only.
- **P5(c)** CMD18 stream survives CS deassert (`sd_card.cpp`) — this WAS on
  the boot path (NextZXOS closes a pre-reset-era stream with CMD12 post-reset).
- **DivMMC RAM = physical SRAM pages 16-31** (`divmmc.{h,cpp}` ram backing) —
  makes tbblue.fw's config-window installs visible to the runtime overlay
  (zx_go gap #1; prerequisite for the $3D00 trampoline dispatch).

## Deliberately deferred

- **P3** (cold NR $03 = "011"): pre-commit fidelity only; entangled with the
  MachineType enum; needs its own pass.
- **P5(a/b/d)** (Ncr=2, OCR $00 voltage bytes, real CRC-16): not boot-blocking
  for this image; still real conformance gaps worth a follow-up batch.
- L2 write-over uses the bank5/7-bypassing `to_sram_page`, but VHDL
  `layer2_A21_A13` (zxnext.vhd:2971) never bypasses — L2 banks 5/7 misroute.
  Rare corner (L2 over BRAM banks); follow-up fix + test.

## Verification

- Boot to welcome: headless screenshot at t=25 s; menu via
  `--delayed-keypress 26 space`, screenshot at t=33 s. RTC live, 1792K.
- Triplet: **ctest 41/41 • FUSE 1356/1356 • regression 43/0/0**.
- New/updated tests: mmu Cat28 BANK7-01..03 + BNK-03, divmmc §11 RB-01..03,
  sdcard CMD18-05, cpu soft-reset register-file, nextreg MID-01/RO-01/RO-02/
  SEL-03, ula S5.09 constants, tilemap BANK7 constant.

## Review (completed 2026-07-10) — APPROVED

Three-round independent review of the full branch:

1. Round 1 (on ee2d8910): APPROVE-WITH-NITS + one MAJOR — the "SRAM page
   0x2E is dead space" claim was refuted (config-mode NR $04=$17 reaches
   every SRAM page). Fixed properly in 910102d7: dedicated bank7_bram_
   buffer per the VHDL truth (bank7_ram dpram2, zxnext.vhd:6670), plus
   BANK7-04 exercising the real config-mode path; minor findings (getenv
   caching, stale doc rows, labels) fixed.
2. Round 2 (on 910102d7): REJECT — BLOCKER: the redirect was unconditional
   and broke standalone-128K/+3 bank-7 RAM (reviewer reproduced with a
   probe against the built library). Fixed in 395b81e7 (rom_in_sram gate +
   Next-only ULA/tilemap wiring + BANK7-05).
3. Round 3 (on 395b81e7): APPROVE-WITH-NITS + required follow-up — stale
   rom_in_sram on live machine-type switch (pre-existing). Fixed in
   8148e958 (+ SWITCH-01/02).
4. Final ACK (on 8148e958): **APPROVE, no residual required items.**

## Remaining follow-ups (non-blocking)

1. The mid-boot visual glitch the user observed (parked; suspects: L2
   bank5/7 routing corner, splash-era rendering).
2. P3 + P5(a/b/d) + the L2 bank5/7 routing fix, each with tests; plus the
   bank-5 (pages 0x0A/0x0B) same-class config-window ticket from review.
3. Boot-to-menu regression test in the screenshot suite (welcome + menu
   references) so the native boot never regresses silently.
4. Retire `--bypass-tbblue-fw` (zx_go removed their warm-boot equivalent
   once cold boot worked); keep the diagnostics.
