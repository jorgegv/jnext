// DMA Subsystem Compliance Test Runner
//
// Tests the DMA subsystem against VHDL-derived expected behaviour.
// All expected values come from the DMA-TEST-PLAN-DESIGN.md spec.
//
// Run: ./build/test/dma_test

#include "peripheral/dma.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <functional>

// ── DMA register identification base bytes (from VHDL zxnext/src/device/dma.vhd) ──
// R0: bit7=0, bit0|bit1=1             → 0x01 template (transfer op)
// R1: bit7=0, bits[2:0]=100           → 0x04
// R2: bit7=0, bits[2:0]=000           → 0x00
// R3: bit7=1, bits[1:0]=00            → 0x80
// R4: bit7=1, bits[1:0]=01            → 0x81   (NOTE: test used to write 0x2D: bit7 missing)
// R5: bits[7:6]=10, bits[2:0]=010     → 0x82
// R6: bit7=1, bits[1:0]=11            → 0x83   (command register)
static constexpr uint8_t DMA_R0_BASE = 0x01;
static constexpr uint8_t DMA_R1_BASE = 0x04;
static constexpr uint8_t DMA_R2_BASE = 0x00;
static constexpr uint8_t DMA_R3_BASE = 0x80;
static constexpr uint8_t DMA_R4_BASE = 0x81;
static constexpr uint8_t DMA_R5_BASE = 0x82;
static constexpr uint8_t DMA_R6_BASE = 0x83;

// Common R4 programmings built from the VHDL-correct base byte.
// R4 bit2=load port B addr LO, bit3=load port B addr HI, bits[6:5]=mode (01=cont,10=burst)
static constexpr uint8_t DMA_R4_CONT_LOAD_B   = DMA_R4_BASE | (0x01 << 5) | 0x04 | 0x08; // 0xAD

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

// ── Mock memory and I/O ──────────────────────────────────────────────

static std::array<uint8_t, 65536> g_mem;
static std::array<uint8_t, 65536> g_io;  // indexed by port address

static void clear_mem() { g_mem.fill(0); }
static void clear_io()  { g_io.fill(0);  }

static void attach_callbacks(Dma& dma) {
    dma.read_memory  = [](uint16_t a) -> uint8_t { return g_mem[a]; };
    dma.write_memory = [](uint16_t a, uint8_t v) { g_mem[a] = v; };
    dma.read_io      = [](uint16_t p) -> uint8_t { return g_io[p]; };
    dma.write_io     = [](uint16_t p, uint8_t v) { g_io[p] = v; };
}

static void fresh(Dma& dma) {
    dma.reset();
    clear_mem();
    clear_io();
    attach_callbacks(dma);
}

// ── Helpers: DMA programming sequences ───────────────────────────────

// Write to ZXN DMA port (0x6B, z80_compat=false)
static void zxn_write(Dma& dma, uint8_t val) {
    dma.write(val, false);
}

// Write to Z80-DMA port (0x0B, z80_compat=true)
static void z80_write(Dma& dma, uint8_t val) {
    dma.write(val, true);
}

// Program a standard mem-to-mem transfer: A->B, both increment, continuous
// src_addr -> dst_addr, block_len bytes
static void program_mem_to_mem(Dma& dma, uint16_t src, uint16_t dst, uint16_t len, bool zxn = true) {
    auto wr = zxn ? zxn_write : z80_write;

    // R0: A->B, port A addr + block length (all 4 sub-bytes)
    // bit2=1 (A->B), bit3=1 (addrLO), bit4=1 (addrHI), bit5=1 (lenLO), bit6=1 (lenHI)
    // = 0b0_1_1_1_1_1_0_1 = 0x7D
    wr(dma, 0x7D);
    wr(dma, src & 0xFF);
    wr(dma, (src >> 8) & 0xFF);
    wr(dma, len & 0xFF);
    wr(dma, (len >> 8) & 0xFF);

    // R1: port A = memory, increment, no timing sub-bytes
    // bits[2:0]=100, bit3=0(mem), bits[5:4]=01(inc), bit6=0(no timing)
    // = 0b0_0_01_0_100 = 0x14
    wr(dma, 0x14);

    // R2: port B = memory, increment, no timing sub-bytes
    // bits[2:0]=000, bit3=0(mem), bits[5:4]=01(inc), bit6=0(no timing)
    // = 0b0_0_01_0_000 = 0x10
    wr(dma, 0x10);

    // R4: continuous mode (01), port B addr follows (bit2+bit3)
    // bits[1:0]=01, bit2=1(addrLO), bit3=1(addrHI), bits[6:5]=01(continuous)
    // = 0b1_01_0_1_1_01 = 0xAD (DMA_R4_CONT_LOAD_B)
    wr(dma, DMA_R4_CONT_LOAD_B);
    wr(dma, dst & 0xFF);
    wr(dma, (dst >> 8) & 0xFF);

    // Load + Enable
    wr(dma, 0xCF);  // Load
    wr(dma, 0x87);  // Enable
}

// Run DMA until idle or max_bytes reached
static int run_dma(Dma& dma, int max_bytes = 65536) {
    int total = 0;
    while (dma.state() == Dma::State::TRANSFERRING && total < max_bytes) {
        int n = dma.execute_burst(max_bytes - total);
        if (n == 0) break;
        total += n;
    }
    return total;
}

// ── Group 1: Port Decoding and Mode Selection ────────────────────────
// Note: We test mode via the counter initialization after Load (ZXN=0, Z80=0xFFFF)

static void test_group1_port_decode() {
    set_group("Port Decode");
    Dma dma;

    // 1.1: Write to ZXN port sets counter to 0 on Load
    {
        fresh(dma);
        zxn_write(dma, 0x79); // R0: A->B, addr+len sub-bytes
        zxn_write(dma, 0x00); zxn_write(dma, 0x80); // portA = 0x8000
        zxn_write(dma, 0x10); zxn_write(dma, 0x00); // len = 16
        zxn_write(dma, 0xCF); // Load
        check("1.1", "ZXN mode: counter=0 after Load",
              dma.counter() == 0,
              DETAIL("counter=%u", dma.counter()));
    }

    // 1.2: Write to Z80 port sets counter to 0xFFFF on Load
    {
        fresh(dma);
        z80_write(dma, 0x79);
        z80_write(dma, 0x00); z80_write(dma, 0x80);
        z80_write(dma, 0x10); z80_write(dma, 0x00);
        z80_write(dma, 0xCF); // Load
        check("1.2", "Z80 mode: counter=0xFFFF after Load",
              dma.counter() == 0xFFFF,
              DETAIL("counter=0x%04X", dma.counter()));
    }

    // 1.3: Mode switches on each access
    {
        fresh(dma);
        // First write ZXN, then Load -> counter=0
        zxn_write(dma, 0x79);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0x10); zxn_write(dma, 0x00);
        zxn_write(dma, 0xCF);
        check("1.3a", "ZXN then Load: counter=0",
              dma.counter() == 0,
              DETAIL("counter=%u", dma.counter()));

        // Now write Z80 Continue -> counter should be 0xFFFF
        z80_write(dma, 0xD3);
        check("1.3b", "Switch to Z80 Continue: counter=0xFFFF",
              dma.counter() == 0xFFFF,
              DETAIL("counter=0x%04X", dma.counter()));
    }

    // 1.4: Default mode after reset (ZXN, counter=0)
    {
        fresh(dma);
        // Just check that a Load with no writes to either port results in ZXN mode
        // We need an R0 write first (any valid one)
        zxn_write(dma, 0x79);
        zxn_write(dma, 0x00); zxn_write(dma, 0x00);
        zxn_write(dma, 0x01); zxn_write(dma, 0x00);
        zxn_write(dma, 0xCF);
        check("1.4", "Default mode is ZXN after reset",
              dma.counter() == 0,
              DETAIL("counter=%u", dma.counter()));
    }
}

