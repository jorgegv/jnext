// Task 18 — Cold-reset baseline probe (SCRATCH; do NOT commit).
//
// Builds a single Emulator with the canonical "boot Next" CLI config
// (machine_type=Next, sd_card_image=roms/nextzxos-1gb-fat32fix.img,
// no --load), calls init() once (hard cold reset), and dumps the
// post-init state to stdout in a stable, grep-friendly format.
//
// Scope: NR registers, MMU slot map, Z80 registers, bootloader flags,
// SRAM page presence. The goal is to establish a single source of
// truth for what jnext looks like immediately after init() — before
// any firmware (nextboot.rom / tbblue.fw / NextZXOS) has run.
//
// See doc/issues/bypass-firmware-v2/JNEXT-COLD-RESET-STATE.md for the
// written-up findings.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "cpu/z80_cpu.h"
#include "memory/mmu.h"
#include "port/nextreg.h"
#include "memory/ram.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

void hr(const char* title) {
    std::printf("\n=== %s ===\n", title);
}

bool page_nonzero(Ram& ram, uint16_t page) {
    const uint8_t* p = ram.page_ptr(page);
    if (!p) return false;
    for (size_t i = 0; i < 0x2000; ++i) if (p[i] != 0) return true;
    return false;
}

uint8_t page_first_byte(Ram& ram, uint16_t page) {
    const uint8_t* p = ram.page_ptr(page);
    if (!p) return 0;
    return p[0];
}

} // namespace

