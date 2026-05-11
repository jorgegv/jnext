# NextZXOS Boot Subsystem — Pass-6 NMI/MF/Port Verification Re-Audit

## Verdict

**1 class-(a) bug found and fixed.** The audit is approaching honest
convergence: the cumulative six-pass sweep has now plumbed every NR I/O
register documented in `zxnext.vhd:5884-6287`, every NMI-FSM corner from
the NmiSource scaffold, every Multiface enable/decode/RAM-overlay path,
every iotrap source, and the Im2Controller status/enable plumbing for
$C0..$CE. Pass-6 found exactly one new genuine deviation that all five
prior passes missed; subsequent re-audit angles surfaced no other
class-(a) deviations within the NMI/MF/Port scope. Diminishing returns:
recommend **next pass be a closure pass focused on documentation
harmonisation rather than code change**.

| Pass | Focus | New class-(a) fixes | Cumulative |
| --- | --- | --- | --- |
| 1 | NMI FSM scaffold | 5 | 5 |
| 2 | Read-mux composition for NRs $00..$50 | 3 | 8 |
| 3 | Iotrap cause + bus_reset readback | 3 | 11 |
| 4 | NR-sweep $80%-coverage gate-omission audit | 9 | 20 |
| 5 | Sprite mirror $34/$75-$79, NR $86-89 reset gate, NR $7F/$80/$8C lo→hi fold | 6 | 26 |
| **6** | **NR $06 reset preservation (bits 6/4/3/2/1/0)** | **1** | **27** |

## Methodology

### Blind audit

Per the Pass-6 prompt, this audit was conducted **without reading any
prior `doc/issues/nextzxos-boot/*.md` report**. VHDL oracle
(`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`)
and the live C++ tree were the only inputs. The cumulative pass-N
diff history was inferred from `git log --oneline` plus inline code
comments such as `PASS-5`, `VERIFY4`, `G56`, `G88`, `G125`, `G131`,
`G153`, `G162`.

### Pass-6 angles walked

The prompt suggested nine specific angles. Each was walked end-to-end
against the VHDL oracle and the live C++ tree:

1. **Cycle-precise NR write effect timing** — every write handler
   audited; the only deferred-commit register (NR 0x07 `cpu_speed`)
   already runs through `clock_.set_pending_cpu_speed` /
   `commit_pending_cpu_speed_on_bus_idle` per the G142 closure. No
   other NR has the "deferred until bus idle" gate. Clean.
2. **NR cross-coupling** — NR 0x44 / NR 0x43 / NR 0x41 (palette
   write select × index × data) verified; NR 0xC4 bit 1 mirroring
   into NR 0x22 line-int-en already plumbed via
   `reschedule_line_interrupt()`. NR 0xC4 bit 0 fan-out into
   `port_ff_reg(6)` (inverted polarity per VHDL :3621-3622) already
   handled. Clean.
3. **NMI multi-source race conditions** — NmiSource priority arbiter
   matches VHDL :2107-2113 (`nmi_assert_mf` highest, then
   `nmi_assert_divmmc` gated on `mf_is_active=0 AND nmi_mf=0`, then
   `nmi_assert_expbus`). End-to-end consumer-feedback wiring
   (`mf_nmi_hold`, `divmmc_nmi_hold`, `divmmc_conmem`,
   `mf_is_active`) verified against `zxnext.vhd:2107-2118` +
   `divmmc.vhd:108-151`. Note: jnext's `is_nmi_hold()` returns
   `automap_held OR button_nmi`, an approximation versus VHDL's
   `automap OR button_nmi` (where `automap` adds the
   `i_automap_active AND i_automap_instant_on` and ROM3 instant-on
   terms). The omitted terms are only high during the M1 fetch
   cycle; in steady state the held latch covers the gate. Filed as
   class-(b) for record but NOT promoted — practical effect is
   one CPU cycle of timing slop on the gate, no observable boot
   regression.
