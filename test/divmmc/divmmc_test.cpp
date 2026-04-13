// DivMMC + SPI Compliance Test Runner
//
// Tests the DivMMC and SPI subsystems against VHDL-derived expected behaviour.
// All expected values come from the DIVMMC-SPI-TEST-PLAN-DESIGN.md spec.
//
// Run: ./build/test/divmmc_test

#include "peripheral/divmmc.h"
#include "peripheral/spi.h"
#include "peripheral/sd_card.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

// -- Test infrastructure (same pattern as copper_test) --------------------

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

// -- Mock SPI device for testing ------------------------------------------

class MockSpiDevice : public SpiDevice {
public:
    uint8_t next_response = 0xFF;
    uint8_t last_tx = 0xFF;
    int exchange_count = 0;
    bool was_deselected = false;

    uint8_t exchange(uint8_t tx) override {
        last_tx = tx;
        exchange_count++;
        return next_response;
    }

    void receive(uint8_t tx) override {
        last_tx = tx;
        exchange_count++;
    }

    uint8_t send() override {
        exchange_count++;
        return next_response;
    }

    void deselect() override {
        was_deselected = true;
    }

    void reset_mock() {
        next_response = 0xFF;
        last_tx = 0xFF;
        exchange_count = 0;
        was_deselected = false;
    }
};

// -- Group 1: Port 0xE3 -- DivMMC Control Register ------------------------

static void test_port_e3() {
    set_group("Port 0xE3");

    // E3-01: Reset clears port 0xE3 to 0x00
    {
        DivMmc div;
        div.reset();
        check("E3-01", "Reset clears port 0xE3 to 0x00",
              div.read_control() == 0x00,
              DETAIL("got=%02x", div.read_control()));
    }

    // E3-02: Write 0x80: conmem=1, mapram=0, bank=0
    {
        DivMmc div;
        div.reset();
        div.write_control(0x80);
        check("E3-02", "Write 0x80: conmem=1, mapram=0, bank=0",
              div.conmem() == true && div.mapram() == false && div.bank() == 0,
              DETAIL("conmem=%d mapram=%d bank=%d", div.conmem(), div.mapram(), div.bank()));
    }

    // E3-03: Write 0x40: mapram set
    {
        DivMmc div;
        div.reset();
        div.write_control(0x40);
        check("E3-03", "Write 0x40: mapram=1",
              div.mapram() == true,
              DETAIL("mapram=%d", div.mapram()));
    }

    // E3-04: Write 0x00 after mapram set: mapram stays 1
    // NOTE: VHDL OR-latches mapram. Our implementation may not do this.
    {
        DivMmc div;
        div.reset();
        div.write_control(0x40);  // set mapram
        div.write_control(0x00);  // try to clear it
        check("E3-04", "mapram OR-latch: stays 1 after write 0x00",
              div.mapram() == true,
              DETAIL("mapram=%d (expected 1)", div.mapram()));
    }

    // E3-06: Write bank 0x0F: bits 3:0 select bank 15
    {
        DivMmc div;
        div.reset();
        div.write_control(0x0F);
        check("E3-06", "Write bank 0x0F: bank=15",
              div.bank() == 15,
              DETAIL("bank=%d", div.bank()));
    }

    // E3-07: Read port 0xE3 returns correct bits
    {
        DivMmc div;
        div.reset();
        div.write_control(0xCF);  // conmem=1, mapram=1, bits5:4=ignored, bank=15
        uint8_t val = div.read_control();
        bool conmem_ok = (val & 0x80) != 0;
        bool mapram_ok = (val & 0x40) != 0;
        bool bank_ok   = (val & 0x0F) == 0x0F;
        check("E3-07", "Read port 0xE3 returns {conmem, mapram, 00, bank}",
              conmem_ok && mapram_ok && bank_ok,
              DETAIL("val=%02x conmem=%d mapram=%d bank=%d",
                     val, conmem_ok, mapram_ok, bank_ok));
    }

    // E3-08: Bits 5:4 of write are ignored (read back as 0)
    {
        DivMmc div;
        div.reset();
        div.write_control(0x30);  // only set bits 5:4
        uint8_t val = div.read_control();
        bool bits54_zero = (val & 0x30) == 0x00;
        check("E3-08", "Bits 5:4 of write are ignored",
              bits54_zero,
              DETAIL("val=%02x bits54=%02x (expected 0)", val, val & 0x30));
    }
}

