# Task 13 — Plan: Bypassing TBBLUE.FW

All citations below are from repos at `/home/jorgegv/src/spectrum/tbblue` and `/home/jorgegv/src/spectrum/jnext` unless stated otherwise. VHDL references are to `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/` via the pre-existing in-emulator comments.

---

## 1. Goal restatement in emulator terms

"Bypassing tbblue.fw" means: JNEXT performs the work that `/home/jorgegv/src/spectrum/tbblue/src/firmware/app/src/boot.c` would have done on the Z80 side, then hands off to NextZXOS at PC=0x0000 with the post-RESET_SOFT state pre-established.

On real hardware, the boot chain is:

1. **FPGA boot ROM** (`nextboot.rom`, 8 KB overlay at 0x0000-0x1FFF) loads the FW loader (`src/firmware/firmware/src/main.c` layout — a 512-byte `fwMap` header plus concatenated 512-byte blocks per module) and switches into the boot module (`FW_BLK_BOOT`).
2. **`boot.c::main` (boot.c:445-598)** runs under config_mode=1 (VHDL `nr_03_config_mode` default '1' at power-on, mirrored in `src/core/emulator.cpp:47-51` via `NextReg::reset()`). It:
   - Initialises NextREGs (REG_TURBO, REG_PERIPH2, VDP, keymap, etc. — boot.c:448-458).
   - Writes NR 0x03 = 0x00 via `disable_bootrom()` (misc.c:48-52) — this just disables the boot ROM overlay (VHDL zxnext.vhd:5122, mirrored in `src/core/emulator.cpp:467-478`); low3=0 → config_mode unchanged.
   - Reads `config.ini`, `menu.ini` / `menu.def` from SD (boot.c:464, config.c:294-406).
   - Paints the "Press SPACEBAR for menu" screen (boot.c:247-277, 494-541).
   - `load_keymap()` → copies 1 KB to SRAM page 0x00 via config-mode writes then pushes through NR 0x28/0x29/0x2A/0x2B registers (boot.c:409-429).
   - `load_roms()` (boot.c:86-176) → SRAM population detailed in §2.
   - `init_registers()` (boot.c:279-407) → sets NR 0x05-0x0A (peripheral), NR 0x82-0x85 (port decoders), REG_PERIPH5 mftype bits.
   - Writes `REG_MACHTYPE = 0x80 | (mode+1)<<4 | (mode+1)` (boot.c:582-583) — for mode=2 (+3): 0xB3. This is the write that commits machine type (gated on config_mode=1 per VHDL zxnext.vhd:5137, mirrored in `src/port/nextreg.cpp:57-73`).
   - Writes NR 0x02 = RESET_SOFT (0x01) or RESET_SOFT|RESET_ESPBUS (0x81) (boot.c:587-596). This triggers the soft reset in `src/core/emulator.cpp:446-455` → `Emulator::soft_reset()` at `src/core/emulator.cpp:1934-1988`.

Bypassing means doing all of the above — except (c) the menu parse, (d) the `config.ini`/`menu.ini` handling, (e) the SPACEBAR wait, and (f) the boot-screen paint — directly in host-side C++.

Two possible handover modes:

- **(a) Synthesised RESET_SOFT** — set up SRAM and NextREGs identically, then call `Emulator::soft_reset()`. Works with the existing Task 11 plumbing.
- **(b) Pre-baked post-RESET_SOFT state** — skip the soft reset entirely, set PC=0x0000 with the machine already in NextZXOS-ready state from `init()`. Requires extending `init()` with a "post-firmware" entry point.

Recommendation: **(a)**. `Emulator::soft_reset()` (`src/core/emulator.cpp:1934-1988`) already does exactly the right thing — preserves SRAM, preserves `boot_rom_en` (already off), runs `init(cfg, preserve_memory=true)` to drop CPU/MMU/NextReg FFs but keep SRAM. This is the Task 11 Branch 3 work; bypassing tbblue.fw is precisely its intended caller, now issued from host code instead of from the firmware. (b) would require a parallel code path and lose VHDL-faithfulness.

## 2. What tbblue.fw actually does before RESET_SOFT

### 2.1 Early NextREG writes (before load_roms)

