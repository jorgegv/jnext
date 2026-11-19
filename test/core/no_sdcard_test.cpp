// --no-sdcard (GH #540): a directly loaded NEX on the synthetic machine with no
// SD card, so no NextZXOS and no ROMs. These rows drive the Emulator the mode
// configures (EmulatorConfig::no_sdcard) and check what the issue asks of it:
//
//   ROM   the first opcode fetched from a ROM is reported once, naming the
//         address and its likely cause (IM 1, RST $nn, a call or jump);
//         a program that stays in RAM, RAM mapped at $0000 included, is not
//   DEV   DivMMC and Multiface are held off whatever NR 0x83 says; the NMI
//         buttons are refused; an SD card change is refused
//
// There is no VHDL counterpart: the mode is an emulator convenience (a real
// Next always has its ROMs), like the esxDOS stand-in. Every program here is a
// few invented bytes poked into RAM; no third-party code.
//
// Run: ./build/test/no_sdcard_test

#include "core/emulator.h"
#include "core/emulator_config.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    bool        passed;
};
std::vector<Result> g_results;
std::string         g_group;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    ++g_total;
    g_results.push_back({g_group, id, cond});
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string fmt(const char* f, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// A Next with no card (no_sdcard) or, for the control rows, the same machine
// without the mode. Neither has an SD image, so neither has ROM bytes: the
// difference under test is the mode alone.
void build(Emulator& emu, bool no_sdcard)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.no_sdcard = no_sdcard;
    emu.init(cfg);
    // Leave config mode (NR 0x03 low bits 001..110), so slot 0 is the ROM.
    emu.port().out(0x243B, 0x03);
    emu.port().out(0x253B, 0x01);
}

void nextreg(Emulator& emu, uint8_t reg, uint8_t value)
{
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, value);
}

void poke(Emulator& emu, uint16_t addr, std::initializer_list<uint8_t> bytes)
{
    for (uint8_t b : bytes) emu.mmu().write(addr++, b);
}

// Start the program at $C000 with the stack in RAM and interrupts off.
void start_at_c000(Emulator& emu)
{
    auto regs = emu.cpu().get_registers();
    regs.PC = 0xC000;
    regs.SP = 0xBFF0;
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
}

// Run whole frames: a frame interrupt reaches an IM 1 / IM 2 program.
void frames(Emulator& emu, int n)
{
    for (int i = 0; i < n; ++i) emu.run_frame();
}

const char* const kTail = "; --no-sdcard has no ROM";

}  // namespace

// ══════════════════════════════════════════════════════════════════════════
// ROM — the first fetch from a ROM is reported, once
// ══════════════════════════════════════════════════════════════════════════

