# G46(b) NextZXOS boot loop — live investigation

> **Branch**: `g46b-investigation` (off `89e18de`). Main stays clean.
>
> **Instrumentation** committed on this branch as `5f8cca0`:
> - `cpu_inst` log channel (permanent — keep on closure)
> - `JNEXT_G46B_PATCH` env-var (TEMP — remove on closure)
> - `JNEXT_G46B_INJECT` env-var (TEMP — remove on closure)
> - `JNEXT_G46B_WATCH` env-var (TEMP — remove on closure)
>
> All of the above were added 2026-05-04 during this investigation.

---

## State as of 2026-05-04 23:30 (start of automode investigation phase)

### What's known

1. **Loop closure**: The boot loop closes at enNextZX.rom $279d-$27ab. Both `ret c` and stack-switch RET pop $0000 from their respective stacks → JP $00EF → next iteration.

2. **Brute-force inject confirmed**: A one-shot inject of `aa 23 74 02` at user-stack saved_SP at the first PC=$279d hit makes the loop pattern STOP (`CPU speed changed` events drop from 65/run to 2/run; firmware then runs 17+ sec silently). So the divergence IS in the user-stack content (slot 7 banked memory).

3. **Slot 7 mapping diverges between hits**:
   - HOOK #1 PC=$2734: nr_mmu_[7] = **0x01** (per `Mmu::get_page(7)`)
   - HOOK #2 PC=$279d: nr_mmu_[7] = **0xDF**
   - User stack at $ff4f in bank 0x01 = `aa 23 74 02` ✓
   - User stack at $ff4f in bank 0xDF = `00 00 00 00` ✗ (uninit / wrong bank)

4. **CRITICAL DISCREPANCY (just discovered, not yet confirmed)**:
   - HOOK #1 says nr_mmu_[7] = 0x01 at PC=$2734.
   - 13 instructions later, the firmware enters the NR-swap routine at $27de.
   - Routine does `IN A,($253B)` to read CURRENT NR_57.
   - The returned old-NR_57 (visible as H byte of $5b8a after `ld ($5b8a),hl` at $2747) is **0x05**, NOT 0x01.
   - **Either**: (a) `Mmu::get_page(7)` is stale relative to the actual port-read NR_57, or (b) the IN A,(C) reads from a different state than nr_mmu_[7] (e.g., regs_[0x57] != nr_mmu_[7]), or (c) the firmware writes NR_57 = 0x05 between $2734 and $27de (no NR writes visible in that range in the disasm).
   - This is the **likely root cause direction**.

### Wrapper architecture (enNextZX.rom)

```
ENTRY ($2730-$274a):
  [2730] (some preceding code)
  [2732] add hl,sp        ; HL = HL + SP
  [2733] ld a,h
  [2734] cp $5c           ; ← HOOK #1 fires here (NR_57=0x01)
  [2736] jr c,$2740       ; if A < $5c, no stack switch
  [2738] ld hl,($5b6a)
  [273b] ld ($5b6a),sp
  [273f] ld sp,hl
  [2740] ld ($5b8e),a
  [2743] push bc
  [2744] call $27de       ; NR-swap: HL=$100b → set new NR pair, returns OLD in HL
  [2747] ld ($5b8a),hl    ; ← WATCH: $5b8a = 0x0504 (old NR_56=0x04, old NR_57=0x05 ✗)
  [274a] ret

NR-SWAP at $27de:
  [27de] ld hl,$100b      ; HL = (L=0x0b new NR_56, H=0x10 new NR_57)
  [27e1] push bc
  [27e2] ld bc,$243b      ; BC = NR select port
  [27e5] out (c),d        ; D=$56, select NR 0x56
  [27e7] inc b            ; BC = $253b (NR data port)
  [27e8] in a,(c)         ; A = current NR_56
  [27ea] out (c),l        ; write 0x0b to NR 0x56
  [27ec] ld l,a           ; L = old NR_56
  [27ed] dec b
  [27ee] inc d            ; D = 0x57
  [27ef] out (c),d        ; select NR 0x57
  [27f1] inc b
  [27f2] in a,(c)         ; A = current NR_57   ← reads 0x05 not 0x01!
  [27f4] out (c),h        ; write 0x10 to NR 0x57
  [27f6] ld h,a           ; H = old NR_57
  [27f7] pop bc
  [27f8] ret              ; HL = (L=old_NR_56, H=old_NR_57)

EXIT ($2792-$27ab):
  [2792] ld hl,($5b8a)    ; load saved old NR pair
  [2795] ld a,l
  [2796] nextreg $56,a    ; restore NR 0x56
  [2799] ld a,h
  [279a] nextreg $57,a    ; restore NR 0x57 (= 0xDF in our trace, NOT 0x01)
  [279d] ld a,($5b8e)     ; ← HOOK #2 fires here (NR_57=0xDF now)
  [27a0] cp $5c
  [27a2] ret c            ; if shallow, ret on current stack
  [27a3] ld hl,($5b6a)
  [27a6] ld ($5b6a),sp
  [27aa] ld sp,hl
  [27ab] ret              ; pops (zeros) → $0000 → loop
```

