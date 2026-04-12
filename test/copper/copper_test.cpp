// Copper Coprocessor Compliance Test Runner
//
// Tests the Copper subsystem against VHDL-derived expected behaviour.
// All expected values come from the COPPER-TEST-PLAN-DESIGN.md spec.
//
// Run: ./build/test/copper_test

#include "peripheral/copper.h"
#include "port/nextreg.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

// ── Test infrastructure ───────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static std::string g_group;

struct TestResult {
    std::string group;
    std::string id;
    std::string description;
    bool passed;
    std::string detail;  // failure detail
};

static std::vector<TestResult> g_results;

static void set_group(const char* name) {
    g_group = name;
}

static void check(const char* id, const char* desc, bool cond, const char* detail = "") {
    g_total++;
    TestResult r;
    r.group = g_group;
    r.id = id;
    r.description = desc;
    r.passed = cond;
    r.detail = detail;
    g_results.push_back(r);

    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL %s: %s", id, desc);
        if (detail[0]) printf(" [%s]", detail);
        printf("\n");
    }
}

// Printf into static buffer for failure details
static char g_buf[512];
#define DETAIL(...) (snprintf(g_buf, sizeof(g_buf), __VA_ARGS__), g_buf)

// ── Helper: program a MOVE instruction into copper RAM via NR 0x63 ──

static void program_move(Copper& cu, uint16_t word_addr, uint8_t reg, uint8_t val) {
    // Set write address: word_addr -> byte_addr = word_addr * 2
    uint16_t byte_addr = word_addr * 2;
    cu.write_reg_0x61(byte_addr & 0xFF);
    cu.write_reg_0x62((cu.read_reg_0x62() & 0xC0) | ((byte_addr >> 8) & 0x07));
    // MOVE encoding: bit15=0, bits[14:8]=reg, bits[7:0]=val
    uint16_t instr = (static_cast<uint16_t>(reg & 0x7F) << 8) | val;
    cu.write_reg_0x63(instr >> 8);   // MSB (even byte -> stored)
    cu.write_reg_0x63(instr & 0xFF); // LSB (odd byte -> commit)
}

// Helper: program a WAIT instruction via NR 0x63
static void program_wait(Copper& cu, uint16_t word_addr, uint8_t hpos, uint16_t vpos) {
    uint16_t byte_addr = word_addr * 2;
    cu.write_reg_0x61(byte_addr & 0xFF);
    cu.write_reg_0x62((cu.read_reg_0x62() & 0xC0) | ((byte_addr >> 8) & 0x07));
    // WAIT encoding: bit15=1, bits[14:9]=hpos, bits[8:0]=vpos
    uint16_t instr = 0x8000 | ((hpos & 0x3F) << 9) | (vpos & 0x1FF);
    cu.write_reg_0x63(instr >> 8);
    cu.write_reg_0x63(instr & 0xFF);
}

// Helper: set copper mode without changing write address
static void set_mode(Copper& cu, uint8_t mode) {
    uint8_t current = cu.read_reg_0x62();
    cu.write_reg_0x62((mode << 6) | (current & 0x07));
}

// Helper: fresh copper + nextreg
static void fresh(Copper& cu, NextReg& nr) {
    cu.reset();
    nr.reset();
}

// ── Group 1: Instruction RAM Access ───────────────────────────────────

