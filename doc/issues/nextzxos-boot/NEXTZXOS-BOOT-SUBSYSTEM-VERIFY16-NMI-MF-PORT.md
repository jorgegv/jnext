# NEXTZXOS Boot Subsystem — Pass-16 Verify Audit (NMI + Multiface + Port + NextREG)

Branch: `task2/verify16-nmi-mf-port` off integration `267764b`.
Workspace: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify16-nmi-mf-port`.
Build: Release `cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`.
Tests: ctest 38/38 PASS, FUSE 1356/1356 PASS.

This is a BLIND audit (Pass 16). Did not read prior verify-pass reports.
VHDL oracle: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`.
Subsystem scope per prompt: `nmi_source`, `multiface`, `nextreg`, `port_dispatch`,
`emulator.cpp` slice for NMI/MF/Port-owned NRs, cross to `mouse`/`joystick`,
hotkey handlers.

Methodology summary: walked the VHDL NMI arbiter (zxnext.vhd:2087-2170),
the multiface entity (cores/zxnext/src/device/multiface.vhd, full 197 lines),
the NR 0x02 / 0xC0 / 0xC2 / 0xC3 / 0xD8 / 0xD9 / 0xDA paths, the
`nr_da_iotrap_cause` clear race, the priority cascade for 3+ simultaneous
NMI sources, the per-NR readback layout (every owned register), the
hotkey gate / strobe semantics, and the port-decode `internal_port_enable`
formula at zxnext.vhd:2392-2393.

Result: **2 findings**, both **class-(b)** (functional divergence,
low/no real-world impact in current jnext usage). No class-(a)
behavior-breaking bugs surfaced. No class-(c) leaks beyond what prior
passes already addressed.

---

## V16-NMP-01 — NR 0x10 readback bits 1:0 ignore live `i_SPKEY_BUTTONS` state (class-b) — **FIX LANDED**

**Status (2026-05-10)**: FIXED + 6 discriminative regression tests added.
NR 0x10 read_handler now recomposes bits 1:0 from `test_hotkey_m1_` /
`test_hotkey_drive_` (the active-high held-state booleans already used
by `nmi_assert_mf` / `nmi_assert_divmmc`); the existing
`inject_hotkey_m1` / `inject_hotkey_drive` setters become the
production-and-test seam for the SPKEY_BUTTONS line. Discriminative
revert (read_handler removed): 5/6 V16-NMP-01 rows FAIL pre-fix; all
6 PASS post-fix. Inadvertently exposed: prior `nr_10_coreid_ = 0x01`
in `Emulator::reset()` was clobbering coreid on every reset, contrary
to VHDL `nr_10_coreid` initial-only semantics (zxnext.vhd:1133, no
reset clause). Removed for VHDL fidelity. Test row TC-NR10-PRESERVE
now passes on the live-coreid path. Tests: ctest 38/38 + FUSE
1356/1356.



VHDL zxnext.vhd:5924 read mux for NR 0x10:

```
when X"10" =>
   port_253b_dat <= '0' & nr_10_coreid & i_SPKEY_BUTTONS(1 downto 0);
```

Per the entity port comment at zxnext.vhd:70:
`i_SPKEY_BUTTONS : in std_logic_vector(1 downto 0); -- raw state of nmi
buttons m1(0) & drive(1)`

Per zxnext_top_issue2.vhd:2281:
`zxn_buttons <= (NOT btn_drive_divmmc_n) & (NOT btn_m1_multiface_n);`

So bit 0 = M1 button (F9 in jnext), bit 1 = Drive button (F10) — both
ACTIVE-HIGH (pressed → 1). The signal is purely combinational, and the
NR 0x10 read mux samples it on every read.

**jnext divergence**:
- `src/core/emulator.cpp:1255-1261` NR 0x10 write_handler returns
  `(nr_10_coreid_ & 0x1F) << 2`, hardcoding bits 1:0 to 0.
- No NR 0x10 read_handler is registered, so reads fall through to
  `regs_[0x10]` — the canonical write-time byte → bits 1:0 are 0 forever.
- Software polling NR 0x10 to detect a held M1 (F9) or Drive (F10) button
  reads 0 in jnext and 1 in real hardware while the button is held.

**Reachability**: real Next firmware does not (to our knowledge) poll
NR 0x10 bits 1:0 — the M1/Drive buttons are normally observed via the
NMI strobe path (`hotkey_m1` / `hotkey_drive`). However, the read mux
exposes the raw state to user code, and tbblue.fw's diagnostic
register-dump screen reads NR 0x10 unconditionally.

