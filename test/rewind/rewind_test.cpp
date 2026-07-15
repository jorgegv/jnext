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
#include <unistd.h>   // mkstemp/write/close/unlink — TZX fixture for the G36 tape-clock row

// ── Helpers ────────────────────────────────────────────────────────────────

static int pass_count = 0;
static int fail_count = 0;

// Skip helper — mirrors test/sdcard/sdcard_test.cpp:48-80 shape.
struct SkipNote { const char* id; const char* reason; };
static std::vector<SkipNote> g_skipped;
static void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
    fprintf(stdout, "SKIP %-16s %s\n", id, reason);
}

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
    CHECK(snap_size < 3 * 1024 * 1024, "snapshot < 3 MB (sanity check)");

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

// ── Test 6: V16-CPU-01 — load_state re-pushes port_ulap_io_en shadow ─────
//
// VHDL oracle:
//   * zxnext.vhd:2439 — `port_ulap_io_en <= internal_port_enable(24);` — bit
//     24 is the first bit of nr_85, i.e. NR 0x85 bit 0.
//   * zxnext.vhd:2685-2686 — `port_bf3b/port_ff3b` are AND-gated by
//     `port_ulap_io_en`.
//   * zxnext.vhd:4496 — `port_contend` OR-folds `port_bf3b` and `port_ff3b`.
//   * zxnext.vhd:1229 — `nr_85_internal_port_enable` resets to all-1.
//
// V15-CPU-NIT-03 (Pass-15 reviewer-promoted) wired NR 0x85 bit 0 into a
// `port_ulap_io_en_` shadow on ContentionModel, with:
//   - `set_port_ulap_io_en(bool)` setter (contention.h)
//   - NR 0x85 write-handler push (emulator.cpp ~line 2466)
//   - init() boot-time push from `nextreg_.cached(0x85) & 0x01`
//     (emulator.cpp ~line 272)
//   - 5 contention regression tests (test/contention/contention_test.cpp,
//     CT-CAT28-V15 group)
//
// V16-CPU-01: the load_state re-push was missed. Emulator::load_state
// rebuilds ContentionModel (which preserves the shadow per
// `rebuild_for_type`), then explicitly re-pushes `port_7ffd_io_en` from
// NR 0x82 bit 1 — but NOT `port_ulap_io_en` from NR 0x85 bit 0. Same
// gap pattern Verify12-memory class-(b) caught for `port_7ffd_io_en`,
// just for the sibling shadow.
//
// Discriminative scenario: take a snapshot with NR 0x85 bit 0 = 1 (default
// post-init). Force the runtime shadow to FALSE (simulating a state where
// runtime NR 0x85 was previously toggled to 0 then back to 1, but where
// the ContentionModel's `port_ulap_io_en_` somehow ended up false at the
// moment of load_state — easiest reproduction: directly set it false on
// the live model). Call load_state. Without the V16 fix, the shadow stays
// false (load_state doesn't push it). With the fix, the shadow is restored
// to true (matching NR 0x85 bit 0 in the snapshot).
//
// We assert post-load `emu.contention().port_ulap_io_en()` matches the
// NR 0x85 bit 0 value in the snapshot. Pre-fix: false (gap). Post-fix:
// true (re-push restored).
//
// Discriminative-check protocol per
// doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-CPU-FIX.md:
//   1. Reproduces the post-fix observable (port_ulap_io_en()==true after
//      load when NR 0x85 b0 = 1).
//   2. Reverting emulator.cpp:6523 `set_port_ulap_io_en()` line MUST flip
//      the assertion to FAIL (post-load shadow stays at the false we
//      planted).
//   3. Re-applying the fix returns the test to PASS.

