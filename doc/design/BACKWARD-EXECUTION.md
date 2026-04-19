# Backwards Execution — Design Document

## 1. Overview

Backwards execution (reverse debugging) allows the user to step and run the emulator in reverse
time: stepping backwards through individual instructions, running back to a previous frame, or
jumping to any recorded instruction. This is invaluable for finding the exact moment a bug was
introduced — e.g., the instruction that corrupted a memory location, or the frame in which a
raster timing problem first appeared.

### Scope (from Phase 8 specification)

> Backwards execution: Have a circular buffer that stores the complete CPU state and memory
> changes (including the bank) up to a maximum size, and allow "rewinding" up to a certain
> number of instructions. Should be toggleable from the debugger and via command line, with a
> configurable maximum number of instructions.

---

## 2. Goals

- **Correctness**: Rewinding to any previously executed instruction must restore the emulator
  to exactly the state it had at that point: CPU registers, memory contents, and all
  hardware subsystem state.
- **Simplicity**: The implementation must not complicate the forward execution path.
- **Configurability**: Buffer depth (number of rewindable frames) is user-configurable.
- **Opt-in**: Feature is disabled by default; enabled via CLI flag or GUI toggle.
- **GUI integration**: Step-back button and rewind slider in the debugger toolbar.
- **Scripting integration**: `step_back [N]` action available in the scriptable debugger DSL.

---

## 3. Architecture

### 3.1 Core Design: Frame-Level Snapshots + Instruction-Level Trace

The fundamental challenge of backwards execution is that Z80 instructions are not generally
reversible — a `LD (HL),A` destroys the old memory content. The only universal solution is
to record enough state to restore the emulator to any past moment.

The design uses a **two-level approach**:

1. **Frame snapshots** (coarse): A complete serialisation of all emulator state taken at
   every frame boundary. Stored in a circular ring buffer. Size: ~1850 KB each. Depth:
   configurable (default 500 frames ≈ 900 MB).

2. **Instruction-level trace** (fine): The existing `TraceLog` circular buffer, extended to
   also record the raw opcode bytes for each instruction. This provides sub-frame granularity
   within the most recent rewind window without extra memory cost.

**Rewind operation**:
- To step back one instruction within the current frame: restore the most recent frame
  snapshot, then fast-forward (re-execute silently) to `target_cycle - 1`.
- To step back to a previous frame: restore the snapshot for that frame, then fast-forward
  to the desired instruction.
- To step back N instructions: walk the TraceLog backwards to find the target cycle, then
  perform the above.

This two-level design means:
- Per-frame overhead: one full snapshot per frame (serialise ~1800 KB to a pre-allocated buffer)
- Per-instruction overhead: only the existing TraceLog record() call
- No overhead on memory writes (no dirty-page tracking required)
- Frame-boundary snapshots avoid the Scheduler queue complexity (queue is empty at frame end)

### 3.2 Snapshot Size Estimation

| Component       | Size         | Notes                                                     |
|-----------------|--------------|-----------------------------------------------------------|
| CPU registers   | ~30 bytes    | Z80Registers + IFF + IM + halted                          |
| RAM             | 2048 KB      | Full backing store copy (128 × 16 KB banks)               |
| MMU             | ~20 bytes    | slots[8], port_7ffd_, port_1ffd_, flags                   |
| NextREG cache   | 256 bytes    | Cached register values                                    |
| Palette         | ~4 KB        | 8 palettes × 256 × 2 bytes (9-bit)                        |
| Sprite attrs    | ~0.6 KB      | 128 sprites × 5 bytes (x, y, attr, flags, pattern number) |
| Sprite patterns | ~16 KB       | Pattern RAM: 128 × 128 bytes (4bpp 16×16 pixels)          |
| Layer 2         | ~20 bytes    | Scroll, bank, resolution, flags                           |
| Tilemap         | ~20 bytes    | Base addr, scroll, palette offset                         |
| Copper          | ~1 KB        | 1024-byte instruction RAM + PC/mode                       |
| AY chips (×3)   | ~120 bytes   | Registers + tone/noise/envelope                           |
| Beeper          | ~4 bytes     | EAR/MIC state                                             |
| DAC             | ~20 bytes    | 4 channels                                                |
| CTC             | ~40 bytes    | 4 channels × counter/mode state                           |
| DivMMC          | ~8 KB        | 8KB SRAM + bank/automap state                             |
| UART            | ~600 bytes   | RX FIFO 512B + TX FIFO 64B + state                        |
| SPI             | ~10 bytes    | CS, shift reg, clock phase                                |
| Clock           | 8 bytes      | uint64_t cycle counter                                    |
| Emulator timing | ~30 bytes    | psg_accum_, sample_accum_, IM2 state                      |
| **Total**       | **~1830 KB** | ~1850 KB with padding/alignment                           |