static void test_group1_ram_access() {
    set_group("RAM Access");
    Copper cu;
    NextReg nr;

    // RAM-01: NR 0x60 sequential write
    {
        fresh(cu, nr);
        cu.write_reg_0x61(0x00);
        cu.write_reg_0x62(0x00); // mode=stop, addr high=0
        // Write MSB then LSB via NR 0x60 for word address 0
        cu.write_reg_0x60(0x40); // even byte -> MSB
        cu.write_reg_0x60(0x55); // odd byte -> LSB
        uint16_t instr = cu.instruction(0);
        check("RAM-01", "NR 0x60 sequential write",
              instr == 0x4055,
              DETAIL("expected=0x4055 got=0x%04x", instr));
    }

    // RAM-02: NR 0x63 sequential write
    {
        fresh(cu, nr);
        cu.write_reg_0x61(0x00);
        cu.write_reg_0x62(0x00);
        cu.write_reg_0x63(0x40); // even byte -> stored
        cu.write_reg_0x63(0x55); // odd byte -> commit
        uint16_t instr = cu.instruction(0);
        check("RAM-02", "NR 0x63 sequential write",
              instr == 0x4055,
              DETAIL("expected=0x4055 got=0x%04x", instr));
    }

    // RAM-03: NR 0x61 address set low
    {
        fresh(cu, nr);
        cu.write_reg_0x62(0x03); // set addr high bits to 3
        cu.write_reg_0x61(0xAB); // set low byte
        uint8_t low = cu.read_reg_0x61();
        uint8_t high = cu.read_reg_0x62();
        check("RAM-03", "NR 0x61 address set low",
              low == 0xAB && (high & 0x07) == 0x03,
              DETAIL("low=%02x(exp AB) high_bits=%d(exp 3)", low, high & 0x07));
    }

    // RAM-04: NR 0x62 address set high + mode
    {
        fresh(cu, nr);
        cu.write_reg_0x62(0xC5); // mode=11, addr high=5
        uint8_t val = cu.read_reg_0x62();
        check("RAM-04", "NR 0x62 address set high + mode",
              val == 0xC5,
              DETAIL("expected=0xC5 got=0x%02x", val));
    }

    // RAM-05: Address auto-increment
    {
        fresh(cu, nr);
        cu.write_reg_0x61(0x00);
        cu.write_reg_0x62(0x00);
        // Write 4 bytes via NR 0x60 -> addr should go 0,1,2,3 -> 4
        cu.write_reg_0x60(0x11);
        cu.write_reg_0x60(0x22);
        cu.write_reg_0x60(0x33);
        cu.write_reg_0x60(0x44);
        uint8_t low = cu.read_reg_0x61();
        check("RAM-05", "Address auto-increment",
              low == 0x04,
              DETAIL("expected addr=4 got=%d", low));
    }

    // RAM-06: Address wrap-around
    {
        fresh(cu, nr);
        // Set addr to 0x7FF (max 11-bit byte addr)
        cu.write_reg_0x61(0xFF);
        cu.write_reg_0x62(0x07); // mode=00, high=7 -> addr = 0x7FF
        // Write 2 bytes -> should wrap from 0x7FF to 0x000
        cu.write_reg_0x60(0xAA);
        cu.write_reg_0x60(0xBB);
        uint8_t low = cu.read_reg_0x61();
        uint8_t high_bits = cu.read_reg_0x62() & 0x07;
        uint16_t addr = (high_bits << 8) | low;
        check("RAM-06", "Address wrap-around",
              addr == 0x001,
              DETAIL("expected addr=0x001 got=0x%03x", addr));
    }

    // RAM-07: Mixed 8/16-bit writes
    {
        fresh(cu, nr);
        cu.write_reg_0x61(0x00);
        cu.write_reg_0x62(0x00);
        // Write word 0 via NR 0x60 (8-bit mode)
        cu.write_reg_0x60(0x40); // MSB
        cu.write_reg_0x60(0x55); // LSB
        // Write word 1 via NR 0x63 (16-bit mode)
        cu.write_reg_0x63(0x41); // stored
        cu.write_reg_0x63(0x66); // commit
        uint16_t w0 = cu.instruction(0);
        uint16_t w1 = cu.instruction(1);
        check("RAM-07", "Mixed 8/16-bit writes",
              w0 == 0x4055 && w1 == 0x4166,
              DETAIL("w0=0x%04x(exp 0x4055) w1=0x%04x(exp 0x4166)", w0, w1));
    }

    // RAM-08: Read-back NR 0x61
    {
        fresh(cu, nr);
        cu.write_reg_0x61(0x5A);
        check("RAM-08", "Read-back NR 0x61",
              cu.read_reg_0x61() == 0x5A,
              DETAIL("got=0x%02x", cu.read_reg_0x61()));
    }

    // RAM-09: Read-back NR 0x62
    {
        fresh(cu, nr);
        cu.write_reg_0x62(0x85); // mode=10, high=5
        uint8_t v = cu.read_reg_0x62();
        check("RAM-09", "Read-back NR 0x62",
              v == 0x85,
              DETAIL("expected=0x85 got=0x%02x", v));
    }

    // RAM-10: Full RAM fill
    {
        fresh(cu, nr);
        cu.write_reg_0x61(0x00);
        cu.write_reg_0x62(0x00);
        // Fill all 1024 instructions via NR 0x63
        for (int i = 0; i < 1024; i++) {
            uint16_t val = i & 0xFFFF;
            cu.write_reg_0x63((val >> 8) & 0xFF);
            cu.write_reg_0x63(val & 0xFF);
        }
        // Check last entry (word 1023)
        uint16_t last = cu.instruction(1023);
        check("RAM-10", "Full RAM fill",
              last == 1023,
              DETAIL("expected=0x%04x got=0x%04x", 1023, last));
    }
}

// ── Group 2: MOVE Instruction ──────────────────────────────────────────

