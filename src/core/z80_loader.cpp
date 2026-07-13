#include "core/z80_loader.h"
#include "core/emulator.h"
#include "core/log.h"

#include <fstream>

bool Z80Loader::load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("Z80: cannot open '{}'", path);
        return false;
    }

    auto file_size = static_cast<size_t>(f.tellg());
    if (file_size < 30) {
        Log::emulator()->error("Z80: file '{}' too small ({} bytes, need >= 30)", path, file_size);
        return false;
    }

    std::vector<uint8_t> buf(file_size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(file_size));

    if (!load_from_buffer(buf)) {
        Log::emulator()->error("Z80: failed to parse '{}'", path);
        return false;
    }

    if (header_.version == 1) {
        Log::emulator()->info("Z80: loaded v1 '{}' — PC={:#06x} SP={:#06x}",
                               path, header_.PC, header_.SP);
    } else {
        Log::emulator()->info("Z80: loaded v{} '{}' — PC={:#06x} SP={:#06x} hardware_mode={} "
                               "is_128k={} pages={}",
                               header_.version, path, header_.PC, header_.SP,
                               header_.hardware_mode, header_.is_128k, pages_.size());
    }

    return true;
}

bool Z80Loader::apply(Emulator& emu) const
{
    if (!loaded_) {
        Log::emulator()->error("Z80: apply() called without successful load()");
        return false;
    }

    Mmu& mmu = emu.mmu();
    if (!apply_ram_to_mmu(mmu)) {
        Log::emulator()->error("Z80: apply_ram_to_mmu() failed");
        return false;
    }

    if (header_.is_128k) {
        emu.port().out(0x7FFD, header_.port_7ffd);
        Log::emulator()->debug("Z80: 128K paging set to {:#04x}", header_.port_7ffd);
    }

    Z80Registers regs{};
    regs.AF   = static_cast<uint16_t>((header_.A << 8) | header_.F);
    regs.BC   = header_.BC;
    regs.DE   = header_.DE;
    regs.HL   = header_.HL;
    regs.AF2  = static_cast<uint16_t>((header_.A2 << 8) | header_.F2);
    regs.BC2  = header_.BC2;
    regs.DE2  = header_.DE2;
    regs.HL2  = header_.HL2;
    regs.IX   = header_.IX;
    regs.IY   = header_.IY;
    regs.SP   = header_.SP;
    regs.PC   = header_.PC;
    regs.I    = header_.I;
    regs.R    = header_.R;
    regs.IM   = header_.IM;
    regs.IFF1 = header_.IFF1 ? 1 : 0;
    regs.IFF2 = header_.IFF2 ? 1 : 0;
    regs.halted = false;
    emu.cpu().set_registers(regs);

    Log::emulator()->debug("Z80: CPU set — PC={:#06x} SP={:#06x} AF={:#06x} IM={}",
                            regs.PC, regs.SP, regs.AF, regs.IM);

    emu.port().out(0x00FE, header_.border & 0x07);
    Log::emulator()->debug("Z80: border colour set to {}", header_.border & 0x07);

    return true;
}