static void group_rom()
{
    set_group("ROM");

    // DI; IM 1; EI; loop: HALT; JR loop - the frame interrupt sends it to $0038.
    const std::initializer_list<uint8_t> im1 = {0xF3, 0xED, 0x56, 0xFB, 0x76, 0x18, 0xFD};

    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, im1);
        start_at_c000(emu);
        frames(emu, 3);
        const std::string first = emu.take_rom_fetch_report();
        frames(emu, 3);
        const std::string again = emu.take_rom_fetch_report();
        check("NOSD-ROM-01",
              "IM 1 program: the frame interrupt's fetch at $0038 is reported once, as IM 1",
              first == std::string("executed ROM code at $0038 (IM 1)") + kTail && again.empty(),
              "first='" + first + "' again='" + again + "'");
    }

    // Control: the same program and machine without the mode reports nothing.
    {
        Emulator emu;
        build(emu, false);
        poke(emu, 0xC000, im1);
        start_at_c000(emu);
        frames(emu, 3);
        const std::string r = emu.take_rom_fetch_report();
        check("NOSD-ROM-02",
              "without --no-sdcard the same IM 1 program reports nothing (the watch is that mode's)",
              r.empty(), "report='" + r + "'");
    }

    // DI; LD A,'A'; RST $10 - the 48K ROM's print restart.
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0xF3, 0x3E, 0x41, 0xD7, 0x18, 0xFE});
        start_at_c000(emu);
        frames(emu, 1);
        const std::string r = emu.take_rom_fetch_report();
        check("NOSD-ROM-03", "RST $10 is reported at $0010 as RST $10",
              r == std::string("executed ROM code at $0010 (RST $10)") + kTail, "report='" + r + "'");
    }

    // DI; RST $08; DEFB $01 - a ROM error report, which the esxDOS stand-in never answers.
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0xF3, 0xCF, 0x01, 0x18, 0xFE});
        start_at_c000(emu);
        frames(emu, 1);
        const std::string r = emu.take_rom_fetch_report();
        check("NOSD-ROM-04",
              "RST $08 the stand-in does not answer is reported at $0008, naming both causes",
              r == std::string("executed ROM code at $0008 (RST $08: a ROM error report, or an esxDOS "
                               "call the stand-in does not answer)") + kTail,
              "report='" + r + "'");
    }

    // DI; CALL $1234 - any other ROM address.
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0xF3, 0xCD, 0x34, 0x12, 0x18, 0xFE});
        start_at_c000(emu);
        frames(emu, 1);
        const std::string r = emu.take_rom_fetch_report();
        check("NOSD-ROM-05", "CALL $1234 is reported at $1234 as a call or jump into the ROM",
              r == std::string("executed ROM code at $1234 (a call or jump into the ROM)") + kTail,
              "report='" + r + "'");
    }

    // IM 2 with the vector table and handler in RAM: interrupts are taken, nothing is
    // reported. DI; LD A,$C1; LD I,A; IM 2; EI; loop: HALT; JR loop. Table $C100-$C200 =
    // $C3, handler at $C3C3: LD HL,$C400; INC (HL); EI; RET.
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0xF3, 0x3E, 0xC1, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
        for (uint16_t a = 0xC100; a <= 0xC200; ++a) emu.mmu().write(a, 0xC3);
        poke(emu, 0xC3C3, {0x21, 0x00, 0xC4, 0x34, 0xFB, 0xC9});
        emu.mmu().write(0xC400, 0);
        start_at_c000(emu);
        frames(emu, 4);
        const std::string r = emu.take_rom_fetch_report();
        const uint8_t ints = emu.mmu().read(0xC400);
        check("NOSD-ROM-06", "an IM 2 program with its table and handler in RAM takes interrupts and "
                             "reports nothing",
              r.empty() && ints >= 2, fmt("interrupts=%u report='%s'", ints, r.c_str()));
    }

    // RAM mapped over the ROM (NR 0x50 / 0x51) runs there unreported: the watch is about
    // what supplied the opcode, not its address. LD A,$10; NEXTREG $50,A; LD A,$11;
    // NEXTREG $51,A; LD HL,$0000; LD (HL),$18; INC HL; LD (HL),$FE; JP $0000 (JR $ at $0000).
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0xF3, 0x3E, 0x10, 0xED, 0x92, 0x50, 0x3E, 0x11, 0xED, 0x92, 0x51,
                           0x21, 0x00, 0x00, 0x36, 0x18, 0x23, 0x36, 0xFE, 0xC3, 0x00, 0x00});
        start_at_c000(emu);
        frames(emu, 1);
        const std::string r = emu.take_rom_fetch_report();
        const uint16_t pc = emu.cpu().get_registers().PC;
        check("NOSD-ROM-07", "a program running in RAM it mapped at $0000 is not reported",
              r.empty() && pc == 0x0000, fmt("pc=$%04X report='%s'", pc, r.c_str()));
    }

    // DI; RST $38 - the same address as the IM 1 vector, reached by an opcode: the
    // previous slot is not an interrupt acknowledge, so the cause is the RST, not IM 1.
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0xF3, 0xFF, 0x18, 0xFE});
        start_at_c000(emu);
        frames(emu, 1);
        const std::string r = emu.take_rom_fetch_report();
        check("NOSD-ROM-08", "RST $38 by opcode is reported at $0038 as RST $38, not as IM 1",
              r == std::string("executed ROM code at $0038 (RST $38)") + kTail, "report='" + r + "'");
    }

    // DI; NEXTREG $06,$08; NEXTREG $02,$08; JR $ - the program's own Multiface software NMI
    // (NR 0x06 bit 3 opens the gate, NR 0x02 bit 3 strobes it), taken at $0066.
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0xF3, 0xED, 0x91, 0x06, 0x08, 0xED, 0x91, 0x02, 0x08, 0x18, 0xFE});
        start_at_c000(emu);
        frames(emu, 1);
        const std::string r = emu.take_rom_fetch_report();
        check("NOSD-ROM-09", "a program's own software NMI (NR 0x02 bit 3) is reported at $0066 as NMI",
              r == std::string("executed ROM code at $0066 (NMI)") + kTail, "report='" + r + "'");
    }
}

// ══════════════════════════════════════════════════════════════════════════
// DEV — what needs a ROM from the card is off or refused
// ══════════════════════════════════════════════════════════════════════════