## Investigation pipeline

### Round 1: NR_57 divergence (highest priority)

**Hypothesis**: regs_[0x57] (what `IN A,($253B)` returns) and nr_mmu_[7] (what `Mmu::get_page(7)` returns) are out of sync. The Mmu cache thinks NR_57 = 0x01 but the NextReg-side state thinks NR_57 = 0x05.

**Test**: Add diagnostic that, at HOOK #1, also dumps NR_57 via simulated port-read (`Io::in(0x253B)` after `Io::out(0x243B, 0x57)`). Compare with nr_mmu_[7].

**Owner**: Agent A (worktree)

### Round 2: confirm $0168 LDDR effect on $5b8a

**Question**: WATCH attributes $5b8a zeroing to PC=$0168 (the RAM-test inner LDDR), but LDDR's DE walks $FFFD..$C000 (slot 6/7), which shouldn't touch $5b8a in slot 2 (bank 0x0a). Either there's bank-aliasing or the WATCH attribution is misleading.

**Owner**: Agent B (Explore) — read-only RE.

### Round 3: trace what writes NR_57 = 0x01 between RAM-test end and HOOK #1

**Question**: After RAM-test ends, NR_57 = 0xDF (last value written). HOOK #1 at $2734 says NR_57 = 0x01. So somewhere in between, NR_57 is set back to 0x01. Where?

**Owner**: Agent C (Explore) — read-only RE.


---

## 2026-05-04 23:55 — ROOT CAUSE CONFIRMED

### Three parallel agents converged on the same diagnosis

**Agent A (NR_57 divergence verifier, worktree)**
- Extended diagnostic to add port-simulated `IN A,($253B)` read alongside `Mmu::get_page(7)`.
- Trace: HOOK #1 PC=$2734 shows `slot7 NR_57(mmu_cached)=0x01 effective=0x01 port_NR57(via $253B)=0xdf`.
- **Verdict: jnext has TWO storage locations for what VHDL has as ONE register**:
  - `Mmu::nr_mmu_[7]` — updated by NR 0x57 writes, NR 0x8E (bit 3=1) writes, port 7FFD writes (via `apply_legacy_ram_slots_`).
  - `NextReg::regs_[0x57]` — updated ONLY by NR 0x57 explicit writes.
- **Fix recommended**: install NR 0x50-0x57 read_handlers that delegate to `mmu_.get_page(i)` so reads return the live MMU register (matching VHDL).

