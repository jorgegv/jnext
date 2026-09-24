// DivMMC Integration Test — full-Emulator SRAM-map verification.
//
// Companion suite of `## DivMMC+SPI`. It hosts the plan rows whose
// observable is the PHYSICAL SRAM layout the DivMMC window is backed by —
// a fact that belongs to `Emulator::init`'s wiring, not to the bare
// `DivMmc` register model, so `divmmc_test.cpp` (which links neither
// `jnext_core` nor a `Ram`) cannot see it.
//
// Those rows (SM-01..05) were recorded as unreachable in divmmc_test.cpp
// on the grounds that "JNEXT keeps RAM and ROM in separate buffers owned
// by DivMmc, with no physical +32 page offset". That stopped being true
// on 2026-07-09: the NextZXOS-boot fix made the Next's DivMMC ROM a view
// of SRAM page 0x08 and its RAM a view of SRAM pages 16-31
// (emulator.cpp `set_rom_backing(ram_.page_ptr(0x08))` /
// `set_ram_backing(ram_.page_ptr(16))`), which is exactly the VHDL ladder
// the rows describe. GH #201 re-checks them here.
//
// Plan: doc/testing/DIVMMC-SPI-TEST-PLAN-DESIGN.md §11 (SRAM mapping).
// Oracle: zxnext.vhd:3084-3099 (the SRAM arbiter's DivMMC branches),
//         divmmc.vhd:88-96 (page0/page1 decode and the forced bank 3).
//
// Run: ./build/test/divmmc_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "memory/ram.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
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
};

std::vector<Result> g_results;
std::string         g_group;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    g_results.push_back(Result{g_group, id, desc, cond});
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

// 8 KB per physical SRAM page — the unit the VHDL `sram_A21_A13` ladder
// addresses (A21..A13), so page N starts at byte N * 0x2000.
constexpr int kPageBytes = 8192;

} // namespace

static void build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
}

// Port 0xE3 — DivMMC control. bit 7 = conmem, bit 6 = mapram, bits 3:0 =
// RAM bank for the 0x2000-0x3FFF window (VHDL zxnext.vhd:4173-4190,
// divmmc.vhd:85-96).
static void divmmc_port_e3(Emulator& emu, uint8_t val) {
    emu.port().out(0x00E3, val);
}