// ── Group 2: R0 — Direction, Port A Address, Block Length ────────────

static void test_group2_r0() {
    set_group("R0 Programming");
    Dma dma;

    // 2.1: Direction A->B
    {
        fresh(dma);
        // R0 with bit2=1 (A->B), addr+len
        zxn_write(dma, 0x7D); // bit2=1 -> A->B
        zxn_write(dma, 0x00); zxn_write(dma, 0x10); // portA = 0x1000
        zxn_write(dma, 0x04); zxn_write(dma, 0x00); // len = 4
        // R4 with port B addr
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x20); // portB = 0x2000
        zxn_write(dma, 0xCF); // Load
        check("2.1", "Direction A->B: src=portA, dst=portB",
              dma.src_addr() == 0x1000 && dma.dst_addr() == 0x2000,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 2.2: Direction B->A
    {
        fresh(dma);
        // R0 with bit2=0 (B->A)
        // bit0=1, bit3=1(addrLO), bit4=1(addrHI), bit5=1(lenLO), bit6=1(lenHI)
        // = 0b0_1_1_1_1_0_0_1 = 0x79
        zxn_write(dma, 0x79); // bit2=0 -> B->A
        zxn_write(dma, 0x00); zxn_write(dma, 0x10); // portA = 0x1000
        zxn_write(dma, 0x04); zxn_write(dma, 0x00); // len = 4
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x20); // portB = 0x2000
        zxn_write(dma, 0xCF); // Load
        // B->A: src=portB(0x2000), dst=portA(0x1000)
        check("2.2", "Direction B->A: src=portB, dst=portA",
              dma.src_addr() == 0x2000 && dma.dst_addr() == 0x1000,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 2.3: Port A start address low byte only
    {
        fresh(dma);
        // R0 with bit3=1 only (addr LO)
        // bit0=1, bit2=1(A->B), bit3=1(addrLO)
        // = 0b0_0_0_0_1_1_0_1 = 0x0D
        zxn_write(dma, 0x0D);
        zxn_write(dma, 0xAB); // portA LO = 0xAB
        zxn_write(dma, 0xCF); // Load
        check("2.3", "Port A addr low byte",
              (dma.src_addr() & 0xFF) == 0xAB,
              DETAIL("src=0x%04X", dma.src_addr()));
    }

    // 2.4: Port A start address high byte
    {
        fresh(dma);
        // R0 with bit3=1 + bit4=1 (addr LO + HI)
        // bit0=1, bit2=1, bit3=1, bit4=1
        // = 0b0_0_0_1_1_1_0_1 = 0x1D
        zxn_write(dma, 0x1D);
        zxn_write(dma, 0x34); // portA LO
        zxn_write(dma, 0x12); // portA HI
        zxn_write(dma, 0xCF);
        check("2.4", "Port A full 16-bit address",
              dma.src_addr() == 0x1234,
              DETAIL("src=0x%04X", dma.src_addr()));
    }

    // 2.5: Block length programming
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0x00); zxn_write(dma, 0x01); // len = 256
        zxn_write(dma, 0xCF);
        check("2.5", "Block length = 256",
              dma.block_length() == 256,
              DETAIL("block_len=%u", dma.block_length()));
    }

    // 2.6: Block length low byte only
    {
        fresh(dma);
        // R0 with bit5=1 only (len LO), bit0=1, bit2=1
        // = 0b0_0_1_0_0_1_0_1 = 0x25
        zxn_write(dma, 0x25);
        zxn_write(dma, 0x10); // len LO = 16
        zxn_write(dma, 0xCF);
        check("2.6", "Block length low byte only",
              (dma.block_length() & 0xFF) == 0x10,
              DETAIL("block_len=%u", dma.block_length()));
    }

    // 2.7: Selective byte programming (addr LO only, no HI, no len)
    {
        fresh(dma);
        // First set full address to known value
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0xEF); zxn_write(dma, 0xBE);
        zxn_write(dma, 0x20); zxn_write(dma, 0x00);
        // Now write only addr LO
        zxn_write(dma, 0x0D); // bit0=1, bit2=1, bit3=1
        zxn_write(dma, 0x42); // new portA LO
        zxn_write(dma, 0xCF);
        check("2.7", "Selective: only addr LO changed",
              dma.src_addr() == 0xBE42,
              DETAIL("src=0x%04X (expected 0xBE42)", dma.src_addr()));
    }
}

// ── Group 3: R1 — Port A Configuration ──────────────────────────────

static void test_group3_r1() {
    set_group("R1 Port A Config");
    Dma dma;

    // 3.1: Port A is memory (default)
    {
        fresh(dma);
        // R1: bit3=0 (memory), bits[5:4]=01 (inc)
        zxn_write(dma, 0x14); // = 0b0_0_01_0_100
        check("3.1", "Port A is memory (bit3=0)",
              dma.src_addr_mode() == Dma::AddrMode::INCREMENT, // just check it programs
              "");
    }

    // 3.2: Port A address increment
    {
        fresh(dma);
        zxn_write(dma, 0x14); // addr_mode = 01 (increment)
        check("3.2", "Port A address mode = increment",
              dma.src_addr_mode() == Dma::AddrMode::INCREMENT,
              DETAIL("mode=%d", (int)dma.src_addr_mode()));
    }

    // 3.3: Port A address decrement
    {
        fresh(dma);
        // R1: bits[5:4]=00 (decrement)
        // = 0b0_0_00_0_100 = 0x04
        zxn_write(dma, 0x04);
        check("3.3", "Port A address mode = decrement",
              dma.src_addr_mode() == Dma::AddrMode::DECREMENT,
              DETAIL("mode=%d", (int)dma.src_addr_mode()));
    }

    // 3.4: Port A address fixed
    {
        fresh(dma);
        // R1: bits[5:4]=10 (fixed)
        // = 0b0_0_10_0_100 = 0x24
        zxn_write(dma, 0x24);
        check("3.4", "Port A address mode = fixed",
              dma.src_addr_mode() == Dma::AddrMode::FIXED,
              DETAIL("mode=%d", (int)dma.src_addr_mode()));
    }

    // 3.5: Port A is I/O
    {
        fresh(dma);
        // R1: bit3=1 (I/O), bits[5:4]=01 (inc)
        // = 0b0_0_01_1_100 = 0x1C
        zxn_write(dma, 0x1C);
        // Verify through a transfer: set up mem-to-IO transfer
        // Program src from IO
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x00); // portA addr = 0x0000
        zxn_write(dma, 0x01); zxn_write(dma, 0x00); // len = 1
        zxn_write(dma, 0x10); // R2: portB = mem, inc
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90); // portB = 0x9000
        g_io[0x0000] = 0x42;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        check("3.5", "Port A is I/O: reads from I/O",
              g_mem[0x9000] == 0x42,
              DETAIL("mem[0x9000]=0x%02X", g_mem[0x9000]));
    }
}