**Agent B (NR_57=0x01 source — read-only RE)**
- No firmware instruction writes NR_57=0x01 between $01CC and $2734.
- Identified the actual writer: `apply_legacy_ram_slots_` clobbers `nr_mmu_[7]` to `port_7ffd_bank * 2 + 1` = `0 * 2 + 1` = **0x01**.
- Trigger: NEXTREG $8E,$08 at $01D7 (bit 3=1, after the RAM tests), and NR_8F=0x00 at $0113 (also calls apply_legacy_paging_).
- **Bonus finding**: $0168 LDDR write to $5B8A is REAL bank aliasing — at PASS-1 c=5, slot 6 = bank 0x0A which is also slot 2's reset bank. LDDR write to $D000-$DFFF (slot 6) lands in physical bank 0x0A which is also visible at $4000-$5FFF (slot 2). $DB8A (slot 6 + offset 0x1B8A) = $5B8A (slot 2 + offset 0x1B8A). Attribution timing is correct, bank aliasing is real. Harmless because this happens BEFORE the LDIR-to-$5B00 routine installs the trampolines.
- Fix shape: `apply_legacy_ram_slots_` should update only `slots_[]` + `rebuild_ptr()`, NOT `nr_mmu_[]`. Equivalent to Agent A's recommendation but on the WRITE side instead of the READ side.

**Agent C (call chain map — read-only RE)**
- Mapped the boot iteration call chain $0000 → $00EF → ... → $234B → $2415 RET → $23A5 → $23A7 jp $27BD → ... → $279D.
- Identified that the wrapper-exit bank-flip via `call $27D9` ($27E1-$27F8) DOES swap slot 7 back from saved value at $5B8A.
- Noted (correctly): "The 'bank 0xDF' framing is misleading: the firmware never executes from bank 0xDF" — bank 0xDF is just the residual NR_57 value at the end of RAM-test PASS-2.
- Suggested user-stack corruption hypothesis (LDIR at $2358 fills $a000-$a151, possibly hitting user SP) — **superseded by Agents A+B's MMU-cache divergence finding**.

### VHDL ground truth (verified via cores/zxnext/src/zxnext.vhd)

Line 6080-6082:
```
when X"57" =>
   port_253b_dat <= MMU7;
```

Lines 4670-4685 (NR 0x8E write effect):
```
elsif port_1ffd_special_old = '1' or port_memory_ram_change_dly = '1' then
   MMU6 <= port_7ffd_bank & '0';
   MMU7 <= port_7ffd_bank & '1';
end if;
```

Lines 4688-4699 (NR 0x57 write):
```
elsif nr_mmu_we = '1' then
   case nr_mmu is
      ...
      when others => MMU7 <= nr_wr_dat;
   end case;
end if;
```

Line 4618 (hard reset):
```
MMU7 <= X"01";
```

**Single register MMU7. Read returns it directly. Multiple write paths (NR 0x57, NR 0x8E bit 3, port 7FFD via port_memory_ram_change_dly, hard/soft reset).**

### The bug end-to-end

