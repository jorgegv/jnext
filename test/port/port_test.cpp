// I/O Port Dispatch Compliance Test Runner
//
// Tests the PortDispatch subsystem against VHDL-derived expected behaviour.
// All expected values come from IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md.
//
// Run: ./build/test/port_test

#include "port/port_dispatch.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

// -- Test infrastructure (same pattern as copper_test) ---------------------

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static std::string g_group;

struct TestResult {
    std::string group;
    std::string id;
    std::string description;
    bool passed;
    std::string detail;
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

static char g_buf[512];
#define DETAIL(...) (snprintf(g_buf, sizeof(g_buf), __VA_ARGS__), g_buf)

// -- Helpers ---------------------------------------------------------------

// Tracking which handler was called, with what port/value
struct CallRecord {
    uint16_t port;
    uint8_t  value;   // for writes
    bool     called;
};

// Create a fresh PortDispatch with no default constructor handlers
static PortDispatch make_clean_dispatch() {
    PortDispatch pd;
    pd.clear_handlers();
    return pd;
}

// -- Test Groups -----------------------------------------------------------

// 1. ULA Port 0xFE
// VHDL: port_fe matches when cpu_a(0) = 0 (any even address)
static void test_ula_fe() {
    set_group("ULA 0xFE");

    // Register handler with mask=0x0001 value=0x0000 (bit 0 = 0)
    PortDispatch pd = make_clean_dispatch();
    CallRecord rd_rec{}, wr_rec{};

    pd.register_handler(0x0001, 0x0000,
        [&](uint16_t p) -> uint8_t { rd_rec.port = p; rd_rec.called = true; return 0xAA; },
        [&](uint16_t p, uint8_t v) { wr_rec.port = p; wr_rec.value = v; wr_rec.called = true; });

    // FE-01: 0x00FE read
    rd_rec = {}; wr_rec = {};
    uint8_t val = pd.read(0x00FE);
    check("FE-01", "0x00FE read dispatches to port_fe handler", rd_rec.called);

    // FE-02: 0xFFFE read (bit 0 = 0)
    rd_rec = {};
    val = pd.read(0xFFFE);
    check("FE-02", "0xFFFE read dispatches (bit 0 = 0)", rd_rec.called);

    // FE-03: 0x00FE write
    wr_rec = {};
    pd.write(0x00FE, 0x42);
    check("FE-03", "0x00FE write dispatches", wr_rec.called);

    // FE-04: 0x00FF should NOT match (bit 0 = 1)
    rd_rec = {};
    pd.read(0x00FF);
    check("FE-04", "0x00FF read does NOT match (bit 0 = 1)", !rd_rec.called);

    // FE-05: 0x01FE (any even address matches)
    rd_rec = {};
    pd.read(0x01FE);
    check("FE-05", "0x01FE read matches (any even address)", rd_rec.called);

    // FE-06: 0xFEFE
    rd_rec = {};
    pd.read(0xFEFE);
    check("FE-06", "0xFEFE read matches", rd_rec.called);
}

// 2. Timex SCLD Port 0xFF
// VHDL: port_ff matches on exact LSB 0xFF
static void test_timex_ff() {
    set_group("Timex 0xFF");

    PortDispatch pd = make_clean_dispatch();
    CallRecord wr_rec{};

    pd.register_handler(0x00FF, 0x00FF,
        [](uint16_t p) -> uint8_t { return 0x00; },
        [&](uint16_t p, uint8_t v) { wr_rec.port = p; wr_rec.value = v; wr_rec.called = true; });

    // FF-01: 0x00FF write
    wr_rec = {};
    pd.write(0x00FF, 0x55);
    check("FF-01", "0x00FF write dispatches to port_ff handler", wr_rec.called);

    // FF-02: verify a different LSB does NOT match
    wr_rec = {};
    pd.write(0x00FE, 0x55);
    check("FF-02", "0x00FE write does NOT dispatch to 0xFF handler", !wr_rec.called);

    // FF-03: 0x00FF read
    CallRecord rd_rec{};
    PortDispatch pd2 = make_clean_dispatch();
    pd2.register_handler(0x00FF, 0x00FF,
        [&](uint16_t p) -> uint8_t { rd_rec.port = p; rd_rec.called = true; return 0x33; },
        nullptr);
    rd_rec = {};
    uint8_t v = pd2.read(0x00FF);
    check("FF-03", "0x00FF read dispatches", rd_rec.called);

    // FF-04: 0x01FF also matches (MSB ignored, LSB-only mask)
    rd_rec = {};
    pd2.read(0x01FF);
    check("FF-04", "0x01FF read also matches (LSB-only decode)", rd_rec.called);
}

// 3. NextREG Ports 0x243B / 0x253B
// VHDL: MSB+LSB matching. 0x243B = MSB 0x24, LSB 0x3B
static void test_nextreg() {
    set_group("NextREG");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_243b{}, rec_253b{};

    pd.register_handler(0xFFFF, 0x243B,
        [&](uint16_t p) -> uint8_t { rec_243b.called = true; return 0x10; },
        [&](uint16_t p, uint8_t v) { rec_243b.called = true; rec_243b.value = v; });

    pd.register_handler(0xFFFF, 0x253B,
        [&](uint16_t p) -> uint8_t { rec_253b.called = true; return 0x20; },
        [&](uint16_t p, uint8_t v) { rec_253b.called = true; rec_253b.value = v; });

    // NR-01: 0x243B write
    rec_243b = {};
    pd.write(0x243B, 0x07);
    check("NR-01", "0x243B write dispatches", rec_243b.called);

    // NR-02: 0x243B read
    rec_243b = {};
    uint8_t v = pd.read(0x243B);
    check("NR-02", "0x243B read dispatches", rec_243b.called);

    // NR-03: 0x253B write
    rec_253b = {};
    pd.write(0x253B, 0x42);
    check("NR-03", "0x253B write dispatches", rec_253b.called);

    // NR-04: 0x253B read
    rec_253b = {};
    v = pd.read(0x253B);
    check("NR-04", "0x253B read dispatches", rec_253b.called);

    // NR-05: 0x243C should NOT match
    rec_243b = {}; rec_253b = {};
    pd.read(0x243C);
    check("NR-05", "0x243C NOT decoded (wrong LSB)", !rec_243b.called && !rec_253b.called);

    // NR-06: two consecutive reads
    rec_253b = {};
    pd.read(0x253B);
    check("NR-06a", "First consecutive 0x253B read works", rec_253b.called);
    rec_253b = {};
    pd.read(0x253B);
    check("NR-06b", "Second consecutive 0x253B read works", rec_253b.called);
}

// 4. 128K Memory Ports (0x7FFD)
// VHDL: port_7ffd matches A15=0, A14=1, A1:0=01 (FD)
// Mask: 0xC002 for A15:14 + A1:0, value 0x4000 | 0x01 = 0x4001
static void test_128k_ports() {
    set_group("128K Mem");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_7ffd{}, rec_dffd{};

    // 0x7FFD: A15=0, A14=1, FD (A1:0=01) -> mask 0xC002, value 0x4000
    pd.register_handler(0xC002, 0x4000,
        [&](uint16_t p) -> uint8_t { rec_7ffd.called = true; return 0xFF; },
        [&](uint16_t p, uint8_t v) { rec_7ffd.called = true; rec_7ffd.value = v; });

    // 0xDFFD: A15:12=1101, FD -> mask 0xF002, value 0xD000
    pd.register_handler(0xF002, 0xD000,
        [&](uint16_t p) -> uint8_t { rec_dffd.called = true; return 0xFF; },
        [&](uint16_t p, uint8_t v) { rec_dffd.called = true; rec_dffd.value = v; });

    // MEM-01: 0x7FFD write
    rec_7ffd = {};
    pd.write(0x7FFD, 0x10);
    check("MEM-01", "0x7FFD write dispatches", rec_7ffd.called);

    // MEM-03: 0xDFFD write
    rec_dffd = {};
    pd.write(0xDFFD, 0x03);
    check("MEM-03", "0xDFFD write dispatches", rec_dffd.called);

    // MEM-06: 0xFFFD should NOT match 0x7FFD handler (A15=1)
    rec_7ffd = {};
    pd.write(0xFFFD, 0x00);
    check("MEM-06", "0xFFFD does NOT match 0x7FFD (A15=1)", !rec_7ffd.called);

    // Verify 0x7FFD mask: 0x5FFD should also match (A15=0, A14=1)
    rec_7ffd = {};
    pd.write(0x5FFD, 0x00);
    check("MEM-01b", "0x5FFD matches 7FFD handler (same masked bits)", rec_7ffd.called);
}

// 5. Kempston Joystick Ports (0x1F, 0x37)
// VHDL: port_1f matches LSB=0x1F, port_37 matches LSB=0x37
static void test_kempston() {
    set_group("Kempston");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_1f{}, rec_37{};

    pd.register_handler(0x00FF, 0x001F,
        [&](uint16_t p) -> uint8_t { rec_1f.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_1f.called = true; });

    pd.register_handler(0x00FF, 0x0037,
        [&](uint16_t p) -> uint8_t { rec_37.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_37.called = true; });

    // JOY-01: 0x001F read
    rec_1f = {};
    pd.read(0x001F);
    check("JOY-01", "0x001F read dispatches to port_1f", rec_1f.called);

    // JOY-04: 0x0037 read
    rec_37 = {};
    pd.read(0x0037);
    check("JOY-04", "0x0037 read dispatches to port_37", rec_37.called);

    // JOY-07: 0xFF1F should match (MSB ignored for LSB-only decode)
    rec_1f = {};
    pd.read(0xFF1F);
    check("JOY-07", "0xFF1F read matches port_1f (MSB ignored)", rec_1f.called);

    // Verify 0x001E does NOT match 0x1F
    rec_1f = {};
    pd.read(0x001E);
    check("JOY-08", "0x001E does NOT match port_1f", !rec_1f.called);
}

// 6. AY Sound Ports (0xFFFD, 0xBFFD)
// VHDL: 0xFFFD mask 0xC002 value 0xC000, 0xBFFD mask 0xC002 value 0x8000
static void test_ay() {
    set_group("AY Sound");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_fffd{}, rec_bffd{};

    pd.register_handler(0xC002, 0xC000,
        [&](uint16_t p) -> uint8_t { rec_fffd.called = true; return 0x42; },
        [&](uint16_t p, uint8_t v) { rec_fffd.called = true; rec_fffd.value = v; });

    pd.register_handler(0xC002, 0x8000,
        [&](uint16_t p) -> uint8_t { rec_bffd.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_bffd.called = true; rec_bffd.value = v; });

    // AY-01: 0xFFFD write (register select)
    rec_fffd = {};
    pd.write(0xFFFD, 0x07);
    check("AY-01", "0xFFFD write dispatches (register select)", rec_fffd.called);

    // AY-02: 0xBFFD write (data write)
    rec_bffd = {};
    pd.write(0xBFFD, 0x3F);
    check("AY-02", "0xBFFD write dispatches (data write)", rec_bffd.called);

    // AY-03: 0xFFFD read (register read)
    rec_fffd = {};
    pd.read(0xFFFD);
    check("AY-03", "0xFFFD read dispatches", rec_fffd.called);

    // Verify: 0xFFFD should NOT match 0xBFFD handler
    rec_bffd = {};
    pd.read(0xFFFD);
    check("AY-04", "0xFFFD does NOT dispatch to BFFD handler", !rec_bffd.called);
}

// 7. SPI Ports (0xE7, 0xEB)
static void test_spi() {
    set_group("SPI");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_e7{}, rec_eb{};

    pd.register_handler(0x00FF, 0x00E7,
        [&](uint16_t p) -> uint8_t { rec_e7.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_e7.called = true; });

    pd.register_handler(0x00FF, 0x00EB,
        [&](uint16_t p) -> uint8_t { rec_eb.called = true; return 0xFF; },
        [&](uint16_t p, uint8_t v) { rec_eb.called = true; });

    // SPI-01: 0x00E7 write (SPI CS)
    rec_e7 = {};
    pd.write(0x00E7, 0x01);
    check("SPI-01", "0x00E7 write dispatches (SPI CS)", rec_e7.called);

    // SPI-02: 0x00EB read (SPI data)
    rec_eb = {};
    pd.read(0x00EB);
    check("SPI-02", "0x00EB read dispatches (SPI data)", rec_eb.called);

    // SPI-03: 0x00EB write
    rec_eb = {};
    pd.write(0x00EB, 0xAA);
    check("SPI-03", "0x00EB write dispatches", rec_eb.called);

    // SPI-04: wrong port
    rec_e7 = {};
    pd.read(0x00E8);
    check("SPI-04", "0x00E8 does NOT match SPI ports", !rec_e7.called);
}

// 8. DivMMC Port (0xE3)
static void test_divmmc() {
    set_group("DivMMC");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec{};

    pd.register_handler(0x00FF, 0x00E3,
        [&](uint16_t p) -> uint8_t { rec.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec.called = true; rec.value = v; });

    // DIV-01: read
    rec = {};
    pd.read(0x00E3);
    check("DIV-01", "0x00E3 read dispatches", rec.called);

    // DIV-02: write
    rec = {};
    pd.write(0x00E3, 0x80);
    check("DIV-02", "0x00E3 write dispatches", rec.called);

    // DIV-03: wrong port
    rec = {};
    pd.read(0x00E4);
    check("DIV-03", "0x00E4 does NOT match DivMMC", !rec.called);
}

// 9. Sprite Ports (0x57, 0x5B, 0x303B)
static void test_sprite() {
    set_group("Sprite");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_57{}, rec_5b{}, rec_303b{};

    pd.register_handler(0x00FF, 0x0057,
        nullptr,
        [&](uint16_t p, uint8_t v) { rec_57.called = true; });

    pd.register_handler(0x00FF, 0x005B,
        nullptr,
        [&](uint16_t p, uint8_t v) { rec_5b.called = true; });

    pd.register_handler(0xFFFF, 0x303B,
        [&](uint16_t p) -> uint8_t { rec_303b.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_303b.called = true; });

    // SPR-01: 0x0057 write (sprite attr)
    rec_57 = {};
    pd.write(0x0057, 0x10);
    check("SPR-01", "0x0057 write dispatches (sprite attr)", rec_57.called);

    // SPR-02: 0x005B write (sprite pattern)
    rec_5b = {};
    pd.write(0x005B, 0xFF);
    check("SPR-02", "0x005B write dispatches (sprite pattern)", rec_5b.called);

    // SPR-03: 0x303B read (sprite status)
    rec_303b = {};
    pd.read(0x303B);
    check("SPR-03", "0x303B read dispatches (sprite status)", rec_303b.called);

    // SPR-04: 0x303B write (sprite index)
    rec_303b = {};
    pd.write(0x303B, 0x05);
    check("SPR-04", "0x303B write dispatches (sprite index)", rec_303b.called);

    // SPR-05: wrong port
    rec_57 = {};
    pd.read(0x0058);
    check("SPR-05", "0x0058 does NOT match sprite ports", !rec_57.called);
}

// 10. Layer 2 Port (0x123B)
static void test_layer2() {
    set_group("Layer 2");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec{};

    pd.register_handler(0xFFFF, 0x123B,
        [&](uint16_t p) -> uint8_t { rec.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec.called = true; });

    // L2-01: read
    rec = {};
    pd.read(0x123B);
    check("L2-01", "0x123B read dispatches", rec.called);

    // L2-02: write
    rec = {};
    pd.write(0x123B, 0x01);
    check("L2-02", "0x123B write dispatches", rec.called);

    // L2-03: wrong address
    rec = {};
    pd.read(0x123C);
    check("L2-03", "0x123C does NOT match Layer 2", !rec.called);
}

// 11. I2C Ports (0x103B, 0x113B)
static void test_i2c() {
    set_group("I2C");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_103b{}, rec_113b{};

    pd.register_handler(0xFFFF, 0x103B,
        [&](uint16_t p) -> uint8_t { rec_103b.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_103b.called = true; });

    pd.register_handler(0xFFFF, 0x113B,
        [&](uint16_t p) -> uint8_t { rec_113b.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_113b.called = true; });

    // I2C-01: 0x103B
    rec_103b = {};
    pd.read(0x103B);
    check("I2C-01", "0x103B read dispatches", rec_103b.called);

    // I2C-02: 0x113B
    rec_113b = {};
    pd.write(0x113B, 0x01);
    check("I2C-02", "0x113B write dispatches", rec_113b.called);

    // I2C-03: wrong address
    rec_103b = {};
    pd.read(0x103C);
    check("I2C-03", "0x103C does NOT match I2C", !rec_103b.called);
}

// 12. UART Ports (0x143B, 0x153B)
static void test_uart() {
    set_group("UART");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec{};

    // UART uses multiple addresses; test with exact match for each
    pd.register_handler(0xFFFF, 0x143B,
        [&](uint16_t p) -> uint8_t { rec.port = p; rec.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec.port = p; rec.called = true; });

    pd.register_handler(0xFFFF, 0x153B,
        [&](uint16_t p) -> uint8_t { rec.port = p; rec.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec.port = p; rec.called = true; });

    // UART-01: 0x143B
    rec = {};
    pd.read(0x143B);
    check("UART-01", "0x143B read dispatches", rec.called);

    // UART-02: 0x153B
    rec = {};
    pd.read(0x153B);
    check("UART-02", "0x153B read dispatches", rec.called);

    // UART-03: 0x133B should NOT match
    rec = {};
    pd.read(0x133B);
    check("UART-03", "0x133B does NOT match UART", !rec.called);
}

// 13. ULA+ Ports (0xBF3B, 0xFF3B)
static void test_ulaplus() {
    set_group("ULA+");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_bf3b{}, rec_ff3b{};

    pd.register_handler(0xFFFF, 0xBF3B,
        [&](uint16_t p) -> uint8_t { rec_bf3b.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_bf3b.called = true; });

    pd.register_handler(0xFFFF, 0xFF3B,
        [&](uint16_t p) -> uint8_t { rec_ff3b.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_ff3b.called = true; });

    // ULAP-01: 0xBF3B write
    rec_bf3b = {};
    pd.write(0xBF3B, 0x01);
    check("ULAP-01", "0xBF3B write dispatches", rec_bf3b.called);

    // ULAP-02: 0xFF3B read
    rec_ff3b = {};
    pd.read(0xFF3B);
    check("ULAP-02", "0xFF3B read dispatches", rec_ff3b.called);

    // ULAP-03: 0xFF3B write
    rec_ff3b = {};
    pd.write(0xFF3B, 0x55);
    check("ULAP-03", "0xFF3B write dispatches", rec_ff3b.called);

    // ULAP-04: wrong address
    rec_bf3b = {};
    pd.read(0xBF3C);
    check("ULAP-04", "0xBF3C does NOT match ULA+", !rec_bf3b.called);
}

// 14. Kempston Mouse Ports (0xFADF, 0xFBDF, 0xFFDF)
static void test_mouse() {
    set_group("Mouse");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_fa{}, rec_fb{}, rec_ff{};

    pd.register_handler(0xFFFF, 0xFADF,
        [&](uint16_t p) -> uint8_t { rec_fa.called = true; return 0x00; },
        nullptr);

    pd.register_handler(0xFFFF, 0xFBDF,
        [&](uint16_t p) -> uint8_t { rec_fb.called = true; return 0x80; },
        nullptr);

    pd.register_handler(0xFFFF, 0xFFDF,
        [&](uint16_t p) -> uint8_t { rec_ff.called = true; return 0x40; },
        nullptr);

    // MOUSE-01: 0xFADF (buttons)
    rec_fa = {};
    pd.read(0xFADF);
    check("MOUSE-01", "0xFADF read dispatches (buttons)", rec_fa.called);

    // MOUSE-02: 0xFBDF (X coord)
    rec_fb = {};
    pd.read(0xFBDF);
    check("MOUSE-02", "0xFBDF read dispatches (X coord)", rec_fb.called);

    // MOUSE-03: 0xFFDF (Y coord)
    rec_ff = {};
    pd.read(0xFFDF);
    check("MOUSE-03", "0xFFDF read dispatches (Y coord)", rec_ff.called);

    // MOUSE-05: wrong nibble
    rec_fa = {}; rec_fb = {}; rec_ff = {};
    pd.read(0xF0DF);
    check("MOUSE-05", "0xF0DF does NOT match mouse", !rec_fa.called && !rec_fb.called && !rec_ff.called);
}

// 15. DMA Port (0x6B)
static void test_dma() {
    set_group("DMA");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec_6b{}, rec_0b{};

    pd.register_handler(0x00FF, 0x006B,
        [&](uint16_t p) -> uint8_t { rec_6b.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_6b.called = true; });

    pd.register_handler(0x00FF, 0x000B,
        [&](uint16_t p) -> uint8_t { rec_0b.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec_0b.called = true; });

    // DMA-01: 0x006B
    rec_6b = {};
    pd.write(0x006B, 0x00);
    check("DMA-01", "0x006B write dispatches", rec_6b.called);

    // DMA-02: 0x000B
    rec_0b = {};
    pd.write(0x000B, 0x00);
    check("DMA-02", "0x000B write dispatches", rec_0b.called);

    // DMA-03: wrong port
    rec_6b = {}; rec_0b = {};
    pd.read(0x006C);
    check("DMA-03", "0x006C does NOT match DMA", !rec_6b.called && !rec_0b.called);
}

// 16. CTC Port (0x183B range)
static void test_ctc() {
    set_group("CTC");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rec{};

    // CTC: A15:11=00011, LSB=3B -> mask bits 15:11 + 7:0
    // A15:11 = 00011 means addresses 0x18xx-0x1Fxx
    // mask = 0xF8FF, value = 0x183B
    pd.register_handler(0xF8FF, 0x183B,
        [&](uint16_t p) -> uint8_t { rec.port = p; rec.called = true; return 0x00; },
        [&](uint16_t p, uint8_t v) { rec.port = p; rec.called = true; });

    // CTC-01: 0x183B
    rec = {};
    pd.read(0x183B);
    check("CTC-01", "0x183B read dispatches", rec.called);

    // CTC-02: 0x1F3B (still A15:11=00011)
    rec = {};
    pd.read(0x1F3B);
    check("CTC-02", "0x1F3B read dispatches (same high bits)", rec.called);

    // CTC-03: 0x203B should NOT match (A15:11=00100)
    rec = {};
    pd.read(0x203B);
    check("CTC-03", "0x203B does NOT match CTC (wrong high bits)", !rec.called);
}

// 17. Handler registration and dispatch ordering
static void test_registration() {
    set_group("Registration");

    // Test that first matching handler wins for reads
    PortDispatch pd = make_clean_dispatch();
    int first_called = 0, second_called = 0;

    pd.register_handler(0x00FF, 0x00AB,
        [&](uint16_t p) -> uint8_t { first_called++; return 0x11; },
        [&](uint16_t p, uint8_t v) { first_called++; });

    pd.register_handler(0x00FF, 0x00AB,
        [&](uint16_t p) -> uint8_t { second_called++; return 0x22; },
        [&](uint16_t p, uint8_t v) { second_called++; });

    // Read: first registered handler should win
    uint8_t v = pd.read(0x00AB);
    check("REG-01", "Read dispatches to first matching handler",
          first_called == 1 && second_called == 0,
          DETAIL("first=%d second=%d val=0x%02x", first_called, second_called, v));

    // Write: VHDL uses wired-OR, our dispatch calls ALL matching handlers
    first_called = 0; second_called = 0;
    pd.write(0x00AB, 0x55);
    check("REG-02", "Write dispatches to all matching handlers",
          first_called == 1 && second_called == 1,
          DETAIL("first=%d second=%d", first_called, second_called));

    // clear_handlers empties the list
    pd.clear_handlers();
    first_called = 0;
    pd.read(0x00AB);
    check("REG-03", "clear_handlers removes all handlers", first_called == 0);
}

// 18. Default read for unmatched ports
static void test_default_read() {
    set_group("Default Read");

    PortDispatch pd = make_clean_dispatch();

    // Without default_read, unmatched returns 0xFF
    uint8_t v = pd.read(0x0042);
    check("DEF-01", "Unmatched read returns 0xFF by default", v == 0xFF,
          DETAIL("got=0x%02x", v));

    // With default_read set
    pd.set_default_read([](uint16_t p) -> uint8_t { return 0x42; });
    v = pd.read(0x0042);
    check("DEF-02", "Unmatched read uses default_read callback", v == 0x42,
          DETAIL("got=0x%02x", v));

    // Matched port still uses its handler, not default
    pd.register_handler(0xFFFF, 0x0099,
        [](uint16_t p) -> uint8_t { return 0xBB; },
        nullptr);
    v = pd.read(0x0099);
    check("DEF-03", "Matched port uses handler, not default_read", v == 0xBB,
          DETAIL("got=0x%02x", v));
}

// 19. IoInterface (in/out) wrappers
static void test_io_interface() {
    set_group("IoInterface");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rd_rec{}, wr_rec{};

    pd.register_handler(0xFFFF, 0x1234,
        [&](uint16_t p) -> uint8_t { rd_rec.called = true; return 0x77; },
        [&](uint16_t p, uint8_t v) { wr_rec.called = true; wr_rec.value = v; });

    // in() calls read()
    rd_rec = {};
    uint8_t v = pd.in(0x1234);
    check("IO-01", "in() dispatches to read handler", rd_rec.called && v == 0x77,
          DETAIL("called=%d val=0x%02x", rd_rec.called, v));

    // out() calls write()
    wr_rec = {};
    pd.out(0x1234, 0x99);
    check("IO-02", "out() dispatches to write handler",
          wr_rec.called && wr_rec.value == 0x99,
          DETAIL("called=%d val=0x%02x", wr_rec.called, wr_rec.value));
}

// 20. RZX override
static void test_rzx() {
    set_group("RZX");

    PortDispatch pd = make_clean_dispatch();
    CallRecord rd_rec{};

    pd.register_handler(0xFFFF, 0x1234,
        [&](uint16_t p) -> uint8_t { rd_rec.called = true; return 0x77; },
        nullptr);

    // RZX playback override: in() returns override value, not handler
    pd.rzx_in_override = [](uint16_t p) -> uint8_t { return 0xEE; };
    rd_rec = {};
    uint8_t v = pd.in(0x1234);
    check("RZX-01", "RZX override bypasses normal dispatch",
          !rd_rec.called && v == 0xEE,
          DETAIL("handler_called=%d val=0x%02x", rd_rec.called, v));

    // RZX recording: record callback called with value
    pd.rzx_in_override = nullptr;
    uint8_t recorded = 0;
    pd.rzx_in_record = [&](uint8_t val) { recorded = val; };
    rd_rec = {};
    v = pd.in(0x1234);
    check("RZX-02", "RZX record captures IN value",
          rd_rec.called && recorded == 0x77,
          DETAIL("recorded=0x%02x expected=0x77", recorded));

    pd.rzx_in_record = nullptr;
}

// 21. Write broadcasts to all matching handlers (wired-OR semantics)
static void test_write_broadcast() {
    set_group("Write Bcast");

    PortDispatch pd = make_clean_dispatch();

    // Two handlers with overlapping masks: both should fire on write
    // Handler A: mask 0x00FF value 0x00FD (matches any xxFD)
    // Handler B: mask 0xC002 value 0xC000 (matches AY port range)
    // Port 0xFFFD matches both
    int a_count = 0, b_count = 0;
    pd.register_handler(0x00FF, 0x00FD,
        nullptr,
        [&](uint16_t p, uint8_t v) { a_count++; });
    pd.register_handler(0xC002, 0xC000,
        nullptr,
        [&](uint16_t p, uint8_t v) { b_count++; });

    pd.write(0xFFFD, 0x07);
    check("BCAST-01", "Write to overlapping handlers fires both",
          a_count == 1 && b_count == 1,
          DETAIL("a=%d b=%d", a_count, b_count));

    // Non-overlapping write: only one fires
    a_count = 0; b_count = 0;
    pd.write(0x00FD, 0x00);  // matches A (LSB=FD) but not B (A15:14 != 11)
    check("BCAST-02", "Non-overlapping write fires only matching handler",
          a_count == 1 && b_count == 0,
          DETAIL("a=%d b=%d", a_count, b_count));
}

// 22. Port address passed correctly to handler
static void test_port_address_passthrough() {
    set_group("Addr Pass");

    PortDispatch pd = make_clean_dispatch();
    uint16_t received_rd = 0, received_wr = 0;

    pd.register_handler(0x0001, 0x0000,
        [&](uint16_t p) -> uint8_t { received_rd = p; return 0x00; },
        [&](uint16_t p, uint8_t v) { received_wr = p; });

    // Full 16-bit address should be passed to handler
    pd.read(0xFEFE);
    check("ADDR-01", "Full 16-bit read address passed to handler",
          received_rd == 0xFEFE,
          DETAIL("got=0x%04x expected=0xFEFE", received_rd));

    pd.write(0xBFFE, 0x10);
    check("ADDR-02", "Full 16-bit write address passed to handler",
          received_wr == 0xBFFE,
          DETAIL("got=0x%04x expected=0xBFFE", received_wr));
}

// -- Main ------------------------------------------------------------------

int main() {
    printf("I/O Port Dispatch Compliance Tests\n");
    printf("====================================\n\n");

    test_ula_fe();
    printf("  Group: ULA 0xFE -- done\n");

    test_timex_ff();
    printf("  Group: Timex 0xFF -- done\n");

    test_nextreg();
    printf("  Group: NextREG -- done\n");

    test_128k_ports();
    printf("  Group: 128K Memory -- done\n");

    test_kempston();
    printf("  Group: Kempston -- done\n");

    test_ay();
    printf("  Group: AY Sound -- done\n");

    test_spi();
    printf("  Group: SPI -- done\n");

    test_divmmc();
    printf("  Group: DivMMC -- done\n");

    test_sprite();
    printf("  Group: Sprite -- done\n");

    test_layer2();
    printf("  Group: Layer 2 -- done\n");

    test_i2c();
    printf("  Group: I2C -- done\n");

    test_uart();
    printf("  Group: UART -- done\n");

    test_ulaplus();
    printf("  Group: ULA+ -- done\n");

    test_mouse();
    printf("  Group: Mouse -- done\n");

    test_dma();
    printf("  Group: DMA -- done\n");

    test_ctc();
    printf("  Group: CTC -- done\n");

    test_registration();
    printf("  Group: Registration -- done\n");

    test_default_read();
    printf("  Group: Default Read -- done\n");

    test_io_interface();
    printf("  Group: IoInterface -- done\n");

    test_rzx();
    printf("  Group: RZX -- done\n");

    test_write_broadcast();
    printf("  Group: Write Broadcast -- done\n");

    test_port_address_passthrough();
    printf("  Group: Address Passthrough -- done\n");

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