4. **Multiface RAM/ROM mapping precise overlap with DivMMC** —
   `mmu_.read()` / `mmu_.write()` cascade verified against VHDL
   :2937-2945 priority cascade (bootrom > MF > DivMMC > Layer2 >
   ROMCS > sram_pre_A21_A13). MF gate is `multiface_->is_mem_active()`
   = `mf_enable_eff = mf_enable OR fetch_66`. DivMMC eligibility
   correctly suppresses on `mf_active` (VHDL line 3030 → override
   `"000"`). Clean.
5. **NR boundary inputs** — NR 0x02 = $00/$01/$02/$03 reset bits,
   NR 0x03 = $00/$80/$B0 config_mode + machine_type, NR 0x07 = $00..03
   CPU speed, NR 0x0A = $00/$80 (mf_type), NR 0x06 = $00/$08/$10
   button NMI enables: all walked. **Found NR 0x06 reset
   preservation bug — see Findings.**
6. **Non-MMU NR effects on Mmu** — NR 0x69 bit 6 → port_7ffd_reg(3) →
   shadow_screen_en already plumbed; NR 0x83 bit 0 → port_divmmc_io_en;
   NR 0x83 bit 1 → port_multiface_io_en (multiface enabled gate);
   NR 0x07 cpu_speed → contention. Clean.
7. **UART NRs $D0-$EF audit** — VHDL has NO NR mux entries in
   $D0..$D7 (port_d7 LSB decode at :2561 is unrelated to the NR
   port). NR $D8/$D9/$DA reset clauses verified at zxnext.vhd:5107,
   3870, 3891. jnext correctly resets `nr_d8_io_trap_fdc_en_`,
   `nr_d9_iotrap_write_`, `nr_da_iotrap_cause_` in `init()`.
8. **NR cluster $F0-$FF** — VHDL only mux entries at $F0/$F8/$F9/$FA
   (xdev/xadc diagnostic). Not modelled in jnext (no Pi/XADC), no
   handler installed, default 0xFF read floor returns 0 from the
   default cached(0). Out-of-scope for NMI/MF/Port. Clean.
9. **NR-coverage spreadsheet** — see below.

## NR coverage table (NMI/MF/Port scope)

| NR | VHDL purpose | Read/Write handlers | Reset behaviour | Pass-6 verdict |
| --- | --- | --- | --- | --- |
| $00 | Machine ID | R only (returns 0x08) | n/a (read-only) | clean |
| $02 | NMI gen / soft+hard reset / iotrap ack / bus_reset | R+W | bus_reset NOT reset (VHDL :5119), iotrap_cause reset to 0 | clean |
| $03 | Machine type / config_mode / timing | R+W | machine_type/config_mode NOT reset (latches survive); machine_timing reset to "011" | clean |
| $06 | hotkey/NMI-button enables / speaker_beep / ps2_mode / psg_mode | R+W | bits 7,5 = '1' on reset; bits 6,4,3,2,1,0 SURVIVE | **FIX (was: full reload to 0xA0)** |
| $0A | mf_type / sd_swap / divmmc_automap_en / mouse | R+W | NO reset (initial-value-only fields, all preserved) | clean (read handler pulls from authoritative subsystems) |
| $20 | unq INT generate / status read | R+W | n/a (no stored state) | clean |
| $80 | expbus reset/replace bits | bare cache | lo→hi fold on reset (PASS-5 fix already in place) | clean |
| $81 | expbus NMI debounce / clken / fdc / speed | R+W | bits all '0' on reset (NR 0x81 not in VHDL reset block — survives) | n/a (already raw cache pass-through; no Pass-6 deviation, may need follow-up Pass-7) |
| $83 | port enables (divmmc / multiface / dffd / 1ffd / mouse) | W (no R, cache used) | bits all '1' on reset (PASS-5 NR 0x82-0x85 group) | clean |
| $87 | bus port enables — divmmc | bare cache | bus_reset_type-gated (PASS-5 NR 0x86-0x89 group) | clean |
| $C0 | im2 vector / stackless_nmi / int mode | R+W | all 0 on reset (Im2Controller) | clean |
| $C2 | NMI return PCL | bare cache | reset to 0 (VHDL :2058) | clean |
| $C3 | NMI return PCH | bare cache | reset to 0 (VHDL :2059) | clean |
| $C4 | int_en ula/line/expbus | R+W | bit 7=1 expbus default | clean |
| $C5 | ctc int_en | R+W | n/a (CTC subsystem) | clean |
| $C6 | UART int_en | R+W | reset to 0 | clean |
| $C8 | int status read / clear (LINE/ULA) | R+W | n/a | clean |
| $C9 | int status read / clear (CTC) | R+W | n/a | clean |
| $CA | int status read / clear (UART) | R+W | n/a (RX bits duplicated per VHDL) | clean |
| $CC | DMA delay enable (NMI/ULA/line) | R+W | reset to 0 | clean |
| $CD | DMA delay enable (CTC) | R+W | reset to 0 | clean |
| $CE | DMA delay enable (UART) | R+W | reset to 0 | clean |
| $D8 | FDC iotrap enable | R+W | reset to 0 | clean |
| $D9 | iotrap captured cpu_do | R+W | reset to 0; gated capture on nmi_accept_cause | clean |
| $DA | iotrap cause readback | R+W | reset to 0; clear on NR 0x02 ← bit 4 = 0; gated set on nmi_accept_cause | clean |