// -- Group 2: DivMMC Memory Paging -- conmem Mode -------------------------

static void test_conmem_paging() {
    set_group("Conmem Paging");

    // CM-01: conmem=1, mapram=0: 0x0000-0x1FFF = DivMMC ROM
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x80);  // conmem=1
        check("CM-01", "conmem=1, mapram=0: ROM mapped at slot 0",
              div.is_rom_mapped(),
              DETAIL("is_rom_mapped=%d", div.is_rom_mapped()));
    }

    // CM-02: conmem=1, mapram=0: 0x2000-0x3FFF = DivMMC RAM bank N
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x85);  // conmem=1, bank=5
        check("CM-02", "conmem=1: RAM mapped at slot 1",
              div.is_ram_mapped(0x2000),
              DETAIL("is_ram_mapped(0x2000)=%d", div.is_ram_mapped(0x2000)));
    }

    // CM-03: conmem=1, mapram=1: 0x0000-0x1FFF = DivMMC RAM bank 3
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0xC0);  // conmem=1, mapram=1
        bool ram_at_slot0 = div.is_ram_mapped(0x0000);
        bool not_rom = !div.is_rom_mapped();
        check("CM-03", "conmem=1, mapram=1: RAM bank 3 at slot 0",
              ram_at_slot0 && not_rom,
              DETAIL("ram_mapped=%d rom_mapped=%d", ram_at_slot0, !not_rom));
    }

    // CM-04: conmem=1, mapram=1: 0x2000-0x3FFF = DivMMC RAM bank N
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0xC7);  // conmem=1, mapram=1, bank=7
        check("CM-04", "conmem=1, mapram=1: RAM at slot 1",
              div.is_ram_mapped(0x2000),
              DETAIL("is_ram_mapped(0x2000)=%d", div.is_ram_mapped(0x2000)));
    }

    // CM-05: conmem=1: 0x0000-0x1FFF is read-only
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x80);  // conmem=1
        check("CM-05", "conmem=1: slot 0 is read-only",
              div.is_read_only(0x0000),
              DETAIL("is_read_only(0x0000)=%d", div.is_read_only(0x0000)));
    }

    // CM-06: conmem=1, mapram=1, bank=3: slot 1 is read-only
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0xC3);  // conmem=1, mapram=1, bank=3
        check("CM-06", "mapram=1, bank=3: slot 1 read-only",
              div.is_read_only(0x2000),
              DETAIL("is_read_only(0x2000)=%d", div.is_read_only(0x2000)));
    }

    // CM-07: conmem=1, mapram=1, bank!=3: slot 1 is writable
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0xC5);  // conmem=1, mapram=1, bank=5
        check("CM-07", "mapram=1, bank!=3: slot 1 writable",
              !div.is_read_only(0x2000),
              DETAIL("is_read_only(0x2000)=%d", div.is_read_only(0x2000)));
    }

    // CM-08: conmem=0, automap=0: no DivMMC mapping
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x00);  // conmem=0
        check("CM-08", "conmem=0, automap=0: not active",
              !div.is_active(),
              DETAIL("is_active=%d", div.is_active()));
    }

    // CM-09: DivMMC paging requires enabled=true
    {
        DivMmc div;
        div.reset();
        div.set_enabled(false);
        div.write_control(0x80);  // conmem=1 but not enabled
        check("CM-09", "DivMMC paging requires enabled=true",
              !div.is_active(),
              DETAIL("is_active=%d (enabled=%d)", div.is_active(), div.is_enabled()));
    }
}

// -- Group 3: DivMMC Memory Paging -- automap Mode ------------------------

