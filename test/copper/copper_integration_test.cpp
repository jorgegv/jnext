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

    // TIM-CYC-02 (GH #201) — a SATISFIED WAIT costs one 28 MHz cycle, not
    // one Z80 instruction.
    //
    // The comment that stood here said a WAIT row would be redundant
    // because "copper_test Group 1-3 already exercises WAIT semantics".
    // Those rows step `Copper::execute` by hand, so they say what a WAIT
    // does per CALL and nothing about how often the Emulator calls it — the
    // whole content of the row. And the GH181-HCULA rows below, which DO
    // measure a WAIT at the Emulator tier, bracket it to `kSlack = 16` raw
    // pixels = two NOPs, so a Copper that advanced once per instruction
    // lands inside their bound and passes. So nothing covered this.
    //
    // VHDL: `copper_mod` is clocked by `i_CLK_28` (zxnext.vhd:3944), and in
    // copper.vhd:92-98 a WAIT whose condition holds does
    // `copper_list_addr_s + 1` on THAT clock with `copper_dout_s <= '0'` —
    // one 28 MHz cycle, no stall. The MOVE at the next address then emits
    // its write pulse on the following cycle (:100-108) and clears it on
    // the one after (:87-89). A whole burst therefore retires inside the 32
    // master cycles of one 3.5 MHz NOP.
    //
    // Shape: part A calibrates that an 8-MOVE burst fits in ONE instruction
    // with no WAIT in front (8 x 2 = 16 cycles, well inside 32). Part B puts
    // an ALREADY-SATISFIED WAIT in front of the same burst and requires the
    // one instruction to still be enough: 1 cycle for the WAIT, 16 for the
    // MOVEs. Pre-G117 — one Copper step per instruction — part A reads 0x10
    // and part B reads 0x00.
    {
        Emulator emu;
        build_next_emulator(emu);

        // A NOP sled makes every step exactly 4 T = 32 master cycles at the
        // NR 0x07 power-on speed, so the window is a known quantity rather
        // than whatever the boot ROM happens to be executing.
        const uint16_t code_addr = 0xC000;
        for (uint32_t a = code_addr; a <= 0xFFFF; ++a)
            emu.mmu().write(static_cast<uint16_t>(a), 0x00);
        auto regs = emu.cpu().get_registers();
        regs.PC   = code_addr;
        regs.IFF1 = 0;
        regs.IFF2 = 0;
        emu.cpu().set_registers(regs);

        // ── Part A: 8 MOVEs, no WAIT. One instruction must retire all 8.
        emu.copper().reset();
        for (int i = 0; i < 32; ++i)
            program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
        for (int i = 0; i < 8; ++i)
            program_word(emu, static_cast<uint16_t>(i),
                         enc_move(0x14, static_cast<uint8_t>(0x10 + i)));
        program_word(emu, 8, enc_wait(0, 511));       // HALT
        nr_write(emu, 0x64, 0);
        nr_write(emu, 0x14, 0x00);
        set_copper_mode(emu, 0);
        set_copper_mode(emu, 1);
        emu.execute_single_instruction();
        const uint8_t burst_one_instr = nr_read(emu, 0x14);

        // ── Part B: the same burst behind an ALREADY-SATISFIED WAIT. One
        // instruction must still retire all 8, because the WAIT costs one
        // 28 MHz cycle out of the window's 32, not the window.
        //
        // "Already satisfied" has to be established, not hoped for: the
        // Copper compares against hc_ula / cvc (zxnext.vhd:3949-3950), so
        // the row walks the raster to a point where the current cvc line is
        // well inside its own satisfied span — hpos 0 means threshold 12
        // (copper.vhd:94), and the span runs to hc_ula 455 — then programs
        // the WAIT for THAT cvc. Programming goes through NR ports, which
        // execute no Z80 instruction, so the raster does not move between
        // the measurement and the one instruction that follows.
        //
        // This is the shape that discriminates. A version that merely
        // stepped until the burst appeared and then allowed one more
        // instruction passes even if a satisfied WAIT eats the rest of its
        // window — measured with that exact mutation, which left the row
        // green and had to be rewritten.
        const int zero_hc = emu.video_timing().hc_ula_zero_raw_hc();
        const int ppl_b   = emu.video_timing().hc_max() + 1;
        const int lpf_b   = emu.video_timing().vc_max() + 1;
        const int origin  = emu.video_timing().display_origin().vc;

        auto hc_ula_now = [&]() {
            const int hc = emu.current_hc();
            return (hc >= zero_hc) ? (hc - zero_hc) : (hc + ppl_b - zero_hc);
        };
        auto cvc_now = [&]() {
            const int hc = emu.current_hc();
            const int vc = emu.current_scanline();
            const int uline = (hc >= zero_hc) ? vc : ((vc - 1 + lpf_b) % lpf_b);
            return (uline - origin + lpf_b) % lpf_b;
        };

        // Park somewhere with the WAIT condition true and ~150 pixels of the
        // same cvc line still ahead, so one 32-master-cycle (8-pixel) window
        // cannot roll the line under us.
        bool parked = false;
        for (int i = 0; i < 4000 && !parked; ++i) {
            const int h = hc_ula_now();
            if (h >= 20 && h <= 300) parked = true;
            else emu.execute_single_instruction();
        }
        const int cvc_park = cvc_now();
        const int hcu_park = hc_ula_now();

        emu.copper().reset();
        for (int i = 0; i < 32; ++i)
            program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
        program_word(emu, 0, enc_wait(0, static_cast<uint16_t>(cvc_park)));
        for (int i = 0; i < 8; ++i)
            program_word(emu, static_cast<uint16_t>(i + 1),
                         enc_move(0x14, static_cast<uint8_t>(0x10 + i)));
        program_word(emu, 9, enc_wait(0, 511));       // HALT
        nr_write(emu, 0x64, 0);
        nr_write(emu, 0x14, 0x00);
        set_copper_mode(emu, 0);
        set_copper_mode(emu, 1);
        emu.execute_single_instruction();
        const uint8_t behind_wait = nr_read(emu, 0x14);

        check("TIM-CYC-02",
              "a satisfied Copper WAIT advances on a 28 MHz cycle, not on a "
              "Z80 instruction: an 8-MOVE burst behind an already-satisfied "
              "WAIT still retires inside ONE instruction window, the same "
              "one an unguarded burst needs  [copper.vhd:92-98 WAIT advance "
              "+ :100-108 MOVE, clocked by zxnext.vhd:3944 i_CLK_28; "
              "hcount_i/vcount_i = hc_ula/cvc per zxnext.vhd:3949-3950]",
              parked && burst_one_instr == 0x17 && behind_wait == 0x17,
              "parked=" + std::to_string(parked ? 1 : 0) +
              " at cvc=" + std::to_string(cvc_park) +
              " hc_ula=" + std::to_string(hcu_park) +
              "; no-WAIT burst in 1 instr = " + hex2(burst_one_instr) +
              "; behind a satisfied WAIT = " + hex2(behind_wait) +
              " (both want 0x17; pre-G117 0x10 / 0x00)");
    }
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

