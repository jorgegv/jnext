// Tilemap raster-split fetch-state regression tests.
//
// TX-1696 changes NR 0x6E/0x6F during the visible frame: one map/tile pair
// draws the fixed HUD, another draws the scrolling playfield, then the HUD
// pair is restored near the bottom. The FPGA tilemap consumes these registers
// as live inputs (tilemap.vhd map_address_i / tile_address_i); rendering a
// completed frame from only the final register values paints one configuration
// across every scanline.
//
// All fixtures below are synthetic and generated in memory.

#include "memory/ram.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "cpu/z80_cpu.h"
#include "memory/mmu.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/tilemap.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t BANK5 = 5u * 16384u;
constexpr uint32_t MAP_A = BANK5 + 0x00u * 256u;
constexpr uint32_t MAP_B = BANK5 + 0x04u * 256u;
constexpr uint32_t DEF_A = BANK5 + 0x10u * 256u;
constexpr uint32_t DEF_B = BANK5 + 0x20u * 256u;

int g_total = 0;
int g_pass = 0;
int g_fail = 0;

void check(const char* id, bool condition, const char* detail) {
    ++g_total;
    if (condition) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, detail);
    }
}

void paint_palette(PaletteManager& palette, uint8_t index, uint8_t rgb8) {
    palette.write_control(0x30);  // tilemap first palette
    palette.set_index(index);
    palette.write_8bit(rgb8);
}

void fill_tile(Ram& ram, uint32_t base, uint8_t tile, uint8_t pixel) {
    const uint8_t packed = static_cast<uint8_t>((pixel << 4) | pixel);
    const uint32_t start = base + static_cast<uint32_t>(tile) * 32u;
    for (uint32_t i = 0; i < 32; ++i)
        ram.write(start + i, packed);
}

void write_map2(Ram& ram, uint32_t base, uint8_t tile, uint8_t attr = 0) {
    ram.write(base, tile);
    ram.write(base + 1, attr);
}

void write_map1(Ram& ram, uint32_t base, uint8_t tile) {
    ram.write(base, tile);
}

uint32_t render_first_pixel(Tilemap& tilemap, int line, const Ram& ram,
                            const PaletteManager& palette) {
    uint32_t pixels[640];
    bool below[640];
    std::memset(pixels, 0, sizeof(pixels));
    std::memset(below, 0, sizeof(below));
    tilemap.render_scanline(pixels, below, line, ram, palette);
    return pixels[0];
}

void nr_write(Emulator& emulator, uint8_t reg, uint8_t value) {
    emulator.port().out(0x243B, reg);
    emulator.port().out(0x253B, value);
}

void park_cpu(Emulator& emulator) {
    emulator.mmu().write(0x8000, 0x76);  // HALT
    auto regs = emulator.cpu().get_registers();
    regs.PC = 0x8000;
    regs.SP = 0xFFFD;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emulator.cpu().set_registers(regs);
}

void copper_word(Emulator& emulator, uint16_t word) {
    nr_write(emulator, 0x60, static_cast<uint8_t>(word >> 8));
    nr_write(emulator, 0x60, static_cast<uint8_t>(word));
}

void fresh(Tilemap& tilemap, PaletteManager& palette, Ram& ram) {
    tilemap.reset();
    palette.reset();
    ram.reset();
    paint_palette(palette, 0x01, 0xE0);
    paint_palette(palette, 0x02, 0x1C);
    paint_palette(palette, 0x11, 0x03);
    paint_palette(palette, 0x21, 0xFC);
}

void test_map_base_split() {
    Tilemap tilemap;
    PaletteManager palette;
    Ram ram;
    fresh(tilemap, palette, ram);

    fill_tile(ram, DEF_A, 1, 1);
    fill_tile(ram, DEF_A, 2, 2);
    write_map2(ram, MAP_A, 1);
    write_map2(ram, MAP_B, 2);

    tilemap.set_control(0x80);
    tilemap.set_map_base(0x00);
    tilemap.set_def_base(0x10);
    tilemap.init_fetch_per_line();
    tilemap.snapshot_fetch_for_line(0);
    tilemap.set_map_base(0x04);
    tilemap.snapshot_fetch_for_line(1);

    const uint32_t line0 = render_first_pixel(tilemap, 0, ram, palette);
    const uint32_t line1 = render_first_pixel(tilemap, 1, ram, palette);
    check("TM-SPLIT-01",
          line0 == palette.tilemap_colour(0x01) &&
          line1 == palette.tilemap_colour(0x02),
          "NR 0x6E map-base changes must affect only subsequent scanlines");
}

void test_definition_base_split() {
    Tilemap tilemap;
    PaletteManager palette;
    Ram ram;
    fresh(tilemap, palette, ram);

    write_map2(ram, MAP_A, 1);
    fill_tile(ram, DEF_A, 1, 1);
    fill_tile(ram, DEF_B, 1, 2);

    tilemap.set_control(0x80);
    tilemap.set_map_base(0x00);
    tilemap.set_def_base(0x10);
    tilemap.init_fetch_per_line();
    tilemap.snapshot_fetch_for_line(0);
    tilemap.set_def_base(0x20);
    tilemap.snapshot_fetch_for_line(1);

    const uint32_t line0 = render_first_pixel(tilemap, 0, ram, palette);
    const uint32_t line1 = render_first_pixel(tilemap, 1, ram, palette);
    check("TM-SPLIT-02",
          line0 == palette.tilemap_colour(0x01) &&
          line1 == palette.tilemap_colour(0x02),
          "NR 0x6F tile-definition changes must affect only subsequent scanlines");
}

