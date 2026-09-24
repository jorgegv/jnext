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
#include "debug/debug_state.h"
#include "memory/attribute_mux.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "peripheral/dma.h"
#include "peripheral/divmmc.h"
#include "peripheral/multiface.h"
#include "peripheral/sd_card.h"
#include "core/warm_start_cache.h"
#include "input/md6_connector_x2.h"
#include "input/membrane_stick.h"
#include "input/keyboard.h"
#include "peripheral/i2c.h"
#include "audio/mixer.h"
#include "cpu/z80_cpu.h"
#include "video/layer2.h"
#include "video/lores.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/sprites.h"
#include "video/tilemap.h"
#include "video/ula.h"
#include "memory/rom.h"
#include "port/nextreg.h"
#include "peripheral/copper.h"
#include "save/state_desc.h"

#include <cstring>
#include <cstdio>
#include <string>
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

// ID-carrying form of CHECK. GH #201: `test/traceability-exceptions.conf`
// lists this suite's planned rows because it has no VHDL counterpart and so
// no plan doc — but every one of them named an assertion that this file
// ALREADY made, anonymously, through the CHECK macro. An ID built nowhere is
// an ID no source reader can see, so the matrix published all of them as
// `missing` while they ran and passed. Spelling the ID out as a literal is
// the whole fix; the condition and the message are unchanged.
static void check(const char* id, bool cond, const char* desc) {
    if (!cond) {
        fprintf(stderr, "FAIL %s: %s\n", id, desc);
        ++fail_count;
    } else {
        fprintf(stdout, "PASS %s: %s\n", id, desc);
        ++pass_count;
    }
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
    check("RING-01", rb->empty(), "rewind buffer starts empty");

    // Run 6 frames — should wrap after 4.
    for (int i = 0; i < 6; ++i)
        emu.run_frame();

    check("RING-02", rb->depth() == 4, "ring depth caps at its 4-frame capacity after 6 frames");
    check("RING-03", rb->newest_frame_num() == 5, "newest frame_num is 5 after the wrap (frames 0..5 taken)");
    check("RING-04", rb->oldest_frame_num() == 2, "oldest frame_num is 2 after the wrap (the two earliest were overwritten)");

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
    check("SB-01", ok, "step_back(5) reports success");
    uint16_t pc_after_5 = emu.cpu().get_registers().PC;
    printf("  PC after step_back(5):  0x%04X (expected 0x%04X)\n", pc_after_5, expected_5);
    check("SB-02", pc_after_5 == expected_5, "step_back(5) lands on the PC the trace recorded 5 instructions back");

    // step_back(10) — fresh emulator for a clean trace
    Emulator emu2;
    build_emulator(emu2, 10);
    emu2.run_frame();
    emu2.run_frame();

    size_t ts2 = emu2.trace_log().size();
    REQUIRE(ts2 >= 20, "at least 20 trace entries for emu2");

    uint16_t expected2_10 = emu2.trace_log().at(ts2 - 10).pc;
    ok = emu2.step_back(10);
    check("SB-03", ok, "step_back(10) reports success");
    uint16_t pc_after_10 = emu2.cpu().get_registers().PC;
    printf("  PC after step_back(10): 0x%04X (expected 0x%04X)\n", pc_after_10, expected2_10);
    check("SB-04", pc_after_10 == expected2_10, "step_back(10) lands on the PC the trace recorded 10 instructions back");

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
    check("RTF-01", rb->depth() == 5, "five frame snapshots are held after five frames");

    uint32_t target_frame = rb->oldest_frame_num() + 1;
    printf("  Rewinding to frame %u (oldest=%u newest=%u)\n",
           target_frame, rb->oldest_frame_num(), rb->newest_frame_num());

    // Rewind to frame target_frame.
    bool ok = emu.rewind_to_frame(target_frame);
    check("RTF-02", ok, "rewind_to_frame() reports success for a frame still in the ring");

    // After rewind, the frame_num_ should reflect the restored state.
    check("RTF-03", emu.frame_num() == target_frame + 1,
          "frame_num is target+1 after the rewind (the snapshot is taken at the start of the target frame)");

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
    check("RW-RT-01", snap_size > 0, "a measured snapshot is larger than zero bytes");
    check("RW-RT-02", snap_size < 3 * 1024 * 1024, "a measured snapshot stays under the 3 MB sanity bound");

    // First snapshot.
    std::vector<uint8_t> buf1(snap_size, 0);
    StateWriter w1(buf1.data(), snap_size);
    emu.save_state(w1);
    check("RW-RT-03", w1.position() == snap_size, "save_state writes exactly the measured snap_size bytes (pass 1)");

    // Restore.
    StateReader r(buf1.data(), snap_size);
    emu.load_state(r);

    // Second snapshot after restore — must be bit-identical.
    std::vector<uint8_t> buf2(snap_size, 0);
    StateWriter w2(buf2.data(), snap_size);
    emu.save_state(w2);
    check("RT-04", w2.position() == snap_size, "save_state writes exactly the measured snap_size bytes again after a load (pass 2)");

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
    check("RT-05", identical, "save -> load -> save produces byte-identical snapshots (determinism)");

    return 0;
}

// ── Test 5: step_back with disabled rewind returns false ──────────────────