// ── GH #181 — the cvc line that SPANS two frames ───────────────

static void test_gh181_frame_boundary_carry()
{
    set_group("GH181-FrameCarry");

    // GH181-HCULA-04 — the rebase shifts the whole frame torus, so one
    // hc_ula/cvc line necessarily STRADDLES the jnext frame boundary, and
    // reaching its tail depends on the wrap being handled.
    //
    // With c_max_hc=455 (456 px/line), c_min_vactive=64, c_max_vc=310 (311
    // lines) and hc_ula==0 at raw hc 125 (zxula_timing.vhd:423-436):
    //   * hc_ula line `uline` covers raw (vc=uline, hc 125..455) — hc_ula
    //     0..330 — then raw (vc=uline+1, hc 0..124) — hc_ula 331..455.
    //   * uline = 310 is the LAST line of the frame, so its hc_ula 331..455
    //     tail is raw line **0**, reached only after `frame_cycle_` has
    //     advanced and `elapsed0` has gone back to ~0. That is the branch
    //     where `(elapsed0 + mcpf - shift_mc) % mcpf` wraps to the top of
    //     the frame instead of going negative.
    //   * cvc there = (310 - 64) mod 311 = 246.
    //
    // WAIT(vpos=246, hpos=40) — threshold (40<<3)+12 = 332, i.e. hc_ula 332,
    // raw hc (125+332) mod 456 = 1 — is therefore reachable ONLY in that
    // wrapped tail, at raw line 0.
    //
    // Discriminative twice over:
    //   * pre-fix (28 MHz master-cycle hc, origin 0): cvc 246 meant raw line
    //     310 and threshold 332 meant master cycle 332, so the MOVE landed
    //     at raw pixel 141443 — the far END of the frame, not its start;
    //   * mishandling the wrap (uline not reduced mod the frame, or cvc
    //     taken from the raw vc) puts cvc at 247 across raw line 0 and the
    //     WAIT never fires at all.
    Emulator emu;
    if (!build_next_emulator(emu)) {
        check("GH181-HCULA-04", "Emulator::init(ZXN_ISSUE2) failed", false, "");
        return;
    }

    const int ppl = emu.video_timing().hc_max() + 1;
    const bool geom_ok = (ppl == 456 &&
                          emu.video_timing().display_origin().vc == 64 &&
                          emu.video_timing().vc_max() == 310);

    // Land on a frame boundary so `frame_cycle_` has just advanced — that is
    // the state in which the wrapped tail is the CURRENT raster position.
    emu.run_frame();

    const uint16_t code_addr = 0xC000;
    for (uint32_t a = code_addr; a <= 0xFFFF; ++a)
        emu.mmu().write(static_cast<uint16_t>(a), 0x00);   // NOP sled
    auto regs = emu.cpu().get_registers();
    regs.PC = code_addr;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);

    emu.copper().reset();
    for (int i = 0; i < 16; ++i)
        program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
    program_word(emu, 0, enc_wait(40, 246));
    program_word(emu, 1, enc_move(0x14, 0x7E));
    program_word(emu, 2, enc_wait(0, 511));   // HALT
    nr_write(emu, 0x64, 0);
    nr_write(emu, 0x14, 0x00);
    set_copper_mode(emu, 1);

    const uint8_t want[1] = {0x7E};
    long got[1] = {-1};
    // One frame's worth of NOPs is ~17700; 40000 covers two with slack.
    const bool fired = run_until_nr14_sequence(emu, want, 1, got, 40000);
    const long line = (got[0] >= 0) ? got[0] / 456 : -1;

    // The tail is raw hc 0..124 of raw line 0. Detection lags by up to one
    // NOP (8 px) and the run_frame() overshoot can already have consumed a
    // few dozen pixels before we start stepping, so bound it at 200 — still
    // inside raw line 0, and three orders of magnitude from the pre-fix
    // position at raw pixel 141443 (raw line 310).
    check("GH181-HCULA-04",
          "the cvc line that straddles the frame boundary: "
          "WAIT(vpos=246,hpos=40) is reachable only in the hc_ula 331..455 "
          "tail, which is raw line 0 AFTER the frame wraps — the MOVE lands "
          "at the START of the frame, not at raw line 310 "
          "[zxula_timing.vhd:423-436,457-470; copper.vhd:94]",
          geom_ok && fired && line == 0 && got[0] >= 1 && got[0] <= 200,
          "geom_ok=" + std::to_string(geom_ok) + " fired=" +
          std::to_string(fired) + " got=" + std::to_string(got[0]) +
          " (raw line " + std::to_string(line) + ")" +
          " want raw line 0, hc in [1,200]; pre-fix 141443 (raw line 310)");
}

