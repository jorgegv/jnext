// Task 18 — Post-boot probe for the "screen stays uniform gray" issue.
//
// Constructs an Emulator with --bypass-tbblue-fw config, runs 600
// frames (~12 s in headless-max-speed) to let NextZXOS finish SD
// init + file loads + any startup paint, then dumps the screen-related
// state so we can decide where the OS painted (if anywhere).
//
// Dumps:
//   - port 0x7FFD bit 3 (shadow-screen select)
//   - NR 0x69 (Layer 2 / display control)
//   - ula.shadow_screen_en flag
//   - first 64 bytes of legacy ULA pixel area (SRAM page 5 / 8K page $0A
//     offset $0000 = address $4000)
//   - first 64 bytes of legacy ULA attribute area (SRAM page 5 offset
//     $1800 = address $5800)
//   - first 64 bytes of shadow ULA pixel area (SRAM page 7 / 8K page
//     $0E offset $0000) and attributes (offset $1800)
//
// This is a one-shot diagnostic; it does NOT register as a CTest case.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "port/nextreg.h"
#include "video/ula.h"

#include <cstdio>
#include <cstdint>

namespace {

void hex_line(const char* label, const uint8_t* p, int n) {
    std::printf("%s ", label);
    if (!p) { std::printf("(nullptr)\n"); return; }
    for (int i = 0; i < n; ++i) std::printf("%02X ", p[i]);
    std::printf("\n");
}

bool any_nonzero(const uint8_t* p, int n) {
    if (!p) return false;
    for (int i = 0; i < n; ++i) if (p[i] != 0) return true;
    return false;
}

}  // namespace

