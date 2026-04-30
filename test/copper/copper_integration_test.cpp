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
    cfg.roms_directory = "/usr/share/fuse";
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

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
