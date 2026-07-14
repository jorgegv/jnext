// Memory/MMU Compliance Test Runner
//
// Full rewrite (Task 5 Step 5 Phase 2, 2026-04-15) against the rebuilt
// doc/testing/MEMORY-MMU-TEST-PLAN-DESIGN.md. Every assertion cites a
// specific VHDL file and line range from the authoritative FPGA source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo — cited here for provenance, not edited).
//
// Ground rules (per doc/testing/UNIT-TEST-PLAN-EXECUTION.md):
//   * The VHDL is the oracle. The C++ implementation is never used to
//     derive an expected value.
//   * Every plan row is either a check() with an ID and a VHDL citation,
//     or a skip() with a one-line reason explaining what is unreachable
//     through the current Mmu public API.
//   * Plan rows whose facility does not exist on the Mmu class (no NR
//     0x8E/8F/8C/03/04 handler, no port 0xDFFD/0xEFF7, no machine-type-
//     aware ROM selection, no cycle-accurate contention inputs, no
//     bank5/bank7 flag accessor, no DivMMC/altrom priority plumbing) are
//     skipped rather than tested as tautologies.
//
// Run: ./build/test/mmu/mmu_test

#include "memory/mmu.h"
#include "memory/ram.h"
#include "memory/rom.h"
#include "memory/contention.h"
#include "core/nex_loader.h"
#include "core/nex_saver.h"
#include "core/szx_saver.h"
#include "core/z80_loader.h"
#include "core/tap_loader.h"
#include "core/tap_saver.h"
#include "core/saveable.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // mkstemp (POSIX) — temp files for loader round-trip tests

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

struct SkipNote {
    const char* id;
    const char* reason;
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

// Little-endian byte readers — used by the BOOT-SNAPSAVE round-trip
// checks to verify saver output against the exact byte offsets the real
// SzxLoader::parse_z80r/parse_spcr/parse_ramp (szx_loader.cpp) and
// NexLoader::load (nex_loader.cpp) read. mmu_test cannot link jnext_core
// so it cannot call those loaders' non-inline load()/apply() methods
// directly (see BOOT-SNAPSAVE-02/03 comments below) — this is the
// structural equivalent: the same offsets, hand-checked against both
// loader .cpp files.
uint16_t ru16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>(b[off]) | (static_cast<uint16_t>(b[off + 1]) << 8);
}
uint32_t ru32(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8)
         | (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

// Append an `ED ED <count> <value>` RLE run (split into <=255-byte chunks)
// to a byte vector under construction — the same encoding
// Z80Loader::decompress_v1()/decompress_page() decode. Used to build
// programmatic .z80 fixtures for BOOT-Z80-* below (no binaries checked in).
void append_rle_run(std::vector<uint8_t>& out, uint8_t value, size_t count) {
    while (count > 0) {
        size_t chunk = std::min(count, static_cast<size_t>(255));
        out.push_back(0xED);
        out.push_back(0xED);
        out.push_back(static_cast<uint8_t>(chunk));
        out.push_back(value);
        count -= chunk;
    }
}

// ── Fixture ──────────────────────────────────────────────────────────

// Default Ram size (2048 KB = 256 pages of 8K) matches the widest legal
// MMU logical page index (0x00..0xDF via VHDL zxnext.vhd:2964 address
// formula, which maps logical 0xDF to physical 0xFF).
struct Fixture {
    Ram ram;
    Rom rom;
    Mmu mmu;

    Fixture() : ram(), rom(), mmu(ram, rom) {
        // Tag every ROM page with (page<<4 | offset_lo) so ROM reads are
        // distinguishable from RAM in the subsequent tests.
        for (int page = 0; page < 8; ++page) {
            uint8_t* p = rom.page_ptr(page);
            if (p) {
                for (int i = 0; i < 8192; ++i) {
                    p[i] = static_cast<uint8_t>((page << 4) | (i & 0x0F));
                }
            }
        }
    }

    void fresh() { mmu.reset(); }
};

// ── Category 1: MMU slot assignment (NR 0x50-0x57) ────────────────────
// VHDL: zxnext.vhd:1018-1025 (NR 0x50..0x57 define MMU0..MMU7),
//       zxnext.vhd:2964 (mmu_A21_A13 formula, page→SRAM mapping),
//       zxnext.vhd:4611-4618 (reset values, used for fresh() baseline).

void test_cat1_slot_assignment() {
    set_group("Cat1 MMU slot assignment");

    // MMU-01..MMU-08: write one slot, observe the mapping lands on the
    // correct physical RAM page. We write via the slot, then read the
    // same byte back through ram.page_ptr(page) — this closes the loop
    // on "slot i → page P → physical RAM page P" per VHDL:2964.
    struct Row { const char* id; int slot; uint16_t addr; uint8_t page; };
    const Row rows[] = {
        {"MMU-01", 0, 0x0000, 0x00},
        {"MMU-02", 1, 0x2000, 0x01},
        {"MMU-03", 2, 0x4000, 0x04},
        {"MMU-04", 3, 0x6000, 0x05},
        {"MMU-05", 4, 0x8000, 0x0A},
        {"MMU-06", 5, 0xA000, 0x0B},
        // Page 0x0E is the dedicated bank-7 BRAM (Cat28 covers it);
        // use the plain page 0x0C here for the slot-6 mapping row.
        {"MMU-07", 6, 0xC000, 0x0C},
        {"MMU-08", 7, 0xE000, 0x0F},
    };
    for (const Row& r : rows) {
        Fixture f;
        f.fresh();
        f.mmu.set_page(r.slot, r.page);
        f.mmu.write(r.addr, 0xA5);
        const uint8_t phys = f.ram.page_ptr(r.page)[0];
        check(r.id,
              "slot write lands on physical RAM page — VHDL zxnext.vhd:2964 page→SRAM",
              phys == 0xA5,
              fmt("slot=%d page=0x%02X ram[%d][0]=0x%02X expected=0xA5",
                  r.slot, r.page, r.page, phys));
    }

    // MMU-09: NR 0x50 = 0xFF → slot maps to ROM (VHDL:2964 overflow bit
    // mmu_A21_A13(8)='1' when page>=0xE0, special-cased at 0xFF). In the
    // C++ surface the slot's read_ptr becomes null → reads return 0xFF.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0xFF);
        const uint8_t v = f.mmu.read(0x0000);
        check("MMU-09",
              "NR 0x50 = 0xFF → slot 0 reads as unmapped/ROM — VHDL zxnext.vhd:2964",
              v == 0xFF,
              fmt("read(0x0000)=0x%02X expected=0xFF (unmapped)", v));
    }

    // MMU-10: page 0x40 → physical SRAM 0x060000 (VHDL formula: sram =
    // (0x20 + 0x40) * 8K = 0x060000). In the C++ ram array that is
    // ram.page_ptr(0x40). Verify by round-trip.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(4, 0x40);
        f.mmu.write(0x8000, 0x10);
        const uint8_t v = f.ram.page_ptr(0x40)[0];
        check("MMU-10",
              "page 0x40 → ram page 0x40 (VHDL SRAM 0x060000) — zxnext.vhd:2964",
              v == 0x10,
              fmt("ram[0x40][0]=0x%02X expected=0x10", v));
    }

    // MMU-11: page 0xDF — highest valid RAM page. VHDL formula yields
    // mmu_A21_A13 = 0x0FF, which is still within the RAM half of SRAM
    // (bit 8 is 0). Round-trip verifies no wrap.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(4, 0xDF);
        f.mmu.write(0x8000, 0x11);
        const uint8_t v = f.ram.page_ptr(0xDF)[0];
        check("MMU-11",
              "page 0xDF accessible as highest RAM page — VHDL zxnext.vhd:2964",
              v == 0x11,
              fmt("ram[0xDF][0]=0x%02X expected=0x11", v));
    }

    // MMU-12: page 0xE0 on a RAM slot (slots 2..7) → floating bus.
    //
    // VHDL zxnext.vhd:3060-3061 — for cpu_a(15:14) ≠ "00" (slots 2..7):
    //   sram_pre_active <= (not mmu_A21_A13(8)) and (not mem_active_bank5)
    //                      and (not mem_active_bank7);
    // The mmu_A21_A13 formula at zxnext.vhd:2964 sets bit 8 = '1' whenever
    // mem_active_page(7 downto 5) = "111", i.e. page >= 0xE0. With
    // sram_pre_active = '0', the SRAM is inactive and the CPU read
    // receives the floating bus — 0xFF in practice, since nothing drives
    // the bus. Mmu::rebuild_ptr now gates RAM slots 2..7 on page >= 0xE0
    // and leaves read_ptr_ = nullptr; Mmu::read falls through to the
    // "unmapped → 0xFF" branch.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(4, 0xE0);          // slot 4 = 0x8000..0x9FFF
        const uint8_t v = f.mmu.read(0x8000);
        check("MMU-12",
              "page 0xE0 on RAM slot 4 → floating bus (0xFF) — VHDL zxnext.vhd:3060-3061",
              v == 0xFF,
              fmt("read(0x8000) with slot4=0xE0: 0x%02X expected=0xFF", v));
    }

    // MMU-13: read-back of NR 0x50..0x57 after writes (registers retain
    // last-written value). VHDL zxnext.vhd:1018-1025.
    {
        Fixture f;
        f.fresh();
        const uint8_t vals[8] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
        for (int i = 0; i < 8; ++i) f.mmu.set_page(i, vals[i]);
        bool ok = true;
        std::string bad;
        for (int i = 0; i < 8; ++i) {
            const uint8_t g = f.mmu.get_page(i);
            if (g != vals[i]) { ok = false; bad = fmt("slot %d: 0x%02X != 0x%02X", i, g, vals[i]); break; }
        }
        check("MMU-13",
              "NR 0x50-0x57 read-back — VHDL zxnext.vhd:4880 NR write handler",
              ok, bad);
    }

    // MMU-14: write/read pattern 0x20..0x27 across all slots (a second
    // independent pattern, distinct from MMU-13 to exercise a different
    // value set). Same VHDL citation.
    {
        Fixture f;
        f.fresh();
        for (int i = 0; i < 8; ++i) f.mmu.set_page(i, 0x20 + i);
        bool ok = true;
        std::string bad;
        for (int i = 0; i < 8; ++i) {
            const uint8_t g = f.mmu.get_page(i);
            if (g != (0x20 + i)) { ok = false; bad = fmt("slot %d: 0x%02X != 0x%02X", i, g, 0x20 + i); break; }
        }
        check("MMU-14",
              "NR 0x50-0x57 write pattern 0x20..0x27 — VHDL zxnext.vhd:4880 NR write handler",
              ok, bad);
    }

    // MMU-15: slot boundary at 0x1FFF / 0x2000 — VHDL slot decode is
    // cpu_a(15:13), so address 0x1FFF is in slot 0 and 0x2000 is in
    // slot 1. Map them to distinct pages and check that the writes hit
    // distinct physical pages.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x10);
        f.mmu.set_page(1, 0x20);
        f.mmu.write(0x1FFF, 0xAA);
        f.mmu.write(0x2000, 0x55);
        const uint8_t lo = f.ram.page_ptr(0x10)[0x1FFF];
        const uint8_t hi = f.ram.page_ptr(0x20)[0];
        check("MMU-15",
              "slot boundary 0x1FFF/0x2000 dispatch — VHDL zxnext.vhd:2952-2959 cpu_a(15:13) slot mux",
              lo == 0xAA && hi == 0x55,
              fmt("page 0x10[0x1FFF]=0x%02X (exp 0xAA), page 0x20[0]=0x%02X (exp 0x55)", lo, hi));
    }
}

// ── Category 2: MMU reset state ───────────────────────────────────────
// VHDL: zxnext.vhd:4610-4618. MMU0=0xFF, MMU1=0xFF, MMU2=0x0A,
// MMU3=0x0B, MMU4=0x04, MMU5=0x05, MMU6=0x00, MMU7=0x01.
//
// Known emulator bug — Task 2 backlog item 5: Mmu::reset() calls
// map_rom(0,0)/map_rom(1,1) after seeding the slots array, which clobbers
// slots_[0]/slots_[1] to 0/1. RST-01/RST-02 therefore fail against the
// VHDL specification; they are left failing as the plan's oracle.

void test_cat2_reset_state() {
    set_group("Cat2 MMU reset state");

    Fixture f;
    f.fresh();

    struct Row { const char* id; int slot; uint8_t expected; };
    const Row rows[] = {
        {"RST-01", 0, 0xFF},
        {"RST-02", 1, 0xFF},
        {"RST-03", 2, 0x0A},
        {"RST-04", 3, 0x0B},
        {"RST-05", 4, 0x04},
        {"RST-06", 5, 0x05},
        {"RST-07", 6, 0x00},
        {"RST-08", 7, 0x01},
    };
    for (const Row& r : rows) {
        const uint8_t g = f.mmu.get_page(r.slot);
        check(r.id,
              "MMU reset register — VHDL zxnext.vhd:4611-4618",
              g == r.expected,
              fmt("MMU%d = 0x%02X expected=0x%02X", r.slot, g, r.expected));
    }
}

// ── Category 3: Legacy 128K paging (port 0x7FFD) ──────────────────────
// VHDL: zxnext.vhd:3640-3814 port_7ffd handling. On write, MMU6/MMU7 are
// loaded from port_7ffd_bank & '0' / port_7ffd_bank & '1' — i.e. for a
// plain bank B, MMU6=2*B and MMU7=2*B+1.

void test_cat3_port_7ffd() {
    set_group("Cat3 port 0x7FFD");

    // P7F-01..P7F-08: bank select for all 8 banks.
    struct BankRow { const char* id; uint8_t port; uint8_t exp6; uint8_t exp7; };
    const BankRow banks[] = {
        {"P7F-01", 0x00, 0x00, 0x01},
        {"P7F-02", 0x01, 0x02, 0x03},
        {"P7F-03", 0x02, 0x04, 0x05},
        {"P7F-04", 0x03, 0x06, 0x07},
        {"P7F-05", 0x04, 0x08, 0x09},
        {"P7F-06", 0x05, 0x0A, 0x0B},
        {"P7F-07", 0x06, 0x0C, 0x0D},
        {"P7F-08", 0x07, 0x0E, 0x0F},
    };
    for (const BankRow& b : banks) {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(b.port);
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check(b.id,
              "0x7FFD bank select MMU6/MMU7 — VHDL zxnext.vhd:3640-3814",
              g6 == b.exp6 && g7 == b.exp7,
              fmt("port=0x%02X MMU6=0x%02X (exp 0x%02X) MMU7=0x%02X (exp 0x%02X)",
                  b.port, g6, b.exp6, g7, b.exp7));
    }

    // P7F-09: ROM 0 select — bit 4 = 0 → slots 0,1 remain in ROM path.
    // VHDL zxnext.vhd:3662-3734 (128K ROM select from port_7ffd(4)).
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        check("P7F-09",
              "port 0x7FFD bit 4=0 → ROM 0 (slots 0,1 read-only) — VHDL zxnext.vhd:4619-4670 port_memory_change",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // P7F-10: ROM 1 select — bit 4 = 1 → slots 0,1 still ROM path, but
    // pointing at ROM page 1. We only have is_slot_rom as observable.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x10);
        check("P7F-10",
              "port 0x7FFD bit 4=1 → ROM 1 (slots 0,1 read-only) — VHDL zxnext.vhd:4619-4670 port_memory_change",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // P7F-11: shadow screen select (port_7ffd_reg(3)). Mmu has no
    // shadow-screen accessor — that signal is consumed by the ULA, not
    // the memory subsystem API.
    // P7F-11 — COVERED AT test/ula/ula_test.cpp S15.02 (and tracked at
    // S15.04). port_7ffd(3) drives ULA VRAM fetch, not Mmu paging: the
    // signal routes straight to Ula in Emulator::init and is observed by
    // UlaBed rendering from bank 7 (page 14). Not a skip here — re-homed
    // to the ULA integration tier.

    // P7F-12: lock bit (port_7ffd_reg(5)) — subsequent bank switches
    // should be ignored. VHDL zxnext.vhd:3814.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);  // bank 0 + lock
        f.mmu.map_128k_bank(0x07);  // attempted switch to bank 7
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check("P7F-12",
              "0x7FFD bit 5 locks further 0x7FFD writes — VHDL zxnext.vhd:3814",
              g6 == 0x00 && g7 == 0x01,
              fmt("post-lock MMU6=0x%02X (exp 0x00) MMU7=0x%02X (exp 0x01)", g6, g7));
    }

    // P7F-13: locked writes leave MMU6/MMU7 unchanged — duplicate check
    // at a non-trivial starting bank to rule out "lock happens to set
    // bank=0". VHDL zxnext.vhd:3814.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x25);  // bank 5 + lock
        const uint8_t pre6 = f.mmu.get_page(6);
        const uint8_t pre7 = f.mmu.get_page(7);
        f.mmu.map_128k_bank(0x01);  // try bank 1
        const uint8_t post6 = f.mmu.get_page(6);
        const uint8_t post7 = f.mmu.get_page(7);
        check("P7F-13",
              "locked port writes do not change MMU6/MMU7 — VHDL zxnext.vhd:3814",
              pre6 == post6 && pre7 == post7 && pre6 == 0x0A && pre7 == 0x0B,
              fmt("pre=0x%02X,0x%02X post=0x%02X,0x%02X", pre6, pre7, post6, post7));
    }

    // P7F-14: NR 0x08 bit 7 clears the 7FFD paging lock. The Mmu exposes
    // unlock_paging() directly; Emulator's NR 0x08 write handler drives it
    // when bit 7 is set. VHDL zxnext.vhd:3654-3656 clears port_7ffd_reg(5)
    // (the lock source) when nr_08_we=1 AND nr_wr_dat(7)=1; the gated
    // writes inside map_128k_bank then take effect again.
    // The unit test drives Mmu::unlock_paging() directly — the full
    // NextReg→Mmu path is exercised by the emulator integration tests.
    {
        Fixture f;
        f.fresh();
        // (a) lock paging via port_7FFD bit 5 → second bank write ignored.
        f.mmu.map_128k_bank(0x20);       // bank 0 + lock
        const uint8_t pre6 = f.mmu.get_page(6);
        const uint8_t pre7 = f.mmu.get_page(7);
        f.mmu.map_128k_bank(0x07);       // attempted switch to bank 7 (ignored)
        const uint8_t mid6 = f.mmu.get_page(6);
        const uint8_t mid7 = f.mmu.get_page(7);
        // (b) unlock via NR 0x08 bit 7 → subsequent bank write takes effect.
        f.mmu.unlock_paging();
        f.mmu.map_128k_bank(0x07);       // now bank 7 should land
        const uint8_t post6 = f.mmu.get_page(6);
        const uint8_t post7 = f.mmu.get_page(7);
        check("P7F-14",
              "NR 0x08 bit 7 clears 7FFD paging lock — VHDL zxnext.vhd:3654-3656",
              pre6 == mid6 && pre7 == mid7 &&
              pre6 == 0x00 && pre7 == 0x01 &&
              post6 == 0x0E && post7 == 0x0F,
              fmt("pre=0x%02X,0x%02X mid=0x%02X,0x%02X post=0x%02X,0x%02X "
                  "(expected pre/mid=0x00,0x01; post=0x0E,0x0F)",
                  pre6, pre7, mid6, mid7, post6, post7));
    }

    // P7F-15: Mmu retains the raw last-written port_7ffd value.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0xC7);  // arbitrary non-trivial value
        const uint8_t v = f.mmu.port_7ffd();
        check("P7F-15",
              "port 0x7FFD register preserved — VHDL zxnext.vhd:3640",
              v == 0xC7,
              fmt("port_7ffd()=0x%02X expected=0xC7", v));
    }

    // P7F-16 / P7F-17 — re-homed 2026-04-24 from test/ula/ula_test.cpp §15
    // (S15.03/04) per doc/design/TASK-MMU-SHADOW-SCREEN-PLAN.md. Both rows
    // test the MMU-level source of the shadow-screen signal (port_7ffd_reg
    // bit 3, VHDL zxnext.vhd:3640 / :4453): `Mmu::shadow_screen_en()` is
    // the single producer the Emulator's 0x7FFD port handler reads and
    // forwards into `Ula::set_shadow_screen_en`. The downstream ULA-side
    // consequence (zxula.vhd:191 forces screen_mode="000" when shadow
    // asserts) is observed separately by INT-SHADOW-01 in
    // test/ula/ula_integration_test.cpp — that row exercises the full
    // port-dispatch → Mmu → Ula chain on a live Emulator.

    // P7F-16: shadow-screen signal tracks port_7ffd bit 3 across a
    // sequence of writes (baseline 0 → asserted 1 → cleared 0). VHDL
    // zxnext.vhd:3640 (port_7ffd_reg latches cpu_do); :4453 names the
    // shadow signal produced from reg(3).
    {
        Fixture f;
        f.fresh();
        const bool boot_clear = !f.mmu.shadow_screen_en();  // reset default
        f.mmu.map_128k_bank(0x08);                          // bit 3 asserted
        const bool on_after_set = f.mmu.shadow_screen_en();
        f.mmu.map_128k_bank(0x00);                          // bit 3 cleared
        const bool off_after_clear = !f.mmu.shadow_screen_en();
        check("P7F-16",
              "port_7ffd bit 3 sequence toggles Mmu::shadow_screen_en — "
              "VHDL zxnext.vhd:3640,:4453 (Ula screen_mode=000 gate at "
              "zxula.vhd:191 covered by ula_integration_test INT-SHADOW-01)",
              boot_clear && on_after_set && off_after_clear,
              fmt("boot_clear=%d on_after_set=%d off_after_clear=%d",
                  boot_clear, on_after_set, off_after_clear));
    }

    // P7F-17: bit-3 routing is orthogonal to the paging-bank bits (0-2)
    // and the ROM/alt-shadow selects (bits 4,6,7) — assert the accessor
    // isolates exactly bit 3 across non-trivial bytes. VHDL
    // zxnext.vhd:4453. Deliberately avoid bit 5 across all writes (lock
    // bit, VHDL :3814): a locked byte would stall subsequent map_128k_bank
    // writes and confuse the probe.
    {
        Fixture f;
        f.fresh();
        // 0xD7 = 1101_0111 — bit 3 CLEAR, bit 5 (lock) clear, other
        // ROM/bank bits asserted.
        f.mmu.map_128k_bank(0xD7);
        const bool off_on_d7 = !f.mmu.shadow_screen_en();
        // 0x08 = 0000_1000 — ONLY bit 3 asserted.
        f.mmu.map_128k_bank(0x08);
        const bool on_only_bit3 = f.mmu.shadow_screen_en();
        // 0xDF = 1101_1111 — bit 3 set, bit 5 clear, all other bits
        // asserted (mirror of 0xD7 with bit 3 flipped).
        f.mmu.map_128k_bank(0xDF);
        const bool on_on_df = f.mmu.shadow_screen_en();
        check("P7F-17",
              "Mmu::shadow_screen_en isolates port_7ffd bit 3 — "
              "VHDL zxnext.vhd:4453 (i_ula_shadow_en <= port_7ffd_reg(3))",
              off_on_d7 && on_only_bit3 && on_on_df,
              fmt("0xD7→off=%d 0x08→on=%d 0xDF→on=%d",
                  off_on_d7, on_only_bit3, on_on_df));
    }
}

// ── Category 4: Extended paging (port 0xDFFD) ─────────────────────────
// VHDL: zxnext.vhd:3640-3814. The Mmu class has no port 0xDFFD ingress;
// extra-bank bits are routed through port dispatch and then into the
// zxnext.vhd port_memory_change_dly logic, none of which is observable
// on the Mmu public API.

void test_cat4_port_dffd() {
    set_group("Cat4 port 0xDFFD");

    // DFFD feeds port_7ffd_bank composition per VHDL zxnext.vhd:3763-3766:
    //   bank(2:0) = port_7ffd_reg(2:0)
    //   bank(4:3) = port_dffd_reg(1:0)    (non-Pentagon, line 3764)
    //   bank(5)   = port_dffd_reg(2)      (non-Pentagon, line 3765)
    //   bank(6)   = port_dffd_reg(3)      (non-Pentagon, non-Profi, line 3766)
    // Slots 6/7 then load from port_7ffd_bank & '0' / & '1' via VHDL:4679-4680.
    // JNEXT is hard-wired non-Pentagon/non-Profi (VHDL:3797 forces profi='0');
    // the composition becomes simply bank = (7FFD & 7) | ((DFFD & 0x0F) << 3).

    // DFF-01: DFFD bit 0 → bank(3)=1 → bank 8 → MMU6=0x10, MMU7=0x11.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        f.mmu.write_port_dffd(0x01);
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check("DFF-01",
              "DFFD bit 0 → port_7ffd_bank(3) — VHDL zxnext.vhd:3764,4679-4680",
              g6 == 0x10 && g7 == 0x11,
              fmt("MMU6=0x%02X (exp 0x10) MMU7=0x%02X (exp 0x11)", g6, g7));
    }

    // DFF-02: DFFD bit 1 → bank(4)=1 → bank 16 → MMU6=0x20, MMU7=0x21.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        f.mmu.write_port_dffd(0x02);
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check("DFF-02",
              "DFFD bit 1 → port_7ffd_bank(4) — VHDL zxnext.vhd:3764,4679-4680",
              g6 == 0x20 && g7 == 0x21,
              fmt("MMU6=0x%02X (exp 0x20) MMU7=0x%02X (exp 0x21)", g6, g7));
    }

    // DFF-03: DFFD bit 2 → bank(5)=1 → bank 32 → MMU6=0x40, MMU7=0x41.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        f.mmu.write_port_dffd(0x04);
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check("DFF-03",
              "DFFD bit 2 → port_7ffd_bank(5) — VHDL zxnext.vhd:3765,4679-4680",
              g6 == 0x40 && g7 == 0x41,
              fmt("MMU6=0x%02X (exp 0x40) MMU7=0x%02X (exp 0x41)", g6, g7));
    }

    // DFF-04: DFFD bit 3 → bank(6)=1 → bank 64 → MMU6=0x80, MMU7=0x81.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        f.mmu.write_port_dffd(0x08);
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check("DFF-04",
              "DFFD bit 3 → port_7ffd_bank(6) — VHDL zxnext.vhd:3766,4679-4680",
              g6 == 0x80 && g7 == 0x81,
              fmt("MMU6=0x%02X (exp 0x80) MMU7=0x%02X (exp 0x81)", g6, g7));
    }

    // DFF-05: Max composition: 7FFD=0x07, DFFD=0x0F → bank=127 →
    // MMU6=0xFE, MMU7=0xFF. Verifies the full 7-bit bank.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x07);
        f.mmu.write_port_dffd(0x0F);
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check("DFF-05",
              "DFFD=0x0F + 7FFD=0x07 → bank 127 — VHDL zxnext.vhd:3763-3766,4679-4680",
              g6 == 0xFE && g7 == 0xFF,
              fmt("MMU6=0x%02X (exp 0xFE) MMU7=0x%02X (exp 0xFF)", g6, g7));
    }

    // DFF-06: Locked by 7FFD bit 5 — DFFD write ignored. VHDL zxnext.vhd:3691
    // gates port_dffd write on (port_7ffd_locked='0' OR profi='1'). JNEXT is
    // non-Profi (VHDL:3797), so lock alone is sufficient to block. Register
    // stays at its pre-lock value.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);           // lock (bit 5), bank 0
        const uint8_t pre = f.mmu.port_dffd_reg();
        f.mmu.write_port_dffd(0x01);         // attempted extra-bank set
        const uint8_t post = f.mmu.port_dffd_reg();
        const uint8_t g6 = f.mmu.get_page(6);
        const uint8_t g7 = f.mmu.get_page(7);
        check("DFF-06",
              "DFFD write gated by 7FFD lock — VHDL zxnext.vhd:3691",
              pre == 0 && post == 0 && g6 == 0x00 && g7 == 0x01,
              fmt("pre=0x%02X post=0x%02X MMU6=0x%02X MMU7=0x%02X "
                  "(want all zero/bank-0)", pre, post, g6, g7));
    }

    // DFF-07: DFFD bit 4 is the Profi DFFD override (VHDL:3769 feeds the
    // port_7ffd_locked signal via nr_8f_mapping_mode_profi). VHDL:3797
    // forces profi='0' unconditionally, so bit 4 is stored (VHDL:3693 →
    // cpu_do(4:0)) but has no downstream banking effect — bank composition
    // at VHDL:3763-3766 is unchanged. Observable: port_dffd_reg() returns
    // the stored bit 4 = 1, MMU6/7 stay at the 7FFD=0 baseline.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        f.mmu.write_port_dffd(0x10);
        const uint8_t reg = f.mmu.port_dffd_reg();
        const uint8_t g6  = f.mmu.get_page(6);
        const uint8_t g7  = f.mmu.get_page(7);
        check("DFF-07",
              "DFFD(4) stored but no effect on bank (Profi forced off) "
              "— VHDL zxnext.vhd:3693,3797",
              reg == 0x10 && g6 == 0x00 && g7 == 0x01,
              fmt("port_dffd_reg=0x%02X MMU6=0x%02X MMU7=0x%02X "
                  "(exp 0x10, 0x00, 0x01)", reg, g6, g7));
    }

    // DFF-08: Soft reset CLEARS port_dffd_reg + MMU6/7 falls back to
    // RESET_PAGES. VHDL zxnext.vhd:3686-3688
    //   if reset = '1' then
    //      port_dffd_reg <= (others => '0');
    // Inside zxnext.vhd `reset = i_RESET = reset_hard OR reset_soft`
    // (zxnext_top_issue5.vhd:880 → :2384 → zxnext.vhd:1730), so the
    // clear fires on BOTH reset types. The previous "Branch C / hard-only"
    // claim was a misreading of the VHDL — corrected G46(b) 2026-05-08
    // when the NextZXOS supervisor's deliberate NR 0x02 ← 0x01 soft reset
    // (bank 3 PC=$3BE8) failed to clear rom_bank to 0, landing the CPU
    // in bank 2's `NOP; JR $0000` infinite-loop trap.
    //
    // Write a non-zero DFFD + 7FFD pair, soft-reset, and assert the
    // register clears to 0 and MMU6/MMU7 fall back to the RESET_PAGES
    // seed (slots_[6]=0x00, slots_[7]=0x01).
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x07);
        f.mmu.write_port_dffd(0x0F);
        const uint8_t pre_reg = f.mmu.port_dffd_reg();
        const uint8_t pre_g6  = f.mmu.get_page(6);
        const uint8_t pre_g7  = f.mmu.get_page(7);
        f.mmu.reset(false);                  // soft reset
        const uint8_t post_reg = f.mmu.port_dffd_reg();
        const uint8_t post_g6  = f.mmu.get_page(6);
        const uint8_t post_g7  = f.mmu.get_page(7);
        check("DFF-08",
              "soft reset CLEARS port_dffd_reg + MMU6/7 reset to seed "
              "— VHDL zxnext.vhd:3686-3688 (reset = hard OR soft)",
              pre_reg == 0x0F && pre_g6 == 0xFE && pre_g7 == 0xFF &&
              post_reg == 0x00 && post_g6 == 0x00 && post_g7 == 0x01,
              fmt("pre reg=0x%02X MMU6=0x%02X MMU7=0x%02X / "
                  "post reg=0x%02X MMU6=0x%02X MMU7=0x%02X "
                  "(exp pre 0x0F/0xFE/0xFF, post 0x00/0x00/0x01)",
                  pre_reg, pre_g6, pre_g7, post_reg, post_g6, post_g7));
    }

    // DFF-09 — VHDL zxnext.vhd:877 (`signal port_dffd_reg_6 : std_logic;`
    // — single-bit flip-flop separate from the 5-bit port_dffd_reg
    // vector), :3693-3694 (write process: `port_dffd_reg <= cpu_do(4
    // downto 0)` AND `port_dffd_reg_6 <= cpu_do(6)` in the same `elsif
    // port_dffd_wr='1'` branch), :4314 (Multiface +3 read-mux consumer:
    // `'0' & port_dffd_reg_6 & '0' & port_dffd_reg`). The Mmu now
    // latches bit 6 into port_dffd_reg_6_ and exposes port_dffd_reg_6()
    // for the Multiface read-mux. Verify: (a) bit 6 round-trips through
    // the accessor; (b) bit 6 is NOT folded into the 5-bit port_dffd_reg
    // vector; (c) hard reset clears it. (G148)
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        // Write DFFD with bit 6 = 1, bits 4:0 = 0x0A. Bit 5 and 7 must
        // be ignored by the latch path; verify DFFD readback is the
        // 5-bit field, the bit-6 accessor is true, and the two storage
        // domains are independent.
        f.mmu.write_port_dffd(0xCA);            // 1100_1010
        const uint8_t reg_after_set = f.mmu.port_dffd_reg();
        const bool    b6_after_set  = f.mmu.port_dffd_reg_6();
        // Re-write with bit 6 = 0 keeping the 5-bit field non-zero, to
        // prove the bit-6 latch is independent of the vector storage.
        f.mmu.write_port_dffd(0x0A);            // 0000_1010
        const uint8_t reg_after_clr = f.mmu.port_dffd_reg();
        const bool    b6_after_clr  = f.mmu.port_dffd_reg_6();
        // Hard reset clears bit 6 (VHDL :3686-3689 same `reset='1'` clause
        // as port_dffd_reg).
        f.mmu.write_port_dffd(0x40);            // bit 6 = 1
        const bool b6_pre_hard = f.mmu.port_dffd_reg_6();
        f.mmu.reset(true);
        const bool b6_post_hard = f.mmu.port_dffd_reg_6();
        check("DFF-09",
              "DFFD bit 6 latched into port_dffd_reg_6, separate from "
              "5-bit port_dffd_reg, hard-reset cleared "
              "— VHDL zxnext.vhd:877, 3693-3694, 3686-3689, 4314",
              reg_after_set == 0x0A && b6_after_set == true &&
              reg_after_clr == 0x0A && b6_after_clr == false &&
              b6_pre_hard == true && b6_post_hard == false,
              fmt("set: dffd=0x%02X b6=%d / clr: dffd=0x%02X b6=%d / "
                  "hard reset: pre b6=%d post b6=%d",
                  reg_after_set, static_cast<int>(b6_after_set),
                  reg_after_clr, static_cast<int>(b6_after_clr),
                  static_cast<int>(b6_pre_hard),
                  static_cast<int>(b6_post_hard)));
    }
}

// ── Category 5: +3 paging (port 0x1FFD) ───────────────────────────────
// VHDL: zxnext.vhd:3640-3814, zxnext.vhd:4623-4632 special-mode MMU
// layouts. Mmu exposes map_plus3_bank() which implements the special
// mode and the ROM-select combination with port_7ffd_(4).

