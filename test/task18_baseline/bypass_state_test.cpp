// Task 18 — discriminative regression test for --bypass-tbblue-fw.
//
// Built as `task18_bypass_state_test`. Asserts post-init state matches
// the bypass contract from doc/issues/bypass-firmware-v2/PLAN-AUDIT.md
// §2-3:
//
//   (a) SRAM page 0 first 4 bytes = `F3 C3 EF 00` (enNextZX.rom's
//       `DI; JP $00EF` reset vector — proves the right ROM was loaded
//       into the right SRAM page).
//   (b) `mmu.boot_rom_enabled()` = false (boot ROM overlay suppressed).
//   (c) `nextreg.nr_03_config_mode()` = false (config_mode cleared by
//       the NR 0x03=0xB3 commit at end of init()).
//   (d) `mmu.machine_type()` = ZX_PLUS3 (committed by the same write).
//
// This is the test that would have caught a regression of any of the
// three bypass-mode gates in Emulator::init():
//   - swapping the wrong ROM file
//   - re-enabling the boot ROM overlay
//   - removing or mis-ordering the NR 0x03 write
//
// Returns 0 on PASS, 1 on FAIL. Each assertion prints its result so the
// failure surface is obvious.
//
// Requires `roms/nextzxos-1gb-fat32fix.img` on the canonical jnext
// fixture path (the standard NextZXOS test image).

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "port/nextreg.h"

#include <cstdio>
#include <cstdint>

namespace {

int fails = 0;

void check(const char* label, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++fails;
}

}  // namespace

int main() {
    Log::init();

    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image = "roms/nextzxos-1gb-fat32fix.img";
    cfg.bypass_tbblue_fw = true;

    Emulator emu;
    if (!emu.init(cfg)) {
        std::fprintf(stderr, "FAIL emu.init() returned false\n");
        return 1;
    }

    // (a) SRAM page 0 first 4 bytes = enNextZX.rom signature `F3 C3 EF 00`.
    const uint8_t* p0 = emu.ram().page_ptr(0);
    if (p0 == nullptr) {
        check("SRAM page 0 page_ptr non-null", false);
    } else {
        const bool sig_ok = (p0[0] == 0xF3 && p0[1] == 0xC3 &&
                             p0[2] == 0xEF && p0[3] == 0x00);
        std::printf("     SRAM page 0 first 4 bytes: %02X %02X %02X %02X (expected F3 C3 EF 00)\n",
                    p0[0], p0[1], p0[2], p0[3]);
        check("SRAM page 0 contains enNextZX.rom DI;JP $00EF signature", sig_ok);
    }

    // (b) boot ROM overlay must be off.
    check("mmu.boot_rom_enabled() == false", emu.mmu().boot_rom_enabled() == false);

    // (c) config_mode must have been cleared by the NR 0x03=0xB3 commit.
    check("nextreg.nr_03_config_mode() == false",
          emu.nextreg().nr_03_config_mode() == false);

    // (d) machine_type committed to +3.
    const auto mt = emu.mmu().machine_type();
    std::printf("     mmu.machine_type() = %d (expected ZX_PLUS3=%d)\n",
                static_cast<int>(mt), static_cast<int>(MachineType::ZX_PLUS3));
    check("mmu.machine_type() == ZX_PLUS3",
          mt == MachineType::ZX_PLUS3);

    // Bonus: rom_in_sram must be on (needed for slot 0/1 to point at SRAM).
    check("mmu.rom_in_sram() == true", emu.mmu().rom_in_sram() == true);

    std::printf("\n%s\n", fails == 0 ? "ALL PASS" : "FAILURES");
    return fails == 0 ? 0 : 1;
}