// ── GH #270 — WHERE along the line a NextREG write lands ───────────────

static void test_gh270_write_hpos()
{
    set_group("GH270-Hpos");

    // Emulator::nr_write_hpos() turns "a NextREG write is happening now"
    // into a column within the current scanline, for the Layer 2 change
    // logs to record. It has two sources and one conversion, and the
    // GH #270 regression row (layer2-midline-bank-func) only exercises one
    // source and the un-wrapped half of the conversion. These two rows
    // cover the rest.
    //
    // Both read the segment list back the way Renderer::render_frame builds
    // it — rewind_to_baseline() then apply_changes_for_line() per row — and
    // ask which column the second segment starts at.

    // ---- GH270-HPOS-01: the Copper hc_ula -> raw hc REBASE + WRAP ----
    //
    // The Copper compares against `hc_ula` (zxnext.vhd:3949, copper.vhd:35;
    // GH #181), whose zero is at raw hc = c_min_hactive - 11 = 125
    // (zxula_timing.vhd:423-436). The per-scanline LINE tag, however, is in
    // raw-frame space — Emulator::on_scanline is scheduled at raw hc == 0
    // (schedule_frame_events) — so the column must be rebased onto raw hc
    // before it means anything relative to that tag.
    //
    // hc_ula 331..455 is the part of an hc_ula line that lies in the NEXT
    // raw line's hc 0..124, i.e. in the left border BEFORE that line's
    // display. WAIT(hpos=40) has threshold (40<<3)+12 = 332
    // (copper.vhd:94), which is exactly there: raw hc = (125+332) mod 456
    // = 1, so the write precedes every pixel of the line it is tagged with
    // and must own ALL of it — first affected source column 1-136+2 = -133.
    //
    // Discriminative: drop the `- hc_span` wrap and the same write reports
    // raw hc 457, column 323 — past the 256-wide display, i.e. "affects no
    // part of this line", the exact opposite of the truth.
    //
    // Driven by run_frame(), not by execute_single_instruction(): the line
    // tag comes from the SCANLINE scheduler events, which the raw one-slot
    // primitive does not drain.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("GH270-HPOS-01", "Emulator::init(ZXN_ISSUE2) failed", false, "");
        } else {
            const int ppl  = emu.video_timing().hc_max() + 1;
            const int minh = emu.video_timing().display_origin().hc;
            const bool geom_ok = (ppl == 456 && minh == 136 &&
                                  emu.video_timing().display_origin().vc == 64);

            emu.run_frame();
            const uint16_t code_addr = 0xC000;
            for (uint32_t a = code_addr; a <= 0xFFFF; ++a)
                emu.mmu().write(static_cast<uint16_t>(a), 0x00);   // NOP sled
            auto regs = emu.cpu().get_registers();
            regs.PC = code_addr;
            regs.IFF1 = 0;
            regs.IFF2 = 0;
            emu.cpu().set_registers(regs);

            emu.copper().reset();
            for (int i = 0; i < 16; ++i)
                program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
            program_word(emu, 0, enc_wait(40, 100));      // hc_ula 332
            program_word(emu, 1, enc_move(0x12, 0x10));   // Layer 2 bank
            program_word(emu, 2, enc_wait(0, 511));       // HALT
            nr_write(emu, 0x64, 0);
            nr_write(emu, 0x12, 0x08);
            set_copper_mode(emu, 1);

            emu.run_frame();
            const bool fired = (nr_read(emu, 0x12) == 0x10);

            int found_row = -1, found_col = 0;
            size_t found_segs = 0;
            emu.layer2().rewind_to_baseline();
            for (int row = 0; row < 256; ++row) {
                emu.layer2().apply_changes_for_line(row);
                if (emu.layer2().line_segment_count() > 1 && found_row < 0) {
                    found_row  = row;
                    found_segs = emu.layer2().line_segment_count();
                    found_col  = emu.layer2().segment_first_column(1, false);
                }
            }
            check("GH270-HPOS-01",
                  "a Copper MOVE at hc_ula 332 rebases to raw hc 1 and owns "
                  "the whole scanline (column -133), not none of it "
                  "[copper.vhd:94; zxula_timing.vhd:423-436]",
                  geom_ok && fired && found_row >= 0 && found_segs == 2
                    && found_col == -133,
                  "geom_ok=" + std::to_string(geom_ok) +
                  " fired=" + std::to_string(fired) +
                  " row=" + std::to_string(found_row) +
                  " segs=" + std::to_string(found_segs) +
                  " col=" + std::to_string(found_col) +
                  " (expected -133; no-wrap would give 323)");
        }
    }

    // ---- GH270-HPOS-02: a CPU write reports its OWN raster position ----
    //
    // Copper::active_move_hc() is only non-negative for the duration of a
    // Copper MOVE's nextreg.write(). Everything else — a CPU OUT to
    // 0x253B, the NEX loader, a soft reset — falls through to
    // Emulator::current_hc(), the raw pixel counter, and lands at the
    // column the instruction ended on.
    //
    // Discriminative: without the fallback the write carries the line-start
    // tag and owns the WHOLE line (column <= 0), instead of splitting it
    // around two thirds of the way across.
    //
    // execute_single_instruction() is the raw one-slot primitive and drains
    // no scheduler events, so the SCANLINE tag this test needs has to be
    // applied the way Emulator::on_scanline would. That is the only part
    // stood in for; the write itself goes through the real port path, the
    // real NR 0x12 handler and the real nr_write_hpos().
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("GH270-HPOS-02", "Emulator::init(ZXN_ISSUE2) failed", false, "");
        } else {
            const int minh = emu.video_timing().display_origin().hc;
            const bool geom_ok = (minh == 136);

            emu.run_frame();
            const uint16_t code_addr = 0xC000;
            for (uint32_t a = code_addr; a <= 0xFFFF; ++a)
                emu.mmu().write(static_cast<uint16_t>(a), 0x00);   // NOP sled
            auto regs = emu.cpu().get_registers();
            regs.PC = code_addr;
            regs.IFF1 = 0;
            regs.IFF2 = 0;
            emu.cpu().set_registers(regs);

            // Make the Copper issue a MOVE FIRST, then stop it. The column
            // it publishes is live only for the duration of its own
            // nextreg.write(); a sentinel left standing afterwards would
            // hand the CPU write below the Copper's column (here hpos=20 ->
            // hc_ula 172 -> raw hc 297 -> column 163) instead of its own.
            emu.copper().reset();
            for (int i = 0; i < 16; ++i)
                program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
            program_word(emu, 0, enc_wait(20, 10));
            program_word(emu, 1, enc_move(0x12, 0x20));
            program_word(emu, 2, enc_wait(0, 511));   // HALT
            nr_write(emu, 0x64, 0);
            set_copper_mode(emu, 1);
            emu.run_frame();
            const bool copper_fired = (nr_read(emu, 0x12) == 0x20);
            set_copper_mode(emu, 0);          // Copper stopped: CPU path only
            nr_write(emu, 0x12, 0x08);

            // A NOP is 4 T-states = 8 raw pixels, so step until the raster
            // is somewhere in [200, 208) of a visible line — well inside the
            // 136..391 display window, so the resulting column is a genuine
            // mid-line split rather than a clamp at either end.
            bool arrived = false;
            for (int i = 0; i < 20000 && !arrived; ++i) {
                arrived = (emu.current_scanline() == 150
                           && emu.current_hc() >= 200
                           && emu.current_hc() < 208);
                if (!arrived) emu.execute_single_instruction();
            }
            const int hc = emu.current_hc();

            // Start a clean change log first. The per-line cursor walk in
            // apply_changes_for_line requires ASCENDING line tags, and the
            // boot frame this test ran leaves its own NR 0x12 traffic tagged
            // at the lines it happened on.
            emu.layer2().start_frame();
            emu.layer2().set_current_line(118);   // fb row of raw line 150
            nr_write(emu, 0x12, 0x10);            // CPU OUT, not a Copper MOVE

            emu.layer2().rewind_to_baseline();
            for (int row = 0; row <= 118; ++row)
                emu.layer2().apply_changes_for_line(row);
            const size_t segs = emu.layer2().line_segment_count();
            const int col     = emu.layer2().segment_first_column(1, false);

            check("GH270-HPOS-02",
                  "a CPU NextREG write takes its column from the live raster "
                  "position (hc 200..207 -> column 66..73), not from the "
                  "line start and not from a Copper MOVE that already ran",
                  geom_ok && copper_fired && arrived && segs == 2
                    && col == hc - minh + 2 && col >= 66 && col <= 73,
                  "geom_ok=" + std::to_string(geom_ok) +
                  " copper_fired=" + std::to_string(copper_fired) +
                  " arrived=" + std::to_string(arrived) +
                  " hc=" + std::to_string(hc) +
                  " segs=" + std::to_string(segs) +
                  " col=" + std::to_string(col) +
                  " want=" + std::to_string(hc - minh + 2));
        }
    }
}