static void test_group2_move() {
    set_group("MOVE");
    Copper cu;
    NextReg nr;

    // MOV-01: Basic MOVE
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 1); // run mode
        // Execute: first call detects mode change (no exec), second executes MOVE
        cu.execute(0, 0, nr);
        cu.execute(1, 0, nr);
        check("MOV-01", "Basic MOVE",
              nr.read(0x40) == 0x55,
              DETAIL("nr[0x40]=%02x expected=0x55", nr.read(0x40)));
    }

    // MOV-02: MOVE register range
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x01, 0xAA);
        program_move(cu, 1, 0x3F, 0xBB);
        program_move(cu, 2, 0x7F, 0xCC);
        set_mode(cu, 1);
        cu.execute(0, 0, nr); // mode change
        cu.execute(1, 0, nr); // MOVE reg 0x01
        cu.execute(2, 0, nr); // move_pending clear
        cu.execute(3, 0, nr); // MOVE reg 0x3F
        cu.execute(4, 0, nr); // move_pending clear
        cu.execute(5, 0, nr); // MOVE reg 0x7F
        bool ok = (nr.read(0x01) == 0xAA) &&
                  (nr.read(0x3F) == 0xBB) &&
                  (nr.read(0x7F) == 0xCC);
        check("MOV-02", "MOVE register range", ok,
              DETAIL("r01=%02x r3f=%02x r7f=%02x",
                     nr.read(0x01), nr.read(0x3F), nr.read(0x7F)));
    }

    // MOV-03: MOVE data values
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x10, 0x00);
        program_move(cu, 1, 0x11, 0xFF);
        program_move(cu, 2, 0x12, 0xA5);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);
        cu.execute(1, 0, nr); // MOVE 0x10=0x00
        cu.execute(2, 0, nr); // pending
        cu.execute(3, 0, nr); // MOVE 0x11=0xFF
        cu.execute(4, 0, nr); // pending
        cu.execute(5, 0, nr); // MOVE 0x12=0xA5
        bool ok = (nr.read(0x10) == 0x00) &&
                  (nr.read(0x11) == 0xFF) &&
                  (nr.read(0x12) == 0xA5);
        check("MOV-03", "MOVE data values", ok,
              DETAIL("r10=%02x r11=%02x r12=%02x",
                     nr.read(0x10), nr.read(0x11), nr.read(0x12)));
    }

    // MOV-04: MOVE NOP (reg=0)
    {
        fresh(cu, nr);
        // NOP: MOVE with reg=0 -> no write, but PC advances
        program_move(cu, 0, 0x00, 0x55); // NOP
        program_move(cu, 1, 0x40, 0xAA); // real MOVE
        set_mode(cu, 1);
        cu.execute(0, 0, nr); // mode change
        cu.execute(1, 0, nr); // NOP: no write, PC advances
        // After NOP, PC should be at 1 (NOP does not set move_pending in VHDL)
        cu.execute(2, 0, nr); // should execute MOVE at addr 1
        bool ok = (nr.read(0x40) == 0xAA);
        check("MOV-04", "MOVE NOP (reg=0)", ok,
              DETAIL("nr[0x40]=%02x expected=0xAA, pc=%d",
                     nr.read(0x40), cu.pc()));
    }

    // MOV-05: MOVE write pulse duration (move_pending lasts 1 cycle)
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        // Put a HALT (WAIT vpos=511) at addr 1 so copper stalls after MOVE
        program_wait(cu, 1, 0, 511);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE executes
        bool pending_after = cu.pc() == 1; // PC advanced past MOVE
        cu.execute(2, 0, nr);  // pending clears
        // After clearing, copper should try to execute addr 1 (the HALT/WAIT)
        check("MOV-05", "MOVE write pulse duration (1 cycle)",
              pending_after && nr.read(0x40) == 0x55,
              DETAIL("pc_after_move=%d nr40=%02x", cu.pc(), nr.read(0x40)));
    }

    // MOV-06: Consecutive MOVEs
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x11);
        program_move(cu, 1, 0x41, 0x22);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE 0x40=0x11
        cu.execute(2, 0, nr);  // pending clear
        cu.execute(3, 0, nr);  // MOVE 0x41=0x22
        bool ok = (nr.read(0x40) == 0x11) && (nr.read(0x41) == 0x22);
        check("MOV-06", "Consecutive MOVEs", ok,
              DETAIL("r40=%02x r41=%02x", nr.read(0x40), nr.read(0x41)));
    }

    // MOV-07: MOVE timing (executes immediately, no wait)
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        // On the very next execute call, the MOVE should fire
        cu.execute(1, 0, nr);
        check("MOV-07", "MOVE timing (immediate execution)",
              nr.read(0x40) == 0x55,
              DETAIL("nr[0x40]=%02x", nr.read(0x40)));
    }

    // MOV-08: MOVE output format (register 7-bit, data 8-bit)
    {
        fresh(cu, nr);
        // MOVE reg=0x7F, data=0xA5 -> instruction = 0x7FA5
        program_move(cu, 0, 0x7F, 0xA5);
        uint16_t instr = cu.instruction(0);
        check("MOV-08", "MOVE output format",
              instr == 0x7FA5,
              DETAIL("expected=0x7FA5 got=0x%04x", instr));
    }
}

// ── Group 4: Control Modes ─────────────────────────────────────────────

