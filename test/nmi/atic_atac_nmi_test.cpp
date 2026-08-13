// Atic Atac Next NMI regressions (GH #84).
//
// Runtime-generated fixtures only: no game data is included. These rows pin
// the four NMI/overlay behaviours exposed while differentially debugging the
// freeware Atic Atac Next release against ZEsarUX and the official FPGA RTL.
//
// Run: ./build/test/atic_atac_nmi_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;

std::string fmt(const char* format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

void check(const char* id, const char* description, bool passed,
           const std::string& detail = {})
{
    if (passed) {
        ++g_pass;
        return;
    }
    ++g_fail;
    std::printf("  FAIL %s: %s", id, description);
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}

void write_nr(Emulator& emu, uint8_t reg, uint8_t value)
{
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, value);
}

void fresh_emulator(Emulator& emu)
{
    EmulatorConfig config;
    config.type = MachineType::ZXN_ISSUE2;
    config.rewind_buffer_frames = 0;
    emu.init(config);

    // Leave the power-on config mode, as real firmware does.
    write_nr(emu, 0x03, 0x01);
}

struct NmiResult {
    uint16_t pc;
    int steps;
    bool taken;
};

NmiResult step_until_nmi(Emulator& emu)
{
    for (int step = 1; step <= 200; ++step) {
        emu.execute_single_instruction();
        const uint16_t pc = emu.cpu().get_registers().PC;
        if (pc >= 0x0066 && pc <= 0x006F) {
            return {pc, step, true};
        }
    }
    return {emu.cpu().get_registers().PC, 200, false};
}

void test_divmmc_rom_is_physical_sram()
{
    Emulator emu;
    fresh_emulator(emu);

    // Atic writes JP IY into physical SRAM page $08 while in config mode.
    write_nr(emu, 0x03, 0x07);
    write_nr(emu, 0x04, 0x04);
    emu.mmu().write(0x0066, 0xFD);
    emu.mmu().write(0x0067, 0xE9);
    write_nr(emu, 0x03, 0x03);

    write_nr(emu, 0x83, 0xFF);
    write_nr(emu, 0x0A, 0x10);
    write_nr(emu, 0xBB, 0x02);
    write_nr(emu, 0x06, 0x30);

    emu.mmu().write(0xC000, 0x00);
    auto regs = emu.cpu().get_registers();
    regs.PC = 0xC000;
    regs.SP = 0xFFFE;
    regs.IY = 0x2000;
    emu.cpu().set_registers(regs);

    write_nr(emu, 0x02, 0x04);
    const NmiResult result = step_until_nmi(emu);
    const uint8_t physical_66 = emu.ram().page_ptr(0x08)[0x0066];
    const uint8_t overlay_66 = emu.divmmc().rom_data()[0x0066];
    if (result.taken) emu.execute_single_instruction();
    const uint16_t handler_pc = emu.cpu().get_registers().PC;

    check("ATIC-NMI-01",
          "config-mode SRAM page $08 supplies the DivMMC $0066 handler",
          result.taken
              && physical_66 == 0xFD
              && overlay_66 == 0xFD
              && emu.divmmc().automap_active()
              && handler_pc == 0x2000,
          fmt("taken=%d physical=%02x overlay=%02x automap=%d pc=%04x",
              result.taken, physical_66, overlay_66,
              emu.divmmc().automap_active(), handler_pc));
}