// ── GH #270 — every tagged NextREG handler ────────────────────────────

static void test_gh270_tagged_handlers()
{
    set_group("GH270-Handlers");

    // GH270-HPOS-03 — the five NextREG write handlers that feed the Layer 2
    // change logs (0x12 and 0x13 -> the bank log; 0x16, 0x17 and 0x71 ->
    // the scroll log) each have to stamp the write with its column. A
    // handler that forgets falls back to the line-start tag, which
    // push_line_segment clamps forward onto the PREVIOUS segment and merges
    // away — so the segment COUNT is the observable, and it is exact.
    //
    // Four WAITs on one cvc line, 8 hpos units apart, give four distinct
    // columns (copper.vhd:94 threshold = (hpos<<3)+12, hc_ula -> raw hc +125,
    // raw hc -> column -136+2):
    //     hpos  8 -> hc_ula  76 -> raw 201 -> column  67   <- the MAME column
    //     hpos 16 -> hc_ula 140 -> raw 265 -> column 131
    //     hpos 24 -> hc_ula 204 -> raw 329 -> column 195
    //     hpos 32 -> hc_ula 268 -> raw 393 -> column 259   (past the display)
    // so a correct build reports 5 segments at exactly those columns, and
    // dropping ANY ONE handler's stamp reports 4.
    //
    // This is also the emulator-tier check on the MAME-measured boundary:
    // the reporter's WAIT(line, hpos=8) case puts it at display column 67.
    Emulator emu;
    if (!build_next_emulator(emu)) {
        check("GH270-HPOS-03", "Emulator::init(ZXN_ISSUE2) failed", false, "");
        return;
    }
    const bool geom_ok = (emu.video_timing().hc_max() + 1 == 456 &&
                          emu.video_timing().display_origin().hc == 136 &&
                          emu.video_timing().display_origin().vc == 64 &&
                          emu.video_timing().vblank_top() == 32);

    emu.run_frame();
    const uint16_t code_addr = 0xC000;
    for (uint32_t a = code_addr; a <= 0xFFFF; ++a)
        emu.mmu().write(static_cast<uint16_t>(a), 0x00);   // NOP sled
    auto regs = emu.cpu().get_registers();
    regs.PC = code_addr;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);

    emu.copper().reset();
    for (int i = 0; i < 16; ++i)
        program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
    program_word(emu, 0, enc_wait( 8, 100));
    program_word(emu, 1, enc_move(0x16, 0x11));   // L2 X scroll LSB
    program_word(emu, 2, enc_wait(16, 100));
    program_word(emu, 3, enc_move(0x17, 0x22));   // L2 Y scroll
    program_word(emu, 4, enc_wait(24, 100));
    program_word(emu, 5, enc_move(0x71, 0x01));   // L2 X scroll MSB
    program_word(emu, 6, enc_wait(32, 100));
    program_word(emu, 7, enc_move(0x13, 0x33));   // L2 shadow bank
    program_word(emu, 8, enc_wait(0, 511));       // HALT
    nr_write(emu, 0x64, 0);
    set_copper_mode(emu, 1);

    emu.run_frame();
    const bool fired = (nr_read(emu, 0x13) == 0x33);

    // fb row of raw line 64+100 = 164 is 164-32 = 132.
    emu.layer2().rewind_to_baseline();
    for (int row = 0; row <= 132; ++row)
        emu.layer2().apply_changes_for_line(row);
    const size_t segs = emu.layer2().line_segment_count();
    int c[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
        c[i] = emu.layer2().segment_first_column(static_cast<size_t>(i + 1), false);

    check("GH270-HPOS-03",
          "NR 0x16 / 0x17 / 0x71 / 0x13 each stamp their write with its own "
          "column: four MOVEs 8 hpos apart give segments at 67, 131, 195 and "
          "259 [copper.vhd:94; zxnext.vhd:5220,5226; layer2.vhd:110-122]",
          geom_ok && fired && segs == 5 &&
          c[0] == 67 && c[1] == 131 && c[2] == 195 && c[3] == 259,
          "geom_ok=" + std::to_string(geom_ok) +
          " fired=" + std::to_string(fired) +
          " segs=" + std::to_string(segs) +
          " cols=" + std::to_string(c[0]) + "," + std::to_string(c[1]) +
          "," + std::to_string(c[2]) + "," + std::to_string(c[3]) +
          " (expected 5 segments at 67,131,195,259)");
}