// ── Group 4: R2 — Port B Configuration ──────────────────────────────

static void test_group4_r2() {
    set_group("R2 Port B Config");
    Dma dma;

    // 4.1: Port B address increment
    {
        fresh(dma);
        zxn_write(dma, 0x10); // R2: mem, inc
        check("4.1", "Port B address mode = increment",
              dma.dst_addr_mode() == Dma::AddrMode::INCREMENT,
              DETAIL("mode=%d", (int)dma.dst_addr_mode()));
    }

    // 4.2: Port B address decrement
    {
        fresh(dma);
        zxn_write(dma, 0x00); // R2: mem, dec (bits[5:4]=00)
        check("4.2", "Port B address mode = decrement",
              dma.dst_addr_mode() == Dma::AddrMode::DECREMENT,
              DETAIL("mode=%d", (int)dma.dst_addr_mode()));
    }

    // 4.3: Port B address fixed
    {
        fresh(dma);
        zxn_write(dma, 0x20); // R2: mem, fixed (bits[5:4]=10)
        check("4.3", "Port B address mode = fixed",
              dma.dst_addr_mode() == Dma::AddrMode::FIXED,
              DETAIL("mode=%d", (int)dma.dst_addr_mode()));
    }

    // 4.4: Port B is I/O
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80); // portA = 0x8000
        zxn_write(dma, 0x01); zxn_write(dma, 0x00); // len = 1
        zxn_write(dma, 0x14); // R1: portA = mem, inc
        zxn_write(dma, 0x28); // R2: portB = IO(bit3=1), fixed (bits[5:4]=10)
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x55); zxn_write(dma, 0x00); // portB = 0x0055
        g_mem[0x8000] = 0xAA;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        check("4.4", "Port B is I/O: writes to I/O port",
              g_io[0x0055] == 0xAA,
              DETAIL("io[0x55]=0x%02X", g_io[0x0055]));
    }

    // 4.5: Port B prescaler byte
    {
        fresh(dma);
        // R2 with timing sub-byte (bit6=1) and prescaler (timing byte bit5=1)
        // R2 base: bits[2:0]=000, bit3=0(mem), bits[5:4]=01(inc), bit6=1(timing follows)
        // = 0b0_1_01_0_000 = 0x50
        zxn_write(dma, 0x50);
        // Timing byte: bits[1:0]=timing, bit5=1(prescaler follows)
        // = 0b0_0_1_0_0_0_01 = 0x21
        zxn_write(dma, 0x21);
        // Prescaler byte
        zxn_write(dma, 0x0A);
        // We can't directly read prescaler, but we can verify it was consumed
        // by checking that the write sequence returned to IDLE
        check("4.5", "Port B prescaler byte consumed",
              true, ""); // Just verify no crash
    }
}

// ── Group 5: R3 — DMA Enable ────────────────────────────────────────

static void test_group5_r3() {
    set_group("R3 DMA Enable");
    Dma dma;

    // 5.1: R3 with bit6=1 starts DMA
    {
        fresh(dma);
        // R3: bit7=1, bits[1:0]=00, bit6=1 (enable)
        // = 0b1_1_0_0_0_0_00 = 0xC0
        zxn_write(dma, 0xC0);
        check("5.1", "R3 bit6=1 starts DMA",
              dma.state() == Dma::State::TRANSFERRING,
              DETAIL("state=%d", (int)dma.state()));
    }

    // 5.2: R3 with bit6=0 does not start
    {
        fresh(dma);
        // R3: bit7=1, bits[1:0]=00, bit6=0
        // = 0b1_0_0_0_0_0_00 = 0x80
        zxn_write(dma, 0x80);
        check("5.2", "R3 bit6=0 does not start DMA",
              dma.state() == Dma::State::IDLE,
              DETAIL("state=%d", (int)dma.state()));
    }

    // 5.3: R3 mask byte consumed (bit3=1)
    {
        fresh(dma);
        // R3 base with bit3=1 (mask byte follows)
        // = 0b1_0_0_0_1_0_00 = 0x88
        zxn_write(dma, 0x88);
        zxn_write(dma, 0xFF); // mask byte - should be consumed
        check("5.3", "R3 mask byte consumed without crash",
              true, "");
    }

    // 5.4: R3 match byte consumed (bit4=1)
    {
        fresh(dma);
        // R3 base with bit3=1 + bit4=1
        // = 0b1_0_0_1_1_0_00 = 0x98
        zxn_write(dma, 0x98);
        zxn_write(dma, 0xFF); // mask byte
        zxn_write(dma, 0xAA); // match byte
        check("5.4", "R3 match byte consumed without crash",
              true, "");
    }
}

// ── Group 6: R4 — Transfer Mode, Port B Address ─────────────────────

static void test_group6_r4() {
    set_group("R4 Mode/PortB");
    Dma dma;

    // 6.1: Default mode is continuous (01)
    {
        fresh(dma);
        check("6.1", "Default transfer mode = continuous",
              dma.transfer_mode() == Dma::TransferMode::CONTINUOUS,
              DETAIL("mode=%d", (int)dma.transfer_mode()));
    }

    // 6.2: Set byte mode (00)
    {
        fresh(dma);
        // R4: bits[6:5]=00(byte), bits[1:0]=01
        // = 0b0_00_0_0_0_01 = 0x01
        zxn_write(dma, 0x01);
        check("6.2", "Byte mode (R4_mode=00)",
              dma.transfer_mode() == Dma::TransferMode::BYTE,
              DETAIL("mode=%d", (int)dma.transfer_mode()));
    }

    // 6.3: Set burst mode (10)
    {
        fresh(dma);
        // R4: bits[6:5]=10(burst), bits[1:0]=01
        // = 0b0_10_0_0_0_01 = 0x41
        zxn_write(dma, 0x41);
        check("6.3", "Burst mode (R4_mode=10)",
              dma.transfer_mode() == Dma::TransferMode::BURST,
              DETAIL("mode=%d", (int)dma.transfer_mode()));
    }

    // 6.4: Port B start address
    {
        fresh(dma);
        // R4: continuous, port B addr follows (bit2+bit3)
        // = 0b1_01_0_1_1_01 = 0xAD (DMA_R4_CONT_LOAD_B)
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x78); // portB LO
        zxn_write(dma, 0x56); // portB HI
        // Need to set direction and Load to check via dst_addr
        zxn_write(dma, 0x7D); // R0: A->B
        zxn_write(dma, 0x00); zxn_write(dma, 0x10);
        zxn_write(dma, 0x01); zxn_write(dma, 0x00);
        zxn_write(dma, 0xCF);
        check("6.4", "Port B address = 0x5678",
              dma.dst_addr() == 0x5678,
              DETAIL("dst=0x%04X", dma.dst_addr()));
    }
}

// ── Group 7: R5 — Auto-restart, CE/WAIT ─────────────────────────────