void test_stackless_nmi()
{
    Emulator emu;
    fresh_emulator(emu);

    write_nr(emu, 0x50, 0x20);
    emu.mmu().write(0x0066, 0xED);
    emu.mmu().write(0x0067, 0x45);
    emu.mmu().write(0xFFFC, 0xA5);
    emu.mmu().write(0xFFFD, 0x5A);
    write_nr(emu, 0xC0, 0x08);

    auto regs = emu.cpu().get_registers();
    regs.PC = 0xC000;
    regs.SP = 0xFFFE;
    regs.IFF1 = 1;
    regs.IFF2 = 1;
    emu.cpu().set_registers(regs);

    emu.cpu().request_nmi();
    emu.execute_single_instruction();
    const auto at_handler = emu.cpu().get_registers();
    const uint16_t captured = static_cast<uint16_t>(
        static_cast<uint16_t>(emu.nextreg().peek(0xC3)) << 8 |
        emu.nextreg().peek(0xC2));
    const bool stack_untouched =
        emu.mmu().read(0xFFFC) == 0xA5 &&
        emu.mmu().read(0xFFFD) == 0x5A;

    StateWriter measure;
    emu.save_state(measure);
    std::vector<uint8_t> snapshot(measure.position());
    StateWriter writer(snapshot.data(), snapshot.size());
    emu.save_state(writer);
    emu.cpu().set_stackless_retn_active_for_load(false);
    StateReader reader(snapshot.data(), snapshot.size());
    const bool snapshot_ok =
        !writer.overflow() && emu.load_state(reader) &&
        emu.cpu().stackless_retn_active();

    // An older snapshot ends after the input sentinel and has no appended
    // stackless-NMI field. Loading one over a live latch must clear it rather
    // than inherit state from the machine being replaced.
    //
    // It is synthesised by truncating EVERY append that follows the input
    // sentinel, each of which is one bool plus its u32 sentinel. There are two:
    // the stackless-RETN latch (GH #84) and the joystick-UART cable's cursor
    // (GH #251) — which contributes only its PRESENT flag here, since this
    // emulator has no cable attached. A THIRD append makes this row fail rather
    // than quietly stop testing what it names (which is exactly how the GH #251
    // one was caught), so add it to the count below when you add it to
    // Emulator::save_state.
    constexpr std::size_t kTailBlock   = sizeof(uint8_t) + sizeof(uint32_t);
    constexpr std::size_t kTailBlocks  = 2;   // stackless_nmi, joy_uart
    Emulator old_snapshot_emu;
    fresh_emulator(old_snapshot_emu);
    old_snapshot_emu.cpu().set_stackless_retn_active_for_load(true);
    const std::size_t old_snapshot_size = snapshot.size() - kTailBlocks * kTailBlock;
    StateReader old_reader(snapshot.data(), old_snapshot_size);
    const bool old_snapshot_ok =
        old_snapshot_emu.load_state(old_reader) &&
        !old_snapshot_emu.cpu().stackless_retn_active();

    // And the BOUNDARY, so the arithmetic above is pinned rather than merely
    // landing somewhere before the field: chopping one block fewer must leave
    // the stackless field present and RESTORE the latch. Without this pair a
    // wrong count still passes the row — it only has to truncate far enough.
    Emulator boundary_emu;
    fresh_emulator(boundary_emu);
    boundary_emu.cpu().set_stackless_retn_active_for_load(false);
    StateReader boundary_reader(snapshot.data(), snapshot.size() - kTailBlock);
    const bool boundary_ok =
        boundary_emu.load_state(boundary_reader) &&
        boundary_emu.cpu().stackless_retn_active();

    // The return bus is live: the handler may replace the captured address.
    write_nr(emu, 0xC2, 0x34);
    write_nr(emu, 0xC3, 0x12);
    emu.execute_single_instruction();
    const auto after_retn = emu.cpu().get_registers();

    check("ATIC-NMI-02",
          "stackless NMI suppresses stack RAM cycles and returns via live C3:C2",
          at_handler.PC == 0x0066
              && at_handler.SP == 0xFFFC
              && captured == 0xC000
              && stack_untouched
              && snapshot_ok
              && old_snapshot_ok
              && boundary_ok
              && after_retn.PC == 0x1234
              && after_retn.SP == 0xFFFE
              && after_retn.IFF1 == 1
              && !emu.cpu().stackless_retn_active(),
          fmt("entry=%04x/%04x captured=%04x stack=%d snapshot=%d old=%d "
              "boundary=%d return=%04x/%04x iff1=%u active=%d",
              at_handler.PC, at_handler.SP, captured, stack_untouched,
              snapshot_ok, old_snapshot_ok, boundary_ok,
              after_retn.PC, after_retn.SP,
              after_retn.IFF1,
              emu.cpu().stackless_retn_active()));
}

