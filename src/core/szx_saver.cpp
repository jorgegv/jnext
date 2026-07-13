#include "core/szx_saver.h"
#include "core/emulator.h"
#include "core/log.h"

std::vector<uint8_t> SzxSaver::save(Emulator& emu)
{
    Mmu&     mmu  = emu.mmu();
    Z80Cpu&  cpu  = emu.cpu();
    auto     regs = cpu.get_registers();

    // ZXSTMID_* machine identifiers per the zx-state spec (predates the
    // Next; there is no dedicated Next id). ZXN_ISSUE2 is treated as
    // ZXSTMID_PLUS3 (5) — the Next is a +3-compatible superset for
    // classic-port paging purposes, same convention already used
    // elsewhere in this codebase (e.g. NextZXOS tape LOAD routing
    // through the +3 BASIC submenu).
    uint8_t machine_id;
    switch (emu.config().type) {
        case MachineType::ZX48K:      machine_id = 1; break;  // ZXSTMID_48K
        case MachineType::ZX128K:     machine_id = 2; break;  // ZXSTMID_128K
        case MachineType::ZX_PLUS3:   machine_id = 5; break;  // ZXSTMID_PLUS3
        case MachineType::ZXN_ISSUE2: machine_id = 5; break;  // treated as +3
        default:                      machine_id = 2; break;
    }

    SpecRegs spec;
    spec.border    = emu.ula().get_border() & 0x07;
    spec.port_7ffd = mmu.port_7ffd();
    spec.port_1ffd = mmu.port_1ffd();
    // SzxLoader::apply() restores the border by writing chFe straight to
    // port 0xFE (it does not separately consult chBorder), so bits 2:0
    // here must already carry the border colour.
    spec.port_fe = static_cast<uint8_t>(
        spec.border
        | (emu.beeper().mic() ? 0x08 : 0x00)
        | (emu.beeper().ear() ? 0x10 : 0x00));

    unsigned bank_count = static_cast<unsigned>(emu.ram().size() / 16384);
    if (bank_count > 112) {
        Log::emulator()->warn(
            "SZX saver: installed RAM ({} KB) exceeds the MMU-reachable "
            "112-bank (1792 KB) ceiling (VHDL zxnext.vhd:2964, pages "
            ">= 0xE0 are not general RAM) — only banks 0-111 were saved",
            emu.ram().size() / 1024);
    }
    uint32_t tstates = *fuse_z80_tstates_ptr();

    auto data = build(regs, tstates, machine_id, spec, mmu, bank_count);
    Log::emulator()->info("SZX saver: saved snapshot ({} bytes, {} RAM banks, machine_id={})",
                          data.size(), bank_count, machine_id);
    return data;
}