// ══════════════════════════════════════════════════════════════════════
// §11. SRAM mapping — SM-01..05
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:3084-3099, the two DivMMC branches of the SRAM arbiter:
//
//   if  sram_pre_override(2)='1' and divmmc_rom_en='1' then
//         sram_A21_A13 <= "000001000";              -- page 0x08
//   elsif sram_pre_override(2)='1' and divmmc_ram_en='1' then
//         sram_A21_A13 <= "00001" & divmmc_bank;    -- pages 16..31
//
// A 9-bit `sram_A21_A13` is a physical 8 KB page index, so "000001000" is
// page 8 (byte 0x010000) and "00001bbbb" is page 16+bank (0x020000 +
// bank*0x2000). Each row below seeds a sentinel DIRECTLY into that
// physical page and requires the CPU-visible DivMMC window to return it —
// which is only true if the window really is a view of that page.
static void test_sram_map(Emulator& emu) {
    set_group("11. SRAM mapping");

    Ram& ram = emu.ram();

    // ── SM-01 — DivMMC ROM maps to SRAM 0x010000-0x011FFF (page 8) ────
    // divmmc.vhd:93 `rom_en <= page0 and (conmem or automap) and not mapram`
    // so conmem=1, mapram=0 puts the ROM in 0x0000-0x1FFF.
    {
        ram.page_ptr(0x08)[0x0000] = 0xA1;
        ram.page_ptr(0x08)[0x1FFF] = 0xA2;
        // A different page must NOT answer: seed page 9 with a decoy.
        ram.page_ptr(0x09)[0x0000] = 0x5C;
        divmmc_port_e3(emu, 0x80);            // conmem=1, mapram=0, bank 0
        const uint8_t lo = emu.mmu().read(0x0000);
        const uint8_t hi = emu.mmu().read(0x1FFF);
        check("SM-01",
              "DivMMC ROM window 0x0000-0x1FFF is physical SRAM page 0x08 "
              "(byte 0x010000), per sram_A21_A13 = \"000001000\" "
              "[zxnext.vhd:3084-3085, divmmc.vhd:93]",
              lo == 0xA1 && hi == 0xA2,
              fmt("lo=0x%02X hi=0x%02X (want 0xA1 / 0xA2)", lo, hi));
    }

    // ── SM-02 — DivMMC RAM bank 0 maps to SRAM 0x020000 (page 16) ─────
    // divmmc.vhd:94-95 — page1 (0x2000-0x3FFF) selects `i_divmmc_reg(3:0)`
    // as the bank, so port 0xE3 bits 3:0 pick the page within 16..31.
    {
        ram.page_ptr(16)[0x0000] = 0xB0;
        ram.page_ptr(16)[0x1FFF] = 0xB1;
        divmmc_port_e3(emu, 0x80 | 0x00);     // conmem=1, bank 0
        const uint8_t lo = emu.mmu().read(0x2000);
        const uint8_t hi = emu.mmu().read(0x3FFF);
        check("SM-02",
              "DivMMC RAM bank 0 is physical SRAM page 16 (byte 0x020000), "
              "per sram_A21_A13 = \"000010000\" "
              "[zxnext.vhd:3092-3093, divmmc.vhd:94-96]",
              lo == 0xB0 && hi == 0xB1,
              fmt("lo=0x%02X hi=0x%02X (want 0xB0 / 0xB1)", lo, hi));
    }

    // ── SM-03 — DivMMC RAM bank 3 maps to SRAM 0x026000 (page 19) ─────
    {
        ram.page_ptr(19)[0x0000] = 0xC3;
        ram.page_ptr(19)[0x1FFF] = 0xC4;
        divmmc_port_e3(emu, 0x80 | 0x03);     // conmem=1, bank 3
        const uint8_t lo = emu.mmu().read(0x2000);
        const uint8_t hi = emu.mmu().read(0x3FFF);
        check("SM-03",
              "DivMMC RAM bank 3 is physical SRAM page 19 (byte 0x026000), "
              "per sram_A21_A13 = \"000010011\" [zxnext.vhd:3092-3093]",
              lo == 0xC3 && hi == 0xC4,
              fmt("lo=0x%02X hi=0x%02X (want 0xC3 / 0xC4)", lo, hi));
    }

    // ── SM-04 — DivMMC RAM bank 15 maps to SRAM 0x03E000 (page 31) ────
    // The top of the 128 KB DivMMC RAM region: proves the bank index runs
    // the full 4 bits and that the region really is 16 pages long.
    {
        ram.page_ptr(31)[0x0000] = 0xDE;
        ram.page_ptr(31)[0x1FFF] = 0xDF;
        // Page 32 is the first page PAST the region — a decoy that must
        // not answer if the +16 base or the 4-bit width were wrong.
        ram.page_ptr(32)[0x0000] = 0x11;
        divmmc_port_e3(emu, 0x80 | 0x0F);     // conmem=1, bank 15
        const uint8_t lo = emu.mmu().read(0x2000);
        const uint8_t hi = emu.mmu().read(0x3FFF);
        check("SM-04",
              "DivMMC RAM bank 15 is physical SRAM page 31 (byte 0x03E000), "
              "per sram_A21_A13 = \"000011111\" [zxnext.vhd:3092-3093]",
              lo == 0xDE && hi == 0xDF,
              fmt("lo=0x%02X hi=0x%02X (want 0xDE / 0xDF)", lo, hi));
    }

    // ── SM-05 — DivMMC outranks the Layer 2 mapping ───────────────
    // zxnext.vhd:3081-3104: the two DivMMC branches are tested BEFORE
    // `elsif sram_layer2_map_en = '1'`, so with both claiming the same 8 KB
    // window the DivMMC view wins. Driven entirely through the real port
    // path here (port 0x123B + NR 0x12 + port 0xE3), which is the seam
    // divmmc_test.cpp's PRI-04 bypasses by poking Mmu directly.
    //
    // The Layer 2 byte is planted THROUGH the Layer 2 write-over path
    // rather than into a page this test computes for itself: the
    // bank-to-SRAM-page arithmetic is Layer 2's own contract (rows G2-*/
    // G5-*), and duplicating it here would make this row fail for a Layer 2
    // reason rather than a priority one.
    {
        emu.port().out(0x243B, 0x12);
        emu.port().out(0x253B, 16);           // NR 0x12 active L2 bank = 16
        emu.port().out(0x123B, 0x05);         // wr_en + rd_en, segment 00
        divmmc_port_e3(emu, 0x00);            // DivMMC overlay off
        emu.mmu().write(0x0100, 0x7A);        // plant the Layer 2 byte
        const uint8_t l2_only = emu.mmu().read(0x0100);

        ram.page_ptr(0x08)[0x0100] = 0xE5;    // DivMMC ROM byte, same offset
        divmmc_port_e3(emu, 0x80);            // conmem on — DivMMC claims it
        const uint8_t with_divmmc = emu.mmu().read(0x0100);

        divmmc_port_e3(emu, 0x00);            // drop DivMMC again
        const uint8_t back_to_l2 = emu.mmu().read(0x0100);

        check("SM-05",
              "DivMMC outranks the Layer 2 mapping in the SRAM arbiter: the "
              "same address reads the Layer 2 byte, then the DivMMC ROM byte "
              "while conmem is set, then the Layer 2 byte again "
              "[zxnext.vhd:3081-3104]",
              l2_only == 0x7A && with_divmmc == 0xE5 && back_to_l2 == 0x7A,
              fmt("l2=0x%02X with=0x%02X back=0x%02X (want 0x7A / 0xE5 / 0x7A)",
                  l2_only, with_divmmc, back_to_l2));
        emu.port().out(0x123B, 0x00);         // drop the L2 mapping again
    }

    (void)kPageBytes;
}

