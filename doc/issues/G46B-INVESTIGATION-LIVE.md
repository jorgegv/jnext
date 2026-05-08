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


### 2026-05-05 09:15 — Verification Agent C (post-Fix-#2 loop diagnosis) result

Agent ran cpu_inst trace post Fix #1+#2 boot. Found LOOP body executes ~200K instructions per ~656ms iteration ending at $21B8 RET → $0000 → JP $00EF.

Verified call chain:
```
$1F40-area (sprite/render dispatcher, INs $FE keyboard)
 → $1D47 → $1D93 → $1D96 → $1DA0 LDIR (small, OK)
 → $1DE6 → $1DF3 (call c,$2043)
 → $2043 (set up IX as a sprite descriptor)
 → $2057 → $2058 → $205B → $2061 cp (ix+$22) → $2064 call c,$2069
 → $2069 → $20A6: ld hl,$2199; call $2178  (copies "JP $21AB / JP $21D3 / JP $2237" to $5B91)
 → $20AC ld e,$01; call $20E6
 → $20E6 ld c,(ix+$11)   ← C = sprite-width field
 → $20E9 ld a,(ix+$12); $20EC sub e (E=$01); $20ED ld b,a; $20EE ret z
 → $20EF push bc; ... $20F9 call $271D; $20FC jr c,...; $20FF call $22C8
 → $2102 pop bc; $2103 push bc; $2104 push bc; $2105 ld b,$00 (B=0)
 → $2107 bit 3,(iy+$45); $210B call z,$5B91 (correctly pushes $210E)
 → $5B91 jp $21AB
 → $21AB push de; $21AC ex de,hl; $21AD ld hl,$0020; $21B0 add hl,de
 → $21B1 push hl; $21B2 push bc; $21B3 LDIR  ← BC=$00xx with C=(ix+$11)
```

**Root cause**: (IX+$11) = $00 in the sprite descriptor being processed. With B=0 (set at $2105) and C=(ix+$11)=0, LDIR runs 65,536 iterations (BC=$0000 wraps to $FFFF and counts down). The 64 KB LDIR overwrites the entire address space — including the stack — so the eventual RET at $21B8 pops a corrupted return address ($0000) instead of the legitimate $210E pushed at $210B.

This is **NOT** a Fix-#1-shape bug (no NR-port-read divergence on the hot path). The (IX+$11)=0 condition is reproducible and stable across iterations, suggesting a stable mis-mapping or uninitialized descriptor — most likely:

(a) Sprite descriptor lives in a memory region that is mis-banked at the moment of read (NR_56/NR_57 wrong bank). Fix #1 only patched NR 0x50-0x57 reads; the descriptor read might happen via different MMU state or via a different NR port that still has stale-cache divergence.
(b) The descriptor is in AltROM region but our pre-loaded enAltZX.rom doesn't have what NextZXOS expects (file content mismatch, or supervisor mutates AltROM at runtime and we don't carry that mutation across).
(c) Some upstream LDIR (e.g., the small one at $1DA0) wrote to wrong destination (DE) due to bank misregistration, corrupting the descriptor.

**Suggested next probe**: log MMU state (NR_56/NR_57 + sram_active_bank) at PC=$20E6 and dump byte at (IX+$11) via the MMU; compare with what slot/bank should hold the descriptor table.


### 2026-05-05 09:40 — Diagnostic: post-Fix-#2 (IX+$11)=0 root cause identified

Added register+memory diagnostic logging at `src/cpu/z80_cpu.cpp:498-535` (TEMP, gated by PC=$00EF/$2043/$20E6) to capture: IX value, MMU state, NR_8C, raw page 0x0F bytes, raw page 0x2F bytes, and `mem_.read(IX+i)` for i=0..15.

**Key empirical observations**:

```
G46B SPRITE PC=0x20e6 ix=0xe01b sp=0xff7f mmu[0..7]=ff ff 0a 0b 04 05 0e 0f  nr_8c=0x00 rom_in_sram=true cfg_mode=false
  via_mem ix[0..15]=00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  raw_p0f[0x1B..0x22]=00 f7 21 90 e0 3e 08 df  | p2f: 00 00 00 00 00 00 00 00
```

Order of state transitions during one supervisor iteration:
- T0 (PC=$00EF, supervisor entry post-soft-reset): page 0x2F **INTACT** (= AltROM-1 upper, populated by Fix #2 mirror experiment that pre-loaded into pages 0x2C-0x2F).
- T0+~339 ms (next PC=$00EF): page 0x2F **WIPED** to all zeros (with 0xBB at offset 0x1FFF).

**Wipe mechanism**: NextZXOS RAM-test PASS 1 at file offset $0130-$016E wipes 16 KB ($C000-$FFFE) per bank when iterator C < 0x0C. For C=7: NR_56=0x0E (dual-port VRAM page 0x0E) + NR_57=0x0F (regular SRAM page 0x2F via VHDL `mmu_A21_A13` shift). PASS 1:
1. `ld (hl),$bb` writes 0xBB to $FFFF (= page 0x2F offset 0x1FFF).
2. `ld (hl),$00` writes 0 to $FFFE.
3. `lddr` BC=$3FFE copies $3FFE bytes downward from $FFFE → $FFFD, propagating zeros through $C000-$FFFE.

Per-page effect:
- Slot 6 ($C000-$DFFF) = page 0x0E: wiped to zeros (dual-port VRAM, fine).
- Slot 7 ($E000-$FFFE) = page 0x2F: wiped to zeros (regular SRAM, **destroys our mirror-load**).
- $FFFF (= page 0x2F[$1FFF]): 0xBB.

**Why this matters**: the supervisor at PC=$20E6 (`ld c,(ix+$11)` with IX=$E01B) reads page 0x2F offset 0x2C = 0x00 (wiped). The expected value was 0x1F (sprite-width from AltROM-1 upper at file offset 0x602C). With C=$00 and B=$00 (set at $2105), BC=$0000 at the LDIR at $21B3 → 64 KB runaway → stack corruption → RET pops $0000 → JP $00EF → loop.

**Per VHDL `zxnext.vhd:2961-2964`** (`mmu_A21_A13 <= ("0001" + ('0' & mem_active_page(7 downto 5))) & mem_active_page(4 downto 0)`):

| logical NR_57 | physical SRAM page |
|---|---|
| 0x0A, 0x0B | 0x0A, 0x0B (exception — bank 5 dual-port VRAM) |
| 0x0E | 0x0E (exception — bank 7 lower dual-port VRAM) |
| 0x0F | **0x2F** (shifted, regular SRAM) |
| 0x0C, 0x0D | 0x2C, 0x2D (shifted, regular SRAM) |

Pre-loading enAltZX.rom into pages 0x0C-0x0F populates the AltROM-when-enabled region (slot 0/1 reads with NR_8C bit 7=1 use the AltROM SRAM redirect via `altrom_sram_page_(addr)` = pages 0x0C..0x0F). But supervisor reads via NR_57=0x0F slot 7 go to physical page 0x2F (different physical region).

Mirror-loading enAltZX.rom into pages 0x2C-0x2F also (experimental) does NOT help: NextZXOS RAM-test wipes those pages 200 ms after supervisor entry.

**Verified hypothesis**: chicken-and-egg AT TWO LEVELS:
1. AltROM (slot 0/1 with NR_8C bit 7=1) needs pre-loaded enAltZX.rom — Fix #2 handles this; verified working (TBBlue boot screen renders).
2. Bank 7 high half (slot 7 with NR_57=0x0F → physical page 0x2F) needs sprite descriptor data — **how real Next hardware populates this region post-RAM-test is unclear**. Not loaded by tbblue.fw (verified). Not loaded by supervisor itself in any obvious way (we don't see corresponding LDIR with destination $E000+ in the boot path before $20E6).

**Next-session options**:
1. **Investigate further**: trace the supervisor's full boot sequence from $00EF to $20E6 looking for any LDIR writing to slot 7 ($E000+); if none, search for LDIR sources that copy from supervisor ROM (pages 0..7) to bank 7 (pages 0x2C-0x2F) via slot-6/7 mappings.
2. **User's suggestion — `--bypass-tbblue-fw` mode**: replicate CSpect's approach. Skip nextboot.rom + tbblue.fw entirely; pre-populate ALL needed SRAM regions (supervisor banks 0-7, DivMMC, MF, AltROM, sprite descriptor bank 7, etc.); set up MMU + NR registers to look like "post-supervisor-init"; start Z80 directly at supervisor's "ready" state. Requires understanding what state CSpect sets up (could reverse-engineer from CSpect runtime, or from NextZXOS supervisor's own initialization).
3. **Wait for a NextZXOS supervisor source code reference** to understand what's expected in bank 7 high half.

**Status of the user-visible symptom**:
- Original G46(b) (boot loop without any visible progress) — **SOLVED** via Fix #1.
- TBBlue boot screen rendering — **SOLVED** via Fix #1 + Fix #2 (commit `0b7c3c2`).
- NextZXOS welcome screen — **NOT YET RENDERED** (post-soft-reset sprite-descriptor wipe issue).

**Files modified on `g46b-investigation` branch (committed)**:
- `0b7c3c2 fix(g46b#2-v2)`: Pre-load enAltZX.rom into SRAM pages 0x0C-0x0F at hard-reset init.

**Uncommitted experimental changes still on the branch**:
- `src/core/emulator.cpp`: mirror-load to pages 0x2C-0x2F (proven futile per RAM-test wipe).
- `src/cpu/z80_cpu.cpp`: PC-conditional G46B SPRITE diagnostic logging.
- `src/memory/mmu.h`: TEMP `Mmu::ram()` accessor for diagnostic access.


### 2026-05-05 09:55 — Final isolation test + status

**Test**: added env-var-gated patch at `src/cpu/z80_cpu.cpp` near PC=$20E6: when
`JNEXT_G46B_PATCH_IX11=1`, copy bytes from physical SRAM page 0x0F (where
Fix #2 pre-loaded AltROM-1 upper) into the corresponding offsets of physical
SRAM page 0x2F (= what the supervisor reads via NR_57=$0F slot 7). This
"undoes" the RAM-test wipe on the descriptor area at the moment of read.

**Result with patch enabled**: boot reaches a slightly different code path —
the screen at t=14s shows the boot-progress color bars at the bottom plus
some scattered white bars near the top and an isolated dithered glyph in the
top-left corner. **CORRECTION (2026-05-05 10:00)**: my initial interpretation
of these top-area marks as "partial NextZXOS UI rendering (menu chrome +
sprite icon)" was an over-claim — the user pointed out they're just screen
RAM corruption from supervisor writes that aren't drawing anything intentional.
No actual welcome-screen text or recognisable UI elements are visible. The
patch only proves the supervisor takes a different (likely deeper) path
before stalling.

**Result at t=18+, t=25, t=35**: same screen state — supervisor has reached
some other stall point, with no further visible progress.

**Conclusion**: the (IX+$11)=0 wipe is a REAL blocker — patching it changes
the supervisor's runtime behaviour and execution path measurably (different
screen RAM writes). But the patch alone does NOT make the supervisor render
the welcome screen, so the bank 7 high half data requirement extends to
multiple read sites (or there are other unrelated stall points downstream).

**Ram-init experiment**: changed `Ram::Ram` and `Ram::reset` to fill with
0xFF instead of 0 (test of "real Next has random/non-zero garbage RAM at
power-on" hypothesis). Result: boot stalls EARLIER (no TBBlue boot screen
at t=14). 0xFF in supervisor work areas / screen / AltROM regions breaks
other things. Reverted.

### Final status of G46(b) and downstream

- **G46(b) original RAM-test/wrapper loop**: SOLVED via Fix #1 (`04fe5bd` on
  `g46b-investigation`, cherry-picked to `main` as `2d90ea1`).
- **TBBlue boot screen rendering**: SOLVED via Fix #1 + Fix #2-v2 (commit
  `0b7c3c2` on `g46b-investigation`). Visible on screen: TBBlue logo +
  "Press SPACEBAR for menu / Press C for extra cores" + firmware/core
  version strings.
- **NextZXOS welcome screen**: NOT REACHED. Blocked by the supervisor reading
  zeros from bank 7 high half (page 0x2F) where it expects sprite descriptors.
  Source of the expected data on real Next hardware is unknown — not in
  tbblue.fw, not in any LDIR we traced from the supervisor's $00EF entry to
  the descriptor read at $20E6, and the supervisor's own RAM-test wipes
  page 0x2F to all zeros so it can't be from a previous boot state.
- **Tested workaround**: env-var `JNEXT_G46B_PATCH_IX11=1` copies AltROM-1
  upper bytes (page 0x0F) into the sprite-descriptor read range of page 0x2F.
  Changes the supervisor's runtime behaviour (different screen RAM writes,
  visible as scattered corruption) but does NOT render the welcome screen.