static int test_step_back_disabled()
{
    printf("\n--- Test 5: step_back with disabled rewind returns false ---\n");

    Emulator emu;
    build_emulator(emu, 0);  // rewind disabled

    check("SBD-01", emu.rewind_buffer() == nullptr, "no rewind buffer is allocated when rewind is disabled");

    emu.run_frame();
    emu.debug_state().set_active(true);
    emu.trace_log().set_enabled(true);
    emu.execute_single_instruction();

    bool ok = emu.step_back(1);
    check("SBD-02", !ok, "step_back reports failure when rewind is disabled");

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

    // Task 60e: the GUI tells the user to reset out of a corrupt restore, so a
    // reset must clear the flag. The soft reset re-runs init() in place, which
    // owns that clear since GH #239 (it used to be duplicated in soft_reset()
    // and the since-removed in-place Emulator::reset()). The hard reset needs
    // no row of its own: its cold boot reconstructs the Emulator.
    {
        StateReader r(buf.data(), snap / 2);
        const bool failed = !emu.load_state(r) && !emu.last_state_error().empty();
        emu.soft_reset();
        CHECK(failed && emu.last_state_error().empty(),
              "SENT-RESET-01 a soft reset clears the failed-restore corruption flag");
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
        check("RB-FRAME-01", rb.empty(), "undersized slot (simulated post-construction widening): the snapshot is dropped, not published");
    }

    // Clean error path: a correctly-sized buffer still publishes normally
    // (the guard refuses only mismatched writes; it is not sticky).
    {
        RewindBuffer rb(3, snap);
        rb.take_snapshot(emu, 100, 1);
        check("RB-FRAME-02", rb.depth() == 1, "exact-size slot still publishes normally: the size guard refuses only mismatched writes and is not sticky");
    }

    // Construction-vs-measured mismatch in the other direction (oversized
    // slots, i.e. save_state shrank): also refused — the slot would carry
    // trailing stale bytes and the size claim would be a lie.
    {
        RewindBuffer rb(3, snap + 16);
        rb.take_snapshot(emu, 100, 1);
        check("RB-FRAME-03", rb.empty(), "oversized slot (save_state shrank since construction) is refused too: the size claim would otherwise be a lie");
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

// ── Test 11b: snapshot size is independent of guest behaviour (issue #42) ──
//
// RewindBuffer sizes every slot from ONE dry-run save_state() at
// construction and then requires each snapshot to be exactly that size.
// That only works if the state stream has a constant width — which it did
// not: Ula::save_state wrote the per-scanline port-0xFF change log as a
// count-prefixed variable-length array (3 bytes/entry) and
// Keyboard::save_state did the same for the auto-type queue (20
// bytes/entry). So the first frame in which the guest touched port 0xFF
// produced a snapshot 3 bytes too large and it was silently DROPPED — for
// a program that writes port 0xFF every frame (any Timex mode change),
// rewind recorded nothing at all while reporting itself enabled.
//
// These rows pin the invariant the fixed-slot design assumes, at the two
// fields that broke it. Pre-fix, RB-SIZE-01/02/04/05 all fail.

static int test_snapshot_size_invariance()
{
    printf("\n--- Test 11b: snapshot size vs guest behaviour (issue #42) ---\n");

    Emulator emu;
    build_emulator(emu, 0);
    emu.run_frame();

    // Baseline: empty port-0xFF log, empty auto-type queue.
    StateWriter base;
    emu.save_state(base);
    const size_t snap = base.position();
    REQUIRE(emu.renderer().ula().port_ff_change_log_size() == 0,
            "precondition: port-0xFF log starts empty");

    // ---- port-0xFF change log ------------------------------------------
    // Drive the real setter, the same path a guest OUT (0xFF),A takes.
    emu.renderer().ula().set_screen_mode(0x02);
    emu.renderer().ula().set_screen_mode(0x06);
    const size_t logged = emu.renderer().ula().port_ff_change_log_size();
    REQUIRE(logged >= 2, "precondition: port-0xFF writes were logged");

    StateWriter after_ff;
    emu.save_state(after_ff);
    CHECK(after_ff.position() == snap,
          "RB-SIZE-01 snapshot size unchanged by port-0xFF writes");

    {
        RewindBuffer rb(3, snap);
        rb.take_snapshot(emu, 100, 1);
        CHECK(rb.depth() == 1,
              "RB-SIZE-02 snapshot taken on a port-0xFF frame is published, not dropped");
    }

    // The fix must not buy constant width by throwing the log away: the
    // in-flight entries still have to round-trip (S5-PSL.05).
    {
        RewindBuffer rb(3, snap);
        rb.take_snapshot(emu, 100, 1);
        emu.renderer().ula().set_screen_mode(0x00);   // disturb live state
        // Deliberately CHECK, not REQUIRE: if the snapshot was dropped this
        // row must FAIL and the suite must still report its pinned row
        // count, rather than aborting and tripping the manifest guard with
        // a second, misleading fault.
        CHECK(rb.restore_nearest(100, emu) == 100 &&
              emu.renderer().ula().port_ff_change_log_size() == logged,
              "RB-SIZE-03 port-0xFF log content survives the snapshot round-trip");
    }

    // ---- auto-type queue ------------------------------------------------
    {
        Emulator emu2;
        build_emulator(emu2, 0);
        emu2.run_frame();
        StateWriter b2;
        emu2.save_state(b2);
        const size_t snap2 = b2.position();

        // The instant-TAP LOAD"" sequence, i.e. the real in-flight case.
        std::vector<Keyboard::AutoKey> keys = {
            {6, 3, -1, -1, 5}, {5, 0, 7, 1, 5}, {5, 0, 7, 1, 5}, {6, 0, -1, -1, 5},
        };
        emu2.keyboard().queue_auto_type(keys);

        StateWriter after_kb;
        emu2.save_state(after_kb);
        CHECK(after_kb.position() == snap2,
              "RB-SIZE-04 snapshot size unchanged by a queued auto-type sequence");

        RewindBuffer rb(3, snap2);
        rb.take_snapshot(emu2, 100, 1);
        CHECK(rb.depth() == 1,
              "RB-SIZE-05 snapshot taken mid-auto-type is published, not dropped");

        // The cap is what makes the constant width honest — an oversized
        // sequence is truncated at the queue, never silently widening the
        // snapshot afterwards.
        std::vector<Keyboard::AutoKey> too_many(Keyboard::MAX_AUTO_TYPE_KEYS + 8,
                                                {6, 0, -1, -1, 5});
        emu2.keyboard().queue_auto_type(too_many);
        StateWriter after_cap;
        emu2.save_state(after_cap);
        CHECK(after_cap.position() == snap2,
              "RB-SIZE-06 over-cap auto-type sequence still yields a constant-size snapshot");
    }

    // ---- content round-trip, not just width -----------------------------
    // Review finding: RB-SIZE-04/05/06 and the pre-existing SL-KBD-03 all
    // check size or a boolean auto_typing() flag, so a field-order bug in
    // Keyboard::load_state (row1/col1 swapped, say) corrupts every rewound
    // keystroke while the whole suite stays green. Assert the restored
    // queue actually plays the RIGHT key. 'F' is row 1 col 3; its row/col
    // transpose is row 3 col 1 = '2'. Both are REAL matrix cells, which is
    // what makes this discriminative — a key like 'J' (6,3) transposes to
    // column 6, which does not exist (a row read has only 5 key bits; bit 6
    // is the EAR line), so a swap there would be invisible here.
    {
        Emulator emu3;
        build_emulator(emu3, 0);
        emu3.run_frame();

        std::vector<Keyboard::AutoKey> f_only = { {1, 3, -1, -1, 5} };
        emu3.keyboard().queue_auto_type(f_only);

        StateWriter w3;
        emu3.save_state(w3);
        RewindBuffer rb(3, w3.position());
        rb.take_snapshot(emu3, 100, 1);

        emu3.keyboard().queue_auto_type({});   // wipe the live queue
        // CHECK, not REQUIRE — see the note at the fallback-colour row above:
        // aborting here would skip RB-SIZE-09/10/11 and report a row count the
        // manifest guard then flags as a SECOND, misleading fault.
        CHECK(rb.restore_nearest(100, emu3) == 100,
              "RB-SIZE-06b slot restores for the auto-type content row");

        emu3.keyboard().tick_auto_type();      // play the restored key
        const uint8_t row1 = emu3.keyboard().read_rows(0xFD);  // A9  low -> row 1
        const uint8_t row3 = emu3.keyboard().read_rows(0xF7);  // A11 low -> row 3
        CHECK((row1 & 0x08) == 0,
              "RB-SIZE-07 restored auto-type queue plays the correct key (row 1 col 3 = F)");
        CHECK((row3 & 0x02) != 0,
              "RB-SIZE-08 restored auto-type queue does not press the row/col transpose (3,1)");
    }

    // Same for the UART FIFOs — the third variable-length field. Inject a
    // distinctive byte sequence, snapshot, drain the live FIFO, restore,
    // and read the bytes back in order through the real RX path.
    {
        Emulator emu4;
        build_emulator(emu4, 0);
        emu4.run_frame();

        StateWriter b4;
        emu4.save_state(b4);
        const size_t snap4 = b4.position();

        emu4.uart().inject_rx(0, 0x41);
        emu4.uart().inject_rx(0, 0x42);
        emu4.uart().inject_rx(0, 0x43);

        StateWriter after_uart;
        emu4.save_state(after_uart);
        CHECK(after_uart.position() == snap4,
              "RB-SIZE-09 snapshot size unchanged by bytes in flight in the UART RX FIFO");

        RewindBuffer rb(3, snap4);
        rb.take_snapshot(emu4, 100, 1);
        CHECK(rb.depth() == 1,
              "RB-SIZE-10 snapshot taken with a non-empty UART FIFO is published, not dropped");

        // Re-review finding: RB-SIZE-09/10 above pin WIDTH only — scrambling
        // the restored byte ORDER left the whole 5292-row suite green, and no
        // uart_test row exercises save_state/load_state at all. Read the bytes
        // back through the REAL port-read path (port_reg 0 = Rx) after
        // polluting the live FIFO, so order and content are both pinned.
        emu4.uart().read(0);                   // drain one live byte
        emu4.uart().inject_rx(0, 0xAA);        // pollute what remains
        CHECK(rb.restore_nearest(100, emu4) == 100,
              "RB-SIZE-10b slot restores for the UART content row");
        const uint8_t u1 = emu4.uart().read(0);
        const uint8_t u2 = emu4.uart().read(0);
        const uint8_t u3 = emu4.uart().read(0);
        CHECK(u1 == 0x41 && u2 == 0x42 && u3 == 0x43,
              "RB-SIZE-11 UART RX FIFO content and order survive the round-trip");
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

// ── Test 13: a rewind restores the render state too (GH #261) ─────────────
//
// Every video subsystem keeps per-scanline render HISTORY beside its live
// registers: change logs with a frame-start baseline (palette, Layer 2,
// sprites, tilemap NR 0x6B, ULA scroll and palette selectors, attribute
// mux, NR 0x15) and per-line snapshot arrays (stencil/blend/NR 0x14/ULA
// enable/ULA clip, tilemap scroll, LoRes). begin_new_frame() rebuilds all of
// it; a snapshot is taken just BEFORE that, and none of it (bar the NR 0x4A
// fallback, the border and the port-0xFF log) is in the stream.
//
// rewind_to_frame() renders straight after the load. Pre-fix that render's
// rewind_to_baseline() copied the PRE-rewind frame's baselines into the LIVE
// registers and replayed its logs, so the rewound machine carried the later
// frame's palette, Layer 2 scroll, sprites, NR 0x6B... for good, not just on
// screen; the Ula palette-selector mirrors were never restored by any rewind.
//
// Fixture: three register sets. S0 is live when the target snapshot is
// taken (frame 1 start); S1 is written between frames 1 and 2; S2 is written
// by the Copper at cvc 100 of frame 3, so the logs being rewound over are
// non-empty and the per-line arrays hold an S1/S2 split. Every value below
// differs from its reset default, so "restored" is not confused with
// "reset". No VHDL oracle: the property is jnext's own — a restore must
// reproduce the snapshot — so expectations are what the snapshot held,
// read back at the moment it was taken.

static void rw_nr(Emulator& emu, uint8_t reg, uint8_t val)
{
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

static void rw_park(Emulator& emu, uint16_t pc)
{
    auto regs = emu.cpu().get_registers();
    regs.PC = pc;
    regs.SP = 0xFFFD;
    regs.IFF1 = 0; regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
}

static void rw_sprite0(Emulator& emu, uint8_t x, uint8_t y, uint8_t pattern_fill)
{
    rw_nr(emu, 0x34, 0x00);
    rw_nr(emu, 0x35, x);
    rw_nr(emu, 0x36, y);
    rw_nr(emu, 0x37, 0x00);
    rw_nr(emu, 0x38, 0x80);          // visible, pattern 0, 4-byte form
    emu.port().out(0x303B, 0x00);    // pattern slot 0
    for (int i = 0; i < 256; ++i)
        emu.port().out(0x005B, pattern_fill);
}

// ZXN, CPU parked on a HALT at 0x8000, S0 programmed, frame 0 run. On return
// the machine is exactly where the frame-1 snapshot is taken.
static void rw_build_s0(Emulator& emu, int rewind_frames)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = rewind_frames;
    emu.init(cfg);
    emu.trace_log().set_enabled(true);
    emu.mmu().write(0x8000, 0x76);   // HALT
    rw_park(emu, 0x8000);

    // GH #266 — the tilemap is ON in S0, so RWR-11's pixel comparison covers
    // it: map at bank 5 0x2000 (NR 0x6E 0x20), tiles at 0x3000 (NR 0x6F
    // 0x30), clear of the ULA screen. Tile t, row r, pixel x holds index
    // 1 + (3t + x + r) % 14 (never 0x0F, the NR 0x4C transparency index), the
    // map cell (x, y) holds tile (x + 2y) % 4, and the two tilemap palettes
    // differ at every one of those indices — so a stale scroll, NR 0x6B mode
    // or NR 0x6B b4 palette select in the rewound render moves pixels. S0
    // selects tilemap palette 1 because the history rewound over ends on
    // palette 0 (S2's NR 0x6B = 0xC1): a stale selector cannot match S0.
    {
        uint8_t* bank5 = emu.mmu().bank5_vram();
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 40; ++x) {
                bank5[0x2000 + (y * 40 + x) * 2]     =
                    static_cast<uint8_t>((x + 2 * y) % 4);
                bank5[0x2000 + (y * 40 + x) * 2 + 1] = 0x00;
            }
        for (int t = 0; t < 4; ++t)
            for (int r = 0; r < 8; ++r)
                for (int b = 0; b < 4; ++b) {
                    const int hi = 1 + (3 * t + 2 * b + r) % 14;
                    const int lo = 1 + (3 * t + 2 * b + 1 + r) % 14;
                    bank5[0x3000 + t * 32 + r * 4 + b] =
                        static_cast<uint8_t>((hi << 4) | lo);
                }
        for (int i = 1; i <= 14; ++i) {
            rw_nr(emu, 0x43, 0x30);      // write tilemap palette 0
            rw_nr(emu, 0x40, static_cast<uint8_t>(i));
            rw_nr(emu, 0x41, static_cast<uint8_t>(i * 17));
            rw_nr(emu, 0x43, 0x70);      // write tilemap palette 1
            rw_nr(emu, 0x40, static_cast<uint8_t>(i));
            rw_nr(emu, 0x41, static_cast<uint8_t>(0xFF - i * 17));
        }
        rw_nr(emu, 0x6E, 0x20);
        rw_nr(emu, 0x6F, 0x30);
        rw_nr(emu, 0x6B, 0x90);          // tilemap on, 40x32, attributes, palette 1
    }
    // Layer 2 shows entry 0x30 (index 0, NR 0x70 offset 3 on the high
    // nibble); make the two banks differ there, so a render with a stale L2
    // selector shows.
    rw_nr(emu, 0x43, 0x50);          // write L2 palette 1
    rw_nr(emu, 0x40, 0x30);
    rw_nr(emu, 0x41, 0x49);
    rw_nr(emu, 0x43, 0x10);          // write L2 palette 0; all selectors 0
    rw_nr(emu, 0x40, 0x05);
    rw_nr(emu, 0x41, 0x1C);          // L2[5] = 0x1C
    rw_nr(emu, 0x16, 0x10); rw_nr(emu, 0x17, 0x08);
    rw_nr(emu, 0x1C, 0x0F);          // all clip indices to x1
    // Layer 2 clip starts right of the ULA clip's left edge, so ULA pixels
    // stay visible and a stale ULA enable (NR 0x68 b7) shows.
    for (uint8_t v : {0x40, 0xF0, 0x04, 0xB0}) rw_nr(emu, 0x18, v);
    for (uint8_t v : {0x10, 0xEF, 0x08, 0xB7}) rw_nr(emu, 0x1A, v);
    rw_nr(emu, 0x12, 0x09);
    rw_nr(emu, 0x69, 0x80);          // Layer 2 on
    rw_nr(emu, 0x70, 0x03);          // 256x192, palette offset 3
    rw_sprite0(emu, 0x20, 0x30, 0x11);
    rw_nr(emu, 0x26, 0x03); rw_nr(emu, 0x27, 0x04);
    rw_nr(emu, 0x68, 0x20);          // ULA on, blend 01, no stencil
    rw_nr(emu, 0x14, 0x12);
    rw_nr(emu, 0x30, 0x05); rw_nr(emu, 0x31, 0x06);
    rw_nr(emu, 0x15, 0x01);          // sprites on, LoRes off, SLU
    rw_nr(emu, 0x32, 0x07); rw_nr(emu, 0x33, 0x09); rw_nr(emu, 0x6A, 0x02);
    emu.mmu().write(0x5800, 0x38);   // attribute byte 0
    emu.run_frame();                 // frame 0
}

static void rw_write_s1(Emulator& emu)
{
    rw_nr(emu, 0x43, 0x1E);          // ULA/L2/sprite selectors -> second
    rw_nr(emu, 0x40, 0x05);
    rw_nr(emu, 0x41, 0xE0);
    rw_nr(emu, 0x6B, 0x90);          // tilemap on, TM palette select 1
    rw_nr(emu, 0x16, 0x40); rw_nr(emu, 0x17, 0x20);
    rw_nr(emu, 0x1C, 0x0F);
    for (uint8_t v : {0x20, 0xE0, 0x10, 0xA0}) rw_nr(emu, 0x18, v);
    for (uint8_t v : {0x30, 0xC0, 0x20, 0x90}) rw_nr(emu, 0x1A, v);
    rw_nr(emu, 0x12, 0x0C);
    rw_nr(emu, 0x69, 0x00);          // Layer 2 off
    rw_nr(emu, 0x70, 0x15);          // 320x256, palette offset 5
    rw_sprite0(emu, 0x55, 0x60, 0x22);
    rw_nr(emu, 0x26, 0x11); rw_nr(emu, 0x27, 0x22);
    rw_nr(emu, 0x68, 0xE5);          // ULA off, blend 11, fine scroll, stencil
    rw_nr(emu, 0x14, 0x34);
    rw_nr(emu, 0x30, 0x22); rw_nr(emu, 0x31, 0x33);
    rw_nr(emu, 0x15, 0x81);          // LoRes on
    rw_nr(emu, 0x32, 0x17); rw_nr(emu, 0x33, 0x19); rw_nr(emu, 0x6A, 0x12);
    emu.mmu().write(0x5800, 0x47);
}

static void rw_load_copper_s2(Emulator& emu)
{
    auto word = [&emu](uint16_t w) {
        rw_nr(emu, 0x60, static_cast<uint8_t>(w >> 8));
        rw_nr(emu, 0x60, static_cast<uint8_t>(w));
    };
    auto move = [&word](uint8_t reg, uint8_t val) {
        word(static_cast<uint16_t>((reg << 8) | val));
    };
    rw_nr(emu, 0x61, 0x00);
    rw_nr(emu, 0x62, 0x00);
    word(0x8000u | 100u);            // WAIT cvc 100
    move(0x40, 0x05); move(0x41, 0xFC);
    move(0x6B, 0xC1);
    move(0x16, 0x70);
    move(0x34, 0x00); move(0x35, 0x66);
    move(0x26, 0x33);
    move(0x68, 0x41);
    move(0x14, 0x56);
    move(0x30, 0x44);
    move(0x32, 0x27);
    word(0x8000u | 511u);            // HALT
    rw_nr(emu, 0x62, 0xC0);          // reset each frame + run
}

// What the render history must reproduce, read at the snapshot instant.
struct RwState {
    uint32_t l2_pal5;
    uint16_t l2_sx; uint8_t l2_sy, l2_cx1, l2_cx2, l2_cy1, l2_cy2, l2_bank;
    bool l2_en; uint8_t l2_res, l2_poff;
    uint8_t spr0[5], pat[256];
    uint8_t tm_ctl; bool tm_en;
    uint8_t ula_sx, ula_sy; bool ula_fine;
    bool sel_ula, sel_l2, sel_spr, sel_tm;
    uint8_t mux0, vram_attr0;
    bool stencil; uint8_t blend, nr14;
    Renderer::UlaClipWindow clip;
    uint16_t tm_scroll_x; uint8_t tm_scroll_y;
    Lores::LineState lores;
};

static RwState rw_capture(Emulator& emu)
{
    RwState s{};
    Layer2& l2 = emu.layer2();
    s.l2_pal5 = emu.palette().layer2_colour(false, 5);
    s.l2_sx = l2.scroll_x(); s.l2_sy = l2.scroll_y();
    s.l2_cx1 = l2.clip_x1(); s.l2_cx2 = l2.clip_x2();
    s.l2_cy1 = l2.clip_y1(); s.l2_cy2 = l2.clip_y2();
    s.l2_bank = l2.active_bank(); s.l2_en = l2.enabled();
    s.l2_res = l2.resolution(); s.l2_poff = l2.palette_offset();
    for (uint8_t b = 0; b < 5; ++b) s.spr0[b] = emu.sprites().read_attr_byte(0, b);
    for (int i = 0; i < 256; ++i)
        s.pat[i] = emu.sprites().read_pattern_byte(static_cast<uint16_t>(i));
    s.tm_ctl = emu.tilemap().get_control(); s.tm_en = emu.tilemap().enabled();
    Ula& ula = emu.ula();
    s.ula_sx = ula.get_ula_scroll_x_coarse(); s.ula_sy = ula.get_ula_scroll_y();
    s.ula_fine = ula.get_ula_fine_scroll_x();
    s.sel_ula = ula.get_active_ula_palette();
    s.sel_l2  = ula.get_active_layer2_palette();
    s.sel_spr = ula.get_active_sprite_palette();
    s.sel_tm  = ula.get_active_tilemap_palette();
    s.mux0 = emu.mmu().attr_mux5().current(0);
    s.vram_attr0 = emu.mmu().read(0x5800);
    // Out-of-range rows return the live register (renderer.h accessors).
    const Renderer& r = emu.renderer();
    s.stencil = r.stencil_mode_for_line(-1);
    s.blend   = r.blend_mode_for_line(-1);
    s.nr14    = r.transparent_rgb_for_line(-1);
    s.clip    = r.ula_clip_for_line(-1);
    // Frame 0 ran with S0 constant, so row 0 of the tilemap snapshot is S0.
    s.tm_scroll_x = emu.tilemap().scroll_x_for_line(0);
    s.tm_scroll_y = emu.tilemap().scroll_y_for_line(0);
    s.lores = r.lores().state_for_line(-1);
    return s;
}

static int test_rewind_restores_render_state()
{
    printf("\n--- Test 13: rewind restores render state (GH #261) ---\n");

    Emulator emu;
    rw_build_s0(emu, 10);
    const RwState s0 = rw_capture(emu);
    REQUIRE(s0.l2_sx == 0x10 && s0.tm_scroll_x == 0x05 && s0.lores.scroll_x == 0x07,
            "RWR fixture: S0 is live when the frame-1 snapshot is taken");
    emu.run_frame();                 // frame 1 — the target snapshot
    rw_write_s1(emu);
    emu.run_frame();                 // frame 2
    rw_load_copper_s2(emu);
    emu.run_frame();                 // frame 3: S1 above cvc 100, S2 below
    // The history being rewound over must really be stale, or every row
    // below passes vacuously: the Copper ran (S2 live) and the per-line
    // arrays hold the S1/S2 split.
    REQUIRE(emu.layer2().scroll_x() == 0x70 &&
            emu.renderer().blend_mode_for_line(0) == 0x03 &&
            emu.renderer().blend_mode_for_line(Renderer::FB_HEIGHT - 1) == 0x02 &&
            emu.tilemap().scroll_x_for_line(0) == 0x22,
            "RWR fixture: frame 3 left S1/S2 render history behind");

    REQUIRE(emu.rewind_to_frame(1), "RWR fixture: rewind_to_frame(1) succeeds");
    const RwState a = rw_capture(emu);

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "RWR-01 palette entry restored (L2[5] %08X, snapshot %08X)",
                  a.l2_pal5, s0.l2_pal5);
    CHECK(a.l2_pal5 == s0.l2_pal5, msg);

    std::snprintf(msg, sizeof(msg),
                  "RWR-02 Layer 2 scroll/clip/bank/enable/NR 0x70 restored "
                  "(sx %03X sy %02X clip %02X/%02X/%02X/%02X bank %02X en %d "
                  "res %u poff %u)",
                  a.l2_sx, a.l2_sy, a.l2_cx1, a.l2_cx2, a.l2_cy1, a.l2_cy2,
                  a.l2_bank, a.l2_en, a.l2_res, a.l2_poff);
    CHECK(a.l2_sx == s0.l2_sx && a.l2_sy == s0.l2_sy &&
          a.l2_cx1 == s0.l2_cx1 && a.l2_cx2 == s0.l2_cx2 &&
          a.l2_cy1 == s0.l2_cy1 && a.l2_cy2 == s0.l2_cy2 &&
          a.l2_bank == s0.l2_bank && a.l2_en == s0.l2_en &&
          a.l2_res == s0.l2_res && a.l2_poff == s0.l2_poff, msg);

    std::snprintf(msg, sizeof(msg),
                  "RWR-03 sprite attributes and pattern RAM restored "
                  "(spr0 X %02X, pattern[0] %02X)", a.spr0[0], a.pat[0]);
    CHECK(std::memcmp(a.spr0, s0.spr0, sizeof(a.spr0)) == 0 &&
          std::memcmp(a.pat, s0.pat, sizeof(a.pat)) == 0, msg);

    std::snprintf(msg, sizeof(msg),
                  "RWR-04 tilemap NR 0x6B restored (%02X, enabled %d)",
                  a.tm_ctl, a.tm_en);
    CHECK(a.tm_ctl == s0.tm_ctl && a.tm_en == s0.tm_en, msg);

    std::snprintf(msg, sizeof(msg),
                  "RWR-05 ULA scroll NR 0x26/0x27/0x68 b2 restored "
                  "(%02X/%02X/%d)", a.ula_sx, a.ula_sy, a.ula_fine);
    CHECK(a.ula_sx == s0.ula_sx && a.ula_sy == s0.ula_sy &&
          a.ula_fine == s0.ula_fine, msg);

    std::snprintf(msg, sizeof(msg),
                  "RWR-06 Ula NR 0x43 b1-3 / NR 0x6B b4 selector mirrors "
                  "restored (%d%d%d%d)",
                  a.sel_ula, a.sel_l2, a.sel_spr, a.sel_tm);
    CHECK(a.sel_ula == s0.sel_ula && a.sel_l2 == s0.sel_l2 &&
          a.sel_spr == s0.sel_spr && a.sel_tm == s0.sel_tm, msg);

    std::snprintf(msg, sizeof(msg),
                  "RWR-07 attribute mux shows the restored VRAM "
                  "(mux %02X, VRAM %02X, snapshot %02X)",
                  a.mux0, a.vram_attr0, s0.vram_attr0);
    CHECK(a.mux0 == s0.vram_attr0 && a.vram_attr0 == s0.vram_attr0, msg);

    // The per-line snapshots the rewind's render read, row by row.
    const Renderer& r = emu.renderer();
    int bad_r = 0, bad_tm = 0, bad_lr = 0;
    for (int row = 0; row < Renderer::FB_HEIGHT; ++row) {
        const Renderer::UlaClipWindow c = r.ula_clip_for_line(row);
        if (r.stencil_mode_for_line(row) != s0.stencil ||
            r.blend_mode_for_line(row) != s0.blend ||
            r.transparent_rgb_for_line(row) != s0.nr14 ||
            c.x1 != s0.clip.x1 || c.x2 != s0.clip.x2 ||
            c.y1 != s0.clip.y1 || c.y2 != s0.clip.y2)
            ++bad_r;
        if (emu.tilemap().scroll_x_for_line(row) != s0.tm_scroll_x ||
            emu.tilemap().scroll_y_for_line(row) != s0.tm_scroll_y)
            ++bad_tm;
        const Lores::LineState l = r.lores().state_for_line(row);
        if (l.enabled != s0.lores.enabled || l.scroll_x != s0.lores.scroll_x ||
            l.scroll_y != s0.lores.scroll_y || l.nr6a != s0.lores.nr6a)
            ++bad_lr;
    }
    std::snprintf(msg, sizeof(msg),
                  "RWR-08 stencil/blend/NR 0x14/ULA-clip rows read the "
                  "restored registers (%d stale rows)", bad_r);
    CHECK(bad_r == 0, msg);
    std::snprintf(msg, sizeof(msg),
                  "RWR-09 tilemap scroll rows read the restored registers "
                  "(%d stale rows)", bad_tm);
    CHECK(bad_tm == 0, msg);
    std::snprintf(msg, sizeof(msg),
                  "RWR-10 LoRes rows read the restored registers "
                  "(%d stale rows)", bad_lr);
    CHECK(bad_lr == 0, msg);

    // The frame the rewind rendered must be the frame a machine that never
    // ran past the snapshot renders at that instant.
    Emulator twin;
    rw_build_s0(twin, 10);
    twin.renderer().render_frame(twin.get_framebuffer(), twin.mmu(), twin.ram(),
                                 twin.palette(), twin.layer2(), &twin.sprites(),
                                 &twin.tilemap());
    // GH #266 — the compared frame must show the tilemap, or RWR-11 is blind
    // to it. Top-left pixel, in the border (no Layer 2, no sprite): scroll
    // (5, 6) puts it on map cell (0, 0) = tile 0, row 6, pixel 5 = index
    // 1 + (5 + 6) % 14 = 12, tilemap palette 1.
    REQUIRE(twin.get_framebuffer()[0] == twin.palette().tilemap_colour(true, 12),
            "RWR fixture: the snapshot-instant frame shows the S0 tilemap");
    const size_t px = static_cast<size_t>(emu.get_framebuffer_width()) *
                      static_cast<size_t>(emu.get_framebuffer_height());
    size_t diff = 0;
    for (size_t i = 0; i < px; ++i)
        if (emu.get_framebuffer()[i] != twin.get_framebuffer()[i])
            ++diff;
    std::snprintf(msg, sizeof(msg),
                  "RWR-11 frame rendered by rewind_to_frame equals a fresh "
                  "render of the snapshot instant (%zu pixels differ)", diff);
    CHECK(diff == 0, msg);

    return 0;
}

// ── Test 14: the other rewind callers (GH #261) ────────────────────────────
//
// rewind_to_cycle() (and step_back() through it) replays from the snapshot,
// so begin_new_frame() re-baselines before anything renders. Two pieces of
// render history still leaked through that path pre-fix:
//  * the Ula palette-selector mirrors are not in the stream, so they kept
//    their pre-rewind value — replayed frames used the wrong palette bank;
//  * SpriteEngine::start_frame() first applies the previous frame's
//    unreplayed pattern-log entries (its vblank catch-up). After a break in
//    mid-frame those entries are the pre-rewind frame's writes, applied over
//    the restored pattern RAM: step_back past a port 0x5B write left the
//    byte written.

static int test_rewind_callers_render_state()
{
    printf("\n--- Test 14: rewind_to_cycle / step_back render state (GH #261) ---\n");

    {
        Emulator emu;
        rw_build_s0(emu, 10);
        rw_nr(emu, 0x6B, 0x80);      // S0 selects TM palette 1 (GH #266): all selectors 0 here
        emu.run_frame();             // frame 1 — the target snapshot
        rw_nr(emu, 0x43, 0x1E);
        rw_nr(emu, 0x6B, 0x10);
        emu.run_frame();
        emu.run_frame();
        const uint64_t cyc = emu.rewind_buffer()->frame_cycle_for(1);
        REQUIRE(emu.rewind_to_cycle(cyc) != UINT64_MAX,
                "RWR fixture: rewind_to_cycle to the frame-1 snapshot");
        const Ula& u = emu.ula();
        char msg[160];
        std::snprintf(msg, sizeof(msg),
                      "RWR-12 rewind_to_cycle restores the Ula selector mirrors "
                      "(%d%d%d%d, PaletteManager %d%d%d%d)",
                      u.get_active_ula_palette(), u.get_active_layer2_palette(),
                      u.get_active_sprite_palette(), u.get_active_tilemap_palette(),
                      emu.palette().active_ula_palette(),
                      emu.palette().active_layer2_palette(),
                      emu.palette().active_sprite_palette(),
                      emu.palette().active_tilemap_palette());
        CHECK(!u.get_active_ula_palette() && !u.get_active_layer2_palette() &&
              !u.get_active_sprite_palette() && !u.get_active_tilemap_palette(),
              msg);
    }
    {
        Emulator emu;
        rw_build_s0(emu, 10);        // pattern 0 filled with 0x11
        // 0x8100: LD BC,0x303B; XOR A; OUT (C),A; LD BC,0x005B; LD A,0x77;
        //         OUT (C),A; HALT — writes pattern byte 0 = 0x77.
        const uint8_t prog[] = { 0x01, 0x3B, 0x30, 0xAF, 0xED, 0x79,
                                 0x01, 0x5B, 0x00, 0x3E, 0x77, 0xED, 0x79,
                                 0x76 };
        for (size_t i = 0; i < sizeof(prog); ++i)
            emu.mmu().write(static_cast<uint16_t>(0x8100 + i), prog[i]);
        rw_park(emu, 0x8100);        // the frame-1 snapshot starts here
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().add_pc(0x810D);  // the HALT
        emu.run_frame();             // breaks mid-frame after the OUT
        const uint8_t written = emu.sprites().read_pattern_byte(0);
        emu.debug_state().breakpoints().clear_all_pc();
        REQUIRE(written == 0x77 && emu.step_back(1),
                "RWR fixture: OUT to port 0x5B ran, step_back(1) succeeds");
        char msg[160];
        std::snprintf(msg, sizeof(msg),
                      "RWR-13 step_back over a port 0x5B write restores the "
                      "pattern byte (PC %04X, pattern[0] %02X, want 11)",
                      emu.cpu().get_registers().PC,
                      emu.sprites().read_pattern_byte(0));
        CHECK(emu.cpu().get_registers().PC == 0x810B &&
              emu.sprites().read_pattern_byte(0) == 0x11, msg);
    }
    return 0;
}

// ── Test 15: a guest soft reset inside the rewind history (GH #263 audit) ──
//
// A soft reset (NR 0x02 bit 0, zxnext.vhd:6370) re-runs Emulator::init(),
// which used to rebuild the rewind buffer from the command-line size and
// clear replay_mode_ — both host state, not machine state. The history is
// the host's record of the machine; a reset is one more event in it.

// ZXN, CPU parked on a HALT, DI; HALT in the ROM window the Z80 restarts in
// (SRAM page 0), frame 0 run, then a Copper program that resets the machine
// at cvc 100 of every frame until the reset stops it.
static void rw_soft_reset_fixture(Emulator& emu, int rewind_frames)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = rewind_frames;
    emu.init(cfg);
    emu.mmu().write(0x8000, 0x76);    // HALT
    rw_park(emu, 0x8000);
    emu.ram().write(0, 0xF3);         // DI
    emu.ram().write(1, 0x76);         // HALT
    emu.run_frame();                  // frame 0
    rw_nr(emu, 0x61, 0x00);
    rw_nr(emu, 0x62, 0x00);
    for (uint16_t w : {static_cast<uint16_t>(0x8000u | 100u),
                       static_cast<uint16_t>((0x02u << 8) | 0x01u),
                       static_cast<uint16_t>(0x8000u | 511u)}) {
        rw_nr(emu, 0x60, static_cast<uint8_t>(w >> 8));
        rw_nr(emu, 0x60, static_cast<uint8_t>(w));
    }
    rw_nr(emu, 0x62, 0xC0);
}

static int test_rewind_across_soft_reset()
{
    printf("\n--- Test 15: rewind across a guest soft reset (GH #263) ---\n");

    {
        // The history before the reset survives it (frames 0 and 1 are
        // still there after frame 1 reset the machine), and so does a size
        // set live from the debugger (2, not the command line's 10).
        Emulator emu;
        rw_soft_reset_fixture(emu, 10);
        emu.run_frame();                  // frame 1 — resets at cvc 100
        emu.run_frame();                  // frame 2
        const RewindBuffer* rb = emu.rewind_buffer();
        const bool kept = rb && rb->depth() == 3 &&
                          rb->frame_cycle_for(1) != UINT64_MAX;
        const size_t depth = rb ? rb->depth() : 0;

        Emulator sized;
        rw_soft_reset_fixture(sized, 10);
        sized.resize_rewind_buffer(2);
        for (int i = 0; i < 4; ++i) sized.run_frame();   // reset in the 1st
        const size_t sized_depth =
            sized.rewind_buffer() ? sized.rewind_buffer()->depth() : 0;
        char msg[200];
        std::snprintf(msg, sizeof(msg),
                      "RWR-14 a guest soft reset keeps the rewind history and "
                      "its live size (depth %zu want 3, frame 1 kept %d; "
                      "resized depth %zu want 2)", depth, kept, sized_depth);
        CHECK(kept && sized_depth == 2, msg);
    }
    {
        // A replay that re-executes the reset stays a replay: rewind into
        // frame 1 past the cvc-100 reset, to raw line 250, mixes no audio.
        Emulator emu;
        rw_soft_reset_fixture(emu, 10);
        emu.run_frame();                  // frame 1 — resets at cvc 100
        emu.run_frame();                  // frame 2
        int16_t drain[1024];
        while (emu.mixer().read_samples(drain, 512) > 0) {}
        const uint64_t f1 = emu.rewind_buffer()->frame_cycle_for(1);
        REQUIRE(f1 != UINT64_MAX, "RWR fixture: frame 1 is in the history");
        const uint64_t target = f1 + 250u * emu.timing().master_cycles_per_line;
        REQUIRE(emu.rewind_to_cycle(target) != UINT64_MAX,
                "RWR fixture: rewind_to_cycle into frame 1");
        const bool reset_replayed =
            emu.cpu().get_registers().PC == 0x0001 && emu.cpu().is_halted();
        char msg[200];
        std::snprintf(msg, sizeof(msg),
                      "RWR-15 a guest soft reset replayed by rewind_to_cycle does "
                      "not end the replay (%d samples mixed, want 0; reset "
                      "replayed %d)", emu.mixer().available(), reset_replayed);
        CHECK(reset_replayed && emu.mixer().available() == 0, msg);
    }
    {
        // A snapshot is taken at the top of begin_new_frame(), before the
        // frame edge commits a pending NR 0x03 timing (zxnext.vhd:6696-6703),
        // so it can hold pending != effective. The pulse-mode /INT width gate
        // decodes the EFFECTIVE timing (:2033, :5761-5776): after a restore
        // both of its copies must show the effective 128K value, and the
        // next frame edge must still commit +3.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX128K;
        cfg.rewind_buffer_frames = 10;
        emu.init(cfg);
        emu.mmu().write(0x8000, 0x76);    // HALT
        rw_park(emu, 0x8000);
        emu.run_frame();                  // frame 0
        rw_nr(emu, 0x03, 0xB0);           // tim_sel +3, pending
        emu.run_frame();                  // frame 1 — snapshot before commit
        emu.run_frame();                  // frame 2
        REQUIRE(emu.cpu().machine_timing_48_or_p3() &&
                emu.rewind_to_frame(1),
                "RWR fixture: +3 committed, rewind_to_frame(1) succeeds");
        const bool cpu_rw = emu.cpu().machine_timing_48_or_p3();
        const bool im2_rw = emu.im2().machine_timing_48_or_p3();
        emu.run_frame();                  // frame 1 again: its edge commits
        const bool cpu_edge = emu.cpu().machine_timing_48_or_p3();
        char msg[200];
        std::snprintf(msg, sizeof(msg),
                      "RWR-16 a restore puts the /INT width gate back on the "
                      "EFFECTIVE timing, in both copies (cpu %d im2 %d, want "
                      "0 0; after the edge cpu %d, want 1)",
                      cpu_rw, im2_rw, cpu_edge);
        CHECK(!cpu_rw && !im2_rw && cpu_edge, msg);
    }
    return 0;
}

// SS-VER-01..07 (G66) removed 2026-07-15 — reclassified as a Phase 11
// future enhancement (see comment above test_monotonic_tape_clock_roundtrip).
// RB-FRAME-01..03 (G67) became real rows in Test 11 (Task 60b).
//
// WONT G68: rewind sub-frame granularity is an explicit design choice
// per EMULATOR-DESIGN-PLAN.md Phase 8 Step 4 (frame snapshots ring
// buffer). Will become a row only if a user asks; not a skip() entry.

// ── Test 17: GH #27 S3 — the descriptor layer's declarations ──────────────
//
// S3 replaced six hand-written save_state/load_state pairs with a walk of one
// `describe_state` declaration each. The MIGRATION was proved by the §17.1
// byte-identity gate: the warm-start recording of a booted NextZXOS machine
// re-extracted after every subsystem and `cmp`ed against a pre-migration
// image, 2 292 965 bytes, clean each time. That gate is a one-shot scaffold —
// it needs a pre-migration build to have produced the golden — so it cannot
// be a row here. S5b then re-baselined that stream deliberately; its own rows
// are Test 23, and the two re-baselined lengths are pinned there.
//
// What CAN be a row, and is what the gate leaves behind, is the LAYOUT the
// gate proved: which fields each subsystem declares, in which order, at which
// width. `rewind_test`'s existing round-trip rows cannot see it —
// save→load→save is idempotence, and a consistently reordered pair of
// same-width fields passes it (design §17.1 says so in as many words). So the
// rows below record the declaration itself and compare it against a list
// spelled out here as literals.
//
// The expected lists are a TRANSCRIPTION of the layout the golden proved, not
// a re-derivation from the code: that is what makes them an oracle rather
// than `feedback_self_consistent_generated_data`. Each block's total width is
// also pinned, and those seven numbers are exactly the block lengths the
// §17.1 sentinel map reports for blocks 0-5 and the IM2 half of block 31.
//
// Every description below is a STRING LITERAL, and the `fprintf` beside each
// one is why. The traceability generator reads a row's text from its own
// `check()` call, so a `cond ? "text" : detail.c_str()` description publishes
// as a bare em-dash — which is exactly what the first run of these rows put
// in TRACEABILITY-MATRIX.md. The diagnosis goes to stderr, where a failing
// run shows it and a passing one costs nothing.

namespace s3 {

/// A `StateDesc` realisation that RECORDS a declaration instead of encoding
/// it: one `"<kind> <name> <width>"` line per call, in declaration order.
///
/// It is a realisation and not a parse of the source, so it sees exactly what
/// `BinWriteDesc` sees — including a field declared inside a loop, which no
/// grep of the source could enumerate.
class RecordDesc final : public jnext::save::StateDesc {
public:
    bool writing() const override { return true; }

    const std::vector<std::string>& fields() const { return f_; }
    std::size_t width() const { return width_; }

    void bytes(const char* n, uint8_t*, std::size_t len) override {
        add("bytes", n, len);
    }
    void blob(const char* n, uint8_t*, std::size_t len) override {
        add("blob", n, len);
    }
    void ram_window(const char* n, uint8_t*, std::size_t len,
                    uint32_t) override {
        add("ram_window", n, len);
    }
    void log(const char* n, jnext::save::LogAccess&, std::size_t&,
             std::size_t capacity) override {
        add("log", n, 2 + capacity * 3);
    }
    void fifo(const char* n, jnext::save::FifoAccess& ring,
              jnext::save::FifoElem elem) override {
        add("fifo", n,
            8 + ring.capacity() *
                    (elem == jnext::save::FifoElem::U8 ? 1u : 2u));
    }
    void sentinel(const char* n, uint32_t, uint32_t) override {
        add("sentinel", n ? n : "?", 4);
    }

protected:
    void do_boolean(const char* n, bool&, jnext::save::Def<bool>) override {
        add("bool", n, 1);
    }
    void do_u8(const char* n, uint8_t&, jnext::save::Def<uint8_t>) override {
        add("u8", n, 1);
    }
    void do_u16(const char* n, uint16_t&, jnext::save::Def<uint16_t>) override {
        add("u16", n, 2);
    }
    void do_u32(const char* n, uint32_t&, jnext::save::Def<uint32_t>) override {
        add("u32", n, 4);
    }
    void do_u64(const char* n, uint64_t&, jnext::save::Def<uint64_t>) override {
        add("u64", n, 8);
    }
    void do_i32(const char* n, int32_t&, jnext::save::Def<int32_t>) override {
        add("i32", n, 4);
    }
    void do_i64(const char* n, int64_t&, jnext::save::Def<int64_t>) override {
        add("i64", n, 8);
    }
    void do_i64_open(const char* n, int64_t&,
                     jnext::save::Def<int64_t>) override {
        add("i64_open", n, 8);
    }
    void do_enum8(const char* n, uint8_t&, const jnext::save::EnumNames&,
                  jnext::save::Def<uint8_t>) override {
        add("enum8", n, 1);
    }

private:
    void add(const char* kind, const char* name, std::size_t w) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s %s %zu", kind, name ? name : "?", w);
        f_.push_back(buf);
        width_ += w;
    }
    std::vector<std::string> f_;
    std::size_t              width_ = 0;
};

/// Compare a recording against an expected list and report the FIRST
/// disagreement by index, because "the layout changed" is not a diagnosis.
std::string diff(const std::vector<std::string>& got,
                 const std::vector<std::string>& want)
{
    const std::size_t n = got.size() < want.size() ? got.size() : want.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (got[i] != want[i]) {
            return "field " + std::to_string(i) + ": got '" + got[i] +
                   "', want '" + want[i] + "'";
        }
    }
    if (got.size() != want.size()) {
        return "field count: got " + std::to_string(got.size()) + ", want " +
               std::to_string(want.size());
    }
    return "";
}