void test_cat5_port_1ffd() {
    set_group("Cat5 port 0x1FFD");

    // P1F-01: +3 ROM 0 — port_1ffd bit 2 = 0 AND port_7ffd bit 4 = 0.
    // VHDL zxnext.vhd:3662 port_1ffd_rom = (1ffd(2), 7ffd(4)).
    // Slots 0/1 should be in ROM path, mapped to ROM page 0.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);  // 7ffd(4)=0
        f.mmu.map_plus3_bank(0x00); // 1ffd(2)=0, non-special
        check("P1F-01",
              "+3 ROM 0: slots 0,1 read-only after 1ffd=0 & 7ffd(4)=0 — VHDL zxnext.vhd:4619-4670",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // P1F-02: +3 ROM 1 — port_7ffd bit 4 = 1 with 1ffd=0.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_plus3_bank(0x00);
        f.mmu.map_128k_bank(0x10);
        check("P1F-02",
              "+3 ROM 1: slots 0,1 read-only after 7ffd(4)=1 — VHDL zxnext.vhd:4619-4670",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // P1F-03: +3 ROM 2 — port_1ffd bit 2 = 1, 7ffd(4)=0.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x00);
        f.mmu.map_plus3_bank(0x04);
        check("P1F-03",
              "+3 ROM 2: slots 0,1 read-only after 1ffd(2)=1 — VHDL zxnext.vhd:4619-4670",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // P1F-04: +3 ROM 3 — both bits set.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x10);
        f.mmu.map_plus3_bank(0x04);
        check("P1F-04",
              "+3 ROM 3: slots 0,1 read-only after 1ffd(2)=1 & 7ffd(4)=1 — VHDL zxnext.vhd:4619-4670",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // P1F-05: special mode enable — 1ffd(0)=1 — all 8 MMU slots become
    // RAM configurations. VHDL zxnext.vhd:4623-4632.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_plus3_bank(0x01);
        bool all_ram = true;
        std::string bad;
        for (int i = 0; i < 8; ++i) {
            if (f.mmu.is_slot_rom(i)) {
                all_ram = false;
                bad = fmt("slot %d still ROM", i);
                break;
            }
        }
        check("P1F-05",
              "+3 special mode: all 8 slots RAM — VHDL zxnext.vhd:4623-4632",
              all_ram, bad);
    }

    // P1F-06: locked by 7FFD bit 5 — 1FFD write should be ignored. VHDL
    // zxnext.vhd:3814.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);  // lock
        f.mmu.map_plus3_bank(0x01); // attempted special mode
        // Slots 0,1 should remain as reset (ROM path), not flipped to
        // the all-RAM special layout.
        check("P1F-06",
              "port 0x1FFD write gated by 0x7FFD bit 5 lock — VHDL zxnext.vhd:3814",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // WONT P1F-07 — port_1FFD bit 3 is the +3 disk motor enable. This
    // bit is routed to the +3 FDC, not to the Mmu. jnext does not model
    // the +3 FDC at all (no disk drive emulation). Category WONT
    // (explicit decision record; refinement of unobservable-audit G).
    // **Decision (2026-04-21)**: won't implement the +3 FDC for this
    // project; NextZXOS and typical software work with jnext's tape /
    // SD / NEX loaders. If +3 FDC emulation ever lands, re-home this
    // row to an fdc_test.cpp.
}

// ── Category 6: +3 special paging modes ───────────────────────────────
// VHDL: zxnext.vhd:4623-4632. Four fixed MMU configurations decoded from
// port_1ffd_reg(2:1) when port_1ffd_reg(0)=1.

void test_cat6_plus3_special() {
    set_group("Cat6 +3 special paging");

    struct SpeRow {
        const char* id;
        uint8_t     port_1ffd;
        uint8_t     expected[8];
    };
    const SpeRow configs[] = {
        // bits=00: MMU = 0x00..0x07 (VHDL:4625-4632 with R21=0, R1not2=0)
        {"SPE-01", 0x01, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}},
        // bits=01: R21=1, R21_and=0, R1not2=1 → MMU = 0x08..0x0F
        {"SPE-02", 0x03, {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}},
        // bits=10: R21=1, R21_and=0, R1not2=0 → MMU6/MMU7 still 0x06/0x07
        {"SPE-03", 0x05, {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x06, 0x07}},
        // bits=11: R21=1, R21_and=1, R1not2=0 → MMU2/MMU3 = 0x0E/0x0F
        {"SPE-04", 0x07, {0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D, 0x06, 0x07}},
    };

    for (const SpeRow& cfg : configs) {
        Fixture f;
        f.fresh();
        f.mmu.map_plus3_bank(cfg.port_1ffd);
        bool ok = true;
        std::string bad;
        for (int i = 0; i < 8; ++i) {
            const uint8_t g = f.mmu.get_page(i);
            if (g != cfg.expected[i]) {
                ok = false;
                bad = fmt("slot %d: 0x%02X != expected 0x%02X", i, g, cfg.expected[i]);
                break;
            }
        }
        check(cfg.id,
              "+3 special paging layout — VHDL zxnext.vhd:4623-4632",
              ok, bad);
    }

    // SPE-05: exiting special mode restores ROM in slots 0,1 — VHDL
    // zxnext.vhd:4634 else-branch re-asserts MMU0/1 = 0xFF when
    // port_eff7_reg_3=0 (default).
    {
        Fixture f;
        f.fresh();
        f.mmu.map_plus3_bank(0x01);  // enter special
        f.mmu.map_plus3_bank(0x00);  // exit special
        check("SPE-05",
              "exit +3 special mode → slots 0,1 return to ROM — VHDL zxnext.vhd:4634",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }
}

// ── Category 7: Paging lock ───────────────────────────────────────────
// VHDL: zxnext.vhd:3814 port_7ffd_locked gating, zxnext.vhd:4623-4632
// lock override via Pentagon-1024.

void test_cat7_paging_lock() {
    set_group("Cat7 paging lock");

    // LCK-01: 7FFD bit 5 locks 7FFD writes (duplicate coverage with
    // P7F-12/13 but at a different bank pair — exercise lock-after-bank-0).
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);  // lock
        f.mmu.map_128k_bank(0x03);  // bank 3 attempt
        check("LCK-01",
              "7FFD(5) locks further 0x7FFD writes — VHDL zxnext.vhd:3814",
              f.mmu.get_page(6) == 0x00 && f.mmu.get_page(7) == 0x01,
              fmt("MMU6=0x%02X MMU7=0x%02X", f.mmu.get_page(6), f.mmu.get_page(7)));
    }

    // LCK-02: 7FFD bit 5 also locks 1FFD writes.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);
        f.mmu.map_plus3_bank(0x01);  // would normally enter special RAM mode
        // If locked, slots 0,1 remain ROM (reset state preserved).
        check("LCK-02",
              "7FFD(5) locks further 0x1FFD writes — VHDL zxnext.vhd:3814",
              f.mmu.is_slot_rom(0) && f.mmu.is_slot_rom(1),
              fmt("slot0 rom=%d slot1 rom=%d", f.mmu.is_slot_rom(0), f.mmu.is_slot_rom(1)));
    }

    // LCK-03: 7FFD bit 5 locks DFFD writes. VHDL zxnext.vhd:3691 gates
    // the port_dffd write on `port_7ffd_locked='0' OR profi='1'`; JNEXT is
    // non-Profi (VHDL:3797), so the lock alone blocks the write. Observable
    // via port_dffd_reg() staying at its pre-lock value, and MMU6/7 holding
    // their pre-lock bank=0 pages (0x00/0x01) rather than the composed
    // bank=8 pages (0x10/0x11) that an accepted write would have produced.
    // Sister assertion to DFF-06 — duplicate coverage at the category level.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);           // lock (bit 5), bank 0
        f.mmu.write_port_dffd(0x01);         // would set extra bank bit 0
        const uint8_t reg = f.mmu.port_dffd_reg();
        const uint8_t g6  = f.mmu.get_page(6);
        const uint8_t g7  = f.mmu.get_page(7);
        check("LCK-03",
              "7FFD(5) lock blocks DFFD write — VHDL zxnext.vhd:3691",
              reg == 0 && g6 == 0x00 && g7 == 0x01,
              fmt("port_dffd_reg=0x%02X MMU6=0x%02X MMU7=0x%02X", reg, g6, g7));
    }

    // LCK-04: NR 0x08 bit 7 clears the lock — exercised via Mmu's
    // unlock_paging() entry point. VHDL zxnext.vhd:3654-3656 clears
    // port_7ffd_reg(5) on nr_08_we=1 AND nr_wr_dat(7)=1, dropping
    // port_7ffd_locked at zxnext.vhd:3769; subsequent port_7FFD writes
    // then pass through the map_128k_bank gate. The full NextReg→Mmu
    // wiring is exercised at the emulator integration level; this unit
    // test covers the Mmu-side lock/unlock state machine.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);       // bank 0 + lock
        // Sanity: lock is active — a subsequent port_7FFD write is ignored.
        f.mmu.map_128k_bank(0x03);
        const uint8_t locked6 = f.mmu.get_page(6);
        // Unlock (what NR 0x08 bit 7 drives at the emulator level).
        f.mmu.unlock_paging();
        f.mmu.map_128k_bank(0x03);       // bank 3 should now land
        const uint8_t unlocked6 = f.mmu.get_page(6);
        check("LCK-04",
              "NR 0x08 bit 7 unlock path drops 7FFD lock — VHDL zxnext.vhd:3654-3656",
              locked6 == 0x00 && unlocked6 == 0x06,
              fmt("locked MMU6=0x%02X (exp 0x00), unlocked MMU6=0x%02X (exp 0x06)",
                  locked6, unlocked6));
    }

    // LCK-05: Pentagon-1024 overrides the 7FFD lock. VHDL zxnext.vhd:3769
    //   port_7ffd_locked <= '0' when (pentagon_1024_en = '1') or ...
    //                       else port_7ffd_reg(5);
    // with pentagon_1024_en gated on mode="11" AND NOT EFF7(2) (VHDL:3801).
    // Phase 2 B landed Mmu::write_nr_8f + effective_paging_locked() so the
    // override is now observable end-to-end.
    {
        Fixture f;
        f.fresh();
        // Baseline: lock via 7FFD(5)=1, bank 0.
        f.mmu.map_128k_bank(0x20);
        // Confirm the lock actually blocks further legacy writes (sanity).
        f.mmu.map_128k_bank(0x03);
        const uint8_t locked_mmu6 = f.mmu.get_page(6);
        // Enable Pentagon-1024 mapping mode — VHDL:3790-3792 stores
        // nr_wr_dat(1:0) into nr_8f_mapping_mode.
        f.mmu.write_nr_8f(0x03);
        // After Pentagon-1024-en, effective_paging_locked() drops to 0
        // (VHDL:3769 branch). A 7FFD write now takes effect. In Pentagon-
        // 1024 mode, bank(2:0)=7ffd(2:0), bank(4:3)=7ffd(7:6),
        // bank(5)=pentagon_1024_en AND 7ffd(5). Here 7FFD=0x07 → bank=7.
        f.mmu.map_128k_bank(0x07);
        const uint8_t unlocked_mmu6 = f.mmu.get_page(6);
        const uint8_t unlocked_mmu7 = f.mmu.get_page(7);
        check("LCK-05",
              "pentagon_1024_en drops port_7ffd_locked — VHDL zxnext.vhd:3769,3801",
              locked_mmu6 == 0x00 && unlocked_mmu6 == 0x0E && unlocked_mmu7 == 0x0F,
              fmt("locked MMU6=0x%02X (exp 0x00), unlocked MMU6=0x%02X MMU7=0x%02X (exp 0x0E/0x0F)",
                  locked_mmu6, unlocked_mmu6, unlocked_mmu7));
    }

    // LCK-06: direct MMU slot writes (NR 0x50..0x57) bypass the paging
    // lock. VHDL: NR writes go through nextreg_register_select, not
    // through port_7ffd_locked gating.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x20);  // lock paging ports
        f.mmu.set_page(6, 0x10);    // direct NR 0x56 write
        check("LCK-06",
              "direct NR 0x50-0x57 bypasses 7FFD(5) lock — VHDL zxnext.vhd:4880 NR write handler",
              f.mmu.get_page(6) == 0x10,
              fmt("MMU6=0x%02X expected=0x10", f.mmu.get_page(6)));
    }

    // LCK-07: NR 0x8E write bypasses the 7FFD paging lock. VHDL
    //   zxnext.vhd:3662-3670 (7FFD reg), 3696-3704 (DFFD reg),
    //   3726-3734 (1FFD reg) — the three `elsif nr_8e_we='1'` branches are
    //   NOT gated on port_7ffd_locked. Phase 2 B landed Mmu::write_nr_8e
    //   so the bypass is now observable.
    {
        Fixture f;
        f.fresh();
        // Lock paging: 7FFD=0x20 sets bit 5 (bank 0 + lock).
        f.mmu.map_128k_bank(0x20);
        // Sanity: a legacy 7FFD write is now blocked.
        f.mmu.map_128k_bank(0x03);
        const uint8_t pre_mmu6 = f.mmu.get_page(6);
        // NR 0x8E write with bit 3 = 1: 7FFD(2:0) <- bits(6:4) = 011 = 3;
        // DFFD(0) <- bit 7 = 1; DFFD(3:1) <- 0. Standard-mode bank
        // composition: bank(2:0)=3, bank(4:3)=DFFD(1:0)=0b01, bank(5)=0,
        // bank(6)=0 → bank = 3 | (1<<3) = 0x0B. MMU6=0x16, MMU7=0x17.
        // 0xB8 = 1011_1000 (bit7=1, bit3=1, bits 6:4=011, others 0).
        f.mmu.write_nr_8e(0xB8);
        const uint8_t post_mmu6 = f.mmu.get_page(6);
        const uint8_t post_mmu7 = f.mmu.get_page(7);
        check("LCK-07",
              "NR 0x8E write bypasses 7FFD(5) lock — VHDL zxnext.vhd:3662,3696,3726",
              pre_mmu6 == 0x00 && post_mmu6 == 0x16 && post_mmu7 == 0x17,
              fmt("pre MMU6=0x%02X (exp 0x00), post MMU6=0x%02X MMU7=0x%02X (exp 0x16/0x17)",
                  pre_mmu6, post_mmu6, post_mmu7));
    }
}

// ── Category 8: NR 0x8E unified paging ────────────────────────────────
// VHDL: zxnext.vhd:3662-3734 (write decomposition to 7FFD/DFFD/1FFD),
//       zxnext.vhd:6158-6159 (read-back composition).
// Phase 2 B: Mmu::write_nr_8e / read_nr_8e on the public API.

void test_cat8_nr_8e() {
    set_group("Cat8 NR 0x8E unified paging");

    // N8E-01: Bank-select path (bit 3 = 1). VHDL:3665 7ffd(2:0) <=
    //   nr_wr_dat(6:4). VHDL:3703 dffd(2:0) <= "00" & nr_wr_dat(7).
    //   Write 0xB8 = 1011_1000 (bit 3=1 bank mode, bits 6:4=011, bit 7=1):
    //     7FFD(2:0) <- 0b011 = 3  (bit6=0, bit5=1, bit4=1)
    //     DFFD(0)   <- 1
    //     DFFD(2:1) <- 00
    //     DFFD(3)   <- 0 (clear-on-write, N8E-06)
    //   Bank = 7FFD(2:0) | (DFFD(1:0)<<3) | (DFFD(2)<<5) | (DFFD(3)<<6)
    //        = 3 | (0b01 << 3) = 0x0B → MMU6=0x16, MMU7=0x17.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8e(0xB8);
        const uint8_t mmu6 = f.mmu.get_page(6);
        const uint8_t mmu7 = f.mmu.get_page(7);
        const uint8_t dffd = f.mmu.port_dffd_reg();
        check("N8E-01",
              "NR 0x8E bit 3=1 stores 7FFD(2:0)<-bits(6:4), DFFD(0)<-bit7 — "
              "VHDL zxnext.vhd:3662-3670,3696-3704",
              mmu6 == 0x16 && mmu7 == 0x17 && (dffd & 0x01) == 0x01,
              fmt("MMU6=0x%02X MMU7=0x%02X DFFD=0x%02X "
                  "(exp 0x16/0x17, DFFD(0)=1)", mmu6, mmu7, dffd));
    }

    // N8E-02: ROM-select path (bit 3=0, bit 2=0 → 7FFD(4) <- bit 0).
    //   VHDL:3669 `if nr_wr_dat(2) = '0' then 7FFD(4) <= nr_wr_dat(0)`.
    //   Write 0x01 = 0000_0001: bit 3=0 (no bank), bit 2=0 (allow 7FFD(4)
    //   update), bit 0=1 → 7FFD(4)=1. Observable via current_rom_bank()
    //   which reads 7FFD(4) (zxnext.vhd:3772 port_1ffd_rom = 1ffd(2) & 7ffd(4)).
    //   Also 1FFD(2) <- bit 1 = 0; 1FFD(1) <- bit 0 = 1; 1FFD(0) <- bit 2 = 0.
    //   current_rom_bank = 1ffd(2)<<1 | 7ffd(4) = 0<<1 | 1 = 1.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8e(0x01);
        const uint8_t rom_bank = f.mmu.current_rom_bank();
        const uint8_t p7ffd    = f.mmu.port_7ffd();
        check("N8E-02",
              "NR 0x8E bit 2=0 routes bit 0 to 7FFD(4) (ROM-high) — "
              "VHDL zxnext.vhd:3668-3670",
              rom_bank == 0x01 && (p7ffd & 0x10) != 0,
              fmt("rom_bank=%u 7FFD=0x%02X (exp rom_bank=1, 7FFD(4)=1)",
                  rom_bank, p7ffd));
    }

    // N8E-03: Special-mode via NR 0x8E (bit 2=1). VHDL:3734
    //   1ffd_reg(0) <= nr_wr_dat(2). Write 0x04 = 0000_0100:
    //     1FFD(0) <- 1 (special mode enable)
    //     1FFD(2) <- bit 1 = 0
    //     1FFD(1) <- bit 0 = 0
    //     7FFD(4) <- NOT updated (bit 2 = 1 gates it, VHDL:3668)
    //   Observable via port_1ffd() bit 0.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8e(0x04);
        const uint8_t p1ffd = f.mmu.port_1ffd();
        check("N8E-03",
              "NR 0x8E bit 2=1 sets 1FFD(0) special-mode — "
              "VHDL zxnext.vhd:3734",
              (p1ffd & 0x01) == 0x01,
              fmt("1FFD=0x%02X (exp bit 0 = 1)", p1ffd));
    }

    // N8E-04: Special + config bits (bit 2=1 + bits 1:0). VHDL:3732-3734
    //   port_1ffd_reg(2) <= nr_wr_dat(1), port_1ffd_reg(1) <= nr_wr_dat(0),
    //   port_1ffd_reg(0) <= nr_wr_dat(2).
    //   Write 0x07 = 0000_0111: 1FFD(2)=1, 1FFD(1)=1, 1FFD(0)=1 →
    //   port_1ffd() bits 2:0 = 0b111 = 7.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8e(0x07);
        const uint8_t p1ffd = f.mmu.port_1ffd();
        check("N8E-04",
              "NR 0x8E bit 2:0=111 sets 1FFD bits 2:1:0 in that VHDL order — "
              "VHDL zxnext.vhd:3732-3734",
              (p1ffd & 0x07) == 0x07,
              fmt("1FFD=0x%02X (exp bits 2:0 = 111)", p1ffd));
    }

    // N8E-05: Read-back format. VHDL zxnext.vhd:6158-6159
    //   port_253b_dat <= dffd(0) & 7ffd(2:0) & '1' & 1ffd(0) & 1ffd(2) &
    //     ((7ffd(4) AND NOT 1ffd(0)) OR (1ffd(1) AND 1ffd(0)));
    // Case A: set 7FFD(2:0)=0b101, 7FFD(4)=1, DFFD(0)=1, 1FFD all 0.
    //   Read = 1 & 101 & 1 & 0 & 0 & (1 AND 1) = 1011_1001 = 0xB9.
    // Case B: same but 1FFD(0)=1 (special) → bit 0 = (1ffd(1) AND 1ffd(0))
    //   = (0 AND 1) = 0 → read = 1 & 101 & 1 & 1 & 0 & 0 = 1011_1100 = 0xBC.
    //   Bit 3 of the read is always '1' (VHDL spec sentinel).
    {
        Fixture f;
        f.fresh();
        // Drive state with NR 0x8E writes: bit 3=1 + bits 7/6/5/4 = 1/1/0/1
        // gives 7FFD(2:0) = bits(6:4) = 0b101 and DFFD(0)=1.
        // 0x8E write 0xD8 = 1101_1000 → bits 6:4 = 101, bit 7=1, bit 3=1.
        f.mmu.write_nr_8e(0xD8);
        // Now set 7FFD(4) via a bit2=0,bit0=1 write that preserves bank.
        // 0x8E write 0x01 = bit 2=0 allows 7FFD(4) <- bit 0 = 1.
        f.mmu.write_nr_8e(0x01);
        // Case A: 1FFD(0)=0 is the default. Verify read-back.
        const uint8_t read_a = f.mmu.read_nr_8e();
        // 0x01 also sets 1FFD(1)=1 (bit 0 -> 1FFD(1)). So cfg1=1 but
        // 1ffd(0)=0 → bit 0 = 7ffd(4) AND NOT 0 = 1. Read:
        //   {dffd(0)=1, 7ffd(2)=1, 7ffd(1)=0, 7ffd(0)=1, '1',
        //    1ffd(0)=0, 1ffd(2)=0, bit0=1} = 1010_1_001 = 0xA9.
        // Wait: 7ffd(2:0)=0b101 → 7ffd(2)=1, 7ffd(1)=0, 7ffd(0)=1.
        // Expected: bit7=1 bit6=1 bit5=0 bit4=1 bit3=1 bit2=0 bit1=0 bit0=1
        //           = 1101_1001 = 0xD9.
        check("N8E-05a",
              "NR 0x8E read-back {dffd(0),7FFD(2:0),1,1FFD(0),1FFD(2),bit0} — "
              "VHDL zxnext.vhd:6158-6159",
              read_a == 0xD9,
              fmt("read=0x%02X (exp 0xD9)", read_a));
        // Case B: enable special mode (1ffd(0)=1) — read bit 2 goes to 1,
        // and bit 0 flips to cfg1. Write 0x8E=0x05 (bit 2=1, bit 0=1).
        // This update preserves existing 7FFD(4) (bit 2=1 gates it) and
        // rewrites 1FFD bits: 1FFD(2)=bit1=0, 1FFD(1)=bit0=1, 1FFD(0)=bit2=1.
        // Bank select (bit 3=0) does NOT touch 7FFD(2:0) or DFFD. So 7FFD
        // keeps (2:0)=0b101, DFFD(0)=1, 7FFD(4)=1.
        f.mmu.write_nr_8e(0x05);
        const uint8_t read_b = f.mmu.read_nr_8e();
        // bit0 = (7ffd(4)=1 AND NOT 1ffd(0)=1) OR (1ffd(1)=1 AND 1ffd(0)=1)
        //      = (1 AND 0) OR (1 AND 1) = 1.
        // bit2 = 1ffd(0) = 1. bit 1 = 1ffd(2) = 0.
        // Read = 1_1_0_1_1_1_0_1 = 0xDD.
        check("N8E-05b",
              "NR 0x8E read-back bit 0 flips with 1FFD(0) selector — "
              "VHDL zxnext.vhd:6159",
              read_b == 0xDD,
              fmt("read=0x%02X (exp 0xDD)", read_b));
    }

    // N8E-06: Bank-select mode clears DFFD(3). VHDL:3698-3700
    //   `if nr_8f_mapping_mode_profi='0' and nr_wr_dat(3)='1'
    //    then dffd_reg(3) <= '0'`. JNEXT has profi forced off.
    //   Prime DFFD(3)=1 via a port_dffd write, then write NR 0x8E with
    //   bit 3 = 1 and observe DFFD(3) clears.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_port_dffd(0x08);  // dffd(3) <- 1 (cpu_do(3) stored)
        const uint8_t before = f.mmu.port_dffd_reg();
        f.mmu.write_nr_8e(0x08);  // bit 3 = 1, no other bits set
        const uint8_t after = f.mmu.port_dffd_reg();
        check("N8E-06",
              "NR 0x8E bit 3=1 clears DFFD(3) (non-Profi) — "
              "VHDL zxnext.vhd:3698-3700",
              (before & 0x08) == 0x08 && (after & 0x08) == 0x00,
              fmt("before DFFD=0x%02X (exp bit 3=1), after DFFD=0x%02X (exp bit 3=0)",
                  before, after));
    }
}

// ── Category 9: Mapping modes (NR 0x8F) ───────────────────────────────
// VHDL: zxnext.vhd:3787-3801 (mode storage + pentagon_*_en derivations),
//       zxnext.vhd:3763-3766 (port_7ffd_bank composition branches).
// Phase 2 B: Mmu::write_nr_8f + pentagon_en / pentagon_1024_en /
// effective_paging_locked on the public API.

void test_cat9_nr_8f() {
    set_group("Cat9 NR 0x8F mapping mode");

    // N8F-01: Standard mode (mapping_mode = 00). VHDL:3764-3766 else
    //   branches: bank(4:3) = DFFD(1:0); bank(5) = DFFD(2); bank(6) =
    //   DFFD(3). Set 7FFD(2:0)=7 and DFFD(3:0)=0b0111=7, expect
    //   bank = 7 | (7 << 3) = 63 = 0x3F → MMU6=0x7E, MMU7=0x7F.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8f(0x00);  // standard
        f.mmu.map_128k_bank(0x07);
        f.mmu.write_port_dffd(0x07);
        const uint8_t mmu6 = f.mmu.get_page(6);
        const uint8_t mmu7 = f.mmu.get_page(7);
        check("N8F-01",
              "standard mapping mode uses DFFD for bank(4:3)/(5)/(6) — "
              "VHDL zxnext.vhd:3764-3766 else branches",
              mmu6 == 0x7E && mmu7 == 0x7F,
              fmt("MMU6=0x%02X MMU7=0x%02X (exp 0x7E/0x7F)", mmu6, mmu7));
    }

    // N8F-02: Pentagon-512 (mapping_mode = 10). VHDL:3764 when-branch
    //   bank(4:3) <= 7FFD(7:6) (instead of DFFD). VHDL:3765 when-branch
    //   bank(5) = pentagon_1024_en AND 7FFD(5); in 512 mode
    //   pentagon_1024_en=0 so bank(5)=0. VHDL:3766 bank(6)=0 in Pentagon.
    //   7FFD=0xC7 (bits 7:6=11, bits 2:0=111) → bank = 7 | (3<<3) = 31 = 0x1F.
    //   MMU6=0x3E, MMU7=0x3F.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8f(0x02);  // Pentagon-512
        f.mmu.map_128k_bank(0xC7);
        const uint8_t mmu6 = f.mmu.get_page(6);
        const uint8_t mmu7 = f.mmu.get_page(7);
        check("N8F-02",
              "Pentagon-512 mode uses 7FFD(7:6) for bank(4:3) — "
              "VHDL zxnext.vhd:3764 when-branch",
              mmu6 == 0x3E && mmu7 == 0x3F,
              fmt("MMU6=0x%02X MMU7=0x%02X (exp 0x3E/0x3F)", mmu6, mmu7));
    }

    // N8F-03: Pentagon-1024 (mapping_mode = 11, EFF7(2)=0). VHDL:3765
    //   when-branch bank(5) = pentagon_1024_en AND 7FFD(5) = 7FFD(5) here.
    //   VHDL:3801 pentagon_1024_en = (mode=11) AND NOT EFF7(2).
    //   VHDL:3769 port_7ffd_locked = '0' when pentagon_1024_en=1, so the
    //   7FFD write with bit 5 = 1 is not blocked by the lock gate —
    //   effective_paging_locked() models this.
    //   7FFD=0xE7 (bits 7:6=11, bit 5=1, bits 2:0=111) → bank = 7 | (3<<3)
    //   | (1<<5) = 63 = 0x3F → MMU6=0x7E, MMU7=0x7F.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8f(0x03);   // Pentagon-1024 (EFF7(2) default 0)
        f.mmu.map_128k_bank(0xE7);
        const uint8_t mmu6 = f.mmu.get_page(6);
        const uint8_t mmu7 = f.mmu.get_page(7);
        check("N8F-03",
              "Pentagon-1024 mode promotes 7FFD(5) into bank(5) — "
              "VHDL zxnext.vhd:3765,3801",
              mmu6 == 0x7E && mmu7 == 0x7F,
              fmt("MMU6=0x%02X MMU7=0x%02X (exp 0x7E/0x7F)", mmu6, mmu7));
    }

    // N8F-04: EFF7(2)=1 disables the Pentagon-1024 extension. VHDL:3801
    //   pentagon_1024_en = (mode="11") AND NOT EFF7(2) — with EFF7(2)=1
    //   pentagon_1024_en drops to 0, so pentagon_en (VHDL:3798) =
    //   (mode=="10" OR pentagon_1024_en) = (FALSE OR 0) = 0. Bank
    //   composition falls back to the standard (DFFD-fed) branch. Also
    //   port_7ffd_locked returns to 7FFD(5) — if 7FFD(5)=1 the write is
    //   blocked. Exercise with bit 5 = 0 to keep the write path open.
    //   7FFD=0xC7 (bits 7:6=11, bit 5=0, bits 2:0=111), DFFD=0b00001 (dffd(0)=1)
    //   so standard bank = 7 | (1<<3) = 15 = 0x0F → MMU6=0x1E, MMU7=0x1F.
    //   Pentagon-1024 would have given bank = 7 | (3<<3) | 0 = 0x1F.
    //   The distinction proves EFF7(2) gated the override.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_nr_8f(0x03);        // request Pentagon-1024
        f.mmu.write_port_eff7(0x04);    // EFF7(2)=1 → disables 1024-en
        f.mmu.write_port_dffd(0x01);    // DFFD(0)=1
        f.mmu.map_128k_bank(0xC7);      // 7FFD(7:6)=11, bit 5=0
        const uint8_t mmu6 = f.mmu.get_page(6);
        const uint8_t mmu7 = f.mmu.get_page(7);
        check("N8F-04",
              "EFF7(2)=1 gates pentagon_1024_en off → standard bank composition — "
              "VHDL zxnext.vhd:3798,3801",
              mmu6 == 0x1E && mmu7 == 0x1F,
              fmt("MMU6=0x%02X MMU7=0x%02X (exp 0x1E/0x1F)", mmu6, mmu7));
    }

    // N8F-05: Pentagon modes force bank(6)=0. VHDL:3766
    //   bank(6) <= '0' when pentagon='1' or profi='1' else DFFD(3).
    //   Prime DFFD(3)=1 (would drive bank(6)=1 in standard mode), then
    //   enable Pentagon-512 and confirm bank(6) drops to 0.
    //   7FFD=0x00, DFFD=0x08 → standard bank = 0|0|0|64 = 64 = 0x40 →
    //   MMU6=0x80, MMU7=0x81. Pentagon bank = 0 → MMU6=0x00, MMU7=0x01.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_port_dffd(0x08);   // DFFD(3)=1
        f.mmu.map_128k_bank(0x00);     // 7FFD all 0
        // Sanity: standard-mode composition first (mode still 00).
        const uint8_t std_mmu6 = f.mmu.get_page(6);
        f.mmu.write_nr_8f(0x02);       // Pentagon-512
        const uint8_t p_mmu6 = f.mmu.get_page(6);
        const uint8_t p_mmu7 = f.mmu.get_page(7);
        check("N8F-05",
              "Pentagon mode forces bank(6)=0 regardless of DFFD(3) — "
              "VHDL zxnext.vhd:3766",
              std_mmu6 == 0x80 && p_mmu6 == 0x00 && p_mmu7 == 0x01,
              fmt("std MMU6=0x%02X (exp 0x80), pentagon MMU6=0x%02X MMU7=0x%02X "
                  "(exp 0x00/0x01)", std_mmu6, p_mmu6, p_mmu7));
    }
}

// ── Category 10: Port 0xEFF7 ──────────────────────────────────────────
// VHDL: zxnext.vhd:4636-4640 (eff7(3) forces RAM at 0x0000); Mmu has no
// port 0xEFF7 handler.

void test_cat10_port_eff7() {
    set_group("Cat10 port 0xEFF7");

    // EF7-01: EFF7 bit 3 = 1 forces RAM at 0x0000. VHDL zxnext.vhd:4636-4644
    // takes the MMU0=0x00, MMU1=0x01 branch whenever port_eff7_reg_3='1',
    // overriding the usual MMU0=MMU1=0xFF (ROM sentinel) path. The rebuild
    // fires on any paging-port write including EFF7 itself (VHDL:4619
    // port_memory_change_dly). Observable: slots 0/1 flip out of ROM mode
    // and the MMU register view shows the RAM pages 0x00 / 0x01.
    {
        Fixture f;
        f.fresh();
        // Reset seeds MMU0/1 as ROM (is_slot_rom=true). Trigger the swap.
        f.mmu.write_port_eff7(0x08);
        const bool rom0 = f.mmu.is_slot_rom(0);
        const bool rom1 = f.mmu.is_slot_rom(1);
        const uint8_t g0 = f.mmu.get_page(0);
        const uint8_t g1 = f.mmu.get_page(1);
        check("EF7-01",
              "EFF7(3)=1 → MMU0=0x00, MMU1=0x01 (RAM at 0x0000) "
              "— VHDL zxnext.vhd:4636-4644",
              !rom0 && !rom1 && g0 == 0x00 && g1 == 0x01,
              fmt("rom0=%d rom1=%d MMU0=0x%02X MMU1=0x%02X "
                  "(exp 0,0,0x00,0x01)", rom0, rom1, g0, g1));
    }

    // EF7-02: EFF7 bit 3 = 0 keeps ROM at 0x0000. Start by flipping to RAM
    // mode (EF7-01), then clear and re-trigger: MMU0/1 should return to the
    // ROM path (is_slot_rom=true). VHDL:4641-4644 takes the else branch and
    // restores the ROM sentinel semantics; our emulator stores the physical
    // ROM page in nr_mmu_[] (see mmu.cpp comment on map_128k_bank) rather
    // than VHDL's 0xFF — that behaviour is a pre-existing deviation covered
    // by P7F-09/10. The observable that distinguishes ROM-at-0x0000 from
    // RAM-at-0x0000 is is_slot_rom(), which is what we assert here.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_port_eff7(0x08);         // RAM at 0x0000 (EF7-01 state)
        f.mmu.write_port_eff7(0x00);         // back to ROM at 0x0000
        const bool rom0 = f.mmu.is_slot_rom(0);
        const bool rom1 = f.mmu.is_slot_rom(1);
        check("EF7-02",
              "EFF7(3)=0 → ROM at 0x0000 (slots 0,1 read-only) "
              "— VHDL zxnext.vhd:4641-4644",
              rom0 && rom1,
              fmt("is_slot_rom(0)=%d is_slot_rom(1)=%d (exp 1,1)",
                  rom0, rom1));
    }

    // EF7-03: EFF7 bit 2 disables Pentagon-1024 extension. VHDL zxnext.vhd:
    // 3801 gates nr_8f_mapping_mode_pentagon_1024_en on NR 0x8F="11" AND
    // port_eff7_reg_2='0'; setting EFF7(2)=1 forces pentagon_1024_en='0'
    // regardless of NR 0x8F. NR 0x8F itself is not on the Mmu surface
    // (Branch B owns it); the Mmu-side contract is that EFF7(2)=1 is stored
    // and observable via port_eff7_disable_p1024(), which Branch B uses to
    // compose the gate. The full lock-override outcome (locked=0 vs 1) is
    // an integration-tier assertion driven from the NR 0x8F test.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_port_eff7(0x04);
        check("EF7-03",
              "EFF7(2)=1 → port_eff7_disable_p1024 set (feeds VHDL:3801 gate) "
              "— VHDL zxnext.vhd:3781,3801",
              f.mmu.port_eff7_disable_p1024() && !f.mmu.port_eff7_ram_at_0000(),
              fmt("disable_p1024=%d ram_at_0000=%d (exp 1,0)",
                  f.mmu.port_eff7_disable_p1024(),
                  f.mmu.port_eff7_ram_at_0000()));
    }

    // EF7-04: Hard reset clears port_eff7_reg_{2,3}. VHDL zxnext.vhd:3777-3779
    //   if reset = '1' then
    //      port_eff7_reg_2 <= '0';
    //      port_eff7_reg_3 <= '0';
    // Both bits start cleared at power-on. Write non-zero, confirm the
    // accessors report set, then reset(hard) and confirm clear.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_port_eff7(0x0C);         // both bits 2 and 3 set
        const bool pre2 = f.mmu.port_eff7_disable_p1024();
        const bool pre3 = f.mmu.port_eff7_ram_at_0000();
        f.mmu.reset(true);                   // hard reset
        const bool post2 = f.mmu.port_eff7_disable_p1024();
        const bool post3 = f.mmu.port_eff7_ram_at_0000();
        check("EF7-04",
              "hard reset clears port_eff7_reg_{2,3} — VHDL zxnext.vhd:3777-3779",
              pre2 && pre3 && !post2 && !post3,
              fmt("pre bit2=%d bit3=%d post bit2=%d bit3=%d (exp 1,1,0,0)",
                  pre2, pre3, post2, post3));
    }

    // EF7-05: Soft reset CLEARS port_eff7_reg_{2,3}. VHDL zxnext.vhd:
    // 3777-3779 — `if reset = '1' then port_eff7_reg <= (others => '0')`.
    // Inside zxnext.vhd `reset = i_RESET = reset_hard OR reset_soft`
    // (zxnext_top_issue5.vhd:880 → :2384 → zxnext.vhd:1730), so the
    // clear fires on BOTH reset types. The previous "Branch C / hard-only"
    // claim was a misreading — corrected G46(b) 2026-05-08 alongside the
    // analogous DFF-08 / port_7ffd_reg / port_1ffd_reg fixes. With both
    // bits cleared, the eff7(3)=1 RAM-at-0x0000 branch no longer fires;
    // slots 0/1 fall back to ROM via the RESET_PAGES/map_rom_physical
    // path in Mmu::reset.
    {
        Fixture f;
        f.fresh();
        f.mmu.write_port_eff7(0x0C);         // bits 2 and 3 both set
        const bool pre_ram0000  = f.mmu.port_eff7_ram_at_0000();
        const bool pre_disp1024 = f.mmu.port_eff7_disable_p1024();
        const bool pre_rom0     = f.mmu.is_slot_rom(0);
        const bool pre_rom1     = f.mmu.is_slot_rom(1);
        f.mmu.reset(false);                  // soft reset
        const bool post_ram0000  = f.mmu.port_eff7_ram_at_0000();
        const bool post_disp1024 = f.mmu.port_eff7_disable_p1024();
        const bool post_rom0     = f.mmu.is_slot_rom(0);
        const bool post_rom1     = f.mmu.is_slot_rom(1);
        check("EF7-05",
              "soft reset CLEARS port_eff7_reg_{2,3} + slots 0/1 → ROM "
              "— VHDL zxnext.vhd:3777-3779 (reset = hard OR soft)",
              pre_ram0000 && pre_disp1024 && !pre_rom0 && !pre_rom1 &&
              !post_ram0000 && !post_disp1024 && post_rom0 && post_rom1,
              fmt("pre ram0000=%d disp1024=%d rom0=%d rom1=%d / "
                  "post ram0000=%d disp1024=%d rom0=%d rom1=%d "
                  "(exp pre 1,1,0,0 / post 0,0,1,1)",
                  pre_ram0000, pre_disp1024, pre_rom0, pre_rom1,
                  post_ram0000, post_disp1024, post_rom0, post_rom1));
    }

    // EF7-06 — re-homed to test/mmu/mmu_integration_test.cpp (G143 src/ gate at emulator.cpp:2222 — NR 0x85 b2)
}

// ── Category 11: ROM selection ────────────────────────────────────────
// VHDL: zxnext.vhd:3662-3734. Machine-type-specific ROM selection (48K,
// 128K, +3) lives in the upper core layers — Mmu exposes only the
// is_slot_rom flag once the RAM-or-ROM decision has landed.