At the default 500-frame ring buffer: **~900 MB** of reserved memory when rewind is enabled.
For 300 frames: ~540 MB. These are allocated up-front at startup, not on demand.

### 3.3 Snapshot Data Flow

```
Emulator::run_frame()
    │
    ├─── [frame start] RewindBuffer::take_snapshot()
    │       │
    │       └─── Emulator::save_state(StateWriter&) → serialises all subsystems
    │
    ├─── [instruction loop]
    │       │
    │       └─── TraceLog::record()  (already exists)
    │
    └─── [frame end] advance frame_cycle_
```

When rewind is disabled, `RewindBuffer` is null and the frame-start call is a single null check.

### 3.4 Fast-Forward (Replay) Mode

After restoring a snapshot, we need to silently re-execute instructions forward to reach
the exact target instruction. This is done in "replay mode":

- Audio sample generation: **disabled** (do not enqueue to SDL mixer)
- Video rendering: **disabled** (do not invoke Renderer)
- Screenshot/trace callbacks: **disabled**
- CPU execution: **normal** (exact same path as live execution)
- Memory writes: **normal** (must produce correct memory state)
- Port/IO writes: **normal** (hardware state must be updated correctly)
- Scheduler: **normal** (CTC ticks, INT generation, etc. must fire correctly)

The replay executes at host-CPU maximum speed (no 50 Hz throttle). For a 3.5 MHz Z80 at
~1000 instructions/frame and a 7 GHz host, replaying 100 frames takes < 1 ms.

This is implemented via a `bool replay_mode_` flag in the emulator, checked only at the
audio sample generation and video render call sites.

---

## 4. Data Structures

### 4.1 Saveable Interface

Every subsystem that has mutable state implements this interface:

```cpp
// src/core/saveable.h
class StateWriter {
public:
    void write_u8(uint8_t v);
    void write_u16(uint16_t v);
    void write_u32(uint32_t v);
    void write_u64(uint64_t v);
    void write_bool(bool v);
    void write_bytes(const uint8_t* src, size_t n);
};

class StateReader {
public:
    uint8_t  read_u8();
    uint16_t read_u16();
    uint32_t read_u32();
    uint64_t read_u64();
    bool     read_bool();
    void     read_bytes(uint8_t* dst, size_t n);
};

class Saveable {
public:
    virtual void save_state(StateWriter& w) const = 0;
    virtual void load_state(StateReader& r) = 0;
    virtual ~Saveable() = default;
};
```

`StateWriter` / `StateReader` operate on pre-allocated flat byte arrays (the snapshot slot in
the ring buffer). No heap allocation occurs during snapshot.

### 4.2 RewindBuffer

```cpp
// src/debug/rewind_buffer.h
class RewindBuffer {
public:
    explicit RewindBuffer(size_t max_frames, size_t snapshot_bytes);

    // Take a snapshot of current emulator state
    void take_snapshot(const Emulator& emu, uint64_t frame_cycle, uint32_t frame_num);

    // Restore state to the nearest frame snapshot ≤ target_cycle
    // Returns the frame_cycle of the restored snapshot
    uint64_t restore_nearest(uint64_t target_cycle, Emulator& emu) const;

    // How many frames are stored
    size_t depth() const;

    // Oldest and newest stored frame cycles
    uint64_t oldest_frame_cycle() const;
    uint64_t newest_frame_cycle() const;

private:
    struct Slot {
        uint64_t frame_cycle;
        uint32_t frame_num;
        std::vector<uint8_t> data;  // Pre-allocated snapshot bytes
    };

    std::vector<Slot> slots_;
    size_t head_ = 0;    // Next write position (oldest overwritten first)
    size_t count_ = 0;
};
```