static void test_group4_control() {
    set_group("Control Modes");
    Copper cu;
    NextReg nr;

    // CTL-01: Mode 00 (Stop) — no instructions execute
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 0); // stop
        for (int i = 0; i < 10; i++)
            cu.execute(i, 0, nr);
        check("CTL-01", "Mode 00 (Stop)",
              nr.read(0x40) == 0x00 && cu.pc() == 0,
              DETAIL("nr40=%02x pc=%d", nr.read(0x40), cu.pc()));
    }

    // CTL-02: Mode 01 (Run) — execution starts from address 0
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 1); // run
        cu.execute(0, 0, nr); // mode change
        cu.execute(1, 0, nr); // MOVE
        check("CTL-02", "Mode 01 (Run)",
              nr.read(0x40) == 0x55,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // CTL-03: Mode 11 (Loop) — execution starts from address 0
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 3); // loop
        cu.execute(0, 0, nr); // mode change
        cu.execute(1, 0, nr); // MOVE
        check("CTL-03", "Mode 11 (Loop)",
              nr.read(0x40) == 0x55,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // CTL-04: Mode 01 address reset
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        program_move(cu, 1, 0x41, 0x66);
        set_mode(cu, 1);
        cu.execute(0, 0, nr); // mode change, PC->0
        cu.execute(1, 0, nr); // MOVE addr 0
        cu.execute(2, 0, nr); // pending
        // Now PC is at 1. Switch to stop then back to run.
        set_mode(cu, 0);
        cu.execute(3, 0, nr); // mode change to stop
        set_mode(cu, 1);
        cu.execute(4, 0, nr); // mode change to run, PC resets to 0
        check("CTL-04", "Mode 01 address reset",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // CTL-05: Mode 11 address reset
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 0);
        cu.execute(0, 0, nr);
        set_mode(cu, 3); // switch to loop
        cu.execute(1, 0, nr); // mode change, PC->0
        check("CTL-05", "Mode 11 address reset",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // CTL-06: Mode 10 behaviour (copper runs, no loop restart)
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 2); // mode 10
        cu.execute(0, 0, nr); // mode change
        cu.execute(1, 0, nr); // should execute MOVE (mode != 00)
        check("CTL-06", "Mode 10 behaviour (runs)",
              nr.read(0x40) == 0x55,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // CTL-07: Loop restart at frame (mode=11, vc=0, hc=0 resets PC)
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        program_wait(cu, 1, 0, 511); // HALT
        set_mode(cu, 3);
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE
        cu.execute(2, 0, nr);  // pending
        cu.execute(3, 0, nr);  // WAIT(511) -> stall
        // Now simulate frame start
        cu.execute(0, 0, nr);  // vc=0, hc=0 -> loop restart, PC=0
        check("CTL-07", "Loop restart at frame",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // CTL-08: Loop restart re-executes instructions
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x00); // will be overwritten
        program_wait(cu, 1, 0, 511);     // HALT
        set_mode(cu, 3);
        cu.execute(0, 0, nr);  // mode change
        // Change the value at addr 0 after first frame
        program_move(cu, 0, 0x40, 0xBB);
        // Restore write addr (program_move changes it)
        cu.execute(0, 0, nr);  // frame restart, PC=0
        cu.execute(1, 0, nr);  // MOVE 0x40=0xBB
        check("CTL-08", "Loop restart re-executes",
              nr.read(0x40) == 0xBB,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // CTL-09: Run does not loop
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        program_wait(cu, 1, 0, 511); // HALT
        set_mode(cu, 1); // run
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE
        cu.execute(2, 0, nr);  // pending
        cu.execute(3, 10, nr); // WAIT stall
        uint16_t pc_before = cu.pc();
        // Simulate frame boundary with on_vsync
        cu.on_vsync();
        cu.execute(0, 0, nr);  // hc=0, vc=0 — should NOT restart in mode 01
        // PC should still be at the WAIT
        check("CTL-09", "Run does not loop",
              cu.pc() == pc_before,
              DETAIL("pc=%d expected=%d", cu.pc(), pc_before));
    }

    // CTL-10: Mode change mid-execution resets address
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x11);
        program_move(cu, 1, 0x41, 0x22);
        program_move(cu, 2, 0x42, 0x33);
        set_mode(cu, 1); // run
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE addr 0
        cu.execute(2, 0, nr);  // pending
        cu.execute(3, 0, nr);  // MOVE addr 1
        // Now switch to loop mid-execution
        set_mode(cu, 3);
        cu.execute(4, 0, nr);  // mode change 01->11, PC->0
        check("CTL-10", "Mode change mid-execution resets address",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // CTL-11: Stop while running
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        program_move(cu, 1, 0x41, 0x66);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE addr 0
        set_mode(cu, 0);       // stop
        cu.execute(2, 0, nr);  // mode change to stop
        cu.execute(3, 0, nr);  // should not execute
        check("CTL-11", "Stop while running",
              nr.read(0x41) == 0x00,
              DETAIL("nr41=%02x (should be 0)", nr.read(0x41)));
    }

    // CTL-12: Same mode no reset
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        program_move(cu, 1, 0x41, 0x66);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE addr 0, PC->1
        cu.execute(2, 0, nr);  // pending
        uint16_t pc_before = cu.pc();
        // Write same mode again
        set_mode(cu, 1);
        cu.execute(3, 0, nr);
        // PC should NOT have reset (same mode, no edge)
        check("CTL-12", "Same mode no reset",
              cu.pc() >= pc_before,
              DETAIL("pc=%d pc_before=%d", cu.pc(), pc_before));
    }

    // CTL-13: Mode change clears dout (move_pending)
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        cu.execute(1, 0, nr);  // MOVE, move_pending=true
        // Now change mode while pending
        set_mode(cu, 3);
        cu.execute(2, 0, nr);  // mode change clears pending, PC=0
        // Next execute should try addr 0, not be stuck in pending
        cu.execute(3, 0, nr);  // should execute MOVE again
        check("CTL-13", "Mode change clears dout",
              cu.pc() == 1, // PC advanced past addr 0
              DETAIL("pc=%d", cu.pc()));
    }
}

