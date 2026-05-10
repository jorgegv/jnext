# NextZXOS Boot — Pass-14 NMI/MF/Port/NextREG Audit

**Branch**: `task2/verify14-nmi-mf-port` (off integration HEAD `73c3146`)
**Subsystem scope**: `src/peripheral/nmi_source.{cpp,h}`,
`src/peripheral/multiface.{cpp,h}`, `src/port/nextreg.{cpp,h}`,
`src/port/port_dispatch.{cpp,h}`, the NMI/MF/Port slice of
`src/core/emulator.cpp`, plus crossings into
`src/input/{mouse,joystick,joystick_dispatcher,mouse_dispatcher}.{cpp,h}`
and the F-key hotkey handlers.

**Methodology**: blind audit (no read of prior pass reports), VHDL as oracle
(`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`),
discriminative regression test landed alongside each class-(a/b/c) fix in the
same commit, release-mode build.

**Pre-fix tests baseline**: ctest 38/38 green; FUSE 1356/1356 green.
**Post-fix tests**: ctest 38/38 green; FUSE 1356/1356 green.

Pass-14 went after coverage angles called out in the prompt as likely
candidates after thirteen prior passes:

* Cache-leak family — every NR write_handler that stores the raw `v` byte
  while VHDL silently masks reserved or mode-gated bits.
* Multi-writer fan-out — flip-flops with three or more writers (NR + NR +
  port + reset) where C++ shadows are not fully symmetric.
* NR readback masks per-bit — every NR with a defined read mux must match
  VHDL byte-exact, including reserved bits and unmapped registers.
* Multiface FSM exhaustive — button latching, lockout, port-LSB decode by
  mode, MF+3 readback case-mux on `cpu_a(15:12)`.
* NMI source priority arbitration order — MF > DivMMC > ExpBus.
* `nr_81_expbus_fdc` gating of `port_1ffd_mtr_n` in the MF+3 readback path.

Three findings landed; all class-(a). Each ships with at least one
discriminative regression row in the same commit.

---

## V14-NMP-01 (class-a) — MF+3 readback at LSB 0x3F leaks port_1ffd bit 3 when FDC disabled

**Where**: `src/core/emulator.cpp` — Multiface +3 readback handler at LSB
`0x3F`, `cpu_a(15:12) = "0001"` mux branch.

**VHDL oracle**:
* `cores/zxnext/src/zxnext.vhd:4310-4327` — MF+3 readback case mux on
  `cpu_a(15:12)`. Branch `"0001"` (= `0x1xxx` for LSB `0x3F`) returns
  `"0000" & (NOT port_1ffd_mtr_n) & port_1ffd_reg`.
* `cores/zxnext/src/zxnext.vhd:879` — `port_1ffd_reg` is 3 bits.
* `cores/zxnext/src/zxnext.vhd:880` — `port_1ffd_mtr_n` is a separate 1-bit
  flip-flop, NOT a slice of `port_1ffd_reg`.
* `cores/zxnext/src/zxnext.vhd:3744-3761` — clocked process for
  `port_1ffd_mtr_n`:
  * `if reset='1'` → `'1'` (motor off)
  * `elsif nr_81_expbus_fdc='0'` → `'1'` (motor off, every clock)
  * `elsif port_xffd='1' AND cpu_a(13:12)="01" AND iowr='1' AND
     port_fd_conflict_wr='0'` → `NOT cpu_do(3)`
* `cores/zxnext/src/zxnext.vhd:1221-1225` — NR 0x81 power-on initial '0'
  for all expbus fields (no reset clause; jnext default is `nr_81_ = 0`,
  so `nr_81_expbus_fdc = 0`).

**Symptom**:
The C++ MF+3 readback at `0x1xxx`-LSB-`0x3F` returned
`mmu_.port_1ffd() & 0x0F`, which leaks `cpu_do(3)` of the most recent port
0x1FFD write into bit 3 of the readback **even when FDC is disabled** (the
jnext default — `nr_81_expbus_fdc = '0'`). VHDL forces `port_1ffd_mtr_n`
to `'1'` on every clock while `nr_81_expbus_fdc='0'`, so the readback bit
3 (`NOT port_1ffd_mtr_n`) ought to be `'0'` regardless of any port 0x1FFD
write.

**Existing test was wrong**: `test/multiface/multiface_test.cpp` row
**MF-MUX-01** wrote `OUT (1FFD), 0xAB` and asserted the readback equalled
`0x0B` — which codifies the buggy "bit 3 = `cpu_do(3)`" behaviour. The
test passed pre-fix and would have continued to pass forever, hiding the
real divergence. This is a coverage-theatre row by the definition in
`doc/testing/UNIT-TEST-PLAN-EXECUTION.md`.

**Fix**:
* `src/core/emulator.cpp` MF+3 mux `case 0x1`: composed bit 3 from the FDC
  gate. When `(nr_81_ & 0x08) != 0` → bit 3 = `port_1ffd_ & 0x08`. When
  FDC disabled → bit 3 = `0`. Bits 2:0 always come from
  `port_1ffd_ & 0x07`.