void test_cat11_rom_selection() {
    set_group("Cat11 ROM selection");

    // ROM-01..07: machine-type-dependent sram_rom selection. Branch C.3
    // exposes Mmu::set_machine_type + Mmu::current_sram_rom modelling the
    // VHDL zxnext.vhd:2981-3008 selector. Each row drives the port state
    // per plan and checks current_sram_rom() returns the expected sram_rom
    // bit pattern.

    // ROM-01: 48K machine always reports sram_rom = 0 (VHDL zxnext.vhd:2984).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX48K);
        const uint8_t v = f.mmu.current_sram_rom();
        check("ROM-01",
              "48K machine sram_rom = 0 — VHDL zxnext.vhd:2984",
              v == 0, fmt("sram_rom=%u expected=0", v));
    }

    // ROM-02: 128K ROM 0 — port_7ffd bit 4 = 0. VHDL zxnext.vhd:3003-3005
    // (Next/128K: sram_rom = '0' & port_1ffd_rom(0), where port_1ffd_rom(0)
    // derives from port_7ffd(4) when port_1ffd(2)=0).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX128K);
        f.mmu.map_128k_bank(0x00);  // 7ffd(4)=0
        const uint8_t v = f.mmu.current_sram_rom();
        check("ROM-02",
              "128K ROM 0: 7FFD(4)=0 → sram_rom=0 — VHDL zxnext.vhd:3003-3005",
              v == 0, fmt("sram_rom=%u expected=0", v));
    }

    // ROM-03: 128K ROM 1 — port_7ffd bit 4 = 1.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX128K);
        f.mmu.map_128k_bank(0x10);  // 7ffd(4)=1
        const uint8_t v = f.mmu.current_sram_rom();
        check("ROM-03",
              "128K ROM 1: 7FFD(4)=1 → sram_rom=1 — VHDL zxnext.vhd:3003-3005",
              v == 1, fmt("sram_rom=%u expected=1", v));
    }

    // ROM-04: +3 ROM 0 — 1FFD(2)=0, 7FFD(4)=0 → sram_rom=00.
    // VHDL zxnext.vhd:2993-2995 (no altrom lock: sram_rom = port_1ffd_rom).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.map_128k_bank(0x00);
        f.mmu.map_plus3_bank(0x00);
        const uint8_t v = f.mmu.current_sram_rom();
        check("ROM-04",
              "+3 ROM 0: 1FFD(2)=0, 7FFD(4)=0 → sram_rom=00 — VHDL zxnext.vhd:2993",
              v == 0, fmt("sram_rom=%u expected=0", v));
    }

    // ROM-05: +3 ROM 1 — 1FFD(2)=0, 7FFD(4)=1 → sram_rom=01.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.map_plus3_bank(0x00);
        f.mmu.map_128k_bank(0x10);
        const uint8_t v = f.mmu.current_sram_rom();
        check("ROM-05",
              "+3 ROM 1: 1FFD(2)=0, 7FFD(4)=1 → sram_rom=01 — VHDL zxnext.vhd:2993",
              v == 1, fmt("sram_rom=%u expected=1", v));
    }

    // ROM-06: +3 ROM 2 — 1FFD(2)=1, 7FFD(4)=0 → sram_rom=10.
    // map_plus3_bank receives port_1ffd directly; bit 2 = 4 → value 0x04.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.map_128k_bank(0x00);
        f.mmu.map_plus3_bank(0x04);  // 1FFD(2)=1, normal (bit 0 = 0)
        const uint8_t v = f.mmu.current_sram_rom();
        check("ROM-06",
              "+3 ROM 2: 1FFD(2)=1, 7FFD(4)=0 → sram_rom=10 — VHDL zxnext.vhd:2993",
              v == 2, fmt("sram_rom=%u expected=2", v));
    }

    // ROM-07: +3 ROM 3 — 1FFD(2)=1, 7FFD(4)=1 → sram_rom=11.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.map_plus3_bank(0x04);
        f.mmu.map_128k_bank(0x10);
        const uint8_t v = f.mmu.current_sram_rom();
        check("ROM-07",
              "+3 ROM 3: 1FFD(2)=1, 7FFD(4)=1 → sram_rom=11 — VHDL zxnext.vhd:2993",
              v == 3, fmt("sram_rom=%u expected=3", v));
    }

    // ROM-08: ROM is read-only. VHDL zxnext.vhd:2933-3133 decode treats
    // ROM slots as read-disable/write-inhibit.
    //
    // VHDL gates this on config_mode (zxnext.vhd:3044-3050): while config_mode=1
    // writes to a ROM slot *are* honoured, routed to SRAM bank nr_04_romram_bank.
    // That is how tbblue.fw's load_roms() populates Spectrum ROM in SRAM before
    // the soft reset. Set config_mode=0 here so we observe the normal-mode
    // read-only behaviour the test is asserting. The config-mode routing path
    // is covered by Category 13 below.
    {
        Fixture f;
        f.fresh();  // slots 0,1 are ROM from reset
        f.mmu.set_config_mode(false);
        const uint8_t before = f.mmu.read(0x0000);
        f.mmu.write(0x0000, static_cast<uint8_t>(before ^ 0xA5));
        const uint8_t after = f.mmu.read(0x0000);
        check("ROM-08",
              "write to ROM slot has no effect — VHDL zxnext.vhd:2933-3133",
              before == after,
              fmt("before=0x%02X after=0x%02X", before, after));
    }

    // ROM-09: altrom_rw=1 makes ROM space writable via altrom write-over.
    // VHDL zxnext.vhd:3056 — sram_pre_rdonly <= not (nr_8c_altrom_en and
    // nr_8c_altrom_rw); when both flags are 1, ROM-slot writes are no
    // longer silently dropped. Per VHDL:3078 fourth clause the write
    // arbiter routes the byte into the alt-ROM SRAM region (pages 12..15
    // per VHDL:3117 / SRAM layout at zxnext.vhd:2924-2925), while reads
    // under en+rw=1 stay on the live ROM/sram_pre path. We assert both
    // halves: (a) the ROM-slot write is accepted (not dropped), observed
    // directly in the alt-ROM SRAM page; (b) toggling to altrom_en=1+rw=0
    // (read-only altrom mode) makes the just-written byte visible at the
    // same CPU address — closing the loop on "altrom is writable".
    {
        Fixture f;
        f.fresh();              // slots 0/1 are ROM (read_only_=true)
        f.mmu.set_config_mode(false);
        // Default machine_type_=ZXN_ISSUE2, port_7ffd_=0 → port_1ffd_rom(0)=0,
        // no altrom locks → alt_128_n=0 → alt-ROM 8K page = 0x0C (12) for
        // a13=0, 0x0D (13) for a13=1. Address 0x0000 → page 12, offset 0.
        f.mmu.set_nr_8c(0xC0);                  // altrom_en | altrom_rw
        f.mmu.write(0x0000, 0x5A);
        const uint8_t in_altram = f.ram.page_ptr(12)[0];
        // Switch to altrom_en=1, altrom_rw=0 → read path redirects to alt-ROM.
        f.mmu.set_nr_8c(0x80);
        const uint8_t read_back = f.mmu.read(0x0000);
        check("ROM-09",
              "altrom_en+altrom_rw=1 routes ROM-slot writes to the alt-ROM "
              "SRAM region — VHDL zxnext.vhd:3056, 3078, 3117",
              in_altram == 0x5A && read_back == 0x5A,
              fmt("ram[12][0]=0x%02X read(0x0000)|en=1,rw=0=0x%02X expected=0x5A/0x5A",
                  in_altram, read_back));
    }

    // ROM-10..12 — G57: three documented gaps in the legacy
    // current_rom_bank() observer. The new Mmu::sram_rom3() accessor
    // composes the VHDL `sram_rom3` signal per machine type with NR
    // 0x8C altrom locks factored in (zxnext.vhd:2985,2990,3000,3004).
    // Each row picks a discriminative pair that current_rom_bank()
    // alone cannot answer.

    // ROM-10 — 48K hardwire (VHDL zxnext.vhd:2985 `sram_rom3 <= '1'`).
    // 48K-mode sram_rom3 is always asserted, independent of port_7ffd /
    // port_1ffd / NR 0x8C state. Discriminative: cycle through several
    // port states and confirm the accessor never drops to 0.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX48K);
        f.mmu.map_128k_bank(0x00);                     // port_7ffd(4)=0
        f.mmu.map_plus3_bank(0x00);                    // port_1ffd(2)=0
        f.mmu.set_nr_8c(0x00);                         // no altrom locks
        const bool s0 = f.mmu.sram_rom3();
        f.mmu.map_128k_bank(0x10);                     // port_7ffd(4)=1
        const bool s1 = f.mmu.sram_rom3();
        f.mmu.set_nr_8c(0x10);                         // lock_rom0 only
        const bool s2 = f.mmu.sram_rom3();
        check("ROM-10",
              "48K hardwires sram_rom3 high for all port / altrom states "
              "— VHDL zxnext.vhd:2985",
              s0 && s1 && s2,
              fmt("sram_rom3 across 3 states: %d %d %d (expect 1 1 1)",
                  static_cast<int>(s0), static_cast<int>(s1),
                  static_cast<int>(s2)));
    }

    // ROM-11 — NR 0x8C altrom-lock factor on the ZXN/128K branch (VHDL
    // zxnext.vhd:2998-3001 — the lock-asserted path picks `sram_rom3 <=
    // nr_8c_altrom_lock_rom1`). Discriminative pair: lock_rom1=1 →
    // sram_rom3=1; lock_rom0=1 alone → sram_rom3=0. The legacy
    // current_rom_bank() ignored altrom locks entirely, so it cannot
    // distinguish the two states.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        f.mmu.map_128k_bank(0x00);                     // port_7ffd(4)=0
        f.mmu.map_plus3_bank(0x00);                    // port_1ffd(2)=0
        f.mmu.set_nr_8c(0x20);                         // lock_rom1 only
        const bool s_lk1 = f.mmu.sram_rom3();
        f.mmu.set_nr_8c(0x10);                         // lock_rom0 only
        const bool s_lk0 = f.mmu.sram_rom3();
        check("ROM-11",
              "ZXN with altrom-lock: sram_rom3 follows lock_rom1, not "
              "lock_rom0 — VHDL zxnext.vhd:3000",
              s_lk1 && !s_lk0,
              fmt("lock_rom1=1 → sram_rom3=%d (exp 1) / lock_rom0=1 → "
                  "sram_rom3=%d (exp 0)",
                  static_cast<int>(s_lk1), static_cast<int>(s_lk0)));
    }

    // ROM-12 — port_1ffd bit 2 selectivity. VHDL zxnext.vhd:2994 (+3
    // branch): `sram_rom3 <= port_1ffd_rom(1) and port_1ffd_rom(0)`,
    // i.e. BOTH port_1ffd(2) AND port_7ffd(4) must be set. VHDL :3004
    // (ZXN/128K branch): `sram_rom3 <= port_1ffd_rom(0)`, i.e. ONLY
    // port_7ffd(4) — port_1ffd(2) does NOT claim sram_rom3 on its own
    // in the non-+3 branch (the documented G57 gap is about whether
    // such a port_1ffd write should reach this signal at all on Next
    // mode, where NR 0x82 b3 would normally gate it; the accessor
    // reports the VHDL-faithful sram_rom3 given live ports). Two
    // states cover the discrimination:
    //   +3:   port_1ffd(2)=1, port_7ffd(4)=0 → sram_rom3=0 (a13 alone insufficient)
    //   +3:   port_1ffd(2)=1, port_7ffd(4)=1 → sram_rom3=1 (both bits)
    //   ZXN:  port_1ffd(2)=1, port_7ffd(4)=0 → sram_rom3=0 (port_7ffd(4) dominates)
    //   ZXN:  port_1ffd(2)=0, port_7ffd(4)=1 → sram_rom3=1
    {
        Fixture fp3a;
        fp3a.fresh();
        fp3a.mmu.set_machine_type(MachineType::ZX_PLUS3);
        fp3a.mmu.map_128k_bank(0x00);                  // port_7ffd(4)=0
        fp3a.mmu.map_plus3_bank(0x04);                 // port_1ffd(2)=1
        const bool p3_a13_only = fp3a.mmu.sram_rom3();
        Fixture fp3b;
        fp3b.fresh();
        fp3b.mmu.set_machine_type(MachineType::ZX_PLUS3);
        fp3b.mmu.map_128k_bank(0x10);                  // port_7ffd(4)=1
        fp3b.mmu.map_plus3_bank(0x04);                 // port_1ffd(2)=1
        const bool p3_both = fp3b.mmu.sram_rom3();
        Fixture fzxa;
        fzxa.fresh();
        fzxa.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        fzxa.mmu.map_128k_bank(0x00);                  // port_7ffd(4)=0
        fzxa.mmu.map_plus3_bank(0x04);                 // port_1ffd(2)=1
        const bool zxn_a13_only = fzxa.mmu.sram_rom3();
        Fixture fzxb;
        fzxb.fresh();
        fzxb.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        fzxb.mmu.map_128k_bank(0x10);                  // port_7ffd(4)=1
        fzxb.mmu.map_plus3_bank(0x00);                 // port_1ffd(2)=0
        const bool zxn_a14_only = fzxb.mmu.sram_rom3();
        check("ROM-12",
              "+3 sram_rom3 = 1FFD(2) AND 7FFD(4); ZXN sram_rom3 = "
              "7FFD(4) alone — VHDL zxnext.vhd:2994,3004",
              !p3_a13_only && p3_both &&
              !zxn_a13_only && zxn_a14_only,
              fmt("+3 a13-only=%d (exp 0) +3 both=%d (exp 1) / "
                  "ZXN a13-only=%d (exp 0) ZXN a14-only=%d (exp 1)",
                  static_cast<int>(p3_a13_only),
                  static_cast<int>(p3_both),
                  static_cast<int>(zxn_a13_only),
                  static_cast<int>(zxn_a14_only)));
    }
}

// ── Category 11b: effective ROM page post-port-write (G46(b) regression) ──
// VHDL: zxnext.vhd:2981-3008. Asserts that apply_legacy_rom_slots_ and
// map_plus3_bank correctly USE current_sram_rom() (which encodes the
// per-machine-type sram_rom selection) when composing the slot-0/1
// physical SRAM page after a $7FFD / $1FFD port write.
//
// Pre-fix bug (G46(b) Divergence A): both call sites computed the bank
// as a 2-bit composite of port_1ffd(2):port_7ffd(4) UNCONDITIONALLY,
// ignoring nr_03_machine_type. For ZXN_ISSUE2 + ($7FFD=$10, $1FFD=$04)
// this produced rom_bank=3 (= enNextZX.rom bank 3 = soft-reset
// trampoline at $0000) instead of VHDL's correct rom_bank=1
// (= enNextZX.rom bank 1 = normal continuation), driving a ~120 ms
// boot loop. The fix delegates both call sites to current_sram_rom().
//
// 16 scenarios: 4 MachineType × 4 (port_7FFD, port_1FFD) pivot pairs
// matching the supervisor's bank-flip wrapper toggle states.
void test_cat11b_effective_rom_page_post_port_write() {
    set_group("Cat11b effective ROM page post-port-write (G46(b) regression)");

    struct Row {
        MachineType machine;
        const char* machine_name;
        uint8_t     port_7ffd;
        uint8_t     port_1ffd;
        uint8_t     expected_page0;
    };

    // Expected per VHDL zxnext.vhd:2981-3008:
    //   48K: hardwired ROM 0 → page 0/1 always
    //   128K, ZXN: 1-bit (port_7ffd(4)) → page 0/1 or 2/3
    //   +3:   2-bit (1ffd(2):7ffd(4)) → page 0/1, 2/3, 4/5, or 6/7
    const Row rows[] = {
        // ── 48K: always sram_rom = 0 ───────────────────────────────
        { MachineType::ZX48K,      "48K",    0x00, 0x00, 0 },
        { MachineType::ZX48K,      "48K",    0x10, 0x00, 0 },
        { MachineType::ZX48K,      "48K",    0x00, 0x04, 0 },
        { MachineType::ZX48K,      "48K",    0x10, 0x04, 0 },
        // ── 128K: 1-bit, only port_7ffd(4) ─────────────────────────
        { MachineType::ZX128K,     "128K",   0x00, 0x00, 0 },
        { MachineType::ZX128K,     "128K",   0x10, 0x00, 2 },
        { MachineType::ZX128K,     "128K",   0x00, 0x04, 0 },
        { MachineType::ZX128K,     "128K",   0x10, 0x04, 2 },
        // ── +3: 2-bit (1ffd(2):7ffd(4)) ────────────────────────────
        { MachineType::ZX_PLUS3,   "+3",     0x00, 0x00, 0 },
        { MachineType::ZX_PLUS3,   "+3",     0x10, 0x00, 2 },
        { MachineType::ZX_PLUS3,   "+3",     0x00, 0x04, 4 },
        { MachineType::ZX_PLUS3,   "+3",     0x10, 0x04, 6 },
        // ── ZXN_ISSUE2 (Next): 1-bit, only port_7ffd(4) ────────────
        // The pivotal case for G46(b): default machine_type, $7FFD←$10
        // and $1FFD←$04 must produce page 2 (bank 1), NOT page 6
        // (bank 3 = soft-reset trampoline).
        { MachineType::ZXN_ISSUE2, "Next",   0x00, 0x00, 0 },
        { MachineType::ZXN_ISSUE2, "Next",   0x10, 0x00, 2 },
        { MachineType::ZXN_ISSUE2, "Next",   0x00, 0x04, 0 },
        { MachineType::ZXN_ISSUE2, "Next",   0x10, 0x04, 2 },
    };

    int idx = 0;
    for (const auto& r : rows) {
        ++idx;
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(r.machine);
        f.mmu.map_128k_bank(r.port_7ffd);
        f.mmu.map_plus3_bank(r.port_1ffd);
        const uint8_t page0 = f.mmu.get_effective_page(0);
        const uint8_t page1 = f.mmu.get_effective_page(1);
        const bool ok = (page0 == r.expected_page0) &&
                        (page1 == static_cast<uint8_t>(r.expected_page0 + 1));
        const std::string id = fmt("EFP-%02d", idx);
        check(id.c_str(),
              fmt("%s machine 7FFD=$%02X 1FFD=$%02X → slot0=page %u, slot1=page %u "
                  "(VHDL zxnext.vhd:2981-3008)",
                  r.machine_name, r.port_7ffd, r.port_1ffd,
                  r.expected_page0, r.expected_page0 + 1).c_str(),
              ok,
              fmt("got slot0=%u slot1=%u (expected %u/%u)",
                  page0, page1, r.expected_page0, r.expected_page0 + 1));
    }
}

// ── Cat 11c: machine_type change preserves slot 0/1 RAM mapping ─────
// Verify7-memory class-(a) regression test. VHDL `sram_rom`
// (zxnext.vhd:2981-3008) is combinational from machine_type, but the
// SRAM arbiter consumes it ONLY on the legacy-ROM branch (:3052) — i.e.
// when MMU<i> is in the ROM-area sentinel range. A machine_type change
// does NOT trigger port_memory_change_dly (zxnext.vhd:3813), so VHDL
// leaves MMU0/MMU1 register values alone — slots explicitly mapped to
// RAM via NR 0x50/0x51 stay at their RAM page.
//
// Pre-fix: Mmu::set_machine_type() unconditionally called
// apply_legacy_rom_slots_(), which forced both slots back to ROM and
// clobbered nr_mmu_[0]/[1] to 0xFF — even when firmware had explicitly
// mapped slot 0/1 to a RAM page via NR 0x50/0x51.
namespace {
void test_cat11c_machine_type_preserves_ram_slots() {
    set_group("Cat11c machine_type preserves slot 0/1 RAM (verify7)");

    // MTC-01: Slot 0 RAM-mapped via NR 0x50, then machine_type change
    // must leave the RAM mapping intact (read_only_=false, nr_mmu_[0]
    // = original page).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        // Map slot 0 to RAM page 0x20 (legacy 7FFD bank 0, post-shift).
        f.mmu.set_page(0, 0x20);
        const uint8_t pre_page  = f.mmu.get_page(0);
        const bool    pre_ro    = f.mmu.is_slot_rom(0);
        // Switch to a different machine type — VHDL would leave MMU0
        // alone on this transition (no port_memory_change_dly pulse).
        f.mmu.set_machine_type(MachineType::ZX128K);
        const uint8_t post_page = f.mmu.get_page(0);
        const bool    post_ro   = f.mmu.is_slot_rom(0);
        check("MTC-01",
              "machine_type change leaves slot 0 NR-mapped to RAM "
              "(VHDL :3813 — no port_memory_change_dly pulse)",
              pre_page == 0x20 && !pre_ro &&
              post_page == 0x20 && !post_ro,
              fmt("pre: page=%u ro=%d / post: page=%u ro=%d "
                  "(expected page=32 ro=0 both)",
                  pre_page, static_cast<int>(pre_ro),
                  post_page, static_cast<int>(post_ro)));
    }

    // MTC-02: Slot 1 RAM-mapped, machine_type change preserves it.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        f.mmu.set_page(1, 0x21);
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        const uint8_t post_page = f.mmu.get_page(1);
        const bool    post_ro   = f.mmu.is_slot_rom(1);
        check("MTC-02",
              "slot 1 RAM mapping preserved across machine_type → +3",
              post_page == 0x21 && !post_ro,
              fmt("post: page=%u ro=%d (expected 33/0)",
                  post_page, static_cast<int>(post_ro)));
    }

    // MTC-03: Both slots in legacy ROM mode — machine_type change refreshes
    // the cached read_ptr_ via engage_legacy_rom_paging_slot. Verify both
    // slots are still in ROM mode AND the cached page tracks new sram_rom.
    // Switch from ZXN_ISSUE2 with port_7ffd(4)=1 (sram_rom=1, pages 2/3)
    // to ZX48K (sram_rom hardwired 0, pages 0/1) — discriminative because
    // the page changes per-machine even with no port write.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x10);           // 7ffd(4)=1; ZXN sram_rom=1
        // Sanity: pre-switch slots use sram_rom=1 → pages 2/3.
        const uint8_t pre_e0 = f.mmu.get_effective_page(0);
        const uint8_t pre_e1 = f.mmu.get_effective_page(1);
        f.mmu.set_machine_type(MachineType::ZX48K);  // sram_rom hardwired 0
        const bool ro0 = f.mmu.is_slot_rom(0);
        const bool ro1 = f.mmu.is_slot_rom(1);
        // After 48K: sram_rom=0 → physical ROM pages 0/1.
        const uint8_t e0 = f.mmu.get_effective_page(0);
        const uint8_t e1 = f.mmu.get_effective_page(1);
        check("MTC-03",
              "machine_type change refreshes legacy-ROM slot 0/1 cache "
              "to new sram_rom-derived pages (ZXN→48K: 2/3 → 0/1)",
              pre_e0 == 2 && pre_e1 == 3 &&
              ro0 && ro1 && e0 == 0 && e1 == 1,
              fmt("pre eff0=%u eff1=%u / post ro0=%d ro1=%d eff0=%u eff1=%u "
                  "(expected pre 2/3, post 1/1/0/1)",
                  pre_e0, pre_e1,
                  static_cast<int>(ro0), static_cast<int>(ro1), e0, e1));
    }
}
}

// ── Cat 21: Nirvana / per-write mux (G12) + Shadow-screen NR (G58) ───
namespace {
void test_cat21_nirvana_multiplex() {
    set_group("G12-MUX");

    // G12-MUX — Task 8 Nirvana round 3 (2026-07-13): the arm/gate
    // mechanism (first a same-byte-repeat-count heuristic, then a
    // "positional" beam-position gate — see doc/issues/KNOWN-
    // FUNCTIONALITY-GAPS-AND-PLAN.md "G12" for the full history) is
    // REMOVED. Measured (headless beast.nex, 2500 frames, release, 3+
    // runs each side) to cost nothing detectable vs. always recording —
    // gating existed only to avoid the log-append cost on ordinary
    // content, and that cost turned out to be unmeasurable. Recording is
    // now unconditional: ANY write landing in the 768-byte attribute
    // sub-range of the bank-5/bank-7 attribute pages is logged
    // immediately, tagged with whatever scanline is current at the time
    // of the write — no repeat count, no beam-position check, no
    // frame-boundary latency. Nirvana-class demos rewrite ULA attribute
    // bytes mid-frame, racing the video beam, because the ULA does not
    // fetch the attribute byte once per frame — it recomputes the
    // attribute-row address from the CURRENT raster position every
    // scanline and re-fetches it every character column: VHDL
    // video/zxula.vhd:192, 223-224 (`py_s <= i_vc + ...`;
    // `addr_a_spc_12_5 <= "110" & py(7 downto 3)` — driven straight off
    // the live `i_vc` counter, not a frame-latched value).

    // G12-MUX-01: a single attribute write is recorded and replayable
    // within the SAME frame it happens — no "next frame boundary" wait
    // of any kind (regression guard against ever reintroducing arm
    // latency).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(2, 0x0A);   // bank-5 attribute page at CPU 0x4000-0x5FFF
        const uint16_t attr5_addr = 0x5810;      // column 16, character row 0
        const uint16_t off = attr5_addr - 0x5800;
        const int disp_y = 32;   // Ula::DISP_Y

        f.mmu.attr_mux_start_frame();
        f.mmu.attr_mux_set_current_line(disp_y + 0);
        f.mmu.write(attr5_addr, 0x42);
        f.mmu.attr_mux_rewind_to_baseline();
        f.mmu.attr_mux_apply_line(disp_y + 0);
        const uint8_t value = f.mmu.attr_mux5().current(off);
        check("G12-MUX-01",
              "a single attribute write is recorded and replayable within "
              "the SAME frame it happens -- no arm/gate, no frame-boundary "
              "latency",
              f.mmu.attr_mux5().log_size() == 1 && value == 0x42,
              fmt("log_size=%zu value=0x%02X (expected 1/0x42)",
                  f.mmu.attr_mux5().log_size(), value));
    }

    // G12-MUX-02: writes tagged OUTSIDE the active display (top/bottom
    // border, vblank) are ALSO recorded now — there is no beam-position
    // gate left to exempt them. This is the direct regression guard
    // against reintroducing any positional or count-based gate: the old
    // gate deliberately left these inert (they "could never race"), but
    // that was a property of the GATE, not of recording itself, and the
    // gate is gone.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(2, 0x0A);
        const uint16_t attr5_addr = 0x5810;

        f.mmu.attr_mux_start_frame();
        f.mmu.attr_mux_set_current_line(0);   // fb_row 0 == top border, above DISP_Y=32
        for (int i = 0; i < 16; ++i) {
            f.mmu.write(attr5_addr, static_cast<uint8_t>(i));
        }
        check("G12-MUX-02",
              "writes tagged outside the active display (border/vblank) "
              "are recorded unconditionally -- no positional gate remains",
              f.mmu.attr_mux5().log_size() == 16,
              fmt("log_size=%zu (expected 16, one per write)",
                  f.mmu.attr_mux5().log_size()));
    }

    // G12-MUX-03 — the actual ULA-side mid-row recolour mux consumer,
    // wired via Mmu::attr_mux5()/attr_mux7() (Mmu::write()'s dedicated
    // always-on detector-plus-recorder — see mmu.h). This Fixture has no
    // Ula/Renderer, so per the task's bounded scope we verify the
    // Mmu-side plumbing is provably correct at the source: a real
    // Ula/Renderer just calls mmu.attr_mux5().current(offset) after
    // Mmu::attr_mux_apply_line(row) -- see src/video/ula.cpp
    // Ula::attr_vram_read() for the actual consumer wiring, exercised
    // end-to-end by the nirvana_demo regression test instead of here.
    //
    // VHDL zxula.vhd:192 (`py_s <= i_vc + scroll_y`, re-latched every
    // scanline) + :223-224 (`addr_a_spc_12_5 <= "110" & py(7 downto
    // 3)`) establish that the ULA re-fetches the SAME attribute address
    // fresh on every one of the 8 scanlines making up a character cell
    // -- the hardware mechanism this replay reconstructs.
    {
        Fixture f;
        f.fresh();
        // Map bank-5 attribute page (0x0A) at slot 2 (CPU 0x4000-0x5FFF)
        // and bank-7 shadow attribute page (0x0E) at slot 3 (CPU
        // 0x6000-0x7FFF) -- the standard Next MMU layout. Attribute
        // bytes land at offset 0x1800-0x1AFF within either page (CPU
        // 0x5800-0x5AFF / 0x7800-0x7AFF).
        f.mmu.set_page(2, 0x0A);
        f.mmu.set_page(3, 0x0E);
        const uint16_t attr5_addr = 0x5810;  // bank5 attribute, column 16 row 0
        const uint16_t attr7_addr = 0x7810;  // bank7 shadow, same column/row
        const uint16_t off = attr5_addr - 0x5800;
        const int disp_y = 32;   // Ula::DISP_Y — arbitrary in-display line; no gate depends on it

        f.mmu.attr_mux_start_frame();

        // Two distinct colour bands within the same cell's own
        // 8-scanline span (screen rows 0-7) -- the exact scenario the
        // OLD repeat>=4 heuristic left flat and silently wrong (a
        // 2-write race never reached the threshold). Must always
        // reconstruct correctly now: there is no threshold.
        f.mmu.attr_mux_set_current_line(disp_y);
        f.mmu.write(attr5_addr, 0xAA);
        f.mmu.attr_mux_set_current_line(disp_y + 4);
        f.mmu.write(attr5_addr, 0xBB);
        f.mmu.attr_mux_rewind_to_baseline();
        f.mmu.attr_mux_apply_line(disp_y);
        const uint8_t band1 = f.mmu.attr_mux5().current(off);
        f.mmu.attr_mux_apply_line(disp_y + 4);
        const uint8_t band2 = f.mmu.attr_mux5().current(off);
        check("G12-MUX-03",
              "a cell racing only 2 colour bands (the scenario the old "
              "repeat>=4 heuristic left flat and silently wrong) correctly "
              "reconstructs both distinct band colours",
              band1 == 0xAA && band2 == 0xBB,
              fmt("band1=0x%02X band2=0x%02X (expected 0xAA/0xBB)", band1, band2));

        // Now race attr5_addr across three scanlines with three
        // different values, then read back the per-line reconstruction.
        f.mmu.attr_mux_start_frame();
        f.mmu.attr_mux_set_current_line(0);
        f.mmu.write(attr5_addr, 0x01);
        f.mmu.attr_mux_set_current_line(1);
        f.mmu.write(attr5_addr, 0x02);
        f.mmu.attr_mux_set_current_line(2);
        f.mmu.write(attr5_addr, 0x03);

        f.mmu.attr_mux_rewind_to_baseline();
        f.mmu.attr_mux_apply_line(0);
        check("G12-MUX-04",
              "scanline 0 replay reconstructs the value written while "
              "tagged line 0, not the frame's last write",
              f.mmu.attr_mux5().current(off) == 0x01,
              fmt("current=0x%02X (expected 0x01)", f.mmu.attr_mux5().current(off)));

        f.mmu.attr_mux_apply_line(1);
        check("G12-MUX-05",
              "scanline 1 replay reconstructs the value written while "
              "tagged line 1",
              f.mmu.attr_mux5().current(off) == 0x02,
              fmt("current=0x%02X (expected 0x02)", f.mmu.attr_mux5().current(off)));

        f.mmu.attr_mux_apply_line(2);
        check("G12-MUX-06",
              "scanline 2 replay reconstructs the value written while "
              "tagged line 2 -- three distinct scanlines, three distinct "
              "colours from ONE physical byte, matching real Nirvana output",
              f.mmu.attr_mux5().current(off) == 0x03,
              fmt("current=0x%02X (expected 0x03)", f.mmu.attr_mux5().current(off)));

        // G12-MUX-07: scanline 3 has no logged write -- per VHDL the ULA
        // re-fetches the SAME RAM address every scanline (zxula.vhd:223-224);
        // if the CPU didn't rewrite it, the fetch returns the value already
        // there. The replay must carry the last value forward, not reset
        // to baseline or corrupt it.
        f.mmu.attr_mux_apply_line(3);
        check("G12-MUX-07",
              "a scanline with no attribute write carries the previous "
              "scanline's reconstructed value forward (matches VHDL: the ULA "
              "re-fetches unchanged RAM and sees the unchanged byte)",
              f.mmu.attr_mux5().current(off) == 0x03,
              fmt("current=0x%02X (expected 0x03, carried from line 2)", f.mmu.attr_mux5().current(off)));

        // G12-MUX-08: bank-7 shadow attribute plane is tracked
        // independently of bank 5 -- writing the SAME column/row offset
        // in the shadow plane must not disturb bank 5's reconstruction.
        f.mmu.attr_mux_set_current_line(2);
        f.mmu.write(attr7_addr, 0x55);
        f.mmu.attr_mux_apply_line(2);
        check("G12-MUX-08",
              "bank-7 shadow attribute plane replays independently of bank 5 "
              "at the same column/row offset",
              f.mmu.attr_mux7().current(off) == 0x55 && f.mmu.attr_mux5().current(off) == 0x03,
              fmt("bank7=0x%02X bank5=0x%02X (expected bank7=0x55 bank5=0x03)",
                  f.mmu.attr_mux7().current(off), f.mmu.attr_mux5().current(off)));
    }

    // G12-MUX-09 — the discriminative fall-through test the always-on
    // redesign specifically needed and previously lacked: a frame in
    // which SOME attribute cells are written mid-frame and OTHERS are
    // left completely untouched. The untouched cell must render its
    // REAL RAM content unchanged (baseline fall-through) across every
    // scanline of its span -- not zero, not a stale/wrong-bank value --
    // while a DIFFERENT cell racing in the SAME frame replays its own
    // mid-frame writes. This is the assertion whose absence let the
    // Task 8 Nirvana round-2 bank-selection bug ship silently (see
    // Ula::attr_vram_read() -- reading the WRONG bank's baseline looked
    // exactly like "no fall-through" from the outside, even though
    // AttributeMux's own baseline+fall-through mechanism was correct
    // all along). Mutation-tested: temporarily forcing
    // AttributeMux::start_frame() to ignore its baseline argument (as if
    // Mmu had passed a null/wrong pointer) makes this row FAIL
    // (untouched byte reads back 0x00, not 0x99); restoring the real
    // baseline snapshot makes it PASS again.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(2, 0x0A);
        const uint16_t untouched_addr = 0x5820;  // a DIFFERENT cell, never rewritten
        const uint16_t racing_addr    = 0x5810;  // the cell under race this frame
        const uint16_t off_untouched  = untouched_addr - 0x5800;
        const uint16_t off_racing     = racing_addr - 0x5800;
        const int disp_y = 32;

        // Pre-existing real RAM content for the untouched cell, written
        // BEFORE this frame starts -- exactly what a genuine "set once,
        // never touched again" attribute byte looks like. This becomes
        // part of the baseline snapshot when attr_mux_start_frame() runs.
        f.mmu.write(untouched_addr, 0x99);

        f.mmu.attr_mux_start_frame();   // snapshots baseline, including the 0x99 above

        // Race a DIFFERENT cell mid-frame -- proves the log/replay
        // mechanism is actively engaged this frame, not merely inert.
        f.mmu.attr_mux_set_current_line(disp_y + 0);
        f.mmu.write(racing_addr, 0x11);
        f.mmu.attr_mux_set_current_line(disp_y + 4);
        f.mmu.write(racing_addr, 0x22);

        f.mmu.attr_mux_rewind_to_baseline();
        f.mmu.attr_mux_apply_line(disp_y + 0);
        const uint8_t untouched_at_line0 = f.mmu.attr_mux5().current(off_untouched);
        const uint8_t racing_at_line0    = f.mmu.attr_mux5().current(off_racing);
        f.mmu.attr_mux_apply_line(disp_y + 4);
        const uint8_t untouched_at_line4 = f.mmu.attr_mux5().current(off_untouched);
        const uint8_t racing_at_line4    = f.mmu.attr_mux5().current(off_racing);

        check("G12-MUX-09",
              "discriminative fall-through: an untouched cell renders its "
              "REAL RAM content unchanged across every scanline, while a "
              "DIFFERENT cell racing in the SAME frame replays its own "
              "mid-frame writes -- proves the mux overlays changes on top "
              "of real RAM rather than replacing the entire plane",
              untouched_at_line0 == 0x99 && untouched_at_line4 == 0x99 &&
              racing_at_line0 == 0x11 && racing_at_line4 == 0x22,
              fmt("untouched@0=0x%02X untouched@4=0x%02X racing@0=0x%02X racing@4=0x%02X "
                  "(expected 0x99/0x99/0x11/0x22)",
                  untouched_at_line0, untouched_at_line4, racing_at_line0, racing_at_line4));
    }

    // G12-MUX-10 — column-accurate resolution (Task 8 Nirvana round 4).
    // Round 3 resolved writes at SCANLINE granularity only: whichever
    // value a byte held as of the scanline tag applied to ALL 32
    // columns of that scanline, which is wrong -- the ULA fetches each
    // column's own attribute byte at a DIFFERENT, increasing hc
    // position within the scanline (zxula.vhd:226-263/270-303; see
    // attribute_mux.h's column-accurate-resolution block comment for
    // the full citation and the hc_fetch(col) = hc_origin + col*8
    // derivation). This row proves resolution is genuinely hc-gated:
    // two writes to the SAME byte on the SAME scanline, straddling that
    // byte's own column-5 fetch instant. The write BEFORE the fetch
    // must win for THIS scanline; the write AFTER it must carry forward
    // only to the cell's NEXT scanline (screen rows 0-7 all belong to
    // character row 0), not retroactively recolour the scanline already
    // rendered.
    //
    // Task 54: the fetch instant is parity-aware (zxula.vhd:271-306 +
    // 368-455 — see AttributeMux::hc_fetch()): odd columns latch at
    // origin + 8 + col*8. For col 5 on 48K that is 116+8+40 = 164, not
    // the pre-Task-54 origin + col*8 = 156. The straddle points below
    // (fetch∓6 = 158/170) are chosen so 158 sits BETWEEN the old and new
    // boundaries: under the old formula the early write would resolve as
    // "after the fetch" — i.e. this row also goes red if hc_fetch()
    // regresses to the old formula.
    //
    // Mutation-tested: temporarily reverting AttributeMux::resolve()'s
    // hc comparison to a line-only comparison (as round 3 had it --
    // `e.line <= target_line_` unconditionally, dropping the `e.hc <=
    // target_hc` half of the condition) makes this row FAIL
    // (this_scanline reads back 0xBB, the LATE write, instead of 0xAA);
    // restoring the real column-gated comparison makes it PASS again.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(2, 0x0A);
        const uint16_t attr5_addr = 0x5805;   // column 5, character row 0
        const uint16_t off = attr5_addr - 0x5800;
        const int disp_y     = 32;   // Ula::DISP_Y
        const int hc_origin  = 116;  // VideoTiming::ula_prefetch_origin_hc(), 48K: 128-12
        const int col        = 5;
        const int fetch_hc   = hc_origin + 8 + col * 8;   // 164 (odd col, Task 54)

        f.mmu.attr_mux_start_frame(hc_origin);
        f.mmu.attr_mux_set_current_line(disp_y + 0);

        // Write BEFORE column 5's own fetch instant this scanline.
        f.mmu.attr_mux_set_current_hc(fetch_hc - 6);
        f.mmu.write(attr5_addr, 0xAA);
        // Write AFTER column 5's own fetch instant this scanline -- real
        // RAM now holds 0xBB, but the ULA already fetched 0xAA for THIS
        // scanline's render of column 5.
        f.mmu.attr_mux_set_current_hc(fetch_hc + 6);
        f.mmu.write(attr5_addr, 0xBB);

        f.mmu.attr_mux_rewind_to_baseline();
        f.mmu.attr_mux_apply_line(disp_y + 0);
        const uint8_t this_scanline = f.mmu.attr_mux5().current(off);

        // Next scanline of the SAME cell: the late write is now "from a
        // strictly earlier scanline" and carries forward unconditionally.
        f.mmu.attr_mux_apply_line(disp_y + 1);
        const uint8_t next_scanline = f.mmu.attr_mux5().current(off);

        check("G12-MUX-10",
              "column-accurate resolution: a write landing AFTER this "
              "column's own attribute-fetch instant this scanline does "
              "NOT apply until the cell's next scanline -- the earlier "
              "write wins for THIS scanline's render, proving genuine "
              "hc gating rather than round 3's line-only gating",
              this_scanline == 0xAA && next_scanline == 0xBB,
              fmt("this_scanline=0x%02X next_scanline=0x%02X "
                  "(expected 0xAA then 0xBB)", this_scanline, next_scanline));
    }

    // G12-MUX-11 — Task 54: fetch-instant PARITY asymmetry. Per
    // zxula.vhd:368-455 the byte governing an EVEN column is consumed by
    // sload_0 (latched 1 tick earlier, at origin+12+8C); an ODD column's
    // byte comes via sload_1 (latched 5 ticks earlier, at origin+8+8C).
    // On 48K: fetch(col 4) = 116+12+32 = 160, fetch(col 5) = 116+8+40 =
    // 164. Three probes pin the pair of boundaries:
    //   * hc=162 write to BOTH cols: after col 4's 160 (next line), but
    //     before col 5's 164 (this line). Kills the pre-Task-54 formula
    //     origin+col*8 (fetch(5)=156 < 162 would resolve col 5 "after").
    //   * hc=166 write to col 5: after 164 (next line). Kills a uniform
    //     parity-blind origin+12+col*8 (fetch(5)=168 > 166 would resolve
    //     it "before" and show it this line).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(2, 0x0A);
        const int disp_y    = 32;
        const int hc_origin = 116;
        const uint16_t addr4 = 0x5804, addr5 = 0x5805;  // cols 4 and 5, row 0

        f.mmu.attr_mux_start_frame(hc_origin);
        f.mmu.attr_mux_set_current_line(disp_y + 0);
        // Baseline values written well before either fetch this scanline.
        f.mmu.attr_mux_set_current_hc(10);
        f.mmu.write(addr4, 0x40);
        f.mmu.write(addr5, 0x50);
        // Between fetch(4)=160 and fetch(5)=164.
        f.mmu.attr_mux_set_current_hc(162);
        f.mmu.write(addr4, 0x41);
        f.mmu.write(addr5, 0x51);
        // After fetch(5)=164 (but before a parity-blind 168).
        f.mmu.attr_mux_set_current_hc(166);
        f.mmu.write(addr5, 0x52);

        f.mmu.attr_mux_rewind_to_baseline();
        f.mmu.attr_mux_apply_line(disp_y + 0);
        const uint8_t c4_this = f.mmu.attr_mux5().current(4);
        const uint8_t c5_this = f.mmu.attr_mux5().current(5);
        f.mmu.attr_mux_apply_line(disp_y + 1);
        const uint8_t c4_next = f.mmu.attr_mux5().current(4);
        const uint8_t c5_next = f.mmu.attr_mux5().current(5);

        check("G12-MUX-11",
              "fetch-instant parity asymmetry: hc=162 lands AFTER even "
              "col 4's fetch (160) but BEFORE odd col 5's (164); hc=166 "
              "lands AFTER col 5's -- col 4 keeps its early value this "
              "line, col 5 shows the 162-write this line and the 166-write "
              "only next line [zxula.vhd:271-306,368-455; Task 54]",
              c4_this == 0x40 && c5_this == 0x51 &&
              c4_next == 0x41 && c5_next == 0x52,
              fmt("c4_this=0x%02X c5_this=0x%02X c4_next=0x%02X c5_next=0x%02X "
                  "(expected 0x40/0x51/0x41/0x52)",
                  c4_this, c5_this, c4_next, c5_next));
    }
}