| NR | Value | Source | Purpose |
|----|-------|--------|---------|
| 0x07 | 0x03 | boot.c:448-449 | 28 MHz for boot |
| 0x06 | 0xA0 | boot.c:457-458 | PS/2 mode=keyboard initially |
| 0x02 | 0x80 | config.c:282-283 (`reset_settings` called from `load_config`) | RESET_ESPBUS hold |
| 0x05 | 0xfa+(opc) | config.c:286-288 | joystick=keyjoy |
| 0x28/0x29/0x2A/0x2B | keyjoy defaults | config.c:255-272 (`load_keyjoys`) | ×2 passes (j1 + j2) |
| 0x03 | 0x00 | misc.c:48-52 (`disable_bootrom()`, called at boot.c:461) | boot ROM off |
| 0x11 | 0x80\|timing | config.c:212-213 (`set_video_mode` via `update_video_settings` at boot.c:480) | video timing |
| 0x09 | 0..3 | config.c:237 | scanline setting |

Palette init (NR 0x40..0x44, 0x4A..0x4C): boot_screen painting is in `display_bootscreen()` (boot.c:247-277) and uses L2 palette writes through `fwRead` seeking block `FW_BLK_SCREENS` (block 8, boot.c:252). The screen payload includes pixels, attributes and a 227-byte Layer-2 palette (`FW_L2_PAL_SIZE`, hardware.h:201). Bypass mode does NOT need to paint the boot screen, so this whole sequence is SKIPPABLE in bypass mode.

The trace fingerprint from `project_nextzxos_firmware_stall_2026_04_18.md` (NR 0x03=0xB0, NR 0x2B ×24, NR 0x04=0x02/0x06/0x00, then silence) matches the early prologue up to part of palette init or early config paging, well before `load_roms()` — confirming the bypass-mode boundary is upstream of the stall.

### 2.2 load_roms() — the actual SRAM population (boot.c:86-176)

`loadFile(destpage, numpages, blocklen)` (boot.c:60-84) writes `blocklen` bytes per iteration starting at address 0x0000, with `REG_NUM=REG_RAMPAGE` then `REG_VAL=destpage++` between iterations. Because config_mode=1 is in effect, those writes land in SRAM per the routing rule at `src/memory/mmu.cpp:160-167`: `page = (nr_04_romram_bank_ << 1) | slot`, so a 16 KB write to 0x0000-0x3FFF with bank=N fills physical RAM pages `(N*2)` and `(N*2)+1`.

For the default `menu=ZX Spectrum Next (standard),2,8,enNextZX.rom,enNxtmmc.rom,enNextMF.rom` (menu.def:1):

| File | RAMPAGE constant | Physical SRAM pages | Size | Call site |
|------|------------------|---------------------|------|-----------|
| `enNxtmmc.rom` (DivMMC) | RAMPAGE_ROMDIVMMC = 0x04 (hardware.h:168) | page 0x08 only (lower half of bank 0x04) | 8 KB | boot.c:105-108, numpages=1, blocklen=8192 |
| `enNextMF.rom` (Multiface) | RAMPAGE_ROMMF = 0x05 (hardware.h:169) | page 0x0A only (lower half) | 8 KB | boot.c:142-153, numpages=1, blocklen=8192 |
| `enNextZX.rom` (Spectrum/Next ROM, 64 KB since mode=2 → i=4) | RAMPAGE_ROMSPECCY = 0x00 (hardware.h:167) | pages 0x00..0x07 | 64 KB | boot.c:155-175, numpages=4, blocklen=16384 |

Note: `loadFile` increments `destpage` once per iteration, but `blocklen=16384` covers *two* 8 KB pages per iteration (bank index `destpage` maps to pages `destpage*2` and `destpage*2+1`). So `numpages=4` with `destpage` stepping 0x00..0x03 actually writes to SRAM pages 0x00..0x07 — the full 64 KB.

### 2.3 Keymap (boot.c:409-429)

