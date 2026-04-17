// DMA Subsystem Compliance Test Runner (Phase 2 per-row rewrite)
//
// Expected values are read from the ZXN FPGA VHDL sources at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/device/dma.vhd
// and cross-checked with the relevant wiring in zxnext.vhd.  The C++
// implementation is never used as an oracle.
//
// One section per plan row from doc/testing/DMA-TEST-PLAN-DESIGN.md (156
// rows across 22 groups).  Every live row is either a single check(id, ...)
// or a skip(id, ...) with a one-line reason.
//
// Run: ./build/test/dma_test

#include "peripheral/dma.h"

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

// ── Test infrastructure ───────────────────────────────────────────────

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

struct SkipNote {
    std::string id;
    std::string reason;
};
std::vector<SkipNote> g_skipped;

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

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
}

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

// ── Mock memory and I/O ──────────────────────────────────────────────

std::array<uint8_t, 65536> g_mem;
std::array<uint8_t, 65536> g_io;

void clear_mem() { g_mem.fill(0); }
void clear_io()  { g_io.fill(0);  }

void attach_callbacks(Dma& dma) {
    dma.read_memory  = [](uint16_t a) -> uint8_t { return g_mem[a]; };
    dma.write_memory = [](uint16_t a, uint8_t v) { g_mem[a] = v; };
    dma.read_io      = [](uint16_t p) -> uint8_t { return g_io[p]; };
    dma.write_io     = [](uint16_t p, uint8_t v) { g_io[p] = v; };
}

void fresh(Dma& dma) {
    dma.reset();
    clear_mem();
    clear_io();
    attach_callbacks(dma);
}

// ── DMA port write helpers (VHDL zxnext.vhd port decode: 0x6B = ZXN,
//    0x0B = Z80-DMA-compat; dma_mode_i latched per access) ─────────────

void zxn(Dma& dma, uint8_t v) { dma.write(v, false); }
void z80(Dma& dma, uint8_t v) { dma.write(v, true);  }

// Run DMA to completion.
int run_to_idle(Dma& dma, int cap = 65536) {
    int total = 0;
    while (dma.state() == Dma::State::TRANSFERRING && total < cap) {
        int n = dma.execute_burst(cap - total);
        if (n == 0) break;
        total += n;
    }
    return total;
}

// Program a standard A->B mem/inc + mem/inc transfer via the register
// protocol, matching the VHDL decode order (R0, R1, R2, R4, LOAD, ENABLE).
// - R0 base 0x7D = 0b0111_1101:
//     bit0=1 (R0 discriminator), bit2=1 (A->B),
//     bit3..bit6=1 (portA LO/HI + len LO/HI sub-bytes follow).
//     VHDL decode: dma.vhd:518-538 (dir + sub-byte gating).
// - R1 base 0x14 = 0b0001_0100:
//     bits[2:0]=100 (R1 id, dma.vhd:542 select),
//     bit3=0 (mem), bits[5:4]=01 (inc), bit6=0 (no timing follow).
// - R2 base 0x10 = 0b0001_0000:
//     bits[2:0]=000 (R2 id, dma.vhd:559 select),
//     bit3=0 (mem), bits[5:4]=01 (inc), bit6=0 (no timing follow).
// - R4 base 0xAD = 0b1010_1101:
//     bit7=1, bits[1:0]=01 (R4 id, dma.vhd:601 select),
//     bits[6:5]=01 (continuous mode), bit2=1 (portB LO), bit3=1 (portB HI).
void program_mem_to_mem_AB(Dma& dma, uint16_t src, uint16_t dst, uint16_t len,
                           bool z80_mode = false) {
    auto wr = z80_mode ? z80 : zxn;
    wr(dma, 0x7D);
    wr(dma, src & 0xFF);
    wr(dma, (src >> 8) & 0xFF);
    wr(dma, len & 0xFF);
    wr(dma, (len >> 8) & 0xFF);
    wr(dma, 0x14);
    wr(dma, 0x10);
    wr(dma, 0xAD);
    wr(dma, dst & 0xFF);
    wr(dma, (dst >> 8) & 0xFF);
    wr(dma, 0xCF);  // LOAD (dma.vhd:654-668)
    wr(dma, 0x87);  // ENABLE (dma.vhd:725)
}

// ══════════════════════════════════════════════════════════════════════
// Group 1 — Port decoding and mode selection
// VHDL: zxnext.vhd port decode sets dma_mode <= port_0b_lsb per access.
// Observed by the Z80-vs-ZXN counter reset value at LOAD
// (dma.vhd:664-668, :673-676, :482-486).
// ══════════════════════════════════════════════════════════════════════