### 4.3 Extended TraceLog

The existing `TraceEntry` captures all CPU registers and opcode bytes. It needs no changes
for the rewind mechanism: `restore_nearest()` gives us CPU state at the frame boundary, and
we fast-forward to the target cycle. The TraceLog is used only to identify the target cycle
when the user says "step back N instructions":

```cpp
// Find cycle of instruction N steps before current
uint64_t target_cycle = trace.at(trace.size() - N - 1).cycle;
```

### 4.4 New DebugState Step Modes

```cpp
enum class StepMode {
    NONE,
    INTO,
    OVER,
    OUT,
    RUN_TO_CYCLE,
    // New:
    STEP_BACK,       // Rewind N instructions then pause
    RUN_BACK_TO_CYCLE // Rewind until cycle <= target_cycle
};
```

### 4.5 Emulator::save_state / load_state

```cpp
// Added to src/core/emulator.h
void save_state(StateWriter& w) const;
void load_state(StateReader& r);
```

These call `save_state`/`load_state` on every subsystem in a defined order:

```cpp
void Emulator::save_state(StateWriter& w) const {
    clock_.save_state(w);           // uint64_t cycle
    cpu_.save_state(w);             // Z80Registers
    ram_.save_state(w);             // 2048KB buffer (128 banks × 16 KB)
    mmu_.save_state(w);             // slots, ports, flags
    nextreg_.save_state(w);         // 256 register cache
    ula_.save_state(w);
    layer2_.save_state(w);
    sprites_.save_state(w);
    tilemap_.save_state(w);
    palette_.save_state(w);
    copper_.save_state(w);
    beeper_.save_state(w);
    ay1_.save_state(w);
    ay2_.save_state(w);
    ay3_.save_state(w);
    dac_.save_state(w);
    ctc_.save_state(w);
    uart_.save_state(w);
    divmmc_.save_state(w);
    spi_.save_state(w);
    // Emulator-internal timing state:
    w.write_u64(psg_accum_);
    w.write_u64(sample_accum_);
    w.write_u8(clip_l2_idx_);
    w.write_u8(clip_spr_idx_);
    w.write_u8(clip_ula_idx_);
    w.write_u8(clip_tm_idx_);
    w.write_bool(dac_enabled_);
    w.write_bytes(im2_int_enable_, 3);
    w.write_bytes(im2_int_status_, 3);
    // frame_cycle is stored in the RewindBuffer slot, not here
}
```

The Scheduler queue is **not serialised**. Since snapshots are taken at frame boundaries
(before any events for the new frame are scheduled), the queue is empty at snapshot time.
When restoring and replaying forward, the scheduler events are naturally re-enqueued as the
emulator executes through `run_frame()` exactly as it does normally.

**Note on I2C/RTC**: The I2C RTC peripheral uses the host system time. On save/restore, the
RTC state is serialised as a frozen timestamp delta. This may cause the emulated clock to
skip slightly on rewind, which is acceptable (the RTC is not cycle-accurate anyway).

**Note on UART FIFOs**: TX and RX FIFOs are serialised in full. Any bytes that were physically
transmitted to the host UART (if configured) cannot be "un-sent", but for emulation purposes
the FIFO contents are fully restored.

---

## 5. Rewind Algorithm

### 5.1 Step Back One Instruction

```
user presses Step Back
│
├─ Find target_cycle = trace.at(trace.size() - 2).cycle
│    (the instruction BEFORE the current one)
│
├─ RewindBuffer::restore_nearest(target_cycle, emu)
│    └─ Deserialise snapshot with frame_cycle ≤ target_cycle
│       Set emu.frame_cycle_ = snapshot.frame_cycle
│
├─ Emulator runs in replay_mode = true
│    └─ run_frame() loop, no audio/video output
│       Executes until clock_.get() >= target_cycle
│       Then pauses (StepMode::RUN_TO_CYCLE)
│
└─ replay_mode = false
   Debugger GUI refreshes
```