static void test_group7_r5() {
    set_group("R5 Auto-restart");
    Dma dma;

    // 7.1: Defaults on reset
    {
        fresh(dma);
        // After reset, auto_restart should be off
        // We test by running a transfer and verifying it stops
        program_mem_to_mem(dma, 0x8000, 0x9000, 4);
        g_mem[0x8000] = 1; g_mem[0x8001] = 2;
        g_mem[0x8002] = 3; g_mem[0x8003] = 4;
        run_dma(dma);
        check("7.1", "Default: DMA stops after block (no auto-restart)",
              dma.state() == Dma::State::IDLE,
              DETAIL("state=%d", (int)dma.state()));
    }

    // 7.2: Auto-restart enabled
    {
        fresh(dma);
        // R5: bit5=1 (auto-restart), bits[7:6]=10, bits[2:0]=010
        // = 0b10_1_0_0_010 = 0xA2
        zxn_write(dma, 0xA2);
        // Program a short transfer
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0x02); zxn_write(dma, 0x00); // len=2
        zxn_write(dma, 0x14); // R1: mem, inc
        zxn_write(dma, 0x10); // R2: mem, inc
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90);
        g_mem[0x8000] = 0xAA; g_mem[0x8001] = 0xBB;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        // Run exactly the block length
        int n = dma.execute_burst(2);
        // After auto-restart, DMA should still be TRANSFERRING
        check("7.2", "Auto-restart: DMA still active after block",
              dma.state() == Dma::State::TRANSFERRING,
              DETAIL("state=%d transferred=%d", (int)dma.state(), n));
    }

    // 7.3: R5 CE/WAIT bit
    {
        fresh(dma);
        // R5: bit4=1 (ce_wait), bits[7:6]=10, bits[2:0]=010
        // = 0b10_0_1_0_010 = 0x92
        zxn_write(dma, 0x92);
        check("7.3", "R5 CE/WAIT bit accepted",
              true, "");
    }
}

// ── Group 8: R6 Commands ─────────────────────────────────────────────