std::vector<std::string> vec(const char* const* a, std::size_t n) {
    return std::vector<std::string>(a, a + n);
}

// The 14 IM2 devices, in DevIdx order (src/cpu/im2.h). Spelled here rather
// than shared with im2.cpp: a table the code and the test both read could
// not catch the code renaming a device.
const char* const kIm2Devices[] = {
    "line", "uart0_rx", "uart1_rx",
    "ctc0", "ctc1", "ctc2", "ctc3", "ctc4", "ctc5", "ctc6", "ctc7",
    "ula", "uart0_tx", "uart1_tx",
};

}  // namespace s3

static int test_s3_descriptor_layout()
{
    printf("\n--- Test 17: GH #27 S3 descriptor declarations ---\n");

    Emulator emu;
    build_emulator(emu, 2);

    // Every recording, kept for the uniqueness row at the foot of this test.
    std::vector<std::pair<std::string, std::vector<std::string>>> all_decls;

    // ── Clock — stream block 0, 12 bytes ─────────────────────────────────
    {
        static const char* const want[] = {
            "u64 cycle 8",
            "i32 cpu_divisor 4",
        };
        s3::RecordDesc rec;
        emu.clock().describe_state(rec);
        all_decls.push_back({"clock", rec.fields()});
        const std::string d = s3::diff(rec.fields(), s3::vec(want, 2));
        if (!d.empty()) fprintf(stderr, "  S3-DECL-CLOCK: %s\n", d.c_str());
        check("S3-DECL-CLOCK", d.empty(),
              "Clock declares exactly the two fields the §17.1 "
              "golden carries, in that order");
        check("S3-WIDTH-CLOCK", rec.width() == 12,
              "Clock's declaration is 12 bytes wide — block 0 of the "
              "2 292 965-byte stream");
    }

    // ── Ram — block 1, 2 097 160 bytes ───────────────────────────────────
    {
        static const char* const want[] = {
            "u64 size_bytes 8",
            "blob ram 2097152",
        };
        s3::RecordDesc rec;
        emu.ram().describe_state(rec);
        all_decls.push_back({"ram", rec.fields()});
        const std::string d = s3::diff(rec.fields(), s3::vec(want, 2));
        if (!d.empty()) fprintf(stderr, "  S3-DECL-RAM: %s\n", d.c_str());
        check("S3-DECL-RAM", d.empty(),
              "Ram declares a u64 count prefix and the 2 MB blob "
              "— and the blob's length comes from the DECLARATION, "
              "which is what makes the prefix un-obeyable");
        check("S3-WIDTH-RAM", rec.width() == 2097160,
              "Ram's declaration is 2 097 160 bytes wide — block 1");
    }

    // ── Mmu — block 2, 24 634 bytes ──────────────────────────────────────
    {
        static const char* const want[] = {
            "bytes slots 8",
            "bool read_only_0 1", "bool read_only_1 1", "bool read_only_2 1",
            "bool read_only_3 1", "bool read_only_4 1", "bool read_only_5 1",
            "bool read_only_6 1", "bool read_only_7 1",
            "bool paging_locked 1",
            "u8 port_7ffd 1",
            "u8 port_1ffd 1",
            "bool l2_write_enable 1",
            "u8 l2_segment_mask 1",
            "u8 l2_bank 1",
            "bool boot_rom_en 1",
            "bool config_mode 1",
            "u8 nr_04_romram_bank 1",
            "bool rom_in_sram 1",
            "bool contention_disabled 1",
            "u8 nr_8c_reg 1",
            "enum8 machine_type 1",
            "u8 port_dffd_reg 1",
            "bool port_eff7_reg_2 1",
            "bool port_eff7_reg_3 1",
            "u8 nr_8f_mode 1",
            "bool l2_read_enable 1",
            "u8 p3_floating_bus_dat 1",
            "bool slot_contended_0 1", "bool slot_contended_1 1",
            "bool slot_contended_2 1", "bool slot_contended_3 1",
            "u8 l2_segment_raw 1",
            "bool l2_enable 1",
            "bool l2_map_shadow 1",
            "u8 l2_offset 1",
            "u8 l2_shadow_bank 1",
            "bool port_dffd_reg_6 1",
            "bool port_1ffd_special_old 1",
            "bytes nr_mmu 8",
            "enum8 machine_timing 1",
            "enum8 pending_machine_timing 1",
            "blob bank7_bram 8192",
            "blob bank5_vram 16384",
            "u16 attr_mux_current_line 2",
        };
        s3::RecordDesc rec;
        emu.mmu().describe_state(rec);
        all_decls.push_back({"mmu", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S3-DECL-MMU: %s\n", d.c_str());
        check("S3-DECL-MMU", d.empty(),
              "Mmu declares 45 fields in the order the golden "
              "carries them, ending with both BRAM blobs and the "
              "attribute-mux cursor");
        check("S3-WIDTH-MMU", rec.width() == 24634,
              "Mmu's declaration is 24 634 bytes wide — block 2");
    }

    // ── NextReg — block 3, 262 bytes ─────────────────────────────────────
    {
        static const char* const want[] = {
            "u8 selected 1",
            "bytes regs 256",
            "bool nr_03_config_mode 1",
            "u8 nr_04_romram_bank 1",
            "u8 nr_03_machine_timing 1",
            "bool nr_03_user_dt_lock 1",
            "u8 nr_03_machine_type 1",
        };
        s3::RecordDesc rec;
        emu.nextreg().describe_state(rec);
        all_decls.push_back({"nextreg", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S3-DECL-NEXTREG: %s\n", d.c_str());
        check("S3-DECL-NEXTREG", d.empty(),
              "NextReg declares the select latch, the 256-byte "
              "register file as a `bytes` (not a blob — under the "
              "§6.1 8 KB line) and the five appended scalars");
        check("S3-WIDTH-NEXTREG", rec.width() == 262,
              "NextReg's declaration is 262 bytes wide — block 3");
    }

    // ── Z80Cpu — block 4, 45 bytes ───────────────────────────────────────
    {
        static const char* const want[] = {
            "u16 af 2", "u16 bc 2", "u16 de 2", "u16 hl 2",
            "u16 af2 2", "u16 bc2 2", "u16 de2 2", "u16 hl2 2",
            "u16 ix 2", "u16 iy 2", "u16 sp 2", "u16 pc 2",
            "u8 i 1", "u8 r 1",
            "u8 iff1 1", "u8 iff2 1", "u8 im 1",
            "bool halted 1",
            "u16 memptr 2",
            "u8 q 1",
            "i32 ei_grace 4",
            "u8 iff2_read 1",
            "bool nmi_pending 1",
            "bool int_pending 1",
            "u8 int_vector 1",
            "u32 int_first_ts_rel 4",
        };
        s3::RecordDesc rec;
        emu.cpu().describe_state(rec);
        all_decls.push_back({"cpu", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S3-DECL-CPU: %s\n", d.c_str());
        check("S3-DECL-CPU", d.empty(),
              "Z80Cpu declares the register file, MEMPTR/Q, and "
              "the three §9.5(3) values that are relative to the "
              "FUSE T-state counter");
        check("S3-WIDTH-CPU", rec.width() == 45,
              "Z80Cpu's declaration is 45 bytes wide — block 4");
    }

    // ── Im2Controller state — block 5, 149 bytes ─────────────────────────
    {
        // The nine per-device fields, in declaration order. Spelled here so a
        // reordering INSIDE the loop — which no width check and no round-trip
        // can see — has to be made twice to pass.
        static const char* const dev[] = {
            "bool %s_int_req 1",
            "bool %s_int_req_d 1",
            "bool %s_int_en 1",
            "bool %s_int_unq 1",
            "bool %s_int_status 1",
            "bool %s_im2_int_req 1",
            "enum8 %s_state 1",
            "bool %s_dma_int_en 1",
            "bool %s_exception 1",
        };
        std::vector<std::string> want;
        for (const char* name : s3::kIm2Devices) {
            for (const char* f : dev) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), f, name);
                want.push_back(buf);
            }
        }
        static const char* const tail[] = {
            "enum8 dec_state 1",
            "bool reti_seen_pulse 1",
            "bool retn_seen_pulse 1",
            "bool reti_decode 1",
            "bool dma_delay_ctrl 1",
            "u8 im_mode 1",
            "bool pulse_int_n 1",
            "u8 pulse_count 1",
            "bool machine_48_or_p3 1",
            "u8 vector_base_msb3 1",
            "bool im2_mode 1",
            "bool stackless_nmi 1",
            "u16 dma_int_en_mask14 2",
            "bool im2_dma_delay_latched 1",
            "bool nmi_activated 1",
            "bool nr_cc_dma_int_en_0_7 1",
            "i32 last_acked 4",
            "u16 legacy_mask 2",
        };
        for (const char* t : tail) want.push_back(t);

        s3::RecordDesc rec;
        emu.im2().describe_state(rec);
        all_decls.push_back({"im2", rec.fields()});
        const std::string d = s3::diff(rec.fields(), want);
        if (!d.empty()) fprintf(stderr, "  S3-DECL-IM2: %s\n", d.c_str());
        check("S3-DECL-IM2", d.empty(),
              "Im2Controller declares 14 named devices x 9 fields "
              "then the decoder / pulse / NR 0xC0 / DMA-delay "
              "scalars — 144 declarations, one per field, not 126 "
              "per device");
        check("S3-WIDTH-IM2", rec.width() == 149,
              "Im2Controller's state declaration is 149 bytes wide — block 5");
    }

    // ── Im2Controller timing — the IM2 half of block 31 ──────────────────
    {
        static const char* const dev[] = {
            "u64 %s_req_at 8",
            "u64 %s_unq_at 8",
            "u64 %s_status_at 8",
            "u64 %s_im2_req_at 8",
            "u64 %s_sreq_at 8",
        };
        std::vector<std::string> want;
        for (const char* name : s3::kIm2Devices) {
            for (const char* f : dev) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), f, name);
                want.push_back(buf);
            }
        }
        static const char* const tail[] = {
            "bool pulse_timed 1",
            "u64 pulse_te 8",
            "u64 pulse_e1 8",
            "u64 pulse_en 8",
            "u32 pulse_d 4",
        };
        for (const char* t : tail) want.push_back(t);

        s3::RecordDesc rec;
        emu.im2().describe_timing(rec);
        all_decls.push_back({"im2_timing", rec.fields()});
        const std::string d = s3::diff(rec.fields(), want);
        if (!d.empty()) fprintf(stderr, "  S3-DECL-IM2-TIMING: %s\n", d.c_str());
        check("S3-DECL-IM2-TIMING", d.empty(),
              "Im2Controller's SECOND declaration (§9.5(2)) is the "
              "GH #265 timing block, which travels in `int_timing` "
              "at the end of the Emulator stream and not in block 5");
        check("S3-WIDTH-IM2-TIMING", rec.width() == 589,
              "the IM2 timing declaration is 589 bytes wide — the first 589 of "
              "block 31's 609, the remaining 20 being the CPU's /INT pair and "
              "the CTC's chained triggers");
    }

    // ── Every key of a declaration must be UNIQUE ────────────────────────
    //
    // A duplicate name is invisible to the byte-identity gate: the binary
    // encoding ignores names entirely, so the stream stays correct to the
    // byte while `JsonWriteDesc` — which writes `obj[name] = value` — drops
    // the first field of the pair and `.jns` silently loses it. That is a
    // fault in exactly the property S3 exists to establish (one field list,
    // two encodings), and the only place it can be caught is here.
    {
        std::string dup;
        for (const auto& sub : all_decls) {
            std::vector<std::string> seen;
            for (const auto& f : sub.second) {
                // "kind name width" -> "name"
                const std::size_t a = f.find(' ');
                const std::size_t b = f.rfind(' ');
                const std::string key = f.substr(a + 1, b - a - 1);
                for (const auto& k : seen) {
                    if (k == key && dup.empty())
                        dup = sub.first + "." + key;
                }
                seen.push_back(key);
            }
        }
        if (!dup.empty()) fprintf(stderr, "  S3-KEYS-UNIQUE: %s\n", dup.c_str());
        check("S3-KEYS-UNIQUE", dup.empty(),
              "no declaration names the same key twice — a duplicate is "
              "invisible to the byte stream, which ignores names, and silently "
              "drops a field from the JSON encoding, which does not");
    }

    return 0;
}