void test_cat3bis_shadow_screen() {
    set_group("SHA");

    // SHA-01..03 — G58: NR 0x69 bit 6 → port_7ffd_reg(3) shadow alias
    // (zxnext.vhd:3658-3660), the sole producer of port_7ffd_shadow
    // (:3768) and i_ula_shadow_en (:4453). The end-to-end NR-side wiring
    // (NR 0x69 write handler → Mmu::set_port_7ffd_bit3 →
    // Ula::set_shadow_screen_en) lives in emulator.cpp and is exercised
    // at the integration tier (G56-D-69b in nextreg_integration_test).
    // The SHA-* rows below pin the Mmu-side primitive that the handler
    // depends on, isolated from the rest of port_7ffd_ semantics.

    // SHA-01 — Mmu::set_port_7ffd_bit3 round-trip via shadow_screen_en().
    // VHDL zxnext.vhd:3658 — nr_69_we drives port_7ffd_reg(3) <=
    // nr_wr_dat(6). VHDL :4453 — port_7ffd_reg(3) (after :3768
    // port_7ffd_shadow rename) is i_ula_shadow_en.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_port_7ffd_bit3(true);
        const bool on_after_set = f.mmu.shadow_screen_en();
        f.mmu.set_port_7ffd_bit3(false);
        const bool off_after_clear = f.mmu.shadow_screen_en();
        check("SHA-01",
              "Mmu::set_port_7ffd_bit3 toggles shadow_screen_en() — "
              "VHDL zxnext.vhd:3658,4453",
              on_after_set && !off_after_clear,
              fmt("on_after_set=%d (exp 1) off_after_clear=%d (exp 0)",
                  static_cast<int>(on_after_set),
                  static_cast<int>(off_after_clear)));
    }

    // SHA-02 — discriminative: bit-3 alias setter shares storage with
    // port_7ffd_, NOT a parallel field. A subsequent full-byte port-7FFD
    // write (map_128k_bank with bit 3 = 0) must clear shadow; a write
    // with bit 3 = 1 must keep it set. VHDL zxnext.vhd:3653 — the
    // full-byte branch latches cpu_do into port_7ffd_reg, overwriting
    // bit 3 along with everything else. A buggy implementation that
    // stored shadow in a parallel flag would FAIL step (b).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_port_7ffd_bit3(true);              // (a) shadow on
        const bool a = f.mmu.shadow_screen_en();
        f.mmu.map_128k_bank(0x10);                   // (b) full byte, bit3=0
        const bool b = f.mmu.shadow_screen_en();
        f.mmu.set_port_7ffd_bit3(true);              // re-set
        f.mmu.map_128k_bank(0x18);                   // (c) full byte, bit3=1
        const bool c = f.mmu.shadow_screen_en();
        check("SHA-02",
              "Bit-3 alias and full-byte port-7FFD write share "
              "port_7ffd_reg storage — VHDL zxnext.vhd:3653,3658",
              a && !b && c,
              fmt("after-set=%d (exp 1) after-fullbyte-bit3=0=%d (exp 0) "
                  "after-fullbyte-bit3=1=%d (exp 1)",
                  static_cast<int>(a), static_cast<int>(b),
                  static_cast<int>(c)));
    }

    // SHA-03 — cross-state isolation: toggling bit 3 must not mutate any
    // other observable port_7ffd_-derived state. VHDL zxnext.vhd:3763-3766
    // shows port_7ffd_bank composition uses bits (2:0)/(7:6) (Pentagon),
    // and bit (5) for the lock; bit 3 is consumed solely by
    // i_ula_shadow_en (:4453). Likewise current_rom_bank uses bit (4),
    // not bit (3). Snapshot then-flip-then-compare: bit 3 mutates ONLY
    // shadow_screen_en, leaving the rest of port_7ffd() and rom-bank
    // selection untouched.
    {
        Fixture f;
        f.fresh();
        // Set a non-trivial baseline so multiple bits are observable.
        // 0x55 = 0101 0101 — bits 0,2,4,6 set, bits 1,3,5,7 clear. Bit 3
        // clear baseline so set_port_7ffd_bit3(true) truly flips it.
        f.mmu.map_128k_bank(0x55);
        const uint8_t  pre_p7ffd_no_bit3 = static_cast<uint8_t>(
            f.mmu.port_7ffd() & 0xF7);
        const uint8_t  pre_rom_bank      = f.mmu.current_rom_bank();
        const bool     pre_shadow        = f.mmu.shadow_screen_en();

        f.mmu.set_port_7ffd_bit3(true);
        const uint8_t  post_p7ffd_no_bit3 = static_cast<uint8_t>(
            f.mmu.port_7ffd() & 0xF7);
        const uint8_t  post_rom_bank      = f.mmu.current_rom_bank();
        const bool     post_shadow        = f.mmu.shadow_screen_en();

        f.mmu.set_port_7ffd_bit3(false);
        const uint8_t  cleared_p7ffd_no_bit3 = static_cast<uint8_t>(
            f.mmu.port_7ffd() & 0xF7);
        const uint8_t  cleared_rom_bank      = f.mmu.current_rom_bank();
        const bool     cleared_shadow        = f.mmu.shadow_screen_en();

        check("SHA-03",
              "Bit-3 toggle isolated to shadow_screen_en; port_7ffd "
              "bits 7:4|2:0 and current_rom_bank unchanged — "
              "VHDL zxnext.vhd:3763-3766,4453",
              !pre_shadow && post_shadow && !cleared_shadow &&
              pre_p7ffd_no_bit3 == post_p7ffd_no_bit3 &&
              pre_p7ffd_no_bit3 == cleared_p7ffd_no_bit3 &&
              pre_rom_bank == post_rom_bank &&
              pre_rom_bank == cleared_rom_bank,
              fmt("shadow pre=%d post=%d cleared=%d (exp 0,1,0); "
                  "p7ffd&~0x08 pre=0x%02X post=0x%02X cleared=0x%02X "
                  "(must match); rom_bank pre=%u post=%u cleared=%u "
                  "(must match)",
                  static_cast<int>(pre_shadow),
                  static_cast<int>(post_shadow),
                  static_cast<int>(cleared_shadow),
                  pre_p7ffd_no_bit3, post_p7ffd_no_bit3,
                  cleared_p7ffd_no_bit3,
                  pre_rom_bank, post_rom_bank, cleared_rom_bank));
    }
}
} // namespace

// ── Category 12: Alternate ROM (NR 0x8C) ──────────────────────────────
// VHDL: zxnext.vhd:2247-2265. Branch C.2 adds NR 0x8C register storage
// + decoded flag accessors on Mmu (set_nr_8c / get_nr_8c /
// nr_8c_altrom_{en,rw,lock_rom0,lock_rom1}). The altrom ADDRESS OVERRIDE
// in the SRAM arbiter (zxnext.vhd:2981-3001, 3021, 3056) is NOT wired
// by Branch C — ALT-08 remains skipped for that reason.

void test_cat12_altrom() {
    set_group("Cat12 alternate ROM (NR 0x8C)");

    // ALT-01: altrom enable. VHDL zxnext.vhd:2262 nr_8c_altrom_en = bit 7.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0x80);
        check("ALT-01",
              "NR 0x8C bit 7 → altrom_en — VHDL zxnext.vhd:2262",
              f.mmu.nr_8c_altrom_en() && !f.mmu.nr_8c_altrom_rw() &&
              !f.mmu.nr_8c_altrom_lock_rom0() && !f.mmu.nr_8c_altrom_lock_rom1(),
              fmt("en=%d rw=%d lk0=%d lk1=%d (expected 1,0,0,0)",
                  f.mmu.nr_8c_altrom_en(), f.mmu.nr_8c_altrom_rw(),
                  f.mmu.nr_8c_altrom_lock_rom0(), f.mmu.nr_8c_altrom_lock_rom1()));
    }

    // ALT-02: altrom disable. bit 7 = 0 clears nr_8c_altrom_en.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0x80);
        f.mmu.set_nr_8c(0x00);
        check("ALT-02",
              "NR 0x8C bit 7 = 0 → altrom_en cleared — VHDL zxnext.vhd:2262",
              !f.mmu.nr_8c_altrom_en(),
              fmt("en=%d (expected 0)", f.mmu.nr_8c_altrom_en()));
    }

    // ALT-03: altrom_rw=1. VHDL zxnext.vhd:2263 nr_8c_altrom_rw = bit 6.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0xC0);  // en | rw
        check("ALT-03",
              "NR 0x8C bit 6 → altrom_rw — VHDL zxnext.vhd:2263",
              f.mmu.nr_8c_altrom_en() && f.mmu.nr_8c_altrom_rw(),
              fmt("en=%d rw=%d (expected 1,1)",
                  f.mmu.nr_8c_altrom_en(), f.mmu.nr_8c_altrom_rw()));
    }

    // ALT-04: altrom read-only. bit 6 = 0 clears nr_8c_altrom_rw.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0x80);  // en, no rw
        check("ALT-04",
              "NR 0x8C bit 6 = 0 → altrom_rw cleared (read-only altrom) "
              "— VHDL zxnext.vhd:2263",
              f.mmu.nr_8c_altrom_en() && !f.mmu.nr_8c_altrom_rw(),
              fmt("en=%d rw=%d (expected 1,0)",
                  f.mmu.nr_8c_altrom_en(), f.mmu.nr_8c_altrom_rw()));
    }

    // ALT-05: Lock ROM1. VHDL zxnext.vhd:2264 nr_8c_altrom_lock_rom1 = bit 5.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0xA0);  // en | lock_rom1
        check("ALT-05",
              "NR 0x8C bit 5 → altrom_lock_rom1 — VHDL zxnext.vhd:2264",
              f.mmu.nr_8c_altrom_lock_rom1() && !f.mmu.nr_8c_altrom_lock_rom0(),
              fmt("lk1=%d lk0=%d (expected 1,0)",
                  f.mmu.nr_8c_altrom_lock_rom1(), f.mmu.nr_8c_altrom_lock_rom0()));
    }

    // ALT-06: Lock ROM0. VHDL zxnext.vhd:2265 nr_8c_altrom_lock_rom0 = bit 4.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0x90);  // en | lock_rom0
        check("ALT-06",
              "NR 0x8C bit 4 → altrom_lock_rom0 — VHDL zxnext.vhd:2265",
              f.mmu.nr_8c_altrom_lock_rom0() && !f.mmu.nr_8c_altrom_lock_rom1(),
              fmt("lk0=%d lk1=%d (expected 1,0)",
                  f.mmu.nr_8c_altrom_lock_rom0(), f.mmu.nr_8c_altrom_lock_rom1()));
    }

    // ALT-07: Reset copies bits 3:0 into 7:4 per VHDL zxnext.vhd:2254-2256
    //   if reset='1' then nr_8c_altrom(7:4) <= nr_8c_altrom(3:0); end if;
    // The low nibble models reset defaults that survive reset and clone
    // into the upper nibble. Starting from 0x85 (upper nibble 0x8, lower
    // 0x5), one reset should yield bits 7:4 = 0x5 and bits 3:0 = 0x5 = 0x55.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0x85);
        f.mmu.reset();
        const uint8_t post = f.mmu.get_nr_8c();
        check("ALT-07",
              "hard reset copies NR 0x8C bits 3:0 into bits 7:4 "
              "— VHDL zxnext.vhd:2254-2256",
              post == 0x55,
              fmt("NR 0x8C after reset=0x%02X expected=0x55", post));
    }

    // ALT-08: altrom SRAM address formula = "0000011" & alt_128_n & a(13).
    // VHDL zxnext.vhd:2981-3001 (sram_rom override) + 3021 (sram_pre_alt_en)
    // + 3078 (read-path arbiter takes altrom when sram_pre_rdonly=1).
    // With altrom_en=1, altrom_rw=0 and no lock bits, on the ZXN_ISSUE2
    // default machine type with port_7ffd_=0, the alt_128_n selector
    // resolves to 0 (port_1ffd_rom(0) = bit 4 of port_7ffd = 0). Thus
    // alt-ROM 8K page = 0x0C | 0 | (addr>>13) = 12 for a13=0, 13 for
    // a13=1. Observe by seeding the alt-ROM SRAM page directly and
    // asserting the CPU read returns the sentinel, not the normal ROM.
    {
        Fixture f;
        f.fresh();                         // slots 0/1 ROM, config_mode off
        f.mmu.set_config_mode(false);
        // Seed alt-ROM SRAM page 12 (addr 0x0000 maps here when alt_128_n=0).
        uint8_t* p12 = f.ram.page_ptr(12);
        if (p12) p12[0x0000] = 0xC3;
        f.mmu.set_nr_8c(0x80);             // altrom_en=1, altrom_rw=0
        const uint8_t v = f.mmu.read(0x0000);
        check("ALT-08",
              "altrom SRAM address formula routes 0x0000 to SRAM page 12 "
              "when alt_128_n=0 — VHDL zxnext.vhd:2981-3001, 3021, 3078, 3117",
              v == 0xC3,
              fmt("altrom-read(0x0000)=0x%02X expected=0xC3 "
                  "(alt-ROM SRAM page 12, a13=0)", v));
    }

    // ALT-09: Read-back returns the stored register value. VHDL
    // zxnext.vhd:6156 port_253b_dat <= nr_8c_altrom.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_8c(0xA5);
        const uint8_t v = f.mmu.get_nr_8c();
        check("ALT-09",
              "NR 0x8C read-back returns stored byte — VHDL zxnext.vhd:6156",
              v == 0xA5,
              fmt("NR 0x8C read=0x%02X expected=0xA5", v));
    }
}

// ── Category 13: Config mode (NR 0x03/0x04) ───────────────────────────
// VHDL: zxnext.vhd:3044-3050. While nr_03_config_mode='1', CPU accesses
// to 0x0000-0x3FFF on ROM-mapped slots are routed to SRAM at bank
// nr_04_romram_bank (8 KB pages, 9-bit address = nr_04<<1 | slot).
// Writes are allowed (sram_pre_rdonly<='0', line 3049). MMU-RAM mapping
// wins over config_mode (line 3037), and boot ROM overlay wins over the
// whole SRAM path.

void test_cat13_config_mode() {
    set_group("Cat13 config mode (NR 0x03/0x04)");

    // CFG-01: config_mode=1 + NR 0x04 routes 0x0000-0x3FFF ROM-slot writes
    // to SRAM bank (nr_04<<1 | slot). Write via MMU, read back via
    // ram.page_ptr() to prove the bank landing. Emulator::init() pushes
    // config_mode=1 for ZXN machines; here we drive it directly since the
    // Mmu unit fixture is machine-type-agnostic.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x02);   // bank 2 → pages 4,5 (slots 0,1)
        f.mmu.write(0x0000, 0xA5);
        f.mmu.write(0x2001, 0x5A);
        const uint8_t* p4 = f.ram.page_ptr(4);
        const uint8_t* p5 = f.ram.page_ptr(5);
        const bool ok = p4 && p5 && p4[0x0000] == 0xA5 && p5[0x0001] == 0x5A;
        check("CFG-01",
              "config_mode=1 routes 0x0000-0x3FFF ROM-slot writes to SRAM via NR 0x04 — VHDL zxnext.vhd:3044-3050",
              ok,
              fmt("p4[0]=0x%02X p5[1]=0x%02X", p4 ? p4[0] : 0xEE, p5 ? p5[1] : 0xEE));
    }

    // CFG-02: config_mode=1 reads from 0x0000-0x3FFF on ROM-slot return
    // SRAM bank contents (not rom_.page_ptr() content). Seed SRAM directly,
    // then read via MMU under config_mode.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x03);   // bank 3 → pages 6,7
        uint8_t* p6 = f.ram.page_ptr(6);
        uint8_t* p7 = f.ram.page_ptr(7);
        if (p6) p6[0x0100] = 0xDE;
        if (p7) p7[0x0200] = 0xAD;
        const uint8_t got0 = f.mmu.read(0x0100);   // slot 0 → page 6
        const uint8_t got1 = f.mmu.read(0x2200);   // slot 1 → page 7
        check("CFG-02",
              "config_mode=1 reads from 0x0000-0x3FFF ROM-slot return SRAM bank contents — VHDL zxnext.vhd:3044-3050",
              got0 == 0xDE && got1 == 0xAD,
              fmt("got0=0x%02X got1=0x%02X (expected 0xDE, 0xAD)", got0, got1));
    }

    // CFG-03: MMU-RAM mapping wins over config_mode (VHDL line 3037 checks
    // mmu_A21_A13(8)=0 before line 3044 checks config_mode). set_page() on
    // slot 0 puts a real RAM page there; writes must land in that RAM page,
    // not in the NR 0x04-selected SRAM bank.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x05);   // bank 5 → pages 10,11 (if routing fires)
        f.mmu.set_page(0, 0x20);             // slot 0 → RAM page 0x20 (not ROM)
        f.mmu.write(0x0010, 0xC3);
        uint8_t* mapped = f.ram.page_ptr(0x20);
        uint8_t* would_be_cfg = f.ram.page_ptr(10);
        const bool ok = mapped && mapped[0x0010] == 0xC3 &&
                        (!would_be_cfg || would_be_cfg[0x0010] != 0xC3);
        check("CFG-03",
              "MMU-RAM mapping on ROM-slot range wins over config_mode routing — VHDL zxnext.vhd:3037",
              ok,
              fmt("mapped[0x10]=0x%02X cfg_bank[0x10]=0x%02X",
                  mapped ? mapped[0x0010] : 0xEE,
                  would_be_cfg ? would_be_cfg[0x0010] : 0xEE));
    }

    // CFG-04: config_mode exits with set_config_mode(false). Writes to
    // 0x0000-0x3FFF on ROM slots revert to the normal read-only behaviour
    // (ROM-08 covers this path, CFG-04 just verifies the toggle).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_nr_04_romram_bank(0x00);
        f.mmu.set_config_mode(false);
        uint8_t* p0 = f.ram.page_ptr(0);
        const uint8_t bank0_before = p0 ? p0[0x0050] : 0xEE;
        f.mmu.write(0x0050, 0xFA);
        const uint8_t bank0_after = p0 ? p0[0x0050] : 0xEE;
        check("CFG-04",
              "config_mode=0 suppresses ROM-slot routing; writes drop — VHDL zxnext.vhd:3044-3050 bypassed",
              bank0_before == bank0_after,
              fmt("bank0[0x50] before=0x%02X after=0x%02X", bank0_before, bank0_after));
    }

    // CFG-05: address bit 13 picks the upper/lower 8 KB of the selected
    // 16 KB ROM bank. VHDL line 3045: sram_pre_A21_A13 = nr_04 & cpu_a(13).
    // slot 0 (addr 0x0000-0x1FFF) → SRAM page nr_04*2 + 0
    // slot 1 (addr 0x2000-0x3FFF) → SRAM page nr_04*2 + 1
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x04);   // bank 4 → pages 8, 9
        f.mmu.write(0x0000, 0x11);           // → page 8 [0]
        f.mmu.write(0x2000, 0x22);           // → page 9 [0]
        uint8_t* p8 = f.ram.page_ptr(8);
        uint8_t* p9 = f.ram.page_ptr(9);
        check("CFG-05",
              "addr bit 13 selects upper/lower 8 KB of nr_04 bank — VHDL zxnext.vhd:3045 (nr_04<<1 | cpu_a(13))",
              p8 && p9 && p8[0] == 0x11 && p9[0] == 0x22,
              fmt("p8[0]=0x%02X p9[0]=0x%02X (expected 0x11 0x22)",
                  p8 ? p8[0] : 0xEE, p9 ? p9[0] : 0xEE));
    }

    // CFG-06: Mmu::reset() leaves config_mode unchanged (Emulator::init() is
    // the owner, since config_mode is a Next-only signal — a 48K MMU reset
    // must not arm Next routing). nr_04_romram_bank is reset to 0x00 per
    // VHDL zxnext.vhd:1104. We verify by: pre-set config_mode=true and
    // nr_04=0xFF; call reset; observe nr_04 went back to 0 while
    // config_mode stayed at 1 (the owner's choice).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0xFF);
        f.mmu.reset();
        // After reset: config_mode is still true (pushed by us above),
        // nr_04 is 0 (VHDL default) → a slot-0 write must land at page 0.
        f.mmu.write(0x0000, 0x77);
        uint8_t* p0   = f.ram.page_ptr(0);
        uint8_t* p510 = f.ram.page_ptr(510);   // where nr_04=0xFF<<1 | 0 would land
        check("CFG-06",
              "Mmu::reset() clears nr_04_romram_bank to 0 (VHDL zxnext.vhd:1104); config_mode is Emulator-owned",
              p0 && p0[0] == 0x77 && (!p510 || p510[0] != 0x77),
              fmt("p0[0]=0x%02X p510[0]=%s",
                  p0 ? p0[0] : 0xEE,
                  p510 ? fmt("0x%02X", p510[0]).c_str() : "<oob>"));
    }

    // CFG-07: out-of-range nr_04 banks (page >= ram size) → read returns 0xFF
    // and write is silently dropped. VHDL zxnext.vhd:3045 allows a full 8-bit
    // bank (Issue-5 board, line 5732) so nr_04=0xFF addresses SRAM page 510,
    // which is past the 2 MB default Ram fixture (256 pages). ram_.page_ptr()
    // returns nullptr and the Mmu hot path falls back to 0xFF / drop.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0xFF);   // → page 510 (OOB for 2 MB)
        f.mmu.write(0x0000, 0xC3);           // must be dropped, not crash
        const uint8_t v = f.mmu.read(0x0000);
        check("CFG-07",
              "out-of-range nr_04 bank → read 0xFF + write drop — Ram::page_ptr nullptr fallback",
              v == 0xFF,
              fmt("read(0x0000)=0x%02X expected=0xFF", v));
    }

    // CFG-08: setter API round-trip — toggle config_mode on/off and change
    // nr_04, confirm the MMU hot path sees the updated values. Exercises
    // that the Emulator-pushed mirror tracks the live state across
    // transitions (matches the NR 0x03 / NR 0x04 write-handler contract).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        f.mmu.set_nr_04_romram_bank(0x00);
        f.mmu.write(0x0000, 0xAA);           // config_mode=0 → dropped at ROM slot
        uint8_t* p0 = f.ram.page_ptr(0);
        const uint8_t dropped = p0 ? p0[0] : 0xEE;
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x30);   // bank 0x30 → pages 96, 97 (in-range)
        f.mmu.write(0x0001, 0xBB);           // config_mode=1 + nr_04=0x30 → page 96
        uint8_t* p96 = f.ram.page_ptr(96);
        const uint8_t routed = p96 ? p96[1] : 0xEE;
        check("CFG-08",
              "set_config_mode / set_nr_04_romram_bank toggle between drop and route",
              dropped != 0xAA && routed == 0xBB,
              fmt("dropped=0x%02X routed=0x%02X", dropped, routed));
    }

    // CFG-09: rom_in_sram=true makes ROM-slot reads come from ram_ pages 0..7
    // instead of rom_.page_ptr(). VHDL zxnext.vhd:3052: sram_pre_A21_A13 <=
    // "000000" & sram_rom & cpu_a(13) — normal-mode ROM reads on the Next
    // target SRAM pages 0..7, not a separate ROM chip. Proof: seed a byte in
    // each backing, toggle rom_in_sram, observe the pointer follows.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);        // disable Branch 1 routing to isolate Branch 2
        // Fixture tags rom_ pages with (page<<4 | offset_lo); rom_[0][0] = 0x00.
        // Seed a distinguishable byte in ram_ page 0 so we can tell the two apart.
        uint8_t* ram0 = f.ram.page_ptr(0);
        if (ram0) ram0[0] = 0x7C;
        const uint8_t via_rom = f.mmu.read(0x0000);  // rom_in_sram=false → rom_[0][0] = 0x00
        f.mmu.set_rom_in_sram(true);
        const uint8_t via_sram = f.mmu.read(0x0000); // rom_in_sram=true → ram_[0][0] = 0x7C
        check("CFG-09",
              "rom_in_sram=true routes ROM-slot reads through ram_ pages 0..7 — VHDL zxnext.vhd:3052",
              via_rom == 0x00 && via_sram == 0x7C,
              fmt("via_rom=0x%02X via_sram=0x%02X (expected 0x00, 0x7C)", via_rom, via_sram));
    }

    // CFG-10: writes to ROM slots while rom_in_sram=true AND config_mode=0
    // are still silently dropped. ROM-in-SRAM only changes where READS come
    // from; the VHDL sram_pre_rdonly signal still flags the slot read-only
    // outside config_mode (zxnext.vhd:3056).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        f.mmu.set_rom_in_sram(true);
        uint8_t* ram0 = f.ram.page_ptr(0);
        if (ram0) ram0[0x0100] = 0x11;
        f.mmu.write(0x0100, 0x99);
        const uint8_t after = ram0 ? ram0[0x0100] : 0xEE;
        check("CFG-10",
              "rom_in_sram + config_mode=0: writes to ROM slot still drop — VHDL zxnext.vhd:3056 sram_pre_rdonly",
              after == 0x11,
              fmt("after=0x%02X expected=0x11", after));
    }

    // CFG-11: toggle rom_in_sram true → false re-points ROM slots back at
    // rom_. Covers the review defense-in-depth: set_rom_in_sram() routes via
    // rebuild_ptr() for every slot, so the sentinel (page==0xFF) and RAM
    // paths stay consistent.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        uint8_t* ram0 = f.ram.page_ptr(0);
        if (ram0) ram0[0] = 0x7C;            // SRAM-side distinguisher
        f.mmu.set_rom_in_sram(true);
        const uint8_t on  = f.mmu.read(0x0000);   // → ram_[0][0] = 0x7C
        f.mmu.set_rom_in_sram(false);
        const uint8_t off = f.mmu.read(0x0000);   // → rom_[0][0] = 0x00 (fixture tag)
        check("CFG-11",
              "set_rom_in_sram(true)→(false) restores ROM-slot reads to rom_ buffer",
              on == 0x7C && off == 0x00,
              fmt("on=0x%02X off=0x%02X (expected 0x7C, 0x00)", on, off));
    }
}

// ── Category 14: Address translation ──────────────────────────────────
// VHDL: zxnext.vhd:2964  mmu_A21_A13 = ("0001" + page(7:5)) & page(4:0).
// For pages < 0xE0 this is equivalent to: ram page index = P, which we
// verify by a round-trip through the Mmu: set_page(slot, P), write, then
// read back through ram.page_ptr(P).

void test_cat14_addr_translation() {
    set_group("Cat14 address translation");

    struct AdrRow { const char* id; uint8_t page; };
    const AdrRow rows[] = {
        {"ADR-01", 0x00},
        {"ADR-02", 0x01},
        {"ADR-03", 0x0A},
        {"ADR-04", 0x0B},
        // 0x0E = dedicated bank-7 BRAM, no SRAM round-trip (Cat28);
        // 0x0F (bank 7 upper) IS plain SRAM per zxnext.vhd:2962.
        {"ADR-05", 0x0F},
        {"ADR-06", 0x10},
        {"ADR-07", 0x20},
        {"ADR-08", 0xDF},
    };
    for (const AdrRow& r : rows) {
        Fixture f;
        f.fresh();
        f.mmu.set_page(4, r.page);        // slot 4 = 0x8000
        f.mmu.write(0x8000, 0x5A);
        const uint8_t v = f.ram.page_ptr(r.page)[0];
        check(r.id,
              "page→SRAM round-trip — VHDL zxnext.vhd:2964",
              v == 0x5A,
              fmt("page 0x%02X: ram[page][0]=0x%02X expected=0x5A", r.page, v));
    }

    // ADR-09 / ADR-10 — floating bus for page ≥ 0xE0 on RAM slots 2..7.
    //
    // VHDL zxnext.vhd:3060-3061 sets sram_pre_active='0' for any RAM slot
    // (cpu_a(15:14) ≠ "00") when mmu_A21_A13(8)='1'. Per the formula at
    // zxnext.vhd:2964, bit 8 = '1' iff mem_active_page(7:5)="111" i.e.
    // page >= 0xE0. Inactive SRAM → CPU read = floating bus (0xFF). The
    // C++ gate lives in Mmu::rebuild_ptr (RAM-slot branch); set_page on
    // slots 2..7 with page >= 0xE0 installs a null read_ptr so read()
    // falls through to the "unmapped → 0xFF" return.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(4, 0xE0);          // slot 4 = 0x8000
        const uint8_t v = f.mmu.read(0x8000);
        check("ADR-09",
              "page 0xE0 on RAM slot → floating bus (0xFF) — VHDL zxnext.vhd:3060-3061",
              v == 0xFF,
              fmt("read(0x8000) with slot4=0xE0: 0x%02X expected=0xFF", v));
    }
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(4, 0xFE);          // slot 4 = 0x8000
        const uint8_t v = f.mmu.read(0x8000);
        check("ADR-10",
              "page 0xFE on RAM slot → floating bus (0xFF) — VHDL zxnext.vhd:3060-3061",
              v == 0xFF,
              fmt("read(0x8000) with slot4=0xFE: 0x%02X expected=0xFF", v));
    }
}

// ── Category 15: Bank 5/7 special pages ───────────────────────────────
// VHDL: zxnext.vhd:2933-3133 sram_bank5/sram_bank7 dual-port routing.
// The C++ Mmu treats pages 0x0A/0x0B/0x0E as ordinary RAM pages — the
// "dual-port" property is irrelevant to the CPU-visible behaviour and
// there is no sram_active / sram_bank5 accessor on the Mmu.

void test_cat15_bank57() {
    set_group("Cat15 bank5/bank7 pages");

    // BNK-01..04: VHDL zxnext.vhd:2961-2962 mark pages 0x0A/0x0B (bank 5)
    // and 0x0E (bank 7 lower) as dedicated dual-port BRAMs: bank5_ram
    // (16K dpram2, zxnext.vhd:6558-6578) and bank7_ram (8K dpram2,
    // :6670). mem_active_bank5/7 suppress the external-SRAM cycle
    // (:3037-3041/:3059-3063), so a slot mapped to page 0x0A/0x0B/0x0E
    // writes the corresponding BRAM buffer — NEVER any ram_ page.
    // (Task 25 rewrite, 2026-07-10: the previous BNK-01/02 asserted the
    // old aliased model — bank-5 writes landing on SRAM page 0x0A/0x0B —
    // which the Task 23 mid-boot-garbage root cause refuted.) Compare
    // against a non-BRAM page like 0x0F which shifts to 0x2F.

    // BNK-01 — page 0x0A maps to the dedicated bank-5 VRAM (lower half).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(4, 0x0A);             // slot 4 @ 0x8000, logical 0x0A
        f.mmu.write(0x8000, 0x5A);
        const uint8_t in_vram = f.mmu.bank5_vram()[0];
        const uint8_t at_0a = f.ram.page_ptr(0x0A)[0];
        const uint8_t at_2a = f.ram.page_ptr(0x2A)[0];
        check("BNK-01",
              "page 0x0A maps to the dedicated bank-5 VRAM buffer, touching "
              "no SRAM page — VHDL zxnext.vhd:2961+6558",
              in_vram == 0x5A && at_0a != 0x5A && at_2a != 0x5A,
              fmt("vram[0]=0x%02X ram[0x0A][0]=0x%02X ram[0x2A][0]=0x%02X",
                  in_vram, at_0a, at_2a));
    }

    // BNK-02 — page 0x0B maps to the bank-5 VRAM upper 8K half.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(4, 0x0B);
        f.mmu.write(0x8000, 0x5B);
        const uint8_t in_vram = f.mmu.bank5_vram()[0x2000];
        const uint8_t at_0b = f.ram.page_ptr(0x0B)[0];
        const uint8_t at_2b = f.ram.page_ptr(0x2B)[0];
        check("BNK-02",
              "page 0x0B maps to the bank-5 VRAM upper half (offset 0x2000), "
              "touching no SRAM page — VHDL zxnext.vhd:2961+6558",
              in_vram == 0x5B && at_0b != 0x5B && at_2b != 0x5B,
              fmt("vram[0x2000]=0x%02X ram[0x0B][0]=0x%02X ram[0x2B][0]=0x%02X",
                  in_vram, at_0b, at_2b));
    }

    // BNK-03 — page 0x0E (bank 7 lower half) is a dedicated BRAM on real
    // hardware (zxnext.vhd:2962 mem_active_bank7; the BRAM is bank7_ram
    // dpram2 at :6670). jnext serves it from Mmu::bank7_bram_ — its own
    // buffer, physically separate from every ram_ page (review 2026-07-10:
    // an interim SRAM-page-0x2E stand-in was refuted because config-mode
    // NR $04=$17 legally reaches 0x2E). See Cat28 for collision tests.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(4, 0x0E);
        f.mmu.write(0x8000, 0x7E);
        const uint8_t in_bram = f.mmu.bank7_bram()[0];
        const uint8_t at_0e = f.ram.page_ptr(0x0E)[0];
        const uint8_t at_2e = f.ram.page_ptr(0x2E)[0];
        check("BNK-03",
              "page 0x0E maps to the dedicated bank-7 BRAM buffer, touching "
              "no SRAM page — VHDL zxnext.vhd:2962+6670",
              in_bram == 0x7E && at_0e != 0x7E && at_2e != 0x7E,
              fmt("bram[0]=0x%02X ram[0x0E][0]=0x%02X ram[0x2E][0]=0x%02X",
                  in_bram, at_0e, at_2e));
    }

    // BNK-04 — page 0x0F is NOT a bypass page: it gets the normal +0x20
    // shift in Next mode. Write via slot → lands on SRAM page 0x2F (not
    // 0x0F). Negative counterpart of BNK-01..03.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(4, 0x0F);
        f.mmu.write(0x8000, 0x7F);
        const uint8_t at_0f = f.ram.page_ptr(0x0F)[0];
        const uint8_t at_2f = f.ram.page_ptr(0x2F)[0];
        check("BNK-04",
              "page 0x0F is NOT dual-port — gets +0x20 shift like any RAM page "
              "— VHDL zxnext.vhd:2961-2962 (bypass only for 0x0A/0x0B/0x0E)",
              at_2f == 0x7F && at_0f != 0x7F,
              fmt("ram[0x0F][0]=0x%02X ram[0x2F][0]=0x%02X (want 0x7F at 0x2F only)",
                  at_0f, at_2f));
    }

    // BNK-05: CPU round-trip through page 0x0A — write via slot, read
    // via slot. VHDL zxnext.vhd:2933-3133 guarantees the CPU sees its
    // own writes (the dual-port only affects ULA/tilemap fetch paths).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(4, 0x0A);
        f.mmu.write(0x8000, 0x55);
        const uint8_t v = f.mmu.read(0x8000);
        check("BNK-05",
              "page 0x0A CPU round-trip — VHDL zxnext.vhd:2933-3133",
              v == 0x55,
              fmt("read=0x%02X expected=0x55", v));
    }

    // BNK-06: CPU round-trip through page 0x0E.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(6, 0x0E);
        f.mmu.write(0xC000, 0x77);
        const uint8_t v = f.mmu.read(0xC000);
        check("BNK-06",
              "page 0x0E CPU round-trip — VHDL zxnext.vhd:2933-3133",
              v == 0x77,
              fmt("read=0x%02X expected=0x77", v));
    }
}

// ── Category 16 (memory contention): MIGRATED ────────────────────────
// The 13 memory-contention rows that lived here have moved to
// test/contention/contention_test.cpp per the §15 C2-move commitment in
// doc/testing/CONTENTION-TEST-PLAN-DESIGN.md. The legacy row identifiers
// no longer exist anywhere in the tree; for the historical mapping see
// the §"Cross-reference" table in that plan doc.

// ── Category 17: Layer 2 memory mapping ───────────────────────────────
// VHDL: zxnext.vhd:2966-2971 (layer2_active_page formula),
//       zxnext.vhd:3077 (L2 decode in 0-16K priority),
//       zxnext.vhd:3100-3107 (L2 segment/auto mapping).

