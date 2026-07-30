// Copper Integration Test — full-Emulator G117 verification.
//
// G117: the Copper VHDL (`device/copper.vhd:54-119`) runs at the
// i_CLK_28 rising edge, advancing one MOVE / WAIT-compare per 28 MHz
// cycle. Pre-G117 jnext stepped the Copper exactly once per Z80
// instruction inside Emulator::run_frame (emulator.cpp:3252-3265,
// :3508-3514), collapsing dense Copper bursts (e.g. tilemap-class
// effects with 32 MOVEs/scanline) to one step per instruction.
//
// These rows install a Copper program through the real NR 0x60-0x63
// port path, set Copper mode = 1 (run), then call step_instruction a
// small number of times and observe the cumulative effect of the
// MOVE writes. Pre-G117 the NRs only reflect a tiny prefix of the
// Copper program; post-G117 the entire program completes within a
// few Z80 instructions.
//
// Plan reference: KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md G117.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "peripheral/copper.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    Result r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string hex2(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
}

} // namespace

// ── Emulator + Copper helpers ────────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// NextREG read/write through the real port path.
static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// Copper instruction encoders (mirror copper.vhd:92-108).
static constexpr uint16_t enc_move(uint8_t reg, uint8_t val) {
    return static_cast<uint16_t>(((reg & 0x7F) << 8) | val);
}
static constexpr uint16_t enc_wait(uint8_t hpos, uint16_t vpos) {
    return static_cast<uint16_t>(0x8000 | ((hpos & 0x3F) << 9) | (vpos & 0x1FF));
}

// Set the Copper byte pointer via NR 0x61 / 0x62. Preserves mode bits.
static void set_copper_byte_ptr(Emulator& emu, uint16_t byte_addr) {
    uint8_t mode_hi = static_cast<uint8_t>(nr_read(emu, 0x62) & 0xC0);
    nr_write(emu, 0x61, static_cast<uint8_t>(byte_addr & 0xFF));
    nr_write(emu, 0x62, static_cast<uint8_t>(mode_hi | ((byte_addr >> 8) & 0x07)));
}

// Program one 16-bit Copper instruction at word-address `word_addr`.
static void program_word(Emulator& emu, uint16_t word_addr, uint16_t instr) {
    set_copper_byte_ptr(emu, static_cast<uint16_t>((word_addr & 0x3FF) << 1));
    nr_write(emu, 0x63, static_cast<uint8_t>(instr >> 8));
    nr_write(emu, 0x63, static_cast<uint8_t>(instr & 0xFF));
}

// Set Copper mode, preserving the 11-bit byte address.
static void set_copper_mode(Emulator& emu, uint8_t mode) {
    uint8_t cur = nr_read(emu, 0x62);
    nr_write(emu, 0x62, static_cast<uint8_t>(((mode & 0x03) << 6) | (cur & 0x07)));
}

// ── G117 — cycle-accurate Copper scheduler ────────────────────────────