// ── Test 21: GH #27 S5 — the peripheral / audio / input declarations ──────
//
// S5 replaced twenty hand-written save_state/load_state pairs with a walk of
// one `describe_state` declaration each (CTC also gains a `describe_timing`,
// design §9.5(2)). The MIGRATION was proved by the §17.1 byte-identity gate:
// the warm-start recording of a booted NextZXOS machine re-extracted after
// every subsystem and `cmp`ed against a pre-migration image, 2 292 965 bytes,
// clean each time. That gate is a one-shot scaffold — it needs a pre-migration
// build to have produced the golden — so it cannot be a row here.
//
// What CAN be a row, and is what the gate leaves behind, is the LAYOUT the
// gate proved. The existing round-trip rows cannot see it: save->load->save is
// idempotence, and a consistently reordered pair of same-width fields passes it
// (design §17.1 says so in as many words).
//
// ── WHERE THE TWO HALVES OF EACH PAIR COME FROM ──────────────────────────
//
// The `S5-WIDTH-*` numbers are an INDEPENDENT oracle. They were read out of
// the pre-migration golden itself, not out of the new code: the stream carries
// a `kStateSentinelMagic ^ ordinal` u32 after every subsystem
// (`emulator.cpp:11606`), so scanning the 2 292 965-byte image for the 33
// sentinels in order gives every block's exact length, and those lengths sum
// to the file size with nothing left over. A declaration whose width is right
// cannot have dropped, gained or resized a field.
//
// The `S5-DECL-*` field lists are a transcription of the declarations, and
// their job is narrower and worth being honest about: they are a CHANGE
// DETECTOR. Widths alone cannot see two same-width fields swapped, which is
// exactly the fault §17.1 says the round-trip rows are blind to, so the names
// have to be spelled out somewhere. Spelling them here means a reordering
// shows up as a failing row rather than as a silently different `.jns`.
//
// Every description below is a STRING LITERAL, and the `fprintf` beside each
// one is why: the traceability generator reads a row's text from its own
// `check()` call, so a `cond ? "text" : detail.c_str()` description publishes
// as a bare em-dash.