void test_cat17_l2_mapping() {
    set_group("Cat17 Layer 2 memory mapping");

    // L2M-01: L2 write-over routes writes to the L2 bank's physical
    // SRAM, not to the MMU page that happens to be mounted in slot 0.
    // Pick an MMU page that is disjoint from the L2-bank physical pages
    // so a successful L2 redirect can be observed as an absence.
    //
    // VHDL zxnext.vhd:2969 layer2_active_page = (layer2_bank << 1) | cpu_a(13)
    // so L2 bank 8 maps to physical pages 0x10 / 0x11.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x20);           // slot 0 → physical page 0x20
        f.mmu.set_l2_port(0x01, 8);  // enable L2 write-over, seg=00, bank 8
        f.mmu.write(0x0000, 0xAB);
        f.mmu.set_l2_port(0x00, 8);  // disable L2 write-over
        const uint8_t mmu_side = f.ram.page_ptr(0x20)[0];
        const uint8_t l2_side  = f.ram.page_ptr(0x10)[0];
        check("L2M-01",
              "L2 write-over lands in L2 bank physical SRAM, not MMU slot — VHDL zxnext.vhd:2969,3077",
              mmu_side != 0xAB && l2_side == 0xAB,
              fmt("MMU page 0x20[0]=0x%02X (should NOT be 0xAB), L2 page 0x10[0]=0x%02X (should be 0xAB)",
                  mmu_side, l2_side));
    }

    // L2M-01b: when the MMU slot IS the same physical page that the L2
    // bank aliases, both paths observe the same SRAM byte. VHDL
    // zxnext.vhd:2964 and :2969 use the same page→SRAM formula, so the
    // collision is architectural.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x10);           // slot 0 IS the L2 alias page
        f.mmu.set_l2_port(0x01, 8);  // L2 bank 8 → physical pages 0x10/0x11
        f.mmu.write(0x0000, 0xAB);
        f.mmu.set_l2_port(0x00, 8);
        const uint8_t via_mmu = f.mmu.read(0x0000);
        check("L2M-01b",
              "L2 bank 8 aliases MMU page 0x10 (hardware collision) — VHDL zxnext.vhd:2964,2969",
              via_mmu == 0xAB,
              fmt("MMU read through slot 0 = 0x%02X expected=0xAB (same SRAM as L2 bank 8)", via_mmu));
    }

    // L2M-02a: L2 read-over redirects 0x0000-0x3FFF reads to the L2 bank
    // instead of returning the byte from the MMU-mapped slot. Picks an
    // MMU page disjoint from the L2-bank physical pages so the outcome
    // is observable as a distinct sentinel. VHDL zxnext.vhd:2969,3077,3100.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x20);                  // slot 0 → physical page 0x20
        f.ram.page_ptr(0x20)[0] = 0x55;           // MMU-side sentinel
        f.ram.page_ptr(0x10)[0] = 0xA7;           // L2 bank 8 → physical page 0x10
        f.mmu.set_l2_port(0x04, 8);               // bit 2 = read-enable, seg=00 (0x0000-0x3FFF)
        const uint8_t got = f.mmu.read(0x0000);
        f.mmu.set_l2_port(0x00, 8);
        check("L2M-02a",
              "L2 read-over returns L2 bank byte, not MMU slot — VHDL zxnext.vhd:2969,3077,3100",
              got == 0xA7,
              fmt("read(0x0000)=0x%02X expected=0xA7 (L2 bank 8 page 0x10); MMU-side sentinel was 0x55", got));
    }

    // L2M-02b: discriminative — clearing bit 2 disables L2 read-over and
    // the CPU read returns the MMU slot byte. Without the read_enable
    // latch this test would fail because the redirect would fire
    // unconditionally (VHDL zxnext.vhd:3077 sram_layer2_map_en requires
    // sram_pre_layer2_rd_en when cpu_rd_n='0').
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x20);
        f.ram.page_ptr(0x20)[0] = 0x55;
        f.ram.page_ptr(0x10)[0] = 0xA7;
        f.mmu.set_l2_port(0x00, 8);               // bit 2 = 0 → read-enable OFF
        const uint8_t got = f.mmu.read(0x0000);
        check("L2M-02b",
              "L2 read-over OFF → MMU slot wins — VHDL zxnext.vhd:3077 sram_pre_layer2_rd_en gate",
              got == 0x55,
              fmt("read(0x0000)=0x%02X expected=0x55 (MMU slot 0 page 0x20); L2 sentinel was 0xA7", got));
    }

    // L2M-03: auto-segment (port_123b bits 7:6 = 11) maps the segment to
    // cpu_a(15:14). Exercise through the write path: enable write-over
    // with seg=11 and verify writes at 0x0000 land on bank+0, at 0x4000
    // on bank+1, at 0x8000 on bank+2. VHDL zxnext.vhd:3100-3107.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x20);
        f.mmu.set_page(2, 0x22);
        f.mmu.set_page(4, 0x24);
        f.mmu.set_l2_port(0xC1, 8);  // bits 7:6=11 (seg=all), enable
        f.mmu.write(0x0000, 0x10);         // → L2 bank 8 (page 0x10)
        f.mmu.write(0x4000, 0x11);         // → L2 bank 9 (page 0x12)
        f.mmu.write(0x8000, 0x12);         // → L2 bank 10 (page 0x14)
        f.mmu.set_l2_port(0x00, 8);
        const uint8_t a = f.ram.page_ptr(0x10)[0];
        const uint8_t b = f.ram.page_ptr(0x12)[0];
        const uint8_t c = f.ram.page_ptr(0x14)[0];
        check("L2M-03",
              "L2 auto-segment follows cpu_a(15:14) — VHDL zxnext.vhd:3100-3107",
              a == 0x10 && b == 0x11 && c == 0x12,
              fmt("seg0 page0x10[0]=0x%02X exp0x10, seg1 page0x12[0]=0x%02X exp0x11, seg2 page0x14[0]=0x%02X exp0x12",
                  a, b, c));
    }

    // L2M-04: L2 mapping does NOT apply to 0xC000-0xFFFF. Enable with
    // seg=all, write at 0xC000 must land on the MMU slot (not on an L2
    // bank). VHDL zxnext.vhd:3077 gates L2 priority on cpu_a(15:14) != "11".
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(6, 0x30);
        f.mmu.set_l2_port(0xC1, 8);
        f.mmu.write(0xC000, 0xCD);
        f.mmu.set_l2_port(0x00, 8);
        const uint8_t mmu_side = f.ram.page_ptr(0x30)[0];
        check("L2M-04",
              "L2 write-over does not apply to 0xC000-0xFFFF — VHDL zxnext.vhd:3077",
              mmu_side == 0xCD,
              fmt("MMU page 0x30[0]=0x%02X expected=0xCD (L2 must not intercept)", mmu_side));
    }

    // L2M-05: L2 base bank from NR 0x12 (active). Mmu's set_l2_port
    // takes the active bank as a direct argument; there is no NR 0x12/0x13
    // distinction on the Mmu surface — the shadow/active selection lives
    // in NextReg.
    // L2M-05 — COVERED AT test/nextreg/nextreg_integration_test.cpp
    // L2-Bank-NR (L2M-05/L2M-05b). NR 0x12 active-bank dispatch lives in
    // Emulator::init (handler registered at src/core/emulator.cpp:232)
    // and stores in Layer2::set_active_bank — not on the Mmu surface. The
    // integration test writes NR 0x12 through the real port path and
    // observes emu.layer2().active_bank(). Not a skip here — re-homed to
    // the NextREG integration tier.

    // L2M-06 — COVERED AT test/nextreg/nextreg_integration_test.cpp
    // L2-Bank-NR (L2M-06/L2M-06b). NR 0x13 shadow-bank dispatch is wired
    // symmetrically at src/core/emulator.cpp:237 and stored via
    // Layer2::set_shadow_bank. Not a skip here — re-homed to the NextREG
    // integration tier.
}

// ── Category 18: Memory decode priority ───────────────────────────────
// VHDL: zxnext.vhd:2933-3133 decode priority ladder
// (boot > multiface > divmmc > L2 > MMU > config > expansion > ROM).
// The Mmu class participates in this ladder only through the DivMmc
// overlay and the L2 write-over; the rest is higher-layer policy.

void test_cat18_priority() {
    set_group("Cat18 decode priority");

    // PRI-01 — TRACKED AT test/divmmc/divmmc_test.cpp group_sm() (SM-05
    // family). DivMMC ROM priority over MMU (VHDL zxnext.vhd:3084) needs
    // an Mmu+DivMmc integration fixture — the cross-object arbitration
    // lives in src/memory/mmu.cpp and is not exercisable from a bare Mmu
    // nor from a bare DivMmc unit. Not a skip here — DivMMC test owns
    // the tracking entry.

    // PRI-02 — TRACKED AT test/divmmc/divmmc_test.cpp group_sm() (SM-05
    // family). DivMMC RAM priority over MMU (VHDL zxnext.vhd:3087) —
    // same arbitration chain as PRI-01. Not a skip here — integration
    // tier owns it.

    // PRI-03: L2 overrides MMU in 0-16K. This is the same behaviour
    // observed by L2M-01; duplicate the observation here with a
    // distinct L2 bank and MMU page to pin it to the priority row.
    // VHDL zxnext.vhd:3077.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x30);           // MMU slot 0 → physical page 0x30
        f.mmu.set_l2_port(0x01, 16); // L2 bank 16 → physical page 0x20
        f.mmu.write(0x0000, 0x9E);
        f.mmu.set_l2_port(0x00, 16);
        const uint8_t mmu_side = f.ram.page_ptr(0x30)[0];
        const uint8_t l2_side  = f.ram.page_ptr(0x20)[0];
        check("PRI-03",
              "L2 write-over outranks MMU in 0-16K — VHDL zxnext.vhd:3077",
              mmu_side != 0x9E && l2_side == 0x9E,
              fmt("MMU page 0x30[0]=0x%02X (should not be 0x9E), L2 page 0x20[0]=0x%02X (should be 0x9E)",
                  mmu_side, l2_side));
    }

    // PRI-04 — TRACKED AT test/divmmc/divmmc_test.cpp group_sm() (SM-05
    // family). DivMMC beats Layer 2 (VHDL zxnext.vhd:3091) — same
    // arbitration chain as PRI-01/02. Not a skip here — integration
    // tier owns it.

    // PRI-05: plain MMU path in the upper half of memory with no
    // overrides active. VHDL zxnext.vhd:2933-3133, 48-64K region uses
    // MMU only.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(6, 0x40);
        f.mmu.write(0xC000, 0x12);
        const uint8_t v = f.ram.page_ptr(0x40)[0];
        check("PRI-05",
              "MMU-only path at 0xC000 with no overrides — VHDL zxnext.vhd:2933-3133",
              v == 0x12,
              fmt("ram[0x40][0]=0x%02X expected=0x12", v));
    }

    // PRI-06: altrom overrides normal ROM in the decode priority ladder.
    // VHDL zxnext.vhd:3078 — when altrom_en=1 AND sram_pre_rdonly=1 (i.e.
    // altrom_rw=0), the read arbiter takes altrom BEFORE the normal ROM
    // path. Observable: seed alt-ROM SRAM page 12 with a sentinel that
    // differs from whatever the normal ROM would return for the same CPU
    // address; flip altrom_en 0→1 and assert the read value flips to the
    // sentinel. Framed as priority ("altrom > normal ROM") rather than
    // address translation (same observable, different framing from ALT-08).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(false);
        // Seed alt-ROM SRAM page 12 with a sentinel distinct from ROM byte 0.
        uint8_t* p12 = f.ram.page_ptr(12);
        if (p12) p12[0x0000] = 0x7E;
        f.mmu.set_nr_8c(0x00);             // altrom disabled → normal ROM
        const uint8_t rom_side = f.mmu.read(0x0000);
        f.mmu.set_nr_8c(0x80);             // altrom_en=1, altrom_rw=0
        const uint8_t alt_side = f.mmu.read(0x0000);
        check("PRI-06",
              "altrom overrides normal ROM when altrom_en=1, altrom_rw=0 "
              "— VHDL zxnext.vhd:3078 arbiter priority",
              rom_side != 0x7E && alt_side == 0x7E,
              fmt("altrom_en=0 read=0x%02X (should not be 0x7E), "
                  "altrom_en=1 read=0x%02X (should be 0x7E)", rom_side, alt_side));
    }

    // PRI-07: config-mode routing overrides the normal ROM serving path.
    // VHDL zxnext.vhd:3044-3050: at ROM-mapped slots with config_mode=1, a
    // CPU access goes to SRAM via nr_04_romram_bank instead of reading from
    // the rom_ buffer. Proof: a seeded SRAM bank is visible under
    // config_mode=1 but not under config_mode=0 (where the same read returns
    // rom_ content tagged by the fixture).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x06);   // bank 6 → SRAM pages 12, 13
        uint8_t* p12 = f.ram.page_ptr(12);
        if (p12) p12[0x0010] = 0x9A;
        const uint8_t cfg_on  = f.mmu.read(0x0010);
        f.mmu.set_config_mode(false);
        const uint8_t cfg_off = f.mmu.read(0x0010);
        check("PRI-07",
              "config_mode ROMRAM routing outranks normal ROM read path — VHDL zxnext.vhd:3044-3052",
              cfg_on == 0x9A && cfg_off != 0x9A,
              fmt("cfg_on=0x%02X cfg_off=0x%02X (expected 0x9A / !=0x9A)", cfg_on, cfg_off));
    }
}

// ── Cat 11.x: Boot ROM Overlay (G140 / G157) ─────────────────────────
void test_boot_rom_overlay() {
    set_group("BOOT-OVL");

    // BOOT-OVL-01 — VHDL zxnext.vhd:1856 gates the overlay on
    // cpu_a(15:14)="00" (full 16 KB at 0x0000-0x3FFF); VHDL:3199-3204
    // wires the bootrom entity to cpu_a(12:0) (13 bits = 8 KB span)
    // so the upper 8 KB MIRRORS the lower 8 KB. After the G140/G157
    // fix, src/memory/mmu.h read() masks `addr & 0x1FFF` and gates
    // `addr < 0x4000`. Load an 8 KB blob whose byte i = (i ^ 0x5A) &
    // 0xFF and verify each of the four 8 KB quarters returns the same
    // expected pattern.
    {
        Fixture f;
        std::vector<uint8_t> blob(0x2000);
        for (size_t i = 0; i < blob.size(); ++i)
            blob[i] = static_cast<uint8_t>((i ^ 0x5A) & 0xFF);
        f.mmu.set_boot_rom(blob.data(), blob.size());
        f.fresh();
        bool ok = true;
        uint32_t fail_addr = 0;
        uint8_t  fail_exp = 0, fail_act = 0;
        for (uint32_t addr = 0; addr < 0x4000; addr += 17) {
            uint8_t expected = blob[addr & 0x1FFF];
            uint8_t actual   = f.mmu.read(static_cast<uint16_t>(addr));
            if (actual != expected) {
                ok = false; fail_addr = addr; fail_exp = expected; fail_act = actual;
                break;
            }
        }
        check("BOOT-OVL-01",
              "8 KB boot ROM overlays full 16 KB at 0x0000-0x3FFF; upper "
              "8 KB mirrors lower per VHDL zxnext.vhd:3199-3204 (cpu_a(12:0)) "
              "and :1856 (cpu_a(15:14)=\"00\" gate)",
              ok,
              fmt("addr=0x%04X expected=0x%02X actual=0x%02X",
                  fail_addr, fail_exp, fail_act));
    }

    // BOOT-OVL-02 — gate is `cpu_a(15:14)="00"` (VHDL:1856), so reads at
    // 0x4000 and beyond fall through to the next overlay. With no DivMMC
    // and no L2 read-over, 0x4000 reads from MMU slot 2 (page 0x0A →
    // bank5) which is zero on a fresh Ram. The boot ROM sentinel 0xA5
    // must NOT appear at 0x4000.
    {
        Fixture f;
        std::vector<uint8_t> blob(0x2000, 0xA5);
        f.mmu.set_boot_rom(blob.data(), blob.size());
        f.fresh();
        const uint8_t at_3FFF = f.mmu.read(0x3FFF);
        const uint8_t at_4000 = f.mmu.read(0x4000);
        check("BOOT-OVL-02",
              "boot ROM does not leak past 0x3FFF — gate is cpu_a(15:14) "
              "per VHDL zxnext.vhd:1856",
              at_3FFF == 0xA5 && at_4000 != 0xA5,
              fmt("read(0x3FFF)=0x%02X read(0x4000)=0x%02X (expected 0xA5 / !=0xA5)",
                  at_3FFF, at_4000));
    }

    // BOOT-OVL-03 — VHDL zxnext.vhd:3199-3204 hardwires cpu_a(12:0), so
    // the overlay is exactly 8 KB. After the G157 fix, set_boot_rom()
    // accepts any source size but materialises an 8 KB internal buffer
    // (zero-padded for short blobs, truncated for long blobs). Load a
    // 4 KB blob: lower half returns blob bytes; upper half returns
    // zero (zero-pad); both halves mirror through the upper 8 KB.
    {
        Fixture f;
        std::vector<uint8_t> short_blob(0x1000);
        for (size_t i = 0; i < short_blob.size(); ++i)
            short_blob[i] = static_cast<uint8_t>(0x40 | (i & 0x3F));
        f.mmu.set_boot_rom(short_blob.data(), short_blob.size());
        f.fresh();
        const uint8_t at_0000 = f.mmu.read(0x0000);
        const uint8_t at_0FFF = f.mmu.read(0x0FFF);
        const uint8_t at_1000 = f.mmu.read(0x1000);
        const uint8_t at_1FFF = f.mmu.read(0x1FFF);
        const uint8_t at_2000 = f.mmu.read(0x2000);
        const uint8_t at_3000 = f.mmu.read(0x3000);
        check("BOOT-OVL-03",
              "wrong-sized boot ROM blob is zero-padded to 8 KB and mirrored "
              "through 16 KB — VHDL zxnext.vhd:3199-3204 hardwires cpu_a(12:0)",
              at_0000 == 0x40 && at_0FFF == 0x7F &&
              at_1000 == 0x00 && at_1FFF == 0x00 &&
              at_2000 == 0x40 && at_3000 == 0x00,
              fmt("0x0000=0x%02X 0x0FFF=0x%02X 0x1000=0x%02X 0x1FFF=0x%02X "
                  "0x2000=0x%02X 0x3000=0x%02X "
                  "(expected 0x40 / 0x7F / 0x00 / 0x00 / 0x40 / 0x00)",
                  at_0000, at_0FFF, at_1000, at_1FFF, at_2000, at_3000));
    }
}

// ── Cat 19: Soundrive Mode 2 vs paging-port write conflict (G146) ────
void test_soundrive_paging_conflict() {
    set_group("SD2");
    // VHDL zxnext.vhd:2708 — port_fd_conflict_wr fires when the OUT's
    // LOW BYTE is 0xF1 or 0xF9 (port_f1_lsb/port_f9_lsb decode the full
    // 8-bit cpu_a(7:0), zxnext.vhd:2508-2576) AND the Soundrive-SD2
    // decode is enabled (NR 0x84 bit 2 → internal_port_enable(18),
    // zxnext.vhd:2429-2430); zxnext.vhd:2718-2720,2725 then suppress
    // the 7FFD/DFFD/1FFD/3FFD paging writes. (0xF3/0xFB have A1:A0="11"
    // and can never alias the FD family; the original wording of these
    // rows — "0xF1FD/0xF9FD writes" — was wrong: those addresses have
    // low byte 0xFD and never conflict. Colliding addresses are e.g.
    // 0x7FF1 / 0xDFF9 / 0x1FF1.)
    //
    // SD2-01 — COVERED AT INTEGRATION TIER (not a skip). The conflict
    // lives in the port-dispatch layer (Emulator handler wiring), which
    // this bare-Mmu fixture cannot reach — Mmu::map_128k_bank() has no
    // port decode. See test/audio/audio_port_dispatch_test.cpp row
    // SD2-01 (gate SET ⇒ paging retained, byte lands on Soundrive).
    //
    // SD2-02 — COVERED AT INTEGRATION TIER (not a skip). Discriminative
    // pair: see test/audio/audio_port_dispatch_test.cpp row SD2-02
    // (gate CLEAR ⇒ the same OUTs reapply paging, DAC untouched).
}

// ── Cat 20: NEX Loader (BOOT-NEX-* parked here) (G155 / G156) ────────
void test_nex_loader() {
    set_group("BOOT-NEX");

    // BOOT-NEX-01 — NEX V1.1+ header byte 8 (`ram_required`) enumerates
    // the minimum installed RAM the file requires. After the G155 fix,
    // NexLoader::ram_required_fits() returns false when required_kb >
    // installed_kb, and apply() aborts before any bank load.
    {
        const bool fits_1mb = NexLoader::ram_required_fits(2, 1024 * 1024);
        check("BOOT-NEX-01",
              "loader rejects NEX whose ram_required exceeds installed RAM "
              "— src/core/nex_loader.cpp:apply() honours header.ram_required",
              fits_1mb == false,
              fmt("ram_required=2 vs 1024KB installed → fits=%d (expected 0)",
                  static_cast<int>(fits_1mb)));
    }

    // BOOT-NEX-02 — discriminative pair: same predicate must accept the
    // file when installed RAM meets or exceeds the requirement. Also
    // pin the spec mapping 0=768/1=1792/2=2048 KB (canonical
    // https://wiki.specnext.dev/NEX_file_format defines 0/1; value 2
    // documented in G155). Unknown values must return 0 (treated as
    // warn+default by the loader).
    {
        const bool fits_2mb = NexLoader::ram_required_fits(2, 2048 * 1024);
        const bool fits_768k_on_1mb = NexLoader::ram_required_fits(0, 1024 * 1024);
        const size_t kb0 = NexLoader::ram_required_kb(0);
        const size_t kb1 = NexLoader::ram_required_kb(1);
        const size_t kb2 = NexLoader::ram_required_kb(2);
        const size_t kb_unknown = NexLoader::ram_required_kb(3);
        check("BOOT-NEX-02",
              "loader accepts NEX whose ram_required ≤ installed RAM; "
              "spec mapping 0=768/1=1792/2=2048 KB; unknown → 0",
              fits_2mb && fits_768k_on_1mb &&
              kb0 == 768 && kb1 == 1792 && kb2 == 2048 && kb_unknown == 0,
              fmt("fits_2mb=%d fits_768k_on_1mb=%d kb={%zu,%zu,%zu,unk=%zu}",
                  static_cast<int>(fits_2mb), static_cast<int>(fits_768k_on_1mb),
                  kb0, kb1, kb2, kb_unknown));
    }

    // BOOT-NEX-03/06 — G156 fix: NexLoader::render_progress_mark() draws
    // a real loading-bar mark (4 bytes, all == `colour`) into physical
    // bank 11 (MMU pages 22/23; the 4 bytes always land in page 23 — see
    // nex_loader.h derivation) for a given bank-slot index `d`. Citation:
    // tbblue/src/asm/nexload/nexload.asm:616-621 `progress`.
    {
        Fixture f;
        f.fresh();
        // Page 23 must start clean so any non-zero byte found came from
        // render_progress_mark(), not fixture noise.
        std::memset(f.ram.page_ptr(23), 0x00, 0x2000);

        // BOOT-NEX-03: "a 1-pixel-row bar advances along VRAM ... per
        // bank loaded" — draw marks for 3 successive bank-slot indices
        // (stimulus colour 0x07/white per the plan row) and confirm each
        // one landed at its own distinct, non-overlapping byte position.
        NexLoader::render_progress_mark(f.mmu, 0, 0x07);
        NexLoader::render_progress_mark(f.mmu, 1, 0x07);
        NexLoader::render_progress_mark(f.mmu, 5, 0x07);

        const uint8_t* p23 = f.ram.page_ptr(23);
        // l = d*2+18 (nexload.asm:620, `24-6`); each mark is 2 bytes wide
        // at offsets 0x1E00+l/0x1F00+l/0x1F00+l+1/0x1E00+l+1.
        auto mark_ok = [&](uint8_t d, uint8_t colour) {
            uint8_t l  = static_cast<uint8_t>(d * 2 + 18);
            uint8_t l1 = static_cast<uint8_t>(l + 1);
            return p23[0x1E00 + l]  == colour && p23[0x1F00 + l]  == colour &&
                   p23[0x1F00 + l1] == colour && p23[0x1E00 + l1] == colour;
        };
        const bool d0_ok = mark_ok(0, 0x07);
        const bool d1_ok = mark_ok(1, 0x07);
        const bool d5_ok = mark_ok(5, 0x07);
        // "Advances": strictly increasing byte offsets, i.e. the three
        // marks do not alias onto the same position.
        const bool positions_distinct = (0 * 2 + 18) != (1 * 2 + 18) &&
                                         (1 * 2 + 18) != (5 * 2 + 18);

        check("BOOT-NEX-03",
              "loading_bar draws a per-bank-slot mark that advances along "
              "VRAM (bank 11 / MMU page 23) — nexload.asm:616-621 `progress`",
              d0_ok && d1_ok && d5_ok && positions_distinct,
              fmt("d0_ok=%d d1_ok=%d d5_ok=%d distinct=%d",
                  static_cast<int>(d0_ok), static_cast<int>(d1_ok),
                  static_cast<int>(d5_ok), static_cast<int>(positions_distinct)));
    }
    {
        // BOOT-NEX-06: loading_bar_colour is honoured verbatim, not a
        // hardcoded default — two DIFFERENT colours at two DIFFERENT
        // bank-slot indices (so they don't alias) must produce two
        // DIFFERENT observed byte values, each matching its own request.
        Fixture f;
        f.fresh();
        std::memset(f.ram.page_ptr(23), 0x00, 0x2000);

        NexLoader::render_progress_mark(f.mmu, 10, 0x02);  // red
        NexLoader::render_progress_mark(f.mmu, 20, 0x07);  // white

        const uint8_t* p23 = f.ram.page_ptr(23);
        auto mark_ok = [&](uint8_t d, uint8_t colour) {
            uint8_t l  = static_cast<uint8_t>(d * 2 + 18);
            uint8_t l1 = static_cast<uint8_t>(l + 1);
            return p23[0x1E00 + l]  == colour && p23[0x1F00 + l]  == colour &&
                   p23[0x1F00 + l1] == colour && p23[0x1E00 + l1] == colour;
        };
        const bool red_ok   = mark_ok(10, 0x02);
        const bool white_ok = mark_ok(20, 0x07);

        check("BOOT-NEX-06",
              "loading_bar_colour byte is written verbatim, not a fixed "
              "default — nexload.asm:617,619-620 `ld a,(LoadCol):ld e,a`",
              red_ok && white_ok,
              fmt("red_ok=%d white_ok=%d", static_cast<int>(red_ok),
                  static_cast<int>(white_ok)));
    }

    // BOOT-NEX-04 — G156 fix: NexLoader::inter_bank_delay_frames() matches
    // nexload.asm's `delay` gate: fires kPostEarlyBankSlots (109) times,
    // once per bank-slot iteration, ONLY when screen_flags != 0. Citation:
    // nexload.asm:541 `ld a,(IsLoadingScr):or a:call nz,delay`, :612-614
    // `delay: ld a,(LoadDel):or a:ret z:call rasterWait`.
    {
        const uint32_t frames_with_screen    = NexLoader::inter_bank_delay_frames(true, 10);
        const uint32_t frames_without_screen = NexLoader::inter_bank_delay_frames(false, 10);
        check("BOOT-NEX-04",
              "loading_delay honoured: 109 post-early bank-slot waits of "
              "loading_delay frames each when a screen is present; zero "
              "frames when no screen is present",
              frames_with_screen == 1090 && frames_without_screen == 0,
              fmt("with_screen=%u (expected 1090) without_screen=%u (expected 0)",
                  frames_with_screen, frames_without_screen));
    }

    // BOOT-NEX-05 — G156 fix: NexLoader::boot_hold_frames() adds the
    // unconditional start_delay (nexload.asm:575-577 `ld a,(StartDel):call
    // rasterWait`) on top of the inter-bank total — and start_delay applies
    // even with NO screen data present (unlike loading_delay above).
    {
        const uint32_t total      = NexLoader::boot_hold_frames(/*start_delay=*/50, /*screen_present=*/true, /*loading_delay=*/10);
        const uint32_t start_only = NexLoader::boot_hold_frames(/*start_delay=*/50, /*screen_present=*/false, /*loading_delay=*/10);
        check("BOOT-NEX-05",
              "start_delay honoured unconditionally before code-entry, on "
              "top of any inter-bank loading_delay total",
              total == 1140 && start_only == 50,
              fmt("total=%u (expected 1140) start_only=%u (expected 50)",
                  total, start_only));
    }

    // BOOT-NEX-07 — G16 fix verification: NexLoader::zero_bank5_screen_pages()
    // must zero the full 16 KB of bank 5 (pages 10+11) so that subsequent
    // ULA / LoRes / HiRes / HiColour ingest paths land in a clean canvas
    // and never expose stale RAM as attribute / pixel leak. apply() calls
    // this helper before the SCREEN_* blocks; here we exercise the helper
    // directly because mmu_test does not link Emulator.
    //
    // Discriminative shape: pre-poke a non-zero junk pattern (0xCC) into
    // both bank-5 pages, plus a ULA-screen-style payload (0xAA) over the
    // first 6912 bytes of page 10 to model the post-load state. The fix
    // (when invoked BEFORE the payload write — i.e. as apply() does it)
    // produces a canvas where the residual is 0x00. We model that here by
    // calling the helper FIRST, then writing the 6912-byte ULA payload,
    // then asserting:
    //   * page 10 [0..0x1AFF]              = 0xAA  (ULA payload survived)
    //   * page 10 [0x1B00..0x1FFF]         = 0x00  (residual zeroed)
    //   * page 11 [0..0x1FFF]              = 0x00  (residual zeroed)
    // Without the fix, those residual ranges would still hold the 0xCC
    // junk. Citation: doc/issues/KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md G16,
    // doc/issues/BEAST-NEX-INVESTIGATION.md § Verdict.
    {
        Fixture f;
        f.fresh();

        // Seed bank 5 (physical SRAM pages 0x0A and 0x0B) with junk to
        // simulate stale pre-load contents.
        std::memset(f.ram.page_ptr(0x0A), 0xCC, 0x2000);
        std::memset(f.ram.page_ptr(0x0B), 0xCC, 0x2000);

        // Apply the G16 fix — same helper apply() invokes before any
        // screen-format ingest path runs.
        NexLoader::zero_bank5_screen_pages(f.mmu);

        // Simulate the SCREEN_ULA payload: 6912 bytes of 0xAA over page 10
        // offset 0. This mirrors apply()'s write_to_ram(mmu, 10, 0, …, 6912)
        // call for SCREEN_ULA. Use direct ram.page_ptr() writes — bank 5
        // SRAM-page 0x0A is the same physical location whether reached via
        // slot mapping or directly (BNK-05 establishes round-trip identity).
        std::memset(f.ram.page_ptr(0x0A), 0xAA, 6912);

        // Verify the three regions.
        const uint8_t* p10 = f.ram.page_ptr(0x0A);
        const uint8_t* p11 = f.ram.page_ptr(0x0B);

        bool ula_payload_ok    = true;
        bool p10_residual_zero = true;
        bool p11_all_zero      = true;
        size_t first_payload_bad   = 0;
        size_t first_p10_resid_bad = 0;
        size_t first_p11_bad       = 0;
        for (size_t i = 0; i < 6912; ++i) {
            if (p10[i] != 0xAA) {
                ula_payload_ok = false;
                first_payload_bad = i;
                break;
            }
        }
        for (size_t i = 6912; i < 0x2000; ++i) {
            if (p10[i] != 0x00) {
                p10_residual_zero = false;
                first_p10_resid_bad = i;
                break;
            }
        }
        for (size_t i = 0; i < 0x2000; ++i) {
            if (p11[i] != 0x00) {
                p11_all_zero = false;
                first_p11_bad = i;
                break;
            }
        }

        check("BOOT-NEX-07",
              "G16 fix: zero_bank5_screen_pages() clears pages 10+11 (16 KB) "
              "before screen-format ingest, eliminating attribute-area leak "
              "from stale pre-load RAM (BEAST-NEX-INVESTIGATION.md §Verdict)",
              ula_payload_ok && p10_residual_zero && p11_all_zero,
              fmt("ula_payload_ok=%d (first_bad@%zu=0x%02X) "
                  "p10_residual_zero=%d (first_bad@%zu=0x%02X) "
                  "p11_all_zero=%d (first_bad@%zu=0x%02X)",
                  static_cast<int>(ula_payload_ok),
                  first_payload_bad, p10[first_payload_bad],
                  static_cast<int>(p10_residual_zero),
                  first_p10_resid_bad,
                  first_p10_resid_bad < 0x2000 ? p10[first_p10_resid_bad] : 0,
                  static_cast<int>(p11_all_zero),
                  first_p11_bad,
                  first_p11_bad < 0x2000 ? p11[first_p11_bad] : 0));
    }
}

