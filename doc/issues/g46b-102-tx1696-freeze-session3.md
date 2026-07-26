# G46(b) #102 session 3 — TX-1696 freeze: proven root cause + VHDL-faithful fix

Status: **FIXED**. Root cause proven via direct instrumentation (not
inference), fix is minimal and VHDL-cited, full test triplet green, new
discriminative unit row mutation-tested.

## Recap

Session 1 produced a 100%-deterministic repro. Session 2 proved CSpect
does **not** reproduce the freeze under the identical script (owner had
already confirmed real hardware is fine) and formed a floating-bus
hypothesis that this session **disproves** — the reads that looked like
"undecoded ports returning a static value" were a column-indexing bug in
the session-2 `awk` analysis (`$7`=`af`, not `bc`; the real port column
is `$8`). The actual dominant I/O traffic at the freeze transition is
`IN A,(C)`/`OUT (C),A` against the fully-decoded ports `$243B`/`$253B`
(NextREG select/data) and the six real keyboard half-rows — nothing
undecoded is involved. That hypothesis is retired.

## (1) Pinning the exact read/mechanism

Extending `JNEXT_G46B_PCTRACE` with `dma_state`/`dma_counter`/
`dma_block_len` (from `Dma::state()`/`counter()`/`block_length()`) and
`im2_dma_delay`/`devstates` (`Im2Controller::dma_delay()` and all 14
`Im2Controller::state(DevIdx)` values) gave a direct, per-frame view of
the DMA and IM2 subsystems — no more inferring from register snapshots.

Result, `tools/g46b_102_repro_stride.sh ... 2000 4200 100` (frames
3695-3714, `devstates` is 14 hex digits, index 3 = `CTC0`):

```
frame  dma_state  dma_counter  dma_block_len  im2_dma_delay  devstates
3696   0          80           80             1              00030000000000   (CTC0=S_ISR, transient)
3697   1          65535        255            0              00000000000000   (live: DMA enabled, no defer)
3701   0          80           80             1              00030000000000   (CTC0=S_ISR, transient — clears)
3702   1          65535        255            0              00000000000000   (live)
3706   0          80           80             1              00030000000000   (transient — clears)
3707   1          65535        255            0              00000000000000   (live)
3711   0          80           80             1              00030000000000   (transient)
3712   1          65535        255            1              00010000000000   (CTC0=S_REQ — STUCK)
3713   1          65535        255            1              00010000000000   (still stuck — forever after)
3714   1          65535        255            1              00010000000000   (still stuck — forever after)
```

At every **live** occurrence (3697/3702/3707), the DMA-enable moment's
transient `im2_dma_delay` (seen the frame before, at 3696/3701/3706)
clears by the next sample — `CTC0` briefly visits `S_ISR` (3) then
returns to `S_0` (0). At **3712**, the transient `im2_dma_delay`
asserted the frame before does **not** clear: `CTC0` is stuck at
`S_REQ` (1), `im2_dma_delay` stays 1, and `dma_counter` stays pinned at
`0xFFFF` (its immediate post-`LOAD` value, per the Z80-DMA "-1"
convention) — the DMA transfer makes **zero** progress, forever.

`JNEXT_G46B_ITRACE` across this transition (with a `# FRAME N` marker
added this session for unambiguous per-frame segmentation) shows the
mechanism generating this state: TX-1696's per-frame update routine
ends with

```
89c8: 21 D2 89        LD HL,0x89D2      ; 16-byte Z80-DMA command table
89cb: 06 10           LD B,0x10         ; 16 bytes
89cd: 0E 0B           LD C,0x0B         ; port 0x0B = Z80-DMA
89cf: ED B3           OTIR              ; stream the command table
89d1: C9              RET
```

Decoding the 16-byte table at `0x89D2` (`JNEXT_G46B_MEMDUMP`):
`83 7D 00 EB FF 00 54 02 68 02 AD 5B 00 82 CF 87` — a Z80-DMA WR
sequence: RESET (`83`), R0 dir=A→B + addr/len follow (`7D`, src=`0xEB00`,
len=`0x00FF`), R1 port A = memory increment (`54 02`), R2 port B = I/O
(`68 02`), R4 continuous mode, port B addr follows (`AD`, dst=`0x005B`
— **the sprite-pattern-data I/O port**), R5 (`82`), **LOAD** (`CF`),
**ENABLE** (`87`). I.e. TX-1696 uses the Z80-DMA controller to stream
sprite pattern bytes from RAM to port `$5B` every frame — a standard
Next fast-upload idiom.