1. Iteration N start at $00EF. NR_8F = 0x00 at $0113 → `apply_legacy_ram_slots_` clobbers `nr_mmu_[7] = 0x01`. `regs_[0x57]` unchanged (still 0xDF from previous iteration's RAM-test).
2. RAM-test pass 1 ($0139-$014F) writes NR_57 = 0x01..0xDF directly. Both `nr_mmu_[7]` and `regs_[0x57]` end at 0xDF. **In sync.**
3. NEXTREG $8E,$08 at $01D7 (bit 3=1, port_7ffd_bank=0) → `apply_legacy_ram_slots_` clobbers `nr_mmu_[7] = 0x01` again. `regs_[0x57]` UNCHANGED (still 0xDF). **DIVERGED.**
4. Wrapper entry at $2734. EFFECTIVE slot 7 = bank 0x01 (per nr_mmu_[7]). User stack at $ff4f reads bytes from bank 0x01 = `aa 23 74 02` ✓.
5. Wrapper calls $27DE (NR-swap). `IN A,($253B)` for NR_57 returns `regs_[0x57] = 0xDF` (NOT the live MMU7 = 0x01). Saved at `$5b8a` H byte = 0xDF. **WRONG VALUE SAVED.**
6. Wrapper body runs (sets new bank pair, does work, etc.).
7. Wrapper exit at $279a writes `NEXTREG $57, 0xDF` (= H of $5b8a). Now `nr_mmu_[7] = regs_[0x57] = 0xDF`. Slot 7 maps to bank 0xDF.
8. Stack-switch RET at $27ab pops `mem[$ff4f]` from bank 0xDF (wrong bank — different physical RAM). Bytes are zeros. RET to $0000.
9. $0000 di / jp $00EF → next iteration. **LOOP.**

### The fix

`src/core/emulator.cpp` near line 1281 (inside the existing NR 0x50-0x57 write-handler registration loop), add a read handler that delegates to `mmu_.get_page(i)`:

```cpp
nextreg_.set_read_handler(static_cast<uint8_t>(0x50 + i),
    [this, i]() -> uint8_t {
        // VHDL zxnext.vhd:6075-6082 — port_253b_dat <= MMU<i>.
        // MMU<i> is the LIVE register, updated by NR 0x50-0x57 writes,
        // NR 0x8E with bit 3=1 (port_memory_ram_change_dly), port 7FFD
        // writes, and reset paths. In jnext, Mmu::nr_mmu_[i] is the
        // MMU<i> mirror updated by all these paths; delegate the NR
        // read so the firmware sees the live MMU value, not the stale
        // last-NR-write byte cached in NextReg::regs_[].
        return mmu_.get_page(i);
    });
```

**Expected outcome**: at wrapper entry, `IN A,($253B)` for NR_57 will return `mmu_.get_page(7) = 0x01` (the live MMU7). Saved at $5b8a H = 0x01. Wrapper exit restores NR_57 = 0x01. Slot 7 stays at bank 0x01. RET pops mem[$ff4f] from bank 0x01 = `aa 23` → $23aa. Boot progresses past the loop. **G46(b) closes.**

---

## 2026-05-05 00:30 — Fix #1 verified, Fix #2 partial, downstream bug remains

### Fix #1 (commit `04fe5bd`) — VERIFIED and SUFFICIENT for G46(b) closure

`src/core/emulator.cpp:1290-1314` — added NR 0x50-0x57 read_handlers delegating to `Mmu::get_page(i)`. Makes `IN A,($253B)` for NR 0x50-0x57 return the LIVE MMU register (matching VHDL).

**Verification**:
- Original loop iteration count: **65/run → 2/run** (= same as the manual JNEXT_G46B_INJECT bypass test).
- Per the diagnostic agent's verification: "the wrapper at `enNextZX.rom $2730-$27ab` now exits cleanly (158/158 wrapper invocations RET to valid firmware addresses `$23aa`/`$0274`, never to `$0000`)".
- Unit tests: **3850/3812/0/38** (no regressions).
- Full regression: **33/0/0** (no regressions).
- Firmware boot makes substantial progress past the wrapper — config-mode work, soft reset, post-reset re-init.

This is the canonical G46(b) closure.

### Fix #2 (commit `4c8a761`) — partial mitigation, exposed a downstream issue

`src/core/emulator.cpp:3651-3712` — load `/MACHINES/NEXT/enAltZX.rom` (32 KB) from SD into SRAM pages 0x0C-0x0F at init.

**Why needed**: post-Fix-#1, the firmware advances to `nextreg $8C,$80; ret` at $007B (enable AltROM, then return into the `nextreg $8C,$00; ret` mirror trampoline at the same address INSIDE the AltROM image). Without enAltZX.rom loaded, pages 0x0C-0x0F are all-zero → RET pops 0x0000 → NOP slide → crash.

**Verification**:
- Pre-fix: post-AltROM-trampoline screenshots all blank (3 colors).
- Post-fix: frame 600 shows boot progress indicator (color bars at bottom of screen, 11 colors). Visible firmware progress.
- Unit tests: 3850/3812/0/38 (no regressions).

### Remaining downstream issue (post Fix #1 + Fix #2)

Screenshots oscillate between "boot progress indicator" (frames 600, 1200) and blank (frames 800, 1000). Welcome screen never appears.

Investigation agent finding (worktree `agent-a106d7be4aa2d1d03`):
- The firmware enters a NEW loop AFTER Fix #1 + Fix #2.
- Loop body: NextZXOS RAM-test sweeps NR_57 = 0x01, 0x03, ..., 0xDF and writes patterns to $C000-$FFFF in each bank. **For C=6 (NR_56=0x0C, NR_57=0x0D) and C=7 (NR_56=0x0E, NR_57=0x0F), the RAM-test writes WIPE the AltROM pages we just pre-loaded.**
- After RAM-test, firmware does `nextreg $8C,$80; ret` at $007B expecting valid AltROM. Pages now zero → NOP slide → eventually pops 0x0000 → DI → JP $00EF → 5578 iterations / 18 sec.
- **Per agent**: "We see ZERO SD reads post-soft-reset — confirming NextZXOS never reaches the AltROM-reload step". On real hardware NextZXOS reloads enAltZX.rom from SD after the RAM-test (via FatFs/DivMMC). In jnext, the firmware loops before reaching this reload step.

### Options for the remaining issue (NOT fixed in this session)

1. **VHDL-faithful (recommended)**: find why NextZXOS doesn't reach the AltROM-from-SD reload step. Add tracing for the NextZXOS code path that calls `f_open` on `enAltZX.rom`. Some downstream emulation bug derails the firmware before it gets there.

2. **Pragmatic band-aid (NOT recommended per `feedback_vhdl_faithful_only.md`)**: re-load AltROM AFTER the RAM-test, e.g., on every NR 0x8C bit-7 set, OR by hooking RST $08 / RST $00 entry, OR by making `Mmu::altrom_sram_page_()` always return ROM data instead of routing through SRAM. All are band-aids.

3. **Defer**: wait for `--bypass-tbblue-fw` (G59) to land. With bypass, jnext could load NextZXOS directly without going through tbblue.fw's full boot, potentially side-stepping the issue.

### Status of session goal

**G46(b) — the original RAM-test/wrapper loop — is SOLVED via Fix #1.** 

The remaining "no welcome screen" symptom is a NEW, downstream bug (the AltROM-corruption-by-RAM-test path), not the original G46(b). Recommend tracking it as a separate ticket (e.g., G46(d) "AltROM reload from SD").

### TEMPORARY instrumentation status

Instrumentation in commit `5f8cca0` (parent of fixes) is:
- `cpu_inst` log channel — **PERMANENT** (per user direction 2026-05-04). Generic per-instruction trace, useful for any future investigation. Documented in `src/core/log.h:21-29`.
- `JNEXT_G46B_PATCH` env-var — **TEMP**, remove on G46(b) closure (now achieved).
- `JNEXT_G46B_INJECT` env-var — **TEMP**, remove on G46(b) closure.
- `JNEXT_G46B_WATCH` env-var — **TEMP**, remove on G46(b) closure.

Cleanup commit needed: revert/remove the env-var-gated instrumentation in `src/cpu/z80_cpu.cpp`. Keep `cpu_inst` channel.


---

## 2026-05-05 08:30 — Session resume: Fix #1 cherry-picked to main; Fix #2 reverted; downstream investigation continues

### Decisions taken before deeper investigation
- **Cherry-picked Fix #1 (`04fe5bd`) to main** as commit `2d90ea1`.
  Verified: build OK, unit tests 3850/3812/0/38 (no regressions),
  full regression 32/0/0 (no failures).
- **Reverted Fix #2 (`4c8a761`) on `g46b-investigation` branch** as commit
  `a210b37`. User flagged Fix #2 as a band-aid; investigating proper path.
- Investigation rules now applied: ultrathink + parallel agents in
  worktrees + verify findings independently + log to this doc.

### Trace of post-Fix-#1 boot behaviour
Captured a 14-second headless run with `sdcard=debug,nextreg=debug,emulator=debug`
log levels (full log at `/tmp/g46b-postfix1.log`).

Observed sequence:
1. `nextboot.rom` IPL boots (silicon-baked 8 KB).
2. nextboot.rom does CMD0/CMD8/ACMD41/CMD58 SD init.
3. Reads MBR (sect 0), VBR (sect 63), FAT (sects 187, 4191, 4192),
   then loads `/TBBLUE.FW` (sects 192927-192981 = first 27 KB,
   plus sects 193377-193397 = next 10 KB) into RAM.
4. nextboot.rom hands off → TBBLUE.FW now running.
5. TBBLUE.FW writes NR 0x07 ← 0x03 (28 MHz turbo).
6. TBBLUE.FW writes NR 0x03 ← 0xB0 (disable boot ROM, machine_type)
   then NR 0x03 ← 0x00.
7. TBBLUE.FW does FATFS f_mount → second CMD0/CMD8/ACMD41/CMD58.
8. TBBLUE.FW reads MACHINES/NEXT directory cluster, then files
   in order: CONFIG.INI (sect 168847), MENU.DEF (sect 169119+),
   ENNEXTZX.ROM (sects 169311-169438 — the OS supervisor),
   plus more TBBLUE.FW module loading.
9. **Crucially: NEVER reads ENALTZX.ROM (sects 169583-169646).**
10. TBBLUE.FW writes NR 0x03 ← 0xB3 (config_mode=0).
11. TBBLUE.FW writes NR 0x02 ← 0x01 (RESET_SOFT) at t=10.5s.
12. Soft reset: jnext reinitialises, `[preserve_memory=1 soft-reset]`.
13. NextZXOS supervisor takes over.
14. **From this point: ZERO SD reads for the remaining 38 seconds**
    (until automatic exit). CPU is doing something but not talking
    to SD.

### TBBlue source verification (`../tbblue/src/firmware/`)
Read the TBBlue 1.05 firmware source (cloned at `../tbblue/`).

`app/src/boot.c` `main()` flow before soft reset:
  - `load_config()` — reads `config.ini` + `menu.def`
  - `load_keymap()` — `loadFile(RAMPAGE_ROMSPECCY=0x00, 1, 1024)` for keymap
  - `load_keyjoys(...)`
  - `load_roms()` (line 86-176) — only loads:
    * DivMMC ROM (8 KB) → `RAMPAGE_ROMDIVMMC=0x04` (= SRAM pages 8-9)
    * Multiface ROM (8 KB) → `RAMPAGE_ROMMF=0x05` (= SRAM pages 10-11)
    * Speccy ROM (16/32/64 KB) → `RAMPAGE_ROMSPECCY=0x00..0x03` (= SRAM 0-7)
  - `init_registers()` — peripheral configs
  - `REG_MACHTYPE` write
  - 65535-cycle pause
  - `REG_RESET = RESET_SOFT` → soft reset
  - infinite loop

`load_roms()` does NOT load enAltZX.rom anywhere. Confirmed via grep:
neither `TBBLUE.FW` (compiled, 297 KB) nor `TBBLUE.TBU` (10 MB) contains
the string "enAltZX.rom" or any case variation. The string ONLY exists
in `enNextZX.rom` (at file offset 0x9FFE) — but the supervisor never
references it as a `f_open` filename.

`hardware.h:170-171`:
```
#define RAMPAGE_ALTROM0  0x06   // = SRAM pages 12, 13
#define RAMPAGE_ALTROM1  0x07   // = SRAM pages 14, 15
```
RAMPAGE_ALTROM0/1 are referenced ONLY by `getCoreBoot()` in `misc.c`
which READS from the area to check for a "coreboot" magic marker —
never WRITES (= never loads) data into AltROM.

### Verdict from independent agent investigation (2026-05-05 08:55)
- First `call $0068` AltROM trampoline reached after $00EF entry is at
  enNextZX.rom file offset **$0C52** (line 1876 of `/tmp/enNextZX.dis`):
  `[0c52] cd 68 00` followed by inline `d9 20` (target = AltROM $20D9).
- Reachability: `$00ef → $0271 (call $2341) → … → $0303 (call $0360)
  → $0357 (jp $0c49) → $0c52`. ~750 instructions of NEXTREG/RAM/sysvar/IM2
  setup before this call.
- enNextZX.rom DOES contain its own raw DivMMC SPI driver (port $E7 SPI-CS,
  port $EB SPI-data) at file offset $98E7+. NextZXOS file system does not
  depend on enNxtmmc.rom for SD access — uses its own driver.
- BUT: enNextZX.rom contains NO `f_open` site that uses the "enAltZX.rom"
  string. The string is in the binary but never used as a filename. The
  only NR $8C writes are spot patches at $41B3/$41BD/$41C5/$41CF/$8E37 —
  not a 32 KB bulk SD load.
- **Verdict: chicken-and-egg.** Supervisor needs AltROM loaded BEFORE
  the first `call $0068` at $0C52 (early — 750 instructions in). It
  cannot load AltROM itself because (a) it never opens enAltZX.rom, and
  (b) by the time it would have a chance to, it has crashed in the
  empty $0000-$3FFF window after `nextreg $8c, $80`.

### Conclusion (pending verification)
On real hardware, AltROM (`enAltZX.rom` content) MUST be loaded by
some stage that runs BEFORE the supervisor enters. tbblue.fw doesn't
do it. nextboot.rom (the IPL) doesn't do it (it just loads TBBLUE.FW
into RAM). The only candidate is the FPGA flash itself — i.e., real
Next FPGA's flash storage contains pre-baked AltROM data that's
loaded into the AltROM SRAM region as part of FPGA bitstream init.