static void test_automap_paging() {
    set_group("Automap Paging");

    // AM-01: automap=1, mapram=0: ROM mapped at slot 0
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);  // trigger automap at RST 0
        check("AM-01", "automap=1, mapram=0: ROM at slot 0",
              div.is_rom_mapped(),
              DETAIL("rom_mapped=%d automap=%d", div.is_rom_mapped(), div.automap_active()));
    }

    // AM-02: automap=1, mapram=0: RAM at slot 1
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x05);  // bank=5, no conmem
        div.check_automap(0x0000, true);
        check("AM-02", "automap=1: RAM at slot 1",
              div.is_ram_mapped(0x2000),
              DETAIL("ram_mapped=%d", div.is_ram_mapped(0x2000)));
    }

    // AM-03: automap=1, mapram=1: RAM bank 3 at slot 0
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x40);  // mapram=1 (no conmem)
        div.check_automap(0x0000, true);
        bool ram_at_0 = div.is_ram_mapped(0x0000);
        bool not_rom  = !div.is_rom_mapped();
        check("AM-03", "automap=1, mapram=1: RAM bank 3 at slot 0",
              ram_at_0 && not_rom,
              DETAIL("ram=%d rom=%d", ram_at_0, !not_rom));
    }

    // AM-04: automap deactivated: normal ROM restored
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);   // activate
        div.check_automap(0x1FF8, true);   // deactivate
        check("AM-04", "automap deactivated: not active",
              !div.is_active(),
              DETAIL("is_active=%d automap=%d", div.is_active(), div.automap_active()));
    }
}

// -- Group 4: RST Entry Points (NextREG 0xB8/0xB9/0xBA) ------------------

static void test_rst_entry_points() {
    set_group("RST Entry Points");

    // EP-01: M1 fetch at 0x0000: automap activates (EP0 enabled by default)
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);
        check("EP-01", "M1 at 0x0000: automap ON",
              div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // EP-02: M1 at 0x0008: automap activates (EP1 enabled by default)
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0008, true);
        check("EP-02", "M1 at 0x0008: automap ON",
              div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // EP-03: M1 at 0x0038: automap activates (EP7 enabled by default)
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0038, true);
        check("EP-03", "M1 at 0x0038: automap ON",
              div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // EP-04 through EP-08: disabled entry points
    {
        static const struct { const char* id; uint16_t addr; int ep; } disabled[] = {
            {"EP-04", 0x0010, 2}, {"EP-05", 0x0018, 3},
            {"EP-06", 0x0020, 4}, {"EP-07", 0x0028, 5},
            {"EP-08", 0x0030, 6},
        };
        for (auto& t : disabled) {
            DivMmc div;
            div.reset();
            div.set_enabled(true);
            div.check_automap(t.addr, true);
            char desc[64];
            snprintf(desc, sizeof(desc), "M1 at 0x%04X: no automap (EP%d disabled)", t.addr, t.ep);
            check(t.id, desc,
                  !div.automap_active(),
                  DETAIL("automap=%d ep=%d", div.automap_active(), t.ep));
        }
    }

    // EP-11: Set NR 0xB8=0xFF: all 8 RST addresses trigger
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.set_entry_points_0(0xFF);  // enable all
        bool all_ok = true;
        uint16_t addrs[] = {0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038};
        for (auto addr : addrs) {
            DivMmc d2;
            d2.reset();
            d2.set_enabled(true);
            d2.set_entry_points_0(0xFF);
            d2.check_automap(addr, true);
            if (!d2.automap_active()) all_ok = false;
        }
        check("EP-11", "NR 0xB8=0xFF: all 8 RST addresses trigger",
              all_ok, DETAIL("all_ok=%d", all_ok));
    }

    // EP-12: Automap only triggers on M1 (is_m1=true)
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, false);  // NOT an M1 fetch
        check("EP-12", "Data read at 0x0000 does NOT trigger automap",
              !div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }
}

// -- Group 5: Non-RST Entry Points (NextREG 0xBB) -------------------------