### 5.2 Step Back N Instructions

```
target_cycle = trace.at(trace.size() - N - 1).cycle
perform rewind to target_cycle (same as above)
```

### 5.3 Run Backwards to Frame N

```
target_frame_cycle = rewind_buffer.frame_cycle_at(N)
RewindBuffer::restore_nearest(target_frame_cycle, emu)
run_frame() in replay mode until clock_.get() == target_frame_cycle
pause at start of frame N
```

### 5.4 Handling TraceLog Wrap

The TraceLog is a circular buffer (default 100,000 entries). If the user tries to step back
more instructions than are in the trace, the operation is clamped to the oldest available
trace entry. The oldest reachable instruction is `max(oldest_frame_snapshot, oldest_trace_entry)`.

When trace entries older than the oldest frame snapshot are requested, the rewind stops at the
oldest available frame and presents that as the limit.

### 5.5 Boundary Conditions

- **Rewinding past the start of emulator session**: Not possible. The oldest frame snapshot
  is the hard limit.
- **Rewinding when rewind is disabled**: Operation fails with a clear error message.
- **Rewinding during RZX playback**: Disabled (RZX supplies IN values from recording; replaying
  backwards would require stored IN values which the RZX may not have).
- **TraceLog disabled**: Step-back within a frame is not possible. Only frame-level rewind
  (to frame boundaries) is available.

---

## 6. Performance Considerations

### 6.1 Forward Execution Overhead (When Rewind Is Enabled)

- **Per frame**: One `save_state()` call at frame start = serialise ~1850 KB to a pre-allocated
  buffer. On a modern CPU, a memcpy of 1850 KB takes < 0.5 ms. At 50 Hz, this is < 2.5% overhead.
- **Per instruction**: No additional overhead beyond the existing TraceLog record().

### 6.2 Replay Speed

Replay executes the emulator at host-CPU maximum speed with audio/video disabled. For:
- 1 frame of replay (worst case for step-back): ~1 ms
- 100 frames of replay: ~100 ms (barely perceptible pause)
- 300 frames of replay: ~300 ms

These estimates are conservative. In practice replay is faster because:
- No audio mixing (significant CPU cost removed)
- No video compositing (SDL texture upload, layer blending removed)
- No 50 Hz sleep/throttle

### 6.3 Memory Usage

| Buffer Depth       | Memory Reserved |
|--------------------|-----------------|
| 10 frames (~0.2s)  | ~18 MB          |
| 60 frames (~1.2s)  | ~110 MB         |
| 100 frames (~2s)   | ~180 MB         |
| 300 frames (~6s)   | ~540 MB         |
| 500 frames (~10s)  | ~900 MB         |
| 1000 frames (~20s) | ~1.8 GB         |

Default: **500 frames**. User-configurable via `--rewind-buffer-size N` (N = number of frames).

Pre-allocation at startup: `N × snapshot_size` bytes reserved in one `std::vector<uint8_t>`.
No allocation during normal execution.

### 6.4 Zero Overhead When Disabled

When `--rewind-buffer-size 0` (disabled), the `RewindBuffer` pointer is null. The call site
in `run_frame()` is a single null check:

```cpp
if (rewind_buffer_) {
    rewind_buffer_->take_snapshot(*this, frame_cycle_, frame_num_);
}
```

This is predicted not-taken when rewind is disabled.

---

## 7. GUI Integration

### 7.1 Debugger Toolbar

Two new controls added to the debugger toolbar (to the left of the existing step buttons):

```
[◀◀ Rewind] [◀ Step Back]  |  [▶ Step Into] [⤼ Step Over] ...
```

- **Step Back** (`◀`): Step back one instruction. Shortcut: `Shift+F7`.
- **Rewind** (`◀◀`): Open the rewind dialog / activate rewind slider.

Both buttons are greyed out (disabled) when:
- Rewind is not enabled
- Rewind buffer is empty (no snapshots yet)
- Currently in RZX playback mode

### 7.2 Rewind Slider

A horizontal slider appearing in the debugger when rewind mode is active:

```
Frame: [━━━━━━━━●───────────] 342 / 512   [Jump Here]
       ▲oldest              ▲current
```

- Slider range: oldest stored frame to current frame
- Dragging the slider shows a preview tooltip: `Frame 342 / 512 (cycle 30,230,880)`
- Releasing the slider (or pressing Jump Here) triggers rewind to that frame
- Frame number and cycle shown numerically

### 7.3 Rewind Indicator

When rewind is enabled, a small indicator in the debugger status bar shows:

```
[⏮ Rewind: 500 frames / 900 MB]
```

When the user has rewound into history (is looking at a past state):

```
[⏮ Rewound: frame 342 of 512  ▶Resume]
```

Pressing Resume or F5 resumes forward execution from the current (past) state.

### 7.4 Main Window Menu

`Debug` menu additions:
```
Debug
  ├── Step Back          Shift+F7
  ├── Rewind...          (opens slider panel)
  ├── ─────────────────
  ├── Enable Rewind      ✓ (toggle)
  └── Rewind Buffer Size... (opens dialog for frame count)
```

---

## 8. CLI Integration

### 8.1 Command-Line Options

```
--rewind-buffer-size N    Enable backwards execution with N-frame ring buffer
                          N=0 disables rewind (default). N=500 is the default depth.
```

Example:
```bash
./build/jnext --headless --machine next \
    --load demo/my_program.nex \
    --rewind-buffer-size 200 \
    --script test/scripts/rewind_test.dbg
```

### 8.2 Scripting DSL Integration

New actions in the scriptable debugger:

```
step_back [N]           # Step back N instructions (default 1)
run_back_to ADDR        # Run backwards until PC == ADDR
run_back_to_frame N     # Rewind to start of frame N
run_back_to_cycle N     # Rewind until cycle == N
```

New built-in variables:
```
REWIND_DEPTH            # How many frames are in the rewind buffer
OLDEST_FRAME            # Oldest frame number in the buffer
```

Example rewind test script:
```
# test_rewind.dbg
# Verify that rewinding to a previous frame restores register state

var expected_hl = 0

on execute 0x8000 once do
    expected_hl = HL
    print "Checkpoint: HL=${HL} at cycle ${CYCLE}"
    run_back_to_frame FRAME - 1
    assert HL == expected_hl "FAIL: HL not restored after rewind"
    print "PASS: rewind restored HL correctly"
    exit 0
end
```

---

## 9. Saveable Implementation for Each Subsystem

Each subsystem listed below requires `save_state(StateWriter&) const` and
`load_state(StateReader&)` methods. The data written/read must be byte-for-byte deterministic.

| Subsystem    | Class      | Key fields to serialise                                                                                     |
|--------------|------------|-------------------------------------------------------------------------------------------------------------|
| CPU          | `Z80Cpu`   | `Z80Registers` (all fields)                                                                                 |
| RAM          | `Ram`      | Full `data_` buffer (2048 KB, 128 × 16 KB banks)                                                            |
| MMU          | `Mmu`      | `slots_[8]`, `port_7ffd_`, `port_1ffd_`, `l2_write_enable_`, `l2_segment_mask_`, `l2_bank_`, `boot_rom_en_` |
| NextREG      | `NextReg`  | 256-byte register cache                                                                                     |
| ULA          | `Ula`      | Screen bank select, flash state, border colour                                                              |
| Layer 2      | `Layer2`   | Scroll X/Y, active/shadow bank, resolution, palette offset, access port                                     |
| Sprites      | `Sprites`  | 128 attribute records (x, y, attr, flags, pattern) + 16 KB pattern RAM + sprite port state                  |
| Tilemap      | `Tilemap`  | Base address, scroll X/Y, palette offset, control flags                                                     |
| Palette      | `Palette`  | All 8 × 256 × 2-byte palette entries; current palette select                                                |
| Copper       | `Copper`   | 1024-byte instruction RAM; PC; mode; write address                                                          |
| Beeper       | `Beeper`   | `ear_`, `mic_`, `tape_ear_`                                                                                 |
| AY chip (×3) | `AyChip`   | `reg_[16]`; tone/noise/envelope counters and state                                                          |
| DAC          | `Dac`      | Per-channel value and enable flags                                                                          |
| Mixer        | `Mixer`    | **Not serialised** — audio ring buffer intentionally discarded on rewind                                    |
| CTC          | `Ctc`      | 4-channel counter values, modes, dividers, interrupt state                                                  |
| UART         | `Uart`     | RX/TX FIFOs (full contents); baud rate state; flags                                                         |
| DivMMC       | `DivMmc`   | 8 KB SRAM; bank select; automap state; CONMEM/MAPRAM flags                                                  |
| SPI          | `Spi`      | CS line, shift register, clock phase, pending byte                                                          |
| I2C          | `I2c`      | RTC frozen timestamp; SCL/SDA state                                                                         |
| DMA          | `Dma`      | State machine; source/dest address; block counter; config registers                                         |
| Clock        | `Clock`    | `cycle_` (uint64_t)                                                                                         |
| Emulator     | `Emulator` | `psg_accum_`, `sample_accum_`, clip indices, `dac_enabled_`, `im2_int_enable_[3]`, `im2_int_status_[3]`     |