// ── Group 8: Reset Behaviour ───────────────────────────────────────────

static void test_group8_reset() {
    set_group("Reset");
    Copper cu;
    NextReg nr;

    // RST-01: Hard reset state — address=0, dout=0
    {
        cu.reset();
        check("RST-01", "Hard reset state (pc=0)",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // RST-02: Mode reset
    {
        cu.reset();
        check("RST-02", "Mode reset (mode=00)",
              cu.mode() == 0,
              DETAIL("mode=%d", cu.mode()));
    }

    // RST-03: Address reset (write address)
    {
        cu.reset();
        uint8_t low = cu.read_reg_0x61();
        uint8_t high = cu.read_reg_0x62();
        check("RST-03", "Address reset (write_addr=0)",
              low == 0x00 && (high & 0x07) == 0x00,
              DETAIL("low=%02x high_bits=%d", low, high & 0x07));
    }

    // RST-04: Offset reset
    // NR 0x64 is managed by NextReg, not by Copper directly.
    // After nextreg.reset(), cached register 0x64 should be 0.
    {
        nr.reset();
        check("RST-04", "Offset reset (NR 0x64=0)",
              nr.read(0x64) == 0x00,
              DETAIL("nr64=%02x", nr.read(0x64)));
    }

    // RST-05: Stored data reset
    // After reset, write_data_stored_ should be 0.
    // Test indirectly: write one byte via 0x63 (stored), reset, then
    // write a second byte — the committed word should use stored=0.
    {
        cu.reset();
        cu.write_reg_0x61(0x00);
        cu.write_reg_0x62(0x00);
        // Write even byte via 0x63 (stores it)
        cu.write_reg_0x63(0xAA);
        // Reset
        cu.reset();
        cu.write_reg_0x61(0x01); // byte addr 1 (odd)
        cu.write_reg_0x62(0x00);
        cu.write_reg_0x63(0xBB); // odd byte -> commit with stored data
        uint16_t instr = cu.instruction(0);
        // After reset, stored data should be 0x00, so word = 0x00BB
        check("RST-05", "Stored data reset",
              instr == 0x00BB,
              DETAIL("expected=0x00BB got=0x%04x", instr));
    }
}

// ── Group 3: WAIT Instruction ──────────────────────────────────────────

static void test_group3_wait() {
    set_group("WAIT");
    Copper cu;
    NextReg nr;

    // WAI-01: WAIT exact match
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 100); // WAIT vpos=100, hpos=0 -> hc >= 12
        program_move(cu, 1, 0x40, 0x55);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(12, 100, nr);  // WAIT matches (vc=100, hc>=12)
        cu.execute(13, 100, nr);  // MOVE executes
        check("WAI-01", "WAIT exact match",
              nr.read(0x40) == 0x55,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // WAI-02: WAIT hpos threshold
    {
        fresh(cu, nr);
        program_wait(cu, 0, 10, 50); // hpos=10 -> threshold = 10*8+12 = 92
        program_move(cu, 1, 0x40, 0xAA);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(91, 50, nr);   // hc=91 < 92: stall
        bool stalled = (cu.pc() == 0);
        cu.execute(92, 50, nr);   // hc=92 >= 92: match
        cu.execute(93, 50, nr);   // MOVE
        check("WAI-02", "WAIT hpos threshold",
              stalled && nr.read(0x40) == 0xAA,
              DETAIL("stalled=%d nr40=%02x", stalled, nr.read(0x40)));
    }

    // WAI-03: WAIT hpos maximum
    {
        fresh(cu, nr);
        program_wait(cu, 0, 63, 50); // hpos=63 -> threshold = 63*8+12 = 516
        program_move(cu, 1, 0x40, 0xBB);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);
        cu.execute(515, 50, nr);  // just below threshold
        bool stalled = (cu.pc() == 0);
        cu.execute(516, 50, nr);  // match
        cu.execute(517, 50, nr);  // MOVE
        check("WAI-03", "WAIT hpos maximum",
              stalled && nr.read(0x40) == 0xBB,
              DETAIL("stalled=%d nr40=%02x", stalled, nr.read(0x40)));
    }

    // WAI-04: WAIT vpos only (hpos=0)
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 75); // hpos=0 -> threshold = 12
        program_move(cu, 1, 0x40, 0xCC);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(12, 75, nr);   // vc=75, hc=12 -> match
        cu.execute(13, 75, nr);   // MOVE
        check("WAI-04", "WAIT vpos only (hpos=0)",
              nr.read(0x40) == 0xCC,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // WAI-05: WAIT no advance before match
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 50);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(100, 49, nr);  // wrong line
        cu.execute(100, 51, nr);  // wrong line
        check("WAI-05", "WAIT no advance before match",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // WAI-06: WAIT vcount must equal (not >=)
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 100);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);      // mode change
        cu.execute(100, 101, nr);   // vc=101, vpos=100: should NOT match
        check("WAI-06", "WAIT vcount must equal",
              cu.pc() == 0,
              DETAIL("pc=%d (should be 0)", cu.pc()));
    }

    // WAI-07: WAIT hcount >= check
    {
        fresh(cu, nr);
        program_wait(cu, 0, 5, 80); // threshold = 5*8+12 = 52
        program_move(cu, 1, 0x40, 0xDD);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(51, 80, nr);   // hc=51 < 52: stall
        bool stalled = (cu.pc() == 0);
        cu.execute(52, 80, nr);   // hc=52 == 52: match (>=)
        bool matched_exact = (cu.pc() == 1);
        check("WAI-07", "WAIT hcount >= check",
              stalled && matched_exact,
              DETAIL("stalled=%d matched=%d pc=%d", stalled, matched_exact, cu.pc()));
    }

    // WAI-08: WAIT then MOVE
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 60);
        program_move(cu, 1, 0x40, 0xEE);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(100, 60, nr);  // WAIT matches
        cu.execute(101, 60, nr);  // MOVE executes
        check("WAI-08", "WAIT then MOVE",
              nr.read(0x40) == 0xEE,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // WAI-09: WAIT for line 0
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 0); // vpos=0, hpos=0 -> threshold=12
        program_move(cu, 1, 0x40, 0xFF);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);   // mode change
        cu.execute(12, 0, nr);  // WAIT matches at vc=0, hc>=12
        cu.execute(13, 0, nr);  // MOVE
        check("WAI-09", "WAIT for line 0",
              nr.read(0x40) == 0xFF,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // WAI-10: WAIT max vpos (511)
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 511); // HALT — never matches
        set_mode(cu, 1);
        cu.execute(0, 0, nr);
        for (int i = 0; i < 20; i++)
            cu.execute(100, 300, nr);
        check("WAI-10", "WAIT max vpos (511) - HALT",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // WAI-11: Multiple WAITs
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 50);
        program_move(cu, 1, 0x40, 0x11);
        program_wait(cu, 2, 0, 100);
        program_move(cu, 3, 0x41, 0x22);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);      // mode change
        cu.execute(100, 50, nr);   // WAIT for line 50 matches
        cu.execute(101, 50, nr);   // MOVE 0x40=0x11
        cu.execute(102, 50, nr);   // pending
        cu.execute(103, 50, nr);   // WAIT for line 100 — stall (vc=50)
        cu.execute(100, 100, nr);  // WAIT for line 100 matches
        cu.execute(101, 100, nr);  // MOVE 0x41=0x22
        bool ok = (nr.read(0x40) == 0x11) && (nr.read(0x41) == 0x22);
        check("WAI-11", "Multiple WAITs", ok,
              DETAIL("r40=%02x r41=%02x", nr.read(0x40), nr.read(0x41)));
    }

    // WAI-12: WAIT past end of line (hpos produces threshold > typical hc max)
    {
        fresh(cu, nr);
        // hpos=63 -> threshold=516. If max hc per line is say 455 (for standard timing),
        // the WAIT should still match if hc >= 516 on the right line.
        program_wait(cu, 0, 63, 50);
        program_move(cu, 1, 0x40, 0xAB);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);
        cu.execute(455, 50, nr);  // below threshold
        bool stalled = (cu.pc() == 0);
        // With hc=516 on the right line, it should match
        cu.execute(516, 50, nr);
        bool advanced = (cu.pc() == 1);
        check("WAI-12", "WAIT past end of line",
              stalled && advanced,
              DETAIL("stalled=%d advanced=%d", stalled, advanced));
    }
}

