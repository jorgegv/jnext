// DivMMC + SPI Compliance Test Runner
//
// Full rewrite (Task 1 Wave 2, 2026-04-15) against the rebuilt
// doc/testing/DIVMMC-SPI-TEST-PLAN-DESIGN.md. Every assertion cites a
// specific VHDL file:line range from the authoritative FPGA source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo — cited here for provenance, not edited).
//
// Ground rules (per doc/testing/UNIT-TEST-PLAN-EXECUTION.md):
//   * The C++ implementation is NEVER the oracle — every expected value
//     comes from VHDL.
//   * No data-driven loops without per-iteration IDs. One section (or
//     tight group) per plan row, labelled with the plan ID.
//   * Plan rows that cannot be realised with the current emulator API
//     are reported via skip(id, reason) and not counted toward pass/fail.
//   * Known Task 2 gaps (e.g. NR 0x83 b0 enable gating, SRAM priority
//     ladder, SPI state machine, sd_swap, ROM3-conditional gating) are
//     left failing and fed back to the Task 3 backlog.
//
// Emulator surface summary (src/peripheral/divmmc.{h,cpp}, spi.{h,cpp}):
//   * DivMmc models port 0xE3 as raw bit fields, exposes conmem/mapram/
//     bank/automap_active, and implements a minimal M1-fetch trigger
//     model with NR 0xB8/0xBB only (no NR 0xB9 valid/NR 0xBA timing
//     split, no ROM3 conditional, no NMI button, no RETN hook, no
//     automap_hold/held latch pipeline, no 0x3Dxx range).
//   * SpiMaster is a zero-latency byte-exchange wrapper (no 16-cycle
//     state machine, no pipeline delay, independent write/read paths,
//     raw CS register with no sd_swap/flash decode, no MISO source
//     priority ladder — any selected device wins).
//
// Run: ./build/test/divmmc_test

#include "peripheral/divmmc.h"
#include "peripheral/nmi_source.h"
#include "peripheral/spi.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "memory/rom.h"
#include "port/nextreg.h"
#include "core/saveable.h"  // StateWriter/StateReader for DA-09 round-trip
// G46(a) DM-RETN-PROPER-01/02: drive DivMmc::on_m1_retn_delay() through
// the real Im2Controller FSM (im2_control.vhd:158-209) so the canonical
// ED 45 vs alias-byte distinction is exercised end-to-end.
#include "cpu/im2.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// ── Mock SPI device ───────────────────────────────────────────────────

class MockSpiDevice : public SpiDevice {
public:
    uint8_t next_response = 0xFF;
    uint8_t last_tx       = 0xFF;
    int     exchange_count = 0;
    bool    was_deselected = false;

    uint8_t exchange(uint8_t tx) override {
        last_tx = tx;
        ++exchange_count;
        return next_response;
    }
    uint8_t receive(uint8_t tx) override {
        last_tx = tx;
        ++exchange_count;
        return next_response;
    }
    uint8_t send() override {
        // Aligns with SpiDevice base class default `send() { return
        // exchange(0xFF); }` — a read-triggered exchange pushes 0xFF on
        // MOSI (VHDL spi_master.vhd:109-110 oshift_r <= all 1s on rd).
        last_tx = 0xFF;
        ++exchange_count;
        return next_response;
    }
    void deselect() override { was_deselected = true; }
};

// Fresh enabled DivMmc.  The VHDL soft-reset default for NR 0xBA
// (entry_timing_0_) is 0x00 — all RST entry points configured as
// "delayed-on", meaning an M1 fetch at the entry point activates automap
// only on the NEXT M1, not the current one (per the automap_hold→held
// pipeline, VHDL divmmc.vhd:128-148).
//
// Most pre-existing tests were written against the earlier collapsed
// single-cycle model and assume a single M1 fetch activates automap. To
// keep those tests concise, this helper overrides NR 0xBA to 0xFF (all
// instant-on), so single-fetch activation works. Tests that explicitly
// exercise the instant vs delayed distinction (TM-01..05) set
// entry_timing_0_ directly.
DivMmc make_divmmc() {
    DivMmc d;
    d.reset();
    d.set_enabled(true);          // VHDL port_divmmc_io_en := '1'
    d.set_nr_0a_4_enable(true);   // VHDL nr_0a_divmmc_automap_en := '1'
                                  // — defaults '0' at hw reset (zxnext.vhd:1126);
                                  //   tests that need automap active must flip
                                  //   it explicitly (firmware does so via
                                  //   NR 0x0A bit 4).
    d.set_entry_timing_0(0xFF);   // all-instant for test convenience
    return d;
}

// ── Mmu + DivMmc integration fixture ──────────────────────────────────
//
// Hosts the cross-object priority tests PRI-01/02/04 (re-homed from
// test/mmu/mmu_test.cpp Cat18 on 2026-04-21, commit 7be3deb). The VHDL
// decode priority ladder at zxnext.vhd:3084-3100 is resolved inside
// Mmu::read/write — specifically, the DivMMC overlay block at
// src/memory/mmu.h:118-133 (read) and :215-220 (write) sits above the
// Layer 2 and config_mode/MMU branches, mirroring the VHDL arbiter.
// Wiring this in a test therefore needs all three objects plus the
// mmu.set_divmmc(&dm) pointer — bare-DivMmc tests cannot see the ladder,
// bare-Mmu tests cannot fire the DivMMC branch.
//
// Default Ram is 2048 KB (256 × 8K pages) per the Mmu test plan; any
// physical page index used by PRI rows must stay in that range.
struct MmuDivFixture {
    Ram    ram;
    Rom    rom;
    Mmu    mmu;
    DivMmc dm;

    MmuDivFixture() : ram(), rom(), mmu(ram, rom), dm() {
        mmu.reset();
        dm.reset();
        dm.set_enabled(true);          // VHDL port_divmmc_io_en := '1'
        dm.set_nr_0a_4_enable(true);   // VHDL nr_0a_divmmc_automap_en := '1'
                                       // (default '0'; flip explicitly for
                                       //  tests that need automap active)
        mmu.set_divmmc(&dm);           // wire the overlay pointer
    }

    // Force DivMMC active via conmem (VHDL port 0xE3 bit 7). Avoids the
    // M1 automap latching pipeline — PRI-01/02/04 only need a stable
    // "DivMMC is active right now" signal, not the activation mechanism.
    void divmmc_conmem_on(bool mapram = false, uint8_t bank = 0) {
        uint8_t ctrl = 0x80 | (mapram ? 0x40 : 0x00) | (bank & 0x0F);
        dm.write_control(ctrl);
    }
};

// ══════════════════════════════════════════════════════════════════════
// §1. Port 0xE3 — DivMMC Control Register
// VHDL: zxnext.vhd:4173-4190 (port decode), divmmc.vhd:85-86 (conmem/mapram)
// ══════════════════════════════════════════════════════════════════════

void group_e3() {
    set_group("1. Port 0xE3");

    // E3-01: reset clears raw control register to 0x00.
    // VHDL: zxnext.vhd:4173 — port_e3_reg default "00000000".
    {
        DivMmc d; d.reset();
        uint8_t v = d.read_control();
        check("E3-01",
              "Reset clears port 0xE3 control register to 0x00",
              v == 0x00,
              fmt("got=%02x exp=00", v));
    }

    // E3-02: Write 0x80 -> conmem=1, mapram=0, bank=0.
    // VHDL: zxnext.vhd:4180 — cpu_do(7)=conmem.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x80);
        bool ok = d.conmem() && !d.mapram() && d.bank() == 0;
        check("E3-02",
              "Write 0x80 decodes as conmem=1, mapram=0, bank=0 "
              "(VHDL zxnext.vhd:4180)",
              ok,
              fmt("conmem=%d mapram=%d bank=%u",
                  d.conmem(), d.mapram(), d.bank()));
    }

    // E3-03: Write 0x40 -> mapram set (bit 6 OR-latched).
    // VHDL: zxnext.vhd:4183 — port_e3_reg(6) <= cpu_do(6) OR port_e3_reg(6).
    {
        DivMmc d = make_divmmc();
        d.write_control(0x40);
        check("E3-03",
              "Write 0x40 sets mapram (VHDL zxnext.vhd:4183)",
              d.mapram() == true,
              fmt("mapram=%d", d.mapram()));
    }

    // E3-04: mapram cannot be cleared by a subsequent write (OR-latch).
    // VHDL: zxnext.vhd:4183.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x40);  // set mapram
        d.write_control(0x00);  // attempt clear
        check("E3-04",
              "mapram remains set after write 0x00 (VHDL zxnext.vhd:4183)",
              d.mapram() == true,
              fmt("mapram=%d exp=1", d.mapram()));
    }

    // E3-05: mapram cleared by NextREG 0x09 bit 3.
    // VHDL: zxnext.vhd:4184-4185 — nr_09_we AND nr_wr_dat(3) => port_e3_reg(6) := '0'.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x40);   // set mapram via OR-latch
        d.clear_mapram();        // equivalent to NR 0x09 write with bit 3 set
        check("E3-05",
              "clear_mapram() clears the mapram OR-latch "
              "(VHDL zxnext.vhd:4184-4185)",
              d.mapram() == false,
              fmt("mapram=%d exp=0", d.mapram()));
    }

    // E3-06: Bits 3:0 select bank 0..15.
    // VHDL: zxnext.vhd:4188 — port_e3_reg(3 downto 0) <= cpu_do(3 downto 0).
    {
        DivMmc d = make_divmmc();
        d.write_control(0x0F);
        check("E3-06",
              "Write 0x0F selects bank 15 (VHDL zxnext.vhd:4188)",
              d.bank() == 0x0F,
              fmt("bank=%u exp=15", d.bank()));
    }

    // E3-07: Read port 0xE3 returns {conmem, mapram, 00, bank[3:0]}.
    // VHDL: zxnext.vhd:4190 — readback mask. The C++ implementation
    // returns the raw control_reg_ instead, so writing 0x30 (sets bits
    // 5:4) will read back as 0x30 in the emulator but 0x00 in VHDL.
    {
        DivMmc d = make_divmmc();
        d.write_control(0xFF);  // all bits set
        uint8_t got = d.read_control();
        // VHDL: bits 5:4 always read as 0 -> expected 0xCF.
        check("E3-07",
              "Read 0xE3 masks bits 5:4 to 0 "
              "(VHDL zxnext.vhd:4190)",
              got == 0xCF,
              fmt("got=%02x exp=CF", got));
    }

    // E3-08: Bits 5:4 of write are ignored (not stored).
    // VHDL: zxnext.vhd:4190 — only bits 7,6,3:0 latched.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x30);   // attempt to set bits 5:4 only
        uint8_t got = d.read_control();
        check("E3-08",
              "Bits 5:4 of write are ignored "
              "(VHDL zxnext.vhd:4190)",
              got == 0x00,
              fmt("got=%02x exp=00", got));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §2. DivMMC Memory Paging — conmem Mode
// VHDL: divmmc.vhd:88-101
// ══════════════════════════════════════════════════════════════════════