static void test_nonrst_entry_points() {
    set_group("Non-RST Entry Points");

    // NR-01: M1 at 0x04C6 with BB[2]=1 (default): automap on
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x04C6, true);
        check("NR-01", "M1 at 0x04C6: automap ON (BB[2]=1)",
              div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }

    // NR-02: M1 at 0x0562 with BB[3]=1 (default): automap on
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0562, true);
        check("NR-02", "M1 at 0x0562: automap ON (BB[3]=1)",
              div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // NR-03: M1 at 0x04D7 with BB[4]=0 (default): no trigger
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x04D7, true);
        check("NR-03", "M1 at 0x04D7: no trigger (BB[4]=0 default)",
              !div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }

    // NR-04: M1 at 0x056A with BB[5]=0 (default): no trigger
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x056A, true);
        check("NR-04", "M1 at 0x056A: no trigger (BB[5]=0 default)",
              !div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // NR-05: Set BB[4]=1, M1 at 0x04D7: triggers
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.set_entry_points_1(div.entry_points_1() | 0x10);  // set bit 4
        div.check_automap(0x04D7, true);
        check("NR-05", "BB[4]=1, M1 at 0x04D7: automap ON",
              div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }

    // NR-06: M1 at 0x3D00 with BB[7]=1 (default 0xCD has bit7=1)
    // The VHDL checks any 0x3Dxx address. Our code may not implement this.
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x3D00, true);
        check("NR-06", "M1 at 0x3D00: automap (BB[7]=1)",
              div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }

    // NR-07: M1 at 0x3DFF with BB[7]=1
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x3DFF, true);
        check("NR-07", "M1 at 0x3DFF: automap (BB[7]=1)",
              div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // NR-08: Set BB[7]=0, M1 at 0x3D00: no trigger
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.set_entry_points_1(div.entry_points_1() & ~0x80);  // clear bit 7
        div.check_automap(0x3D00, true);
        check("NR-08", "BB[7]=0, M1 at 0x3D00: no trigger",
              !div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }
}

// -- Group 6: Automap Deactivation ----------------------------------------

static void test_deactivation() {
    set_group("Deactivation");

    // DA-01: M1 at 0x1FF8 deactivates automap
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);   // activate
        div.check_automap(0x1FF8, true);   // deactivate
        check("DA-01", "M1 at 0x1FF8: automap OFF",
              !div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // DA-02: M1 at 0x1FFF deactivates
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);
        div.check_automap(0x1FFF, true);
        check("DA-02", "M1 at 0x1FFF: automap OFF",
              !div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // DA-03: M1 at 0x1FF7: no deactivation
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);
        div.check_automap(0x1FF7, true);
        check("DA-03", "M1 at 0x1FF7: no deactivation",
              div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // DA-04: M1 at 0x2000: no deactivation
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);
        div.check_automap(0x2000, true);
        check("DA-04", "M1 at 0x2000: no deactivation",
              div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }

    // DA-05: Set BB[6]=0: deactivation range disabled
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.set_entry_points_1(div.entry_points_1() & ~0x40);  // clear bit 6
        div.check_automap(0x0000, true);   // activate
        div.check_automap(0x1FF8, true);   // try deactivate
        check("DA-05", "BB[6]=0: deactivation disabled",
              div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }

    // DA-07: Reset clears automap state
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);
        div.reset();
        check("DA-07", "Reset clears automap state",
              !div.automap_active(),
              DETAIL("automap=%d", div.automap_active()));
    }
}

// -- Group 7: DivMMC Memory Read/Write ------------------------------------