**Audio Mixer note**: The SDL audio mixer ring buffer is intentionally discarded on rewind.
Any audio already sent to the SDL output device cannot be "un-played". After rewind and
replay, audio resumes from the replay target position. This produces a brief audio glitch
(silence or a click) at the rewind point, which is acceptable behaviour.

---

## 10. Implementation Plan

### Phase 1 — Foundation: Saveable Interface and Emulator Snapshot

- [ ] `src/core/saveable.h` — `Saveable`, `StateWriter`, `StateReader` classes
  - `StateWriter` backed by `std::vector<uint8_t>` (variable length, for testing)
  - `StateWriter` backed by `uint8_t*` + offset (fixed-length, for ring buffer)
- [ ] `src/debug/rewind_buffer.h/.cpp` — `RewindBuffer` class (ring buffer of flat byte slots)
- [ ] Implement `save_state`/`load_state` for CPU (`Z80Cpu`)
- [ ] Implement `save_state`/`load_state` for RAM (`Ram`)
- [ ] Implement `save_state`/`load_state` for MMU (`Mmu`)
- [ ] Implement `save_state`/`load_state` for Clock (`Clock`)
- [ ] `Emulator::save_state()` / `Emulator::load_state()` — stub calling above subsystems
- [ ] Unit tests: round-trip save/load for CPU+RAM+MMU; assert byte-for-byte equality
- [ ] Compute exact snapshot size at runtime; assert matches expected estimate

**Milestone**: Can serialise and deserialise core emulator state correctly.

### Phase 2 — All Subsystem Serialisation

- [ ] Implement `save_state`/`load_state` for all remaining subsystems (see table §9)
- [ ] Add all subsystem calls to `Emulator::save_state()`
- [ ] Integration test: save state mid-frame, restore, run forward — compare to uninterrupted run
  - Use regression test screenshot comparison for this
- [ ] Handle `RewindBuffer` pre-allocation at startup
- [ ] `--rewind-buffer-size N` CLI option
- [ ] Frame-start snapshot call in `Emulator::run_frame()`

**Milestone**: Full emulator state round-trips correctly. Snapshots take < 1 ms.

### Phase 3 — Replay Mode and Rewind Algorithm

- [ ] `replay_mode_` flag in `Emulator`; check in audio and video render paths
- [ ] `Emulator::rewind_to_cycle(uint64_t target_cycle)`:
  - `RewindBuffer::restore_nearest()`
  - Run forward in replay mode to target cycle
  - Pause
- [ ] `StepMode::STEP_BACK` and `StepMode::RUN_BACK_TO_CYCLE` in `DebugState`
- [ ] `Emulator::step_back(int n = 1)` — uses TraceLog to find target cycle, then rewinds
- [ ] `Emulator::run_back_to_cycle(uint64_t)` — rewinds to target cycle
- [ ] `Emulator::run_back_to_frame(uint32_t)` — rewinds to start of frame N
- [ ] Unit test: step forward 20 instructions, step back 10, assert PC matches TraceLog

