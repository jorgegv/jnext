#include "core/nex_saver.h"
#include "core/emulator.h"
#include "core/log.h"

NexSaver::BuildResult NexSaver::save(Emulator& emu)
{
    Mmu&    mmu  = emu.mmu();
    Z80Cpu& cpu  = emu.cpu();
    auto    regs = cpu.get_registers();

    uint8_t border = emu.ula().get_border() & 0x07;

    auto result = build(regs.PC, regs.SP, border, mmu, emu.ram().size());

    if (!result.contiguous_entry_bank) {
        Log::emulator()->warn(
            "NEX saver: MMU slots 6/7 are not a contiguous bank pair "
            "(page6={} page7={}) — entry_bank in the saved file is a "
            "best-effort approximation (see NexSaver class doc-comment)",
            mmu.get_page(6), mmu.get_page(7));
    }
    if (result.truncated_by_112_bank_limit) {
        Log::emulator()->warn(
            "NEX saver: installed RAM ({} KB) exceeds the NEX format's "
            "112-bank (1792 KB) ceiling — only banks 0-111 were saved",
            emu.ram().size() / 1024);
    }

    Log::emulator()->info("NEX saver: saved snapshot ({} bytes, {} banks, PC={:#06x} SP={:#06x})",
                          result.data.size(), result.banks_written, regs.PC, regs.SP);
    return result;
}