static int test_v16_cpu_01_load_state_repushes_port_ulap_io_en()
{
    printf("\n--- Test 6: V16-CPU-01 load_state re-pushes port_ulap_io_en ---\n");

    Emulator emu;
    build_emulator(emu, 5);

    // NR 0x85 power-on default is 0x8F (low 4 bits set; bit 7 = reset_type).
    // After build_emulator's init(), the contention shadow is true (b0=1).
    bool init_shadow = emu.contention().port_ulap_io_en();
    CHECK(init_shadow, "post-init shadow == true (NR 0x85 default 0x8F → b0=1)");

    // Save state. The snapshot captures NR 0x85 = 0x8F.
    StateWriter measure;
    emu.save_state(measure);
    size_t snap_size = measure.position();
    std::vector<uint8_t> buf(snap_size, 0);
    StateWriter w(buf.data(), snap_size);
    emu.save_state(w);
    REQUIRE(w.position() == snap_size, "save_state writes exactly snap_size bytes");

    // Plant the gap: directly set the shadow to FALSE on the live model.
    // This simulates the state divergence the V16 fix protects against
    // (e.g. a prior runtime path toggled the shadow but forgot to push
    // the NR-derived value back, OR a partial state-restore sequence
    // where another component modified the shadow before this load).
    emu.contention().set_port_ulap_io_en(false);
    CHECK(!emu.contention().port_ulap_io_en(),
          "shadow planted false (pre-load divergence simulation)");

    // Load state. The fix re-pushes NR 0x85 bit 0 → shadow.
    StateReader r(buf.data(), snap_size);
    emu.load_state(r);

    // Post-load: shadow MUST match NR 0x85 bit 0 in the snapshot (= 1).
    // Pre-fix: shadow stays false (gap). Post-fix: shadow is true.
    bool post_load_shadow = emu.contention().port_ulap_io_en();
    printf("  Post-load shadow: %s (expect true)\n",
           post_load_shadow ? "true" : "false");
    CHECK(post_load_shadow,
          "V16-CPU-01: load_state re-pushes port_ulap_io_en from NR 0x85 b0");

    // Cross-check: also assert the contention model actually fires for
    // ULA+ ports post-load. This guards against future refactors that
    // might keep the shadow accessor-correct but break the gate logic.
    {
        // Use a (hc, vc) inside the active raster window per zxula.vhd:
        // hc=4 (hc_adj=5, hc_adj(3:2)=01 → wait_s=1), vc=100 (visible).
        const uint8_t stretch = emu.contention().contention_tick(
            /*mreq_n=*/true, /*iorq_n=*/false,
            /*rd_n=*/false,  /*wr_n=*/true,
            /*cpu_a=*/0xBF3B, /*hc=*/4, /*vc=*/100);
        // 48K is contended at IORQ to BF3B per VHDL when port_ulap_io_en=1.
        // Pre-fix (no re-push): shadow=false → contention_tick stretch=0.
        // Post-fix: shadow=true → contention_tick stretch>0.
        CHECK(stretch > 0,
              "post-load contention_tick at $BF3B with default param "
              "fires non-zero stretch (V15-CPU-NIT-03 OR-fold sees true shadow)");
    }

    return 0;
}

// ── main ───────────────────────────────────────────────────────────────────

// Save-state schema versioning (was SS-VER-01..07 / G66) is a Phase 11
// future enhancement (EMULATOR-DESIGN-PLAN.md Phase 11), not a gap — the
// schema does not exist yet (only NEX does). Rows removed 2026-07-15 per
// user decision; see the G66 tombstone in the known-gaps doc.