static void test_memory_rw() {
    set_group("Memory R/W");

    // MEM-01: Write and read slot 1 RAM
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x82);  // conmem=1, bank=2
        div.write(0x2000, 0xAB);
        check("MEM-01", "Write/read slot 1 RAM bank 2",
              div.read(0x2000) == 0xAB,
              DETAIL("got=%02x", div.read(0x2000)));
    }

    // MEM-02: Slot 0 writes are discarded (read-only)
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x80);  // conmem=1, mapram=0
        uint8_t before = div.read(0x0000);
        div.write(0x0000, 0x42);
        check("MEM-02", "Slot 0 writes discarded (ROM read-only)",
              div.read(0x0000) == before,
              DETAIL("before=%02x after=%02x", before, div.read(0x0000)));
    }

    // MEM-03: mapram=1, bank=3: slot 1 is read-only
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0xC3);  // conmem=1, mapram=1, bank=3
        uint8_t before = div.read(0x2000);
        div.write(0x2000, 0x55);
        check("MEM-03", "mapram=1, bank=3: slot 1 read-only",
              div.read(0x2000) == before,
              DETAIL("before=%02x after=%02x", before, div.read(0x2000)));
    }

    // MEM-04: mapram=1, bank!=3: slot 1 writable
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0xC5);  // conmem=1, mapram=1, bank=5
        div.write(0x2000, 0x77);
        check("MEM-04", "mapram=1, bank!=3: slot 1 writable",
              div.read(0x2000) == 0x77,
              DETAIL("got=%02x", div.read(0x2000)));
    }

    // MEM-05: mapram=1: slot 0 reads from RAM page 3
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        // Write to RAM page 3 via slot 1 first
        div.write_control(0x83);  // conmem=1, bank=3
        div.write(0x2000, 0xBE);
        // Now switch to mapram mode
        div.write_control(0xC0);  // conmem=1, mapram=1
        // Slot 0 should read from RAM page 3
        check("MEM-05", "mapram=1: slot 0 reads RAM page 3",
              div.read(0x0000) == 0xBE,
              DETAIL("got=%02x expected=0xBE", div.read(0x0000)));
    }

    // MEM-06: Bank switching changes slot 1 data
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.write_control(0x80);  // conmem=1, bank=0
        div.write(0x2000, 0x11);
        div.write_control(0x81);  // bank=1
        div.write(0x2000, 0x22);
        div.write_control(0x80);  // back to bank=0
        check("MEM-06", "Bank switching: data preserved per bank",
              div.read(0x2000) == 0x11,
              DETAIL("got=%02x expected=0x11", div.read(0x2000)));
    }

    // MEM-07: Address outside 0x0000-0x3FFF returns 0xFF
    {
        DivMmc div;
        div.reset();
        check("MEM-07", "Read outside range returns 0xFF",
              div.read(0x4000) == 0xFF,
              DETAIL("got=%02x", div.read(0x4000)));
    }
}

// -- Group 8: NextREG defaults --------------------------------------------

static void test_nextreg_defaults() {
    set_group("NextREG Defaults");

    DivMmc div;
    div.reset();

    // NR-B8 default
    check("NRD-01", "NR 0xB8 default = 0x83",
          div.entry_points_0() == 0x83,
          DETAIL("got=%02x", div.entry_points_0()));

    // NR-B9 default
    check("NRD-02", "NR 0xB9 default = 0x01",
          div.entry_valid_0() == 0x01,
          DETAIL("got=%02x", div.entry_valid_0()));

    // NR-BA default
    check("NRD-03", "NR 0xBA default = 0x00",
          div.entry_timing_0() == 0x00,
          DETAIL("got=%02x", div.entry_timing_0()));

    // NR-BB default
    check("NRD-04", "NR 0xBB default = 0xCD",
          div.entry_points_1() == 0xCD,
          DETAIL("got=%02x", div.entry_points_1()));
}

// -- Group 9: NMI Entry Point (0x0066) ------------------------------------

static void test_nmi_entry() {
    set_group("NMI Entry");

    // NMI via entry_points_1_ bit 1 (NMI instant) - default 0xCD has bit1=0
    // So NMI at 0x0066 should NOT trigger with default settings
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0066, true);
        check("NM-01", "M1 at 0x0066 default (BB[1]=0): no automap",
              !div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }

    // With BB[1] set (NMI instant)
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.set_entry_points_1(div.entry_points_1() | 0x02);  // set bit 1
        div.check_automap(0x0066, true);
        check("NM-02", "BB[1]=1, M1 at 0x0066: automap ON",
              div.automap_active(),
              DETAIL("automap=%d ep1=%02x", div.automap_active(), div.entry_points_1()));
    }
}

// -- Group 10: Port 0xE7 -- SPI Chip Select -------------------------------

