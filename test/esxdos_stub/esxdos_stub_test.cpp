#include "core/emulator.h"
#include "core/emulator_config.h"
#include "platform/emulator_boot.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int passed = 0;
int failed = 0;

void check(const char* id, bool condition) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("FAIL %s\n", id);
    }
}

void write_string(Emulator& emu, uint16_t address, const std::string& value) {
    for (std::size_t i = 0; i < value.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(address + i), value[i]);
    emu.mmu().write(static_cast<uint16_t>(address + value.size()), 0);
}

bool call(Emulator& emu, uint8_t function, Z80Registers& regs) {
    return emu.cpu().on_esxdos_call && emu.cpu().on_esxdos_call(function, regs);
}

bool carry(const Z80Registers& regs) {
    return (regs.AF & 1) != 0;
}

} // namespace

int main() {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.esxdos_stub = true;
    cfg.rewind_buffer_frames = 0;
    cfg.load_file = "/tmp/jnext-esxdos/selector.nex";

    Emulator emu;
    emu.init(cfg);

    Z80Registers regs{};
    check("ESX-01-version",
          call(emu, 0x88, regs) && !carry(regs) && regs.BC == 0x4E58);

    constexpr uint16_t name_addr = 0x8000;
    constexpr uint16_t data_addr = 0x8100;
    write_string(emu, name_addr, "empty.cfg");
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0E00;
    const bool created_empty = call(emu, 0x9A, regs) && !carry(regs);
    regs.AF = 0x0100;
    const bool closed_empty = call(emu, 0x9B, regs) && !carry(regs);
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0100;
    check("ESX-02-empty-file",
          created_empty && closed_empty && call(emu, 0x9A, regs) && !carry(regs));

    write_string(emu, name_addr, "options.cfg");
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0E00;
    const bool opened_write = call(emu, 0x9A, regs) && !carry(regs);
    const uint8_t handle = static_cast<uint8_t>(regs.AF >> 8);
    emu.mmu().write(data_addr + 0, 0x43);
    emu.mmu().write(data_addr + 1, 0x42);
    emu.mmu().write(data_addr + 2, 0x01);
    regs = {};
    regs.AF = static_cast<uint16_t>(handle << 8);
    regs.IX = data_addr;
    regs.BC = 3;
    const bool wrote = call(emu, 0x9E, regs) && !carry(regs) && regs.BC == 3;
    regs.AF = static_cast<uint16_t>(handle << 8);
    const bool closed = call(emu, 0x9B, regs) && !carry(regs);
    check("ESX-03-write", opened_write && wrote && closed);

    emulator_cold_boot(emu, cfg);
    write_string(emu, name_addr, "options.cfg");
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0100;
    const bool opened_read = call(emu, 0x9A, regs) && !carry(regs);
    regs = {};
    regs.AF = static_cast<uint16_t>(handle << 8);
    regs.IX = data_addr;
    regs.BC = 3;
    const bool read = call(emu, 0x9D, regs) && !carry(regs) && regs.BC == 3;
    check("ESX-04-cold-boot-read",
          opened_read && read && emu.mmu().read(data_addr) == 0x43 &&
          emu.mmu().read(data_addr + 1) == 0x42 &&
          emu.mmu().read(data_addr + 2) == 0x01);

    write_string(emu, name_addr, "run game.nex");
    regs = {};
    regs.IX = name_addr;
    const bool run_ok = call(emu, 0x8F, regs) && !carry(regs);
    const auto expected = std::filesystem::path("/tmp/jnext-esxdos/game.nex");
    check("ESX-05-run-sibling",
          run_ok && std::filesystem::path(emu.take_nex_load_request()) == expected);
    check("ESX-06-request-consumed", emu.take_nex_load_request().empty());

    write_string(emu, name_addr, "RUN \"Game Two.NEX\"");
    regs = {};
    regs.IX = name_addr;
    const auto quoted = std::filesystem::path("/tmp/jnext-esxdos/Game Two.NEX");
    check("ESX-07-quoted-name",
          call(emu, 0x8F, regs) && !carry(regs) &&
          std::filesystem::path(emu.take_nex_load_request()) == quoted);

    write_string(emu, name_addr, "run ../escape.nex");
    regs = {};
    regs.IX = name_addr;
    check("ESX-08-reject-parent",
          call(emu, 0x8F, regs) && carry(regs) && emu.take_nex_load_request().empty());

    write_string(emu, name_addr, "run sibling.tap");
    regs = {};
    regs.IX = name_addr;
    check("ESX-09-reject-non-nex",
          call(emu, 0x8F, regs) && carry(regs) && emu.take_nex_load_request().empty());

    write_string(emu, name_addr, "delete sibling.nex");
    regs = {};
    regs.IX = name_addr;
    check("ESX-10-reject-command",
          call(emu, 0x8F, regs) && carry(regs) && emu.take_nex_load_request().empty());

    const int total = passed + failed;
    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n", total, passed, failed);
    return failed == 0 ? 0 : 1;
}