// ── Test: monotonic tape clock survives save/load (G36 review fix) ─────────
//
// Emulator::monotonic_tstates() (base + live FUSE counter) is the clock
// real-time TZX/WAV playback runs on. The base is folded up in
// begin_new_frame(); the live counter is NOT serialised by Z80Cpu (Pass-9
// note there). save_state therefore writes the FOLDED monotonic instant and
// load_state re-establishes it as the base with a zeroed live counter.
// Without that field, a rewind during --tape-realtime playback left the
// base at its pre-rewind (future) value → monotonic_tstates() jumped
// forward by the rewound distance and the tape desynced. This test is the
// rewind suite's only tape-clock coverage: it save_states mid-realtime-TZX
// playback, runs on, load_states, and asserts exact clock continuity plus
// consistent post-restore advancement.
static int test_monotonic_tape_clock_roundtrip()
{
    printf("\n--- Test: monotonic tape clock across save/load (G36) ---\n");

    Emulator emu;
    build_emulator(emu, 5);

    // Minimal TZX: one standard-speed block (flag 0x00 → 8063 pilot pulses
    // ≈ 17.5M T ≈ 250 frames of playback — the tape is still mid-pilot for
    // the whole test). Written to a temp file because load_tzx takes a path.
    static const uint8_t tzx_min[] = {
        'Z','X','T','a','p','e','!',0x1A, 1, 20,
        0x10,             // standard speed data
        0xE8, 0x03,       // pause 1000 ms
        0x03, 0x00,       // 3 data bytes
        0x00, 0xAA, 0xAA, // flag 0x00 (header-class pilot) + payload
    };
    char tzx_path[] = "/tmp/jnext_rewind_tzxXXXXXX";
    int fd = mkstemp(tzx_path);
    REQUIRE(fd >= 0, "mkstemp for TZX fixture");
    REQUIRE(write(fd, tzx_min, sizeof(tzx_min)) == (ssize_t)sizeof(tzx_min),
            "write TZX fixture");
    close(fd);

    bool loaded = emu.load_tzx(tzx_path, /*fast_load=*/false);
    unlink(tzx_path);
    REQUIRE(loaded, "load_tzx (realtime) succeeds");
    CHECK(emu.tzx_tape().is_playing(), "TZX realtime playback is live before snapshot");

    // Run a few frames so the base has folded frames in it, then snapshot.
    for (int i = 0; i < 3; ++i) emu.run_frame();
    const uint64_t mono_at_save = emu.monotonic_tstates();

    StateWriter measure;
    emu.save_state(measure);
    size_t snap_size = measure.position();
    std::vector<uint8_t> buf(snap_size, 0);
    StateWriter w(buf.data(), snap_size);
    emu.save_state(w);

    // Run past the snapshot point — pre-fix this is what poisoned the base.
    for (int i = 0; i < 3; ++i) emu.run_frame();
    const uint64_t mono_before_restore = emu.monotonic_tstates();

    StateReader r(buf.data(), snap_size);
    emu.load_state(r);

    const uint64_t mono_after_restore = emu.monotonic_tstates();
    char msg[160];
    snprintf(msg, sizeof(msg),
             "monotonic tape clock exactly restored (saved=%llu restored=%llu, "
             "pre-restore=%llu)",
             (unsigned long long)mono_at_save,
             (unsigned long long)mono_after_restore,
             (unsigned long long)mono_before_restore);
    CHECK(mono_after_restore == mono_at_save && mono_after_restore < mono_before_restore,
          msg);

    // Post-restore advancement must be consistent: 2 frames advance the
    // clock by 2 nominal frame lengths ± the difference in end-of-frame
    // instruction overshoot between the save point and the measure point
    // (a few T either way; ±100 T bound) — no double-fold of a stale
    // live counter, no lost frames. A missing serialisation would show
    // up as a ~whole-frame discrepancy here or as a jump in the check
    // above, both far outside this window.
    emu.run_frame();
    emu.run_frame();
    const uint64_t delta = emu.monotonic_tstates() - mono_after_restore;
    const uint64_t two_frames = 2ull * 69888ull;  // 48K: 224 T × 312 lines
    snprintf(msg, sizeof(msg),
             "post-restore clock advances by ~2 frames +-100 T (delta=%llu)",
             (unsigned long long)delta);
    CHECK(delta >= two_frames - 100 && delta <= two_frames + 100, msg);

    return 0;
}