void test_default_attribute_split() {
    Tilemap tilemap;
    PaletteManager palette;
    Ram ram;
    fresh(tilemap, palette, ram);

    write_map1(ram, MAP_A, 1);
    fill_tile(ram, DEF_A, 1, 1);

    tilemap.set_control(0xA0);  // enable + stripped attributes
    tilemap.set_map_base(0x00);
    tilemap.set_def_base(0x10);
    tilemap.set_default_attr(0x10);
    tilemap.init_fetch_per_line();
    tilemap.snapshot_fetch_for_line(0);
    tilemap.set_default_attr(0x20);
    tilemap.snapshot_fetch_for_line(1);

    const uint32_t line0 = render_first_pixel(tilemap, 0, ram, palette);
    const uint32_t line1 = render_first_pixel(tilemap, 1, ram, palette);
    check("TM-SPLIT-03",
          line0 == palette.tilemap_colour(0x11) &&
          line1 == palette.tilemap_colour(0x21),
          "NR 0x6C default-attribute changes must affect only subsequent scanlines");
}

void test_emulator_copper_wiring() {
    Emulator emulator;
    EmulatorConfig config;
    config.type = MachineType::ZXN_ISSUE2;
    config.rewind_buffer_frames = 0;
    if (!emulator.init(config)) {
        check("TM-SPLIT-04", false, "failed to initialize full Emulator fixture");
        return;
    }
    park_cpu(emulator);

    uint8_t* bank5 = emulator.mmu().bank5_vram();
    const uint32_t map_a = 0x0000;
    const uint32_t map_b = 0x2000;
    const uint32_t def_a = 0x1000;
    for (int entry = 0; entry < 40 * 32; ++entry) {
        bank5[map_a + entry * 2] = 1;
        bank5[map_a + entry * 2 + 1] = 0;
        bank5[map_b + entry * 2] = 2;
        bank5[map_b + entry * 2 + 1] = 0;
    }
    std::memset(bank5 + def_a + 1 * 32, 0x11, 32);
    std::memset(bank5 + def_a + 2 * 32, 0x22, 32);

    paint_palette(emulator.palette(), 0x01, 0xE0);
    paint_palette(emulator.palette(), 0x02, 0x1C);

    nr_write(emulator, 0x68, 0x80);  // hide ULA
    nr_write(emulator, 0x6B, 0x80);  // tilemap on, 40 columns, attributes present
    nr_write(emulator, 0x6E, 0x00);  // map A at frame start
    nr_write(emulator, 0x6F, 0x10);  // shared definitions

    // WAIT cvc=100; MOVE NR 0x6E,0x20; HALT. This exercises the real
    // Copper -> NextREG -> Emulator::on_scanline -> renderer path.
    constexpr int wait_cvc = 100;
    const uint16_t wait_100 = static_cast<uint16_t>(0x8000u | wait_cvc);
    const uint16_t move_map_b = static_cast<uint16_t>((0x6Eu << 8) | 0x20u);
    const uint16_t halt = static_cast<uint16_t>(0x8000u | 511u);
    nr_write(emulator, 0x61, 0x00);
    nr_write(emulator, 0x62, 0x00);
    copper_word(emulator, wait_100);
    copper_word(emulator, move_map_b);
    copper_word(emulator, halt);
    nr_write(emulator, 0x62, 0xC0);  // reset each frame + run

    emulator.run_frame();

    const uint32_t* framebuffer = emulator.get_framebuffer();
    const uint32_t expected_before = emulator.palette().tilemap_colour(0x01);
    const uint32_t expected_after = emulator.palette().tilemap_colour(0x02);
    const int expected_transition = wait_cvc + Renderer::DISP_Y + 1;
    int first_transition = -1;
    int transition_count = 0;
    int unexpected_row = -1;
    uint32_t unexpected_pixel = 0;
    uint32_t previous = framebuffer[Renderer::DISP_Y * Renderer::FB_WIDTH];
    for (int row = Renderer::DISP_Y; row < Renderer::FB_HEIGHT; ++row) {
        const uint32_t pixel = framebuffer[row * Renderer::FB_WIDTH];
        const uint32_t expected =
            row < expected_transition ? expected_before : expected_after;
        if (pixel != expected && unexpected_row < 0) {
            unexpected_row = row;
            unexpected_pixel = pixel;
        }
        if (row > Renderer::DISP_Y && pixel != previous) {
            ++transition_count;
            if (first_transition < 0)
                first_transition = row;
        }
        previous = pixel;
    }

    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "full Emulator/Copper path must produce one NR 0x6E transition "
                  "at row %d (first=%d count=%d unexpected-row=%d pixel=%08X map=%02X)",
                  expected_transition, first_transition, transition_count,
                  unexpected_row, unexpected_pixel,
                  emulator.tilemap().get_map_base_raw());
    check("TM-SPLIT-04",
          unexpected_row < 0 &&
          transition_count == 1 &&
          first_transition == expected_transition,
          detail);
}

} // namespace

int main() {
    std::printf("Tilemap raster-split fetch-state regression tests\n");
    test_map_base_split();
    test_definition_base_split();
    test_default_attribute_split();
    test_emulator_copper_wiring();
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
