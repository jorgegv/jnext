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