// ── Test 8: live enable/disable via resize_rewind_buffer (Task 27 A1b) ─────
// The debugger's Enable Rewind toggle allocates the buffer live via
// Emulator::resize_rewind_buffer() (no restart, mmap-backed after A1).
// Verify the exact contract the GUI depends on:
//   - with rewind_buffer_frames=0 no buffer exists (A1 default),
//   - resize_rewind_buffer(N) mid-run allocates, enables snapshotting AND
//     the instruction trace (step_back() needs it — Task 27 A2),
//   - snapshots start from the next frame,
//   - set_rewind_enabled(false) pauses snapshotting but keeps the history
//     (the toggle's keep-but-pause disable semantics),
//   - step_back() works after a live enable,
//   - resize_rewind_buffer(0) frees the buffer and disables rewind.
static int test_live_enable_resize()
{
    printf("\n--- Test 8: live enable via resize_rewind_buffer (A1b) ---\n");

    Emulator emu;
    build_emulator(emu, 0);              // A1 default: rewind off
    emu.trace_log().set_enabled(false);  // undo build_emulator's enable; the
                                         // live path must switch it back on

    emu.run_frame();
    emu.run_frame();
    CHECK(emu.rewind_buffer() == nullptr, "A1B-01 no buffer with frames=0");
    CHECK(!emu.rewind_enabled(), "A1B-02 rewind disabled with frames=0");

    // Live enable — exactly what the debugger toggle now does.
    emu.resize_rewind_buffer(4);
    REQUIRE(emu.rewind_buffer() != nullptr, "buffer allocated live");
    CHECK(emu.rewind_buffer()->empty(), "A1B-03 buffer empty until next frame");
    CHECK(emu.rewind_enabled(), "A1B-04 snapshotting enabled by resize");
    CHECK(emu.trace_log().enabled(),
          "A1B-05 trace enabled by resize (step_back dependency)");

    emu.run_frame();
    CHECK(emu.rewind_buffer()->depth() == 1,
          "A1B-06 snapshot taken at next frame start");
    emu.run_frame();

    // Keep-but-pause: snapshotting stops, history retained.
    emu.set_rewind_enabled(false);
    size_t depth_at_pause = emu.rewind_buffer()->depth();
    emu.run_frame();
    emu.run_frame();
    CHECK(emu.rewind_buffer()->depth() == depth_at_pause,
          "A1B-07 pause: no new snapshots while disabled");
    CHECK(!emu.rewind_buffer()->empty(),
          "A1B-08 pause: recorded history retained");

    // Resume.
    emu.set_rewind_enabled(true);
    emu.run_frame();
    CHECK(emu.rewind_buffer()->depth() == depth_at_pause + 1,
          "A1B-09 resume: snapshotting continues");

    // step_back after a live enable must succeed.
    CHECK(emu.step_back(1), "A1B-10 step_back works after live enable");

    // Free: resize to 0 drops the buffer and disables rewind.
    emu.resize_rewind_buffer(0);
    CHECK(emu.rewind_buffer() == nullptr, "A1B-11 resize(0) frees the buffer");
    CHECK(!emu.rewind_enabled(), "A1B-12 resize(0) disables rewind");

    return 0;
}

// ── Test 9: StateWriter/StateReader bounds (Task 60b) ──────────────────────
//
// capacity_ used to be stored and never consulted: a write past the buffer
// end memcpy'd into whatever followed the allocation, and a read past the
// end returned adjacent heap bytes. Both are now suppressed + latched into
// a sticky flag (saveable.h).