static void test_g117_cycle_accurate(Emulator& emu) {
    set_group("G117-CycleAccurate");

    // G117-MPC-01 — many MOVEs in one Copper burst all fire within
    // the master-cycle window of a small number of CPU instructions.
    //
    // Pre-G117: each execute_single_instruction() advances the Copper exactly
    // once. The 16 MOVEs would need 32+ instructions to all fire
    // (16 MOVEs + 16 move_pending stall cycles).
    //
    // Post-G117: a single 3.5 MHz CPU instruction = 32 master cycles,
    // so one or two execute_single_instruction() calls cover all 32 cycles +
    // give the Copper enough room to retire all 16 MOVEs.
    //
    // We use 3 execute_single_instruction() calls to leave generous slack.
    {
        // Reset the Copper instruction RAM to NOPs (MOVE NR 0, 0 — VHDL
        // copper.vhd:152 treats reg==0 as no-op). We program our 16-MOVE
        // burst at addresses 0..15 and a HALT (WAIT vpos=511) at 16.
        for (int i = 0; i < 64; ++i) {
            program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
        }

        // 16 MOVEs to NR 0x14 (transparent RGB — pure-storage register,
        // no write-handler side effects beyond storing the byte). Values
        // 0x10..0x1F so the FINAL value (0x1F) signals all 16 fired.
        for (int i = 0; i < 16; ++i) {
            program_word(emu, static_cast<uint16_t>(i),
                         enc_move(0x14, static_cast<uint8_t>(0x10 + i)));
        }
        program_word(emu, 16, enc_wait(0, 511));   // HALT (vpos=511 unreachable)

        // Reset NR 0x14 baseline so any Copper write is observable.
        nr_write(emu, 0x14, 0x00);

        // Stop the Copper, then start it (mode 0→1 edge resets PC to 0).
        set_copper_mode(emu, 0);
        set_copper_mode(emu, 1);

        // Three Z80 instructions ≈ 96 master cycles at 3.5 MHz, plenty
        // for 16 MOVEs (each costing ~2 cycles: 1 dispatch + 1 stall).
        for (int i = 0; i < 3; ++i) emu.execute_single_instruction();

        const uint8_t final_nr14 = nr_read(emu, 0x14);
        check("G117-MPC-01",
              "16 Copper MOVEs to NR 0x14 all fire within 3 Z80 "
              "instructions (post-G117 cycle-accurate scheduler)",
              final_nr14 == 0x1F,
              "got NR 0x14 = " + hex2(final_nr14) +
              " expected 0x1F (last of 16 MOVEs)");
    }

    // (MOVE-RASTER-style WAIT semantics are already exercised by
    // copper_test Group 1-3; the G117 fix only changes call frequency
    // from the Emulator, not Copper::execute internals — so a separate
    // integration row would be redundant.)
}

// ── G65 — CPU vs Copper NR-write priority ─────────────────────────────

static void test_g65_cpu_wins_tied_edge(Emulator& emu) {
    set_group("G65-Priority");

    // G65-PRI-01 — VHDL `zxnext.vhd:4769-4777` arbitration: when CPU
    // and Copper want to write the same NR within the same instruction
    // window, Copper's write fires first (priority mux selects Copper)
    // but CPU's request is HELD OVER and commits the next cycle, so
    // the FINAL NR value is the CPU's, not the Copper's.
    //
    // jnext models this as: CPU NR writes via port 0x253B enqueue
    // during the per-instruction tick (defer_cpu_nr_writes_=true) and
    // are flushed AFTER tick_copper_for_master_cycles. So if the
    // Copper-loop wrote NR X = V_copper at some master cycle in the
    // window, and the CPU's enqueued write is NR X = V_cpu, the
    // post-flush value is V_cpu.
    //
    // Stimulus: Copper program writes NR 0x14 = 0x55 then HALTs;
    // start the Copper, then inject a Z80N NEXTREG_NN that writes
    // NR 0x14 = 0xAA and execute it once.
    //
    // Pre-G65: Copper writes 0x55 AFTER the synchronous CPU commit
    //          inside the same instruction window → final 0x55. FAIL.
    // Post-G65: CPU write enqueued → Copper writes 0x55 mid-loop →
    //          flush_pending_cpu_nr_writes commits 0xAA → final 0xAA.
    {
        // Reset Copper state — clears PC + last_mode_ so the mode 0→1
        // edge below genuinely fires (last_mode_ tracking otherwise
        // leaks across test functions sharing the Emulator). Also
        // wipes instruction RAM, which is fine because we reload it
        // right after.
        emu.copper().reset();

        // Copper program: MOVE NR 0x14, 0x55 at PC 0; HALT at PC 1.
        for (int i = 0; i < 64; ++i) {
            program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
        }
        program_word(emu, 0, enc_move(0x14, 0x55));
        program_word(emu, 1, enc_wait(0, 511));   // HALT

        // Reset NR 0x14 baseline + start the Copper (mode 0→1 edge).
        nr_write(emu, 0x14, 0x00);
        set_copper_mode(emu, 0);
        set_copper_mode(emu, 1);

        // Inject a Z80N NEXTREG_NN instruction (ED 91 nn vv, 17 T-states).
        // ED 91 14 AA → write NR 0x14 = 0xAA via the CPU port path.
        // Place at 0xC000 (RAM bank visible at slot 6 for ZX Next default).
        const uint16_t code_addr = 0xC000;
        emu.mmu().write(code_addr + 0, 0xED);
        emu.mmu().write(code_addr + 1, 0x91);
        emu.mmu().write(code_addr + 2, 0x14);
        emu.mmu().write(code_addr + 3, 0xAA);

        // Set PC = 0xC000.
        auto regs = emu.cpu().get_registers();
        regs.PC = code_addr;
        emu.cpu().set_registers(regs);

        // Execute one instruction — the Z80N NEXTREG that writes NR 0x14.
        // During this one instruction's master-cycle window (~136 cycles
        // at 3.5 MHz), the Copper-loop fires its single MOVE writing
        // NR 0x14 = 0x55. Post-G65 the CPU's enqueued 0xAA commits AFTER
        // that, so the visible NR 0x14 is 0xAA.
        emu.execute_single_instruction();

        const uint8_t final_nr14 = nr_read(emu, 0x14);
        check("G65-PRI-01",
              "Tied-edge CPU vs Copper NR write: CPU value wins as final "
              "(VHDL zxnext.vhd:4769-4777 — Copper-priority mux + "
              "CPU-held-over)",
              final_nr14 == 0xAA,
              "got NR 0x14 = " + hex2(final_nr14) +
              " expected 0xAA (CPU deferred-write commits after Copper)");
    }
}

