# Pass-5 Blind Verification Re-audit — NMI / MF / Port

**Branch**: `task2/verify5-nmi-mf-port`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify5-nmi-mf-port`
**HEAD**: see `git log -1` at end of pass.
**Test status**: 37/37 ctest passes (100 %); regression 30/3 with the same 3
pre-existing failures observed on the pass-4 baseline (`parallax-demo`,
`rzx-record-func`, `rzx-playback-func`) — pass-5 introduces **no
regressions**.

## Verdict

**NEW FINDINGS — 6 class-(a) bugs fixed.**

The pass-5 sweep closed every cluster the prior pass deferred and produced
six concrete class-(a) corrections plus one cross-subsystem fix that had
been open since pass-3. The remaining cosmetic gaps (UART NRs, full
xdev/xadc state model) are class-(b) — diagnostic, not boot-critical, and
explicitly left untouched per pass scope.

## Cluster-by-cluster summary

| Cluster | Coverage | Findings |
|---|---|---|
| Cross-subsystem NR $09 / $15 (open since pass-3) | YES | 1 fix |
| Sprite mirror $34 / $35-$39 / $75-$79 | YES | 1 fix |
| UART $D0-$EF | YES | No real UART NRs in this range; class-(b) gaps for $F0/$F8/$F9/$FA xdev/xadc state |
| cpu_speed misc $F0-$FF | YES | NR $07/$F7 hotkeys already covered; xdev/xadc state class-(b), deferred |
| NR $86/$87/$88 reset-type gating | YES | 1 fix (full save/restore parallel to $82-$85) |
| Residual ($7F, $80, $8C lo→hi nibble fold, FPGA power-on defaults) | YES | 3 fixes |

Total: **6 class-(a) fixes** committed.

## Cross-subsystem fix — NR $09 bit 3 / NR $15 bit 1

**Bug** (open since pass-3): `src/core/emulator.cpp` NR $09 write handler
called `sprites_.set_over_border((v & 0x08) != 0)` from bit 3. Per VHDL
`zxnext.vhd:4184-4186`, NR $09 bit 3 is the **DivMMC mapram-latch clear**
trigger only — `port_e3_reg(6) := '0'` when `nr_09_we='1' and
nr_wr_dat(3)='1'`. The read mux at VHDL `:5909` composes NR $09 with
`nr_09_psg_mono(2:0) & nr_09_sprite_tie & '0' & (NOT nr_09_hdmi_audio_en) &
eff_nr_09_scanlines(1:0)` — bit 3 reads back as a constant `'0'`. Sprite
over-border is exclusively wired to NR $15 bit 1
(`nr_15_sprite_over_border_en`) per VHDL `:5233` (write) and `:5939`
(read). The NR $15 write handler at `emulator.cpp:1101` already calls
`sprites_.set_over_border((v & 0x02) != 0)` — that path is correct and
left alone.

**Fix**: removed the spurious `set_over_border` call from the NR $09
handler. NR $09 bit 3 now ONLY clears DivMMC mapram, matching VHDL
exactly. Software writing NR $09 bit 3 to invalidate the mapram latch
no longer collaterally toggles sprite drawing over the border.

Citation: VHDL `zxnext.vhd:4184-4186` (mapram clear),
`zxnext.vhd:5187-5189` (NR $09 write field decode), `zxnext.vhd:5909` (NR
$09 read), `zxnext.vhd:5233` and `:5939` (NR $15 sprite_over_border_en).

## Sprite mirror cluster

**NR $34** — VHDL `:4855` (write triggers `nr_sprite_mirror_we`,
mirror_index defaults to `"111"` per `:4828`) and `:6033` (read returns
`'0' & sprite_mirror_id(6:0)`). jnext at `emulator.cpp:1252-1260` sets
`mirror_sprite_num` on write, masks to 7 bits on read. **Clean**.

**NR $35-$39 (no-inc)** — VHDL `:4857-4875`, `nr_sprite_mirror_index =
"000".."100"`, `nr_sprite_mirror_inc=0` (bit 6 of `nr_wr_reg=0`, see
VHDL `:4916`). Read mux has no entry — falls through to
`(others => '0')`. jnext at `emulator.cpp:1921-1928` writes the byte and
returns 0 from the write handler so cached read = 0. **Clean**.

**NR $75-$79 (per-byte inc)** — VHDL `:4857-4875` same as $35-$39 but
bit 6 of `nr_wr_reg=1` so `nr_sprite_mirror_inc='1'`. Read mux has no
entry. **Bug**: jnext returned `v` from the write handler, leaking the
last-written byte through `regs_[]` cache. Read handler not registered,
so the cached byte was returned instead of the VHDL-mandated 0.

**Fix**: write handler now returns `0` to match VHDL fall-through and
align with the NR $35-$39 convention.

Citation: VHDL `zxnext.vhd:4855-4875` (mirror decode),
`zxnext.vhd:4916` (`nr_sprite_mirror_inc <= nr_sprite_mirror_we and
nr_wr_reg(6)`), `zxnext.vhd:5878-6289` (read-mux no-entry =
`(others => '0')`).

## UART NRs $D0-$EF

The full $C0-$FF range was VHDL-audited. The defined NRs are:
`$C0/$C2/$C3/$C4/$C5/$C6/$C8/$C9/$CA/$CC/$CD/$CE`,
`$D8/$D9/$DA` (IOTRAP, DivMMC), `$F0/$F8/$F9/$FA`, `$FF`.

**Conclusion**: there are NO actual UART NRs in $D0-$EF. UART access goes
through I/O ports `$133B/$143B/$153B/$163B`, not the NextREG indirection.
The $D8-$DA cluster is FDC IOTRAP (DivMMC-adjacent) and was already
audited in earlier passes. No new findings.

## cpu_speed misc $F0-$FF

NR $F0/$F8/$F9/$FA implement xdev_cmd / xadc — VHDL
`zxnext.vhd:1266-1276`, `:7440-7544`. State machine includes
`nr_f0_select`, `nr_f0_xdna_en`, `nr_f0_xadc_en`, EOC/EOS bits, ADC daddr,
read-modify selection of the composed read byte at `:7473-7480`.

jnext has **no handler** for these NRs. Cached `regs_[]` reads return
zero, writes go to cache verbatim. This is a **class-(b) limitation**:
the FPGA's XDNA (unique chip ID) and XADC (analog-to-digital converter
for voltage/temperature monitoring) are diagnostic-only peripherals not
exposed by the emulator. No software boot path depends on them.
Deferred per pass-5 scope (the prompt explicitly flags these as "less
boot-relevant").

## NR $86/$87/$88 reset-type gating

**Bug** (open question from pass-4): jnext at `nextreg.cpp:69-71` (old
line numbering) reset NR $86/$87/$88 to `0xFF` and NR $89 to `0x8F`
unconditionally on every reset, with a comment noting this is "the same
approximation as NR 0x82-0x84".

VHDL `zxnext.vhd:5061-5067` gates the reload on
`nr_89_bus_port_reset_type='0'` — the INVERSE polarity of the
`nr_85_internal_port_reset_type='1'` gate at `:5052` for the 0x82-0x85
group. With `nr_89_bus_port_reset_type='1'` (the FPGA power-on default
per `:1235`), the entire 0x86/0x87/0x88/0x89 group **survives** the
reset; only when bit 7 of NR 0x89 is cleared does the reset block
reload them.

**Fix**: applied the same save/restore dance previously used for
$82-$85 to $86-$89, with the inverse polarity check
(`bus_reset_type_0 = (nextreg_.cached(0x89) & 0x80) == 0`). Saves at
the top of `Emulator::reset()` and `Emulator::soft_reset()` capture
the pre-reset bytes; the post-init restore conditionally re-installs
them when the reset-type gate is closed.

Per VHDL the reset_type bit (NR 0x89 bit 7 / NR 0x85 bit 7) is itself
NEVER reset (only set at FPGA power-on or by an explicit write at
`:5509/:5522`), so the cached value of bit 7 always survives the reset
block too. The pack mask of `(saved & 0x8F)` keeps the cached byte
consistent with the read-mux composition `reset_type & "000" &
enable[3:0]` (`zxnext.vhd:6138/6150`).

## FPGA-power-on default constructor init

**Bug** (revealed once the reset_type gating became real): on the very
first `NextReg::reset()` call from the constructor, `regs_{}` is value-
initialised to all zeros. With the new reset_type-gated paths,
`reset_type_1 = (regs_[0x85] & 0x80) != 0 = false` and
`bus_reset_type_0 = (regs_[0x89] & 0x80) == 0 = true` — and the cold-init
fall-through preserved zero bytes for $82-$85, while the bus-port-reset
path WOULD reload $86-$89 to $FF/$8F. So the "preserve" path took the
wrong branch on cold init.

**Fix**: pre-populate the FPGA-power-on signal defaults in the
`NextReg::NextReg()` constructor BEFORE the first call to `reset()`:

- `regs_[0x82] = regs_[0x83] = regs_[0x84] = 0xFF` (VHDL `:1226-1228`)
- `regs_[0x85] = 0x8F` (VHDL `:1229-1230`)
- `regs_[0x86] = regs_[0x87] = regs_[0x88] = 0xFF` (VHDL `:1231-1233`)
- `regs_[0x89] = 0x8F` (VHDL `:1234-1235`)
- `regs_[0x7F] = 0xFF` (VHDL `:1216`, `nr_7f_user_register_0`)

After this, the cold-init reset() sees the correct `reset_type_1=true`
and `bus_reset_type_0=false`, so it takes the "reload to defaults"
branch for $82-$85 and the "preserve" branch for $86-$89 (which is
inert since they already hold the documented power-on bytes).
RST-08 / PE-05 unit rows pass exactly as VHDL specifies.

## NR $7F user register

**Bug**: VHDL `zxnext.vhd:1216` declares
`nr_7f_user_register_0 := X"FF"` and has NO entry in any reset block
(only the write at `:5486`). It survives both hard and soft reset.
jnext clobbered it to `0` on every reset via the `regs_.fill(0)` in
`NextReg::reset()` and never restored a default.

**Fix**: save the byte at the top of `reset()` and restore it after
`fill(0)`, parallel to the new $80/$8C lo→hi paths. Power-on default
$FF is set in the constructor so cold-boot reads return $FF.

## NR $80 lo→hi nibble fold on reset

**Bug**: VHDL `zxnext.vhd:2185-2186` —
`nr_80_expbus(7 downto 4) <= nr_80_expbus(3 downto 0)` on `reset='1'`.
Bits 3:0 (FDC enable / clken / NMI debounce / ULA override) survive;
bits 7:4 (expbus enable / ROMCS replace / disable IO/MEM) are
**recomputed** from bits 3:0. jnext zeroed the entire byte via
`regs_.fill(0)` and never reapplied the fold.

**Fix**: capture `regs_[0x80] & 0x0F`, recompute
`computed_80 = (lo << 4) | lo`, install after `fill(0)`. Even though
no expansion-bus emulation consumes these bits today (G45 tracks
that), the cached byte now matches what a real FPGA would present to a
NextREG read after reset.

## NR $8C lo→hi nibble fold (cache parity)

**Same VHDL pattern** at `zxnext.vhd:2255` —
`nr_8c_altrom(7 downto 4) <= nr_8c_altrom(3 downto 0)` on reset. The
MMU-side mirror `Mmu::nr_8c_reg_` already handles this in
`Mmu::reset()` (`mmu.cpp:103-107`). The NextReg-side cached
`regs_[0x8C]` was being zeroed.

**Fix**: capture-and-recompute the lo→hi fold for `regs_[0x8C]` too,
so the NextReg cache stays in sync with the MMU mirror across reset.
Even though the read handler at `emulator.cpp:2140` reads from the MMU
mirror, the cache is used by save/load and by the rewind subsystem;
keeping it consistent prevents spooky downstream bugs after a snapshot
restore that crosses a reset boundary.

## Findings list (numbered)

1. **NR $09 bit 3 cross-subsystem** — sprite over-border was incorrectly
   driven from bit 3; moved to NR $15 bit 1 only.
2. **NR $75-$79 read leak** — write handler returned `v`; now returns 0
   to match VHDL no-entry read fall-through.
3. **NR $86/$87/$88/$89 reset-type gating** — was unconditional;
   now honours `nr_89_bus_port_reset_type` per VHDL `:5061-5067`.
4. **NR $7F user register** — was zeroed on reset; now preserved
   (no VHDL reset entry) and constructor installs $FF power-on default.
5. **NR $80 lo→hi nibble fold** — was zeroed on reset; now applies
   `(lo << 4) | lo` per VHDL `:2185-2186`.
6. **NR $8C cached parity** — NextReg cache was zeroed on reset (MMU
   mirror was OK); now applies the same lo→hi fold so cache and mirror
   stay in sync.

Cross-cutting cleanup: pack-mask cached `regs_[0x85]` and `regs_[0x89]`
to `0x8F` on the preserve path so the cached byte matches the
documented read composition (`reset_type & "000" & enable[3:0]`).

## Convergence assessment

After five passes — pass-1/2/3/4 finds + pass-5's six new fixes — the
NMI / Multiface / Port / NextREG cluster is converging fast:

- Every defined NR in the read-mux ($00-$1F, $20-$2F, $30-$3F, $40-$4F,
  $50-$57, $60-$71, $80-$93, $A0-$B2, $B8-$BB, $C0-$CE, $D8-$DA, $F0,
  $F8-$FA, $FF) has a write or read handler in jnext, or correctly falls
  through to the cached byte.
- All reset-type gates now honour their VHDL-documented polarity
  (`nr_85_internal_port_reset_type='1'` for $82-$85 RELOAD;
  `nr_89_bus_port_reset_type='0'` for $86-$89 RELOAD).
- All "no-reset-entry" registers (NR $7F, NR $03 config_mode, NR $03
  machine_type, NR $05, NR $0E, NR $10) now correctly survive reset
  paths.
- All "lo→hi nibble fold" registers (NR $80 expbus, NR $8C altrom) now
  apply the fold on reset.
- All "write-only" sprite mirror NRs ($35-$39, $75-$79) return 0 to
  match VHDL read fall-through.

Pass-5 closes the open class-(a) item from pass-3 (NR $09/$15 mis-wiring)
and the deferred `nr_89_bus_port_reset_type` question from pass-4.

## Open questions / remaining gaps

1. **xdev_cmd / xadc state model (NR $F0/$F8/$F9/$FA)** — class-(b),
   diagnostic-only. Full implementation would require wiring an XADC
   simulator (voltage/temp readbacks) and an XDNA shift register
   (FPGA serial number). Not boot-critical.
2. **NR $98/$99/$9A/$9B output-register power-on defaults**
   (`X"FF"`/`X"01"`/`X"00"`/`X"0"`) — class-(b), Pi GPIO output drive
   never modelled. Reads come from `i_GPIO` inputs which jnext stubs
   to 0 at the read handler; the output cache divergence is invisible.
3. **NR $81 expbus state survives reset per VHDL** — class-(b),
   `nr_81_=0` is reapplied in `Emulator::init()` because there is no
   expansion-bus consumer. With G45 expbus emulation pending, this
   becomes a real concern; flagged for that task, not pass-5.
4. **Sprite mirror `mirror_sprite_q` reset** — already correct
   (`SpriteEngine::reset()` clears `mirror_sprite_num_=0`), per VHDL
   `sprites.vhd:598-599`.

## Test status

```
ctest --test-dir build -j$(nproc)
100 % tests passed, 0 tests failed out of 37
```

Regression suite: 30 pass / 3 fail / 0 skip. The 3 failures
(`parallax-demo`, `rzx-record-func`, `rzx-playback-func`) are
**pre-existing** — verified against the pass-4 baseline at
`task2/verify4-nmi-mf-port`, which fails the same 3 cases.

## Files touched

- `src/core/emulator.cpp` — NR $09 spurious set_over_border removed
  (cross-subsystem fix); NR $75-$79 write-handler return value;
  Emulator::reset / Emulator::soft_reset save/restore for $86-$89.
- `src/port/nextreg.cpp` — NR $86-$89 reset-type gating (parallel to
  $82-$85); NR $7F preserve-across-reset; NR $80 / NR $8C lo→hi fold
  on reset; FPGA-power-on default install in constructor.