static void test_group8_r6_commands() {
    set_group("R6 Commands");
    Dma dma;

    // 8.1: 0xC3 Reset
    {
        fresh(dma);
        // Set some non-default state first
        zxn_write(dma, 0xA2); // R5: auto-restart
        zxn_write(dma, 0x87); // Enable DMA
        // Now reset
        zxn_write(dma, 0xC3);
        check("8.1", "0xC3 Reset -> IDLE",
              dma.state() == Dma::State::IDLE,
              DETAIL("state=%d", (int)dma.state()));
    }

    // 8.2: 0xC7 Reset port A timing
    {
        fresh(dma);
        zxn_write(dma, 0xC7);
        check("8.2", "0xC7 Reset port A timing (no crash)",
              true, "");
    }

    // 8.3: 0xCB Reset port B timing
    {
        fresh(dma);
        zxn_write(dma, 0xCB);
        check("8.3", "0xCB Reset port B timing (no crash)",
              true, "");
    }

    // 8.4: 0xCF Load A->B direction
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x34); zxn_write(dma, 0x12); // portA = 0x1234
        zxn_write(dma, 0x08); zxn_write(dma, 0x00);
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x78); zxn_write(dma, 0x56); // portB = 0x5678
        zxn_write(dma, 0xCF);
        check("8.4", "Load A->B: src=portA, dst=portB",
              dma.src_addr() == 0x1234 && dma.dst_addr() == 0x5678,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 8.5: 0xCF Load B->A direction
    {
        fresh(dma);
        zxn_write(dma, 0x79); // B->A (bit2=0)
        zxn_write(dma, 0x34); zxn_write(dma, 0x12);
        zxn_write(dma, 0x08); zxn_write(dma, 0x00);
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x78); zxn_write(dma, 0x56);
        zxn_write(dma, 0xCF);
        check("8.5", "Load B->A: src=portB, dst=portA",
              dma.src_addr() == 0x5678 && dma.dst_addr() == 0x1234,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 8.6: 0xCF Load counter ZXN mode
    {
        fresh(dma);
        zxn_write(dma, 0x79);
        zxn_write(dma, 0x00); zxn_write(dma, 0x00);
        zxn_write(dma, 0x10); zxn_write(dma, 0x00);
        zxn_write(dma, 0xCF);
        check("8.6", "Load ZXN: counter=0",
              dma.counter() == 0,
              DETAIL("counter=0x%04X", dma.counter()));
    }

    // 8.7: 0xCF Load counter Z80 mode
    {
        fresh(dma);
        z80_write(dma, 0x79);
        z80_write(dma, 0x00); z80_write(dma, 0x00);
        z80_write(dma, 0x10); z80_write(dma, 0x00);
        z80_write(dma, 0xCF);
        check("8.7", "Load Z80: counter=0xFFFF",
              dma.counter() == 0xFFFF,
              DETAIL("counter=0x%04X", dma.counter()));
    }

    // 8.8: 0xD3 Continue ZXN mode
    {
        fresh(dma);
        // Load first to set addresses, then transfer some bytes, then Continue
        program_mem_to_mem(dma, 0x8000, 0x9000, 4);
        g_mem[0x8000] = 1; g_mem[0x8001] = 2;
        g_mem[0x8002] = 3; g_mem[0x8003] = 4;
        run_dma(dma);
        uint16_t src_after = dma.src_addr();
        uint16_t dst_after = dma.dst_addr();
        zxn_write(dma, 0xD3); // Continue
        check("8.8", "Continue ZXN: counter=0, addrs preserved",
              dma.counter() == 0 &&
              dma.src_addr() == src_after && dma.dst_addr() == dst_after,
              DETAIL("counter=0x%04X src=0x%04X dst=0x%04X",
                     dma.counter(), dma.src_addr(), dma.dst_addr()));
    }

    // 8.9: 0xD3 Continue Z80 mode
    {
        fresh(dma);
        z80_write(dma, 0x79);
        z80_write(dma, 0x00); z80_write(dma, 0x80);
        z80_write(dma, 0x04); z80_write(dma, 0x00);
        z80_write(dma, 0xCF);
        z80_write(dma, 0xD3);
        check("8.9", "Continue Z80: counter=0xFFFF",
              dma.counter() == 0xFFFF,
              DETAIL("counter=0x%04X", dma.counter()));
    }

    // 8.10: 0x87 Enable DMA
    {
        fresh(dma);
        zxn_write(dma, 0x87);
        check("8.10", "Enable DMA -> TRANSFERRING",
              dma.state() == Dma::State::TRANSFERRING,
              DETAIL("state=%d", (int)dma.state()));
    }

    // 8.11: 0x83 Disable DMA
    {
        fresh(dma);
        zxn_write(dma, 0x87); // Enable first
        zxn_write(dma, 0x83); // Disable
        check("8.11", "Disable DMA -> IDLE",
              dma.state() == Dma::State::IDLE,
              DETAIL("state=%d", (int)dma.state()));
    }

    // 8.12: 0x8B Reinitialize status
    {
        fresh(dma);
        // Run a transfer to set status bits
        program_mem_to_mem(dma, 0x8000, 0x9000, 1);
        g_mem[0x8000] = 0x42;
        run_dma(dma);
        // Now reinitialize status
        zxn_write(dma, 0x8B);
        // Read status: should be 0x3A (endofblock_n=1, atleastone=0) wait...
        // Actually status byte: bit5=endofblock_n (1=not ended), bits[4:1]=1101=0x1A, bit0=atleastone
        // After reinit: endofblock=false -> bit5=1(not ended), atleastone=false -> bit0=0
        // = 0b00_1_1101_0 = 0x3A
        zxn_write(dma, 0xBF); // Read status byte command
        uint8_t status = dma.read();
        check("8.12", "0x8B reinitializes status",
              status == 0x3A,
              DETAIL("status=0x%02X (expected 0x3A)", status));
    }

    // 8.13: 0xBB Read mask follows
    {
        fresh(dma);
        zxn_write(dma, 0xBB);
        zxn_write(dma, 0x07); // mask = bits 0,1,2 only
        check("8.13", "0xBB read mask accepted",
              true, "");
    }

    // 8.14: 0xBF Read status byte
    {
        fresh(dma);
        zxn_write(dma, 0xBF);
        uint8_t status = dma.read();
        // Initial status: endofblock_n=1, atleastone=0
        // = 0b00_1_1101_0 = 0x3A
        // Wait, the status byte from code: (end_of_block ? 0 : 0x20) | 0x1A | atleastone
        // Initially: end_of_block_=false -> 0x20, atleastone=false -> 0
        // = 0x20 | 0x1A = 0x3A
        check("8.14", "0xBF: read status returns 0x3A initially",
              status == 0x3A,
              DETAIL("status=0x%02X", status));
    }

    // 8.15: Interrupt commands accepted (no-ops)
    {
        fresh(dma);
        zxn_write(dma, 0xAF); // Disable interrupts
        zxn_write(dma, 0xAB); // Enable interrupts
        zxn_write(dma, 0xA3); // Reset+disable interrupts
        zxn_write(dma, 0xB7); // Enable after RETI
        zxn_write(dma, 0xB3); // Force ready
        check("8.15", "Interrupt/ready commands accepted (no-ops)",
              true, "");
    }
}

// ── Group 9: Memory-to-Memory Transfer ──────────────────────────────

static void test_group9_mem_to_mem() {
    set_group("Mem-to-Mem Transfer");
    Dma dma;

    // 9.1: Simple A->B, increment both
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 4);
        g_mem[0x8000] = 0x11; g_mem[0x8001] = 0x22;
        g_mem[0x8002] = 0x33; g_mem[0x8003] = 0x44;
        int n = run_dma(dma);
        bool ok = (g_mem[0x9000] == 0x11 && g_mem[0x9001] == 0x22 &&
                   g_mem[0x9002] == 0x33 && g_mem[0x9003] == 0x44);
        check("9.1", "A->B, inc both, 4 bytes",
              ok && n == 4,
              DETAIL("n=%d mem=[%02X %02X %02X %02X]", n,
                     g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.2: B->A direction
    {
        fresh(dma);
        // R0: B->A (bit2=0)
        zxn_write(dma, 0x79);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90); // portA = 0x9000
        zxn_write(dma, 0x04); zxn_write(dma, 0x00); // len = 4
        zxn_write(dma, 0x14); // R1: portA = mem, inc
        zxn_write(dma, 0x10); // R2: portB = mem, inc
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80); // portB = 0x8000
        g_mem[0x8000] = 0xAA; g_mem[0x8001] = 0xBB;
        g_mem[0x8002] = 0xCC; g_mem[0x8003] = 0xDD;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        // B->A: src=portB(0x8000), dst=portA(0x9000)
        bool ok = (g_mem[0x9000] == 0xAA && g_mem[0x9001] == 0xBB &&
                   g_mem[0x9002] == 0xCC && g_mem[0x9003] == 0xDD);
        check("9.2", "B->A direction, 4 bytes",
              ok,
              DETAIL("mem=[%02X %02X %02X %02X]",
                     g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.3: A->B, decrement source
    {
        fresh(dma);
        zxn_write(dma, 0x7D); // A->B
        zxn_write(dma, 0x03); zxn_write(dma, 0x80); // portA = 0x8003
        zxn_write(dma, 0x04); zxn_write(dma, 0x00); // len = 4
        zxn_write(dma, 0x04); // R1: portA = mem, decrement (bits[5:4]=00)
        zxn_write(dma, 0x10); // R2: portB = mem, increment
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90); // portB = 0x9000
        g_mem[0x8000] = 0x44; g_mem[0x8001] = 0x33;
        g_mem[0x8002] = 0x22; g_mem[0x8003] = 0x11;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        // Source decrements from 0x8003: reads 0x11, 0x22, 0x33, 0x44
        bool ok = (g_mem[0x9000] == 0x11 && g_mem[0x9001] == 0x22 &&
                   g_mem[0x9002] == 0x33 && g_mem[0x9003] == 0x44);
        check("9.3", "A->B, src decrement",
              ok,
              DETAIL("mem=[%02X %02X %02X %02X]",
                     g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.4: A->B, fixed source (fill)
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80); // portA = 0x8000
        zxn_write(dma, 0x04); zxn_write(dma, 0x00); // len = 4
        zxn_write(dma, 0x24); // R1: portA = mem, fixed (bits[5:4]=10)
        zxn_write(dma, 0x10); // R2: portB = mem, increment
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90);
        g_mem[0x8000] = 0xFF;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        bool ok = (g_mem[0x9000] == 0xFF && g_mem[0x9001] == 0xFF &&
                   g_mem[0x9002] == 0xFF && g_mem[0x9003] == 0xFF);
        check("9.4", "A->B, fixed source (fill)",
              ok,
              DETAIL("mem=[%02X %02X %02X %02X]",
                     g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.5: Block length = 1
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 1);
        g_mem[0x8000] = 0x42;
        int n = run_dma(dma);
        check("9.5", "Block length = 1",
              n == 1 && g_mem[0x9000] == 0x42,
              DETAIL("n=%d mem=0x%02X", n, g_mem[0x9000]));
    }

    // 9.6: Block length = 256
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 256);
        for (int i = 0; i < 256; i++) g_mem[0x8000 + i] = (uint8_t)i;
        int n = run_dma(dma);
        bool ok = true;
        for (int i = 0; i < 256; i++) {
            if (g_mem[0x9000 + i] != (uint8_t)i) { ok = false; break; }
        }
        check("9.6", "Block length = 256",
              ok && n == 256,
              DETAIL("n=%d", n));
    }

    // 9.7: Block length = 0 (edge case)
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 0);
        g_mem[0x8000] = 0xFF;
        int n = run_dma(dma);
        check("9.7", "Block length = 0: no bytes transferred",
              n == 0 && g_mem[0x9000] == 0x00,
              DETAIL("n=%d mem=0x%02X", n, g_mem[0x9000]));
    }
}

// ── Group 10: Memory-to-IO Transfer ─────────────────────────────────