// ── Cat 22-26: Tape SAVE / .z80 loader / Snapshot save / DeciLoad / FDC ──
void test_boot_format_loaders() {
    set_group("BOOT-FORMATS");

    // Cat 22 — Tape SAVE (G33 Phase 1, Task 57). TapSaver
    // (src/core/tap_saver.h) builds standard TAP blocks; its block/file
    // APIs are header-inline (z80_loader.h precedent) so this suite can
    // exercise them without linking jnext_core. Expected bytes below are
    // hand-computed from the TAP container spec (2-byte LE length =
    // payload+2, flag, payload, XOR checksum over flag+payload — the
    // byte stream the 48K ROM SA-BYTES routine at 0x04C2 emits, framed),
    // NEVER from TapSaver's own output.

    // BOOT-TAPESAVE-01 — header block byte layout. A BASIC "Program"
    // header for name "X": type=0x00, 10-char name 'X'+9 spaces,
    // length=0x0102, autostart line=0x000A, vars offset=0x0102.
    // Hand-computed checksum: 0x00 ^00 ^58 ^(20 x9) ^02 ^01 ^0A ^00 ^02
    // ^01 = 0x72. Length field = 17+2 = 19 = 0x0013 LE.
    {
        static const uint8_t header_payload[17] = {
            0x00,                                                         // type: Program
            0x58, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,  // "X         "
            0x02, 0x01,                                                   // length 0x0102
            0x0A, 0x00,                                                   // autostart line 10
            0x02, 0x01,                                                   // vars offset 0x0102
        };
        static const uint8_t expected[21] = {
            0x13, 0x00,                                                   // LE length 19
            0x00,                                                         // flag: header
            0x00, 0x58, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
            0x20, 0x02, 0x01, 0x0A, 0x00, 0x02, 0x01,                    // payload
            0x72,                                                         // XOR checksum
        };
        std::vector<uint8_t> block =
            TapSaver::build_block(0x00, header_payload, sizeof(header_payload));
        bool size_ok  = block.size() == sizeof(expected);
        bool bytes_ok = size_ok &&
                        std::memcmp(block.data(), expected, sizeof(expected)) == 0;
        size_t first_bad = 0;
        if (size_ok && !bytes_ok)
            while (first_bad < sizeof(expected) && block[first_bad] == expected[first_bad])
                ++first_bad;
        check("BOOT-TAPESAVE-01",
              "TapSaver::build_block header block: LE length prefix (payload+2), "
              "flag 0x00, payload verbatim, XOR checksum — hand-computed TAP "
              "image (G33 Phase 1)",
              bytes_ok,
              fmt("size=%zu (want %zu) first_bad_off=%zu got=0x%02X want=0x%02X",
                  block.size(), sizeof(expected), first_bad,
                  (size_ok && first_bad < block.size()) ? block[first_bad] : 0,
                  first_bad < sizeof(expected) ? expected[first_bad] : 0));
    }

    // BOOT-TAPESAVE-02 — data block checksum over a non-trivial payload,
    // plus multi-block append ordering through the real file API.
    // Hand-computed checksum: 0xFF ^DE=0x21 ^AD=0x8C ^BE=0x32 ^EF=0xDD
    // ^01 = 0xDC. Then set_output(mkstemp) + append header then data;
    // raw file bytes must be the two hand-computed images concatenated
    // in append order.
    {
        static const uint8_t data_payload[5] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };
        static const uint8_t expected_data_block[9] = {
            0x07, 0x00,                    // LE length 7 = 5+2
            0xFF,                          // flag: data
            0xDE, 0xAD, 0xBE, 0xEF, 0x01,  // payload
            0xDC,                          // XOR checksum
        };
        static const uint8_t header_payload[17] = {
            0x00,
            0x58, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
            0x02, 0x01, 0x0A, 0x00, 0x02, 0x01,
        };
        static const uint8_t expected_header_block[21] = {
            0x13, 0x00, 0x00,
            0x00, 0x58, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
            0x20, 0x02, 0x01, 0x0A, 0x00, 0x02, 0x01, 0x72,
        };

        std::vector<uint8_t> block =
            TapSaver::build_block(0xFF, data_payload, sizeof(data_payload));
        bool data_block_ok = block.size() == sizeof(expected_data_block) &&
                             std::memcmp(block.data(), expected_data_block,
                                         sizeof(expected_data_block)) == 0;

        // Multi-block append ordering via the file API.
        char tmp_path[] = "/tmp/jnext-tapesave-XXXXXX";
        int fd = mkstemp(tmp_path);
        bool append_ok = false, file_ok = false;
        std::vector<uint8_t> file_bytes;
        if (fd >= 0) {
            close(fd);
            TapSaver saver;
            append_ok = saver.set_output(tmp_path) &&
                        saver.append_block(0x00, header_payload, sizeof(header_payload)) &&
                        saver.append_block(0xFF, data_payload, sizeof(data_payload)) &&
                        saver.blocks_written() == 2;
            std::ifstream f(tmp_path, std::ios::binary | std::ios::ate);
            if (f) {
                file_bytes.resize(static_cast<size_t>(f.tellg()));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(file_bytes.data()),
                       static_cast<std::streamsize>(file_bytes.size()));
            }
            file_ok = file_bytes.size() ==
                          sizeof(expected_header_block) + sizeof(expected_data_block) &&
                      std::memcmp(file_bytes.data(), expected_header_block,
                                  sizeof(expected_header_block)) == 0 &&
                      std::memcmp(file_bytes.data() + sizeof(expected_header_block),
                                  expected_data_block, sizeof(expected_data_block)) == 0;
            unlink(tmp_path);
        }
        check("BOOT-TAPESAVE-02",
              "TapSaver data block (non-trivial XOR checksum) + append_block "
              "file ordering: file bytes == header-block || data-block, "
              "hand-computed images (G33 Phase 1)",
              data_block_ok && append_ok && file_ok,
              fmt("data_block_ok=%d append_ok=%d file_ok=%d file_size=%zu (want %zu)",
                  static_cast<int>(data_block_ok), static_cast<int>(append_ok),
                  static_cast<int>(file_ok), file_bytes.size(),
                  sizeof(expected_header_block) + sizeof(expected_data_block)));
    }

    // BOOT-TAPESAVE-03 — saver→loader round-trip. TapSaver output
    // (two build_block images concatenated — exactly what append_block
    // writes, per BOOT-TAPESAVE-02) parsed back by the REAL loader parse
    // path: TapLoader::parse_blocks is the exact loop TapLoader::load()
    // runs on file contents (header-inline for this suite; z80_loader.h
    // precedent). Block boundaries, flags, payload identity and loader-
    // side checksum verification must all hold, with zero parse warnings.
    {
        static const uint8_t header_payload[17] = {
            0x00,
            0x58, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
            0x02, 0x01, 0x0A, 0x00, 0x02, 0x01,
        };
        static const uint8_t data_payload[5] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };

        std::vector<uint8_t> image =
            TapSaver::build_block(0x00, header_payload, sizeof(header_payload));
        std::vector<uint8_t> data_block =
            TapSaver::build_block(0xFF, data_payload, sizeof(data_payload));
        image.insert(image.end(), data_block.begin(), data_block.end());

        std::vector<TapBlock> blocks;
        int warnings = 0;
        TapLoader::parse_blocks(image, blocks,
                                [&warnings](const std::string&) { ++warnings; });

        bool count_ok = blocks.size() == 2;
        bool b0_ok = count_ok && blocks[0].flag == 0x00 &&
                     blocks[0].data.size() == sizeof(header_payload) &&
                     std::memcmp(blocks[0].data.data(), header_payload,
                                 sizeof(header_payload)) == 0 &&
                     blocks[0].verify_checksum();
        bool b1_ok = count_ok && blocks[1].flag == 0xFF &&
                     blocks[1].data.size() == sizeof(data_payload) &&
                     std::memcmp(blocks[1].data.data(), data_payload,
                                 sizeof(data_payload)) == 0 &&
                     blocks[1].verify_checksum();
        check("BOOT-TAPESAVE-03",
              "TapSaver → TapLoader::parse_blocks round-trip: 2 blocks, "
              "correct boundaries/flags, payload identity, loader checksum "
              "verification, zero parse warnings (G33 Phase 1)",
              count_ok && b0_ok && b1_ok && warnings == 0,
              fmt("blocks=%zu b0_ok=%d b1_ok=%d warnings=%d",
                  blocks.size(), static_cast<int>(b0_ok),
                  static_cast<int>(b1_ok), warnings));
    }

    // Cat 23 — .z80 loader (G34). Z80Loader (src/core/z80_loader.h/.cpp)
    // landed Task 13b. mmu_test does not link jnext_core (no Emulator), so
    // — same technique as BOOT-NEX-07 / NexLoader — these exercise
    // Z80Loader::load_from_buffer() + apply_ram_to_mmu() directly against a
    // Fixture's bare Mmu, with fixtures built as byte arrays in-test (no
    // binaries checked in). Field-offset citations: worldofspectrum.org/
    // faq/reference/z80format.htm (the canonical `.z80` v1/v2/v3 spec every
    // implementation — FUSE/CSpect/ZEsarUX — traces back to).

    // BOOT-Z80-01 — v1 (uncompressed) .z80 round-trip. 30-byte header with
    // PC != 0 (offset 6-7) is the v1 sentinel; body is a raw 49152-byte 48K
    // dump laid out 0x4000-0x7FFF / 0x8000-0xBFFF / 0xC000-0xFFFF. Verifies
    // both the header field parse and that Mmu reads at 0x4000-0xFFFF (via
    // the post-reset() default 48K slot mapping — mmu.cpp RESET_PAGES —
    // bank5@slots2/3, bank2@slots4/5, bank0@slots6/7) match the raw image.
    {
        std::vector<uint8_t> buf(30, 0);
        buf[0] = 0x11; buf[1] = 0x22;                    // A, F
        buf[2] = 0x44; buf[3] = 0x33;                    // BC = 0x3344 (LE)
        buf[4] = 0x66; buf[5] = 0x55;                    // HL = 0x5566
        buf[6] = 0x00; buf[7] = 0x80;                    // PC = 0x8000 (v1 sentinel: != 0)
        buf[8] = 0xFE; buf[9] = 0xFF;                    // SP = 0xFFFE
        buf[10] = 0x3F;                                  // I
        buf[11] = 0x2A;                                  // R low 7 bits
        buf[12] = 0x07;                                  // flags1: R hi=1, border=3, uncompressed
        buf[13] = 0x88; buf[14] = 0x77;                  // DE = 0x7788
        buf[15] = 0xAA; buf[16] = 0x99;                  // BC' = 0x99AA
        buf[17] = 0xCC; buf[18] = 0xBB;                  // DE' = 0xBBCC
        buf[19] = 0xEE; buf[20] = 0xDD;                  // HL' = 0xDDEE
        buf[21] = 0x01; buf[22] = 0x02;                  // A', F'
        buf[23] = 0x34; buf[24] = 0x12;                  // IY = 0x1234
        buf[25] = 0x78; buf[26] = 0x56;                  // IX = 0x5678
        buf[27] = 0x01;                                  // IFF1 = EI
        buf[28] = 0x01;                                  // IFF2 = EI
        buf[29] = 0x01;                                  // flags2: IM = 1

        buf.resize(30 + 49152, 0);
        std::fill(buf.begin() + 30,           buf.begin() + 30 + 16384, 0xA5);
        std::fill(buf.begin() + 30 + 16384,   buf.begin() + 30 + 32768, 0xB6);
        std::fill(buf.begin() + 30 + 32768,   buf.begin() + 30 + 49152, 0xC7);

        Fixture f;
        f.fresh();
        Z80Loader loader;
        bool load_ok  = loader.load_from_buffer(buf);
        bool apply_ok = load_ok && loader.apply_ram_to_mmu(f.mmu);
        const auto& h = loader.header();

        bool header_ok = h.version == 1 && h.PC == 0x8000 && h.SP == 0xFFFE &&
                          h.A == 0x11 && h.F == 0x22 && h.BC == 0x3344 && h.HL == 0x5566 &&
                          h.DE == 0x7788 && h.IX == 0x5678 && h.IY == 0x1234 &&
                          h.IM == 1 && h.IFF1 && h.IFF2 && h.border == 3;

        bool mem_ok = f.mmu.read(0x4000) == 0xA5 && f.mmu.read(0x7FFF) == 0xA5 &&
                      f.mmu.read(0x8000) == 0xB6 && f.mmu.read(0xBFFF) == 0xB6 &&
                      f.mmu.read(0xC000) == 0xC7 && f.mmu.read(0xFFFF) == 0xC7;

        check("BOOT-Z80-01",
              "v1 (uncompressed) .z80 round-trip — Mmu reads at 0x4000-0xFFFF match "
              "the raw-RAM image; PC/AF/BC/etc. match header bytes "
              "(worldofspectrum.org/faq/reference/z80format.htm v1 layout)",
              load_ok && apply_ok && header_ok && mem_ok,
              fmt("load_ok=%d apply_ok=%d header_ok=%d mem_ok=%d version=%d",
                  static_cast<int>(load_ok), static_cast<int>(apply_ok),
                  static_cast<int>(header_ok), static_cast<int>(mem_ok), h.version));
    }

    // BOOT-Z80-02 — v2 (RLE-compressed) .z80 round-trip. PC==0 at offset
    // 6-7 signals v2/v3; additional-header length 23 (offset 30-31) marks
    // v2; hardware_mode=0 (offset 34) selects the 48K page table (page 4 ->
    // 0x8000-0xBFFF, page 5 -> 0xC000-0xFFFF, page 8 -> 0x4000-0x7FFF).
    // Each page is RLE-encoded as 16000 bytes of a fill value (multiple
    // `ED ED <count> <byte>` runs, since count is a single byte, max 255)
    // followed by 384 literal bytes — exercising both the run decoder and
    // the literal-byte path of decompress_page() in the same block.
    {
        auto build_compressed_page = [](uint8_t fill) {
            std::vector<uint8_t> page;
            append_rle_run(page, fill, 16000);
            for (int i = 0; i < 384; ++i)
                page.push_back(static_cast<uint8_t>(0x30 + (i & 0x0F)));
            return page;
        };
        auto append_page_block = [](std::vector<uint8_t>& out, uint8_t page_num,
                                     const std::vector<uint8_t>& compressed) {
            uint16_t len = static_cast<uint16_t>(compressed.size());
            out.push_back(static_cast<uint8_t>(len & 0xFF));
            out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            out.push_back(page_num);
            out.insert(out.end(), compressed.begin(), compressed.end());
        };

        std::vector<uint8_t> buf(30, 0);        // buf[6..7] left 0 -> v2/v3 sentinel
        uint16_t add_len = 23;
        buf.push_back(static_cast<uint8_t>(add_len & 0xFF));
        buf.push_back(static_cast<uint8_t>((add_len >> 8) & 0xFF));  // offset 30-31
        uint16_t real_pc = 0x9000;
        buf.push_back(static_cast<uint8_t>(real_pc & 0xFF));
        buf.push_back(static_cast<uint8_t>((real_pc >> 8) & 0xFF));  // offset 32-33
        buf.push_back(0x00);                     // offset 34: hardware_mode = 0 (48K)
        buf.push_back(0x00);                     // offset 35: port_7ffd (unused, 48K)
        for (int i = 0; i < 19; ++i) buf.push_back(0);  // pad to 23-byte additional header
        // buf.size() == 32 + 23 == 55 here (memory pages start at offset 55).

        append_page_block(buf, 4, build_compressed_page(0xA5));
        append_page_block(buf, 5, build_compressed_page(0xB6));
        append_page_block(buf, 8, build_compressed_page(0xC7));

        Fixture f;
        f.fresh();
        Z80Loader loader;
        bool load_ok  = loader.load_from_buffer(buf);
        bool apply_ok = load_ok && loader.apply_ram_to_mmu(f.mmu);
        const auto& h = loader.header();

        bool header_ok = h.version == 2 && !h.is_128k && h.hardware_mode == 0 &&
                          h.PC == 0x9000;

        auto verify_region = [&](uint16_t base, uint8_t fill) {
            for (int i = 0; i < 16000; ++i)
                if (f.mmu.read(static_cast<uint16_t>(base + i)) != fill) return false;
            for (int i = 0; i < 384; ++i)
                if (f.mmu.read(static_cast<uint16_t>(base + 16000 + i)) !=
                    static_cast<uint8_t>(0x30 + (i & 0x0F))) return false;
            return true;
        };
        bool mem_ok = verify_region(0x8000, 0xA5) &&   // z80 page 4 -> bank 2
                      verify_region(0xC000, 0xB6) &&   // z80 page 5 -> bank 0
                      verify_region(0x4000, 0xC7);      // z80 page 8 -> bank 5

        check("BOOT-Z80-02",
              "v2 (RLE-compressed) .z80 round-trip — decompress_page() reproduces "
              "the fill-run + literal-tail pattern across all three 48K page-number "
              "-> bank mappings (page4/5/8 -> 0x8000/0xC000/0x4000)",
              load_ok && apply_ok && header_ok && mem_ok,
              fmt("load_ok=%d apply_ok=%d header_ok=%d mem_ok=%d version=%d is_128k=%d",
                  static_cast<int>(load_ok), static_cast<int>(apply_ok),
                  static_cast<int>(header_ok), static_cast<int>(mem_ok),
                  h.version, static_cast<int>(h.is_128k)));
    }

    // BOOT-Z80-03 — v3 (extended-header, 128K) .z80. Additional-header
    // length 54 marks v3; hardware_mode=4 (offset 34) is unambiguously
    // 128K-class in both v2 and v3 (only value 3 is disputed across
    // sources — see Z80Loader header comment; not exercised here).
    // port_7ffd at offset 35 (0x23) must land in header().port_7ffd. All
    // 8 z80 page numbers (3..10) map to RAM banks 0..7 (page-3), each
    // tagged with its own page number so bank routing is unambiguous.
    {
        std::vector<uint8_t> buf(30, 0);        // buf[6..7] left 0 -> v2/v3 sentinel
        uint16_t add_len = 54;
        buf.push_back(static_cast<uint8_t>(add_len & 0xFF));
        buf.push_back(static_cast<uint8_t>((add_len >> 8) & 0xFF));  // offset 30-31
        uint16_t real_pc = 0xC000;
        buf.push_back(static_cast<uint8_t>(real_pc & 0xFF));
        buf.push_back(static_cast<uint8_t>((real_pc >> 8) & 0xFF));  // offset 32-33
        buf.push_back(0x04);                     // offset 34: hardware_mode = 4 (128K)
        buf.push_back(0x10);                     // offset 35 (0x23): port_7ffd = 0x10
        for (int i = 0; i < 50; ++i) buf.push_back(0);  // pad to 54-byte additional header
        // buf.size() == 32 + 54 == 86 here.

        for (uint8_t page_num = 3; page_num <= 10; ++page_num) {
            uint16_t len = 0xFFFF;               // sentinel: 16384 bytes follow uncompressed
            buf.push_back(static_cast<uint8_t>(len & 0xFF));
            buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            buf.push_back(page_num);
            buf.insert(buf.end(), 16384, page_num);  // whole page tagged with its own number
        }

        Fixture f;
        f.fresh();
        Z80Loader loader;
        bool load_ok  = loader.load_from_buffer(buf);
        bool apply_ok = load_ok && loader.apply_ram_to_mmu(f.mmu);
        const auto& h = loader.header();

        bool header_ok = h.version == 3 && h.is_128k && h.hardware_mode == 4 &&
                          h.port_7ffd == 0x10 && h.PC == 0xC000;

        // Verify each of the 8 banks via Mmu (not ram.page_ptr() directly —
        // bank 5 / physical pages 10-11 route through a dedicated
        // bank5_vram_ buffer in Next mode per mmu.h:1135, so only a Mmu
        // round-trip is guaranteed consistent for every bank).
        auto verify_page_uniform = [&](uint8_t page, uint8_t expect) {
            constexpr int TEMP_SLOT = 1;
            uint8_t saved = f.mmu.get_page(TEMP_SLOT);
            f.mmu.set_page(TEMP_SLOT, page);
            bool ok = true;
            for (int off = 0; off < 0x2000; ++off) {
                if (f.mmu.read(static_cast<uint16_t>(0x2000 + off)) != expect) { ok = false; break; }
            }
            f.mmu.set_page(TEMP_SLOT, saved);
            return ok;
        };
        bool banks_ok = true;
        for (int bank = 0; bank < 8 && banks_ok; ++bank) {
            uint8_t expect = static_cast<uint8_t>(bank + 3);
            banks_ok = verify_page_uniform(static_cast<uint8_t>(bank * 2), expect) &&
                       verify_page_uniform(static_cast<uint8_t>(bank * 2 + 1), expect);
        }

        check("BOOT-Z80-03",
              "v3 (extended-header, 128K) .z80 — Ram banks 0-7 populated per the "
              "page3..10 -> bank0..7 table; port_7ffd_ from header byte 0x23",
              load_ok && apply_ok && header_ok && banks_ok,
              fmt("load_ok=%d apply_ok=%d header_ok=%d banks_ok=%d version=%d "
                  "is_128k=%d port_7ffd=%#04x",
                  static_cast<int>(load_ok), static_cast<int>(apply_ok),
                  static_cast<int>(header_ok), static_cast<int>(banks_ok),
                  h.version, static_cast<int>(h.is_128k), h.port_7ffd));
    }

    // BOOT-Z80-04 — Unsupported / corrupt .z80 file rejected. A v1 header
    // (PC != 0) claims compression (flags1 bit 5) but the body is a
    // truncated RLE run marker (`ED ED 05`, missing its value byte and the
    // 00 ED ED 00 end marker) — decompress_v1() must detect the truncation
    // and load_from_buffer() must return false. Also proves the emulator
    // stays in pre-load state: a canary byte written before the failed
    // load survives apply_ram_to_mmu() (which itself must refuse to write
    // anything, since load() never succeeded).
    {
        std::vector<uint8_t> buf(30, 0);
        buf[6] = 0x00; buf[7] = 0x80;   // PC = 0x8000 (v1 sentinel: != 0)
        buf[12] = 0x20;                 // flags1: compressed bit set
        buf.push_back(0xED);
        buf.push_back(0xED);
        buf.push_back(0x05);            // truncated: run marker with no value byte,
                                         // and no 00 ED ED 00 end marker anywhere

        Fixture f;
        f.fresh();
        f.mmu.write(0x4000, 0xEE);      // canary: must survive the failed load

        Z80Loader loader;
        bool load_ok  = loader.load_from_buffer(buf);
        bool apply_ok = loader.apply_ram_to_mmu(f.mmu);
        bool canary_intact = (f.mmu.read(0x4000) == 0xEE);

        check("BOOT-Z80-04",
              "Unsupported / corrupt .z80 file rejected — truncated RLE run with no "
              "end marker; loader returns error and Mmu is left untouched",
              !load_ok && !loader.is_loaded() && !apply_ok && canary_intact,
              fmt("load_ok=%d is_loaded=%d apply_ok=%d canary_intact=%d",
                  static_cast<int>(load_ok), static_cast<int>(loader.is_loaded()),
                  static_cast<int>(apply_ok), static_cast<int>(canary_intact)));
    }

    // BOOT-Z80-05 — structurally-valid .z80 whose pages are all foreign
    // page numbers must be rejected by apply_ram_to_mmu(), not silently
    // report success with zero RAM written. A v3/128K header (add_len=54,
    // hardware_mode=4) passes header parsing and parse_pages()'s
    // `!pages_.empty()` check (one page present), but that page carries
    // page_num=255 — outside the valid 3..10 (128K) / {4,5,8} (48K) sets —
    // so apply_ram_to_mmu()'s bank-routing loop applies zero pages.
    // load_from_buffer() must still SUCCEED here (parsing is structurally
    // fine — the corruption is only in which page numbers were used), but
    // apply_ram_to_mmu() must FAIL and leave Mmu untouched (independent
    // review finding: without the `applied > 0` guard this silently
    // "succeeds" with no RAM written at all — exit 0, nothing logged).
    {
        std::vector<uint8_t> buf(30, 0);        // buf[6..7] left 0 -> v2/v3 sentinel
        uint16_t add_len = 54;
        buf.push_back(static_cast<uint8_t>(add_len & 0xFF));
        buf.push_back(static_cast<uint8_t>((add_len >> 8) & 0xFF));  // offset 30-31
        uint16_t real_pc = 0x8000;
        buf.push_back(static_cast<uint8_t>(real_pc & 0xFF));
        buf.push_back(static_cast<uint8_t>((real_pc >> 8) & 0xFF));  // offset 32-33
        buf.push_back(0x04);                     // offset 34: hardware_mode = 4 (128K)
        buf.push_back(0x00);                     // offset 35: port_7ffd
        for (int i = 0; i < 50; ++i) buf.push_back(0);  // pad to 54-byte additional header
        // buf.size() == 32 + 54 == 86 here.

        uint16_t len = 0xFFFF;                   // sentinel: 16384 bytes follow uncompressed
        buf.push_back(static_cast<uint8_t>(len & 0xFF));
        buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(255)); // foreign page number: outside 3..10
        buf.insert(buf.end(), 16384, 0xAA);

        Fixture f;
        f.fresh();
        f.mmu.write(0x4000, 0xEE);      // canary: must survive the rejected apply

        Z80Loader loader;
        bool load_ok  = loader.load_from_buffer(buf);
        bool apply_ok = load_ok && loader.apply_ram_to_mmu(f.mmu);
        bool canary_intact = (f.mmu.read(0x4000) == 0xEE);

        check("BOOT-Z80-05",
              "Structurally-valid .z80 with only foreign page numbers is rejected by "
              "apply_ram_to_mmu() (zero pages applied), not silently reported as a "
              "successful load with no RAM written",
              load_ok && !apply_ok && canary_intact,
              fmt("load_ok=%d apply_ok=%d canary_intact=%d",
                  static_cast<int>(load_ok), static_cast<int>(apply_ok),
                  static_cast<int>(canary_intact)));
    }

    // Cat 24 — Snapshot save (G35).
    //
    // CLOSED 2026-05-04 BOOT-SNAPSAVE-01: sna_saver IS now reachable from
    // the GUI. SnaSaver::save() lives at src/core/sna_saver.cpp:48 and is
    // invoked by MainWindow::on_save_snapshot() at src/gui/main_window.cpp
    // (File > Save Snapshot..., Ctrl+Shift+S). Direct verification of the
    // 49179-byte 48K SNA layout would require constructing a full
    // Emulator (jnext_core link), which this Mmu-tier suite intentionally
    // avoids. The byte-count contract is pinned by the SNA_48K_SIZE
    // constant in sna_saver.cpp:50 and exercised by emulator.cpp:3882
    // (existing RZX recording path). The GUI consumer's existence is
    // pinned by the MainWindow declaration + linker (the build itself
    // proves the symbol).
    //
    // CLOSED 2026-05-04 BOOT-SNAPSAVE-04: Save-As dialog is wired —
    // QFileDialog::getSaveFileName() with `.sna` filter, auto-extension,
    // QFile::write() of the SnaSaver buffer. See on_save_snapshot() in
    // src/gui/main_window.cpp.
    // CLOSED (Task 13b, reopened 2026-07-13) BOOT-SNAPSAVE-02/03: SzxSaver /
    // NexSaver exist (src/core/szx_saver.h, src/core/nex_saver.h) and are
    // wired into MainWindow::on_save_snapshot() (extension-dispatched, same
    // entry point SnaSaver already used). Both savers' Emulator-free
    // build() entry point is exercised here directly against a bare
    // Mmu/Ram fixture — this Mmu-tier suite cannot link jnext_core, so
    // it cannot call the real SzxLoader::load()/NexLoader::load() (both
    // non-inline, defined in szx_loader.cpp/nex_loader.cpp, part of
    // jnext_core). The checks below instead verify every byte the real
    // loaders are known to read, at the exact offsets hand-verified
    // against both loader .cpp files, plus the full RAM payload content
    // byte for byte. A full-pipeline round trip (build → file →
    // Emulator::load_szx()/load_nex() → verify live Emulator state)
    // additionally lives in mmu_integration_test.cpp, which does link
    // jnext_core.
    //
    // REDESIGN 2026-07-13: .szx is scoped to 48K/128K/+2A/+3 only (see
    // SzxSaver class doc-comment SCOPE) — the RAM page SET, not a bank
    // COUNT, is now what varies per machine, so BOOT-SNAPSAVE-02/02B/02C
    // below were rewritten from the old 64-bank-ceiling-clamp contract to
    // this per-machine-page-set + refusal contract.
    {
        Fixture f;
        f.fresh();

        // Distinctive content in every physical bank the saver might
        // touch (0-7); the +3 save below is expected to include all of
        // them, so pattern all 8.
        for (unsigned page = 0; page < 16; ++page) {
            uint8_t* p = f.ram.page_ptr(static_cast<uint16_t>(page));
            for (int i = 0; i < 8192; ++i)
                p[i] = static_cast<uint8_t>(page * 3 + i);
        }

        Z80Registers regs{};
        regs.AF = 0x1234; regs.BC = 0x5678; regs.DE = 0x9ABC; regs.HL = 0xDEF0;
        regs.AF2 = 0x1111; regs.BC2 = 0x2222; regs.DE2 = 0x3333; regs.HL2 = 0x4444;
        regs.IX = 0x5555; regs.IY = 0x6666; regs.SP = 0x8000; regs.PC = 0x9000;
        regs.MEMPTR = 0xABCD;
        regs.I = 0x3F; regs.R = 0x7E;
        regs.IFF1 = 1; regs.IFF2 = 0; regs.IM = 2;
        regs.halted = true;
        const uint32_t tstates = 0x1357FBDA;
        const uint8_t machine_id = 5;  // ZXSTMID_PLUS3
        const std::vector<uint8_t> expect_banks = {0, 1, 2, 3, 4, 5, 6, 7};

        SzxSaver::SpecRegs spec;
        spec.border    = 3;
        spec.port_7ffd = 0x17;
        spec.port_1ffd = 0x04;
        spec.port_fe   = 0x0B;  // border=3 | MIC(0x08)

        std::vector<uint8_t> bytes = SzxSaver::build(regs, tstates, machine_id, spec, f.mmu);

        const size_t z80r_payload  = 16;
        const size_t spcr_payload  = 61;
        const size_t ramp0         = 69;
        const size_t ramp_stride   = 8 + 3 + 16384;
        const size_t expected_size = ramp0 + expect_banks.size() * ramp_stride;

        bool header_ok = bytes.size() >= 8
            && bytes[0]=='Z' && bytes[1]=='X' && bytes[2]=='S' && bytes[3]=='T'
            && bytes[4]==1 && bytes[5]==4 && bytes[6]==machine_id && bytes[7]==0;

        bool z80r_chunk_ok = bytes.size() > z80r_payload
            && bytes[8]=='Z' && bytes[9]=='8' && bytes[10]=='0' && bytes[11]=='R'
            && ru32(bytes, 12) == 37;

        bool z80r_payload_ok = z80r_chunk_ok
            && ru16(bytes, z80r_payload+0)==regs.AF   && ru16(bytes, z80r_payload+2)==regs.BC
            && ru16(bytes, z80r_payload+4)==regs.DE   && ru16(bytes, z80r_payload+6)==regs.HL
            && ru16(bytes, z80r_payload+8)==regs.AF2  && ru16(bytes, z80r_payload+10)==regs.BC2
            && ru16(bytes, z80r_payload+12)==regs.DE2 && ru16(bytes, z80r_payload+14)==regs.HL2
            && ru16(bytes, z80r_payload+16)==regs.IX  && ru16(bytes, z80r_payload+18)==regs.IY
            && ru16(bytes, z80r_payload+20)==regs.SP  && ru16(bytes, z80r_payload+22)==regs.PC
            && bytes[z80r_payload+24]==regs.I && bytes[z80r_payload+25]==regs.R
            && bytes[z80r_payload+26]==1 && bytes[z80r_payload+27]==0 && bytes[z80r_payload+28]==regs.IM
            && ru32(bytes, z80r_payload+29)==tstates
            && bytes[z80r_payload+34]==0x02   // chFlags bit1 = ZXSTZF_HALTED (regs.halted==true)
            && ru16(bytes, z80r_payload+35)==regs.MEMPTR;

        bool spcr_chunk_ok = bytes.size() > spcr_payload
            && bytes[53]=='S' && bytes[54]=='P' && bytes[55]=='C' && bytes[56]=='R'
            && ru32(bytes, 57) == 8
            && bytes[spcr_payload+0]==spec.border && bytes[spcr_payload+1]==spec.port_7ffd
            && bytes[spcr_payload+2]==spec.port_1ffd && bytes[spcr_payload+3]==spec.port_fe;

        bool ramp_ok = bytes.size() == expected_size;
        size_t first_bad_idx = expect_banks.size(), first_bad_off = 0;
        uint8_t got = 0, want = 0;
        if (ramp_ok) {
            for (size_t idx = 0; idx < expect_banks.size(); ++idx) {
                uint8_t bank = expect_banks[idx];
                size_t p = ramp0 + idx * ramp_stride;
                uint8_t chpageno = bytes[p + 10];
                bool chunk_hdr_ok = bytes[p]=='R' && bytes[p+1]=='A' && bytes[p+2]=='M' && bytes[p+3]=='P'
                                  && ru32(bytes, p+4)==16387
                                  && ru16(bytes, p+8)==0            // wFlags: uncompressed
                                  && chpageno==bank;                // chPageNo == physical bank number
                if (!chunk_hdr_ok) { ramp_ok = false; first_bad_idx = idx; break; }
                bool bank_ok = true;
                for (size_t i = 0; i < 16384; ++i) {
                    uint16_t page = static_cast<uint16_t>(bank * 2 + (i >= 8192 ? 1 : 0));
                    uint8_t expect = static_cast<uint8_t>(page * 3 + (i % 8192));
                    uint8_t actual = bytes[p + 11 + i];
                    if (actual != expect) {
                        ramp_ok = false; bank_ok = false; first_bad_idx = idx; first_bad_off = i;
                        got = actual; want = expect;
                        break;
                    }
                }
                if (!bank_ok) break;
            }
        }

        bool all_ok = header_ok && z80r_payload_ok && spcr_chunk_ok && ramp_ok;

        check("BOOT-SNAPSAVE-02",
              "SzxSaver::build() produces a spec-conformant .szx for +3 "
              "(machine_id=5): 8-byte header, ZXSTZ80REGS(37B)/"
              "ZXSTSPECREGS(8B)/ZXSTRAMPAGE chunks at their exact published "
              "offsets, all 8 physical RAM banks (0-7) with chPageNo == "
              "physical bank number and full content — "
              "spectaculator.com/docs/zx-state/"
              "{header,z80regs,specregs,rampage}.shtml + libspectrum "
              "szx.c:3337-3342 (128-memory-capability page set) (G35)",
              all_ok,
              fmt("header=%d z80r_chunk=%d z80r_payload=%d spcr=%d ramp=%d "
                  "size=%zu expected=%zu "
                  "first_bad_idx=%zu off=%zu got=0x%02X want=0x%02X",
                  static_cast<int>(header_ok), static_cast<int>(z80r_chunk_ok),
                  static_cast<int>(z80r_payload_ok), static_cast<int>(spcr_chunk_ok),
                  static_cast<int>(ramp_ok),
                  bytes.size(), expected_size,
                  first_bad_idx, first_bad_off, got, want));
    }

    // BOOT-SNAPSAVE-02B — discriminative pair for -02: halted=false and
    // IFF2=1 (inverse of the -02 fixture) must each independently flip
    // their single bit in chFlags/Z80R, proving the ternaries in
    // SzxSaver::build() aren't hard-coded to one polarity.
    {
        Fixture f;
        f.fresh();
        Z80Registers regs{};
        regs.IFF1 = 0; regs.IFF2 = 1; regs.halted = false;
        SzxSaver::SpecRegs spec{};
        std::vector<uint8_t> bytes = SzxSaver::build(regs, 0, 1, spec, f.mmu);
        bool ok = bytes.size() >= 53
            && bytes[16 + 26] == 0     // IFF1 = 0
            && bytes[16 + 27] == 1     // IFF2 = 1
            && bytes[16 + 34] == 0x00; // chFlags: not halted
        check("BOOT-SNAPSAVE-02B",
              "SzxSaver::build() IFF1/IFF2/halted encode independently "
              "(discriminative pair for BOOT-SNAPSAVE-02) — "
              "spectaculator.com/docs/zx-state/z80regs.shtml",
              ok,
              fmt("IFF1=0x%02X IFF2=0x%02X chFlags=0x%02X",
                  bytes.size() >= 53 ? bytes[16+26] : 0xFF,
                  bytes.size() >= 53 ? bytes[16+27] : 0xFF,
                  bytes.size() >= 53 ? bytes[16+34] : 0xFF));
    }

    // BOOT-SNAPSAVE-02C — 48K page SET, not a bank COUNT. A real 48K
    // machine's RAM is physical banks {5, 2, 0} (0x4000/0x8000/0xC000 —
    // see SzxSaver class doc-comment PAGE NUMBERING, libspectrum
    // szx.c:3330-3334), NOT "the first 3 banks {0,1,2}". This is the
    // exact contract a naive "clamp to N" implementation gets wrong, and
    // the discriminative case for the whole per-machine-page-set
    // redesign: asserts on the emitted BYTES (RAMP chunk count and every
    // chPageNo present), not on round-tripping through jnext's own
    // loader (SzxLoader::parse_ramp() has no page-set validation either,
    // so a loader-only round trip would not catch a wrong page set).
    {
        Fixture f;
        f.fresh();
        Z80Registers regs{};
        SzxSaver::SpecRegs spec{};
        std::vector<uint8_t> bytes = SzxSaver::build(regs, 0, 1, spec, f.mmu);  // machine_id=1 (48K)

        const size_t ramp0       = 69;
        const size_t ramp_stride = 8 + 3 + 16384;
        const size_t expected    = ramp0 + static_cast<size_t>(3) * ramp_stride;
        bool size_ok = bytes.size() == expected;

        unsigned ramp_chunk_count = 0;
        bool pages_ok = size_ok;
        bool saw[8] = {};
        if (size_ok) {
            for (unsigned idx = 0; idx < 3; ++idx) {
                size_t p = ramp0 + static_cast<size_t>(idx) * ramp_stride;
                if (bytes[p]!='R' || bytes[p+1]!='A' || bytes[p+2]!='M' || bytes[p+3]!='P') {
                    pages_ok = false;
                    break;
                }
                ++ramp_chunk_count;
                uint8_t chpageno = bytes[p + 10];
                if (chpageno < 8) saw[chpageno] = true;
            }
        }
        // Exactly {0, 2, 5} must be present; 1, 3, 4, 6, 7 must NOT.
        bool set_ok = pages_ok && saw[0] && saw[2] && saw[5]
                    && !saw[1] && !saw[3] && !saw[4] && !saw[6] && !saw[7];
        bool ok = size_ok && pages_ok && ramp_chunk_count == 3 && set_ok;
        check("BOOT-SNAPSAVE-02C",
              "SzxSaver::build() emits exactly banks {0, 2, 5} for a 48K "
              "save (machine_id=1) — the SZX page-numbering convention "
              "(spectaculator.com/docs/zx-state/rampage.shtml + "
              "libspectrum szx.c:3330-3334), NOT the first 3 banks {0,1,2}",
              ok,
              fmt("size_ok=%d pages_ok=%d ramp_chunks=%u "
                  "saw={%d,%d,%d,%d,%d,%d,%d,%d} size=%zu expected=%zu",
                  static_cast<int>(size_ok), static_cast<int>(pages_ok), ramp_chunk_count,
                  saw[0], saw[1], saw[2], saw[3], saw[4], saw[5], saw[6], saw[7],
                  bytes.size(), expected));
    }

    // BOOT-SNAPSAVE-02D — refusal for a machine .szx cannot represent.
    // SzxSaver::build()/ram_page_set() must return empty (no partial or
    // misrepresenting file) for any chMachineId outside {1, 2, 4, 5} — see
    // SzxSaver class doc-comment SCOPE/REFUSAL. Exercises the raw
    // build()/ram_page_set() API directly (Emulator-level refusal, i.e.
    // "--machine next", is covered in mmu_integration_test.cpp since it
    // needs a real Emulator/MachineType).
    {
        Fixture f;
        f.fresh();
        Z80Registers regs{};
        SzxSaver::SpecRegs spec{};

        struct Row { uint8_t machine_id; };
        const Row bad_ids[] = {{0}, {3}, {6}, {99}};
        bool all_refused = true;
        std::string detail;
        for (const auto& row : bad_ids) {
            std::string error;
            std::vector<uint8_t> bytes = SzxSaver::build(regs, 0, row.machine_id, spec, f.mmu, &error);
            std::vector<uint8_t> page_set = SzxSaver::ram_page_set(row.machine_id);
            bool refused = bytes.empty() && page_set.empty() && !error.empty();
            if (!refused) {
                all_refused = false;
                detail = fmt("machine_id=%u bytes_empty=%d page_set_empty=%d error_empty=%d",
                              row.machine_id, static_cast<int>(bytes.empty()),
                              static_cast<int>(page_set.empty()), static_cast<int>(error.empty()));
                break;
            }
        }
        check("BOOT-SNAPSAVE-02D",
              "SzxSaver::build()/ram_page_set() refuse (empty return, "
              "error message set) for any chMachineId outside {1,2,4,5} — "
              "48K/128K/+2A/+3 are the only machines .szx can represent",
              all_refused, detail);
    }

    // BOOT-SNAPSAVE-03 — NexSaver round trip (structural, see the
    // BOOT-SNAPSAVE-02 comment above for why this is Mmu-tier-only).
    // Uses a 768 KB (48-bank) Ram so num_banks/ram_required stay in the
    // non-clamped, unambiguous range (see BOOT-SNAPSAVE-03B for the
    // 112-bank clamp case) and a contiguous MMU slot 6/7 bank pair (see
    // BOOT-SNAPSAVE-03C for the non-contiguous case).
    {
        Ram small_ram(768 * 1024);
        Rom rom;
        Mmu mmu(small_ram, rom);
        mmu.reset();

        const unsigned bank_count = static_cast<unsigned>(small_ram.size() / 16384);  // 48
        for (unsigned page = 0; page < bank_count * 2; ++page) {
            uint8_t* p = small_ram.page_ptr(static_cast<uint16_t>(page));
            for (int i = 0; i < 8192; ++i)
                p[i] = static_cast<uint8_t>(page * 5 + i);
        }

        // Bank 9 (pages 18/19) mapped contiguously at slots 6/7 — the
        // clean "entry_bank" case NEX can represent exactly.
        mmu.set_page(6, 18);
        mmu.set_page(7, 19);

        const uint16_t pc = 0xC050, sp = 0xFF00;
        const uint8_t  border = 5;

        NexSaver::BuildResult r = NexSaver::build(pc, sp, border, mmu, small_ram.size());
        const std::vector<uint8_t>& bytes = r.data;

        bool header_ok = bytes.size() >= 512
            && bytes[0]=='N' && bytes[1]=='e' && bytes[2]=='x' && bytes[3]=='t'
            && bytes[4]=='V' && bytes[5]=='1' && bytes[6]=='.' && bytes[7]=='2'
            && bytes[8]==0                          // ram_required: 768 KB -> 0
            && bytes[9]==bank_count
            && bytes[10]==0                         // screen_flags: none
            && bytes[11]==border
            && ru16(bytes, 12)==sp && ru16(bytes, 14)==pc
            && bytes[134]==1                        // preserve_regs
            && bytes[139]==9;                       // entry_bank = page6/2 = 9

        bool banks_flag_ok = header_ok;
        if (banks_flag_ok) {
            for (unsigned b = 0; b < 112; ++b) {
                uint8_t want_flag = (b < bank_count) ? 1 : 0;
                if (bytes[18 + b] != want_flag) { banks_flag_ok = false; break; }
            }
        }

        // NexSaver writes banks in the same order NexLoader::apply()
        // reads them back in — NexLoader's private kBankOrder,
        // duplicated here (see nex_loader.h / nex_saver.h).
        static const int kBankOrder48[] = {
            5,2,0,1,3,4,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,
            24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
        };
        bool payload_ok = header_ok;
        size_t off = 512;
        size_t first_bad_bank = 999, first_bad_i = 0;
        uint8_t got = 0, want = 0;
        if (payload_ok) {
            for (int bank : kBankOrder48) {
                if (static_cast<unsigned>(bank) >= bank_count) continue;
                if (off + 16384 > bytes.size()) { payload_ok = false; first_bad_bank = static_cast<size_t>(bank); break; }
                bool bank_ok = true;
                for (size_t i = 0; i < 16384; ++i) {
                    uint16_t page = static_cast<uint16_t>(bank * 2 + (i >= 8192 ? 1 : 0));
                    uint8_t expect = static_cast<uint8_t>(page * 5 + (i % 8192));
                    uint8_t actual = bytes[off + i];
                    if (actual != expect) {
                        payload_ok = false; bank_ok = false;
                        first_bad_bank = static_cast<size_t>(bank); first_bad_i = i;
                        got = actual; want = expect;
                        break;
                    }
                }
                if (!bank_ok) break;
                off += 16384;
            }
            if (payload_ok && off != bytes.size()) payload_ok = false;  // no trailing junk
        }

        bool all_ok = header_ok && banks_flag_ok && payload_ok
                    && r.contiguous_entry_bank && !r.truncated_by_112_bank_limit
                    && r.banks_written == bank_count;

        check("BOOT-SNAPSAVE-03",
              "NexSaver::build() produces a spec-conformant .nex V1.2: "
              "512-byte header fields at their exact NexLoader-parsed "
              "offsets (magic/version/ram_required/num_banks/border/sp/"
              "pc/banks[]/preserve_regs/entry_bank), full bank payloads "
              "in NexLoader's kBankOrder — "
              "https://wiki.specnext.dev/NEX_file_format (G35)",
              all_ok,
              fmt("header=%d banks_flag=%d payload=%d contig=%d trunc=%d "
                  "written=%u/%u first_bad_bank=%zu i=%zu got=0x%02X want=0x%02X",
                  static_cast<int>(header_ok), static_cast<int>(banks_flag_ok),
                  static_cast<int>(payload_ok), static_cast<int>(r.contiguous_entry_bank),
                  static_cast<int>(r.truncated_by_112_bank_limit),
                  r.banks_written, bank_count, first_bad_bank, first_bad_i, got, want));
    }

    // BOOT-SNAPSAVE-03B — the NEX bank table is a fixed 112 bytes;
    // installed RAM beyond 1792 KB (jnext's 2048 KB ceiling = 128 banks)
    // cannot be represented. NexSaver must clamp to 112 banks and mark
    // the result truncated, not silently overflow the banks[] array or
    // claim ram_required=2 (which nex_loader.h documents as an
    // out-of-canonical-spec value it merely tolerates on read, per G155).
    {
        Fixture f;
        f.fresh();  // default Ram = 2048 KB = 128 banks
        NexSaver::BuildResult r = NexSaver::build(0, 0, 0, f.mmu, f.ram.size());
        bool clamp_ok = r.truncated_by_112_bank_limit == true
                      && r.banks_written == 112
                      && r.data.size() >= 20
                      && r.data[8] == 1     // ram_required: 112*16=1792KB -> value 1
                      && r.data[9] == 112;
        bool banks_array_ok = true;
        for (unsigned b = 0; b < 112 && banks_array_ok; ++b)
            if (r.data[18 + b] != 1) banks_array_ok = false;
        bool size_ok = r.data.size() == 512 + static_cast<size_t>(112) * 16384;
        bool all_ok = clamp_ok && banks_array_ok && size_ok;
        check("BOOT-SNAPSAVE-03B",
              "NexSaver::build() clamps to the format's 112-bank ceiling "
              "on >1792 KB installs and reports the clamp rather than "
              "overflowing banks[112] or writing an unrepresentable "
              "ram_required — nex_loader.h banks[112]/kBankOrder (G155)",
              all_ok,
              fmt("clamp=%d banks_array=%d size=%d written=%u size_bytes=%zu",
                  static_cast<int>(clamp_ok), static_cast<int>(banks_array_ok),
                  static_cast<int>(size_ok), r.banks_written, r.data.size()));
    }

    // BOOT-SNAPSAVE-03C — when MMU slots 6/7 are NOT a contiguous
    // (even, even+1) bank pair, NEX's single entry_bank byte cannot
    // represent the split exactly; NexSaver must flag this rather than
    // silently produce a file that resumes into the wrong bank at
    // 0xE000-0xFFFF (see NexSaver class doc-comment).
    {
        Ram small_ram(256 * 1024);  // 16 banks
        Rom rom;
        Mmu mmu(small_ram, rom);
        mmu.reset();
        mmu.set_page(6, 4);   // page 4 = bank 2, low half
        mmu.set_page(7, 9);   // page 9 = bank 4, high half — NOT bank 2's high half (5)
        NexSaver::BuildResult r = NexSaver::build(0x1000, 0x2000, 0, mmu, small_ram.size());
        bool ok = !r.contiguous_entry_bank && r.data.size() > 139 && r.data[139] == 2;
        check("BOOT-SNAPSAVE-03C",
              "NexSaver::build() detects a non-contiguous slot 6/7 bank "
              "pair and flags contiguous_entry_bank=false rather than "
              "silently mis-saving (NexSaver class doc-comment)",
              ok,
              fmt("contiguous=%d entry_bank_byte=%d (page6=4 page7=9, expect entry_bank=2)",
                  static_cast<int>(r.contiguous_entry_bank),
                  r.data.size() > 139 ? r.data[139] : -1));
    }

    // Cat 25 — Tape DeciLoad / Real-time loading (G36/G37). TZX 0x15
    // not decoded; WAV real-time pulse-shaping unverified.
    skip("BOOT-DECI-01", "TZX 0x15 not implemented (see G36)");
    skip("BOOT-DECI-02", "TZX 0x15 not implemented (see G36)");
    skip("BOOT-DECI-03", "WAV real-time path unverified (see G37)");
    skip("BOOT-DECI-04", "WAV real-time path unverified (see G37)");

    // WONT BOOT-FDC-01 / BOOT-FDC-02 / BOOT-FDC-03 — G38: +3 FDC `.dsk`
    // loader. uPD765 chip unmodelled; jnext does not emulate the +3
    // floppy controller at all. Already coupled to P1F-07=WONT (port
    // 0x1FFD bit 3 motor strobe — same plan entry, same precedent). No
    // current software target on the platform exercises +3 FDC; modern
    // Spectrum Next demos and NextZXOS use SD card / NEX loaders. Per
    // feedback_wont_taxonomy.md: explicit decision NOT to implement.
    // Revisit trigger: someone adds uPD765 + `.dsk` parser to jnext, at
    // which point these three rows become meaningful (boot/stage/motor).
}