// ── T58 — Copper c_max_vc follows runtime video-timing changes ────────

static void test_t58_cmaxvc_repush() {
    set_group("T58-CMaxVc");

    // T58-CVC-01 — the Copper's vertical wrap must follow a RUNTIME
    // video-timing change.
    //
    // VHDL: zxula_timing.vhd:457-470 wraps the copper offset counter
    // `cvc` at `c_max_vc` (reload to i_cu_offset at ula_min_vactive,
    // +1 per line, wrap to 0 when cvc == c_max_vc), and `c_max_vc` is
    // re-derived from i_timing / i_50_60 alongside every other c_*
    // constant (zxula_timing.vhd:204 = 310 for 128K 50 Hz, :238 = 263
    // for 128K 60 Hz). i_50_60 is `eff_nr_05_5060`, latched at the
    // frame edge (zxnext.vhd:6697-6700, :6720). jnext models cvc as
    // (vc + NR 0x64 offset) mod (c_max_vc + 1) in Copper::execute.
    //
    // Discriminator: ZXN_ISSUE2 boots with 128K timing at 50 Hz
    // (c_max_vc = 310). Switch to 60 Hz (frame = 264 lines, c_max_vc =
    // 263) and set NR 0x64 offset = 100. A WAIT for vpos = 60 then:
    //   * correct c_max_vc = 263: cvc = (vc+100) mod 264 == 60 at
    //     vc = 224 (< 264) → the WAIT fires every frame;
    //   * stale c_max_vc = 310 (the pre-Task-58 bug — set once in
    //     init() and never re-pushed): cvc = (vc+100) mod 311 over
    //     vc ∈ [0,263] covers only 100..310 ∪ 0..52 — vpos 60 is
    //     unreachable, the WAIT never fires.
    // The MOVE behind the WAIT (NR 0x14 ← 0x5A) is the observable.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("T58-CVC-01", "Emulator::init(ZXN_ISSUE2) failed", false,
                  "Emulator::init returned false");
            return;
        }

        // Precondition: 128K-class timing at 50 Hz → vc_max = 310
        // (zxula_timing.vhd:204).
        const bool pre_ok = emu.video_timing().vc_max() == 310;

        // Copper program: WAIT(h=0, v=60); MOVE NR 0x14 ← 0x5A; HALT.
        for (int i = 0; i < 64; ++i) {
            program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
        }
        program_word(emu, 0, enc_wait(0, 60));
        program_word(emu, 1, enc_move(0x14, 0x5A));
        program_word(emu, 2, enc_wait(0, 511));   // HALT

        nr_write(emu, 0x64, 100);   // copper vertical offset
        nr_write(emu, 0x14, 0x00);  // observable baseline

        // Switch to 60 Hz: NR 0x05 bit 2 pending; the frame edge
        // commits it (zxnext.vhd:6697-6700) and the fix re-pushes the
        // Copper wrap alongside the rest of the timing constants.
        nr_write(emu, 0x05, 0x04);
        emu.run_frame();
        const bool at_60 = emu.video_timing().vc_max() == 263;

        // Start the Copper (mode 0→1 edge resets PC) and run one full
        // 60 Hz frame — the WAIT must fire at vc = 224 and the MOVE
        // must land.
        set_copper_mode(emu, 0);
        set_copper_mode(emu, 1);
        emu.run_frame();

        const uint8_t got = nr_read(emu, 0x14);
        check("T58-CVC-01",
              "runtime 50→60 Hz switch re-pushes the Copper c_max_vc "
              "wrap: WAIT vpos=60 with NR 0x64 offset=100 fires at "
              "vc=224 in a 264-line frame (stale 311-line wrap never "
              "reaches cvc=60) [zxula_timing.vhd:204/238/457-470; "
              "zxnext.vhd:6697-6700]",
              pre_ok && at_60 && got == 0x5A,
              std::string("pre_ok=") + std::to_string(pre_ok) +
              " at_60=" + std::to_string(at_60) +
              " NR14=" + hex2(got) + " (want 0x5A)");
    }
}