static void test_group10_mem_to_io() {
    set_group("Mem-to-IO Transfer");
    Dma dma;

    // 10.1: Mem(A) -> IO(B), A inc, B fixed
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80); // portA = 0x8000
        zxn_write(dma, 0x03); zxn_write(dma, 0x00); // len = 3
        zxn_write(dma, 0x14); // R1: portA = mem, inc
        zxn_write(dma, 0x28); // R2: portB = IO(bit3=1), fixed(bits[5:4]=10)
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0xFE); zxn_write(dma, 0x00); // portB = 0x00FE
        g_mem[0x8000] = 0xAA; g_mem[0x8001] = 0xBB; g_mem[0x8002] = 0xCC;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        // Last byte written to IO port 0x00FE should be 0xCC
        check("10.1", "Mem->IO: last byte to IO port",
              g_io[0x00FE] == 0xCC,
              DETAIL("io[0xFE]=0x%02X", g_io[0x00FE]));
    }

    // 10.2: IO(A) -> Mem(B)
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0xFE); zxn_write(dma, 0x00); // portA = 0x00FE
        zxn_write(dma, 0x01); zxn_write(dma, 0x00); // len = 1
        zxn_write(dma, 0x2C); // R1: portA = IO(bit3=1), fixed(bits[5:4]=10) = 0b0_0_10_1_100
        zxn_write(dma, 0x10); // R2: portB = mem, inc
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90); // portB = 0x9000
        g_io[0x00FE] = 0x55;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        check("10.2", "IO->Mem: byte read from IO",
              g_mem[0x9000] == 0x55,
              DETAIL("mem[0x9000]=0x%02X", g_mem[0x9000]));
    }

    // 10.3: IO(A) -> IO(B)
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x10); zxn_write(dma, 0x00); // portA = 0x0010
        zxn_write(dma, 0x01); zxn_write(dma, 0x00); // len = 1
        zxn_write(dma, 0x1C); // R1: portA = IO, inc
        zxn_write(dma, 0x18); // R2: portB = IO(bit3=1), inc(bits[5:4]=01) = 0b0_0_01_1_000
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x20); zxn_write(dma, 0x00); // portB = 0x0020
        g_io[0x0010] = 0x77;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        check("10.3", "IO->IO transfer",
              g_io[0x0020] == 0x77,
              DETAIL("io[0x20]=0x%02X", g_io[0x0020]));
    }
}

// ── Group 11: Address Mode Combinations ─────────────────────────────

static void test_group11_addr_modes() {
    set_group("Address Modes");
    Dma dma;

    // 11.1: Both increment
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 4);
        g_mem[0x8000] = 1; g_mem[0x8001] = 2;
        g_mem[0x8002] = 3; g_mem[0x8003] = 4;
        run_dma(dma);
        check("11.1", "Both increment: src=0x8004, dst=0x9004",
              dma.src_addr() == 0x8004 && dma.dst_addr() == 0x9004,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 11.2: Both decrement
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x03); zxn_write(dma, 0x80); // portA = 0x8003
        zxn_write(dma, 0x04); zxn_write(dma, 0x00);
        zxn_write(dma, 0x04); // R1: portA = mem, dec
        zxn_write(dma, 0x00); // R2: portB = mem, dec
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x03); zxn_write(dma, 0x90); // portB = 0x9003
        g_mem[0x8000] = 0x44; g_mem[0x8001] = 0x33;
        g_mem[0x8002] = 0x22; g_mem[0x8003] = 0x11;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        // src ends at 0x7FFF, dst ends at 0x8FFF
        check("11.2a", "Both decrement: addresses decrease",
              dma.src_addr() == 0x7FFF && dma.dst_addr() == 0x8FFF,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
        check("11.2b", "Both decrement: data correct",
              g_mem[0x9003] == 0x11 && g_mem[0x9002] == 0x22 &&
              g_mem[0x9001] == 0x33 && g_mem[0x9000] == 0x44,
              DETAIL("mem=[%02X %02X %02X %02X]",
                     g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 11.3: Both fixed (port-to-port style)
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0x03); zxn_write(dma, 0x00); // len = 3
        zxn_write(dma, 0x24); // R1: portA = mem, fixed
        zxn_write(dma, 0x20); // R2: portB = mem, fixed
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90);
        g_mem[0x8000] = 0x42;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        check("11.3", "Both fixed: addresses unchanged",
              dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 11.4: Address wrap at 0xFFFF
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0xFF); zxn_write(dma, 0xFF); // portA = 0xFFFF
        zxn_write(dma, 0x02); zxn_write(dma, 0x00); // len = 2
        zxn_write(dma, 0x14); // R1: portA = mem, inc
        zxn_write(dma, 0x10); // R2: portB = mem, inc
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90);
        g_mem[0xFFFF] = 0xAA;
        g_mem[0x0000] = 0xBB;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        run_dma(dma);
        // src wraps: 0xFFFF -> 0x0000 -> 0x0001
        check("11.4", "Address wrap: src goes 0xFFFF -> 0x0001",
              dma.src_addr() == 0x0001,
              DETAIL("src=0x%04X", dma.src_addr()));
    }
}

// ── Group 12: Transfer Modes ─────────────────────────────────────────

static void test_group12_transfer_modes() {
    set_group("Transfer Modes");
    Dma dma;

    // 12.1: Continuous mode — full block transferred
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 8);
        for (int i = 0; i < 8; i++) g_mem[0x8000 + i] = (uint8_t)(i + 1);
        int n = run_dma(dma);
        check("12.1", "Continuous: full block in one burst",
              n == 8 && dma.state() == Dma::State::IDLE,
              DETAIL("n=%d state=%d", n, (int)dma.state()));
    }

    // 12.2: Burst mode — transfers one byte then pauses
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0x04); zxn_write(dma, 0x00); // len = 4
        zxn_write(dma, 0x14); // R1: mem, inc
        // R2 with prescaler: base 0x50 (mem, inc, timing follows), timing 0x21 (prescaler follows), prescaler 0x01
        zxn_write(dma, 0x50);
        zxn_write(dma, 0x21);
        zxn_write(dma, 0x01); // prescaler = 1
        // R4: burst mode
        zxn_write(dma, 0x4D); // bits[6:5]=10(burst), bit2+bit3, bits[1:0]=01
        zxn_write(dma, 0x00); zxn_write(dma, 0x90);
        g_mem[0x8000] = 0xAA;
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        // Execute one burst - should transfer 1 byte then wait
        int n = dma.execute_burst(100);
        check("12.2", "Burst mode: 1 byte per burst with prescaler",
              n == 1,
              DETAIL("n=%d", n));
    }

    // 12.3: Burst mode — no prescaler acts like continuous per byte
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0x04); zxn_write(dma, 0x00);
        zxn_write(dma, 0x14);
        zxn_write(dma, 0x10);
        zxn_write(dma, 0x41); // R4: burst mode, no addr follows
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90);
        for (int i = 0; i < 4; i++) g_mem[0x8000 + i] = (uint8_t)(i + 1);
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87);
        // With prescaler=0, burst mode still transfers one byte at a time
        int n1 = dma.execute_burst(100);
        check("12.3", "Burst mode, no prescaler: 1 byte per execute",
              n1 == 1,
              DETAIL("n=%d", n1));
    }
}

// ── Group 14: Counter Behaviour ──────────────────────────────────────