int main(int argc, char** argv) {
    // Match the canonical "boot Next" config exactly. Default SD path
    // mirrors CLAUDE.md "canonical NextZXOS test image".
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image = (argc >= 2) ? argv[1] : "roms/nextzxos-1gb-fat32fix.img";
    cfg.rewind_buffer_frames = 0;  // no snapshots — keep init() lean

    Emulator emu;
    bool ok = emu.init(cfg);
    std::printf("init() returned: %s\n", ok ? "true" : "false");
    std::printf("sd_card_image  : %s\n", cfg.sd_card_image.c_str());
    std::printf("machine_type   : Next (ZXN_ISSUE2)\n");

    // ── NR registers ──────────────────────────────────────────────────
    hr("NextREG cached() dump (full 256 bytes)");
    for (int row = 0; row < 16; ++row) {
        std::printf("%02X:", row * 16);
        for (int col = 0; col < 16; ++col) {
            uint8_t reg = static_cast<uint8_t>(row * 16 + col);
            std::printf(" %02X", emu.nextreg().cached(reg));
        }
        std::printf("\n");
    }

    hr("NextReg state flags");
    std::printf("nr_03_config_mode    : %d\n", emu.nextreg().nr_03_config_mode() ? 1 : 0);
    std::printf("nr_03_machine_type   : 0x%02X\n", emu.nextreg().nr_03_machine_type());
    std::printf("nr_03_machine_timing : 0x%02X\n", emu.nextreg().nr_03_machine_timing());
    std::printf("nr_03_user_dt_lock   : %d\n", emu.nextreg().nr_03_user_dt_lock() ? 1 : 0);
    std::printf("nr_04_romram_bank    : 0x%02X\n", emu.nextreg().nr_04_romram_bank());
    std::printf("selected (NR ptr)    : 0x%02X\n", emu.nextreg().selected());

    // ── Specific NR registers of interest (per task scope) ────────────
    // Dumps BOTH cached() (raw byte in regs_[]) AND read() (live value
    // through registered read handlers — VHDL-faithful read mux).
    // They differ for any NR whose read handler composes from subsystem
    // state rather than the cache (e.g. NR 0x13 reads shadow_bank from
    // Layer2, NR 0x12 reads active_bank, etc.).
    hr("NR registers in task-18 scope (cached | read())");
    auto dump_nr = [&](uint8_t reg, const char* note) {
        const uint8_t cached = emu.nextreg().cached(reg);
        const uint8_t live   = emu.nextreg().read(reg);
        std::printf("NR 0x%02X  cached=0x%02X  read()=0x%02X%s  %s\n",
                    reg, cached, live,
                    cached == live ? "" : "  *DIFFER*",
                    note);
    };
    const uint8_t scope[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
        0x0B, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
        0x22, 0x23,
        0x40, 0x41, 0x42, 0x43, 0x44,
        0x4A, 0x4B, 0x4C,
        0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
        0x69, 0x6B,
        0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
        0x8C, 0x8E, 0x8F,
        0xA0,
        0xC0, 0xC4, 0xC5, 0xC6, 0xC8,
        0xD8, 0xD9, 0xDA,
    };
    for (uint8_t r : scope) dump_nr(r, "");

    // ── MMU slot map ──────────────────────────────────────────────────
    hr("MMU slot map (8 KB slots)");
    for (int slot = 0; slot < 8; ++slot) {
        std::printf("slot %d  nr_mmu=0x%02X  eff_page=0x%02X  read_only=%d  rom_area=%d\n",
                    slot,
                    emu.mmu().get_page(slot),
                    emu.mmu().get_effective_page(slot),
                    emu.mmu().is_slot_rom(slot) ? 1 : 0,
                    emu.mmu().slot_in_rom_area(slot) ? 1 : 0);
    }
    std::printf("boot_rom_enabled : %d\n", emu.mmu().boot_rom_enabled() ? 1 : 0);
    std::printf("rom_in_sram      : %d\n", emu.mmu().rom_in_sram() ? 1 : 0);

    // ── Legacy paging ports ───────────────────────────────────────────
    hr("Legacy paging ports / mode");
    std::printf("port_7FFD : 0x%02X\n", emu.mmu().port_7ffd());
    std::printf("port_1FFD : 0x%02X\n", emu.mmu().port_1ffd());
    std::printf("paging_locked : %d\n", emu.mmu().paging_locked() ? 1 : 0);

    // ── Z80 registers ─────────────────────────────────────────────────
    hr("Z80 register defaults");
    auto regs = emu.cpu().get_registers();
    std::printf("PC=0x%04X SP=0x%04X\n", regs.PC, regs.SP);
    std::printf("AF=0x%04X BC=0x%04X DE=0x%04X HL=0x%04X\n", regs.AF, regs.BC, regs.DE, regs.HL);
    std::printf("AF'=0x%04X BC'=0x%04X DE'=0x%04X HL'=0x%04X\n", regs.AF2, regs.BC2, regs.DE2, regs.HL2);
    std::printf("IX=0x%04X IY=0x%04X MEMPTR=0x%04X\n", regs.IX, regs.IY, regs.MEMPTR);
    std::printf("I=0x%02X R=0x%02X  IFF1=%d IFF2=%d IM=%d  halted=%d\n",
                regs.I, regs.R, regs.IFF1, regs.IFF2, regs.IM, regs.halted ? 1 : 0);

    // ── SRAM page summary ─────────────────────────────────────────────
    hr("SRAM pages with non-zero contents (first 128 pages)");
    for (uint16_t page = 0; page < 128; ++page) {
        if (page_nonzero(emu.ram(), page)) {
            std::printf("page 0x%02X non-zero (first byte=0x%02X)\n", page, page_first_byte(emu.ram(), page));
        }
    }

    // ── DivMMC / Multiface ────────────────────────────────────────────
    hr("Peripheral ROM presence");
    std::printf("DivMMC is_enabled    : %d\n", emu.divmmc().is_enabled() ? 1 : 0);
    std::printf("DivMMC ROM[0]        : 0x%02X\n", emu.divmmc().rom_data()[0]);
    std::printf("DivMMC ROM[1]        : 0x%02X\n", emu.divmmc().rom_data()[1]);
    std::printf("Multiface ROM[0]     : 0x%02X\n", emu.multiface().rom_data()[0]);

    std::fflush(stdout);
    return 0;
}