// ══════════════════════════════════════════════════════════════════════
// §12 companion — SS-08: the Flash chip-select gate, at the Emulator tier
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:3319-3320:
//
//   elsif cpu_do = X"7F" and ((nr_03_config_mode = '1')
//                             or (nr_02_reset_type(2) = '1')) then
//      port_e7_reg <= X"7F";
//
// and :3321-3322 drops it to all-ones otherwise. `spi_ss_flash_n <=
// port_e7_reg(7)` (:3328) and `spi_ss_sd1_n / sd0_n <= port_e7_reg(1) /
// (0)` (:3331-3332), so the pattern both asserts Flash and deasserts both
// SD lines — "selects Flash" in the plan row's words.
//
// Why this row belongs HERE rather than next to SS-09/SS-13: the gate has
// TWO VHDL sources ORed together, and `SpiMaster` sees neither. It sees one
// composite boolean via `set_flash_cs_enable`, which `divmmc_test`'s SS-13
// drives by hand. Nothing proved that `nr_03_config_mode` and
// `nr_02_reset_type(2)` actually reach it — and they are fanned out from
// FOUR separate sites in `Emulator` (the NR 0x02 handler, the NR 0x03
// handler, `init`, and `load_state`), any of which could be missing while
// SS-13 stayed green. That is exactly the "covered elsewhere" trap: SS-13
// covers the mux, not the wires into it.
//
// The row was carried as a skip with a rationale that has not been true
// since 2026-05-09: "jnext stub returns 0xFF (Flash device + config_mode
// signal not modelled)". `SpiMaster::set_flash_cs_enable` and the
// `nr_03_config_mode` model both exist. What is still absent is the Flash
// DEVICE — the data path behind the select — which is SPI-MX-01's problem,
// not this row's: SS-08 is a chip-select decode row and the chip-select
// register is fully modelled.