static void test_spi_cs() {
    set_group("SPI CS (Port 0xE7)");

    // SS-01: Reset: all CS deasserted (0xFF)
    {
        SpiMaster spi;
        spi.reset();
        check("SS-01", "Reset: CS = 0xFF (all deselected)",
              spi.read_cs() == 0xFF,
              DETAIL("cs=%02x", spi.read_cs()));
    }

    // SS-02: Write CS value
    {
        SpiMaster spi;
        spi.reset();
        spi.write_cs(0xFE);  // select device 0
        check("SS-02", "Write CS 0xFE: device 0 selected",
              spi.read_cs() == 0xFE,
              DETAIL("cs=%02x", spi.read_cs()));
    }

    // SS-03: CS read-back matches write
    {
        SpiMaster spi;
        spi.reset();
        spi.write_cs(0xFD);
        check("SS-03", "CS read-back matches write",
              spi.read_cs() == 0xFD,
              DETAIL("cs=%02x", spi.read_cs()));
    }

    // SS-04: Deselect callback fires on CS transition
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        spi.write_cs(0xFE);  // select device 0
        spi.write_cs(0xFF);  // deselect
        check("SS-04", "Deselect callback fires on CS high transition",
              mock.was_deselected,
              DETAIL("was_deselected=%d", mock.was_deselected));
    }

    // SS-05: No deselect callback when device stays selected
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        spi.write_cs(0xFE);  // select device 0
        spi.write_cs(0xFE);  // same value
        check("SS-05", "No deselect when CS unchanged",
              !mock.was_deselected,
              DETAIL("was_deselected=%d", mock.was_deselected));
    }

    // SS-06: Multiple devices: only active one responds
    {
        SpiMaster spi;
        MockSpiDevice mock0, mock1;
        spi.reset();
        spi.attach_device(0, &mock0);
        spi.attach_device(1, &mock1);
        mock0.next_response = 0xAA;
        mock1.next_response = 0xBB;
        spi.write_cs(0xFE);  // select device 0 (bit 0 = 0)
        uint8_t val = spi.read_data();
        check("SS-06", "Only active device responds to read",
              val == 0xAA,
              DETAIL("got=%02x expected=0xAA", val));
    }
}

// -- Group 11: Port 0xEB -- SPI Data Exchange -----------------------------

static void test_spi_data() {
    set_group("SPI Data (Port 0xEB)");

    // SX-01: Write to 0xEB sends byte to device
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        spi.write_cs(0xFE);
        spi.write_data(0xA5);
        check("SX-01", "Write 0xEB: byte sent to device",
              mock.last_tx == 0xA5,
              DETAIL("last_tx=%02x", mock.last_tx));
    }

    // SX-02: Read from 0xEB returns device response
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        spi.write_cs(0xFE);
        mock.next_response = 0x5A;
        uint8_t val = spi.read_data();
        check("SX-02", "Read 0xEB: returns device response",
              val == 0x5A,
              DETAIL("got=%02x expected=0x5A", val));
    }

    // SX-04: First read after reset returns 0xFF
    {
        SpiMaster spi;
        spi.reset();
        // No device attached: read should return 0xFF
        uint8_t val = spi.read_data();
        check("SX-04", "First read after reset returns 0xFF",
              val == 0xFF,
              DETAIL("got=%02x", val));
    }

    // SX-05: Write then read sequence
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        spi.write_cs(0xFE);
        mock.next_response = 0x42;
        spi.write_data(0xAA);  // write command
        mock.next_response = 0x42;
        uint8_t val = spi.read_data();  // read response
        check("SX-05", "Write then read: gets device response",
              val == 0x42,
              DETAIL("got=%02x expected=0x42", val));
    }

    // SX-06: No device selected: read returns 0xFF
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        mock.next_response = 0xAB;
        spi.write_cs(0xFF);  // all deselected
        uint8_t val = spi.read_data();
        check("SX-06", "No device selected: read = 0xFF",
              val == 0xFF,
              DETAIL("got=%02x", val));
    }

    // SX-07: Multiple exchanges on same device
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        spi.write_cs(0xFE);
        mock.next_response = 0x11;
        spi.read_data();
        mock.next_response = 0x22;
        spi.read_data();
        mock.next_response = 0x33;
        uint8_t val = spi.read_data();
        check("SX-07", "Multiple exchanges: last response returned",
              val == 0x33,
              DETAIL("got=%02x expected=0x33", val));
    }
}

// -- Group 12: SD Card Device (basic protocol) ----------------------------