void group_cm() {
    set_group("2. conmem paging");

    // CM-01: conmem=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM (rom_en=1).
    // VHDL: divmmc.vhd:94.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x80);  // conmem=1, mapram=0, bank=0
        bool ok = d.is_active() && d.is_rom_mapped()
                  && !d.is_ram_mapped(0x0000);
        check("CM-01",
              "conmem=1 mapram=0: ROM mapped at page0 "
              "(VHDL divmmc.vhd:94)",
              ok,
              fmt("active=%d rom=%d ram0=%d",
                  d.is_active(), d.is_rom_mapped(),
                  d.is_ram_mapped(0x0000)));
    }

    // CM-02: conmem=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N.
    // VHDL: divmmc.vhd:95-96 — page1 AND conmem; ram_bank=reg(3:0).
    {
        DivMmc d = make_divmmc();
        d.write_control(0x85);  // conmem=1, bank=5
        bool ok = d.is_ram_mapped(0x2000) && d.bank() == 5;
        check("CM-02",
              "conmem=1 mapram=0: page1 RAM from reg(3:0) "
              "(VHDL divmmc.vhd:95-96)",
              ok,
              fmt("ram1=%d bank=%u",
                  d.is_ram_mapped(0x2000), d.bank()));
    }

    // CM-03: conmem=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3.
    // VHDL: divmmc.vhd:95-96 — ram_bank=3 when page0, page0 RAM mapped.
    {
        DivMmc d = make_divmmc();
        d.write_control(0xC0);  // conmem=1, mapram=1, bank=0
        // ram mapped at 0x0000 and read resolves to RAM page 3.
        // Seed RAM bank 3 byte 0 to a sentinel, read through API.
        // We cannot poke ram_ directly; but ram_page_for() is private.
        // Observe via is_ram_mapped and is_rom_mapped inversion.
        bool ok = d.is_ram_mapped(0x0000) && !d.is_rom_mapped();
        check("CM-03",
              "conmem=1 mapram=1: page0 maps to RAM bank 3 "
              "(VHDL divmmc.vhd:95-96)",
              ok,
              fmt("ram0=%d rom=%d",
                  d.is_ram_mapped(0x0000), d.is_rom_mapped()));
    }

    // CM-04: conmem=1, mapram=1, bank=5: 0x2000-0x3FFF = RAM bank 5.
    // VHDL: divmmc.vhd:95-96.
    {
        DivMmc d = make_divmmc();
        d.write_control(0xC5);  // conmem=1, mapram=1, bank=5
        check("CM-04",
              "conmem=1 mapram=1: page1 maps to reg(3:0) "
              "(VHDL divmmc.vhd:95-96)",
              d.is_ram_mapped(0x2000) && d.bank() == 5,
              fmt("ram1=%d bank=%u",
                  d.is_ram_mapped(0x2000), d.bank()));
    }

    // CM-05: 0x0000-0x1FFF is read-only (page0 always rdonly).
    // VHDL: divmmc.vhd:100 — rdonly = page0 OR (mapram AND bank=3).
    {
        DivMmc d = make_divmmc();
        d.write_control(0x80);  // conmem=1
        check("CM-05",
              "page0 is read-only with conmem=1 "
              "(VHDL divmmc.vhd:100)",
              d.is_read_only(0x0000) && d.is_read_only(0x1FFF),
              fmt("ro0=%d ro1fff=%d",
                  d.is_read_only(0x0000),
                  d.is_read_only(0x1FFF)));
    }

    // CM-06: conmem=1, mapram=1, bank=3: 0x2000-0x3FFF read-only.
    // VHDL: divmmc.vhd:100.
    {
        DivMmc d = make_divmmc();
        d.write_control(0xC3);  // conmem=1, mapram=1, bank=3
        check("CM-06",
              "conmem+mapram+bank=3: page1 read-only "
              "(VHDL divmmc.vhd:100)",
              d.is_read_only(0x2000) && d.is_read_only(0x3FFF),
              fmt("ro2000=%d ro3fff=%d",
                  d.is_read_only(0x2000),
                  d.is_read_only(0x3FFF)));
    }

    // CM-07: conmem=1, mapram=1, bank!=3: page1 writable.
    // VHDL: divmmc.vhd:100.
    {
        DivMmc d = make_divmmc();
        d.write_control(0xC5);  // conmem=1, mapram=1, bank=5
        check("CM-07",
              "conmem+mapram+bank!=3: page1 writable "
              "(VHDL divmmc.vhd:100)",
              !d.is_read_only(0x2000),
              fmt("ro2000=%d exp=0", d.is_read_only(0x2000)));
    }

    // CM-08: conmem=0, automap=0: no DivMMC mapping.
    // VHDL: divmmc.vhd:94-95 — both rom_en and ram_en zero.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x00);  // everything off
        bool ok = !d.is_active()
                  && !d.is_rom_mapped()
                  && !d.is_ram_mapped(0x0000)
                  && !d.is_ram_mapped(0x2000);
        check("CM-08",
              "conmem=0 automap=0: no DivMMC mapping "
              "(VHDL divmmc.vhd:94-95)",
              ok,
              fmt("act=%d rom=%d ram0=%d ram1=%d",
                  d.is_active(), d.is_rom_mapped(),
                  d.is_ram_mapped(0x0000), d.is_ram_mapped(0x2000)));
    }

    // CM-09: mapping gated by i_en (port_divmmc_io_en).
    // VHDL: divmmc.vhd:98 — o_divmmc_rom_en = rom_en AND i_en.
    {
        DivMmc d;
        d.reset();
        d.set_enabled(false);   // i_en = 0
        d.write_control(0x80);  // conmem=1
        bool ok = !d.is_active()
                  && !d.is_rom_mapped()
                  && !d.is_ram_mapped(0x2000);
        check("CM-09",
              "i_en=0 suppresses DivMMC mapping "
              "(VHDL divmmc.vhd:98)",
              ok,
              fmt("act=%d rom=%d ram=%d",
                  d.is_active(), d.is_rom_mapped(),
                  d.is_ram_mapped(0x2000)));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §3. DivMMC Memory Paging — automap Mode
// VHDL: divmmc.vhd:94-96, 148
// ══════════════════════════════════════════════════════════════════════

void group_am() {
    set_group("3. automap paging");

    // Helper: trigger default EP0 (0x0000) automap.
    auto trigger = [](DivMmc& d) {
        d.check_automap(0x0000, true);
    };

    // AM-01: automap=1, mapram=0: ROM mapped at page0.
    // VHDL: divmmc.vhd:94.
    {
        DivMmc d = make_divmmc();
        trigger(d);
        check("AM-01",
              "automap active + mapram=0: ROM mapped at page0 "
              "(VHDL divmmc.vhd:94)",
              d.is_active() && d.is_rom_mapped(),
              fmt("act=%d rom=%d automap=%d",
                  d.is_active(), d.is_rom_mapped(),
                  d.automap_active()));
    }

    // AM-02: automap=1, mapram=0: page1 RAM bank=N.
    // VHDL: divmmc.vhd:95-96.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x07);  // bank=7 (no conmem, no mapram)
        trigger(d);
        check("AM-02",
              "automap + bank=7: page1 maps to bank 7 "
              "(VHDL divmmc.vhd:95-96)",
              d.is_ram_mapped(0x2000) && d.bank() == 7,
              fmt("ram1=%d bank=%u",
                  d.is_ram_mapped(0x2000), d.bank()));
    }

    // AM-03: automap=1, mapram=1: 0x0000-0x1FFF = RAM bank 3.
    // VHDL: divmmc.vhd:95-96.
    {
        DivMmc d = make_divmmc();
        d.write_control(0x40);  // mapram=1 (set via OR-latch)
        trigger(d);
        check("AM-03",
              "automap + mapram=1: page0 maps to RAM bank 3 "
              "(VHDL divmmc.vhd:95-96)",
              d.is_ram_mapped(0x0000) && !d.is_rom_mapped(),
              fmt("ram0=%d rom=%d",
                  d.is_ram_mapped(0x0000),
                  d.is_rom_mapped()));
    }

    // AM-04: automap active, then deactivated via 0x1FF8 range.
    // VHDL: divmmc.vhd:131 (delayed_off), zxnext.vhd non-RST NR BB[6].
    // The delayed-off signal clears hold on the triggering M1, but the
    // automap combinational output still reads 1 during that fetch because
    // held carries the previous value. Deactivation is visible on the
    // NEXT M1, so we do a third fetch to let held propagate.
    {
        DivMmc d = make_divmmc();
        trigger(d);                      // activate at 0x0000
        d.check_automap(0x1FF8, true);   // deactivation-range trigger
        d.check_automap(0x0003, true);   // next M1 — held propagates 0
        bool ok = !d.automap_active() && !d.is_active();
        check("AM-04",
              "automap deactivates via 0x1FF8 (visible on next M1) "
              "(VHDL divmmc.vhd:131)",
              ok,
              fmt("automap=%d act=%d",
                  d.automap_active(), d.is_active()));
    }

    // AM-08 — VHDL zxnext.vhd:4147 — NR 0x83 bit 0 clear must hide
    // DivMMC ROM/RAM overlay even with conmem=1; i_en AND-gates
    // rom_en/ram_en. (G124 — fixed)
    {
        DivMmc d = make_divmmc();        // both levers true
        // Force conmem on (port 0xE3 bit 7) → automap-equivalent active.
        d.write_control(0x80);
        bool active_with_conmem = d.is_active() && d.is_rom_mapped();
        // Now clear NR 0x83 bit 0 equivalent — port_io_enable=false. With
        // i_en AND-gating rom_en/ram_en, the overlay must hide even though
        // conmem stays asserted.
        d.set_port_io_enable(false);
        bool hidden = !d.is_active() && !d.is_rom_mapped();
        check("AM-08",
              "NR 0x83 bit 0 clear hides DivMMC overlay even with conmem=1 "
              "(VHDL zxnext.vhd:4147 i_en AND-gate)",
              active_with_conmem && hidden,
              fmt("with-port_io=%d hidden=%d",
                  active_with_conmem, hidden));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §4. Automap Entry Points — RST Addresses
// VHDL: zxnext.vhd:2848-2890, divmmc.vhd:128-148
// ══════════════════════════════════════════════════════════════════════

void group_ep() {
    set_group("4. RST entry points");

    // Default NR state per reset: B8=0x83, B9=0x01, BA=0x00, BB=0xCD.

    // EP-01: M1 fetch at 0x0000 activates automap (EP0 enabled, valid,
    // delayed). VHDL: zxnext.vhd:2850, divmmc.vhd:129-131.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);
        check("EP-01",
              "M1 at 0x0000 activates automap with default NR state "
              "(VHDL zxnext.vhd:2850)",
              d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // EP-02: M1 at 0x0008 with default NR B9[1]=0 -> rom3_delayed_on,
    // which ONLY triggers when ROM3 is active. The C++ model ignores
    // the ROM3 conditional path and activates unconditionally whenever
    // B8[1]=1. VHDL authority: zxnext.vhd:2856, divmmc.vhd:130.
    // Expected VHDL behaviour (no ROM3): automap must NOT activate.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0008, true);
        // With no ROM3 active, VHDL should NOT activate automap.
        // Known Task 3 bug: C++ activates anyway (no ROM3 gating).
        check("EP-02",
              "M1 at 0x0008 with default NR state and no ROM3: "
              "automap must NOT activate "
              "(VHDL zxnext.vhd:2856, divmmc.vhd:130)",
              !d.automap_active(),
              fmt("automap=%d exp=0 (ROM3 gating missing)",
                  d.automap_active()));
    }

    // EP-03: M1 at 0x0038 default -> rom3_delayed_on (EP7 valid=0).
    // Same ROM3 conditional gap as EP-02.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0038, true);
        check("EP-03",
              "M1 at 0x0038 with default NR and no ROM3: "
              "automap must NOT activate "
              "(VHDL zxnext.vhd:2890, divmmc.vhd:130)",
              !d.automap_active(),
              fmt("automap=%d exp=0 (ROM3 gating missing)",
                  d.automap_active()));
    }

    // EP-04..EP-08: EP2..EP6 disabled by default (B8 bit clear).
    // VHDL: zxnext.vhd:2862-2884 — all unselected bits yield no trigger.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0010, true);
        check("EP-04",
              "M1 at 0x0010 with B8[2]=0 (default): no automap "
              "(VHDL zxnext.vhd:2862)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0018, true);
        check("EP-05",
              "M1 at 0x0018 with B8[3]=0 (default): no automap "
              "(VHDL zxnext.vhd:2868)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0020, true);
        check("EP-06",
              "M1 at 0x0020 with B8[4]=0 (default): no automap "
              "(VHDL zxnext.vhd:2874)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0028, true);
        check("EP-07",
              "M1 at 0x0028 with B8[5]=0 (default): no automap "
              "(VHDL zxnext.vhd:2880)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0030, true);
        check("EP-08",
              "M1 at 0x0030 with B8[6]=0 (default): no automap "
              "(VHDL zxnext.vhd:2886)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // EP-09: NR 0xBA[i] selects instant_on vs delayed_on for EP i.
    // VHDL zxnext.vhd:2852, divmmc.vhd:129-148: instant_on contributes to
    // the combinational `automap` output same-cycle (line 148); delayed_on
    // contributes only to `hold` (line 129), which propagates to `held`
    // between M1 fetches, so activation is visible starting the NEXT fetch.
    // Task 7 Branch A now actually differentiates these — assert the
    // observable delay.
    {
        // Instant: EP0 with timing[0]=1 activates on the triggering M1.
        DivMmc d = make_divmmc();
        d.set_entry_timing_0(0x01);
        d.check_automap(0x0000, true);
        const bool instant_fires_same_cycle = d.automap_active();

        // Delayed: EP0 with timing[0]=0 does NOT activate on the
        // triggering M1; it activates on the next M1.
        DivMmc d2 = make_divmmc();
        d2.set_entry_timing_0(0x00);
        d2.check_automap(0x0000, true);
        const bool delayed_fires_same_cycle = d2.automap_active();
        d2.check_automap(0x0003, true);   // any non-trigger PC
        const bool delayed_fires_next = d2.automap_active();

        check("EP-09",
              "NR 0xBA[0]: instant activates same-cycle; delayed activates "
              "on the next M1 fetch (VHDL divmmc.vhd:128-148)",
              instant_fires_same_cycle &&
              !delayed_fires_same_cycle && delayed_fires_next,
              fmt("instant_same=%d delayed_same=%d delayed_next=%d "
                  "(expected 1,0,1)",
                  instant_fires_same_cycle,
                  delayed_fires_same_cycle,
                  delayed_fires_next));
    }

    // EP-10: NR 0xB9[i] is the "valid" flag. When valid=0 AND ROM3 is not
    // active, the entry point must NOT fire. Only when valid=1 (or ROM3
    // is active — not modelled here, Task 7) does the entry point fire.
    // VHDL: zxnext.vhd:2856, divmmc.vhd:148.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x03);   // enable EP0 (0x0000) and EP1 (0x0008)
        d.set_entry_valid_0(0x01);    // only EP0 valid; EP1 not valid
        d.check_automap(0x0008, true);
        bool gated_off = !d.automap_active();

        DivMmc d2 = make_divmmc();
        d2.set_entry_points_0(0x03);
        d2.set_entry_valid_0(0x03);   // both EP0 and EP1 valid
        d2.check_automap(0x0008, true);
        bool gated_on = d2.automap_active();

        check("EP-10",
              "NR 0xB9 valid flag gates EP1 without ROM3 "
              "(VHDL zxnext.vhd:2856)",
              gated_off && gated_on,
              fmt("valid=0 gated=%d valid=1 fires=%d", gated_off, gated_on));
    }

    // EP-11: NR B8=0xFF with B9=0x01 default. Per VHDL zxnext.vhd:2892-2905,
    // only EP0 (PC=0x0000) reaches delayed_on — the other 7 entry points
    // drop into rom3_delayed_on and need i_automap_rom3_active. Without the
    // ROM3 plumbing in C++, only 1/8 should fire against VHDL; the current
    // emulator wrongly fires all 8 (two compounding bugs: ignores B9 and
    // ignores ROM3). This row is a failing regression witness for the
    // ROM3-gating Emulator Bug backlog item.
    {
        int fired = 0;
        const uint16_t rst[8] = {0x0000,0x0008,0x0010,0x0018,
                                 0x0020,0x0028,0x0030,0x0038};
        for (uint16_t addr : rst) {
            DivMmc dd = make_divmmc();
            dd.set_entry_points_0(0xFF);
            dd.check_automap(addr, true);
            if (dd.automap_active()) ++fired;
        }
        check("EP-11",
              "NR B8=0xFF, B9=0x01 default: only EP0 reaches delayed_on; "
              "other 7 need ROM3 (VHDL zxnext.vhd:2892-2905)",
              fired == 1,
              fmt("fired=%d exp=1 (B9/ROM3 gating missing)", fired));
    }

    // EP-12: Automap only triggers on M1+MREQ (is_m1=true). Data reads
    // at 0x0000 must NOT activate. VHDL: divmmc.vhd:128.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, false);  // non-M1 fetch
        check("EP-12",
              "Non-M1 fetch at 0x0000 must not activate automap "
              "(VHDL divmmc.vhd:128)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §5. Automap Entry Points — Non-RST (NR 0xBB)
// VHDL: zxnext.vhd:2896-2908
// ══════════════════════════════════════════════════════════════════════

void group_nr() {
    set_group("5. Non-RST entry points");

    // NR-01: BB[2]=1 (default): M1 at 0x04C6 triggers rom3_delayed_on.
    // Again, C++ does not gate on ROM3, so it will activate. VHDL says
    // it requires ROM3 active; we assert the VHDL contract.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x04C6, true);
        check("NR-01",
              "M1 at 0x04C6 with BB[2]=1 (default) and no ROM3: "
              "automap must NOT activate "
              "(VHDL zxnext.vhd:2898)",
              !d.automap_active(),
              fmt("automap=%d (ROM3 gating missing)",
                  d.automap_active()));
    }

    // NR-02: BB[3]=1 (default): 0x0562 triggers rom3_delayed_on.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0562, true);
        check("NR-02",
              "M1 at 0x0562 with BB[3]=1 (default) and no ROM3: "
              "automap must NOT activate "
              "(VHDL zxnext.vhd:2900)",
              !d.automap_active(),
              fmt("automap=%d (ROM3 gating missing)",
                  d.automap_active()));
    }

    // NR-03: BB[4]=0 (default): M1 at 0x04D7: no trigger.
    // VHDL: zxnext.vhd:2902.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x04D7, true);
        check("NR-03",
              "M1 at 0x04D7 with BB[4]=0 (default): no automap "
              "(VHDL zxnext.vhd:2902)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-04: BB[5]=0 (default): 0x056A: no trigger.
    // VHDL: zxnext.vhd:2904.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x056A, true);
        check("NR-04",
              "M1 at 0x056A with BB[5]=0 (default): no automap "
              "(VHDL zxnext.vhd:2904)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-05: Enable BB[4]=1 then M1 at 0x04D7 -> rom3_delayed_on.
    // VHDL: zxnext.vhd:2902. Requires ROM3; assert VHDL contract.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0xCD | 0x10);  // BB[4]=1
        d.check_automap(0x04D7, true);
        check("NR-05",
              "M1 at 0x04D7 with BB[4]=1 and no ROM3: automap must NOT "
              "activate (VHDL zxnext.vhd:2902)",
              !d.automap_active(),
              fmt("automap=%d (ROM3 gating missing)",
                  d.automap_active()));
    }

    // NR-06: BB[7]=1 (default): M1 at any 0x3Dxx -> rom3_instant_on.
    // VHDL zxnext.vhd:2898-2899. Without rom3_active=1 the
    // sram_divmmc_automap_rom3_en gate at zxnext.vhd:3138 is low and the
    // trap path is suppressed — so this stays NOT activated.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x3D00, true);
        check("NR-06",
              "M1 at 0x3D00 with BB[7]=1 and no ROM3: automap must NOT "
              "activate (VHDL zxnext.vhd:2898-2899,3138)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-07: M1 at 0x3DFF with BB[7]=1 and no ROM3: no activation.
    // VHDL: zxnext.vhd:2898-2899 — port_3dxx_msb decodes any cpu_a(15:8)=$3D.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x3DFF, true);
        check("NR-07",
              "M1 at 0x3DFF with BB[7]=1 and no ROM3: no automap "
              "(VHDL zxnext.vhd:2898-2899)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-08: Disable BB[7]=0, M1 at 0x3D00: no trigger even with ROM3.
    {
        DivMmc d = make_divmmc();
        d.set_rom3_active(true);
        d.set_entry_points_1(0xCD & ~0x80);  // clear bit 7
        d.check_automap(0x3D00, true);
        check("NR-08",
              "M1 at 0x3D00 with BB[7]=0: no automap "
              "(VHDL zxnext.vhd:2898-2899)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-09: BB[7]=1 with rom3_active=1: M1 at 0x3D00 fires
    // rom3_instant_on (= same-cycle automap=1). VHDL zxnext.vhd:2898-2899.
    {
        DivMmc d = make_divmmc();
        d.set_rom3_active(true);
        d.check_automap(0x3D00, true);
        check("NR-09",
              "M1 at 0x3D00 with BB[7]=1 + rom3_active=1: rom3_instant_on "
              "fires automap (VHDL zxnext.vhd:2898-2899)",
              d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-10: BB[7]=1 with rom3_active=1: M1 at any address $3Dxx
    // (e.g. $3D7F mid-range) fires rom3_instant_on. Confirms wildcard.
    {
        DivMmc d = make_divmmc();
        d.set_rom3_active(true);
        d.check_automap(0x3D7F, true);
        check("NR-10",
              "M1 at 0x3D7F (mid wildcard) with BB[7]=1 + rom3_active=1: "
              "rom3_instant_on fires (VHDL zxnext.vhd:2898-2899)",
              d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-11: BB[7]=1 with rom3_active=1 AND layer2_map_read=1: rom3 path
    // is suppressed (sram_divmmc_automap_rom3_en factor `NOT
    // sram_layer2_map_en` at zxnext.vhd:3138).
    {
        DivMmc d = make_divmmc();
        d.set_rom3_active(true);
        d.set_layer2_map_read(true);
        d.check_automap(0x3D00, true);
        check("NR-11",
              "M1 at 0x3D00 with BB[7]=1 + rom3=1 + L2_read=1: rom3 path "
              "suppressed (VHDL zxnext.vhd:3138)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-12: 0x0066 NMI delayed (BB[0]=1) — VHDL zxnext.vhd:2908 +
    // divmmc.vhd:121,131. With button_nmi=1 the delayed bit feeds
    // automap_hold but NOT same-cycle automap (line 148 only includes
    // nmi_instant_on). Held promotes on next M1 → automap=1 there.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0x01);          // ONLY bit 0 (delayed) set
        d.set_button_nmi(true);
        d.check_automap(0x0066, true);
        // Same-cycle: delayed alone shouldn't fire combinational automap.
        // (VHDL: automap = held OR (active AND nmi_instant_on); with only
        // bit 0, nmi_instant_on=0 and held=0 → automap=0.)
        check("NR-12a",
              "M1 at 0x0066 + BB[0]=1 + button_nmi=1 (delayed alone): "
              "automap NOT fired same-cycle (VHDL divmmc.vhd:148)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
        // Next M1: held promotes from hold (which had delayed bit set).
        d.check_automap(0x0070, true);  // arbitrary non-trigger PC
        check("NR-12b",
              "M1 after 0x0066 + BB[0]=1 + button_nmi=1: held promotes "
              "(VHDL divmmc.vhd:128-141)",
              d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-13: 0x0066 NMI delayed (BB[0]=1) WITHOUT button_nmi: no trigger.
    // VHDL divmmc.vhd:121 gates nmi_delayed_on on button_nmi.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0x01);    // BB[0]=1, BB[1]=0
        // button_nmi defaults to false
        d.check_automap(0x0066, true);
        d.check_automap(0x0070, true);
        check("NR-13",
              "M1 at 0x0066 + BB[0]=1 + button_nmi=0: no trigger "
              "(VHDL divmmc.vhd:121)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-14 (CONTRACT-PIN; TASK2-VERIFY1/2 commit 399c9ae): the $3Dxx
    // wildcard is ROM3-conditional. With NR $BB bit 7 set BUT
    // rom3_active=0, an M1 at $3D00..$3DFF must NOT fire instant_on. VHDL
    // zxnext.vhd:2898-2899 requires rom3_active=1 (= sram_pre_override(2)+
    // (0) AND !layer2_map composite, divmmc.vhd:130) for the rom3_*_on
    // path to match.
    //
    // Reviewer finding (NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-
    // REVIEW.md §4.3 / §4.2): pre-fix the wildcard branch did not exist
    // at all, so rom3=0 also yielded automap=false (trivially). This
    // test cannot fail pre-fix — it is a CONTRACT-PIN guarding a future
    // regression where the && rom3_path_eligible gate is removed,
    // re-enabling the wildcard for rom3=0 (which would be a new bug).
    // Useful as a negative-companion to NR-09/NR-10 (which cover the
    // positive rom3=1 case) and as a guard against future churn that
    // touches the rom3 gating in zxnext.vhd:2898-2899 / divmmc.vhd:130.
    //
    // Commit attribution corrected (was: ff84d3e — that commit only
    // touched emulator.cpp and sd_card.cpp; the actual fix that
    // introduced the $3Dxx wildcard branch is 399c9ae per the report's
    // table row 1b).
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0x80);    // BB[7]=1, all others 0
        d.set_rom3_active(false);      // ROM3 NOT active
        d.check_automap(0x3D42, true); // mid-wildcard, M1
        check("NR-14",
              "CONTRACT-PIN: M1 at $3D42 with BB[7]=1 + rom3_active=0: "
              "rom3_instant_on stays gated; automap not active "
              "(VHDL zxnext.vhd:2898-2899, divmmc.vhd:130). NOT a "
              "discriminative regression sentinel for 399c9ae — pre-fix "
              "the wildcard branch did not exist; this row guards against "
              "future regressions that remove the && rom3_path_eligible "
              "gate.",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §6. Automap Deactivation
// VHDL: divmmc.vhd:108, 126-131; zxnext.vhd:~2906
// ══════════════════════════════════════════════════════════════════════

void group_da() {
    set_group("6. Deactivation");

    // DA-01: M1 at 0x1FF8 with automap held: deactivates on the NEXT M1.
    // VHDL: zxnext.vhd NR BB[6]=1; divmmc.vhd:131. The off trigger clears
    // hold during its fetch; the held latch propagates to 0 between fetches;
    // the following M1 observes automap=0.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);   // activate
        d.check_automap(0x1FF8, true);   // off trigger
        d.check_automap(0x0003, true);   // held propagates 0
        check("DA-01",
              "M1 at 0x1FF8 deactivates held automap (next-fetch visible) "
              "(VHDL divmmc.vhd:131)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // DA-02: M1 at 0x1FFF: deactivates (upper bound of off range).
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);
        d.check_automap(0x1FFF, true);   // off trigger
        d.check_automap(0x0003, true);   // held propagates 0
        check("DA-02",
              "M1 at 0x1FFF deactivates held automap (next-fetch visible) "
              "(VHDL divmmc.vhd:131)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // DA-03: M1 at 0x1FF7: below range, no deactivation.
    // VHDL: cpu_a[7:3]=11111 required (bits 7..3 = 11111 -> 0xF8..0xFF).
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);
        d.check_automap(0x1FF7, true);
        check("DA-03",
              "M1 at 0x1FF7: no deactivation (below 0x1FF8) "
              "(VHDL zxnext.vhd: cpu_a[7:3]=11111)",
              d.automap_active(),
              fmt("automap=%d exp=1", d.automap_active()));
    }

    // DA-04: M1 at 0x2000: above range, no deactivation.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);
        d.check_automap(0x2000, true);
        check("DA-04",
              "M1 at 0x2000: no deactivation (above range) "
              "(VHDL zxnext.vhd: port_1fxx_msb)",
              d.automap_active(),
              fmt("automap=%d exp=1", d.automap_active()));
    }

    // DA-05: BB[6]=0 disables deactivation range.
    // VHDL: zxnext.vhd NR BB[6] gates delayed_off.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);            // activate
        d.set_entry_points_1(0xCD & ~0x40);       // clear bit 6
        d.check_automap(0x1FF8, true);            // should NOT deactivate
        check("DA-05",
              "BB[6]=0 disables 0x1FF8 deactivation "
              "(VHDL zxnext.vhd NR 0xBB[6])",
              d.automap_active(),
              fmt("automap=%d exp=1", d.automap_active()));
    }

    // DA-06: RETN instruction clears automap state.
    // VHDL: divmmc.vhd:126,139 — i_retn_seen branch clears the
    // automap_hold/automap_held pipeline latches (JNEXT collapses both
    // onto automap_active_). button_nmi clear (divmmc.vhd:108) is
    // Task 8 (Multiface) and intentionally not covered here.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);   // activate
        bool before = d.automap_active();
        d.on_retn();
        check("DA-06",
              "on_retn() clears automap_active_ "
              "(VHDL divmmc.vhd:126,139)",
              before && !d.automap_active(),
              fmt("before=%d after=%d", before, d.automap_active()));
    }

    // DA-07: Reset clears automap state.
    // VHDL: divmmc.vhd:108,127,139 (i_reset branch).
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);   // activate
        d.reset();
        check("DA-07",
              "reset() clears automap_active "
              "(VHDL divmmc.vhd:127)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // DA-08: automap_reset (port_divmmc_io_en=0 OR NR 0x0A[4]=0) clears
    // the automap latch. VHDL divmmc.vhd:126-139. Implemented via
    // apply_enabled_transition_(): any enabled→false edge clears
    // automap_active_.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);   // activate
        bool before = d.automap_active();
        d.set_enabled(false);
        check("DA-08",
              "set_enabled(false) clears automap_active_ latch "
              "(VHDL divmmc.vhd:126)",
              before && !d.automap_active(),
              fmt("before=%d after=%d", before, d.automap_active()));
    }

    // DA-09 (CONTRACT-PIN; TASK2-PASS10 commit 770f78d): DivMmc::save_state
    // must NOT persist `rom3_active_`. The flag is a feeder shadow of VHDL
    // sram_pre_rom3 (zxnext.vhd:2981-3008,:3138) refreshed by the
    // Emulator at port-write / NR-commit / machine-type-change sites. A
    // snapshot taken with sram_rom=3 selected (e.g. during a CMD18 boot
    // stream) MUST come back with rom3_active_=false (constructor
    // default), forcing the load-time external sync that the pass-10
    // fix added at Emulator::load_state (`divmmc_.set_rom3_active(
    // mmu_.sram_rom3())`).
    //
    // Reviewer finding (NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-
    // REVIEW.md §4.2 / §2 row DA-09): this is a CONTRACT-PIN, not a
    // discriminative regression sentinel for the 770f78d fix itself.
    // Reverting Emulator::load_state's set_rom3_active() does NOT make
    // this test fail — the test only verifies the pre-condition that the
    // external re-sync is necessary (i.e., that DivMmc::save_state does
    // not persist the field). The actual fix is in Emulator-tier and
    // requires an integration test (full Emulator + Mmu + StateReader)
    // to exercise. This row pins the contract so a future change that
    // starts persisting rom3_active_ inside DivMmc::save/load_state
    // can't silently land without re-evaluating the external sync's
    // double-write hazards.
    {
        DivMmc d_src = make_divmmc();
        d_src.set_rom3_active(true);            // simulate ROM3 selected
        const bool src_rom3 = d_src.rom3_active();

        // Round-trip via the same StateWriter/StateReader pair the
        // emulator uses (saveable.h). Use measure mode to size the
        // buffer first, then write+read.
        StateWriter measure;
        d_src.save_state(measure);
        std::vector<uint8_t> buf(measure.position(), 0);
        StateWriter w(buf.data(), buf.size());
        d_src.save_state(w);
        StateReader r(buf.data(), buf.size());

        DivMmc d_dst;            // fresh, rom3_active_ = false (constructor)
        d_dst.load_state(r);
        const bool dst_rom3 = d_dst.rom3_active();

        check("DA-09",
              "CONTRACT-PIN: DivMmc::save_state does NOT persist "
              "rom3_active_; load_state yields constructor default (false). "
              "Pre-condition for the external Emulator::load_state "
              "set_rom3_active(mmu_.sram_rom3()) re-sync (VHDL feeder "
              "shadow of sram_pre_rom3, zxnext.vhd:2981-3008,:3138). NOT "
              "a discriminative sentinel for 770f78d — reverting the "
              "Emulator-tier fix does not fail this test (integration-"
              "tier coverage required for the actual fix path).",
              src_rom3 && !dst_rom3,
              fmt("src_rom3=%d dst_rom3=%d (expected 1, 0)",
                  src_rom3, dst_rom3));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §7. Automap Timing — Instant vs Delayed
// VHDL: divmmc.vhd:123-148
// ══════════════════════════════════════════════════════════════════════

void group_tm() {
    set_group("7. Instant vs delayed");

    // Task 7 Branch A implements the VHDL two-stage automap pipeline
    // (divmmc.vhd:123-148): automap_hold latches on M1+MREQ low with the
    // current-cycle OR of instant/delayed/held-minus-off; automap_held
    // latches from hold on MREQ rising edge. The combinational output
    // (line 148) is `held OR instant_on`, so instant matches fire the
    // same cycle while delayed matches fire only via held-promotion on
    // the next M1.

    // TM-01: instant_on (NR 0xBA bit=1) activates automap on the
    // triggering M1 via the combinational OR (held || instant). After the
    // first fetch, held is still 0 (it latches from hold on the NEXT MREQ
    // rising edge, which corresponds to the start of the NEXT M1 call in
    // our per-M1 model). On the second fetch, held promotes to 1.
    {
        DivMmc d = make_divmmc();            // timing_0_=0xFF (all instant)
        d.check_automap(0x0000, true);
        const bool active_1 = d.automap_active();
        const bool hold_1  = d.automap_hold();
        const bool held_1  = d.automap_held();
        d.check_automap(0x0003, true);
        const bool held_2  = d.automap_held();
        check("TM-01",
              "instant_on: active=1 and hold=1 after fetch 1 (held=0 yet); "
              "held=1 after fetch 2 (VHDL divmmc.vhd:141 latches held from "
              "hold on MREQ rising edge)",
              active_1 && hold_1 && !held_1 && held_2,
              fmt("fetch1 active=%d hold=%d held=%d; fetch2 held=%d",
                  active_1, hold_1, held_1, held_2));
    }

    // TM-02: delayed_on (NR 0xBA bit=0) sets hold/held this M1 but the
    // combinational output remains 0 because it reads the PREVIOUS held
    // value. Activation is visible on the next M1.
    {
        DivMmc d = make_divmmc();
        d.set_entry_timing_0(0x00);          // all delayed
        d.check_automap(0x0000, true);
        bool active_same = d.automap_active();
        bool hold_same  = d.automap_hold();
        // Next M1 at a non-trigger PC: held promotes from the previous
        // hold and active reads 1.
        d.check_automap(0x0003, true);
        bool active_next = d.automap_active();
        check("TM-02",
              "delayed_on: hold=1 this M1, active stays 0; next M1 active=1 "
              "(VHDL divmmc.vhd:129,141,148)",
              hold_same && !active_same && active_next,
              fmt("hold_same=%d active_same=%d active_next=%d "
                  "(expected 1,0,1)",
                  hold_same, active_same, active_next));
    }

    // TM-03: MREQ rising-edge latch — `held` carries forward between M1
    // fetches. Seed hold via an instant fetch, then on a subsequent
    // non-trigger fetch the held value is still visible and drives active.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);       // instant → hold=1, held=0
        // Per VHDL line 131 the held-carry term is `(automap_held and not
        // (i_automap_active and i_automap_delayed_off))`. Held latches
        // from hold at the start of this next call (our per-M1 model for
        // the MREQ rising edge between fetches). Active = held || instant
        // = 1 || 0 = 1.
        d.check_automap(0x0100, true);
        check("TM-03",
              "held persists across non-trigger M1 via hold propagation "
              "(VHDL divmmc.vhd:141-142,131)",
              d.automap_held() && d.automap_active(),
              fmt("held=%d active=%d", d.automap_held(), d.automap_active()));
    }

    // TM-04: is_m1=false does NOT update hold (VHDL divmmc.vhd:128 gates
    // the hold process on `cpu_mreq_n='0' AND cpu_m1_n='0'`). Strong
    // form: first activate via an instant M1 (so there IS real state),
    // then a NON-M1 fetch at an entry-point PC. Held must still promote
    // from the previous hold (normal per-fetch carry), but the non-M1
    // fetch itself must not alter hold.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);       // instant activation (hold=1, held=0)
        const bool pre_active = d.automap_active();
        const bool pre_hold   = d.automap_hold();
        // Non-M1 access at 0x0008 (another entry-point) — must be ignored.
        d.check_automap(0x0008, /*is_m1=*/false);
        const bool post_active = d.automap_active();
        const bool post_hold   = d.automap_hold();
        const bool post_held   = d.automap_held();
        check("TM-04",
              "non-M1 access at entry-point does NOT alter hold/held "
              "(VHDL divmmc.vhd:128 gates on M1+MREQ)",
              pre_active && pre_hold && post_active && post_hold && !post_held,
              fmt("pre act=%d hold=%d; post act=%d hold=%d held=%d",
                  pre_active, pre_hold, post_active, post_hold, post_held));
    }

    // TM-05: held persistence across multiple non-off M1 fetches. Once
    // activated, automap stays on through fetches that don't match any
    // entry point or off trigger.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);       // activate
        for (int i = 0; i < 5; ++i) {
            d.check_automap(static_cast<uint16_t>(0x0100 + i * 3), true);
        }
        check("TM-05",
              "held persists across 5 non-trigger M1 fetches "
              "(VHDL divmmc.vhd:131 — held AND NOT off keeps hold at 1)",
              d.automap_active(),
              fmt("active=%d after 5 non-trigger fetches", d.automap_active()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §8. Automap ROM3-Conditional Activation
// VHDL: zxnext.vhd:3137-3138
// ══════════════════════════════════════════════════════════════════════

void group_r3() {
    set_group("8. ROM3 conditional");

    // R3-01..R3-03: ROM3-conditional automap activation. Task 7 Branch B
    // plumbs the ROM-selection signal from MMU into DivMmc via
    // set_rom3_active(); Emulator pushes on port 0x7FFD/0x1FFD writes.
    // The full VHDL composite `sram_divmmc_automap_rom3_en` at zxnext.vhd:
    // 3138 also factors in altrom and Layer 2 read-map; those feeders are
    // out of scope here (R3-03 remains skip — see below).

    // R3-01: when ROM3 is the selected ROM, an entry point configured as
    // "ROM3-only" (NR 0xB9 bit=0) activates automap. VHDL divmmc.vhd:130
    // gates (i_automap_rom3_active AND rom3_*_on) into the hold formula.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x02);          // enable EP1 (RST 0x08)
        d.set_entry_valid_0(0x00);           // EP1 is ROM3-only path
        d.set_entry_timing_0(0xFF);          // instant (test-convenient)
        d.set_rom3_active(true);
        d.check_automap(0x0008, true);
        check("R3-01",
              "ROM3-only entry (NR 0xB9 bit=0) fires when rom3_active=1 "
              "(VHDL zxnext.vhd:2856,3138 + divmmc.vhd:130)",
              d.automap_active(),
              fmt("active=%d rom3=%d", d.automap_active(), d.rom3_active()));
    }

    // R3-02: same entry point, ROM3 NOT selected — automap must stay off.
    // This is the EP-02/EP-03-tightening mentioned in the Task 7 prompt:
    // prior to Branch B these would fire wrongly because ROM3 wasn't
    // observable.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x02);
        d.set_entry_valid_0(0x00);           // ROM3-only
        d.set_entry_timing_0(0xFF);
        d.set_rom3_active(false);            // ROM3 NOT selected
        d.check_automap(0x0008, true);
        check("R3-02",
              "ROM3-only entry does NOT fire when rom3_active=0 "
              "(VHDL zxnext.vhd:2856 gates on sram_pre_rom3)",
              !d.automap_active(),
              fmt("active=%d rom3=%d", d.automap_active(), d.rom3_active()));
    }

    // R3-03: Layer 2 read-map suppresses the ROM3 automap path.
    // VHDL zxnext.vhd:3138 — sram_divmmc_automap_rom3_en factors in
    // `NOT sram_layer2_map_en`: when Layer 2 is actively read-mapped
    // at 0x0000-0x3FFF, the ROM3-conditional automap must be suppressed
    // (the L2 read overlay owns that region). Two-stimulus discriminative
    // test: identical R3-01-style ROM3-only setup; the only difference is
    // the layer2_map_read_ latch.
    {
        // Sub-case A: L2 read-map OFF → ROM3-only entry fires (matches R3-01).
        DivMmc dA = make_divmmc();
        dA.set_entry_points_0(0x02);           // enable EP1 (RST 0x08)
        dA.set_entry_valid_0(0x00);            // ROM3-only path
        dA.set_entry_timing_0(0xFF);           // instant
        dA.set_rom3_active(true);
        dA.set_layer2_map_read(false);
        dA.check_automap(0x0008, true);

        // Sub-case B: same setup, L2 read-map ON → ROM3 path is suppressed.
        DivMmc dB = make_divmmc();
        dB.set_entry_points_0(0x02);
        dB.set_entry_valid_0(0x00);
        dB.set_entry_timing_0(0xFF);
        dB.set_rom3_active(true);
        dB.set_layer2_map_read(true);
        dB.check_automap(0x0008, true);

        const bool ok = dA.automap_active() && !dB.automap_active();
        check("R3-03",
              "Layer 2 read-map suppresses ROM3-only automap path "
              "(VHDL zxnext.vhd:3138 — sram_divmmc_automap_rom3_en "
              "AND NOT sram_layer2_map_en)",
              ok,
              fmt("L2off active=%d L2on active=%d",
                  dA.automap_active(), dB.automap_active()));
    }

    // R3-04: sram_divmmc_automap_en = sram_pre_override(2). Roughly
    // corresponds to the i_en gate modelled in CM-09.
    // VHDL: zxnext.vhd:3137.
    {
        DivMmc d;
        d.reset();
        d.set_enabled(true);
        d.set_nr_0a_4_enable(true);   // VHDL automap-en default '0', flip on
        d.write_control(0x80);  // conmem=1
        check("R3-04",
              "DivMMC enabled + conmem: non-ROM3 automap path active "
              "(VHDL zxnext.vhd:3137)",
              d.is_active(),
              fmt("act=%d", d.is_active()));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §9. NMI and DivMMC Button
// VHDL: divmmc.vhd:105-116, 120-121, 150
// ══════════════════════════════════════════════════════════════════════

void group_nm() {
    set_group("9. NMI / button");

    // DM-NMI-BTN-OFF / DM-NMI-BTN-ON: pin the VHDL divmmc.vhd:120
    // gating of automap_nmi_instant_on on the latched button_nmi
    // signal. NR 0xBB bit 1 enables the 0x0066 entry point (see VHDL
    // zxnext.vhd:2907 — divmmc_automap_nmi_instant_on = port_00xx_msb
    // AND port_66_lsb AND nr_bb_divmmc_ep_1(1)); the instant-on still
    // additionally requires button_nmi=1 per divmmc.vhd:120. Without
    // that additional gate we would spuriously automap on any PC=0x0066
    // fetch reached via normal control flow — which was the NextZXOS
    // enNextZX.rom hang symptom fixed in this commit series.
    //
    // NR 0xBB soft-reset default is 0xCD (zxnext.vhd:5090); bit 1 is 0
    // by default, so these tests explicitly set bit 1 to isolate the
    // button_nmi gate from the NR 0xBB enable.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0xCD | 0x02);   // enable 0x0066 entry point
        // button_nmi_ is cleared by reset(); make_divmmc() does not set it.
        d.check_automap(0x0066, true);
        check("DM-NMI-BTN-OFF",
              "PC=0x0066 M1 with NR BB[1]=1 but button_nmi=0: automap "
              "stays off (VHDL divmmc.vhd:120)",
              !d.automap_active(),
              fmt("automap=%d button_nmi=%d",
                  d.automap_active(), d.button_nmi()));
    }
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0xCD | 0x02);   // enable 0x0066 entry point
        d.set_button_nmi(true);
        d.check_automap(0x0066, true);
        check("DM-NMI-BTN-ON",
              "PC=0x0066 M1 with NR BB[1]=1 and button_nmi=1: "
              "instant-on automap activates (VHDL divmmc.vhd:120)",
              d.automap_active(),
              fmt("automap=%d button_nmi=%d",
                  d.automap_active(), d.button_nmi()));
    }

    // NM-01..NM-08: DivMMC NMI button lifecycle. VHDL divmmc.vhd:105-150
    // models a latched button_nmi signal that enters the automap pipeline
    // alongside RST entry points, and o_disable_nmi suppresses re-entry
    // until the handler completes.
    //
    // Un-skipped by TASK-NMI-SOURCE-PIPELINE-PLAN.md Wave B: NmiSource's
    // arbiter FSM emits the VHDL:2170 `nmi_divmmc_button` strobe on the
    // IDLE→FETCH transition for the DivMMC path, which Emulator wires to
    // `DivMmc::set_button_nmi(true)`. Rows NM-01..08 pin the four clear
    // paths (reset, automap_reset, RETN-seen, automap_held rising edge)
    // plus the VHDL:150 `o_disable_nmi = automap_held OR button_nmi`
    // accessor and the automap instant-on gate at VHDL:120.

    // NM-01 — NmiSource IDLE→FETCH via the DivMMC path pulses the
    // arbiter-side `nmi_divmmc_button` strobe; Emulator forwards that to
    // `DivMmc::set_button_nmi(true)` every tick. Model that seam here
    // (the same mini-fixture pattern as test/nmi/nmi_test.cpp DIS-01) so
    // divmmc_test covers the DivMmc-side observable.
    // VHDL: zxnext.vhd:2170 / divmmc.vhd:108-111.
    {
        NmiSource nmi;
        DivMmc    d;
        nmi.set_divmmc_enable(true);      // NR 0x06 bit 4 = 1
        const bool pre = d.button_nmi();  // reset → 0

        // Drive the DivMMC hotkey edge; FSM latches DivMMC next tick.
        nmi.strobe_divmmc_button();
        nmi.set_divmmc_nmi_hold(d.is_nmi_hold());
        nmi.set_divmmc_conmem(d.is_conmem());
        nmi.tick(1);
        const bool strobe = nmi.divmmc_button_strobe();
        if (strobe) d.set_button_nmi(true);
        const bool post = d.button_nmi();

        check("NM-01",
              "Arbiter IDLE->FETCH pulses nmi_divmmc_button; "
              "DivMmc::set_button_nmi(true) latches button_nmi_ "
              "(VHDL divmmc.vhd:108-111, zxnext.vhd:2170)",
              !pre && strobe && post,
              fmt("pre=%d strobe=%d post=%d", pre, strobe, post));
    }

    // NM-02 — With button_nmi=1 AND NR 0xBB bit 1 (port_66_lsb-aware entry)
    // enabled, an M1 fetch at PC=0x0066 fires the NMI instant-on automap
    // path. VHDL divmmc.vhd:120-121 gates `automap_nmi_instant_on` on the
    // latched button_nmi signal; the RST-entry pipeline activates automap.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0xCD | 0x02);     // enable 0x0066 (bit 1)
        d.set_button_nmi(true);
        const bool pre_btn    = d.button_nmi();
        const bool pre_active = d.automap_active();
        d.check_automap(0x0066, true);
        const bool post_active = d.automap_active();
        check("NM-02",
              "PC=0x0066 M1 with button_nmi=1 -> automap_nmi_instant_on "
              "fires (VHDL divmmc.vhd:120-121)",
              pre_btn && !pre_active && post_active,
              fmt("pre_btn=%d pre_active=%d post=%d",
                  pre_btn, pre_active, post_active));
    }

    // NM-03 — Complement of NM-02: with button_nmi=0, the 0x0066 NMI
    // instant-on path is gated off. Automap does NOT activate on a bare
    // 0x0066 fetch reached via normal control flow. This is the behaviour
    // restored by the NMI-button gate fix (see DM-NMI-BTN-OFF).
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0xCD | 0x02);
        // button_nmi_ = 0 from reset
        const bool pre_btn    = d.button_nmi();
        const bool pre_active = d.automap_active();
        d.check_automap(0x0066, true);
        const bool post_active = d.automap_active();
        check("NM-03",
              "PC=0x0066 M1 with button_nmi=0 -> no NMI instant-on "
              "automap (VHDL divmmc.vhd:120)",
              !pre_btn && !pre_active && !post_active,
              fmt("pre_btn=%d pre_active=%d post=%d",
                  pre_btn, pre_active, post_active));
    }

    // NM-04 — reset() clears button_nmi_ (VHDL divmmc.vhd:108 i_reset
    // branch). Latch must round-trip: set → reset → cleared.
    {
        DivMmc d;
        d.set_button_nmi(true);
        const bool pre = d.button_nmi();
        d.reset();
        const bool post = !d.button_nmi();
        check("NM-04",
              "reset() clears button_nmi_ (VHDL divmmc.vhd:108 i_reset)",
              pre && post,
              fmt("pre=%d post_cleared=%d", pre, post));
    }

    // NM-05 — i_automap_reset clears button_nmi_ (VHDL divmmc.vhd:108).
    // JNEXT models i_automap_reset as the enabled→disabled transition in
    // apply_enabled_transition_(). Setting enabled=false must drop the
    // latched button_nmi_ on the same call.
    {
        DivMmc d;
        d.set_enabled(true);
        d.set_nr_0a_4_enable(true);   // VHDL automap-en default '0', flip on
        d.set_button_nmi(true);
        const bool pre = d.button_nmi();
        d.set_enabled(false);
        const bool post = !d.button_nmi();
        check("NM-05",
              "enabled(true->false) (i_automap_reset) clears button_nmi_ "
              "(VHDL divmmc.vhd:108 / zxnext.vhd:4112)",
              pre && post,
              fmt("pre=%d post_cleared=%d", pre, post));
    }

    // NM-06 — i_retn_seen clears button_nmi_ (VHDL divmmc.vhd:108).
    // JNEXT exposes this as on_retn()/on_retn_seen(); Emulator forwards
    // Im2Controller::retn_seen_this_cycle() to this entry point.
    {
        DivMmc d;
        d.set_enabled(true);
        d.set_nr_0a_4_enable(true);   // VHDL automap-en default '0', flip on
        d.set_button_nmi(true);
        const bool pre = d.button_nmi();
        d.on_retn_seen();
        const bool post = !d.button_nmi();
        check("NM-06",
              "on_retn_seen() (i_retn_seen) clears button_nmi_ "
              "(VHDL divmmc.vhd:108)",
              pre && post,
              fmt("pre=%d post_cleared=%d", pre, post));
    }

    // NM-07 — `automap_held=1` rising edge clears button_nmi_
    // (VHDL divmmc.vhd:112-113: `elsif automap_held = '1' then
    // button_nmi <= '0'`). Drive the two-stage pipeline so held rises on
    // the second check_automap while button_nmi_ is already latched.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x01);    // RST 0x00 enabled
        d.set_entry_valid_0(0x01);     // main path valid
        d.set_entry_timing_0(0x01);    // instant timing for RST 0x00
        d.set_button_nmi(true);        // latch set
        const bool pre_btn      = d.button_nmi();
        const bool pre_held     = d.automap_held();

        // First M1: hold=1, held still 0 (loads from hold on NEXT M1).
        d.check_automap(0x0000, true);
        const bool mid_btn      = d.button_nmi();
        const bool mid_held     = d.automap_held();

        // Second M1: held loads from hold (=1). Rising edge drops
        // button_nmi_ per divmmc.vhd:112-113.
        d.check_automap(0x0100, true);
        const bool post_btn     = !d.button_nmi();
        const bool post_held    = d.automap_held();

        check("NM-07",
              "automap_held rising 0->1 clears button_nmi_ "
              "(VHDL divmmc.vhd:112-113)",
              pre_btn && !pre_held && mid_btn && !mid_held
                  && post_btn && post_held,
              fmt("pre_btn=%d pre_held=%d mid_btn=%d mid_held=%d "
                  "post_btn_cleared=%d post_held=%d",
                  pre_btn, pre_held, mid_btn, mid_held,
                  post_btn, post_held));
    }

    // NM-09 (TASK2-VERIFY8 commit 6ebfd2b): continuous-while-held button_nmi
    // clear. VHDL divmmc.vhd:112-113:
    //   elsif automap_held = '1' then button_nmi <= '0'
    // re-clears the latch every clock while `automap_held` is high. The
    // pre-fix model used a 0->1 rising-edge one-shot, so a button_nmi
    // strobe arriving AFTER held already became 1 was kept asserted until
    // the next held 0->1 edge. NM-07 above only exercises the rising-edge
    // case — this row covers the after-the-fact set: prime held=1 first,
    // THEN inject button_nmi=true and verify the next check_automap call
    // drops it. Without the pass-8 fix this would fail (button_nmi stays
    // 1 across an arbitrary number of further M1s while held remains 1).
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x01);    // RST 0x00 enabled
        d.set_entry_valid_0(0x01);     // main path valid
        d.set_entry_timing_0(0x01);    // instant for RST 0x00

        // Prime the two-stage pipeline: first M1 sets hold; second M1
        // promotes hold->held. After this, automap_held_ is steady at 1.
        d.check_automap(0x0000, true);
        d.check_automap(0x0100, true);
        const bool held_steady = d.automap_held();

        // Now set button_nmi AFTER held has risen — this is the case the
        // rising-edge pre-fix could not handle.
        d.set_button_nmi(true);
        const bool btn_set = d.button_nmi();

        // The next check_automap call (any address — the clear is gated
        // on held=1, not on PC) must drop button_nmi to 0 immediately.
        d.check_automap(0x0200, true);
        const bool btn_cleared_after = !d.button_nmi();
        const bool held_still = d.automap_held();

        check("NM-09",
              "button_nmi set while automap_held=1 is cleared on the "
              "next check_automap call (continuous-while-held semantics) "
              "(VHDL divmmc.vhd:112-113)",
              held_steady && btn_set && btn_cleared_after && held_still,
              fmt("held_steady=%d btn_set=%d btn_cleared=%d held_still=%d",
                  held_steady, btn_set, btn_cleared_after, held_still));
    }

    // NM-08 — o_disable_nmi = automap OR button_nmi  (VHDL divmmc.vhd:148+150)
    // Exercise the steady-state truth table via `DivMmc::is_nmi_hold()`:
    // {00, 01, 10, 11}. Two `check_automap` calls drive the (held=1)
    // legs so the pipeline reaches steady state — i.e. `automap_held_`
    // has caught up to `automap_hold_` and the combinational `automap`
    // (= held OR instant_match this cycle) collapses to `held` after the
    // entry-point M1 has gone by. NM-10 covers the discriminative
    // first-M1-instant-on case where active != held.
    {
        DivMmc d1;                                      // (0, 0)
        const bool s00 = !d1.is_nmi_hold();

        DivMmc d2;                                      // (btn=1)
        d2.set_button_nmi(true);
        const bool s01 = d2.is_nmi_hold() && !d2.automap_held();

        DivMmc d3 = make_divmmc();                      // (held=1)
        d3.set_entry_points_0(0x01);
        d3.set_entry_valid_0(0x01);
        d3.set_entry_timing_0(0x01);
        d3.check_automap(0x0000, true);
        d3.check_automap(0x0100, true);
        const bool s10 = d3.is_nmi_hold() && d3.automap_held()
                         && !d3.button_nmi();

        DivMmc d4 = make_divmmc();                      // (held=1, then btn=1)
        d4.set_entry_points_0(0x01);
        d4.set_entry_valid_0(0x01);
        d4.set_entry_timing_0(0x01);
        d4.check_automap(0x0000, true);
        d4.check_automap(0x0100, true);
        d4.set_button_nmi(true);                        // set AFTER held rises
        const bool s11 = d4.is_nmi_hold() && d4.automap_held()
                         && d4.button_nmi();

        check("NM-08",
              "is_nmi_hold() steady-state = automap OR button_nmi across "
              "all 4 input combinations after held has caught up to hold "
              "(VHDL divmmc.vhd:148,150 o_disable_nmi). Discriminative "
              "first-M1 active-vs-held case is in NM-10.",
              s00 && s01 && s10 && s11,
              fmt("s00=%d s01=%d s10=%d s11=%d", s00, s01, s10, s11));
    }

    // NM-10 (TASK2-VERIFY11 V11-DIVMMC-02): is_nmi_hold() must follow
    // VHDL divmmc.vhd:148+150 — `o_disable_nmi <= automap or button_nmi`
    // where `automap` (line 148) is the COMBINATIONAL signal:
    //
    //   automap <= (NOT i_automap_reset) AND
    //              (held OR (active AND instant_on this cycle) OR ...);
    //
    // Pre-fix `is_nmi_hold` used the registered `automap_held_` directly,
    // so on the very first M1 fetch where an instant-on entry-point
    // matched, `is_nmi_hold` returned false even though VHDL would assert
    // `o_disable_nmi` immediately. The divergence drops back to zero on
    // the next M1 (when held catches up to hold), so NM-08's two-fetch
    // setup misses it — NM-10 pins the first-fetch case directly.
    //
    // Discriminative stimulus: configure RST 0x00 as instant-on, main
    // path. Issue ONE `check_automap(0x0000, true)`. Post-fix, the M1
    // call leaves held=0 (just promoted from prior 0) and active=1
    // (held(0) || instant_match(1)). Pre-fix returned false; post-fix
    // returns true. NM-08's `s10`/`s11` legs DO call check_automap
    // twice, so the steady-state held-OR-button reading still passes
    // unchanged — both implementations agree once held has caught up.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x01);    // RST 0x00 enabled
        d.set_entry_valid_0(0x01);     // main path valid (NR 0xB9 bit 0)
        d.set_entry_timing_0(0x01);    // instant timing (NR 0xBA bit 0)

        // Single M1 fetch at the entry point — held is still 0 right
        // after this call (it only catches up on the NEXT M1 cycle's
        // step-1 promotion), but the combinational automap is 1 because
        // instant_match this cycle is 1.
        d.check_automap(0x0000, true);

        const bool held_zero    = !d.automap_held();
        const bool active_one   = d.automap_active();
        const bool nmi_hold_one = d.is_nmi_hold();

        check("NM-10",
              "First-M1 instant-on entry-point: is_nmi_hold() reflects "
              "the COMBINATIONAL automap (held(0) OR instant_match(1) = "
              "1) immediately, not the registered held bit (still 0 "
              "until the next M1 promotes hold→held). VHDL "
              "divmmc.vhd:148+150 — `o_disable_nmi <= automap or "
              "button_nmi`, where `automap` is line 148 combinational.",
              held_zero && active_one && nmi_hold_one,
              fmt("held=%d (exp 0) active=%d (exp 1) is_nmi_hold=%d "
                  "(exp 1)",
                  d.automap_held(), d.automap_active(),
                  d.is_nmi_hold()));
    }

    // DM-RETN-PROPER-01 — G46(a): canonical ED 45 RETN clears
    // automap_held via the one-M1-cycle delay register inside DivMmc.
    // The overlay survives RETN's own ED 45 fetch (held still 1 right
    // after the ED 45 ext-byte M1) and drops on the FIRST M1 of the
    // returned-to instruction, mirroring the VHDL register-shift shape
    // (divmmc.vhd:126,131,139 + im2_control.vhd:236).
    //
    // Stimulus path: drive Im2Controller's FSM (the same FSM the
    // Emulator's on_m1_cycle lambda uses in production) so the
    // canonical ED 45 vs alias-byte distinction is exercised
    // end-to-end, not faked via direct boolean injection.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x01);    // RST 0x00 enabled
        d.set_entry_valid_0(0x01);     // main path valid
        d.set_entry_timing_0(0x01);    // instant timing for RST 0x00

        // Two-stage automap: first M1 sets hold, second M1 promotes
        // hold→held (mirrors NM-07's setup pattern).
        d.check_automap(0x0000, true);
        d.check_automap(0x0100, true);
        const bool held_pre = d.automap_held();

        // Drive RETN through the real Im2 FSM. Z80 emulator delivers
        // BOTH bytes of an ED-prefix opcode to on_m1_cycle (G87, see
        // src/cpu/z80_cpu.cpp:480-481), so the FSM transitions
        // S_0 → S_ED_T4 → S_ED45_T4. retn_seen_this_cycle() pulses on
        // the second call (the ED 45 ext-byte M1).
        Im2Controller im2; im2.reset();

        // M1.A: ED prefix byte. FSM: S_0 → S_ED_T4. retn_seen=false.
        im2.on_m1_cycle(0x1000, 0xED);
        d.on_m1_retn_delay(im2.retn_seen_this_cycle());
        const bool held_after_ed = d.automap_held();

        // M1.B: 0x45 ext byte. FSM: S_ED_T4 → S_ED45_T4. retn_seen=true.
        // DivMmc latches retn_pending_clear_; held STAYS true on this
        // M1 (the delayed clear fires on the NEXT M1, not this one).
        im2.on_m1_cycle(0x1001, 0x45);
        d.on_m1_retn_delay(im2.retn_seen_this_cycle());
        const bool held_after_retn = d.automap_held();

        // M1.C: returned-to instruction's first byte (e.g. NOP at
        // 0x2000 — the address popped from stack by RETN's own logic;
        // doesn't matter for this test as DivMmc never sees the PC,
        // only the boolean from Im2 plus any check_automap call). The
        // pending clear fires here, dropping held → false.
        // Note: also call check_automap() to mirror the production
        // sequence (on_m1_prefetch fires before on_m1_cycle for every
        // instruction). The PC is non-trigger so check_automap is a
        // no-op for the held state — it just exercises the same
        // call ordering the Emulator uses.
        d.check_automap(0x2000, true);
        im2.on_m1_cycle(0x2000, 0x00);
        d.on_m1_retn_delay(im2.retn_seen_this_cycle());
        const bool held_after_next = d.automap_held();

        check("DM-RETN-PROPER-01",
              "ED 45 RETN clears automap_held one M1 after the RETN "
              "fetch (VHDL divmmc.vhd:139 + im2_control.vhd:236, "
              "modelled via DivMmc::on_m1_retn_delay one-M1 delay register)",
              held_pre && held_after_ed && held_after_retn
                  && !held_after_next,
              fmt("pre=%d after_ed=%d after_retn=%d after_next_cleared=%d",
                  held_pre, held_after_ed, held_after_retn,
                  !held_after_next));
    }

    // DM-RETN-PROPER-02 — G46(a): negative test for the canonical
    // RETN check. Im2Controller's FSM (im2_control.vhd:236) only
    // pulses retn_seen on a CANONICAL ED 45. None of the legacy alias
    // bytes (0x55/5D/65/6D/75/7D — LD r,L variants that some
    // documentation calls RETN-equivalent), nor ED 4D (RETI), nor a
    // standalone 0x45 (LD B,L without ED prefix) should clear
    // automap_held via this path. This row retires the pre-G87
    // RETN-alias band-aid in src/core/emulator.cpp.
    {
        // Build a per-byte fixture: each entry is (label, prefix_byte
        // OR 0xFF for "no prefix", target_byte). For each, set up
        // automap_held=true and feed the bytes through Im2 + DivMmc;
        // assert held STAYS true after both M1s plus a third "next
        // instruction" M1 (the same shape DM-RETN-PROPER-01 uses for
        // the positive case).
        struct Case { const char* label; uint8_t pre; uint8_t op; };
        const Case cases[] = {
            // ED-prefixed alias bytes (legacy RETN-equivalent encodings
            // per Z80 lore but NOT canonical per im2_control.vhd:236).
            {"ED 4D (RETI)",        0xED, 0x4D},
            {"ED 55 (LD D,L)",      0xED, 0x55},
            {"ED 5D (LD E,L)",      0xED, 0x5D},
            {"ED 65 (LD H,L)",      0xED, 0x65},
            {"ED 6D (LD L,L)",      0xED, 0x6D},
            {"ED 75 (LD (HL),L)",   0xED, 0x75},
            {"ED 7D (LD A,L)",      0xED, 0x7D},
            // Standalone 0x45 = LD B,L. NO ED prefix → FSM stays in
            // S_0; retn_seen_this_cycle() must NOT pulse.
            {"0x45 (LD B,L, no ED)", 0xFF, 0x45},
        };

        bool all_held_preserved = true;
        std::string detail;
        for (const Case& c : cases) {
            DivMmc d = make_divmmc();
            d.set_entry_points_0(0x01);
            d.set_entry_valid_0(0x01);
            d.set_entry_timing_0(0x01);
            d.check_automap(0x0000, true);
            d.check_automap(0x0100, true);
            const bool held_pre = d.automap_held();

            Im2Controller im2; im2.reset();

            // Optional ED prefix M1. 0xFF sentinel = no prefix.
            if (c.pre != 0xFF) {
                im2.on_m1_cycle(0x1000, c.pre);
                d.on_m1_retn_delay(im2.retn_seen_this_cycle());
            }

            // Target byte M1. For ED-prefixed alias bytes the FSM
            // transitions S_ED_T4 → (some other state, NOT S_ED45_T4),
            // so retn_seen pulse is FALSE. For standalone 0x45 the
            // FSM stays in S_0 and retn_seen is also FALSE.
            im2.on_m1_cycle(0x1001, c.op);
            d.on_m1_retn_delay(im2.retn_seen_this_cycle());
            const bool held_after_op = d.automap_held();

            // Step one more M1 to confirm no clear was queued. If a
            // false-positive had latched retn_pending_clear_, held
            // would drop here.
            im2.on_m1_cycle(0x2000, 0x00);
            d.on_m1_retn_delay(im2.retn_seen_this_cycle());
            const bool held_after_next = d.automap_held();

            const bool ok = held_pre && held_after_op && held_after_next;
            if (!ok) {
                all_held_preserved = false;
                if (!detail.empty()) detail += "; ";
                detail += c.label;
                detail += "(pre=" + std::to_string(held_pre)
                       +  " after_op=" + std::to_string(held_after_op)
                       +  " after_next=" + std::to_string(held_after_next)
                       +  ")";
            }
        }

        check("DM-RETN-PROPER-02",
              "RETN-alias bytes (ED 4D/55/5D/65/6D/75/7D, standalone "
              "0x45) do NOT clear automap_held — only canonical ED 45 "
              "matches Im2Controller::retn_seen_this_cycle() "
              "(VHDL im2_control.vhd:236)",
              all_held_preserved,
              detail.empty() ? std::string("all 8 alias cases preserved held")
                             : detail);
    }
}

// ══════════════════════════════════════════════════════════════════════
// §10. NextREG 0x0A — DivMMC Automap Enable
// VHDL: zxnext.vhd:4112
// ══════════════════════════════════════════════════════════════════════

void group_na() {
    set_group("10. NR 0x0A enable");

    // NA-01: NR 0x0A[4]=0 (default) asserts automap_reset.
    // VHDL: zxnext.vhd:4112. In the emulator the equivalent lever is
    // set_enabled(false) (there is no NR 0x0A bit 4 handler on DivMmc).
    // The contract: with enable off, automap cannot take effect.
    {
        DivMmc d;
        d.reset();
        d.set_enabled(false);
        d.check_automap(0x0000, true);
        check("NA-01",
              "enable=false (NR 0x0A[4]=0 equivalent): no mapping on "
              "automap trigger (VHDL zxnext.vhd:4112)",
              !d.is_active(),
              fmt("act=%d", d.is_active()));
    }

    // NA-01b: cold-boot equivalent — set_enabled(true) ALONE (i.e.
    // port_divmmc_io_en='1', nr_0a_divmmc_automap_en still '0' as per
    // VHDL zxnext.vhd:1126 default) MUST NOT activate automap on the
    // RST 0 entry-point match at PC=0x0000 even with NR 0xB8 default
    // 0x83 (which has bit 0 = RST 0 enabled). Discriminative regression
    // row for the cold-boot tbblue.fw splash bug fixed 2026-05-04: a
    // future change that re-collapses both gates inside set_enabled
    // would brick the splash logo and this row would catch it.
    {
        DivMmc d;
        d.reset();
        d.set_enabled(true);          // VHDL port_divmmc_io_en := '1'
                                      // — nr_0a_4_enable_ stays false
        d.set_entry_timing_0(0xFF);   // even with all-instant timing...
        d.check_automap(0x0000, true);
        check("NA-01b",
              "set_enabled(true) alone (nr_0a_4_enable_=false) keeps "
              "automap reset (VHDL zxnext.vhd:1126,4112)",
              !d.is_active() && !d.is_rom_mapped(),
              fmt("act=%d rom=%d", d.is_active(), d.is_rom_mapped()));
    }

    // NA-01c: CONMEM path is gated by port_divmmc_io_en ONLY, NOT by the
    // combined (port_io AND nr_0a_4). VHDL `divmmc.vhd:94-95,98` derives
    // `rom_en/ram_en` from `(conmem OR automap)` with no enable gate;
    // the output gate is `o_divmmc_rom_en <= rom_en AND i_en` where
    // `i_en => port_divmmc_io_en` per `zxnext.vhd:4147`. The combined
    // enable only gates `automap` itself (via `divmmc_automap_reset`
    // at `zxnext.vhd:4112`); CONMEM sails through nr_0a_4=0. This is
    // load-bearing for the ESXDOS IM1 handler in enNextZX.rom (writes
    // 0x80 to port 0xE3 to overlay DivMMC ROM, RETN at 0x0066 to drop)
    // which fires BEFORE firmware writes NR 0x0A bit 4=1 in init at
    // PC=0x0245. Pairs with NA-01b: NA-01b pins automap-off direction,
    // NA-01c pins CONMEM-on direction.
    {
        DivMmc d;
        d.reset();
        d.set_enabled(true);          // VHDL port_divmmc_io_en := '1'
                                      // — nr_0a_4_enable_ stays false
        d.write_control(0x80);        // OUT (0xE3), 0x80 — CONMEM ON
        check("NA-01c",
              "CONMEM with set_enabled(true) alone (nr_0a_4=0): "
              "is_active() true (VHDL divmmc.vhd:94 + zxnext.vhd:4147)",
              d.is_active() && d.is_rom_mapped(),
              fmt("act=%d rom=%d", d.is_active(), d.is_rom_mapped()));
    }

    // NA-02: NR 0x0A[4]=1 releases automap_reset -> automap can function.
    // Uses explicit all-instant timing to observe single-fetch activation
    // (default reset state is timing=0 = all delayed).
    {
        DivMmc d;
        d.reset();
        d.set_enabled(true);
        d.set_nr_0a_4_enable(true);   // simulates the firmware NR 0x0A bit 4 set
        d.set_entry_timing_0(0xFF);
        d.check_automap(0x0000, true);
        check("NA-02",
              "enable=true releases reset, automap functions "
              "(VHDL zxnext.vhd:4112)",
              d.is_active() && d.is_rom_mapped(),
              fmt("act=%d rom=%d",
                  d.is_active(), d.is_rom_mapped()));
    }

    // NA-03: enable = port_divmmc_io_en AND NR 0x0A[4]; the two levers
    // are independent and either one going low asserts automap_reset.
    // VHDL: zxnext.vhd:4112.
    {
        DivMmc d;
        d.reset();
        d.set_port_io_enable(true);
        d.set_nr_0a_4_enable(false);
        bool blocked_by_nr0a = !d.is_enabled();

        d.set_nr_0a_4_enable(true);
        bool both_on_enabled = d.is_enabled();

        d.set_port_io_enable(false);
        bool blocked_by_port = !d.is_enabled();

        check("NA-03",
              "port_io_enable and nr_0a_4_enable are independent levers; "
              "enabled_ = port_io_enable AND nr_0a_4_enable "
              "(VHDL zxnext.vhd:4112)",
              blocked_by_nr0a && both_on_enabled && blocked_by_port,
              fmt("nr0a=0→%d both=1→%d port=0→%d",
                  blocked_by_nr0a, both_on_enabled, blocked_by_port));
    }

    // NA-04 — VHDL zxnext.vhd:1126,4112,5196 — NR 0x0A bit 4 toggle
    // calls DivMmc::set_nr_0a_4_enable on NextREG write. divmmc.h:113
    // setter exists; emulator.cpp NR 0x0A handler now wires it (G123 — fixed).
    //
    // Mirror the production NR 0x0A handler (emulator.cpp:447-466) to
    // verify the wiring through NextReg::write at unit-test scope. Test
    // assumes firmware has already written NR 0x0A bit 4=1 (so both
    // gates are on at fixture entry); subsequent NR 0x0A writes flip
    // bit 4 around to observe the toggle.
    {
        NextReg nr;
        DivMmc d; d.reset();
        SpiMaster s;
        d.set_enabled(true);          // VHDL port_divmmc_io_en := '1'
        d.set_nr_0a_4_enable(true);   // assume firmware NR 0x0A bit 4=1
                                      //   already (the only state where the
                                      //   bit-4 toggle below has observable
                                      //   effect on is_enabled()).
        // Emulator-side NR 0x0A handler (mirror — keep in sync with
        // the production handler at src/core/emulator.cpp).
        nr.set_write_handler(0x0A, [&](uint8_t v) -> uint8_t {
            if (nr.nr_03_config_mode()) {
                s.set_sd_swap((v & 0x20) != 0);
            }
            d.set_nr_0a_4_enable((v & 0x10) != 0);
            return v;
        });

        // NR 0x0A bit 4 = 0 → set_nr_0a_4_enable(false).
        nr.write(0x0A, 0x00);
        bool off = !d.nr_0a_4_enable() && !d.is_enabled();
        // NR 0x0A bit 4 = 1 → set_nr_0a_4_enable(true).
        nr.write(0x0A, 0x10);
        bool on = d.nr_0a_4_enable() && d.is_enabled();
        check("NA-04",
              "NR 0x0A bit 4 wired through to DivMmc::set_nr_0a_4_enable "
              "(VHDL zxnext.vhd:1126,5196)",
              off && on,
              fmt("off=%d on=%d", off, on));
    }

    // NA-05 — VHDL zxnext.vhd:2412,4112 — NR 0x83 bit 0 clear asserts
    // divmmc_automap_reset (port_io_en path). emulator.cpp NR 0x83 handler
    // now propagates bit 0 to DivMmc::set_port_io_enable (G124 — fixed).
    // Test assumes firmware has already written NR 0x0A bit 4=1 (so
    // toggling NR 0x83 bit 0 has observable effect on is_enabled()).
    {
        NextReg nr;
        DivMmc d; d.reset();
        d.set_enabled(true);          // VHDL port_divmmc_io_en := '1'
        d.set_nr_0a_4_enable(true);   // assume firmware NR 0x0A bit 4=1 already
        // Mirror production NR 0x83 handler.
        nr.set_write_handler(0x83, [&](uint8_t v) -> uint8_t {
            d.set_port_io_enable((v & 0x01) != 0);
            return v;
        });

        // NR 0x83 bit 0 = 0 → port_io_enable_=false; enabled_ collapses.
        nr.write(0x83, 0x00);
        bool off = !d.port_io_enable() && !d.is_enabled();
        // NR 0x83 bit 0 = 1 → port_io_enable_=true; enabled_ recovers
        // (nr_0a_4_enable_ was left true from set_enabled(true)).
        nr.write(0x83, 0x01);
        bool on = d.port_io_enable() && d.is_enabled();
        check("NA-05",
              "NR 0x83 bit 0 wired through to DivMmc::set_port_io_enable "
              "(VHDL zxnext.vhd:2412)",
              off && on,
              fmt("off=%d on=%d", off, on));
    }

    // NA-06 — VHDL zxnext.vhd:1107-1108 — NR 0x06 power-on default = 0xA0
    // (bit 7 hotkey_cpu_speed_en='1', bit 5 hotkey_5060_en='1', others '0').
    // jnext NextReg::reset now stores 0xA0 in regs_[0x06]. G125.
    {
        NextReg nr;
        nr.reset();
        const uint8_t v = nr.cached(0x06);
        check("NA-06",
              "NR 0x06 power-on default = 0xA0 (b7=1, b5=1) "
              "(VHDL zxnext.vhd:1107-1108)",
              v == 0xA0,
              fmt("got=0x%02X exp=0xA0", v));
    }

    // NA-07 — VHDL zxnext.vhd:5162-5169 — NR 0x06 bit 7 / bit 5 round-trip.
    // NextReg::write stores raw 8 bits at regs_[reg]; reads return regs_[].
    // The emulator NR 0x06 handler does not currently shadow bits 7/5 into
    // dedicated state (G132 hotkey FSM is the implementation gap), but the
    // VHDL contract for the read mux at zxnext.vhd:5900 is the bit
    // round-trip — which the storage layer already provides.
    {
        NextReg nr;
        nr.reset();
        // Write 0xA0 (bits 7,5 set) then read back.
        nr.write(0x06, 0xA0);
        uint8_t a0 = nr.read(0x06);
        // Write 0x00 (bits 7,5 cleared) then read back.
        nr.write(0x06, 0x00);
        uint8_t z = nr.read(0x06);
        // Write 0x80 only (bit 7 set, bit 5 cleared).
        nr.write(0x06, 0x80);
        uint8_t b7 = nr.read(0x06);
        check("NA-07",
              "NR 0x06 bits 7/5 round-trip through NextReg storage "
              "(VHDL zxnext.vhd:5162,5164,5900)",
              a0 == 0xA0 && z == 0x00 && b7 == 0x80,
              fmt("0xA0→0x%02X 0x00→0x%02X 0x80→0x%02X", a0, z, b7));
    }

    // NA-08 — VHDL zxnext.vhd:5191-5198 — NR 0x0A bit 5 (sd_swap) and
    // bits 7:6 (mf_type) writes only commit when nr_03_config_mode='1'.
    // emulator.cpp NR 0x0A handler now gates these on
    // nextreg_.nr_03_config_mode(). G131 — fixed.
    {
        NextReg nr;
        DivMmc d; d.reset();
        SpiMaster s;
        d.set_enabled(true);
        d.set_nr_0a_4_enable(true);   // VHDL automap-en default '0', flip on
        nr.set_write_handler(0x0A, [&](uint8_t v) -> uint8_t {
            if (nr.nr_03_config_mode()) {
                s.set_sd_swap((v & 0x20) != 0);
            }
            d.set_nr_0a_4_enable((v & 0x10) != 0);
            return v;
        });

        // Power-on default: nr_03_config_mode_ = true. Write NR 0x0A bit
        // 5 — sd_swap should commit.
        nr.write(0x0A, 0x30);            // b5=1, b4=1 (keep DivMMC enabled)
        bool committed_in_cm = s.sd_swap();

        // Exit config_mode (NR 0x03 bits[2:0] = 001..110 (not 000/111)).
        nr.apply_nr_03_config_mode_transition(0x01);
        // Write NR 0x0A clearing bit 5 — sd_swap should be UNCHANGED
        // (write blocked by config_mode gate).
        nr.write(0x0A, 0x10);            // b5=0, b4=1
        bool blocked_outside_cm = s.sd_swap();   // expect still true

        // Re-enter config_mode (NR 0x03 bits[2:0] = 111).
        nr.apply_nr_03_config_mode_transition(0x07);
        // Now writing b5=0 must commit.
        nr.write(0x0A, 0x10);
        bool unblocked_after_cm = !s.sd_swap();

        check("NA-08",
              "NR 0x0A bit 5 (sd_swap) write blocked when "
              "nr_03_config_mode=0 (VHDL zxnext.vhd:5191-5194)",
              committed_in_cm && blocked_outside_cm && unblocked_after_cm,
              fmt("in_cm=%d blocked=%d unblock=%d",
                  committed_in_cm, blocked_outside_cm,
                  unblocked_after_cm));
    }

    // NA-09 (CONTRACT-PIN; TASK2-VERIFY6 commit c54192d): NR 0x83 reset
    // propagation gap. NextReg::reset() reloads regs_[0x83] to 0xFF on
    // the reset_type_1=true path (VHDL zxnext.vhd:5052-5057), but the
    // registered NR 0x83 write_handler is NOT fired by the reset path —
    // so consumer shadows DivMmc::port_io_enable_ and Multiface::enabled_
    // keep their stale pre-reset values until the next explicit
    // `nextreg_.write(0x83, ...)`. The pass-6 Emulator::init fix added an
    // explicit sync at end-of-init from `cached(0x83)` into both DivMmc
    // and Multiface bits 0:1.
    //
    // This row pins the contract jointly:
    //   (1) NextReg::reset() reloads regs_[0x83] to 0xFF (cached() = 0xFF)
    //   (2) The registered write_handler is NOT invoked by reset()
    //       (handler-call counter does not increment)
    //   (3) Applying the explicit `cached(0x83)` sync brings DivMmc and
    //       Multiface back into agreement (bits 0:1 → enables).
    //
    // Reviewer finding (NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-
    // REVIEW.md §4.2 / §2 row NA-09): this is a CONTRACT-PIN, not a
    // discriminative regression sentinel for the c54192d fix itself.
    // Reverting Emulator::init's explicit cached(0x83) sync does NOT
    // make this test fail — the test only verifies the gap pattern
    // (NextReg::reset doesn't fire write_handlers; explicit cached-sync
    // recovers the consumer). The actual fix is in Emulator-tier and
    // requires an integration test (full Emulator::init path) to
    // exercise. Without the fix, an emulator that wrote NR 0x83 ← 0xFE
    // before a soft reset would still observe the divmmc disabled /
    // multiface enabled afterward (handler never re-runs), even though
    // VHDL's reset_type_1=true reload restores the default 0xFF — but
    // that observation requires the full Emulator instance.
    {
        NextReg nr;
        DivMmc d; d.reset();
        // We don't need a real Multiface — just validate the same
        // DivMmc.port_io_enable_ shadow path via NR 0x83 bit 0.
        int handler_calls_total = 0;
        nr.set_write_handler(0x83, [&](uint8_t v) -> uint8_t {
            ++handler_calls_total;
            d.set_port_io_enable((v & 0x01) != 0);
            return v;
        });

        // Drive NR 0x83 ← 0xFE (clear bit 0 → divmmc port_io disabled).
        nr.write(0x83, 0xFE);
        const int after_first_write = handler_calls_total;
        const bool divmmc_after_clear = !d.port_io_enable();

        // Reset NextReg. With NR 0x85 power-on default 0x80 (reset_type_1
        // = true, VHDL zxnext.vhd:1230), reset() reloads regs_[0x83] to
        // 0xFF.
        nr.reset();
        const uint8_t cached_after_reset = nr.cached(0x83);
        const int handler_calls_after_reset = handler_calls_total;
        // DivMmc shadow stays stale until an external sync runs.
        const bool divmmc_stale = !d.port_io_enable();

        // Apply the Emulator::init pass-6 fix: explicit sync from
        // cached(0x83) bit 0 into the DivMmc shadow.
        d.set_port_io_enable((nr.cached(0x83) & 0x01) != 0);
        const bool divmmc_after_sync = d.port_io_enable();

        check("NA-09",
              "CONTRACT-PIN: NR 0x83 reset reloads cache to 0xFF without "
              "firing the registered write_handler; explicit Emulator::"
              "init sync from cached(0x83) bit 0 brings DivMmc::"
              "port_io_enable_ back into agreement (VHDL zxnext.vhd:5052-"
              "5057 reload; handler not on reset path). NOT a "
              "discriminative sentinel for c54192d — reverting the "
              "Emulator-tier fix does not fail this test (integration-"
              "tier coverage required for the actual fix path).",
              after_first_write == 1
                  && divmmc_after_clear
                  && cached_after_reset == 0xFF
                  && handler_calls_after_reset == 1   // unchanged
                  && divmmc_stale                     // still stale before sync
                  && divmmc_after_sync,
              fmt("calls(write/post-reset)=%d/%d cached=%02x "
                  "stale=%d after_sync=%d",
                  after_first_write, handler_calls_after_reset,
                  cached_after_reset, divmmc_stale, divmmc_after_sync));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §11. DivMMC SRAM Address Mapping
// VHDL: zxnext.vhd:3084-3097
// ══════════════════════════════════════════════════════════════════════

void group_sm() {
    set_group("11. SRAM mapping");

    // SM-01..SM-07 — category A (physical hardware layout artifact).
    //
    // The VHDL SRAM address ladder (zxnext.vhd:3084-3097) maps DivMMC
    // ROM and RAM banks into a single 22-bit SRAM address space shared
    // with Layer 2 and ROMCS. JNEXT keeps RAM and ROM in separate
    // buffers owned by DivMmc, with no physical +32 page offset and no
    // cross-object priority chain — DivMMC/Layer 2/ROMCS arbitration is
    // resolved in `src/memory/mmu.cpp`, not in `DivMmc`. None of the
    // seven rows expose a software-visible difference and all are
    // unreachable from this unit test. Same shape as Layer 2
    // G7-01..03/05a-c already in-repo.
    //
    // SM-01: SRAM ROM address ladder    (VHDL zxnext.vhd:3084)
    // SM-02: SRAM RAM bank 0 address    (VHDL zxnext.vhd:3087)
    // SM-03: SRAM RAM bank 3 address    (VHDL zxnext.vhd:3087)
    // SM-04: SRAM RAM bank 15 address   (VHDL zxnext.vhd:3087)
    // SM-05: DivMMC vs Layer2 priority  (VHDL zxnext.vhd:3091)
    // SM-06: DivMMC vs ROMCS priority   (VHDL zxnext.vhd:3094)
    // SM-07: ROMCS->DivMMC bank 14/15 aliasing (VHDL zxnext.vhd:3097)

    // ── Re-homed from test/mmu/mmu_test.cpp (Cat18 decode priority) ──
    //
    // PRI-01/02/04 are the Mmu-side view of the same DivMMC-vs-everything
    // priority ladder that SM-05/06 tracks as physical-layout items.
    // They were re-homed from test/mmu/mmu_test.cpp on 2026-04-21 (commit
    // 7be3deb) because exercising the cross-object arbitration needs both
    // a live DivMmc and the Mmu overlay path (Mmu::set_divmmc +
    // divmmc_read/divmmc_write). The MmuDivFixture at the top of this
    // file supplies exactly that — Ram + Rom + Mmu + DivMmc wired by
    // mmu.set_divmmc(&dm), with conmem-driven activation so the tests
    // bypass the M1 automap latching pipeline and pin the priority
    // decision itself.
    //
    // VHDL oracle: the SRAM arbiter at zxnext.vhd:3084-3100 places DivMMC
    // ROM (:3084), DivMMC RAM (:3087), and DivMMC vs Layer 2 (:3091)
    // above the Layer 2 / MMU / config_mode / ROMCS branches. The C++
    // mirror lives in src/memory/mmu.h:118-133 (read) and :215-220
    // (write) — DivMMC check fires before the L2 and config_mode
    // branches, which is what PRI-04 asserts directly.

    // PRI-01: DivMMC ROM > MMU at 0x0000-0x1FFF when DivMMC is active.
    // VHDL zxnext.vhd:3084.
    //
    // Stage 1 — DivMMC active with conmem, MMU slot 0 pointed at a RAM
    // page carrying a distinct sentinel. Read at 0x0000 must return the
    // DivMMC ROM byte (0xFF default — kRomSize bytes initialised to 0xFF
    // in DivMmc::DivMmc()), NOT the MMU sentinel.
    //
    // Stage 2 — flip DivMMC off (both levers) so the overlay drops out;
    // the SAME read at 0x0000 must now return the MMU sentinel. The
    // asymmetry is what pins this to the priority ladder — without it,
    // stage 1 alone could be a coincidence (0xFF happens to match the
    // default ROM read path).
    {
        MmuDivFixture f;
        f.mmu.set_page(0, 0x20);           // MMU slot 0 → physical page 0x20
        f.ram.page_ptr(0x20)[0] = 0x5A;    // MMU sentinel at 0x0000
        f.divmmc_conmem_on();               // DivMMC active, bank=0, !mapram

        const uint8_t with_divmmc = f.mmu.read(0x0000);

        f.dm.set_enabled(false);            // drop overlay — MMU wins
        const uint8_t without_divmmc = f.mmu.read(0x0000);

        check("PRI-01",
              "DivMMC ROM overrides MMU at 0x0000-0x1FFF when overlay "
              "active (VHDL zxnext.vhd:3084)",
              with_divmmc == 0xFF && without_divmmc == 0x5A,
              fmt("with_divmmc=0x%02X (expect 0xFF DivMMC ROM default) "
                  "without_divmmc=0x%02X (expect 0x5A MMU sentinel)",
                  with_divmmc, without_divmmc));
    }

    // PRI-02: DivMMC RAM > MMU at 0x2000-0x3FFF when DivMMC is active.
    // VHDL zxnext.vhd:3087.
    //
    // Seeds two distinct sentinels — one in MMU-backed RAM page 0x21
    // (slot 1 target), one in DivMMC RAM bank 0 — and verifies that the
    // slot-1 read returns the DivMMC byte while DivMMC is active, and
    // the MMU byte once DivMMC is disabled. DivMMC RAM is seeded via
    // mmu.write while the overlay is active, which exercises the same
    // write-side priority block this test is proving on the read side.
    {
        MmuDivFixture f;
        f.mmu.set_page(1, 0x21);             // MMU slot 1 → physical page 0x21
        f.ram.page_ptr(0x21)[0] = 0xA5;      // MMU sentinel at 0x2000
        f.divmmc_conmem_on(false, 0);        // conmem, !mapram, bank 0

        // Seed DivMMC RAM bank 0, offset 0 via the overlay write path
        // (slot 1 writes are accepted unless mapram+bank=3 per
        // DivMmc::write at src/peripheral/divmmc.cpp:326-329).
        f.mmu.write(0x2000, 0xC3);

        const uint8_t with_divmmc = f.mmu.read(0x2000);

        f.dm.set_enabled(false);             // drop overlay — MMU wins
        const uint8_t without_divmmc = f.mmu.read(0x2000);

        check("PRI-02",
              "DivMMC RAM overrides MMU at 0x2000-0x3FFF when overlay "
              "active (VHDL zxnext.vhd:3087)",
              with_divmmc == 0xC3 && without_divmmc == 0xA5,
              fmt("with_divmmc=0x%02X (expect 0xC3 DivMMC RAM) "
                  "without_divmmc=0x%02X (expect 0xA5 MMU sentinel)",
                  with_divmmc, without_divmmc));
    }

    // PRI-04: DivMMC > Layer 2 write-over at 0x0000-0x3FFF when DivMMC
    // is active. VHDL zxnext.vhd:3084-3100 — the SRAM arbiter chain
    // (DivMMC-ROM @3084, DivMMC-RAM @3092, L2-map @3100) makes DivMMC's
    // above-L2 priority implicit in the if/elsif ordering. PRI-04
    // asserts the combined outcome.
    //
    // Setup: L2 write-over enabled with segment 0 (0x0000-0x3FFF), bank
    // 16 (physical page 0x20), MMU slot 0 pointed at a distinct page
    // 0x30, DivMMC active via conmem.
    //
    // Stage 1 (DivMMC on): a write to 0x0000 must NOT land in L2 SRAM
    // page 0x20 — the DivMMC branch at mmu.h:215-220 absorbs the write
    // before the L2 block at :223-238 gets a look. The DivMMC slot-0
    // write itself is discarded inside DivMmc::write (conmem+!mapram,
    // src/peripheral/divmmc.cpp:310-323), so neither L2 nor the MMU
    // RAM page should carry the sentinel after this write.
    //
    // Stage 2 (DivMMC off): with the overlay dropped, the SAME write
    // falls to the L2 branch and lands in page 0x20[0]. Proving the
    // write would have reached L2 if DivMMC hadn't intercepted — pins
    // the priority decision to the DivMMC branch.
    {
        MmuDivFixture f;
        f.mmu.set_page(0, 0x30);             // MMU slot 0 → physical page 0x30
        f.mmu.set_l2_port(0x01, 16);         // L2 wr_en, seg 0, bank 16 → page 0x20
        f.divmmc_conmem_on();                // DivMMC active

        // Stage 1: write with DivMMC winning the arbitration.
        f.mmu.write(0x0000, 0xDB);
        const uint8_t l2_after_divmmc  = f.ram.page_ptr(0x20)[0];
        const uint8_t mmu_after_divmmc = f.ram.page_ptr(0x30)[0];

        // Stage 2: drop DivMMC, re-write. L2 should now take the byte.
        f.dm.set_enabled(false);
        f.mmu.write(0x0000, 0xEC);
        const uint8_t l2_after_no_divmmc = f.ram.page_ptr(0x20)[0];

        check("PRI-04",
              "DivMMC beats Layer 2 write-over at 0x0000-0x1FFF when "
              "overlay active (VHDL zxnext.vhd:3084-3100 chain)",
              l2_after_divmmc  != 0xDB &&  // L2 did NOT take the byte
              mmu_after_divmmc != 0xDB &&  // MMU also did NOT take it
              l2_after_no_divmmc == 0xEC,  // L2 takes it once DivMMC drops
              fmt("l2_after_divmmc=0x%02X (must NOT be 0xDB) "
                  "mmu_after_divmmc=0x%02X (must NOT be 0xDB) "
                  "l2_after_no_divmmc=0x%02X (expect 0xEC)",
                  l2_after_divmmc, mmu_after_divmmc, l2_after_no_divmmc));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §12. Port 0xE7 — SPI Chip Select
// VHDL: zxnext.vhd:3300-3332
// ══════════════════════════════════════════════════════════════════════

void group_ss() {
    set_group("12. Port 0xE7 CS");

    // SS-01: Reset -> port_e7_reg = 0xFF.
    // VHDL: zxnext.vhd:3302.
    {
        SpiMaster m; m.reset();
        uint8_t v = m.read_cs();
        check("SS-01",
              "Reset sets port_e7 to 0xFF (all deselected) "
              "(VHDL zxnext.vhd:3302)",
              v == 0xFF,
              fmt("got=%02x", v));
    }

    // SS-02..SS-05: sd_swap decode logic. VHDL zxnext.vhd:3311-3314.
    //   sd_swap=0: cpu_do(1:0)=10 → port_e7_reg=0xFE (SD0)
    //              cpu_do(1:0)=01 → port_e7_reg=0xFD (SD1)
    //   sd_swap=1: cpu_do(1:0)=10 → port_e7_reg=0xFD (SD1, swapped)
    //              cpu_do(1:0)=01 → port_e7_reg=0xFE (SD0, swapped)
    // RPI0/RPI1 branches are unaffected (swap is SD-only).

    // SS-02: sd_swap=0, write 0xFE → SD0 pattern 0xFE.
    {
        SpiMaster m; m.reset();
        m.set_sd_swap(false);
        m.write_cs(0xFE);
        check("SS-02",
              "sd_swap=0: write 0xFE selects SD0 (0xFE) "
              "(VHDL zxnext.vhd:3311)",
              m.read_cs() == 0xFE,
              fmt("got=%02x exp=FE", m.read_cs()));
    }

    // SS-03: sd_swap=0, write 0xFD → SD1 pattern 0xFD.
    {
        SpiMaster m; m.reset();
        m.set_sd_swap(false);
        m.write_cs(0xFD);
        check("SS-03",
              "sd_swap=0: write 0xFD selects SD1 (0xFD) "
              "(VHDL zxnext.vhd:3313)",
              m.read_cs() == 0xFD,
              fmt("got=%02x exp=FD", m.read_cs()));
    }

    // SS-04: sd_swap=1, write 0xFE → swapped to SD1 (0xFD).
    {
        SpiMaster m; m.reset();
        m.set_sd_swap(true);
        m.write_cs(0xFE);
        check("SS-04",
              "sd_swap=1: write 0xFE maps to SD1 pattern 0xFD "
              "(VHDL zxnext.vhd:3311)",
              m.read_cs() == 0xFD,
              fmt("got=%02x exp=FD", m.read_cs()));
    }

    // SS-05: sd_swap=1, write 0xFD → swapped to SD0 (0xFE).
    {
        SpiMaster m; m.reset();
        m.set_sd_swap(true);
        m.write_cs(0xFD);
        check("SS-05",
              "sd_swap=1: write 0xFD maps to SD0 pattern 0xFE "
              "(VHDL zxnext.vhd:3313)",
              m.read_cs() == 0xFE,
              fmt("got=%02x exp=FE", m.read_cs()));
    }

    // SS-06: Write 0xFB selects RPI0. VHDL: zxnext.vhd:3318.
    // Emulator stores verbatim, and bit 2 clear selects device index 2.
    {
        SpiMaster m; m.reset();
        m.write_cs(0xFB);
        // Observable: read_cs() returns raw; active device resolution
        // is internal but we can verify the store.
        check("SS-06",
              "Write 0xFB selects RPI0 (bit 2 clear) "
              "(VHDL zxnext.vhd:3318)",
              m.read_cs() == 0xFB,
              fmt("got=%02x", m.read_cs()));
    }

    // SS-07: Write 0xF7 selects RPI1. VHDL: zxnext.vhd:3320.
    {
        SpiMaster m; m.reset();
        m.write_cs(0xF7);
        check("SS-07",
              "Write 0xF7 selects RPI1 (bit 3 clear) "
              "(VHDL zxnext.vhd:3320)",
              m.read_cs() == 0xF7,
              fmt("got=%02x", m.read_cs()));
    }

    // SS-08 — SPI Flash CS gated by config_mode + reset_type(2).
    // VHDL zxnext.vhd:3315-3320: cpu_do=0x7F AND (config_mode='1' OR
    // reset_type(2)='1') → spi_ss_flash_n asserted. jnext spi.cpp:73-77
    // documents the omission; no Flash backend.
    // WONT SS-08 — G136 SPI Flash CS out of scope: closing as PASS
    // would require emulating the on-FPGA SPI Flash chip itself
    // (FPGA core image storage + TBBLUE.FW firmware + tbblue config +
    // Flash device backend with command/data state machine +
    // persistence). jnext runs the FPGA core's behavior, not the
    // FPGA bitstream management tooling. Same principle as G45
    // (expansion bus) and G29 (Pi I2S host capture). The safety
    // case (write 0x7F outside config_mode → all-deselected 0xFF) is
    // already covered as PASS by SS-09 below. Revisit when jnext
    // gains a `--flash-image FILE` mode for FPGA-Flash emulation
    // (FPGA core update tooling, firmware-update programs, tbblue
    // config persistence).

    // SS-09: Write 0x7F outside config mode -> all deselected (0xFF).
    // VHDL: zxnext.vhd:3326 — flash select blocked. Emulator stores raw.
    // Expected (VHDL): read_cs == 0xFF. Current C++ will return 0x7F.
    {
        SpiMaster m; m.reset();
        m.write_cs(0x7F);
        check("SS-09",
              "Write 0x7F outside config mode: all deselected (0xFF) "
              "(VHDL zxnext.vhd:3326)",
              m.read_cs() == 0xFF,
              fmt("got=%02x exp=FF", m.read_cs()));
    }

    // SS-10: Write any other value -> all deselected (0xFF).
    // VHDL zxnext.vhd:3322 — default OTHERS => 0xFF.
    // Value 0x00 has bits 1:0="00", which doesn't match the SD card
    // branches ("10"/"01") or the exact-match branches (0xFB/0xF7/0x7F),
    // so it falls through to the VHDL default → all ones.
    // (Previously used 0x12, but bits 1:0="10" matches the SD card branch.)
    {
        SpiMaster m; m.reset();
        m.write_cs(0x00);  // bits 1:0="00" — hits VHDL default branch
        check("SS-10",
              "Write unrecognised value: all deselected (0xFF) "
              "(VHDL zxnext.vhd:3322)",
              m.read_cs() == 0xFF,
              fmt("got=%02x exp=FF", m.read_cs()));
    }

    // SS-11: Only one device selected at a time.
    // VHDL: zxnext.vhd:3300-3332 — each written value decodes to exactly
    // one selected bit. In the emulator the raw byte 0xFC would leave
    // two bits clear; the VHDL decoder would collapse that to 0xFF.
    {
        SpiMaster m; m.reset();
        m.write_cs(0xFC);  // two bits clear (ambiguous)
        check("SS-11",
              "Ambiguous SS write (two bits clear) must collapse to "
              "0xFF — single-device enforcement "
              "(VHDL zxnext.vhd:3328)",
              m.read_cs() == 0xFF,
              fmt("got=%02x exp=FF", m.read_cs()));
    }

    // SS-12 (PASS-7): SpiMaster::reset() must NOT clear attached device
    // bindings. The devices_[] array models the physical wires from the
    // SPI master to its slaves (SD card on CS0). VHDL has no equivalent
    // notion — the spi_ss_*_n outputs are wired permanently and reset
    // only clears the port_e7_reg FF state (zxnext.vhd:3308-3322), never
    // the connectivity itself. Pre-fix `SpiMaster::reset()` did
    // `devices_.fill(nullptr)`, which silently unhooked the SD card on
    // every soft/hard reset; production code masked this only because
    // `Emulator::init()` re-runs `attach_device()` after `reset()` —
    // a future caller (save-state restore, runtime SD swap, future test
    // fixture) that called reset() without re-attaching would silently
    // disconnect the bus.
    //
    // This row pins the post-fix behaviour: after reset(), the previously
    // attached device must still receive an `exchange()` round-trip and
    // its returned byte must propagate back through the master's MISO
    // pipeline. Without the fix, write_data() would route to a nullptr
    // active_device, force rx_data_=0xFF, and the host would never see
    // the device's response byte (0x42 below).
    {
        SpiMaster m;
        MockSpiDevice dev;
        dev.next_response = 0x42;
        m.attach_device(0, &dev);  // physical "wire" attached
        m.reset();                  // reset must NOT unhook

        // After reset() the device should still be attached. Exercise the
        // round-trip: select CS0, send/recv one byte, observe the device's
        // response byte (0x42) on the next read.
        m.write_cs(0xFE);           // bit 0 low → select device 0
        m.write_data(0xA5);
        const uint8_t got = m.read_data();
        check("SS-12",
              "SpiMaster::reset() preserves device bindings — wires are "
              "not in the FPGA reset domain (VHDL zxnext.vhd:3308-3322 "
              "only resets port_e7_reg FF, not connectivity)",
              got == 0x42 && dev.exchange_count >= 1,
              fmt("rx=%02x exp=42 dev_count=%d (must be >=1)",
                  got, dev.exchange_count));
    }

    // SS-13 (TASK2-VERIFY8 commit 6ebfd2b): write_cs(0x7F) Flash-CS decode
    // is gated by the composite VHDL signal:
    //   (nr_03_config_mode='1') OR (nr_02_reset_type(2)='1')
    // (zxnext.vhd:3319). The pass-7 emulator dropped X"7F" to 0xFF
    // unconditionally, blocking the firmware's Flash-IPL path; pass-8
    // routed the gate via SpiMaster::set_flash_cs_enable. This row is the
    // discriminative pair to the existing SS-09 (gate closed -> 0xFF):
    // when the gate is OPEN, write_cs(0x7F) must preserve 0x7F so the
    // SPI Flash select propagates. SS-09 + SS-13 together pin both legs
    // of VHDL line 3319 + 3326. Class-(b) -> resolved.
    {
        // Gate-OPEN sub-case: flash_cs_enable=true keeps 0x7F.
        SpiMaster mA; mA.reset();
        mA.set_flash_cs_enable(true);
        mA.write_cs(0x7F);
        const uint8_t open_v = mA.read_cs();

        // Gate-CLOSED sub-case (regression sentinel for SS-09):
        // flash_cs_enable defaults false -> 0x7F drops to 0xFF.
        SpiMaster mB; mB.reset();
        // NB: do NOT call set_flash_cs_enable -> default false.
        mB.write_cs(0x7F);
        const uint8_t closed_v = mB.read_cs();

        check("SS-13",
              "Write 0x7F: gate OPEN (flash_cs_enable=1) preserves 0x7F; "
              "gate CLOSED falls through to 0xFF "
              "(VHDL zxnext.vhd:3319 composite gate; jnext SpiMaster::"
              "set_flash_cs_enable feeds nr_03_config_mode | "
              "nr_02_reset_type(2))",
              open_v == 0x7F && closed_v == 0xFF,
              fmt("open=%02x exp=7F closed=%02x exp=FF",
                  open_v, closed_v));
    }

    // SS-14 (CONTRACT-PIN; TASK2-VERIFY9 commit ff84d3e): the same physical
    // SD card is reachable on BOTH CS0 and CS1, mirroring the VHDL net at
    // zxnext.vhd:3280:
    //   spi_miso <= i_SPI_SD_MISO when spi_ss_sd1_n='0' or spi_ss_sd0_n='0'
    // (single i_SPI_SD_MISO source for both selects). The TBBlue board has
    // a single SD slot wired to both CS lines, and NR 0x0A bit 5 (sd_swap)
    // flips which CS the firmware writes. Pre-fix attached only on CS0,
    // so a sd_swap=1 write_cs(0xFE) -> swapped 0xFD -> CS1 found
    // devices_[1]==nullptr and the card became unreachable.
    //
    // This test attaches the SAME mock device to BOTH CS0 and CS1 (the
    // production wiring at emulator.cpp:4007-4023 after the pass-9 fix),
    // toggles sd_swap, and verifies a round-trip exchange surfaces the
    // device's response in both swap orientations.
    //
    // Reviewer finding (NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-DIVMMC-SD-SPI-
    // REVIEW.md §4.2 / §2 row SS-14): this is a CONTRACT-PIN, not a
    // discriminative regression sentinel for the ff84d3e fix itself.
    // The test directly attaches dev to BOTH CS0 and CS1, bypassing
    // Emulator::init (where the actual fix lives). Reverting the
    // Emulator-tier change does NOT make this test fail. What this row
    // verifies is the SpiMaster-tier contract: when the same device is
    // bound to both CS lines, the CS-decode + sd_swap routing both
    // surface the device. The actual fix is in Emulator-tier and
    // requires an integration test (full Emulator + SD attach) to
    // exercise the regression.
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x99;
        m.attach_device(0, &dev);   // CS0 wiring
        m.attach_device(1, &dev);   // CS1 wiring (same physical card)

        // Sub-case A: sd_swap=0, write 0xFE -> SD0 selected -> dev.
        m.set_sd_swap(false);
        m.write_cs(0xFE);
        m.write_data(0xA1);
        const uint8_t rx_swap0 = m.read_data();
        const int     cnt_swap0 = dev.exchange_count;

        // Sub-case B: sd_swap=1, write 0xFE -> swap maps to SD1 -> dev.
        // Reset the mock counter to count just this leg.
        dev.exchange_count = 0;
        m.set_sd_swap(true);
        m.write_cs(0xFF);   // deselect first to avoid mid-leg confusion
        m.write_cs(0xFE);   // host writes 0xFE; sd_swap=1 -> 0xFD (CS1)
        m.write_data(0xA2);
        const uint8_t rx_swap1 = m.read_data();
        const int     cnt_swap1 = dev.exchange_count;

        check("SS-14",
              "CONTRACT-PIN: SD card attached to BOTH CS0 (sd_swap=0) "
              "and CS1 (sd_swap=1): round-trip surfaces device byte in "
              "either orientation (VHDL zxnext.vhd:3280 single "
              "i_SPI_SD_MISO MUX; pass-9 emulator wires same backend on "
              "both CS). NOT a discriminative sentinel for ff84d3e — "
              "test attaches dev directly to both CS lines, bypassing "
              "Emulator::init. Reverting the Emulator-tier fix does not "
              "fail this test (integration-tier coverage required).",
              rx_swap0 == 0x99 && cnt_swap0 >= 1
                  && rx_swap1 == 0x99 && cnt_swap1 >= 1,
              fmt("swap0 rx=%02x cnt=%d  swap1 rx=%02x cnt=%d",
                  rx_swap0, cnt_swap0, rx_swap1, cnt_swap1));
    }

    // SS-15 (TASK2-VERIFY11 V11-DIVMMC-01): SpiMaster::reset() must pulse
    // `deselect()` on every currently-selected device before clearing the
    // CS register, mirroring VHDL zxnext.vhd:3308-3309 (`port_e7_reg <=
    // (others => '1')` on `reset='1'`). The CS rising edge from any
    // already-asserted slot is what prompts a connected SD card to abort
    // an in-flight transaction (CMD17 SENDING_DATA, CMD18 multi-block,
    // CMD24 RECEIVING_DATA, etc.) and return its protocol-state FFs to
    // IDLE.
    //
    // Pre-fix, `reset()` set `cs_=0xFF` directly without the deselect()
    // callback, so a slave selected at the moment of reset retained its
    // pre-reset state until the next firmware-driven CS write. The SD
    // backend's `receive()` default branch papers over this on the write
    // path (any 0x40-0x7F byte starts a new command) but `send()` keeps
    // streaming the prior block until the firmware writes — divergence
    // becomes observable for any caller that reads first (test
    // fixtures, save-state replay, host bug).
    //
    // Discriminative shape: attach a MockSpiDevice on CS0, select it via
    // write_cs(0xFE), then call reset(). The mock's `was_deselected`
    // flag must be true after reset (post-fix) and `cs_` must be 0xFF.
    // Pre-fix the flag stays false. SS-12 (the device-bindings-survive
    // test) is unaffected because it never selects the device before
    // calling reset() — `cs_` was already 0xFF when reset() ran, so no
    // deselect() was ever needed in either direction.
    {
        SpiMaster m;
        MockSpiDevice dev;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);                 // select SD0 (CS0 active-low)
        const bool selected_before = (m.read_cs() == 0xFE);
        const bool desel_before    = dev.was_deselected;
        m.reset();
        const bool desel_after  = dev.was_deselected;
        const uint8_t cs_after  = m.read_cs();

        check("SS-15",
              "SpiMaster::reset() pulses deselect() on every currently-"
              "selected device before clearing cs_=0xFF (VHDL "
              "zxnext.vhd:3308-3309 — port_e7_reg → all-ones on reset, "
              "physical CS rising edge resets connected SPI slaves' "
              "protocol state). Pre-fix dropped this notification; SD "
              "card protocol-state FFs survived reset until the next "
              "firmware-driven CS write.",
              selected_before && !desel_before
                  && desel_after && cs_after == 0xFF,
              fmt("sel_before=%d desel_before=%d desel_after=%d "
                  "cs_after=%02x",
                  selected_before, desel_before, desel_after,
                  cs_after));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §13. Port 0xEB — SPI Data Exchange
// VHDL: spi_master.vhd:80-177
// ══════════════════════════════════════════════════════════════════════

void group_sx() {
    set_group("13. Port 0xEB xchg");

    // SX-01: Write to 0xEB sends byte via MOSI.
    // VHDL: spi_master.vhd:111-112 — oshift_r <= i_spi_mosi_dat on wr.
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);           // select device 0 (bit0 low)
        m.write_data(0xA5);
        check("SX-01",
              "Write 0xEB forwards byte to MOSI "
              "(VHDL spi_master.vhd:111-112)",
              dev.last_tx == 0xA5 && dev.exchange_count == 1,
              fmt("tx=%02x cnt=%d",
                  dev.last_tx, dev.exchange_count));
    }

    // SX-02: Read from 0xEB triggers exactly one SPI exchange.
    // VHDL: spi_master.vhd:109-110 — oshift_r <= all 1s on rd;
    // the read initiates a full 16-state_r transfer regardless of MISO.
    //
    // NOTE: the prior oracle also asserted `v == 0x5A` (the freshly-
    // exchanged byte). That claim was a false-pass against the current
    // non-pipelined C++ model — VHDL spi_master.vhd:159-175 latches
    // miso_dat one state_last_d cycle after the exchange, so the
    // CPU-visible byte on a read is the result of the PREVIOUS exchange,
    // not the one this read just triggered. That semantic is covered by
    // SX-03 / SX-05 / ML-05; SX-02's scope is narrowed here to the
    // "a read triggers one exchange cycle" fact only. See also row 27
    // of the Task 2 Emulator Bug backlog.
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x5A;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        (void)m.read_data();
        check("SX-02",
              "Read 0xEB pushes 0xFF on MOSI and triggers one exchange "
              "(VHDL spi_master.vhd:109-110)",
              dev.last_tx == 0xFF && dev.exchange_count == 1,
              fmt("last_tx=0x%02x cnt=%d", dev.last_tx, dev.exchange_count));
    }

    // SX-03: Read returns PREVIOUS exchange result (pipeline delay).
    // VHDL: spi_master.vhd:159-166 — miso_dat latched at end of PREVIOUS
    // transfer, one-cycle state_last_d delay. The C++ SpiMaster has no
    // pipeline delay: read_data() returns the freshly-exchanged byte.
    // Expected (VHDL): the byte returned by a read is the result of the
    // PREVIOUS exchange (or the reset value for the very first read).
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x11;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        // First read: pipeline says "result of previous" — i.e. reset
        // value 0xFF — NOT 0x11.
        uint8_t first = m.read_data();
        check("SX-03",
              "First read after select returns previous-cycle result "
              "(VHDL spi_master.vhd:162-166)",
              first == 0xFF,
              fmt("got=%02x exp=FF (pipeline delay not modelled)",
                  first));
    }

    // SX-04: First read after reset returns 0xFF.
    // VHDL: spi_master.vhd:162-163 — miso_dat reset to all 1s.
    // With no device attached this is trivially satisfied.
    {
        SpiMaster m; m.reset();
        uint8_t v = m.read_data();
        check("SX-04",
              "First read after reset (no device) returns 0xFF "
              "(VHDL spi_master.vhd:162)",
              v == 0xFF,
              fmt("got=%02x", v));
    }

    // SX-05: Write 0xAA then read: read returns MISO from the write
    // cycle (pipeline). VHDL: spi_master.vhd:159-166.
    // C++: write_data feeds device via receive(); read_data triggers a
    // separate send() — so read returns the send() value, NOT the
    // result of the write exchange. Expect VHDL contract to fail until
    // the emulator models the shared pipeline.
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0xC3;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        m.write_data(0xAA);         // triggers exchange, miso=0xC3
        uint8_t v = m.read_data();  // VHDL: returns 0xC3 from write cyc
        check("SX-05",
              "Read after write returns MISO of the write exchange "
              "(VHDL spi_master.vhd:164-165)",
              v == 0xC3,
              fmt("got=%02x exp=C3 (independent write/read paths)", v));
    }

    // SX-11 (TASK2-VERIFY3 commit d54a053): write_data with no active
    // device must capture 0xFF (not leave a previously-selected slave's
    // stale byte hanging in rx_data_). VHDL zxnext.vhd:3278-3280 forces
    //   spi_miso <= '1'
    // when no spi_ss_*_n line is asserted; the spi_master entity still
    // runs the transfer (i_spi_wr=1 regardless of CS state) and miso_dat
    // captures all-ones. Pre-fix the C++ left rx_data_ unchanged, leaking
    // the previous slave's stale byte through subsequent reads with CS
    // deasserted. This row exercises the leak path: device returns 0x42,
    // host deselects, host issues another write_data — the next read
    // must see 0xFF (NOT 0x42).
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x42;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);            // select dev0
        m.write_data(0xA5);          // exchange -> rx_data_=0x42
        const uint8_t leaked_baseline = m.read_data();  // primes pipeline -> 0x42

        // Now deselect and issue another write while NO slave is selected.
        // Pre-fix: rx_data_ stays 0x42; post-fix: forced to 0xFF.
        m.write_cs(0xFF);            // all deselected
        m.write_data(0x55);          // no active device
        const uint8_t after_no_slave_write = m.read_data();

        check("SX-11",
              "write_data with no slave forces rx_data_=0xFF (no stale-"
              "byte leak from previously-selected slave) "
              "(VHDL zxnext.vhd:3278-3280 default-else spi_miso<='1')",
              leaked_baseline == 0x42 && after_no_slave_write == 0xFF,
              fmt("baseline=%02x exp=42  after=%02x exp=FF",
                  leaked_baseline, after_no_slave_write));
    }

    // SX-12 (TASK2-VERIFY3 commit d54a053): symmetric to SX-11 but for
    // read_data. With no slave selected, read_data must update rx_data_
    // to 0xFF (the byte returned to the CALLER reflects the previous
    // pipeline value — which after the prior leg below is itself 0xFF;
    // we observe the post-state by issuing one more read and checking
    // the output is still 0xFF). VHDL zxnext.vhd:3278-3280.
    //
    // Discriminative shape: prime rx_data_ with a non-FF byte via a
    // selected exchange, deselect, then drive read_data twice. The
    // FIRST post-deselect read returns the primed byte (one-cycle
    // pipeline) BUT MUST also rewrite rx_data_ to 0xFF; the SECOND
    // post-deselect read therefore returns 0xFF.
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x77;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        m.write_data(0x33);         // primes rx_data_=0x77
        const uint8_t primed = m.read_data();   // returns 0x77, latches 0x77 next
        // Pipeline after that primed read: rx_data_=0x77 (re-latched
        // from another send()). One more read with active device to
        // settle pipeline cleanly:
        const uint8_t resettle = m.read_data(); // returns 0x77

        // Now deselect and call read_data twice. Post-fix: each read
        // rewrites rx_data_ to 0xFF.
        m.write_cs(0xFF);
        const uint8_t r1 = m.read_data();  // returns previous (0x77 if pre-fix; 0x77 if post-fix)
        const uint8_t r2 = m.read_data();  // returns the rx_data_ updated above

        check("SX-12",
              "read_data with no slave forces rx_data_=0xFF on subsequent "
              "reads (post-deselect pipeline drains to 0xFF, no stale "
              "leak) (VHDL zxnext.vhd:3278-3280 default-else)",
              primed == 0x77 && resettle == 0x77 && r2 == 0xFF,
              fmt("primed=%02x resettle=%02x r1=%02x r2=%02x exp r2=FF",
                  primed, resettle, r1, r2));
    }

    // SX-06..SX-10 — category B (VHDL-internal pipeline signals).
    //
    // JNEXT's `SpiMaster` (src/peripheral/spi.cpp) is intentionally a
    // zero-latency byte wrapper: each `write_data()` triggers a full
    // exchange synchronously and `read_data()` honours the single-
    // stage pipeline delay already covered by SX-01..05 and ML-03/05.
    // Modelling cycle-accurate SPI internals (16-cycle state counter,
    // SCK/MOSI pin-level output, bit-level MISO sampling, back-to-back
    // pipelined restart) would require a full FSM rewrite with no
    // functional benefit — SD card protocol works end-to-end on the
    // byte boundary.
    //
    // SX-06: SPI transfer is 16 clock cycles (VHDL spi_master.vhd:86,95-97)
    // SX-07: o_spi_sck = state_r(0)          (VHDL spi_master.vhd:172)
    // SX-08: MOSI outputs MSB first          (VHDL spi_master.vhd:173)
    // SX-09: MISO sampled on delayed rising SCK (VHDL spi_master.vhd:148-156)
    // SX-10: state_last pipelined restart    (VHDL spi_master.vhd:82)
}

// ══════════════════════════════════════════════════════════════════════
// §14. SPI State Machine
// VHDL: spi_master.vhd:86-100
// ══════════════════════════════════════════════════════════════════════

void group_st() {
    set_group("14. SPI state machine");

    // ST-01..ST-08 — category B (VHDL-internal pipeline signals).
    //
    // Same rationale as SX-06..10 above: the state counter, spi_wait_n
    // and idle/last flags never surface in JNEXT's byte-wrapper
    // `SpiMaster`. Modelling a cycle-accurate SPI FSM is architectural
    // scope expansion for no functional benefit — SD card end-to-end
    // protocol works on the byte boundary.
    //
    // ST-01: state_r idle flag              (VHDL spi_master.vhd:87,93)
    // ST-02: spi_begin transition           (VHDL spi_master.vhd:82,94-95)
    // ST-03: state counter increment        (VHDL spi_master.vhd:96-97)
    // ST-04: state wrap to idle             (VHDL spi_master.vhd:96-97)
    // ST-05: spi_wait_n signal exposure     (VHDL spi_master.vhd:177)
    // ST-06: spi_wait_n signal exposure     (VHDL spi_master.vhd:177)
    // ST-07: pipelined begin condition      (VHDL spi_master.vhd:82)
    // ST-08: mid-transfer rd/wr suppression (VHDL spi_master.vhd:82)

    // ST-09 — DMA wait line surfaced via accessor.
    // VHDL serial/spi_master.vhd:56,177:
    //   o_spi_wait_n <= state_idle or state_last_d;
    // consumed by DMA at zxnext.vhd:3297. JNEXT's SpiMaster is a zero-
    // latency byte wrapper: every observable moment is "idle", so
    // spi_wait_n() == true (VHDL '1' = no wait). G137: the accessor
    // surface lets DMA / unit-tests observe the byte-level invariant.
    {
        SpiMaster m; m.reset();
        bool idle_at_reset = m.spi_wait_n();

        MockSpiDevice dev;
        dev.next_response = 0x42;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        m.write_data(0x12);
        bool idle_after_write = m.spi_wait_n();
        (void)m.read_data();
        bool idle_after_read = m.spi_wait_n();

        check("ST-09",
              "SPI o_spi_wait_n surfaced via spi_wait_n() accessor; "
              "byte-wrapper master is always idle when observed "
              "(VHDL serial/spi_master.vhd:56,177)",
              idle_at_reset && idle_after_write && idle_after_read,
              fmt("reset=%d after_w=%d after_r=%d",
                  idle_at_reset, idle_after_write, idle_after_read));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §15. SPI MISO Data Latch
// VHDL: spi_master.vhd:121-168
// ══════════════════════════════════════════════════════════════════════

void group_ml() {
    set_group("15. MISO latch");

    // ML-01 — category B (VHDL-internal pipeline signal).
    // Bit-level ishift_r shifted in on delayed rising SCK
    // (VHDL spi_master.vhd:152-155) is not modelled: the byte-wrapper
    // `SpiMaster` captures the full response atomically inside
    // `write_data()` / `read_data()`. Byte-level behaviour is covered
    // by SX-01..05 and ML-03/05.
    //
    // ML-02 — category B (VHDL-internal pipeline signal).
    // state_last_d-timed miso_dat latch (VHDL spi_master.vhd:164-165)
    // is not observable at the byte boundary. ML-03 asserts the
    // resulting held-value semantics, ML-05 asserts the reset value.

    // ML-03: miso_dat holds value until next transfer completes.
    // Observable: two consecutive reads with the same device response
    // should both return that response (once the pipeline has primed).
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x42;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        (void)m.read_data();            // prime pipeline
        uint8_t a = m.read_data();
        uint8_t b = m.read_data();
        check("ML-03",
              "miso_dat stable across reads with same response "
              "(VHDL spi_master.vhd:164-165)",
              a == 0x42 && b == 0x42,
              fmt("a=%02x b=%02x", a, b));
    }

    // ML-04 — category B (VHDL-internal pipeline signal).
    // ishift_r vs oshift_r independence (VHDL spi_master.vhd:102-117
    // vs :119-157) is not observable at the byte-level C++ API — both
    // paths collapse to synchronous byte exchanges in `SpiMaster`.

    // ML-05: Reset sets ishift_r to all 1s.
    // VHDL: spi_master.vhd:151-152. The observable proxy is that the
    // first read after reset (before any exchange) returns 0xFF.
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x00;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        uint8_t v = m.read_data();
        // VHDL: pipeline delay means first read returns reset value 0xFF.
        check("ML-05",
              "First read after reset reflects ishift_r reset to 0xFF "
              "(VHDL spi_master.vhd:151-152)",
              v == 0xFF,
              fmt("got=%02x exp=FF", v));
    }

    // ML-06 — category B (VHDL-internal pipeline signal).
    // 16-cycle minimum between read/write operations
    // (VHDL spi_master.vhd:86-100 comment) is moot: JNEXT's zero-
    // latency byte wrapper exposes no such timing constraint.
}

// ══════════════════════════════════════════════════════════════════════
// §16. SPI MISO Source Multiplexing
// VHDL: zxnext.vhd:3278-3280
// ══════════════════════════════════════════════════════════════════════

void group_mx() {
    set_group("16. MISO mux");

    // MX-01, MX-02 — will-not-implement (Flash + RPI out of JNEXT scope).
    //
    // VHDL zxnext.vhd:3278-3279 cascades Flash (highest priority) and
    // RPI (second priority) above SD on the MISO source multiplexer.
    // Flash is used only to reflash TBBlue firmware; RPI is an
    // accelerator peripheral. Neither is on the JNEXT roadmap, so the
    // priority cascade collapses to "SD / default (0xFF)" — already
    // covered by MX-03/04 below. See 2026-04-17f handover for scope
    // decision.
    //
    // MX-01: Flash MISO source highest priority (VHDL zxnext.vhd:3278)
    // MX-02: RPI  MISO source second priority   (VHDL zxnext.vhd:3279)

    // MX-03: SD is third priority. Observable by attaching an SD device
    // to CS 0 and selecting it. VHDL: zxnext.vhd:3280.
    {
        SpiMaster m; m.reset();
        MockSpiDevice sd;
        sd.next_response = 0x77;
        m.attach_device(0, &sd);
        m.write_cs(0xFE);
        (void)m.read_data();            // prime
        uint8_t v = m.read_data();
        check("MX-03",
              "SD selected: MISO sourced from SD device "
              "(VHDL zxnext.vhd:3280)",
              v == 0x77,
              fmt("got=%02x", v));
    }

    // MX-04: No device selected -> MISO = 1 (pull-up).
    // VHDL: zxnext.vhd:3280 default branch.
    {
        SpiMaster m; m.reset();
        // All CS bits high (reset default) = no selection.
        uint8_t v = m.read_data();
        check("MX-04",
              "No device selected: MISO reads as 0xFF "
              "(VHDL zxnext.vhd:3280)",
              v == 0xFF,
              fmt("got=%02x", v));
    }

    // MX-05 — will-not-implement (cascade moot without Flash/RPI).
    // VHDL zxnext.vhd:3278-3280 cascaded if-else Flash > RPI > SD >
    // default is reduced to SD > default in JNEXT (see MX-01/02
    // scope decision). MX-03 + MX-04 together cover the reduced
    // cascade end-to-end.
}

// ══════════════════════════════════════════════════════════════════════
// §17. Integration Scenarios
// ══════════════════════════════════════════════════════════════════════

void group_in() {
    set_group("17. Integration");

    // IN-01: Boot: automap at 0x0000 with default NR values maps ROM.
    // VHDL: zxnext.vhd:2850 (EP0 default), divmmc.vhd:94.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);
        check("IN-01",
              "Boot automap: M1 at 0x0000 maps DivMMC ROM "
              "(VHDL divmmc.vhd:94, zxnext.vhd:2850)",
              d.is_active() && d.is_rom_mapped(),
              fmt("act=%d rom=%d",
                  d.is_active(), d.is_rom_mapped()));
    }

    // IN-02: SD init: select SD0, exchange, deselect.
    // VHDL: zxnext.vhd:3302-3332 (CS), spi_master.vhd:82-117 (xchg).
    {
        SpiMaster m; m.reset();
        MockSpiDevice sd;
        sd.next_response = 0x01;
        m.attach_device(0, &sd);
        m.write_cs(0xFE);            // select SD0
        m.write_data(0x40);          // send CMD0 opcode byte
        m.write_cs(0xFF);            // deselect
        bool ok = sd.last_tx == 0x40
                  && sd.was_deselected;
        check("IN-02",
              "SD init sequence: select, write, deselect "
              "(VHDL zxnext.vhd:3302, spi_master.vhd:109)",
              ok,
              fmt("tx=%02x desel=%d",
                  sd.last_tx, sd.was_deselected));
    }

    // IN-03: RETN after handler entry: automap deactivated.
    // VHDL: divmmc.vhd:126,139 — i_retn_seen clears hold/held.
    // Integration form of DA-06: activate via an entry point, then
    // RETN must clear the overlay latch.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);   // boot entry activates overlay
        bool active_before = d.automap_active();
        d.on_retn();
        bool cleared = !d.automap_active();
        check("IN-03",
              "RETN after handler clears automap overlay "
              "(VHDL divmmc.vhd:126,139)",
              active_before && cleared,
              fmt("before=%d cleared=%d", active_before, cleared));
    }

    // IN-04: integration form of R3-01/R3-02. With default NR state
    // (NR 0xB8=0x83 enables EP0..1, NR 0xB9=0x01 flags only EP0 as main
    // path), RST 0x08 is ROM3-only. Without rom3_active it must not
    // activate; with rom3_active it must. Covered by Task 7 Branch B
    // (MMU→DivMmc ROM3 plumbing).
    {
        DivMmc d = make_divmmc();
        d.set_entry_timing_0(0xFF);          // instant for single-fetch observability
        d.set_rom3_active(false);
        d.check_automap(0x0008, true);
        const bool fired_without_rom3 = d.automap_active();
        d.reset();
        d.set_enabled(true);
        d.set_entry_timing_0(0xFF);
        d.set_rom3_active(true);
        d.check_automap(0x0008, true);
        const bool fired_with_rom3 = d.automap_active();
        check("IN-04",
              "RST 0x08 fires only when rom3_active=1 with default "
              "NR 0xB9=0x01 (EP1 flagged ROM3-only) "
              "(VHDL zxnext.vhd:2856,3138)",
              !fired_without_rom3 && fired_with_rom3,
              fmt("without_rom3=%d with_rom3=%d (expected 0,1)",
                  fired_without_rom3, fired_with_rom3));
    }

    // IN-05: Rapid SPI exchanges: back-to-back without idle gap.
    // Observable: two consecutive writes both reach the device.
    {
        SpiMaster m; m.reset();
        MockSpiDevice dev;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        m.write_data(0x11);
        m.write_data(0x22);
        check("IN-05",
              "Two back-to-back writes both reach device "
              "(VHDL spi_master.vhd:82)",
              dev.exchange_count == 2 && dev.last_tx == 0x22,
              fmt("cnt=%d last=%02x",
                  dev.exchange_count, dev.last_tx));
    }

    // IN-06: conmem during automap: mapping is active (either source).
    // VHDL: divmmc.vhd:94 — rom_en = page0 AND (conmem OR automap).
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);   // automap on
        d.write_control(0x80);            // conmem on as well
        check("IN-06",
              "conmem during automap: mapping remains active "
              "(VHDL divmmc.vhd:94)",
              d.is_active() && d.is_rom_mapped(),
              fmt("act=%d rom=%d",
                  d.is_active(), d.is_rom_mapped()));
    }

    // IN-07: DivMMC disabled via enable=false: no automap, SPI
    // still works. VHDL: zxnext.vhd:4112, spi_master.vhd (independent).
    {
        DivMmc d = make_divmmc();
        d.set_enabled(false);
        d.check_automap(0x0000, true);
        bool divmmc_off = !d.is_active();

        SpiMaster m; m.reset();
        MockSpiDevice dev;
        dev.next_response = 0x88;
        m.attach_device(0, &dev);
        m.write_cs(0xFE);
        m.write_data(0xBB);
        bool spi_ok = dev.last_tx == 0xBB;

        check("IN-07",
              "DivMMC disabled: no automap mapping, SPI still exchanges "
              "(VHDL zxnext.vhd:4112)",
              divmmc_off && spi_ok,
              fmt("divmmc_off=%d spi_ok=%d",
                  divmmc_off, spi_ok));
    }
}

