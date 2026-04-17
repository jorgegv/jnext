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
#include "peripheral/spi.h"

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

// Fresh enabled DivMmc with default (soft-reset) NR 0xB8/B9/BA/BB values.
DivMmc make_divmmc() {
    DivMmc d;
    d.reset();
    d.set_enabled(true);
    return d;
}

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
    // VHDL: zxnext.vhd:~4186 — nr_09_we AND nr_wr_dat(3) clears bit 6.
    // No C++ hook exists (DivMmc has no clear_mapram accessor, and the
    // NextReg 0x09 handler does not reach DivMmc).
    skip("E3-05",
         "No emulator path: NR 0x09[3] does not reach DivMmc::mapram_ "
         "(VHDL zxnext.vhd:~4186)");

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
    {
        DivMmc d = make_divmmc();
        trigger(d);                      // activate at 0x0000
        d.check_automap(0x1FF8, true);   // deactivation range
        bool ok = !d.automap_active() && !d.is_active();
        check("AM-04",
              "automap deactivates on M1 at 0x1FF8 "
              "(VHDL divmmc.vhd:131)",
              ok,
              fmt("automap=%d act=%d",
                  d.automap_active(), d.is_active()));
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

    // EP-09: Setting NR 0xBA[0]=1 makes EP0 use instant_on timing.
    // VHDL: zxnext.vhd:2852, divmmc.vhd:148. No C++ NR BA handler exists
    // on DivMmc — set_entry_timing_0 setter is available but the
    // check_automap path does not consult it (instant vs delayed is
    // collapsed). Observable effect identical either way in the current
    // model, so there is no distinguishing assertion.
    skip("EP-09",
         "No emulator path: DivMmc::check_automap collapses instant vs "
         "delayed timing, NR 0xBA has no observable effect");

    // EP-10: Setting NR 0xB9[1]=1 makes 0x0008 an unconditional automap
    // (not ROM3 conditional). No C++ handler — DivMmc has no
    // set_entry_valid_0 effect on check_automap.
    skip("EP-10",
         "No emulator path: DivMmc::check_automap ignores NR 0xB9 valid "
         "flag (VHDL zxnext.vhd:2856)");

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
    // VHDL: zxnext.vhd:2908. C++ DivMmc does not implement 0x3Dxx at all
    // — leave failing. Expected (VHDL): NOT activated without ROM3.
    // Since C++ does nothing at 0x3Dxx, this passes vacuously.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x3D00, true);
        check("NR-06",
              "M1 at 0x3D00 with BB[7]=1 and no ROM3: automap must NOT "
              "activate (VHDL zxnext.vhd:2908)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-07: M1 at 0x3DFF with BB[7]=1 and no ROM3: no activation.
    // VHDL: zxnext.vhd:2908 — range any 0x3Dxx.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x3DFF, true);
        check("NR-07",
              "M1 at 0x3DFF with BB[7]=1 and no ROM3: no automap "
              "(VHDL zxnext.vhd:2908)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // NR-08: Disable BB[7]=0, M1 at 0x3D00: no trigger.
    // VHDL: zxnext.vhd:2908. C++ DivMmc does not handle 0x3Dxx, so this
    // passes vacuously — but the observable contract still holds.
    {
        DivMmc d = make_divmmc();
        d.set_entry_points_1(0xCD & ~0x80);  // clear bit 7
        d.check_automap(0x3D00, true);
        check("NR-08",
              "M1 at 0x3D00 with BB[7]=0: no automap "
              "(VHDL zxnext.vhd:2908)",
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

    // DA-01: M1 at 0x1FF8 with automap held: deactivates.
    // VHDL: zxnext.vhd NR BB[6]=1; divmmc.vhd:131.
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);   // activate
        d.check_automap(0x1FF8, true);   // deactivate
        check("DA-01",
              "M1 at 0x1FF8 deactivates held automap "
              "(VHDL divmmc.vhd:131)",
              !d.automap_active(),
              fmt("automap=%d", d.automap_active()));
    }

    // DA-02: M1 at 0x1FFF: deactivates (upper bound).
    {
        DivMmc d = make_divmmc();
        d.check_automap(0x0000, true);
        d.check_automap(0x1FFF, true);
        check("DA-02",
              "M1 at 0x1FFF deactivates held automap "
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
    // VHDL: divmmc.vhd:108 (button_nmi clear), :126-139 (hold/held
    // clear on i_retn_seen). No C++ hook — DivMmc has no
    // notify_retn/on_retn method.
    skip("DA-06",
         "No emulator path: DivMmc has no RETN hook "
         "(VHDL divmmc.vhd:108,126,139)");

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

    // DA-08: automap_reset (port_divmmc_io_en=0 OR NR 0x0A[4]=0)
    // clears automap. VHDL: divmmc.vhd:126-139 (i_automap_reset branch).
    // C++ surface: set_enabled(false) is the only path and it merely
    // gates is_active(), it does NOT clear automap_active_.
    skip("DA-08",
         "No emulator path: set_enabled(false) gates visibility but does "
         "not clear automap_active_ latch (VHDL divmmc.vhd:126)");
}

// ══════════════════════════════════════════════════════════════════════
// §7. Automap Timing — Instant vs Delayed
// VHDL: divmmc.vhd:123-148
// ══════════════════════════════════════════════════════════════════════

void group_tm() {
    set_group("7. Instant vs delayed");

    // TM-01..TM-05: all require modelling of automap_hold/automap_held
    // pipeline (two-process M1+MREQ edge latch). The C++ model collapses
    // everything into a single automap_active_ boolean latched on
    // check_automap() so there is nothing to distinguish instant vs
    // delayed, nothing to observe at MREQ rising edge, nothing to
    // probe mid-M1.
    skip("TM-01",
         "No emulator path: automap_hold/held pipeline not modelled "
         "(VHDL divmmc.vhd:123-148)");
    skip("TM-02",
         "No emulator path: delayed_on pipeline not modelled "
         "(VHDL divmmc.vhd:129)");
    skip("TM-03",
         "No emulator path: MREQ_n rising-edge latch not modelled "
         "(VHDL divmmc.vhd:141-142)");
    skip("TM-04",
         "No emulator path: M1+MREQ gating for automap_hold not modelled "
         "(VHDL divmmc.vhd:128)");
    skip("TM-05",
         "No emulator path: automap_held persistence across non-off "
         "fetches not modelled (VHDL divmmc.vhd:131)");
}

// ══════════════════════════════════════════════════════════════════════
// §8. Automap ROM3-Conditional Activation
// VHDL: zxnext.vhd:3137-3138
// ══════════════════════════════════════════════════════════════════════

void group_r3() {
    set_group("8. ROM3 conditional");

    // R3-01..R3-03: require ROM3/altrom/Layer2/ROMCS state to be
    // observable to DivMmc. None of these signals are plumbed into
    // DivMmc — it has no i_automap_rom3_active input equivalent.
    skip("R3-01",
         "No emulator path: sram_divmmc_automap_rom3_en not modelled "
         "(VHDL zxnext.vhd:3137-3138)");
    skip("R3-02",
         "No emulator path: ROM page selection not observable to DivMmc "
         "(VHDL zxnext.vhd:3137)");
    skip("R3-03",
         "No emulator path: Layer 2 mapping override not observable to "
         "DivMmc (VHDL zxnext.vhd:3137)");

    // R3-04: sram_divmmc_automap_en = sram_pre_override(2). Roughly
    // corresponds to the i_en gate modelled in CM-09.
    // VHDL: zxnext.vhd:3137.
    {
        DivMmc d;
        d.reset();
        d.set_enabled(true);
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

    // NM-01..NM-08: DivMmc has no button_nmi_ flag, no
    // set_divmmc_button() API, no o_disable_nmi output. Entire NMI
    // lifecycle is absent from the emulator.
    skip("NM-01",
         "No emulator path: button_nmi flag not modelled "
         "(VHDL divmmc.vhd:108-111)");
    skip("NM-02",
         "No emulator path: automap_nmi_*_on signals not gated on "
         "button_nmi (VHDL divmmc.vhd:120-121)");
    skip("NM-03",
         "No emulator path: button_nmi gating not modelled "
         "(VHDL divmmc.vhd:120)");
    skip("NM-04",
         "No emulator path: button_nmi reset clear not modelled "
         "(VHDL divmmc.vhd:108)");
    skip("NM-05",
         "No emulator path: button_nmi automap_reset clear not modelled "
         "(VHDL divmmc.vhd:108)");
    skip("NM-06",
         "No emulator path: button_nmi RETN clear not modelled "
         "(VHDL divmmc.vhd:108)");
    skip("NM-07",
         "No emulator path: button_nmi clear on automap_held=1 not "
         "modelled (VHDL divmmc.vhd:112-113)");
    skip("NM-08",
         "No emulator path: o_disable_nmi output not exposed "
         "(VHDL divmmc.vhd:150)");
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

    // NA-02: NR 0x0A[4]=1 releases automap_reset -> automap can function.
    {
        DivMmc d;
        d.reset();
        d.set_enabled(true);
        d.check_automap(0x0000, true);
        check("NA-02",
              "enable=true releases reset, automap functions "
              "(VHDL zxnext.vhd:4112)",
              d.is_active() && d.is_rom_mapped(),
              fmt("act=%d rom=%d",
                  d.is_active(), d.is_rom_mapped()));
    }

    // NA-03: port_divmmc_io_en=0 asserts automap_reset even with NR
    // 0x0A[4]=1. There is no separate port_divmmc_io_en lever on the
    // emulator side — set_enabled() is the only gate.
    skip("NA-03",
         "No emulator path: port_divmmc_io_en and NR 0x0A[4] collapsed "
         "onto single set_enabled() lever (VHDL zxnext.vhd:4112)");
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

    // SS-02..SS-05: sd_swap decode logic from zxnext.vhd:3308-3314.
    // VHDL transforms cpu_do(1:0) according to sd_swap (NR 0x0A[5]).
    // The C++ SpiMaster stores the raw CS byte verbatim — there is no
    // sd_swap input and no transform of the low two bits. All four
    // rows are unreachable without wiring NR 0x0A[5] into SpiMaster.
    skip("SS-02",
         "No emulator path: sd_swap decode not modelled, write_cs stores "
         "raw byte (VHDL zxnext.vhd:3308)");
    skip("SS-03",
         "No emulator path: sd_swap decode not modelled "
         "(VHDL zxnext.vhd:3308)");
    skip("SS-04",
         "No emulator path: sd_swap=1 path not modelled "
         "(VHDL zxnext.vhd:3308)");
    skip("SS-05",
         "No emulator path: sd_swap=1 reverse mapping not modelled "
         "(VHDL zxnext.vhd:3308)");

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

    // SS-08 — will-not-implement (Flash device out of JNEXT scope).
    //
    // VHDL zxnext.vhd:3324 — write 0x7F with config_mode asserted
    // selects the external config Flash used only to reflash TBBlue
    // firmware. JNEXT does not emulate the Flash backend nor the
    // config_mode gating signal; see 2026-04-17f session handover
    // for the scope decision (Flash + RPI are dropped from the
    // roadmap, together with MX-01/02/05 below).

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

    // IN-03: RETN after NMI handler: automap deactivated.
    // VHDL: divmmc.vhd:126,139 (i_retn_seen branch). No RETN hook in
    // C++ DivMmc — same gap as DA-06.
    skip("IN-03",
         "No emulator path: DivMmc has no RETN hook "
         "(VHDL divmmc.vhd:126,139)");

    // IN-04: Automap at 0x0008 (RST 8) is ROM3-conditional.
    // VHDL: zxnext.vhd:2856, :3137. Same ROM3 gating gap as EP-02.
    skip("IN-04",
         "No emulator path: ROM3 conditional activation not modelled "
         "(VHDL zxnext.vhd:2856,3137)");

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