static void test_sd_card_basic() {
    set_group("SD Card Basic");

    // SD-01: SD card exchange returns 0xFF initially (no image mounted)
    {
        SdCardDevice sd;
        sd.reset();
        uint8_t val = sd.exchange(0xFF);
        check("SD-01", "SD card: initial exchange returns 0xFF",
              val == 0xFF,
              DETAIL("got=%02x", val));
    }

    // SD-02: SD card deselect does not crash
    {
        SdCardDevice sd;
        sd.reset();
        sd.deselect();
        check("SD-02", "SD card: deselect after reset",
              true, "");  // Just tests no crash
    }

    // SD-03: SD card not mounted initially
    {
        SdCardDevice sd;
        check("SD-03", "SD card: not mounted initially",
              !sd.mounted(),
              DETAIL("mounted=%d", sd.mounted()));
    }
}

// -- Group 13: Integration scenarios --------------------------------------

static void test_integration() {
    set_group("Integration");

    // IN-01: Boot sequence: automap at 0x0000, DivMMC mapped
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);
        check("IN-01", "Boot: automap at 0x0000, DivMMC active",
              div.is_active() && div.is_rom_mapped(),
              DETAIL("active=%d rom=%d", div.is_active(), div.is_rom_mapped()));
    }

    // IN-02: SD card init: select, exchange, deselect
    {
        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        mock.next_response = 0x01;  // R1 idle response
        spi.write_cs(0xFE);         // select
        spi.write_data(0x40);       // CMD0
        uint8_t resp = spi.read_data();
        spi.write_cs(0xFF);         // deselect
        check("IN-02", "SD init: select, exchange, deselect",
              resp == 0x01 && mock.was_deselected,
              DETAIL("resp=%02x deselected=%d", resp, mock.was_deselected));
    }

    // IN-06: conmem override during automap
    {
        DivMmc div;
        div.reset();
        div.set_enabled(true);
        div.check_automap(0x0000, true);  // automap active
        div.write_control(0x85);          // conmem=1, bank=5 (overrides automap)
        check("IN-06", "conmem override: active with conmem+automap",
              div.is_active() && div.conmem(),
              DETAIL("active=%d conmem=%d", div.is_active(), div.conmem()));
    }

    // IN-07: DivMMC disabled: no automap, SPI still works
    {
        DivMmc div;
        div.reset();
        div.set_enabled(false);
        div.check_automap(0x0000, true);
        bool no_automap = !div.automap_active();

        SpiMaster spi;
        MockSpiDevice mock;
        spi.reset();
        spi.attach_device(0, &mock);
        mock.next_response = 0x42;
        spi.write_cs(0xFE);
        uint8_t val = spi.read_data();
        bool spi_works = (val == 0x42);

        check("IN-07", "DivMMC disabled: no automap, SPI still works",
              no_automap && spi_works,
              DETAIL("automap=%d spi_resp=%02x", !no_automap, val));
    }
}

// -- Main -----------------------------------------------------------------

int main() {
    printf("DivMMC + SPI Compliance Tests\n");
    printf("==============================\n\n");

    test_port_e3();
    printf("  Group: Port 0xE3 -- done\n");

    test_conmem_paging();
    printf("  Group: Conmem Paging -- done\n");

    test_automap_paging();
    printf("  Group: Automap Paging -- done\n");

    test_rst_entry_points();
    printf("  Group: RST Entry Points -- done\n");

    test_nonrst_entry_points();
    printf("  Group: Non-RST Entry Points -- done\n");

    test_deactivation();
    printf("  Group: Deactivation -- done\n");

    test_memory_rw();
    printf("  Group: Memory R/W -- done\n");

    test_nextreg_defaults();
    printf("  Group: NextREG Defaults -- done\n");

    test_nmi_entry();
    printf("  Group: NMI Entry -- done\n");

    test_spi_cs();
    printf("  Group: SPI CS -- done\n");

    test_spi_data();
    printf("  Group: SPI Data -- done\n");

    test_sd_card_basic();
    printf("  Group: SD Card Basic -- done\n");

    test_integration();
    printf("  Group: Integration -- done\n");

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