// ── Group 6: Vertical Offset ───────────────────────────────────────────

static void test_group6_offset() {
    set_group("Vertical Offset");
    NextReg nr;

    // OFS-01: Zero offset (NR 0x64=0)
    {
        nr.reset();
        nr.write(0x64, 0x00);
        check("OFS-01", "Zero offset",
              nr.read(0x64) == 0x00,
              DETAIL("nr64=%02x", nr.read(0x64)));
    }

    // OFS-02: Non-zero offset
    {
        nr.reset();
        nr.write(0x64, 32);
        check("OFS-02", "Non-zero offset",
              nr.read(0x64) == 32,
              DETAIL("nr64=%d", nr.read(0x64)));
    }

    // OFS-03: WAIT with offset
    // Note: The copper vertical offset shifts cvc. This test verifies the
    // NextReg storage; actual cvc shifting requires integration with the
    // video timing subsystem which isn't available in unit tests.
    {
        nr.reset();
        nr.write(0x64, 10);
        check("OFS-03", "WAIT with offset (NR storage)",
              nr.read(0x64) == 10,
              DETAIL("nr64=%d", nr.read(0x64)));
    }

    // OFS-04: Offset read-back
    {
        nr.reset();
        nr.write(0x64, 0x80);
        check("OFS-04", "Offset read-back",
              nr.read(0x64) == 0x80,
              DETAIL("nr64=%02x", nr.read(0x64)));
    }

    // OFS-05: Offset reset
    {
        nr.reset();
        nr.write(0x64, 0xFF);
        nr.reset();
        check("OFS-05", "Offset reset",
              nr.read(0x64) == 0x00,
              DETAIL("nr64=%02x", nr.read(0x64)));
    }

    // OFS-06: CVC wraps at max_vc
    // This requires integration with video timing, so we just verify the register works
    {
        nr.reset();
        nr.write(0x64, 0xFF);
        check("OFS-06", "CVC max offset value",
              nr.read(0x64) == 0xFF,
              DETAIL("nr64=%02x", nr.read(0x64)));
    }
}

