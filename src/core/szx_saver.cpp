#include "core/szx_saver.h"
#include "core/emulator.h"
#include "core/log.h"

SzxSaver::SaveResult SzxSaver::save(Emulator& emu)
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

    constexpr unsigned SZX_MAX_RAM_BANKS = 64;  // libspectrum: page > 63 rejected — see class doc-comment
    const unsigned installed_banks = static_cast<unsigned>(emu.ram().size() / 16384);
    const unsigned written_banks   = installed_banks > SZX_MAX_RAM_BANKS
                                    ? SZX_MAX_RAM_BANKS : installed_banks;
    const bool     truncated       = installed_banks > SZX_MAX_RAM_BANKS;
    if (truncated) {
        Log::emulator()->warn(
            "SZX saver: installed RAM ({} KB) exceeds the .szx format's "
            "real-world 64-bank (1024 KB) ceiling (libspectrum "
            "szx.c:read_ramp_chunk rejects page > 63 — NOT the same as "
            "jnext's own 112-bank MMU ceiling) — only banks 0-{} were "
            "saved; banks {}-{} are NOT in this file",
            emu.ram().size() / 1024, written_banks - 1, written_banks, installed_banks - 1);
    }
    uint32_t tstates = *fuse_z80_tstates_ptr();

    SaveResult result;
    result.data             = build(regs, tstates, machine_id, spec, mmu, installed_banks);
    result.truncated        = truncated;
    result.banks_written    = written_banks;
    result.banks_installed  = installed_banks;
    Log::emulator()->info("SZX saver: saved snapshot ({} bytes, {} of {} RAM banks, machine_id={})",
                          result.data.size(), written_banks, installed_banks, machine_id);
    return result;
}