static int test_s5_descriptor_layout()
{
    printf("\n--- Test 21: GH #27 S5 descriptor declarations ---\n");

    Emulator emu;
    build_emulator(emu, 2);

    // `rtc_` has no Emulator accessor, and it does not need one: a declaration
    // is a property of the CLASS, so a standalone instance walks the same
    // field list the Emulator's does.
    I2cRtc rtc;

    // Every recording, kept for the uniqueness row at the foot of this test.
    std::vector<std::pair<std::string, std::vector<std::string>>> all_decls;

    // ── ctc — block 12 — four channels of ten fields: 40 bytes ──
    {
        static const char* const want[] = {
            "bool ch0_control_int_en 1",
            "bool ch0_control_counter 1",
            "bool ch0_control_prescale 1",
            "bool ch0_control_edge 1",
            "bool ch0_control_trigger 1",
            "u8 ch0_time_constant 1",
            "u8 ch0_counter 1",
            "u8 ch0_prescaler 1",
            "enum8 ch0_state 1",
            "bool ch0_clk_trg_prev 1",
            "bool ch1_control_int_en 1",
            "bool ch1_control_counter 1",
            "bool ch1_control_prescale 1",
            "bool ch1_control_edge 1",
            "bool ch1_control_trigger 1",
            "u8 ch1_time_constant 1",
            "u8 ch1_counter 1",
            "u8 ch1_prescaler 1",
            "enum8 ch1_state 1",
            "bool ch1_clk_trg_prev 1",
            "bool ch2_control_int_en 1",
            "bool ch2_control_counter 1",
            "bool ch2_control_prescale 1",
            "bool ch2_control_edge 1",
            "bool ch2_control_trigger 1",
            "u8 ch2_time_constant 1",
            "u8 ch2_counter 1",
            "u8 ch2_prescaler 1",
            "enum8 ch2_state 1",
            "bool ch2_clk_trg_prev 1",
            "bool ch3_control_int_en 1",
            "bool ch3_control_counter 1",
            "bool ch3_control_prescale 1",
            "bool ch3_control_edge 1",
            "bool ch3_control_trigger 1",
            "u8 ch3_time_constant 1",
            "u8 ch3_counter 1",
            "u8 ch3_prescaler 1",
            "enum8 ch3_state 1",
            "bool ch3_clk_trg_prev 1",
        };
        s3::RecordDesc rec;
        emu.ctc().describe_state(rec);
        all_decls.push_back({"ctc", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-CTC: %s\n", d.c_str());
        check("S5-DECL-CTC", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-CTC", rec.width() == 40u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── ctc_timing — the CTC part of block 31 int_timing: 4 bytes ──
    {
        static const char* const want[] = {
            "u8 ch0_trg_delay 1",
            "u8 ch1_trg_delay 1",
            "u8 ch2_trg_delay 1",
            "u8 ch3_trg_delay 1",
        };
        s3::RecordDesc rec;
        emu.ctc().describe_timing(rec);
        all_decls.push_back({"ctc_timing", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-CTC-TIMING: %s\n", d.c_str());
        check("S5-DECL-CTC-TIMING", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-CTC-TIMING", rec.width() == 4u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── dma — block 13: 43 bytes ──
    {
        static const char* const want[] = {
            "bool dir_a_to_b 1",
            "u16 port_a_addr 2",
            "u16 block_len 2",
            "bool port_a_is_io 1",
            "u8 port_a_addr_mode 1",
            "u8 port_a_timing 1",
            "bool port_b_is_io 1",
            "u8 port_b_addr_mode 1",
            "u8 port_b_timing 1",
            "u8 port_b_prescaler 1",
            "bool dma_en 1",
            "u8 mode 1",
            "u16 port_b_addr 2",
            "bool ce_wait 1",
            "bool auto_restart 1",
            "u8 read_mask 1",
            "enum8 state 1",
            "u16 src 2",
            "u16 dst 2",
            "u16 counter 2",
            "bool status_at_least_one 1",
            "bool status_end_of_block 1",
            "enum8 wr_seq 1",
            "enum8 rd_seq 1",
            "u8 reg_temp 1",
            "bool z80_compat 1",
            "u8 turbo 1",
            "u16 dma_timer_s 2",
            "bool in_waiting_cycles 1",
            "enum8 phase 1",
            "bool cpu_busreq_n 1",
            "bool cpu_bao_n 1",
            "bool cpu_bai_n 1",
            "bool bus_busreq_n 1",
            "bool dma_delay 1",
            "bool daisy_busy 1",
        };
        s3::RecordDesc rec;
        emu.dma().describe_state(rec);
        all_decls.push_back({"dma", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-DMA: %s\n", d.c_str());
        check("S5-DECL-DMA", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-DMA", rec.width() == 43u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── spi — block 14: 3 bytes ──
    {
        static const char* const want[] = {
            "u8 cs 1",
            "u8 rx_data 1",
            "bool sd_swap 1",
        };
        s3::RecordDesc rec;
        emu.spi().describe_state(rec);
        all_decls.push_back({"spi", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-SPI: %s\n", d.c_str());
        check("S5-DECL-SPI", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-SPI", rec.width() == 3u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── i2c — block 15: 13 bytes ──
    {
        static const char* const want[] = {
            "u8 scl 1",
            "u8 sda_out 1",
            "u8 sda_in 1",
            "u8 prev_scl 1",
            "u8 prev_sda 1",
            "enum8 state 1",
            "u8 bit_count 1",
            "u8 shift_reg 1",
            "u8 device_addr 1",
            "bool is_read 1",
            "u8 read_data 1",
            "bool pi_i2c1_scl 1",
            "bool pi_i2c1_sda 1",
        };
        s3::RecordDesc rec;
        emu.i2c().describe_state(rec);
        all_decls.push_back({"i2c", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-I2C: %s\n", d.c_str());
        check("S5-DECL-I2C", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-I2C", rec.width() == 13u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── rtc — block 16 — no Emulator accessor, so a standalone I2cRtc: 69 bytes ──
    {
        static const char* const want[] = {
            "u8 reg_ptr 1",
            "bool addr_set 1",
            "bytes regs 64",
            "bool osc_halt 1",
            "bool mode_12h 1",
            "bool use_real_time 1",
        };
        s3::RecordDesc rec;
        rtc.describe_state(rec);
        all_decls.push_back({"rtc", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-RTC: %s\n", d.c_str());
        check("S5-DECL-RTC", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-RTC", rec.width() == 69u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── uart — block 17 — the selector and two channels, FIFOs included: 2330 bytes ──
    {
        static const char* const want[] = {
            "i32 select 4",
            "fifo ch0_tx_fifo 72",
            "fifo ch0_rx_fifo 1032",
            "u8 ch0_prescaler_msb 1",
            "u16 ch0_prescaler_lsb 2",
            "u8 ch0_framing 1",
            "bool ch0_tx_busy 1",
            "u32 ch0_tx_timer_byte 4",
            "bool ch0_err_overflow 1",
            "bool ch0_err_framing 1",
            "bool ch0_err_break 1",
            "bool ch0_bitlevel_mode 1",
            "enum8 ch0_tx_state 1",
            "enum8 ch0_tx_state_next 1",
            "u8 ch0_tx_shift 1",
            "u32 ch0_tx_timer 4",
            "u32 ch0_tx_prescaler_snap 4",
            "u8 ch0_tx_bit_count 1",
            "bool ch0_tx_parity_live 1",
            "bool ch0_tx_frame_parity_en 1",
            "bool ch0_tx_frame_stop_bits 1",
            "bool ch0_tx_parity_odd_snap 1",
            "bool ch0_cts_n 1",
            "bool ch0_tx_line_out 1",
            "bool ch0_tx_busy_bitlevel 1",
            "bool ch0_tx_en 1",
            "enum8 ch0_rx_state 1",
            "enum8 ch0_rx_state_next 1",
            "u8 ch0_rx_shift 1",
            "u32 ch0_rx_timer 4",
            "u32 ch0_rx_prescaler_snap 4",
            "bool ch0_rx_timer_updated 1",
            "u8 ch0_rx_bit_count 1",
            "bool ch0_rx_parity_live 1",
            "u8 ch0_rx_frame_bits 1",
            "bool ch0_rx_frame_parity_en 1",
            "bool ch0_rx_frame_stop_bits 1",
            "bool ch0_rx_parity_odd_snap 1",
            "u8 ch0_rx_debounce_counter 1",
            "u8 ch0_rx_button_sync 1",
            "bool ch0_rx_raw 1",
            "bool ch0_rx_debounced 1",
            "bool ch0_rx_d 1",
            "bool ch0_rx_edge 1",
            "bool ch0_rx_byte_parity_err 1",
            "bool ch0_rx_byte_framing_err 1",
            "fifo ch1_tx_fifo 72",
            "fifo ch1_rx_fifo 1032",
            "u8 ch1_prescaler_msb 1",
            "u16 ch1_prescaler_lsb 2",
            "u8 ch1_framing 1",
            "bool ch1_tx_busy 1",
            "u32 ch1_tx_timer_byte 4",
            "bool ch1_err_overflow 1",
            "bool ch1_err_framing 1",
            "bool ch1_err_break 1",
            "bool ch1_bitlevel_mode 1",
            "enum8 ch1_tx_state 1",
            "enum8 ch1_tx_state_next 1",
            "u8 ch1_tx_shift 1",
            "u32 ch1_tx_timer 4",
            "u32 ch1_tx_prescaler_snap 4",
            "u8 ch1_tx_bit_count 1",
            "bool ch1_tx_parity_live 1",
            "bool ch1_tx_frame_parity_en 1",
            "bool ch1_tx_frame_stop_bits 1",
            "bool ch1_tx_parity_odd_snap 1",
            "bool ch1_cts_n 1",
            "bool ch1_tx_line_out 1",
            "bool ch1_tx_busy_bitlevel 1",
            "bool ch1_tx_en 1",
            "enum8 ch1_rx_state 1",
            "enum8 ch1_rx_state_next 1",
            "u8 ch1_rx_shift 1",
            "u32 ch1_rx_timer 4",
            "u32 ch1_rx_prescaler_snap 4",
            "bool ch1_rx_timer_updated 1",
            "u8 ch1_rx_bit_count 1",
            "bool ch1_rx_parity_live 1",
            "u8 ch1_rx_frame_bits 1",
            "bool ch1_rx_frame_parity_en 1",
            "bool ch1_rx_frame_stop_bits 1",
            "bool ch1_rx_parity_odd_snap 1",
            "u8 ch1_rx_debounce_counter 1",
            "u8 ch1_rx_button_sync 1",
            "bool ch1_rx_raw 1",
            "bool ch1_rx_debounced 1",
            "bool ch1_rx_d 1",
            "bool ch1_rx_edge 1",
            "bool ch1_rx_byte_parity_err 1",
            "bool ch1_rx_byte_framing_err 1",
        };
        s3::RecordDesc rec;
        emu.uart().describe_state(rec);
        all_decls.push_back({"uart", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-UART: %s\n", d.c_str());
        check("S5-DECL-UART", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-UART", rec.width() == 2330u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── divmmc — block 18 — 131 089 bytes DECLARED, 17 in an Emulator ──
    //
    // `RecordDesc` records the DECLARATION, and the declaration still names a
    // 131 072-byte window: that is what the field IS, and it is what the JSON
    // encoding writes a reference TO. What S5b changed is the binary
    // realisation, which emits no bytes for it at machine level — so this
    // width is the pre-S5b block length and the standalone one, while an
    // Emulator-driven DivMmc block is 17 bytes (S5B-DIVMMC-BLOCK).
    {
        static const char* const want[] = {
            "bool enabled 1",
            "bool conmem 1",
            "bool mapram 1",
            "u8 bank 1",
            "u8 control_reg 1",
            "bool automap_active 1",
            "u8 entry_points_0 1",
            "u8 entry_valid_0 1",
            "u8 entry_timing_0 1",
            "u8 entry_points_1 1",
            "bool automap_hold 1",
            "bool automap_held 1",
            "bool button_nmi 1",
            "bool layer2_map_read 1",
            "bool retn_pending_clear 1",
            "ram_window ram 131072",
            "bool port_io_enable 1",
            "bool nr_0a_4_enable 1",
        };
        s3::RecordDesc rec;
        emu.divmmc().describe_state(rec);
        all_decls.push_back({"divmmc", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-DIVMMC: %s\n", d.c_str());
        check("S5-DECL-DIVMMC", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-DIVMMC", rec.width() == 131089u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measured — which since "
              "S5b is the STANDALONE block width, the machine-level one "
              "being 17 because the window became a reference (§17.0)");
    }

    // ── beeper — block 19: 3 bytes ──
    {
        static const char* const want[] = {
            "bool ear 1",
            "bool mic 1",
            "bool tape_ear 1",
        };
        s3::RecordDesc rec;
        emu.beeper().describe_state(rec);
        all_decls.push_back({"beeper", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-BEEPER: %s\n", d.c_str());
        check("S5-DECL-BEEPER", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-BEEPER", rec.width() == 3u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── turbosound — block 20 — three chips of 25 fields: 152 bytes ──
    {
        static const char* const want[] = {
            "bool ay0_ay_mode 1",
            "bytes ay0_reg 16",
            "u8 ay0_addr 1",
            "u8 ay0_cnt_div 1",
            "bool ay0_noise_div 1",
            "bool ay0_ena_div 1",
            "bool ay0_ena_div_noise 1",
            "u16 ay0_tone_cnt_a 2",
            "u16 ay0_tone_cnt_b 2",
            "u16 ay0_tone_cnt_c 2",
            "bool ay0_tone_op_a 1",
            "bool ay0_tone_op_b 1",
            "bool ay0_tone_op_c 1",
            "u8 ay0_noise_cnt 1",
            "u32 ay0_poly17 4",
            "bool ay0_noise_op 1",
            "u16 ay0_env_cnt 2",
            "bool ay0_env_ena 1",
            "bool ay0_env_reset 1",
            "u8 ay0_env_vol 1",
            "bool ay0_env_inc 1",
            "bool ay0_env_hold 1",
            "u8 ay0_out_a 1",
            "u8 ay0_out_b 1",
            "u8 ay0_out_c 1",
            "bool ay1_ay_mode 1",
            "bytes ay1_reg 16",
            "u8 ay1_addr 1",
            "u8 ay1_cnt_div 1",
            "bool ay1_noise_div 1",
            "bool ay1_ena_div 1",
            "bool ay1_ena_div_noise 1",
            "u16 ay1_tone_cnt_a 2",
            "u16 ay1_tone_cnt_b 2",
            "u16 ay1_tone_cnt_c 2",
            "bool ay1_tone_op_a 1",
            "bool ay1_tone_op_b 1",
            "bool ay1_tone_op_c 1",
            "u8 ay1_noise_cnt 1",
            "u32 ay1_poly17 4",
            "bool ay1_noise_op 1",
            "u16 ay1_env_cnt 2",
            "bool ay1_env_ena 1",
            "bool ay1_env_reset 1",
            "u8 ay1_env_vol 1",
            "bool ay1_env_inc 1",
            "bool ay1_env_hold 1",
            "u8 ay1_out_a 1",
            "u8 ay1_out_b 1",
            "u8 ay1_out_c 1",
            "bool ay2_ay_mode 1",
            "bytes ay2_reg 16",
            "u8 ay2_addr 1",
            "u8 ay2_cnt_div 1",
            "bool ay2_noise_div 1",
            "bool ay2_ena_div 1",
            "bool ay2_ena_div_noise 1",
            "u16 ay2_tone_cnt_a 2",
            "u16 ay2_tone_cnt_b 2",
            "u16 ay2_tone_cnt_c 2",
            "bool ay2_tone_op_a 1",
            "bool ay2_tone_op_b 1",
            "bool ay2_tone_op_c 1",
            "u8 ay2_noise_cnt 1",
            "u32 ay2_poly17 4",
            "bool ay2_noise_op 1",
            "u16 ay2_env_cnt 2",
            "bool ay2_env_ena 1",
            "bool ay2_env_reset 1",
            "u8 ay2_env_vol 1",
            "bool ay2_env_inc 1",
            "bool ay2_env_hold 1",
            "u8 ay2_out_a 1",
            "u8 ay2_out_b 1",
            "u8 ay2_out_c 1",
            "u8 ay_select 1",
            "u8 ay0_pan 1",
            "u8 ay1_pan 1",
            "u8 ay2_pan 1",
            "bool enabled 1",
            "bool stereo_mode 1",
            "u8 mono_mode 1",
            "u16 pcm_l 2",
            "u16 pcm_r 2",
        };
        s3::RecordDesc rec;
        emu.turbosound().describe_state(rec);
        all_decls.push_back({"turbosound", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-TURBOSOUND: %s\n", d.c_str());
        check("S5-DECL-TURBOSOUND", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-TURBOSOUND", rec.width() == 152u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── dac — block 21: 4 bytes ──
    {
        static const char* const want[] = {
            "bytes channels 4",
        };
        s3::RecordDesc rec;
        emu.dac().describe_state(rec);
        all_decls.push_back({"dac", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-DAC: %s\n", d.c_str());
        check("S5-DECL-DAC", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-DAC", rec.width() == 4u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── i2s — the i2s block: 4 bytes ──
    {
        static const char* const want[] = {
            "u16 left 2",
            "u16 right 2",
        };
        s3::RecordDesc rec;
        emu.i2s().describe_state(rec);
        all_decls.push_back({"i2s", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-I2S: %s\n", d.c_str());
        check("S5-DECL-I2S", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-I2S", rec.width() == 4u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── nmi_source — the nmi_source block, less the Emulator byte after it: 28 bytes ──
    {
        static const char* const want[] = {
            "bool mf_button 1",
            "bool divmmc_button 1",
            "bool expbus_nmi_n 1",
            "bool strobe_mf_button_pending 1",
            "bool strobe_divmmc_button_pending 1",
            "bool nmi_sw_gen_mf 1",
            "bool nmi_sw_gen_divmmc 1",
            "bool iotrap_strobe_pending 1",
            "bool mf_enable 1",
            "bool divmmc_enable 1",
            "bool expbus_debounce_disable 1",
            "bool expbus_eff_en 1",
            "bool expbus_eff_disable_mem 1",
            "bool config_mode 1",
            "bool mf_nmi_hold 1",
            "bool mf_is_active 1",
            "bool divmmc_nmi_hold 1",
            "bool divmmc_conmem 1",
            "bool nmi_mf 1",
            "bool nmi_divmmc 1",
            "bool nmi_expbus 1",
            "enum8 state 1",
            "bool nr_02_pending_mf 1",
            "bool nr_02_pending_divmmc 1",
            "bool prev_wr_n 1",
            "bool mf_button_strobe 1",
            "bool divmmc_button_strobe 1",
            "u8 reset_type 1",
        };
        s3::RecordDesc rec;
        emu.nmi_source().describe_state(rec);
        all_decls.push_back({"nmi_source", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-NMI: %s\n", d.c_str());
        check("S5-DECL-NMI", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-NMI", rec.width() == 28u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── multiface — the block less its presence byte: 8200 bytes on a 48K ──
    //
    // `emu` is a 48K, where nothing calls `set_ram_backing` and the private
    // array IS the store, so the RAM member is declared and the width is the
    // pre-S5b one. On the Next the member is absent entirely and the block is
    // 8 bytes — S5b's one machine-dependent width (S5B-MF-NEXT-ABSENT).
    {
        static const char* const want[] = {
            "bool enabled 1",
            "bool nmi_active 1",
            "bool invisible 1",
            "bool mf_enable 1",
            "bool port_io_dly 1",
            "bool mode_p3 1",
            "bool mode_128 1",
            "bool mode_48 1",
            "u8 mf_type 1",
            "blob ram 8192",
        };
        s3::RecordDesc rec;
        emu.multiface().describe_state(rec);
        all_decls.push_back({"multiface", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-MULTIFACE: %s\n", d.c_str());
        check("S5-DECL-MULTIFACE", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-MULTIFACE", rec.width() == 8201u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── keyboard — the head of the input block: 342 bytes ──
    {
        static const char* const want[] = {
            "bytes matrix 8",
            "u16 ex_matrix 2",
            "bytes shift_hist 2",
            "u32 auto_queue_count 4",
            "i32 auto00_row1 4",
            "i32 auto00_col1 4",
            "i32 auto00_row2 4",
            "i32 auto00_col2 4",
            "i32 auto00_frames 4",
            "i32 auto01_row1 4",
            "i32 auto01_col1 4",
            "i32 auto01_row2 4",
            "i32 auto01_col2 4",
            "i32 auto01_frames 4",
            "i32 auto02_row1 4",
            "i32 auto02_col1 4",
            "i32 auto02_row2 4",
            "i32 auto02_col2 4",
            "i32 auto02_frames 4",
            "i32 auto03_row1 4",
            "i32 auto03_col1 4",
            "i32 auto03_row2 4",
            "i32 auto03_col2 4",
            "i32 auto03_frames 4",
            "i32 auto04_row1 4",
            "i32 auto04_col1 4",
            "i32 auto04_row2 4",
            "i32 auto04_col2 4",
            "i32 auto04_frames 4",
            "i32 auto05_row1 4",
            "i32 auto05_col1 4",
            "i32 auto05_row2 4",
            "i32 auto05_col2 4",
            "i32 auto05_frames 4",
            "i32 auto06_row1 4",
            "i32 auto06_col1 4",
            "i32 auto06_row2 4",
            "i32 auto06_col2 4",
            "i32 auto06_frames 4",
            "i32 auto07_row1 4",
            "i32 auto07_col1 4",
            "i32 auto07_row2 4",
            "i32 auto07_col2 4",
            "i32 auto07_frames 4",
            "i32 auto08_row1 4",
            "i32 auto08_col1 4",
            "i32 auto08_row2 4",
            "i32 auto08_col2 4",
            "i32 auto08_frames 4",
            "i32 auto09_row1 4",
            "i32 auto09_col1 4",
            "i32 auto09_row2 4",
            "i32 auto09_col2 4",
            "i32 auto09_frames 4",
            "i32 auto10_row1 4",
            "i32 auto10_col1 4",
            "i32 auto10_row2 4",
            "i32 auto10_col2 4",
            "i32 auto10_frames 4",
            "i32 auto11_row1 4",
            "i32 auto11_col1 4",
            "i32 auto11_row2 4",
            "i32 auto11_col2 4",
            "i32 auto11_frames 4",
            "i32 auto12_row1 4",
            "i32 auto12_col1 4",
            "i32 auto12_row2 4",
            "i32 auto12_col2 4",
            "i32 auto12_frames 4",
            "i32 auto13_row1 4",
            "i32 auto13_col1 4",
            "i32 auto13_row2 4",
            "i32 auto13_col2 4",
            "i32 auto13_frames 4",
            "i32 auto14_row1 4",
            "i32 auto14_col1 4",
            "i32 auto14_row2 4",
            "i32 auto14_col2 4",
            "i32 auto14_frames 4",
            "i32 auto15_row1 4",
            "i32 auto15_col1 4",
            "i32 auto15_row2 4",
            "i32 auto15_col2 4",
            "i32 auto15_frames 4",
            "i32 auto_frame_count 4",
            "bool auto_gap 1",
            "bool cancel_extended 1",
        };
        s3::RecordDesc rec;
        emu.keyboard().describe_state(rec);
        all_decls.push_back({"keyboard", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-KEYBOARD: %s\n", d.c_str());
        check("S5-DECL-KEYBOARD", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-KEYBOARD", rec.width() == 342u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── joystick — the input block: 7 bytes ──
    {
        static const char* const want[] = {
            "u8 nr_05_raw 1",
            "enum8 joy0_mode 1",
            "enum8 joy1_mode 1",
            "u16 joy_left_bits 2",
            "u16 joy_right_bits 2",
        };
        s3::RecordDesc rec;
        emu.joystick().describe_state(rec);
        all_decls.push_back({"joystick", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-JOYSTICK: %s\n", d.c_str());
        check("S5-DECL-JOYSTICK", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-JOYSTICK", rec.width() == 7u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── mouse — the input block: 6 bytes ──
    {
        static const char* const want[] = {
            "u8 x 1",
            "u8 y 1",
            "u8 buttons 1",
            "u8 wheel 1",
            "bool button_reverse 1",
            "u8 dpi 1",
        };
        s3::RecordDesc rec;
        emu.mouse().describe_state(rec);
        all_decls.push_back({"mouse", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-MOUSE: %s\n", d.c_str());
        check("S5-DECL-MOUSE", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-MOUSE", rec.width() == 6u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── md6 — the input block: 16 bytes ──
    {
        static const char* const want[] = {
            "u16 raw_left 2",
            "u16 raw_right 2",
            "u16 latched_left 2",
            "u16 latched_right 2",
            "u16 state 2",
            "bool six_button_left 1",
            "bool six_button_right 1",
            "u32 clk_en_accum 4",
        };
        s3::RecordDesc rec;
        emu.md6().describe_state(rec);
        all_decls.push_back({"md6", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-MD6: %s\n", d.c_str());
        check("S5-DECL-MD6", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-MD6", rec.width() == 16u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── membrane_stick — the input block: 73 bytes ──
    {
        static const char* const want[] = {
            "enum8 mode_left 1",
            "enum8 mode_right 1",
            "u16 state_left 2",
            "u16 state_right 2",
            "bytes keymap 64",
            "u8 keymap_sel 1",
            "u16 keymap_addr 2",
        };
        s3::RecordDesc rec;
        emu.membrane_stick().describe_state(rec);
        all_decls.push_back({"membrane_stick", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-MEMBRANE: %s\n", d.c_str());
        check("S5-DECL-MEMBRANE", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-MEMBRANE", rec.width() == 73u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }

    // ── iomode — the input block: 6 bytes ──
    {
        static const char* const want[] = {
            "u8 nr_0b_raw 1",
            "bool pin7 1",
            "bool uart0_tx 1",
            "bool uart1_tx 1",
            "bool joy_left_bit5 1",
            "bool joy_right_bit5 1",
        };
        s3::RecordDesc rec;
        emu.iomode().describe_state(rec);
        all_decls.push_back({"iomode", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S5-DECL-IOMODE: %s\n", d.c_str());
        check("S5-DECL-IOMODE", d.empty(),
              "the declaration walks exactly the fields the golden's block "
              "carries, in that order");
        check("S5-WIDTH-IOMODE", rec.width() == 6u,
              "the declaration is exactly as wide as the block the "
              "pre-migration golden's sentinel map measures");
    }


    // ── Every key of a declaration must be UNIQUE ────────────────────────
    //
    // The same fault S3-KEYS-UNIQUE catches for the core subsystems, and it
    // matters more here: five of these declarations are built by REPEATING
    // one field list over several instances (four CTC channels, two UART
    // channels, three AY chips, sixteen auto-type slots), so a key table that
    // forgot to carry the instance number would produce a declaration that is
    // byte-perfect and silently loses three quarters of its JSON.
    {
        std::string dup;
        for (const auto& sub : all_decls) {
            std::vector<std::string> seen;
            for (const auto& f : sub.second) {
                // "kind name width" -> "name"
                const std::size_t a = f.find(' ');
                const std::size_t b = f.rfind(' ');
                const std::string key = f.substr(a + 1, b - a - 1);
                for (const auto& k : seen) {
                    if (k == key && dup.empty())
                        dup = sub.first + "." + key;
                }
                seen.push_back(key);
            }
        }
        if (!dup.empty()) fprintf(stderr, "  S5-KEYS-UNIQUE: %s\n", dup.c_str());
        check("S5-KEYS-UNIQUE", dup.empty(),
              "no S5 declaration names the same key twice — a duplicate is "
              "invisible to the byte stream, which ignores names, and silently "
              "drops a field from the JSON encoding, which does not");
    }

    // ── The six input classes sum to the `input` block ───────────────────
    //
    // Each of the six is pinned above on its own, but the block they share is
    // 450 bytes and the golden's sentinel map is what says so. Summing them
    // here is the row that would catch a SEVENTH class being appended to
    // `Emulator::save_state`'s input group without the stream being
    // re-measured — which is how the joy_uart block came to vary in width.
    {
        std::size_t sum = 0;
        for (const auto& sub : all_decls) {
            if (sub.first == "keyboard" || sub.first == "joystick" ||
                sub.first == "mouse" || sub.first == "md6" ||
                sub.first == "membrane_stick" || sub.first == "iomode") {
                for (const auto& f : sub.second)
                    sum += std::stoul(f.substr(f.rfind(' ') + 1));
            }
        }
        if (sum != 450) fprintf(stderr, "  S5-INPUT-BLOCK: sum is %zu\n", sum);
        check("S5-INPUT-BLOCK", sum == 450,
              "the six input declarations sum to the 450 bytes the golden's "
              "sentinel map measures for the input block");
    }

    return 0;
}

// ── Test 18: GH #27 S3 — what the migration CHANGED, not just transcribed ─

static int test_s3_restore_behaviour()
{
    printf("\n--- Test 18: GH #27 S3 restore behaviour ---\n");

    // ── Ram: the count prefix is CHECKED, never obeyed ───────────────────
    //
    // Before S3 the prefix WAS the write length for the 2 MB buffer:
    //     uint64_t sz = r.read_u64();
    //     r.read_bytes(data_.data(), static_cast<size_t>(sz));
    // Both of StateReader::read_bytes' branches are unbounded there. The
    // warm-start loader checks only the TOTAL stream length, so a tampered
    // cache of the right total size reaches Ram::load_state with a hostile
    // number. The row asserts the property that makes that impossible: the
    // restore consumes 8 + the DECLARED length, whatever the prefix says.
    {
        Ram ram(64 * 1024);
        for (uint32_t i = 0; i < 64 * 1024; ++i)
            ram.write(i, static_cast<uint8_t>(i * 7 + 3));

        StateWriter measure;
        ram.save_state(measure);
        const size_t n = measure.position();
        std::vector<uint8_t> buf(n, 0);
        StateWriter w(buf.data(), n);
        ram.save_state(w);

        // Forge a prefix twelve times the real size — in range for a size_t,
        // so pre-fix this took read_bytes' memset branch and zeroed 768 KB
        // over a 64 KB heap buffer.
        const uint64_t lie = 12ull * 64 * 1024;
        std::memcpy(buf.data(), &lie, sizeof(lie));

        Ram back(64 * 1024);
        StateReader r(buf.data(), n);
        back.load_state(r);

        bool content_ok = true;
        for (uint32_t i = 0; i < 64 * 1024; ++i)
            if (back.read(i) != static_cast<uint8_t>(i * 7 + 3)) { content_ok = false; break; }

        check("S3-RAM-PREFIX", r.position() == 8 + 64u * 1024 && content_ok,
              "a RAM count prefix twelve times the real size neither moves the "
              "stream nor reaches past the buffer: the restore takes its "
              "length from the DECLARATION and the content is intact");
        check("S3-RAM-PREFIX-SANE", n == 8 + 64u * 1024,
              "…and an honest save is still exactly the prefix plus the RAM");
    }

    // ── enum8: an ordinal outside the declared set is REFUSED ────────────
    //
    // Pre-S3 both of these were `static_cast<Enum>(r.read_u8())` — any byte
    // became a state. §16.1: a wrong FSM state is not a safe default. The
    // stream must still stay in sync, because the byte was consumed either
    // way, and that is the half a refusal usually gets wrong.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.set_machine_type(MachineType::ZX128K);

        StateWriter measure;
        mmu.save_state(measure);
        const size_t n = measure.position();
        std::vector<uint8_t> buf(n, 0);
        StateWriter w(buf.data(), n);
        mmu.save_state(w);

        // machine_type is declaration index 21, at stream offset 28: 8 (slots)
        // + 8 (read_only) + 12 single-byte scalars.
        check("S3-ENUM-OFFSET", buf[28] == static_cast<uint8_t>(MachineType::ZX128K),
              "the machine_type ordinal really is at stream offset 28 — the "
              "row below is meaningless if it corrupts some other field");
        buf[28] = 0x7F;   // no MachineType has ordinal 127

        Mmu back(ram, rom);
        back.set_machine_type(MachineType::ZX_PLUS3);
        StateReader r(buf.data(), n);
        back.load_state(r);

        check("S3-ENUM-MMU", back.machine_type() == MachineType::ZX_PLUS3 &&
                             r.position() == n,
              "an out-of-range machine_type ordinal leaves the field at its "
              "pre-load value instead of casting garbage into it, and the "
              "stream still ends exactly where it should");
    }

    // ── Mmu: the pending/effective timing pair survives a machine restore ─
    //
    // The pair used to be read behind `if (!r.eof())`, with a flag recording
    // whether both halves arrived so Emulator::load_state could fall back to
    // re-deriving them from NR 0x03. S3 declares them, so the flag is now
    // unconditionally true and the fallback is unreachable. That is only safe
    // if the declared pair really does survive — including the case the
    // fallback would get WRONG, where pending differs from effective.
    {
        Emulator emu;
        build_emulator(emu, 2);
        emu.mmu().set_machine_timing(MachineTimingMode::Timing128);
        emu.mmu().set_pending_machine_timing(MachineTimingMode::TimingPentagon);

        StateWriter measure;
        emu.save_state(measure);
        const size_t n = measure.position();
        std::vector<uint8_t> buf(n, 0);
        StateWriter w(buf.data(), n);
        emu.save_state(w);

        emu.mmu().set_machine_timing(MachineTimingMode::Timing48);
        emu.mmu().set_pending_machine_timing(MachineTimingMode::Timing48);

        StateReader r(buf.data(), n);
        const bool ok = emu.load_state(r);
        check("S3-MMU-TIMING-PAIR", ok &&
              emu.mmu().machine_timing() == MachineTimingMode::Timing128 &&
              emu.mmu().pending_machine_timing() == MachineTimingMode::TimingPentagon,
              "a deferred NR 0x03 timing commit — pending != effective — "
              "survives a full Emulator save/load, which is the case the "
              "retired old-format fallback would have collapsed");
    }

    // ── Mmu: the BRAM blobs are restored BEFORE the dispatch rebuild ─────
    //
    // The hand-written load_state called rebuild_ptr() twice, once mid-stream
    // and once at the end; S3 calls it once, at the end. The end call is the
    // load-bearing one: a slot holding page 0x0E (bank-7 lower half) must
    // point at the freshly restored buffer, not at the pre-load one.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.set_page(3, 0x0E);          // bank-7 lower half — the BRAM page
        mmu.write(0x7000, 0xA5);

        StateWriter measure;
        mmu.save_state(measure);
        const size_t n = measure.position();
        std::vector<uint8_t> buf(n, 0);
        StateWriter w(buf.data(), n);
        mmu.save_state(w);

        Mmu back(ram, rom);
        StateReader r(buf.data(), n);
        back.load_state(r);
        check("S3-MMU-BRAM-PTR", back.read(0x7000) == 0xA5,
              "a byte written into the bank-7 BRAM is readable through the "
              "restored slot: the single rebuild_ptr() pass runs AFTER the "
              "blobs land, which the mid-stream call never did");
    }

    // ── The restore-time masks, which S3 MOVED ───────────────────────────
    //
    // NR 0x8F is 2 bits (VHDL zxnext.vhd:3787-3794) and the two NR 0x03
    // sub-fields are 3 bits each (:1099, :1103). Before S3 each mask was
    // applied to the value as it was read; S3 applies it to the member after
    // the walk, because in the declaration it would change what the WRITE
    // direction emits. Identical result — and NOTHING covered it either way:
    // reverting all three masks killed no row in any suite. A mask nothing
    // asserts is a mask the next edit deletes, so the three rows below are
    // the answer the mutation table owed.
    {
        Ram ram;
        Rom rom;
        Mmu mmu(ram, rom);
        mmu.write_nr_8f(0x01);
        mmu.set_machine_type(MachineType::ZX128K);   // a neighbour, to pin the offset

        StateWriter measure;
        mmu.save_state(measure);
        const size_t n = measure.position();
        std::vector<uint8_t> buf(n, 0);
        StateWriter w(buf.data(), n);
        mmu.save_state(w);

        // nr_8f_mode is declaration index 25, stream offset 32.
        check("S3-MMU-NR8F-OFFSET",
              buf[32] == 0x01 &&
                  buf[28] == static_cast<uint8_t>(MachineType::ZX128K),
              "nr_8f_mode is at stream offset 32 and machine_type at 28 — the "
              "row below is meaningless if it pokes some other field");
        buf[32] = 0xFF;

        Mmu back(ram, rom);
        StateReader r(buf.data(), n);
        back.load_state(r);
        check("S3-MMU-NR8F-MASK",
              back.nr_8f_mode() == 0x03 &&
                  back.machine_type() == MachineType::ZX128K &&
                  r.position() == n,
              "a restored NR 0x8F keeps only its 2 declared bits "
              "(zxnext.vhd:3787-3794), its neighbour is untouched and the "
              "stream still ends where it should");
    }
    {
        NextReg nr;
        nr.set_nr_03_machine_timing(0x05);
        nr.set_nr_03_machine_type(0x02);
        nr.select(0x42);                    // a neighbour, to pin the offset

        StateWriter measure;
        nr.save_state(measure);
        const size_t n = measure.position();
        std::vector<uint8_t> buf(n, 0);
        StateWriter w(buf.data(), n);
        nr.save_state(w);

        // selected 0, regs 1..256, nr_03_config_mode 257,
        // nr_04_romram_bank 258, nr_03_machine_timing 259,
        // nr_03_user_dt_lock 260, nr_03_machine_type 261.
        check("S3-NEXTREG-NR03-OFFSET",
              buf[0] == 0x42 && buf[259] == 0x05 && buf[261] == 0x02,
              "the two NR 0x03 sub-fields are at stream offsets 259 and 261, "
              "behind the 256-byte register file");
        buf[259] = 0xFF;
        buf[261] = 0xFF;

        NextReg back;
        StateReader r(buf.data(), n);
        back.load_state(r);
        check("S3-NEXTREG-NR03-MASK",
              back.nr_03_machine_timing() == 0x07 &&
                  back.nr_03_machine_type() == 0x07 &&
                  back.selected() == 0x42 && r.position() == n,
              "both restored NR 0x03 sub-fields keep only their 3 declared "
              "bits (zxnext.vhd:1099, :1103) and the select latch is intact");
    }

    // ── The CPU's /INT window is RELATIVE to a counter that is re-seeded ──
    //
    // §9.5(3): the u32 in the CPU block is int_first_ts_ minus the FUSE
    // T-state counter, because load_state does not restore that counter.
    // Marshalling a constant instead killed no row: at the EMULATOR level the
    // appended int_timing block (31) replaces the pair straight afterwards,
    // so the CPU block's copy is invisible there. It is not invisible to a
    // standalone Z80Cpu save/load, which is what this row exercises.
    {
        Emulator emu;
        build_emulator(emu, 2);
        Z80Cpu& cpu = emu.cpu();

        *fuse_z80_tstates_ptr() = 0x1000;
        cpu.request_interrupt(0xFD, 0x1000, 0x1020);
        *fuse_z80_tstates_ptr() = 0x1050;      // the window is now 0x50 behind

        uint8_t buf[256];
        StateWriter w(buf, sizeof(buf));
        cpu.save_state(w);
        const size_t n = w.position();

        *fuse_z80_tstates_ptr() = 0x9000;      // a different frame's counter
        StateReader r(buf, n);
        cpu.load_state(r);

        check("S3-CPU-INT-WINDOW",
              cpu.int_window_first_ts() == 0x9000 - 0x50,
              "the /INT window's first boundary is restored RELATIVE to "
              "whatever the T-state counter now is (0x50 behind it), not as "
              "the absolute stamp it was saved from");
    }

    return 0;
}


// ── Test 19: GH #27 S4 — the video declarations ───────────────────────────
//
// S4 replaced eight hand-written save_state/load_state pairs with a walk of
// one `describe_state` declaration each. The MIGRATION was proved by the
// §17.1 byte-identity gate: the warm-start recording of a booted NextZXOS
// machine re-extracted after every subsystem and `cmp`ed against the
// pre-migration image, 2 292 965 bytes, clean each time. That gate is a
// one-shot scaffold — it needs a pre-migration build to have produced the
// golden — so it cannot be a row here, exactly as S3 found.
//
// What CAN be a row is the LAYOUT the gate proved. The expected lists below
// are a TRANSCRIPTION of the PRE-MIGRATION `save_state` bodies (the code that
// wrote the golden) plus the block widths the golden's sentinel map reports —
// NOT a re-derivation from the new declarations, which would only prove
// self-consistency (`feedback_self_consistent_generated_data`).
//
// The six widths below are exactly the block lengths that map reports:
// palette 4 622 (block 6), layer2 12 (7), sprites 17 039 (8), tilemap 26 (9),
// renderer+ULA+LoRes 3 688 (10), copper 2 057 (11).

static int test_s4_descriptor_layout()
{
    printf("\n--- Test 19: GH #27 S4 video descriptor declarations ---\n");

    Emulator emu;
    build_emulator(emu, 2);

    std::vector<std::pair<std::string, std::vector<std::string>>> all_decls;

    // ── PaletteManager — block 6, 4 622 bytes ────────────────────────────
    //
    // The four `uint16_t` stores and the priority store are §9.4's loop
    // collapse: ten `for` statements became five declarations. Each width is
    // the loop's own: 2 banks x 256 entries x sizeof(element).
    {
        static const char* const want[] = {
            "bytes ula_rgb333 1024",
            "bytes layer2_rgb333 1024",
            "bytes sprite_rgb333 1024",
            "bytes tilemap_rgb333 1024",
            "u8 control 1",
            "u8 index 1",
            "enum8 target_palette 1",
            "bool auto_inc_disabled 1",
            "bool active_ula_second 1",
            "bool active_l2_second 1",
            "bool active_spr_second 1",
            "bool active_tm_second 1",
            "bool ulanext_mode 1",
            "bool nine_bit_first_written 1",
            "u8 nine_bit_first_byte 1",
            "u8 global_transparency 1",
            "u8 sprite_transparency 1",
            "u8 tilemap_transparency 1",
            "bytes layer2_priority 512",
        };
        s3::RecordDesc rec;
        emu.palette().describe_state(rec);
        all_decls.push_back({"palette", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-PALETTE: %s\n", d.c_str());
        check("S4-DECL-PALETTE", d.empty(),
              "PaletteManager declares the four RGB333 stores, the "
              "14 scalars and the Layer 2 priority store in the "
              "order the golden carries them");
        check("S4-WIDTH-PALETTE", rec.width() == 4622,
              "PaletteManager's declaration is 4 622 bytes wide — block 6 of "
              "the 2 292 965-byte stream");
    }

    // ── Layer2 — block 7, 12 bytes ───────────────────────────────────────
    {
        static const char* const want[] = {
            "u8 active_bank 1",
            "u8 shadow_bank 1",
            "u16 scroll_x 2",
            "u8 scroll_y 1",
            "u8 palette_offset 1",
            "u8 resolution 1",
            "bool enabled 1",
            "u8 clip_x1 1", "u8 clip_x2 1", "u8 clip_y1 1", "u8 clip_y2 1",
        };
        s3::RecordDesc rec;
        emu.layer2().describe_state(rec);
        all_decls.push_back({"layer2", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-LAYER2: %s\n", d.c_str());
        check("S4-DECL-LAYER2", d.empty(),
              "Layer2 declares its 11 registers in stream order");
        check("S4-WIDTH-LAYER2", rec.width() == 12,
              "Layer2's declaration is 12 bytes wide — block 7");
    }

    // ── SpriteEngine — block 8, 17 039 bytes ─────────────────────────────
    //
    // `attributes` is the 128-sprite loop collapsed: 128 x 5 = 640 bytes, the
    // exact count the five `write_u8` sites produced. `pattern_ram` is a
    // BLOB and not `bytes` — §6.1 names it, a peripheral store at or above
    // 8 KB.
    {
        static const char* const want[] = {
            "bytes attributes 640",
            "blob pattern_ram 16384",
            "u8 attr_slot 1",
            "u8 attr_byte 1",
            "u16 pattern_offset 2",
            "u8 pattern_slot_msb 1",
            "bool sprites_visible 1",
            "bool over_border 1",
            "bool zero_on_top 1",
            "u8 clip_x1 1", "u8 clip_x2 1", "u8 clip_y1 1", "u8 clip_y2 1",
            "bool collision 1",
            "bool max_sprites 1",
            "bool border_clip_en 1",
        };
        s3::RecordDesc rec;
        emu.sprites().describe_state(rec);
        all_decls.push_back({"sprites", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-SPRITES: %s\n", d.c_str());
        check("S4-DECL-SPRITES", d.empty(),
              "SpriteEngine declares the 640-byte attribute file, "
              "the 16 KB pattern blob and the 15 control bytes");
        check("S4-WIDTH-SPRITES", rec.width() == 17039,
              "SpriteEngine's declaration is 17 039 bytes wide — block 8");
    }

    // ── Tilemap — block 9, 26 bytes ──────────────────────────────────────
    {
        static const char* const want[] = {
            "u8 control_raw 1",
            "bool enabled 1",
            "bool mode_80col 1",
            "bool text_mode 1",
            "bool force_attr 1",
            "bool mode_512 1",
            "bool ula_on_top 1",
            "u8 default_attr 1",
            "u8 map_base_raw 1",
            "u8 def_base_raw 1",
            "u32 map_base_addr 4",
            "u32 def_base_addr 4",
            "u16 scroll_x 2",
            "u8 scroll_y 1",
            "u8 clip_x1 1", "u8 clip_x2 1", "u8 clip_y1 1", "u8 clip_y2 1",
            "bool palette_sel 1",
        };
        s3::RecordDesc rec;
        emu.tilemap().describe_state(rec);
        all_decls.push_back({"tilemap", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-TILEMAP: %s\n", d.c_str());
        check("S4-DECL-TILEMAP", d.empty(),
              "Tilemap declares its 19 fields, both decoded base "
              "addresses included, in stream order");
        check("S4-WIDTH-TILEMAP", rec.width() == 26,
              "Tilemap's declaration is 26 bytes wide — block 9");
    }

    // ── Renderer (+ ULA + LoRes) — block 10, 3 688 bytes ─────────────────
    //
    // The one block built from THREE declarations. `Renderer::describe_state`
    // nests the ULA's first and LoRes's last, which is the order the
    // pre-migration `Renderer::save_state` called `ula_.save_state(w)` and
    // `lores_.save_state(w)` in.
    //
    // `log port_ff_log 3074` is §9.5(1)'s padded history: a u16 count plus
    // EXACTLY 1 024 three-byte entries, which is what `RewindBuffer`'s
    // constant slot width requires and what issue #42 broke when it was
    // variable-length.
    static const char* const want_block10[] = {
        // Ula
        "bool ula_enabled 1",
        "bool vram_use_bank7 1",
        "u8 ula_clip_x1 1", "u8 ula_clip_x2 1",
        "u8 ula_clip_y1 1", "u8 ula_clip_y2 1",
        "u8 border_colour 1",
        "bytes border_per_line 256",
        "i32 flash_counter 4",
        "bool flash_phase 1",
        "u8 screen_mode_reg 1",
        "enum8 screen_mode 1",
        "u8 ula_scroll_x_coarse 1",
        "u8 ula_scroll_y 1",
        "bool ula_fine_scroll_x 1",
        "u8 ulanext_format 1",
        "bool ulanext_en 1",
        "bool ulap_en 1",
        "bool alt_file 1",
        "bool shadow_screen_en 1",
        "bool border_clr_tmx_src 1",
        "u8 ulap_mode 1",
        "u8 baseline_port_ff 1",
        "u16 current_line 2",
        "log port_ff_log 3074",
        // Renderer's own
        "u8 layer_priority 1",
        "u8 fallback_colour 1",
        "u8 transparent_rgb 1",
        "bool sprite_en 1",
        "bool stencil_mode 1",
        "bool tm_enabled 1",
        "u8 blend_mode 1",
        "bytes fallback_per_line 320",
        // Lores
        "bool lores_enabled 1",
        "u8 lores_scroll_x 1",
        "u8 lores_scroll_y 1",
        "u8 lores_nr6a 1",
    };
    constexpr std::size_t kBlock10Fields =
        sizeof(want_block10) / sizeof(want_block10[0]);
    constexpr std::size_t kUlaFields  = 25;   // through "log port_ff_log 3074"
    constexpr std::size_t kLoresFields = 4;   // the trailing lores_* group
    {
        s3::RecordDesc rec;
        emu.renderer().describe_state(rec);
        all_decls.push_back({"block10", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want_block10, kBlock10Fields));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-BLOCK10: %s\n", d.c_str());
        check("S4-DECL-BLOCK10", d.empty(),
              "Renderer's declaration nests the ULA's, then its "
              "own eight fields, then LoRes's four — the order "
              "the golden carries block 10 in");
        check("S4-WIDTH-BLOCK10", rec.width() == 3688,
              "the nested declaration is 3 688 bytes wide — block 10, of "
              "which the ULA is 3 357");
    }

    // The nested halves must be the SAME declaration the two subsystems walk
    // standalone. Without these two rows, `Ula::load_state` (which
    // `ula_test.cpp` uses on its own) and `Renderer::load_state` could drift
    // into reading two different field lists — the exact failure this layer
    // exists to make impossible, applied to its own nesting.
    {
        s3::RecordDesc rec;
        emu.ula().describe_state(rec);
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want_block10, kUlaFields));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-ULA-PREFIX: %s\n", d.c_str());
        check("S4-DECL-ULA-PREFIX", d.empty() && rec.width() == 3357,
              "Ula::describe_state walked standalone is EXACTLY "
              "the first 25 fields / 3 357 bytes of block 10");
    }
    {
        s3::RecordDesc rec;
        emu.renderer().lores().describe_state(rec);
        const std::string d = s3::diff(
            rec.fields(),
            s3::vec(want_block10 + (kBlock10Fields - kLoresFields), kLoresFields));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-LORES-SUFFIX: %s\n", d.c_str());
        check("S4-DECL-LORES-SUFFIX", d.empty() && rec.width() == 4,
              "Lores::describe_state walked standalone is EXACTLY "
              "the last four fields of block 10");
    }

    // ── Copper — block 11, 2 057 bytes ───────────────────────────────────
    {
        static const char* const want[] = {
            "bytes instructions 2048",
            "u16 pc 2",
            "enum8 mode 1",
            "enum8 last_mode 1",
            "bool move_pending 1",
            "u16 write_addr 2",
            "u8 write_data_stored 1",
            "u8 offset 1",
        };
        s3::RecordDesc rec;
        emu.copper().describe_state(rec);
        all_decls.push_back({"copper", rec.fields()});
        const std::string d =
            s3::diff(rec.fields(), s3::vec(want, sizeof(want) / sizeof(want[0])));
        if (!d.empty()) fprintf(stderr, "  S4-DECL-COPPER: %s\n", d.c_str());
        check("S4-DECL-COPPER", d.empty(),
              "Copper declares the 2 KB instruction RAM as one "
              "array, then the seven control fields");
        check("S4-WIDTH-COPPER", rec.width() == 2057,
              "Copper's declaration is 2 057 bytes wide — block 11");
    }

    // ── Every key of these declarations must be UNIQUE ───────────────────
    //
    // The same fault S3-KEYS-UNIQUE covers, extended to S4's declarations —
    // and it matters MORE here, because block 10 is three declarations
    // flattened into one object: `enabled` is a name the ULA, the Renderer
    // and LoRes would all otherwise want, and a collision across the nesting
    // is one a per-subsystem check could not see. A duplicate is invisible to
    // the byte-identity gate (the binary encoding ignores names entirely) and
    // silently drops a field from `JsonWriteDesc`'s `obj[name] = value`.
    {
        std::string dup;
        for (const auto& sub : all_decls) {
            std::vector<std::string> seen;
            for (const auto& f : sub.second) {
                const std::size_t a = f.find(' ');
                const std::size_t b = f.rfind(' ');
                const std::string key = f.substr(a + 1, b - a - 1);
                for (const auto& k : seen) {
                    if (k == key && dup.empty())
                        dup = sub.first + "." + key;
                }
                seen.push_back(key);
            }
        }
        if (!dup.empty()) fprintf(stderr, "  S4-KEYS-UNIQUE: %s\n", dup.c_str());
        check("S4-KEYS-UNIQUE", dup.empty(),
              "no video declaration names the same key twice, block 10's "
              "three-way nesting included");
    }

    return 0;
}

// ── Test 20: GH #27 S4 — what the migration CHANGED, not just transcribed ─
//
// Derived from `git diff`, not from the row list above: every behaviour S4
// MOVED or ADDED gets a row, and each poke-a-byte row is paired with an
// OFFSET row proving the byte it corrupts really is the field it names. A
// corruption row that hits the wrong field passes for the wrong reason.

namespace s4 {

/// Save a subsystem into a right-sized buffer, the standard two-pass idiom.
template <typename T>
std::vector<uint8_t> save_bytes(const T& obj)
{
    StateWriter measure;
    obj.save_state(measure);
    std::vector<uint8_t> buf(measure.position(), 0);
    StateWriter w(buf.data(), buf.size());
    obj.save_state(w);
    return buf;
}

// Offsets into a STANDALONE subsystem save, each derived by adding up the
// declaration above it — the same arithmetic S3's S3-ENUM-OFFSET row does,
// and each one pinned by its own OFFSET row below.
constexpr std::size_t kPalTargetOff   = 4 * 1024 + 2;   // after the four stores
constexpr std::size_t kPalPriorityOff = 4 * 1024 + 14;  // after the 14 scalars
constexpr std::size_t kUlaModeOff     = 269;
constexpr std::size_t kUlaLogCountOff = 283;
constexpr std::size_t kUlaBytes       = 3357;
constexpr std::size_t kCopperModeOff  = 2048 + 2;

}  // namespace s4

static int test_s4_restore_behaviour()
{
    printf("\n--- Test 20: GH #27 S4 restore behaviour ---\n");

    // ── PaletteManager: the ARGB caches are rebuilt AFTER the walk ────────
    //
    // Pre-S4 the loader recomputed each ARGB entry inline as it read its
    // RGB333 word. S4 moved the rebuild to a single post-walk pass, which is
    // a behaviour MOVED rather than transcribed — and nothing pinned that it
    // covers all four palettes in BOTH banks. Poke one entry per palette per
    // bank straight into the stream and read it back through the ARGB
    // accessors: dropping any palette from the rebuild, or looping only
    // bank 0, fails here.
    {
        PaletteManager pal;
        std::vector<uint8_t> buf = s4::save_bytes(pal);

        // RGB333 0x1B5 = r3 6, g3 6, b3 5 — distinct in every component, so a
        // rebuild that transposed two of them would not survive either.
        const uint16_t rgb333 = 0x1B5;
        const uint32_t argb = rgb333_to_argb8888((rgb333 >> 6) & 7,
                                                 (rgb333 >> 3) & 7,
                                                 rgb333 & 7);
        // store s in {ula, layer2, sprite, tilemap}, bank p, entry 200.
        for (std::size_t s = 0; s < 4; ++s)
            for (std::size_t p = 0; p < 2; ++p) {
                const std::size_t off = (s * 1024) + (p * 512) + 200 * 2;
                buf[off]     = static_cast<uint8_t>(rgb333 & 0xFF);
                buf[off + 1] = static_cast<uint8_t>(rgb333 >> 8);
            }

        PaletteManager back;
        StateReader r(buf.data(), buf.size());
        back.load_state(r);

        bool ok = r.position() == buf.size();
        for (int p = 0; p < 2 && ok; ++p) {
            const bool second = (p == 1);
            ok = ok && back.ula_colour(second, 200)     == argb
                    && back.layer2_colour(second, 200)  == argb
                    && back.sprite_colour(second, 200)  == argb
                    && back.tilemap_colour(second, 200) == argb
                    && back.ula_rgb333(second, 200)     == rgb333;
        }
        check("S4-PALETTE-ARGB", ok,
              "the post-walk ARGB rebuild covers all FOUR palettes in BOTH "
              "banks, and the u16 entries land little-endian at 2*(bank*256 + "
              "index) — which is what makes the ten-loop collapse into five "
              "`bytes` a transcription");
    }

    // ── PaletteManager: target_palette is an enum8 ────────────────────────
    {
        PaletteManager pal;
        // NR 0x43 = 0x60 -> target_palette 6 (SPRITE_SECOND), every other
        // bit of the byte clear; the index latch takes an unrelated value.
        // Distinct neighbours are the point: an OFFSET row whose field
        // happens to equal the byte beside it proves nothing.
        pal.write_control(0x60);
        pal.set_index(0x5A);
        std::vector<uint8_t> buf = s4::save_bytes(pal);
        check("S4-PALETTE-TARGET-OFFSET",
              buf.size() == 4622 && s4::kPalTargetOff == 4098 &&
                  buf[4096] == 0x60 && buf[4097] == 0x5A &&
                  buf[4098] == 6 && buf[4099] == 0,
              "target_palette really is the byte at offset 4 098, between the "
              "control byte and the auto-increment flag and equal to neither "
              "— the row below is meaningless if it corrupts another field");
        buf[s4::kPalTargetOff] = 0x2A;   // no PaletteId has ordinal 42

        PaletteManager back;
        back.write_control(0x10);   // target 1 (LAYER2_FIRST) before the load
        StateReader r(buf.data(), buf.size());
        back.load_state(r);
        // Read the restored target back through the stream: the class has no
        // getter for it. Asserting only `r.position()` would NOT discriminate
        // — a plain `u8` consumes the same byte and ends in the same place,
        // and a mutation replacing the enum8 with a u8 proved exactly that.
        const std::vector<uint8_t> after = s4::save_bytes(back);
        check("S4-PALETTE-TARGET",
              after[s4::kPalTargetOff] == 1 && after[4096] == 0x60 &&
                  r.position() == buf.size(),
              "an out-of-range target_palette ordinal is REFUSED: the field "
              "keeps its pre-load target instead of being cast in, the plain "
              "control byte beside it IS restored, and the stream still ends "
              "exactly where it should — the byte was consumed either way");
    }

    // ── Ula: screen_mode is an enum8 WITH HOLES ───────────────────────────
    //
    // TimexScreenMode is not contiguous: 3, 4 and 5 are states
    // `set_screen_mode` cannot produce, spelled `nullptr` in the name table,
    // and refused in both directions. Pre-S4 this was
    // `static_cast<TimexScreenMode>(r.read_u8())` and ANY byte became a mode.
    {
        Ula ula;
        // Port 0xFF = 0x07 gives screen_mode_reg 0x07 and mode HI_RES (6),
        // so the raw register byte and the enum ordinal DIFFER. With
        // set_screen_mode(0x02) they would both be 2 and the offset row
        // would pass at either of the two adjacent offsets.
        ula.set_screen_mode(0x07);
        std::vector<uint8_t> buf = s4::save_bytes(ula);
        check("S4-ULA-MODE-OFFSET",
              buf.size() == s4::kUlaBytes && buf[268] == 0x07 &&
                  buf[s4::kUlaModeOff] ==
                      static_cast<uint8_t>(TimexScreenMode::HI_RES),
              "screen_mode really is at offset 269 of a 3 357-byte ULA save, "
              "and is 6 where the raw port-0xFF register beside it is 7");
        buf[s4::kUlaModeOff] = 4;           // a HOLE: unreachable ordinal

        Ula back;
        back.set_screen_mode(0x02);         // HI_COLOUR, so a wrong restore shows
        StateReader r(buf.data(), buf.size());
        back.load_state(r);
        // Read the restored mode back through the stream rather than an
        // accessor the class does not expose.
        const std::vector<uint8_t> after = s4::save_bytes(back);
        check("S4-ULA-MODE",
              after[s4::kUlaModeOff] ==
                      static_cast<uint8_t>(TimexScreenMode::HI_COLOUR) &&
                  after[268] == 0x07 && r.position() == buf.size(),
              "ordinal 4 is a HOLE in TimexScreenMode and is refused: the "
              "enum field keeps its pre-load mode instead of becoming a state "
              "the ULA cannot be in, the plain register byte beside it IS "
              "restored, and the stream still ends where it should");
    }

    // ── Ula: the port-0xFF log count is CLAMPED, never obeyed ─────────────
    //
    // The stream always carries exactly MAX_CHANGES_PER_FRAME entries
    // (issue #42 — RewindBuffer needs a constant slot width), so a forged
    // count can neither move the stream nor make the live count exceed the
    // array. Pre-S4 a hand-written clamp did this; `d.log` now does, and
    // nothing pinned the property across the move.
    {
        Ula ula;
        ula.start_frame();
        ula.set_current_line(40);
        ula.set_screen_mode(0x02);      // one logged change
        std::vector<uint8_t> buf = s4::save_bytes(ula);
        const uint16_t live = static_cast<uint16_t>(buf[s4::kUlaLogCountOff] |
                                (buf[s4::kUlaLogCountOff + 1] << 8));
        check("S4-ULA-LOG-COUNT-OFFSET", live == 1 && buf.size() == s4::kUlaBytes,
              "the port-0xFF log count really is the u16 at offset 283, and "
              "one logged change reads as 1");
        buf[s4::kUlaLogCountOff]     = 0xFF;   // 65 535 — 64x the capacity
        buf[s4::kUlaLogCountOff + 1] = 0xFF;

        Ula back;
        StateReader r(buf.data(), buf.size());
        back.load_state(r);
        check("S4-ULA-LOG-COUNT",
              back.port_ff_change_log_size() == Ula::MAX_CHANGES_PER_FRAME &&
                  r.position() == buf.size(),
              "a forged count 64x the capacity is clamped to the capacity "
              "declared IN THE CODE and the stream still ends where it "
              "should: the entry loop is bounded by the declaration, never by "
              "the file");
    }

    // ── Lores: the NR $6A 6-bit mask survived the move ───────────────────
    //
    // Pre-S4 the mask was part of the read expression
    // (`r.read_u8() & 0x3F`); S4 moved it after the walk, because a mask in
    // the declaration would change what the WRITE direction emits. Nothing
    // pinned it in either place.
    {
        Lores lo;
        lo.set_nr6a(0x25);
        std::vector<uint8_t> buf = s4::save_bytes(lo);
        check("S4-LORES-NR6A-OFFSET", buf.size() == 4 && buf[3] == 0x25,
              "lores_nr6a really is the fourth and last byte of a LoRes save");
        buf[3] = 0xC5;   // bits 7:6 set — not part of a 6-bit register

        Lores back;
        StateReader r(buf.data(), buf.size());
        back.load_state(r);
        check("S4-LORES-NR6A-MASK",
              back.nr6a() == 0x05 && r.position() == buf.size(),
              "NR $6A is restored masked to its six hardware bits "
              "(zxnext.vhd:5032-5034), so a stream carrying bits 7:6 cannot "
              "put the register in a state a live write could not");
    }

    // ── Renderer: the NR 0x68 blend-mode 2-bit mask survived the move ─────
    {
        Renderer ren;
        std::vector<uint8_t> buf = s4::save_bytes(ren);
        const std::size_t blend_off = s4::kUlaBytes + 6;
        check("S4-BLEND-OFFSET",
              buf.size() == 3688 && blend_off == 3363 && buf[blend_off] == 0,
              "blend_mode really is at offset 3 363 — after the ULA's 3 357 "
              "bytes and the Renderer's first six");
        buf[blend_off] = 0xFE;   // bits 7:2 set — not part of a 2-bit field

        Renderer back;
        StateReader r(buf.data(), buf.size());
        back.load_state(r);
        check("S4-BLEND-MASK",
              back.blend_mode() == 0x02 && r.position() == buf.size(),
              "NR 0x68 bits 6:5 are restored masked to two bits, so a stream "
              "carrying more cannot select a blend mode the VHDL has no "
              "encoding for");
    }

    // ── SpriteEngine: the 128x5 collapse really is the 128x5 order ────────
    //
    // `d.bytes("attributes", …, 640)` replaced five `write_u8` sites inside a
    // 128-iteration loop. If `SpriteAttr` ever gained padding, or the
    // collapse used the wrong length, the golden would move — but only on a
    // full-machine save. This row pins the mapping directly: byte 5*i+k of
    // the attribute file is sprite i's attribute byte k.
    {
        SpriteEngine spr;
        // Sprite 37, attribute byte 3 = visible + pattern 0x11.
        spr.set_attr_slot(37);
        // Byte 3 carries bit 6 (extended) SET: write_attr_byte
        // auto-increments the slot after byte 3 when it is clear, which would
        // put byte 4 on sprite 38 and make this row test the wrong thing.
        static const uint8_t vals[5] = {0x40, 0x41, 0x42, 0xD1, 0x44};
        for (uint8_t k = 0; k < 5; ++k) spr.write_attr_byte(k, vals[k]);
        std::vector<uint8_t> buf = s4::save_bytes(spr);
        const std::size_t base = 37 * 5;
        check("S4-SPRITE-ATTR-ORDER",
              buf.size() == 17039 &&
                  buf[base + 0] == 0x40 && buf[base + 1] == 0x41 &&
                  buf[base + 2] == 0x42 && buf[base + 3] == 0xD1 &&
                  buf[base + 4] == 0x44 &&
                  buf[base - 1] == 0 && buf[base + 5] == 0,
              "the 640-byte attribute file is sprite-major, five bytes each: "
              "sprite 37's five bytes are at offsets 185-189, exactly where "
              "the pre-migration 128-iteration loop put them");
    }

    // ── Ula: the port-0xFF replay cursor restarts at the restored log ────
    //
    // `port_ff_render_cursor_` is transient render state and is NOT in the
    // stream, so after a restore it still points into the log the restoring
    // object had BEFORE the load — a log that no longer exists. S4 moved that
    // reset out of `load_state` into `after_load_state` (so `Renderer` can run
    // it while performing the nested walk itself), and a mutation deleting it
    // killed no row in any suite. It does now.
    {
        Ula a;
        a.start_frame();
        a.set_current_line(7);
        a.set_screen_mode(0x02);    // log entry {line 7, value 0x02}
        a.rewind_to_baseline();     // live register back to the baseline 0x00
        std::vector<uint8_t> buf = s4::save_bytes(a);

        Ula b;
        b.start_frame();
        b.set_current_line(3);
        b.set_screen_mode(0x06);    // b's OWN log: {line 3, value 0x06}
        b.rewind_to_baseline();
        b.apply_changes_for_line(3);  // b's cursor advances past its entry
        StateReader r(buf.data(), buf.size());
        b.load_state(r);
        b.apply_changes_for_line(7);  // replay the RESTORED log, no rewind first

        const std::vector<uint8_t> after = s4::save_bytes(b);
        check("S4-ULA-CURSOR-RESET",
              after[268] == 0x02 && r.position() == buf.size(),
              "a restore restarts the port-0xFF replay cursor at the top of "
              "the RESTORED log: replaying line 7 applies the entry the "
              "stream carried, instead of finding a cursor left past the end "
              "by the log the object had before the load");
    }

    // ── Ula: the per-line control snapshot is DEACTIVATED by a restore ───
    //
    // `control_per_line_` holds the pre-restore frame's rows and is not in
    // the stream. Leaving `control_per_line_active_` set makes the render
    // `Emulator::rewind_to_frame` does immediately after a load read those
    // rows instead of the registers it just restored (GH #256). S4 moved that
    // clear into `after_load_state` and a mutation deleting it killed no row.
    {
        Ula a;
        a.set_ulanext_en(false);
        std::vector<uint8_t> buf = s4::save_bytes(a);

        Ula b;
        b.set_ulanext_en(true);
        b.init_control_per_line();   // every row says "true", flag active
        StateReader r(buf.data(), buf.size());
        b.load_state(r);
        check("S4-ULA-PERLINE-CLEARED",
              b.ulanext_en_for_line(5) == false && r.position() == buf.size(),
              "a restore deactivates the per-line control snapshot, so a "
              "render taken before the next frame initialises it reads the "
              "RESTORED live registers and not the pre-restore frame's rows");
    }

    // ── Renderer: the nested restore runs BOTH children's post-walk work ──
    //
    // `Renderer::load_state` performs the ULA's and LoRes's walks itself, as
    // part of its own nested declaration, so it must call both
    // `after_load_state()`s. This is the path the emulator actually uses —
    // `Emulator::load_state` calls `renderer_.load_state(r)`, never
    // `ula_.load_state` — and the rows above exercise the two subsystems
    // STANDALONE, which is a different entry point.
    {
        Renderer a;
        a.ula().set_ulanext_en(false);
        a.lores().set_nr6a(0x05);
        std::vector<uint8_t> buf = s4::save_bytes(a);
        // lores_nr6a is the LAST byte of block 10 (S4-DECL-LORES-SUFFIX).
        check("S4-RENDERER-NESTED-OFFSET",
              buf.size() == 3688 && buf[3687] == 0x05,
              "lores_nr6a really is the last byte of a Renderer save — the "
              "row below is meaningless if it corrupts another field");
        buf[3687] = 0xC5;   // bits 7:6 set: not part of a 6-bit register

        Renderer b;
        b.ula().set_ulanext_en(true);
        b.ula().init_control_per_line();
        StateReader r(buf.data(), buf.size());
        b.load_state(r);
        check("S4-RENDERER-NESTED-AFTER-LOAD",
              b.lores().nr6a() == 0x05 &&
                  b.ula().ulanext_en_for_line(5) == false &&
                  r.position() == buf.size(),
              "a restore driven through Renderer — the path Emulator::"
              "load_state uses — runs BOTH nested subsystems' post-walk work: "
              "LoRes's NR $6A mask and the ULA's per-line deactivation, "
              "neither of which the nested walk itself performs");
    }

    // ── Copper: the instruction RAM collapse, and the mode enum8 ──────────
    {
        Copper cop;
        // NR 0x61/0x62 set the write address; NR 0x63 writes 16 bits.
        cop.write_reg_0x62(0x00);        // mode 00, addr MSB 0
        cop.write_reg_0x61(0x14);        // byte address 0x14 -> instruction 10
        cop.write_reg_0x63(0xAB);
        cop.write_reg_0x63(0xCD);
        cop.write_reg_0x62(0x80);        // mode 10 = run from last point
        std::vector<uint8_t> buf = s4::save_bytes(cop);
        check("S4-COPPER-INSTR-ORDER",
              buf.size() == 2057 && buf[0x14] == 0xCD && buf[0x15] == 0xAB &&
                  cop.instruction(10) == 0xABCD,
              "the 2 048-byte instruction array is instruction-major and "
              "little-endian within each 16-bit word: instruction 10 lands at "
              "byte offsets 0x14/0x15, exactly where 1 024 write_u16 calls "
              "put it");
        check("S4-COPPER-MODE-OFFSET",
              s4::kCopperModeOff == 2050 && buf[s4::kCopperModeOff] == 2,
              "mode really is at offset 2 050, straight after the array and "
              "the 16-bit PC");
        buf[s4::kCopperModeOff] = 0x0C;   // no NR 0x62 mode has ordinal 12

        Copper back;
        back.write_reg_0x62(0xC0);        // mode 11, so a wrong restore shows
        StateReader r(buf.data(), buf.size());
        back.load_state(r);
        check("S4-COPPER-MODE",
              back.mode() == 3 && r.position() == buf.size(),
              "an out-of-range NR 0x62 mode ordinal is refused: the field "
              "keeps its pre-load mode instead of taking one the two-bit "
              "register cannot hold, and the stream still ends where it "
              "should");
    }

    return 0;
}


// ── Test 22: GH #27 S5 — what the migration CHANGED, not just transcribed ─
//
// The mutation table for S5 was derived from `git diff`, not from the row
// list, and eight reverts killed nothing in any suite:
//
//   * the two Dma restore masks (turbo_ & 0x03, dma_timer_s_ & 0x3FFF)
//   * the three Md6ConnectorX2 masks (two 12-bit latches, the 9-bit counter)
//   * the MembraneStick keymap_addr_ & 0x01FF mask
//   * DivMmc restoring its two split enable levers from the STREAM rather
//     than deriving them from the composite `enabled_` byte
//   * I2cController restoring its two pi_i2c1 line inputs at all
//
// Every one of them is behaviour S5 MOVED rather than introduced — the masks
// out of the read expressions and into the line after the walk, the DivMMC
// levers by dropping a dead mid-read seed — and every one was uncovered
// before S5 as well. A ninth, the auto-type queue's count clamp, is behaviour
// S5 RESHAPED: the rebuild loop is now bounded by MAX_AUTO_TYPE_KEYS with the
// count gating the push, which is `BinReadDesc::fifo`'s idiom and is what
// makes a forged count unable to index past the staging array.
//
// Each poke-a-byte row is PAIRED with an OFFSET row asserting that the byte
// it pokes really is the field it names. A corruption row that hits the wrong
// field passes for the wrong reason, which is the failure mode this pairing
// exists to close.
//
// The offsets are read off the declarations the S5-DECL-* rows pin, so a
// reordering that moved a field would fail there first and these rows second,
// rather than silently testing a neighbour.

namespace s5 {

/// Save `obj` into a fresh buffer sized by a measure pass. Returns the bytes.
template <typename T>
std::vector<uint8_t> save_bytes(const T& obj)
{
    StateWriter measure;
    obj.save_state(measure);
    std::vector<uint8_t> buf(measure.position(), 0);
    StateWriter w(buf.data(), buf.size());
    obj.save_state(w);
    return buf;
}

void poke16(std::vector<uint8_t>& b, std::size_t off, uint16_t v)
{
    std::memcpy(b.data() + off, &v, sizeof(v));
}

uint16_t peek16(const std::vector<uint8_t>& b, std::size_t off)
{
    uint16_t v = 0;
    std::memcpy(&v, b.data() + off, sizeof(v));
    return v;
}

}  // namespace s5

static int test_s5_restore_behaviour()
{
    printf("\n--- Test 22: GH #27 S5 restore behaviour ---\n");

    // ── Dma: turbo_ is masked to 2 bits, dma_timer_s_ to 14 ──────────────
    //
    // `turbo_` is NR 0x06 bits 1:0 (zxnext.vhd:4966) and `dma_timer_s_` is
    // device/dma.vhd's 14-bit burst prescaler timer, so a wider value in the
    // stream is not a state the hardware can be in. Pre-S5 the masks were
    // part of the read expressions; they are now the line after the walk, and
    // nothing pinned them in either place.
    {
        Dma dma;
        dma.set_turbo(0x02);
        std::vector<uint8_t> b = s5::save_bytes(dma);

        check("S5-DMA-OFFSET", b.size() == 43 && b[32] == 0x02,
              "byte 32 of Dma's 43-byte block is turbo_ — the field the next "
              "row pokes, proved by an honest save of a known value");

        b[32] = 0xFF;                    // turbo_:       6 bits too wide
        s5::poke16(b, 33, 0xFFFF);       // dma_timer_s_: 2 bits too wide

        Dma back;
        StateReader r(b.data(), b.size());
        back.load_state(r);

        check("S5-DMA-TURBO", back.turbo() == 0x03,
              "an over-wide turbo_ in the stream restores masked to its two "
              "VHDL bits instead of carrying six bits the hardware has no "
              "encoding for");
        check("S5-DMA-TIMER", back.dma_timer() == 0x3FFF,
              "an over-wide dma_timer_s_ restores masked to the 14 bits "
              "device/dma.vhd's burst prescaler actually has");
        check("S5-DMA-TIMER-OFFSET", r.position() == 43,
              "…and the restore consumed exactly the declared 43 bytes, so "
              "the two pokes landed inside Dma's block and not past it");
    }

    // ── Md6ConnectorX2: two 12-bit latches and a 9-bit counter ───────────
    {
        Md6ConnectorX2 md6;
        md6.set_latched_left_for_test(0x0A5A);
        md6.set_latched_right_for_test(0x05A5);
        md6.set_state_for_test(0x0155);
        std::vector<uint8_t> b = s5::save_bytes(md6);

        check("S5-MD6-OFFSET",
              b.size() == 16 && s5::peek16(b, 4) == 0x0A5A &&
                  s5::peek16(b, 6) == 0x05A5 && s5::peek16(b, 8) == 0x0155,
              "bytes 4, 6 and 8 of Md6ConnectorX2's 16-byte block are the two "
              "latches and the select counter — the three fields the next row "
              "pokes, proved by an honest save of three known values");

        s5::poke16(b, 4, 0xFFFF);
        s5::poke16(b, 6, 0xFFFF);
        s5::poke16(b, 8, 0xFFFF);

        Md6ConnectorX2 back;
        StateReader r(b.data(), b.size());
        back.load_state(r);
        // The two latches have setters but no getters, so the masked values
        // are read back out through an honest re-save at the same offsets the
        // OFFSET row above proved are theirs.
        std::vector<uint8_t> again = s5::save_bytes(back);

        check("S5-MD6-LATCH",
              s5::peek16(again, 4) == 0x0FFF && s5::peek16(again, 6) == 0x0FFF,
              "two over-wide latches restore masked to the 12 bits the MD "
              "6-button word has, which a re-save reads straight back out");
        check("S5-MD6-STATE", back.state_for_test() == 0x01FF,
              "an over-wide select counter restores masked to the 9 bits "
              "md6_connector_x2.vhd's FSM counter has");
        check("S5-MD6-POS", r.position() == 16 && again.size() == 16,
              "…and the restore consumed exactly the declared 16 bytes, so "
              "the three pokes landed inside Md6's block and not past it");
    }

    // ── MembraneStick: NR 0x28's keymap address is 9-bit ─────────────────
    {
        MembraneStick ms;
        std::vector<uint8_t> b = s5::save_bytes(ms);

        check("S5-MEMBRANE-OFFSET", b.size() == 73 && s5::peek16(b, 71) == 0,
              "bytes 71-72 of MembraneStick's 73-byte block are keymap_addr_ "
              "— the field the next row pokes, at the end of a block whose "
              "length is itself the proof that nothing follows it");

        s5::poke16(b, 71, 0xFFFF);

        MembraneStick back;
        StateReader r(b.data(), b.size());
        back.load_state(r);
        std::vector<uint8_t> again = s5::save_bytes(back);

        check("S5-MEMBRANE-ADDR", s5::peek16(again, 71) == 0x01FF,
              "an over-wide keymap_addr_ restores masked to the 9 bits "
              "NR 0x28 gives it, which a re-save reads straight back out");
        check("S5-MEMBRANE-ADDR-POS", r.position() == 73,
              "…and the restore consumed exactly the declared 73 bytes, so "
              "the poke landed inside MembraneStick's block and not past it");
    }

    // ── DivMmc: the split levers come from the STREAM ────────────────────
    //
    // The hand-written pair seeded them from the composite `enabled_` byte
    // mid-read and then overwrote both from their own persisted values at the
    // end of the same read; the seed was dead and S5 dropped it. The property
    // that makes the drop safe is the one this row pins, and it is the whole
    // reason the two levers are persisted separately: the firmware-reset
    // shape is port_io=1 with nr_0a_4=0, whose composite is 0, so deriving
    // either lever from the composite loses it.
    {
        DivMmc mmc;
        mmc.set_enabled(false);
        mmc.set_port_io_enable(true);
        mmc.set_nr_0a_4_enable(false);
        std::vector<uint8_t> b = s5::save_bytes(mmc);

        check("S5-DIVMMC-OFFSET",
              b.size() == 131089 && b[0] == 0 && b[131087] == 1 &&
                  b[131088] == 0,
              "byte 0 of DivMmc's 131 089-byte block is the composite "
              "`enabled_` and bytes 131 087-131 088 are the two split levers "
              "— the exact firmware-reset shape, saved honestly");

        DivMmc back;
        back.set_port_io_enable(false);
        back.set_nr_0a_4_enable(true);
        StateReader r(b.data(), b.size());
        back.load_state(r);

        check("S5-DIVMMC-LEVERS",
              back.port_io_enable() && !back.nr_0a_4_enable(),
              "the two split enable levers restore from the STREAM, not from "
              "the composite byte: a snapshot holding port_io=1 / nr_0a_4=0 "
              "with enabled=0 survives, which deriving either from the "
              "composite would lose");
        check("S5-DIVMMC-LEVERS-POS", r.position() == 131089,
              "…and the restore consumed exactly the declared 131 089 bytes, "
              "128 KB window included");
    }

    // ── I2cController: the two pi_i2c1 line inputs travel ────────────────
    //
    // Both are `uint8_t` members the stream has always carried as BOOLEANS,
    // so S5 marshals them through a local `bool` and writes back 0/1.
    // Reverting the write-back left them at their pre-load values and no row
    // noticed.
    {
        I2cController i2c;
        i2c.set_pi_i2c1_scl(false);
        i2c.set_pi_i2c1_sda(true);
        std::vector<uint8_t> b = s5::save_bytes(i2c);

        check("S5-I2C-OFFSET",
              b.size() == 13 && b[11] == 0 && b[12] == 1,
              "bytes 11 and 12 of I2cController's 13-byte block are the two "
              "pi_i2c1 line inputs — the fields the next row restores, proved "
              "by an honest save of a known pair");

        I2cController back;
        back.set_pi_i2c1_scl(true);     // the OPPOSITE of what the stream has
        back.set_pi_i2c1_sda(false);
        StateReader r(b.data(), b.size());
        back.load_state(r);

        check("S5-I2C-PI",
              !back.pi_i2c1_scl() && back.pi_i2c1_sda(),
              "both pi_i2c1 line inputs restore from the stream, over the "
              "opposite live values — so a rewind replays the Pi's lines "
              "rather than keeping the ones the run had reached");
        check("S5-I2C-PI-POS", r.position() == 13,
              "…and the restore consumed exactly the declared 13 bytes");
    }

    // ── Keyboard: the auto-type count is CHECKED, never obeyed ───────────
    //
    // S5 marshals the queue through a LOCAL staging array, so the rebuild
    // loop indexes it — and a count taken from the file deciding how far to
    // index is precisely the `Ram::load_state` shape S3 found. Both loops are
    // therefore bounded by MAX_AUTO_TYPE_KEYS, with the count only gating the
    // push, which is what `BinReadDesc::fifo` does and for the same reason.
    {
        Keyboard kb;
        std::vector<Keyboard::AutoKey> keys;
        keys.push_back({1, 3, -1, -1, 2});   // row 1 col 3 = F
        keys.push_back({2, 0, -1, -1, 2});
        kb.queue_auto_type(keys);
        std::vector<uint8_t> b = s5::save_bytes(kb);

        uint32_t cnt = 0;
        std::memcpy(&cnt, b.data() + 12, sizeof(cnt));
        check("S5-KB-COUNT-OFFSET", b.size() == 342 && cnt == 2,
              "bytes 12-15 of Keyboard's 342-byte block are the auto-type "
              "queue count — the field the next row forges, proved by an "
              "honest save of a two-key queue");

        // A count far past the sixteen slots the stream actually carries.
        const uint32_t lie = 0x40000000u;
        std::memcpy(b.data() + 12, &lie, sizeof(lie));

        Keyboard back;
        StateReader r(b.data(), b.size());
        back.load_state(r);
        std::vector<uint8_t> again = s5::save_bytes(back);

        uint32_t restored = 0;
        std::memcpy(&restored, again.data() + 12, sizeof(restored));
        check("S5-KB-COUNT", restored == 16 && again.size() == 342,
              "a forged auto-type count of 2^30 restores clamped to the "
              "sixteen slots the stream actually carries — the rebuild loop "
              "is bounded by the DECLARED capacity, so it can neither index "
              "past the staging array nor resize the block");
        check("S5-KB-COUNT-POS", r.position() == 342,
              "…and the restore consumed exactly the declared 342 bytes, so "
              "the forged count did not move the stream either");

        // The hazard S5 INTRODUCED, as opposed to moved. One declaration has
        // to serve both directions, so the queue is marshalled out of the
        // staging array on the WRITE path too — and a rebuild that put back
        // anything other than what was there would make `save_state` mutate
        // the machine it is saving. Two saves of the same object must
        // therefore agree to the byte, and the second must still see two
        // keys: `StateWriter`'s measure pass is itself a save, so a rebuild
        // that inflated the queue would be visible in the very buffer the
        // measure pass sized.
        Keyboard pure;
        pure.queue_auto_type(keys);
        const std::vector<uint8_t> first  = s5::save_bytes(pure);
        const std::vector<uint8_t> second = s5::save_bytes(pure);
        uint32_t after = 0;
        std::memcpy(&after, second.data() + 12, sizeof(after));
        check("S5-KB-SAVE-PURE", first == second && after == 2,
              "saving twice gives byte-identical buffers and the queue still "
              "holds its two keys — one declaration serves both directions, "
              "so the write path's rebuild must put back exactly what it "
              "took and never mutate the machine being saved");
    }

    return 0;
}

// ── Test 23: GH #27 S5b — the duplicated RAM is gone ──────────────────────
//
// D3 and D4 (design §4.3, §17.0): 131 072 + 8 192 bytes travelled in every
// snapshot that did not need to, 6.1 % of every rewind slot. The DivMMC
// window is `Ram` page 16 onwards and the `ram` block already carries it; the
// Multiface private array is, on the Next, eight kilobytes of zeros whose live
// counterpart is `Ram` page 0x0B. S5b stops writing both.
//
// ── WHY THESE ROWS AND NOT THE GOLDEN ────────────────────────────────────
//
// The re-baselined golden was verified once, by hand, at the commit: the new
// 2 153 701-byte stream is the old 2 292 965-byte one with exactly two
// contiguous ranges excised and every other byte identical IN PLACE —
// [2 152 291, 2 283 363) and [2 283 678, 2 291 870). Both were proved
// redundant BEFORE the change, on the pre-S5b golden: the first was
// byte-identical to that same stream's `Ram` page 16 at offset 131 096 across
// all 131 072 bytes, and the second was entirely zero. That check needs a
// pre-S5b build to have produced the old image, so it cannot be a row here
// any more than the §17.1 gate could be.
//
// What CAN be a row is what the check leaves behind: the two re-baselined
// LENGTHS, the two block widths either side of the machine-level boundary,
// and — the ones that matter — that the bytes still arrive, through `Ram`,
// after a whole-machine restore. A shorter stream that loses state would pass
// a length row and fail these.
static int test_s5b_duplicated_ram_removed()
{
    printf("\n--- Test 23: GH #27 S5b duplicated RAM removed ---\n");

    // §17.0's two numbers. The Next differs from the other three by exactly
    // the Multiface array, and by nothing else: on 48K/128K/+3 there is no
    // backing, so that array IS the store and still travels (§4.3(2)).
    {
        struct { MachineType type; const char* name; size_t want; } cases[] = {
            { MachineType::ZXN_ISSUE2, "next",   2154295 },
            { MachineType::ZX48K,      "48k",    2162487 },
            { MachineType::ZX128K,     "128k",   2162487 },
            { MachineType::ZX_PLUS3,   "plus3",  2162487 },
        };
        bool all_ok = true;
        std::string detail;
        for (const auto& c : cases) {
            Emulator emu;
            EmulatorConfig cfg;
            cfg.type = c.type;
            emu.init(cfg);
            StateWriter measure;
            emu.save_state(measure);
            if (measure.position() != c.want) {
                all_ok = false;
                detail += std::string(c.name) + "=" +
                          std::to_string(measure.position()) + " (want " +
                          std::to_string(c.want) + ") ";
            }
        }
        if (!all_ok) fprintf(stderr, "  JNSX-S5B-LENGTHS: %s\n", detail.c_str());
        check("JNSX-S5B-LENGTHS", all_ok,
              "the stream is 2 154 295 bytes on the Next and 2 162 487 on "
              "48K/128K/+3 — every deliberate change to the byte stream is a "
              "number in a test rather than a fact in a commit message, and "
              "the machine-dependence is exactly the Multiface array and "
              "nothing else. S5b re-baselined it to 2 153 701 / 2 161 893 by "
              "removing the duplicated RAM; S6 adds 594: mf_type (1 byte, "
              "§10.2 P13) and the SD card's SPI FSM (589 + its 4-byte "
              "sentinel, §10.2 P1). Both deltas are machine-independent, so "
              "the 8 192-byte gap between the two numbers is unchanged");
    }

    // The DivMMC block, either side of the machine-level boundary. 17 bytes
    // of scalars at machine level; standalone the 128 KB is the only copy of
    // itself and still travels, which is what keeps divmmc_test row DA-09 a
    // round-trip rather than a silent no-op.
    {
        Emulator emu;
        build_emulator(emu, 2);
        StateWriter mw;
        emu.divmmc().save_state(mw);
        const size_t backed = mw.position();

        DivMmc bare;                      // no set_ram_backing()
        StateWriter sw;
        bare.save_state(sw);
        const size_t standalone = sw.position();

        check("S5B-DIVMMC-BLOCK", backed == 17,
              "a DivMmc the Emulator backed writes 17 bytes, not 131 089: the "
              "128 KB window is a REFERENCE to Ram page 16, which the same "
              "stream's `ram` block carries seventeen blocks earlier");
        check("S5B-DIVMMC-STANDALONE", standalone == 131089,
              "…while one nothing backed still writes all 131 089, because a "
              "stream with no `ram` block in it has nowhere to point and the "
              "private array is then the only copy of itself");
    }

    // The row that makes the removal safe rather than merely smaller: the
    // bytes must still ARRIVE. Write through the DivMMC overlay, save the
    // whole machine, scribble over the physical page, restore, read back
    // through the overlay.
    {
        Emulator emu;
        build_emulator(emu, 2);
        emu.divmmc().write_control(0x80 | 0x02);      // conmem, bank 2
        emu.divmmc().write(0x2123, 0x5A);             // -> Ram page 18
        emu.ram().page_ptr(16)[0x0007] = 0xC9;

        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        emu.save_state(w);

        // Destroy both the window's view and the physical page it aliases.
        emu.ram().page_ptr(18)[0x0123] = 0x00;
        emu.ram().page_ptr(16)[0x0007] = 0x00;

        StateReader r(buf.data(), buf.size());
        const bool loaded = emu.load_state(r);
        emu.divmmc().write_control(0x80 | 0x02);
        const uint8_t via_overlay = emu.divmmc().read(0x2123);
        const uint8_t via_page    = emu.ram().page_ptr(16)[0x0007];

        check("S5B-DIVMMC-RESTORE",
              loaded && via_overlay == 0x5A && via_page == 0xC9 &&
                  r.position() == buf.size() && !r.out_of_bounds(),
              "DivMMC RAM still arrives after a whole-machine restore, now "
              "through the `ram` block rather than its own copy — and the "
              "stream is consumed exactly, so dropping 128 KB from the write "
              "side did not leave the read side reading them");
    }

    // The Multiface array is machine-type conditional (§4.3(2)): the live 8 KB
    // on the Next is Ram page 0x0B, and what the stream used to carry was the
    // untouched private array — 8 192 zeros, verified on the pre-S5b golden.
    {
        Emulator next_emu;
        EmulatorConfig ncfg;
        ncfg.type = MachineType::ZXN_ISSUE2;
        next_emu.init(ncfg);
        StateWriter nw;
        next_emu.multiface().save_state(nw);

        s3::RecordDesc nrec;
        next_emu.multiface().describe_state(nrec);
        bool declares_ram = false;
        for (const auto& f : nrec.fields()) {
            if (f.rfind("blob ram ", 0) == 0) declares_ram = true;
        }

        Emulator k48;
        build_emulator(k48, 2);                        // 48K: no MF backing
        StateWriter kw;
        k48.multiface().save_state(kw);

        Multiface bare;                                // standalone: no backing
        StateWriter bw;
        bare.save_state(bw);

        check("S5B-MF-NEXT-ABSENT",
              nw.position() == 9 && !declares_ram,
              "on the Next the Multiface RAM member is ABSENT, not "
              "zero-filled: the declaration drops it and the block is 8 "
              "bytes of flip-flops plus S6's mf_type byte, because the live "
              "8 KB is Ram page 0x0B and the private array it used to write "
              "was dead zeros");
        check("S5B-MF-STANDALONE-PRESENT",
              kw.position() == 8201 && bw.position() == 8201,
              "…and on 48K/128K/+3, and in a standalone round-trip, it is "
              "still all 8 201 bytes, because with no backing the private "
              "array is the real store (§4.3(2)) — the one place the stream's "
              "width depends on the machine type");
    }

    // Multiface's counterpart of S5B-DIVMMC-RESTORE, on the machine where the
    // member was dropped.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        emu.init(cfg);
        emu.multiface().ram_data()[0x0100] = 0x3C;
        emu.ram().page_ptr(0x0B)[0x0101]   = 0xA7;

        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        emu.save_state(w);

        emu.ram().page_ptr(0x0B)[0x0100] = 0x00;
        emu.ram().page_ptr(0x0B)[0x0101] = 0x00;

        StateReader r(buf.data(), buf.size());
        const bool loaded = emu.load_state(r);

        check("S5B-MF-NEXT-RESTORE",
              loaded && emu.multiface().ram_data()[0x0100] == 0x3C &&
                  emu.multiface().ram_data()[0x0101] == 0xA7 &&
                  r.position() == buf.size() && !r.out_of_bounds(),
              "Multiface RAM still arrives on the Next after a whole-machine "
              "restore, through Ram page 0x0B — the window the device reads "
              "and writes is the page the `ram` block carries, which is why "
              "the private array was droppable in the first place");
    }

    // The stream changed shape, so a cache recorded by a pre-S5b jnext must
    // not be deserialised by this one. The length is part of the identity and
    // moved by 139 264 bytes, so it would already have been discarded; the
    // version is bumped anyway because `warm_start_cache.h`'s own rule says
    // to bump it whenever `save_state` changes shape, and a version bumped
    // only when nothing else would catch the change is one nobody can reason
    // about.
    check("S5B-WARMSTART-VERSION",
          warm_start::kFormatVersion == 3,
          "the warm-start state-stream format version is 3: S5b changed the "
          "shape of Emulator::save_state and S6 changed it again (mf_type + "
          "the SD FSM), and a cache recorded by an older jnext would "
          "otherwise be read field-for-field wrong");

    return 0;
}


// =====================================================================
// Test 24 — GH #27 S6: the gaps (design §10.2 P1, P13)
// =====================================================================
//
// S5b proved the removals; these prove the ADDITIONS, at the level that
// matters for a user: a whole-machine save and restore.
static int test_s6_gaps()
{
    printf("\n--- Test 24: GH #27 S6 gaps (SD FSM, mf_type) ---\n");

    // A scratch image with per-sector magic, so a block can be identified
    // from its first bytes alone.
    char tmpl[] = "/tmp/jnext-rewind-s6-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        check("S6-EMU-CMD18-MID", false, "could not create a scratch SD image");
        return 1;
    }
    for (uint32_t sec = 0; sec < 16; ++sec) {
        unsigned char blk[512] = {};
        blk[0] = static_cast<unsigned char>(sec);
        blk[1] = 0xA5;
        for (int i = 2; i < 512; ++i)
            blk[i] = static_cast<unsigned char>((sec * 3 + i) & 0xFF);
        if (write(fd, blk, 512) != 512) { /* checked by the rows below */ }
    }
    close(fd);
    const std::string img = tmpl;

    // ── S6-EMU-CMD18-MID — the stage's own acceptance criterion ─────────
    //
    // The SD FSM reaches the file through Emulator::save_state's appended
    // "sdcard" block, which is what makes the device-level round trip
    // (sdcard_test S6-SD-CMD18-MID) a property of a SNAPSHOT rather than of
    // a class nothing serialises. Pre-S6 there was no block at all and this
    // row's restored card answered 0xFF forever.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        emu.init(cfg);
        SdCardDevice& sd = emu.sd_card();
        sd.mount(img);

        auto cmd = [&sd](uint8_t c, uint32_t arg) {
            sd.receive(static_cast<uint8_t>(0x40 | (c & 0x3F)));
            sd.receive(static_cast<uint8_t>((arg >> 24) & 0xFF));
            sd.receive(static_cast<uint8_t>((arg >> 16) & 0xFF));
            sd.receive(static_cast<uint8_t>((arg >> 8) & 0xFF));
            sd.receive(static_cast<uint8_t>(arg & 0xFF));
            sd.receive(0x95);
            for (int i = 0; i < 16; ++i) if (sd.send() != 0xFF) break;
        };
        cmd(0, 0);
        cmd(8, 0x1AA);
        cmd(55, 0);
        cmd(41, 0x40000000);
        cmd(58, 0);
        cmd(18, 3);                       // stream from sector 3
        for (int i = 0; i < 16; ++i) if (sd.send() == 0xFE) break;
        uint8_t first[512];
        for (int i = 0; i < 512; ++i) first[i] = sd.send();
        (void)sd.send(); (void)sd.send();  // CRC
        for (int i = 0; i < 16; ++i) if (sd.send() == 0xFE) break;
        uint8_t part[100];
        for (int i = 0; i < 100; ++i) part[i] = sd.send();

        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        emu.save_state(w);

        // Destroy the stream as thoroughly as a fresh process would: the
        // pre-S6 restore left exactly this.
        sd.reset();

        StateReader r(buf.data(), buf.size());
        const bool loaded = emu.load_state(r);

        // The rest of sector 4, then the whole of sector 5's framing.
        bool tail_ok = true;
        for (int i = 100; i < 512; ++i) {
            if (sd.send() != static_cast<uint8_t>((4 * 3 + i) & 0xFF)) {
                tail_ok = false;
                break;
            }
        }
        (void)sd.send(); (void)sd.send();  // CRC
        bool token = false;
        for (int i = 0; i < 16; ++i) if (sd.send() == 0xFE) { token = true; break; }
        const uint8_t next0 = sd.send();
        const uint8_t next1 = sd.send();

        check("S6-EMU-CMD18-MID",
              loaded && first[0] == 3 && part[0] == static_cast<uint8_t>(4) &&
                  tail_ok && token && next0 == 5 && next1 == 0xA5 &&
                  r.position() == buf.size() && !r.out_of_bounds(),
              "a whole-machine save taken 100 bytes into the second sector "
              "of a CMD18 stream restores a card that is STILL streaming: "
              "the rest of that sector arrives, then sector 5's token and "
              "its magic. Pre-S6 the SD FSM was not in the stream at all "
              "and the restored card answered 0xFF for ever (design §10.2 "
              "P1, defect D1)");
    }

    // ── S6-EMU-MF-TYPE — mf_type survives a whole-machine round trip ────
    //
    // multiface_test's S6-MF-TYPE-01 proves the device; this proves it
    // through `Emulator::save_state`, where the value is what NR 0x0A reads
    // back to the guest.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        emu.init(cfg);
        emu.multiface().set_mode(0x02);          // "10": the lossy encoding

        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        emu.save_state(w);

        emu.multiface().set_mode(0x00);
        StateReader r(buf.data(), buf.size());
        const bool loaded = emu.load_state(r);

        check("S6-EMU-MF-TYPE",
              loaded && emu.multiface().mf_type() == 0x02 &&
                  emu.multiface().mode_128(),
              "NR 0x0A's mf_type \"10\" survives a whole-machine save: the "
              "pre-S6 rebuild from the three mode booleans returned \"01\", "
              "so a guest could watch a bit it had written change under a "
              "save (design §10.2 P13, defect D2)");
    }

    // ══ P7 — the mid-frame pause a save must never refuse ═══════════════
    //
    // `save_state` documents that snapshots "are only ever taken at a frame
    // boundary … so a restored machine has no frame in flight". The debugger
    // breaks MID-frame, which is exactly when a developer reaches for File ▸
    // Save Snapshot. The owner's rule (2026-09-23) is ALWAYS ADVANCE, NEVER
    // REFUSE, and the advance has to be safe.
    {
        Emulator emu;
        rw_build_s0(emu, 4);

        // One whole frame first, so the machine is at a real boundary and the
        // rows below are not measuring the very first frame's specialities.
        emu.run_frame();

        // A tilemap scroll set at the TOP of the frame. Its change-log entry
        // is tagged at row 0 and the compositor replays it from there.
        rw_nr(emu, 0x2F, 0x00); rw_nr(emu, 0x30, 0x11);

        // Break half-way down. RUN_TO_CYCLE pauses inside `run_frame`'s loop
        // exactly as a breakpoint does, leaving the frame half-executed.
        const uint64_t mid = emu.current_frame_cycle() +
                             emu.timing().master_cycles_per_frame / 2;
        emu.debug_state().set_active(true);
        emu.debug_state().breakpoints().set_oneshot(0xBEEF);
        emu.debug_state().run_to_cycle(mid);
        emu.run_frame();

        const bool broke_mid_frame =
            emu.debug_state().paused() && emu.frame_in_progress();

        // …and a SECOND scroll, written from the paused machine, which the
        // log tags at the row the break landed on.
        rw_nr(emu, 0x30, 0x77);

        const bool advanced   = emu.advance_to_frame_boundary();
        const bool at_boundary = !emu.frame_in_progress();

        // THE ROW THAT MATTERS. If the advance had re-run `begin_new_frame()`
        // on a frame already in progress — the Task 40 defect — the
        // per-scanline change log would have been cleared and re-baselined to
        // the MID-frame value, so every line would read 0x77 and beast.nex's
        // Copper gradient would render as a flat sky. Both values must
        // survive, on the sides of the break they were written on.
        const uint16_t top    = emu.tilemap().scroll_x_for_line(0);
        const uint16_t bottom =
            emu.tilemap().scroll_x_for_line(Renderer::FB_HEIGHT - 1);

        check("S6-P7-ADVANCE-01",
              broke_mid_frame && advanced && at_boundary,
              "a machine paused mid-frame is ADVANCED to the next frame "
              "boundary rather than refused: the save always works, and the "
              "cost — up to one frame past where the user paused — is the "
              "documented trade (design §10.2 P7, owner decision "
              "2026-09-23)");

        check("S6-P7-HISTORY-01",
              top == 0x11 && bottom == 0x77,
              "…and the advance does NOT wipe the frame's per-scanline "
              "change log: the scroll written at the top of the frame is "
              "still replayed at row 0 and the one written from the paused "
              "machine at the bottom. Re-running begin_new_frame() mid-frame "
              "is the Task 40 defect that flattened beast.nex's Copper sky, "
              "and a save that quietly destroyed a frame's raster history "
              "would be worse than one that refused");

        check("S6-P7-DEBUG-INTACT",
              emu.debug_state().paused() && emu.debug_state().active() &&
                  emu.debug_state().breakpoints().has_oneshot() &&
                  emu.debug_state().breakpoints().oneshot_addr() == 0xBEEF,
              "…and the debugging session is left exactly as it was found: "
              "still paused, still active, with its pending one-shot "
              "breakpoint intact — which resume()+pause() would have "
              "destroyed");

        // Calling it again at a boundary is a no-op that says so.
        check("S6-P7-ADVANCE-02",
              !emu.advance_to_frame_boundary() && !emu.frame_in_progress(),
              "a machine already at a frame boundary is not advanced, and "
              "the call reports that it did nothing — the running-machine "
              "case (the save queued to the next begin_new_frame()) lands "
              "here");
    }

    std::remove(img.c_str());
    return 0;
}

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
    test_snapshot_size_invariance();
    test_rewind_chain_corrupted_slot();
    test_rewind_restores_render_state();
    test_rewind_callers_render_state();
    test_rewind_across_soft_reset();
    test_s3_descriptor_layout();
    test_s3_restore_behaviour();
    test_s4_descriptor_layout();
    test_s4_restore_behaviour();
    test_s5_descriptor_layout();
    test_s5_restore_behaviour();
    test_s5b_duplicated_ram_removed();
    test_s6_gaps();

    printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
           pass_count + fail_count + (int)g_skipped.size(),
           pass_count, fail_count, g_skipped.size());
    return fail_count > 0 ? 1 : 0;
}
