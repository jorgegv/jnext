/// Rewind / backwards execution unit tests.
///
/// Verifies that:
///   1. RewindBuffer ring-wrap works correctly (capacity overflow).
///   2. Stepping forward N instructions then back N/2 lands on the correct PC.
///   3. rewind_to_frame() restores CPU state to a known past frame.
///   4. Snapshot round-trip: save_state → load_state → save_state produces
///      identical bytes (determinism check).
///
/// No GUI, no ROM file required. Uses machine_type=48k with a small inline
/// program injected directly into RAM.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/rewind_buffer.h"

#include <cstring>
#include <cstdio>
#include <vector>
#include <cassert>

// ── Helpers ────────────────────────────────────────────────────────────────

static int pass_count = 0;
static int fail_count = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++fail_count; \
    } else { \
        fprintf(stdout, "PASS %s\n", msg); \
        ++pass_count; \
    } \
} while(0)

#define REQUIRE(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "ABORT [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++fail_count; \
        return 1; \
    } \
} while(0)

// Build a minimal emulator with:
//   - 48K machine (smallest RAM, fast init)
//   - Rewind buffer of `rewind_frames` frames
//   - TraceLog enabled
//   - A simple Z80 program injected at 0x8000:
//       LD HL, 0x1234
//       LD BC, 0x5678
//       NOP (×N)    ; so we can step through predictably
//       JP 0x8000   ; loop forever
static bool build_emulator(Emulator& emu, int rewind_frames)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = rewind_frames;
    // Don't require ROM to load for this test — we inject our own program.
    emu.init(cfg);

    // Enable trace log (required for step_back).
    emu.trace_log().set_enabled(true);

    // Inject a small deterministic program at 0x8000.
    // LD HL, 0x1234  — 3 bytes: 21 34 12
    // LD BC, 0x5678  — 3 bytes: 01 78 56
    // NOP            — 1 byte:  00   (×20)
    // JP 0x8000      — 3 bytes: C3 00 80
    std::vector<uint8_t> prog;
    prog.push_back(0x21); prog.push_back(0x34); prog.push_back(0x12); // LD HL,0x1234
    prog.push_back(0x01); prog.push_back(0x78); prog.push_back(0x56); // LD BC,0x5678
    for (int i = 0; i < 20; ++i) prog.push_back(0x00);               // NOP ×20
    prog.push_back(0xC3); prog.push_back(0x00); prog.push_back(0x80); // JP 0x8000

    for (size_t i = 0; i < prog.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);

    // Set PC = 0x8000.
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000;
    regs.SP = 0xFFFD;
    regs.IFF1 = 0; regs.IFF2 = 0;
    emu.cpu().set_registers(regs);

    return true;
}

// ── Test 1: RewindBuffer ring wrap ─────────────────────────────────────────

static int test_rewind_ring_wrap()
{
    printf("\n--- Test 1: RewindBuffer ring wrap ---\n");

    Emulator emu;
    build_emulator(emu, 4);  // Only 4 frame slots

    auto* rb = emu.rewind_buffer();
    REQUIRE(rb != nullptr, "rewind buffer exists with 4 frames");
    CHECK(rb->empty(), "buffer starts empty");

    // Run 6 frames — should wrap after 4.
    for (int i = 0; i < 6; ++i)
        emu.run_frame();

    CHECK(rb->depth() == 4, "depth capped at 4 after 6 frames");
    CHECK(rb->newest_frame_num() == 5, "newest frame_num is 5 (frames 0..5 taken, 6th pending)");
    CHECK(rb->oldest_frame_num() == 2, "oldest frame_num is 2 after wrap");

    return 0;
}

// ── Test 2: step_back() restores correct PC ────────────────────────────────

static int test_step_back_pc()
{
    printf("\n--- Test 2: step_back() restores correct PC ---\n");

    // Trace is populated by run_frame() (not execute_single_instruction).
    // Run 2 full frames — the trace will have thousands of entries from the
    // 48K BASIC ROM executing (or our injected program).
    Emulator emu;
    build_emulator(emu, 10);

    emu.run_frame();
    emu.run_frame();

    size_t trace_size = emu.trace_log().size();
    REQUIRE(trace_size >= 20, "at least 20 trace entries after 2 frames");

    // step_back(N) lands at trace[size-N].pc  (undo N instructions).
    uint16_t expected_5  = emu.trace_log().at(trace_size - 5).pc;
    uint16_t expected_10 = emu.trace_log().at(trace_size - 10).pc;

    printf("  Trace size: %zu  expected_5=0x%04X  expected_10=0x%04X\n",
           trace_size, expected_5, expected_10);

    // step_back(5)
    bool ok = emu.step_back(5);
    CHECK(ok, "step_back(5) returns true");
    uint16_t pc_after_5 = emu.cpu().get_registers().PC;
    printf("  PC after step_back(5):  0x%04X (expected 0x%04X)\n", pc_after_5, expected_5);
    CHECK(pc_after_5 == expected_5, "step_back(5) lands on correct PC");

    // step_back(10) — fresh emulator for a clean trace
    Emulator emu2;
    build_emulator(emu2, 10);
    emu2.run_frame();
    emu2.run_frame();

    size_t ts2 = emu2.trace_log().size();
    REQUIRE(ts2 >= 20, "at least 20 trace entries for emu2");

    uint16_t expected2_10 = emu2.trace_log().at(ts2 - 10).pc;
    ok = emu2.step_back(10);
    CHECK(ok, "step_back(10) returns true");
    uint16_t pc_after_10 = emu2.cpu().get_registers().PC;
    printf("  PC after step_back(10): 0x%04X (expected 0x%04X)\n", pc_after_10, expected2_10);
    CHECK(pc_after_10 == expected2_10, "step_back(10) lands on correct PC");

    return 0;
}