static int test_state_bounds()
{
    printf("\n--- Test 9: StateWriter/StateReader bounds (Task 60b) ---\n");

    // StateWriter: oversized write is caught and cannot corrupt memory.
    {
        uint8_t buf[16];
        std::memset(buf, 0xAA, sizeof(buf));
        StateWriter w(buf, 4);            // capacity 4; bytes 4..15 are guards
        w.write_u32(0x11223344);          // fills the buffer exactly
        CHECK(!w.overflow(), "SW-BND-00 in-bounds write does not trip overflow");
        w.write_u64(0xDEADBEEFCAFEF00DULL);  // would cross capacity
        CHECK(w.overflow(), "SW-BND-01 write past capacity latches overflow flag");
        bool guards_intact = true;
        for (int i = 4; i < 16; ++i) guards_intact = guards_intact && (buf[i] == 0xAA);
        CHECK(guards_intact, "SW-BND-02 overflowing write leaves adjacent bytes untouched");
        CHECK(w.position() == 12, "SW-BND-03 position keeps counting intended stream offset");
    }

    // Measure mode (buf=nullptr) can never overflow regardless of capacity 0.
    {
        StateWriter m;
        m.write_u64(1); m.write_u64(2);
        CHECK(!m.overflow() && m.position() == 16,
              "SW-BND-04 measure mode never overflows");
    }

    // StateReader: read past the end is caught and zero-filled.
    {
        const uint8_t buf[4] = {1, 2, 3, 4};
        StateReader r(buf, 4);
        (void)r.read_u32();               // consumes the whole buffer
        CHECK(!r.out_of_bounds(), "SR-BND-00 in-bounds read does not trip flag");
        uint64_t v = r.read_u64();        // past the end
        CHECK(r.out_of_bounds(), "SR-BND-01 read past end latches out_of_bounds flag");
        CHECK(v == 0, "SR-BND-02 out-of-bounds read returns zero, not adjacent memory");
    }

    return 0;
}

// ── Test 10: per-subsystem state sentinels (Task 60b) ──────────────────────
//
// Emulator::save_state writes kStateSentinelMagic ^ ordinal (u32) after
// every subsystem block; load_state verifies the same sequence and fails
// loudly with the subsystem NAME on the first mismatch. This converts
// "asymmetric save/load edit silently corrupts every downstream subsystem"
// into an immediate named error.

static int test_state_sentinels()
{
    printf("\n--- Test 10: per-subsystem state sentinels (Task 60b) ---\n");

    Emulator emu;
    build_emulator(emu, 0);
    emu.run_frame();
    emu.run_frame();

    StateWriter measure;
    emu.save_state(measure);
    const size_t snap = measure.position();
    std::vector<uint8_t> buf(snap, 0);
    StateWriter w(buf.data(), snap);
    emu.save_state(w);
    CHECK(!w.overflow() && w.position() == snap,
          "SENT-00 exact-size save fills the buffer without overflow");

    // Pristine buffer restores cleanly (round-trip still works).
    {
        StateReader r(buf.data(), snap);
        const bool ok = emu.load_state(r);
        CHECK(ok, "SENT-OK-01 pristine snapshot: load_state returns true");
        CHECK(emu.last_state_error().empty(),
              "SENT-OK-02 pristine snapshot: last_state_error is empty");
    }

    // Corrupt the 'mmu' sentinel (ordinal 2 in the save_state sequence) and
    // the load must fail naming exactly that subsystem.
    {
        const uint32_t mmu_sentinel = Emulator::kStateSentinelMagic ^ 2u;
        size_t off  = 0;
        int    hits = 0;
        for (size_t i = 0; i + 4 <= snap; ++i) {
            uint32_t v;
            std::memcpy(&v, buf.data() + i, 4);
            if (v == mmu_sentinel) { off = i; ++hits; }
        }
        CHECK(hits == 1, "SENT-CORRUPT-00 mmu sentinel value occurs exactly once in the snapshot");

        std::vector<uint8_t> bad(buf);
        bad[off] ^= 0xFF;
        StateReader r(bad.data(), snap);
        const bool ok = emu.load_state(r);
        CHECK(!ok, "SENT-CORRUPT-01 corrupted mmu sentinel: load_state returns false");
        CHECK(emu.last_state_error() == "mmu",
              "SENT-CORRUPT-02 corrupted mmu sentinel: error names subsystem 'mmu'");

        // Restore a pristine snapshot so the emulator is consistent again.
        StateReader r2(buf.data(), snap);
        CHECK(emu.load_state(r2), "SENT-CORRUPT-03 pristine reload after failed load succeeds");
    }

    // A truncated buffer (simulates a desynced/short snapshot) fails loudly
    // instead of silently zero-loading the missing half.
    {
        StateReader r(buf.data(), snap / 2);
        const bool ok = emu.load_state(r);
        CHECK(!ok, "SENT-TRUNC-01 truncated snapshot: load_state returns false");
        CHECK(!emu.last_state_error().empty(),
              "SENT-TRUNC-02 truncated snapshot: failing subsystem is named");

        StateReader r2(buf.data(), snap);
        emu.load_state(r2);
    }

    return 0;
}

