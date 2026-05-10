# Pass-17 — NMI / Multiface / Port-decode / NextREG audit

Branch: `task2/verify17-nmi-mf-port` (off integration HEAD `f718876`)
Worktree: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify17-nmi-mf-port`
Build: Release-mode CMake; ctest 38/38 PASS, FUSE 1356/1356 PASS.
Methodology: blind protocol — read VHDL line-by-line for the entire NMI source pipeline (zxnext.vhd:2050-2170 + 3819-3898 + 5083-5100 reset block + 5871-6293 read mux), the multiface entity (multiface.vhd:1-197), the port decode (zxnext.vhd:2540-2742), and every NR write/read handler in the emulator. Cross-checked against the converged Memory subsystem.

## Findings summary

3 findings — all class-(b). No class-(a), no class-(c), no class-(d).

| ID            | Title                                                            | Class | VHDL ref                     | Source ref                          |
|---------------|------------------------------------------------------------------|-------|------------------------------|-------------------------------------|
| V17-NMP-01    | NR 0xB8/0xB9/0xBA/0xBB readback returns 0x00 instead of reset    | (b)   | zxnext.vhd:5087-5090, :6217-6227 | src/core/emulator.cpp:2461-2487 (pre-fix) |
| V17-NMP-02    | port_FE handler mask too narrow (LSB-only, not bit-0)            | (b)   | zxnext.vhd:2582              | src/core/emulator.cpp:3148 (pre-fix)      |
| V17-NMP-03    | port_FF handler mask too narrow (full 16-bit, not LSB-only)      | (b)   | zxnext.vhd:2540-2571,:2583   | src/core/emulator.cpp:3204 (pre-fix)      |

---

## V17-NMP-01 — NR 0xB8/0xB9/0xBA/0xBB readback after reset returns 0x00

### VHDL spec

`zxnext.vhd:5087-5090` (master reset block, fires unconditionally on `i_reset='1'`):

```vhdl
nr_b8_divmmc_ep_0       <= X"83";
nr_b9_divmmc_ep_valid_0 <= X"01";
nr_ba_divmmc_ep_timing_0 <= X"00";
nr_bb_divmmc_ep_1       <= X"CD";
```

Read mux at `zxnext.vhd:6217-6227` returns each storage byte verbatim:

```vhdl
when X"B8" => port_253b_dat <= nr_b8_divmmc_ep_0;
when X"B9" => port_253b_dat <= nr_b9_divmmc_ep_valid_0;
when X"BA" => port_253b_dat <= nr_ba_divmmc_ep_timing_0;
when X"BB" => port_253b_dat <= nr_bb_divmmc_ep_1;
```

### Bug description

Pre-fix, `src/core/emulator.cpp:2461-2464` registered only WRITE handlers for NR 0xB8-0xBB (forwarding to `divmmc_.set_entry_points_0()` etc.). With no read_handler installed, `NextReg::read()` (src/port/nextreg.cpp:408-414) fell through to `regs_[reg]`, which `regs_.fill(0)` in `NextReg::reset()` had cleared to 0x00. So a Z80 `IN A,(0x253B)` after writing 0xB8 to 0x243B returned 0x00 instead of the VHDL-spec 0x83.

The DivMmc subsystem itself stores the correct defaults in `entry_points_0_=0x83`, `entry_valid_0_=0x01`, `entry_timing_0_=0x00`, `entry_points_1_=0xCD` (`src/peripheral/divmmc.h:342-345`, also re-applied in `divmmc.cpp:43-46` reset). But the NextReg-side cache was dark.

### Observable impact

Firmware that polls NR 0xB8 after reset to verify the configured automap entry points sees 0x00 (no entry points enabled at any RST address) instead of 0x83 (RST 0 + RST 0x38 enabled — the VHDL default). This silently disables auto-entry behaviour for any code path that reads-then-acts on NR 0xB8.

### Fix

Register read_handlers that pull live from the DivMmc subsystem, mirroring the same pattern used for NR 0x12 / NR 0x13 (which pull from Layer2):

```cpp
nextreg_.set_read_handler(0xB8, [this]() -> uint8_t { return divmmc_.entry_points_0(); });
nextreg_.set_read_handler(0xB9, [this]() -> uint8_t { return divmmc_.entry_valid_0(); });
nextreg_.set_read_handler(0xBA, [this]() -> uint8_t { return divmmc_.entry_timing_0(); });
nextreg_.set_read_handler(0xBB, [this]() -> uint8_t { return divmmc_.entry_points_1(); });
```

### Discriminative test

`RST-13` in `test/nextreg/nextreg_integration_test.cpp` — reads NR 0xB8/0xB9/0xBA/0xBB after `Emulator::init()` and asserts the bytes equal 0x83/0x01/0x00/0xCD.

---

## V17-NMP-02 — port_FE handler matches LSB==0xFE, not bit-0==0

### VHDL spec

`zxnext.vhd:2582`:

```vhdl
port_fe <= '1' when cpu_a(0) = '0' else '0';
```

The ULA port decode is the standard ZX Spectrum 48K rule: any port with bit 0 = 0 routes to the ULA. This includes `OUT (0xFC), A`, `OUT (0xF8), A`, etc. — used by some early Spectrum software for border tricks.

`port_fe_wr <= iowr AND port_fe` (line 2711) → border / EAR / MIC update on any even-port write.

### Bug description

Pre-fix, `src/core/emulator.cpp:3148` registered the port_FE handler with mask `0x00FF` / value `0x00FE` — requiring the LSB to equal exactly 0xFE. So `OUT (0xFC), A` would not invoke the handler — border / EAR / MIC stayed unchanged.

The existing test `REG-01` in `test/port/port_test.cpp` only exercised LSB==0xFE addresses (0xFEFE, 0x01FE, 0x00FE) so this gap was untested.

### Observable impact

Software using OUT to even ports other than 0xFE (the border trick is documented for many older games) sees no border / EAR / MIC update on jnext, but real hardware (and CSpect / Fuse) does the update. Visual-only divergence on uncommon code paths.

### Fix

Change the handler mask to `0x0001` / value `0x0000` — match any port with bit 0 = 0. The most-specific-wins dispatch in `PortDispatch::write()` ensures more-specific handlers (which all have odd LSBs in the active jnext set) still win where applicable.

```cpp
port_.register_handler(0x0001, 0x0000, [...read...], [...write...]);
```

### Discriminative test

`REG-01b` in `test/port/port_test.cpp` — writes border to ports 0xFC, 0xF8, 0x4242 and asserts the border updated each time.

---

## V17-NMP-03 — port_FF handler requires full 16-bit match, not LSB==0xFF

### VHDL spec

`zxnext.vhd:2540-2571` (LSB case statement on `cpu_a(7:0)`):

```vhdl
when X"FF"  => port_ff_lsb <= '1';
```

`zxnext.vhd:2583`:

```vhdl
port_ff <= '1' when port_ff_lsb = '1' else '0';
```

`port_ff_wr <= iowr AND port_ff AND port_ff_io_en` (line 2714). High byte is irrelevant — any port with LSB == 0xFF routes to the Timex screen-mode register.

### Bug description

Pre-fix, `src/core/emulator.cpp:3204` registered the port_FF write handler with mask `0xFFFF` / value `0x00FF` — requiring the FULL 16-bit address to equal exactly 0x00FF. Software using `OUT (0x12FF), A` would update Timex screen mode on real hardware but not in jnext. The pre-fix comment "(full 16-bit match)" acknowledged the deviation but didn't justify it against VHDL.

### Observable impact

Any Z80 code that uses a high-byte address paired with LSB 0xFF to write the Timex screen-mode register (e.g. for Hi-Res / Hi-Color / Alt-display selection) silently fails to commit on jnext. This is uncommon but spec-correct on real hardware.

### Fix

Change the handler mask to `0x00FF` / value `0x00FF` — LSB-only match. The handler only registers a write callback (read is `nullptr`), so reads still fall through to the floating-bus default which already handles LSB==0xFF correctly.

```cpp
port_.register_handler(0x00FF, 0x00FF, nullptr, [...write...]);
```

### Discriminative test

`REG-02b` in `test/port/port_test.cpp` — writes 0x06 (hi-res mode) to port 0x12FF and asserts the Timex screen_mode register reflects bits 2:0 = 6.

---

## Verification

Build: Release-mode CMake `-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`.
Tests:
- `ctest --test-dir build`: 38/38 PASS.
- `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 PASS.

