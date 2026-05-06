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

---

## 2026-05-06 08:46 CEST — DEFINITIVE BREAKTHROUGHS (parallel-agent confirmed)

### Architecture fully decoded

The supervisor's bank-transition machinery has **four parallel wrapper
families**, all at the SAME PCs but different bytes per ROM bank:

#### 1. RST $20 / RST $28 / wrapper-PC bank-transition table (verified by agent 3)

| PC      | bank 0 → sets | bank 1 → sets | bank 2 → sets |
|---------|---------------|---------------|---------------|
| $3E00   | rom_bank **2** | (regular code) | rom_bank **0** |
| $3E80   | rom_bank **1** | rom_bank **0** | NOPs |
| $3F00   | (regular code) | rom_bank **2** | rom_bank **1** |
| $3F30   | local helper | regular routine | **BANK SWITCH wrapper (sets rom_bank 3)** |

Each wrapper at $3E00 / $3E80 / $3F00 in different banks reads inline
target data after RST $XY, then sets a specific rom_bank, RETs
to inline target. Target's RET pops the wrapper's continuation,
the wrapper's "exit half" (in the OTHER bank — same PC, different
bytes) sets the original rom_bank back, RETs to caller.

So the supervisor uses RST $20 to call bank 2 routines from bank 0,
RST $28 to call bank 1 from bank 0, etc.

#### 2. Alt-ROM trampoline at $007B (verified)

- Main ROM (banks 0+1) $007B: `ED 91 8C 80` (alt-ROM ENABLE)
- Alt-ROM (enAltZX) $007B: `ED 91 8C 00` (alt-ROM DISABLE)

Same PC, different code by ROM context — **clever protocol**.
Supervisor calls $007B (in main ROM) to enable alt-ROM, then later
$007B (now in alt-ROM) to disable it.

#### 3. NR_8E semantics (verified by agent 2 against VHDL)

NR_8E,N for N=$00..$03 directly sets rom_bank=N via simultaneous
update of port_7FFD bit 4 and port_1FFD bit 2:
- $8E,$00 → rom_bank = 0
- $8E,$01 → rom_bank = 1
- $8E,$02 → rom_bank = 2 ★ (used by RST $20 wrapper at $3E00)
- $8E,$03 → rom_bank = 3 ★ (used by BANK SWITCH wrapper at $3CFC)
- $8E,$7A → rom_bank = 2 + bank 7 in slot 7 (used by $1F01 setup)

jnext's `Mmu::write_nr_8e()` is VHDL-faithful (zxnext.vhd:3662-3734).

#### 4. AUTOMAP-NOP-sled wrapper at DivMMC $0009-$000D (decoded)

```
$0009: LD (DE),A  ; sysvar update (DE = some addr, A = data)
$000A: DEC B      ; B--, sets F based on result (★ encodes target low byte)
$000B: POP HL     ; restore HL from saved state
$000C: PUSH AF    ; pushes A:F onto stack as target
$000D: JP $3364   ; into NOP sled to $3D00 RET → JP A:F
```

The AUTOMAP-NOP-sled SENTINEL ($C9 RET at $3D00) is **installed at
boot** by bank 2 PC=$1F01-$1F28:

```
$1F01: NEXTREG $8E,$7A     ; rom_bank=2, bank 7 in slot 7
$1F05: LD HL,$1F13         
$1F08: LD DE,$ED27         
$1F0B: LD BC,$0016         
$1F0E: LDIR                ; copy 22 bytes to slot 7 RAM
$1F10: JP $ED27            ; jump into RAM-resident copy
;; (the copied code:)
$ED27: LD A,$81; OUT ($E3),A    ; DivMMC MAPRAM=1
       LD A,$C9; LD ($2009),A   ; install RET at $2009
       LD A,$80; OUT ($E3),A    ; MAPRAM=0, CONMEM still on
       LD A,$C9; LD ($3D00),A   ; ★ install RET at $3D00 (the SENTINEL!)
       XOR A; OUT ($E3),A       ; deactivate DivMMC
       RET
```