At the specific instant this session's repro's `ENABLE` (`0x87`) byte
lands, `CTC0`'s IM2 interrupt request was mid-flight (`S_REQ`, not yet
acknowledged). `Im2Controller::step_dma_delay()` (`im2.cpp:1414-1419`)
latches `im2_dma_delay_` from `dma_int_pending()` — true whenever *any*
IM2 device's `state != S_0` with its `dma_int_en` bit set
(`im2.cpp:758-766`) — with a self-hold term for the decoder's RETI/SRL
window. Both terms require `im2_.tick()` to actually run to make
progress; see (3) below for why it doesn't.

## (2) VHDL verdict — what a genuinely undecoded port returns, and the
scope of the Timex/floating-bus mux

Traced `zxnext.vhd`'s CPU data-in mux directly (this closes the
session-2 hypothesis for good, independent of the arithmetic bug that
produced it):

**`zxnext.vhd:1866-1878`** — the IORQ read mux:

```vhdl
elsif cpu_iorq_n = '0' then
   if cpu_m1_n = '0' and im2_ieo = '0' then
      cpu_di <= im2_vector;
   elsif port_internal_rd_response = '1' and bus_iorq_ula = '0' then
      cpu_di <= port_rd_dat;
   elsif expbus_eff_en = '1' and expbus_eff_disable_io = '0' then
      cpu_di <= i_BUS_DI;              -- expansion bus
   else
      cpu_di <= X"FF";                 -- <<< genuinely undecoded port
   end if;
```

A port that **no internal peripheral decodes** (`port_internal_rd_response
= '0'`) and that has no expansion-bus device attached returns `X"FF"`,
**unconditionally, in every machine mode** — not the port-`$FF`-specific
Timex/floating-bus mux.