// ── Test 11: RewindBuffer size-bound guard (G67 / Task 60b) ────────────────
//
// snapshot_bytes_ is measured once at construction; if save_state widens
// (or shrinks) afterwards, take_snapshot must refuse to publish the slot
// (RB-FRAME-01..03, formerly skipped under G67).

static int test_rb_frame_guard()
{
    printf("\n--- Test 11: RewindBuffer size-bound guard (G67) ---\n");

    Emulator emu;
    build_emulator(emu, 0);
    emu.run_frame();

    StateWriter measure;
    emu.save_state(measure);
    const size_t snap = measure.position();

    // Undersized slots (simulated post-construction widening): the write
    // overflows the slot and the snapshot must be dropped, not published.
    {
        RewindBuffer rb(3, snap - 16);
        rb.take_snapshot(emu, 100, 1);
        CHECK(rb.empty(), "RB-FRAME-01 undersized slot: snapshot dropped, not published");
    }

    // Clean error path: a correctly-sized buffer still publishes normally
    // (the guard refuses only mismatched writes; it is not sticky).
    {
        RewindBuffer rb(3, snap);
        rb.take_snapshot(emu, 100, 1);
        CHECK(rb.depth() == 1, "RB-FRAME-02 exact-size slot: snapshot publishes normally");
    }

    // Construction-vs-measured mismatch in the other direction (oversized
    // slots, i.e. save_state shrank): also refused — the slot would carry
    // trailing stale bytes and the size claim would be a lie.
    {
        RewindBuffer rb(3, snap + 16);
        rb.take_snapshot(emu, 100, 1);
        CHECK(rb.empty(), "RB-FRAME-03 slot/measured size mismatch: snapshot dropped");
    }

    // Eviction branch: a mismatched write over a FULL ring scribbles the
    // oldest PUBLISHED slot — take_snapshot must unpublish exactly that
    // slot (depth -1, oldest advances) and not publish the failed one.
    // The size mismatch is injected via the shrink test hook because the
    // real state-stream size is compile-time-fixed (see rewind_buffer.h).
    {
        RewindBuffer rb(2, snap);
        rb.take_snapshot(emu, 100, 1);
        rb.take_snapshot(emu, 200, 2);
        CHECK(rb.depth() == 2, "RB-FRAME-04a ring filled with 2 good snapshots");
        rb.shrink_expected_snapshot_bytes_for_test(snap - 16);
        rb.take_snapshot(emu, 300, 3);   // overflows the shrunk claim, ring full
        CHECK(rb.depth() == 1,
              "RB-FRAME-04 failed write over full ring evicts exactly the destroyed oldest");
        CHECK(rb.oldest_frame_cycle() == 200 && rb.oldest_frame_num() == 2,
              "RB-FRAME-05 survivor is the second-oldest snapshot");
        CHECK(rb.newest_frame_cycle() == 200,
              "RB-FRAME-06 failed snapshot is not published as newest");
    }

    return 0;
}