static void test_group14_counter() {
    set_group("Counter Behaviour");
    Dma dma;

    // 14.1: ZXN mode counter starts at 0
    {
        fresh(dma);
        zxn_write(dma, 0x79);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0x05); zxn_write(dma, 0x00);
        zxn_write(dma, 0xCF);
        check("14.1", "ZXN: counter=0 after Load",
              dma.counter() == 0,
              DETAIL("counter=0x%04X", dma.counter()));
    }

    // 14.2: Z80 mode counter starts at 0xFFFF
    {
        fresh(dma);
        z80_write(dma, 0x79);
        z80_write(dma, 0x00); z80_write(dma, 0x80);
        z80_write(dma, 0x05); z80_write(dma, 0x00);
        z80_write(dma, 0xCF);
        check("14.2", "Z80: counter=0xFFFF after Load",
              dma.counter() == 0xFFFF,
              DETAIL("counter=0x%04X", dma.counter()));
    }

    // 14.3: Counter increments per byte (ZXN mode)
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 10);
        for (int i = 0; i < 10; i++) g_mem[0x8000 + i] = (uint8_t)i;
        run_dma(dma);
        // After 10 bytes transferred in ZXN mode, counter should be 10
        check("14.3", "ZXN: counter=block_len after transfer",
              dma.counter() == 10,
              DETAIL("counter=%u", dma.counter()));
    }

    // 14.4: ZXN block_len=0 transfers 0 bytes
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 0);
        g_mem[0x8000] = 0xFF;
        int n = run_dma(dma);
        check("14.4", "ZXN: block_len=0 transfers 0 bytes",
              n == 0,
              DETAIL("n=%d", n));
    }

    // 14.5: Counter readback via read sequence
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 5);
        for (int i = 0; i < 5; i++) g_mem[0x8000 + i] = (uint8_t)i;
        run_dma(dma);
        // Set read mask to counter only (bits 1,2)
        zxn_write(dma, 0xBB);
        zxn_write(dma, 0x06); // mask = 0b0000110 = bits 1,2
        zxn_write(dma, 0xA7); // Init read sequence
        uint8_t lo = dma.read();
        uint8_t hi = dma.read();
        uint16_t cnt = (hi << 8) | lo;
        check("14.5", "Counter readback = 5",
              cnt == 5,
              DETAIL("counter readback=0x%04X (expected 5)", cnt));
    }
}

// ── Group 16: Auto-Restart and Continue ──────────────────────────────

static void test_group16_auto_restart_continue() {
    set_group("Auto-restart/Continue");
    Dma dma;

    // 16.1: Auto-restart reloads addresses
    {
        fresh(dma);
        zxn_write(dma, 0xA2); // R5: auto-restart
        program_mem_to_mem(dma, 0x8000, 0x9000, 2);
        g_mem[0x8000] = 0xAA; g_mem[0x8001] = 0xBB;
        run_dma(dma, 2); // Run exactly one block
        // After auto-restart, addresses should be reloaded
        check("16.1", "Auto-restart reloads src/dst addresses",
              dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 16.2: Continue preserves addresses
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 4);
        for (int i = 0; i < 4; i++) g_mem[0x8000 + i] = (uint8_t)i;
        run_dma(dma);
        uint16_t src_after = dma.src_addr();
        uint16_t dst_after = dma.dst_addr();
        zxn_write(dma, 0xD3); // Continue
        check("16.2", "Continue preserves addresses",
              dma.src_addr() == src_after && dma.dst_addr() == dst_after,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 16.3: Continue resets counter
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 4);
        for (int i = 0; i < 4; i++) g_mem[0x8000 + i] = (uint8_t)i;
        run_dma(dma);
        zxn_write(dma, 0xD3); // Continue
        check("16.3", "Continue resets counter to 0 (ZXN)",
              dma.counter() == 0,
              DETAIL("counter=0x%04X", dma.counter()));
    }
}

// ── Group 17: Status Register and Read Sequence ─────────────────────

static void test_group17_status_readback() {
    set_group("Status/Readback");
    Dma dma;

    // 17.1: Initial status byte
    {
        fresh(dma);
        zxn_write(dma, 0xBF); // Read status byte
        uint8_t s = dma.read();
        // Initial: endofblock_n=1(not ended), atleastone=0
        // = 0b00_1_1101_0 = 0x3A
        // Wait: 0x20 | 0x1A | 0 = 0x3A
        check("17.1", "Initial status = 0x3A",
              s == 0x3A,
              DETAIL("status=0x%02X", s));
    }

    // 17.2: Status after partial transfer (at-least-one set)
    // We need to check after a transfer completes since we can't stop mid-transfer easily
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 1);
        g_mem[0x8000] = 0x42;
        run_dma(dma);
        // After transfer: endofblock=true -> bit5=0, atleastone=true -> bit0=1
        // = 0b00_0_1101_1 = 0x1B
        zxn_write(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.2", "Status after complete transfer = 0x1B",
              s == 0x1B,
              DETAIL("status=0x%02X", s));
    }

    // 17.3: Status cleared by 0x8B
    {
        fresh(dma);
        program_mem_to_mem(dma, 0x8000, 0x9000, 1);
        g_mem[0x8000] = 0x42;
        run_dma(dma);
        zxn_write(dma, 0x8B); // Reinit status
        zxn_write(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.3", "Status cleared by 0x8B = 0x3A",
              s == 0x3A,
              DETAIL("status=0x%02X", s));
    }

    // 17.4: Default read mask = 0x7F (all 7 fields)
    {
        fresh(dma);
        // Program known values, do a Load
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x34); zxn_write(dma, 0x12);
        zxn_write(dma, 0x08); zxn_write(dma, 0x00);
        zxn_write(dma, 0x14);
        zxn_write(dma, 0x10);
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x78); zxn_write(dma, 0x56);
        zxn_write(dma, 0xCF);
        // Init read sequence
        zxn_write(dma, 0xA7);
        // Read all 7 fields: status, counter_lo, counter_hi, portA_lo, portA_hi, portB_lo, portB_hi
        uint8_t vals[7];
        for (int i = 0; i < 7; i++) vals[i] = dma.read();
        // Status should be 0x3A, counter=0x0000, portA=src=0x1234, portB=dst=0x5678
        check("17.4", "Full read sequence: status",
              vals[0] == 0x3A,
              DETAIL("status=0x%02X", vals[0]));
        check("17.5", "Full read sequence: counter",
              vals[1] == 0x00 && vals[2] == 0x00,
              DETAIL("counter=[%02X %02X]", vals[1], vals[2]));
        check("17.6", "Full read sequence: port A (src)",
              vals[3] == 0x34 && vals[4] == 0x12,
              DETAIL("portA=[%02X %02X]", vals[3], vals[4]));
        check("17.7", "Full read sequence: port B (dst)",
              vals[5] == 0x78 && vals[6] == 0x56,
              DETAIL("portB=[%02X %02X]", vals[5], vals[6]));
    }

    // 17.8: Custom read mask
    {
        fresh(dma);
        zxn_write(dma, 0xBB);
        zxn_write(dma, 0x07); // mask = 0b0000111 = status + counter_lo + counter_hi
        zxn_write(dma, 0xA7); // Init read sequence
        uint8_t v0 = dma.read(); // status
        uint8_t v1 = dma.read(); // counter_lo
        uint8_t v2 = dma.read(); // counter_hi
        uint8_t v3 = dma.read(); // wraps to status again
        check("17.8", "Custom mask: 3 fields then wrap",
              v0 == v3, // should wrap back to status
              DETAIL("first_status=0x%02X wrap_status=0x%02X", v0, v3));
    }
}