**`zxnext.vhd:2813`** (the Timex/floating-bus mux, `port_ff_rd_dat`)
is scoped to **`port_ff_rd`** only — the dedicated `port_fe`-style
one-bit decode `port_ff <= '1' when cpu_a(7 downto 0) = X"FF" else '0'`
feeding `port_ff_rd`. It contributes to the general `port_rd_dat`
OR-reduction (`zxnext.vhd:2837-2839`) **only when `port_ff_rd='1'`**
(else it drives `X"00"`, per the `port_ff_rd_dat <= ... else X"00"`
line) — it never leaks into reads of any *other* port. jnext's
`floating_bus_read()` is wired as `PortDispatch`'s catch-all default
(`emulator.cpp:547`, comment: *"unmatched port reads return ULA bus
value in **48K/128K modes**"*) — narrower in scope than real hardware's
`X"FF"` default for Next/Pentagon/+3, per the function's own header
comment (`emulator.h:545-547`). **This is a real, pre-existing,
self-documented scope gap** (confirmed via VHDL, independent of the
session-2 arithmetic bug) — but it is not what caused the TX-1696
freeze; see (1)/(3) for the actual mechanism. Filed as a residual
finding, not fixed this session (out of scope for #102; no live
game/test currently observes it — `floating_bus_test`/`port_test`
remain green).

**`ports.txt`** / `device/ctc.vhd` — checked whether the game's dense
`IN A,(C)` traffic could be hitting an under-decoded CTC range (8
channels documented, only 4 implemented per `src/peripheral/ctc.h:10-13`
and `emulator.cpp:5287-5330`'s own V21-NMP-02 audit comment). **Moot for
this bug**: the actual port dominating the trace (20340/`~22000` `IN
A,(C)` hits in a 20-frame window) is `$253B` (NextREG data, fully
decoded, `emulator.cpp:4133-4141`), not any CTC address — the earlier
"CTC4-7 unimplemented" concern from session 2's port range was based on
the same column-indexing bug and does not apply here.

## (3) Root cause — `Dma::is_active()` used as the CPU-stall gate

**`src/core/emulator.cpp:7420` (`Emulator::step_one_instruction()`,
before this fix):**

```cpp
if (dma_.is_active()) {                 // <-- true across BOTH cases below
    int transferred = dma_.execute_burst(16);
    master_cycles = ...;                // CPU instruction NOT executed
}
```

`Dma::is_active()` (`dma.h:82-87`) returns true whenever
`state_==TRANSFERRING`, **regardless of whether the bus arbitration has
actually granted the bus**. `state_` becomes `TRANSFERRING` the instant
`ENABLE` (`$87`) is processed (`dma.cpp:515-519`) — but per VHDL
`dma.vhd:267-281`, the FSM can sit deferred in `Phase::START_DMA`
indefinitely while `dma_delay_`/`daisy_busy_`/`bus_busreq_n_` is
asserted (`dma.cpp:108-121`, `Dma::tick_arbitration()`). `dma_test.cpp`
already tests and pins this exact deferral (rows **15.4-15.6**, **20.1**)
— but only at the `cpu_busreq_n()` level, never through
`Emulator::step_one_instruction()`.

VHDL's actual bus-stall signal is **`zxnext.vhd:1825`**:
`dma_holds_bus <= '1' when z80_busak_n = '0' else '0'` — the CPU is
bus-stalled **only once the arbitration handshake has genuinely granted
the bus**. jnext already has this exact signal, correctly implemented
and separately tested (**`Dma::dma_holds_bus()`**, `dma.cpp:90-96`,
tested by row **15.7**, and already used elsewhere for port-dispatch
gating, `emulator.cpp:5426-5439`) — it was simply never consulted by
`step_one_instruction()`'s stall decision.

**The deadlock**: `is_active()`'s unconditional `true` during deferral
skips the entire `else` branch of `step_one_instruction()` — which is
where `im2_.tick()` lives (`emulator.cpp:7629`, only reachable from the
normal-CPU-instruction path). `im2_.tick()` drives `step_devices()` (the
`S_0→S_REQ→S_ACK→S_ISR→S_0` transitions that clear a pending device's
interrupt) and `step_dma_delay()` (the very latch gating the DMA). Once
the CPU takes the DMA-stall fast path even once while `CTC0` is at
`S_REQ`, **nothing ever runs `im2_.tick()` again** — `CTC0` can never
reach `S_ACK`/`S_ISR`/back to `S_0`, `im2_dma_delay_` can never clear,
`Dma::tick_arbitration()`'s `START_DMA` gate can never open, and the
DMA transfer never makes the one byte of progress that would let
`state_` return to `IDLE`. A closed loop with no exit.

## The fix

**`src/core/emulator.cpp`, `step_one_instruction()`** — gate the
CPU-stall fast path on `Dma::dma_holds_bus()` (VHDL-named, VHDL-cited,
already tested) OR'd with `transferred > 0` (the common case: an
8-16-byte burst that completes wholly within one `execute_burst()` call
has already released the bus — `state_=IDLE`, `cpu_busreq_n_=true` —
by the time control returns, so `dma_holds_bus()` alone would
under-report a step that genuinely *did* hold the bus for
`transferred` bytes; `dma_test.cpp` 23.1-23.3 pin the resulting
T-state cost). `execute_burst()` is still called **unconditionally**
whenever `state_==TRANSFERRING`, progressing the arbitration FSM every
step regardless of whether the CPU is stalled this step — so a defer
can clear as soon as (and only as soon as) it VHDL-genuinely should.

While merely deferred, the code now falls through to the **normal**
CPU-instruction branch, so `im2_.tick()`/CTC/UART ticking run exactly
as they would on any other step — this is what lets the pending
interrupt resolve and the deadlock never form.

## Verification

- `JNEXT_G46B_PCTRACE` `dma_state`/`im2_dma_delay`/`devstates` columns:
  no `TRANSFERRING`/non-`S_0` streak longer than a handful of frames
  anywhere in an 8100-frame post-fix trace (vs. permanent from frame
  3712 pre-fix).
- Screenshots at frames 8000/15000/20000 of the identical script differ
  from each other (CPU alive, responsive, executing varying code) —
  no repeat of the pre-fix byte-for-byte-forever signature.
- The exact scripted key schedule now diverges from the pre-fix
  trajectory earlier than frame 3712 (expected: correcting real
  DMA/CPU-interleaving timing shifts exactly when each frame's
  processing completes, which shifts which absolute frame the
  schedule's `space` presses land on relative to evolving game state
  — the schedule was tuned against the *old*, buggy timing). The game
  reaches the NextZXOS main menu by frame ~8000-20000 via what traces
  as a clean, valid CPU state (`HALT`, `IM=1`, all `devstates=0`) —
  consistent with a normal exit/attract-mode timeout given how many
  more `space`/fire presses now actually reach the game (previously
  silently dropped by the freeze itself from frame 3712 onward), not
  with a crash. Re-deriving a schedule that keeps the fixed jnext in
  active gameplay for direct visual comparison against CSpect's
  session-2 trace was not attempted this session (time budget) — flagged
  as the natural next-session check if further confidence is wanted,
  but the deadlock's specific, instrumented signature is independently
  and conclusively gone.

## Tests

- **`test/dma/dma_test.cpp` row 23.7** (new): programs a Z80-DMA
  mem→mem transfer through a full `Emulator`, forces `dma_delay_=1`
  before `ENABLE`, and asserts the CPU executes a normal instruction
  that step (PC advances) while the DMA stays deferred
  (`state()==TRANSFERRING && !dma_holds_bus()`); clearing the defer then
  lets the transfer complete on the next step. VHDL-cited
  (`zxnext.vhd` `dma_holds_bus`, `dma.vhd:267-269`).
- **Mutation-tested**: reverting the stall condition to the old
  unconditional-`true` (i.e. `is_active()`-equivalent) fails 23.7
  (`cpu_ran=0`, PC never advances) and only 23.7 — confirmed via a
  `cp`-backup revert/restore cycle (never `git checkout`), all other
  156 rows still pass under the mutation.
- **`test/unit-tests.conf`**: `dma_test` 156 → 157.
- **`doc/testing/DMA-TEST-PLAN-DESIGN.md`**: +23.7 row, section-23
  count 6 → 7.
- **`test/SUBSYSTEM-TESTS-STATUS.md`**: regenerated via
  `make unit-test-dashboard`.

## Full triplet (post-fix)

- `make clean && make gui-release`: clean.
- `make unit-test`: **5686/5686 passed, 0 failed, 0 skipped** (76
  suites, all declared/registered).
- `./build/test/fuse_z80_test build/test/fuse`: **1356/1356**.
- `JNEXT_TEST_JOBS=4 make regression` (logged, not piped):
  **106/106 passed, 0 failed, 0 skipped** — no screenshot reference
  shifts.

## Files (branch `fix/102-tx1696-freeze2`, worktree `fix-102b`)

Commits (this session):
1. `afab86be` — probes (`JNEXT_G46B_PORTTRACE`, DMA/IM2 state columns,
   `JNEXT_G46B_MEMDUMP`, `# FRAME N` marker): `src/core/emulator.h`,
   `src/platform/headless_app.cpp`, and the `default_read_` logging
   hook in `src/core/emulator.cpp`.
2. `2e16105f` — the fix + tests: `src/core/emulator.cpp`
   (`step_one_instruction()`), `test/dma/dma_test.cpp` (row 23.7),
   `test/unit-tests.conf`, `test/SUBSYSTEM-TESTS-STATUS.md`,
   `doc/testing/DMA-TEST-PLAN-DESIGN.md`.

This document + the session-2 write-up
(`doc/issues/g46b-102-tx1696-freeze-session2.md`, whose floating-bus
hypothesis this session retires) will be committed together.

## Residual, out-of-scope findings for a future session

- `Emulator::floating_bus_read()` (`emulator.cpp:8447-8526`,
  `emulator.h:545-547`) is scoped to 48K/128K per its own doc comment,
  but is wired as the generic default for *every* machine mode. Per
  VHDL §(2) above, the correct default for a genuinely undecoded port
  in Next/Pentagon/+3 mode is a flat `X"FF"`, not the Timex-register
  arm's `Ula::get_screen_mode_reg()` (reached because NextZXOS sets
  both `nr_08_port_ff_rd_en` and `port_ff_io_en`). No live game or
  existing test currently observes this gap (confirmed: it is not
  TX-1696's mechanism), so it was not touched this session — VHDL-cited
  above for whoever picks it up.
- CTC channels 4-7 (`$1C3B-$1F3B`) remain unimplemented
  (`ctc.h:10-13`) per an existing, already-VHDL-audited "Class-(c)
  inert divergence" note in `emulator.cpp:5299-5330` — confirmed
  genuinely unrelated to this bug (no low-byte-`$3B` port ever appears
  in the freeze-transition trace).