- `loadFile(RAMPAGE_ROMSPECCY, 1, 1024)` → 1 KB of keymap into SRAM pages 0x00-0x01 (scratch usage — it gets overwritten by `load_roms()` which follows).
- Then shifted out via NR 0x28 (KMHA)=0, NR 0x29 (KMLA)=0, and a 1024-iteration loop of NR 0x2A/0x2B writes.
- **Bypass mode**: if we can reach into the internal keymap state (if any), we pre-program it; otherwise skip (jnext's own input layer handles keys host-side — the Z80 side of keymap only matters if a Z80 program reads the keymap regs).

### 2.4 init_registers() — peripheral defaults (boot.c:279-407)

Builds NR 0x05-0x0A from `settings[]` and writes NR 0x82-0x85 port-enable decoders with per-machine-mode masks (e.g. mode=2/+3 → `hwenables[0] = 0xda` at boot.c:390). **These are the load-bearing values we need to replicate.** Exact default-settings values are in `settingDefaults[]` at config.c:93-124.

For the default menu entry (+3 mode) with defaults:
- NR 0x05 (PERIPH1) = 0x01 | (1<<6) | (3<<4) = 0x71 (joystick1=1 K-joy, joystick2=3 sinclair+fire, 5060=0, scandoubler=1)
- NR 0x06 (PERIPH2) = 1 (psgmode) | 0x80 (turbokey) | 0x04 (PS2=1) = 0x85
- NR 0x07 already 0x03 (28 MHz).
- NR 0x08 (PERIPH3) = ...
- NR 0x09 (PERIPH4) = 0 (scanlines) | 0 (hdmisound=1 → bit2 NOT set) = 0
- NR 0x0A (PERIPH5) = 1 (mousedpi) | 0x10 (DivMMC=0 per default but menu forces=1 via boot.c:562-573) → becomes 0x11 | mftype<<6 (mftype=0 for +3 mode per boot.c:125-128 since mode==2)
- NR 0x82 = 0xda (mode=2), NR 0x83 = 0x00 or 0x02 (DivPorts=1 → 0x09 on 83, but mode=2 specific), NR 0x84 = 0x01, NR 0x85 = 0x0F (enables) — reset_type bit 7 clear by default.

See `init_registers()` (boot.c:281-406) — the exact bit assembly.

### 2.5 Machine type commit + RESET_SOFT

- NR 0x03 = 0xB3 for mode=2 (= 0x80 | 0x30 | 0x03). Bits[2:0]=011 ∈ {001..110} → triggers `apply_nr_03_config_mode_transition` clearing config_mode to 0 (VHDL 5147-5151, `src/port/nextreg.cpp:57-73`), and the `machine_type` latch commits since config_mode was 1 at write time (VHDL 5137). Machine-type latch in jnext is advisory — `cfg.type` stays at `ZXN_ISSUE2` throughout.
- Pause loop (boot.c:586).
- NR 0x02 = 0x01 (RESET_SOFT) or 0x81 (RESET_ESPBUS|RESET_SOFT).

### 2.6 Final handover contract (state at the moment RESET_SOFT latches)

- **Config_mode**: 0 (cleared by the NR 0x03=0xB3 write just before).
- **boot_rom_en**: 0 (cleared at boot.c:461 via `disable_bootrom`).
- **SRAM**: pages 0x00..0x07 = 64 KB enNextZX.rom, page 0x08 = enNxtmmc.rom (DivMMC), page 0x0A = enNextMF.rom (Multiface).
- **rom_in_sram**: 1 (VHDL zxnext.vhd:3052; `Mmu::set_rom_in_sram(true)` already set by `Emulator::init()` at `src/core/emulator.cpp:1186-1195`).
- **Port 0x7FFD / 0x1FFD**: both 0x00 after the soft reset — MMU slot map resets via `Mmu::reset()` (`src/memory/mmu.cpp:18-42`) which re-seeds slots to the canonical 128K layout with slot 0/1 = ROM bank 0 (i.e. physical ROM page 0 and 1 — which is SRAM pages 0,1 under rom_in_sram=1, which now contain enNextZX.rom low 16 KB).
- **NR 0x82-0x85**: preserved across soft reset (NR 0x85 bit 7 = 0 means the trio survives; `src/port/nextreg.cpp:8-42`).
- **NR 0x03 machine type**: preserved in `regs_[0x03]` as 0xB3 (the register write happened before reset; `NextReg::reset()` doesn't clear it specifically since we ZERO the array and then re-seed — actually we DO clobber it: `regs_.fill(0)` then `regs_[0x03] = 0x00`). This needs attention — see §6.
- **CPU**: PC=0, SP=0xFFFF (actually SP=0 after reset, NextZXOS autoexec.1st sets it), IFF=0, all regs zeroed per Z80 reset semantics.
- **First fetch**: Z80 reads from MMU slot 0 (0x0000-0x1FFF) → points at SRAM page 0 → enNextZX.rom first 8 KB.

### 2.7 Menu / keymap / config.ini

These are NEXT-Z80-side only and purely cosmetic for bypass mode:

- `config.ini` sets `settings[]` defaults (config.c:294-343) — we'd need CLI flags or a small INI parser to honour them.
- `menu.ini` / `menu.def` lets user pick which machine mode + ROM file. For bypass mode v1, pick the default entry (first `menu=` line or CLI-overridden).
- SPACEBAR menu: can't reach; feature is LOST in bypass mode. Not a regression in practice — most users boot straight past it.

## 3. What JNEXT must do instead

Given the SD image is already mounted (since Task 9 Stage C) and the Next machine is configured, the bypass sequence in `Emulator::init()` (or a new `bypass_tbblue_firmware()` method called just before returning) is:

1. **Parse menu.def / menu.ini from SD** (host-side) to pick the default entry (`settings[eSettingMenuDefault]`=0 by default → first entry).
   - For v1 (Branch 2 below), accept `--next-rom enNextZX.rom`, `--divmmc-rom enNxtmmc.rom`, `--mf-rom enNextMF.rom` from CLI — files supplied on the host FS, bypassing the SD parse entirely.
   - For v2 (Branch 4 below), locate them on the SD image via a host-side FAT32 reader and `/machines/next/` path (NEXT_DIRECTORY hardware.h:57).
2. **Read the three ROM files** into host buffers.
3. **Populate SRAM pages directly** using `ram_.page_ptr()` (`src/memory/ram.cpp`) — NO config_mode write path, NO Z80 involvement:
   - SRAM pages 0x00..0x07 ← 64 KB Spectrum/Next ROM (for mode=2). For 128K mode (menu=1): pages 0x00..0x03 ← 32 KB. For 48K (menu=0): pages 0x00..0x01 ← 16 KB. We already use `ram_.page_ptr()` for the ROM-in-SRAM seed at `src/core/emulator.cpp:1187-1192`, which we'd skip in bypass mode (since our manual copy supersedes it).
   - SRAM page 0x08 ← 8 KB DivMMC ROM (if enabled).
   - SRAM page 0x0A ← 8 KB Multiface ROM (if enabled).
4. **Program the NextREG state** to match the `init_registers()` outputs for the chosen menu-mode + settings defaults:
   - NR 0x03 = 0xB3 (mode=2), 0xA2 (mode=1), 0x91 (mode=0 48K), 0xD3 (mode=3 pent) via `nextreg_.write(0x03, ...)` which will also flip config_mode.
   - NR 0x05, 0x06, 0x08, 0x09, 0x0A, 0x82, 0x83, 0x84, 0x85 per `init_registers()` assembly rules.
   - NR 0x07 = 0x00 (3.5 MHz — boot.c:448 sets 28 MHz during boot but that's firmware-only; NextZXOS sets its own speed).
5. **Disable boot ROM** (`mmu_.set_boot_rom_enabled(false)`) since the firmware would have done this at boot.c:461.
6. **Set `rom_in_sram=true`** (already done for Next machines at `src/core/emulator.cpp:1194`).
7. **Synthesise RESET_SOFT**: call `soft_reset()` directly. This re-runs `init(cfg, preserve_memory=true)` which:
   - Preserves SRAM (our populated ROMs stay).
   - Resets CPU (PC=0), MMU slots, peripherals.
   - Preserves NR 0x82-0x84 (if NR 0x85 bit 7 = 0, which is the default — `src/port/nextreg.cpp:34-42`).
   - The `nr_03_config_mode_ = true` re-assertion on reset (`src/port/nextreg.cpp:51`) is arguably WRONG per VHDL for soft resets (VHDL has no reset branch clearing this), but see §6 Risks.

Alternative: instead of calling `soft_reset()`, skip it and leave PC=0 from `init()`'s CPU reset. The rest of the state is already consistent. This is simpler but less VHDL-faithful — it doesn't exercise the same code path real firmware uses.

## 4. Tradeoffs — honest

**Pros:**
- No more Task 12 stall — the Z80 doesn't run tbblue.fw, so the `3-NR-0x04-then-silent` symptom documented in `project_nextzxos_firmware_stall_2026_04_18.md` is bypassed by construction.
- ~instant boot (< 1 frame vs. ~2-3 seconds of firmware + SPACEBAR prompt).
- Easier to debug NextZXOS-specific issues, because the state at PC=0 is host-controlled and predictable.
- Lets us boot NextZXOS even with partial/imperfect FatFs read semantics or with SD images that don't pass tbblue.fw's strict FAT32 check (project_nextzxos_task9_stagec.md Bug 1 note), provided we can still read the ROM blobs out of them host-side using a more lenient FAT32 library.

**Cons:**
- **Loss of config.ini / menu.ini**: user customisations for 50/60Hz, scandoubler, joystick mapping, DivMMC/MF enables, turbosound, DAC, mouse DPI, etc. — all lost. Mitigation: a corresponding JNEXT config file (e.g. `~/.config/JNEXT/nextboot.yaml`) or CLI flags mapping 1:1 to the ~29 entries in `settingName[]` at config.c:27-58. v1 can hard-code defaults; v2 adds CLI/config parsing.
- **Firmware-version drift**: the handover contract is empirically reverse-engineered from tbblue v1.44 boot.c. If tbblue v1.45+ adds new NRs to `init_registers()`, bypass mode silently diverges. Mitigation: (a) pin the tested tbblue version in docs; (b) gate bypass mode behind an explicit opt-in flag (not default); (c) add a regression test that boots both paths and compares NextREG state immediately post-reset.
- **Less VHDL-faithful**: jnext's stated goal is to be an authoritative implementation of the VHDL core. Bypass mode short-circuits the firmware Z80 path that is ITSELF just Z80 software running on the VHDL — so technically it's still VHDL-faithful at the hardware layer. But it means we're skipping real-system-integration coverage of tbblue.fw behaviour. Mitigation: keep the firmware boot path fully working (Task 11 + future Task 12) as the default, and treat bypass mode as a pragmatic "fast lane" for users who just want NextZXOS to boot.
- **Maintenance surface**: we now maintain two boot paths (firmware-run + host-bypass). The bypass path contains ~30 NR writes worth of replicated firmware logic; tbblue updates can invalidate it. Mitigation: document each value-setting rule with a `boot.c:LINE` citation so the mapping is auditable.

## 5. Proposed incremental branches

### Branch 1 — CLI plumbing only (lightweight, low risk)

- Add `--bypass-tbblue-fw` to `src/main.cpp` (around line 122-137) and a `bool bypass_tbblue_fw = false;` to `EmulatorConfig` in `src/core/emulator_config.h`.
- `src/core/emulator.cpp::init()` logs a one-line "bypass-tbblue-fw requested" at emulator-info level near the end of `init()` if the flag is set. No functional behaviour yet.
- Tests: unit test in `test/main_args_test.*` (or similar) that asserts the flag is parsed and the config field is set.
- Acceptance: flag present in `--help`, parsing robust, no regression in regression suite.

### Branch 2 — Host-side SRAM population via CLI-supplied ROMs

- New CLI: `--bypass-next-rom FILE` (64 KB or per-mode), `--bypass-divmmc-rom FILE` (8 KB), `--bypass-mf-rom FILE` (8 KB), `--bypass-mode [48k|128k|plus3|pentagon]` (default plus3 to match `enNextZX.rom`).
- New method `Emulator::bypass_populate_sram_from_files(...)` called from `init()` only when `cfg.bypass_tbblue_fw && !preserve_memory`. Reads files with `std::ifstream`, validates size by mode, memcpys into `ram_.page_ptr()` per §2.2 table. Logs each copy with page + byte count.
- Skips the existing seed loop at `src/core/emulator.cpp:1186-1193` (either via an `if` around the seed or by doing it anyway and letting the bypass copy overwrite — the latter is simpler and idempotent).
- **Does NOT yet issue RESET_SOFT or write NR 0x03/0x82-0x85** — just populates SRAM. User can still observe result via debugger and verify byte-for-byte that SRAM page 0 first 8 bytes match the ROM file.
- Tests: unit test that loads known bytes into 3 files, calls `bypass_populate_sram_from_files`, asserts `ram_.page_ptr(0)[0..7]` == expected.
- VHDL citations: zxnext.vhd:3052 (rom_in_sram), :1102 (config_mode at reset) — cite in the code comments.
- Acceptance: SRAM pages 0x00..0x07, 0x08, 0x0A contain the expected content when the flag and three ROM files are supplied.

### Branch 3 — NextREG post-firmware state + synthetic RESET_SOFT

- Add `Emulator::bypass_apply_post_firmware_state(mode)` that writes (via `nextreg_.write()` so handlers fire):
  - NR 0x07 = 3 (turbo=28MHz), NR 0x06 = 0xA0 (PS/2=keyboard).
  - NR 0x03 = 0x00 (mirrors `disable_bootrom`) — this clears boot_rom via the handler.
  - NR 0x05..0x0A per §2.4 rules using default settings[].
  - NR 0x82..0x85 per mode-specific masks (boot.c:367-400) and `hwenables[3] = 0` (bit 7 clear → port-enables stable across soft reset).
  - NR 0x03 = 0xB3/0xA2/0x91/0xD3 per mode — commits machine-type latch and clears config_mode.
- Call `soft_reset()` at the end.
- Placed AFTER SRAM population (Branch 2) in `init()`.
- Tests: unit tests asserting post-init NextREG state (regs cached correctly) and post-`soft_reset()` MMU slot pointers (slot 0 → `ram_.page_ptr(0)`).
- VHDL citations: zxnext.vhd:5137 (machine-type commit gate), :5147-5151 (config_mode transitions), :5052-5057 (NR 0x82-0x84 soft-reset semantics), :3052 (rom_in_sram), :3044-3050 (config_mode routing).
- Acceptance: running `./build/jnext --machine next --boot-rom ... --divmmc-rom ... --sd-card ... --bypass-tbblue-fw --bypass-next-rom enNextZX.rom --bypass-divmmc-rom enNxtmmc.rom --bypass-mf-rom enNextMF.rom --bypass-mode plus3 --delayed-screenshot ... --delayed-automatic-exit 15` produces a NextZXOS boot screen screenshot matching reference — AND a regression test comparing the screenshot to a checked-in reference.

### Branch 4 (optional) — Host-side FAT32 reader for TBBLUE.FW + menu.def

- Add a small FAT32 reader (e.g. integrate `fatfs` in a sandbox mode or use a minimal single-file FAT32 reader; there are several BSD-licensed options). Read `/machines/next/menu.def`, `/machines/next/config.ini`, and the referenced `.rom` files directly from the SD image host-side.
- Makes `--bypass-tbblue-fw` alone sufficient when combined with `--sd-card`.
- Tests: parse the canonical 1 GB fat32fix image, verify extracted files byte-match those already in `/tmp/nextzxos-extract/`.
- Acceptance: bypass mode boots NextZXOS off the canonical SD image with only the `--bypass-tbblue-fw` flag.

## 6. Risks & open questions

- **Q1 (VHDL)**: Does VHDL really preserve `nr_03_config_mode` across soft reset? Our `src/port/nextreg.cpp:47-51` sets `nr_03_config_mode_ = true` unconditionally on `reset()`. The comment at emulator.cpp:1954-1963 claims bootrom_en reset is gated by `if nr_03_config_mode = '1'`. If config_mode itself has no reset branch, then on soft reset it holds. If firmware wrote NR 0x03=0xB3 (clearing config_mode to 0) just before RESET_SOFT, real hardware enters the post-reset state with config_mode=0 but jnext enters with config_mode=1 — a divergence. This would cause the first Z80 ROM read in slot 0 to still route through the config_mode SRAM path (page `(0 << 1) | 0` = page 0, same physical page in this case, so functionally OK) but the write path is wrong: writes to ROM area would go to SRAM instead of being dropped. For bypass mode, we can work around by explicitly calling `nextreg_.write(0x03, 0xB3)` AFTER `soft_reset()` to force config_mode=0. **Needs VHDL verification**.
- **Q2 (VHDL)**: NR 0x03 `regs_[0x03]` is zeroed by `NextReg::reset()` (`src/port/nextreg.cpp:16,29`). But VHDL nr_03 machine-type commit latches to a SEPARATE signal — our emulator doesn't model that separate latch. The re-zeroing on reset could lose the machine-type register read-back. Is this visible via NR 0x03 read? **Needs check against zxnext.vhd:5137**.
- **Q3 (emulator state)**: After `soft_reset()`, does MMU slot 0 correctly point at `ram_.page_ptr(0)` given `rom_in_sram_=true` (re-asserted by `init()` at emulator.cpp:1194) and `slots_[0]=0xFF` (reset value) with `read_only_[0]=true`? Following `rebuild_ptr` at mmu.cpp:44-62: slot 0, `page=0xFF`, `read_only_=true` → `read_ptr_[0] = rom_in_sram_ ? ram_.page_ptr(0xFF) : rom_.page_ptr(0xFF)`. `ram_.page_ptr(0xFF)` — does the Ram (1792 KB = 224 pages of 8 KB) have a page 0xFF = 255? 1792KB / 8KB = 224 pages, so page 0xFF is INVALID and `page_ptr` likely returns nullptr → Z80 reads 0xFF from unmapped slot. This is a bug in my understanding OR in the code — the existing `map_rom_physical(0, 0)` called at `src/memory/mmu.cpp:40-41` right after the loop fixes slot 0 to page 0. So the re-seed from rom_ seed loop (emulator.cpp:1186-1193) plus the `map_rom_physical(0,0)` in `Mmu::reset()` do the right thing. **This should be re-verified at code time — traceability is marginal.**
- **Q4**: What's the correct SP / IM / border value at PC=0 handover? Z80 hardware reset: PC=0, SP=0xFFFF, I=0, R=0, IM=0, IFF1=IFF2=0. NextZXOS autoexec.1st assumes normal ROM-first boot so these shouldn't matter — the ROM itself sets SP. **Low risk**.
- **Q5 (FAT32 reader)**: is using a FAT32 library in the host worth the dependency? For Branch 4, yes; for Branches 1-3 we cheat with CLI-supplied files.
- **Q6 (menu.def parsing)**: does the default menu always land on the first entry (`settings[eSettingMenuDefault]=0` per config.c:110)? If `menuDefault` is in config.ini on the user's SD, we'd miss it. Bypass mode v1 should accept `--bypass-menu-index N` for override.
- **Q7**: alt-ROM pages 0x06/0x07 (RAMPAGE_ALTROM0/1) — does `enNextZX.rom` include an alt-ROM section, or is it pure Spectrum ROM? **Needs inspection of the 64 KB blob layout**. If yes, we need to populate them too; if no, they stay zero (unmapped, typical boot).
- **Q8 (screen)**: `display_bootscreen()` writes 0x2800 bytes to SRAM page 0 at offset 0x0000 AND 0x0100 bytes to page 1 at offset 0x3F00 (boot.c:250-257). This is overwritten by `load_roms()` for mode=2 (which writes 64KB starting at page 0). For mode=0 (48K, loads only 16 KB), the boot-screen residue could survive in pages 0x02+. For bypass mode we never paint the boot screen, so this is a non-issue.

## 7. Relationship with Task 12

Task 12 = diagnose why tbblue.fw stalls at 3 NR 0x04 writes before reaching `load_roms()`. Task 13 = bypass tbblue.fw entirely.

If Task 13 succeeds, Task 12 becomes **optional but not irrelevant**:

- Task 12 still matters for users who want the authentic firmware experience — SPACEBAR menu, cores, updater, per-machine .rom selection at runtime.
- Task 12 also tests the VHDL faithfulness of the config_mode + SRAM-routing + CMD17/18 paths that Task 11 + Stage C landed. Losing that coverage reduces our confidence in the subsystem boundary.
- However, the urgency drops: Task 13 gives users a working NextZXOS boot NOW, so Task 12 can be tackled on a more relaxed timeline.

Recommendation: **pursue Task 13 (do with caveats)** — it's a pragmatic win — but keep Task 12 on the roadmap rather than cancelled, and gate Task 13 behind `--bypass-tbblue-fw` so the default path remains the firmware-driven one.

---

### Critical Files for Implementation
- `/home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp` (bypass entry point inside `init()`, around lines 1186-1195 and near the end; uses existing `soft_reset` at 1934-1988)
- `/home/jorgegv/src/spectrum/jnext/src/core/emulator_config.h` (new `bool bypass_tbblue_fw` + bypass ROM paths + bypass_mode)
- `/home/jorgegv/src/spectrum/jnext/src/main.cpp` (CLI flag parsing around 118-137, config wiring at 193-197)
- `/home/jorgegv/src/spectrum/jnext/src/memory/ram.cpp` (direct `page_ptr()` population — no changes needed, just used)
- `/home/jorgegv/src/spectrum/jnext/src/port/nextreg.cpp` (post-firmware NR writes go through this; check `reset()` behavior around config_mode per Q1/Q2)