void group1_port_decode() {
    set_group("G1 Port Decode");
    Dma dma;

    // 1.1 Write to port 0x6B -> ZXN mode (counter=0 at LOAD).
    {
        fresh(dma);
        zxn(dma, 0x05);  // R0 dir A->B, no sub-bytes (bit0=1, bit2=1)
        zxn(dma, 0xCF);  // LOAD
        check("1.1", "Write 0x6B latches ZXN mode: counter=0 on LOAD",
              dma.counter() == 0,
              fmt("counter=0x%04X  VHDL dma.vhd:664-665", dma.counter()));
    }

    // 1.2 Write to port 0x0B -> Z80 mode (counter=0xFFFF at LOAD).
    {
        fresh(dma);
        z80(dma, 0x05);
        z80(dma, 0xCF);
        check("1.2", "Write 0x0B latches Z80 mode: counter=0xFFFF on LOAD",
              dma.counter() == 0xFFFF,
              fmt("counter=0x%04X  VHDL dma.vhd:666-667", dma.counter()));
    }

    // 1.3 Read from 0x6B -> ZXN mode.  The C++ read() path latches the
    // mode via the same write(bool) parameter through a preceding access;
    // we model it by doing a Z80 LOAD, then a ZXN CONTINUE which the VHDL
    // treats as a mode latch on port access (dma.vhd:670-676).
    {
        fresh(dma);
        z80(dma, 0x05);
        z80(dma, 0xCF);  // counter = 0xFFFF
        zxn(dma, 0xD3);  // CONTINUE with ZXN mode -> counter = 0
        check("1.3", "Subsequent 0x6B access latches ZXN: CONTINUE counter=0",
              dma.counter() == 0,
              fmt("counter=0x%04X  VHDL dma.vhd:673-674", dma.counter()));
    }

    // 1.4 Read from 0x0B -> Z80 mode.
    {
        fresh(dma);
        zxn(dma, 0x05);
        zxn(dma, 0xCF);  // ZXN LOAD -> counter=0
        z80(dma, 0xD3);  // Z80 CONTINUE -> counter=0xFFFF
        check("1.4", "Subsequent 0x0B access latches Z80: CONTINUE counter=0xFFFF",
              dma.counter() == 0xFFFF,
              fmt("counter=0x%04X  VHDL dma.vhd:675-676", dma.counter()));
    }

    // 1.5 — REMOVED (redundant with 1.1/1.2).  dma_mode_i is re-latched on
    // every port access in VHDL dma.vhd:213-242; the "default after reset"
    // check would just re-test 1.1 (ZXN LOAD yields counter=0) with no
    // additional signal.  No independent observable, no test value.

    // 1.6 Mode switches per access: alternate ZXN and Z80 LOADs.
    {
        fresh(dma);
        zxn(dma, 0x05); zxn(dma, 0xCF);
        bool zxn_ok = (dma.counter() == 0);
        z80(dma, 0xCF);  // Z80 LOAD re-latches mode, counter -> 0xFFFF
        bool z80_ok = (dma.counter() == 0xFFFF);
        check("1.6", "Mode re-latched on each port access",
              zxn_ok && z80_ok,
              fmt("zxn_load=%d z80_load=%d  VHDL dma.vhd:664-668",
                  (int)zxn_ok, (int)z80_ok));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 2 — R0 (direction, port A address, block length)
// VHDL dma.vhd:518-538 (R0 decode + sub-byte gating), :739-772 (sub-bytes).
// R0 base byte: bit7=0, (bit0=1 OR bit1=1) discriminator; bit2=dir (1=A->B);
// bits[6:3]=follow-up mask (addrLO, addrHI, lenLO, lenHI).
// ══════════════════════════════════════════════════════════════════════

void group2_r0() {
    set_group("G2 R0 programming");
    Dma dma;

    // 2.1 Direction A->B: after LOAD, src=portA, dst=portB.
    // VHDL dma.vhd:656-658 (cmd_load AtoB).
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x10);  // portA = 0x1000
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x20);  // portB = 0x2000
        zxn(dma, 0xCF);
        check("2.1", "R0 bit2=1 dir A->B: src=portA, dst=portB",
              dma.src_addr() == 0x1000 && dma.dst_addr() == 0x2000,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:656-658",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 2.2 Direction B->A: R0 bit2=0 -> src=portB, dst=portA.
    // VHDL dma.vhd:659-662.
    {
        fresh(dma);
        zxn(dma, 0x79);                  // bit2=0
        zxn(dma, 0x00); zxn(dma, 0x10);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x20);
        zxn(dma, 0xCF);
        check("2.2", "R0 bit2=0 dir B->A: src=portB, dst=portA",
              dma.src_addr() == 0x2000 && dma.dst_addr() == 0x1000,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:659-662",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 2.3 Port A start address low byte alone (R0 bit3=1 only).
    // VHDL dma.vhd:739 R0_start_addr_port_A_s(7 downto 0).
    {
        fresh(dma);
        zxn(dma, 0x0D);      // bit0=1, bit2=1, bit3=1
        zxn(dma, 0xAB);
        zxn(dma, 0xCF);
        check("2.3", "R0 addr LO sub-byte only",
              (dma.src_addr() & 0x00FF) == 0xAB,
              fmt("src=0x%04X  VHDL dma.vhd:739", dma.src_addr()));
    }

    // 2.4 Port A start address high byte (bit3+bit4).
    // VHDL dma.vhd:752 R0_start_addr_port_A_s(15 downto 8).
    {
        fresh(dma);
        zxn(dma, 0x1D);      // bit3=1, bit4=1
        zxn(dma, 0x34);
        zxn(dma, 0x12);
        zxn(dma, 0xCF);
        check("2.4", "R0 addr HI sub-byte",
              dma.src_addr() == 0x1234,
              fmt("src=0x%04X  VHDL dma.vhd:752", dma.src_addr()));
    }

    // 2.5 Full 16-bit port A address (same base 0x7D as 2.1; the all-bytes
    // path exercises addr LO + HI in one write sequence).
    // VHDL dma.vhd:739 + :752.
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x78); zxn(dma, 0x56);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0xCF);
        check("2.5", "R0 full 16-bit port A address",
              dma.src_addr() == 0x5678,
              fmt("src=0x%04X  VHDL dma.vhd:739,752", dma.src_addr()));
    }

    // 2.6 Block length low byte (bit5=1).
    // VHDL dma.vhd:763 R0_block_len_s(7 downto 0).
    {
        fresh(dma);
        zxn(dma, 0x25);
        zxn(dma, 0x10);
        zxn(dma, 0xCF);
        check("2.6", "R0 block length LO sub-byte",
              (dma.block_length() & 0x00FF) == 0x10,
              fmt("block_len=%u  VHDL dma.vhd:763", dma.block_length()));
    }

    // 2.7 Block length high byte (bit5+bit6).
    // VHDL dma.vhd:772 R0_block_len_s(15 downto 8).
    {
        fresh(dma);
        zxn(dma, 0x65);
        zxn(dma, 0x00);
        zxn(dma, 0x01);
        zxn(dma, 0xCF);
        check("2.7", "R0 block length HI sub-byte",
              dma.block_length() == 0x0100,
              fmt("block_len=0x%04X  VHDL dma.vhd:772", dma.block_length()));
    }

    // 2.8 Selective programming: write full R0, then re-write only addr LO;
    // addr HI and block length must be preserved (VHDL parses only the
    // sub-bytes selected by the current base byte — dma.vhd:518-538).
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0xEF); zxn(dma, 0xBE);
        zxn(dma, 0x20); zxn(dma, 0x00);
        zxn(dma, 0x0D);
        zxn(dma, 0x42);
        zxn(dma, 0xCF);
        check("2.8", "R0 selective re-program: only addr LO updated",
              dma.src_addr() == 0xBE42 && dma.block_length() == 0x0020,
              fmt("src=0x%04X len=0x%04X  VHDL dma.vhd:518-538",
                  dma.src_addr(), dma.block_length()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 3 — R1 (port A configuration)
// VHDL dma.vhd:542-545 (R1_portAisIO_s, R1_portA_addrMode_s), :776 (timing).
// R1 base: bits[2:0]=100, bit3=isIO, bits[5:4]=addrMode, bit6=timing follow.
// Observability: port_a_is_io not exposed; verified by transfer side-effect.
// addr mode exposed through src_addr_mode() (when dir A->B).
// timing byte not observable (no cycle counter).
// ══════════════════════════════════════════════════════════════════════

void group3_r1() {
    set_group("G3 R1 Port A");
    Dma dma;

    // 3.1 Port A is memory (default after R1 write with bit3=0).
    // Verified by A->B transfer reading from g_mem (not g_io).
    // VHDL dma.vhd:542 R1_portAisIO_s = cpu_d_i(3).
    {
        fresh(dma);
        g_mem[0x8000] = 0x55;
        g_io[0x8000]  = 0xAA;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);
        check("3.1", "R1 bit3=0: portA reads memory",
              g_mem[0x9000] == 0x55,
              fmt("mem[0x9000]=0x%02X  VHDL dma.vhd:542", g_mem[0x9000]));
    }

    // 3.2 Port A is I/O: 0x1C = 0b0001_1100 (bit3=1 IO, bits[5:4]=01 inc).
    // Source should come from g_io on an A->B transfer.
    {
        fresh(dma);
        g_io[0x0030] = 0x5A;
        zxn(dma, 0x7D);
        zxn(dma, 0x30); zxn(dma, 0x00);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x1C);                  // R1 bit3=1 IO
        zxn(dma, 0x10);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("3.2", "R1 bit3=1: portA reads I/O",
              g_mem[0x9000] == 0x5A,
              fmt("mem[0x9000]=0x%02X  VHDL dma.vhd:542", g_mem[0x9000]));
    }

    // 3.3 Port A addr increment (bits[5:4]=01).  R1 base 0x14.
    // VHDL dma.vhd:543 R1_portA_addrMode_s = cpu_d_i(5 downto 4).
    {
        fresh(dma);
        zxn(dma, 0x14);
        check("3.3", "R1 addr mode 01 = increment",
              dma.src_addr_mode() == Dma::AddrMode::INCREMENT,
              fmt("mode=%d  VHDL dma.vhd:543", (int)dma.src_addr_mode()));
    }

    // 3.4 Port A addr decrement (bits[5:4]=00).  R1 base 0x04.
    {
        fresh(dma);
        zxn(dma, 0x04);
        check("3.4", "R1 addr mode 00 = decrement",
              dma.src_addr_mode() == Dma::AddrMode::DECREMENT,
              fmt("mode=%d  VHDL dma.vhd:543", (int)dma.src_addr_mode()));
    }

    // 3.5 Port A addr fixed (bits[5:4]=10 or 11).  R1 base 0x24.
    {
        fresh(dma);
        zxn(dma, 0x24);
        check("3.5", "R1 addr mode 10 = fixed",
              dma.src_addr_mode() == Dma::AddrMode::FIXED,
              fmt("mode=%d  VHDL dma.vhd:543", (int)dma.src_addr_mode()));
    }

    // 3.6 R1 timing byte programmable via R1 sub-byte (bit6=1 makes a
    // timing byte follow).  VHDL dma.vhd:776 / :312-317.  Write timing "10"
    // (fastest) via R1 base 0x54 (bit2=1 R1, bit4=0 dec, bit6=1 timing), then
    // the timing sub-byte with bits[1:0]=10.
    {
        fresh(dma);
        zxn(dma, 0x54);                  // R1: addr mode 00, timing follows
        zxn(dma, 0x02);                  // timing sub-byte: 10 (fastest 2-cyc)
        check("3.6", "R1 timing byte stored (00/01/10/11)",
              dma.port_a_timing() == 0x02,
              fmt("port_a_timing=0x%02X  VHDL dma.vhd:776",
                  dma.port_a_timing()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 4 — R2 (port B configuration)
// VHDL dma.vhd:559-561 (R2_portBisIO_s, R2_portB_addrMode_s), :790 (timing),
// :799 (prescaler).  R2 base: bits[2:0]=000, bit3=isIO, bits[5:4]=addrMode,
// bit6=timing follow; within timing byte, bit5=prescaler follow.
// ══════════════════════════════════════════════════════════════════════

void group4_r2() {
    set_group("G4 R2 Port B");
    Dma dma;

    // 4.1 Port B is memory (A->B dest).  Transfer writes memory.
    // VHDL dma.vhd:559.
    {
        fresh(dma);
        g_mem[0x8000] = 0x77;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);
        check("4.1", "R2 bit3=0: portB writes memory",
              g_mem[0x9000] == 0x77,
              fmt("mem[0x9000]=0x%02X  VHDL dma.vhd:559", g_mem[0x9000]));
    }

    // 4.2 Port B is I/O (bit3=1).  R2 base 0x28 = 0b0010_1000
    // (fixed mode to avoid incrementing the I/O port).
    {
        fresh(dma);
        g_mem[0x8000] = 0xA5;
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x28);                  // R2 bit3=1 IO, fixed
        zxn(dma, 0xAD);
        zxn(dma, 0x55); zxn(dma, 0x00);  // portB = 0x0055
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("4.2", "R2 bit3=1: portB writes I/O",
              g_io[0x0055] == 0xA5,
              fmt("io[0x55]=0x%02X  VHDL dma.vhd:559", g_io[0x0055]));
    }

    // 4.3 Port B addr increment (bits[5:4]=01).
    // VHDL dma.vhd:560.
    {
        fresh(dma);
        zxn(dma, 0x10);
        check("4.3", "R2 addr mode 01 = increment",
              dma.dst_addr_mode() == Dma::AddrMode::INCREMENT,
              fmt("mode=%d  VHDL dma.vhd:560", (int)dma.dst_addr_mode()));
    }

    // 4.4 Port B addr decrement (bits[5:4]=00).
    {
        fresh(dma);
        zxn(dma, 0x00);
        check("4.4", "R2 addr mode 00 = decrement",
              dma.dst_addr_mode() == Dma::AddrMode::DECREMENT,
              fmt("mode=%d  VHDL dma.vhd:560", (int)dma.dst_addr_mode()));
    }

    // 4.5 Port B addr fixed (bits[5:4]=10).
    {
        fresh(dma);
        zxn(dma, 0x20);
        check("4.5", "R2 addr mode 10 = fixed",
              dma.dst_addr_mode() == Dma::AddrMode::FIXED,
              fmt("mode=%d  VHDL dma.vhd:560", (int)dma.dst_addr_mode()));
    }

    // 4.6 R2 timing byte programmable via R2 sub-byte (bit6=1 makes a
    // timing byte follow).  VHDL dma.vhd:790 / :319-324.  Write timing "10"
    // via R2 base 0x40 (bits[2:0]=000 R2, bit6=1 timing follow), then the
    // timing sub-byte with bits[1:0]=10.
    {
        fresh(dma);
        zxn(dma, 0x40);                  // R2: timing follow
        zxn(dma, 0x02);                  // timing sub-byte: 10
        check("4.6", "R2 timing byte stored (00/01/10/11)",
              dma.port_b_timing() == 0x02,
              fmt("port_b_timing=0x%02X  VHDL dma.vhd:790",
                  dma.port_b_timing()));
    }

    // 4.7 R2 prescaler byte is programmable but only observable through
    // burst-mode wait behaviour (exercised in 12.3).  The setter path itself
    // has no direct accessor — we verify that programming the sequence does
    // not derail later state (sub-byte state machine returns to IDLE and a
    // subsequent transfer still runs).
    // VHDL dma.vhd:799.
    {
        fresh(dma);
        zxn(dma, 0x50);                  // R2 with timing follow (bit6=1)
        zxn(dma, 0x21);                  // timing byte with prescaler follow (bit5=1)
        zxn(dma, 0x0A);                  // prescaler = 0x0A
        // Follow with a successful 1-byte transfer to prove the sequencer
        // is back in IDLE and accepting new base bytes.
        g_mem[0x8000] = 0x3C;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);
        check("4.7", "R2 prescaler sub-byte consumed; sequencer returns to IDLE",
              g_mem[0x9000] == 0x3C,
              fmt("mem[0x9000]=0x%02X  VHDL dma.vhd:799", g_mem[0x9000]));
    }

    // 4.8 Prescaler = 0 (the reset default): continuous mode must not
    // enter WAITING_CYCLES.  Observable via is_active() staying asserted
    // until the block finishes in one execute_burst() call.
    // VHDL dma.vhd:424 gate `(R2_portB_preescaler_s > 0) AND ...`.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        int n = dma.execute_burst(1000);
        check("4.8", "Prescaler=0 default: full block in one burst (no wait)",
              n == 4 && dma.state() == Dma::State::IDLE,
              fmt("n=%d state=%d  VHDL dma.vhd:424",
                  n, (int)dma.state()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 5 — R3 (DMA enable)
// VHDL dma.vhd:576-582.  R3 base: bit7=1, bits[1:0]=00; bit6 triggers
// START_DMA; bit3/bit4 gate mask/match sub-bytes (commented out in VHDL
// but still consumed by the sequencer).
// ══════════════════════════════════════════════════════════════════════

void group5_r3() {
    set_group("G5 R3 Enable");
    Dma dma;

    // 5.1 R3 bit6=1 starts DMA.  Base 0xC0 = 0b1100_0000.
    {
        fresh(dma);
        zxn(dma, 0xC0);
        check("5.1", "R3 bit6=1 -> TRANSFERRING",
              dma.state() == Dma::State::TRANSFERRING,
              fmt("state=%d  VHDL dma.vhd:576-579", (int)dma.state()));
    }

    // 5.2 R3 bit6=0 leaves DMA idle.  Base 0x80.
    {
        fresh(dma);
        zxn(dma, 0x80);
        check("5.2", "R3 bit6=0 -> IDLE",
              dma.state() == Dma::State::IDLE,
              fmt("state=%d  VHDL dma.vhd:576", (int)dma.state()));
    }

    // 5.3 R3 mask byte (bit3=1 follow-up).  Base 0x88.  Observable: after
    // the mask is consumed, another R0 write must still land correctly.
    {
        fresh(dma);
        zxn(dma, 0x88);
        zxn(dma, 0xFF);                  // mask byte
        zxn(dma, 0x0D);                  // R0 addr LO follow-up
        zxn(dma, 0x42);
        zxn(dma, 0xCF);
        check("5.3", "R3 mask sub-byte consumed; subsequent R0 still parsed",
              (dma.src_addr() & 0x00FF) == 0x42,
              fmt("src=0x%04X  VHDL dma.vhd:576-582 (mask commented)",
                  dma.src_addr()));
    }

    // 5.4 R3 match byte (bit3+bit4).  Base 0x98.
    {
        fresh(dma);
        zxn(dma, 0x98);
        zxn(dma, 0xFF);                  // mask
        zxn(dma, 0xAA);                  // match
        zxn(dma, 0x0D);
        zxn(dma, 0x24);
        zxn(dma, 0xCF);
        check("5.4", "R3 match sub-byte consumed; subsequent R0 still parsed",
              (dma.src_addr() & 0x00FF) == 0x24,
              fmt("src=0x%04X  VHDL dma.vhd:576-582 (match commented)",
                  dma.src_addr()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 6 — R4 (transfer mode, port B address)
// VHDL dma.vhd:601-603 (R4_mode_s, R4_start_addr_port_B_s decode),
// :816 (portB LO), :827 (portB HI), :236 (reset default "01" continuous).
// R4 base: bit7=1, bits[1:0]=01, bits[6:5]=mode, bit2=addrLO follow,
// bit3=addrHI follow.
// ══════════════════════════════════════════════════════════════════════

void group6_r4() {
    set_group("G6 R4 Mode/PortB");
    Dma dma;

    // 6.1 Byte mode: R4_mode="00".  Base 0x81.
    {
        fresh(dma);
        zxn(dma, 0x81);
        check("6.1", "R4 mode 00 = byte",
              dma.transfer_mode() == Dma::TransferMode::BYTE,
              fmt("mode=%d  VHDL dma.vhd:601", (int)dma.transfer_mode()));
    }

    // 6.2 Continuous mode: R4_mode="01".  Base 0xA1.
    {
        fresh(dma);
        zxn(dma, 0xA1);
        check("6.2", "R4 mode 01 = continuous",
              dma.transfer_mode() == Dma::TransferMode::CONTINUOUS,
              fmt("mode=%d  VHDL dma.vhd:601", (int)dma.transfer_mode()));
    }

    // 6.3 Burst mode: R4_mode="10".  Base 0xC1.
    {
        fresh(dma);
        zxn(dma, 0xC1);
        check("6.3", "R4 mode 10 = burst",
              dma.transfer_mode() == Dma::TransferMode::BURST,
              fmt("mode=%d  VHDL dma.vhd:601", (int)dma.transfer_mode()));
    }

    // 6.4 Default R4_mode is "01" (continuous) after reset.
    // VHDL dma.vhd:236.
    {
        fresh(dma);
        check("6.4", "Reset default R4 mode = continuous",
              dma.transfer_mode() == Dma::TransferMode::CONTINUOUS,
              fmt("mode=%d  VHDL dma.vhd:236", (int)dma.transfer_mode()));
    }

    // 6.5 Port B start address LO byte (bit2=1 follow-up).  Base 0x85.
    // VHDL dma.vhd:816.
    {
        fresh(dma);
        zxn(dma, 0x7D);                  // establish A->B dir
        zxn(dma, 0x00); zxn(dma, 0x00);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x85);                  // R4 byte w/ portB LO follow
        zxn(dma, 0x34);
        zxn(dma, 0xCF);
        check("6.5", "R4 portB addr LO sub-byte",
              (dma.dst_addr() & 0x00FF) == 0x34,
              fmt("dst=0x%04X  VHDL dma.vhd:816", dma.dst_addr()));
    }

    // 6.6 Port B start address HI byte (bit3=1 follow-up after LO).
    // VHDL dma.vhd:827.
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x00);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x8D);                  // R4 with both portB sub-bytes
        zxn(dma, 0x34);
        zxn(dma, 0x12);
        zxn(dma, 0xCF);
        check("6.6", "R4 portB addr HI sub-byte",
              dma.dst_addr() == 0x1234,
              fmt("dst=0x%04X  VHDL dma.vhd:827", dma.dst_addr()));
    }

    // 6.7 Full 16-bit port B address (same mechanism combined).
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x00);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x78); zxn(dma, 0x56);
        zxn(dma, 0xCF);
        check("6.7", "R4 full 16-bit port B address",
              dma.dst_addr() == 0x5678,
              fmt("dst=0x%04X  VHDL dma.vhd:816,827", dma.dst_addr()));
    }

    // 6.8 Mode "11" has no VHDL branch: dma.vhd:601 assigns cpu_d_i(6:5)
    // straight into R4_mode_s so the TransferMode enum ends up with value 3
    // (outside {BYTE, CONTINUOUS, BURST}).  The plan row describes this as
    // "no special case" — the live observation is simply that the stored
    // 2-bit value is "11".  The public transfer_mode() accessor returns a
    // TransferMode cast, which for value 3 is an out-of-range enum.  Test
    // as (int) raw to document the behaviour.
    {
        fresh(dma);
        zxn(dma, 0xE1);                  // R4 base + mode=11
        int raw = static_cast<int>(dma.transfer_mode());
        check("6.8", "R4 mode 11 stored as raw value 3 (no VHDL special case)",
              raw == 3,
              fmt("mode_raw=%d  VHDL dma.vhd:601", raw));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 7 — R5 (auto-restart, CE/WAIT)
// VHDL dma.vhd:622-623 (R5 decode), :237-238 (reset defaults), :473 (auto
// restart reload in FINISH_DMA).  R5 base: bits[7:6]=10, bits[2:0]=010;
// bit4=ce_wait, bit5=auto_restart.
// No public accessor for R5 fields; observable only via transfer outcome.
// ══════════════════════════════════════════════════════════════════════

void group7_r5() {
    set_group("G7 R5 Restart/CE");
    Dma dma;

    // 7.1 Auto-restart enabled: after the block completes, FINISH_DMA
    // reloads addresses and re-enters START_DMA (dma.vhd:473-491).
    // Observable: after one full block, state is still TRANSFERRING and
    // src/dst have been reloaded to their programmed start values.
    {
        fresh(dma);
        zxn(dma, 0xA2);                  // R5 bit5=1 auto-restart
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xA0 + i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 2);
        dma.execute_burst(2);            // transfer exactly one block
        check("7.1", "R5 auto-restart: state=TRANSFERRING and addrs reloaded",
              dma.state() == Dma::State::TRANSFERRING &&
              dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000,
              fmt("state=%d src=0x%04X dst=0x%04X  VHDL dma.vhd:473-491",
                  (int)dma.state(), dma.src_addr(), dma.dst_addr()));
    }

    // 7.2 Auto-restart disabled (reset default): after the block, state=IDLE.
    // VHDL dma.vhd:238, :494.
    {
        fresh(dma);
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 2);
        run_to_idle(dma);
        check("7.2", "R5 auto-restart off (default): state=IDLE after block",
              dma.state() == Dma::State::IDLE,
              fmt("state=%d  VHDL dma.vhd:238,494", (int)dma.state()));
    }

    // 7.3 — DEFERRED (not a skip).  VHDL dma.vhd:622 assigns R5_ce_wait_s
    // but the consuming branches at dma.vhd:341, :409 are commented out in
    // the VHDL source.  When/if those branches are re-enabled, this test
    // comes back:
    //   {
    //       fresh(dma);
    //       zxn(dma, 0x92);                  // R5 with ce_wait=1
    //       check("7.3", "R5 ce_wait bit stored",
    //             /* would need a ce_wait() accessor */);
    //   }

    // 7.4 — DEFERRED (not a skip).  R5 reset defaults: ce_wait=0 +
    // auto_restart=0.  The auto_restart reset is covered by 7.2 (behavioural
    // proof).  ce_wait has no behavioural consequence (see 7.3), so checking
    // its reset value is field-initialisation busywork.  Keep deferred until
    // 7.3 is implementable.
}

// ══════════════════════════════════════════════════════════════════════
// Group 8 — R6 commands
// VHDL dma.vhd:628-733 (R6 command decode) and :859-861 (R6_BYTE_0 for
// read mask follow-up).
// ══════════════════════════════════════════════════════════════════════

void group8_r6_commands() {
    set_group("G8 R6 Commands");
    Dma dma;

    // 8.1 0xC3 RESET -> IDLE and defaults.  VHDL dma.vhd:638-645.
    {
        fresh(dma);
        zxn(dma, 0x87);                  // ENABLE first
        zxn(dma, 0xA2);                  // R5 auto-restart on
        zxn(dma, 0xC3);                  // RESET
        check("8.1", "0xC3 RESET: state=IDLE",
              dma.state() == Dma::State::IDLE,
              fmt("state=%d  VHDL dma.vhd:638", (int)dma.state()));
    }

    // 8.2 0xC7 Reset port A timing to "01".  VHDL dma.vhd:648.
    {
        fresh(dma);
        zxn(dma, 0x54); zxn(dma, 0x02);  // program port A timing = 10
        zxn(dma, 0xC7);                  // reset port A timing
        check("8.2", "0xC7 resets port A timing to 01",
              dma.port_a_timing() == 0x01,
              fmt("port_a_timing=0x%02X  VHDL dma.vhd:648",
                  dma.port_a_timing()));
    }

    // 8.3 0xCB Reset port B timing to "01".  VHDL dma.vhd:651.
    {
        fresh(dma);
        zxn(dma, 0x40); zxn(dma, 0x02);  // program port B timing = 10
        zxn(dma, 0xCB);                  // reset port B timing
        check("8.3", "0xCB resets port B timing to 01",
              dma.port_b_timing() == 0x01,
              fmt("port_b_timing=0x%02X  VHDL dma.vhd:651",
                  dma.port_b_timing()));
    }

    // 8.4 0xCF LOAD clears status_endofblock_n.  VHDL dma.vhd:654.
    // Observable via status byte read (bit5=endofblock_n).
    {
        fresh(dma);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);                // sets end-of-block
        zxn(dma, 0xCF);                  // LOAD again
        zxn(dma, 0xBF);                  // read status next
        uint8_t s = dma.read();
        check("8.4", "LOAD clears status_endofblock_n (bit5=1)",
              (s & 0x20) == 0x20,
              fmt("status=0x%02X  VHDL dma.vhd:654", s));
    }

    // 8.5 LOAD A->B: src=portA, dst=portB.  VHDL dma.vhd:656-658.
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x34); zxn(dma, 0x12);
        zxn(dma, 0x08); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x78); zxn(dma, 0x56);
        zxn(dma, 0xCF);
        check("8.5", "LOAD A->B: src=0x1234, dst=0x5678",
              dma.src_addr() == 0x1234 && dma.dst_addr() == 0x5678,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:656-658",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 8.6 LOAD B->A: src=portB, dst=portA.  VHDL dma.vhd:660-662.
    {
        fresh(dma);
        zxn(dma, 0x79);                  // bit2=0 -> B->A
        zxn(dma, 0x34); zxn(dma, 0x12);
        zxn(dma, 0x08); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x78); zxn(dma, 0x56);
        zxn(dma, 0xCF);
        check("8.6", "LOAD B->A: src=0x5678, dst=0x1234",
              dma.src_addr() == 0x5678 && dma.dst_addr() == 0x1234,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:660-662",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 8.7 LOAD in ZXN mode: counter=0.  VHDL dma.vhd:664-665.
    {
        fresh(dma);
        zxn(dma, 0x05);
        zxn(dma, 0xCF);
        check("8.7", "LOAD ZXN: counter=0",
              dma.counter() == 0,
              fmt("counter=0x%04X  VHDL dma.vhd:664-665", dma.counter()));
    }

    // 8.8 LOAD in Z80 mode: counter=0xFFFF.  VHDL dma.vhd:666-667.
    {
        fresh(dma);
        z80(dma, 0x05);
        z80(dma, 0xCF);
        check("8.8", "LOAD Z80: counter=0xFFFF",
              dma.counter() == 0xFFFF,
              fmt("counter=0x%04X  VHDL dma.vhd:666-667", dma.counter()));
    }

    // 8.9 0xD3 CONTINUE resets counter, keeps addresses.  VHDL dma.vhd:670-676.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        run_to_idle(dma);
        uint16_t src_after = dma.src_addr();
        uint16_t dst_after = dma.dst_addr();
        zxn(dma, 0xD3);
        check("8.9", "CONTINUE: counter reset, addrs preserved",
              dma.counter() == 0 &&
              dma.src_addr() == src_after && dma.dst_addr() == dst_after,
              fmt("counter=%u src=0x%04X dst=0x%04X  VHDL dma.vhd:670-676",
                  dma.counter(), dma.src_addr(), dma.dst_addr()));
    }

    // 8.10 CONTINUE ZXN -> counter=0.  VHDL dma.vhd:673-674.
    {
        fresh(dma);
        zxn(dma, 0x05); zxn(dma, 0xCF);
        zxn(dma, 0xD3);
        check("8.10", "CONTINUE ZXN: counter=0",
              dma.counter() == 0,
              fmt("counter=0x%04X  VHDL dma.vhd:673-674", dma.counter()));
    }

    // 8.11 CONTINUE Z80 -> counter=0xFFFF.  VHDL dma.vhd:675-676.
    {
        fresh(dma);
        z80(dma, 0x05); z80(dma, 0xCF);
        z80(dma, 0xD3);
        check("8.11", "CONTINUE Z80: counter=0xFFFF",
              dma.counter() == 0xFFFF,
              fmt("counter=0x%04X  VHDL dma.vhd:675-676", dma.counter()));
    }

    // 8.12 0x87 ENABLE -> TRANSFERRING.  VHDL dma.vhd:725.
    {
        fresh(dma);
        zxn(dma, 0x87);
        check("8.12", "ENABLE -> TRANSFERRING",
              dma.state() == Dma::State::TRANSFERRING,
              fmt("state=%d  VHDL dma.vhd:725", (int)dma.state()));
    }

    // 8.13 0x83 DISABLE -> IDLE.  VHDL dma.vhd:728.
    {
        fresh(dma);
        zxn(dma, 0x87);
        zxn(dma, 0x83);
        check("8.13", "DISABLE -> IDLE",
              dma.state() == Dma::State::IDLE,
              fmt("state=%d  VHDL dma.vhd:728", (int)dma.state()));
    }

    // 8.14 0x8B Reinitialize status byte.  VHDL dma.vhd:691-692.
    // After a transfer, status bits 5 and 0 should both be cleared:
    //   endofblock_n=1 (bit5=1), atleastone=0 (bit0=0), fixed middle=1101.
    // Status byte = 0b00_1_1101_0 = 0x3A.
    {
        fresh(dma);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        g_mem[0x8000] = 0x11;
        run_to_idle(dma);
        zxn(dma, 0x8B);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("8.14", "0x8B status reinit: byte = 0x3A",
              s == 0x3A,
              fmt("status=0x%02X  VHDL dma.vhd:691-692,902", s));
    }

    // 8.15 0xBB Read mask follows: writes next byte into R6_read_mask_s.
    // VHDL dma.vhd:731 + :859-860.  Observable via the read sequence
    // advancing differently.  Set mask = 0x01 (status only) and confirm
    // repeated reads always return the status byte.
    {
        fresh(dma);
        zxn(dma, 0xBB);
        zxn(dma, 0x01);                  // mask = bit0 only (status)
        zxn(dma, 0xA7);                  // init read sequence
        uint8_t s1 = dma.read();
        uint8_t s2 = dma.read();
        check("8.15", "0xBB mask=0x01: read sequence locked to status",
              s1 == 0x3A && s2 == 0x3A,
              fmt("s1=0x%02X s2=0x%02X  VHDL dma.vhd:731,859-860",
                  s1, s2));
    }

    // 8.16 0xBF Read status byte: sets reg_rd_seq_s := RD_STATUS, so the
    // very next dma.read() returns the status byte regardless of where
    // the read sequence previously was.  VHDL dma.vhd:696-699.
    {
        fresh(dma);
        // Walk the read pointer off of STATUS first.
        zxn(dma, 0xA7);
        dma.read();                      // consumes STATUS, advances
        zxn(dma, 0xBF);                  // force next read = status
        uint8_t s = dma.read();
        check("8.16", "0xBF forces next read = status byte",
              s == 0x3A,
              fmt("status=0x%02X  VHDL dma.vhd:696-699", s));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 9 — Memory-to-memory transfer
// VHDL dma.vhd:359-396 (address update), :424-434 (block-length check).
// ══════════════════════════════════════════════════════════════════════

void group9_mem_to_mem() {
    set_group("G9 Mem-to-Mem");
    Dma dma;

    // 9.1 A->B, both increment, 4 bytes.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x10 + i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        run_to_idle(dma);
        bool ok = g_mem[0x9000] == 0x10 && g_mem[0x9001] == 0x11 &&
                  g_mem[0x9002] == 0x12 && g_mem[0x9003] == 0x13;
        check("9.1", "A->B inc both, 4 bytes copied in order",
              ok,
              fmt("dst=[%02X %02X %02X %02X]  VHDL dma.vhd:379-391",
                  g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.2 B->A, both increment.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xA0 + i);
        zxn(dma, 0x79);                  // B->A
        zxn(dma, 0x00); zxn(dma, 0x90);  // portA = 0x9000
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14); zxn(dma, 0x10);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x80);  // portB = 0x8000
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        bool ok = g_mem[0x9000] == 0xA0 && g_mem[0x9001] == 0xA1 &&
                  g_mem[0x9002] == 0xA2 && g_mem[0x9003] == 0xA3;
        check("9.2", "B->A inc both, 4 bytes copied portB->portA",
              ok,
              fmt("dst=[%02X %02X %02X %02X]  VHDL dma.vhd:660-662,389-391",
                  g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.3 A->B, decrement source (R1 bits[5:4]=00).
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x10 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x03); zxn(dma, 0x80);  // portA = 0x8003
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x04);                  // R1 portA dec
        zxn(dma, 0x10);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        bool ok = g_mem[0x9000] == 0x13 && g_mem[0x9001] == 0x12 &&
                  g_mem[0x9002] == 0x11 && g_mem[0x9003] == 0x10;
        check("9.3", "A->B src decrement: reads walk backwards",
              ok,
              fmt("dst=[%02X %02X %02X %02X]  VHDL dma.vhd:384-387",
                  g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.4 A->B, fixed source (fill).
    {
        fresh(dma);
        g_mem[0x8000] = 0x5A;
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x24);                  // R1 portA fixed
        zxn(dma, 0x10);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        bool ok = g_mem[0x9000] == 0x5A && g_mem[0x9001] == 0x5A &&
                  g_mem[0x9002] == 0x5A && g_mem[0x9003] == 0x5A;
        check("9.4", "A->B fixed src: identical bytes written N times",
              ok,
              fmt("dst=[%02X %02X %02X %02X]  VHDL dma.vhd:379-396 (no update path)",
                  g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.5 A->B, fixed destination (probe).
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xC0 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x20);                  // R2 portB fixed
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("9.5", "A->B fixed dst: last byte remains in single slot",
              g_mem[0x9000] == 0xC3 && g_mem[0x9001] == 0x00 &&
              g_mem[0x9002] == 0x00 && g_mem[0x9003] == 0x00,
              fmt("dst=[%02X %02X %02X %02X]  VHDL dma.vhd:389-396 (no update path)",
                  g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 9.6 Block length = 1 transfers exactly 1 byte in ZXN mode.
    // VHDL dma.vhd:426 counter<block_len check.
    {
        fresh(dma);
        g_mem[0x8000] = 0x77;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        int n = run_to_idle(dma);
        check("9.6", "Block length = 1 transfers 1 byte (ZXN)",
              n == 1 && g_mem[0x9000] == 0x77 && g_mem[0x9001] == 0x00,
              fmt("n=%d dst[0]=0x%02X dst[1]=0x%02X  VHDL dma.vhd:426",
                  n, g_mem[0x9000], g_mem[0x9001]));
    }

    // 9.7 Block length = 256 transfers 256 bytes.
    {
        fresh(dma);
        for (int i = 0; i < 256; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 256);
        int n = run_to_idle(dma);
        bool ok = (n == 256);
        for (int i = 0; i < 256 && ok; ++i) ok = g_mem[0x9000 + i] == static_cast<uint8_t>(i);
        check("9.7", "Block length = 256 transfers 256 bytes",
              ok,
              fmt("n=%d  VHDL dma.vhd:426", n));
    }

    // 9.8 Block length = 0 edge case — ZXN.  VHDL state machine at
    // dma.vhd:359-437 transfers the byte at TRANSFERING_WRITE_1 (:361
    // increments counter), then checks `counter_s < block_len_s` at
    // TRANSFERING_WRITE_4 (:426).  With block_len=0, ZXN counter starts
    // at 0 (LOAD, :665), first byte transfers, counter becomes 1,
    // 1 < 0 is false (unsigned) -> FINISH_DMA.  One byte is transferred.
    // The VHDL author's own "TO DO" comment at :426 flags this edge case.
    {
        fresh(dma);
        g_mem[0x8000] = 0xEE;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 0);
        int n = run_to_idle(dma);
        check("9.8", "Block length = 0 transfers 1 byte (ZXN)",
              n == 1 && g_mem[0x9000] == 0xEE,
              fmt("n=%d dst=0x%02X  VHDL dma.vhd:361,426 (counter "
                  "incremented at WRITE_1, checked at WRITE_4)",
                  n, g_mem[0x9000]));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 10 — Memory-to-IO transfer
// VHDL dma.vhd:290-296 (source bus signals), :351-354 (dest bus signals),
// :530-543 (emulator read/write callback selection).
// ══════════════════════════════════════════════════════════════════════

void group10_mem_to_io() {
    set_group("G10 Mem-to-IO");
    Dma dma;

    // 10.1 Mem(A) -> IO(B), A inc, B fixed.
    {
        fresh(dma);
        for (int i = 0; i < 3; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x40 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x03); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x28);                  // R2 IO, fixed
        zxn(dma, 0xAD);
        zxn(dma, 0xFE); zxn(dma, 0x00);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("10.1", "Mem->IO A inc, B fixed: last byte at fixed IO port",
              g_io[0x00FE] == 0x42,
              fmt("io[0xFE]=0x%02X  VHDL dma.vhd:559 (portB IO)",
                  g_io[0x00FE]));
    }

    // 10.2 Mem(A) -> IO(B), both inc: consecutive IO ports receive bytes.
    {
        fresh(dma);
        for (int i = 0; i < 3; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x60 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x03); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x18);                  // R2 IO, inc
        zxn(dma, 0xAD);
        zxn(dma, 0x20); zxn(dma, 0x00);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("10.2", "Mem->IO A inc, B inc: 3 consecutive IO ports written",
              g_io[0x0020] == 0x60 && g_io[0x0021] == 0x61 &&
              g_io[0x0022] == 0x62,
              fmt("io[0x20..22]=[%02X %02X %02X]  VHDL dma.vhd:559,389-391",
                  g_io[0x0020], g_io[0x0021], g_io[0x0022]));
    }

    // 10.3 MREQ on read / IORQ on write, per VHDL dma.vhd:186-190:
    //   dma_mreq_n_o asserts on memory phases (src mem on read, dst mem on
    //   write); dma_iorq_n_o asserts on IO phases.  The C++ emulator does
    //   not expose raw bus signals, but the phase-specific access type is
    //   observable via which callback is invoked (read_memory vs read_io,
    //   write_memory vs write_io).  This IS the VHDL-observable fact.
    {
        fresh(dma);
        int mem_reads = 0, io_reads = 0, mem_writes = 0, io_writes = 0;
        dma.read_memory  = [&](uint16_t a) -> uint8_t { ++mem_reads;  return g_mem[a]; };
        dma.read_io      = [&](uint16_t p) -> uint8_t { ++io_reads;   return g_io[p];  };
        dma.write_memory = [&](uint16_t a, uint8_t v) { ++mem_writes; g_mem[a] = v;    };
        dma.write_io     = [&](uint16_t p, uint8_t v) { ++io_writes;  g_io[p]  = v;    };
        g_mem[0x8000] = 0xAA;
        // A->B, A=mem (MREQ on read), B=I/O (IORQ on write).
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x14);                  // R1 mem, inc
        zxn(dma, 0x28);                  // R2 IO, fixed
        zxn(dma, 0xAD);
        zxn(dma, 0xFE); zxn(dma, 0x00);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("10.3",
              "Mem->IO: read phase asserts MREQ (mem callback), "
              "write phase asserts IORQ (io callback)",
              mem_reads == 1 && io_reads == 0 &&
              mem_writes == 0 && io_writes == 1,
              fmt("mem_rd=%d io_rd=%d mem_wr=%d io_wr=%d  "
                  "VHDL dma.vhd:186-190,290-296",
                  mem_reads, io_reads, mem_writes, io_writes));
    }

    // 10.4 IO(A) -> Mem(B): portA is IO, portB is mem.
    {
        fresh(dma);
        g_io[0x00FE] = 0x99;
        zxn(dma, 0x7D);
        zxn(dma, 0xFE); zxn(dma, 0x00);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x2C);                  // R1 portA IO fixed
        zxn(dma, 0x10);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("10.4", "IO->Mem: byte arrives from IO to memory",
              g_mem[0x9000] == 0x99,
              fmt("mem[0x9000]=0x%02X  VHDL dma.vhd:542 (portA IO)",
                  g_mem[0x9000]));
    }

    // 10.5 IO(A) -> IO(B): both ports IO.
    {
        fresh(dma);
        g_io[0x0010] = 0x66;
        zxn(dma, 0x7D);
        zxn(dma, 0x10); zxn(dma, 0x00);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x1C);                  // R1 portA IO inc
        zxn(dma, 0x18);                  // R2 portB IO inc
        zxn(dma, 0xAD);
        zxn(dma, 0x20); zxn(dma, 0x00);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("10.5", "IO->IO: single byte arrives at destination IO port",
              g_io[0x0020] == 0x66,
              fmt("io[0x20]=0x%02X  VHDL dma.vhd:542,559", g_io[0x0020]));
    }

    // 10.6 Port B full 16-bit address on bus: dma_a_o is 16 bits (dma.vhd:
    // 36) so an IO write to an address > 0xFF must deliver the byte to the
    // full 16-bit port number in the callback.
    {
        fresh(dma);
        g_mem[0x8000] = 0x3C;
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x28);                  // R2 IO fixed
        zxn(dma, 0xAD);
        zxn(dma, 0x34); zxn(dma, 0x12);  // portB = 0x1234 (16-bit)
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("10.6", "IO port B delivered with full 16-bit address",
              g_io[0x1234] == 0x3C,
              fmt("io[0x1234]=0x%02X  VHDL dma.vhd:36 (dma_a_o 15:0)",
                  g_io[0x1234]));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 11 — Address mode combinations
// VHDL dma.vhd:379-396 (four parallel if-blocks for src/dst inc/dec).
// ══════════════════════════════════════════════════════════════════════

void group11_addr_modes() {
    set_group("G11 Addr Modes");
    Dma dma;

    // 11.1 Both increment A->B.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        run_to_idle(dma);
        check("11.1", "Both inc A->B: src=0x8004 dst=0x9004 after 4 bytes",
              dma.src_addr() == 0x8004 && dma.dst_addr() == 0x9004,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:379-391",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 11.2 Both decrement A->B.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        zxn(dma, 0x7D);
        zxn(dma, 0x03); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x04);                  // R1 portA dec
        zxn(dma, 0x00);                  // R2 portB dec
        zxn(dma, 0xAD);
        zxn(dma, 0x03); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("11.2", "Both dec A->B: src=0x7FFF dst=0x8FFF after 4 bytes",
              dma.src_addr() == 0x7FFF && dma.dst_addr() == 0x8FFF,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:384-396",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 11.3 Source inc, destination dec.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x10 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x00);                  // R2 portB dec
        zxn(dma, 0xAD);
        zxn(dma, 0x03); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("11.3", "Src inc, dst dec: dst walks backwards while src ascends",
              g_mem[0x9003] == 0x10 && g_mem[0x9002] == 0x11 &&
              g_mem[0x9001] == 0x12 && g_mem[0x9000] == 0x13,
              fmt("dst=[%02X %02X %02X %02X]  VHDL dma.vhd:379-396",
                  g_mem[0x9000], g_mem[0x9001], g_mem[0x9002], g_mem[0x9003]));
    }

    // 11.4 Source dec, destination fixed.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x20 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x03); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x04);                  // R1 portA dec
        zxn(dma, 0x20);                  // R2 portB fixed
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("11.4", "Src dec, dst fixed: dst=0x9000 holds last source byte (0x20)",
              dma.src_addr() == 0x7FFF && dma.dst_addr() == 0x9000 &&
              g_mem[0x9000] == 0x20 && g_mem[0x9001] == 0x00,
              fmt("src=0x%04X dst=0x%04X dst[0]=0x%02X  VHDL dma.vhd:384-396",
                  dma.src_addr(), dma.dst_addr(), g_mem[0x9000]));
    }

    // 11.5 Both fixed (port-to-port style).
    {
        fresh(dma);
        g_mem[0x8000] = 0x7E;
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x03); zxn(dma, 0x00);
        zxn(dma, 0x24);
        zxn(dma, 0x20);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("11.5", "Both fixed: addresses unchanged after transfer",
              dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:379-396 (no branch taken)",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 11.6 Address wrap at 0xFFFF: the address bus is 16 bits (dma.vhd:36
    // `dma_a_o : out 16`), so src increment from 0xFFFF produces 0x0000.
    {
        fresh(dma);
        g_mem[0xFFFF] = 0xAA;
        g_mem[0x0000] = 0xBB;
        zxn(dma, 0x7D);
        zxn(dma, 0xFF); zxn(dma, 0xFF);
        zxn(dma, 0x02); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x10);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        run_to_idle(dma);
        check("11.6", "Src wraps 0xFFFF -> 0x0000 (16-bit address)",
              g_mem[0x9000] == 0xAA && g_mem[0x9001] == 0xBB &&
              dma.src_addr() == 0x0001,
              fmt("dst=[%02X %02X] src=0x%04X  VHDL dma.vhd:36,381",
                  g_mem[0x9000], g_mem[0x9001], dma.src_addr()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 12 — Transfer modes
// VHDL dma.vhd:420-434 (post-byte branching), :439-464 (WAITING_CYCLES),
// :236 (default mode), :601 (mode decode).
// Continuous mode holds the bus; burst mode releases it between prescaled
// bytes.  Observable in C++ via execute_burst() byte counts and is_active().
// ══════════════════════════════════════════════════════════════════════

void group12_transfer_modes() {
    set_group("G12 Xfer Modes");
    Dma dma;

    // 12.1 Continuous, full block in one run.
    {
        fresh(dma);
        for (int i = 0; i < 8; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i + 1);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 8);
        int n = dma.execute_burst(1000);
        check("12.1", "Continuous: whole block in one execute_burst",
              n == 8 && dma.state() == Dma::State::IDLE,
              fmt("n=%d state=%d  VHDL dma.vhd:426-430,601 mode=01",
                  n, (int)dma.state()));
    }

    // 12.2 Burst with no prescaler: dma.vhd:581-586 shows burst still
    // breaks after each byte via `break` in C++.  VHDL equivalent: the
    // prescaler path at :424 is bypassed when prescaler=0, so it loops
    // through START_DMA each byte but does not enter WAITING_CYCLES.
    // Observable: execute_burst returns 1 byte at a time in burst+prescaler=0.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x10);
        zxn(dma, 0xCD);                  // R4 burst + portB sub-bytes
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        int n1 = dma.execute_burst(1000);
        check("12.2", "Burst prescaler=0: 1 byte per execute_burst",
              n1 == 1,
              fmt("n1=%d  VHDL dma.vhd:424 (prescaler>0 gate false)", n1));
    }

    // 12.3 Burst with prescaler > 0: enters WAITING_CYCLES after each byte,
    // burst_wait_ set on the C++ side, is_active() returns false.
    // VHDL dma.vhd:424-425 WAITING_CYCLES entry.
    {
        fresh(dma);
        g_mem[0x8000] = 0x12;
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x50);                  // R2 with timing follow
        zxn(dma, 0x21);                  // timing w/ prescaler follow
        zxn(dma, 0x08);                  // prescaler = 8
        zxn(dma, 0xCD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        dma.execute_burst(1000);
        check("12.3", "Burst prescaler>0: is_active() false during wait",
              dma.is_active() == false && dma.state() == Dma::State::TRANSFERRING,
              fmt("is_active=%d state=%d  VHDL dma.vhd:424-425",
                  (int)dma.is_active(), (int)dma.state()));
    }

    // 12.4 Burst mode bus-release timing: `cpu_busreq_n_s <= '1'` in
    // WAITING_CYCLES (dma.vhd:445).  With a burst-mode prescaler wait, the
    // DMA drops BUSREQ so the CPU can run.
    {
        fresh(dma);
        g_mem[0x8000] = 0x12;
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x50);                  // R2 timing follow
        zxn(dma, 0x21);                  // timing + prescaler follow
        zxn(dma, 0x08);                  // prescaler = 8
        zxn(dma, 0xCD);                  // R4 burst + portB
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        dma.execute_burst(1000);
        check("12.4", "Burst WAITING_CYCLES: cpu_busreq_n deasserted (bus released)",
              dma.cpu_busreq_n() == true,
              fmt("cpu_busreq_n=%d  VHDL dma.vhd:445",
                  (int)dma.cpu_busreq_n()));
    }

    // 12.5 After the prescaler wait expires, the DMA re-requests the bus
    // (VHDL dma.vhd:451-460 — WAITING_CYCLES returns to START_DMA which
    // re-asserts cpu_busreq_n=0).
    {
        fresh(dma);
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x40 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x02); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x50);
        zxn(dma, 0x21);
        zxn(dma, 0x01);                  // prescaler = 1
        zxn(dma, 0xCD);
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        dma.execute_burst(1000);         // one byte then wait (bus released)
        dma.set_turbo(0);                // +8 per clock
        dma.tick_burst_wait(8);          // timer reaches 64 > prescaler*32
        check("12.5", "After prescaler expires: cpu_busreq_n re-asserted",
              dma.cpu_busreq_n() == false,
              fmt("cpu_busreq_n=%d  VHDL dma.vhd:451-460",
                  (int)dma.cpu_busreq_n()));
    }

    // 12.6 R4 mode "00" (byte mode) — VHDL dma.vhd:426 is not gated by
    // R4_mode_s: the block-length check runs in every mode, so the DMA
    // transfers the full block regardless of mode selection. The Z80-DMA
    // external data sheet defines byte mode as "one byte per enable", but
    // the jnext VHDL does not implement per-enable gating. The test
    // asserts the VHDL-observable fact (full block transferred), not the
    // Z80-DMA data sheet. Task 3 audit reclassified the prior `n==1`
    // oracle as a plan misreading, not an emulator bug; Task 2 backlog
    // item 6a (DMA byte-mode gate) is removed accordingly.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xA0 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x10);
        zxn(dma, 0x8D);                  // R4 byte mode + portB follow
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        int n = dma.execute_burst(1000);
        check("12.6",
              "R4 mode=00 transfers full block_len bytes "
              "(VHDL dma.vhd:426 block-length check is mode-agnostic)",
              n == 4,
              fmt("n=%d VHDL dma.vhd:426", n));
    }

    // 12.7 Continuous mode still respects the prescaler between bytes.
    // VHDL dma.vhd:424 is inside the TRANSFERING_WRITE_4 path, not gated
    // by mode, so WAITING_CYCLES fires in continuous mode too.
    {
        fresh(dma);
        // Source data starts at 0xA0 (non-zero) so written-byte counting works
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xA0 + i);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x80);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0x14);
        zxn(dma, 0x50);                  // R2 timing follow
        zxn(dma, 0x21);                  // timing + prescaler follow
        zxn(dma, 0x04);                  // prescaler = 4
        zxn(dma, 0xAD);                  // continuous + portB
        zxn(dma, 0x00); zxn(dma, 0x90);
        zxn(dma, 0xCF); zxn(dma, 0x87);
        // VHDL TRANSFERING_WRITE_4 -> WAITING_CYCLES gate at dma.vhd:424 is
        // NOT guarded by R4_mode_s, so continuous mode pauses between bytes
        // when a prescaler is set.  Expected: after one execute_burst,
        // exactly 1 byte has been written and DMA is still TRANSFERRING
        // (waiting for prescaler to expire before next byte).
        dma.execute_burst(1000);
        int written = 0;
        for (int i = 0; i < 4; ++i) if (g_mem[0x9000 + i] != 0) ++written;
        check("12.7", "Continuous+prescaler: one byte then wait (TRANSFERRING)",
              written == 1 && dma.state() == Dma::State::TRANSFERRING,
              fmt("written=%d state=%d  VHDL dma.vhd:424",
                  written, (int)dma.state()));
    }

    // 12.8 Prescaler vs timer comparison with CPU speed scaling.
    // VHDL dma.vhd:424,444 compare `R2_portB_preescaler_s > DMA_timer_s(13:5)`
    // where the timer increments by 8/4/2/1 per clock at 3.5/7/14/28 MHz.
    // Observable: at 28 MHz (turbo=11), 8x more master clocks are needed
    // to advance the comparison value than at 3.5 MHz.
    {
        Dma d1, d2;
        fresh(d1);
        fresh(d2);
        d1.set_turbo(0);                 // 3.5 MHz: +8/clock
        d2.set_turbo(3);                 // 28 MHz:  +1/clock
        d1.tick_burst_wait(4);
        d2.tick_burst_wait(32);
        check("12.8",
              "Prescaler vs timer scales with turbo_i (8x clocks at 28MHz)",
              d1.dma_timer_hi9() == d2.dma_timer_hi9() &&
                  d1.dma_timer_hi9() == 1,
              fmt("t1_hi9=%u t2_hi9=%u  VHDL dma.vhd:424",
                  d1.dma_timer_hi9(), d2.dma_timer_hi9()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 13 — Prescaler and timing
// All VHDL references point at the 14-bit DMA_timer_s (dma.vhd:109-159)
// and the prescaler comparison `(0 & preescaler) > timer(13:5)` at :424.
// The C++ implementation approximates this with `burst_wait_ = prescaler*32`
// (dma.cpp:582-583), but the timer is not reset/exposed and cycle counts
// are not comparable.  All rows describing exact wait-cycle counts are
// unreachable.
// ══════════════════════════════════════════════════════════════════════

void group13_prescaler_timing() {
    set_group("G13 Prescaler/Timing");
    Dma dma;

    // 13.1 Prescaler = 0 bypasses WAITING_CYCLES.  Already exercised at 4.8
    // with block-granularity.  Here we assert is_active() stays true until
    // the block ends in continuous mode.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        bool before = dma.is_active();
        dma.execute_burst(1000);
        check("13.1", "Prescaler=0: no WAITING_CYCLES (runs to IDLE)",
              before && dma.state() == Dma::State::IDLE,
              fmt("before=%d state=%d  VHDL dma.vhd:424",
                  (int)before, (int)dma.state()));
    }

    // 13.2 At turbo=00 (3.5 MHz), DMA_timer_s increments by 8 per clock.
    // VHDL dma.vhd:251.  Starting from 0, N clocks yield timer = 8*N.
    {
        fresh(dma);
        dma.set_turbo(0);
        dma.tick_burst_wait(10);
        check("13.2", "turbo=00 (3.5MHz): timer += 8 per clock",
              dma.dma_timer() == 80,
              fmt("timer=%u (expected 80)  VHDL dma.vhd:251",
                  dma.dma_timer()));
    }

    // 13.3 turbo=01 (7 MHz): +4 per clock.  VHDL dma.vhd:252.
    {
        fresh(dma);
        dma.set_turbo(1);
        dma.tick_burst_wait(10);
        check("13.3", "turbo=01 (7MHz): timer += 4 per clock",
              dma.dma_timer() == 40,
              fmt("timer=%u (expected 40)  VHDL dma.vhd:252",
                  dma.dma_timer()));
    }

    // 13.4 turbo=10 (14 MHz): +2 per clock.  VHDL dma.vhd:253.
    {
        fresh(dma);
        dma.set_turbo(2);
        dma.tick_burst_wait(10);
        check("13.4", "turbo=10 (14MHz): timer += 2 per clock",
              dma.dma_timer() == 20,
              fmt("timer=%u (expected 20)  VHDL dma.vhd:253",
                  dma.dma_timer()));
    }

    // 13.5 turbo=11 (28 MHz): +1 per clock.  VHDL dma.vhd:254.
    {
        fresh(dma);
        dma.set_turbo(3);
        dma.tick_burst_wait(10);
        check("13.5", "turbo=11 (28MHz): timer += 1 per clock",
              dma.dma_timer() == 10,
              fmt("timer=%u (expected 10)  VHDL dma.vhd:254",
                  dma.dma_timer()));
    }

    // 13.6 Prescaler is compared against DMA_timer_s bits(13:5), so the
    // comparison granularity is 32 timer units.  VHDL dma.vhd:424.
    {
        fresh(dma);
        // timer = 32 -> hi9 = 1.  At prescaler=1, 1 > 1 is false (gate open).
        dma.set_turbo(3);                // +1 per clock
        dma.tick_burst_wait(32);
        uint16_t hi9 = dma.dma_timer_hi9();
        check("13.6", "Prescaler comparison uses timer bits(13:5)",
              dma.dma_timer() == 32 && hi9 == 1,
              fmt("timer=%u hi9=%u  VHDL dma.vhd:424",
                  dma.dma_timer(), hi9));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 14 — Counter behaviour (ZXN vs Z80)
// VHDL dma.vhd:361 counter increment, :426 block-length check,
// :664-667 LOAD initial value by mode.
// ══════════════════════════════════════════════════════════════════════

void group14_counter() {
    set_group("G14 Counter");
    Dma dma;

    // 14.1 ZXN: counter starts at 0 after LOAD.
    {
        fresh(dma);
        zxn(dma, 0x05); zxn(dma, 0xCF);
        check("14.1", "ZXN LOAD: counter=0",
              dma.counter() == 0,
              fmt("counter=0x%04X  VHDL dma.vhd:664-665", dma.counter()));
    }

    // 14.2 Z80: counter starts at 0xFFFF after LOAD.
    {
        fresh(dma);
        z80(dma, 0x05); z80(dma, 0xCF);
        check("14.2", "Z80 LOAD: counter=0xFFFF",
              dma.counter() == 0xFFFF,
              fmt("counter=0x%04X  VHDL dma.vhd:666-667", dma.counter()));
    }

    // 14.3 Counter increments per byte (ZXN mode): after 10 bytes, counter=10.
    // VHDL dma.vhd:361 `dma_counter_s + 1`.
    {
        fresh(dma);
        for (int i = 0; i < 10; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 10);
        run_to_idle(dma);
        check("14.3", "ZXN: counter=N after N-byte block",
              dma.counter() == 10,
              fmt("counter=%u  VHDL dma.vhd:361", dma.counter()));
    }

    // 14.4 ZXN block_len=5 transfers exactly 5 bytes.
    // VHDL dma.vhd:426 counter<block_len while counter stepping 0..4.
    {
        fresh(dma);
        for (int i = 0; i < 8; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xE0 + i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 5);
        int n = run_to_idle(dma);
        check("14.4", "ZXN block_len=5: 5 bytes transferred",
              n == 5 && g_mem[0x9004] == 0xE4 && g_mem[0x9005] == 0x00,
              fmt("n=%d dst[4]=0x%02X dst[5]=0x%02X  VHDL dma.vhd:426",
                  n, g_mem[0x9004], g_mem[0x9005]));
    }

    // 14.5 Z80 block_len=5 transfers 6 bytes.  In Z80 mode the counter
    // starts at 0xFFFF, so after byte 1 counter=0x0000 and the check
    // `counter<5` is still true — 5 more bytes transfer (total 6).
    // VHDL dma.vhd:426 with init 0xFFFF.
    {
        fresh(dma);
        for (int i = 0; i < 8; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xC0 + i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 5, /*z80_mode=*/true);
        int n = run_to_idle(dma);
        check("14.5", "Z80 block_len=5: 6 bytes (block_len+1)",
              n == 6 && g_mem[0x9005] == 0xC5 && g_mem[0x9006] == 0x00,
              fmt("n=%d dst[5]=0x%02X dst[6]=0x%02X  VHDL dma.vhd:426,666-667",
                  n, g_mem[0x9005], g_mem[0x9006]));
    }

    // 14.6 ZXN block_len=0 transfers 1 byte.  VHDL dma.vhd:361 increments
    // counter at TRANSFERING_WRITE_1 (after the byte is transferred), then
    // :426 checks `counter_s < block_len_s` at WRITE_4.  Counter starts at
    // 0 (LOAD, :665), becomes 1 after first byte, 1 < 0 is false → FINISH.
    {
        fresh(dma);
        g_mem[0x8000] = 0xDE;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 0);
        int n = run_to_idle(dma);
        check("14.6", "ZXN block_len=0: 1 byte transferred",
              n == 1,
              fmt("n=%d  VHDL dma.vhd:361,426", n));
    }

    // 14.7 Z80 block_len=0 transfers 1 byte.  Z80 counter starts at 0xFFFF
    // (LOAD, :667), wraps to 0x0000 after first byte, 0 < 0 → FINISH.
    {
        fresh(dma);
        g_mem[0x8000] = 0xCA;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 0, /*z80_mode=*/true);
        int n = run_to_idle(dma);
        check("14.7", "Z80 block_len=0: 1 byte transferred",
              n == 1,
              fmt("n=%d  VHDL dma.vhd:361,426,667", n));
    }

    // 14.8 Counter readback via read sequence.
    // VHDL dma.vhd:933-935 + mask-driven advancement.
    {
        fresh(dma);
        for (int i = 0; i < 5; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 5);
        run_to_idle(dma);
        zxn(dma, 0xBB);
        zxn(dma, 0x06);                  // mask bits 1,2 = counter LO + HI
        zxn(dma, 0xA7);                  // init sequence
        uint8_t lo = dma.read();
        uint8_t hi = dma.read();
        uint16_t cnt = static_cast<uint16_t>((hi << 8) | lo);
        check("14.8", "Counter readback = 5 after 5-byte block",
              cnt == 5,
              fmt("readback=0x%04X  VHDL dma.vhd:933-947", cnt));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 15 — Bus arbitration
// VHDL dma.vhd:267-302 (START_DMA / WAITING_ACK), zxnext.vhd dma_holds_bus
// wiring.  None of the BUSREQ/BUSACK/daisy-chain signals are exposed by
// the C++ Dma class.
// ══════════════════════════════════════════════════════════════════════

void group15_bus_arbitration() {
    set_group("G15 Bus Arb");
    Dma dma;

    // 15.1 DMA asserts cpu_busreq_n=0 while transferring (VHDL dma.vhd:278).
    {
        fresh(dma);
        g_mem[0x8000] = 0x11;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        dma.execute_burst(1);
        check("15.1", "TRANSFER: cpu_busreq_n asserted (false)",
              dma.cpu_busreq_n() == false,
              fmt("cpu_busreq_n=%d  VHDL dma.vhd:278",
                  (int)dma.cpu_busreq_n()));
    }

    // 15.2 WAITING_ACK waits for cpu_bai_n=0 (VHDL dma.vhd:296).  Drive
    // cpu_bai_n=true (no ack); no bytes should transfer.
    {
        fresh(dma);
        g_mem[0x8000] = 0x22;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        dma.set_cpu_bai_n(true);         // no ack: stays in WAITING_ACK
        int n_before = dma.execute_burst(4);
        dma.set_cpu_bai_n(false);        // ack arrives
        int n_after = dma.execute_burst(4);
        check("15.2", "WAITING_ACK gates transfer on cpu_bai_n",
              n_before == 0 && n_after > 0,
              fmt("before=%d after=%d  VHDL dma.vhd:296",
                  n_before, n_after));
    }

    // 15.3 In IDLE, cpu_busreq_n=1 (deasserted).  VHDL dma.vhd:225,262.
    {
        fresh(dma);
        check("15.3", "IDLE: cpu_busreq_n deasserted (true)",
              dma.state() == Dma::State::IDLE &&
                  dma.cpu_busreq_n() == true,
              fmt("state=%d busreq_n=%d  VHDL dma.vhd:225,262",
                  (int)dma.state(), (int)dma.cpu_busreq_n()));
    }

    // 15.4 set_bus_busreq_n(false) at START_DMA: DMA defers (VHDL dma.vhd:269).
    {
        fresh(dma);
        g_mem[0x8000] = 0x44;
        dma.set_bus_busreq_n(false);     // external device holds bus
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        int n = dma.execute_burst(4);
        check("15.4", "bus_busreq_n=0 at START_DMA: DMA defers",
              n == 0 && dma.cpu_busreq_n() == true,
              fmt("n=%d busreq_n=%d  VHDL dma.vhd:269",
                  n, (int)dma.cpu_busreq_n()));
    }

    // 15.5 set_daisy_busy(true) at START_DMA: DMA defers (VHDL dma.vhd:269
    // — the cpu_bai_n=0 "upstream busy" gate, modelled as daisy_busy_).
    {
        fresh(dma);
        g_mem[0x8000] = 0x55;
        dma.set_daisy_busy(true);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        int n = dma.execute_burst(4);
        check("15.5", "daisy_busy=true at START_DMA: DMA defers",
              n == 0 && dma.cpu_busreq_n() == true,
              fmt("n=%d busreq_n=%d  VHDL dma.vhd:269",
                  n, (int)dma.cpu_busreq_n()));
    }

    // 15.6 set_dma_delay(true) at START_DMA: DMA defers (VHDL dma.vhd:269).
    {
        fresh(dma);
        g_mem[0x8000] = 0x66;
        dma.set_dma_delay(true);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        int n = dma.execute_burst(4);
        check("15.6", "dma_delay=1 at START_DMA: DMA defers",
              n == 0 && dma.cpu_busreq_n() == true,
              fmt("n=%d busreq_n=%d  VHDL dma.vhd:269",
                  n, (int)dma.cpu_busreq_n()));
    }

    // 15.7 dma_holds_bus is true while DMA has the bus (busreq + ack).
    // Mirrors zxnext.vhd `dma_holds_bus <= not cpu_busak_n`.
    {
        fresh(dma);
        g_mem[0x8000] = 0x77;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        dma.execute_burst(1);
        check("15.7", "dma_holds_bus=true while transferring",
              dma.dma_holds_bus() == true,
              fmt("dma_holds_bus=%d  VHDL zxnext.vhd dma_holds_bus",
                  (int)dma.dma_holds_bus()));
    }

    // 15.8 — COVERED AT DISPATCHER LAYER (not a skip).  The VHDL gate
    //   port_dma_rd/wr <= port_dma_rd_raw/wr_raw AND NOT dma_holds_bus
    // sits at the port dispatcher, not inside the Dma module.  See
    // test/port/port_test.cpp row REG-22-BUS for the coverage.  Kept here
    // as a source-level reference to the DMA plan row.
}

// ══════════════════════════════════════════════════════════════════════
// Group 16 — Auto-restart and continue
// VHDL dma.vhd:473-491 (FINISH_DMA auto-restart reload), :670-676 (CONTINUE).
// ══════════════════════════════════════════════════════════════════════

void group16_autorestart_continue() {
    set_group("G16 Auto-restart/Cont");
    Dma dma;

    // 16.1 Auto-restart reloads addresses from start registers.
    {
        fresh(dma);
        zxn(dma, 0xA2);
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 2);
        dma.execute_burst(2);
        check("16.1", "Auto-restart: src/dst reloaded to 0x8000/0x9000",
              dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:473-481",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 16.2 Auto-restart reloads counter to mode-specific value.
    {
        fresh(dma);
        zxn(dma, 0xA2);
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 2);
        dma.execute_burst(2);
        check("16.2", "Auto-restart ZXN: counter reloaded to 0",
              dma.counter() == 0,
              fmt("counter=0x%04X  VHDL dma.vhd:482-486", dma.counter()));
    }

    // 16.3 Auto-restart in direction A->B (already covered by 16.1 which
    // programs A->B).  Repeat more explicitly.
    {
        fresh(dma);
        zxn(dma, 0xA2);
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0xF0 + i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 2);
        dma.execute_burst(2);
        check("16.3", "Auto-restart A->B reload uses R0 as src, R4 as dst",
              dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:474-476",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 16.4 Auto-restart in direction B->A: src reloaded from port B start.
    {
        fresh(dma);
        zxn(dma, 0xA2);
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x80 + i);
        zxn(dma, 0x79);                  // B->A
        zxn(dma, 0x00); zxn(dma, 0x90);  // portA = 0x9000
        zxn(dma, 0x02); zxn(dma, 0x00);
        zxn(dma, 0x14); zxn(dma, 0x10);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0x80);  // portB = 0x8000
        zxn(dma, 0xCF); zxn(dma, 0x87);
        dma.execute_burst(2);
        check("16.4", "Auto-restart B->A reload: src=portB, dst=portA",
              dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:478-479",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 16.5 CONTINUE preserves current src/dst.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        run_to_idle(dma);
        uint16_t s = dma.src_addr(), d = dma.dst_addr();
        zxn(dma, 0xD3);
        check("16.5", "CONTINUE preserves src/dst",
              dma.src_addr() == s && dma.dst_addr() == d,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:670-676",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 16.6 CONTINUE vs LOAD: LOAD overwrites src/dst, CONTINUE does not.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        run_to_idle(dma);
        zxn(dma, 0xCF);                  // LOAD -> src/dst = start values
        bool load_restored = (dma.src_addr() == 0x8000 && dma.dst_addr() == 0x9000);
        // Advance src manually by running a partial transfer so addresses differ
        zxn(dma, 0x87);
        dma.execute_burst(2);
        uint16_t s = dma.src_addr(), d = dma.dst_addr();
        zxn(dma, 0xD3);                  // CONTINUE -> keeps current addrs
        bool cont_preserved = (dma.src_addr() == s && dma.dst_addr() == d);
        check("16.6", "LOAD restores start addrs; CONTINUE keeps current addrs",
              load_restored && cont_preserved,
              fmt("load=%d cont=%d  VHDL dma.vhd:656-662 vs :670-676",
                  (int)load_restored, (int)cont_preserved));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 17 — Status register and read sequence
// VHDL dma.vhd:902 status byte layout, :691-692 reinit, :638-645 hard reset,
// :239 read mask reset default, :859-861 mask programming.
// Layout: [7:6]=00, [5]=endofblock_n, [4:1]=1101, [0]=atleastone
// Idle: 0b00_1_1101_0 = 0x3A.  After full block: 0b00_0_1101_1 = 0x1B.
// ══════════════════════════════════════════════════════════════════════

void group17_status() {
    set_group("G17 Status/Readback");
    Dma dma;

    // 17.1 Status byte format: fixed middle nibble is 1101.
    {
        fresh(dma);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.1", "Status bits [4:1] = 1101",
              (s & 0x1E) == 0x1A,
              fmt("status=0x%02X middle_nibble=0x%02X  VHDL dma.vhd:902",
                  s, s & 0x1E));
    }

    // 17.2 End-of-block flag is clear initially (bit5=1 = NOT ended).
    {
        fresh(dma);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.2", "Initial endofblock_n = 1 (bit5 set)",
              (s & 0x20) == 0x20,
              fmt("status=0x%02X  VHDL dma.vhd:242", s));
    }

    // 17.3 End-of-block set after full transfer (bit5=0).
    {
        fresh(dma);
        g_mem[0x8000] = 0x11;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.3", "After block: endofblock_n = 0 (bit5 clear)",
              (s & 0x20) == 0x00,
              fmt("status=0x%02X  VHDL dma.vhd:471", s));
    }

    // 17.4 At-least-one flag set after first byte transferred.
    {
        fresh(dma);
        g_mem[0x8000] = 0x22;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.4", "After 1 byte: atleastone = 1 (bit0 set)",
              (s & 0x01) == 0x01,
              fmt("status=0x%02X  VHDL dma.vhd:412", s));
    }

    // 17.5 Status cleared by 0x8B reinit (status = 0x3A).
    {
        fresh(dma);
        g_mem[0x8000] = 0x33;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);
        zxn(dma, 0x8B);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.5", "0x8B reinit: status = 0x3A",
              s == 0x3A,
              fmt("status=0x%02X  VHDL dma.vhd:691-692", s));
    }

    // 17.6 Status cleared by 0xC3 hard reset.
    {
        fresh(dma);
        g_mem[0x8000] = 0x44;
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 1);
        run_to_idle(dma);
        zxn(dma, 0xC3);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("17.6", "0xC3 reset: status = 0x3A",
              s == 0x3A,
              fmt("status=0x%02X  VHDL dma.vhd:638-641", s));
    }

    // 17.7 Default read mask after reset = "01111111" = 0x7F.
    // Observable: the read sequence after ENABLE/LOAD rotates through
    // all 7 fields once before wrapping.
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x34); zxn(dma, 0x12);
        zxn(dma, 0x08); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x78); zxn(dma, 0x56);
        zxn(dma, 0xCF);
        zxn(dma, 0xA7);                  // init read sequence
        uint8_t v[8];
        for (int i = 0; i < 8; ++i) v[i] = dma.read();
        // Expected sequence: status, cnt_lo, cnt_hi, pA_lo, pA_hi, pB_lo, pB_hi,
        // then wrap back to status.
        bool ok = v[0] == 0x3A && v[1] == 0x00 && v[2] == 0x00 &&
                  v[3] == 0x34 && v[4] == 0x12 && v[5] == 0x78 && v[6] == 0x56 &&
                  v[7] == v[0];
        check("17.7", "Default mask 0x7F: 7-field cycle then wrap",
              ok,
              fmt("v=[%02X %02X %02X %02X %02X %02X %02X %02X]  VHDL dma.vhd:239",
                  v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]));
    }

    // 17.8 Read sequence cycles through mask (default 0x7F): 7 reads all
    // distinct fields when addresses are unique.
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x11); zxn(dma, 0x22);
        zxn(dma, 0x08); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x33); zxn(dma, 0x44);
        zxn(dma, 0xCF);
        zxn(dma, 0xA7);
        uint8_t v[7];
        for (int i = 0; i < 7; ++i) v[i] = dma.read();
        check("17.8", "Read sequence advances mask bits 0..6 in order",
              v[1] == 0x00 && v[2] == 0x00 &&
              v[3] == 0x11 && v[4] == 0x22 &&
              v[5] == 0x33 && v[6] == 0x44,
              fmt("v=[%02X %02X %02X %02X %02X %02X %02X]  VHDL dma.vhd:902-922",
                  v[0],v[1],v[2],v[3],v[4],v[5],v[6]));
    }

    // 17.9 Custom read mask (status + counter only): 3 reads then wrap.
    {
        fresh(dma);
        zxn(dma, 0xBB);
        zxn(dma, 0x07);                  // bits 0,1,2
        zxn(dma, 0xA7);
        uint8_t a = dma.read();          // status
        uint8_t b = dma.read();          // counter LO
        uint8_t c = dma.read();          // counter HI
        uint8_t d = dma.read();          // wrap to status
        check("17.9", "Mask 0x07: 3 fields (status, cnt LO/HI) then wrap",
              a == 0x3A && b == 0x00 && c == 0x00 && d == a,
              fmt("[a=%02X b=%02X c=%02X d=%02X]  VHDL dma.vhd:696-717",
                  a, b, c, d));
    }

    // 17.10 Read sequence wraps after last enabled field.
    {
        fresh(dma);
        zxn(dma, 0xBB);
        zxn(dma, 0x41);                  // bits 0 and 6 = status + portB HI
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x00);
        zxn(dma, 0x01); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x00); zxn(dma, 0xEE);
        zxn(dma, 0xCF);
        zxn(dma, 0xA7);
        uint8_t s1 = dma.read();         // status
        uint8_t pbh = dma.read();        // portB HI = 0xEE
        uint8_t s2 = dma.read();         // wrap to status
        check("17.10", "Mask with two bits: wraps after last enabled field",
              s1 == 0x3A && pbh == 0xEE && s2 == 0x3A,
              fmt("[s1=%02X pbh=%02X s2=%02X]  VHDL dma.vhd:919-922",
                  s1, pbh, s2));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 18 — Read sequence fields