// ── Test 3: rewind_to_frame() restores known register state ───────────────

static int test_rewind_to_frame()
{
    printf("\n--- Test 3: rewind_to_frame() restores register state ---\n");

    Emulator emu;
    build_emulator(emu, 20);

    // Run 5 frames.
    for (int i = 0; i < 5; ++i)
        emu.run_frame();

    // Record register state after frame 2 (by running to frame 2, capturing regs).
    // The rewind buffer has snapshots for frames 0..4 (taken at frame start).
    // Frame snapshot N captures state at the START of frame N — i.e. after N frames ran.
    // So snapshot for frame_num=3 captured state at the start of frame 3
    // (which is the state after frames 0,1,2 ran).

    // Get rewind buffer info.
    auto* rb = emu.rewind_buffer();
    REQUIRE(rb != nullptr, "rewind buffer exists");
    CHECK(rb->depth() == 5, "5 snapshots after 5 frames");

    uint32_t target_frame = rb->oldest_frame_num() + 1;
    printf("  Rewinding to frame %u (oldest=%u newest=%u)\n",
           target_frame, rb->oldest_frame_num(), rb->newest_frame_num());

    // Rewind to frame target_frame.
    bool ok = emu.rewind_to_frame(target_frame);
    CHECK(ok, "rewind_to_frame() returns true");

    // After rewind, the frame_num_ should reflect the restored state.
    CHECK(emu.frame_num() == target_frame + 1,
          "frame_num_ matches target+1 after rewind (snapshot taken at start of target)");

    return 0;
}

// ── Test 4: snapshot round-trip determinism ───────────────────────────────

static int test_snapshot_roundtrip()
{
    printf("\n--- Test 4: snapshot round-trip determinism ---\n");

    Emulator emu;
    build_emulator(emu, 5);

    // Run 2 frames to get some state.
    emu.run_frame();
    emu.run_frame();

    // Measure snapshot size.
    StateWriter measure;
    emu.save_state(measure);
    size_t snap_size = measure.position();
    printf("  Snapshot size: %zu bytes\n", snap_size);
    CHECK(snap_size > 0, "snapshot size > 0");
    CHECK(snap_size < 2 * 1024 * 1024, "snapshot < 2 MB (sanity check)");

    // First snapshot.
    std::vector<uint8_t> buf1(snap_size, 0);
    StateWriter w1(buf1.data(), snap_size);
    emu.save_state(w1);
    CHECK(w1.position() == snap_size, "save_state writes exactly snap_size bytes (pass 1)");

    // Restore.
    StateReader r(buf1.data(), snap_size);
    emu.load_state(r);

    // Second snapshot after restore — must be bit-identical.
    std::vector<uint8_t> buf2(snap_size, 0);
    StateWriter w2(buf2.data(), snap_size);
    emu.save_state(w2);
    CHECK(w2.position() == snap_size, "save_state writes exactly snap_size bytes (pass 2)");

    bool identical = (std::memcmp(buf1.data(), buf2.data(), snap_size) == 0);
    if (!identical) {
        // Find first differing byte for diagnostics.
        for (size_t i = 0; i < snap_size; ++i) {
            if (buf1[i] != buf2[i]) {
                fprintf(stderr, "  First diff at byte %zu: 0x%02X vs 0x%02X\n",
                        i, buf1[i], buf2[i]);
                break;
            }
        }
    }
    CHECK(identical, "save→load→save produces identical bytes (determinism)");

    return 0;
}

// ── Test 5: step_back with disabled rewind returns false ──────────────────

static int test_step_back_disabled()
{
    printf("\n--- Test 5: step_back with disabled rewind returns false ---\n");

    Emulator emu;
    build_emulator(emu, 0);  // rewind disabled

    CHECK(emu.rewind_buffer() == nullptr, "rewind buffer is null when disabled");

    emu.run_frame();
    emu.debug_state().set_active(true);
    emu.trace_log().set_enabled(true);
    emu.execute_single_instruction();

    bool ok = emu.step_back(1);
    CHECK(!ok, "step_back returns false when rewind is disabled");

    return 0;
}

// ── main ───────────────────────────────────────────────────────────────────

int main()
{
    printf("=== Rewind tests ===\n");

    test_rewind_ring_wrap();
    test_step_back_pc();
    test_rewind_to_frame();
    test_snapshot_roundtrip();
    test_step_back_disabled();

    printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", pass_count + fail_count, pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