// ── GH #272 — a row boundary is crossed at its own master cycle ────────

// The framebuffer row a display register belongs to is decided by
// `Emulator::on_scanline()`: it snapshots the finished row's render state
// and retags the per-scanline change logs. Those events sit on the video-row
// scheduler and are due at raw hc 0.
//
// The per-instruction device cluster used to step the Copper across the
// WHOLE master-cycle window the instruction consumed and only then drain
// that scheduler. So a Copper MOVE that is physically AFTER a row boundary
// was counted into the row BEFORE it whenever one Z80 instruction happened
// to straddle the boundary — and which MOVEs of a burst were caught that way
// depended on where the CPU's instruction boundaries fell, which drifts from
// frame to frame in any real program. The reported symptom was a flickering
// line: next-point's controls menu, whose Copper writes NR 0x6F / 0x4C /
// 0x6B together at `WAIT(n, 40)`, showed a pink row on 3 frames in every 7.
//
// `WAIT(n, 40)` is the shape that makes this reachable: the threshold is
// `(40<<3)+12 = 332` in the hc_ula domain (copper.vhd:94), hc_ula 0 is raw
// hc 125 (zxula_timing.vhd:423-436) and a raw line is 456 pixels, so it is
// satisfied at raw hc 1 of the NEXT raw line, a handful of master cycles
// past the boundary. The MOVEs follow it two 28 MHz cycles apart
// (copper.vhd:87-89 clears the write pulse on the cycle after :100-108
// raises it), i.e. at raw offsets ~5, ~7 and ~9.
//
// Hardware puts all of them in that next raw line's OWN row: the tilemap's
// counters `whc` / `wvc` — which gate its per-character S_IDLE register
// latch (tilemap.vhd:345-354) and its fetch pipeline (:213-231) — are not
// reloaded for the row until raw hc 89 (`wide_min_hactive = c_min_hactive -
// 32 - 16 = 88`, registered, zxula_timing.vhd:475-503), well after the
// writes, and the transparency index is compared live per pixel (:427).
static void test_gh272_row_boundary_atomic() {
    set_group("GH272-RowBoundary");

    // Six consecutive Copper lines, each writing three registers that live
    // in DIFFERENT subsystems but share one snapshot point
    // (Emulator::snapshot_row_render_state): NR 0x4A fallback colour,
    // NR 0x14 transparent RGB, NR 0x31 tilemap Y scroll.
    //
    // Six lines is what sweeps the CPU phase. The CPU runs the reporter's
    // loop — `INC HL` + `JR` = 18 T — at 28 MHz, so 18 master cycles, and a
    // raw line is 1824: 1824 mod 18 = 6, so consecutive lines meet the loop
    // at all three of its phases. One of them ends an instruction between
    // the first MOVE and the second (the split that draws the pink row),
    // one ends past all three (the whole group one row early) and one is
    // aligned. Before the fix the six groups therefore land on a MIXTURE of
    // rows; after it, every group lands on the row the VHDL puts it in.
    static constexpr int kGroups   = 6;
    static constexpr int kFirstCvc = 100;
    static constexpr int kHpos     = 40;

    Emulator emu;
    if (!build_next_emulator(emu)) {
        check("GH272-ROWATOM-01", "Emulator::init(ZXN_ISSUE2) failed", false, "");
        return;
    }

    // The CPU: the reported program's 18 T-state loop at 28 MHz, in an
    // uncontended bank (0xC000 holds RAM bank 0 at reset, and the +3-class
    // contention rule only contends banks 4-7).
    emu.mmu().write(0xC000, 0x23);   // INC HL
    emu.mmu().write(0xC001, 0x18);   // JR -3
    emu.mmu().write(0xC002, 0xFD);
    auto regs = emu.cpu().get_registers();
    regs.PC   = 0xC000;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
    nr_write(emu, 0x07, 0x03);       // 28 MHz

    // Baseline the three observables, and the Copper's vertical offset.
    nr_write(emu, 0x64, 0x00);
    nr_write(emu, 0x4A, 0x00);
    nr_write(emu, 0x14, 0x00);
    nr_write(emu, 0x31, 0x00);

    emu.copper().reset();
    for (int i = 0; i < 64; ++i)
        program_word(emu, static_cast<uint16_t>(i), enc_move(0, 0));
    for (int g = 0; g < kGroups; ++g) {
        const uint16_t base = static_cast<uint16_t>(g * 4);
        program_word(emu, base,
                     enc_wait(kHpos, static_cast<uint16_t>(kFirstCvc + g)));
        program_word(emu, base + 1, enc_move(0x4A, static_cast<uint8_t>(0x10 + g)));
        program_word(emu, base + 2, enc_move(0x14, static_cast<uint8_t>(0x20 + g)));
        program_word(emu, base + 3, enc_move(0x31, static_cast<uint8_t>(0x30 + g)));
    }
    program_word(emu, kGroups * 4, enc_wait(0, 511));   // HALT

    set_copper_mode(emu, 0);
    set_copper_mode(emu, 3);   // run, reset the PC at every VBI

    // Three frames: the first commits the CPU-speed change and starts the
    // Copper, the next two run it from the top. The snapshot arrays hold
    // the LAST completed frame.
    emu.run_frame();
    emu.run_frame();
    emu.run_frame();

    // Where the VHDL puts group g. The WAIT threshold `(hpos<<3)+12` is in
    // the hc_ula domain, whose zero is raw hc `hc_ula_zero_raw_hc()`; if
    // that lands past the end of the raw line the MOVEs belong to the NEXT
    // raw line. cvc counts from the first active display line
    // (`display_origin().vc`) plus the NR 0x64 offset, which is 0 here, and
    // the framebuffer row is the raw line minus the vblank top.
    const int ppl  = emu.video_timing().hc_max() + 1;
    const int zero = emu.video_timing().hc_ula_zero_raw_hc();
    const int thr  = (kHpos << 3) + 12;
    const int carry = (zero + thr) / ppl;
    const int first_row = kFirstCvc + emu.video_timing().display_origin().vc
                        + carry - emu.video_timing().vblank_top();

    // The row before the first group must still carry what the registers
    // held when the frame opened — which is the LAST group's values, since
    // the Copper program reruns from the top every frame. That is the
    // assertion for group 0: before the fix its writes leak into this row
    // on the CPU phases where an instruction straddles the boundary.
    const uint8_t carry_fb = static_cast<uint8_t>(0x10 + kGroups - 1);
    const uint8_t carry_tr = static_cast<uint8_t>(0x20 + kGroups - 1);
    const uint8_t carry_sy = static_cast<uint8_t>(0x30 + kGroups - 1);
    bool   pre_ok = false;
    bool   all_ok = true;
    std::string detail;
    if (first_row >= 1 && first_row + kGroups <= Renderer::FB_HEIGHT) {
        pre_ok = emu.renderer().fallback_for_line(first_row - 1) == carry_fb
              && emu.renderer().transparent_rgb_for_line(first_row - 1) == carry_tr
              && emu.tilemap().scroll_y_for_line(first_row - 1) == carry_sy;
        for (int g = 0; g < kGroups; ++g) {
            const int r = first_row + g;
            const uint8_t fb = emu.renderer().fallback_for_line(r);
            const uint8_t tr = emu.renderer().transparent_rgb_for_line(r);
            const uint8_t sy = emu.tilemap().scroll_y_for_line(r);
            if (fb != 0x10 + g || tr != 0x20 + g || sy != 0x30 + g) all_ok = false;
            detail += " row" + std::to_string(r) + "=" + hex2(fb) + "/"
                    + hex2(tr) + "/" + hex2(sy);
        }
    } else {
        all_ok = false;
        detail = " first_row=" + std::to_string(first_row) + " out of range";
    }

    check("GH272-ROWATOM-01",
          "a Copper MOVE burst released in horizontal blanking lands "
          "ENTIRELY in the row it physically precedes, on every CPU phase: "
          "six groups on consecutive Copper lines each own one framebuffer "
          "row, all three registers together, instead of splitting across "
          "two rows wherever a Z80 instruction straddles the boundary "
          "[copper.vhd:94 WAIT threshold + :87-89/:100-108 MOVE cadence; "
          "zxula_timing.vhd:423-436 hc_ula origin, :475-503 the tilemap's "
          "whc/wvc reload at raw hc 89; tilemap.vhd:345-354, :427]",
          pre_ok && all_ok,
          "carried row " + std::to_string(first_row - 1) +
          (pre_ok ? " ok;" : " NOT the carried value;") + detail +
          " (want 0x10+g/0x20+g/0x30+g on rows " +
          std::to_string(first_row) + ".." +
          std::to_string(first_row + kGroups - 1) + ")");
}