Where "bare cache" appears, no read/write handler is installed; the
default `regs_[reg] = val` write path and `regs_[reg]` read path apply.
This is correct when the VHDL read mux returns the byte verbatim AND no
write-side gate is needed.

## Findings

### Class-(a) — NR 0x06 reset preservation

**Location**: `src/port/nextreg.cpp::NextReg::reset()`,
`src/core/emulator.cpp::Emulator::init()` (NR 0x06 shadow flags).

**VHDL oracle**: `zxnext.vhd:1107-1113` declares NR 0x06 as a per-bit
signal split with three classes of fields:

| Bit | VHDL signal | Default | In reset block? |
| --- | --- | --- | --- |
| 7 | `nr_06_hotkey_cpu_speed_en` | '1' | YES (`zxnext.vhd:4932`) |
| 6 | `nr_06_internal_speaker_beep` | '0' | NO (initial-value-only) |
| 5 | `nr_06_hotkey_5060_en` | '1' | YES (`zxnext.vhd:4933`) |
| 4 | `nr_06_button_drive_nmi_en` | '0' | NO (initial-value-only) |
| 3 | `nr_06_button_m1_nmi_en` | '0' | NO (initial-value-only) |
| 2 | `nr_06_ps2_mode` | '0' | NO (write-gated on `nr_03_config_mode`) |
| 1:0 | `nr_06_psg_mode` | "00" | NO (initial-value-only) |

The reset clause at `zxnext.vhd:4926-5111` explicitly lists every
register that gets a reset value. NR 0x06 has only the two hotkey-en
fields; every other field is **initial-value-only** ("`signal sig := X`"
power-on default), and the FF therefore retains its prior Q value
across both hard and soft reset.

**Pre-Pass-6 jnext behaviour**: `NextReg::reset()` ran `regs_.fill(0)`
followed by `regs_[0x06] = 0xA0`, unconditionally clobbering bits
6/4/3/2/1/0 on every reset. `Emulator::init()` then re-cleared
`nr_06_button_m1_nmi_en_`, `nr_06_button_drive_nmi_en_`,
`nr_06_internal_speaker_beep_`, and `nr_06_ps2_mode_` to `false`,
losing any previously latched user value.