// ── GH #181 — WAIT hpos is compared against hc_ula, not master cycles ──

// Run NOPs from `code_addr` until NR 0x14 reaches each of `want[]` in turn,
// recording the frame-relative RAW pixel position (raw_vc * pixels_per_line
// + raw_hc) at the end of the instruction that produced each transition.
// Returns false if any expected value never appeared within `max_steps`.
static bool run_until_nr14_sequence(Emulator& emu, const uint8_t* want, int n,
                                    long* out_pixel, int max_steps)
{
    const int ppl = emu.video_timing().hc_max() + 1;
    int idx = 0;
    for (int s = 0; s < max_steps && idx < n; ++s) {
        emu.execute_single_instruction();
        if (nr_read(emu, 0x14) == want[idx]) {
            out_pixel[idx] = static_cast<long>(emu.current_scanline()) * ppl
                           + emu.current_hc();
            ++idx;
        }
    }
    return idx == n;
}

static void test_gh181_wait_hpos_domain()
{
    set_group("GH181-HcUla");

    // The Copper's `hcount_i` is the ULA 7 MHz PIXEL counter `hc_ula`,
    // NOT the 28 MHz master-cycle offset into the raw scanline:
    //
    //   zxnext.vhd:3949-3950  hcount_i => hc,  vcount_i => cvc
    //   zxnext.vhd:6737-6739  o_hc_ula => hc,  o_vc_cu  => cvc
    //   zxula_timing.vhd:427-438  hc_ula is clocked on i_CLK_7 (one tick
    //                             per PIXEL) and reset when the raw frame
    //                             counter hc = ula_min_hactive (:423-424,
    //                             = c_min_hactive - 12). Being a REGISTERED
    //                             reset it takes effect one tick later, so
    //                             hc_ula == 0 at raw hc == c_min_hactive-11
    //                             (:343 "delayed one pixel"; same origin
    //                             AttributeMux uses, attribute_mux.h:281).
    //   zxula_timing.vhd:457-470  cvc steps on the SAME ula_max_hc pulse,
    //                             so its line boundary is the shifted one.
    //   copper.vhd:94         hcount_i >= (hpos & "000") + 12
    //
    // For the 128K/Next 50 Hz slot (zxula_timing.vhd:195-196, :203):
    //   c_max_hc = 455 (456 pixels/line), c_min_hactive = 136,
    //   c_min_vactive = 64  =>  hc_ula == 0 at raw hc == 125.
    //
    // The observable is a Copper MOVE landing at a RAW raster position:
    // the CPU runs a NOP sled (8 raw pixels per instruction) so we can
    // bracket where each MOVE fired to within one instruction.
    Emulator emu;
    if (!build_next_emulator(emu)) {
        check("GH181-HCULA-01", "Emulator::init(ZXN_ISSUE2) failed", false, "");
        return;
    }

    // Geometry guard — every expected position below is a VHDL-derived
    // literal for this timing slot. If the slot ever changes, say so
    // loudly instead of silently comparing against the wrong constants.
    const int ppl  = emu.video_timing().hc_max() + 1;
    const int minh = emu.video_timing().display_origin().hc;
    const int minv = emu.video_timing().display_origin().vc;
    const bool geom_ok = (ppl == 456 && minh == 136 && minv == 64 &&
                          emu.video_timing().vc_max() == 310);
    const std::string geom = "ppl=" + std::to_string(ppl) +
                             " min_hactive=" + std::to_string(minh) +
                             " min_vactive=" + std::to_string(minv);

    // Land on a frame boundary, then take the CPU over with a NOP sled
    // and interrupts off so the raster advances at a fixed 8 pixels per
    // execute_single_instruction().
    emu.run_frame();

    const uint16_t code_addr = 0xC000;
    for (uint32_t a = code_addr; a <= 0xFFFF; ++a)
        emu.mmu().write(static_cast<uint16_t>(a), 0x00);   // NOP
    auto regs = emu.cpu().get_registers();
    regs.PC = code_addr;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);

    // One Copper list, three observable MOVEs to NR 0x14 (pure storage):
    //   0: WAIT(hpos=52, vpos=95)   1: MOVE NR 0x14 = 0x33   <- show512
    //   2: WAIT(hpos=0,  vpos=100)  3: MOVE NR 0x14 = 0x5A   <- origin
    //   4: WAIT(hpos=50, vpos=100)  5: MOVE NR 0x14 = 0xA7   <- scale
    //   6: HALT
    emu.copper().reset();
    for (int i = 0; i < 16; ++i)
        program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
    program_word(emu, 0, enc_wait(52, 95));
    program_word(emu, 1, enc_move(0x14, 0x33));
    program_word(emu, 2, enc_wait(0, 100));
    program_word(emu, 3, enc_move(0x14, 0x5A));
    program_word(emu, 4, enc_wait(50, 100));
    program_word(emu, 5, enc_move(0x14, 0xA7));
    program_word(emu, 6, enc_wait(0, 511));   // HALT
    nr_write(emu, 0x64, 0);                   // no Copper vertical offset
    nr_write(emu, 0x14, 0x00);                // observable baseline
    set_copper_mode(emu, 1);

    const uint8_t want[3] = {0x33, 0x5A, 0xA7};
    long got[3] = {-1, -1, -1};
    const bool all_fired = run_until_nr14_sequence(emu, want, 3, got, 40000);

    // Detection granularity: the NOP that CONTAINS the firing master cycle
    // is observed at its END, so an observation may lag the true position
    // by up to one instruction (8 raw pixels). Allow 16.
    constexpr long kSlack = 16;
    auto near = [](long obs, long exp) {
        return obs >= exp && obs <= exp + kSlack;
    };

    // GH181-HCULA-01 — ORIGIN. WAIT(hpos=0, vpos=100) has threshold
    // (0<<3)+12 = 12, satisfied at hc_ula == 12, i.e. raw hc = 125+12 = 137
    // (raw hc >= 125, so this is still raw line 100+64 = 164).
    // Pre-fix jnext compared 12 against MASTER cycles from the raw line
    // start, firing at raw pixel 3 of the same line — 134 pixels early.
    {
        const long exp = 164L * 456 + 137;   // 74921
        check("GH181-HCULA-01",
              "WAIT hpos=0 fires at hc_ula==12, i.e. raw hc = "
              "(c_min_hactive-11)+12 = 137 — not at raw hc 3 "
              "[copper.vhd:94; zxula_timing.vhd:423-436]",
              geom_ok && all_fired && near(got[1], exp),
              geom + " got=" + std::to_string(got[1]) +
              " expected=" + std::to_string(exp) +
              " (pre-fix would be " + std::to_string(164L * 456 + 3) + ")");
    }

    // GH181-HCULA-02 — SCALE. Two WAITs on the SAME cvc line differing by
    // hpos 0 -> 50 must be exactly 50*8 = 400 PIXELS apart (copper.vhd:94
    // shifts hpos left by 3 into a 7 MHz pixel counter). Pre-fix those 400
    // units were MASTER cycles, i.e. 100 pixels — a 4x scale error.
    // The second WAIT's threshold 412 lands at raw hc (125+412) mod 456 =
    // 81, which is on the NEXT raw line (165) but still cvc line 100.
    {
        const long delta = got[2] - got[1];
        check("GH181-HCULA-02",
              "hpos step of 50 == 400 raw PIXELS between two WAITs on one "
              "cvc line (7 MHz hc_ula), not 400 master cycles = 100 pixels "
              "[copper.vhd:94; zxnext.vhd:3949 + :6737]",
              geom_ok && all_fired && delta >= 400 - kSlack &&
              delta <= 400 + kSlack,
              geom + " delta=" + std::to_string(delta) +
              " expected=400 (pre-fix 100); p0=" + std::to_string(got[1]) +
              " p1=" + std::to_string(got[2]));
    }

    // GH181-HCULA-03 — the show512.nex worked example from GH #181.
    // ShowAll512Colors does WAIT(vpos=95, hpos=52) then MOVE NR $43.
    // Threshold (52<<3)+12 = 428. hc_ula=428 => raw hc = (125+428) mod 456
    // = 97, which is BEFORE the cvc line boundary at raw hc 125, so it is
    // raw line 95+64+1 = 160 — in the blanking ahead of that line's
    // display. framebuffer_row = 160 - vblank_top(32) = 128, the field
    // boundary the demo actually shows on hardware.
    // Pre-fix: 428 master cycles = raw pixel 107 of raw line 159 => fb row
    // 127, the one-row anomaly filed in GH #181.
    {
        const long exp = 160L * 456 + 97;    // 73057
        const long line = (got[0] >= 0) ? got[0] / 456 : -1;
        check("GH181-HCULA-03",
              "show512 WAIT(vpos=95,hpos=52) MOVE lands on raw line 160 "
              "(fb row 128) at raw hc 97, not raw line 159 (fb row 127) "
              "[copper.vhd:94; zxula_timing.vhd:423-436, :457-470]",
              geom_ok && all_fired && near(got[0], exp),
              geom + " got=" + std::to_string(got[0]) +
              " (raw line " + std::to_string(line) + ")" +
              " expected=" + std::to_string(exp) + " (raw line 160);" +
              " pre-fix " + std::to_string(159L * 456 + 107) + " (line 159)");
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Copper Integration Tests (G117 cycle-accurate scheduler)\n");
    std::printf("====================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_g117_cycle_accurate(emu);
    std::printf("  Group: G117-CycleAccurate — done\n");

    test_g65_cpu_wins_tied_edge(emu);
    std::printf("  Group: G65-Priority — done\n");

    test_t58_cmaxvc_repush();
    std::printf("  Group: T58-CMaxVc — done\n");

    test_gh181_wait_hpos_domain();
    std::printf("  Group: GH181-HcUla — done\n");

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