// GH #272, the CPU half — the row boundary is crossed at its own master
// cycle even when the Copper is stopped, and a deferred CPU NextREG write
// is placed by the 28 MHz edge it commits on rather than by the end of the
// instruction that issued it.
//
// A CPU NR write is enqueued during the instruction and committed after it
// (G65), carrying the edge `io_request_edge() + 2` — VHDL
// zxnext.vhd:4739-4777, where the request is edge-detected into `cpu_req`
// and the registers take it on the following CLK_28 edge. Which row it
// belongs to is decided by that edge and the boundary, not by the
// instruction's extent.
//
// Two instructions straddle the same boundary from the same starting
// cycle and differ only in WHERE inside themselves they raise the request,
// so the pair pins both directions at once:
//
//   * `OUT (C),A` raises IORQ several T-states in, so at 28 MHz its commit
//     edge lands PAST the boundary — the write belongs to the next row.
//     Before the fix the whole deferred queue drained at the end of the
//     instruction, ahead of the boundary event, and it landed one row
//     early. This is the half that fails on unfixed code.
//   * the Z80N `NEXTREG nn,n` opcode requests at the instruction's first
//     cycle, so its edge precedes the boundary and the write belongs to
//     the row that is ending — exactly as it did before the fix. This is
//     the half that fails if the boundary commits the WHOLE deferred queue
//     instead of the part of it that precedes the boundary.
//
// Both run with the Copper stopped, which is also what pins the boundary
// walk as unconditional: gate it on `Copper::is_running()` and neither row
// is reached at its own cycle at all.