**Class**: (b) — software-observable divergence, low real-world impact.

**Suggested fix shape** (NOT applied — out of scope per blind-audit prompt):
- Add `set_read_handler(0x10, ...)` that composes `((coreid & 0x1F) << 2)
  | (live_buttons & 0x03)` where `live_buttons` reflects the current
  state of the F9/F10 keyboard inputs (host-side held-key tracking,
  not the one-shot NMI strobe).
- Symmetrically, plumb F9/F10 held-state into the read handler — likely
  via a small `Emulator::spkey_buttons()` accessor or via direct probing
  of the FUSE-side keyboard matrix.

VHDL refs: zxnext.vhd:70, 5924, zxnext_top_issue2.vhd:2281.
jnext refs: `src/core/emulator.cpp:1255-1261`, comment block at
`src/core/emulator.cpp:1242-1247` already acknowledges this gap as
"jnext does not model SPKEY_BUTTONS — they read as 0 (idle)" but does
not classify it as a finding.

---

## V16-NMP-02 — `internal_port_enable` formula ignores NR 0x86–0x89 when `expbus_eff_en=1` (class-b) — **FIX LANDED**

**Status (2026-05-10)**: FIXED + 7 discriminative regression tests added.
Added `Emulator::effective_internal_port_enable(reg [, override_reg,
override_val])` helper that AND-masks NR 0x82-0x85 with NR 0x86-0x89
when `expbus_eff_en=1` per VHDL :2392-2393. All 33 port-decode gate
sites in `emulator.cpp` (the original 47 cited in this audit minus
8 save/restore + 6 read-mux/comment lines) now route through the
helper; install handlers for NR 0x86/0x87/0x88/0x89 + NR 0x80
re-push every downstream shadow via `propagate_effective_port_enables`
on every formula-input change. Override-pair pattern handles the
NextReg cache-after-handler ordering (regs_[reg] is committed AFTER
the handler returns). NR 0x85↔NR 0x89 pair AND'ed only on the
enable nibble (bits 3:0); reset_type bit 7 is preserved out-of-band.
Discriminative revert (helper short-circuited to raw cache): 5/7
V16-NMP-02 rows FAIL pre-fix; all 7 PASS post-fix. Tests: ctest
38/38 + FUSE 1356/1356.



VHDL zxnext.vhd:2392-2393 specifies the 32-bit `internal_port_enable`
vector that controls every internal-port decode (port_7ffd_io_en,
port_dffd_io_en, port_1ffd_io_en, port_multiface_io_en, port_divmmc_io_en,
port_ay_io_en, … all 28 bits) as:

```
internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82) when expbus_eff_en = '0' else
                       ((nr_89 AND nr_85) & (nr_88 AND nr_84) & (nr_87 AND nr_83) & (nr_86 AND nr_82));
```

Both NR 0x82-0x85 (internal-port enable group) and NR 0x86-0x89 (bus-port
enable group) are software-writable. When `expbus_eff_en=0` (the
default — no expansion-bus device wired), the formula collapses to the
internal-port enables alone, and NR 0x86-0x89 affect nothing.

When `expbus_eff_en=1` (set via NR 0x80 bit 7), every bit-i of the
effective port-enable is the AND of the corresponding bit in the
internal and bus-port enable bytes. This means software can mask out
specific ports from being decoded internally by clearing the matching
bus-port bit, even with the internal-port bit set.

**jnext divergence**:
- The contention model and the port handlers consult NR 0x82-0x85 only
  (e.g. `src/core/emulator.cpp:760` for port_7ffd_io_en, line 2446 for
  port_multiface_io_en, line 2466 for port_ulap_io_en).
- NR 0x86-0x89 writes are stored in `regs_[0x86..0x89]` for read-back
  but never AND-ed into any port-decode gate.
- When user software sets NR 0x80 bit 7 = 1 AND clears one of the
  NR 0x86-0x89 bits to mask a port, jnext continues to decode the port
  internally; VHDL would suppress it.

**Reachability**: jnext does not model expansion-bus devices, and
NextZXOS firmware leaves `expbus_eff_en=0` in its boot path. The
divergence is observable only when user code explicitly sets NR 0x80
bit 7 for testing or diagnostics. Likelihood of real software hitting
this is low.

**Class**: (b) — functional divergence, exercised only under unusual
software state.