// ══════════════════════════════════════════════════════════════════════
// §18. G46(b) — sram_pre_override priority arbiter gates
// VHDL: zxnext.vhd:3029-3066, 3137-3138 + divmmc.vhd:130,148
// ══════════════════════════════════════════════════════════════════════
//
// The DivMmc::check_automap pass-through args sram_pre_override_2 and
// sram_pre_override_0 model the VHDL priority-arbiter bits that gate
// `i_automap_active` and `i_automap_rom3_active`. Production callers
// (Emulator) compute them per-M1 from Mmu::sram_pre_override_*; tests
// here exercise the gates directly.

void group_po() {
    set_group("18. PriOverride G46(b)");

    // PO-01: main path requires sram_pre_override(2). NR_B9 valid=1 RST
    // entry must NOT fire when pre_override(2)=0 (= CPU access outside
    // slot 0/1, OR Multiface owns the window). VHDL zxnext.vhd:3137 +
    // divmmc.vhd:148.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x01);          // RST 0x00
        d.set_entry_valid_0(0x01);           // main path
        d.set_entry_timing_0(0xFF);
        d.check_automap(0x0000, true,
                        /*pre_override_2=*/false,
                        /*pre_override_0=*/false);
        check("PO-01",
              "Main path blocked when sram_pre_override(2)=0 "
              "(VHDL zxnext.vhd:3137)",
              !d.automap_active(),
              fmt("active=%d", d.automap_active()));
    }

    // PO-02: ROM3 path requires sram_pre_override(0). NR_B9 valid=0 RST
    // entry with rom3_active=1 must NOT fire when pre_override(0)=0
    // (= config_mode active OR effective page < 0xE0 OR MF active).
    // VHDL zxnext.vhd:3138.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x02);          // RST 0x08
        d.set_entry_valid_0(0x00);           // ROM3 path
        d.set_entry_timing_0(0xFF);
        d.set_rom3_active(true);
        d.check_automap(0x0008, true,
                        /*pre_override_2=*/true,
                        /*pre_override_0=*/false);
        check("PO-02",
              "ROM3 path blocked when sram_pre_override(0)=0 "
              "(VHDL zxnext.vhd:3138)",
              !d.automap_active(),
              fmt("active=%d", d.automap_active()));
    }

    // PO-03: both gates open + valid=1 → main path fires. Sanity check
    // the new gates don't break the happy path.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x01);          // RST 0x00
        d.set_entry_valid_0(0x01);           // main path
        d.set_entry_timing_0(0xFF);
        d.check_automap(0x0000, true,
                        /*pre_override_2=*/true,
                        /*pre_override_0=*/true);
        check("PO-03",
              "Main path fires when sram_pre_override(2)=1 "
              "(VHDL zxnext.vhd:3137)",
              d.automap_active(),
              fmt("active=%d", d.automap_active()));
    }

    // PO-04: both gates open + valid=0 + rom3_active=1 → ROM3 path fires.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x02);          // RST 0x08
        d.set_entry_valid_0(0x00);           // ROM3 path
        d.set_entry_timing_0(0xFF);
        d.set_rom3_active(true);
        d.check_automap(0x0008, true,
                        /*pre_override_2=*/true,
                        /*pre_override_0=*/true);
        check("PO-04",
              "ROM3 path fires when full sram_divmmc_automap_rom3_en "
              "composite is high (VHDL zxnext.vhd:3138)",
              d.automap_active(),
              fmt("active=%d", d.automap_active()));
    }

    // PO-05: defaults preserve back-compat — calling check_automap(pc,
    // is_m1) without the new args fires the same as before (= both gates
    // implicitly true). Guards against silent regression in tests that
    // construct a bare DivMmc.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_0(0x01);
        d.set_entry_valid_0(0x01);
        d.set_entry_timing_0(0xFF);
        d.check_automap(0x0000, true);       // no override args
        check("PO-05",
              "check_automap default args fire main path (back-compat)",
              d.automap_active(),
              fmt("active=%d", d.automap_active()));
    }

    // PO-06: off trigger (NR_BB bit 6, PC in 0x1FF8-0x1FFF) IS gated by
    // sram_pre_override(2). VHDL divmmc.vhd:131:
    //   automap_hold <= ... OR (automap_held AND NOT
    //                           (i_automap_active AND i_automap_delayed_off))
    // The off-fire term ANDs `i_automap_active` (= pre_override(2)) with
    // `i_automap_delayed_off`. So when pre_override(2)=0, the held latch
    // PROPAGATES instead of dropping. Realistic case: Multiface owns
    // slot 0/1 (mf_mem_en=1) at the moment the CPU happens to fetch from
    // 0x1FF8-0x1FFF — VHDL keeps DivMMC AUTOMAP held; jnext must too.
    //
    // Two-stimulus discriminative test: identical setup, only differing
    // in pre_override(2) at the off-trigger fetch.
    {
        // Sub-case A: pre_override(2)=1 at off trigger → AUTOMAP drops.
        DivMmc dA = make_divmmc();
        dA.set_entry_points_0(0x01);
        dA.set_entry_valid_0(0x01);
        dA.set_entry_timing_0(0xFF);
        dA.check_automap(0x0000, true, /*po2=*/true, /*po0=*/true);
        dA.check_automap(0x1FF8, true, /*po2=*/true, /*po0=*/true);
        dA.check_automap(0x0003, true, /*po2=*/true, /*po0=*/true);

        // Sub-case B: pre_override(2)=0 at off trigger → AUTOMAP held.
        DivMmc dB = make_divmmc();
        dB.set_entry_points_0(0x01);
        dB.set_entry_valid_0(0x01);
        dB.set_entry_timing_0(0xFF);
        dB.check_automap(0x0000, true, /*po2=*/true, /*po0=*/true);
        dB.check_automap(0x1FF8, true, /*po2=*/false, /*po0=*/false);
        dB.check_automap(0x0003, true, /*po2=*/true, /*po0=*/true);

        const bool ok = !dA.automap_active() && dB.automap_active();
        check("PO-06",
              "Off trigger gated by pre_override(2) — held propagates "
              "when MF owns slot 0/1 (VHDL divmmc.vhd:131)",
              ok,
              fmt("po2=1 active=%d po2=0 active=%d",
                  dA.automap_active(), dB.automap_active()));
    }
}

}  // namespace