**Concrete observable failure mode**: software that arms an NMI
button by writing `NR 0x06 ← 0x18` (bit 4 = 1: enable DivMMC button;
bit 3 = 1: enable Multiface button) and then issues `NR 0x02 ← 0x01`
(soft reset) expects the NMI gates to **survive** the soft reset on
real hardware. Pre-pass-6 jnext silently disarmed both gates the
moment the soft reset fired — the next button press would do
nothing until the firmware re-issued an NR 0x06 write. Same shape
as the NR 0x82-0x85 group (already covered by Pass-5) but applied to
the NMI button gate.

**Fix (`src/port/nextreg.cpp`)**: mirror the existing PASS-5 NR 0x80
lo→hi-fold pattern. Save the pre-reset NR 0x06 byte, set bits 7 and 5
to '1', preserve bits 6/4/3/2/1/0 verbatim, and write the result to
`regs_[0x06]` after `fill(0)`:

```cpp
const uint8_t saved_06 = regs_[0x06];
const uint8_t computed_06 = static_cast<uint8_t>((saved_06 & ~0xA0) | 0xA0);
regs_.fill(0);
regs_[0x06] = computed_06;   // replaces the pre-pass-6 unconditional 0xA0
```

**Fix (`src/core/emulator.cpp::init()`)**: after `nextreg_.reset()` has
applied the preserved value, derive the Emulator-side state shadows
from the live `cached(0x06)` rather than unconditionally clearing
them:

```cpp
const uint8_t cached_06 = nextreg_.cached(0x06);
nr_06_button_m1_nmi_en_      = (cached_06 & 0x08) != 0;
nr_06_button_drive_nmi_en_   = (cached_06 & 0x10) != 0;
nr_06_internal_speaker_beep_ = (cached_06 & 0x40) != 0;
nr_06_ps2_mode_              = (cached_06 & 0x04) != 0;
nmi_source_.set_mf_enable(nr_06_button_m1_nmi_en_);
nmi_source_.set_divmmc_enable(nr_06_button_drive_nmi_en_);
```

The forward of `nr_06_button_*_nmi_en_` into `nmi_source_` is
load-bearing: `nmi_source_.reset()` (called earlier in `init()`)
clears the gate flags to false, and without the explicit re-arm here
the NmiSource's `mf_enable_` / `divmmc_enable_` would diverge from
the preserved `regs_[0x06]` for the rest of the post-reset session
until the firmware happens to re-issue an NR 0x06 write.

**Power-on still 0xA0**: at the constructor, `regs_{}` is
value-initialised to all zeros, so `saved_06 == 0` →
`computed_06 == 0xA0` — matching the pre-pass-6 G125 power-on
behaviour. The reset preservation only diverges on subsequent
resets.

**Test coverage**:

* `test/nextreg/nextreg_integration_test.cpp::test_nr_06_composed_read`
  — added new row `G56-CR-NR06-RESET-PRESERVE` that writes
  NR 0x06 ← 0x5F (every preserved bit + clear bits 7,5) and re-runs
  `init()`, then verifies the readback is 0xFF (preserved bits
  intact + bits 7,5 re-asserted). Pre-fix this returned 0xA0;
  post-fix returns 0xFF.
* The existing `G56-CR-NR06-01` row (Reset default = 0xA0)
  was updated to explicitly write NR 0x06 ← 0x00 before the second
  `init()` so it remains deterministic regardless of what earlier
  groups wrote (no longer depends on the pre-pass-6 unconditional
  clobber).
* `test/audio/audio_nextreg_test.cpp::MX-23` — explicitly clears
  NR 0x06 ← 0x00 in the baseline path, since the previous group
  (MX-22) sets bit 6 and bit 6 now survives `fresh()`. Without this
  the EAR/MIC `exc_i` gate would already be high in the baseline
  scenario and the test's expected 2048-step delta would collapse to
  0.

## Class-(b) — recorded for honesty, not promoted