// VHDL dma.vhd:902 (status), :933 (counter LO), :935-947 (advancement),
// and per-direction src/dest mapping driven by R0_dir_AtoB_s.
// ══════════════════════════════════════════════════════════════════════

void group18_read_fields() {
    set_group("G18 Read Fields");
    Dma dma;

    auto program_and_init = [&](bool z80_mode, uint16_t pa, uint16_t pb,
                                 bool a_to_b) {
        fresh(dma);
        auto wr = z80_mode ? z80 : zxn;
        wr(dma, a_to_b ? 0x7D : 0x79);
        wr(dma, pa & 0xFF);
        wr(dma, (pa >> 8) & 0xFF);
        wr(dma, 0x01); wr(dma, 0x00);
        wr(dma, 0x14); wr(dma, 0x10);
        wr(dma, 0xAD);
        wr(dma, pb & 0xFF);
        wr(dma, (pb >> 8) & 0xFF);
        wr(dma, 0xCF);                   // LOAD
        wr(dma, 0xA7);                   // init read sequence
    };

    // 18.1 Read status byte.
    {
        program_and_init(false, 0x1234, 0x5678, true);
        // The sequence starts at mask bit 0 = STATUS.
        uint8_t s = dma.read();
        check("18.1", "Read field: status byte",
              s == 0x3A,
              fmt("status=0x%02X  VHDL dma.vhd:902", s));
    }

    // 18.2 Read counter LO.
    {
        program_and_init(false, 0x1234, 0x5678, true);
        dma.read();                      // consume status
        uint8_t lo = dma.read();
        check("18.2", "Read field: counter LO = 0x00 (ZXN just LOADed)",
              lo == 0x00,
              fmt("cnt_lo=0x%02X  VHDL dma.vhd:933", lo));
    }

    // 18.3 Read counter HI.
    {
        program_and_init(false, 0x1234, 0x5678, true);
        dma.read(); dma.read();          // consume status, cnt_lo
        uint8_t hi = dma.read();
        check("18.3", "Read field: counter HI = 0x00 (ZXN just LOADed)",
              hi == 0x00,
              fmt("cnt_hi=0x%02X  VHDL dma.vhd:935", hi));
    }

    // 18.4 Read port A LO when A->B (= dma_src_s LO).
    // VHDL dma.vhd:910-912 uses R0_dir_AtoB_s to select src vs dest.
    {
        program_and_init(false, 0x1234, 0x5678, true);
        for (int i = 0; i < 3; ++i) dma.read();
        uint8_t pa_lo = dma.read();
        check("18.4", "Read field: portA LO = src LO (0x34) under A->B",
              pa_lo == 0x34,
              fmt("pA_lo=0x%02X  VHDL dma.vhd:910-912", pa_lo));
    }

    // 18.5 Read port A HI when A->B.
    {
        program_and_init(false, 0x1234, 0x5678, true);
        for (int i = 0; i < 4; ++i) dma.read();
        uint8_t pa_hi = dma.read();
        check("18.5", "Read field: portA HI = src HI (0x12) under A->B",
              pa_hi == 0x12,
              fmt("pA_hi=0x%02X  VHDL dma.vhd:913-915", pa_hi));
    }

    // 18.6 Read port B LO when A->B.
    {
        program_and_init(false, 0x1234, 0x5678, true);
        for (int i = 0; i < 5; ++i) dma.read();
        uint8_t pb_lo = dma.read();
        check("18.6", "Read field: portB LO = dst LO (0x78) under A->B",
              pb_lo == 0x78,
              fmt("pB_lo=0x%02X  VHDL dma.vhd:916-918", pb_lo));
    }

    // 18.7 Read port B HI when A->B.
    {
        program_and_init(false, 0x1234, 0x5678, true);
        for (int i = 0; i < 6; ++i) dma.read();
        uint8_t pb_hi = dma.read();
        check("18.7", "Read field: portB HI = dst HI (0x56) under A->B",
              pb_hi == 0x56,
              fmt("pB_hi=0x%02X  VHDL dma.vhd:919-921", pb_hi));
    }

    // 18.8 Under B->A the port A/B readback swaps: portA reads dma_dest_s,
    // portB reads dma_src_s.  VHDL dma.vhd:910-921 check R0_dir_AtoB_s.
    {
        program_and_init(false, 0x1234, 0x5678, false);  // B->A
        // src = portB = 0x5678; dst = portA = 0x1234
        for (int i = 0; i < 3; ++i) dma.read();
        uint8_t pa_lo = dma.read();      // should be dst LO = 0x34
        uint8_t pa_hi = dma.read();      // dst HI = 0x12
        uint8_t pb_lo = dma.read();      // src LO = 0x78
        uint8_t pb_hi = dma.read();      // src HI = 0x56
        check("18.8", "B->A: portA reads dst, portB reads src",
              pa_lo == 0x34 && pa_hi == 0x12 && pb_lo == 0x78 && pb_hi == 0x56,
              fmt("[pA=%02X%02X pB=%02X%02X]  VHDL dma.vhd:910-921",
                  pa_hi, pa_lo, pb_hi, pb_lo));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 19 — Reset behaviour
// VHDL dma.vhd:213-242 (hard reset block), :638-645 (0xC3 soft reset).
// ══════════════════════════════════════════════════════════════════════

void group19_reset() {
    set_group("G19 Reset");
    Dma dma;

    // 19.1 Hardware reset: state=IDLE, mode=continuous, counter=0,
    //      src/dst/block_len = 0, read_mask=0x7F, inc+inc.
    // VHDL dma.vhd:213-242.
    {
        fresh(dma);
        bool ok = dma.state() == Dma::State::IDLE &&
                  dma.transfer_mode() == Dma::TransferMode::CONTINUOUS &&
                  dma.src_addr_mode() == Dma::AddrMode::INCREMENT &&
                  dma.dst_addr_mode() == Dma::AddrMode::INCREMENT &&
                  dma.counter() == 0 &&
                  dma.block_length() == 0 &&
                  dma.src_addr() == 0 && dma.dst_addr() == 0;
        check("19.1", "Hardware reset defaults",
              ok,
              fmt("state=%d mode=%d src_m=%d dst_m=%d cnt=%u bl=%u "
                  "src=0x%04X dst=0x%04X  VHDL dma.vhd:213-242",
                  (int)dma.state(), (int)dma.transfer_mode(),
                  (int)dma.src_addr_mode(), (int)dma.dst_addr_mode(),
                  dma.counter(), dma.block_length(),
                  dma.src_addr(), dma.dst_addr()));
    }

    // 19.2 Soft reset (0xC3) puts state=IDLE and clears status.
    // VHDL dma.vhd:638-641.
    {
        fresh(dma);
        zxn(dma, 0x87);                  // ENABLE
        zxn(dma, 0xC3);
        zxn(dma, 0xBF);
        uint8_t s = dma.read();
        check("19.2", "0xC3 soft reset: state=IDLE and status=0x3A",
              dma.state() == Dma::State::IDLE && s == 0x3A,
              fmt("state=%d status=0x%02X  VHDL dma.vhd:638-641",
                  (int)dma.state(), s));
    }

    // 19.3 0xC3 does NOT reset R0 port A start address nor R4 port B start
    // address.  VHDL dma.vhd:638-645 lists the reset signals — port A/B
    // start address registers are absent from that list.
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x34); zxn(dma, 0x12);
        zxn(dma, 0x08); zxn(dma, 0x00);
        zxn(dma, 0xAD);
        zxn(dma, 0x78); zxn(dma, 0x56);
        zxn(dma, 0xC3);
        zxn(dma, 0xCF);                  // LOAD reveals preserved start regs
        check("19.3", "0xC3 preserves R0 and R4 start addresses",
              dma.src_addr() == 0x1234 && dma.dst_addr() == 0x5678,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:638-645",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 19.4 0xC3 resets both port timings to "01".  VHDL dma.vhd:641-642.
    {
        fresh(dma);
        zxn(dma, 0x54); zxn(dma, 0x02);  // port A timing = 10
        zxn(dma, 0x40); zxn(dma, 0x02);  // port B timing = 10
        zxn(dma, 0xC3);                  // soft reset
        check("19.4", "0xC3 resets both port timings to 01",
              dma.port_a_timing() == 0x01 && dma.port_b_timing() == 0x01,
              fmt("A=0x%02X B=0x%02X  VHDL dma.vhd:641-642",
                  dma.port_a_timing(), dma.port_b_timing()));
    }

    // 19.5 0xC3 resets prescaler to 0x00.  Only indirectly observable via
    // continuous-mode runs-to-IDLE behaviour (row 4.8).
    {
        fresh(dma);
        // Arm prescaler to a large value, then soft reset and confirm a
        // subsequent transfer completes in one execute_burst (prescaler=0).
        zxn(dma, 0x50); zxn(dma, 0x21); zxn(dma, 0xFF);
        zxn(dma, 0xC3);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        int n = dma.execute_burst(1000);
        check("19.5", "0xC3 resets prescaler: transfer runs to IDLE in one burst",
              n == 4 && dma.state() == Dma::State::IDLE,
              fmt("n=%d state=%d  VHDL dma.vhd:643", n, (int)dma.state()));
    }

    // 19.6 0xC3 resets auto-restart to 0.  Observable: after soft reset,
    // a transfer ends at IDLE (no restart loop).
    {
        fresh(dma);
        zxn(dma, 0xA2);                  // enable auto-restart
        zxn(dma, 0xC3);                  // reset
        for (int i = 0; i < 2; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 2);
        run_to_idle(dma);
        check("19.6", "0xC3 clears auto-restart: transfer ends at IDLE",
              dma.state() == Dma::State::IDLE,
              fmt("state=%d  VHDL dma.vhd:645", (int)dma.state()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 20 — DMA delay and interrupt integration
// VHDL dma.vhd:267-281 (dma_delay_i gate in START_DMA), zxnext.vhd nextreg
// 0xCC/0xCD/0xCE wiring for IM2 DMA.  None of these are exposed on Dma.
// ══════════════════════════════════════════════════════════════════════

void group20_dma_delay() {
    set_group("G20 DMA Delay");
    Dma dma;

    // 20.1 dma_delay_i asserted at START_DMA blocks the transfer
    // (VHDL dma.vhd:269).  Observable: n=0 transfers with delay set.
    {
        fresh(dma);
        g_mem[0x8000] = 0x81;
        dma.set_dma_delay(true);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        int n_deferred = dma.execute_burst(4);
        dma.set_dma_delay(false);
        int n_ok = dma.execute_burst(4);
        check("20.1", "dma_delay=1 blocks START_DMA; deasserting proceeds",
              n_deferred == 0 && n_ok > 0,
              fmt("deferred=%d ok=%d  VHDL dma.vhd:269",
                  n_deferred, n_ok));
    }

    // 20.2 Mid-transfer dma_delay_i=1: DMA drops back to START_DMA and
    // releases the bus (VHDL dma.vhd:427-428).  Observable: after the
    // byte that triggered the delay, cpu_busreq_n returns to true.
    {
        fresh(dma);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(0x90 + i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        dma.execute_burst(1);            // one byte, bus asserted
        dma.set_dma_delay(true);
        dma.execute_burst(1);            // trips mid-transfer delay
        check("20.2", "dma_delay mid-transfer: cpu_busreq_n released",
              dma.cpu_busreq_n() == true,
              fmt("busreq_n=%d  VHDL dma.vhd:427-428",
                  (int)dma.cpu_busreq_n()));
    }

    // 20.3 — COVERED AT INTEGRATION TIER (not a skip).  NR 0xCC/0xCD/0xCE
    // DMA enable bit layouts + `compose_im2_dma_int_en()` 14-bit mask.
    // See test/nextreg/nextreg_integration_test.cpp group "DMA-IM2-Delay",
    // rows 20.3a..g.  Kept here as a source-level reference to the DMA
    // plan row; not executed at the Dma unit level because the register
    // handlers live in Emulator, not in Dma.

    // 20.4 — COVERED AT INTEGRATION TIER (not a skip).  im2_dma_delay
    // composition formula (im2_dma_int | (nmi & nr_cc_7) | (prev & dma_delay),
    // VHDL zxnext.vhd:2007).  See nextreg_integration_test.cpp rows 20.4a..f.
}

// ══════════════════════════════════════════════════════════════════════
// Group 21 — Timing byte effects
// VHDL dma.vhd:311-327 (read cycle count) and :363-376 (write cycle count).
// None observable: C++ Dma has no cycle counter exposed.
// ══════════════════════════════════════════════════════════════════════

void group21_timing_bytes() {
    set_group("G21 Timing Bytes");
    Dma dma;

    // 21.1-21.4 Cycle mapping from the 2-bit timing byte.  VHDL dma.vhd:
    // 313,365 (00->4), 314,366 (01->3), 315,367 (10->2), 316,368 (11->4
    // `when others`).  Program A->B so read_cycles() uses R1 (port A).
    {
        fresh(dma);
        zxn(dma, 0x05);                  // R0 dir A->B (no sub-bytes)
        zxn(dma, 0x54); zxn(dma, 0x00);  // R1 timing = 00
        check("21.1", "Timing 00 -> 4 cycles",
              dma.read_cycles() == 4,
              fmt("read_cycles=%u  VHDL dma.vhd:313", dma.read_cycles()));
    }
    {
        fresh(dma);
        zxn(dma, 0x05);
        zxn(dma, 0x54); zxn(dma, 0x01);
        check("21.2", "Timing 01 -> 3 cycles",
              dma.read_cycles() == 3,
              fmt("read_cycles=%u  VHDL dma.vhd:314", dma.read_cycles()));
    }
    {
        fresh(dma);
        zxn(dma, 0x05);
        zxn(dma, 0x54); zxn(dma, 0x02);
        check("21.3", "Timing 10 -> 2 cycles",
              dma.read_cycles() == 2,
              fmt("read_cycles=%u  VHDL dma.vhd:315", dma.read_cycles()));
    }
    {
        fresh(dma);
        zxn(dma, 0x05);
        zxn(dma, 0x54); zxn(dma, 0x03);
        check("21.4", "Timing 11 -> 4 cycles (when others)",
              dma.read_cycles() == 4,
              fmt("read_cycles=%u  VHDL dma.vhd:316", dma.read_cycles()));
    }

    // 21.5 A->B uses R1 for read; B->A uses R2.  VHDL dma.vhd:311 vs :319.
    {
        fresh(dma);
        // A->B: R1 timing 10 (2-cyc), R2 timing 00 (4-cyc).  read should be 2.
        zxn(dma, 0x05);
        zxn(dma, 0x54); zxn(dma, 0x02);  // R1 = 10
        zxn(dma, 0x40); zxn(dma, 0x00);  // R2 = 00
        uint8_t ab_read = dma.read_cycles();
        // B->A: R1 timing 10, R2 timing 00.  read should be 4 (R2 selected).
        zxn(dma, 0x01);                  // R0 dir B->A (bit2=0, bit0=1)
        uint8_t ba_read = dma.read_cycles();
        check("21.5", "Read timing selects R1 (A->B) vs R2 (B->A)",
              ab_read == 2 && ba_read == 4,
              fmt("ab=%u ba=%u  VHDL dma.vhd:311 vs :319", ab_read, ba_read));
    }

    // 21.6 A->B uses R2 for write; B->A uses R1.  VHDL dma.vhd:371 vs :364.
    {
        fresh(dma);
        zxn(dma, 0x05);
        zxn(dma, 0x54); zxn(dma, 0x02);  // R1 = 10
        zxn(dma, 0x40); zxn(dma, 0x00);  // R2 = 00
        uint8_t ab_write = dma.write_cycles();  // A->B uses R2 = 00 -> 4
        zxn(dma, 0x01);                         // B->A
        uint8_t ba_write = dma.write_cycles();  // B->A uses R1 = 10 -> 2
        check("21.6", "Write timing selects R2 (A->B) vs R1 (B->A)",
              ab_write == 4 && ba_write == 2,
              fmt("ab=%u ba=%u  VHDL dma.vhd:371 vs :364",
                  ab_write, ba_write));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group 22 — Edge cases
// ══════════════════════════════════════════════════════════════════════

void group22_edge() {
    set_group("G22 Edge Cases");
    Dma dma;

    // 22.1 Disable during active transfer -> IDLE.  VHDL dma.vhd:728.
    {
        fresh(dma);
        for (int i = 0; i < 32; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 32);
        dma.execute_burst(4);
        zxn(dma, 0x83);
        check("22.1", "DISABLE mid-transfer -> IDLE",
              dma.state() == Dma::State::IDLE,
              fmt("state=%d  VHDL dma.vhd:728", (int)dma.state()));
    }

    // 22.2 Enable without LOAD: VHDL allows it and uses the current
    // dma_src_s / dma_dest_s / block_len values as-is (dma.vhd:725 simply
    // sets dma_seq_s <= START_DMA).
    {
        fresh(dma);
        zxn(dma, 0x87);
        check("22.2", "ENABLE without LOAD: state=TRANSFERRING",
              dma.state() == Dma::State::TRANSFERRING,
              fmt("state=%d src=0x%04X dst=0x%04X  VHDL dma.vhd:725",
                  (int)dma.state(), dma.src_addr(), dma.dst_addr()));
    }

    // 22.3 Multiple LOADs before ENABLE: the last LOAD wins.
    {
        fresh(dma);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x10);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0xAD); zxn(dma, 0x00); zxn(dma, 0x20);
        zxn(dma, 0xCF);
        zxn(dma, 0x7D);
        zxn(dma, 0x00); zxn(dma, 0x30);
        zxn(dma, 0x04); zxn(dma, 0x00);
        zxn(dma, 0xAD); zxn(dma, 0x00); zxn(dma, 0x40);
        zxn(dma, 0xCF);
        check("22.3", "Multiple LOADs: last values used",
              dma.src_addr() == 0x3000 && dma.dst_addr() == 0x4000,
              fmt("src=0x%04X dst=0x%04X  VHDL dma.vhd:656-668",
                  dma.src_addr(), dma.dst_addr()));
    }

    // 22.4 CONTINUE during auto-restart: only counter is reset, current
    // addresses survive.  VHDL dma.vhd:670-676.
    {
        fresh(dma);
        zxn(dma, 0xA2);
        for (int i = 0; i < 4; ++i) g_mem[0x8000 + i] = static_cast<uint8_t>(i);
        program_mem_to_mem_AB(dma, 0x8000, 0x9000, 4);
        dma.execute_burst(4);            // one block -> auto-reload
        // Manually step one more byte so src advances from 0x8000.
        dma.execute_burst(1);
        uint16_t s = dma.src_addr(), d = dma.dst_addr();
        zxn(dma, 0xD3);
        check("22.4", "CONTINUE during auto-restart: counter reset, addrs kept",
              dma.counter() == 0 && dma.src_addr() == s && dma.dst_addr() == d,
              fmt("counter=%u src=0x%04X dst=0x%04X  VHDL dma.vhd:670-676",
                  dma.counter(), dma.src_addr(), dma.dst_addr()));
    }

    // 22.5 Decode ambiguity: a byte with bit7=0 can match multiple
    // non-exclusive R0/R1/R2 patterns in theory, but VHDL dma.vhd:542 and
    // :559 require bits[2:0]=100/000 which are mutually exclusive with R0
    // needing bit0|bit1 set.  Writing 0x14 (R1 inc) must NOT touch R0 dir.
    // Use 0x01 as the baseline R0 (dir B->A, no sub-bytes) so the R1 byte
    // that follows is interpreted as a fresh base byte and hits R1 decode.
    {
        fresh(dma);
        zxn(dma, 0x01);                  // R0: bit0=1, bit2=0 (B->A), no sub-bytes
        zxn(dma, 0x14);                  // R1 base — must not disturb dir
        zxn(dma, 0xAD); zxn(dma, 0xCD); zxn(dma, 0xAB);  // R4 + portB sub-bytes
        zxn(dma, 0xCF);                  // LOAD
        check("22.5", "R1 base byte does not update R0 direction",
              dma.src_addr() == 0xABCD,
              fmt("src=0x%04X (dir B->A -> src=portB=0xABCD)  "
                  "VHDL dma.vhd:542 vs :518-520", dma.src_addr()));
    }

    // 22.6 Byte 0x00: bit7=0 and bits[2:0]=000 matches R2, and R0 is NOT
    // matched because R0 requires (bit0|bit1)=1.  VHDL dma.vhd:559 vs
    // :518-520.  After writing 0x00, R2 addr mode becomes "00" (decrement),
    // observable via dst_addr_mode().
    {
        fresh(dma);
        zxn(dma, 0x00);
        check("22.6", "0x00 matches R2 (dec), not R0",
              dma.dst_addr_mode() == Dma::AddrMode::DECREMENT,
              fmt("dst_mode=%d  VHDL dma.vhd:518-520,559",
                  (int)dma.dst_addr_mode()));
    }
}

}  // namespace

// ══════════════════════════════════════════════════════════════════════

int main() {
    std::printf("DMA Subsystem Compliance Tests (Phase 2 rewrite)\n");
    std::printf("================================================\n\n");

    group1_port_decode();          std::printf("  G1  Port Decode          done\n");
    group2_r0();                   std::printf("  G2  R0                   done\n");
    group3_r1();                   std::printf("  G3  R1                   done\n");
    group4_r2();                   std::printf("  G4  R2                   done\n");
    group5_r3();                   std::printf("  G5  R3                   done\n");
    group6_r4();                   std::printf("  G6  R4                   done\n");
    group7_r5();                   std::printf("  G7  R5                   done\n");
    group8_r6_commands();          std::printf("  G8  R6                   done\n");
    group9_mem_to_mem();           std::printf("  G9  Mem-to-Mem           done\n");
    group10_mem_to_io();           std::printf("  G10 Mem-to-IO            done\n");
    group11_addr_modes();          std::printf("  G11 Addr Modes           done\n");
    group12_transfer_modes();      std::printf("  G12 Xfer Modes           done\n");
    group13_prescaler_timing();    std::printf("  G13 Prescaler/Timing     done\n");
    group14_counter();             std::printf("  G14 Counter              done\n");
    group15_bus_arbitration();     std::printf("  G15 Bus Arb              done\n");
    group16_autorestart_continue();std::printf("  G16 Auto-restart/Cont    done\n");
    group17_status();              std::printf("  G17 Status/Readback      done\n");
    group18_read_fields();         std::printf("  G18 Read Fields          done\n");
    group19_reset();               std::printf("  G19 Reset                done\n");
    group20_dma_delay();           std::printf("  G20 DMA Delay            done\n");
    group21_timing_bytes();        std::printf("  G21 Timing Bytes         done\n");
    group22_edge();                std::printf("  G22 Edge Cases           done\n");

    std::printf("\n================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-24s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-24s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows (%zu, unrealisable with current C++ API):\n",
                    g_skipped.size());
        for (const auto& s : g_skipped) {
            std::printf("  %-8s %s\n", s.id.c_str(), s.reason.c_str());
        }
    }

    return g_fail > 0 ? 1 : 0;
}