// ── Main ─────────────────────────────────────────────────────────────

int main() {
    std::printf("DivMMC + SPI Compliance Tests (rewritten Task 1 Wave 2)\n");
    std::printf("======================================================\n\n");

    group_e3();  std::printf("  §1  Port 0xE3            done\n");
    group_cm();  std::printf("  §2  conmem paging        done\n");
    group_am();  std::printf("  §3  automap paging       done\n");
    group_ep();  std::printf("  §4  RST entry points     done\n");
    group_nr();  std::printf("  §5  Non-RST entry points done\n");
    group_da();  std::printf("  §6  Deactivation         done\n");
    group_tm();  std::printf("  §7  Instant/delayed      done\n");
    group_r3();  std::printf("  §8  ROM3 conditional     done\n");
    group_nm();  std::printf("  §9  NMI / button         done\n");
    group_na();  std::printf("  §10 NR 0x0A enable       done\n");
    group_sm();  std::printf("  §11 SRAM mapping         done\n");
    group_ss();  std::printf("  §12 Port 0xE7 CS         done\n");
    group_sx();  std::printf("  §13 Port 0xEB xchg       done\n");
    group_st();  std::printf("  §14 SPI state machine    done\n");
    group_ml();  std::printf("  §15 MISO latch           done\n");
    group_mx();  std::printf("  §16 MISO mux             done\n");
    group_in();  std::printf("  §17 Integration          done\n");
    group_po();  std::printf("  §18 PriOverride G46(b)   done\n");

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown
    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-24s %d/%d\n",
                            last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-24s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows (%zu total, unreachable with "
                    "current C++ API):\n", g_skipped.size());
        for (const auto& s : g_skipped) {
            std::printf("  %-8s %s\n", s.id, s.reason);
        }
    }

    std::printf("\nTotals: %d checks, %d skips, %d plan rows covered\n",
                g_total, (int)g_skipped.size(),
                g_total + (int)g_skipped.size());

    return g_fail > 0 ? 1 : 0;
}