**Discriminative test**:
* `test/multiface/multiface_test.cpp` **MF-MUX-01** rewritten — FDC=0
  case (default), expected readback = `0x03` (bit 3 = 0).
* `test/multiface/multiface_test.cpp` **MF-MUX-01b** new — FDC=1 case
  (NR 0x81 b3 set), expected readback = `0x0B` (bit 3 = `cpu_do(3)`).

The MF-MUX-01 / MF-MUX-01b pair discriminates on the FDC gate: pre-fix
both return `0x0B`; post-fix the FDC=0 row returns `0x03` and the FDC=1
row returns `0x0B`.

---

## V14-NMP-02 (class-a) — NR 0x28 read returns cached last-write byte instead of `nr_stored_palette_value`

**Where**: `src/core/emulator.cpp` — NR 0x28 write_handler stored `v` raw,
no read_handler. `src/video/palette.h` — `nine_bit_first_byte_` was
private (no accessor).

**VHDL oracle**:
* `cores/zxnext/src/zxnext.vhd:1190` — signal declaration:
  `signal nr_stored_palette_value : std_logic_vector(7 downto 0);`
* `cores/zxnext/src/zxnext.vhd:5398-5399` — first half of NR 0x44 9-bit
  palette write (`nr_palette_sub_idx = '0'`) latches:
  `nr_stored_palette_value <= nr_wr_dat;`
* `cores/zxnext/src/zxnext.vhd:6003-6004` — NR 0x28 read mux:
  `when X"28" => port_253b_dat <= nr_stored_palette_value;`
* `cores/zxnext/src/zxnext.vhd:6301-6303` — NR 0x28 write effects:
  `nr_keymap_sel <= nr_wr_dat(7); nr_keymap_addr(8) <= nr_wr_dat(0);`
  i.e. NR 0x28 writes do NOT touch the palette signal that the read mux
  surfaces.
* `cores/zxnext/src/zxnext.vhd:5011` — power-on default `X"00"`.

**Symptom**:
NR 0x28 in jnext is treated as a write-keymap-only register: the
write_handler dispatches to `MembraneStick::write_nr_28(v)` and returns
`v`, which `NextReg::write` stores in `regs_[0x28]`. With no
`set_read_handler(0x28, ...)`, `NextReg::read` falls through to the
cached byte. So an NR 0x28 read returns the byte last written to
NR 0x28 — never the `nr_stored_palette_value` signal that real hardware
exposes.

`PaletteManager::nine_bit_first_byte_` (palette.cpp:332) is the in-jnext
shadow of `nr_stored_palette_value`; it was private and unaccessible.

**Fix**:
* `src/video/palette.h`: add public accessor
  `uint8_t nine_bit_first_byte() const`.
* `src/core/emulator.cpp`: install a NR 0x28 read_handler that returns
  `palette_.nine_bit_first_byte()`.

**Discriminative test**: `test/nextreg/nextreg_integration_test.cpp` new
group **V14-NMP-02-NR28**, four rows:
* `V14-NMP-02-NR28-01` — fresh emulator: NR 0x28 read = 0x00.
* `V14-NMP-02-NR28-02` — NR 0x28 ← 0xA5 must NOT change NR 0x28 read.
* `V14-NMP-02-NR28-03` — first half of NR 0x44 ← 0xC3 latches the byte
  into `nr_stored_palette_value`; NR 0x28 read = 0xC3.
* `V14-NMP-02-NR28-04` — second half of NR 0x44 ← 0x01 doesn't update
  `nr_stored_palette_value`; NR 0x28 read remains 0xC3.

Pre-fix: NR 0x28 reads echo the cached NR 0x28 last-write or 0x00. Rows
01/02 happen to pass coincidentally; rows 03/04 fail outright.

---

## V14-NMP-03 (class-a) — NR 0x2B write-only register leaks last-write byte through reads

**Where**: `src/core/emulator.cpp` — NR 0x2B write_handler returned `v` raw.

**VHDL oracle**:
* `cores/zxnext/src/zxnext.vhd:4851` — `when X"2B" => nr_2b_we <= '1';`
  (the only write-side decode entry).
* `cores/zxnext/src/zxnext.vhd:6306-6307` — NR 0x2B's only effect:
  `elsif nr_2b_we = '1' then nr_keymap_addr <= nr_keymap_addr + 1;`
  (auto-increment). Plus the joymap/keymap dpram write at :6321-6324.
* `cores/zxnext/src/zxnext.vhd:5878-6289` — NR read mux. **No** `when
  X"2B"` entry exists.
* `cores/zxnext/src/zxnext.vhd:6286-6287` — fall-through default:
  `when others => port_253b_dat <= (others => '0');` → NR 0x2B reads
  must return `0x00`.

