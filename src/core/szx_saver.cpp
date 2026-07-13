#include "core/szx_saver.h"
#include "core/emulator.h"
#include "core/log.h"

SzxSaver::SaveResult SzxSaver::save(Emulator& emu)
{
    SaveResult result;

    // .szx is a classic-Spectrum interchange format — it can only
    // represent 48K, 128K and +2A/+3 (see class doc-comment SCOPE). jnext
    // has no distinct +2A MachineType (ZX_PLUS3 covers both — identical
    // memory model). Every other machine, including the Next itself
    // (jnext's DEFAULT --machine), is refused outright: no truncation, no
    // lying chMachineId, no partial file.
    uint8_t machine_id;
    switch (emu.config().type) {
        case MachineType::ZX48K:      machine_id = 1; break;  // ZXSTMID_48K
        case MachineType::ZX128K:     machine_id = 2; break;  // ZXSTMID_128K
        case MachineType::ZX_PLUS3:   machine_id = 5; break;  // ZXSTMID_PLUS3
        case MachineType::ZXN_ISSUE2:
        default:
            result.error =
                "SZX saver: the '.szx' format cannot represent this machine's RAM — "
                "it only supports 48K, 128K and +2A/+3 (--machine 48k|128k|plus3). "
                "Use '.nex' for a Next-native snapshot instead.";
            Log::emulator()->error("{}", result.error);
            return result;
    }

    Mmu&     mmu  = emu.mmu();
    Z80Cpu&  cpu  = emu.cpu();
    auto     regs = cpu.get_registers();

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

    uint32_t tstates = *fuse_z80_tstates_ptr();

    result.data = build(regs, tstates, machine_id, spec, mmu, &result.error);
    result.ok   = !result.data.empty();
    if (result.ok) {
        Log::emulator()->info("SZX saver: saved snapshot ({} bytes, machine_id={})",
                              result.data.size(), machine_id);
    } else if (!result.error.empty()) {
        Log::emulator()->error("{}", result.error);
    }
    return result;
}