**Milestone**: `step_back()` and `run_back_to_cycle()` work correctly in headless mode.

### Phase 4 — GUI Integration

- [ ] Step Back button in debugger toolbar (`Shift+F7`)
- [ ] Rewind slider panel in debugger window
- [ ] Rewind enabled indicator in status bar
- [ ] "Rewound" indicator when viewing past state
- [ ] Resume button / F5 resumes from rewound position
- [ ] Debug menu additions (Enable Rewind toggle, Rewind Buffer Size dialog)
- [ ] Disable rewind controls during RZX playback

**Milestone**: Full GUI rewind experience works end-to-end.

### Phase 5 — Scripting Integration

- [ ] `step_back [N]` action in script engine
- [ ] `run_back_to ADDR` action
- [ ] `run_back_to_frame N` action
- [ ] `run_back_to_cycle N` action
- [ ] `REWIND_DEPTH`, `OLDEST_FRAME` built-in variables
- [ ] Regression test using a script that rewinds and asserts state

**Milestone**: Rewind fully accessible from `.dbg` scripts; usable in CI.

### Phase 6 — Polish and Edge Cases

- [ ] Handle rewind buffer wrap correctly (oldest frame display in slider)
- [ ] Error messages when rewind is not enabled or buffer is empty
- [ ] TraceLog depth warning: if trace is shorter than rewind depth, warn user
- [ ] Rewind during loading (TZX/TAP): disable rewind during tape loading, enable after
- [ ] Keyboard shortcut conflict check and documentation update
- [ ] `doc/testing/REGRESSION-TEST-SUITE.md` update: add rewind tests to test suite
- [ ] Performance benchmarks: snapshot time, replay time

---

## 11. Risks and Mitigations

| Risk                                                                                | Severity | Mitigation                                                                           |
|-------------------------------------------------------------------------------------|----------|--------------------------------------------------------------------------------------|
| Snapshot serialisation has a bug in one subsystem, causing divergence after restore | High     | Integration test: save → restore → run → compare screenshots (pixel-perfect)         |
| Scheduler queue is non-empty at frame boundary                                      | Medium   | Assert queue is empty before snapshot; investigate if not                            |
| Audio glitch on rewind                                                              | Low      | Expected and documented; flush SDL audio buffer on rewind start                      |
| RAM copy is too slow (2048 KB per frame at 50 Hz)                                   | Low      | Benchmark first; if too slow, use dirty-page bitfield (128 dirty flags × 16 KB each) |
| UART FIFO state diverges on restore if real UART is connected                       | Low      | Disable UART rewind integration if external UART configured                          |
| DivMMC SRAM state includes DivMMC ROM (8KB read-only)                               | None     | ROM never changes; only SRAM needs serialising                                       |
| RewindBuffer takes too much memory                                                  | Low      | Default 500 frames (~900 MB); configurable; warn if > 2 GB requested                 |
| Rewinding past tape loading breaks tape state                                       | Medium   | Disable rewind during TZX/TAP loading; resume after load complete                    |

---

## 12. Testing Strategy

### 12.1 Unit Tests

- `save_state` then `load_state` produces identical state (all fields equal)
- `RewindBuffer` ring wraps correctly at capacity
- Step back 1 instruction: PC matches `trace.at(-2).pc`
- Step back N instructions: verified against trace log

### 12.2 Integration Tests (Regression Screenshots)

1. Run demo program for 50 frames, take snapshot of frame 50.
2. Continue running to frame 100.
3. Rewind to frame 50.
4. Compare emulator state with saved snapshot — must be identical.
5. Continue forward to frame 100 again.
6. Compare final screenshot with reference — must be pixel-perfect.

This test verifies that rewind does not introduce any non-determinism.

### 12.3 Performance Tests

- Snapshot time per frame: measured over 1000 frames, must be < 1 ms average
- Replay speed: 100 frames of replay must complete in < 100 ms wall time
- Memory usage: 500-frame buffer must use ≤ ~1 GB (10% margin over estimate)

---

*Document created: 2026-04-09*
*Status: Design — awaiting implementation approval*