void test_divmmc_retn_boundary()
{
    Emulator emu;
    fresh_emulator(emu);

    write_nr(emu, 0x50, 0x20);
    emu.mmu().write(0x1000, 0x3C);  // underlying INC A

    uint8_t* divmmc_rom = emu.ram().page_ptr(0x08);
    divmmc_rom[0x0066] = 0xED;
    divmmc_rom[0x0067] = 0x45;
    divmmc_rom[0x1000] = 0xED;
    divmmc_rom[0x1001] = 0x23;  // stale-overlay SWAPNIB poison

    emu.divmmc().set_enabled(true);
    emu.divmmc().set_nr_0a_4_enable(true);
    emu.divmmc().set_entry_points_1(0x02);
    emu.divmmc().set_button_nmi(true);

    auto regs = emu.cpu().get_registers();
    regs.PC = 0x1000;
    regs.SP = 0xFFFE;
    regs.AF = 0x1200;
    regs.IFF1 = 1;
    regs.IFF2 = 1;
    emu.cpu().set_registers(regs);

    emu.cpu().request_nmi();
    emu.execute_single_instruction();
    emu.execute_single_instruction();
    const auto after_retn = emu.cpu().get_registers();
    const bool overlay_cleared = !emu.divmmc().automap_active();
    emu.execute_single_instruction();
    const auto after_returned = emu.cpu().get_registers();

    check("ATIC-NMI-03",
          "DivMMC clears after RETN but before returned-opcode predecode",
          after_retn.PC == 0x1000
              && overlay_cleared
              && after_returned.PC == 0x1001
              && static_cast<uint8_t>(after_returned.AF >> 8) == 0x13,
          fmt("retn_pc=%04x cleared=%d return_pc=%04x A=%02x",
              after_retn.PC, overlay_cleared, after_returned.PC,
              static_cast<uint8_t>(after_returned.AF >> 8)));
}

void test_multiface_retn_boundary()
{
    Emulator emu;
    fresh_emulator(emu);

    emu.mmu().set_boot_rom_enabled(false);
    write_nr(emu, 0x50, 0x20);
    emu.mmu().write(0x1000, 0x3C);  // underlying INC A

    uint8_t* mf_rom = emu.ram().page_ptr(0x0A);
    mf_rom[0x0066] = 0xED;
    mf_rom[0x0067] = 0x45;
    mf_rom[0x1000] = 0xED;
    mf_rom[0x1001] = 0x23;  // stale-overlay SWAPNIB poison

    emu.multiface().set_enabled(true);
    emu.multiface().set_mode(0x02);
    emu.multiface().button_press();

    auto regs = emu.cpu().get_registers();
    regs.PC = 0x1000;
    regs.SP = 0xFFFE;
    regs.AF = 0x1200;
    regs.IFF1 = 1;
    regs.IFF2 = 1;
    emu.cpu().set_registers(regs);

    emu.cpu().request_nmi();
    emu.execute_single_instruction();
    emu.execute_single_instruction();
    const auto after_retn = emu.cpu().get_registers();
    const bool overlay_cleared = !emu.multiface().is_mem_active();
    emu.execute_single_instruction();
    const auto after_returned = emu.cpu().get_registers();

    check("ATIC-NMI-04",
          "Multiface clears after RETN but before returned-opcode predecode",
          after_retn.PC == 0x1000
              && overlay_cleared
              && after_returned.PC == 0x1001
              && static_cast<uint8_t>(after_returned.AF >> 8) == 0x13,
          fmt("retn_pc=%04x cleared=%d return_pc=%04x A=%02x",
              after_retn.PC, overlay_cleared, after_returned.PC,
              static_cast<uint8_t>(after_returned.AF >> 8)));
}

}  // namespace

int main()
{
    std::printf("Atic Atac Next NMI Regression Tests\n");
    std::printf("===================================\n\n");

    test_divmmc_rom_is_physical_sram();
    test_stackless_nmi();
    test_divmmc_retn_boundary();
    test_multiface_retn_boundary();

    std::printf("\n===================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_pass + g_fail, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