static void test_flash_cs_gate() {
    set_group("12. Flash CS gate");

    // Port 0xE7 is WRITE-ONLY in VHDL (there is no `port_e7_rd` signal; see
    // the V16-DIVMMC-01 note in emulator.cpp), so the register is read back
    // through `Emulator::spi()` rather than through an `in`. The STIMULUS is
    // still the real guest path: `port().out(0x00E7, ...)`.
    {
        Emulator emu;
        build_next_emulator(emu);

        // Leg 1 — the reset_type(2) source. A cold boot leaves reset_type at
        // "100" (zxnext.vhd:1306) and `Emulator::init` clears config_mode for
        // a firmware-less boot, so bit 2 alone must hold the gate open.
        const bool cfg_boot   = emu.nextreg().nr_03_config_mode();
        const uint8_t rt_boot = emu.nmi_source().reset_type();

        // Select an SD line first, so the Flash write has to REPLACE a live
        // selection rather than merely add a bit: `port_e7_reg` is assigned
        // whole in every branch (:3312-3322), and the resulting 0x7F leaves
        // spi_ss_sd1_n / sd0_n (bits 1/0, :3331-3332) and both RPI selects
        // (bits 3/2, :3329-3330) HIGH. That is the other half of "selects
        // Flash", and it is why the mux at :3278-3280 cannot reach the SD
        // arm while Flash is up (":3276 note: do not AND together miso
        // sources"). SPI-MX-01, the Flash DATA arm, still has no backend.
        emu.port().out(0x00E7, 0x02);
        const uint8_t cs_sd = emu.spi().read_cs();

        emu.port().out(0x00E7, 0x7F);
        const uint8_t cs_rt = emu.spi().read_cs();

        // Leg 2 — the nr_03_config_mode source, with reset_type(2) cleared.
        // A soft-reset strobe advances the FSM "100" -> "010" (:1735), so
        // after it ONLY config_mode can open the gate.
        emu.on_hotkey_f4_soft_reset();
        const uint8_t rt_soft = emu.nmi_source().reset_type();

        // 2a: both sources clear -> 0x7F is rewritten to all-deselected.
        emu.port().out(0x00E7, 0x7F);
        const uint8_t cs_closed = emu.spi().read_cs();

        // 2b: enter config mode (NR 0x03 low 3 bits "111", :5147-5149) and
        //     the same write now stands.
        emu.port().out(0x243B, 0x03);
        emu.port().out(0x253B, 0x07);
        emu.port().out(0x00E7, 0x7F);
        const uint8_t cs_cfg = emu.spi().read_cs();

        // 2c: leave config mode again ("010" keeps machine_type = ZX Next)
        //     and the gate closes behind it.
        emu.port().out(0x243B, 0x03);
        emu.port().out(0x253B, 0x02);
        emu.port().out(0x00E7, 0x7F);
        const uint8_t cs_exit = emu.spi().read_cs();

        const bool ok =
            !cfg_boot && (rt_boot & 0x04) != 0 &&      // leg-1 precondition
            (cs_sd & 0x03) != 0x03 &&                  // an SD line WAS low
            cs_rt     == 0x7F &&                       // ...and is high again
            (rt_soft & 0x04) == 0 &&                   // leg-2 precondition
            cs_closed == 0xFF &&
            cs_cfg    == 0x7F &&
            cs_exit   == 0xFF;

        check("SS-08",
              "port 0xE7 <- 0x7F stands as 0x7F only while "
              "nr_03_config_mode OR nr_02_reset_type(2) is set, and is "
              "rewritten to 0xFF otherwise — both VHDL sources reach the "
              "SpiMaster gate from the Emulator, and the pattern deasserts "
              "the SD and RPI selects "
              "(zxnext.vhd:3319-3322; :1306, :1735, :5147-5149)",
              ok,
              fmt("cfg_boot=%d rt_boot=%u cs_sd=0x%02X cs_rt=0x%02X "
                  "rt_soft=%u cs_closed=0x%02X cs_cfg=0x%02X cs_exit=0x%02X "
                  "(want 0,4,SD-bit-low,0x7F,2,0xFF,0x7F,0xFF)",
                  cfg_boot ? 1 : 0, rt_boot, cs_sd, cs_rt, rt_soft,
                  cs_closed, cs_cfg, cs_exit));
    }

}

int main() {
    std::printf("DivMMC Integration Test (GH #201)\n");
    std::printf("====================================\n\n");

    Emulator emu;
    build_next_emulator(emu);

    test_sram_map(emu);
    std::printf("  Group: 11. SRAM mapping — done\n");

    test_flash_cs_gate();
    std::printf("  Group: 12. Flash CS gate — done\n");

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