int main() {
    Log::init();

    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image = "roms/nextzxos-1gb-fat32fix.img";
    cfg.bypass_tbblue_fw = true;
    cfg.rewind_buffer_frames = 0;  // disable rewind to make this fast

    Emulator emu;
    if (!emu.init(cfg)) { std::fprintf(stderr, "emu.init failed\n"); return 1; }

    std::printf("=== Post-init state (frame 0) ===\n");
    std::printf("ula.shadow_screen_en = %d\n", emu.ula().get_shadow_screen_en());

    const int frames = 600;
    std::printf("\nRunning %d frames ...\n", frames);
    for (int f = 0; f < frames; ++f) {
        emu.run_frame();
        if ((f+1) % 100 == 0) std::printf("  frame %d done\n", f+1);
    }
    std::printf("Done.\n");

    std::printf("\n=== Post-run state (frame %d) ===\n", frames);

    // ULA shadow flag
    std::printf("ula.shadow_screen_en = %d\n", emu.ula().get_shadow_screen_en());

    // NR 0x69 (Layer 2 + display control)
    std::printf("NR 0x69 cached = 0x%02X\n", emu.nextreg().cached(0x69));

    // NR 0x68 (display control)
    std::printf("NR 0x68 cached = 0x%02X\n", emu.nextreg().cached(0x68));

    // NR 0x8E (+3 paging emulation)
    std::printf("NR 0x8E cached = 0x%02X\n", emu.nextreg().cached(0x8E));

    // NR 0x8C (alt-ROM ctrl)
    std::printf("NR 0x8C cached = 0x%02X\n", emu.nextreg().cached(0x8C));

    // NR 0x03 cached
    std::printf("NR 0x03 cached = 0x%02X  config_mode = %d  machine_type = %d\n",
                emu.nextreg().cached(0x03),
                emu.nextreg().nr_03_config_mode(),
                emu.nextreg().nr_03_machine_type());

    // Layer 2 state — if NextZXOS draws to L2, content lives in SRAM
    // page indexed by NR $12 (active L2 page).
    std::printf("\n--- Layer 2 state ---\n");
    const uint8_t l2_active = emu.nextreg().cached(0x12);
    const uint8_t l2_shadow = emu.nextreg().cached(0x13);
    std::printf("  NR 0x12 (L2 active page) = 0x%02X\n", l2_active);
    std::printf("  NR 0x13 (L2 shadow page) = 0x%02X\n", l2_shadow);
    // L2 active page indexes 16K banks (each bank holds 16 KB of L2 pixels).
    // The first 32 bytes of the L2 active bank:
    if (l2_active > 0) {
        // Page is a 16K bank index; in 8K-page terms that's 2*l2_active.
        const uint8_t* p = emu.ram().page_ptr(l2_active * 2);
        std::printf("  L2 active bank %02X (8K page %d) first 32: ",
                    l2_active, l2_active * 2);
        if (p) for (int i = 0; i < 32; ++i) std::printf("%02X ", p[i]);
        std::printf("\n");
        std::printf("  L2 active any non-zero in 8 KB? %d\n",
                    p ? any_nonzero(p, 0x2000) : 0);
    } else {
        std::printf("  L2 active is bank 0 (start of SRAM — same as our ROM seed area)\n");
    }

    // Border color
    std::printf("ula.border = %d\n", emu.ula().get_border());

    // PC at the end
    std::printf("Z80 PC = 0x%04X\n", emu.cpu().get_registers().PC);

    // Legacy ULA VRAM = bank 5 = 8K pages $0A and $0B.
    // Pixels: bank 5 offset 0x0000-0x17FF (= 6 KB = pages $0A:$0000-$1FFF + $0B:$0000-$17FF)
    //   ULA actually reads 8 KB = page $0A entire (covers pixels + 256 bytes of attr area)
    // Attributes: bank 5 offset 0x1800-0x1AFF (= page $0B:$1800-$1AFF — wait no,
    //   the legacy attr area is at address $5800 = bank 5 offset $1800. Bank 5 = 16 KB.
    //   In 8K-page terms, bank 5 = pages $0A and $0B. Address $4000-$5FFF = page $0A
    //   (offset 0); $6000-$7FFF = page $0B. So pixels $4000-$57FF span page $0A entirely
    //   plus the first $1800 of page $0B.
    //   Attributes $5800-$5AFF span page $0B offset $1800-$1AFF.
    //
    // Wait — 16 KB bank = 2 * 8 KB pages. ULA screen size = 6912 bytes = pixels (6144) + attr (768).
    //   pixels $4000-$57FF = bytes 0..6143 of bank 5 = page $0A offset 0..6143
    //                                                  (page $0A is 8192 bytes; pixels fit entirely)
    //   attr   $5800-$5AFF = bytes 6144..6911 of bank 5 = page $0A offset 6144..6911

    // Task 25 (2026-07-10): banks 5 and 7-lower are dedicated dual-port
    // BRAMs (Mmu::bank5_vram / Mmu::bank7_bram), no longer aliased onto
    // ram_ pages 0x0A/0x0B/0x0E — read the buffers the ULA actually
    // fetches from (the ram_ pages now hold unrelated external-SRAM data,
    // e.g. the firmware's enNextMf.rom copy).
    std::printf("\n--- Legacy ULA bank 5 (dedicated VRAM) — pixels @ offset 0 ---\n");
    const uint8_t* p_normal_px = emu.mmu().bank5_vram();
    hex_line("  $4000:", p_normal_px, 32);
    hex_line("  $4100:", p_normal_px ? p_normal_px + 0x100 : nullptr, 32);

    std::printf("--- Legacy ULA bank 5 — attributes @ offset $1800 ---\n");
    hex_line("  $5800:", p_normal_px ? p_normal_px + 0x1800 : nullptr, 32);
    hex_line("  $5900:", p_normal_px ? p_normal_px + 0x1900 : nullptr, 32);

    std::printf("\n--- Shadow ULA bank 7 (dedicated BRAM) — pixels @ offset 0 ---\n");
    const uint8_t* p_shadow_px = emu.mmu().bank7_bram();
    hex_line("  $4000:", p_shadow_px, 32);
    std::printf("--- Shadow ULA bank 7 — attributes @ offset $1800 ---\n");
    hex_line("  $5800:", p_shadow_px ? p_shadow_px + 0x1800 : nullptr, 32);

    // Quick nonzero scan
    std::printf("\n--- Content presence ---\n");
    std::printf("  bank 5 page $0A any non-zero in first 8 KB? %d\n",
                p_normal_px ? any_nonzero(p_normal_px, 0x2000) : 0);
    std::printf("  bank 7 page $0E any non-zero in first 8 KB? %d\n",
                p_shadow_px ? any_nonzero(p_shadow_px, 0x2000) : 0);

    // Scan pixel area $4000-$57FF (6 KB) for any non-zero byte — would
    // indicate BASIC text has been drawn somewhere on screen.
    std::printf("\n--- Bank 5 pixel-area non-zero scan ($4000-$57FF) ---\n");
    int nz_count = 0; int first_nz_off = -1;
    if (p_normal_px) {
        for (int i = 0; i < 0x1800; ++i) {
            if (p_normal_px[i] != 0) {
                if (first_nz_off < 0) first_nz_off = i;
                ++nz_count;
            }
        }
    }
    std::printf("  non-zero bytes in pixel area: %d (first at offset 0x%04X)\n",
                nz_count, first_nz_off);
    // If pixels exist, dump 64 bytes around the first non-zero
    if (first_nz_off >= 0 && p_normal_px) {
        int start = (first_nz_off & ~0x1F);
        std::printf("  $%04X:", 0x4000 + start);
        for (int i = 0; i < 64 && (start + i) < 0x1800; ++i)
            std::printf(" %02X", p_normal_px[start + i]);
        std::printf("\n");
    }

    // Same scan for the attribute area $5800-$5AFF
    std::printf("\n--- Bank 5 attribute-area uniformity scan ($5800-$5AFF) ---\n");
    if (p_normal_px) {
        uint8_t first_attr = p_normal_px[0x1800];
        int diff_count = 0;
        for (int i = 0; i < 0x300; ++i) {
            if (p_normal_px[0x1800 + i] != first_attr) ++diff_count;
        }
        std::printf("  first attribute byte = 0x%02X, %d bytes differ from it\n",
                    first_attr, diff_count);
    }

    // BASIC system variables live at $5C00-$5CBF (legacy bank 5 offset $1C00).
    // Key ones:
    //   $5C3B FLAGS — bit 0 = "K cursor", bit 1 = "K input", ...
    //   $5C41 MODE — 0 = K/L mode, 1 = E mode, etc.
    //   $5C8F (etc.) — current PROG, ELINE
    std::printf("\n--- BASIC sysvars area ($5C00-$5C7F) ---\n");
    if (p_normal_px) {
        std::printf("  $5C00:");
        for (int i = 0; i < 32; ++i) std::printf(" %02X", p_normal_px[0x1C00 + i]);
        std::printf("\n  $5C20:");
        for (int i = 0; i < 32; ++i) std::printf(" %02X", p_normal_px[0x1C20 + i]);
        std::printf("\n  $5C40:");
        for (int i = 0; i < 32; ++i) std::printf(" %02X", p_normal_px[0x1C40 + i]);
        std::printf("\n  $5C60:");
        for (int i = 0; i < 32; ++i) std::printf(" %02X", p_normal_px[0x1C60 + i]);
        std::printf("\n");
    }

    // Port 0x7FFD shadow bit + paging info
    // We can't directly read port state but we can read NR 0x8E (port shadow) or
    // via port_7ffd accessor on Mmu if exposed.
    std::printf("\n--- Z80 register state ---\n");
    auto r = emu.cpu().get_registers();
    std::printf("  AF=0x%04X BC=0x%04X DE=0x%04X HL=0x%04X\n",
                r.AF, r.BC, r.DE, r.HL);
    std::printf("  IX=0x%04X IY=0x%04X SP=0x%04X PC=0x%04X\n",
                r.IX, r.IY, r.SP, r.PC);
    std::printf("  IFF1=%d IFF2=%d IM=%d  halted=%d\n",
                r.IFF1, r.IFF2, r.IM, r.halted);

    // MMU slot map
    std::printf("\n--- MMU slot map ---\n  slots = [");
    for (int s = 0; s < 8; ++s) std::printf(" %02X", emu.mmu().get_page(s));
    std::printf(" ]\n");
    std::printf("  boot_rom_enabled = %d, rom_in_sram = %d\n",
                emu.mmu().boot_rom_enabled(), emu.mmu().rom_in_sram());
    std::printf("  machine_type = %d\n", static_cast<int>(emu.mmu().machine_type()));

    // What the Z80 actually reads at PC=$0000, $0038 (IM1 ISR vector),
    // and the stack-top return address.
    std::printf("\n--- Z80 memory view ---\n");
    std::printf("  $0000:");
    for (int i = 0; i < 16; ++i) std::printf(" %02X", emu.mmu().read(i));
    std::printf("\n  $0038 (IM1 ISR):");
    for (int i = 0; i < 16; ++i) std::printf(" %02X", emu.mmu().read(0x0038 + i));
    std::printf("\n  $00EF (where bypass code starts after JP):");
    for (int i = 0; i < 16; ++i) std::printf(" %02X", emu.mmu().read(0x00EF + i));
    // Top of stack
    std::printf("\n  $%04X (SP):", r.SP);
    for (int i = 0; i < 16; ++i) std::printf(" %02X", emu.mmu().read(r.SP + i));
    std::printf("\n  $0066 (NMI vec):");
    for (int i = 0; i < 16; ++i) std::printf(" %02X", emu.mmu().read(0x0066 + i));
    std::printf("\n  IY-32..IY (sysvars before IY=$5C3A): ");
    for (int i = -16; i < 0; ++i) std::printf(" %02X", emu.mmu().read((r.IY + i) & 0xFFFF));
    std::printf("\n");

    return 0;
}
