#include "core/sna_saver.h"
#include "core/emulator.h"
#include "core/log.h"

/// Write a little-endian uint16_t to a byte buffer.
static void put_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

/// Read `len` bytes from a physical 8K RAM page via the MMU.
/// Temporarily maps the page into slot 7 (0xE000-0xFFFF), reads, then restores.
static void read_from_page(Mmu& mmu, uint8_t page, uint8_t* dst,
                           size_t page_offset, size_t len)
{
    constexpr int TEMP_SLOT = 7;
    constexpr uint16_t SLOT_BASE = 0xE000;

    uint8_t saved = mmu.get_page(TEMP_SLOT);
    mmu.set_page(TEMP_SLOT, page);

    for (size_t i = 0; i < len; ++i) {
        dst[i] = mmu.read(static_cast<uint16_t>(SLOT_BASE + page_offset + i));
    }

    mmu.set_page(TEMP_SLOT, saved);
}

/// Read `len` bytes from RAM starting at the given 8K page.
static void read_from_ram(Mmu& mmu, uint16_t start_page, size_t start_offset,
                          uint8_t* dst, size_t len)
{
    size_t remaining = len;
    size_t dst_pos = 0;
    uint16_t page = start_page;
    size_t page_off = start_offset;

    while (remaining > 0) {
        size_t chunk = std::min(remaining, static_cast<size_t>(0x2000) - page_off);
        read_from_page(mmu, static_cast<uint8_t>(page), dst + dst_pos, page_off, chunk);
        dst_pos += chunk;
        remaining -= chunk;
        ++page;
        page_off = 0;
    }
}

/// Read one 16K RAM bank (physical 8K pages 2N, 2N+1) into `dst`.
static void read_bank(Mmu& mmu, uint8_t bank, uint8_t* dst) {
    read_from_ram(mmu, static_cast<uint16_t>(bank * 2), 0, dst, 16384);
}

/// The set of banks the 128K form carries AFTER its three named blocks: every
/// bank except 5, 2 and the one paged at 0xC000. SnaLoader::apply() computes
/// the same set with the same rule (`if (bank == 2 || bank == 5 ||
/// bank == paged_bank) continue;`), which is what makes this the exact inverse
/// for EVERY port 0x7FFD value, including the two where the paged bank is
/// itself 2 or 5 and six banks remain rather than five.
static std::vector<uint8_t> extra_bank_set(uint8_t paged_bank) {
    std::vector<uint8_t> banks;
    for (uint8_t b = 0; b < 8; ++b) {
        if (b == 2 || b == 5 || b == paged_bank) continue;
        banks.push_back(b);
    }
    return banks;
}

std::vector<uint8_t> SnaSaver::save(Emulator& emu, std::string* error) {
    // GH #274 — the form follows the MACHINE, and a machine neither form can
    // describe is REFUSED rather than written lossily. It used to write a 48K
    // SNA for every machine and exit 0, which told the user nothing was missing
    // when almost everything was. Same shape as SzxSaver::save(): no data, a
    // reason the caller can show, and a pointer at a format that can do the job.
    // The reasoning per machine is in the class doc-comment.
    auto refuse = [&](const std::string& msg) {
        Log::emulator()->error("{}", msg);
        if (error) *error = msg;
        return std::vector<uint8_t>{};
    };

    switch (emu.config().type) {
        case MachineType::ZX48K:
            // The CPU view IS the machine: banks 5, 2, 0, no paging register.
            return save_cpu_view_unchecked(emu);

        case MachineType::ZX128K:
            return save_128k(emu);

        case MachineType::ZX_PLUS3: {
            // Representable as a 128K machine unless port 0x1FFD says
            // otherwise — see the class doc-comment PLUS3 for why bit 0 and
            // bit 2 are refusals and bits 1/3 are not.
            const uint8_t p1ffd = emu.mmu().port_1ffd();
            if (p1ffd & 0x01) {
                return refuse(
                    "SNA saver: this +3 is in SPECIAL PAGING (port 0x1FFD bit 0) — four RAM "
                    "banks and no ROM across the whole address space. The '.sna' format has "
                    "no 0x1FFD byte and defines its three blocks as banks 5, 2 and the bank "
                    "paged at 0xC000, which is not that layout. Use '.szx', which carries "
                    "0x1FFD, or '.jns'.");
            }
            if (p1ffd & 0x04) {
                return refuse(
                    "SNA saver: this +3 has ROM 2 or ROM 3 paged (port 0x1FFD bit 2). The "
                    "'.sna' format carries only 0x7FFD bit 4, so the snapshot would come "
                    "back running a different ROM. Use '.szx', which carries 0x1FFD, or "
                    "'.jns'.");
            }
            return save_128k(emu);
        }

        case MachineType::ZXN_ISSUE2:
        default:
            return refuse(
                "SNA saver: the '.sna' format cannot represent a ZX Spectrum Next — it "
                "describes a 48K/128K Spectrum, with none of the Next's extra RAM, "
                "NextREGs or video hardware (--machine 48k|128k|plus3). "
                "Use '.jns' for a Next-native snapshot instead.");
    }
}