// Step NOPs until the NEXT instruction will start on the last pixel of a
// raw line, far enough down the frame that the following frame's first
// rows cannot overwrite the snapshot we are about to read. Returns the raw
// scanline parked on, or -1.
static int park_at_end_of_line(Emulator& emu, int pixels_before_end) {
    const int ppl = emu.video_timing().hc_max() + 1;
    for (int i = 0; i < 400000; ++i) {
        emu.debugger_step();
        const int row = emu.current_scanline() - emu.video_timing().vblank_top();
        // The test instruction is patched in at PC, so PC must be inside the
        // RAM sled with room for four bytes before its closing JP.
        const uint16_t pc = emu.cpu().get_registers().PC;
        if (emu.current_hc() == ppl - pixels_before_end && row > 64
                && row < Renderer::FB_HEIGHT - 8
                && pc >= 0xC000 && pc < 0xFFF0)
            return emu.current_scanline();
    }
    return -1;
}

// Finish the frame the write landed in WITHOUT starting to overwrite the
// rows we care about: step until the scanline wraps past the frame end.
static void finish_frame_by_stepping(Emulator& emu, int park_vc) {
    for (int i = 0; i < 400000; ++i) {
        if (emu.current_scanline() < park_vc) return;
        emu.debugger_step();
    }
}

static void prepare_cpu_write_emulator(Emulator& emu, bool turbo) {
    // 28 MHz makes one T-state one master cycle, so a NOP is exactly one
    // 7 MHz pixel and parking walks `current_hc()` one at a time; 3.5 MHz
    // makes a NOP 8 pixels and every instruction eight times longer, which
    // is what puts a row boundary comfortably PAST an OUT's request edge
    // while still inside the instruction.
    if (turbo) nr_write(emu, 0x07, 0x03);
    nr_write(emu, 0x4A, 0x00);      // the observable's baseline
    for (uint32_t a = 0xC000; a <= 0xFFFF; ++a)
        emu.mmu().write(static_cast<uint16_t>(a), 0x00);   // NOP sled
    // Close the sled into a loop: 16 K of NOPs is a few lines' worth at
    // 28 MHz, and a PC that walks off the end lands in ROM, where the
    // `mmu().write()` that patches the test instruction in is a no-op and
    // the row under test never gets written at all.
    emu.mmu().write(0xFFFD, 0xC3);   // JP 0xC000
    emu.mmu().write(0xFFFE, 0x00);
    emu.mmu().write(0xFFFF, 0xC0);
    auto regs = emu.cpu().get_registers();
    regs.PC   = 0xC000;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
    emu.copper().reset();           // and deliberately NOT started
}