All commits: `cc07618` (V17-NMP-01), `e4c04ab` (V17-NMP-02), `2949dc6` (V17-NMP-03).

## Cross-cutting families surveyed (no findings)

The Pass-17 audit explicitly looked for the recurring families catalogued in prior passes:

- **NR cache-leak** (config-mode-gated bits stored raw): NR 0x06 b2, NR 0x0A b7:5, NR 0x05 b2, NR 0x11, NR 0x03 — all already canonicalised by their write_handlers (V11-NMP-02, V11-NMP-03, V13-NMP-01).
- **Multi-writer fan-out**: port_e3_reg(6) (port-0xE3 OR-set + NR 0x09 bit 3 clear), port_ff_reg(6) (port-0xFF + NR 0x22 + NR 0xC4) — both already correctly fanned out (V12-NMP-01).
- **Write-only NR readback**: NR 0xC0 bit 4 (forced 0), NR 0x09 bit 3 (forced 0), NR 0x22 bits 6:3 (forced 0), NR 0xC4 bits 6:2 (forced 0) — all read_handlers compose correctly.
- **Default-FF**: VHDL `when others => port_253b_dat <= (others => '0')` (line 6286-6287) means unimplemented NRs return 0x00 in jnext via `regs_[reg]` cache (which is zeroed by `regs_.fill(0)`). Match VHDL.
- **load_state shadow re-push**: NmiSource::load_state and Multiface::load_state both use direct member assignment, no setter side effects.
- **Port-decode AND-mask vs OR-mask**: V16-NMP-02 already routed 33 sites through `effective_internal_port_enable`; this pass added no new sites. ContentionModel's `port_7ffd_io_en` / `port_ulap_io_en` shadows are correctly fanned out via `propagate_effective_port_enables()`.
- **NMI assert/deassert with EI / HALT / DI**: Z80Cpu::request_nmi() correctly handles HALT (bumps PC by +1) and saves PC for NR 0xC2/0xC3 capture. nmi_pending_ is one-shot, cleared on entry to fuse_z80_nmi.
- **RETN semantics**: FUSE Z80 core's `fuse_z80_nmi` correctly pushes PC and clears IFF1; RETN restores IFF1 from IFF2. The `z80_retn_seen_28` pulse correctly fires both Multiface::on_retn_seen() (unconditional) and DivMmc::on_m1_retn_delay (gated on `!multiface_.is_active()` per VHDL :4111).
- **`effective_internal_port_enable` audit**: All 33 port-decode sites added in V16-NMP-02 are correctly routed; no new sites missed.