// ── Category 26: G46(b) sram_pre_override priority arbiter ───────────
// VHDL: zxnext.vhd:3029-3066, 3137-3138.
//
// The two priority-arbiter helpers feed DivMmc::check_automap; tests
// here pin the truth-table for each gate in isolation.

void test_cat26_sram_pre_override() {
    set_group("Cat26 sram_pre_override (G46(b))");

    Fixture f;

    // PR-01: slot_in_rom_area is true iff effective page >= 0xE0 (=
    // VHDL `mmu_A21_A13(8)=1` per zxnext.vhd:2964 formula). Slot 0 at
    // reset has nr_mmu_=0xFF (sentinel) which the helper resolves via
    // get_effective_page; the legacy fallthrough here is also 0xFF for
    // a reset Mmu. NR 0x50/51=0xFF → effective page 0xFF → ROM area.
    f.fresh();
    {
        const bool ok =
            f.mmu.slot_in_rom_area(0) &&
            f.mmu.slot_in_rom_area(1);
        check("PR-01",
              "slot_in_rom_area at reset (NR 0x50/51 = 0xFF): true "
              "for slots 0/1 (VHDL :2964 mmu_A21_A13(8)=1 when "
              "effective page >= 0xE0)",
              ok,
              fmt("s0=%d s1=%d",
                  f.mmu.slot_in_rom_area(0),
                  f.mmu.slot_in_rom_area(1)));
    }

    // PR-02: NR 0x50 ← 0x0A (RAM page bank 5) → slot 0 NOT in ROM area.
    {
        f.fresh();
        f.mmu.set_page(0, 0x0A);
        check("PR-02",
              "slot_in_rom_area false when NR 0x50 = 0x0A (RAM bank 5)",
              !f.mmu.slot_in_rom_area(0),
              fmt("s0=%d", f.mmu.slot_in_rom_area(0)));
    }

    // PR-03: NR 0x50 ← 0xE0 (boundary) → slot 0 IS in ROM area.
    {
        f.fresh();
        f.mmu.set_page(0, 0xE0);
        check("PR-03",
              "slot_in_rom_area true at boundary NR 0x50 = 0xE0 "
              "(VHDL :2964 boundary)",
              f.mmu.slot_in_rom_area(0),
              fmt("s0=%d", f.mmu.slot_in_rom_area(0)));
    }

    // PR-04: NR 0x50 ← 0xDF (one below boundary) → slot 0 NOT in ROM
    // area.
    {
        f.fresh();
        f.mmu.set_page(0, 0xDF);
        check("PR-04",
              "slot_in_rom_area false at NR 0x50 = 0xDF (just below "
              "VHDL :2964 boundary)",
              !f.mmu.slot_in_rom_area(0),
              fmt("s0=%d", f.mmu.slot_in_rom_area(0)));
    }

    // PR-05: sram_pre_override(2) requires PC in slot 0/1. PC=0x4000 → 0.
    f.fresh();
    {
        check("PR-05",
              "pre_override(2)=0 for PC>=0x4000 (cpu_a(15:14)!=00)",
              !f.mmu.sram_pre_override_divmmc_eligible(0x4000, false),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_divmmc_eligible(0x4000, false)));
    }

    // PR-06: sram_pre_override(2) blocked when MF active.
    {
        check("PR-06",
              "pre_override(2)=0 when mf_active=1 (VHDL :3030 — MF "
              "wins, override='000')",
              !f.mmu.sram_pre_override_divmmc_eligible(0x0000, true),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_divmmc_eligible(0x0000, true)));
    }

    // PR-07: sram_pre_override(2) high in slot 0 with no MF.
    {
        check("PR-07",
              "pre_override(2)=1 for PC<0x4000 with mf_active=0",
              f.mmu.sram_pre_override_divmmc_eligible(0x0000, false) &&
              f.mmu.sram_pre_override_divmmc_eligible(0x3FFF, false),
              fmt("PC=0x0000 got=%d, PC=0x3FFF got=%d",
                  (int)f.mmu.sram_pre_override_divmmc_eligible(0x0000, false),
                  (int)f.mmu.sram_pre_override_divmmc_eligible(0x3FFF, false)));
    }

    // PR-08: sram_pre_override(0) high only in normal-ROM-mode branch.
    // Reset state (NR 0x50/51 = 0xFF, no MF, config_mode=0) → bit 0 high.
    {
        f.fresh();
        check("PR-08",
              "pre_override(0)=1 in normal ROM mode (VHDL :3057 → '111')",
              f.mmu.sram_pre_override_romcs_priority(0x0000, false, false),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_romcs_priority(0x0000, false, false)));
    }

    // PR-09: sram_pre_override(0) blocked by config_mode=1.
    // VHDL :3044 → "110".
    {
        f.fresh();
        check("PR-09",
              "pre_override(0)=0 when config_mode=1 (VHDL :3044)",
              !f.mmu.sram_pre_override_romcs_priority(0x0000, false, true),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_romcs_priority(0x0000, false, true)));
    }

    // PR-10: sram_pre_override(0) blocked when slot is RAM-mapped
    // (effective page < 0xE0). VHDL :3037 → "110".
    {
        f.fresh();
        f.mmu.set_page(0, 0x0A);             // RAM
        check("PR-10",
              "pre_override(0)=0 for slot 0 RAM-mapped (VHDL :3037)",
              !f.mmu.sram_pre_override_romcs_priority(0x0000, false, false),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_romcs_priority(0x0000, false, false)));
    }

    // PR-11: sram_pre_override(0) blocked by MF (composite gate).
    {
        f.fresh();
        check("PR-11",
              "pre_override(0)=0 when mf_active=1 (VHDL :3030 → '000')",
              !f.mmu.sram_pre_override_romcs_priority(0x0000, true, false),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_romcs_priority(0x0000, true, false)));
    }

    // PR-12: sram_pre_override(0) blocked by PC>=0x4000.
    {
        f.fresh();
        check("PR-12",
              "pre_override(0)=0 for PC>=0x4000",
              !f.mmu.sram_pre_override_romcs_priority(0x4000, false, false),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_romcs_priority(0x4000, false, false)));
    }

    // PR-13: PC=0x2000 lands in slot 1 (cpu_a(15:13)="001"). pre_override(0)
    // depends on slot 1 (NR 0x51), not slot 0. With NR 0x51 reset to 0xFF
    // and no MF / no config_mode → bit 0 high.
    {
        f.fresh();
        check("PR-13",
              "pre_override(0)=1 in slot 1 (PC=0x2000) with NR 0x51=0xFF",
              f.mmu.sram_pre_override_romcs_priority(0x2000, false, false),
              fmt("got=%d",
                  (int)f.mmu.sram_pre_override_romcs_priority(0x2000, false, false)));
    }

    // PR-14: split slots — NR 0x50 ROM, NR 0x51 RAM. PC=0x0000 → bit 0
    // high (slot 0 is ROM-mapped). PC=0x2000 → bit 0 low (slot 1 is
    // RAM-mapped).
    {
        f.fresh();
        f.mmu.set_page(0, 0xFF);     // slot 0 ROM
        f.mmu.set_page(1, 0x0A);     // slot 1 RAM
        const bool s0 =
            f.mmu.sram_pre_override_romcs_priority(0x0000, false, false);
        const bool s1 =
            f.mmu.sram_pre_override_romcs_priority(0x2000, false, false);
        check("PR-14",
              "pre_override(0) tracks per-slot ROM/RAM mode (VHDL :2952 "
              "mem_active_page selects MMU0..MMU7 by cpu_a(15:13))",
              s0 && !s1,
              fmt("PC=0x0000 got=%d (exp=1), PC=0x2000 got=%d (exp=0)",
                  (int)s0, (int)s1));
    }
}

// ─────────────────────────────────────────────────────────────────────
// Cat 27: Memory subsystem fix-regression tests (testcov-memory pass).
// Each row is a 1-to-1 regression for a specific commit-fix from the
// Task-2 verify1..verify10 chain. The sole purpose is to prevent the
// pre-fix bug from being reintroduced silently by future refactors.
// VHDL citations are reproduced from the original commit messages.
// ─────────────────────────────────────────────────────────────────────

namespace {

// FIX-NR5xFF-01..03 — commit f832f38 ("fix(mmu): NR $5x,$FF — VHDL-faithful
// per-slot semantics"). VHDL zxnext.vhd:4686-4696 explicit nr_mmu_we writes
// only MMU<i> on a NR $50..$57 = 0xFF write; no recomputation of other slots.
// Pre-fix engage_legacy_rom_paging() was called for slots 0/1, clobbering
// the other slot's prior NR-driven mapping; engage_legacy_ram_paging() was
// called for slots 6/7 (wrongly forcing legacy RAM auto-paging instead of
// inactive); slots 2-5 were mapped to ROM page 0 (also wrong — should be
// inactive per VHDL :3061 sram_pre_active='0' for mmu_A21_A13(8)='1').
void test_cat27_nr5x_ff_per_slot() {
    set_group("Cat27 fix-regression NR $5x,$FF (commit f832f38)");

    // FIX-NR5xFF-01: NR $50,RAM + NR $51,$FF must NOT clobber slot 0.
    // Pre-fix: engage_legacy_rom_paging() refreshed BOTH slots 0+1, so the
    // explicit NR $50,RAM mapping was lost when NR $51,$FF was written.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        // Step 1: map slot 0 to RAM page 0x05 explicitly via NR 0x50.
        f.mmu.set_page(0, 0x05);
        const uint8_t pre_g0 = f.mmu.get_page(0);
        const bool    pre_ro = f.mmu.is_slot_rom(0);
        // Step 2: write NR $51,$FF — must affect ONLY slot 1.
        f.mmu.engage_legacy_rom_paging_slot(1);
        const uint8_t post_g0 = f.mmu.get_page(0);
        const bool    post_ro = f.mmu.is_slot_rom(0);
        check("FIX-NR5xFF-01",
              "NR $51=$FF (engage_legacy_rom_paging_slot(1)) preserves slot 0 "
              "RAM mapping — VHDL zxnext.vhd:4686-4696 nr_mmu_we per-slot",
              pre_g0 == 0x05 && !pre_ro &&
              post_g0 == 0x05 && !post_ro,
              fmt("pre slot0: page=0x%02X ro=%d / post: page=0x%02X ro=%d",
                  pre_g0, (int)pre_ro, post_g0, (int)post_ro));
    }

    // FIX-NR5xFF-02: slots 2..5 with $FF must be inactive (read 0xFF, write
    // dropped). Pre-fix mapped them to ROM page 0 via map_rom(i, 0).
    // VHDL :3061 — for cpu_a(15:14) ≠ "00" with mmu_A21_A13(8)='1':
    //   sram_pre_active <= '0'  → SRAM does not respond.
    {
        Fixture f;
        f.fresh();
        // Seed SRAM page 0 (the wrong target pre-fix would have aliased here).
        // Make it a non-ROM-tagged byte so a stray fallback would be visible.
        if (f.ram.page_ptr(0)) f.ram.page_ptr(0)[0] = 0x42;
        f.mmu.set_page(2, 0xFF);
        const uint8_t r2 = f.mmu.read(0x4000);
        // Try writing — must be silently dropped.
        f.mmu.write(0x4000, 0x99);
        // Verify the underlying RAM page 0 is unchanged.
        const uint8_t ram_unchanged = f.ram.page_ptr(0) ? f.ram.page_ptr(0)[0] : 0;
        check("FIX-NR5xFF-02",
              "NR $52=$FF → slot 2 inactive: read returns 0xFF, write dropped "
              "(VHDL zxnext.vhd:3061 sram_pre_active=0 when mmu_A21_A13(8)=1)",
              r2 == 0xFF && ram_unchanged == 0x42,
              fmt("read(0x4000)=0x%02X (exp 0xFF), ram[0][0]=0x%02X (exp 0x42)",
                  r2, ram_unchanged));
    }

    // FIX-NR5xFF-03: slots 6..7 with $FF must be inactive too (NOT legacy
    // RAM auto-paged). Pre-fix engage_legacy_ram_paging() forced bank-0
    // mapping (port_7ffd composition).
    {
        Fixture f;
        f.fresh();
        // Seed legacy RAM bank 0 page so we can detect a buggy auto-paging
        // fallback. With port_7ffd_ at reset = 0, legacy bank 0 maps to
        // physical page 0x20 (bank 0 + 7FFD shift +0x20 in Next mode).
        if (f.ram.page_ptr(0x20)) f.ram.page_ptr(0x20)[0] = 0x33;
        f.mmu.set_page(6, 0xFF);
        const uint8_t r6 = f.mmu.read(0xC000);
        check("FIX-NR5xFF-03",
              "NR $56=$FF → slot 6 inactive (NOT legacy RAM auto-paged) — "
              "VHDL zxnext.vhd:3061 sram_pre_active=0",
              r6 == 0xFF,
              fmt("read(0xC000)=0x%02X (exp 0xFF — slot inactive)", r6));
    }
}

// FIX-PLUS3-01..02 — commit 45d8b30 ("fix(mmu): VHDL-faithful +3 special-
// paging arbitration"). VHDL zxnext.vhd:4623-4684 — when
// port_1ffd_special=1, the special-paging table fully replaces MMU0..7;
// on the 1→0 transition, slots 2-5 revert (bank 5 / bank 2). Pre-fix
// only the entry path was modeled; the exit revert was missing, leaving
// slots 2-5 stale. Pre-fix also missed: NR $8E entering special mode
// left legacy paging in place; map_128k_bank/write_port_dffd/eff7/nr_8f
// all clobbered the in-effect special table.
void test_cat27_plus3_special_arbitration() {
    set_group("Cat27 fix-regression +3 special arbitration (commit 45d8b30)");

    // FIX-PLUS3-01: 1→0 transition reverts slots 2-5 to legacy defaults
    // (bank 5 in 2/3, bank 2 in 4/5). VHDL zxnext.vhd:4655-4670.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        // Enter +3 special with config 0x07 (R21=1,R21_and=1,R1not2=0):
        //   slots 0/1=0x08/09, 2/3=0x0E/0x0F, 4/5=0x0C/0x0D, 6/7=0x06/0x07
        f.mmu.map_plus3_bank(0x07);
        const uint8_t s2_in = f.mmu.get_page(2);
        const uint8_t s4_in = f.mmu.get_page(4);
        // Exit special — must revert slots 2-5 to bank 5 (0x0A/0x0B) and
        // bank 2 (0x04/0x05).
        f.mmu.map_plus3_bank(0x00);
        const uint8_t s2_out = f.mmu.get_page(2);
        const uint8_t s3_out = f.mmu.get_page(3);
        const uint8_t s4_out = f.mmu.get_page(4);
        const uint8_t s5_out = f.mmu.get_page(5);
        check("FIX-PLUS3-01",
              "+3 special-mode 1→0 transition reverts slots 2-5 to bank 5 / "
              "bank 2 — VHDL zxnext.vhd:4655-4670",
              s2_in == 0x0E && s4_in == 0x0C &&
              s2_out == 0x0A && s3_out == 0x0B &&
              s4_out == 0x04 && s5_out == 0x05,
              fmt("in: s2=0x%02X s4=0x%02X / out: s2=0x%02X s3=0x%02X "
                  "s4=0x%02X s5=0x%02X (exp out 0x0A/0B/04/05)",
                  s2_in, s4_in, s2_out, s3_out, s4_out, s5_out));
    }

    // FIX-PLUS3-02: while special mode is active, a port_7FFD write must
    // NOT clobber the special table — the VHDL arbiter rewrites all 8
    // slots from the special table on every paging trigger when
    // port_1ffd_special='1'.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        // Enter special mode: 1FFD = 0x07 → cfg (B,A)=(1,1) → slots
        // 0..7 = {8,9,14,15,12,13,6,7}.
        f.mmu.map_plus3_bank(0x07);
        // Now write port_7FFD with bank 3 — pre-fix this clobbered slot
        // 6/7 with bank 3 pages (0x06/0x07). Post-fix the special table
        // wins: slot 6/7 stay at 0x06/0x07 (which happen to coincide
        // with bank 3 in this config — pick a different config to
        // disambiguate).
        f.mmu.map_128k_bank(0x05);  // bank 5 → would yield 0x0A/0x0B
        const uint8_t s6 = f.mmu.get_page(6);
        const uint8_t s7 = f.mmu.get_page(7);
        check("FIX-PLUS3-02",
              "+3 special: port_7FFD write does NOT clobber special table "
              "— VHDL zxnext.vhd:4623 (arbiter rewrites 0..7)",
              s6 == 0x06 && s7 == 0x07,
              fmt("post 7FFD=0x05 in special: s6=0x%02X s7=0x%02X (exp 0x06/0x07)",
                  s6, s7));
    }

    // FIX-PLUS3-03: port_1ffd_special_old serialization round-trip.
    // VHDL zxnext.vhd:3716/3729 + commit-message: "State serialization
    // grows port_1ffd_special_old by one bool". Save inside special mode,
    // load it back, then exit — the slot 2-5 revert must still fire.
    {
        Fixture src;
        src.fresh();
        src.mmu.set_machine_type(MachineType::ZX_PLUS3);
        src.mmu.map_plus3_bank(0x07);  // enter special
        // Save state.
        StateWriter measure;
        src.mmu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter writer(buf.data(), buf.size());
        src.mmu.save_state(writer);

        Fixture dst;
        dst.fresh();
        dst.mmu.set_machine_type(MachineType::ZX_PLUS3);
        StateReader reader(buf.data(), buf.size());
        dst.mmu.load_state(reader);
        // Now exit special mode on the loaded mmu — this must trigger the
        // slot 2-5 revert (special_old=1 ∧ new special=0).
        dst.mmu.map_plus3_bank(0x00);
        const uint8_t s2 = dst.mmu.get_page(2);
        const uint8_t s4 = dst.mmu.get_page(4);
        check("FIX-PLUS3-03",
              "port_1ffd_special_old persisted across save/load — exit "
              "after load fires slot 2-5 revert (VHDL :3716,3729; commit 45d8b30)",
              s2 == 0x0A && s4 == 0x04,
              fmt("post-load post-exit s2=0x%02X (exp 0x0A) s4=0x%02X (exp 0x04)",
                  s2, s4));
    }
}

// FIX-NR8C-CACHE-01..02 — commit 3dd4e73 ("fix(memory): VHDL-faithful slot
// 0/1 high-page handling + NR 0x8C cache refresh"). Two findings:
//
// Finding 1: NR 0x8C lock-bit changes did not propagate to cached slot 0/1
// read pointer. VHDL :2981-3008 makes altrom_lock_rom1/rom0 feed sram_rom
// combinationally. set_nr_8c was inline header that only stored the byte;
// fix moved it out-of-line + apply_legacy_rom_slots_().
//
// Finding 2: NR $50/$51 with v ∈ [$E0..$FE] mis-routed slot 0/1 to wrong
// SRAM (page = (v + $20) mod 256). Fix: high-page gate now routes slot
// 0/1 to legacy ROM (sram_rom-derived).
void test_cat27_nr8c_cache_and_high_page() {
    set_group("Cat27 fix-regression NR $8C cache + slot $E0..$FE (commit 3dd4e73)");

    // FIX-NR8C-CACHE-01: NR 0x8C lock_rom1 flip on a +3 machine moves
    // sram_rom from a previous value to a value that differs in the
    // bottom bit. The cached read_ptr_ for slot 0/1 must update on the
    // NR 0x8C write — pre-fix the cache was stale until the next
    // port-paging write.
    //
    // Strategy: switch to +3, set port_7ffd(4)=0 and port_1ffd(2)=0
    // (sram_rom = 0 with no locks → ROM page 0/1). Then set NR 0x8C lock
    // bits to flip sram_rom to a different value (lock_rom1=1 →
    // sram_rom=2, ROM page 4/5). Tag the underlying ROM bytes with their
    // page index so a stale cache reading from the OLD ROM page would
    // be observable.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.map_128k_bank(0x00);                 // 7ffd(4)=0
        f.mmu.map_plus3_bank(0x00);                // 1ffd(2)=0 → sram_rom=0
        // Pre-NR-8C read: slot 0 should serve from ROM page 0 (Fixture
        // tagged ROM bytes with (page<<4 | offset_lo) → byte at 0x0000
        // of page 0 = 0x00).
        const uint8_t pre = f.mmu.read(0x0000);
        // Now set lock_rom1=1 (NR 0x8C bit 5). Per VHDL :2987-2995 +3
        // branch with a lock asserted: sram_rom <= "lock_rom1, lock_rom0"
        // = "10" = 2 → ROM page 4 for slot 0 (sram_rom*2 + slot).
        // Fixture page 4 byte 0 is (4<<4)|0 = 0x40.
        f.mmu.set_nr_8c(0x20);
        const uint8_t post = f.mmu.read(0x0000);
        check("FIX-NR8C-CACHE-01",
              "NR 0x8C lock_rom1 flip refreshes slot-0 cached read pointer "
              "(pre→0x00, post→0x40) — VHDL :2981-3008 + 3052; commit 3dd4e73",
              pre == 0x00 && post == 0x40,
              fmt("pre=0x%02X (exp 0x00 ROM page 0) post=0x%02X (exp 0x40 "
                  "ROM page 4 — sram_rom=2 via lock_rom1)", pre, post));
    }

    // FIX-NR8C-CACHE-02: strengthened — pin TWO independent discriminative
    // observables on a NR 0x8C write that does NOT change sram_rom (only
    // altrom_en/rw bits flip). VHDL :3813 (`port_memory_change_dly` only
    // pulses on a paging-port write that changes the relevant signal),
    // so an altrom-en-only write must NOT clobber the cached slot
    // pointers. Pre-fix `set_nr_8c` was a header inline (no rebuild) so
    // both observables passed trivially. Post-fix `set_nr_8c` calls
    // `apply_legacy_rom_slots_()` which is gated on `read_only_=true`
    // (verify3 fix) so an explicit-RAM slot stays put. The strengthened
    // row pins:
    //   (a) The slot 0 nr_mmu / read_only state is preserved.
    //   (b) A subsequent CPU read at 0x0000 still serves the RAM-page
    //       byte (= the pre-write data round-trip survives the NR 0x8C
    //       write — verifies the cached read_ptr_ was not wiped).
    //
    // Reviewer note (#9): the original row was flagged as "weak /
    // companion" because pre-fix set_nr_8c was a no-op so it would
    // also pass. The strengthening keeps the same discriminative target
    // (intermediate-state regression where set_nr_8c forgets the
    // read_only_ gate) but adds a CPU-read observable that pins the
    // cached pointer specifically.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.map_128k_bank(0x00);
        f.mmu.map_plus3_bank(0x00);
        f.mmu.set_page(0, 0x05);                   // explicit RAM map
        // Seed the RAM page with a sentinel — the CPU read at 0x0000
        // must continue to serve this byte after the NR 0x8C write.
        // RAM page 0x05 (= bank 2 hi on +3 default) — under +3 with
        // map_128k_bank(0)/map_plus3_bank(0) we are NOT in special
        // paging, so set_page(0, 0x05) lands the slot at the explicit
        // physical page directly via to_sram_page (no shift in non-Next
        // mode for this fixture).
        if (f.ram.page_ptr(0x05)) f.ram.page_ptr(0x05)[0] = 0xA9;
        const uint8_t pre = f.mmu.get_page(0);
        const bool pre_ro = f.mmu.is_slot_rom(0);
        const uint8_t pre_read = f.mmu.read(0x0000);
        // NR 0x8C write that sets altrom_en/rw only (no lock change).
        f.mmu.set_nr_8c(0xC0);
        const uint8_t post = f.mmu.get_page(0);
        const bool post_ro = f.mmu.is_slot_rom(0);
        const uint8_t post_read = f.mmu.read(0x0000);
        check("FIX-NR8C-CACHE-02",
              "NR 0x8C write with no lock change preserves slot 0 "
              "RAM mapping AND cached read pointer — VHDL :3813",
              pre == 0x05 && !pre_ro && pre_read == 0xA9 &&
              post == 0x05 && !post_ro && post_read == 0xA9,
              fmt("pre: page=0x%02X ro=%d read=0x%02X / post: page=0x%02X "
                  "ro=%d read=0x%02X (exp 0x05/0/0xA9 both)",
                  pre, (int)pre_ro, pre_read,
                  post, (int)post_ro, post_read));
    }

    // FIX-SLOT01-HIPAGE-01: NR $50 with v=$E5 must route slot 0 to legacy
    // ROM (sram_rom-derived), NOT to wrap-aliased SRAM page (0xE5 + 0x20)
    // mod 256 = 0x05. Slot 0 nr_mmu_ keeps verbatim 0xE5 (NR-port readback
    // contract); slot routing is legacy ROM via :3052.
    //
    // Discriminative-fix (review TESTCOV-MEMORY-REVIEW finding #9): the
    // wrap-aliasing bug only fires in Next mode where `to_sram_page(0xE5)`
    // applies the +0x20 shift (= page 0x05). Fixture default has
    // `rom_in_sram_=false`, where `to_sram_page` is the identity (= 0xE5)
    // — both pre-fix and post-fix would resolve to ROM-page-0-byte-0 = 0
    // (Fixture ROM tag for page 0 offset 0 = (0<<4)|0 = 0). Enabling
    // `rom_in_sram_=true` activates the wrap-aliasing path AND routes the
    // post-fix legacy ROM to RAM page `sram_rom*2+0=0` (Next mode serves
    // ROM from RAM in pages 0..7 per Mmu::set_rom_in_sram). Seed both the
    // wrap-target SRAM page (0x05) and the post-fix ROM-in-SRAM page
    // (0x00) with distinct sentinels so pre-fix and post-fix yield
    // observably different results.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        f.mmu.set_rom_in_sram(true);  // Next mode — `to_sram_page(0xE5)` = 0x05
        // Wrap-target sentinel (pre-fix would read this).
        if (f.ram.page_ptr(0x05)) f.ram.page_ptr(0x05)[0] = 0x77;
        // ROM-in-SRAM page 0 sentinel (post-fix legacy-ROM path serves
        // from `ram.page_ptr(sram_rom*2+slot=0)`). Distinct from 0x77 and
        // from 0xFF (the unmapped value) so pre-fix vs post-fix is
        // observable.
        if (f.ram.page_ptr(0x00)) f.ram.page_ptr(0x00)[0] = 0x33;
        f.mmu.set_page(0, 0xE5);
        // Pre-fix: rebuild_ptr → `ram.page_ptr(to_sram_page(0xE5)=0x05)[0]`
        //          = 0x77 (wrap-aliased into RAM page 0x05).
        // Post-fix: legacy-ROM branch in rebuild_ptr → `ram.page_ptr(0)[0]`
        //           = 0x33 (sram_rom*2+0=0; Next mode serves ROM from RAM).
        const uint8_t v = f.mmu.read(0x0000);
        const uint8_t nr_rb = f.mmu.get_page(0);  // verbatim per VHDL :4611-4612
        check("FIX-SLOT01-HIPAGE-01",
              "NR $50=0xE5 routes slot 0 to legacy ROM (sram_rom-derived) — "
              "VHDL :2964 mmu_A21_A13(8)=1 + :3052; commit 3dd4e73",
              v == 0x33 && nr_rb == 0xE5,
              fmt("read(0x0000)=0x%02X (exp 0x33 ROM-in-SRAM page 0; "
                  "pre-fix would alias 0x77 from wrap RAM page 0x05) "
                  "NR readback=0x%02X (exp 0xE5 verbatim)", v, nr_rb));
    }
}

// FIX-UNLOCK-01 — commit 31d1786 ("verify3(memory): pass-3 re-audit"),
// finding 1: unlock_paging() did not clear port_7ffd_ bit 5. VHDL
// zxnext.vhd:3654-3656 clears port_7ffd_reg(5) directly on NR 0x08
// bit 7. Pre-fix kept bit 5 set in port_7ffd_, only clearing the
// standalone paging_locked_ flag — readback / Pentagon-1024 composition
// saw stale bit.
void test_cat27_unlock_clears_bit5() {
    set_group("Cat27 fix-regression unlock_paging clears 7FFD(5) (commit 31d1786)");

    // FIX-UNLOCK-01: unlock_paging() must clear bit 5 of port_7ffd_.
    {
        Fixture f;
        f.fresh();
        f.mmu.map_128k_bank(0x25);  // bit 5 set + bank 5 select
        const uint8_t pre = f.mmu.port_7ffd();
        const bool    pre_locked = f.mmu.paging_locked();
        f.mmu.unlock_paging();
        const uint8_t post = f.mmu.port_7ffd();
        const bool    post_locked = f.mmu.paging_locked();
        check("FIX-UNLOCK-01",
              "unlock_paging() clears bit 5 of port_7ffd_ AND paging_locked_ "
              "— VHDL zxnext.vhd:3654-3656; commit 31d1786",
              (pre & 0x20) != 0 && pre_locked &&
              (post & 0x20) == 0 && !post_locked,
              fmt("pre p7ffd=0x%02X locked=%d / post p7ffd=0x%02X locked=%d "
                  "(exp pre bit5=1, post bit5=0)",
                  pre, (int)pre_locked, post, (int)post_locked));
    }
}

// FIX-NR8C-PRESERVE-01 — commit 31d1786 finding 2: set_nr_8c() clobbered
// slots 0/1 even when explicitly RAM-mapped via NR 0x50/0x51 with v < $E0.
// VHDL leaves MMU<i> alone on NR 0x8C. Fix: only refresh slots in legacy
// ROM mode (read_only_=true).
void test_cat27_nr8c_preserves_ram_slots() {
    set_group("Cat27 fix-regression NR $8C preserves RAM slots (commit 31d1786)");

    // FIX-NR8C-PRESERVE-01: with slot 0 RAM-mapped via NR 0x50, an NR 0x8C
    // write must preserve the RAM mapping (read_only_=false, nr_mmu_[0]
    // stays at the explicit page, NOT 0xFF).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x05);                   // explicit RAM
        f.mmu.set_nr_8c(0x20);                     // lock_rom1 toggle
        const uint8_t g0 = f.mmu.get_page(0);
        const bool    ro0 = f.mmu.is_slot_rom(0);
        check("FIX-NR8C-PRESERVE-01",
              "NR 0x8C write preserves slot 0 explicit RAM mapping — "
              "VHDL :3813 no port_memory_change_dly; commit 31d1786",
              g0 == 0x05 && !ro0,
              fmt("g0=0x%02X ro0=%d (exp 0x05/0)", g0, (int)ro0));
    }

    // FIX-NR8C-PRESERVE-02: discriminative — slot in legacy-ROM mode
    // (read_only_=true) DOES get refreshed when sram_rom changes.
    // (Already covered by FIX-NR8C-CACHE-01 above; this row pins the
    // negative side: explicit-RAM slot is NOT refreshed.)
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.set_page(1, 0x06);                   // explicit RAM in slot 1
        const uint8_t pre = f.mmu.get_page(1);
        f.mmu.set_nr_8c(0x20);                     // lock_rom1
        const uint8_t post = f.mmu.get_page(1);
        check("FIX-NR8C-PRESERVE-02",
              "NR 0x8C with sram_rom-changing lock preserves slot 1 RAM "
              "mapping — VHDL :3813; commit 31d1786",
              pre == 0x06 && post == 0x06,
              fmt("pre=0x%02X post=0x%02X (exp both 0x06)", pre, post));
    }
}

// FIX-EFF7-FF-01 — commit 31d1786 finding 3 + commit 560cb18 finding 1:
// engage_legacy_rom_paging_slot() under EFF7=1 must keep nr_mmu_ at 0xFF
// (verbatim NR-write value) AND must NOT mirror EFF7's RAM-at-0x0000
// override on the NR-write cycle. VHDL nr_mmu_we (zxnext.vhd:4686-4696)
// stores nr_wr_dat verbatim; eff7 override fires only on
// port_memory_change_dly='1', which NR 0x50/0x51 does NOT trigger.
void test_cat27_eff7_nr5x_ff() {
    set_group("Cat27 fix-regression EFF7+NR $5x,$FF (commits 31d1786, 560cb18)");

    // FIX-EFF7-FF-01: an `NR $50,$FF` while EFF7(3)=1 must:
    //   (a) leave nr_mmu_[0] at 0xFF (verbatim — NR-port readback contract)
    //   (b) route slot 0 to legacy ROM (sram_rom-derived), NOT to RAM page 0.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        // Activate eff7(3) RAM-at-0x0000.
        f.mmu.write_port_eff7(0x08);
        // Confirm baseline: slot 0/1 is now RAM (eff7 override applied via
        // port_memory_change_dly path).
        const bool baseline_ro0 = f.mmu.is_slot_rom(0);
        // Now write NR 0x50 = 0xFF — must engage legacy ROM (NOT mirror eff7).
        f.mmu.engage_legacy_rom_paging_slot(0);
        const uint8_t nr_rb0 = f.mmu.get_page(0);  // VHDL nr_mmu_we → 0xFF
        const bool    rom0   = f.mmu.is_slot_rom(0);
        check("FIX-EFF7-FF-01",
              "NR $50=$FF under EFF7(3)=1: nr_mmu_[0]=0xFF verbatim, slot 0 "
              "→ legacy ROM (not RAM) — VHDL :4686-4696 nr_mmu_we; "
              "commits 31d1786 + 560cb18",
              !baseline_ro0 && nr_rb0 == 0xFF && rom0,
              fmt("baseline_ro0=%d (exp 0 — eff7 RAM mode), "
                  "nr_rb0=0x%02X (exp 0xFF), rom0=%d (exp 1)",
                  (int)baseline_ro0, nr_rb0, (int)rom0));
    }
}

