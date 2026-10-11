#include "core/szx_saver.h"
#include "core/emulator.h"
#include "core/log.h"
#include "core/sna_saver.h"

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
        case MachineType::PENTAGON:   machine_id = 7; break;  // ZXSTMID_PENTAGON128 (libspectrum szx.c:41-57)
        case MachineType::ZXN_ISSUE2:
        default:
            result.error =
                "SZX saver: the '.szx' format cannot represent this machine's RAM — "
                "it only supports 48K, 128K, Pentagon 128 and +2A/+3 (--machine 48k|128k|pentagon|plus3). "
                "Use '.jns' for a Next-native snapshot instead.";
            Log::emulator()->error("{}", result.error);
            return result;
    }

    Mmu&     mmu  = emu.mmu();
    Z80Cpu&  cpu  = emu.cpu();
    auto     regs = cpu.get_registers();

    // GH #324 (F3): the file carries banks 0-7, ch7ffd and ch1ffd, and the
    // loader rebuilds the 0x4000-0xFFFF map from them alone. Extended paging
    // puts a bank above 7 at 0xC000 (port 0xDFFD bits, NR 0x8F Pentagon
    // 512/1024: zxnext.vhd:3763-3766) and the Next MMU registers can move
    // slots 2-7; either way the reload maps a different bank and loses the
    // real one. Refuse, like SnaSaver. +3 special paging is described by
    // ch1ffd, so its (deliberately different) map is not checked.
    const bool special_paging = emu.config().type == MachineType::ZX_PLUS3 &&
                                (mmu.port_1ffd() & 0x01);
    if (!special_paging) {
        const uint8_t bank_c000 = emu.config().type == MachineType::ZX48K
            ? 0 : static_cast<uint8_t>(mmu.port_7ffd() & 0x07);
        std::string found;
        if (!classic_window_intact(mmu, bank_c000, found)) {
            result.error =
                "SZX saver: the RAM mapped at 0x4000-0xFFFF is not the map an '.szx' "
                "describes (bank 5, bank 2, then bank " + std::to_string(bank_c000) + ") - "
                + found + ". EXTENDED PAGING (port 0xDFFD, NR 0x8F) or the Next MMU "
                "registers moved it; the file has no field for that and would reload "
                "a different bank. Use '.jns'.";
            Log::emulator()->error("{}", result.error);
            return result;
        }
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