### Recommended next-session paths
1. **`--bypass-tbblue-fw` mode (user's suggestion)** — replicate CSpect's
   approach. Skip the entire firmware boot. Pre-populate everything CSpect
   pre-populates. Don't run NextZXOS's RAM-test (it would wipe the
   pre-loaded data). This requires either patching enNextZX.rom (copyright
   issue) or hooking the supervisor to skip the test.
2. **Reverse-engineer CSpect's runtime memory state** at the moment of
   NextZXOS welcome screen rendering, to see what data CSpect puts in
   bank 7 high half + AltROM + everywhere else.
3. **Continue deep RE of NextZXOS supervisor** to find what populates bank 7
   high half (and other UI data regions) on real hardware. Possibly the
   answer is in code we haven't traced yet — e.g., a routine called from
   the AltROM trampoline that DOES populate the descriptor table from a
   source we haven't identified.


### 2026-05-05 11:30 — CSpect runtime inspection

User ran NextZXOS in CSpect, broke at PC=$20E6 with the welcome screen rendered. Key data captured:

**Z80 register**: IX = **$5CCA** (vs our jnext IX = $E01B — completely different address, different slot).

**MMU state** (from CSpect's NextRegisters window):

| Slot | NR | Real CSpect value | Phys page (via to_sram_page) | Our jnext value |
|---|---|---|---|---|
| 0 | NR_50 | 0xFF (ROM sentinel) | ROM | 0xFF |
| 1 | NR_51 | 0xFF (ROM sentinel) | ROM | 0xFF |
| 2 | NR_52 | **0x00** | 0x20 (shifted, regular RAM) | **0x0A** (page 0x0A bank 5 dual-port) |
| 3 | NR_53 | **0x11** | 0x31 (shifted) | **0x0B** (bank 5 upper) |
| 4 | NR_54 | 0x04 | 0x24 | 0x04 |
| 5 | NR_55 | 0x05 | 0x25 | 0x05 |
| 6 | NR_56 | **0x0B** | 0x0B (exception) | **0x0E** |
| 7 | NR_57 | **0x10** | 0x30 (shifted, regular RAM) | **0x0F** |

So CSpect maps slots 2/3 to bank 0/8 (regular work RAM at +0x20 shift), and slots 6/7 to bank 5 upper + bank 8 lower. Our jnext maps slots 2/3 to bank 5 (dual-port VRAM) and slots 6/7 to bank 7 (sprite VRAM + bank 7 high).

**Memory dump at IX=$5CCA in CSpect** (30 bytes):
```
$5CCA: 4D 5B 4D 5B 57 7F 27 6D 0C BE 2B 2F 01 00 EF 05 F8 20 15 00 00 1F 14 00 A8 A0 00 01 33 08
```

So sprite-width (= byte at IX+$11 = $5CDB) on real hw = **$20** (= 32 decimal). With C=$20 and B=$00, BC=$0020 → LDIR copies 32 bytes (bounded), no runaway.

**Source of the descriptor template**: enNextZX.rom file offset **$949A** contains the SAME first 11 bytes (`4d 5b 4d 5b 57 7f 27 6d 0c be 2b`). After byte 11 the ROM has code (`30 00 e5 d5 c5 2a 53 5c 2b e5 d9 cd 00 3e 76 05 d1 e1 c1 a7 ed`) while CSpect's runtime descriptor has dynamic data (`2f 01 00 ef 05 f8 20 15 00 00 1f 14 00 a8 a0 00 01 33 08`). 5 similar templates exist at file offsets $9266, $92A1, $92E9, $9344, $949B — all with the `4D 5B 4D 5B` header (= two pointers to $5B4D).

So the supervisor:
1. Copies static template (first ~11 bytes) from one of these ROM locations to RAM.
2. Computes/fills dynamic fields (offsets 0x0B+ including sprite-width at 0x11) at runtime.
3. Iterates through the descriptor list rendering sprites.

**The cascading divergence in our jnext**:
- Our supervisor's MMU layout at PC=$20E6 differs (slots 2/3 + 6/7 point to different physical pages).
- Our IX value differs ($E01B vs $5CCA) — supervisor reads descriptor from a different address.
- The descriptor at our IX address is uninitialized (all zeros) because:
  - The supervisor's "build descriptor" code path either doesn't run or writes to a different location.
  - Or our IX iterates wrong descriptors (off-by-N from real hw).

Multiple layers of state divergence stem from upstream supervisor behaviour we haven't identified. Likely a supervisor branch decision early in the boot is taking a different path on jnext.

**Conclusion**: the firmware-driven boot path requires fixing the supervisor's data-flow divergence — which requires either (a) finding the upstream root divergence (a specific NR-port-read or memory-read returning a different value), or (b) bypassing the supervisor's full init by pre-populating expected state (the user's suggestion).


### 2026-05-05 11:50 — Wrapper analysis: our supervisor reaches $20E6 via dispatcher, not via wrapper

Counted hits in our 12-second cpu_inst trace:
- PC=$2734 (wrapper entry test): 106 hits
- PC=$27DE (NR-swap routine): 318 hits
- PC=$20E6 (sprite read): ~119 hits per loop iteration × ~3700 iterations

So our supervisor DOES enter the wrapper (106 times) and DOES execute the NR-swap (318 times — not 1:1 because each wrapper invocation may call $27DE multiple times for entry+exit). But the path that reaches PC=$20E6 doesn't go through the wrapper-mediated route.

**First $20E6 hit** in our trace traces back via:
```
$1D96 → $1DA0 (small LDIR, copies 8 bytes to $F350)
      → $1DE6 (read ix+$24)
      → $1DF3 call c, $2043 (sprite descriptor setup)
      → $2057 → $2058 call $1A88 → $205B → $2069
      → $20A6 ld hl,$2199; call $2178 (copies JP trampolines to $5B91)
      → $20AC ld e,$01; call $20E6   ← we hit here
```

This is the **sprite/render dispatcher path** (entered from $1F40-area which reads port $FE keyboard). NO wrapper $2730 in this call chain.

**On real CSpect** (per the BP capture), $20E6 is reached with NR_56=$0B, NR_57=$10 (the values written by wrapper's NR-swap at $27DE). So CSpect's $20E6 hit happened from inside a wrapper-mediated call chain.

**Hypothesis** (needs verification): on real Next, the supervisor enters wrapper FIRST, then the wrapper invokes the rendering chain that reaches $20E6. On our jnext, the supervisor reaches the rendering chain BEFORE wrapper-mediated calls happen, so $20E6 is hit with the pre-wrapper MMU state and pre-wrapper IX.

**To verify**: get the user to set BP at $2734 (wrapper entry) in CSpect. Compare with our jnext's first $2734 hit timing (line 12462281 = ~190ms after supervisor entry). If CSpect's $2734 is before $20E6 first hit, but ours is after, that's the divergence.

**Or simpler**: the real divergence may be in the supervisor's internal state machine — our jnext takes a wrong branch early, leading to dispatcher-first instead of wrapper-first. Likely culprits:
- Some NR or port read returning a stale value (similar to Fix #1 but for a different register)
- Some memory state we didn't initialize correctly
- A keyboard/joystick state that influences the supervisor's mode selection

Without further runtime data from CSpect (especially comparison of pre-$20E6 execution paths), we can't pinpoint the upstream divergence.


### 2026-05-05 12:15 — User provided 2nd CSpect screenshot (FIRST $20E6 hit)

User captured CSpect debugger with PC=$20E6 BP firing for the **first time** during boot (welcome screen was already rendered when user broke; second screenshot is from a fresh boot with fresh BP).

**FIRST $20E6 hit in CSpect**:
- IX = **$F700** (vs SECOND hit IX = $5CCA, vs ours $E01B)
- BC = 0000
- DE = 5B00
- HL = 218A
- SP = 5BF9 (supervisor stack — INSIDE wrapper)
- IR = 09FB
- MMU = FF FF 0A 11 04 05 0B 10 (vs SECOND hit FF FF 00 11 04 05 0B 10, vs ours FF FF 0A 0B 04 05 0E 0F)

Differences between FIRST and SECOND CSpect hits: only NR_52 changed ($0A → $00) and IX changed ($F700 → $5CCA). Both have NR_53=$11, NR_56=$0B, NR_57=$10 (all = the wrapper-explicit writes).

**Our jnext FIRST $20E6 hit MMU vs CSpect FIRST**:
- NR_52: SAME ($0A)
- NR_53: ours $0B (default), CSpect $11 (explicit write to bank 8 upper)
- NR_56: ours $0E (port_7ffd_bank=7 derived), CSpect $0B (explicit wrapper write)
- NR_57: ours $0F (port_7ffd_bank=7 derived), CSpect $10 (explicit wrapper write)
- IX: ours $E01B, CSpect $F700

NR_56=$0B and NR_57=$10 are NOT a port-7FFD-derived consecutive pair — they came from explicit individual NR_56/NR_57 writes (= what wrapper $27DE does per disasm `ld hl,$100b; out`). NR_53=$11 is also explicit.

### 2026-05-05 12:20 — Tested NR_8E propagation skip (failed)

VHDL zxnext.vhd:4670+ says NR_8E with bit 3=1 propagates `port_7ffd_bank * 2 / +1` to MMU6/MMU7, overriding any explicit NR_56/NR_57 writes. The AltROM wrapper at altzx file offset $00FC writes NR_8E=$78 (bit 3=1) — and our trace shows 3711 such writes in a 12-sec run, each propagating port_7ffd_bank=7 to MMU6=$0E, MMU7=$0F.

**Hypothesis tested**: comment out the apply_legacy_ram_slots_ call in our `Mmu::write_nr_8e()` so the propagation doesn't fire. This would let explicit NR_56/57 writes survive.

**Result**: same boot stall. TBBlue boot screen at t=4s, then black at t=14/18/30. Skipping the propagation didn't help reach welcome screen. Reverted (kept VHDL-faithful behaviour).

### Multiple concurrent divergences

Our supervisor's MMU state at PC=$20E6 differs from CSpect in MULTIPLE registers (NR_53, NR_56, NR_57). IX value differs ($E01B vs $F700). SP differs ($FF7F user-stack vs $5BF9 supervisor-stack — confirms our jnext is OUTSIDE wrapper while CSpect is INSIDE).

The NR_8E propagation is ONE source of divergence (overrides explicit NR_56/57 writes). But fixing just that doesn't bring welcome screen — there are other concurrent divergences:
- Why does our supervisor reach $20E6 OUTSIDE wrapper while CSpect reaches it INSIDE?
- Why is NR_53 default ($0B) in our jnext but explicitly $11 in CSpect?

These suggest our supervisor takes a fundamentally different early code path. Some upstream condition (NR-port read, port read, sysvar value, memory state) returns differently in our jnext, leading to a different control-flow branch.

### Next-session recommended path

Per user's preference for the firmware-driven boot path: the next concrete step is to identify what UPSTREAM divergence puts our supervisor on the wrong code branch. Candidates:
- A NR-port read (NR_03, NR_06, NR_07, NR_08, NR_8E, NR_8F) returning different value
- A port read (FE keyboard, FF border, etc.) returning different value
- A sysvar at $5C00+ or $5B00+ being uninitialized differently
- An IY+offset read returning wrong value (supervisor uses IY heavily)

Concrete tooling: add a comparator that logs ALL `nextreg $XX, A` writes (= ed 92 XX) along with the A value, on both branches of a path. Compare CSpect's NextReg writes (would need CSpect's NextReg-write log if CSpect can produce one) vs ours.

OR: bypass the firmware boot entirely (per the user's earlier suggestion) — pre-populate the supervisor state directly. Skip the divergent boot path.


---

## 2026-05-05 12:30 — SESSION HANDOVER (next session: FW bypass approach)

### Final state of g46b-investigation branch

```
6d57367 doc(g46b): NR_8E propagation experiment failed; document multiple concurrent divergences
460c054 doc(g46b): CSpect runtime inspection — IX=$5CCA, NR_56=$0B, NR_57=$10 + descriptor template at $949A
004fed7 doc(g46b): wrapper-vs-dispatcher path divergence analysis
3cbb3bc doc(g46b): correct overclaim about JNEXT_G46B_PATCH_IX11 patch outcome
81d00e8 diag(g46b#2-v4): JNEXT_G46B_PATCH_IX11 isolation test for sprite-descriptor stall
a2c3d1c diag(g46b#2-v3): page 0x2F mirror-load + PC-conditional sprite-descriptor diag
0b7c3c2 fix(g46b#2-v2): pre-load enAltZX.rom into AltROM SRAM pages 0x0C-0x0F at hard-reset init
a210b37 Revert "fix(g46b#2): load enAltZX.rom from SD into SRAM pages 0x0C-0x0F"
04fe5bd fix(g46b): NR 0x50-0x57 read handlers delegate to Mmu — single MMU register source of truth
5f8cca0 diag(g46b): add cpu_inst log channel + temp G46(b) instrumentation
89e18de  ← branch base (= main)
```

`main` HEAD = `2d90ea1` (= Fix #1 cherry-picked from g46b-investigation `04fe5bd`).

### Reference data captured in CSpect (preserve for next session)

Three screenshots saved at repo root (untracked, please commit if needed):
- `cspect-nextzxos-boot.png` — what NextZXOS welcome screen should look like
- `cspect-boot-debugger-1.png` — CSpect debugger at PC=$20E6 SECOND hit (welcome screen rendered)
- `cspect-boot-debugger-2.png` — CSpect debugger at PC=$20E6 FIRST hit
- `mmu-nextregs-1.png` — CSpect NextReg window showing NR_50..NR_5F + others

**CSpect data summary**:

| | FIRST hit ($F700) | SECOND hit ($5CCA) | Our jnext ($E01B) |
|---|---|---|---|
| PC | $20E6 | $20E6 | $20E6 |
| IX | $F700 | $5CCA | $E01B |
| BC | $0000 | $0000 | $0000 |
| DE | $5B00 | (n/a) | (n/a) |
| HL | $218A | (n/a) | (n/a) |
| SP | $5BF9 (sup-stk) | $5BF9 | $FF7F (user-stk) |
| NR_50 | FF | FF | FF |
| NR_51 | FF | FF | FF |
| NR_52 | 0A | **00** | 0A |
| NR_53 | **11** | 11 | 0B (default) |
| NR_54 | 04 | 04 | 04 |
| NR_55 | 05 | 05 | 05 |
| NR_56 | **0B** | 0B | 0E (port-derived) |
| NR_57 | **10** | 10 | 0F (port-derived) |
| State | INSIDE wrapper | INSIDE wrapper | OUTSIDE wrapper |

**Bytes at IX in CSpect**:

FIRST hit ($F700, slot 7 with NR_57=$10 → physical SRAM page 0x30 offset 0x1700):
```
$F700: EB D7 08 FA 01 93 E9 3F AA 46 F1 59 0E 00 FB 08 FF 20 18 00 00 1F 17 00 C0 10 18 01 20 08
                                                              ^^
                                              (IX+$11) = 0x20 = sprite-width
```

SECOND hit ($5CCA, slot 2 with NR_52=$00 → physical SRAM page 0x20 offset 0x1CCA):
```
$5CCA: 4D 5B 4D 5B 57 7F 27 6D 0C BE 2B 2F 01 00 EF 05 F8 20 15 00 00 1F 14 00 A8 A0 00 01 33 08
                                                              ^^
                                              (IX+$11) = 0x20 = sprite-width
```

Both CSpect descriptors have sprite-width=$20 (= 32 dec) at IX+$11. Our jnext reads 0 from IX+$11 (page 0x2F is wiped, and our IX points to a different region anyway).

### What remains to be tried (next session)

#### Option A — `--bypass-tbblue-fw` (USER'S PREFERRED PATH)

Replicate CSpect's approach. Skip nextboot.rom + tbblue.fw entirely. Set up the MMU, NR registers, and SRAM contents to look like "post-supervisor-init" state, then start Z80 directly at the supervisor's "ready" state.

Concrete steps:
1. Add `--bypass-tbblue-fw` CLI option (off by default).
2. When enabled, on hard reset:
   - DON'T overlay nextboot.rom at $0000-$1FFF (skip the boot ROM enable)
   - Pre-populate SRAM pages 0..7 with enNextZX.rom (= what tbblue.fw `load_roms()` does)
   - Pre-populate SRAM page 0x08 with enNxtmmc.rom (DivMMC)
   - Pre-populate SRAM page 0x0A with enNextMf.rom (Multiface)
   - Pre-populate SRAM pages 0x0C-0x0F with enAltZX.rom (AltROM, this Fix #2-v2 already does)
   - Set NR registers to expected post-tbblue.fw values (need to figure out what these are):
     * NR_03 = machine type for Next
     * NR_07 = 0x03 (28 MHz)
     * NR_8E = appropriate value
     * Others as needed
   - Set Z80 PC = $0000 (which JP $00EF → supervisor entry)
   - Or PC = $00EF directly (= just after the JP)
3. Start the supervisor and see what happens.

Key unknowns:
- What exact NR register values does CSpect set up before handing off to NextZXOS?
- What sysvar memory state does NextZXOS expect post-init?

For the unknowns, can either:
- Empirically experiment (try common values, see if welcome screen renders)
- Inspect CSpect's runtime memory state at the START of NextZXOS execution (set BP at $00EF in CSpect, dump all NR registers + key SRAM pages)

#### Option B — supervisor RAM-test bypass / patch

Patch enNextZX.rom at file offset $0130 (RAM-test PASS 1 entry) to JP straight to $01CC (post-RAM-test). This skips the RAM-test entirely. With Fix #2-v2 (pre-loaded AltROM), the supervisor would have valid AltROM + valid sprite descriptors (assuming we also pre-populate bank 7 / pages 0x2C-0x2F with appropriate data).

Risk: not VHDL-faithful, modifies copyrighted ROM.

#### Option C — Continue divergence-tracing in firmware path

Add NR-write logging to our jnext and run from supervisor entry $00EF. Capture every `nextreg` write. Compare with CSpect's writes (would need CSpect to produce the same log — could potentially use CSpect's plugin API).

The first divergence point in the NR-write log = where supervisor branches differently. Fix that root cause.

Risk: time-consuming. We already know the IX, MMU, and SP all differ at PC=$20E6 — finding the SINGLE upstream cause may be hard.

### Recommendation

Per user direction: **Option A (`--bypass-tbblue-fw`)** in the next session.


---

## 2026-05-05 (afternoon) — `--bypass-tbblue-fw` LANDED

### Implementation

Per user directive, added `--bypass-tbblue-fw` CLI option (`g46b-investigation`).

Changes:
- `src/core/emulator_config.h` — new `bool bypass_tbblue_fw = false;`
- `src/main.cpp` — CLI flag parsing + propagation into `EmulatorConfig`
- `src/core/emulator.cpp::Emulator::init()`:
  - Machine ROM block: load `/MACHINES/NEXT/enNextZX.rom` (4 banks, 64 KB) into `rom_` instead of `48.rom` when bypass is on (the existing ROM-in-SRAM seed at lines 3560+ then copies pages 0..7 into `ram_` automatically).
  - Boot ROM block: skip `nextboot.rom` overlay when bypass is on; log explicit message.
  - Post-handoff init: synthesise a `nextreg_.write(0x03, 0x04)` to commit `machine_type = Next (0x04)` and transition `config_mode → 0` (mimicking what tbblue.fw normally does before jumping to the supervisor). Without this the supervisor's first NR 0x03 write of `0xB0` (low3=000 = no change) leaves `config_mode` stuck at the power-on default of 1, which routes ROM reads through `nr_04_romram_bank` instead of `sram_rom`.

DivMMC / Multiface / AltROM remain extracted from the SD as in normal mode — the AltROM seed at pages 0x0C..0x0F (Fix #2-v2) is unchanged.

### Smoke-test (4-second window, headless)

Bypass mode now reaches the **same supervisor loop** as normal mode:

- Boot-ROM overlay: skipped (per init log)
- enNextZX.rom: 65 536 bytes loaded into rom_ banks 0..3 (per init log)
- AltROM, DivMMC, Multiface: loaded as in normal mode (per init log)
- After post-handoff write: `NextREG 0x03 ← 0x04 (config_mode=0)` ✓
- Z80 starts at $0000 → JP $00EF → supervisor entry
- Supervisor reaches the documented loop:
  ```
  G46B SPRITE PC=0x1df3 ix=0xe01b sp=0xff87 mmu[0..7]=ff ff 0a 0b 04 05 0e 0f
  G46B SPRITE PC=0x2043 ix=0xe01b sp=0xff85 mmu[0..7]=ff ff 0a 0b 04 05 0e 0f
  G46B SPRITE PC=0x20e6 ix=0xe01b sp=0xff7f mmu[0..7]=ff ff 0a 0b 04 05 0e 0f
  ```
  (identical IX, SP, MMU mapping to non-bypass mode at these PCs — confirming bypass is not introducing any new divergence.)
- Periodic `NextREG 0x03 ← 0xb0  (config_mode=0)` writes ~every 500 ms (= the 270 ms periodic re-init loop pattern from the previous trace), interleaved with `CPU speed changed to 28 MHz`.
- Screen: black (welcome screen NOT rendered — same as we'd expect given the divergence is upstream of the bypass).

### Verification — non-bypass path unchanged

`./build/jnext --headless --machine next --sd-card roms/nextzxos-1gb-fat32fix.img` (no `--bypass-tbblue-fw`):
- TBBlue boot screen visible at t=4s (logo + "Press SPACEBAR for menu" + Firmware/Core version) ✓
- `make unit-test` → 3850 / 3812 / 0 / 38 (matches prior baseline)
- `bash test/00regression/regression.sh` → 33 PASS / 0 FAIL / 0 SKIP

### Why this matters

`--bypass-tbblue-fw` gives us a clean, isolated reproducer: the supervisor reaches the same loop without any tbblue.fw intermediation. From here, the next-step debugging plan from the EOD memo applies directly:

1. Capture CSpect runtime state at the moment the supervisor enters $00EF (NR registers, key SRAM pages, sysvars at $5B00-$5C00).
2. Pre-populate the same state in jnext's bypass mode.
3. Iterate until the welcome screen renders.

This isolates the divergence to "what state CSpect sets up before $00EF" vs "what jnext's bypass sets up", removing all firmware-side variables.

### TEMP instrumentation — still on g46b-investigation branch

Unchanged from EOD. Removed only on full G46(b) closure (= welcome screen renders).

---

## 2026-05-05 13:00 — `--bypass-tbblue-fw` post-handoff init expanded; ULTRATHINK investigation begins

### tbblue.fw boot.c::main() complete sequence (RE'd from `/home/jorgegv/src/spectrum/tbblue/src/firmware/app/src/boot.c`)

The tbblue firmware's `main()` does the following before its trailing `RESET_SOFT`:

```c
// Top of main:
REG_TURBO  = 0x03;        // NR 0x07 = 3  → 28 MHz
REG_PERIPH2 = 0xa0;       // NR 0x06 = 0xa0 → PS/2 keyboard transient

vdp_init();
disable_bootrom();        // This writes NR 0x03 to disable boot ROM overlay
load_config();            // Reads SD config.ini

// Boot scrolls + key polling for ~5 sec (Press SPACEBAR menu loop)

// MF/DivMMC ROM-file overrides from menu line, etc.
load_keymap();            // Loads /MACHINES/NEXT/keymap.bin into RAMPAGE_ROMSPECCY
load_keyjoys(...);        // Configures keyjoy defaults
load_roms();              // Loads enNxtmmc.rom, enNextMf.rom, enNextZX.rom into SRAM
init_registers();         // Writes PERIPH1..5 + DECODE_INT0..3

// FINALLY:
REG_MACHTYPE = 0x80 | ((mode+1) << 4) | (mode+1);   // NR 0x03 — for mode=2 (+3): 0xB3
for (cont = 0; cont < 0xffff; cont++);              // Pause
REG_RESET = RESET_SOFT;                              // NR 0x02 = 0x01 → soft reset
for (;;);                                            // wait for reset
```

**Computed values for the NextZXOS canonical boot** (SD config.ini + menu.def line 0 = "ZX Spectrum Next (standard)" mode=2 = +3):

| NR | Value | Source / Meaning |
|---|---|---|
| 0x07 | 0x03 | TURBO = 28 MHz |
| 0x06 | 0xa0 | PERIPH2 transient (PS/2 keyboard) |
| 0x05 | 0x81 | PERIPH1: joystick1=2 (bits 7-6=10) + scandoubler=1 (bit 0) |
| 0x06 | 0x80 | PERIPH2: turbokey=1 (bit 7), psgmode=0 |
| 0x08 | 0x3e | PERIPH3: stereo + speaker + DAC + Timex + turbosound |
| 0x09 | 0x00 | PERIPH4: scanlines=0, hdmisound=1 (= bit 2 NOT set) |
| 0x0a | 0x01 | PERIPH5: mousedpi=1 |
| 0x82 | 0xda | DECODE_INT0 hwenables[0] for +3 mode: disable ff/dffd/6b |
| 0x83 | 0x3d | DECODE_INT1 hwenables[1]: divports + uart + i2c + kmouse |
| 0x84 | 0xff | DECODE_INT2 hwenables[2]: AY + all DACs (DAC=1) |
| 0x85 | 0x01 | DECODE_INT3 hwenables[3]: ULAplus=1, DMA=0 |
| 0x03 | 0xB3 | MACHTYPE: timing=+3 + machine_type=+3 + low3=011 → exits config_mode |

### Bypass-handoff init committed (`150075e`)

`src/core/emulator.cpp` `Emulator::init()` now contains, when `--bypass-tbblue-fw` is on:

```cpp
nextreg_.write(0x07, 0x03);
nextreg_.write(0x06, 0xa0);
nextreg_.write(0x05, 0x81);
nextreg_.write(0x06, 0x80);
nextreg_.write(0x08, 0x3e);
nextreg_.write(0x09, 0x00);
nextreg_.write(0x0a, 0x01);
nextreg_.write(0x82, 0xda);
nextreg_.write(0x83, 0x3d);
nextreg_.write(0x84, 0xff);
nextreg_.write(0x85, 0x01);
nextreg_.write(0x03, 0xB3);   // = the firmware's REG_MACHTYPE write for mode=2
```

### Empirical result — bypass STILL stalls at same loop

Test: `./build/jnext --headless --machine next --sd-card roms/nextzxos-1gb-fat32fix.img --bypass-tbblue-fw --delayed-screenshot /tmp/t6.png --delayed-screenshot-time 6 --delayed-automatic-exit 7`

Result: **identical** PC=$1DF3/$2043/$20E6 loop, IX=$E01B, MMU=`ff ff 0a 0b 04 05 0e 0f`. Black screen. So the supervisor's path divergence is NOT explained by the firmware-time peripheral writes — it's something deeper.

### What's NOT replicated in our `--bypass-tbblue-fw` path

The firmware's last action before handoff is **`REG_RESET = RESET_SOFT`** (= NR 0x02 with value 0x01), which triggers a SOFT RESET in the FPGA. This resets various FPGA state machines and re-enters the Z80 at $0000. Our bypass mode does NOT trigger a soft reset — it just lets the Z80 start at $0000 from the cold-boot init state.

**Hypothesis**: there is FPGA state (peripheral state machines, interrupt latches, NMI state, port_7FFD, etc.) that gets reset by `RESET_SOFT` but is NOT in the same state in our cold-boot init. This residual difference might cause the supervisor to take a different early branch.

### ULTRATHINK investigation phase started — 3 parallel agents launched

**Agent 1 (path comparison)** [worktree-isolated]: trace from supervisor entry $00EF to first $20E6 hit, identify the FIRST conditional branch where our jnext takes a different path from CSpect (which goes via wrapper-mediated chain). Report → `doc/issues/G46B-AGENT-PATHCOMPARE.md`.

**Agent 2 (RESET_SOFT VHDL audit)** [worktree-isolated]: enumerate every FPGA signal reset by `RESET_SOFT` in zxnext.vhd, compare with our jnext's cold-boot init. List items missing from cold boot. Report → `doc/issues/G46B-AGENT-RESETSOFT.md`.

**Agent 3 (NR handler vs VHDL audit)** [worktree-isolated]: per-register audit of NR write/read handlers vs VHDL, looking for any subtle divergence (similar to Fix #1 for NR 0x50-0x57). Report → `doc/issues/G46B-AGENT-NRAUDIT.md`.

After all three complete, an independent reviewer agent will verify each finding before any code change.

### 2026-05-05 13:15 — Direct RE of supervisor self-init at $00EF

Disassembled `enNextZX.rom` ($00EF onwards) — the **supervisor's own self-init** independent of tbblue.fw. Critical finding: **the supervisor at $00EF aggressively rewrites NR registers**, mostly OVERWRITING any pre-handoff state we set.

```asm
[00ef] nextreg $07, $03      ; turbo = 28 MHz
[00f3] nextreg $03, $b0      ; timing=+3, low3=000 → preserves config_mode + machine_type
[00f7] nextreg $c0, $08      ; im2_vector=0, stackless_nmi=1, pulse mode (not IM2)
[00fb] ld a, $ff
[00fd] nextreg $82, a        ; DECODE_INT0 = $FF (all enabled — overwrites tbblue's $da)
[0100] nextreg $83, a        ; DECODE_INT1 = $FF (overwrites tbblue's $3d)
[0103] nextreg $84, a        ; DECODE_INT2 = $FF (matches tbblue's $ff)
[0106] nextreg $85, a        ; DECODE_INT3 = $FF (overwrites tbblue's $01)
[0109] xor a
[010a] nextreg $80, a        ; expansion-bus-1 disable
[010d] nextreg $81, a        ; expansion-bus-2 disable
[0110] nextreg $8a, a        ; mapping/paging mode reset
[0113] nextreg $8f, a        ; mapping mode reset
[0116] ld bc, $243b
[0119] ld d, $06
[011b] out (c), d            ; select NR_06
[011d] inc b                 ; bc = $253b
[011e] in a, (c)             ; A = NR_06 (= 0x80 from tbblue or our bypass)
[0120] and $44               ; mask bits 6,2 (turbokey, ps2)
[0122] out (c), a            ; NR_06 = 0 (since 0x80 & 0x44 = 0)
```

This means **all our pre-handoff DECODE_INT and PERIPH writes get clobbered**. The supervisor self-resets to a known state.

**What survives**:
- NR_03 machine_type (supervisor's $B0 has low3=000 = no change)
- NR_03 config_mode (supervisor's $B0 has low3=000 = no change). Without our handoff write, power-on default = 1 → ROM reads route through NR_04. With our 0xB3 handoff: config_mode=0. ← **Critical**
- The 4 NR pages set by supervisor at $0139+: NR_56/57 are written per RAM-test loop iteration

**RAM-test entry** (`$0130-$018C`):
```asm
[0130] ld bc, $7000          ; B=$70 (banks?), C=0
[0133] ld hl, $4000
[0136] ld a, c
[0137] exx
[0138] add a                 ; A = 2 * bank
[0139] nextreg $56, a        ; NR_56 = 2 * bank
[013c] inc a
[013d] nextreg $57, a        ; NR_57 = 2 * bank + 1
[0140] srl a                 ; restore bank index
...
```

**RAM-test inner loop** (`$018E-$01C0`):
```asm
[018e] xor a
[018f] ld bc, $4000
[0192] ld de, $0108
[0195] add a
[0196] nextreg $56, a        ; NR_56 = 2 * sub-bank
[0199] inc a
[019a] nextreg $57, a        ; NR_57 = 2 * sub-bank + 1
[019d] srl a
[019f] ex af, af'
[01a0] ld hl, $ffff
[01a3] ld a, (hl)             ; READ byte at $FFFF in current slot 7 bank
[01a4] cp $bb                ; compare with $BB
[01a6] jr nz, $01cc          ; if NOT $BB → exit to post-RAM-test
```

**Critical observation**: the RAM-test only continues testing a bank IF the byte at the top of slot 7 ($FFFF) equals **$BB**. If anything else, it jumps to $01CC (post-RAM-test exit).

**Question**: where does $BB come from? It's not written by the supervisor's earlier code we've seen. So either:
- It's a marker the FPGA hardware writes to mark "available" banks
- Or it's a marker tbblue.fw's `load_roms()` writes
- Or it's a side-effect of the bank-clearing code we haven't seen yet

This $BB marker might be a key piece of state we're missing. If our jnext's banks have 0x00 at $FFFF (uninitialized), then RAM-test exits IMMEDIATELY for bank 0, skipping the wipe entirely. If they have $BB, RAM-test wipes them sequentially.

**Need to check**:
1. Where does $BB get written into upper-bank $FFFF positions? (need search of enNextZX.rom for ld (hl),$bb or similar)
2. Empirically: does our jnext's RAM at $FFFF in slot 7 (page 0x2F) contain $BB or $00 at the moment of RAM-test entry?

This may be the key to understanding the divergence.

---

### 2026-05-05 13:30 — Self-paging trampoline pattern + CRITICAL trace findings

**Discovery via direct disassembly + cpu_inst trace**: NextZXOS uses a clever **self-paging RST 20 trampoline** that flips ROM banks during execution.

The wrapper at `$3E00-$3E17` (in slot 1) does:
1. Read 2 operand bytes after RST 20 (= target address into BC)
2. Push `$3E13` (post-target trampoline) onto stack
3. Push BC (target) on top
4. **Execute `nextreg $8e, $02` AT `$3E13`** (this is the "switch to bank 2" point)
5. RET pops target → run target

When target completes (rets), it pops `$3E13` → executes the trampoline. **But $3E13 in bank 2 is `nextreg $8e, $00`** (different bytes than bank 0's `$3E13`):

| Address | Bank 0 bytes | Bank 0 disasm | Bank 2 bytes | Bank 2 disasm |
|---|---|---|---|---|
| `$3E13` | `ed 91 8e 02` | `nextreg $8e, $02` | `ed 91 8e 00` | `nextreg $8e, $00` |

So:
- Wrapper from bank 0: switches to bank 2 (NR_8E=$02 → 1FFD(2)=1)
- Trampoline in bank 2: switches back to bank 0 (NR_8E=$00 → 1FFD(2)=0)

This is how the supervisor multiplexes 2 ROM banks through one wrapper.

### 2026-05-05 13:35 — Supervisor's path is identical pre-loop (cpu_inst trace verified)

Captured a full cpu_inst trace from `--bypass-tbblue-fw` (with ScheduleWakeup-allowed PC-gate that excludes RAM-test pass1+2). Supervisor reaches:

- **$00EF self-init**: NR_07/03/C0/82..85/80/81/8A/8F (overwrites bypass init).
- **RAM-test passes at $0130-$01CB** (filtered from log).
- **$01CC post-RAM-test exit**: writes $5B69, sets SP=$5BFF.
- **$01D4 RST 20 → $3E00 wrapper**: flips to bank 2.
- **$1F01 (in bank 2) target**: writes NR_8E=$7a (port_7ffd_bank=7), copies 22 bytes to $ED27 (slot 7 = page 0x2F), JP $ED27.
- **$ED27-$ED3C in slot 7 (page 0x2F)**: executes copied code, returns.
- **$3E13 (in bank 2) trampoline**: writes NR_8E=$00 → switches back to bank 0.
- **$01D7 (in bank 0) continuation**: `nextreg $8e, $08`, then `im 1`, `call $00E3`.
- **$01E0+ further init**: sets sysvars $5CB4, $5C7B, $5CB2; calls RST 28 calculator at $01ED.
- **$01FB**: sets `IY = $5C3A` (canonical 48K BASIC sysvar pointer).
- **$0202+**: calls $0D6B (NR-read function) multiple times (reads NR_05, NR_08, NR_06, NR_0A — checks peripheral config).
- **$024A onwards**: clears RAM, calls $2341, etc.

The supervisor IS executing complex init successfully. It eventually reaches the dispatcher loop at $1DF3/$2043/$20E6, but there's a long initialisation chain we need to follow further to find the upstream divergence.

### Preliminary root-cause hypothesis (refined)

The supervisor reaches the dispatcher loop because somewhere during init, BASIC/NextZXOS reaches a state where the keyboard scan path ($1F40 area) fires before the wrapper-mediated rendering chain. Possibilities:
1. A specific **memory read** returns a value our jnext's read path computes differently from CSpect's
2. A specific **port read** ($FE keyboard, $7FFD, $1FFD, $E3 DivMMC, etc.) returns a different value
3. An **NR read** of NR 0x06 / 0x13 / 0x41 returns a different value (these are the only NRs the supervisor reads in boot, per Agent 2 reviewer)
4. The SD card path (CMD reads via SPI) returns different timing/data

### Next steps

- **Agent 1 (pathcompare) still running** — will identify the exact branch divergence point.
- **CSpect debugger dump still pending** from user (saved as memory reminder).
- **Reviewers complete**: RESET_SOFT review (REWORK — FSM strobe correct but not boot-relevant; supervisor never reads NR 0x02). NR audit review (APPROVE WITH ADDITIONS — no HIGH-severity bugs; NR 0x57 triple-source already fixed by Fix #1).


### 2026-05-05 13:50 — Agent 1 (pathcompare) report + 5-minute follow-up

Agent 1's full report at `doc/issues/G46B-AGENT-PATHCOMPARE.md` (486 lines).

**Definitive finding**: Our jnext and CSpect run **two different code paths** that both terminate at $20E6 — they're NOT the same routine taking different inputs.

#### Our path to first $20E6 (verified via cpu_inst trace + disasm)

```
$00EF self-init → ... → $01CC post-RAM-test exit → $01D4 RST 20 →
... [many init steps] ...
→ $2341 (sprite buffer init at slot 5 $A000-$A150)
   $2348 call $272e (wrapper) — returns with state in E
   $234C dec e; jr z, $238a    ← *** FIRST CRITICAL BRANCH ***
   $234E ld hl,$a000; ld de,$a001; ld bc,$0151; ld (hl),l; ldir
                                      ; clears $A000-$A150 (slot 5)
   $235A ld a, ($5b69)            ; reads RAM-test result
   $235D inc a; add a; ld ($a050), a   ; stores 2*(last_bank+1) at $A050
   $2362 ld a, $10; ld ($a051), a      ; stores $10 at $A051
   $2367..$237D more sysvar setup
   $2380 ld hl, $0001
   $2383 RST 20 with operand $01BD
        → wrapper at $3E00 switches to bank 2
        → $01BD in bank 2 = JP $0E9F
   $0E9F (in bank 2):
       ld bc, $243B
       ld a, $57; out (c), a
       inc b
       in d, (c)              ; D = current NR_57
       ld a, $10; out (c), a  ; *** WRITE NR_57 = $10 ***
                              ; (= map slot 7 to physical page 0x30)
   $0EAD ld ix, $E000
   $0EB1 ld a, ($E050)        ; reads byte at $E050 in slot 7 (page 0x30)
   $0EB4 bit 0, h
   $0EB6 jr z, $0EBF
   $0EB8 ld ix, $E020
   $0EBC ld a, ($E051)
   $0EBF ld b, a              ; B = read byte
   ... eventual IXL adjustment via add ixl + ld ixl, a → IXL = $1B
   ... IX = $E01B
   ... eventually reaches $20E6 with IX=$E01B
```

#### CSpect's path (per Agent 1 + multiple `LD IX, $F700` sites)

CSpect reaches $20E6 with **IX=$F700** loaded directly via:
- bank 0 PC=$2CB5: `ld ix, $F700`
- bank 0 PC=$329A: `ld ix, $F700`
- bank 0 PC=$3409: `ld ix, $F700`

**None of these PCs ever fire `op=DD` in our entire 1.77M-instruction trace.** Our supervisor never executes the bank-0 path that sets IX=$F700.

#### Why the path divergence

The supervisor's `$2341+` code path is reached via $1FE0+ rendering chain. CSpect's equivalent path goes through `$2CB5/$329A/$3409` (in bank 0) which is a DIFFERENT entry point for the same sprite-rendering subsystem.

**Hypothesised candidates for the upstream divergence**:
1. **Sysvar `$5B69`** (= last successful RAM-test bank). Our jnext's RAM-test exits early at PASS 2 first iteration (per `cp $bb; jr nz, $01cc` at $01A4-$01A6 — the byte at slot 7 $FFFF is NOT $BB on our jnext). So `$5B69` = low value. CSpect likely has a higher value (RAM test goes further). When supervisor reads `($5B69)` at $235A, it computes a different `$A050` byte, leading to different IX in `$0EAD`.

2. **Sysvar `$5C7F`** (4-bit machine cfg, per Agent 1's pathcompare).

3. **Memory contents at slot 7 / slot 5** at the moment of writes — since slot 5 (NR_55=$05 default → page $25) and slot 7 (NR_57=$10 → page $30) point to DIFFERENT physical pages, the supervisor's writes to `$A050` (slot 5 → page $25) don't reach the byte read at `$E050` (slot 7 → page $30). **This is suspicious** — it implies the supervisor expects NR_55 to also be $10, OR there's some aliasing we don't model.

#### Critical observation about RAM-test PASS 2

Our supervisor's PASS 2 inner loop at $018E-$01A6:
```
$01A0 ld hl, $ffff
$01A3 ld a, (hl)
$01A4 cp $bb
$01A6 jr nz, $01cc      ← exits if byte at $FFFF is not $BB
```

For bank 0 (NR_56=0, NR_57=1): reads $FFFF in slot 7 with NR_57=1 → physical page... hmm wait, NR_57=1 with to_sram_page(1) = 0x21. Page 0x21 in our jnext = ROM-in-SRAM page 1 (= file offset 0x2000-0x3FFF in enNextZX.rom). Byte at $1FFF in that page = enNextZX.rom file byte 0x3FFF = `$FF` (per xxd).

So our jnext at PASS 2 bank 0 reads $FF, not $BB → exits to $01CC. **Sysvar `$5B69` = bank 0 (lowest possible).**

CSpect must have $BB at $FFFF in some banks → PASS 2 progresses → `$5B69` = higher value.

**Where does $BB at $FFFF come from?** Per supervisor PASS 1:
```
$0153 ld (hl), $bb     ; HL=$0000 here (after wraparound)
                        ; Writes $BB to $0000 in slot 0
```

This writes $BB to **slot 0** ($0000) — but slot 0 maps to ROM-in-SRAM. With AltROM disabled (default), slot 0 ROM-write is **discarded** (read-only). So $BB never gets written.

Wait — let me re-read. After $014E `ld (hl), $00` with HL=$FFFF (write 0 to slot 7 $FFFF), then $0150 `inc hl` (wraps HL to $0000), then $0153 `ld (hl), $bb` (writes $BB to slot 0 $0000).

If slot 0 is read-only ROM, this $BB write is discarded. If slot 0 is AltROM-rw mode (`nr_8c_altrom_en=1` AND `nr_8c_altrom_rw=1`), it goes to AltROM SRAM. But supervisor at $00EF didn't enable AltROM.

Hmm so PASS 1's $BB write doesn't actually take effect. Therefore $5B69 = bank 0 in BOTH our jnext AND on real hardware.

UNLESS — real hardware has different ROM-in-SRAM write semantics. Or — real hardware has $BB pre-loaded at SRAM offsets corresponding to slot 7 $FFFF for some banks.

This needs verification via VHDL. **The PASS 1 wipe-and-mark loop assumes $BB writes succeed**, so either the hardware allows ROM-in-SRAM writes in some mode the bypass init doesn't trigger, OR there's a separate mechanism that puts $BB at slot 7 $FFFF.

#### Concrete next step

If `$5B69` ends up at the wrong value because PASS 2's `cp $bb` always fails on jnext, then **we need to understand how real hardware passes $BB at $FFFF**. This is the immediate next investigation thread.

**Possibility**: maybe `$0153 ld (hl), $bb` is supposed to take effect at slot 7's offset $0000 (= some other physical page), not slot 0. Let me re-check the disasm.

Actually looking again at PASS 1 ($0148-$0153): after `ld (hl), a` at $0148 (writes original $FFFF byte back), $014C `cp $0c`, `jr nc, $0150` (skip wipe if c >= $0C). For c < $0C: $014E `ld (hl), $00` (writes 0 to $FFFF — different from `$BB`!). Then $0150 `inc hl` (wraps $FFFF → $0000). Then $0153 `ld (hl), $bb` writes $BB to slot 0 $0000.

But slot 0 is ROM. The $BB write is discarded.

**This suggests a fundamental hardware behaviour we're not modelling**: maybe ROM-in-SRAM writes ARE allowed in this configuration, populating SRAM that doubles as the RAM-test marker. Need to research VHDL behaviour.


### 2026-05-05 14:00 — VHDL ROM-in-SRAM write semantics VERIFIED + RAM-test hypothesis REFUTED

**Verification done per user request.**

#### VHDL writeback semantics (zxnext.vhd:3010-3132)

`sram_pre_rdonly` is the read-only flag for the early-decode SRAM controller. For slot 0 (`cpu_a(15:14) = "00"`), the rule is:

```vhdl
if mf_mem_en = '1' then
   sram_pre_rdonly <= not cpu_a(13);          -- MF: $0000-$1FFF read-only
elsif mmu_A21_A13(8) = '0' then
   sram_pre_rdonly <= '0';                     -- regular MMU mapping → WRITES OK
elsif nr_03_config_mode = '1' then
   sram_pre_rdonly <= '0';                     -- config mode → WRITES OK
else
   sram_pre_rdonly <= not (nr_8c_altrom_en
                           and nr_8c_altrom_rw); -- ROM-in-SRAM: rdonly
                                                  -- unless AltROM-rw mode
end if;
```

For slots 2..7 (`cpu_a(15:14) ≠ "00"`), at line 3060:
```vhdl
sram_pre_rdonly <= '0';   -- ALL slots 2..7 always writable
                          -- (subject to mmu_A21_A13(8) gate)
```

`mmu_A21_A13` formula (zxnext.vhd:2964):
```vhdl
mmu_A21_A13 <= ("0001" + ('0' & mem_active_page(7:5))) & mem_active_page(4:0)
```

So `mmu_A21_A13(8) = '1'` iff `mem_active_page(7:5) = "111"` i.e. logical page ≥ 0xE0. With NR_50=0xFF default → mmu_A21_A13(8)=1 → goes to `sram_pre_rdonly = NOT (altrom_en AND altrom_rw)` clause → rdonly=1 (with default AltROM off) → writes to slot 0 dropped.

For slot 7 with NR_57 ≤ 0xDF (any RAM page), `mmu_A21_A13(8)=0` → `sram_pre_active = '1'` AND `sram_pre_rdonly = '0'` → **writes to slot 7 always succeed** (modulo bank5/bank7 special cases).

#### Re-reading the supervisor RAM-test PASS 1 inner loop

```asm
$0136 ld a, c       ; main bank, A = c
$0137 exx           ; → alt bank
$0138 add a         ; alt A = c*2
$0139 nextreg $56, a
$013c inc a
$013d nextreg $57, a (= c*2 + 1)
$0140 srl a
$0142 ld hl, $ffff   ; alt HL = $ffff
$0145 ex af, af'
$0146 ld a, (hl)     ; read slot 7 $ffff (alt bank)
$0147 exx            ; → main bank
$0148 ld (hl), a     ; main HL = $4000 (set at $0133), write A to $4000
$0149 ld a, c        ; main A = c
$014a cp $0c
$014c jr nc, $0150
$014e ld (hl), $00   ; main HL = $4000 still, write 0 to $4000
$0150 inc hl         ; main HL = $4001
$0151 exx            ; → alt bank, alt HL = $ffff (from $0142)
$0152 ex af, af'
$0153 ld (hl), $bb   ; *** writes $BB to alt-HL = $ffff (slot 7!) ***
                     ; NOT slot 0 as I earlier mis-read.
$0155 dec hl         ; alt HL = $fffe
... LDDR continues filling slot 6+7 with 0 ...
```

So `$0153` writes `$BB` to **slot 7 `$FFFF`** (alt-bank HL=$ffff, NR_57 = c*2+1). The page mapped is regular RAM (per `mmu_A21_A13(8)='0'` for NR_57 < 0xE0). **The write succeeds.**

#### Empirical verification: instrumented `($5B69)` reads

Added a diagnostic at PC=$01CE/$2341/$20E6 that logs the byte at sysvar $5B69 (the canonical "last RAM-test bank tested" stash):

```
[cpu] [info] RAMTEST PC=0x01ce ($5B69)=0x00  (last RAM-test bank stored)
[cpu] [info] RAMTEST PC=0x2341 ($5B69)=0x6f  (last RAM-test bank stored)
[cpu] [info] RAMTEST PC=0x20e6 ($5B69)=0x6f  (last RAM-test bank stored)
```

`($5B69) = 0x6F` after RAM-test = 111 banks tested = **full PASS 2 completion**. The PC=$01CE log shows 0x00 because that's the value BEFORE the store at $01CE itself — the supervisor's pre-store read.

So:
- The RAM-test `$BB` marker IS placed (in slot 7, at PC=$0153)
- PASS 2 completes all 111 banks
- $5B69 has the same value our jnext + CSpect would both produce (= ROM-side state)

#### Therefore the divergence is NOT in $5B69

My earlier hypothesis "PASS 2 exits early because $BB write hits read-only slot 0" was a misreading of the disasm. The write hits slot 7 (which is writable in normal NR_57 mapping). The RAM-test runs to completion. CSpect would compute the same $5B69 = $6F.

**The ACTUAL upstream divergence remains to be identified.** Most plausible remaining candidates from Agent 1's analysis:
1. `($5C7F)` 4-bit machine cfg — set by ?
2. NR 0x06 read returning different value (early)
3. Some other sysvar set by specific code path

The `$2341 → $2383 RST 20 → bank-2 $01BD JP $0E9F` route is taken on our jnext but evidently NOT on CSpect. Need to trace what conditional branch chooses dispatcher-mode vs sprite-mode print path. Most likely culprit: a NR or sysvar read whose value diverges.


### 2026-05-05 14:30 — Force-IX experiment + ZEsarUX agent + investigation conclusion

#### Experiment: force IX=$F700 + NR_57=$10 at first $1800 hit

Added `JNEXT_G46B_FORCE_IX` env var that, when set, patches IX=$F700 + NR_57=$10 at the first ~16 hits of PC=$1800 (print routine entry). Combined with the `--bypass-tbblue-fw` page 0x30 pre-load (CSpect-captured data).

**Result: BREAKTHROUGH but partial.** Bypass mode escapes the dispatcher loop on the first $1800 hit (vs ~119 hits per loop iteration before). Only **1 FORCE_IX** event fires in 8 seconds — supervisor advances and goes idle. Partial UI render visible: "RED" text + sprite-icons in top-left of screen. But full welcome screen does NOT render.

Conclusion: IX divergence is the proximate cause of the loop, BUT downstream divergences (likely more bad page references, possibly with NR_57 ≠ $10 paths) prevent full render. Force-IX alone isn't sufficient.

#### ZEsarUX vs jnext compare agent — final findings

Agent report at `doc/issues/G46B-AGENT-ZESARUX.md`. Cross-referenced ZEsarUX's NextZXOS support against jnext.

Key conclusions:

1. **No analogous bypass mode exists** in ZEsarUX. Its `--tbblue-fast-boot-mode` is "boot 48K BASIC fallback", NOT a NextZXOS supervisor bypass. **CSpect, ZEsarUX, and real Next hardware ALL run the actual `tbblue.fw` firmware**. There is no upstream template for our `--bypass-tbblue-fw` approach.

2. **MMU semantics are functionally equivalent.** ZEsarUX's compact form vs jnext's `to_sram_page` produce the same effective mappings. The +0x20 shift is implemented (in different ways).

3. **`to_sram_page` exception for pages 0x0A/0x0B/0x0E IS CORRECT but was under-documented.** These pages co-locate jnext's physical SRAM with the dual-port VRAM area accessed by the ULA. Without the exception, CPU writes to slot 6/7 with NR_56/NR_57 = 0x0A/0x0B/0x0E would land in pages 0x2A/0x2B/0x2E while the ULA still fetches 0x0A/0x0B/0x0E — breaking screen updates. ZEsarUX uses a different design (separate Spectrum-bank memory + always-shift), but ours is VHDL-equivalent for observable CPU+ULA behaviour. **Documentation strengthened** in commit (mmu.h:786-820 now cites zxnext.vhd:2961-2962, 3060-3061 + ula.cpp:68).

4. **Soft-reset FSM**: jnext is MORE VHDL-faithful than ZEsarUX (jnext models the `nr_02_reset_type` shift FSM, ZEsarUX returns the last-written byte). Our reset-tree audit (Agent 2) was correct.

5. **AltROM, slot 0/1 writability, NR 0x8E**: all functionally equivalent.

#### Final conclusion: bypass mode is fundamentally limited

Per the agent's analysis, the only viable paths for NextZXOS welcome-screen rendering are:

A. **Run the real `tbblue.fw`** (= what CSpect/ZEsarUX/real-Next do). Our current normal-boot mode reaches the TBBlue boot screen but doesn't render the welcome (per EOD memo). The fix is to debug the firmware-side execution to see what makes it stall during NextZXOS handoff.

B. **Replicate ALL firmware-side SRAM state** in `--bypass-tbblue-fw`. The CSpect-captured `page30.raw` is one part. There are likely more pages + more state we'd need to capture and pre-load. Each piece is a fragile manual replication.

**Recommended path forward**: pivot back to debugging the **firmware boot path** (non-bypass mode). The bypass-mode investigation has produced significant value (Fix #1 cherry-picked to main, supervisor's bank-flip wrapper RE'd, page 0x30 captured) but has hit a fundamental limitation. The architectural sweet spot is what real hardware does.

#### State of g46b-investigation branch

HEAD = current (after this commit). Includes:
- `--bypass-tbblue-fw` CLI option + post-handoff init (12 NR writes + FSM strobe)
- Page 0x30 pre-load (CSpect data)
- `JNEXT_G46B_FORCE_IX` env-gated diagnostic (proves IX divergence is central)
- 6 agent reports under `doc/issues/G46B-AGENT-*.md` (PATHCOMPARE, RESETSOFT, RESETSOFT-REVIEW, NRAUDIT, NRAUDIT-REVIEW, ZESARUX)
- CSpect captures under `doc/issues/cspect-captures/`
- Strengthened mmu.h:786-820 documentation citing VHDL line numbers

Main is unchanged at `2d90ea1` (Fix #1 only). Cleanup of TEMP instrumentation deferred until G46(b) full closure.

---

## 2026-05-05 evening — Probes 1–4 (firmware-path internal-diff)

Following yesterday's pivot recommendation, ran four probes to confirm whether
the divergence vs CSpect originates internally (something jnext does that
differs between bypass and firmware paths) or from external state (firmware-
populated SRAM that CSpect/ZEsarUX have but jnext lacks).

### Probe 1 — `cpu_inst` trace, bypass vs non-bypass at supervisor PCs

8-second runs of both modes, full per-instruction trace, filtered to supervisor
PCs `$00ef|$2043|$20e6|$2341|$1df3|$1800|$2730|$279d`. Logs at
`/tmp/g46b-probe1-{bypass,nonbypass}-full.log`.

**Result: bit-identical supervisor entry sequences in both modes.**

| field at first `$20E6` hit | non-bypass | bypass |
| --- | --- | --- |
| `IX`               | `0xe01b` | `0xe01b` |
| `SP`               | `0xff7f` | `0xff7f` |
| `mmu[0..7]`        | `ff ff 0a 0b 04 05 0e 0f` | `ff ff 0a 0b 04 05 0e 0f` |
| `nr_8c`            | `0x00` | `0x00` |
| `rom_in_sram`      | `true` | `true` |
| `cfg_mode`         | `false` | `false` |
| `($5B69)` (RAMTEST)| `0x6f` | `0x6f` |

Both paths converge to the SAME stall location with the SAME state.

### Probe 2 — NR-write log diff (`nextreg=trace`)

8-second runs both modes, captured every `NextREG write reg=… val=…`. Logs at
`/tmp/g46b-probe2-{bypass,nonbypass}-nr.log` (147 K vs 80 K writes; 642 vs 554
unique reg-val pairs).

NRs written ONLY in non-bypass: `0x02 0x04 0x11 0x28 0x29 0x2a 0x2b 0x88` —
all firmware-side init (RESET, ROM-bank load, video timing, keymap address +
data, joy keymap + data, palette index 0). Bypass skips these because it skips
firmware. None affect supervisor execution post-handoff.

NRs written in BOTH but with diverging value sets: `0x03 0x05 0x06 0x08 0x0a
0x40 0x80 0x83`. Inspection: all are firmware-init transients (machine type
clear-then-set, peripheral 0xa0→0x80→0xa0, palette setup). Final values match.

**No NR-write divergence post-supervisor-handoff.** Both paths leave the
supervisor with equivalent NR state.

### Probe 3 — `$5B6A` / `$5B8A` / `$5B8E` wrapper sysvars

Added TEMP diagnostic (z80_cpu.cpp around line 539): on PC `$2738` and `$27A3`,
log `SP`, `($5B6A)`, `($5B8A)`, `($5B8E)`. Captures the supervisor↔user-mode
context-switch state. Logs at `/tmp/g46b-probe3-{bypass,nonbypass}.log`.

**Result: bit-identical wrapper rotation in both modes.**

```
WRAPPER PC=0x2738 sp=0xff4f ($5B6A)=0x5bff ($5B8A)=0x0000 ($5B8E)=0x00
WRAPPER PC=0x27a3 sp=0x5bff ($5B6A)=0xff4f ($5B8A)=0x0504 ($5B8E)=0xff
WRAPPER PC=0x2738 sp=0xff51 ($5B6A)=0x5bff ($5B8A)=0x0504 ($5B8E)=0xff
WRAPPER PC=0x27a3 sp=0x5bff ($5B6A)=0xff51 ($5B8A)=0x0100 ($5B8E)=0xff
```

Identical across modes. Same wrapper depth, same SP rotation, same NR-pair
saves. Confirms the wrapper itself works correctly and `$5B6A`-based stack
context-switching is functioning.

### Probe 4 — jnext vs ZEsarUX side-by-side

The static-source ZEsarUX comparison agent already covered this question
yesterday (`doc/issues/G46B-AGENT-ZESARUX.md`). Live ZRCP-trace side-by-side
deferred — high setup cost, low marginal info given Probes 1–3 conclusions.

ZEsarUX runs the real `tbblue.fw` (no bypass mode). MMU/AltROM/NR-handler
semantics are functionally equivalent to jnext. The only structural divergence
flagged was the `to_sram_page` exception at mmu.h:798 — already audited as
correct + documentation strengthened (commit `cb8fd2d`).

### Synthesis

Probes 1–3 confirm: **bypass and non-bypass modes produce bit-identical
supervisor state at the loop entry.** This means:

1. The supervisor's loop-trigger state (`IX=$e01b`, `SP=$ff7f`, `mmu=ff ff 0a
   0b 04 05 0e 0f`) is determined by the supervisor itself, not by which boot
   path led to it.
2. Whatever fix unblocks one path will unblock the other simultaneously.
3. The divergence vs CSpect therefore **cannot** come from anything visible in
   internal jnext-only tracing — both paths are symmetric and both wrong.
4. This points (still) at a peripheral / SRAM-state divergence that real
   `tbblue.fw` writes during boot but jnext's bypass-init synthesis misses,
   AND that the non-bypass firmware execution doesn't reach (because something
   else stalls firmware before it gets there).

### Concrete actionable next step

Per the bypass-init in `emulator.cpp:3791-3814`, NRs `0x50–0x57` are NOT pre-set
— they stay at `RESET_PAGES = {0xff, 0xff, 0x0a, 0x0b, 0x04, 0x05, 0x00, 0x01}`.
But CSpect at `$20E6` has `NR_57=0x10` (page 0x30 mapped); jnext arrives with
`NR_57=0x0F` (page 0x2F). The force-IX experiment confirmed forcing `NR_57=$10`
+ `IX=$F700` at first `$1800` produces partial UI render — proximate cause
correct, but downstream divergences remain.

**Highest-ROI follow-up for next session:**
1. Trace what makes the firmware path (non-bypass) stall before NextZXOS
   handoff. Both paths converge on the same stall, but firmware-path stall
   happens at the same supervisor location *despite* having had real
   `tbblue.fw` populate SRAM. Either:
   - tbblue.fw never reaches the SRAM-population stage in jnext (a peripheral
     or DivMMC bug stops it earlier), OR
   - tbblue.fw does run, but jnext's bypass-init synthesis is missing the
     post-firmware NR_55–NR_57 settings.
2. Run a `cpu_inst` trace of non-bypass mode for the first ~5 seconds (= the
   pre-`$00EF` window: bootloader → tbblue.fw → RESET_SOFT → supervisor
   entry). See whether tbblue.fw's NR_55/NR_56/NR_57 writes happen in jnext
   non-bypass mode. If they don't, that's the firmware-side stall.

---

## 2026-05-05 evening — Probe 5: NR_57 lifecycle + wrapper IN reads

### Probe 5a — full NR-write trace (non-bypass, 10s)

`/tmp/g46b-probe5-nb-nr.log` (11 MB, 169 K lines). 21 K NR_55/56/57 writes.

**Firmware DOES write NR_55/56/57 in non-bypass.** Initial values:
`NR_55=0x05, NR_56=0x00, NR_57=0x01` (twice — once during firmware setup,
once at supervisor re-init).

After supervisor takes over (~t=26s wall), a sequential **bank pair sweep**
runs: `(NR_56, NR_57)` cycles `(0x00,0x01) → (0x02,0x03) → … → (0xDE,0xDF)`.
Sweep is the supervisor RAM-test (PASS 1+2 per the existing live doc).

After sweep ends at NR_57=0xDF, supervisor writes:
```
0x10, 0x01           ← wrapper rotation (set 0x10 → run body → restore 0x01)
0x10, 0x01           ← second wrapper rotation
0x11, 0x1f           ← brief transient
0x01, 0x01, 0x01     ← three resets
0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f   ← !!! six writes of 0x0F
0x01, 0x03, 0x05, 0x07, …            ← second sweep starts
```

**The supervisor enters a re-entrant outer loop** that re-runs the RAM-test
sweep periodically, with NR_57=0x0F as the operating value between sweeps.

### Probe 5b — mmu state at first `$00EF` (supervisor handoff)

Augmented PAGES diagnostic to also log live `mmu[0..7]`. First `$00EF` hit:

```
PAGES PC=0x00ef mmu[0..7]=ff ff 0a 0b 04 05 00 01  ...
```

That is RESET_PAGES exactly (per VHDL `zxnext.vhd:4611-4618`). **tbblue.fw
hands off to the supervisor with NR_5x at default reset state — it does
NOT pre-set anything special.**

This confirms: the bypass-init synthesis is correct in leaving NR_5x at
RESET_PAGES (matching real-firmware handoff). The divergence vs CSpect
must be elsewhere.

### Probe 5c — bypass-mode NR_57 trace tail (parallel-structure check)

`/tmp/g46b-probe5-bypass-nr.log`. **Bit-identical** NR_57 sequence to
non-bypass: same sweep, same wrapper rotation, same drift to 0x0F, same
second sweep restart. Confirms (yet again) bypass and non-bypass produce
identical supervisor execution.

### Probe 5d — mmu[6]/mmu[7] at wrapper IN A,($253B) ($27e8 / $27f2)

Added `WRAPPER_IN` diagnostic. Per loop iteration the wrapper is called 4
times:

```
WRAPPER_IN PC=0x27e8 mmu[6]=0x00 mmu[7]=0x01   ← call 1: NR_56 read
WRAPPER_IN PC=0x27f2 mmu[6]=0x00 mmu[7]=0x01   ← call 1: NR_57 read (live=0x01)
WRAPPER_IN PC=0x27e8 mmu[6]=0x00 mmu[7]=0x01   ← call 2 (repeats)
WRAPPER_IN PC=0x27f2 mmu[6]=0x00 mmu[7]=0x01
WRAPPER_IN PC=0x27e8 mmu[6]=0x00 mmu[7]=0x01   ← call 3 (this is the
WRAPPER_IN PC=0x27f2 mmu[6]=0x0b mmu[7]=0x01   ←   one that sets 0x10)
WRAPPER_IN PC=0x27e8 mmu[6]=0x0b mmu[7]=0x10   ← call 4: live NR_57=0x10
WRAPPER_IN PC=0x27f2 mmu[6]=0x00 mmu[7]=0x10
```

Fix #1 path is working: `Mmu::get_page(7)` returns the LIVE NR_57 (`0x01`
or `0x10`), not the stale `regs_[0x57]`. The wrapper saves the correct
old value to $5B8A and restores it on exit.

**But the `$20E6` stall has mmu[7]=0x0F**, NOT 0x10. So `$20E6` is being
run in a DIFFERENT code path (outside the bank-switched wrapper body)
where NR_57 has been written to 0x0F. Per Probe 5a's NR_57 tail, six
writes of `0x57=0x0F` happen between the wrapper rotations and the second
sweep. **Identifying the supervisor PC that writes NR_57=0x0F (vs CSpect,
which presumably writes 0x10 here) is the next concrete instrument.**

### Probe 5 synthesis

1. **Bypass-init NR-handoff state is correct** (matches real-firmware
   handoff = RESET_PAGES). No fix needed there.
2. **Fix #1 (NR 0x50–0x57 read handlers) is working correctly**. Wrapper
   IN reads return live MMU values.
3. **The wrapper bank-switch logic is correct**. Saves+restores NR_56/NR_57
   pair faithfully.
4. **The divergence is in WHICH supervisor code path runs at `$20E6`.**
   On jnext, `$20E6` runs in the "between wrapper rotations" context with
   NR_57=0x0F. On CSpect, `$20E6` runs in the wrapper-body context with
   NR_57=0x10 — i.e., the supervisor's branch decision is different.
5. **Six explicit writes of NR_57=0x0F** happen in the trace between the
   wrapper rotations and the next sweep. These are the divergence-vs-CSpect
   point. Likely PC range: somewhere in the supervisor's "operating mode"
   code that runs after RAM-test settle and before re-entry.

### Concrete next-session instrument

Add a one-shot diagnostic that logs **PC + previous-PC** at every
`NR_57=0x0F` write. That gives the supervisor instruction(s) that decide
"map NR_57 to 0x0F" instead of "map to 0x10". Cross-reference with
disassembly of `enNextZX.rom` to identify the conditional that branches
differently between jnext and CSpect.

Suspected root cause class: a peripheral or NR read that returns different
values under different hardware states — the supervisor reads it, branches
on it, and ends up at NR_57=0x0F instead of 0x10. Candidates:
- NR 0x06 (peripheral 2) read paths
- NR 0x83/0x85 (DECODE_INT1/3, DivMMC enable bits)
- NR 0x8C (AltROM control)
- Sysvar at $5B69 ($5B69=0x6F vs CSpect-unknown — but Probe 5b confirmed
  jnext's value is plausible)
- Memory read at some specific page that diverges (= what was originally
  populated by tbblue.fw in CSpect but not by our bypass synthesis)

---

## 2026-05-05 evening — Probe 6: identify supervisor PC writing NR_57=0x0F

Added `selected_nr` sniffer (tracks last NR selected via OUT $243B,N or
NEXTREG opcodes) + pattern-match for NR_57=0x0F writes via NEXTREG-imm,
NEXTREG-A, OUT(C),A/B/C/D/E/H/L, and OUT($253B),A.

### Probe 6 findings

Three distinct supervisor PCs write NR_57=0x0F via NEXTREG-A (`ED 92 57`,
value from A register):

```
G46B NEXTREG_57=0x0F via NEXTREG-A PC=0x013d  mmu[0..7]=ff ff 0a 0b 04 05 0e 0d
G46B NEXTREG_57=0x0F via NEXTREG-A PC=0x019a  mmu[0..7]=ff ff 0a 0b 04 05 0e 0d
G46B NEXTREG_57=0x0F via NEXTREG-A PC=0x008e  mmu[0..7]=ff ff 0a 0b 04 05 0e 0?  ×6
```

**`$013D` and `$019A` are RAM-test sweep iteration #7** (= bank 7, where
`A = bank*2 = 0x0E`, `INC A` → `0x0F`, then `NEXTREG NR_57,A`):
- bank 0 file `$0139-$0140`: `ED 92 56  3C  ED 92 57  CB 3F` =
  `NEXTREG NR_56,A; INC A; NEXTREG NR_57,A; SRL A` (PASS 1)
- bank 0 file `$0197-$019D`: `87 ED 92 56 3C ED 92 57 CB 3F 08` =
  `ADD A,A; NEXTREG NR_56,A; INC A; NEXTREG NR_57,A; SRL A; EX AF,AF'` (PASS 2)

Both are normal — once per RAM-test pass, A passes through 0x0F.

**`$008E` is a RUNTIME-INSTALLED TRAMPOLINE** (5 bytes:
`ED 92 57  F1  C9` = `NEXTREG NR_57,A; POP AF; RET`). The
bytes at slot 0 `$008E` in physical RAM page 0 differ from the static
enNextZX.rom file (file at `$008E` has `C3 48 5B` = `JP $5B48`) — the
supervisor copies/synthesises this trampoline at boot.

The trampoline is invoked via the self-paging push-continuation-then-call
trick:
```asm
LD A, $0F           ; value to write
PUSH $0082          ; continuation address
CALL $008E          ; or JP via self-paging
; helper:
;   NEXTREG NR_57, A      ← writes NR_57 = 0x0F
;   POP AF                ← discards retaddr-after-CALL
;   RET                   ← jumps to $0082 (= continuation)
```

Probe 6e confirms: at every `$008E` hit, `tos0 = $0082` (constant
continuation address). 6 sequential calls per supervisor iteration with
ascending SP ($5C21 → $5C25 → ... → $5C35 = each call consumes 4 bytes).

### What this means for the divergence vs CSpect

PC=$008E is invoked **by design** by some supervisor sequence that wants
slot 7 mapped to physical RAM page `to_sram_page(0x0F) = 0x0F` (passthrough
exception — AltROM 1 upper). After 6 calls, NR_57 is `0x0F`. Then `$20E6`
sprite-descriptor read fires while NR_57=0x0F, reading from AltROM 1
upper instead of page 0x30 (sprite-descriptor area).

**Two hypotheses for the divergence vs CSpect:**

1. **CSpect's supervisor takes a DIFFERENT branch and never enters the
   "NR_57=0x0F + jump to $0082" path.** Instead, it ends with NR_57=0x10
   and jumps to a different continuation. The jnext path is incorrect
   because some upstream condition diverges.
2. **CSpect runs the same `$008E` trampoline + same continuation $0082**
   but `$0082` itself produces different behaviour because slot 1 ($4000-
   $5FFF) maps to a different physical page in CSpect than in jnext.

Hypothesis 1 (different branch) is supported by yesterday's force-IX
experiment: forcing IX=$F700 + NR_57=0x10 at first $1800 produces partial
UI render. The supervisor can run the "correct" path with NR_57=0x10 and
gets further.

### Concrete next-session instrument

Find the **CALLER** that pushes $0082 and CALLs $008E with A=$0F. With
the cpu_inst PC-range gate currently excluding the RAM-test loops, the
caller may be in the gated range — adjust the gate to include the
relevant slot 0 / slot 1 PC ranges and re-run.

Suggested approach:
1. Adjust cpu_inst gate to also LOG `$0070-$00FF` and `$2700-$27FF`
   (wrapper area).
2. Re-run with cpu_inst trace + Probe 6e WRAPPER_IN — capture the 6-cycle
   sequence:
   `<caller_pc> → PUSH $0082 → CALL $008E → trampoline body → JP $0082 → <next_pc>`.
3. Disassemble `<caller_pc>` to find the conditional that selected
   "NR_57=0x0F + cont=$0082" instead of "NR_57=0x10 + cont=other".

### Probe 6 status

Instrumentation committed on `g46b-investigation` (TEMP — remove on full
G46(b) closure).

---

## 2026-05-05 evening — Probe 7: caller of $008E + slot 0 runtime dump

### Probe 7a — ring buffer of last N PCs

Captures the 32-PC history before each `$008E` hit. Result is identical
across all 6 hits per iteration:

```
ring (oldest→newest):
3ce8 3ce9 3cea 3ceb 3cec 3ced 3cee 3cef 3cf0 3cf1 3cf2 3cf3
3cf4 3cf5 3cf6 3cf7 3cf8 3cf9 3cfa 3cfb 3cfc 3cfd 3cfe 3cff
3d00 0082 0083 0085 0089 008a 008d 008e
```

The supervisor runs 25 sequential PCs `$3CE8..$3D00` in slot 1, then
"falls through" into slot 0 at `$0082`. The path `$0082→$0083→$0085→
$0089→$008A→$008D→$008E` matches the runtime trampoline exactly:
`PUSH AF (1) → LD A,$07 (2) → JR +2 (2) → ADD A,A (1) → NEXTREG NR_56,A
(3) → INC A (1) → NEXTREG NR_57,A`.

### Probe 7b — runtime bytes at $0080-$0095

```
$0080: 78 c9 f5 3e 07 18 02 f5 79 87 ed 92 56 3c ed 92
$0090: 57 f1 c9 06 ff 4e ...
```

Decoded:
- `$0080: LD A,B; RET` — short helper
- `$0082: PUSH AF` ← entry to NR-pair-set helper
- `$0083: LD A,$07` ← hardcoded constant!
- `$0085: JR +2 → $0089`
- `$0087: PUSH AF; LD A,C` (skipped via JR)
- `$0089: ADD A,A → A=$0E`
- `$008A: NEXTREG NR_56, A` (NR_56 = $0E)
- `$008D: INC A → A=$0F`
- `$008E: NEXTREG NR_57, A` (NR_57 = $0F)
- `$0091: POP AF; RET`

So `$0082` is a fixed-constant helper that always sets NR_56=$0E,
NR_57=$0F. It is NOT a generic NR-pair setter — the constant `$07` is
inlined.

### Probe 7c — slot 0 $0000-$00FF runtime dump

Slot 0 RAM at trigger time is **completely different** from the static
`enNextZX.rom` bank 0 lower:

| addr | static rom (bank 0 lower)              | runtime slot 0                          |
|------|-----------------------------------------|------------------------------------------|
| $0000 | `f3 c3 ef 00 45 44 09 02`             | `f3 c3 6a 00 44 56 09 02`               |
| $0008 | `c3 3b 10 2a 2e 2a ff 00`             | `c3 12 05 e1 f5 c3 64 33`               |
| $0010 | `ef 10 00 c9 c3 ef 00 00`             | `df 10 00 c9 3e 3a a7 c9`               |
| $0020 | `c3 00 3e 14 00 04 98 00`             | `33 33 cd ce 00 c3 71 00`               |
| $0030 | `c3 24 10 cd d7 04 77 c9`             | `d9 e3 d5 57 e5 c3 8d 01`               |
| $0080 | `4e 23 46 23 e3 ed 8a 5b`             | `78 c9 f5 3e 07 18 02 f5`               |
| $0090 | `5b f5 c5 01 fd 7f 3a 5c`             | `57 f1 c9 06 ff 4e 23 e5`               |

Almost every byte is different. **The supervisor REWRITES the entire boot
vector area at runtime** — building dispatch tables, RST handlers,
trampolines, and the `$0082` NR-pair helper.

### What this means

The investigation can no longer rely on static disassembly of
`enNextZX.rom`. The supervisor's actual code path at slot 0 is
runtime-constructed and only visible by introspecting jnext's RAM at the
moment of execution.

### Hypothesis update

The earlier hypothesis "CSpect calls a different trampoline with A=0x10"
is too specific. A better framing: **the supervisor at slot 1 `$3CE8`
runs through 25 sequential instructions then falls into a CONSTANT
helper at `$0082` that ALWAYS sets NR_57=$0F**. This is hardcoded
behaviour, not branch-dependent.

So the divergence vs CSpect can NOT be "supervisor takes a different
branch and chooses NR_57=$10". Instead it must be one of:

1. **Slot 1 `$3CE8` contents differ between jnext and CSpect** — the 25
   sequential instructions in slot 1 do something different (e.g., they
   modify A or fall into a different continuation). Slot 1 contents are
   themselves runtime-built; CSpect may build different bytes there.
2. **The supervisor reaches `$0082` from a DIFFERENT slot 1 region in
   CSpect** — e.g., CSpect's supervisor uses `$3DE8` or `$3CE8` with
   different bytes that fall into a different helper.
3. **The 6× call-pattern is intentional supervisor housekeeping with
   NR_57=$0F as the correct operating state**, and the divergence is
   PURELY in the bytes/state at SOME OTHER PC where CSpect would have
   set NR_57=$10. The `$0082` helper is a red herring.

### Recommended next-session probe

Capture the runtime contents of slot 1 area `$3CE0-$3D10` AT the moment
the supervisor enters that range (= dump on PC=$3CE8 first hit, before
any subsequent bank-flip overwrites the visible bytes). The current
Probe 7c dumps slot 1 AT `$008E`, which is too late — slot 1 has been
re-mapped/cleared by then.

Plan:
1. Add diagnostic at PC == $3CE8 first hit: dump slot 1 `$2000-$3FFF`
   contents into a `/tmp/g46b-slot1-at-3ce8.bin` file (8 KB).
2. Run, capture, then disassemble offline with z88dk-dis using the
   captured page as input.
3. Cross-reference the disassembled `$3CE8` code with what should have
   ended up there during boot. Compare against bank 0/1/2/3 upper
   contents to identify which bank was last mapped before this point.

This should reveal the actual code path leading into the `$0082` helper,
and from there we can inspect the conditionals.

---

## 2026-05-05 evening — Probe 8: BREAKTHROUGH — slot 0 IS DivMMC firmware

### Probe 8a — capture slot 0 + slot 1 + DivMMC state at PC=$3CE8 first hit

Added diagnostic in `z80_cpu.cpp` that, on first hit of `pc==$3CE8`:
- Dumps slot 0 (`$0000-$1FFF`) to `/tmp/g46b-slot0-at-3ce8.bin`
- Dumps slot 1 (`$2000-$3FFF`) to `/tmp/g46b-slot1-at-3ce8.bin`
- Dumps physical SRAM page 0 + page 1 (the seeded supervisor pages)
- Logs DivMmc state, mmu[0..7], and the 32-PC ring buffer

### Findings

**Slot 0 at PC=$3CE8 first hit is 100% identical to `enNxtmmc.rom`** (the
DivMMC firmware, 8 KB):

```
$ cmp /tmp/g46b-slot0-at-3ce8.bin /tmp/enNxtmmc.rom
(no output — files are byte-for-byte identical)
```

**Slot 1 at PC=$3CE8 first hit is ALL ZEROS** except a single `c9` (RET)
byte at offset `$1D00` (= PC `$3D00`):

```
slot1 ${0..1CFF}: 0x00 (8192 bytes minus 1 byte)
slot1 $1D00: 0xC9
slot1 ${1D01..1FFF}: 0x00
```

**Physical SRAM page 0 + page 1 ARE seeded with the supervisor binary**
(bypass-init at `emulator.cpp:3580-3584` correctly memcpy's `rom_.page_ptr(p)
→ ram_.page_ptr(p)` for p=0..7). The CPU just isn't reading from them.

**DivMmc state at trigger:**
```
divmmc=[active=1 rom_mapped=1 conmem=0 automap=1 mapram=0]
```

→ DivMMC is **ACTIVE via AUTOMAP** (not CONMEM). Slot 0 reads from DivMMC
ROM (enNxtmmc.rom), slot 1 reads from DivMMC RAM bank 0 (uninitialized
= zeros).

**mmu[0..7] = `ff ff 0a 0b 04 05 00 01`** → `RESET_PAGES` exactly. The
nominal MMU register state is correct, but DivMMC overlay overrides it.

### What this means

The `$008E` trampoline that writes `NR_57=$0F` is **DivMMC firmware code**,
NOT supervisor code. The trampoline at `$0082` (`PUSH AF; LD A,$07; JR +2;
ADD A,A; NEXTREG NR_56,A; INC A; NEXTREG NR_57,A; POP AF; RET`) lives in
`enNxtmmc.rom` at file offset `$0082`. Same binary is used by CSpect, so
this code is identical between emulators.

The execution path:
1. Supervisor code triggered DivMMC AUTOMAP (some trap PC).
2. AUTOMAP active → slot 0 = DivMMC ROM, slot 1 = DivMMC RAM (= bank 0 by
   default = empty).
3. Code somewhere jumped to `$3CC9` in slot 1 (= empty DivMMC RAM).
4. CPU NOPped through `$3CC9 → $3CFF` (49 NOPs).
5. At `$3D00` byte = `c9` → RET. Stack pop returns to `$0082`.
6. DivMMC firmware `$0082` sets `NR_57=$0F`.

**Step 3 is the bug.** The supervisor jumped to slot 1 expecting code at
`$3CE8` (or thereabouts). Slot 1 is DivMMC RAM bank 0, which is empty in
jnext. CSpect must have populated it OR the supervisor in CSpect doesn't
take this jump (= different upstream conditional based on hardware state).

### Why DivMMC RAM is empty in jnext

DivMmc class (`peripheral/divmmc.h`) allocates 128 KB of RAM (16 × 8K
banks), all zero-initialised in the constructor. **Nobody populates it
during boot**:

- Static-rom seeding only loads `enNxtmmc.rom` (8 KB) into DivMMC ROM
  (`emulator.cpp:3625-3637`).
- No code path writes to DivMMC RAM at boot.
- Bypass-init synthesis only writes NRs (`emulator.cpp:3791-3814`).

CSpect must populate DivMMC RAM somehow. Two candidate mechanisms:
1. **`tbblue.fw` populates DivMMC RAM during boot** — by enabling DivMMC,
   writing helper code via slot 1 writes, then disabling. jnext
   non-bypass mode runs the same `tbblue.fw` but apparently doesn't
   reach the populate stage.
2. **The supervisor populates DivMMC RAM at boot** — but this is the
   same supervisor in both emulators, so jnext should populate too.

### Concrete next-session probe

Need to find what populates DivMMC RAM. Two angles:

1. **In jnext non-bypass**: capture supervisor-side writes to slot 1
   while DivMMC AUTOMAP is active. Add a memory-write watch on writes
   to DivMMC RAM (= writes to `$2000-$3FFF` when DivMMC active). If
   there are no such writes, DivMMC RAM in CSpect must be populated by
   `tbblue.fw` or by a different mechanism we're missing.

2. **Disassemble `enNxtmmc.rom` $0066-$1FF8**: the DivMMC firmware
   itself may have an init routine that populates its RAM. Look for
   LDIR or write loops to slot 1 ($2000-$3FFF).

3. **Check if the supervisor or tbblue.fw triggers DivMMC CONMEM mode**
   to write helper code to DivMMC RAM, then turn it off. Search
   enNextZX.rom + tbblue/firmware sources for `port $E3` writes.

### Pre-cursor: where does PC=$3CC9 come from?

The 32-PC ring shows ONLY `$3CC9..$3CE8` — sequential — already
truncated. To find the upstream jumper that landed in `$3CC9`, extend
the ring to >100 entries OR snapshot just before AUTOMAP fires.

DivMMC AUTOMAP fires on these trap PCs (per VHDL): `$0000`, `$0008`,
`$0038`, `$0066`, `$04C6`, `$04D7`, `$0562`, `$056A`. Capture the PC
sequence between the LAST trap PC and `$3CC9`.

### Status

Probe 8 done. Major finding committed. Investigation now has a clear
fix-direction: **populate DivMMC RAM with whatever CSpect has there**,
or **avoid the supervisor falling into empty DivMMC RAM** by ensuring
correct upstream conditionals.

---

## 2026-05-05 evening — Probes 9-11: deeper picture

### Probe 9 — non-bypass mode also has empty DivMMC RAM

Re-ran Probe 8 in non-bypass mode (= full firmware). Same result:
slot 1 at PC=$3CE8 = empty (1 non-zero byte). DivMMC RAM is NEVER
populated, even when running real `tbblue.fw`.

### Probe 10 — disasm of enNxtmmc.rom + sentinel-installer

Static analysis revealed the enNextZX.rom routine at `bank 2 lower
$1F01-$1F28` that installs the **2 sentinel `c9` (RET) bytes** into
DivMMC RAM banks 0 and 1:

```asm
$1F01: NEXTREG $8E, $7A
$1F05: LD HL, $1F13            ; src
$1F08: LD DE, $ED27             ; dst (in supervisor RAM)
$1F0B: LD BC, $0016             ; 22 bytes
$1F0E: LDIR                      ; copy template to $ED27
$1F10: JP $ED27                  ; execute relocated copy

$1F13:  ; the 22-byte template that runs at $ED27:
$1F13: LD A, $81                ; CONMEM=1 + bank=1
$1F15: OUT ($E3), A             ; activate DivMMC, slot 1 = bank 1
$1F17: LD A, $C9                ; opcode = RET
$1F19: LD ($2009), A            ; ★ install sentinel in bank 1 @ slot1+$0009
$1F1C: LD A, $80                ; CONMEM=1 + bank=0
$1F1E: OUT ($E3), A             ; switch to bank 0
$1F20: LD A, $C9                ; RET
$1F22: LD ($3D00), A            ; ★ install sentinel in bank 0 @ slot1+$1D00
$1F25: XOR A
$1F26: OUT ($E3), A             ; deactivate CONMEM
$1F28: RET
```

So the supervisor INTENTIONALLY:
- Activates DivMMC CONMEM mode (which makes slot 1 a writable DivMMC RAM)
- Writes single `c9` RET sentinel bytes at specific offsets in banks 0
  and 1
- Deactivates CONMEM

**This is a working safety-net mechanism.** When the supervisor uses
DivMMC AUTOMAP for a bank-flip-via-NOP-sled, it can RET out of the sled
via this sentinel.

### Probe 11 — supervisor uses AUTOMAP for INTENTIONAL bank flipping

Trace showed at first AUTOMAP activation:
1. Supervisor runs at `$5B0A-$5B20` (in slot 2 sysvar area).
2. RETs to `$0000`.
3. **Slot 0 at `$0000` = `$00` (NOT `$F3` as initial)**. Some prior
   operation remapped slot 0 to a zero page.
4. CPU NOPs through `$0000-$0008` (9 NOPs).
5. At `$0008`, RST $08 trap fires (delayed AUTOMAP per `entry_points_0_=0x83`).
6. At `$0009`, AUTOMAP active. Slot 0 = enNxtmmc.rom (DivMMC ROM).
7. CPU continues into DivMMC ROM bytes:
   ```
   $0009: 12       LD (DE), A
   $000A: 05       DEC B
   $000B: e1       POP HL
   $000C: f5       PUSH AF
   $000D: c3 64 33 JP $3364
   ```
8. Lands in `$3364` (slot 1 = DivMMC RAM bank 0, all zeros except `c9`
   at `$3D00` sentinel).
9. NOPs through `$3364-$3CFF` (= ~2400 NOPs = a deliberate timing
   delay).
10. RET at `$3D00` pops `$0082` from stack.
11. Lands at DivMMC firmware `$0082`: `PUSH AF; LD A,$07; JR +2; ADD A,A;
    NEXTREG NR_56,A; INC A; NEXTREG NR_57,A; POP AF; RET`.
12. Sets `NR_56=$0E, NR_57=$0F`.

This is an INTENTIONAL bank-flip mechanism. The supervisor uses:
- AUTOMAP delayed activation timing to switch banks mid-instruction
- DivMMC RAM as a NOP-sled timer
- Sentinel `c9` to exit the sled cleanly
- Pre-pushed continuation address (`$0082`) on the stack for RET-jump

### What this means for divergence vs CSpect

**The mechanism itself works** — same enNxtmmc.rom code in both
emulators, same supervisor binary, same NR-pair-set helper at `$0082`
that hardcodes A=$07 → NR_57=$0F.

**The divergence is in WHAT VALUE IS ON THE STACK** when the RET at
`$3D00` fires. In jnext, `$0082` is on the stack → land in the NR_57=$0F
helper. In CSpect, presumably **a DIFFERENT value** is on the stack
(e.g., `$0080` = `LD A,B; RET` — no NR write, or `$008E` = direct
NEXTREG NR_57 with whatever A is = could be $10).

So the bug propagates BACKWARDS: jnext's call chain pushed `$0082`
because some upstream conditional took the "NR_57=$0F path" instead of
the "NR_57=$10 path".

### Concrete next-session probe

1. Extend the PC ring buffer to 512+ entries to capture more upstream
   history. Identify the supervisor PC that PUSH'd `$0082`.
2. Track NR_50/NR_04 changes between initial boot ($0000=$F3) and
   first AUTOMAP trap ($0000=$00). Something remaps slot 0 to a
   zero-page just before the trap; what is it?
3. Examine what happens at the SECOND boot iteration. The supervisor
   does work in slot 2 (sysvars `$5B0A`), RETs to a "next-iteration"
   target, but slot 0 is now zero-mapped.
4. Search supervisor binary for the call sequence that pushes `$0082`
   and chains into the AUTOMAP/sled path.

### Status — Probes 1-11 fully documented

Net deltas this session:
- Probes 1-3: bypass + non-bypass converge bit-identical
- Probe 5: Fix #1 verified working
- Probe 6: NR_57=0x0F writers identified
- Probe 7: trampoline path is sequential not CALL
- Probe 8: slot 0 IS DivMMC firmware
- Probe 9: non-bypass also has empty DivMMC RAM
- Probe 10: 2 sentinel bytes are intentional installs from supervisor
  $1F01 (bank 2 lower)
- Probe 11: bank-flip mechanism uses AUTOMAP + NOP-sled + sentinel-RET
- The divergence vs CSpect is **what value is PUSHed onto the stack**
  before the AUTOMAP/sled invocation, which determines the
  NR_57 outcome via different DivMMC-firmware entry points

g46b-investigation HEAD is `b8e2059` after Probe 8. Subsequent probes
9-11 are observational only (no code commits beyond Probe 8 + Probe 9
diagnostic for all 16 banks dump + automap-state dump).

---

## 2026-05-05 evening — Probe 11 CSpect comparison (USER-PROVIDED DUMP)

User provided CSpect screenshot at first hit of `bp 3d00`:

### CSpect state at first $3D00 hit

- **PC = $3D00** (BP fired here as planned)
- **AF = $0144** (A=$01)
- **IX = $E01B** (same as jnext at the equivalent supervisor stall)
- **SP = $5BEF** (supervisor stack — DEEPER pushes than jnext)
- **Stack top (TOS) = $0448** ← **the divergence vs jnext's $0082**
- **NR_57 = $0F**, NR_56 = $0E (already set by previous helper)
- **NR_04 = $00** (default ROM bank)
- **Banks (live mmu): FF FF 0A 0B 04 05 0E 0F**
- **DivMMC: E3:00** (port E3=0, no CONMEM)
- **DivRAM: 00** (= bank 0 default)
- **+3ROM: 3** ← supervisor has selected ROM bank 3 via port 7FFD bit 4 + port 1FFD bit 2
- **NR_B8=$82, NR_B9=$00, NR_BA=$00, NR_BB=$F2** (configured trap PCs)
- **Slot 1 at `$3CE8` = bank 3 upper of enNextZX.rom** (= file 0xFCCE-FCFF):
  ```
  21 5E 5B   LD HL, $5B5E
  71         LD (HL), C
  7A         LD A, D
  CD 13 00   CALL $0013
  57         LD D, A
  70         LD (HL), B
  7B         LD A, E
  CD 13 00   CALL $0013
  5F         LD E, A
  E1         POP HL
  C3 7C 30   JP $307C
  A6 C9      AND (HL); RET
  B6 C9      OR  (HL); RET
  AE C9      XOR (HL); RET
  FF         RST $38
  00 00 00 ... NOPs
  ```
  This is REAL SUPERVISOR CODE — bank 3 upper at file `$FCCE`.

### jnext state at first $3D00 hit (from Probe 8)

- **PC = $3D00**
- **SP = $FF4F-area** (USER stack, not supervisor stack)
- **Stack top = $0082** ← **DIFFERENT from CSpect's $0448**
- **NR_57 = $01** (still default — has NOT been set to $0F yet)
- **NR_56 = $00** (default)
- **mmu[0..7] = `FF FF 0A 0B 04 05 00 01`** — default RESET_PAGES
- **DivMMC: active=1 rom_mapped=1 conmem=0 automap=1 mapram=0 bank=0**
  ← **AUTOMAP IS ON** (mid-cycle in the activation/deactivation pattern)
- **+3ROM: ?** (not captured but presumably default 0)
- Slot 1 at `$3CE8` = empty DivMMC RAM bank 0 (all zeros except c9 sentinel at $3D00)

### Verified: jnext DOES write NR_B8=$82, NR_BB=$F2 (18× each)

Earlier conclusion that "jnext never writes NR_B8/BB" was wrong. Re-checked
the NR-trace and confirmed both NR_B8=$82 and NR_BB=$F2 ARE written 18
times each (= 18 boot loop iterations). The set-difference logic in
Probe 5a was misread.

The supervisor reaches `$01F0-$0217` init code 18 times in jnext, runs
the writes, then eventually loops back to `$00EF`. So the trap config IS
set in jnext.

### The ACTUAL divergence

CSpect's first `$3D00` hit is in a **LATER boot phase**:
- supervisor has already configured NR_56/NR_57 to $0E/$0F
- supervisor has selected +3ROM bank 3 (via port 7FFD/port 1FFD writes)
- AUTOMAP has been CLEARED via off-trap path through `$1FF8-$1FFF`
- supervisor is on its supervisor stack at `$5BEF`
- supervisor is running real supervisor code from bank 3 upper

jnext's first `$3D00` hit is in an EARLIER phase:
- supervisor has NOT yet configured NR_56/NR_57 (still default $00/$01)
- supervisor has NOT switched +3ROM (still default)
- AUTOMAP IS ON (mid-cycle)
- supervisor is on USER stack at `$FF4F`
- supervisor is in DivMMC firmware bank-flip path (= the wrong path)

**jnext is stuck in an early boot loop and never reaches the
post-init phase where bank-3 ROM operations would happen.**

CSpect successfully:
1. Boots → RAM-test → init → NR_B8/BB writes
2. Configures port 7FFD + port 1FFD to select +3ROM=3
3. Switches to supervisor-stack
4. Sets NR_56/57 = $0E/$0F via the helper trampoline
5. Reaches normal operation

jnext gets stuck in step 1 (RAM-test then loop) and never proceeds to 2-5.

### Root-cause hypothesis (REFINED)

The supervisor's boot path REQUIRES some state that jnext doesn't
provide. Candidates:

1. **Specific DivMMC AUTOMAP timing** — CSpect's AUTOMAP cycle happens
   to clear at a different point in the supervisor's boot path,
   allowing certain code to execute. jnext's clear timing is different
   (off by some clocks), causing supervisor to enter the wrong branch.
2. **Port 7FFD / port 1FFD writes** — the supervisor's init code at
   `$0207+` or later writes these ports to select ROM bank 3. If jnext
   doesn't propagate the write correctly (e.g. `+3ROM` doesn't update),
   slot 0/1 stays on bank 0 even after the writes. The supervisor then
   tries to call code in bank 3 upper but reads the wrong bytes.
3. **A peripheral read** — at some boot step the supervisor reads NR or
   port and branches based on the value. jnext returns a different
   value than CSpect, leading to the wrong branch.

### Concrete next-session probe

1. **Add port 7FFD + port 1FFD trace** to jnext. Capture every write +
   the resulting +3ROM bank state. Compare with the CSpect screenshot
   (which shows +3ROM=3).
2. **Trace when jnext reaches $0207 init the FIRST time** vs when CSpect
   does. The first iteration's NR writes are critical.
3. **Find what conditional in the supervisor selects ROM bank 3 vs
   ROM bank 0** at boot. Search enNextZX.rom for port 7FFD writes
   (= `D3 7F` immediate or `OUT (C),r` with BC=$7FFD).
4. **Capture jnext's port_7ffd state at first AUTOMAP off-trap**
   (= when PC reaches $1FF8-$1FFF). If jnext clears AUTOMAP in the
   "wrong" port_7ffd state, supervisor may take divergent path
   afterward.

---

## 2026-05-05 evening — Probe 13: port 7FFD/1FFD writes confirmed working in jnext

Ran jnext bypass with `--log-level port=trace`. Per loop iteration:

```
OUT $253B ← $80      (NEXTREG NR_X = $80 — pre-write)
OUT $7FFD ← $10      (port 7FFD bit 4 set → +3ROM bit 0 = 1)
OUT $1FFD ← $04      (port 1FFD bit 2 set → +3ROM bit 1 = 1)
... bank-3 work ...
OUT $7FFD ← $00      (clear)
OUT $1FFD ← $00      (clear)
```

18 iterations × 4 writes each. **Port writes ARE being honored by jnext**
— +3ROM transitions to bank 3 then back. So the `+3ROM=3` behavior
matches CSpect.

So +3ROM/port-handling is NOT the bug.

## 2026-05-05 evening — Probe 14: VHDL deep-dive on AUTOMAP gating

Re-read VHDL `divmmc.vhd` and `zxnext.vhd:2892-2905, 3137-3138`:

### Per VHDL the AUTOMAP firing conditions are:

```
divmmc_automap_instant_on <= rst_ep AND rst_ep_valid AND rst_ep_timing;
divmmc_automap_delayed_on <= rst_ep AND rst_ep_valid AND NOT rst_ep_timing;

divmmc_automap_rom3_instant_on <= (rst_ep AND NOT rst_ep_valid AND rst_ep_timing) OR
                                   (port_3dxx_msb AND nr_bb(7));
divmmc_automap_rom3_delayed_on <= (rst_ep AND NOT rst_ep_valid AND NOT rst_ep_timing) OR
                                   (port_04xx_msb AND port_c6_lsb AND nr_bb(2)) OR
                                   ... (other tape traps)
```

Then `automap_hold` activates when EITHER:
- `i_automap_active AND (instant_on OR delayed_on OR ...)`  — main path
- `i_automap_rom3_active AND (rom3_instant_on OR rom3_delayed_on)`  — ROM3 path

With **NR_B9=$00 (all valid bits clear)**, `rst_ep_valid=0` → main path
condition false → only ROM3 path can fire.

ROM3 path fires only when `i_automap_rom3_active = sram_divmmc_automap_rom3_en`
is high. Per line 3138:

```
sram_divmmc_automap_rom3_en <= sram_pre_override(2) AND sram_pre_override(0) AND 
   (NOT sram_layer2_map_en) AND (NOT sram_romcs) AND 
   ((sram_altrom_en AND sram_pre_alt_128_n) OR (sram_pre_rom3 AND NOT sram_altrom_en));
```

So ROM3 AUTOMAP needs:
- `sram_pre_override(2)` = DivMMC priority bit
- `sram_pre_override(0)` = some other priority bit
- `NOT sram_layer2_map_en` = L2 not read-mapping the area
- `NOT sram_romcs` = ROMCS pin not asserting
- `(AltROM with 128n) OR (ROM3 selected without AltROM)`

### jnext's check_automap is missing gates

`src/peripheral/divmmc.cpp:315` checks `valid || (rom3_active_ && !layer2_map_read_)`.

This is INCOMPLETE compared to VHDL:
- Doesn't check `sram_pre_override(2)` (the DivMMC overlay priority)
- Doesn't check `sram_pre_override(0)` (other priority bit)
- Doesn't check `sram_romcs`
- Doesn't check the AltROM-vs-ROM3 selector logic

In CSpect, with bank 3 selected, the full priority arbiter probably
results in `sram_pre_override(2)=0` for some reason — preventing the
ROM3 AUTOMAP from firing. jnext skips this check and fires anyway.

### Hypothesis: jnext is missing `sram_pre_override` gating

The VHDL `sram_pre_override` is a 3-bit signal computed by a priority
arbiter that decides which memory overlay is active (DivMMC, Layer 2,
ROMCS, etc.). Bit 2 is set when DivMMC is the winning overlay.

jnext's DivMmc class doesn't track this — it just fires AUTOMAP when
any of: NR_B8 bit set + PC matches RST + (NR_B9 bit set OR ROM3+!L2).

This means jnext fires AUTOMAP in cases where VHDL would NOT
(specifically when `sram_pre_override(2)` is low).

### Concrete next-session fix

1. **Implement `sram_pre_override` priority arbiter in jnext's DivMmc** —
   gate AUTOMAP firing on the same combination of conditions VHDL has:
   - DivMMC priority (`sram_pre_override(2)`)
   - Other priority (`sram_pre_override(0)`)
   - Not L2 read-mapping
   - Not ROMCS
   - AltROM/ROM3 selection logic
2. Verify against CSpect's behavior (re-run with the fix and check
   first $3D00 hit AUTOMAP state matches CSpect's).
3. If all gating is implemented correctly, supervisor should run
   normally without spurious AUTOMAP triggers, and welcome screen
   should render.

This is a **non-trivial fix** — needs to model the VHDL priority
arbiter accurately. But it's now the concrete path forward.



---

## 2026-05-06 morning — sram_pre_override fix LANDED, NOT root cause

### Implementation

Per the implementation handover, the VHDL `sram_pre_override(2)` /
`sram_pre_override(0)` priority-arbiter bits are now modelled in jnext.
Three small surface changes:

1. **`Mmu` helpers** (`src/memory/mmu.h`):
   - `slot_in_rom_area(slot)` — true iff `nr_mmu_[slot] >= 0xE0`
     (= VHDL `mmu_A21_A13(8)=1`). Uses raw `get_page(slot)`, NOT
     `get_effective_page` — VHDL `mem_active_page` is the LOGICAL
     MMU register value (preserves the 0xFF sentinel), not the
     physical SRAM page.
   - `sram_pre_override_divmmc_eligible(pc, mf_active)` — bit 2 of
     `sram_pre_override`. VHDL :3029-3066.
   - `sram_pre_override_romcs_priority(pc, mf_active, config_mode)`
     — bit 0 of `sram_pre_override`. VHDL :3057.

2. **`DivMmc::check_automap`** (`src/peripheral/divmmc.cpp`): added
   two pass-through args `sram_pre_override_2` and
   `sram_pre_override_0` (default `true` for back-compat). Splits
   entry-point matching into:
   - `main_path_eligible = sram_pre_override_2`
   - `rom3_path_eligible = sram_pre_override_2 AND
     sram_pre_override_0 AND !layer2_map_read_ AND rom3_active_`

   Off-trigger (NR_BB bit 6, PC=0x1FF8-0x1FFF) is intentionally NOT
   gated — VHDL divmmc.vhd:131 does not factor `i_automap_active` /
   `i_automap_rom3_active` into `automap_delayed_off`.

3. **Emulator wiring** (`src/core/emulator.cpp`): the `on_m1_prefetch`
   lambda computes both gates per M1 fetch from
   `Multiface::is_mem_active()`, `NextReg::nr_03_config_mode()`, and
   the new Mmu helpers, then passes them to `DivMmc::check_automap`.

### Verification

- **Unit tests**: 14 new MMU tests (`test_cat26_sram_pre_override`,
  PR-01..14) pin the truth-table for the helpers; 6 new DivMmc tests
  (`group_po`, PO-01..06) pin the gate behaviour in `check_automap`.
  All pass. mmu_test 164/0/22, divmmc_test 117/0/0.
- **Full regression**: 32/0/0 PASS — no regressions.
- **Boot smoke test**: NextZXOS still does not reach welcome screen.

### What the fix actually does

Compared to baseline (pre-fix), the runtime trace shape is essentially
identical: same number of $008e helper invocations, same final stuck
state at the sprite-descriptor stall (PC=0x20E6, ix=0xE01B, mmu[0..7]=
ff ff 0a 0b 04 05 0e 0f). The fix is a no-op in this specific boot
trace because:

1. The supervisor's RST traps fire with `nr_mmu_[0]=0xFF` (sentinel,
   ROM area) → `slot_in_rom_area=true` → `pre_override(0)=1`.
2. config_mode=0 throughout the boot loop.
3. No Multiface activation in this path.
4. PC is in slot 0/1 → `pre_override(2)=1`.

So both gates evaluate to true at the AUTOMAP-trigger PCs, and the
behaviour matches the pre-fix path. The fix is correct VHDL-faithfully
but does not address the boot loop.

### Hypothesis from handover memo was incomplete

The handover memo (`project_g46b_2026_05_06_implementation_handover.md`)
posited that `sram_pre_override` was the missing gate causing the boot
loop. After landing the fix, this is **disproven for the bypass path**.
The boot stall is downstream — at the sprite-descriptor stall first
diagnosed by the FORCE_IX experiment (CSpect reaches PC=0x20E6 with
IX=0xF700; jnext reaches with IX=0xE01B).

The fix is still the correct VHDL-faithful behaviour and should land
regardless. It also unblocks any future investigation that needs the
arbiter modelled (e.g., AltROM-aware AUTOMAP gating, future MF
interaction paths).

### Next investigative step

The CSpect-vs-jnext IX divergence at PC=0x1800 (= the print-routine
entry, where IX gets loaded) is the actual divergence point. The
PRECEDING conditional that drives IX selection is what needs hunting
next. Suggested approach:

1. Trace IX writes upstream of PC=0x1800 (= where does IX get its
   value before it's used as a sprite-descriptor pointer?).
2. Identify the conditional that branches to IX=0xF700 vs IX=0xE01B.
3. Check what input drives that conditional — peripheral read,
   memory read, NR read?

### 2026-05-06 review correction — off-trigger gating

Independent reviewer flagged a bug in the off-trigger handling. VHDL
divmmc.vhd:131:

```
automap_hold <= ... OR (automap_held AND NOT
                       (i_automap_active AND i_automap_delayed_off))
```

The off-fire term IS gated by `i_automap_active` (=
`sram_divmmc_automap_en` = `sram_pre_override(2)`). Pre-fix C++ left
the off match un-gated; review caught this and the gate was added
(`&& main_path_eligible`). Coverage:

- Realistic case where this matters: Multiface owns slot 0/1
  (mf_mem_en=1) at the moment CPU happens to fetch from
  0x1FF8-0x1FFF. VHDL keeps DivMMC AUTOMAP held; pre-fix jnext would
  have dropped it incorrectly.
- PO-06 was reworked from a single-fire test to a two-stimulus
  discriminative test (sub-case A: gates open → drop; sub-case B:
  pre_override(2)=0 → held propagates).

This is a real (if narrow) divergence from VHDL that the fix now
closes. Independent review verdict: APPROVE.

---

## 2026-05-06 — Probe 15+16: IX is STALE — control-flow divergence

### Key findings

1. **`LD IX,$E01B` does NOT exist anywhere in `enNextZX.rom`** (verified
   by static byte search).
2. **5 `LD IX,$F700` sites** exist in the supervisor:
   - file `0x2CB5` (bank 0 slot 1, PC=$2CB5)
   - file `0x329A` (bank 0 slot 1, PC=$329A) — gated by `RET NC` at PC=$3290
   - file `0x3409` (bank 0 slot 1, PC=$3409) — fall-through after `LD HL,$0F49`
   - file `0x359C` (bank 0 slot 1, PC=$359C) — after `LD A,($D633)`
   - file `0x2338` (bank 2 slot 1, PC=$2338) — gated by `RET NC` at PC=$2333
3. **jnext NEVER visits ANY of these 5 sites** before reaching the
   sprite-descriptor stall at PC=$20E6. Confirmed by the unique-PC
   control-flow tracer (Probe 16) capturing 166 unique CF
   destinations between the IX=$E01B set and PC=$20E6.
4. **IX=$E01B is computed at PC=$0ECF** in BANK 2 slot 0 via:
   ```
   [0EAD] LD IX,$E000             ; IX = $E000
   [0EB1] LD A,($E050)            ; A = mem[$E050]
   ...
   [0EC7] DEC A                   ; A = mem[$E050] - 1
   [0EC8-CA] RRCA × 3             ; rotate right 3
   [0ECB] AND $1F                 ; mask low 5 bits
   [0ECD] ADD IXL                 ; A += IXL
   [0ECF] LD IXL,A                ; IXL = A → IX = $E01B
   ```
   Reverse-computed: `mem[$E050] = $E0` produces `IXL = $1B` →
   `IX = $E000 + $1B = $E01B`.
5. The `$E000-$E060` data is in slot 7 = `mmu[7]=0x10` =
   physical SRAM page `to_sram_page(0x10) = 0x30` (the AltROM region).
   Pre-loaded from `doc/issues/cspect-captures/page30.raw` in bypass
   mode. CSpect at runtime would have different live data here.
6. **IX=$E01B is then PUSHed and survives across the sprite-prep
   routine** at PC=$23D2-$2415. After `POP IX` at PC=$2413, IX is
   back to $E01B (= the persistent value the consumer at PC=$20E6
   eventually uses).

### The divergence

The supervisor's path to PC=$20E6 in jnext:
```
$2738 (wrapper) → $26B6 → CALL $32CC (BANK 1 slot 1 — supervisor wrapper
                                       at $32CC, NOT the bank-0 LD IX,$F700
                                       routine)
... → $1800 (RST 16 print) → ... → $2043 → $2057 → $1A88 → $205B
... → $2069 → $20A6 → $2178 → $20AC → CALL $20E6
```

Bank 0 slot 1 is loaded at SOME points (the sprite-prep loop at
$23D2-$2415), but the supervisor executes only the small loop body —
not the surrounding code that contains `LD IX,$F700` at $329A or
$2CB5.

### Hypothesis

The supervisor's sprite-descriptor table at memory address $F700 is
**never initialized** in jnext. The consumer at PC=$20E6 reads via
IX (which has stale $E01B from the earlier bank-2 routine) and gets
zeros / garbage.

In CSpect, somewhere in the supervisor's boot path, one of the 5
`LD IX,$F700` sites IS executed, populating $F700 with valid sprite
descriptors AND leaving IX=$F700 for the consumer.

The exact upstream conditional that diverts jnext away from the
table-init routine is **not yet pinpointed**. Most promising lead:
the gating `RET NC` at PC=$3290 (BANK 0 slot 1) — if `RST $20`
returns with carry CLEAR in jnext but SET in CSpect, the
`LD IX,$F700` at $329A is skipped.

### Next-session investigation

1. Extend the trace to cover PC=$3290 specifically when the
   supervisor visits BANK 0 slot 1 — log carry flag at that
   instruction.
2. RST $20 routes through slot 0 PC=$0020 — capture what's there
   at the time (DivMMC firmware? or supervisor code?). Check
   carry-flag setting.
3. Or — look for SD-card / FAT32 reads that may differ between
   jnext and CSpect (the "RST $20" might be a file-read call).
4. Alternative: trace ALL CALL/JP into the BANK 0 slot 1 PC range
   $32xx-$36xx area, see what the supervisor does there in
   jnext vs what it should do.

### Probe 17: BANK is the divergence axis

Probe 17 logs each visit to one of the 5 LD IX,$F700 sites (or the
gating instruction immediately preceding). Result:

```
G46B P17 PC=0x3409 (LD-IX-F700@3409) cf=0 bytes=0x00 0x00 mmu=...
G46B P17 PC=0x359c (LD-IX-F700@359C) cf=0 bytes=0x00 0x00 mmu=...
```

**The supervisor DOES "visit" PC=$3409 and PC=$359C**, but the bytes
there are **`0x00 0x00`** (NOP) — NOT `dd 21 00 f7` (LD IX,$F700).
This corresponds to **BANK 2** in slot 1 (file 0xB409 / 0xB59C —
both zeros), NOT BANK 0 (file 0x3409 / 0x359C — where the actual
LD IX,$F700 lives).

This is the supervisor's **AUTOMAP-NOP-sled mechanism**: it PUSHes a
helper return address (e.g., $0082 for the NR_56/57 set helper),
triggers AUTOMAP via an RST trap, the DivMMC firmware JPs into
slot 1 (= BANK 2 NOP-padding region), the CPU NOPs through until
hitting the sentinel RET at $3D00, then RETs to the PUSHed helper.

So the supervisor is **never running with BANK 0 in slot 1** during
the boot path that leads to PC=$20E6 in jnext. CSpect must, at
some point, switch port_7FFD bit 4 / port_1FFD bit 2 to select
ROM bank 0 (or 3) and execute the menu code that contains
`LD IX,$F700` at PCs $2CB5, $329A, $3409, $359C.

### Refined hypothesis

The G46(b) bug is **a missing ROM-bank switch**: the supervisor in
jnext never switches to BANK 0 (the menu/UI bank) before reaching
the consumer at PC=$20E6. CSpect does. The conditional that drives
this switch is somewhere upstream in the boot/initialization chain.

Investigation focus:
1. Find where the supervisor writes port_7FFD bit 4 + port_1FFD
   bit 2 to select ROM banks.
2. Compare jnext's port write trace with the expected sequence.
3. Probe 13 already showed `OUT $7FFD ← $10` then `OUT $1FFD ← $04`
   in jnext (= +3ROM bank 3) — but only inside a transient window
   (the wrapper). This switch evidently doesn't persist into the
   menu code path.

Combined with the Probe 11 / Probe 13 findings: the BANK-flip
mechanism via AUTOMAP+sled IS WORKING in jnext, but the supervisor's
INTENDED PERSISTENT bank state at the menu-render stage is not what
jnext arrives at. The bypass-mode init may be skipping some init
step that real tbblue.fw would have done.

---

## 2026-05-06 21:30 — Three-agent investigation: bank hypothesis FALSIFIED, real divergence identified

### Three parallel agents launched

- **Agent A**: bypass-init audit vs CSpect NR dump.
- **Agent B**: static analysis of $329A callers + wrapper IN/OUT.
- **Agent C**: ROM-bank trace (Probe 18 added).

### Findings

**Agent C — bank hypothesis FALSIFIED.** BANK 0 IS the resting state in
slot 1 throughout the boot. The 60-event Probe 18 trace shows transient
0/2/0/3/0/1/0 cycles at the wrapper-sled PCs `$3E17 / $3E97 / $5B10 /
$5B1D / $5B4C / $5B51` only — sustained execution sits in BANK 0. The
PC=$20E6 stall fetches from BANK 0 ROM. So Probe 17's "PC=$3409 visited
with bytes 0x00" was the brief BANK 2 wrapper-sled iteration, NOT real
LD IX,$F700 attempt. The hypothesis of a missing sustained bank switch
is **disproven**.

**Agent A — NR mask divergence.** jnext bypass init writes NR 0x82=0xDA,
0x83=0x3D, 0x85=0x01 (matching tbblue.fw's `init_registers()` for +3
mode); CSpect's `nrdump.raw` shows all 0xFF. **However**: the supervisor
itself overwrites these to 0xFF at PC=$00FB (verified by static disasm:
`LD A,$FF; NEXTREG $82,A; ...`). Tested writing 0xFF in bypass —
**zero behavioural change**. Agent A's recommendation is moot; the
discrepancy is just a snapshot timing difference.

**Agent B — wrapper IN/OUT NR_56/57 lead.** Agent B suggested jnext's
NR_56/57 reads via port $253B might return wrong values. Verified:
jnext's read handler at `emulator.cpp:1330` returns `mmu_.get_page(i)`
which is the live `nr_mmu_[i]` value. Probe 13 already confirmed
wrapper port writes are working (jnext writes `OUT $7FFD ← $10` and
`OUT $1FFD ← $04` correctly). So this lead is **also disproven**.

### The real divergence (refined)

Re-reading the CSpect screenshot data carefully:

- **CSpect at first $3D00 hit**: stack TOS = `$0448`, NR_57=$0F,
  NR_56=$0E, +3ROM=3, AUTOMAP=OFF, on supervisor stack (SP=$5BEF).
- **jnext at first $3D00 hit**: stack TOS = `$0082`, NR_57=$01 (default),
  NR_56=$00 (default), AUTOMAP=ON, on user stack.

Both have IX=$E01B (same). The divergence is in WHERE the AUTOMAP-sled
RETurns:

- **CSpect path**: RET pops `$0448` → DivMMC ROM at $0448 has
  `OUT ($E3),A; CALL $045D; XOR A; OUT ($E3),A; NEXTREG $8E,$02;
  PUSH $3F40; JP $1FF9`. This is the **AUTOMAP-off + bank-switch via
  NR $8E** entry — sets NR $8E = $02 (bank-mapping reg), pushes
  $3F40 (next return), JPs into AUTOMAP-off range to deactivate
  AUTOMAP. Then RETs to $3F40 in the now-active supervisor bank.

- **jnext path**: RET pops `$0082` → DivMMC ROM at $0082 has
  `PUSH AF; LD A,$07; JR +2; ... NEXTREG $56,$0E; INC A;
  NEXTREG $57,$0F; POP AF; RET`. This is the **NR_56/$0E + NR_57/$0F
  set helper**. Sets the slot 6/7 mapping and returns.

The CSpect supervisor has invoked the BANK-SWITCH wrapper (BANK 2
PC=$3F30+: `DI; PUSH AF; PUSH IY; OUT ($E3),A; PUSH $0448; JP $3CFC`).
The supervisor must be in BANK 2 to execute this wrapper. The PUSH
$0448 + JP $3CFC sequence triggers the sled with $0448 as return.

### What we still don't know

- **Why jnext pushes $0082 instead.** No `PUSH $0082` (Z80N or
  LD+PUSH) exists anywhere in the 64KB supervisor binary. So $0082
  must be PUSHed via a different mechanism — likely a `RET to
  ($5B5A)` style indirect dispatch, where some sysvar holds $0082
  in jnext but $0448 in CSpect.
- **Where in the supervisor flow** the divergence first occurs that
  causes one path or the other.

### Probe 18 (committed)

ROM-bank-switch tracer in `src/cpu/z80_cpu.cpp:592-616`. Logs every
ROM-bank change with PC. Cap = 60 events.

### Next investigation step

Add a probe that captures the FULL stack contents (~32 bytes) at
the moment of the FIRST AUTOMAP-sled hit (PC=$3D00 sentinel RET).
Compare with CSpect's stack dump. The TOS divergence is known
($0082 vs $0448); the deeper stack might reveal what supervisor
routines were active.

Alternatively: add a write-watch on sysvar $5B5A — log every
write to that address with the PC that wrote it. If $5B5A
controls the dispatch target, the writer PC tells us which
supervisor routine made the choice.

### Probe 19: Stack snapshot at first $3D00 hit

```
G46B P19 PC=$3D00 first hit:
  SP=0x5C01
  stack=[1f22 0000 0000 0000 0000 0000 0000 0000 ...]  (top → bottom)
  sysvars: ($5B5A)=0x0000  ($5B6A)=0xFF51  ($5B8A)=0x0100  ($5B8E)=0x00FF
            ($5B5C)=0x10    ($5B67)=0x04
  ring (oldest→newest): 3CE1 → 3CE2 → ... → 3D00 (NOP-sled run-up)
```

**Stack TOS = $1F22** — NOT $0082 as the earlier memory `project_g46b_2026_05_05_evening_probes.md` claimed. The $0082 in prior probes was at PC=$008E (the helper destination AFTER stack TOS=$1F22 was popped and consumed). At PC=$3D00 the actual TOS is $1F22.

`$1F22` in DivMMC ROM = SD card SPI transfer helper (`LD A,C; LD C,$EB; OUT (C),A; ...` — repeated `OUT ($EB),X` writes for SPI byte exchange).

So at first $3D00 hit, jnext is invoking the **SD card SPI helper** via the AUTOMAP-sled. CSpect's stack TOS=$0448 invokes the **NR $8E + AUTOMAP-off bank-switch** entry.

These are TWO COMPLETELY DIFFERENT operations:
- jnext: read/write SD card data via SPI
- CSpect: deactivate AUTOMAP and switch ROM mapping

CSpect has already finished SD-card init and moved to bank-switching for menu UI. jnext is still in SD-card I/O phase — earlier in boot.

Sysvars at first $3D00 in jnext:
- `($5B5C) = 0x10` and `($5B67) = 0x04` — port_7FFD shadow + port_1FFD shadow, encoding ROM bank 3. **Same as CSpect's +3ROM=3**.
- `($5B6A) = 0xFF51` — saved user stack pointer (wrapper pattern)
- `($5B8A) = 0x0100` and `($5B8E) = 0x00FF` — wrapper-saved bank-pair / control values.
- `($5B5A) = 0x0000` — not yet set (presumably set later when supervisor reaches the bank-switch wrapper).

### Refined hypothesis (final for tonight)

The G46(b) bug is **NOT** an early-boot loop. It's that **jnext's
supervisor reaches the FIRST $3D00 sentinel during SD-card I/O phase,
while CSpect's supervisor reaches it during MENU bank-switch phase**.
This means jnext is GENUINELY EARLIER in the boot sequence at the
first $3D00 hit — and never progresses past the SD-card init phase
to the menu-render phase.

The supervisor in jnext does many AUTOMAP-sled invocations of SD
helpers ($1F22, etc.) but never advances to the menu code. There's
SOMETHING blocking progress through the SD-init phase.

### Most-promising next angles

1. **SD card emulation correctness**: jnext's SD card emulation may
   be returning slightly different responses than CSpect's, causing
   the supervisor to retry SD operations forever. Check
   `src/peripheral/sd_card.cpp` for response semantics.
2. **DivMMC SPI register reads ($EB) returning wrong values**:
   the supervisor reads SD response bytes via IN A,($EB). If jnext
   returns 0xFF or wrong values, the SD operations fail silently.
3. **FAT32 file-read operations**: enNextZX.rom is loaded but
   maybe NextZXOS expects to read additional files from SD (e.g.,
   menu config, fonts). If those reads fail in jnext bypass, the
   supervisor stalls.

### Probe 19 (committed)

Stack-snapshot probe in `src/cpu/z80_cpu.cpp:554-590`. Dumps SP,
top 16 stack words, key sysvars, and the 32-PC ring at first
PC=$3D00. Easy to extend.

---

## 2026-05-06 22:00 — Probes 20+22: $1F22 on stack is STALE garbage, not deliberate push

### Probe 20: port $EB / $E7 trace

Tested two configurations:
1. Default bypass: 950+ events captured. First 6 OUT $EB bytes:
   `3B 00 00 FF BF 24` then 0xFF responses repeat infinitely.
2. With `sd_card_.set_initialized(true)` + `spi_.write_cs(0xFE)`:
   Same byte pattern, same 0xFF responses. **Behaviour unchanged.**

Critical finding: **byte 0 = $3B has top bits 00, NOT 01** — so it's
NOT a valid SD command byte (SPI CMD format requires bit 6=1). The
SD card's `receive()` correctly ignores it (sd_card.cpp:108 requires
`(tx & 0xC0) == 0x40`). So the supervisor isn't actually trying to
send a SD CMD here — `$1F22` invocation is sending random bytes.

**Zero writes to port $E7 (CS) during the entire boot-stall window.**
The supervisor never asserts CS for the SD card.

### Probe 22: pre-sled stack snapshot

At first entry to slot-1 PC>=$3000:
```
PC=$3E00 SP=$5BFD stack=[01D5 0000 ...]
```
That's the FIRST RST $20 wrapper entry. TOS=$01D5 is the correct
return-PC (= byte after `RST $20` at $01D4 in the supervisor's
init code). Normal.

But Probe 19 (at first $3D00 sentinel RET) showed:
```
PC=$3D00 SP=$5C01 stack=[1F22 0000 ...]
```

**SP=$5C01 in jnext vs CSpect's SP=$5BEF — 18-byte (9-word)
difference!** jnext has popped 9 more entries than CSpect. This
means by the time the AUTOMAP-sled fires in jnext, the stack
underflows past `LD SP,$5BFF` initial baseline into uninitialised
data territory at $5C00+.

### What the bytes at $5C01-$5C02 actually mean

The bytes `22 1F` (= $1F22 little-endian) sitting at $5C01-$5C02
weren't deliberately PUSHed there. They are **stale stack data**
from earlier PUSH operations, before the supervisor reset SP via
`LD SP,$5BFF` at PC=$01D1. The AUTOMAP-sled RET at $3D00 pops these
stale bytes and JPs to $1F22 — which happens to be a real entry
(SD SPI helper in DivMMC ROM).

Searched ALL ROMs (enNextZX.rom 64KB, enNxtmmc.rom 8KB,
enAltZX.rom 32KB, enNextMf.rom 8KB) for `Z80N PUSH $1F22` /
`LD HL,$1F22` / `LD DE,$1F22` / `LD BC,$1F22`: **zero matches**.
No code anywhere produces $1F22 as a deliberate push value.

### The actual root cause (refined)

jnext's supervisor stack is in a different state than CSpect's. By
the time the AUTOMAP-sled fires at first $3D00, jnext's SP is
9 words higher than CSpect's. That means CSpect has 9 more PUSHes
that haven't been POPed yet (or jnext has 9 more POPs that
shouldn't have happened).

The "$1F22 vs $0448" difference is just a SYMPTOM of the deeper
stack-state divergence. CSpect's deep stack happens to have $0448
at TOS (because the supervisor pushed it via the BANK 2 wrapper at
PC=$3F39); jnext's shallower stack has stale $1F22 at TOS.

### Probes 20+22 added

- `src/cpu/z80_cpu.cpp:592-616` — Probe 18 (ROM-bank tracer)
- `src/cpu/z80_cpu.cpp:~570` — Probe 19 (stack snapshot at $3D00)
- `src/cpu/z80_cpu.cpp:~600` — Probe 22 (pre-sled stack snapshot)
- `src/cpu/z80_cpu.cpp:G46B_PC_RING_SIZE` — extended to 256 entries
- `src/core/emulator.cpp:3137-3159` — Probe 20 (port $EB/$E7 tracer)
- `src/peripheral/sd_card.h` — added `set_initialized(bool)` (TEMP API)

### Next-session direction

The stack-depth divergence is the real issue. To find it:

1. **Trace SP across boot.** Log every CALL/RET pair with SP before
   and after. Find the FIRST imbalance: a CALL without matching
   RET (or vice versa) that puts jnext's stack in a different
   state than CSpect's.
2. **Compare with CSpect's path.** Probably need a CSpect-side
   stack-trace at different boot phases.
3. **The SD pre-init experiment + CS pre-assert NEITHER changed
   behaviour.** This means the SD-card emulation isn't the gating
   issue at this layer. The bug is upstream in supervisor flow.
4. **Zero $E7 writes** is consistent with "supervisor never reaches
   the SD-init phase that would assert CS" rather than "supervisor
   did SD init but $E7 went elsewhere".

Probably the divergence is in early boot — different RAM-test
results, different conditional branches, different sysvar values.

---

## 2026-05-07 22:38 CEST — Probe 23 SP-tracer landed; SD-helper busy-loop confirmed

### Setup

Added Probe 23 (SP-tracer) at `src/cpu/z80_cpu.cpp:587-694`. Hooks
the per-step path BEFORE `fuse_z80_execute_one()`. On entry:

1. Flushes the PREVIOUS step's pending CF event (now we know
   `sp_after` and `landed_pc` because the prior instruction
   has executed and registers are synced).
2. Classifies the CURRENT instruction (CALL / CALLcc / RET /
   RETcc / RST / RETI / RETN / Z80N PUSH).
3. Captures pre-state (PC, SP, opcode bytes, target) and arms
   the pending event.

Output: `/tmp/g46b-sp-trace.log`, capped at 8000 events.
Maintains a running `depth` counter (++ on push, -- on pop).
Untaken conditional CF ops are tagged `.nt` with `delta=0`.

Smoke run: `--bypass-tbblue-fw`, 30 s timeout.

### Headline findings (smoking gun)

**The boot is stuck in the DivMMC SD-card SPI wait-for-response
busy loop at `enNxtmmc.rom $1F40-$1F48`** — the supervisor never
exits this loop.

The captured trace shows 7881 of 8000 events (98 %) are the loop:

```
RETcc.nt pc=$1f44 op=$c010 sp_b=$5c01 sp_a=$5c01 d=+0 landed=$1f45 depth=-6
```

Disassembly of `enNxtmmc.rom` $1F40-$1F4A (verified by xxd):

```
$1F3D: 01 32 00      ld bc,$0032        ; outer counter = 50
$1F40: db eb         in a,($eb)         ; read SD-SPI byte
$1F42: fe ff         cp $ff             ; idle / no-data?
$1F44: c0            ret nz             ; got non-$FF → return
$1F45: 10 f9         djnz $1f40         ; b--
$1F47: 0d            dec c              ; c--
$1F48: 20 f6         jr nz,$1f40        ; outer retry
$1F4A: c9            ret                ; gave up
```

Helper at `$1F22-$1F3C` is the SD-SPI command sender:

```
$1F22: 79            ld a,c             ; A = command byte
$1F23: 0e eb         ld c,$eb           ; SPI port
$1F25: ed 79         out (c),a          ; cmd
$1F27: 7c ed 79      ld a,h; out (c),a  ; arg-3
$1F2A: 7d ed 79      ld a,l; out (c),a  ; arg-2
$1F2D: 7a ed 79      ld a,d; out (c),a  ; arg-1
$1F30: 7b ed 79      ld a,e; out (c),a  ; arg-0
$1F33: 78 ed 79      ld a,b; out (c),a  ; CRC
$1F36: cd 3d 1f      call $1f3d         ; ★ wait-for-response (loops!)
$1F39: a7            and a
$1F3A: c0            ret nz
$1F3B: 37            scf
$1F3C: c9            ret
```

This perfectly matches the bytes Probe 20 saw on port $EB earlier
(`3B 00 00 FF BF 24`):

| Helper register at entry | Sent byte |
|---|---|
| C = `$3B` | command (NOT a valid SD command — bit 6 = 0) |
| H = `$00` | arg-3 |
| L = `$00` | arg-2 |
| D = `$FF` | arg-1 |
| E = `$BF` | arg-0 |
| B = `$24` | CRC |

`$24/$3B` = NextREG select port low/high bytes. **The supervisor
intended to write to NextREG `(BC)=$243B`, but the AUTOMAP-sled
$3D00 RET dispatched it into the SD-SPI helper instead.** This
is the same divergence Probe 20 caught — now we have the *exact*
loop spot.

### Final 12 events before the loop

```
 109 RET       pc=$5b20 op=$c9cd sp_b=$5bff sp_a=$5c01 d=+2 landed=$0000 depth=-6
 110 RET       pc=$3d00 op=$c900 sp_b=$5c01 sp_a=$5c03 d=+2 landed=$1f22 depth=-7
 111 CALL      pc=$1f36 op=$cd3d sp_b=$5c03 sp_a=$5c01 d=-2 landed=$1f3d depth=-6
 …loop at $1f44 begins, 7881 events…
2617 CALL      pc=$00e7 op=$cd45 sp_b=$5bfb sp_a=$5bf9 d=-2 landed=$0045 depth=-4
2618 CALL      pc=$0056 op=$cd09 sp_b=$26eb sp_a=$26e9 d=-2 landed=$2009 depth=-3
2619 RET       pc=$2009 op=$c900 sp_b=$26e9 sp_a=$26eb d=+2 landed=$0059 depth=-4
2620 RET       pc=$0060 op=$c9dd sp_b=$5bf9 sp_a=$5bfb d=+2 landed=$00ea depth=-5
2621 RET       pc=$00f1 op=$c901 sp_b=$5bff sp_a=$5c01 d=+2 landed=$1f45 depth=-6
```

The loop is CONTINGENTLY exited at event 2616 (eventually B and C
both reach 0 → `RET` at $1F4A → returns to $1F39 → `AND A; RET NZ`
→ returns to $1F3C/wider caller). Then a new tower of CALL/RET
runs starting at PC=$00E7 (event 2617), which RETurns to $1F45
(event 2621) — re-entering the same SD-helper loop on the *next*
DJNZ iteration. The supervisor is in a meta-loop of "drain inner
loop, do some setup, re-enter inner loop" forever.

### Stack-depth statistics (7881 SP events)

- `min_depth = -7` (7 RETs without matching CALL we saw)
- `max_depth = +1` (only one moment where supervisor was 1 net
  CALL deep)

The supervisor's recorded CF events are massively pop-heavy.
Every 1F44 RETcc.nt is delta=0 (untaken), so doesn't affect
depth. The big drift came from the 7 RETs in events 0-12 with
no preceding logged CALL — those POPs ate stack contents that
were placed there by NON-CF instructions (LD (HL),...; or
direct RAM writes during sysvar init).

### Critical sub-finding: event 109 — `RET pc=$5B20 lands=$0000`

- `$5B20` is in **RAM** (NextZXOS sysvar area), NOT ROM.
- Per handover memo §6.2 + §6.6, the supervisor LDIRs a code
  template from ROM `$0091` to RAM `$5B00` at PC=$01DD
  (`CALL $00E3`). $5B00-onwards is the runtime sysvar code.
- The `RET` at $5B20 with bytes `c9 cd` (note: `cd` is the next
  instruction byte → `CALL nn`) pops $0000 from `[$5BFF,$5C00]`.
- $0000 is the **RESET vector**. So the supervisor JPed to its
  own boot vector mid-execution.
- After re-entering at $0000, control flows through the AUTOMAP-
  NOP-sled mechanism to $3D00 (event 110), which pops *the next*
  stack word `[$5C01,$5C02]` = `$1F22`, landing in the SD-SPI
  helper.

### Critical sub-finding: $1F22 at `mem[$5C01,$5C02]`

The bytes `$22, $1F` at RAM `$5C01-$5C02` are what the AUTOMAP-
sled $3D00 RET pops. These bytes were written by some earlier
operation. Per handover memo §4 search results, NO ROM contains
a `Z80N PUSH $1F22` or `LD HL/DE/BC,$1F22` instruction. So the
bytes weren't written by a code-controlled PUSH.

Likely sources:
- Direct memory write `LD ($5C01),HL` with HL=$1F22.
- Indirect via a multi-byte LDIR/LDDR (template copy with $1F22
  embedded as data).
- Stack PUSH from a DIFFERENT SP context (before `LD SP,$5BFF`
  at $01D1) — but PUSHes from SP=$FFFF wouldn't reach $5C01.

### Concrete next-session questions

1. **What's at RAM `$5B20` mid-loop?** It needs to be a `RET`
   instruction (op `$C9`). The prior probe (event 109) confirmed
   bytes there are `c9 cd`. Need to disassemble the LDIR'd
   template at ROM `$0091` to understand what $5B20 means in the
   template. Specifically: is this `RET; CALL nn` a valid
   exit-to-caller pattern, or evidence of mis-located bytes?

2. **Who writes `$1F22` to `[$5C01,$5C02]`?** Add a write-watch
   probe at `$5C01` to log the writer PC.

3. **CSpect comparison.** The user has CSpect available — capturing
   a CSpect SP-trace at the same boot phase would let us diff the
   supervisor's CF history. The first divergent SP/PC pinpoints
   the imbalance.

4. **Is the $0000 RE-ENTRY normal?** In real hardware boot, does
   the supervisor's `$5B20` RET actually go to `$0000`? Or is the
   $0000 a wrong target due to stack corruption?

### Probes added on this branch

- `src/cpu/z80_cpu.cpp:587-694` — **Probe 23** (SP-tracer)
- All previously-listed probes still active.

### Files generated

- `/tmp/g46b-sp-trace.log` — 8000-event SP trace.
- `/tmp/g46b-sp-prelude.txt` — 119 non-loop events (the pre-loop
  call chain + post-loop re-entry chain).

---

## 2026-05-07 23:12 CEST — Probe 23 augmented; **PUSH AF \$1F22 at PC=\$000C identified**

### Summary

Two augmentations to Probe 23, both inside the existing CF-event
hot path so they don't perturb non-CF timing:

1. **Per-event mem dump**: every SP-trace event now also logs
   `mem[\$5BFC..\$5C03]` (the 8 bytes around the AUTOMAP-sled
   pop target). This reveals when each byte changes.
2. **Regular PUSH/POP classification**: `PUSH_BC` (\$C5),
   `PUSH_DE` (\$D5), `PUSH_HL` (\$E5), `PUSH_AF` (\$F5),
   `POP_BC/DE/HL/AF`, `PUSH_IX/IY` (\$DD/FD \$E5), `POP_IX/IY`
   are now logged. The `target` field for PUSH events shows
   the register value being pushed.

### The exact event sequence that writes \$22 \$1F to \$5C01-\$5C02

```
RET       pc=\$5b20 sp_b=\$5bff sp_a=\$5c01 landed=\$0000  ★ wrapper exit
        mem[5bfc..5c03]=25 08 1f 00 00 00 00 00         ← \$5C01,\$5C02 still 00
POP_HL    pc=\$000b sp_b=\$5c01 sp_a=\$5c03 landed=\$000c
        mem[5bfc..5c03]=25 08 1f 00 00 00 00 00
PUSH_AF   pc=\$000c sp_b=\$5c03 sp_a=\$5c01 landed=\$000d  AF=\$1f22  ★★
        mem[5bfc..5c03]=25 08 1f 00 00 22 1f 00         ← NOW \$5C01=\$22, \$5C02=\$1F
RET       pc=\$3d00 sp_b=\$5c01 sp_a=\$5c03 landed=\$1f22
        mem[5bfc..5c03]=25 08 1f 00 00 22 1f 00
```

PUSH AF at PC=\$000C with AF=\$1F22 writes \$22 (F flags) at
\$5C01 and \$1F (A reg) at \$5C02. SP \$5C03 → \$5C01.

Then JP \$3364 (after PUSH AF) enters the NOP sled, which runs
to \$3D00 RET. The RET pops \$5C01,\$5C02 = \$1F22 → JPs to
\$1F22 (DivMMC SD-SPI command-send helper).

### What's at DivMMC ROM \$000B-\$000D?

```
[000b] e1            POP HL
[000c] f5            PUSH AF
[000d] c3 64 33      JP \$3364
```

This is a **generic AUTOMAP-NOP-sled wrapper** with AF as the
target parameter. The supervisor calls this wrapper with AF
set to whatever JP-target it wants. The wrapper:
1. POP HL — restores HL from caller's saved state.
2. PUSH AF — pushes target onto stack (will be popped by sled
   sentinel).
3. JP \$3364 — jumps into NOP sled, which runs to \$3D00 RET.

Note: \$3364 is NOT in slot 1 (\$2000-\$3FFF) directly. \$3364
is in slot 1 mapped via current bank. \$3D00 sentinel RET is
the well-documented sled exit.

### Conclusion

This is **NOT a stack-corruption bug**. The supervisor
**intentionally** sets AF=\$1F22 and uses the wrapper to JP to
\$1F22 (the DivMMC SD-SPI command-send helper). Both jnext and
CSpect should follow this exact path.

The remaining divergence is **upstream**: at \$1F22 entry, the
SD-command parameter registers contain values that look like
NextREG-port setup, not an SD command:

| Reg | Value | Meaning |
|-----|-------|---------|
| C   | \$3B  | low byte of NextREG select port (\$243B) — NOT a valid SD cmd (bit 6 = 0) |
| H   | \$00  |   |
| L   | \$00  |   |
| D   | \$FF  |   |
| E   | \$BF  |   |
| B   | \$24  | high byte of NextREG select port (\$243B) |

So the supervisor seems to be doing **two things at once**:
- Calling SD-SPI command-send helper (via AF=\$1F22 + wrapper).
- With registers prepared for `LD BC,\$243B; OUT (C),B`
  (NextREG select).

That's a weird mix. Likely interpretations:
- (a) The supervisor was setting up NextREG, but a stale
  AF=\$1F22 carried over from an earlier code path → wrong
  wrapper target.
- (b) The supervisor is calling SD-SPI helper with arbitrary
  register state (it doesn't care), and the SD helper just
  loops because no SD response. The bug is that
  jnext's SD emulation never returns non-\$FF for this
  command sequence.

Probe 20 (port \$EB tracer, earlier session) showed jnext sends
bytes \$3B \$00 \$00 \$FF \$BF \$24 over SPI. Byte 0 (\$3B) has
bit 6 = 0 → not a valid SD CMD. So it's plausible the SD card
emulation correctly returns \$FF for every byte (no command to
respond to). The supervisor's wait-loop times out, returns to
caller, which then... what?

### Critical next-session question

**Where is AF=\$1F22 set in the supervisor flow that led to
\$5B00 wrapper?** Need to trace AF backwards from PC=\$5B00
entry. The wrapper saved AF at \$5B00 PUSH AF; restored at
\$5B1F POP AF. So AF at wrapper EXIT (PC=\$5B20) = AF at wrapper
ENTRY (PC=\$5B00).

But trace shows POP_AF at \$5B1F popped value where mem[\$5BFD,
\$5BFE] was \$08 \$1F — i.e., A=\$1F, F=\$08. Then between
\$5B20 RET and PC=\$000C PUSH_AF, **F changed from \$08 to \$22**
while A stayed at \$1F. Some instruction(s) between PC=\$0000
and PC=\$000B affected F flags. Need trace of PCs in that range
to identify what.

Or alternatively: scan the trace for all PUSH_AF events with
target=\$1F22 to find any earlier setting of AF=\$1F22.

### Probes added on this branch

- `src/cpu/z80_cpu.cpp:587-712` — **Probe 23** (SP-tracer, full
  classification including PUSH/POP/IX/IY).
- All previously-listed probes still active.

### Files generated

- `/tmp/g46b-sp-trace.log` — 8000 SP events + per-event
  mem[\$5BFC..\$5C03] dump (~3400 lines for first \$5B20 RET
  area).

### Important note: Mmu::read has a SIDE EFFECT (NOT a timing issue)

Initial diagnosis was "boot is timing-sensitive". User pointed
out this can't be true — the emulator runs in virtual time,
so a slow probe just slows wall-clock, not virtual time.

Real cause: `Mmu::read()` line 319-321 (mmu.h):

```cpp
if (slot_contended_[addr >> 14]) {
    p3_floating_bus_dat_ = val;
}
```

This is a faithful model of VHDL zxnext.vhd:4498-4509 — the +3
floating-bus latch captures the byte on every contended memory
read. Address \$5C01 sits in slot 1 (16K segment 1, \$4000-\$7FFF),
which IS contended.

Probe 24 polled `mem_.read(\$5C01)` and 4 nearby addresses on
EVERY step. Each call clobbered `p3_floating_bus_dat_` with the
RAM byte at that address, instead of leaving it as whatever the
supervisor's last read had set. When the supervisor later read
port \$0FFD (or any port that surfaces the floating bus), it
saw an unintended value → branched onto a different code path.

This is a real emulator-state mutation through the read API,
not a wall-clock timing artefact. The earlier 7-events-vs-8000-
events behaviour is deterministic for the same code; "remove
Probe 24" → revert the clobber → recover the original path.

`Mmu::write()` line 413-415 has the analogous `p3_floating_bus_dat_
= val` write-side mirror, so naive write-side instrumentation has
the same risk.

### Fix landed: `Mmu::peek(addr)` side-effect-free observer

Commit `251ceb6` adds `Mmu::peek(addr)` — same arbiter dispatch
as `Mmu::read()` (boot ROM, MF, DivMMC, Layer 2, alt-ROM, config-
mode, normal slots) but skips the `p3_floating_bus_dat_` latch
update and the data-breakpoint hit-record.

All Probe 23 reads (per-event mem-dump and opcode-byte classification)
now go through `peek()`. Probe 24 re-added with peek-based polling.

Smoke test (commit `251ceb6`):

```
$ JNEXT_G46B_P24=1 timeout 35 ./build/jnext --machine next --headless \
    --sd-card roms/nextzxos-1gb-fat32fix.img --bypass-tbblue-fw \
    --delayed-screenshot-time 25 --delayed-automatic-exit 30
G46B P24 WRITE addr=0x5c01 0x00->0x22 writer_pc=0x000c sp=0x5c01
```

Both probes (SP-tracer + write-watch) agree: PC=$000C PUSH AF
writes the $22 $1F bytes that the AUTOMAP-sled $3D00 RET pops.

### Bonus finding: wrapper is generic

With the side-effect-free trace, the supervisor's AUTOMAP-sled
wrapper at DivMMC $000B-$000D is used for **multiple targets**
across the boot:

- AF=$1F22 → SD-SPI helper
- AF=$00BA → continuation-restore (PUSH HL ; LD HL,($5B5A) ; EX (SP),HL ; RET)
- AF=$00AA → another routine
- ... and others

So this wrapper is a **generic JP-via-AUTOMAP** dispatch with AF
as the target parameter. The bug is therefore: which AF target
jnext picks vs CSpect for a given wrapper call, OR what register
state is set up before each invocation.

### The meta-loop confirmation

Counting PUSH_AF events at PC=\$000C with AF=\$1F22 in the
8000-event trace: **66 occurrences**, evenly spread (every
~120 events). The supervisor is in a meta-loop:

1. \$5B00 bank-flip wrapper enters with AF=\$1F22 in caller's regs.
2. Wrapper saves+restores AF, RETs to \$0000.
3. Some path at \$0000-\$000B (DivMMC ROM, slot 0).
4. PC=\$000B POP HL ; PC=\$000C PUSH AF (target=\$1F22) ; JP \$3364.
5. NOP sled to \$3D00 RET → JPs to \$1F22.
6. \$1F22 SD-SPI helper sends 6 bytes (\$3B \$00 \$00 \$FF \$BF \$24)
   on port \$EB.
7. \$1F36 CALL \$1F3D → wait-for-non-\$FF loop at \$1F40-\$1F48.
8. Loop times out (12,800 iterations × 256 = 3.3M cycles).
9. \$1F4A RET → \$1F39 → \$1F3C RET → some caller of \$1F36.
10. Caller eventually returns through wrapper machinery to a
    point that re-enters \$5B00 wrapper with AF=\$1F22 again.

So jnext reaches a deterministic stuck-loop where it
ENDLESSLY tries to send the same (invalid) SD command and
times out. CSpect's flow either:
- (a) Doesn't call \$1F22 with this exact register state, OR
- (b) Calls it but its SD emulation responds non-\$FF so the
  loop exits successfully and supervisor moves on.

The handover memo earlier disproved option (b): CS pre-assert
+ SD initialized in bypass don't change behaviour. So the
divergence is option (a) — different register / control-flow
state upstream.

### Status of investigation tonight

- **Confirmed**: SD-helper busy loop at \$1F40-\$1F48.
- **Confirmed**: AUTOMAP-NOP-sled at PC=\$000B-\$000D uses AF
  as target. AF=\$1F22 at PUSH AF time → \$5C01 \$5C02 = \$22 \$1F.
- **Confirmed**: 66 round-trips through this loop in 8000 events.
- **Open**: Where AF=\$1F22 is set in the supervisor flow.
- **Open**: Why C=\$3B (NextREG-related) appears in SD command
  parameter slot.
- **Open**: CSpect comparison (would need CSpect SP-trace at the
  same boot phase).

Branch HEAD: `1c50e0c` (this doc commit + 822d852 + 7c03527 +
6c4f17d + 1c50e0c).

### End-of-session 2026-05-07 23:15 CEST

Stopping here. Six sub-commits this session on the
g46b-investigation branch. SP-tracer fully classifies all
stack-affecting opcodes (CALL, RET, RST, RETI/RETN,
Z80N PUSH, regular PUSH/POP, IX/IY PUSH/POP). Per-event mem
dump shows exact stack contents around \$5C00 area.

Next session priorities:
1. Find what sets AF=\$1F22 upstream (manual disasm of $5B00
   callers + tracking AF flow).
2. Find what sets BC=\$243B / register state at \$1F22 entry.
3. Get CSpect SP-trace for cross-comparison.
4. Either find a non-perturbing memory write hook, OR rely
   on careful manual disasm.

## 2026-05-08 09:30 CEST — RAM pre-load BREAKTHROUGH (sysvars + screen + slot7)

### Re-verification of CSpect nrdump.raw

Yesterday's EOD memo claimed CSpect dump showed `NR_82=$82, NR_83=$00,
NR_84=$00, NR_85=$48, NR_8C=$0C`. Re-reading the actual file
`doc/issues/cspect-captures/nrdump.raw` byte-by-byte today
contradicts those values:

```
NR_82 = $FF (default)    NR_83 = $FF (default)
NR_84 = $FF (default)    NR_85 = $FF (default)
NR_8C = $00 (default)    NR_8E = $00 (default)
```

So `NR_82..$85` are all `$FF` — power-on default (all peripheral
hardware-decode bits enabled). Confirms 2026-05-06 Agent A finding
that the supervisor itself overwrites these to `$FF` at PC=$00FB.

The `JNEXT_G46B_NR_CSPECT=1` env-gated test in
`emulator.cpp:3860-3870` was therefore writing WRONG values
(`$82/$00/$00/$48` instead of `$FF/$FF/$FF/$FF`). And the prior
test log `/tmp/g46b-cspect-nr.log` actually shows P25 still firing
with target=$1F22 (not $0082 as the memo claimed) — the memo
conflated the 2026-05-06 first-$3D00-hit-TOS finding ($0082) with
the 2026-05-08 test result.

### Three parallel agents this session

1. **Agent A (enAltZX bytes)**: confirmed enAltZX.rom bank 1
   $7E80-$7FFF is **all zeros** — PC fall-through from $3E93 in
   alt-ROM bank 1 IS what jnext does.
2. **Agent B (tbblue.fw `init_registers`)**: canonical NR write
   sequence for mode=2 is approximately matched by jnext bypass init,
   but several values diverge (NR_05=$41 vs jnext $81, NR_08=$1e vs
   $3e, NR_0A=$00 vs $01, NR_84=$01 vs $ff, NR_85=$00 vs $01). None
   of these matter for boot path (verified by Agent A on 2026-05-06
   with NR_82..$85=$FF test → no behavioural change).
3. **Agent C (VHDL alt-ROM mapping)**: jnext's `altrom_sram_page_()`
   in `mmu.h:960` matches VHDL `zxnext.vhd:3116-3117` exactly. No
   divergence in alt-ROM physical mapping.

### Critical insight — uninitialised stack RAM

Re-reading the 2026-05-08 EOD memo step 8:
> $5B20 RET pops mem[$5BFF,$5C00] = $0000 (uninitialized stack bytes)

In **CSpect's `sysvars.raw` capture** (which covers $5B00-$5CFF):
```
mem[$5BFF] = $00, mem[$5C00] = $FF  →  pops $FF00
```

So in CSpect, the wrapper at $5B20 RET pops `$FF00`, not `$0000`.
PC=$FF00 lands in slot 7 RAM (different code path entirely from
the AUTOMAP-NOP-sled).

**ROOT CAUSE candidate**: jnext's bank 5 RAM is zero-initialised
(`Ram::reset()` at `ram.cpp:28`), so unwritten bytes default to
`$00`. CSpect's RAM has different init — bytes the supervisor never
explicitly wrote retain pre-existing values that lead to a
DIFFERENT control flow. The `sysvars.raw` capture preserves
exactly these "natural" RAM contents.

### The fix — pre-load CSpect RAM captures

Added to `src/core/emulator.cpp:3815-3858` (extending the existing
page30.raw pre-load for bypass-mode):

```cpp
const PreLoad cspect_pre_loads[] = {
    { "doc/issues/cspect-captures/screen.raw",  0x0A, 0x0000, 6912 },
    { "doc/issues/cspect-captures/sysvars.raw", 0x0A, 0x1B00,  512 },
    { "doc/issues/cspect-captures/slot7.raw",   0x2F, 0x0000, 8192 },
};
```

- `page 0x0A` = SRAM bank 5 low half ($4000-$5FFF); per `mmu.h:937`
  `to_sram_page` exception, logical 0x0A maps to physical 0x0A
  unchanged (dual-port VRAM bank).
- `page 0x2F` = slot 7 mapping at the moment CSpect's first $3D00
  hit was captured (NR_57=$0F → physical 0x0F + 0x20 = 0x2F).

### Result — supervisor reaches NEW STATE never seen before

**Before pre-load (default bypass)**:
```
P26 hit#1..#5: eff_mmu = ..04 05 00 01  (slot 6/7 = $00,$01 default)
P25 first PC=$000C: target $1F22, mmu = ..04 05 00 01
```

**With pre-load**:
```
P26 hit#5 NEW: eff_mmu = ..04 05 1e 1f  ← slot 6/7 = $1E/$1F!
P25 first PC=$000C: target $1F22, mmu = ..04 05 00 01  (still ONCE)
```

The supervisor now reaches the point of setting `NR_56=$1E,
NR_57=$1F` — a state that was never reached without the pre-load.
This confirms the pre-load makes the supervisor follow a more
accurate boot path.

### Remaining loop — alt-ROM toggle at $007B (different cadence)

```
P27 alt-ROM enable @ PC=$007B  prev5=26be 26c2 3e93 3e97 007b
P27 alt-ROM disable @ PC=$007B prev5=0071 0072 0076 0077 007b
```

The supervisor enters a periodic loop at ~270 ms wall-clock period
(200 toggle pairs over 60 s). Loop body:

1. Caller @ $26BE (main bank 0) computes a slot-6 mapping based on
   `H` register + NR_$13 (Layer 2 active page) read via port $243B/
   $253B.
2. Calls `$3E80` wrapper which is "CALL_BANK1_INLINE" template:
   - reads inline `DW` after caller's CALL
   - pushes $3E93 (NEXTREG $8E,$01 / RET) + target BC
   - falls into $3E93 → rom_bank=1 → RET to target
3. Target is `$007B` (main bank 1) = `ED 91 8C 80` = NEXTREG $8C,$80
   (alt-ROM ENABLE).
4. RET pops next stack value → eventually $0071 in alt-ROM bank 0
   = a "alt-ROM disable trampoline" at $0071-$007F:
   ```
   $0070: 23 INC HL
   $0071: E3 EX (SP),HL
   $0072: ED 8A 00 7B PUSH $007B (Z80N PUSH16)
   $0076: C5 PUSH BC
   $0077: ED 4B 54 5B LD BC,($5B54)
   $007B: ED 91 8C 00 NEXTREG $8C,$00 (alt-ROM DISABLE)
   $007F: C9 RET
   ```
5. Eventually loop returns to `$26BE` and repeats.

The AUTOMAP-NOP-sled at PC=$000C is hit **exactly once** (P25 is
one-shot-first), then the loop continues without re-hitting AUTOMAP.

### Test run with `JNEXT_G46B_NR_CSPECT=1` + pre-load

No additional progress over pre-load alone. The loop persists at
the same cadence. NR alignment doesn't help once RAM state is
correct.

### Hypothesis for the remaining loop

Two possibilities:
- **(a)** Loop is the supervisor's normal vsync ISR / video-buffer
  juggling routine, called every frame. CSpect reaches the same
  loop but EXITS it because some condition is met. jnext blocks
  progress because some additional state (more RAM, more
  peripheral IO response, a different NR value) is missing.
- **(b)** Loop is a busy-wait for a hardware event that jnext
  doesn't generate (e.g., a specific NR strobe, a port read
  returning a specific value, a CTC/IM2 interrupt).

The bytes sent to port $EB at the now-once-only $1F22 SD-helper
hit are `3B 00 00 FF BF 24` — first byte $3B has bit 6=0, so it's
NOT a valid SD command frame. The helper either aborted or sent
"dummy" clocks to initialise the SD card.

### Concrete next-session experiments

1. **Capture full bank-5/bank-7 RAM dump from CSpect** at multiple
   moments in boot (we have only one snapshot). With more snapshots
   we can pre-load successively closer states.
2. **Add probe at PC=$26BE** to trace the loop's input/output
   state: H register, NR_$13 read value, NR_$56 written value.
   Compare across iterations to see if anything is changing.
3. **Run the same SD image in CSpect with debugger** and verify
   whether CSpect ALSO enters the alt-ROM toggle loop at boot —
   if yes, the loop is normal and we should look at what BREAKS
   CSpect out of it.
4. **Check vsync interrupt delivery**: is jnext generating IM2
   vsync interrupts at the expected rate? If supervisor's loop is
   waiting for `EI ; HALT` to advance, missed interrupts would
   block forever.
5. **Force more aggressive RAM pre-load**: dump and pre-load
   ALL CSpect RAM banks (1 MiB) at the first $3D00 hit moment,
   not just bank 5 + slot 7.

### Branch state

11 commits at HEAD `b4bbd20`. Pre-load fix not yet committed.
About to add 12th commit with the pre-load addition.

### End of 2026-05-08 09:30 CEST entry

## 2026-05-08 09:35 CEST — Probe 28: pre-load is OVERWRITTEN; divergence is in flow, not RAM init

Added Probe 28 at PC=$5B20 (the bank-flip wrapper RET) to verify
whether the sysvars.raw pre-load actually persists to runtime.

### Result — pre-loaded bytes are OVERWRITTEN

P28 hit#1 (SP=$5BFF):
```
mem[$5BFC..$5C07] = 25 08 1F 00 00 00 00 00 00 00 00 00
                                ^^ ^^
                                $5BFF=$00, $5C00=$00 → pops $0000
```

CSpect's `sysvars.raw` at the same offsets:
```
sysvars[$5BFC..$5C07] = 00 57 0C 00 FF 00 00 00 FF 00 22 0D
                                    ^^
                                    $5C00=$FF → pops $FF00
```

So **jnext's supervisor writes $00 to mem[$5C00] BEFORE reaching
$5B20 RET**, overwriting our pre-loaded $FF. The pre-load doesn't
persist.

Comparing the wider stack region:
| Addr | jnext runtime | CSpect sysvars |
|------|---------------|----------------|
| $5BFC | $25 | $00 |
| $5BFD | $08 | $57 |
| $5BFE | $1F | $0C |
| $5BFF | $00 | $00 |
| $5C00 | $00 | $FF |
| $5C01 | $00 | $00 |
| $5C02 | $00 | $00 |
| $5C03 | $00 | $00 |
| $5C04 | $00 | $FF |
| $5C06 | $00 | $22 |
| $5C07 | $00 | $0D |

The bytes $5BFC-$5BFE in jnext look like a stack PUSH pattern
($25 $08 = word $0825, $1F $00 = word $001F). The supervisor's
PUSH/POP sequence leading up to $5B20 in jnext is COMPLETELY
DIFFERENT from CSpect.

### Conclusion — initial RAM state is NOT the root cause

The pre-load helped (supervisor reaches NR_56/57=$1E/$1F never
seen before, AUTOMAP-NOP-sled fires only once), but the deeper
divergence is in the supervisor's CODE EXECUTION FLOW. Different
PUSH/POP patterns produce different stack content.

Possible causes of execution-flow divergence:
1. **Port reads return different values** (jnext vs CSpect SD-SPI,
   keyboard, mouse, etc.).
2. **NR reads return different values** (bypass init NRs we set
   may produce different read-back than CSpect's natural
   tbblue.fw init).
3. **CPU INT/IM2/HALT timing differs** — supervisor may be
   waiting in EI;HALT and missing interrupts.
4. **Z80 instruction emulation bug** producing different register
   state at some specific opcode.
5. **Pre-init hardware state** (port_7FFD, port_1FFD, port_$E3
   DivMMC, etc.) differs from CSpect.

### Next-session experiments

1. Add probes at multiple checkpoints (PC=$26BE entry, $3E80
   entry, $5B00 entry) to dump SP and a few stack words. Identify
   the FIRST checkpoint where stacks diverge from CSpect.
2. Get a CSpect SP-trace using equivalent instrumentation
   (would need CSpect-side debugger / lua hook).
3. Check whether jnext's vsync IM2 interrupt fires at the
   expected rate during boot — supervisor's loops may depend on
   regular ISR firing.
4. Trace ALL port reads during the first 1 ms of boot, compare
   value distribution (e.g., port $FE keyboard, port $EB
   DivMMC SD).

### Probe 28 added

`src/cpu/z80_cpu.cpp:933-953`. Captures first 5 hits of
PC=$5B20 with SP, popped-value, mem[$5BFC..$5C07] dump.

### End of 2026-05-08 09:35 CEST entry

## 2026-05-09 — EOD-6: bank topology fully decoded; AUTOMAP fires; supervisor MAIN never loaded

### TL;DR

Today's session decoded the supervisor's bank-call architecture
end-to-end and confirmed a refined hypothesis. The boot DOES enter
bank 1 via $3E80 long-call wrapper (multiple times per boot), and
DivMMC AUTOMAP DOES fire repeatedly (10+ ON/OFF transitions in 5
seconds). But the supervisor's MAIN code (which lives in bank-7
RAM and is supposed to be loaded from SD) is never produced —
hence no welcome screen. Refined hypothesis: the AUTOMAP-driven
SD-load chain is incomplete in jnext (DivMMC firmware runs but
doesn't issue real SD CMD17 reads, OR they go to wrong destination).

### Bank-0 RST vector decoding (NEW — fully resolved today)

| RST  | Bank-0 $XX bytes              | Routine target | Effect |
|------|-------------------------------|----------------|--------|
| $00  | (PC=$0000 reset path)         | DI; JP $00EF   | cold-boot init |
| $08  | C3 3B 10 (JP $103B)           | $103B          | swap SP, NEXTREG $8E,$78 (RAM bank 7), RET via supervisor SP |
| $18  | C3 80 3E (JP $3E80)           | $3E80 wrapper  | long-call to bank 1 (with inline param) |
| $20  | C3 00 3E (JP $3E00)           | $3E00 wrapper  | long-call to bank 2 (with inline param) |
| $28  | ED 43 54 5B; E3; C3 80 00     | JP $0080       | long-call to bank 3 via $5B48 |
| $30  | C3 24 10 (JP $1024)           | $1024          | NEXTREG $8E,$08, swap SP, RET via supervisor SP |
| $38  | (= $FF byte)                  | error trap     | RST $38 fallthrough |

`$103B` (RST $08 trampoline) is THE critical mechanism: it sets
RAM bank 7 (mapping bank-7 RAM into slot 6/7) and RETs via
supervisor SP. This is how the boot ROM hands off to supervisor
MAIN code resident in bank-7 RAM.

`$3E80` decoded:

```
$3E80: ED 43 54 5B    LD ($5B54),BC
$3E84: E3             EX (SP),HL          ; HL = original return addr
$3E85: 4E             LD C,(HL)           ; C = inline param low
$3E86: 23             INC HL
$3E87: 46             LD B,(HL)           ; B = inline param high
$3E88: 23             INC HL              ; HL = past inline param
$3E89: E3             EX (SP),HL          ; advance return addr on stack
$3E8A: ED 8A 3E 93    PUSH NN $3E93       ; push reciprocal-bank wrapper
$3E8E: C5             PUSH BC             ; push target
$3E8F: ED 4B 54 5B    LD BC,($5B54)       ; restore BC
$3E93: ED 91 8E 01    NEXTREG $8E,$01     ; switch to bank 1
$3E97: C9             RET                 ; pops target → JP target in bank 1
```

Bank 1 mem $3E93 (reciprocal): `ED 91 8E 00 C9` = NEXTREG $8E,$00;
RET — switches back to bank 0 when bank-1 callee RETs.

The same wrapper PC ($3E80) exists in all banks with different
NEXTREG values — same-PC-different-code reciprocal pivots. Same
for $3E00 (bank-2 dispatcher) and $3F00 (bank-1 ↔ bank-2 pivot).

### Bank-flip wrapper at $5B00..$5B20 (RAM-resident, fully decoded)

LDIR'd from bank-0 $0091 (82 bytes) to RAM $5B00 by boot $00E3:

```
$5B00: F5 C5            PUSH AF / PUSH BC
$5B02: 01 FD 7F         LD BC,$7FFD
$5B05: 3A 5C 5B         LD A,($5B5C)        ; saved 7FFD
$5B08: EE 10            XOR $10             ; toggle bit 4
$5B0A: F3               DI
$5B0B: 32 5C 5B         LD ($5B5C),A
$5B0E: ED 79            OUT (C),A           ; port_7FFD ← toggled
$5B10: 01 FD 1F         LD BC,$1FFD
$5B13: 3A 67 5B         LD A,($5B67)        ; saved 1FFD
$5B16: EE 04            XOR $04
$5B18: 32 67 5B         LD ($5B67),A
$5B1B: ED 79            OUT (C),A           ; port_1FFD ← toggled
$5B1D: FB               EI
$5B1E: C1 F1            POP BC / POP AF
$5B20: C9               RET                 ← THIS is the $5B20 hit
```

This wrapper does NOT change RAM bank — it toggles ROM-low bit
(7FFD bit 4) and 1FFD bit 2. Net effect: rom_bank XOR 3 (0↔3,
1↔2). Used by the supervisor to flip between paired ROM banks.

`$5B5C` and `$5B67` are init'd to ZERO by bank-1 supervisor at
$00EC (`XOR A; LD ($5B5C),A`). Both jnext and CSpect have these
at zero (per CSpect's sysvars.raw at offsets $5C, $67).

### Probe 42 (replaces P40): comprehensive band-aid

Reload sysvars.raw + screen.raw + slot7.raw at EVERY $5B20 entry
(cap 200). With this band-aid, the boot makes substantially more
progress than the one-shot P40:

- P28 pops sequence: $FF00, $010E, $010E, $2200 (real supervisor
  PCs, not $0000).
- P44 captures bank-1 entry confirmed:
  - hit#1 rom_bank=0 → bank-1 $3485 (from boot $025A `RST $18`)
  - hit#2 rom_bank=0 → bank-1 $1500 (from boot $0274 `RST $18`)
  - hit#3 rom_bank=1 → bank-0 $2668 (from bank-1 $1506
    `CALL $3E80; dw $2668`)

So the boot DOES enter bank 1 via the long-call wrapper. Bank-1
$1500 contains:

```
$1500: CD B6 15         CALL $15B6
$1503: CD 8F 15         CALL $158F
$1506: CD 80 3E         CALL $3E80
$1509: 68 26            (inline = $2668 = bank-0 target)
```

### Probe 43: RST $08 → $103B trampoline never fires from rom_bank=0

P43 watches first hits on $0008 / $103B / $1040 / $104D / $1051.
Result: $0008 fires ONLY at rom_bank=$03 (BASIC ROM context,
where $0008 is BASIC ERROR-1, NOT the JP $103B trampoline).
$103B / $1040 / $104D / $1051 NEVER fire.

This is significant: the JP $103B path (which would set RAM bank
7 + RET into supervisor MAIN at slot 7) is unreachable in jnext
because the boot never executes RST $08 with rom_bank=0.

### Probe 44: $3F00 reached only with rom_bank=$00

P44 watches $3E80 wrapper hits + $3F00 hits with rom_bank.
$3F00 fires only with rom_bank=$00 (bank-0 mem $3F00 = utility
code `LD (HL),A; JR $3EE7`, NOT the dead-loop I initially
mis-disassembled). For bank-2 supervisor MAIN to start, $3F00
needs rom_bank=$01 (bank-1 mem $3F00 = wrapper to bank 2).

In jnext, rom_bank=$01 + PC=$3F00 NEVER occurs.

### Probe 45: AUTOMAP DOES fire repeatedly (refined hypothesis)

P45 hooks DivMMC's `automap_active` state — captures every
ON→OFF / OFF→ON transition with PC + slot mapping + rom_bank.
With P42 band-aid, 10 transitions in 5 seconds:

```
ON  pc=$000A rom_bank=$03  # RST $08 from BASIC ROM
OFF pc=$0002
ON  pc=$000A rom_bank=$03  # RST $08 again
OFF pc=$17F8 (auto-unmap range $1FF8-$1FFF)
ON  pc=$000C rom_bank=$03
OFF pc=$17F8
ON  pc=$003A rom_bank=$02  # near IM 1 vector area
OFF pc=$0051
ON  pc=$3F19 rom_bank=$02
OFF pc=$0000
```

So AUTOMAP IS active. slot 0/1 stays $FF/$FF (legacy mapping)
but DivMMC overlay should override at runtime. The bug is NOT
"AUTOMAP never fires" — it's "AUTOMAP fires + DivMMC firmware
runs but supervisor MAIN never loaded into bank-7 RAM".

### Refined hypothesis (CRITICAL — drives next investigation)

The supervisor MAIN code is **NOT statically present in
enNextZX.rom**. Bank 7 of enNextZX.rom doesn't exist (file is
only 4 banks = 64 KB). Bank-7 RAM (logical pages $0E/$0F)
must be loaded at boot time from the SD card.

In CSpect:
1. tbblue.fw IPL reads SD, loads supervisor MAIN into bank-7 RAM,
   sets up sysvars.
2. Boot ROM AUTOMAP sentinel install at $1F01 enables DivMMC
   trapping.
3. When boot does `RST $08` with rom_bank=0, JP $103B sets RAM
   bank 7 + RETs into supervisor MAIN at slot 7.
4. Supervisor MAIN runs, draws welcome screen, processes input.

In jnext (`--bypass-tbblue-fw`):
1. tbblue.fw skipped → bank-7 RAM is zeroed by boot ROM cascade.
2. Boot AUTOMAP sentinel installed correctly (P45 confirmed).
3. AUTOMAP fires on subsequent traps but DivMMC firmware doesn't
   issue SD reads to populate bank-7 RAM with supervisor MAIN
   (or it does, but writes go to wrong physical RAM page).
4. RST $08 from rom_bank=0 NEVER fires because no caller reaches
   that path (boot ROM relies on supervisor presence).

### Next-session experiments

1. **Probe SPI port $E7 writes** during boot. Watch SD-card CMD
   bytes (CMD17 = read single block = $51, CMD18 = read multi-block
   = $52). If these never fire, DivMMC firmware doesn't issue SD
   reads. If they fire, check destination addresses.

2. **Probe DivMMC bank-7 RAM writes**. Add a write-watch on
   physical pages $2E/$2F (= bank-7 logical $0E/$0F + $20 Next
   shift). If no writes ever land there post-AUTOMAP, supervisor
   MAIN never gets loaded — confirming the hypothesis.

3. **Compare against CSpect**: instrument CSpect (Lua hook?) to
   capture SD CMD17 sequence + destination addresses during boot.
   Diff against jnext.

4. **Diagnostic-inject supervisor MAIN**: pre-load
   `doc/issues/cspect-captures/slot7.raw` directly into physical
   page $2F at the FIRST `$5B20` band-aid hit (not the boot init,
   so it survives the LDDR cascade). Then JR $0008 (RST $08) to
   force the trampoline. If welcome screen renders, supervisor
   MAIN absence is confirmed.

### Probes added this session (cumulative on g46b-investigation)

- P42 (replaces P40): comprehensive band-aid (sysvars+screen+slot7
  at every $5B20 hit, cap 200). z80_cpu.cpp:1181-1240.
- P43: first-hit watch on $0008 / $103B / $1040 / $104D / $1051
  (RST $08 → bank-7 trampoline path). z80_cpu.cpp:1273+.
- P44: track $3E80 wrapper hits + $3F00 hits with rom_bank +
  inline param target. z80_cpu.cpp:608+.
- P45: track AUTOMAP active-state transitions with PC + slot/rom
  context. z80_cpu.cpp:608+ (just before P44).

Branch state: `g46b-investigation` HEAD `7462d70`, 21 commits
off `89e18de`.

### End of 2026-05-09 EOD-6 entry


## 2026-05-09 EOD-7 — SD CMD-by-CMD trace identifies the failure point

### TL;DR

Today's session deeply traced the boot's SD card protocol activity and
identified the EXACT divergence point. The boot does NOT use DivMMC
firmware to load supervisor MAIN — instead, the supervisor's bank-2
ROM (and DivMMC firmware) BOTH do SD I/O directly via SPI ports
\$E7/\$EB. We traced the full CMD sequence and identified that the
firmware abort happens at or just after CMD9.

Branch HEAD `e6e9f62`, 24 commits off `89e18de`.

### P20/P46 probes — SPI activity full picture

P20 (port-write trace) + P46 (PC sample on every 1000th IN \$EB read)
revealed the boot's SD activity:

**Pre-init phase** (\~40ms):
- 6 OUT \$EB bytes (initial junk: \$3B \$FF \$00 \$FF \$BF \$24)
- 5000+ IN \$EB reads (all returning \$FF) — busy-wait poll for
  non-\$FF response that never comes (state IDLE = always \$FF).

**Init CMD sequence per iteration**:
```
CMD12 (recovery) → CMD12 (recovery again) → CMD0 (reset) →
CMD8 (send_if_cond) → CMD55 (app_cmd) → ACMD41 (send_op_cond) →
CMD58 (read_ocr) → CMD9 (send_csd) → [FIRMWARE RESTARTS]
```

**Without CMD9 implemented (pre-fix)**: 1 iteration, then no SD activity.
**With CMD9 returning NCR+R1+token+CSD+CRC**: 100 iterations in 5s,
firmware loops the entire init.

### P46 SPI poll-PC samples

```
sample #1 .. #12000:  pc=$1F40 rom_bank=$03  (DivMMC firmware @ $1F40)
sample #13000..#15000+: pc=$1928 rom_bank=$02 (supervisor bank-2 @ $1928)
```

Both sites contain the IDENTICAL SD-poll routine:

```
LD BC,$0032        ; B=0, C=$32 (50*256 = 12800 retries max)
loop:
  IN A,($EB)
  CP $FF           ; non-$FF = response received
  RET NZ
  DJNZ loop
  DEC C
  JR NZ,loop
  RET              ; timeout (A=$FF)
```

The firmware does NOT issue full-duplex reads — `OUT $EB` doesn't
clock in response. Instead, response polling uses dedicated `IN $EB`
loops AFTER the CMD frame is sent. State machine in our SD emulation
correctly transitions IDLE→RECEIVING_CMD (6 bytes)→process_command()
→RESPONDING (response queued).

### SPI activity is NOT under AUTOMAP — boot ROM does direct SPI

Critical: P20+P45 cross-reference shows ALL SPI commands fire with
AUTOMAP=OFF. The boot ROM (enNextZX.rom) does SD I/O DIRECTLY via
ports $E7/$EB, NOT through the DivMMC firmware. AUTOMAP fires
elsewhere (possibly for unrelated paging tricks) but not for SD reads.

This rules out the earlier hypothesis "DivMMC SD-load chain is
broken". The SD-load is in supervisor bank-2 code itself.

### Decoded supervisor SD CMD9 path (bank-2 $1802 onward)

```
$17FF: LD HL,$F700           ; CSD destination buffer in slot-7 RAM
$1802: CALL $1904            ; CMD9 wrapper:
                             ;   sends CMD frame, reads NCR+R1
                             ;   waits for $FE token via $1933
                             ;   INI 18 bytes (16 CSD + 2 CRC) into ($F700)
$1805: JR NC,$1840           ; CMD9 failed → error path
$1807: AND A
$1808: JR Z,$1843            ; A=0 → alternative path
$180A: LD HL,($F706)         ; capacity calc using CSD bytes 6,7
$180D: LD A,L; AND $03       ; mask C_SIZE high bits (CSD v1.0 layout!)
$1810-$185A: complex C_SIZE arithmetic ...
$185D: JR NC,$183C
$1840: LD A,$00; RET         ; error → return $00
$1843: LD HL,($F708)         ; alternative path uses bytes 8,9
$1846-$1862: more capacity calc ...
```

The firmware extracts C_SIZE from CSD bytes 6/7/8/9 assuming **CSD
v1.0 (SDSC)** format. CSD v2.0 (SDHC) has those bytes in different
positions, so SDHC-format CSD yields wrong capacity → firmware aborts.

### Three CSD/OCR experiments — none unblocked the boot

| Experiment | Result |
|------------|--------|
| Original SDHC v2.0 CSD + OCR CCS=1 | 100 iterations, abort at CMD9 |
| CSD = all $0B (ZEsarUX-style)      | 100 iterations |
| OCR = $05/$00/$00/$00/$00 (no CCS) | 80 iterations |
| CSD v1.0 SDSC (proper format)      | 80 iterations |

So the firmware's failure is NOT triggered by CSD content. Even with
proper CSD v1.0 layout, the firmware loops.

### ZEsarUX research findings (parallel agent, 6 recommendations)

ZEsarUX (`/home/jorgegv/src/spectrum/zesarux/src/storage/mmc.c`) has
a known-working SD emulation that boots NextZXOS:

1. **Does NOT implement CMD55+ACMD41** — ZEsarUX returns illegal
   command for unknown CMDs, NextZXOS firmware works around it.
   Our firmware DOES use ACMD41, so this isn't the same path.

2. **CMD9/CMD10 response shape**: NCR ($FF) + R1 + $FE token +
   16 register bytes + 2 CRC ($FF $FF). MATCHES ours.

3. **CSD = all $0B** (line 44). Tried — no change.

4. **OCR = $05 + $00 $00 $00 $00** (line 47). Tried — no change.

5. **State machine is index-based**, not named states. Cosmetic.

6. **Pre-init: returns $FF on $00 case for TBBlue**. We return $FF
   in IDLE state too — matches.

So ZEsarUX-style values don't unblock our boot. The bug isn't in
the SD emulation responses themselves.

### Probes added this session (cumulative on g46b-investigation)

- P42 (replaces P40): comprehensive band-aid (sysvars+screen+slot7
  reload at every $5B20 hit, cap 200).
- P43: first-hit watch on $0008 / $103B / $1040 / $104D / $1051
  (RST $08 → bank-7 trampoline path).
- P44: track $3E80 wrapper hits + $3F00 hits with rom_bank.
- P45: track AUTOMAP active-state transitions with PC + slot/rom.
- P46: sample PC + rom_bank every 1000th IN $EB read (cap raised
  to 5000).

### Concrete next-session experiments

1. **PROBE THE FIRMWARE'S DECISION POINT POST-CMD9**: Add an SD-emul
   trace log of all read response bytes returned to the firmware
   for the FIRST init iteration. Identify exactly which response
   byte the firmware sees that triggers the abort. Trace bank-2
   PC for the period RIGHT AFTER $185A or $1843 to find the
   abort-decision branch.

2. **TRY ACMD41 POLLING**: Maybe ACMD41 needs multiple iterations
   returning IDLE before READY (real cards take 20-100ms). Currently
   ACMD41 returns READY ($00) on first call. Make first 3 calls
   return $01 (idle), then $00 (ready). Test if firmware loops
   ACMD41 properly.

3. **PROBE CMD58 OCR RESPONSE BYTES**: Maybe the issue is in CMD58
   not CMD9. The firmware might check CCS bit AFTER ACMD41 and
   route differently. Trace exactly what bytes the firmware reads
   from CMD58 response.

4. **STUDY ZEsarUX'S HOST-SIDE BEHAVIOR**: launch ZEsarUX with
   --debug, capture exact sequence of SPI activity, compare with
   jnext's. Difference will reveal the divergent CMD or response.

5. **AUDIT SPI HARDWARE EMULATION**: Check `src/peripheral/spi.cpp`
   for any timing/state quirks. Maybe write_data/read_data
   semantics are off (real SPI is full-duplex; our model is
   half-duplex).

### End of 2026-05-09 EOD-7 entry


## 2026-05-07 EOD-8 — TBBlue logo reached; bypass-mode parked

### TL;DR

**Major progress**: real TBBLUE.FW now boots through `MMC_Init()`
successfully with our SD emulation. Screen shows TBBlue logo +
Firmware v1.44.db + Core v3.02.03 + "For video mode selection
press: A=All, D=Digital, V=VGA, R=RGB". Same state the user has
been observing in their own runs — we are at parity, not regressed.

Branch HEAD `c4a72b4`, 27 commits off `89e18de`. Rollback marker
tag `g46b-pre-zesarux-rewrite` at `c19cd9b`.

### STRICT user directives (2026-05-07)

1. **No `--bypass-tbblue-fw`** until NextZXOS welcome menu renders.
   Bypass-mode is parked. All G46(b) testing now exercises the real
   TBBLUE.FW boot path.
2. **No `--delayed-keypress`** for the video-mode-selection screen —
   TBBLUE.FW auto-advances on its own timeout.

### What landed

#### Commit `f0a7a35` — `persistent_response_byte` infra (KEEPER)

Added `persistent_response_byte_` field to `SdCardDevice`. After a
CMD's transient response queue drains, IDLE state's read returns
this byte instead of $FF. CMD handlers set it post-response:
- CMD0 → $01 (sustained while last_command=0x40)
- CMD12 → $01 (sustained while last_command=0x4C)

Reset to $FF on: new CMD start byte, deselect(), reset()/mount().

Mirrors ZEsarUX `storage/mmc.c:846` switch behavior (per-command
sustained response while `mmc_last_command` is unchanged).

In bypass mode this dropped SD CMD count from 644-801 in 5s to 10
(single clean init, no retries). The supervisor in bypass mode
reached bank-2 main entry for the first time. In non-bypass mode it
doesn't significantly change MMC_Init (which only sees CMD12/CMD0
once each), but it does no harm.

#### Commit `c4a72b4` — CMD58 reverted to standard SD spec

Earlier today I had also changed `cmd58_read_ocr` to ZEsarUX-style
`{$FF, $05, $00, $00, $00, $00}` per the agent's recommendation.
That broke TBBLUE.FW's `MMC_Init`. The firmware checks R1 with
`and #0xFE` at `SD_SEND_CMD_2_ARGS_TEST_BUSY` and rejects any
non-zero result — R1=$05 has bit 2 (illegal-command) set → init
aborts with "Error initializing SD card!" displayed on screen.

Reverted to standard SDHC R3 response: `{$FF, R1=$00 (when
initialized), $C0, $FF, $80, $00}` — CCS bit set so TBBLUE.FW
treats card as SDHC.

### TBBLUE.FW MMC_Init source decoded

Extracted via mtools from the SD image at
`src/firmware/loader/src/mmc.s` → `/tmp/loader-mmc-unix.s`.

Init flow:
1. CS=$FF, 80 clocks of $FF
2. CS=$FE, CMD12 (cancel multi-sector), 9 dummy reads
3. CMD0 with up to 16 retries
4. CMD8 with arg=$1AA + check pattern $AA
   - Carry set → SDv1 path (CMD1)
   - Carry clear → SDv2 path (ACMD41)
5. ACMD41 / CMD1 with 120 × 256 retries
6. CMD58: read OCR, test bit 6 (CCS) of byte 0
   - CCS=0 → CMD16 to set block size 512
   - CCS=1 → SDHC, no CMD16
7. Return 3 (SDHC) or 2 (SDSC)

R1 check at `SD_SEND_CMD_2_ARGS_TEST_BUSY`:
```
ld b, a       ; save R1
and #0xFE     ; mask out idle bit
ld a, b
jr nz, setaErro  ; ANY non-idle bit set → ERROR
```

Our CMD58 must return R1 with only bit 0 (idle) potentially set, no
other bits. R1=$00 (when initialized) satisfies this.

### ZEsarUX deep-dive — 10 insights extracted

(See EOD-7 entry for the full breakdown. Summary: ZEsarUX's
`storage/mmc.c` is pragmatic-but-not-spec-compliant. Some patterns
we adopted; some don't apply to TBBLUE.FW v1.44.db's flow.)

1. mmc_r1 gate (return mmc_r1 when not idle).
2. CMD8 deliberately broken (returns $00) — for OLDER firmware.
3. CSD dynamically computed from image size.
4. OCR is 9-byte response, sustained R1 across reads.
5. CMD12 = single $01.
6. CMD17 has only 1 CRC byte.
7. CMD18 first-byte=$FE quirk for SDHC.
8. CS deassert sets mmc_r1=1 (idle).
9. CMD55+ACMD41 NOT implemented.
10. Port wiring matches ours.

### Next session priorities

1. **Run with longer timeout, no keypress** — TBBLUE.FW should
   auto-advance past video-mode prompt. See where it stalls next.
2. **If it stalls at video-mode prompt indefinitely**, check
   CTC/RTC/timer emulation — maybe our timer doesn't tick fast
   enough for TBBLUE.FW's auto-advance.
3. **Trace CMD17/CMD18 reads** once boot moves past video-mode.
   These should fire when TBBLUE.FW reads MBR/BPB and loads
   modules.

### End of 2026-05-07 EOD-8


## 2026-05-08 01:15 CEST — BREAKTHROUGH: NextZXOS supervisor handoff working

### TL;DR

**Major progress**. After running the no-bypass smoke (per EOD-8 next-step
plan), three independent agents identified that jnext's `Mmu::reset` was
preserving `port_7ffd_reg`, `port_1ffd_reg`, `port_dffd_reg`, and related
paging state across soft reset — but per VHDL `zxnext_top_issue5.vhd:880`
(`reset <= reset_hard or reset_soft`) those registers MUST clear on both
reset types. The supervisor's deliberate NR 0x02 ← 0x01 soft reset relied
on hardware to clear rom_bank to 0 so that PC=$0000 lands in
`enNextZX.rom` bank 0's `DI; JP $00EF` cold-start. With the bug, the CPU
landed in bank 2's `NOP; JR $0000` infinite-loop sentinel.

**Fix landed (uncommitted)**. After the fix, post-soft-reset:
- rom_bank cleared 3 → 0 ✓
- AUTOMAP runs at $006a → exits at $00ef ✓
- NextREG $07,$03 (28 MHz) executed ✓
- NextREG $03,$b0 executed ✓
- LDDR cascade (P35) iterates through bank pairs 00..0B ✓
- Sysvars zeroing at $5C00-$5C3F ✓
- Supervisor running through bank-flip wrappers, AUTOMAP $003A, ATTR-ENTER hits, slot-7 entries

`mmu_test` 164/186 PASS / 0 FAIL / 22 SKIP after the test fixture updates.

### Diagnosis chain

#### Step 1 — first smoke after EOD-8

Re-ran the no-bypass / no-keypress smoke at 60s. Output: EXIT=124 (timeout)
with no screenshot. Log showed:
- 1st soft reset at ~14s (TBBLUE.FW handoff — expected).
- Supervisor ran ~21s polling SD at PC=$1972 (rom_bank=0x02).
- 2nd soft reset triggered at ~36s — followed by tight `$006a ↔ $0000`
  AUTOMAP-toggle loop with rom_bank=0x02.

#### Step 2 — added PC + rom_bank to NR 0x02 reset log

Modified `emulator.cpp:1476-1485` to dump CPU PC + rom_bank + mmu7 in the
soft/hard reset log lines. Re-ran:
- 1st reset PC=$6D31, rom_bank=0x00 (TBBLUE.FW post-MMC_Init handoff)
- 2nd reset PC=$3BF5, rom_bank=0x03 (supervisor in bank 3)

#### Step 3 — disassemble bank 3 around $3BE8

Extracted `enNextZX.rom` from SD via mtools, disassembled bank 3:

```
[3be8] 3e 02         ld   a,$02
[3bea] ed 79         out  (c),a    ; assumes BC=$243B (NR select)
[3bec] 04            inc  b        ; BC=$253B
[3bed] ed 78         in   a,(c)    ; A = current NR 0x02
[3bef] e6 80         and  $80      ; preserve only bit 7
[3bf1] f6 01         or   $01      ; force soft-reset
[3bf3] ed 79         out  (c),a    ; ← NR 0x02 ← bit7|0x01 = SOFT RESET
[3bf5] ff            (next byte)
```

Bank 3 at $0000 = `f3 af 01 3b 24 c3 e8 3b` = `DI; XOR A; LD BC,$243B; JP $3BE8`
— bank 3 cold-entry IS the soft-reset routine. Bank 2 at $0000 =
`00 18 fd` = `NOP; JR $0000` — INFINITE LOOP TRAP. Bank 0 at $0000 =
`f3 c3 ef 00` = `DI; JP $00EF` — proper boot entry to NextZXOS init.

#### Step 4 — three parallel agents (per Task 1 rules)

Launched three agents in parallel (auto mode, isolated read-only contexts):

- **Agent A (RE supervisor caller chain)**: confirmed bank-3 cold-entry
  $0000 IS the soft-reset issuer. NO state setup before the call — supervisor
  relies on hardware to clear rom_bank.
- **Agent B (RE bank entries)**: confirmed bank 0 = NextZXOS cold-start
  + browser; bank 1 = +3DOS / DOTcommand; bank 2 = supervisor + FAT32 +
  trap sentinel at $0000; bank 3 = 48K BASIC ROM with $0000 repurposed as
  soft-reset entry.
- **Agent C (VHDL audit)**: the smoking gun.

Agent C's findings (verbatim verbatim VHDL excerpts):

```
zxnext_top_issue5.vhd:880  reset <= reset_hard or reset_soft;
zxnext_top_issue5.vhd:2384 i_RESET => reset
zxnext.vhd:1730            reset <= i_RESET;

zxnext.vhd:3646-3648  if reset = '1' then port_7ffd_reg <= (others => '0');
zxnext.vhd:3713-3715  if reset = '1' then port_1ffd_reg <= (others => '0');
zxnext.vhd:3686-3690  if reset = '1' then port_dffd_reg <= (others => '0');
                                          port_dffd_reg_6 <= '0';
```

**Inside zxnext.vhd, `reset='1'` is the OR'd hard|soft signal — every
`if reset='1'` clause fires on BOTH reset types.** The previous "Branch C
architectural decision" comments in `mmu.cpp:67-86` claimed otherwise; that
was a misreading.

### Fix

`/home/jorgegv/src/spectrum/jnext/src/memory/mmu.cpp` `Mmu::reset(bool hard)`:
dropped `if (hard)` guards on `paging_locked_`, `contention_disabled_`,
`port_dffd_reg_`, `port_dffd_reg_6_`, `port_eff7_reg_2_`, `port_eff7_reg_3_`,
and the `nr_8c_reg_` lo→hi nibble copy. Also added unconditional clears of
`port_7ffd_ = 0` and `port_1ffd_ = 0` (these were never in the prior reset
at all). Removed the now-redundant `apply_legacy_paging_()` call at the
end of reset (with all port state zeroed it's a no-op).

`/home/jorgegv/src/spectrum/jnext/test/mmu/mmu_test.cpp`:
- DFF-08 inverted: now asserts `port_dffd_reg` clears + MMU6/7 reset to seed.
- EF7-05 inverted: now asserts `port_eff7_reg_{2,3}` clear + slots 0/1 → ROM.

`/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp` reset log lines
now include PC + rom_bank + mmu7 (permanent diagnostic).

### Post-fix smoke (longer 90s timeout)

Boot now progresses past the trap. After 2nd soft reset:
- ROM-BANK changes 53× (bank-flip wrappers running)
- 10 SLOT7-ENTER hits (supervisor dispatching through slot 7 mapped code)
- Multiple AUTOMAP cycles
- PC reaches $053D in rom_bank=3 (deep NextZXOS code)
- Last log timestamp ~71 wall-seconds after start

But still no welcome screen, no writes to $4000-$57FF (bitmap) or
$5800-$5AFF (attr) — supervisor is running but hasn't reached the screen
paint phase. Likely still doing FAT32 mount, file finding, etc.

### Branch state

Branch `g46b-investigation` HEAD `bc27887` (EOD-8 doc commit). Files
modified, not yet committed:
- `src/core/emulator.cpp` (reset-log diagnostic enhancement)
- `src/memory/mmu.cpp` (the fix)
- `test/mmu/mmu_test.cpp` (DFF-08, EF7-05 inversion)

Independent reviewer agent launched (agent ID a177a71ae2e352f6a).

### Next steps after this commit

1. After reviewer approval → commit fix to branch.
2. Investigate why supervisor doesn't reach screen paint:
   - Add P30-style watcher on $5800-$5AFF + $4000-$57FF.
   - Trace the supervisor call graph after 2nd reset (what's it doing
     at $053D, $1805, $1808, $19C2 etc?).
3. The boot might still be making real progress — try even longer
   timeouts (180s+) before concluding it's stuck.


## 2026-05-08 02:30 CEST — SD-preserve-on-soft-reset experiment (reverted)

After the Mmu::reset fix landed (commit `1bdc7d5`), boot still stalls
post-2nd-reset in heavy SPI polling at PC=$1972 in rom_bank=0x02. Tested
hypothesis: real Next hardware soft reset (NR 0x02 ← 0x01) does NOT
power-cycle the physical SD card. Tried gating `sd_card_.reset()` and
the SD remount on `!preserve_memory` (= hard reset only), with a
`sd_card_.deselect()` on soft reset to model the FPGA SPI controller's
CS-deassert edge.

**Result: regression**. With the SD preservation change, the supervisor
stalls in SPI poll BEFORE issuing the 2nd soft reset (NEVER issues it,
even after 75 wall seconds and 16 million SPI poll samples). Total OUT
$EB byte count post-1st-reset was identical (#845-#850 = the same 6
CMD12 bytes), but the supervisor never advances to its `boot.c`
final-handoff path that triggers NR 0x02 ← 0x01.

**Conclusion**: the supervisor's pre-handoff state machine DEPENDS on
sd_card_.reset() (full re-init) firing on the 1st soft reset (TBBLUE.FW
handoff). The SD card "looks like" it just got powered up to the
supervisor's eyes, and the supervisor walks through its full SD init
again before reaching the final NR 0x02 ← 0x01.

This is NOT VHDL-faithful from a hardware-architecture standpoint, but
it matches what the supervisor expects, so leave the full SD reset on
all reset types. The change was reverted.

The TRUE next-layer issue is somewhere in the post-2nd-reset path
(after the rom_bank fix unblocked the trap):
- Supervisor at PC=$0AC0 wrote NEXTREG_57=$0F (slot-7 page)
- Bank-flip wrappers run through banks 0/1/2/3
- AUTOMAP cycles at $003a / $054c (rom_bank=3)
- 10 SLOT7-ENTER hits (PC=$FF00 with mmu_slot7=$01, but bytes are
  all $00 — the slot-7 mapping is empty)
- No screen writes to $4000-$57FF or $5800-$5AFF after 75 wall seconds
- Boot timestamps at end of log show emulated time advancing slowly

**Next-session priorities**:

1. Add P30 watcher for $4000-$5AFF (screen + attr) to confirm whether
   ANY screen-area writes happen post-2nd-reset.
2. The SLOT7-ENTER probe shows mmu_slot7=$01 but all bytes 0 — that
   physical RAM page is empty/unmapped. Investigate what should be at
   page $01 after 2nd reset (FAT32 helper code? sector buffer?).
3. Trace the supervisor's call graph at PC=$1805/$1808/$19C2 — these
   were the most active post-2nd-reset PCs. Decode what they're doing.
4. **ZEsarUX side-by-side comparison** would be the cleanest way
   forward — if their boot succeeds with the same SD image, capture
   their NR-write trace + SD-CMD trace and diff against ours.


## 2026-05-08 03:00 CEST — CMD12 persistent_response fix → boot reaches NextZXOS init

After the Mmu::reset fix landed (commit `1bdc7d5`), the supervisor's
NR 0x02 ← 0x01 soft reset works correctly post-`bank-3 $3BE8`, but boot
stalled in a tight SPI-poll loop at PC=$1972 in `enNextZX.rom` bank 2.

### Diagnosis (per CSpect-as-reference user directive)

Disassembled bank 2 `$196D-$1978`:
```
[196d] LD A,$4C            ; CMD12 STOP_TRANSMISSION
[196f] CALL $18D6          ; send CMD
[1972] IN A,($EB)          ; read SPI byte ← stuck here
[1974] AND A               ; test for zero
[1975] SCF
[1976] JR Z,$1972          ; if A == 0, loop back (still busy)
[1978] JR $1961            ; if non-zero, exit (busy phase ended)
```

Standard SD R1b post-busy poll: real SD pulls MISO low ($00) during the
BUSY phase after CMD12's R1 response, then releases the line back to
IDLE ($FF). Supervisor loops while reading $00 (busy) and exits when it
sees non-zero.

Bug found at `src/peripheral/sd_card.cpp:391` `cmd12_stop_transmission()`:
after queuing the response (8 stuff bytes + NCR + R1), the code set
`persistent_response_byte_ = r1` where `r1 = initialized_ ? 0x00 : 0x01`.
Post-MMC_Init, `initialized_=true` → R1=$00 → persistent=$00. Supervisor's
loop interpreted $00 as "still busy" forever.

### Fix

Changed `persistent_response_byte_ = r1` to `persistent_response_byte_ = 0xFF`
(idle, line released, no command active). Matches real SD spec: post-busy
MISO returns to idle ($FF). More correct than ZEsarUX's $01 (also non-zero
but technically wrong post-busy semantics).

`sdcard_test`: 15/15 PASS post-fix.

### Smoke results

After the CMD12 fix, with `--delayed-screenshot-frames 3000` (60 emulated
seconds):
- Supervisor reaches PC=$00EF (NextZXOS cold-start) ✓
- Reaches PC range $0100-$05F1 in bank 0 (deep into NextZXOS init) ✓
- 5056 OUT $EB bytes including 100× CMD17 (READ_SINGLE_BLOCK) reads — supervisor reading sectors from SD ✓
- Eventually reaches wait-for-key loop at PC=$1F40 in bank 3 (48K BASIC ROM area)
- 2nd soft reset (bank-3 $3BE8) is NO LONGER triggered (proper init flow doesn't fall into the soft-reset fallback) ✓

### Remaining issue

Screen still solid gray (no welcome menu rendered). Supervisor is at the
wait-for-key loop in bank 3 at PC=$1F3D HALT / $1F40 OR C / JR Z, but the
welcome menu hasn't been painted to ULA screen.

### CSpect comparison

CSpect "boot success" reference at
`/home/jorgegv/src/spectrum/jnext/doc/issues/cspect-captures/cspect-boot-success-nowelcome.png`
shows the rendered NextZXOS welcome menu (Browser / Command Line /
NextBASIC / Calculator / Guide / More... / 1792K) on light gray border.

Our jnext at the equivalent boot phase shows just the gray border with no
center menu rectangle. The RAM dumps in that directory (sysvars.raw,
screen.raw, slot7.raw, page30.raw) were captured at the boot-success
state and could be used as an oracle for what RAM should look like.

### Next-session priorities

1. **Trace welcome-menu drawing code** — find what writes to $4000-$5AFF
   in the NextZXOS supervisor. Add a wider P30 watcher covering screen +
   attr area to find when (and which PC) writes happen.
2. **Decode `$1F40` wait context** — the supervisor calls into bank 3
   (48K BASIC) for the wait-for-key. Trace what was on the stack pre-call,
   which bank-2 supervisor PC made the call, and what happens after the
   wait ends (timeout vs key press).
3. **CSpect dump comparison** — load `screen.raw` / `sysvars.raw` and
   compare against jnext's RAM at the equivalent boot phase. If divergent,
   identify which writes are missing.
4. **Maybe HALT timing issue** — supervisor uses HALT to wait for vblank.
   If our IM 1 INT isn't firing at 50Hz reliably, the supervisor stalls.
   Add IM 1 fire trace.

### Branch state

```
3659f74 doc(g46b): SD-preserve experiment reverted; next-session plan
1bdc7d5 fix(mmu): VHDL-faithful soft reset clears paging registers ← KEEPER
bc27887 doc(g46b): EOD-8 handover — TBBlue logo reached
```

Pending uncommitted (this session):
- src/peripheral/sd_card.cpp — CMD12 persistent_response fix (~6 lines)
- src/core/emulator.cpp — P20 log cap bump 5000→50000 (diagnostic only)


## 2026-05-08 08:30 CEST — IFF1=0 root cause confirmed via force-IFF1 hack

After commits `1bdc7d5` (Mmu::reset) + `779ef51` (CMD12 persistent) the
boot reached the NextZXOS supervisor's BASIC WAIT-KEY at bank-3 $1F40
but the welcome menu never rendered (screen solid gray = single color
RGB(182,182,182)).

### Switched to gui-release build (per user directive 2026-05-08)

The standard debug build was running ~1 frame per wall second when
supervisor was busy. gui-release runs at near-1:1 emulated:wall ratio,
making 7500-frame screenshot tests practical.

### CSpect captures used as oracle

Per user directive, switched from ZEsarUX to CSpect as the reference
emulator. The CSpect dumps in `doc/issues/cspect-captures/` were used:
- `nrdump.raw` — 256-byte NextRegs dump at boot success.
  - NR 0x69 = $00 → Layer 2 OFF, Tilemap OFF (welcome menu is ULA-based)
  - NR 0xC0 = $08 → IM 2 mode = 0 (legacy IM 1 mode), stackless_nmi = 1
  - NR 0x12/$13 = $09/$09 → Layer 2 page 9 (set even though L2 off)
  - NR 0x52..$57 = $0A,$0B,$04,$05,$00,$01 → standard MMU layout
- `screen.raw` — all-zero bitmap, attribute uniform $38 (white paper /
  black ink). Confirms welcome menu uses ULA, but capture moment is
  pre-text.
- `slot7.raw` — last 96 bytes contain font glyphs (8x8 chars L-U).

Screen color in jnext = RGB(182,182,182) = expanded RGB333 (5,5,5).
This isn't the default ULA white (6,6,6) → (219,219,219), suggesting
a specific palette write happens during boot. The user's CSpect
screenshot has matching gray border, so this is correct behaviour.

### Diagnostic chain (probes P47-P53)

1. **P47** (`Emulator::run_frame` ULA INT scheduler): logs
   `iff1` at every scheduled INT firing. Result: **iff1 = 0 across
   2200+ samples** spanning 60 emulated seconds.

2. **P48** (CPU `on_m1_cycle` hook for opcodes $FB / $F3): logs every
   EI / DI execution with PC + IFF1 before-execution. Result:
   - 200 entries in first ~2 wall seconds — supervisor's bank-flip
     wrapper at $5B00 is the hot path: `DI at $5B0A; ... ; EI at $5B1D`
     with IFF1 alternating 0 → 1 → 0 across each wrapper invocation.
   - Then **wrapper invocations stop**; supervisor enters RAM-resident
     code at PCs $6000-$BFFF (NextZXOS overlay loaded from SD via
     CMD17). That code has NO EI/DI of its own. IFF1 stays at last
     value (=0 after final DI).

3. **P49** (NMI vector $0066): 0 hits — NMI never fires.

4. **P50** (every 100k T-states with `int_pending=true`, log iff1):
   100% of 30+ samples showed iff1=0. The supervisor RAM-resident
   code spends all its time with IFF1=0.

5. **P51** (post-EI iff1 dump): confirms FUSE Z80 sets iff1=1 + iff2=1
   correctly after EI executes. So EI emulation is fine.

6. **P53** (track `fuse_z80_interrupt` accept attempts): **0 attempts
   in entire run**. The `if (z80.iff1)` branch in z80_cpu.cpp:435 is
   never entered because at every execute() entry where int_pending=
   true, iff1=0.

### IFF1 force-hack proof

Added `JNEXT_G46B_FORCE_IFF1` env-var: when set, forces `z80.iff1=1`
at every INT-pending check. Result:
- **PC=$0038 (IM 1 vector) IS REACHED** ✓
- **Screen renders COLORFUL CONTENT** (vs solid gray without hack) ✓
- iff1=1 throughout (per P47 with hack)

The rendered content is GARBLED (tile-like blocks of magenta/yellow/
cyan/black/red — looks like attribute or sprite data interpreted as
bitmap). This suggests there's a SECOND issue (font/page mapping)
beyond IFF1, but IFF1 was the primary blocker.

The env-var is kept guarded for future diagnosis. With it set, the
boot DOES progress past the wait-for-key — making the next layer of
issues debuggable.

### Diagnosis summary

The NextZXOS supervisor's RAM-resident overlay code (loaded from SD
via 100 CMD17 reads to mid-memory $6000-$BFFF) **doesn't execute EI**.
It expects to enter with IFF1=1 inherited from the bank-flip wrapper
caller. But by the time supervisor exits the wrapper-burst phase and
enters the overlay, the last wrapper invocation's caller may have
landed on a DI (within the wrapper's $5B0A path) → IFF1=0 → overlay
runs forever with IM 1 disabled.

This is timing-sensitive. CSpect somehow has the supervisor entering
the overlay with IFF1=1. Possible CSpect mechanisms:
- INT pulse held longer than jnext's 32 T-states → INT acks during
  EI'd window, IM 1 ISR at $0038 sets iff1=1 via EI before RET (per
  bank 3 $0038 IM 1 handler at $0051).
- Different scheduling that aligns vblank INT with EI'd window.

### Boot progression metrics with hack

- Frame 100: TBBlue logo only
- Frame 200: TBBlue logo + "Press SPACEBAR for menu" + "Press C for
  extra cores"
- Frame 300+: blank gray (without hack); colorful tiled content (with
  hack)
- Frame 7500 (= 150 emulated sec): same garbled tiles (with hack)

### Next-session priorities

1. **Investigate proper INT acknowledgment timing**. The current
   32-T-state pulse cutoff might be too tight for the wrapper hot
   path. Try widening (256, 1M) — but my early test with 1M didn't
   help, which suggests the issue is NOT pulse expiry but something
   else (maybe FUSE's `interrupts_enabled_at` always being in the
   future when iff1=1). Need deeper FUSE Z80 audit.

2. **Compare INT timing with FUSE / ZEsarUX / CSpect** Z80 emulators.
   Maybe our integration with FUSE has a bug where INT acknowledgment
   conditions are stricter than the rest of the world expects.

3. **With force-IFF1 hack enabled, debug the garbled rendering**.
   Possible causes:
   - Slot 7 page $0F font data not loaded (per P41 SLOT7-ENTER showing
     all-zero bytes at $FF00).
   - Wrong screen-area mapping (supervisor may write to RAM page that
     isn't displayed by ULA renderer).
   - Layer 2 enabled by hack-induced IM 1 ISR even though CSpect has
     L2 off.


## 2026-05-08 09:55 CEST — INT pulse extension experiment: NOT the cause

Confirmed via experiment: extending `INT_PULSE_TSTATES` to 100,000,000
(effectively never expires) does NOT fix the boot. Boot still:
- Solid gray screen
- P39 PC=$0038: 0 hits — IM 1 vector never reached
- P47 iff1=0 iff2=0 across 2200+ samples

So the issue is NOT that the INT pulse is too short for the wrapper
hot path. The fundamental problem is that **iff1 is 0 at every
execute() entry** when int_pending is true. The supervisor's RAM-
resident overlay code (loaded from SD into $6000-$BFFF) appears to
not have any DI/EI to keep iff1 toggling.

Reverted INT_PULSE_TSTATES back to 32 (original).

### What we know about iff1=0 source

Per P48 detailed tracking:
- DI/EI counts are LOW: ~200 total over 30+ wall seconds (vs hundreds
  per frame expected if wrapper hot loop were running constantly).
- Most P48 entries are in early boot wrapper bursts at $5B0A/$5B1D.
- After wrapper burst, supervisor runs ~5 wall seconds of code at
  $1BCB/$1BCD (BASIC tokenizer) with NO DI/EI executed.

Per P54 (RETI/RETN tracking): 0 hits — those don't clear iff1 either.

### Mystery: how does iff1 stay 0?

Per Z80 spec, iff1 only changes via:
- DI ($F3): iff1=0
- EI ($FB): iff1=1 (delayed)
- INT ack: iff1=0 (and FUSE saves iff2)
- NMI: iff2=iff1, iff1=0
- RETN ($ED $45): iff1=iff2
- RETI ($ED $4D): same as RETN per FUSE
- Reset: iff1=0

We see no DI in the post-wrapper phase, no INT acks (P39=0), no NMI
(P49=0), no RETI/RETN (P54=0), no resets. Yet iff1=0 in P47 and the
hack proved iff1=0 at every check.

Per P51 (post-EI iff1 dump): immediately after EI, iff1=1, iff2=1.
So FUSE's EI logic works correctly.

**Hypothesis**: maybe a CPU instruction we're not catching clears
iff1. Or maybe there's a reset-like event that clears it without
triggering our log probes.

### Workaround in place

`JNEXT_G46B_FORCE_IFF1=1` env-var lets boot progress past this
obstruction (proves IFF1=0 is the blocker). Garbled tile-pattern
rendering with hack indicates SECOND issue (likely slot-7 font /
page mapping).

### Next-session priorities (revised)

1. **Find what actually clears iff1**. Add a probe that fires
   whenever `z80.iff1` transitions from 1 to 0 (without going through
   our DI/RETI/RETN tracking). Maybe there's an undocumented FUSE Z80
   path or an interaction with NMI that doesn't show up in M1 hook.

2. **CSpect side-by-side**. Run CSpect (if available locally) on the
   same SD image. Capture its RAM at the moment of stall and compare
   memory state against jnext.

3. **Fix garbled rendering** (with hack enabled). The tile pattern
   suggests bitmap data is wrong. Slot-7 page $0F has all-zero bytes
   per P41 SLOT7-ENTER probe — needs to contain font data.



## 2026-05-08 18:15 CEST — ROOT CAUSE FOUND: clock_ reset mid-frame loop

**THE ACTUAL BUG.** P55 transition probe + P56/P57 statistics traced
the stuck IFF1=0 to a much deeper bug: `Emulator::soft_reset()` →
`init(preserve_memory=true)` was unconditionally calling
`clock_.reset()`, `scheduler_.reset()`, and `frame_cycle_=0` —
**including when called mid-frame from the supervisor's NR 0x02 = 0x01
soft-reset NextReg write handler**.

### Evidence chain

P55 (iff1 1→0 transition probe): only 200 hits in entire run, all at
the supervisor's bank-flip wrapper $5B0A (DI). After ~10M tstates,
hit#202/#203 at PC=$0000 (DI; JP $006A — BASIC reset vector). No
post-wrapper transitions. iff1 stayed 0 forever.

P56 (int-pending stats): `pend=1248` and `pulse_exp=271` FROZEN,
`request_at=2346` FROZEN, despite tstates climbing past 150M.
Translation: 271 INT pulses fired in the first ~2M tstates (28 frames),
then NEVER AGAIN. INT scheduler stopped firing.

P57 (run_frame entry probe): `pre_reset_tstates ≈ 567264` per frame
entry — **frames are progressing normally and tstates IS being reset
each frame**. Contradicting P55's tstates=10M values.

P58 (frame loop inner): caught the smoking gun — at iter#10000000 of
ONE run_frame call, `clock=124M frame_end=153M tstates=124M`. **A
single run_frame() call ran for 124M tstates without exiting.** The
loop's `while (clock_.get() < frame_end)` was running with clock_
freshly reset to 0 but `frame_end` still set to the pre-reset value
(~113M). Loop spun for ~28M iterations (rate-limited by clock advance
per instruction) before clock_ caught up to the stale frame_end.

### Root cause

`Emulator::init(config, preserve_memory)` at `src/core/emulator.cpp:58`
was calling `clock_.reset()` unconditionally. Per VHDL, the master PLL
clock is free-running and is NOT in any reset domain — `i_RESET` /
`reset_soft` only reset FF-based subsystems. Resetting `cycle_=0`
mid-frame (while run_frame()'s local `frame_end` was already set)
caused:

1. The current run_frame() to spin for ~28M iterations.
2. The scheduler queue to be cleared, dropping the supervisor's
   pending ULA INT.
3. No new INTs scheduled until the (extremely long) current frame
   exited.
4. iff1=0 remained because EI never fired in ISRs that never ran.

### Fix (commit pending)

`src/core/emulator.cpp:58-83` — gate `clock_.reset()`,
`scheduler_.reset()`, `frame_cycle_=0`, `frame_num_=0` on
`!preserve_memory`. Preserves PLL clock continuity across soft reset.

### Verification

After the fix:
- mmu_test 164/186 PASS / 0 FAIL (unchanged baseline).
- sdcard_test 15/15 PASS (unchanged baseline).
- TBBlue logo + "Press SPACEBAR" still renders correctly (frame 100/200).
- P55 now shows actual INT acceptances at PC=$0038 (IM 1 vector) post-supervisor:
  e.g. hit#0 prev_pc=$1F44 → now_pc=$0038 (INT acked from BASIC wait loop)
  hit#199 prev_pc=$5693 → now_pc=$0038 (INT acked from supervisor).
- Multiple wrapper bursts (200+ DI/EI cycles) followed by INT acks →
  followed by another wrapper burst → INT ack — supervisor is now
  cycling through its normal IM 1 service path.
- Post-fix, the screen still shows solid gray at frame 1500, indicating
  there's a SECOND issue past this (welcome menu drawing). But IFF1=0
  / INT scheduler stall is RESOLVED.

## 2026-05-07 22:00 CEST — SD re-init storm FIXED: soft-reset preserves SD/Multiface state

**Follow-up to EOD-12.** EOD-12 gated `clock_/scheduler_/frame_cycle_`
on `!preserve_memory`, but the same audit missed three sibling resets
that fire on every NR 0x02 ← 0x01 soft reset and were the actual cause
of the post-EOD-12 SD re-init storm:

1. `sd_card_.reset()` (`src/core/emulator.cpp:146`) — clears
   `initialized_=false`, dropping the SD card's post-CMD0/ACMD41/CMD58
   handshake state.
2. `sd_card_.mount(...)` (`src/core/emulator.cpp:3889-3896`) — re-opens
   the SD image, which itself runs the `mount()`-side reset and zaps
   `initialized_` again.
3. `multiface_.reset(/*hard=*/true)` (`src/core/emulator.cpp:132`) —
   passes `hard=true` unconditionally, wiping MF SRAM on every soft
   reset. Should be `hard=!preserve_memory`.

Bonus VHDL-faithfulness fix: the DivMMC + Multiface SD-extraction block
(`src/core/emulator.cpp:3707-3748`) was also unguarded; flash-baked ROMs
survive soft reset on real hardware.

### Diagnostic chain (three parallel agents)

- **Agent A (baseline)** built gui-release on the EOD-12 worktree, ran
  the canonical 30 s headless smoke test with `--log-level sdcard=trace`,
  confirmed the 102×CMD0 / 116×CMD12 / 181×CMD18 EOD-12 numbers, then
  noticed the smoking gun in the log:

  ```
  [emulator] Soft reset triggered via NextREG 0x02 (0x01) PC=0x6d31 ...
  [sdcard]  SD image unmounted
  [sdcard]  mounted SD image: …
  [divmmc]  loaded ROM from byte buffer (8192 bytes)
  [multiface] loaded Multiface ROM from byte buffer (8192 bytes)
  ```

  i.e. the soft reset was tearing down and rebuilding all the
  flash-equivalent state. Pinpointed `emulator.cpp:3890-3896` as the
  unguarded mount call.

- **Agent B (bank-2 RE)** disassembled enNextZX.rom bank 2 at
  `$1700-$1FFF` with `z88dk-dis -mz80n`. Mapped:
  - `$19DD-$1A66` = full SD-init sequence (CMD0, CMD8, ACMD41, CMD58,
    CMD16) — invoked from any time the supervisor's IDE driver decides
    the card needs re-init.
  - `$1925-$1932` = `spi_wait_idle_FF` busy poll.
  - `$196D-$1979` = CMD12 STOP_TRANSMISSION emitter.
  - `$1A8E-$1AEF` = `ide_init_drive` — the high-level entry that calls
    init then enumerates 16 partition probes × 2 passes via
    hookcode `RST 8 / .DB $00 $8A` (= IDE_BANK system call).
  - Magic constants: CMD8 R7 echo expects `$01 $AA`; data-block
    start token expects `$FE`; init result returned in `A`.

- **Agent C (PC probe)** added `JNEXT_G46B_P59` env-gated probe (callback
  pattern) that logs Z80 PC at every CMD0/CMD12/CMD17/CMD18 entry.
  Branch `g46b-sd-probe` (commit `c1dcaa7`, not yet merged into
  `g46b-investigation`). Probe identified:
  - `$059B` = DivMMC firmware (boot-time, one-shot).
  - `$7876` = NextZXOS bank-loaded code (initial reads, gives up).
  - `$18FB` = **the loop driver**, exclusively issuing CMD12→CMD0
    pairs in the post-handoff storm. Sits 42 bytes before the known
    bank-2 SD-init cluster `$1925`/`$196D`/`$1972`.

  All three agents converged on the same root cause without coordination.

### Fix (commit pending on g46b-investigation)

```cpp
// src/core/emulator.cpp:131-138
divmmc_.reset();
multiface_.reset(/*hard=*/!preserve_memory);   // was: /*hard=*/true

// src/core/emulator.cpp:146-156
if (!preserve_memory) {
    sd_card_.reset();
}

// src/core/emulator.cpp:3707
if (!preserve_memory && !cfg.sd_card_image.empty()) {  // gate DivMMC/MF SD-extract

// src/core/emulator.cpp:3889-3896
if (!preserve_memory && !cfg.sd_card_image.empty()) {  // gate sd_card_.mount
    if (sd_card_.mount(...)) { ... }
}
```

### Verification (post-fix, 75 s wall headless smoke @ frame 3000)

| counter | EOD-12 baseline | post-fix | delta |
|---|---|---|---|
| CMD0 GO_IDLE | 102 | 66 | -36 (-35%) |
| CMD8 SEND_IF_COND | 102 | 66 | -36 |
| CMD55 + ACMD41 | 102 | 65 | -37 |
| CMD58 READ_OCR | 102 | 65 | -37 |
| CMD9 SEND_CSD | 100 | 63 | -37 |
| CMD12 STOP_TRANSMISSION | 116 | 80 | -36 |
| CMD17 READ_SINGLE_BLOCK | 100 | 100 | 0 |
| CMD18 READ_MULTIPLE_BLOCK | 13 | 13 | 0 |
| **SD unmount events** | (≥1) | **0** | **eliminated** |
| **SD remount events** | (≥1) | **0** | **eliminated** |
| Soft resets seen | 1 | 2 | +1 (NEW) |

The supervisor now triggers a **second** soft reset at PC=$3BF5 with
rom_bank=$03 (BASIC ROM region) — visible only after the fix because
the 1st-reset SD storm previously prevented the supervisor from getting
that far. After the 2nd reset the SD activity stops entirely, suggesting
the supervisor has handed off (or attempted to hand off) to BASIC.

### Tests baseline (post-fix)

- `mmu_test`: 186 total / 164 PASS / 0 FAIL / 22 SKIP — unchanged.
- `sdcard_test`: 15 / 15 / 0 / 0 — unchanged.

### Visible state at frame 3000 (≈ 60 s emulated)

Screen still solid gray with a few stray pixels (3-4 single-pixel
black marks, 1 short horizontal line). Same as frame 1500 — supervisor
is no longer painting the screen. NOT a stuck SD loop, but NOT a
welcome menu either. **Third blocker is now in scope:** what stops the
supervisor from drawing the welcome menu after the 2nd soft reset.

### Next-session priorities

1. **3rd blocker investigation**: trace what happens between the 2nd
   soft reset (PC=$3BF5 rom_bank=$03) and the silent gray screen at
   f3000. Specifically:
   - Is the supervisor in an HALT loop? IM 1 ack loop? infinite tight
     loop? PC-trace gating on rom_bank and post-2nd-reset cycle range.
   - Is the screen-paint code (which writes to physical pages 0x0A/0x0B
     for ULA + 0x?? for L2) being called at all? Add a write-watcher
     on screen RAM ($4000-$5AFF in classic ULA mode) and log per-frame
     write counts.
   - Why does the supervisor issue a SECOND soft reset at PC=$3BF5?
     What is the bank-3 code at that PC? (Bank 3 of enNextZX.rom = 48K
     BASIC ROM, so this might be the supervisor jumping to a BASIC
     subroutine that itself trips a soft reset trap.)

2. **Verify the 33% CMD count reduction is acceptable**: the remaining
   65 CMD0 cycles aren't a loop — they're spread across the supervisor's
   normal IDE-driver re-init flow (each soft reset triggers one full
   re-init via `$19DD`, plus partition-table enumeration via
   `$1A8E::ide_init_drive`'s 16 × 2 partition scan). This is supervisor
   software behaviour, not emulator misbehaviour.

3. **Optional cleanup**: merge the JNEXT_G46B_P59 PC-probe from
   `g46b-sd-probe` into the investigation branch as a long-lived
   diagnostic.

## 2026-05-07 22:15 CEST — Post-2nd-reset 3-PC loop decoded; supervisor in bank 1 parser/wait-for-INT

After the SD/MF preserve-on-soft-reset fix lands at `aa53107` (and the
P59 / P60 / P61 probes at `2b3a9e9` / `4508cf8` / `18e2a1c`), the
supervisor's post-2nd-reset behaviour becomes traceable.

### P60 sampler results (2nd soft reset → frame 3000)

800 samples collected (P60 every 200 000 instructions ≈ every 6 ms
emulated). Distribution dominated by 3 PCs:

| pc | rom_bank | mmu7 | iff1 | im | halted | samples | % |
|---|---|---|---|---|---|---|---|
| `$0060` | 0x01 | 0x01 | 1 | 1 | 0 | 268 | 33.5 % |
| `$1FFE` | 0x01 | 0x01 | 1 | 1 | 0 | 267 | 33.4 % |
| `$1FFF` | 0x01 | 0x01 | 1 | 1 | 0 | 258 | 32.3 % |
| (other) | (transient) | — | — | — | — | 7 | 0.9 % |

NOT halted, IM 1 enabled, INTs being acked. **rom_bank=$01 sustained
— bank 1 of enNextZX.rom is paged in for the entire post-2nd-reset
window.** Per memory `project_g46b_2026_05_06_eod6_bank_topology_decoded.md`
the supervisor previously NEVER reached rom_bank=$01 sustained execution.
**The fix unlocks bank-1 supervisor execution for the first time.**

### Decode of the 3 stuck PCs (bank 1 of enNextZX.rom)

PC=`$0060` — file offset `0x4060`:

```
$0060: E1   POP HL          ; standard IM 1 ISR exit
$0061: F1   POP AF
$0062: FB   EI
$0063: C9   RET              ; return from ISR
```

The IM 1 ISR entry chain (PC=$0038 → $0046):

```
$0038: F5            PUSH AF
$0039: E5            PUSH HL
$003A: 26 00         LD H,$00
$003C: 3E 80         LD A,$80
$003E: C3 46 00      JP $0046    ; trampoline to ISR body
$0046: D3 E3         OUT ($E3),A ; A=$80 → DivMMC config: MAPRAM=1,
                                  ; CONMEM=0, bank=0
... (ISR body, eventually falls through to $0060 exit)
```

PC=`$1FFE`/`$1FFF` — file offset `0x5FFE`/`0x5FFF`. Surrounding
disassembly:

```
$1FF0: 2C            INC L
$1FF1: 28 0B         JR Z,$1FFE          ; if L wraps to 0
$1FF3: CD 9B 1F      CALL $1F9B
$1FF6: 30 03         JR NC,$1FFB         ; if NC, skip RST
$1FF8: E7            RST $20
$1FF9: 18 03         JR  $1FFE           ; unconditional fall-through
$1FFB: CD 2D 0E      CALL $0E2D
$1FFE: D1            POP DE              ; ← P60 hit (267×)
$1FFF: FE 2C         CP  $2C             ; ← P60 hit (258×); $2C = ',' ASCII
$2001: C0            RET NZ              ; not a comma → return to caller
$2002: E7            RST $20             ; comma path → RST $20
```

**This is a parser routine.** `CP $2C` (compare with `,` ASCII) +
`RET NZ` is the canonical "is the current char a comma?" check used
in argument-list / config-file parsers. The supervisor is iterating
over some text input — likely the loaded config / menu definition —
and dispatching on commas via RST $20 (which is bank 0's hookcode
sub-dispatch per EOD-9).

### P61 screen-write results

Frame-by-frame writes to physical SRAM pages 0x0A/0x0B (classic ULA
RAM = `$4000-$5AFF` + bank-5 high half) and 0x2A/0x2B (alt views):

| frame | classic ULA writes | alt-view writes | comment |
|---|---|---|---|
| 287 | … | … | 2nd soft reset |
| 290 | **16388** | 0 | Full bank-5 wipe — supervisor LDIR-clears |
| 294 | 312 | 19 | Attribute work begins |
| 295 | 1430 | 0 |  |
| 296 | 1509 | 0 |  |
| 297 | 1509 | 0 | Heavy attribute writes (~5500 over 5 frames) |
| 298 | 1146 | 0 |  |
| 305 | 14215 | 0 | Second clear / re-paint |
| 306 | 78 | 0 |  |
| 307 | 2 | 0 |  |
| **308 onwards** | **0** | **0** | **Silent for 2664+ frames** |

**Translation**: the supervisor cleared all of bank 5 ($4000-$7FFF =
16384 bytes + a 4-byte tail), then wrote ~5500 attribute / pixel
bytes across frames 294-298, then did another large-clear at frame
305, and then went silent. It IS drawing — just to attributes that
turn out invisible (probably paper=ink or paper=7 ink=7 → no contrast)
in the rendered output. The frame 1500 / 3000 screenshots show solid
mid-gray with 4-5 isolated stray pixels: consistent with a screen
paint that uses paper=7 ink=7 (or similar uniform attributes) for
99 % of cells, with only a handful of cells diverging — likely
position cursors / borders.

### Synthesis — what the supervisor is doing

1. **Frame 287**: 2nd soft reset (PC=$3BF5, rom_bank=$03 BASIC clean-
   reboot trampoline) — supervisor's deliberate path to re-enter
   the firmware-handoff state. The trampoline is at bank-3 $0000:
   `DI; XOR A; LD BC,$243B; JP $3BE8` → `LD A,$02; OUT (C),A; INC B;
   IN A,(C); AND $80; OR $01; OUT (C),A` (NR 0x02 ← bit-7-preserved |
   $01 = soft reset, bit 7 = block hard-reset).
2. **Frame 290**: post-reset, rom_bank flips to $01 (= bank 1, the
   supervisor's "main" code per old EOD-6 hypothesis). Supervisor
   issues a 16384-byte LDIR clear of bank-5 RAM ($4000-$7FFF).
3. **Frames 294-307**: supervisor draws something — ~5500 attribute
   writes + a 14215-write second clear. Probably the welcome menu
   layout. With wrong attributes the result renders as featureless
   gray.
4. **Frame 308+**: supervisor enters its idle wait-for-input loop
   at parser tail $1FFE/$1FFF. Each ULA INT (50 Hz) is acked through
   the IM 1 ISR ($0038 → $0046 → … → $0060 exit). Between INTs the
   loop runs through the parser body. **No further screen writes,
   no further SD activity.**

### Open questions for next session

1. **What is `RST $20` in bank 1?** `$0020` is conventionally the
   `KEY_INT` test address in 48K BASIC, but bank 1 ≠ bank 3 here.
   Bank-1 `RST $20` (`$0020`) decode is needed to understand the
   parser's branch behaviour (commas dispatch via this RST).
2. **What is at `$0E2D` and `$1F9B`?** Both are CALLed in the
   parser. `$0E2D` is presumably a per-token handler; `$1F9B` is
   the parser core or string-equality test. Decoding either should
   reveal the parser's data source — config.ini? menu.def? hard-coded
   string?
3. **Is the menu actually being drawn but invisible?** Quick test:
   dump physical SRAM page 0x0A bytes `$1800-$1AFF` (= attribute area
   `$5800-$5AFF`) at frame 1000. If most attributes are `$77` (bright
   white on white) or similar, that confirms the "drawn but invisible"
   theory and the bug is in attribute selection. Sample with a
   per-frame snapshot via ram_.page_ptr(0x0A) + std::memcpy + a probe
   that dumps every Nth frame.
4. **Why does the parser loop look idle?** If the supervisor is
   waiting for keyboard input, our headless mode never delivers any.
   Per `feedback_g46b_no_keypress.md` the user has never needed a
   keypress historically; either the loop is genuinely stuck OR
   it's polling something other than keyboard (e.g. a real-time
   clock, or a queue populated by another routine).
5. **Compare with CSpect**: dump CSpect's PC at the equivalent point
   in its boot. If CSpect spends time at the same PCs $1FFE/$1FFF,
   the loop is normal NextZXOS behaviour; if not, jnext has a stuck
   loop CSpect doesn't.

### Branch state at session end

`g46b-investigation` HEAD = `18e2a1c`. Commits added this session:

```
18e2a1c diag(g46b): JNEXT_G46B_P61 probe — count screen-bank writes per frame
4508cf8 diag(g46b): JNEXT_G46B_P60 probe — sample CPU PC every 200k instructions
2b3a9e9 diag(g46b): JNEXT_G46B_P59 probe — log CPU PC at SD CMD0/12/17/18 entry
aa53107 fix(g46b): preserve SD/Multiface state across soft reset
463cd0e fix(g46b): preserve clock_/scheduler_/frame_cycle_ on soft reset (EOD-12, prior)
```

Tests baseline (post-fix, post-probes):
- `mmu_test`: 186 / 164 PASS / 0 FAIL / 22 SKIP — unchanged.
- `sdcard_test`: 15 / 15 PASS / 0 FAIL / 0 SKIP — unchanged.

Worktrees at session end:
- `.claude/worktrees/sd-probe` — branch `g46b-sd-probe`, has same
  commits as `g46b-investigation`. Can be removed.
- `.claude/worktrees/sd-baseline` — detached HEAD on the EOD-12
  state, no commits made there. Can be removed.

## 2026-05-07 22:40 CEST — BREAKTHROUGH: supervisor running user's leftover dump.bas; BASIC sysvars corrupted

Two parallel agents (P62 attribute dumper + bank-1 parser RE) +
inline P62 extension to dump BASIC sysvars + CH_ADD-pointed memory.
Three CONFIRMED findings overturn the EOD-14 hypothesis:

### Finding 1 — Welcome menu was NEVER drawn (P62 attribute dump REFUTES "drawn but invisible")

P62 attribute histogram at frames 305, 1000, 2000 — IDENTICAL output:
```
G46B P62 frame#1000 attr_hist (top20): $38:768
```
Every single one of the 768 ULA attribute cells is `$38` — the
**standard BASIC default** (paper=7 white, ink=0 black). NOT a
"paper=ink invisible" pattern; black-on-white text would render
PERFECTLY visibly if any pixel data existed.

Pixel data: 5 / 6144 non-zero bytes (0.08%). Border = `$07` (white,
non-bright = mid-gray rendered).

**The "menu" is simply not drawn.** The 22000+ writes from frames
290-307 went to non-screen RAM (BASIC PROG/sysvars/workspace area
above $5C00, NOT the screen at $4000-$5AFF).

### Finding 2 — Bank-1 parser is BASIC's INPUT statement (Agent B RE)

`$1FF0-$2003` is the canonical 48K BASIC INPUT-statement parameter
parser. Single caller in entire 64K ROM: bank-1 `$1AC4`. Parser
reads chars via `RST $20` which is bank-1 `$0020` = standard
NEXT-CHAR routine reading from `CH_ADD ($5C5D)`. `$0E2D` is a
long-call wrapper to bank-2 `$24FB` (expression evaluator).
`$1F9B` is the variable-name / binary-literal classifier.

The supervisor enters this parser via the canonical 48K BASIC
INPUT/LET runtime in bank 1 — same code structure as the
real BASIC ROM, just with bank-flip wrappers on RSTs.

### Finding 3 — PROG contains user's leftover /dump.bas (BYTE-FOR-BYTE)

Extended P62 dumps PROG ($5CCB, 128 bytes):
```
00 0A 30 00 EA 20 47 34 36 28 62 29 20 2D 20 4E 52 20
72 65 67 69 73 74 65 72 73 20 2B 20 73 79 73 76 61 72
73 20 2B 20 73 6C 6F 74 20 37 20 64 75 6D 70 0D ...
```
Decoded: line 10, length 48, REM `G46(b) - NR registers + sysvars
+ slot 7 dump`.

`mtools` extract of `roms/nextzxos-1gb-fat32fix.img`:
```
mcopy -i ...@@32256 -n ::/dump.bas /tmp/dump.bas
xxd -s 0x80 -l 80 /tmp/dump.bas
00000080: 000a 3000 ea20 4734 3628 6229 202d 204e
00000090: 5220 7265 6769 7374 6572 7320 2b20 7379
000000a0: 7376 6172 7320 2b20 736c 6f74 2037 2064
000000b0: 756d 700d ...
```

**EXACT byte-for-byte match** with PROG=$5CCB content (after the 128-
byte PLUS3DOS header is stripped). The supervisor loaded the user's
diagnostic dump program, originally created during the EOD-3..EOD-6
CSpect-capture sessions. The file is at `/dump.bas` on the SD card
root with mtime 2026-05-05 14:58.

### Finding 4 — BASIC sysvars are partially corrupt (STKEND<STKBOT)

P62 sysvars at frame 1000:
```
FLAGS=$DC PROG=$5CCB E_LINE=$6055 K_CUR=$6055 CH_ADD=$5F42
WORKSP=$607A STKBOT=$608B STKEND=$5E06
```

`FLAGS=$DC` (1101 1100): bit 7=1 → BASIC is in **running mode** (vs
syntax-check), bit 6=1, bit 4=1, bit 3=1, bit 2=1.

**`STKEND=$5E06 < STKBOT=$608B` is INVALID** — the calculator stack
must grow upward from STKBOT, so STKEND >= STKBOT in any consistent
BASIC state. This is partial sysvar corruption.

CH_ADD=$5F42 reads ALL ZEROS for 64 bytes (and 16 bytes before).
Parser is consuming `$00` characters which fail `CP $2C` (comma)
and fail `CP $0D` (end-of-line) and fail every other token test —
the parser loops forever without a terminator.

PROG=$5CCB has only 4.4% non-zero bytes in the first 4096 — the user's
1-line REM program (~52 bytes) plus a few stragglers, then mostly
zero. The "tokenised program" the parser SHOULD be parsing was
probably stored briefly in CH_ADD's vicinity but is now cleared.

### Synthesis — what the supervisor is doing

1. **Boot path** (after our SD/MF preserve fix): supervisor handles
   TBBLUE.FW handoff → 1st soft reset (PC=$6D31) → resumes in bank 0
   → 2nd soft reset (PC=$3BF5, bank-3 NextZXOS clean-reboot
   trampoline at $3BE8) → resumes in `rom_bank=$01` (bank 1).
2. **Bank-1 entry**: supervisor jumps into BASIC startup. Standard
   BASIC clears bank-5 RAM ($4000-$7FFF, 16384-byte LDIR observed at
   frame 290), initialises sysvars (partially — STKEND/STKBOT
   inversion suggests bug here), and probably loads a default startup
   BASIC program.
3. **Loads `/dump.bas`** — **mechanism unclear**. NextZXOS doesn't
   have a documented root-level autoexec. `/nextzxos/booter.bas`
   exists but contains different bytes. Possibilities:
   - NextZXOS has an undocumented "load-last-saved-program" feature
     that reads a sysvar pointer from the SD into RAM.
   - The user's dump.bas is being picked up because of a coincidental
     filename match (e.g. NextZXOS searches for `BOOT.BAS` and finds
     `dump.bas` via some glob matcher). Unlikely.
   - The dump.bas content is NOT loaded by NextZXOS at all — instead
     it's a residual ARTIFACT of a CSpect-saved RAM image somehow
     replayed by jnext. (Test: temporarily move `/dump.bas` aside
     and re-run — does BASIC PROG end up empty?)
4. **BASIC `RUN` invoked** — sets FLAGS bit 7, calls into bank-1
   INPUT statement runtime at $1A0E (or via long-call from bank 2).
5. **INPUT parser hangs** at $1FFE/$1FFF reading $00 from CH_ADD —
   never finds a comma, never finds end-of-line, infinite loop.

### Root-cause hypothesis (for next session)

The most likely chain:

(a) **NextZXOS' standard BASIC startup** (whether or not it loads
dump.bas) initialises sysvars to a state we don't fully replicate —
specifically the STKBOT/STKEND fields. In jnext the inversion
STKEND<STKBOT means our memory layout / Mmu paging puts the BASIC
workspace in a different physical bank than CSpect's, OR the
supervisor's sysvar-init code skips a step that depends on a register
value we haven't matched.

(b) **CH_ADD pointing to zero RAM** is a SYMPTOM: with the corrupt
STKEND/STKBOT bracket, BASIC's "next-char-pointer" calculation went
wrong and points into unused memory.

(c) The fix is most likely upstream — find the supervisor code that
sets STKEND/STKBOT and figure out why our state diverges.

### Frames where supervisor wrote BASIC PROG content

P61 caught 22000+ writes to bank-5 RAM (pages 0x0A + 0x0B) at frames
290-307. Most of those went to PROG/sysvars/E_LINE — not to the
screen. The supervisor IS doing significant memory work, just not
painting glyphs.

### Next-session priorities (ordered)

1. **Test load-of-dump.bas hypothesis (5 min)**: rename
   `roms/nextzxos-1gb-fat32fix.img` aside, mount via mtools, rename
   `/dump.bas` to `/dump.bak` in a copy, mount the copy as SD,
   re-run with P62, observe whether PROG=$5CCB is now empty. If yes:
   confirms NextZXOS deliberately loads dump.bas. If no: PROG=
   $5CCB content has a different origin and we need to find it.

2. **Find STKEND/STKBOT init code (M)**: search bank 0/1/2 of
   enNextZX.rom for `LD ($5C65),HL` (STKEND init = `22 65 5C`) and
   `LD ($5C63),HL` (STKBOT init). Decode the routine that sets these.
   Compare with what CSpect runs (if a trace is available).

3. **Decode bank-2 $24FB (long-call target from $0E2D) (S)**: this
   is the expression evaluator the parser invokes. If it's stuck
   in an infinite recursion or expression-stack underflow, that's
   another angle.

4. **CSpect side-by-side trace (L)**: compare CSpect's PC trajectory
   in the same SD-image boot. If CSpect ALSO ends up in bank-1
   $1FFE/$1FFF, the loop is normal idle behaviour and the welcome
   menu is drawn LATER (and we have a different bug). If CSpect
   doesn't, jnext has a stuck loop.

5. **Cleanup TEMP env-var probes once G46(b) closes** (P55..P62,
   FORCE_IFF1, NR_CSPECT, INJECT, PATCH, WATCH).

### Branch state

`g46b-investigation` HEAD = `e4ad286` (P62 extension committed
2026-05-07 22:40). Commits this session beyond EOD-14:

```
e4ad286 diag(g46b): extend JNEXT_G46B_P62 probe — dump BASIC sysvars + PROG/CH_ADD/E_LINE/WORKSP
a0f15fb diag(g46b): JNEXT_G46B_P62 probe — dump ULA attribute + pixel state at key frames
3ab98a2 doc(g46b): post-2nd-reset 3-PC loop decoded
18e2a1c diag(g46b): JNEXT_G46B_P61 probe — count screen-bank writes per frame
4508cf8 diag(g46b): JNEXT_G46B_P60 probe — sample CPU PC every 200k instructions
2b3a9e9 diag(g46b): JNEXT_G46B_P59 probe — log CPU PC at SD CMD0/12/17/18 entry
aa53107 fix(g46b): preserve SD/Multiface state across soft reset
```

Tests baseline (post-fix, post-probes): mmu_test 186/164/0/22,
sdcard_test 15/15/0/0 — both unchanged from EOD-12.

## 2026-05-07 22:55 CEST — EOD-14c: P42 band-aid REMOVED, real bug exposed

### Root cause of the EOD-14b "running user's leftover dump.bas" finding

The PROG=$5CCB content was NOT loaded by NextZXOS from /dump.bas
(verified by renaming /dump.bas to /DUMP.BAK on a copy of the SD
image — the bytes still appeared in PROG identically). Instead, it
came from a leftover **TEMP DIAGNOSTIC BAND-AID** at
`src/cpu/z80_cpu.cpp:1417-1477`:

```cpp
// G46(b) Probe 42 (TEMP, 2026-05-09): COMPREHENSIVE DIAGNOSTIC BAND-AID
// — at EVERY PC=$5B20 entry (bank-flip wrapper RET), reload CSpect's
// sysvars + screen + slot7 captures. ... Capped at 200 hits to bound
// runtime work.
```

This was introduced in EOD-5 (2026-05-09) as a hypothesis test and
**never removed**. It was NOT gated on `--bypass-tbblue-fw` — fired
unconditionally on every `pc == 0x5B20` until 200 hits.

The band-aid's `sysvars.raw` byte 0x1CB onward contains the tokenised
"G46(b) - NR registers..." REM line — captured from CSpect at a moment
when the user had `dump.bas` loaded as the BASIC program. Every time
jnext hit $5B20 (bank-flip wrapper RET in slot-7 RAM at $5B00..$5B20),
the band-aid pasted those bytes into RAM at $5B00..$5CFF. The
supervisor then "ran" the captured BASIC state.

Per `[feedback_vhdl_faithful_only.md]` STRICT 2026-05-04 — band-aids
must be removed in favour of VHDL-faithful fixes. **Removed in commit
`8c198fe` (2026-05-07 22:55).**

### Post-removal behaviour

P62 dump at frame 305 (post-band-aid-removal, fresh boot, no bypass):

```
attr_hist (top20): $00:736 $5B:4 $01:2 $67:2 $32:2 $5C:2 ...
attr_row[00]: 00 00 00 00 ... (all rows 0-22 = $00)
attr_row[23]: F5 C5 01 FD 7F 3A 5C 5B EE 10 F3 32 5C 5B ED 79
              01 FD 1F 3A 67 5B EE 04 32 67 5B ED 79 FB C1 F1
pixel_nonzero=0/6144 (0.00%)
border=$07
sysvars: FLAGS=$00 PROG=$0000 E_LINE=$0000 K_CUR=$0000 CH_ADD=$0000
         WORKSP=$0000 STKBOT=$0000 STKEND=$0000
PROG[$0000..+4095] nonzero=3891/4096 (95.0%)
```

Critical observations:

- **Screen is BLACK** (attribute=$00 = black paper, black ink, no
  bright = pure black). Border $07 (white) renders as mid-gray. So
  the screenshot has BLACK SCREEN AREA + GRAY BORDER — distinct from
  the previous all-gray-with-stray-pixels.
- **Sysvars are all zeros** (FLAGS=$00, PROG=$0000, etc.) — supervisor
  never initialised BASIC sysvars. Without the band-aid forcing them,
  no sysvar init code path is reached.
- **Attribute row 23 (last row, $5AE0..$5AFF) contains executable
  Z80 code** — decodes as the bank-flip wrapper:
  ```
  PUSH AF; PUSH BC; LD BC,$7FFD; LD A,($5B5C); XOR $10; DI;
  LD ($5B5C),A; OUT (C),A; LD BC,$1FFD; LD A,($5B67); XOR $04;
  LD ($5B67),A; OUT (C),A; EI; POP BC; POP AF
  ```
  This is canonically at $5B00 (per memory `project_g46b_2026_05_06_p29_p30_breakthrough.md`).
  Why is it appearing 32 bytes earlier at $5AE0? Possible
  off-by-one in the LDIR target the supervisor uses, or the bytes
  are at $5B00 in a different physical bank we're not viewing.
- **Boot stuck in NR-init loop**: last 20 log lines show repeated
  ```
  CPU speed changed to 28 MHz (NextREG 0x07=0x03)
  NextREG 0x03 ← 0xb0  (config_mode=0)
  ```
  every ~120 ms. Supervisor is re-running the early NR-init code
  in a tight loop.
- **1 soft reset** (PC=$6D31, rom_bank=$00) — was 2 with the band-aid.
  The supervisor doesn't reach the bank-3 clean-reboot trampoline
  without the captured RAM state to drive it there.

### What the band-aid was hiding

With the band-aid:
- CSpect-captured BASIC state present → supervisor entered BASIC
  → ran "REM dump.bas" line → ended in INPUT-statement parser hang.
  THIS LOOKED LIKE FORWARD PROGRESS but was running on FAKE STATE.

Without the band-aid (true behaviour):
- Supervisor never properly sets up BASIC sysvars.
- Loops in early NR-init.
- Stuck SOON AFTER the 1st soft reset.

This means:
- **All previous "boot reaches X" claims since EOD-5** that didn't
  explicitly note the band-aid are SUSPECT.
- The supervisor's NextZXOS RAM-init code path is BROKEN somewhere
  between the 1st soft reset (PC=$6D31) and the BASIC sysvar setup.

### Branch state

`g46b-investigation` HEAD = `8c198fe` (P42 band-aid removed).

Commits since EOD-12:
```
8c198fe fix(g46b): REMOVE P42 band-aid — was loading CSpect captures every \$5B20 hit
7cf13ba doc(g46b): EOD-14b — supervisor reaches BASIC interpreter, runs user's leftover dump.bas
e4ad286 diag(g46b): extend JNEXT_G46B_P62 probe — dump BASIC sysvars + PROG/CH_ADD/E_LINE/WORKSP
a0f15fb diag(g46b): JNEXT_G46B_P62 probe — dump ULA attribute + pixel state at key frames
3ab98a2 doc(g46b): post-2nd-reset 3-PC loop decoded
18e2a1c diag(g46b): JNEXT_G46B_P61 probe — count screen-bank writes per frame
4508cf8 diag(g46b): JNEXT_G46B_P60 probe — sample CPU PC every 200k instructions
2b3a9e9 diag(g46b): JNEXT_G46B_P59 probe — log CPU PC at SD CMD0/12/17/18 entry
aa53107 fix(g46b): preserve SD/Multiface state across soft reset
463cd0e fix(g46b): preserve clock_/scheduler_/frame_cycle_ on soft reset (EOD-12)
```

Tests: mmu_test 186/164/0/22, sdcard_test 15/15/0/0 — unchanged.

### Next-session priorities (revised post-EOD-14c)

1. **Audit for OTHER unconditional band-aids**. Grep for similar
   patterns: `fopen.*cspect-captures`, `BAND-AID`, `pre-load.*at every`
   in src/. Specifically check for:
   - z80_cpu.cpp Probes P29/P30/P40/P41 — any of those reloading state?
   - emulator.cpp pre-load blocks not gated on bypass.
   The EOD-12 / EOD-14a fixes are GENUINE VHDL-faithful fixes (gated
   on `!preserve_memory`). They stand. But anything firing at runtime
   based on PC value is suspicious.

2. **Trace the NR-init loop**. Set `JNEXT_G46B_P60=1` and capture
   the PC trajectory after the 1st soft reset. Identify the loop
   body (the code that keeps re-writing NR_07 / NR_03). Decode it.

3. **Find the supervisor's BASIC sysvar init code**. On real hardware
   NextZXOS supervisor must initialise BASIC sysvars at some point.
   Search bank 0/1/2 of enNextZX.rom for `LD HL,$5C3B` (FLAGS init),
   `LD HL,$5CCB` (PROG init), or large LDIRs writing to the $5C00
   area. Identify what triggers the init — is it called from
   tbblue.fw post-handoff, or from the supervisor's bank-3
   trampoline path?

4. **Compare with CSpect** (still recommended). Run the same SD
   image in CSpect with PC trace from boot to welcome menu. If
   CSpect doesn't have the NR-init loop, jnext has a bug in NR_07 /
   NR_03 / clock that traps execution. If CSpect ALSO loops here
   briefly, the loop is a legitimate "wait" and the bug is downstream.

5. **Consider re-introducing the SD-preserve assertion test**. EOD-9
   tried "SD-preserve on soft reset" and it caused stalls — that was
   PRE-EOD-12 fix. With clock + SD/MF now preserved, the SD-preserve
   experiment may now succeed. Worth retesting once NR-init loop is
   resolved.


## 2026-05-08 09:59 CEST — EOD-15 wave-1: 3-agent parallel investigation post-band-aid

After P42 band-aid removal (commit `8c198fe`), three parallel agents
were tasked with EOD-14c next-session priorities #1, #2, #3:
band-aid audit, NR-init loop trace, BASIC sysvar init RE.

All three returned consistent findings. **The "NR-init loop" is NOT
a polling wait — it is a soft-reset loop driven by wrapper-toggle
corruption.** Plus one new band-aid found that the EOD-14c memo
missed.

### Agent 1 — `src/` band-aid audit (post-P42)

**Verdict:** P42 stub at `src/cpu/z80_cpu.cpp:1417-1427` is genuinely
dead code (comment-only). All ~41 G46(b) probes in src/ are properly
gated (env-var or `cfg.bypass_tbblue_fw` or `!preserve_memory`)
EXCEPT one previously-overlooked band-aid:

**NEW BAND-AID-SUSPECT — `src/core/emulator.cpp:3827-3839`**
```c++
// G46(b) v3 EXPERIMENT: also pre-populate pages 0x2C-0x2F (the
// +0x20 shifted versions per to_sram_page) so NR_57=0x0F slot 7
// reads see the AltROM data. TEMP — verifying hypothesis that
// supervisor reads bank 7 from this region.
size_t off2 = 0;
for (uint16_t p = 0x2C; p <= 0x2F && off2 < to_copy; ++p) {
    uint8_t* dst = ram_.page_ptr(p);
    if (!dst) break;
    const size_t chunk = std::min<size_t>(0x2000, to_copy - off2);
    std::memcpy(dst, alt_rom_bytes.data() + off2, chunk);
    off2 += chunk;
}
```

- Fires on every NextZXOS hard reset whenever `enAltZX.rom` extracts.
- Outer gate: `cfg.type == ZXN_ISSUE2 && !preserve_memory` — but
  **NOT gated on `cfg.bypass_tbblue_fw`**.
- Diverges from real Next: pages 0x2C-0x2F are SRAM bank 6 high half
  on real hardware, NOT an AltROM landing zone. The legitimate
  AltROM landing is pages 0x0C-0x0F (verified
  `Mmu::altrom_sram_page_` at `mmu.h:1045-1070` returning
  `0x0C | (alt_128_n<<1) | a13`).
- Same family as P42: places ROM bytes in RAM pages the supervisor
  may read via NR_57=$0F slot-7 mapping → physical page 0x2F.
- Self-comments as "v3 EXPERIMENT" / "TEMP — verifying hypothesis".

**Other diagnostic-mutating env-gated hooks** (all inert by default,
should be deleted at G46(b) closure):
- `JNEXT_G46B_FORCE_IFF1` (z80_cpu.cpp:511-521)
- `JNEXT_G46B_FORCE_IX` (z80_cpu.cpp:2157-2177; mutates IX + slot7)
- `JNEXT_G46B_PATCH_IX11` (z80_cpu.cpp:2134-2156)
- `JNEXT_G46B_PATCH` / `JNEXT_G46B_INJECT` (z80_cpu.cpp:2225-2272)
- `JNEXT_G46B_NR_CSPECT` (emulator.cpp:3955-4007; double-gated on
  bypass + env)

### Agent 2 — NR-init loop body decoded

Worktree `.claude/worktrees/nr-init-loop-trace`, branch
`g46b-nr-init-loop-trace`, new commit `e5a893c` adds
`JNEXT_G46B_P63` (32-PC ring-buffer trail decoder).

**P60 sample summary** (top 10 PC frequencies post-1st-reset):

| Hits | PC | rom_bank | Notes |
|------|-----|----------|-------|
| 102 | $0168 | $00 | LDDR cascade in enNextZX.rom |
| 35  | $21B3 | $00 | LDIR helper utility |
| 9   | $1F40 | $03 | DivMMC bank-3 SPI poll loop tail |
| 8   | $1F42 | $03 | same |
| 6   | $1F45 | $03 | same |
| 5   | $1F44 | $03 | same |
| 2   | $240D | $00 | |
| 2   | $23FD | $00 | |
| 2   | $15AE | $01 | bank 1, briefly visited |
| 2   | $01BF | $00 | RAM-test verify pass |

**Loop period**: 110 NR_07←$03 events between t=09:32:53.724 and
t=09:33:07.462, **~7.7 Hz, ~130 ms per iteration** — matches the
~120 ms reported in EOD-14c.

**Inner-loop trail (P63)** — 14 distinct PCs cycling:

```
$00EF (enNextZX.rom bank 0)        ; supervisor entry after reset
  ED 91 07 03   NEXTREG $07,$03    ; CPU = 28 MHz
$00F3
  ED 91 03 B0   NEXTREG $03,$B0    ; boot ROM disabled, machine = Next 128
$00F7
  ED 91 C0 08   NEXTREG $C0,$08    ; INT mode (IM2 vector reset)
... continues to bank-flip wrapper at slot-2 RAM $5B00 ...

$5B00 (slot-2 RAM)                 ; bank-flip wrapper
  F5 C5 01 FD 7F 3A 5C 5B EE 10 F3 32 5C 5B ED 79
  01 FD 1F 3A 67 5B EE 04 32 67 5B ED 79 FB C1 F1 C9
  ; PUSH AF; PUSH BC; LD BC,$7FFD; LD A,($5B5C); XOR $10;
  ; DI; LD ($5B5C),A; OUT (C),A;     ← rom_bank 3→2 here
  ; LD BC,$1FFD; LD A,($5B67); XOR $04; LD ($5B67),A; OUT (C),A;
  ;                                    ← rom_bank 2→0 here
  ; EI; POP BC; POP AF; RET
$5B20
  C9            RET                 ; ← POPS $0000 OFF STACK
                                    ;   (supervisor's caller didn't
                                    ;    push a valid return)

$0000 (after bank ping-pong: now in DivMMC overlay via automap)
  F3            DI                  ; (would be enNextZX.rom byte
                                    ;  but DivMMC automap intercepts)
$0001
  C3 6A 00      JP $006A
$006A (enNxtmmc.rom)
  ED 8A 00 01   PUSH $0001          ; Z80N PUSH IMMEDIATE
$006E
  C3 A0 1E      JP $1EA0
$1EA0
  AF            XOR A
$1EA1
  D3 E3         OUT ($E3),A         ; clear DivMMC control port
$1EA3
  C3 F9 1F      JP $1FF9
$1FF9
  C9            RET                 ; M1 fetch in $1FF8/$1FF9 →
                                    ;   automap-OFF delayed match
                                    ; pops $0001
$0001 (now back in enNextZX.rom; DivMMC unmapped)
  C3 EF 00      JP $00EF            ; ← LOOP CLOSURE
```

**This is NOT a polling-wait loop. There is no JR cc / JP cc / RET cc
in the steady-state body.** Every instruction executes
unconditionally. The loop is closed by the wrapper RET at $5B20
popping $0000, which lands in the DivMMC trampoline, which RETs to
$0001, which JPs to $00EF — restarting the entire init sequence.

**Verification:**
- Re-extracted enNextZX.rom and enNxtmmc.rom from the SD image via
  `mcopy` with MBR offset 32256. MD5 of enNextZX.rom:
  `8c34d957fb2cbbd0be423cea3b370fd5`.
- Disassembled $0000-$0220 of enNextZX.rom and $0000-$0080 +
  $1E90-$2000 of enNxtmmc.rom with `z88dk-dis -mz80n`.
- DivMMC automap entry-point logic at `src/peripheral/divmmc.cpp:329-415`
  matches the trail: `entry_points_0_` defaults to $83 (RST $00, $08,
  $38) with delayed timing; PC=$0000 fetch → automap_hold=true → next
  M1 sees automap_active=true → DivMMC ROM overlays slot 0; PC=$1FF8
  match off range → automap_hold=false → next M1 unmaps.
- Re-ran with P60-only and P63 separately — frequencies and trail
  consistent across runs (no probe artifact).

### Agent 3 — BASIC sysvar init code located in enNextZX.rom

**Located at bank-0 $02AB-$02E5** in enNextZX.rom. Canonical Sinclair
"NEW" pattern (bit-for-bit):

```
$02B0  22 53 5C   LD ($5C53),HL    ; PROG
$02B6  36 80      LD (HL),$80      ; terminator
$02B9  22 59 5C   LD ($5C59),HL    ; E_LINE
$02BC  36 0D      LD (HL),$0D      ; CR
$02C2  22 61 5C   LD ($5C61),HL    ; WORKSP
$02C5  22 63 5C   LD ($5C63),HL    ; STKBOT
$02C8  22 65 5C   LD ($5C65),HL    ; STKEND
$02CD  32 8D 5C   LD ($5C8D),A     ; ATTR_P (A=$38 paper-7-ink-0)
$02D0  32 8F 5C   LD ($5C8F),A     ; MASK_P
$02D9  32 48 5C   LD ($5C48),A     ; BORDCR
$02DC  3E 07      LD A,$07
$02DE  D3 FE      OUT ($FE),A      ; border = WHITE
$02E3  22 09 5C   LD ($5C09),HL    ; REPDEL/REPPER
```

**Boot-path chain to reach this init:**

```
$00EF  NEXTREG $07,$03 ; $03,$B0 ; $C0,$08 ; ports $82..$85=$FF ; ...
$0124  LD HL,$5800 ; LDIR clear ULA attr area ($5800-$5AFF)
$0130  RAM-test pass 1 (B=$70 banks via NR_56,A / NR_57,A+1)
$018E  RAM-test pass 2 ($DCBA pattern)
$01CE  LD ($5B69),A                ; save RAM bank count
$01D1  LD SP,$5BFF                 ; setup stack
$01D4  RST $20 / 1F ED 91 8E 08    ; bank-2 syscall
$01DB  IM 1                        ; enable IM 1 INTs
$01DD  CALL $00E3                  ; copy 82-byte bank-flip wrapper
                                   ; ($0091..$00E2 → $5B00..$5B51)
$01E0..$0252  setup, RST $28 (bank-3 LDIR), populate slots
$025A  RST $18 / 85 34             ; bank-1 long-call → $3485 (SP setup)
$0271  CALL $2341                  ; MAJOR INIT (slot-2 RAM, RST $28)
$0274  RST $18 / 00 15             ; bank-1 long-call → $1500
                                   ; ($1500 = palette + L2/Tilemap setup)
$02A0  RST $28 / 33 EB             ; bank-3 LDIR-style call
                                   ; (copies 21-byte template
                                   ;  bank-3 $15AF → RAM $5CB6,
                                   ;  HL emerges in $5CCB area)
$02AB+ *** BASIC SYSVAR INIT BLOCK ***
$02E6+ welcome-menu drawing
```

**Bank-flip wrapper at $5B00 (82 bytes from ROM $0091..$00E2):**
- `$5B00..$5B20`: port_7FFD bit-4 + port_1FFD bit-2 toggle helper
- `$5B3A..$5B42`: PUSH $0A9E; NEXTREG $8E,$01; RET — bank-1 pivot
- `$5B43..$5B47`: NEXTREG $8E,$02; RET — bank-2 pivot
- `$5B48..$5B4C`: NEXTREG $8E,$03; RET — bank-3 pivot (RST $28)
- `$5B4D..$5B51`: NEXTREG $8E,$00; RET — bank-0 return-pivot

**Bank-3 SOFT-RESET trampoline at $3BE8** (the smoking gun):

```
$3BE8  LD A,$02 ; OUT (C),A     ; select NEXTREG $02 (BC=$243B/$253B)
$3BEC  INC B ; IN A,(C)         ; read NR $02 current value
$3BEF  AND $80 ; OR $01         ; preserve bit 7, set bit 0 (soft reset)
$3BF3  OUT (C),A                ; trigger soft reset
$3BF5  RST $38                  ; halt-loop until reset fires
```

Bank-3 entry at $C000 (= bank-3 file offset $0000):
```
$C000  DI ; XOR A ; LD BC,$243B ; JP $3BE8
```

**So whenever execution lands at PC=$0000 with bank 3 paged into
slot 0, it triggers a soft reset.**

**Verification:**
- Pattern `LD HL,X; LD ($5C53),HL; ...; LD ($5C65),HL` is bit-for-bit
  the canonical Sinclair "NEW" routine; only one match in the entire
  64KB ROM.
- Bank-3 $3BE8 byte pattern (`3E 02 ED 79 04 ED 78 E6 80 F6 01 ED 79`)
  is unique in the ROM (1 match).
- No `JP $0000`, no `JP $00EF` (other than the $0001 reset-entry
  trampoline) anywhere in the ROM — re-entry to NR_03/NR_07 init MUST
  come via reset.

### Synthesis — convergent root cause

Agents 2 and 3 independently identified the same picture:

1. The "NR-init loop" is a SOFT-RESET LOOP, not a polling wait.
2. The wrapper RET at $5B20 pops $0000.
3. $0000 + DivMMC automap → trampoline → JP $00EF restarts init.
4. Each iteration is one full cycle through the supervisor's early
   init code.

**Why does the wrapper RET pop $0000?** Two complementary reasons:

(a) **Caller-side**: the supervisor's CALL to the wrapper had its
return address overwritten by the LDDR cascade at $0166-$0168 of
boot init, which zeros all of slot-2 RAM ($4000-$5AFF) including
the supervisor's saved frames. (Hits=102 at PC=$0168 confirm this
runs ~once per loop iteration.)

(b) **Wrapper-side**: when the wrapper at $5B00 reads
`LD A,($5B5C); XOR $10; LD ($5B5C),A; OUT (C),A` — with $5B5C
initialised to ZERO (no band-aid pre-load), the toggle writes
$10 → $00 → $10 → $00 alternately to port $7FFD. This ping-pongs
slot 1 between ROM and RAM in a way the supervisor doesn't expect.

The captured `sysvars.raw` that the P42 band-aid had been pre-loading
contained the CORRECT mid-boot values of `$5B5C` and `$5B67` from a
real CSpect session. Without it, those shadows are zero, the
ping-pong toggles wrong, bank state diverges from supervisor's
expectation, CALL/RET targets land in wrong banks, the wrapper RET
pops $0000, and the supervisor falls into the DivMMC trampoline
back to $00EF.

**This is consistent with the EOD-14a observation that the supervisor
"reaches a SECOND soft reset at PC=$3BF5 rom_bank=$03"** — that's the
bank-3 trampoline at $3BE8 firing. EOD-15 now establishes that the
trampoline keeps firing every ~120 ms forever.

**The real bug is not in supervisor logic — it is in jnext's
emulation of one of the boot-time bank-paging primitives.** The
supervisor's CALL/RET chain works on real hardware; on jnext, by
the time RET at $5B20 fires, the stack has $0000 instead of the
correct return-pivot. Candidates for the misemulated primitive:

1. **NEXTREG $8E semantics** for values $00..$03. The wrapper pivots
   at $5B3A/$5B43/$5B48/$5B4D do `PUSH <pivot>; NEXTREG $8E,N; RET`,
   so the RET fetches the new bank's first byte at $0000. If
   NEXTREG $8E semantics differ between jnext and FPGA VHDL for any
   N, the next instruction comes from the wrong bank.

2. **port $7FFD / $1FFD bank-paging** behaviour. The wrapper toggles
   bit 4 of $7FFD and bit 2 of $1FFD. If jnext models the resulting
   bank/RAM layout differently from VHDL (especially under NextZXOS
   `port_decoding` register settings), the rest of the supervisor
   reads wrong bytes.

3. **DivMMC automap timing.** PC=$0000 fetch happens AFTER bank
   ping-pong; if automap should NOT be active (because the
   supervisor expects to land in a real ROM, not the DivMMC
   overlay), but jnext fires it, that's a bug. Conversely, if it
   SHOULD be active and jnext's timing is right, this is just a
   symptom and the real bug is upstream.

4. **NEXTREG $03 (machine config) side-effects.** The supervisor
   writes $03 ← $B0 at $00F3, which on real hardware sets
   machine = Next 128 + boot ROM disabled. If jnext's handling of
   the boot-ROM-disable transition leaves slot 0 in a different
   state than VHDL (for example, leaving boot ROM mapped when
   VHDL would unmap it), every fetch at $0000 onward reads wrong
   bytes.

### Branch state

`g46b-investigation` HEAD `3180d38` — UNCHANGED.

Worktree `.claude/worktrees/nr-init-loop-trace` (branch
`g46b-nr-init-loop-trace`) HEAD `e5a893c` — adds `JNEXT_G46B_P63`
probe. Will be useful for next-wave follow-on probes.

mmu_test 186/164/0/22 unchanged. sdcard_test 15/15 unchanged.

### Next-wave plan (EOD-15 wave-2)

Based on these findings, the wave-2 actions are:

1. **Remove the AltROM mirror band-aid** at emulator.cpp:3827-3839
   (per VHDL-faithful directive). Re-baseline boot to see whether
   removal changes the loop or supervisor depth.

2. **Probe wrapper installation**: dump bytes at $5B00..$5B51 on
   first hit of PC=$01E0 (after `CALL $00E3`) to verify wrapper
   was installed correctly. Compare to the canonical 82 bytes from
   ROM $0091..$00E2.

3. **Probe wrapper-toggle shadows**: log $5B5C and $5B67 values
   on every $5B00 wrapper entry. Compare jnext trajectory vs the
   captured CSpect values (which we know unblocked the supervisor).

4. **Probe bank-3 $3BE8 trampoline hits**: count hits at PC=$3BE8
   with rom_bank=$03 and confirm trampoline fires once per loop
   iteration.

5. **Probe `CALL $2341` at PC=$0271**: does the supervisor reach
   this call? If yes, does it return successfully, or does the
   bank-3 trampoline fire mid-call?

6. **Audit NEXTREG $8E semantics** against VHDL `nextreg.vhd`.
   Specifically the bit semantics of $8E for values $00..$03 vs
   port $7FFD / $1FFD interaction.

7. **CSpect side-by-side trace** if/when CSpect-side instrumentation
   becomes available.

## 2026-05-08 10:21 CEST — EOD-15 wave-2: AltROM mirror removed; wrapper bytes correct; bank-3 $3BE8 confirmed as loop driver

Three parallel agents executed wave-2:
- **Agent A** removed the AltROM mirror band-aid found in wave-1.
- **Agent B** added `JNEXT_G46B_P64` probes (wrapper bytes / shadows /
  trampoline / CALL $2341 reach).
- **Agent C** audited NEXTREG $8E semantics jnext vs VHDL.

### Agent A — AltROM mirror band-aid removed

Branch `g46b-altrom-mirror-removal`, commit `8d96dc1`, **fast-forwarded
into g46b-investigation**. 13 lines code removed (the experimental
`for (p=0x2C..0x2F)` mirror loop), replaced with explanatory comment.

**Pre/post comparison (30 s headless boots):**

| Metric | With mirror | Without mirror | Delta |
|--------|-------------|----------------|-------|
| Soft resets | 1 | 1 | 0 |
| NR_07 writes (28 MHz) | 110 | 110 | 0 |
| NR_03 writes | 113 | 113 | 0 |
| P60 hits | 282 | 282 | 0 |
| Top P60 PCs | $0168×102, $21B3×35, ... | $0168×102, $21B3×35, ... | identical |
| Screenshot md5 | `d6d5c6d55705954baee13cde8156ddc9` | `d6d5c6d55705954baee13cde8156ddc9` | bit-identical |

**Verdict: the mirror was truly INERT.** The supervisor never read
pages 0x2C-0x2F during the 30-second observation window. The "v3
hypothesis" (supervisor reads AltROM via NR_57=$0F slot 7 → physical
page 0x2F) is decisively WRONG. Removal is a clean code-hygiene win,
nothing more.

mmu_test 164/186/0/22, sdcard_test 15/15 — unchanged. fuse_z80_test
+ z80n_test pre-existing link failures (DivMmc/BreakpointSet symbol
issues, predates this work).

### Agent C — NEXTREG $8E audit: STRONG NO

Three independent audits (today + 2026-04-19 + 2026-05-06) agree:
**NR_$8E is bit-for-bit VHDL-faithful.** Cross-checked:

- `zxnext.vhd:3662-3670, 3696-3704, 3726-3734` (NR_$8E decomposition
  into port shadows) vs `mmu.cpp:444-502` (Mmu::write_nr_8e).
- `zxnext.vhd:3772` (`port_1ffd_rom = 1FFD(2) & 7FFD(4)`) vs
  `mmu.cpp:347` (rom_bank computation).
- `zxnext.vhd:3813-3814` (port_memory_change_dly) vs the rebuild gate
  at `mmu.cpp:497`.
- `zxnext.vhd:6158-6159` (read-back) vs `mmu.cpp:509-523`.

For all four wrapper-pivot values ($00/$01/$02/$03), jnext slot-0 ROM
page selection matches VHDL exactly (banks 0/1/2/3 = SRAM pages
0/2/4/6).

**Critical clarification of an ambiguity in the wave-1 hypothesis:**
The bank-flip wrapper at `$5B00..$5B20` does NOT use NEXTREG $8E. It
uses raw `OUT ($7FFD)` / `OUT ($1FFD)` toggles. The NEXTREG $8E pivots
at `$5B3A`/`$5B43`/`$5B48`/`$5B4D` are SEPARATE auxiliary trampolines
used by RST $20/$28 long-call dispatch, not by the wrapper itself.
So even a hypothetical NR_$8E bug couldn't explain the wrapper RET
popping $0000.

NR_$8E definitively eliminated as a candidate. Bug must be in:
- port `$7FFD` / `$1FFD` direct-write semantics (wrapper-relevant),
- soft-reset stack / RAM preservation,
- supervisor stack-frame setup before wrapper entry,
- NR_$03 config_mode transition (corner case, less likely),
- port_eff7(3) RAM-at-$0000 mode (corner case, less likely).

### Agent B — wrapper / shadow / $3BE8 / CALL $2341 probes (THE BIG ONE)

Branch `g46b-wrapper-shadow-probe`, commit `0442a71`. Adds
`JNEXT_G46B_P64` env-gated probe (4 sub-probes).

#### Probe 1 — wrapper bytes at $5B00..$5B51

**81 of 82 bytes MATCH** byte-for-byte against canonical ROM
$0091..$00E2. ONE deliberate divergence at $5B33: RAM=$DF, ROM=$00.
This is by-design — bank-0 file offset $2387 contains
`LD ($5B33),A` in the `M_GETSETDRV` sequence, deliberately reusing
the dead $00 NOP filler byte after the JR-displacement at $5B32 as
a 1-byte sysvar slot (likely current-drive). Downstream of an
unconditional `JR $-35` at $5B31..$5B32, so cannot affect wrapper
execution.

**Wrapper LDIR install is BYTE-CORRECT.** No code corruption.

#### Probe 2 — wrapper shadows on every $5B00 entry (50 hits)

| Pattern | $5B5C | $5B67 | AF | BC | SP | rom_bank | Hit indices |
|---------|-------|-------|------|-------|-------|----------|-------------|
| Even | $00 | $00 | $1F08 | $253B | $5BFF | 0 | 0,2,4,...,48 |
| Odd | $10 | $04 | $0082 | $D2FF | $5C39 | 3 | 1,3,5,...,49 |

**Two-state oscillation**, deterministic.

**Hit #0 shadows are exactly $00, $00.** Wrapper XORs them with
$10/$04 → outputs ($10, $04) → port $7FFD bit 4 set + port $1FFD
bit 2 set → rom_bank flips 0→3. Next entry sees ($10, $04), XOR
toggles back to (0,0) → rom_bank flips 3→0. Cycle repeats forever.

**The supervisor never wrote sane initial values to $5B5C/$5B67.**
The captured `sysvars.raw` (formerly loaded by the P42 band-aid)
contained mid-boot CSpect values that gave the supervisor a
"correct" starting state. Without it, both shadows are zero and the
toggle ping-pong has nothing to anchor.

**SP=$5BFF at every even-state wrapper entry** (= the value set by
`LD SP,$5BFF` at $01D1). This is BEFORE the wrapper's PUSH AF, so
SP=$5BFF means the supervisor entered $5B00 via **JP, not CALL**
(no preceding push). Therefore the wrapper RET at $5B20 will pop
from $5BFF/$5C00 — bytes that are uninitialized (sysvar area zeros)
→ RET pops $0000.

**Even-state context** (rom_bank=0, AF=$1F08, BC=$253B, SP=$5BFF):
the supervisor JPs to $5B00 from somewhere in bank 0 with SP at the
top of stack and BC=$253B (NextREG select port). This is post-init
state.

**Odd-state context** (rom_bank=3, AF=$0082, BC=$D2FF, SP=$5C39):
the supervisor enters $5B00 from inside BANK 3 with SP at $5C39
(deeper stack with values pushed). BC=$D2FF is unusual.

#### Probe 3 — bank-3 $3BE8 hits

**1275 hits in ~28 emulated seconds** (~45 hits/sec). Clusters of
5-10 nested hits per loop iteration, separated by ~700K-step pauses.
SP at hit-time drifts $5C01 → $5C39 across cluster, then resets.

**The bank-3 $3BE8 trampoline IS the soft-reset driver.** Loop period
~120 ms matches the user-reported 130 ms. The "1 soft reset" log
count is likely jnext only logging the FIRST one, not coalescing
the trampoline-driven re-entries into the counter.

#### Probe 4 — CALL $2341 reach

**30 hits at PC=$0271, 30 hits at PC=$2341, lock-step.**
Every hit: rom_bank=$00, AF=$6F00, BC=$006F, HL=$FF58, SP=$FF53/51.

CALL $2341 from $0271 lands correctly in bank 0. **No bank-routing
bug at this site.** Same machine state every iteration → deterministic
hot path. The supervisor reaches the "MAJOR INIT" code at $2341.

The discriminating fact: SP at $0271/$2341 hits is $FF53/$FF51, NOT
$5BFF. So $0271/$2341 are reached AFTER significant stack activity
(SP grew to $FF5x — way above the initial $5BFF). The supervisor
must have done `LD SP,$FFXX` somewhere between $01D1 and $0271.

### Synthesis — convergent picture (wave-2)

**Confirmed:**
- Wrapper code at $5B00..$5B51 is byte-correct.
- NR_$8E is VHDL-faithful.
- Shadows $5B5C/$5B67 enter the wrapper as (0,0).
- Wrapper RET at $5B20 pops from uninitialized stack → $0000.
- Bank-3 $3BE8 is the soft-reset trampoline driving the loop.
- CALL $2341 reaches with correct bank state and registers.
- AltROM mirror to pages 0x2C-0x2F was inert.

**Unresolved:**
- WHY does the supervisor enter $5B00 with SP=$5BFF (= no PUSH/CALL
  preceded the entry)? What is the entry mechanism?
- WHO/WHAT was supposed to populate the bytes at $5BFF/$5C00 (the
  RET pop target)?
- WHY does the supervisor enter $5B00 a SECOND time with rom_bank=3
  and SP=$5C39? What chain leads bank 3 → wrapper → bank 0 → ...?

**Likely shape of the bug:** The supervisor, somewhere between
$01D1 (LD SP,$5BFF) and the first $5B00 entry, was supposed to:
- Push a target address onto the stack (CALL $5B00 or PUSH;JP $5B00),
  OR
- Initialize $5B5C/$5B67 (port shadows) to known values, OR
- Initialize bytes at $5BFE/$5BFF/$5C00 (the RET pop area), OR
- Some combination of the above.

In jnext, that initialization either doesn't run at all, or runs with
zero/wrong values. The *mechanism* by which it should run on real
hardware (presumably via a CALL to a NextZXOS init helper, or via a
RST/syscall trampoline that returns with state set) is what's broken.

### Branch state

`g46b-investigation` HEAD `8d96dc1` (post-merge of AltROM mirror
removal). 5 commits ahead of EOD-14c HEAD `3180d38`:
```
8d96dc1 fix(g46b): REMOVE AltROM mirror to pages 0x2C-0x2F band-aid
f1ca628 doc(g46b): EOD-15 wave-1 — 3-agent parallel investigation post-band-aid
3180d38 doc(g46b): EOD-14c — P42 band-aid removed, real bug exposed
8c198fe fix(g46b): REMOVE P42 band-aid
7cf13ba doc(g46b): EOD-14b — supervisor reaches BASIC interpreter
```

Worktrees alive:
- `.claude/worktrees/altrom-mirror-removal` (merged, can be removed).
- `.claude/worktrees/wrapper-shadow-probe` (P64 probe, useful for
  follow-up).
- `.claude/worktrees/nr-init-loop-trace` (P63 probe from wave-1,
  still useful).

mmu_test 164/186/0/22, sdcard_test 15/15 — unchanged across all
wave-1 + wave-2 changes.

### Wave-3 plan (next moves)

1. **Stack-write watcher + RET-pop logger** (`JNEXT_G46B_P65`):
   - Watch every memory write to addresses $5BFC..$5C03 (the wrapper
     RET pop area + slack). Log PC + value + addr.
   - Log peek($5BFE), peek($5BFF), peek($5C00), peek($5C01) at PC=$5B00
     entry.
   - Log the bytes RET will pop at PC=$5B20.
   - This will reveal whether anything ever populates the stack
     target bytes, and confirm RET always pops $0000.

2. **Static analysis — find all entry sites to $5B00** in
   enNextZX.rom (any bank). Search for:
   - `JP $5B00` byte pattern (`C3 00 5B`).
   - `CALL $5B00` (`CD 00 5B`).
   - `JR ... $5B00` (relative jump). 
   - Indirect via stack push: `PUSH HL` where HL=$5B00, then `RET`.
   Identify which sites match the runtime data (bank-0 vs bank-3 entry
   paths).

3. **port $7FFD / $1FFD direct-write semantics audit** vs VHDL
   (per Agent C's recommendation): especially `paging_locked`,
   `port_1ffd_special_old`, RAM-bank routing under different
   port_eff7 / NR_8C / NR_57 conditions.

4. **Decode the bank-3 $3BE8 cluster pattern**: 5-10 nested hits per
   loop iteration suggests RST $38 at $3BF5 doesn't actually halt or
   the soft reset doesn't fire on every NR_$02 ← $01 write. Either
   NR_$02 handler has an edge-detect guard, or RST $38 returns. Worth
   decoding bank-3 $0038 (RST $38 vector) to understand.

5. **CSpect side-by-side trace** if instrumentation becomes available.

## 2026-05-08 11:04 CEST — EOD-15 wave-3: ROOT CAUSE FOUND — Divergence A in legacy ROM-bank composition

Three parallel agents (X, Y, Z) plus a fix-implementor agent
identified and remediated the root cause of the ~120ms NR-init
soft-reset loop.

### Wave-3 agent summary

#### Agent X — `JNEXT_G46B_P65` stack-write watcher + RET-pop logger + 16K PC trail

Branch `g46b-stack-write-watcher` HEAD `1a2c29a`. Captured:
- 200 stack writes to $5BFC..$5C0F.
- 50 P65E entries at PC=$5B00.
- 50 P65R RET-pop logs at PC=$5B20.
- 16,384-entry PC ring buffer dumped at first $5B00 hit.

**P65R confirms: RET pops $0000 in BOTH even and odd context.**

**P65T trail revealed the supervisor's full chain to $5B00:**

```
bank-1 $26B9: PUSH HL ; LD HL,($5B54) ; EX (SP),HL ;
              PUSH $007B ; JP $3E93
bank-1 $3E93: ED 91 8E 00 C9   = NEXTREG $8E,0; RET (flip to bank 0)
              RET pops $007B
bank-0 $007B: ED 91 8C 80 C9   = NEXTREG $8C,$80; RET (enable alt-rom)
              RET pops $5B54_value (= $3E93)
              ↓
PC=$3E93, rom_bank=0, alt-rom enabled
              ↓
              7,278 SEQUENTIAL +1 INSTRUCTION FETCHES (NOP sled)
              all the way to $5B00
              ↓
$5B00: bank-flip wrapper executes
$5B20: RET pops $0000 → DivMMC trampoline → JP $00EF → loop
```

**Diagnosis:** `enAltZX.rom` from the user's SD image is sparse —
contains valid code at $0000-$2730 + $4000-$5180 + $6800-$7B20 but
ZEROS at $3E93+ and $7E93+. When the supervisor JPs to $3E93 with
alt-rom enabled, jnext routes slot-0 reads through `altrom_sram_page_`
to alt-rom pages (0x0C-0x0F) which contain those zeros.

**However**: VHDL `zxnext.vhd:3138` (referenced in `mmu.h:728-730`)
gates alt-rom routing on `sram_rom3='1'` — alt-rom only replaces
ROM 3, not ROM 0/1/2. jnext's `Mmu::read` at `mmu.h:310` does NOT
check `sram_rom3()` — it only checks `nr_8c_altrom_en()`. **This
is a SECOND divergence (Divergence B) potentially distinct from
Divergence A** — to be investigated in wave-4.

#### Agent Y — Static analysis of $5B00 entry sites in enNextZX.rom

Found:
- **ONE** external JP/CALL to $5B00 in entire ROM: bank-1 `JP $5B00`
  at file offset $41A6 (= bank-1 $01A6, runtime $5BD1 after LDIR).
- Bank-3 $0000 entry decoded: `DI; XOR A; LD BC,$243B; JP $3BE8`
  — confirmed soft-reset trampoline.
- Bank-3 $0038 RST $38 vector: **canonical IM 1 ISR** (NOT a
  halt/spin). PUSH AF/HL; INC FRAMES; CALL $386E; POP; EI; RET.
  Explains why 1275 hits at $3BE8 produce only 1 logged soft reset
  in the runtime — RST $38 returns rather than halting.
- Bank-1 $0000: `NOP; JP $3F00` — normal continuation.
- Shadow init helpers found:
  - bank-2 $25A8 sets $5B5C=$03 / $5B67=$04 (alt-rom path).
  - bank-3 $1FA8 sets $5B5C=$10 / $5B67=$04 (long-call path).
- **No code in any bank statically primes $5BFE/$5BFF/$5C00/$5C01**
  — those bytes are only stack-residue from CALL pushes.

#### Agent Z — port $7FFD/$1FFD direct-write semantics audit jnext vs VHDL

VHDL line-by-line cross-check (`zxnext.vhd:2593, 2599` decode;
`:3640-3815` registers; `:4607-4700` MMU rebuild). Substantially
VHDL-faithful EXCEPT one major divergence:

**Divergence A — `apply_legacy_rom_slots_` and `map_plus3_bank`
non-special branch use 2-bit ROM composition unconditionally:**

```cpp
int rom_bank = ((port_1ffd_ >> 2) & 1) << 1 | ((port_7ffd_ >> 4) & 1);
```

Per VHDL `zxnext.vhd:2981-3008`:
- 48K mode: always ROM 0 (1-bit hardwired).
- 128K and Next modes: 1-bit composition (only $7FFD bit 4).
- +3 mode: 2-bit composition.

For default `MachineType::ZXN_ISSUE2` + the supervisor's bank-flip
wrapper writes `OUT ($7FFD), $10` then `OUT ($1FFD), $04`:
- **jnext**: rom_bank = `(1<<1)|1 = 3` → slot 0 maps to SRAM page 6
  = enNextZX.rom **bank 3 = SOFT-RESET TRAMPOLINE** at $0000.
- **VHDL Next mode**: sram_rom = `'0' & port_7ffd(4) = '01' = 1`
  → slot 0 maps to SRAM page 2 = enNextZX.rom **bank 1** = `NOP; JP
  $3F00` (normal continuation).

When the wrapper RET pops $0000:
- jnext: lands in bank-3 $0000 = `DI; XOR A; LD BC,$243B; JP $3BE8`
  → soft-reset trampoline → loop.
- VHDL: lands in bank-1 $0000 = `NOP; JP $3F00` → continues normally.

The accessor `Mmu::current_sram_rom()` at `mmu.h:859-878` already
encodes the per-machine-type semantics correctly (handles 48K /
128K / Next / +3 + altrom-lock overrides). The bug was that
`apply_legacy_rom_slots_` and `map_plus3_bank` did not delegate to
it.

### Fix landed — commit `9b9fb2c` on branch `g46b-divergence-a-fix`

```cpp
// Was (mmu.cpp:347):
int rom_bank = ((port_1ffd_ >> 2) & 1) << 1 | ((port_7ffd_ >> 4) & 1);
map_rom_physical(0, rom_bank * 2);
map_rom_physical(1, rom_bank * 2 + 1);

// Now:
const uint8_t sram_rom = current_sram_rom();
map_rom_physical(0, sram_rom * 2);
map_rom_physical(1, sram_rom * 2 + 1);
```

Plus `map_plus3_bank` non-special branch now delegates to
`apply_legacy_rom_slots_()` — single source of truth.

20 insertions, 13 deletions. 1 file changed.

### Post-fix smoke test results (BREAKTHROUGH)

Pre-fix vs post-fix 30s headless baseline:

| Metric | Pre-fix (Divergence A) | Post-fix | Delta |
|--------|------------------------|----------|-------|
| Soft resets | 1 (logged) but 1275 $3BE8 hits | 1 | ~ |
| NR_07 writes | **110** (CPU=28MHz loop) | **2** (boot + post-soft-reset) | **-108** |
| NR_03 writes | 113 | 5 | -108 |
| Top P60 PCs | $0168×102, $21B3×35 (loop) | scattered $6c00/$7800/$b7xx/$b9xx/$e900/$f37x/$fa28 | NEW CODE PATHS |
| Screenshot md5 | `d6d5c6d5...` (black/grey) | `dea737c7...` (RED/light-grey) | CHANGED |

**The supervisor is now executing real code in slot-2 RAM ($4000-$7FFF)
and slot-7 RAM ($E000-$FFFF) — NextZXOS supervisor's main code
regions.** The ~120 ms NR-init soft-reset loop is GONE.

Screen now shows light-grey border + red interior. Likely
INK=PAPER=red attribute bug (welcome menu not yet rendered to
legibility) — separate issue, not a paging concern.

### Test results (post-fix)

- mmu_test: **186 / 164 PASS / 0 FAIL / 22 SKIP** — IDENTICAL to
  baseline.
- sdcard_test: **15 / 15 / 0 / 0** — IDENTICAL.
- Regression: **32 PASS / 1 FAIL** (rewind-func — pre-existing per
  EOD-12 memory). **Zero regressions introduced.**

### Branch state

`g46b-investigation` HEAD `3f539eb` — UNCHANGED (fix awaits
independent reviewer per project rules).

`g46b-divergence-a-fix` HEAD `9b9fb2c` — fix ready for review.

Worktrees alive:
- `.claude/worktrees/divergence-a-fix` — fix candidate (await reviewer).
- `.claude/worktrees/wrapper-shadow-probe` — P64 probes (useful for
  follow-up).
- `.claude/worktrees/nr-init-loop-trace` — P63 probes.
- `.claude/worktrees/stack-write-watcher` — P65 probes.

### Implications for prior memos

EOD-14a's "supervisor reaches a SECOND soft reset at PC=$3BF5
rom_bank=$03" claim is now explained: that was the bank-3 $3BE8
trampoline being entered via wrapper-RET-to-$0000-in-bank-3 due to
Divergence A.

EOD-14a's "supervisor settles into rom_bank=$01 sustained — first
time bank 1 reached" is also explained: with Divergence A active,
rom_bank composition oscillated between $00 and $03; "rom_bank=$01"
appearances were rare/transient. Post-fix, rom_bank=$01 is now the
sustained state during alt-rom dispatch (as VHDL intends).

### Wave-4 plan

1. **Independent reviewer for Divergence A fix** (mandatory per
   project rules). Cross-check:
   - VHDL `zxnext.vhd:2981-3008` line-by-line.
   - `current_sram_rom()` semantics for all 4 machine types.
   - `apply_legacy_rom_slots_` callers don't break under the new
     delegation.
   - mmu_test scenarios for non-default machine_type.
   - `map_plus3_bank` non-special branch delegation correctness.

2. **Investigate red-screen / next blocker.** The supervisor now
   reaches PCs in $b7xx-$b9xx and $e900/$f37x/$fa28 — these are
   sysvars init / screen attr fill / welcome-menu draw routines.
   Probe what the supervisor is doing there. The red screen
   suggests INK=PAPER attribute issue.

3. **Investigate Divergence B (sram_rom3 gating in alt-rom routing).**
   `Mmu::read` at `mmu.h:310` routes to alt-rom whenever
   `nr_8c_altrom_en()` is set, but VHDL `zxnext.vhd:3138` gates
   alt-rom routing on `sram_rom3='1'`. May be benign or may be
   another root cause depending on supervisor's sram_rom3 trajectory.
   Confirm with a probe.

4. **Cleanup probes after G46(b) closure**: P55..P65 + FORCE_IFF1
   etc. should all go.

## 2026-05-08 11:25 CEST — EOD-15 wave-3 reviewer report

Independent reviewer agent (per `feedback_never_self_review.md`)
audited Divergence A fix on branch `g46b-divergence-a-fix`
HEAD `9b9fb2c`.

### VHDL faithfulness

Cross-checked `zxnext.vhd:2981-3008` (sram_rom selector),
`zxnext.vhd:3772` (port_1ffd_rom composition), and
`zxnext.vhd:5741-5757` (machine-type decoder) against jnext
`current_sram_rom()` at `mmu.h:859-878`.

**Bit-by-bit table for the four wrapper pivot scenarios in Next mode:**

| port_7FFD | port_1FFD | VHDL Next "else" branch | jnext ZXN_ISSUE2 | Match |
|-----------|-----------|--------------------------|------------------|-------|
| $00 | $00 | 0 | 0 | YES |
| $10 | $00 | 1 | 1 | YES |
| $00 | $04 | 0 | 0 | YES |
| $10 | $04 | 1 | 1 | YES |

48K mode + +3 mode + altrom-lock overrides all match. **VHDL-faithful
for the scenarios this fix addresses.**

Critical structural confirmation: VHDL has only THREE branches
(`machine_type_48`, `machine_type_p3`, "else") — `machine_type_128`
falls through to "else", same as Next/Pentagon. jnext's
`current_sram_rom()` correctly groups ZX128K and ZXN_ISSUE2 in the
same `default` arm.

### Caller analysis

All callers of `apply_legacy_rom_slots_` and `map_plus3_bank` verified:
- `apply_legacy_paging_` — unchanged, calls both halves.
- `write_nr_8e` — modifies port_7ffd_/port_1ffd_ before call;
  `current_sram_rom()` reads post-update state. Correct.
- `map_128k_bank` / `write_port_dffd` / `write_port_eff7` /
  `write_nr_8f` — all unchanged, invoke `apply_legacy_paging_`.
- $1FFD port-out handler — invokes `map_plus3_bank` (now delegating).

**No caller broken.**

### Tests + reproducibility

Independent rerun:
- mmu_test 186/164/0/22 — matches baseline.
- sdcard_test 15/15 — matches baseline.
- Headless smoke (4th run): 1 soft reset, 2 NR_07 writes, 5 NR_03
  writes, screenshot md5 `dea737c7e5587d07f6cf4f5cd1f210d3` —
  REPRODUCIBLE across all 4 runs (3 implementor + 1 reviewer).

### Issues found by reviewer

**CRITICAL:** none.

**MAJOR — coverage gap (per CLAUDE.md project rule):** No regression
test for the fixed bug. Should add Cat5/Cat11 mmu_test cases asserting
`get_effective_page(0)` post-`map_plus3_bank` for all 4 MachineType
values × 4 port pivots (16 scenarios).

**MAJOR — Divergence C (pre-existing, untouched by fix):** `map_plus3_bank`
non-special branch (now delegating to `apply_legacy_rom_slots_`)
still doesn't refresh slots 6/7 RAM, while VHDL `:4677-4680` would
clobber any NR-mmu-set values back to legacy. Practical impact
limited (port_7ffd_bank doesn't change on $1FFD writes), but a real
divergence in the NR-mmu-clobber scenario. **Not a regression** —
preserves pre-existing behavior.

**MINOR — `current_sram_rom()` ZX128K branch:** Returns raw
`(port_7ffd_>>4)&1` without altrom-lock check; `sram_rom3()` at
mmu.h:901-918 correctly applies the lock. Inconsistent. Pre-existing,
no functional impact today (boot path runs ZXN_ISSUE2).

**MINOR — incidental EFF7(3) repair not surfaced:** The
`map_plus3_bank` delegation incidentally fixes a latent bug where
the EFF7(3)=1 RAM-at-$0000 mode wasn't honored on $1FFD writes
pre-fix. Worth calling out in commit message / doc.

**NIT:** Comment at line 348 cosmetic only.

### Reviewer verdict

**APPROVE-WITH-NITS.** Reviewer recommends:

> "CHERRY-PICK / FAST-FORWARD-MERGE INTO `g46b-investigation`
> IMMEDIATELY, with the following follow-up tasks queued:
> 1. Add Cat5 / Cat11 regression tests (16 scenarios).
> 2. Investigate Divergence C (slot 6/7 on $1FFD writes).
> 3. Fix `current_sram_rom()` ZX128K altrom-lock grouping.
> 4. Update commit message / EOD doc to surface EFF7(3) repair.
> The fix is ready for merge. Wave-4 investigation (red-screen /
> Divergence B / sram_rom3 alt-rom gating) can proceed after merge."

### Manager note

User directed "stop before starting wave 4" at this checkpoint.
**Merge of `g46b-divergence-a-fix` into `g46b-investigation` is
NOT yet performed.** Awaiting explicit go/no-go from the user.
Reviewer recommends merge; bug fix is VHDL-faithful and zero
regressions verified.