**Suggested fix shape** (NOT applied):
- Add a per-bit composer that exposes the EFFECTIVE
  `internal_port_enable` mask as a function of NR 0x80 bit 7 +
  NR 0x82-0x85 + NR 0x86-0x89, and route every existing port-enable
  consumer through it. Affected handlers in
  `src/core/emulator.cpp`:
  - line 760: `set_port_7ffd_io_en` (NR 0x82 bit 1)
  - line 2442: `set_port_io_enable` (NR 0x83 bit 0, DivMMC)
  - line 2446: `set_enabled` (NR 0x83 bit 1, Multiface)
  - line 2466: `set_port_ulap_io_en` (NR 0x85 bit 0)
  - and the IO observers that gate on cached(0x82..0x85).
- Each of those would compose `(nr_8X bit_i) AND (nr_80 bit 7 = 0 ? 1 :
  nr_8X+4 bit_i)` to get the effective gate.
- When `expbus_eff_en` toggles, all these effective gates need to be
  recomputed.

VHDL refs: zxnext.vhd:2392-2393, 2197 (`expbus_en <= nr_80_expbus(7)`),
5800-5813 (commit on bus-idle).
jnext refs: `src/core/emulator.cpp:760, 2442, 2446, 2466`.

---

## Areas scrutinized that did NOT yield findings

1. **NR 0x02 readback layout (bits 7/4/3/2/1:0)** — verified bit-by-bit
   against VHDL:5891. `nr_02_bus_reset` (bit 7), `nr_02_iotrap` (bit 4
   = `nr_da_iotrap_cause(1) OR nr_da_iotrap_cause(0)`), `nr_02_generate_mf_nmi`
   (bit 3), `nr_02_generate_divmmc_nmi` (bit 2), `nr_02_reset_type(1:0)`
   all match. The set/clear cascade in `NmiSource::nr_02_write` matches
   the VHDL priority `if nmi_gen_nr_*=1 AND nmi_accept_cause=1 SET; elsif
   nr_02_we=1 AND bit_low=0 CLEAR` shape exactly.

2. **NR 0x02 bit 4 iotrap clear vs concurrent capture** — VHDL :3866-3880
   priority cascade gives SET-on-trap precedence over CLEAR-on-NR-write.
   Z80 cannot generate both events on the same bus cycle (different
   instructions); jnext implements them sequentially with the right
   final state.

3. **Three-source NMI priority cascade** — exhaustively walked
   the latch update at VHDL:2107-2113 against
   `NmiSource::recompute_()`. With MF, DivMMC, ExpBus all asserted
   simultaneously: only MF latches (gates: NR 0x06 bit 3, port_e3_reg(7),
   divmmc_nmi_hold). With MF blocked (CONMEM=1), DivMMC latches if
   gates pass (NR 0x06 bit 4, mf_is_active=0). With both blocked,
   ExpBus latches. C++ matches VHDL `if/elsif/elsif` cascade via
   sequential `if` statements gated on `!nmi_mf_` / `!nmi_divmmc_`.

4. **NR 0x02 software-NMI strobe + iotrap OR'd into nmi_assert_mf** —
   VHDL:3837 `nmi_sw_gen_mf <= nmi_gen_nr_mf OR nmi_gen_iotrap`.
   C++ matches via separate `nmi_sw_gen_mf_` and `iotrap_strobe_pending_`
   flags OR'd in `nmi_assert_mf()`. NR 0x06 bit 3 gates the entire
   producer (including iotrap path) — matches VHDL :2090.

5. **`nr_da_iotrap_cause` set gate on `nmi_accept_cause`** — VHDL only
   updates the cause field when FSM is in IDLE or FETCH (line 3871). C++
   port handlers gate via `nmi_accept_cause_()` before assigning
   `nr_da_iotrap_cause_`. `nr_d9_iotrap_write` follows the same gate
   per VHDL:3892 — C++ matches.

6. **Multiface FSM — all four FFs verified** —
   - `port_io_dly`: latches OR of all four port_* on every clock edge
     (multiface.vhd:122-131). C++ clock_edge_ matches.
   - `nmi_active`: priority cascade reset > button_pulse > clear-conditions
     (lines 137-148). Clear path gated by `port_io_dly=0` for port_*_wr
     and port_dis_rd-mode_p3, but NOT for retn_seen. C++ matches.
   - `invisible`: priority reset > button_pulse > set-condition
     (lines 152-163). Mode-specific port_*_wr gates port_io_dly=0.
     C++ matches.
   - `mf_enable`: priority cascade reset > fetch_66+mreq → '1' >
     port_dis_rd OR retn → '0' > port_en_rd → NOT invisible_eff
     (lines 171-184). NB: clear paths via port_dis_rd / retn are NOT
     gated by port_io_dly (asymmetric vs nmi_active/invisible). C++
     matches.