// FIX-NRMMU-SAVE-01 — commit 560cb18 finding 2: nr_mmu_[8] not persisted in
// save_state. load_state recovered nr_mmu_[i] = read_only_[i] ? 0xFF :
// slots_[i], which collapsed verbatim 0xE0..0xFE NR 0x50/0x51 writes back
// to the 0xFF sentinel. Fix: persist verbatim.
void test_cat27_nrmmu_save_roundtrip() {
    set_group("Cat27 fix-regression nr_mmu_ save-roundtrip (commit 560cb18)");

    // FIX-NRMMU-SAVE-01: write NR 0x50 = 0xE5 (verbatim VHDL MMU<0>=$E5),
    // save+load, verify get_page(0) returns 0xE5 (NOT 0xFF after the
    // pre-fix lossy reconstruction).
    {
        Fixture src;
        src.fresh();
        src.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        src.mmu.set_page(0, 0xE5);  // routes slot 0 to legacy ROM, nr_mmu_[0]=0xE5
        const uint8_t pre = src.mmu.get_page(0);
        // Save.
        StateWriter measure;
        src.mmu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter writer(buf.data(), buf.size());
        src.mmu.save_state(writer);
        // Load into fresh Mmu.
        Fixture dst;
        dst.fresh();
        dst.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        StateReader reader(buf.data(), buf.size());
        dst.mmu.load_state(reader);
        const uint8_t post = dst.mmu.get_page(0);
        check("FIX-NRMMU-SAVE-01",
              "nr_mmu_[0]=0xE5 verbatim round-trips through save/load — "
              "VHDL :4686-4699 + :6075-6082 NR readback; commit 560cb18",
              pre == 0xE5 && post == 0xE5,
              fmt("pre=0x%02X post=0x%02X (exp both 0xE5; pre-fix post=0xFF)",
                  pre, post));
    }
}

// FIX-NR12-PROP-01 — commit 560cb18 finding 3: NR 0x12 (Layer 2 active
// bank) write did not propagate to Mmu::l2_bank_. VHDL :2968 makes
// layer2_active_bank combinational from nr_12_layer2_active_bank, so the
// CPU L2 read/write-over path must use the new bank immediately. NR 0x13
// had analogous propagation; NR 0x12 was the missing seam. Fix: added
// Mmu::set_l2_active_bank() and called it from the NR 0x12 handler.
void test_cat27_nr12_propagation() {
    set_group("Cat27 fix-regression NR 0x12 propagation (commit 560cb18)");

    // FIX-NR12-PROP-01: setting the active bank via Mmu::set_l2_active_bank
    // must affect a subsequent L2 read at 0x0000 (low half always-on per
    // VHDL :3043). Discriminative: pick two distinct banks, seed the
    // physical pages with different sentinels, and verify the read changes.
    {
        Fixture f;
        f.fresh();
        // Enable L2 read with seg=00 (low half).
        f.mmu.set_l2_port(0x04, 8);          // bit 2=read, bank=8
        // Seed L2 bank 8 (pages 0x10/0x11) and bank 16 (pages 0x20/0x21).
        if (f.ram.page_ptr(0x10)) f.ram.page_ptr(0x10)[0] = 0xAA;
        if (f.ram.page_ptr(0x20)) f.ram.page_ptr(0x20)[0] = 0xBB;
        const uint8_t r_bank8 = f.mmu.read(0x0000);
        // Switch active bank via the verify4-fix API.
        f.mmu.set_l2_active_bank(16);
        const uint8_t r_bank16 = f.mmu.read(0x0000);
        check("FIX-NR12-PROP-01",
              "Mmu::set_l2_active_bank propagates to CPU L2 read path — "
              "VHDL :2968 + :2969 layer2_active_page; commit 560cb18",
              r_bank8 == 0xAA && r_bank16 == 0xBB,
              fmt("bank8 read=0x%02X (exp 0xAA) bank16 read=0x%02X (exp 0xBB)",
                  r_bank8, r_bank16));
    }
}

// FIX-RESET-CFG-01 — commit 165835d finding 1: Mmu::reset() unconditionally
// re-enabled boot_rom_en when a boot ROM was loaded. VHDL :5109-5111 gates
// the re-assertion on `if nr_03_config_mode='1'`. After firmware cleared
// config_mode, a subsequent reset would re-arm the boot ROM in jnext but
// leave it cleared in VHDL. Fix: gate on config_mode_.
void test_cat27_reset_bootrom_config_mode_gate() {
    set_group("Cat27 fix-regression reset bootrom config_mode gate (commit 165835d)");

    // FIX-RESET-CFG-01: with config_mode=0 at reset time, boot_rom_en
    // must NOT be re-armed (preserves the cleared state).
    {
        // Stand-alone Mmu — pass a dummy boot ROM pointer so the gate
        // would be reachable.
        std::vector<uint8_t> dummy_boot(0x2000, 0xC9);  // 8K of RET
        Fixture f;
        f.mmu.set_boot_rom(dummy_boot.data(), dummy_boot.size());
        // Simulate firmware having cleared config_mode AND boot_rom_en.
        f.mmu.set_config_mode(false);
        f.mmu.set_boot_rom_enabled(false);
        // Soft reset.
        f.mmu.reset(false);
        const bool boot_en_after_reset_cfg0 = f.mmu.boot_rom_enabled();
        check("FIX-RESET-CFG-01-A",
              "reset with config_mode=0 leaves boot_rom_en cleared — "
              "VHDL :5109-5111; commit 165835d",
              !boot_en_after_reset_cfg0,
              fmt("boot_rom_enabled() after reset(cfg=0) = %d (exp 0)",
                  (int)boot_en_after_reset_cfg0));
    }

    // FIX-RESET-CFG-01-B: discriminative — with config_mode=1 at reset
    // time, boot_rom_en IS re-armed.
    {
        std::vector<uint8_t> dummy_boot(0x2000, 0xC9);
        Fixture f;
        f.mmu.set_boot_rom(dummy_boot.data(), dummy_boot.size());
        f.mmu.set_config_mode(true);
        f.mmu.set_boot_rom_enabled(false);
        f.mmu.reset(false);
        const bool boot_en_after_reset_cfg1 = f.mmu.boot_rom_enabled();
        check("FIX-RESET-CFG-01-B",
              "reset with config_mode=1 re-arms boot_rom_en — "
              "VHDL :5109-5111; commit 165835d",
              boot_en_after_reset_cfg1,
              fmt("boot_rom_enabled() after reset(cfg=1) = %d (exp 1)",
                  (int)boot_en_after_reset_cfg1));
    }
}

// FIX-MTC-SPECIAL-01 — commit 165835d finding 2: set_machine_type() called
// apply_legacy_rom_slots_() on transitions, clobbering slots 0/1 even
// while +3 special paging was active. VHDL :4623-4632: the special-paging
// MMU table is independent of sram_rom / machine_type. Fix: early-return
// when port_1ffd_(0)=1.
void test_cat27_machine_type_during_special() {
    set_group("Cat27 fix-regression set_machine_type during +3 special (commit 165835d)");

    // FIX-MTC-SPECIAL-01: enter +3 special, change machine type, slots 0/1
    // must remain at their special-mapping pages (not get clobbered).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX_PLUS3);
        f.mmu.map_plus3_bank(0x07);  // special, cfg(B,A)=(1,1) → s0/1=0x08/09
        const uint8_t s0_pre = f.mmu.get_page(0);
        const uint8_t s1_pre = f.mmu.get_page(1);
        const bool ro0_pre = f.mmu.is_slot_rom(0);
        // Now change machine type while still in special — must NOT affect
        // the special MMU image.
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        const uint8_t s0_post = f.mmu.get_page(0);
        const uint8_t s1_post = f.mmu.get_page(1);
        const bool ro0_post = f.mmu.is_slot_rom(0);
        check("FIX-MTC-SPECIAL-01",
              "set_machine_type during +3 special preserves special-mapping "
              "slots 0/1 — VHDL :4623-4632 (table independent of sram_rom); "
              "commit 165835d",
              s0_pre == 0x08 && s1_pre == 0x09 && !ro0_pre &&
              s0_post == 0x08 && s1_post == 0x09 && !ro0_post,
              fmt("pre s0=0x%02X s1=0x%02X ro0=%d / post s0=0x%02X s1=0x%02X "
                  "ro0=%d (exp pre 0x08/09/0, post 0x08/09/0)",
                  s0_pre, s1_pre, (int)ro0_pre,
                  s0_post, s1_post, (int)ro0_post));
    }
}

// FIX-CURRSRAMROM-128K-01 — commit b6b42dd A5: current_sram_rom() ZX128K
// case bypassed altrom-lock. VHDL :2997-3007 routes 128K and Next/Pentagon
// through the same else branch with shared altrom-lock semantics.
void test_cat27_currentsramrom_128k_altrom_lock() {
    set_group("Cat27 fix-regression current_sram_rom 128K altrom-lock (commit b6b42dd)");

    // FIX-CURRSRAMROM-128K-01: 128K with altrom lock_rom1=1 must yield
    // sram_rom=1 (= lock_rom1 select), NOT pure (port_7ffd_>>4)&1.
    // Discriminative: clear port_7ffd_(4) so the pre-fix path would have
    // returned 0 — fix returns lock_rom1=1 → sram_rom=1.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZX128K);
        f.mmu.map_128k_bank(0x00);   // 7ffd(4)=0
        f.mmu.set_nr_8c(0x20);       // lock_rom1=1
        const uint8_t v = f.mmu.current_sram_rom();
        check("FIX-CURRSRAMROM-128K-01",
              "128K with lock_rom1=1: sram_rom = lock_rom1 = 1 (NOT 7ffd(4)=0)"
              " — VHDL :2997-3007 shared else branch; commit b6b42dd",
              v == 1,
              fmt("current_sram_rom()=%u (exp 1)", v));
    }
}

// FIX-L2-OVERLAY-LOWHALF-01..02 — commit b6b42dd A1: Layer 2 overlay
// segment gating. VHDL :3043/:3050/:3057 enable L2 in low half
// UNCONDITIONALLY (non-MF cases) regardless of seg; :3065 enables L2 in
// high half ONLY when seg=11. Pre-fix bitmask treated seg as window-mask
// (seg=01 → enabled at 0x4000-0x7FFF only).
void test_cat27_l2_overlay_lowhalf() {
    set_group("Cat27 fix-regression L2 overlay low-half always-on (commit b6b42dd)");

    // FIX-L2-OVERLAY-LOWHALF-01: with seg=01 (was: low half DISABLED
    // pre-fix), a write at 0x0000 must still be redirected to L2.
    // VHDL :3043 sram_pre_override(1)='1' for the low half regardless of
    // seg in the non-MF branch.
    //
    // The seg=01 case sets offset_pre = 1, so bank+offset_pre = 9 →
    // physical pages 0x12/0x13.
    {
        Fixture f;
        f.fresh();
        // Slot 0 reset is ROM (read_only_=true). Set slot 0 to RAM page 0x20
        // so write would land there if L2 didn't intercept.
        f.mmu.set_page(0, 0x20);
        f.mmu.set_l2_port(0x41, 8);   // bit 0=write, bits 7:6=01 (seg=01), bank=8
        f.mmu.write(0x0000, 0xCC);
        const uint8_t mmu_side = f.ram.page_ptr(0x20)[0];
        const uint8_t l2_side  = f.ram.page_ptr(0x12)[0];
        check("FIX-L2-OVERLAY-LOWHALF-01",
              "L2 write-over with seg=01 still intercepts low half (0x0000) "
              "— VHDL :3043 sram_pre_override(1)=1; commit b6b42dd",
              mmu_side != 0xCC && l2_side == 0xCC,
              fmt("MMU page 0x20[0]=0x%02X (must NOT be 0xCC); "
                  "L2 page 0x12[0]=0x%02X (must be 0xCC — bank+offset_pre=9)",
                  mmu_side, l2_side));
    }

    // FIX-L2-OVERLAY-LOWHALF-02: with seg=10 (was: 0x8000+ only pre-fix)
    // a write at 0x0000 must STILL be redirected (low half always-on).
    // bank+offset_pre = 8+2 = 10 → physical pages 0x14/0x15.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_page(0, 0x20);
        f.mmu.set_l2_port(0x81, 8);   // bit 0=write, bits 7:6=10 (seg=10), bank=8
        f.mmu.write(0x0000, 0xDD);
        const uint8_t mmu_side = f.ram.page_ptr(0x20)[0];
        const uint8_t l2_side  = f.ram.page_ptr(0x14)[0];
        check("FIX-L2-OVERLAY-LOWHALF-02",
              "L2 write-over with seg=10 still intercepts low half (0x0000) "
              "— VHDL :3043; commit b6b42dd",
              mmu_side != 0xDD && l2_side == 0xDD,
              fmt("MMU page 0x20[0]=0x%02X (must NOT be 0xDD); "
                  "L2 page 0x14[0]=0x%02X (must be 0xDD)",
                  mmu_side, l2_side));
    }
}

// FIX-L2-ROM-AREA-01..02 — commit 9d252b6 (verify10 class-(c)): Layer 2
// arbiter layer2_A21_A13(8)=1 → sram_active=0 gate. VHDL :2971 + :3101-3102:
// for (bank+bofs) bits[6:4]=111 (sum & 0x70 == 0x70, i.e. sum ∈
// {0x70..0x7F, 0xF0..0xFF}), the SRAM is INACTIVE — read returns floating
// bus, write dropped. Pre-fix to_sram_page(...) wrapped through +0x20 and
// aliased into ROM-in-SRAM region (page 0..7), risking ROM corruption.
void test_cat27_l2_rom_area_gate() {
    set_group("Cat27 fix-regression L2 ROM-area inactive gate (commit 9d252b6)");

    // FIX-L2-ROM-AREA-01: enable L2 read with bank=0x70 (boundary), seg=00
    // (offset_pre=0). sum = 0x70 → gate fires → read returns 0xFF.
    {
        Fixture f;
        f.fresh();
        // Pre-fix would compute phys_page = to_sram_page(0xE0) = 0x00 and
        // return ram[0][0]. Tag it with a sentinel so a regression would
        // be visible.
        if (f.ram.page_ptr(0x00)) f.ram.page_ptr(0x00)[0] = 0x55;
        f.mmu.set_l2_port(0x04, 0x70);  // bit 2=read, seg=00, bank=0x70
        const uint8_t v = f.mmu.read(0x0000);
        check("FIX-L2-ROM-AREA-01",
              "L2 read with bank=0x70 → sram_active=0 → 0xFF (NOT ROM-area "
              "wrap) — VHDL :2971 + :3101-3102; commit 9d252b6",
              v == 0xFF,
              fmt("read(0x0000)=0x%02X (exp 0xFF; pre-fix would alias 0x55)", v));
    }

    // FIX-L2-ROM-AREA-02: write must be silently dropped. The pre-fix bug
    // wrote to `to_sram_page(bank+bofs)`. With bank=0x70 + bofs=0x00,
    // `to_sram_page(0xE0)` = 0x00 in Next mode (rom_in_sram_=true; +0x20
    // wraps to 0x00) — i.e. the write would land in ROM-in-SRAM page 0,
    // corrupting ROM. Post-fix: write is silently dropped per VHDL.
    //
    // Discriminative-fix (review TESTCOV-MEMORY-REVIEW finding #23):
    // Fixture default has `rom_in_sram_=false`, where `to_sram_page` is
    // the identity. Pre-fix would write to `ram.page_ptr(0xE0)[0]`, NOT
    // page 0x00, so the seeded sentinel at page 0x00 is unchanged in
    // BOTH pre-fix and post-fix paths — the test passes either way and
    // the bug is masked. Enable `rom_in_sram_=true` so the wrap-aliasing
    // path fires (Next-mode behaviour matches the bug).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        f.mmu.set_rom_in_sram(true);  // Next mode — `to_sram_page(0xE0)` = 0x00
        if (f.ram.page_ptr(0x00)) f.ram.page_ptr(0x00)[0] = 0x55;
        f.mmu.set_l2_port(0x01, 0x70);  // bit 0=write, seg=00, bank=0x70
        f.mmu.write(0x0000, 0xAB);
        const uint8_t after = f.ram.page_ptr(0x00) ? f.ram.page_ptr(0x00)[0] : 0;
        check("FIX-L2-ROM-AREA-02",
              "L2 write with bank=0x70 → sram_active=0 → write dropped "
              "(NOT corrupting ROM area) — VHDL :2971 + :3101-3102; commit 9d252b6",
              after == 0x55,
              fmt("ram[0][0] post-write=0x%02X (exp 0x55 — write dropped; "
                  "pre-fix would have stored 0xAB at ROM-in-SRAM page 0 "
                  "via to_sram_page(0xE0)=0x00 wrap)", after));
    }
}

// FIX-CONTEND-NR03-01 — commit f5ec6d8 (verify9) A3: NR 0x03 runtime
// machine-timing commit did not rebuild ContentionModel LUT.
// ContentionModel::rebuild_for_type() refreshes type + LUT without
// touching dynamic gate state. This regression test is in
// contention_test.cpp instead — see the same name added there.

// FIX-CONTEND-7FFD-01 — commit f5ec6d8 (verify9) A4: port_7ffd_active
// OR-term in port_contend(). This regression test is in
// contention_test.cpp — see the same name added there.

// FIX-MEMACTIVE-PAGE-01 — commit a9cbf79 (verify6): mem_active_page_for()
// in z80_cpu.cpp passed the physical ROM page (sram_rom*2+slot) instead of
// MMU<i> register's 0xFF sentinel that VHDL feeds (zxnext.vhd:2949-2956).
// The fix uses Mmu::get_page (= nr_mmu_[slot]). This is a CPU-side fix;
// the regression test is in contention_test.cpp at the integration level.

// V11-MEM-01 — Verify11-memory class-(c) finding: NR $50/$51 with v ∈
// [$E0..$FE] left `slots_[slot]` carrying the verbatim NR-write value
// (e.g. 0xE5) while `read_only_[slot]=true`. This dual-semantics state
// (= "MMU register value" when read_only_=false; = "physical SRAM page"
// when read_only_=true) was inconsistent for the NR-driven slot 0/1
// high-page case. After save_state + load_state the rebuild_ptr first
// branch (`if (read_only_[slot])`) consumes `slots_[slot]` as the
// physical page and dispatches reads to the wrong SRAM region (e.g.
// `ram_.page_ptr(0xE5)` instead of the legacy-ROM page
// `ram_.page_ptr(sram_rom*2+slot)`). VHDL zxnext.vhd:3037-3057 always
// falls into the legacy ROM branch (`sram_pre_A21_A13 <= "000000" &
// sram_rom & cpu_a(13)`) for `mmu_A21_A13(8)='1'` slot 0/1 access; the
// physical SRAM page driven on the bus is `sram_rom*2+slot`. Fix:
// rebuild_ptr's `page >= 0xE0` slot 0/1 branch now stores `rom_page`
// (the actual physical page) into `slots_[slot]` so the array semantics
// stay consistent. `nr_mmu_[slot]` keeps the verbatim NR-write value
// for the read-back at zxnext.vhd:6075-6082.
void test_cat27_v11_mem_01_save_load_high_page() {
    set_group("Cat27 V11-MEM-01 NR $50/$51 high-page slots_[] consistency");

    // V11-MEM-01-A: discriminative — after `set_page(0, 0xE5)` followed by
    // save_state + load_state, the loaded Mmu must read from the legacy
    // ROM page (sram_rom*2+slot=0 for fresh ZXN_ISSUE2 with port_7ffd=0,
    // port_1ffd=0), NOT from RAM page 0xE5.
    //
    // Sentinel layout (Next mode, rom_in_sram_=true):
    //   - RAM page 0x00 (the legacy-ROM-derived target after the fix) ← 0x33
    //   - RAM page 0xE5 (the pre-fix wrong target) ← 0x77
    // Pre-fix: load_state's rebuild_ptr first branch reads
    //          ram.page_ptr(slots_[0]=0xE5)[0] = 0x77.
    // Post-fix: slots_[0] was rewritten to rom_page=0 inside rebuild_ptr's
    //           >=0xE0 branch at set_page time, so save+load round-trips
    //           slots_[0]=0; rebuild_ptr's first branch reads
    //           ram.page_ptr(0)[0] = 0x33.
    {
        Fixture src;
        src.fresh();
        src.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        src.mmu.set_rom_in_sram(true);  // Next mode (rom served from SRAM)
        // Seed the two distinguishing pages.
        if (src.ram.page_ptr(0x00)) src.ram.page_ptr(0x00)[0] = 0x33;  // legacy-ROM target
        if (src.ram.page_ptr(0xE5)) src.ram.page_ptr(0xE5)[0] = 0x77;  // pre-fix wrong target
        src.mmu.set_page(0, 0xE5);
        // Sanity (runtime path is already correct per FIX-SLOT01-HIPAGE-01).
        const uint8_t pre = src.mmu.read(0x0000);

        // Save.
        StateWriter measure;
        src.mmu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter writer(buf.data(), buf.size());
        src.mmu.save_state(writer);

        // Load into a fresh Mmu, with the same RAM seed AND machine state
        // (rom_in_sram_ comes through save/load).
        Fixture dst;
        dst.fresh();
        dst.mmu.set_machine_type(MachineType::ZXN_ISSUE2);
        if (dst.ram.page_ptr(0x00)) dst.ram.page_ptr(0x00)[0] = 0x33;
        if (dst.ram.page_ptr(0xE5)) dst.ram.page_ptr(0xE5)[0] = 0x77;
        StateReader reader(buf.data(), buf.size());
        dst.mmu.load_state(reader);

        const uint8_t post = dst.mmu.read(0x0000);
        const uint8_t nr_rb = dst.mmu.get_page(0);

        check("V11-MEM-01-A",
              "NR $50=0xE5 + save_state + load_state: rebuild_ptr serves "
              "legacy ROM (sram_rom*2+slot=0) via consistent slots_[] — "
              "VHDL zxnext.vhd:3037-3057 :3052; verify11-memory",
              pre == 0x33 && post == 0x33 && nr_rb == 0xE5,
              fmt("pre-save read=0x%02X (exp 0x33), "
                  "post-load read=0x%02X (exp 0x33; pre-fix=0x77 from "
                  "wrong RAM page 0xE5), "
                  "nr_mmu_[0]=0x%02X (exp 0xE5 verbatim)",
                  pre, post, nr_rb));
    }
}

} // namespace (Cat27 testcov-memory)
} // namespace (top-level anon)

// ── main ─────────────────────────────────────────────────────────────


// ---------------------------------------------------------------------------
// Cat28 — dedicated bank-7 BRAM (NextZXOS-boot fix, 2026-07-10)
//
// VHDL zxnext.vhd:2962 marks MMU page 0x0E (16K bank 7 lower half) as a
// dedicated dual-port BRAM (mem_active_bank7; sram_pre_active forced 0 at
// :3039-3041/:3061; the BRAM is `bank7_ram: entity work.dpram2` at :6670,
// 13-bit address = 8K, physically separate from external SRAM). jnext
// serves MMU page 0x0E from Mmu::bank7_bram_ — its own buffer.
//
// History: the original bug aliased the BRAM onto physical SRAM page 0x0E
// (the alt-ROM upper half) — NextZXOS's alt-ROM install clobbered its own
// MMU-page-14 workspace ($DA35 saved SP → RET-to-$0000 boot trap). An
// interim fix relocated it to SRAM page 0x2E, refuted in review: the
// config-mode NR $04 window reaches EVERY SRAM page ($04=$17 → 0x2E/0x2F,
// zxnext.vhd:3044-3050 with sram_pre_bank7 forced '0'), so no page is
// dead. Hence the dedicated buffer.
// ---------------------------------------------------------------------------

void test_cat28_bank7_bram_standin() {
    set_group("Cat28 dedicated bank-7 BRAM");

    // BANK7-01: MMU page 0x0E is served from the dedicated BRAM buffer —
    // neither phys page 0x0E (alt-ROM) nor phys page 0x2E sees the write.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(6, 0x0E);
        f.mmu.write(0xC000, 0xA5);
        const uint8_t in_bram = f.mmu.bank7_bram()[0];
        const uint8_t at_0e = f.ram.page_ptr(0x0E)[0];
        const uint8_t at_2e = f.ram.page_ptr(0x2E)[0];
        check("BANK7-01",
              "MMU page 0x0E lands in the dedicated BRAM buffer, not in any "
              "SRAM page — VHDL zxnext.vhd:2962+6670",
              in_bram == 0xA5 && at_0e != 0xA5 && at_2e != 0xA5,
              fmt("bram[0]=0x%02X ram[0x0E][0]=0x%02X ram[0x2E][0]=0x%02X",
                  in_bram, at_0e, at_2e));
    }

    // BANK7-02 (the NextZXOS boot regression): a direct write to physical
    // page 0x0E (the alt-ROM install path) must NOT be visible through an
    // MMU page-0x0E mapping. Pre-fix this exact aliasing clobbered
    // NextZXOS's saved-SP variable at logical $DA35 (offset 0x1A35) and
    // the boot died in a RET-to-$0000 sentinel trap.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(6, 0x0E);
        f.mmu.write(0xDA35, 0xF5);                    // NextZXOS: LD ($DA35),SP lo
        f.mmu.write(0xDA36, 0x5B);                    //                       hi
        f.ram.page_ptr(0x0E)[0x1A35] = 0x2F;          // alt-ROM install bytes
        f.ram.page_ptr(0x0E)[0x1A36] = 0x5F;
        const uint8_t lo = f.mmu.read(0xDA35);
        const uint8_t hi = f.mmu.read(0xDA36);
        check("BANK7-02",
              "alt-ROM write to phys page 0x0E does not corrupt MMU-page-0x0E "
              "workspace (the $DA35 saved-SP NextZXOS boot killer)",
              lo == 0xF5 && hi == 0x5B,
              fmt("read back $DA35=0x%02X $DA36=0x%02X (expected F5 5B)", lo, hi));
    }

    // BANK7-03 (rewritten, Task 25 2026-07-10): bank 5 (pages 0x0A/0x0B)
    // is a dedicated 16K dual-port VRAM (VHDL bank5_ram dpram2,
    // zxnext.vhd:6558-6578; mem_active_bank5 gate :2961 + sram_pre_active
    // suppression :3037-3041/:3059-3063) — MMU-mapped CPU writes land in
    // the bank5_vram_ buffer, NOT in external SRAM page 0x0A (the old
    // aliased model this row previously asserted, refuted by the Task 23
    // mid-boot-garbage root cause) nor 0x2A (the raw formula value).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(6, 0x0A);
        f.mmu.write(0xC000, 0x3C);
        const uint8_t in_vram = f.mmu.bank5_vram()[0];
        const uint8_t at_0a = f.ram.page_ptr(0x0A)[0];
        const uint8_t at_2a = f.ram.page_ptr(0x2A)[0];
        check("BANK7-03",
              "MMU page 0x0A lands in the dedicated bank-5 VRAM, not in any "
              "SRAM page — VHDL zxnext.vhd:2961+6558",
              in_vram == 0x3C && at_0a != 0x3C && at_2a != 0x3C,
              fmt("vram[0]=0x%02X ram[0x0A][0]=0x%02X ram[0x2A][0]=0x%02X",
                  in_vram, at_0a, at_2a));
    }

    // BANK7-04 (review finding, 2026-07-10): the REAL config-mode NR $04
    // path. NR $04 = $17 in config mode addresses SRAM pages 0x2E/0x2F
    // (zxnext.vhd:3044-3050, sram_pre_bank7 forced '0' — external SRAM,
    // not the BRAM). Writes through that window must not corrupt the
    // bank-7 BRAM content, and vice versa. This exercises the genuine
    // write path (set_config_mode + set_nr_04_romram_bank + mmu.write on
    // a ROM-mapped slot), not a raw page_ptr poke.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(6, 0x0E);
        f.mmu.write(0xC123, 0x77);                    // seed BRAM via MMU page
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x17);
        f.mmu.write(0x0123, 0x99);                    // config window, slot 0 → SRAM 0x2E
        const uint8_t sram_2e = f.ram.page_ptr(0x2E)[0x0123];
        const uint8_t bram    = f.mmu.bank7_bram()[0x0123];
        const uint8_t via_mmu = f.mmu.read(0xC123);
        check("BANK7-04",
              "config-mode NR $04=$17 window writes SRAM page 0x2E without "
              "touching the bank-7 BRAM — VHDL zxnext.vhd:3044-3050",
              sram_2e == 0x99 && bram == 0x77 && via_mmu == 0x77,
              fmt("sram[0x2E][0x123]=0x%02X bram[0x123]=0x%02X mmu_read=0x%02X",
                  sram_2e, bram, via_mmu));
    }

    // BANK7-05 (review REJECT-round fix, 2026-07-10): standalone legacy
    // machines (rom_in_sram_=false — the Fixture default) keep bank 7 as
    // ORDINARY flat RAM at pages 0x0E/0x0F. The first dedicated-buffer
    // commit redirected page 0x0E unconditionally, silently splitting a
    // standalone-128K program's 16K bank 7 across two disjoint memories
    // (lower 8K in the BRAM buffer, upper 8K in real RAM). Discriminative:
    // write through the real 128K paging port path and assert CONTENT
    // lands in ram_ page 0x0E (the pre-existing map_128k_bank rows only
    // asserted get_page(), which is blind to this).
    {
        Fixture f;
        f.fresh();                          // rom_in_sram_ = false
        f.mmu.map_128k_bank(0x07);          // port 0x7FFD bank 7
        f.mmu.write(0xC000, 0xAB);          // lower 8K of bank 7
        f.mmu.write(0xE000, 0xCD);          // upper 8K of bank 7
        const uint8_t lo   = f.ram.page_ptr(0x0E)[0];
        const uint8_t hi   = f.ram.page_ptr(0x0F)[0];
        const uint8_t bram = f.mmu.bank7_bram()[0];
        check("BANK7-05",
              "standalone-machine (rom_in_sram=false) bank-7 writes land in "
              "flat RAM pages 0x0E/0x0F, NOT the Next-only BRAM buffer",
              lo == 0xAB && hi == 0xCD && bram != 0xAB,
              fmt("ram[0x0E][0]=0x%02X ram[0x0F][0]=0x%02X bram[0]=0x%02X",
                  lo, hi, bram));
    }

    // ─── Task 25 (2026-07-10): dedicated bank-5 16K VRAM ──────────────
    //
    // VHDL bank5_ram dpram2 (zxnext.vhd:6558-6578), mem_active_bank5
    // decode (:2961), sram_pre_active suppression (:3037-3041/:3059-3063).
    // Root cause of the Task 23 NextZXOS mid-boot garbage: pre-fix,
    // logical bank 5 aliased onto external SRAM pages 0x0A/0x0B, which
    // the config-mode NR $04=$05 window legally writes (tbblue.fw loading
    // enNextMf.rom into SRAM bank 5 — the Multiface area hard-wired at
    // zxnext.vhd:3029-3036) — so the load painted the visible screen.

    // BANK5-01: both 8K halves (pages 0x0A and 0x0B) map into the single
    // 16K buffer at the right offsets (VHDL addr_width_g => 14 — one
    // contiguous 16K, page bit 0 = BRAM A13).
    {
        Fixture f;
        f.fresh();
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(6, 0x0A);
        f.mmu.set_page(7, 0x0B);
        f.mmu.write(0xC000, 0x11);                    // page 0x0A → offset 0x0000
        f.mmu.write(0xE000, 0x22);                    // page 0x0B → offset 0x2000
        const uint8_t lo = f.mmu.bank5_vram()[0x0000];
        const uint8_t hi = f.mmu.bank5_vram()[0x2000];
        const uint8_t rd_lo = f.mmu.read(0xC000);
        const uint8_t rd_hi = f.mmu.read(0xE000);
        check("BANK5-01",
              "pages 0x0A/0x0B are the lower/upper 8K halves of the single "
              "16K bank-5 VRAM — VHDL zxnext.vhd:6558 (addr_width 14)",
              lo == 0x11 && hi == 0x22 && rd_lo == 0x11 && rd_hi == 0x22,
              fmt("vram[0]=0x%02X vram[0x2000]=0x%02X read=0x%02X/0x%02X",
                  lo, hi, rd_lo, rd_hi));
    }

    // BANK5-02 (the Task 23 regression): the config-mode NR $04 = $05
    // window addresses EXTERNAL SRAM pages 0x0A/0x0B (zxnext.vhd:3044-3050
    // — sram_pre_bank5 forced '0'). Writes through that window (tbblue.fw
    // loading enNextMf.rom) must NOT touch the bank-5 VRAM (the visible
    // screen), and vice versa.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_page(6, 0x0A);
        f.mmu.write(0xC456, 0x55);                    // seed screen via MMU page
        f.mmu.set_config_mode(true);
        f.mmu.set_nr_04_romram_bank(0x05);
        f.mmu.write(0x0456, 0x99);                    // config window, slot 0 → SRAM 0x0A
        const uint8_t sram_0a = f.ram.page_ptr(0x0A)[0x0456];
        const uint8_t vram    = f.mmu.bank5_vram()[0x0456];
        const uint8_t via_mmu = f.mmu.read(0xC456);
        check("BANK5-02",
              "config-mode NR $04=$05 window writes SRAM page 0x0A without "
              "touching the bank-5 VRAM (the NextZXOS mid-boot-garbage "
              "killer) — VHDL zxnext.vhd:3044-3050",
              sram_0a == 0x99 && vram == 0x55 && via_mmu == 0x55,
              fmt("sram[0x0A][0x456]=0x%02X vram[0x456]=0x%02X mmu_read=0x%02X",
                  sram_0a, vram, via_mmu));
    }

    // BANK5-03: standalone legacy machines (rom_in_sram=false) keep bank 5
    // as ordinary flat RAM at pages 0x0A/0x0B — mirrors BANK7-05's
    // REJECT-round lesson. Bank 5 is always CPU-visible at 0x4000-0x7FFF.
    {
        Fixture f;
        f.fresh();                          // rom_in_sram_ = false
        f.mmu.write(0x4000, 0xAB);          // lower 8K of bank 5
        f.mmu.write(0x6000, 0xCD);          // upper 8K of bank 5
        const uint8_t lo   = f.ram.page_ptr(0x0A)[0];
        const uint8_t hi   = f.ram.page_ptr(0x0B)[0];
        const uint8_t vram = f.mmu.bank5_vram()[0];
        check("BANK5-03",
              "standalone-machine (rom_in_sram=false) bank-5 writes land in "
              "flat RAM pages 0x0A/0x0B, NOT the Next-only VRAM buffer",
              lo == 0xAB && hi == 0xCD && vram != 0xAB,
              fmt("ram[0x0A][0]=0x%02X ram[0x0B][0]=0x%02X vram[0]=0x%02X",
                  lo, hi, vram));
    }

    // BANK5-04 (Task 25 bullet 1): the CPU Layer 2 window computes
    // layer2_A21_A13 with the same UNCONDITIONAL formula as mmu_A21_A13
    // (zxnext.vhd:2966-2971) and the arbiter forces sram_bank5 low for it
    // (:3100-3107) — L2 bank 5 addresses external SRAM pages 0x2A/0x2B
    // (ZX RAM bank 5), never physical 0x0A/0x0B and never the VRAM. The
    // pre-fix to_sram_page bank-5 bypass routed this write onto the
    // visible screen.
    {
        Fixture f;
        f.fresh();
        f.mmu.set_rom_in_sram(true);
        f.mmu.set_l2_port(0x01, 5);         // L2 write-over, seg=00, bank 5
        f.mmu.write(0x0000, 0x77);          // L2 window low half, sub-page 0
        f.mmu.set_l2_port(0x00, 5);
        const uint8_t at_2a  = f.ram.page_ptr(0x2A)[0];
        const uint8_t at_0a  = f.ram.page_ptr(0x0A)[0];
        const uint8_t vram   = f.mmu.bank5_vram()[0];
        check("BANK5-04",
              "CPU L2 window with bank 5 writes SRAM page 0x2A (unconditional "
              "layer2_A21_A13 formula), not page 0x0A and not the VRAM — "
              "VHDL zxnext.vhd:2966-2971 + 3100-3107",
              at_2a == 0x77 && at_0a != 0x77 && vram != 0x77,
              fmt("ram[0x2A][0]=0x%02X ram[0x0A][0]=0x%02X vram[0]=0x%02X",
                  at_2a, at_0a, vram));
    }
}

int main() {
    std::printf("MMU/Memory Compliance Test Runner (Phase 2 rewrite)\n");
    std::printf("====================================================\n\n");

    test_cat1_slot_assignment();
    test_cat2_reset_state();
    test_cat3_port_7ffd();
    test_cat4_port_dffd();
    test_cat5_port_1ffd();
    test_cat6_plus3_special();
    test_cat7_paging_lock();
    test_cat8_nr_8e();
    test_cat9_nr_8f();
    test_cat10_port_eff7();
    test_cat11_rom_selection();
    test_cat11b_effective_rom_page_post_port_write();
    test_cat11c_machine_type_preserves_ram_slots();
    test_cat12_altrom();
    test_cat13_config_mode();
    test_cat14_addr_translation();
    test_cat15_bank57();
    // Cat-16 (memory contention) migrated to test/contention/contention_test.cpp
    // per the §15 C2-move commitment in CONTENTION-TEST-PLAN-DESIGN.md.
    test_cat17_l2_mapping();
    test_cat18_priority();
    test_boot_rom_overlay();
    test_soundrive_paging_conflict();
    test_nex_loader();
    test_boot_format_loaders();
    test_cat21_nirvana_multiplex();
    test_cat3bis_shadow_screen();
    test_cat26_sram_pre_override();
    // Cat 27 — testcov-memory regression rows for Task-2 verify1..verify10
    // memory-subsystem fixes. See doc/issues/nextzxos-boot/
    // NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY.md for the coverage matrix.
    test_cat27_nr5x_ff_per_slot();
    test_cat27_plus3_special_arbitration();
    test_cat27_nr8c_cache_and_high_page();
    test_cat27_unlock_clears_bit5();
    test_cat27_nr8c_preserves_ram_slots();
    test_cat27_eff7_nr5x_ff();
    test_cat27_nrmmu_save_roundtrip();
    test_cat27_nr12_propagation();
    test_cat27_reset_bootrom_config_mode_gate();
    test_cat27_machine_type_during_special();
    test_cat27_currentsramrom_128k_altrom_lock();
    test_cat27_l2_overlay_lowhalf();
    test_cat27_l2_rom_area_gate();
    test_cat27_v11_mem_01_save_load_high_page();
    test_cat28_bank7_bram_standin();

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-36s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-36s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows (%zu):\n", g_skipped.size());
        for (const auto& s : g_skipped)
            std::printf("  SKIP %-8s — %s\n", s.id, s.reason);
    }

    return g_fail > 0 ? 1 : 0;
}