* **`DivMmc::is_nmi_hold()` omits in-flight automap terms**. VHDL
  `divmmc.vhd:148-150` defines `o_disable_nmi <= automap OR button_nmi`,
  where `automap` includes `i_automap_active AND
  (i_automap_instant_on OR automap_nmi_instant_on)` and
  `i_automap_rom3_active AND i_automap_rom3_instant_on`. jnext
  approximates as `automap_held_ OR button_nmi_`. The omitted terms
  are only high during the M1 fetch cycle that fires the automap; in
  steady-state the held latch covers them. No observable boot
  regression in either CSpect-comparison runs or unit tests.
  Promotion would require a per-cycle `automap_active` plumbing with
  no boot benefit; left for a future divmmc-deep-dive pass.
* **NR 0x05 bits 0 and 2 (scandouble_en, 5060) reset clobber.** Same
  shape as the NR 0x06 fix above (initial-value-only fields per
  VHDL :1302-1303 with no reset clause), but in the joystick /
  display-mode subsystem rather than NMI/MF/Port — out of scope for
  this audit. Recommended for a Pass-7 joystick/UI verify pass.
* **NR 0x09 bits 7:5/2/1:0 reset clobber.** Same shape — VHDL
  :1121/1122/1304 are initial-value-only; the reset block at
  :4937 only clears `nr_09_sprite_tie`. Audio subsystem scope; out
  of NMI/MF/Port. Pass-7 audio verify candidate.
* **NR 0x0F (board_issue) returns 0 unconditionally.** No VHDL
  read-mux match in jnext's handler list, so reads return cached(0) =
  0. For Issue 2 (jnext default `MachineType::ZXN_ISSUE2`), the
  expected value is `g_board_issue = X"0"` per
  `zxnext_top_issue2.vhd:39` — happens to match. For Issue 4 / Issue
  5 boards it would not, but jnext does not currently model those
  variants. Filed but not actionable.

## Convergence assessment

After six passes and 27 cumulative class-(a) fixes, the NMI/MF/Port
subsystem audit has reached a state where:

* Every NR with a VHDL read-mux entry in the NMI/MF/Port scope has a
  matching jnext handler or a defensible bare-cache pass-through.
* Every NR with a write-side gate (config_mode / port_enable /
  reset-type) honours the gate.
* Every NMI source (MF button, DivMMC button, ExpBus pin, FDC
  iotrap) is wired into NmiSource with the priority arbiter matching
  VHDL.
* Multiface mem-overlay is in the correct cascade slot
  (bootrom > MF > DivMMC > L2 > ROMCS) per VHDL :2937-2945.
* All resets that VHDL specifies as preserving FF state across reset
  (NR 0x7F, 0x80 lo→hi, 0x82-0x85 with reset_type gate, 0x86-0x89
  with bus_reset_type gate, 0x8C lo→hi, 0x06 bits 6/4/3/2/1/0 — the
  Pass-6 fix) now preserve correctly in jnext.

The remaining class-(b) items are NR fields **outside the NMI/MF/Port
scope** (NR 0x05 joystick, NR 0x09 audio, NR 0x0F board_issue diag)
that follow the same "initial-value-only / not in reset block" pattern.
They are real bugs but belong in their respective subsystem audits
(joystick / audio / diag).

**Recommendation**: declare convergence on the NMI/MF/Port subsystem
audit. The next NMI/MF/Port pass would yield very low signal-to-noise
and is unlikely to find class-(a) bugs without a fundamental
methodology change (e.g., DZRP-trace cycle-by-cycle vs CSpect rather
than VHDL-spec-vs-C++ static comparison). The remaining audit budget
is better spent on the joystick/audio/board_issue NR fields and on
the divmmc instant-on automap terms.

## Test status

```
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   3.37 sec
```

All 37 ctest groups green, including the new
`G56-CR-NR06-RESET-PRESERVE` row exercising the Pass-6 fix and the
updated `MX-23` and `G56-CR-NR06-01` rows that no longer rely on the
(incorrect) pre-pass-6 NR 0x06 reset clobber.