If this is correct, then **loading enAltZX.rom from SD into SRAM
pages 0x0C-0x0F at jnext init time IS the proper VHDL-faithful
equivalent** — modeling the FPGA flash's pre-baked AltROM. The
"band-aid" criticism of Fix #2 may have been based on the prior
agent's wrong hypothesis (that NextZXOS itself reloads AltROM
from SD post-RAM-test).

NEXT STEPS:
1. Verify chicken-and-egg hypothesis with two independent parallel
   agents: (a) trace the first $0068 call site execution in jnext to
   confirm it's reached early; (b) test pre-loading AltROM and
   confirming welcome screen renders.
2. If both verify → re-apply Fix #2 (or equivalent), confirm
   welcome screen, document as the proper fix.
3. If chicken-and-egg fails → continue investigation.


### 2026-05-05 09:00 — Verification Agent A (independent live trace) result

Worktree: `agent-a5fccbf698534576a`. Built the branch + widened cpu_inst PC-range gate to capture $0060-$008F + $00EE-$011F + $0C00-$0C7F + $20D0-$21FF + $2730-$27AF. Captured 14-sec headless trace (2.77 M instructions across 658 unique PCs) at `/tmp/verif1-trace.log`.

Empirical results:
- PC=$007B IS reached, at t=50.421s wall, ~244 ms after supervisor enters $00EF.
- Sequence:
  1. Lines 105-156: real code at $00EF…$011E + wrapper at $0080-$008E execute from RAM (real opcodes).
  2. Lines 157-182: wrapper at $279D-$27AB → $2732-$274A executes (real opcodes — Fix #1 makes the wrapper exit cleanly).
  3. **Line 183 (t=50.421): PC=$007B reads op=0xed** — last instruction with non-zero opcodes. This is the AltROM-enable trampoline `nextreg $8C, $80; ret` getting executed because the wrapper RET'd to $007B (that addr was on the user stack).
  4. Line 184 onwards: PC NOP-slides through the entire AltROM-overlaid $0000-$3FFF window (all opcodes = 0x00).
  5. PC linearly advances: $007F → $008F → wraps via $00EE→$011F → $0C00 → $0C7F → $20D0 → $20D9, all NOPs.
- End state at 14s emulated / 88s wall: still NOP-sliding (PC=$27AF op=0x00 sp=$7A0F). SP drifted from $5BFD to $7A0F (+0x1E12 bytes popped via implicit RETs from $0038 IM1 vector that's also NOP).

**Key insight**: the supervisor exits the wrapper at $27AB. The user stack contains $007B as the return address. RET pops $007B → PC=$007B (which is in the AltROM-mapped slot 0/1) → `nextreg $8C, $80; ret` → enables AltROM → RET pops next addr (the AltROM function ptr) → jumps into AltROM → AltROM is empty → NOP slide.

This is the canonical "call into AltROM" pattern: caller pushes $007B + altrom_addr onto stack, then calls supervisor wrapper. Wrapper exit RET pops $007B → enable AltROM → RET to altrom_addr.

**Verification verdict**: chicken-and-egg confirmed. The supervisor's first AltROM call happens VERY EARLY (within 250 ms of supervisor entry), via a wrapper-mediated call. AltROM SRAM pages 0x0C-0x0F are empty (0x00) → entire 16K window NOP-slides → boot wedges.


### 2026-05-05 09:25 — Verification Agent B (independent AltROM pre-load + welcome-screen test) result

Worktree: `agent-a22e3197032d547cc`. Agent re-applied Fix #2-equivalent code (61-line block in `Emulator::init` after Multiface ROM load), gated on `cfg.type == ZXN_ISSUE2 && !preserve_memory && !cfg.sd_card_image.empty()`. enAltZX.rom (32 KB) extracted from SD and split into 4 × 8 KB chunks → `ram_.page_ptr(0x0C..0x0F)`. Built and ran headless test; captured screenshots at multiple time points.

**Boot timeline observed (Fix #1 + Fix #2 applied)**:
| Time | State |
|------|-------|
| t=3s | Clean TBBlue logo + "For video mode selection press: A=All, D=Digital, V=VGA, R=RGB" + "Firmware v1.44.db / Core v3.02.03" |
| t=4s | TBBlue logo + "Press SPACEBAR for menu / Press C for extra cores" — full TBBlue boot menu rendered |
| t=5s | Garbled (blue/white noise upper, TBBlue text lingering bottom) |
| t=6s+ | Black with intermittent color bars at frame 600, 1200 |

This is **dramatic progress vs Fix #1 alone** (black screen forever). With Fix #2:
- TBBlue.fw boots cleanly and renders its boot menu UI.
- Implies TBBlue.fw can now read the AltROM area without crashing.
- Boot makes it ALL THE WAY to the SPACEBAR-menu countdown.
- Times out → load_keymap → load_roms → init_registers → REG_RESET (soft reset).
- Soft reset hands off to NextZXOS supervisor — AND THIS is where the regression happens.

**Verdict**: Fix #2 IS the correct architectural fix for the AltROM-empty problem (modeling FPGA-flash-pre-baked AltROM). It's NOT a band-aid. But it's INSUFFICIENT — a downstream issue manifests post-soft-reset.

**Hypothesis (preliminary, needs verification)**: After TBBlue soft-resets to hand off to NextZXOS supervisor, jnext re-inits with `preserve_memory=true`. The Fix #2 AltROM-load is gated on `!preserve_memory`, so it's NOT repeated. SRAM pages 12-15 should survive (preserve_memory keeps SRAM), but something is going wrong that prevents the NextZXOS supervisor from reaching the welcome screen.

Possible specific causes (TBD — needs Round 3 investigation):
1. Some memory-corrupting operation between AltROM load and soft-reset destroys AltROM contents.
2. NextZXOS supervisor takes a different code path post-soft-reset that fails for a different reason.
3. The `preserve_memory=true` path in `Emulator::init` does something subtle that corrupts AltROM (e.g., the ROM-in-SRAM seed or DivMMC ROM reload uses overlapping pages).

**Unit tests after Agent B's pre-load patch**: 3850/3812/0/38 (no regressions vs Fix #1 baseline).