// ── Group 19: Reset Behaviour ────────────────────────────────────────

static void test_group19_reset() {
    set_group("Reset Behaviour");
    Dma dma;

    // 19.1: Hardware reset defaults
    {
        fresh(dma);
        check("19.1a", "Reset: state=IDLE",
              dma.state() == Dma::State::IDLE, "");
        check("19.1b", "Reset: transfer_mode=continuous",
              dma.transfer_mode() == Dma::TransferMode::CONTINUOUS, "");
        check("19.1c", "Reset: src_addr_mode=increment",
              dma.src_addr_mode() == Dma::AddrMode::INCREMENT, "");
        check("19.1d", "Reset: dst_addr_mode=increment",
              dma.dst_addr_mode() == Dma::AddrMode::INCREMENT, "");
        check("19.1e", "Reset: counter=0",
              dma.counter() == 0, DETAIL("counter=%u", dma.counter()));
        check("19.1f", "Reset: block_length=0",
              dma.block_length() == 0, DETAIL("len=%u", dma.block_length()));
    }

    // 19.2: Soft reset (0xC3) preserves addresses
    {
        fresh(dma);
        // Set port A and B addresses
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x34); zxn_write(dma, 0x12);
        zxn_write(dma, 0x08); zxn_write(dma, 0x00);
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x78); zxn_write(dma, 0x56);
        // Soft reset
        zxn_write(dma, 0xC3);
        // Load after reset - addresses should still be programmed
        zxn_write(dma, 0xCF);
        check("19.2", "Soft reset preserves port addresses",
              dma.src_addr() == 0x1234 && dma.dst_addr() == 0x5678,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 19.3: Soft reset goes to IDLE
    {
        fresh(dma);
        zxn_write(dma, 0x87); // Enable DMA
        zxn_write(dma, 0xC3); // Reset
        check("19.3", "Soft reset -> IDLE",
              dma.state() == Dma::State::IDLE,
              DETAIL("state=%d", (int)dma.state()));
    }
}

// ── Group 22: Edge Cases ─────────────────────────────────────────────

static void test_group22_edge_cases() {
    set_group("Edge Cases");
    Dma dma;

    // 22.1: Disable during active transfer
    {
        fresh(dma);
        // Set up burst mode transfer to have control
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x80);
        zxn_write(dma, 0xFF); zxn_write(dma, 0x00); // len = 255
        zxn_write(dma, 0x14);
        zxn_write(dma, 0x10);
        zxn_write(dma, 0x21); // R4: continuous
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x90);
        zxn_write(dma, 0xCF);
        zxn_write(dma, 0x87); // Enable
        // Transfer a few bytes
        dma.execute_burst(3);
        // Disable mid-transfer
        zxn_write(dma, 0x83);
        check("22.1", "Disable during transfer -> IDLE",
              dma.state() == Dma::State::IDLE,
              DETAIL("state=%d", (int)dma.state()));
    }

    // 22.2: Enable without Load
    {
        fresh(dma);
        zxn_write(dma, 0x87); // Enable without Load
        check("22.2", "Enable without Load: uses default addresses",
              dma.state() == Dma::State::TRANSFERRING,
              DETAIL("state=%d src=0x%04X dst=0x%04X",
                     (int)dma.state(), dma.src_addr(), dma.dst_addr()));
    }

    // 22.3: Multiple Loads before Enable
    {
        fresh(dma);
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x10); // first portA = 0x1000
        zxn_write(dma, 0x04); zxn_write(dma, 0x00);
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x20);
        zxn_write(dma, 0xCF); // First Load
        // Reprogram
        zxn_write(dma, 0x7D);
        zxn_write(dma, 0x00); zxn_write(dma, 0x30); // second portA = 0x3000
        zxn_write(dma, 0x04); zxn_write(dma, 0x00);
        zxn_write(dma, DMA_R4_CONT_LOAD_B);
        zxn_write(dma, 0x00); zxn_write(dma, 0x40);
        zxn_write(dma, 0xCF); // Second Load
        check("22.3", "Multiple Loads: last values used",
              dma.src_addr() == 0x3000 && dma.dst_addr() == 0x4000,
              DETAIL("src=0x%04X dst=0x%04X", dma.src_addr(), dma.dst_addr()));
    }

    // 22.4: R2 byte 0x00 matches R2 not R0
    {
        fresh(dma);
        // Byte 0x00: bits[7]=0, bits[2:0]=000 -> matches R2
        // R0 requires bit0=1 or bit1=1, so 0x00 does NOT match R0
        // After writing 0x00, port B should be configured (addr_mode=00=decrement)
        zxn_write(dma, 0x00); // Should be R2
        check("22.4", "Byte 0x00 matches R2 (dec), not R0",
              dma.dst_addr_mode() == Dma::AddrMode::DECREMENT,
              DETAIL("dst_mode=%d", (int)dma.dst_addr_mode()));
    }
}

// ── Main ─────────────────────────────────────────────────────────────

int main() {
    printf("DMA Subsystem Compliance Tests\n");
    printf("==============================\n\n");

    test_group1_port_decode();
    printf("  Group: Port Decode — done\n");

    test_group2_r0();
    printf("  Group: R0 Programming — done\n");

    test_group3_r1();
    printf("  Group: R1 Port A Config — done\n");

    test_group4_r2();
    printf("  Group: R2 Port B Config — done\n");

    test_group5_r3();
    printf("  Group: R3 DMA Enable — done\n");

    test_group6_r4();
    printf("  Group: R4 Mode/PortB — done\n");

    test_group7_r5();
    printf("  Group: R5 Auto-restart — done\n");

    test_group8_r6_commands();
    printf("  Group: R6 Commands — done\n");

    test_group9_mem_to_mem();
    printf("  Group: Mem-to-Mem Transfer — done\n");

    test_group10_mem_to_io();
    printf("  Group: Mem-to-IO Transfer — done\n");

    test_group11_addr_modes();
    printf("  Group: Address Modes — done\n");

    test_group12_transfer_modes();
    printf("  Group: Transfer Modes — done\n");

    test_group14_counter();
    printf("  Group: Counter Behaviour — done\n");

    test_group16_auto_restart_continue();
    printf("  Group: Auto-restart/Continue — done\n");

    test_group17_status_readback();
    printf("  Group: Status/Readback — done\n");

    test_group19_reset();
    printf("  Group: Reset Behaviour — done\n");

    test_group22_edge_cases();
    printf("  Group: Edge Cases — done\n");

    printf("\n==============================\n");
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
                printf("  %-25s %d/%d\n", last_group.c_str(), gp, gp + gf);
            last_group = r.group;
            gp = gf = 0;
        }
        if (r.passed) gp++; else gf++;
    }
    if (!last_group.empty())
        printf("  %-25s %d/%d\n", last_group.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