static void group_dev()
{
    set_group("DEV");

    // NR 0x83 bits 0 / 1 are the DivMMC and Multiface port enables (zxnext.vhd:2412,
    // :2415). The mode holds both off; the control shows the write opens them.
    {
        Emulator off, on;
        build(off, true);
        build(on, false);
        nextreg(off, 0x83, 0xFF);
        nextreg(on, 0x83, 0xFF);
        check("NOSD-DEV-01",
              "NR 0x83 = $FF leaves DivMMC and Multiface off with --no-sdcard, on without it",
              !off.divmmc().port_io_enable() && !off.multiface().is_enabled() &&
              on.divmmc().port_io_enable() && on.multiface().is_enabled(),
              fmt("no-sdcard: divmmc=%d mf=%d; control: divmmc=%d mf=%d",
                  off.divmmc().port_io_enable(), off.multiface().is_enabled(),
                  on.divmmc().port_io_enable(), on.multiface().is_enabled()));
    }

    // The NMI buttons (F9 Multiface, F10 DivMMC), their NR 0x06 gates open: neither
    // producer latches and the CPU never reaches $0066.
    {
        Emulator emu;
        build(emu, true);
        poke(emu, 0xC000, {0x00, 0x00, 0x18, 0xFC});
        start_at_c000(emu);
        emu.nmi_source().set_mf_enable(true);
        emu.nmi_source().set_divmmc_enable(true);
        emu.on_hotkey_f9_mf_nmi();
        emu.on_hotkey_f10_divmmc_nmi();
        bool reached_0066 = false;
        for (int s = 0; s < 200; ++s) {
            emu.execute_single_instruction();
            const uint16_t pc = emu.cpu().get_registers().PC;
            if (pc >= 0x0066 && pc <= 0x006F) reached_0066 = true;
        }
        check("NOSD-DEV-02", "the F9 and F10 NMI buttons are refused: no producer latches, no NMI is taken",
              !emu.nmi_source().nmi_mf() && !emu.nmi_source().nmi_divmmc() && !reached_0066,
              fmt("mf=%d divmmc=%d reached_0066=%d", emu.nmi_source().nmi_mf(),
                  emu.nmi_source().nmi_divmmc(), reached_0066));
    }

    // An SD card change (File > Insert / Eject, --delayed-sdcard-insert-frames) is
    // refused and nothing is queued; without the mode the same machine accepts it.
    {
        Emulator off, on;
        build(off, true);
        build(on, false);
        const std::string why = off.request_sd_card_change({"/nonexistent/card.img", false});
        const bool queued = off.take_sd_card_change_request().has_value();
        check("NOSD-DEV-03", "an SD card change is refused with --no-sdcard and nothing is queued; "
                             "the control would accept it",
              why == "this session runs with --no-sdcard, so it has no SD card" && !queued &&
              on.sd_card_change_refusal().empty(),
              "why='" + why + "' control='" + on.sd_card_change_refusal() + "'");
    }

    // A hard reset is a cold boot with the load dropped (load_file empty): the boot ROM
    // runs, finds no card and stays in its own code (its "Error initializing SD card!"
    // screen), which is not a ROM fetch to report. The control, the same cold boot
    // without the mode and with no image, installs no boot ROM. Config mode is left
    // alone here: an NR 0x03 write would switch the boot ROM off.
    {
        Emulator off, on;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        cfg.no_sdcard = true;
        off.init(cfg);
        cfg.no_sdcard = false;
        on.init(cfg);
        frames(off, 20);
        frames(on, 1);
        using K = jnext::dbg::MemSpace::Kind;
        const K k_off = off.mmu().last_m1().kind, k_on = on.mmu().last_m1().kind;
        const std::string r = off.take_rom_fetch_report();
        check("NOSD-DEV-04", "a cold boot with --no-sdcard and no program runs the boot ROM, and "
                             "reports no ROM fetch; without the mode no boot ROM is installed",
              k_off == K::BootRom && r.empty() && k_on != K::BootRom,
              fmt("no-sdcard fetch kind=%d report='%s'; control kind=%d",
                  static_cast<int>(k_off), r.c_str(), static_cast<int>(k_on)));
    }
}

int main()
{
    std::printf("--no-sdcard Tests (GH #540)\n");
    std::printf("===============================================\n\n");

    group_rom();
    std::printf("  Group: ROM — done\n");
    group_dev();
    std::printf("  Group: DEV — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty()) std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty()) std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