std::vector<uint8_t> SnaSaver::save_128k(Emulator& emu) {
    // 128K SNA: 27-byte header + 48 KB (bank 5, bank 2, the bank paged at
    // 0xC000) + 4-byte extended header (PC, 0x7FFD, TR-DOS) + the remaining
    // banks. SnaLoader::load_from_buffer()/apply() is the oracle for every
    // offset here.
    static constexpr size_t HEADER_SIZE   = 27;
    static constexpr size_t RAM48_SIZE    = 49152;
    static constexpr size_t EXT_HDR_AT    = HEADER_SIZE + RAM48_SIZE;   // 49179
    static constexpr size_t EXT_HDR_SIZE  = 4;
    static constexpr size_t BANK_SIZE     = 16384;

    Mmu&    mmu   = emu.mmu();
    auto    regs  = emu.cpu().get_registers();
    const uint8_t port_7ffd  = mmu.port_7ffd();
    const uint8_t paged_bank = static_cast<uint8_t>(port_7ffd & 0x07);
    const std::vector<uint8_t> extra = extra_bank_set(paged_bank);

    std::vector<uint8_t> data(EXT_HDR_AT + EXT_HDR_SIZE + extra.size() * BANK_SIZE, 0);

    // ---- the 27-byte header -------------------------------------------
    // Identical layout to the 48K form with ONE difference that matters: SP is
    // the machine's REAL SP. This form carries PC in the extended header, so
    // nothing is pushed and the guest's stack is left exactly as it was.
    data[0] = regs.I;
    put_u16(data.data() + 1,  regs.HL2);
    put_u16(data.data() + 3,  regs.DE2);
    put_u16(data.data() + 5,  regs.BC2);
    put_u16(data.data() + 7,  regs.AF2);
    put_u16(data.data() + 9,  regs.HL);
    put_u16(data.data() + 11, regs.DE);
    put_u16(data.data() + 13, regs.BC);
    put_u16(data.data() + 15, regs.IY);
    put_u16(data.data() + 17, regs.IX);
    data[19] = regs.IFF2 ? 0x04 : 0x00;   // SnaLoader reads bit 2 -> IFF1 = IFF2
    data[20] = regs.R;
    put_u16(data.data() + 21, regs.AF);
    put_u16(data.data() + 23, regs.SP);
    data[25] = regs.IM;
    data[26] = static_cast<uint8_t>(emu.ula().get_border() & 0x07);

    // ---- bank 5, bank 2, the bank paged at 0xC000 ---------------------
    read_bank(mmu, 5, data.data() + HEADER_SIZE);
    read_bank(mmu, 2, data.data() + HEADER_SIZE + BANK_SIZE);
    read_bank(mmu, paged_bank, data.data() + HEADER_SIZE + 2 * BANK_SIZE);

    // ---- the 4-byte extended header -----------------------------------
    put_u16(data.data() + EXT_HDR_AT, regs.PC);
    data[EXT_HDR_AT + 2] = port_7ffd;
    data[EXT_HDR_AT + 3] = 0;   // TR-DOS paged: jnext emulates no TR-DOS

    // ---- the remaining banks, ascending -------------------------------
    size_t off = EXT_HDR_AT + EXT_HDR_SIZE;
    for (uint8_t bank : extra) {
        read_bank(mmu, bank, data.data() + off);
        off += BANK_SIZE;
    }

    Log::emulator()->info("SNA saver: saved 128K snapshot ({} bytes, port_7ffd={:#04x}, "
                          "paged bank {} at 0xC000, {} further bank(s))",
                          data.size(), port_7ffd, paged_bank, extra.size());
    return data;
}

std::vector<uint8_t> SnaSaver::save_cpu_view_unchecked(Emulator& emu) {
    // 48K SNA: 27-byte header + 49152 bytes RAM
    static constexpr size_t SNA_48K_SIZE = 49179;
    static constexpr size_t HEADER_SIZE = 27;
    static constexpr size_t RAM_SIZE = 49152;

    std::vector<uint8_t> data(SNA_48K_SIZE, 0);

    Mmu& mmu = emu.mmu();
    Z80Cpu& cpu = emu.cpu();
    auto regs = cpu.get_registers();

    // For 48K SNA, PC is pushed onto the stack (destructive to stack).
    // We modify SP to push the return address.
    uint16_t sp = regs.SP;

    // Push PC onto stack
    sp -= 2;
    mmu.write(sp, static_cast<uint8_t>(regs.PC & 0xFF));
    mmu.write(static_cast<uint16_t>(sp + 1), static_cast<uint8_t>(regs.PC >> 8));

    // Build header
    data[0] = regs.I;
    put_u16(data.data() + 1,  regs.HL2);
    put_u16(data.data() + 3,  regs.DE2);
    put_u16(data.data() + 5,  regs.BC2);
    put_u16(data.data() + 7,  regs.AF2);
    put_u16(data.data() + 9,  regs.HL);
    put_u16(data.data() + 11, regs.DE);
    put_u16(data.data() + 13, regs.BC);
    put_u16(data.data() + 15, regs.IY);
    put_u16(data.data() + 17, regs.IX);
    data[19] = regs.IFF2 ? 0x04 : 0x00;
    data[20] = regs.R;
    put_u16(data.data() + 21, regs.AF);
    put_u16(data.data() + 23, sp);
    data[25] = regs.IM;
    // The border the machine is showing. It used to be written as 0 ("fine for
    // RZX"), but it is not: an RZX embeds this snapshot, and a program that
    // set its border once and never again then replayed with a black one.
    data[26] = static_cast<uint8_t>(emu.ula().get_border() & 0x07);

    // Read RAM: Bank 5 (pages 10,11), Bank 2 (pages 4,5), Bank 0 (pages 0,1)
    read_from_ram(mmu, 10, 0, data.data() + HEADER_SIZE, 16384);
    read_from_ram(mmu, 4, 0, data.data() + HEADER_SIZE + 16384, 16384);
    read_from_ram(mmu, 0, 0, data.data() + HEADER_SIZE + 32768, 16384);

    Log::emulator()->info("SNA saver: saved 48K snapshot ({} bytes)", data.size());
    return data;
}