// ── Group 5: Timing Accuracy ───────────────────────────────────────────

static void test_group5_timing() {
    set_group("Timing");
    Copper cu;
    NextReg nr;

    // TIM-01: MOVE takes 2 cycles
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        program_move(cu, 1, 0x41, 0x66);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // cycle 0: mode change
        cu.execute(1, 0, nr);  // cycle 1: MOVE fires, move_pending=true
        bool has_write = (nr.read(0x40) == 0x55);
        uint16_t pc_after_move = cu.pc();
        cu.execute(2, 0, nr);  // cycle 2: pending clears
        cu.execute(3, 0, nr);  // cycle 3: second MOVE fires
        bool ok = has_write && pc_after_move == 1 && nr.read(0x41) == 0x66;
        check("TIM-01", "MOVE takes 2 cycles",
              ok,
              DETAIL("write=%d pc=%d r41=%02x", has_write, pc_after_move, nr.read(0x41)));
    }

    // TIM-02: WAIT no-op cycle
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 200);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        uint16_t pc0 = cu.pc();
        cu.execute(100, 100, nr);  // WAIT doesn't match (vc=100 != 200)
        check("TIM-02", "WAIT no-op cycle",
              cu.pc() == pc0,
              DETAIL("pc=%d expected=%d", cu.pc(), pc0));
    }

    // TIM-03: Copper runs at 28 MHz (one decision per execute call)
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change — one decision
        cu.execute(1, 0, nr);  // MOVE — one decision
        check("TIM-03", "One decision per execute call",
              nr.read(0x40) == 0x55 && cu.pc() == 1,
              DETAIL("nr40=%02x pc=%d", nr.read(0x40), cu.pc()));
    }

    // TIM-04: WAIT + MOVE pipeline
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 50);
        program_move(cu, 1, 0x40, 0xAA);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(50, 50, nr);   // WAIT matches, PC advances
        cu.execute(51, 50, nr);   // MOVE executes on next cycle
        check("TIM-04", "WAIT + MOVE pipeline",
              nr.read(0x40) == 0xAA,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // TIM-05: Dual-port read timing (instruction available when needed)
    // In single-clock emulation, this should just work
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);
        cu.execute(1, 0, nr);
        check("TIM-05", "Dual-port read timing",
              nr.read(0x40) == 0x55,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }

    // TIM-06: Instruction throughput (10 MOVEs in 20 cycles + 1 mode change)
    {
        fresh(cu, nr);
        for (int i = 0; i < 10; i++)
            program_move(cu, i, 0x40 + i, i + 1);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        // 10 MOVEs * 2 cycles each = 20 cycles
        for (int c = 1; c <= 20; c++)
            cu.execute(c, 0, nr);
        bool ok = true;
        for (int i = 0; i < 10; i++) {
            if (nr.read(0x40 + i) != (i + 1)) { ok = false; break; }
        }
        check("TIM-06", "Instruction throughput (10 MOVEs)",
              ok,
              DETAIL("pc=%d", cu.pc()));
    }

    // TIM-07: WAIT granularity (8-pixel steps)
    {
        fresh(cu, nr);
        // hpos=1 -> threshold = 1*8+12 = 20
        // hpos=2 -> threshold = 2*8+12 = 28
        program_wait(cu, 0, 1, 50);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(19, 50, nr);   // hc=19 < 20: stall
        bool stall_19 = (cu.pc() == 0);
        cu.execute(20, 50, nr);   // hc=20 >= 20: match
        bool match_20 = (cu.pc() == 1);
        check("TIM-07", "WAIT granularity (8-pixel steps)",
              stall_19 && match_20,
              DETAIL("stall@19=%d match@20=%d", stall_19, match_20));
    }
}

// ── Group 9: Edge Cases ────────────────────────────────────────────────