7. **`mf_port_en_o` combinational** — VHDL :195: `'1' when port_mf_enable_rd_i
   AND NOT invisible_eff AND (mode_128 OR mode_p3)`. C++
   `update_mf_port_en_` matches; consumed by the MF readback handlers
   in emulator.cpp (which apply additional per-LSB / per-mf_type gates
   matching VHDL :4310-4322).

8. **MF +3 / MF128 readback mux** — verified four cases in VHDL :4310-4322:
   - "0001" → port_1ffd readback with motor bit gated on
     `nr_81_expbus_fdc` (V14-NMP-01 fix already applied).
   - "0111" → port_7ffd_reg full byte.
   - "1101" → '0' & port_dffd_reg(6) & '0' & port_dffd_reg(4:0).
   - "1110" → "0000" & port_eff7(3) & port_eff7(2) & "00".
   - others → "00000" & port_fe_reg(2:0).
   All match `src/core/emulator.cpp:451-505`.

9. **NMI return-address shadow latch (NR 0xC2/0xC3)** — VHDL :2050-2070,
   software-writable via `nr_c2_we`/`nr_c3_we` (active strobe at line
   4894-4895), plus hardware-capture on `Z80N_command_s = NMIACK_*` AND
   `cpu_wr_n=0`. Read mux at :6232-6235 returns the latch verbatim. C++
   path (z80_cpu.cpp:424-427 + nextreg.cpp:464-467) writes both bytes
   into `regs_[0xC2/0xC3]` on NMI ack; NR 0xC2/0xC3 fall-through write
   path stores software-written bytes via the bare cache. Reads always
   return the cache. Both sources converge — matches VHDL.

10. **NR 0xC0 read mux** — VHDL :6230 layout
    `nr_c0_im2_vector(2:0) & '0' & nr_c0_stackless_nmi & z80_im_mode(1:0) &
    nr_c0_int_mode_pulse_0_im2_1`. C++ at 2553-2560 composes the same
    field widths and bit positions.

11. **NR 0x06 read mux** — VHDL :5900 layout matches C++ at line 3612-3618.
    `nr_06_ps2_mode` is sourced from authoritative state to honour the
    config_mode write-gate (V11-NMP-03 prior-pass fix).

12. **NR 0x05 read mux** — VHDL :5897 byte layout decoded from
    joy0(1:0) → 7:6, joy1(1:0) → 5:4, joy0(2) → 3, eff_5060 → 2,
    joy1(2) → 1, eff_scandouble_en → 0. C++ at line 1199-1218 composes
    the same. Pentagon-mode bit-2 force-clear handled at write time
    (V13-NMP-01) and read time. Note: the `eff_*` vs SHADOW distinction
    is technically observable on a frame-sync window for users who
    write NR 0x05 mid-frame and read it back before vsync; this is a
    video-timing concern outside this subsystem and was not pursued.

13. **NR 0x07 read mux** — VHDL :5905 `"00" & cpu_speed & "00" &
    nr_07_cpu_speed`. C++ at 742-746 composes `(act<<4)|req`. The
    `cpu_speed = expbus_speed when expbus_en=1` divergence (which would
    force bits 5:4 to "00" when expbus_en=1) is a CPU-clock-domain
    concern not tracked under this subsystem.

14. **NR 0x08 read mux** — bit 7 = NOT effective-paging-locked, bit 6 =
    eff_nr_08_contention_disable, bits 5:0 = stored-mirror. Verify8
    fix already applied; matches VHDL :5908.

15. **NR 0x80 lo-nibble fold on reset** — VHDL :2186 `nr_80_expbus(7:4)
    <= nr_80_expbus(3:0)`. C++ NextReg::reset at lines 87-89 computes
    `(low << 4) | low` = same result. Match.

16. **NR 0x81 read mux** — VHDL :6125 layout, V11-NMP-01 already fixed
    the bits-1:0 hardcoded-to-"00" semantics (VHDL :5496).

17. **NR 0xD8/0xD9/0xDA layouts** — VHDL :6265-6272. NR 0xD8 returns
    `"0000000" & nr_d8_io_trap_fdc_en`. NR 0xD9 returns the full
    `nr_d9_iotrap_write`. NR 0xDA returns `"000000" & nr_da_iotrap_cause`.
    C++ matches at 1993-2034.