static void test_gh272_cpu_write_past_boundary() {
    set_group("GH272-RowBoundary");

    // ── GH272-ROWATOM-02 — commit edge PAST the boundary → the next row.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("GH272-ROWATOM-02", "Emulator::init(ZXN_ISSUE2) failed", false, "");
        } else {
            prepare_cpu_write_emulator(emu, /*turbo=*/true);
            // Select NR 0x4A up front so the CPU executes only the DATA
            // write, whose request edge is the one under test.
            emu.port().out(0x243B, 0x4A);
            auto r = emu.cpu().get_registers();
            r.BC = 0x253B;
            r.AF = static_cast<uint16_t>(0x3C00 | (r.AF & 0x00FF));
            emu.cpu().set_registers(r);

            const int park_vc = park_at_end_of_line(emu, 1);
            uint8_t before = 0xFF, after = 0xFF;
            int row_before = -1;
            if (park_vc >= 0) {
                auto r2 = emu.cpu().get_registers();
                emu.mmu().write(r2.PC + 0, 0xED);   // OUT (C),A
                emu.mmu().write(r2.PC + 1, 0x79);
                emu.debugger_step();
                emu.mmu().write(r2.PC + 0, 0x00);
                emu.mmu().write(r2.PC + 1, 0x00);
                finish_frame_by_stepping(emu, park_vc);
                row_before = park_vc - emu.video_timing().vblank_top();
                before = emu.renderer().fallback_for_line(row_before);
                after  = emu.renderer().fallback_for_line(row_before + 1);
            }
            check("GH272-ROWATOM-02",
                  "a deferred CPU NextREG write whose commit edge falls PAST "
                  "a row boundary inside the same instruction lands in the "
                  "row AFTER the boundary: `OUT (C),A` on 0x253B started on "
                  "the line's last pixel raises IORQ past the boundary, and "
                  "the boundary is reached at its own master cycle although "
                  "the Copper is stopped "
                  "[zxnext.vhd:4739-4777 cpu_req edge-detect + next-edge "
                  "commit]",
                  park_vc >= 0 && before == 0x00 && after == 0x3C,
                  "park_vc=" + std::to_string(park_vc) +
                  " row " + std::to_string(row_before) + "=" + hex2(before) +
                  " (want 0x00) row " + std::to_string(row_before + 1) + "=" +
                  hex2(after) + " (want 0x3C)");
        }
    }

    // ── GH272-ROWATOM-03 — commit edge BEFORE the boundary → the row that
    // is ending. The companion direction: splitting an instruction at a row
    // boundary must not sweep the CPU's earlier writes over it.
    //
    // At the 3.5 MHz power-on speed one T-state is 8 master cycles, so
    // `OUT (C),A` spans 96 and its IORQ edge sits partway in (measured
    // between 64 and 80 by bisecting the park position — the row's detail
    // string prints enough to re-bisect if the CPU core's cycle placement
    // ever moves). Parking 32 pixels before the line's end puts the next
    // instruction 128 master cycles before the boundary; one `INC HL` (48,
    // and it does not touch A) advances that to 80, so the boundary falls
    // strictly inside the OUT and strictly AFTER its request edge. The
    // write therefore belongs to the row that is ending, exactly as it did
    // before the split existed.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            check("GH272-ROWATOM-03", "Emulator::init(ZXN_ISSUE2) failed", false, "");
            return;
        }
        prepare_cpu_write_emulator(emu, /*turbo=*/false);
        emu.port().out(0x243B, 0x4A);
        auto r = emu.cpu().get_registers();
        r.BC = 0x253B;
        r.AF = static_cast<uint16_t>(0x3C00 | (r.AF & 0x00FF));
        emu.cpu().set_registers(r);

        const int park_vc = park_at_end_of_line(emu, 32);
        uint8_t before = 0xFF, after = 0xFF;
        int row_before = -1;
        if (park_vc >= 0) {
            uint16_t at = emu.cpu().get_registers().PC;
            emu.mmu().write(at, 0x23);          // INC HL — the 48-cycle shim
            emu.debugger_step();
            emu.mmu().write(at, 0x00);
            at = emu.cpu().get_registers().PC;
            emu.mmu().write(at + 0, 0xED);      // OUT (C),A  with BC = 0x253B
            emu.mmu().write(at + 1, 0x79);
            emu.debugger_step();
            emu.mmu().write(at + 0, 0x00);
            emu.mmu().write(at + 1, 0x00);
            finish_frame_by_stepping(emu, park_vc);
            row_before = park_vc - emu.video_timing().vblank_top();
            before = emu.renderer().fallback_for_line(row_before);
            after  = emu.renderer().fallback_for_line(row_before + 1);
        }
        check("GH272-ROWATOM-03",
              "a deferred CPU NextREG write whose commit edge precedes the "
              "row boundary stays in the row that is ENDING, although the "
              "instruction that issued it runs on past the boundary: "
              "splitting the instruction's window at the boundary must "
              "carry the writes that precede it across, not defer the whole "
              "queue past it "
              "[zxnext.vhd:4739-4777 cpu_req edge-detect + next-edge commit]",
              park_vc >= 0 && before == 0x3C && after == 0x3C,
              "park_vc=" + std::to_string(park_vc) +
              " row " + std::to_string(row_before) + "=" + hex2(before) +
              " (want 0x3C) row " + std::to_string(row_before + 1) + "=" +
              hex2(after) + " (want 0x3C)");
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

    test_gh181_frame_boundary_carry();
    std::printf("  Group: GH181-FrameCarry — done\n");

    test_gh270_write_hpos();
    std::printf("  Group: GH270-Hpos — done\n");

    test_gh270_tagged_handlers();
    std::printf("  Group: GH270-Handlers — done\n");

    test_gh272_row_boundary_atomic();
    test_gh272_cpu_write_past_boundary();
    std::printf("  Group: GH272-RowBoundary — done\n");

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