**Symptom**: NR 0x2B write_handler dispatched the byte to
`MembraneStick::write_nr_2b(v)` and returned `v`, so the regs_[] cache
held the keymap-data byte. With no read_handler, NR 0x2B reads echoed
that byte instead of VHDL's `0x00` fall-through. This is the same shape
as the NR 0x29 fix already in place at the same location — write_handler
returns 0 instead of `v`.

Same bug exists for any NR with no read mux entry whose write_handler
returns `v` raw. Pass-14 limits the fix to NR 0x2B because NR 0x29 is
already correct and NR 0x2B's bidirectional-leak is the most observable
case in the keymap programming path.

**Fix**: NR 0x2B write_handler returns 0 — same shape as the existing
NR 0x29 canonicalisation directly above it.

**Discriminative test**: `test/nextreg/nextreg_integration_test.cpp` group
**WO-Integration**, new row **WO-INT-2B**: write `0xAA` to NR 0x2B, expect
read to return `0x00`. Pre-fix: returns `0xAA`. Post-fix: returns `0x00`.

---

## Findings outside Pass-14 scope (noted for context, not landed)

### Class-(d) — architectural / out-of-jnext-scope

* **NR 0x82-0x85 expansion-bus AND-with-NR-0x86-0x89** — VHDL :2392-2393
  composes `internal_port_enable` as `nr_8x AND nr_8y_bus_port_enable`
  when `expbus_eff_en = '1'`. jnext doesn't model the expansion bus,
  `expbus_eff_en` is permanently 0, the AND term is dead, and the
  C++ port handlers gate solely on `cached(0x82..0x85)`. Pure
  expansion-bus emulation is the architectural escalation; not a Pass-14
  finding.
* **NR 0x07 cpu_speed bus-idle commit lag** — VHDL :5817-5828 commits
  `nr_07_cpu_speed → cpu_speed` only on bus-idle CLK_CPU edges. jnext
  collapses this to "commit at instruction boundaries" via
  `clock_.commit_pending_cpu_speed_on_bus_idle()`. The NR 0x07 read at
  :5903 returns "00 & cpu_speed & 00 & nr_07_cpu_speed", and the C++
  short-circuits both fields to the cached `req` value. Mid-instruction
  reads do not exist in our coarse-tick model, so this is a (d) — same
  architectural escalation as the per-instruction tick granularity for
  NR 0x09 `eff_nr_09_scanlines` etc.
* **F5 / F6 expbus enable/disable hotkeys** — VHDL :2185-2195 fans
  `hotkey_expbus_enable / disable` into `nr_80_expbus(7)`. jnext does not
  emit those hotkeys (G147 tracks the multi-writer for NR 0x80 bit 7).
  Out of NMP scope.
* **`port_1ffd_mtr_n` consumers beyond MF+3 readback** — VHDL also wires
  `o_BUS_P3_MTR_n <= port_1ffd_mtr_n` (line 1695). jnext doesn't model
  the +3 disk-motor pin externally; the V14-NMP-01 fix is observable
  only via the MF+3 readback path. Class-(d) for the unmodelled pin.

### Class-(c) — latent / lower-priority

* **NR 0x2A** is dead in VHDL (write decoder commented out at :4850;
  read mux entry absent). The C++ stores the raw write byte in regs_[]
  and reads return the cache. Same shape as NR 0x2B but observable only
  via direct NR 0x2A write→read sequences, which neither real firmware
  nor jnext bootcode exercises. Catalogued for a future "all-dead-NR
  cache-zero canonicalisation" cleanup pass.

---

## Build / test summary

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON  ✓
cmake --build build -j$(nproc)                                ✓
ctest --test-dir build --output-on-failure                    38/38 PASS
./build/test/fuse_z80_test build/test/fuse                    1356/1356 PASS
```

Discriminative test counts added by Pass-14:
* `multiface_test`: 49 rows (was 48, +1 net — MF-MUX-01 rewritten,
  MF-MUX-01b new).
* `nextreg_integration_test`: 240 rows (was 235, +5: WO-INT-2B + four
  V14-NMP-02-NR28 rows).

All new rows pass post-fix; the rewritten MF-MUX-01 row asserts the
VHDL-faithful FDC=0 expected value (0x03 instead of the pre-fix 0x0B).

---

## Final result

| Finding ID | Class | Subsystem(s) | Status |
|------------|-------|--------------|--------|
| V14-NMP-01 | (a)   | Multiface MF+3 readback / NR 0x81 FDC gate | Fixed + tested |
| V14-NMP-02 | (a)   | NextREG NR 0x28 read mux / palette signal | Fixed + tested |
| V14-NMP-03 | (a)   | NextREG NR 0x2B unmapped read default     | Fixed + tested |
| (NR 0x2A leak)   | (c) | NextREG dead-register cache | Catalogued only |
| (NR 0x82-0x85 expbus AND-mask) | (d) | Expansion bus emulation | Catalogued only |

Total: **3 findings, all class-(a), all fixed and tested**.