18. **NR readback for write-only registers** — Pass-15 (V15-NMP-01,
    V15-NMP-02) addressed NR 0x63 and NR 0xFF write-only readback
    leakage per VHDL :6286-6287 fall-through to "00000000". I scanned
    other write-only NR groups (NR 0x40-0x44 palette, NR 0x68/0x69
    layer2/lores, NR 0xCC/0xCD/0xCE DMA-INT enables) and all have
    explicit read mux entries OR explicit cache canonicalisation in
    C++. No further leaks surfaced.

19. **Port-mask precedence tie-breaker** — `port_dispatch.cpp` uses
    most-specific-mask-wins for both reads and writes. Reads fall
    through to the next-most-specific handler when the best handler
    has no read callback (models VHDL exclusive one-hot decode plus
    the AY-fallback for DFFD/etc. write-only ports). The MF observer
    is registered as a side-effect-only `add_io_observer` so it sees
    the port LSB regardless of which handler wins dispatch.

20. **Hotkey edge-detect bit mapping** — emu_fnkeys.cpp uses bit i-1 for
    Fi (F1=bit 0, F9=bit 8, F10=bit 9). VHDL hotkeys_0/1 use 1-indexed
    `(N)` → Fi. The offset-by-1 convention is consistent. F-key gates
    (`nr_06_hotkey_5060_en` for F3, `nr_06_hotkey_cpu_speed_en` for F8,
    `port_divmmc_io_en` for F10, `NOT nr_03_config_mode` for F4) all
    honoured. F2 (scandouble), F7 (scanlines), F9 (hotkey_m1) have no
    NR 0x06 gates per VHDL :6341, :6346, :6348 — C++ matches.

21. **Hotkey emit-on-rising-edge vs level** — every hotkey strobe in
    VHDL is `hotkeys_0(N) AND NOT hotkeys_1(N)` = rising-edge pulse.
    C++ EmuFnKeys::fire_mf_side_effects computes `rising = cur & ~prev`
    and dispatches once per edge. NmiSource MF/DivMMC button strobes
    are also one-shot via `strobe_*_button()` setting `mf_button_=true`
    + `strobe_mf_button_pending_=true`; consumed in next `tick()`.
    Functionally equivalent to VHDL one-cycle pulse semantics.

22. **NMI source priority arbitration when 3+ sources fire same cycle**
    — see angle 3 above. No race condition observed at the
    per-instruction tick granularity jnext uses.

23. **Stackless NMI (NR 0xC0 bit 3)** — explicitly out of scope per the
    NMI plan ("Wave D cut, Q1"). Not pursued.

24. **NR 0x80 write fan-out** — `expbus_eff_en` and `expbus_eff_disable_mem`
    bits committed immediately (jnext approximation, vs VHDL bus-idle
    commit). Acceptable since jnext has no expbus device. Documented
    in code comments.

25. **Multiface mode change does not reset FFs** — VHDL `mode_p3/128/48`
    are combinational from `mf_mode_i`; changing mode mid-stream does
    not affect the four FFs (which are clocked). C++ `set_mode` only
    updates mode booleans without touching FFs. Match.

---

## Test results

```
ctest --test-dir build --output-on-failure
38/38 PASS, 0 FAIL, 0 SKIP

./build/test/fuse_z80_test build/test/fuse
1356/1356 PASS, 0 FAIL, 0 SKIP
```

Build mode: Release (`-DCMAKE_BUILD_TYPE=Release`).

---

## Findings summary

| ID            | Class | Component                  | Description (one-line)                                                       | Status        |
| ------------- | ----- | -------------------------- | ---------------------------------------------------------------------------- | ------------- |
| V16-NMP-01    | b     | NextREG NR 0x10 readback   | Bits 1:0 always 0; should reflect F9/F10 button-held state per i_SPKEY_BUTTONS | **FIX LANDED** |
| V16-NMP-02    | b     | Port-decode enable formula | NR 0x86-0x89 mask not AND'd with NR 0x82-0x85 when expbus_eff_en=1            | **FIX LANDED** |

Class breakdown:
- (a) behavior-breaking: 0
- (b) functional divergence: 2 (both fixed in same chain as the audit)
- (c) leak / cache divergence: 0
- (d) architectural: 0

Both findings fixed in commits on `task2/verify16-nmi-mf-port` after
the audit landed. 13 new discriminative regression tests added
(6 V16-NMP-01 + 7 V16-NMP-02) in
`test/nextreg/nextreg_integration_test.cpp`. Discriminative revert
verification confirmed both fixes' tests FAIL pre-fix and PASS
post-fix. Tests: ctest 38/38 + FUSE 1356/1356, no regressions.