// ── Test 12: rewind chain fails loudly on a corrupted slot (Task 60b) ──────
//
// The blocker case: a sentinel mismatch during a REAL rewind (RewindBuffer →
// Emulator::load_state → rewind_to_cycle → step_back / rewind_to_frame) must
// propagate — pre-fix, restore_nearest dropped load_state's bool and
// step_back()/rewind_to_frame() returned true unconditionally, so the
// debugger reported success over a torn machine.

static int test_rewind_chain_corrupted_slot()
{
    printf("\n--- Test 12: rewind chain fails on corrupted slot (Task 60b) ---\n");

    Emulator emu;
    build_emulator(emu, 5);   // rewind on (instruction trace auto-enabled)
    emu.run_frame();
    emu.run_frame();
    emu.run_frame();

    RewindBuffer* rb = emu.rewind_buffer();
    REQUIRE(rb != nullptr && rb->depth() >= 2, "rewind buffer holds >= 2 real snapshots");

    // Corrupt the 'mmu' sentinel (ordinal 2) in EVERY stored slot, so
    // whichever snapshot the rewind selects fails verification.
    const uint32_t mmu_sentinel = Emulator::kStateSentinelMagic ^ 2u;
    size_t corrupted = 0;
    for (size_t i = 0; i < rb->depth(); ++i) {
        uint8_t* d = rb->slot_data_for_test(i);
        for (size_t off = 0; off + 4 <= rb->snapshot_bytes(); ++off) {
            uint32_t v;
            std::memcpy(&v, d + off, 4);
            if (v == mmu_sentinel) { d[off] ^= 0xFF; ++corrupted; break; }
        }
    }
    CHECK(corrupted == rb->depth(),
          "SENT-CHAIN-00 mmu sentinel corrupted in every stored slot");

    // step_back must fail through the whole chain, not pause-as-successful.
    const bool sb = emu.step_back(1);
    CHECK(!sb, "SENT-CHAIN-01 step_back returns false on corrupted slot");
    CHECK(emu.last_state_error() == "mmu",
          "SENT-CHAIN-02 chain failure names subsystem 'mmu'");

    // rewind_to_frame must fail the same way.
    const bool rf = emu.rewind_to_frame(rb->oldest_frame_num());
    CHECK(!rf, "SENT-CHAIN-03 rewind_to_frame returns false on corrupted slot");

    // rewind_to_cycle is the shared workhorse — verify its own contract.
    const uint64_t rc = emu.rewind_to_cycle(rb->newest_frame_cycle());
    CHECK(rc == UINT64_MAX,
          "SENT-CHAIN-04 rewind_to_cycle returns UINT64_MAX on corrupted slot");

    return 0;
}

// SS-VER-01..07 (G66) removed 2026-07-15 — reclassified as a Phase 11
// future enhancement (see comment above test_monotonic_tape_clock_roundtrip).
// RB-FRAME-01..03 (G67) became real rows in Test 11 (Task 60b).
//
// WONT G68: rewind sub-frame granularity is an explicit design choice
// per EMULATOR-DESIGN-PLAN.md Phase 8 Step 4 (frame snapshots ring
// buffer). Will become a row only if a user asks; not a skip() entry.

int main()
{
    printf("=== Rewind tests ===\n");

    test_rewind_ring_wrap();
    test_step_back_pc();
    test_rewind_to_frame();
    test_snapshot_roundtrip();
    test_step_back_disabled();
    test_v16_cpu_01_load_state_repushes_port_ulap_io_en();
    test_monotonic_tape_clock_roundtrip();
    test_live_enable_resize();
    test_state_bounds();
    test_state_sentinels();
    test_rb_frame_guard();
    test_rewind_chain_corrupted_slot();

    printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
           pass_count + fail_count + (int)g_skipped.size(),
           pass_count, fail_count, g_skipped.size());
    return fail_count > 0 ? 1 : 0;
}