Both jnext and CSpect run this setup; jnext's slot 1 dump confirms
$C9 at $3D00. So sentinel install is correct.

### Real divergence — supervisor's path forks at NR_8C trampoline

When the supervisor at $3E97 RETs (after NR_8E,$01 set rom_bank=1)
and lands at $007B (alt-ROM enable), alt-ROM gets enabled. Then RET
at $007F lands at $3E93.

In jnext, $3E93 in alt-ROM page $0E (= enAltZX bank 1 low half) is
`$00` (NOP, all-zeros). enAltZX file $3E80-$3EFF and $7E80-$7EFF
are entirely zeros. So PC falls through 7000+ NOPs from $3E93 →
slot 1 → slot 2 RAM → reaches $5B00 (LDIR'd bank-flip wrapper).

The wrapper at $5B00 RETs to popped value $0000 (mem[$5BFF,$5C00]
were uninitialized = 0). Slot 0 with alt-ROM enabled = enAltZX page
$0E (NOPs). PC=$0000-$0008 NOPs. AUTOMAP fires at M1 fetch $0008.
Slot 0 = DivMMC. PC=$0009-$000D wrapper executes with A=$1F, B=$25.
DEC B → F=$22. PUSH AF target=$1F22. JP $3D00 → JP $1F22 (SD-SPI
command-send helper). Helper sends `3B 00 00 FF BF 24` on port $EB
— invalid SD command (bit 6 = 0) — busy-loop forever.

### Why jnext diverges from CSpect — NR state at boot start

CSpect's `nrdump.raw` shows:
- NR_03 = $33 (jnext bypass writes $B3) ★
- NR_07 = $07 (jnext bypass writes $03) ★
- NR_82 = $82 (jnext bypass writes $DA) ★ — DECODE_INT0
- NR_85 = $48 (jnext bypass writes $01) ★ — DECODE_INT3

**Significant divergence in 4 NextREG values.** These control:
- NR_03: machine type + config_mode FSM
- NR_07: turbo CPU speed (jnext sets 28MHz, CSpect 14MHz)
- NR_82-$85: peripheral hardware-enable masks (port-decode gates)

The bypass synthesizes these "post-tbblue.fw handoff" values from
documentation, but the documentation might be wrong, OR CSpect's
dump is from a later moment than handoff and includes supervisor's
own NR writes. Either way, jnext's starting NR state isn't
equivalent to what tbblue.fw + supervisor produce in CSpect.

### Concrete next-session experiments

1. **Align bypass init NR values with CSpect dump**: write NR_03=$33,
   NR_07=$07, NR_82=$82, NR_85=$48 in bypass init. See if boot path
   changes.
2. **Trace jnext NR state at the same observable point** (e.g., first
   $3E93 hit) and compare against CSpect dump byte-by-byte. Probe 26
   already shows mmu state matches; add NR_82/$83/$84/$85/$8C state
   to probe.
3. **Check if NR_82/$85 differences gate port behavior** that
   matters during boot. NR_82 bit 1 = port DFFD enable; NR_82 bit 6
   = ?. Search VHDL for the exact bit semantics.
4. **Disassemble enNextZX bank 0 PC=$01D4-$01F0 area** to understand
   what the supervisor does AFTER the first RST $20 ; DW $1F01 (= the
   SENTINEL install routine). The next instructions might explain
   why subsequent flow takes wrong path.

### Probes added in this final round

- Probe 25: one-shot register snapshot at PC=$000C (the AUTOMAP-sled PUSH AF).
- Probe 26: 5-shot mmu+peek snapshot at PC=$3E93 (slot 1 mapping check).
- Probe 27: NEXTREG $8C tracer (alt-ROM toggle log).

### Branch HEAD: 977a56a (9 commits this session)

Files generated:
- `/tmp/g46b-sp-trace.log` — 1.95M-line SP trace.
- `/tmp/g46b-cpuinst.log` — 158M-line cpu_inst trace.
- `/tmp/g46b-p27.log` — alt-ROM toggle log.

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