static void test_group9_edge() {
    set_group("Edge Cases");
    Copper cu;
    NextReg nr;

    // EDG-01: End of instruction RAM (PC wraps at 1024)
    {
        fresh(cu, nr);
        // Program a MOVE at addr 1023
        program_move(cu, 1023, 0x40, 0x55);
        // Set PC to 1023 by executing 1023 NOPs? Too slow.
        // Instead: program NOPs from 0, set mode, and manually advance.
        // Simpler: program a MOVE at 1023, fill 0-1022 with NOPs.
        // Even simpler: after reset, all instructions are 0 (NOP/MOVE reg=0).
        // So just program addr 1023 and let copper run through 1023 NOPs.
        set_mode(cu, 1);
        cu.execute(0, 0, nr);  // mode change
        // NOPs don't set move_pending (reg=0), so each takes 1 cycle
        for (int i = 0; i < 1023; i++)
            cu.execute(i + 1, 0, nr);
        // Now at addr 1023
        cu.execute(1024, 0, nr); // MOVE at 1023
        cu.execute(1025, 0, nr); // pending
        // PC should wrap to 0
        check("EDG-01", "End of instruction RAM (PC wraps)",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // EDG-02: WAIT for already-passed line (stalls)
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 10); // wait for line 10
        set_mode(cu, 1);
        cu.execute(0, 0, nr);     // mode change
        cu.execute(100, 50, nr);  // vc=50, vpos=10: doesn't match
        cu.execute(100, 100, nr); // vc=100: doesn't match
        check("EDG-02", "WAIT for already-passed line",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // EDG-05: All-WAIT program (stalls at first non-matching)
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 200);
        program_wait(cu, 1, 0, 201);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);
        for (int i = 0; i < 10; i++)
            cu.execute(100, 100, nr); // vc=100, never matches vpos=200
        check("EDG-05", "All-WAIT program stalls",
              cu.pc() == 0,
              DETAIL("pc=%d", cu.pc()));
    }

    // EDG-06: All-NOP program (all execute, no writes)
    {
        fresh(cu, nr);
        // After reset, all 1024 words are 0 = NOP (MOVE reg=0, val=0)
        set_mode(cu, 1);
        cu.execute(0, 0, nr); // mode change
        // Run 1024 NOPs
        for (int i = 0; i < 1024; i++)
            cu.execute(i + 1, 0, nr);
        // PC should wrap to 0 and no writes should have occurred
        // Verify a register that was never touched
        check("EDG-06", "All-NOP program (no writes)",
              nr.read(0x40) == 0x00 && cu.pc() == 0,
              DETAIL("nr40=%02x pc=%d", nr.read(0x40), cu.pc()));
    }

    // EDG-07: WAIT hpos=0 matches at hcount=12
    {
        fresh(cu, nr);
        program_wait(cu, 0, 0, 50);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);    // mode change
        cu.execute(11, 50, nr);  // hc=11 < 12: stall
        bool stall_11 = (cu.pc() == 0);
        cu.execute(12, 50, nr);  // hc=12 >= 12: match
        bool match_12 = (cu.pc() == 1);
        check("EDG-07", "WAIT hpos=0 matches at hcount=12",
              stall_11 && match_12,
              DETAIL("stall@11=%d match@12=%d", stall_11, match_12));
    }

    // EDG-08: Rapid mode toggling
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        // 00 -> 01 (reset PC to 0)
        set_mode(cu, 1);
        cu.execute(0, 0, nr); // mode change
        bool pc0_after_01 = (cu.pc() == 0);
        // 01 -> 00 (stop)
        set_mode(cu, 0);
        cu.execute(1, 0, nr); // mode change
        // 00 -> 11 (reset PC to 0)
        set_mode(cu, 3);
        cu.execute(2, 0, nr); // mode change
        bool pc0_after_11 = (cu.pc() == 0);
        check("EDG-08", "Rapid mode toggling",
              pc0_after_01 && pc0_after_11,
              DETAIL("pc_01=%d pc_11=%d", pc0_after_01, pc0_after_11));
    }
}

// ── Group 7: NextREG Write Arbitration ─────────────────────────────────
// Note: These tests are limited since arbitration happens at integration
// level (CPU + copper contending for nextreg bus). We test what we can.

static void test_group7_arbitration() {
    set_group("Arbitration");
    Copper cu;
    NextReg nr;

    // ARB-04: Copper register masking (7-bit max = 0x7F)
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x7F, 0xAA);
        set_mode(cu, 1);
        cu.execute(0, 0, nr);
        cu.execute(1, 0, nr);
        check("ARB-04", "Copper register masking (7-bit, max 0x7F)",
              nr.read(0x7F) == 0xAA,
              DETAIL("nr7f=%02x", nr.read(0x7F)));
    }

    // ARB-05: No copper interference when stopped
    {
        fresh(cu, nr);
        program_move(cu, 0, 0x40, 0x55);
        set_mode(cu, 0);
        for (int i = 0; i < 10; i++)
            cu.execute(i, 0, nr);
        nr.write(0x40, 0xBB);
        check("ARB-05", "No copper interference when stopped",
              nr.read(0x40) == 0xBB,
              DETAIL("nr40=%02x", nr.read(0x40)));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    printf("Copper Coprocessor Compliance Tests\n");
    printf("====================================\n\n");

    test_group8_reset();
    printf("  Group: Reset — done\n");

    test_group1_ram_access();
    printf("  Group: RAM Access — done\n");

    test_group2_move();
    printf("  Group: MOVE — done\n");

    test_group4_control();
    printf("  Group: Control Modes — done\n");

    test_group3_wait();
    printf("  Group: WAIT — done\n");

    test_group5_timing();
    printf("  Group: Timing — done\n");

    test_group6_offset();
    printf("  Group: Vertical Offset — done\n");

    test_group7_arbitration();
    printf("  Group: Arbitration — done\n");

    test_group9_edge();
    printf("  Group: Edge Cases — done\n");

    printf("\n====================================\n");
    printf("Results: %d/%d passed", g_pass, g_total);
    if (g_fail > 0)
        printf(" (%d FAILED)", g_fail);
    printf("\n");

    // Per-group summary
    printf("\nPer-group breakdown:\n");
    std::string last_group;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last_group) {
            if (!last_group.empty())
                printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);
            last_group = r.group;
            gp = gf = 0;
        }
        if (r.passed) gp++; else gf++;
    }
    if (!last_group.empty())
        printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