## Bugs noted but DEFERRED to future passes / out of scope

- **Multiface mf_type=10 lossy serialisation**: `Multiface::load_state` reconstructs `mf_type_` from mode booleans, mapping `mode_128` to 01 — losing distinction between mf_type=01 and mf_type=10. Documented in multiface.cpp:380-385. Class-(c) at most; would require schema bump.
- **Port 1FFD motor_n separate write path**: VHDL :3755-3757 motor_n updates use a manual decode (`port_xffd AND cpu_a(13:12)="01" AND iowr AND not port_fd_conflict_wr`) that bypasses `port_1ffd_io_en` (NR 0x82 b3). jnext's port-1FFD handler gates the entire write on NR 0x82 b3, so motor_n is NOT updated when the port is gated off. Only observable when NR 0x81 b3 (FDC) is on AND NR 0x82 b3 is off — extremely unusual configuration. Class-(c).
- **Profi/SD DAC handlers use mask 0xFFFF**: VHDL `port_dac_*` decodes use port_*_lsb (LSB-only). jnext uses full 16-bit match per a deliberate compromise (avoids alias with sprite pattern port 0x5B). Documented in `emulator.cpp:3464-3467`. Class-(c) per the original rationale.
- **Stackless NMI (NR 0xC0 bit 3)**: Stored only (F-deferred). The `z80_stackless_nmi` Z80N command path (VHDL :2052) is not implemented. Existing class-(d) item already on the architectural backlog.
- **`inject_hotkey_m1`/`inject_hotkey_drive` only feed NR 0x10 readback**: The dedicated MF/DivMMC NMI buttons in real hardware feed BOTH `i_SPKEY_BUTTONS` (NR 0x10 readback) AND `i_SPKEY_FUNCTION(9)/(10)` (via the membrane FSM, which OR's the button into F9/F10). jnext's test injectors only set the NR 0x10 readback path; tests must additionally call `on_hotkey_f9_mf_nmi()` to fire the actual NMI. Class-(c) ergonomics.
