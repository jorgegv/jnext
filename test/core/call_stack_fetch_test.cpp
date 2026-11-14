// GH #489 - the call-stack tracker is fed from the CPU's own M1 fetch buffer after execute(),
// not from a per-instruction prediction: frames through the emulator's real path, and the
// prediction counter (the fetch space is predicted only for a slot that may take an INT or NMI).
//
// Oracle for every frame value: Z80 instruction semantics (Zilog Z80 CPU User Manual: CALL, RST,
// RET, RETI, RETN, PUSH, EX (SP),HL, JP (HL)), the Z80N PUSH nn (ED 8A hi lo), and the
// interrupt response (IM 1 -> 0x0038, NMI -> 0x0066). The frame fields are written here as the
// instruction semantics give them, never read back from the implementation.
//
// No ROM, no SD image: a bare Next with a program written straight into RAM.
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Bytes = std::vector<uint8_t>;

constexpr uint16_t kMain = 0xC000, kSub = 0xC020, kSp = 0xBFF0;

struct Rig {
    std::unique_ptr<Emulator> emu;          // declared first: dies last
    std::unique_ptr<Debugger> dbg;          // declared last: dies first
    ClientId client = jnext::dbg::CLIENT_NONE;

    Rig() {
        emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        ok = emu->init(cfg);
        // What the debugger window's attach switches on (DebuggerManager::attach_backend()).
        dbg = std::make_unique<Debugger>(*emu);
        client = dbg->attach(jnext::dbg::ClientInfo{"Qt GUI", jnext::dbg::ClientKind::Gui}).value;
        dbg->set_live_raster(client, true);
        dbg->set_call_stack_enabled(client, true);
    }
    bool ok = false;

    void nr(uint8_t reg, uint8_t val) {
        emu->port().out(0x243B, reg);
        emu->port().out(0x253B, val);
    }
    void poke(uint16_t addr, const Bytes& b) {
        for (size_t i = 0; i < b.size(); ++i) emu->mmu().write(static_cast<uint16_t>(addr + i), b[i]);
    }
    void start(uint16_t pc, uint16_t sp = kSp) {
        Z80Registers r = emu->cpu().get_registers();
        r.PC = pc;
        r.SP = sp;
        r.IFF1 = r.IFF2 = 0;
        emu->cpu().set_registers(r);
    }
    void regs(uint16_t ix, uint16_t iy) {
        Z80Registers r = emu->cpu().get_registers();
        r.IX = ix;
        r.IY = iy;
        emu->cpu().set_registers(r);
    }
    void step(int n = 1) {
        for (int i = 0; i < n; ++i) emu->execute_single_instruction();
    }
    const std::vector<CallFrame>& frames() const { return emu->call_stack().frames(); }
};

std::string describe(const std::vector<CallFrame>& f) {
    char b[96];
    std::string s = "frames=" + std::to_string(f.size());
    for (const CallFrame& x : f) {
        std::snprintf(b, sizeof b, " [t%d %04X->%04X sp=%04X st=%d@%04X]", int(x.type), x.caller_pc,
                      x.target_pc, x.sp_at_call, int(x.ret_stored), x.ret_stored_at);
        s += b;
    }
    return s;
}

// A routine at kSub entered with `PUSH BC; CALL kSub` (caller_pc = kMain + 1, return = kMain + 4,
// SP slot = kSp - 4, the pushed argument above it): the shape of a __z88dk_callee prologue.
constexpr uint16_t kCallerPc = kMain + 1, kRet = kMain + 4, kSlot = kSp - 4, kArgAt = kSp - 2;

void enter_callee(Rig& r, const Bytes& body, uint16_t ix = kRet, uint16_t iy = kRet) {
    r.poke(kMain, {0xC5, 0xCD, kSub & 0xFF, kSub >> 8});     // PUSH BC; CALL kSub
    r.poke(kSub, body);
    r.regs(ix, iy);
    r.start(kMain);
    r.step(2);   // PUSH BC, CALL kSub
}

void csf_01_02() {
    // The main loop: DI/EI, then a busy CALL/RET with a counter. 10 frames.
    // CSF-01: interrupts disabled (the INT pulse is still pending for its window, but IFF1 is clear): no slot predicts.
    {
        Rig r;
        r.poke(kSub, {0x2A, 0x00, 0xC2, 0x23, 0x22, 0x00, 0xC2, 0xC9});   // LD HL,(C200); INC HL; LD (C200),HL; RET
        r.poke(kMain, {0xF3, 0xCD, kSub & 0xFF, kSub >> 8, 0x18, 0xFB});   // DI; loop: CALL sub; JR loop
        r.start(kMain);
        for (int f = 0; f < 10; ++f) r.emu->run_frame();
        const unsigned iters = r.emu->mmu().peek(0xC200) | (r.emu->mmu().peek(0xC201) << 8);
        check("CSF-01", "interrupts off, a busy CALL/RET loop for 10 frames: the tracker predicts no slot's fetch space",
              r.ok && iters > 1000 && r.emu->call_stack_predictions_for_test() == 0,
              "iters=" + std::to_string(iters) + " predictions=" +
                  std::to_string(r.emu->call_stack_predictions_for_test()));
    }
    // CSF-02: IM 2 and EI (IM 1's vector is in ROM, which a bare Next does not have): the INT
    // pulse of each frame is pending for at most 32 T-states, so at most 8 slots of >= 4 T
    // (+1) per frame predict; the ISR spins (interrupts off), so its INT frame is still open.
    {
        Rig r;
        for (int i = 0; i < 257; ++i) r.emu->mmu().write(static_cast<uint16_t>(0xFD00 + i), 0xC1);
        r.poke(0xC1C1, {0x18, 0xFE});                                       // ISR: JR $
        r.poke(kSub, {0x2A, 0x00, 0xC2, 0x23, 0x22, 0x00, 0xC2, 0xC9});
        r.poke(kMain, {0x3E, 0xFD, 0xED, 0x47, 0xED, 0x5E, 0xFB,            // LD A,FD; LD I,A; IM 2; EI
                       0xCD, kSub & 0xFF, kSub >> 8, 0x18, 0xFB});          // loop: CALL sub; JR loop
        r.start(kMain);
        for (int f = 0; f < 10; ++f) r.emu->run_frame();
        const uint64_t p = r.emu->call_stack_predictions_for_test();
        bool int_frame = false;
        for (const CallFrame& f : r.frames()) int_frame = int_frame || f.type == CallType::INT;
        check("CSF-02", "IM 2 and EI for 10 frames: an INT is taken and recorded as a frame, and only the slots with an INT pending predict (1 to 90)",
              r.ok && int_frame && p >= 1 && p <= 90,
              "predictions=" + std::to_string(p) + " " + describe(r.frames()));
    }
}

void csf_03_06() {
    {   // CSF-03: CALL nn at A with SP = S: one CALL frame {A, nn, S - 2}.
        Rig r;
        r.poke(kMain, {0xCD, 0x00, 0xC1});
        r.start(kMain);
        r.step();
        const auto& f = r.frames();
        check("CSF-03", "CALL nn: one frame, caller = its PC, target = the operand, SP after the push",
              f.size() == 1 && f[0].type == CallType::CALL && f[0].caller_pc == kMain &&
                  f[0].target_pc == 0xC100 && f[0].sp_at_call == kSp - 2,
              describe(f));
    }
    {   // CSF-04: CALL NZ,nn not taken (Z set) opens no frame; taken (Z clear) opens one.
        Rig r;
        r.poke(kMain, {0xAF, 0xC4, 0x00, 0xC1, 0xF6, 0x01, 0xC4, 0x00, 0xC1});   // XOR A; CALL NZ; OR 1; CALL NZ
        r.start(kMain);
        r.step(2);
        const bool none = r.frames().empty();
        r.step(2);
        const auto& f = r.frames();
        check("CSF-04", "CALL NZ,nn: no frame while not taken (Z set), one frame to the operand once taken",
              none && f.size() == 1 && f[0].type == CallType::CALL && f[0].caller_pc == kMain + 6 &&
                  f[0].target_pc == 0xC100,
              describe(f));
    }
    {   // CSF-05: the eight RSTs open an RST frame to 00, 08, ..., 38.
        bool ok = true;
        std::string bad;
        for (unsigned i = 0; i < 8; ++i) {
            Rig r;
            r.poke(kMain, {static_cast<uint8_t>(0xC7 + 8 * i)});
            r.start(kMain);
            r.step();
            const auto& f = r.frames();
            const bool good = f.size() == 1 && f[0].type == CallType::RST && f[0].caller_pc == kMain &&
                              f[0].target_pc == 8 * i && f[0].sp_at_call == kSp - 2;
            if (!good) { ok = false; bad += " rst" + std::to_string(i) + ":" + describe(f); }
        }
        check("CSF-05", "RST 00..38 (C7..FF): one RST frame each, target = 8 * n", ok, bad);
    }
    {   // CSF-06: RET, RET Z not taken / taken, RETI, RETN after a CALL.
        struct Case { const char* name; Bytes sub; int steps; size_t frames_after; };
        const Case cases[] = {
            {"RET", {0xC9}, 1, 0},
            {"RET Z not taken", {0xF6, 0x01, 0xC8}, 2, 1},          // OR 1; RET Z (Z clear)
            {"RET Z taken", {0xAF, 0xC8}, 2, 0},                    // XOR A; RET Z (Z set)
            {"RETI", {0xED, 0x4D}, 1, 0},
            {"RETN", {0xED, 0x45}, 1, 0},
        };
        bool ok = true;
        std::string bad;
        for (const Case& c : cases) {
            Rig r;
            r.poke(kMain, {0xCD, kSub & 0xFF, kSub >> 8});
            r.poke(kSub, c.sub);
            r.start(kMain);
            r.step();   // CALL
            r.step(c.steps);   // the instructions of the routine
            if (r.frames().size() != c.frames_after) { ok = false; bad += std::string(" ") + c.name + ":" + describe(r.frames()); }
        }
        check("CSF-06", "RET pops the frame; RET Z only when taken; RETI and RETN (ED 4D / ED 45) pop it", ok, bad);
    }
}

void csf_07() {
    // Inside a CALLed routine that moves its return address up and stores it back (PUSH r; GH #349 R1''):
    // PUSH BC; CALL sub; sub: POP HL; POP BC; <the store>. The store lands at SP = kArgAt, above the slot.
    struct Case { const char* id; const char* what; Bytes store; };
    auto run = [](const Case& c) {
        Rig r;
        Bytes body{0xE1, 0xC1};   // POP HL (the return address); POP BC (the argument)
        body.insert(body.end(), c.store.begin(), c.store.end());
        enter_callee(r, body);
        r.step(3);   // POP HL, POP BC, the store
        const auto& f = r.frames();
        const bool ok = f.size() == 1 && f[0].type == CallType::CALL && f[0].caller_pc == kCallerPc &&
                        f[0].sp_at_call == kSlot && f[0].ret_stored && f[0].ret_stored_at == kArgAt;
        return std::make_pair(ok, describe(f));
    };
    const Case a{"CSF-07a", "PUSH HL", {0xE5}};
    const Case b{"CSF-07b", "PUSH IX (DD E5)", {0xDD, 0xE5}};
    const Case c{"CSF-07c", "PUSH IY (FD E5)", {0xFD, 0xE5}};
    const Case d{"CSF-07d", "Z80N PUSH nn (ED 8A)", {0xED, 0x8A, kRet >> 8, kRet & 0xFF}};
    auto ra = run(a), rb = run(b), rc = run(c), rd = run(d);
    check("CSF-07a", "a callee storing its return address back with PUSH HL (E5) is noted at the store's SP", ra.first, ra.second);
    check("CSF-07b", "a callee storing its return address back with PUSH IX (DD E5) is noted at the store's SP", rb.first, rb.second);
    check("CSF-07c", "a callee storing its return address back with PUSH IY (FD E5) is noted at the store's SP", rc.first, rc.second);
    check("CSF-07d", "a callee storing its return address back with Z80N PUSH nn (ED 8A) is noted at the store's SP", rd.first, rd.second);
}

void csf_08() {
    bool ok = true;
    std::string bad;
    {   // EX (SP),HL: POP HL (the return address), then swap it with the argument: stored at the argument's slot.
        Rig r;
        enter_callee(r, {0xE1, 0xE3});
        r.step(2);
        const auto& f = r.frames();
        if (!(f.size() == 1 && f[0].ret_stored && f[0].ret_stored_at == kArgAt)) { ok = false; bad += " EX(SP),HL:" + describe(f); }
    }
    {   // EX (SP),IX (DD E3) with IX = the return address.
        Rig r;
        enter_callee(r, {0xE1, 0xDD, 0xE3});
        r.step(2);
        const auto& f = r.frames();
        if (!(f.size() == 1 && f[0].ret_stored && f[0].ret_stored_at == kArgAt)) { ok = false; bad += " EX(SP),IX:" + describe(f); }
    }
    {   // POP HL; JP (HL): a jump to the return point ends the frame (GH #349 rule A).
        Rig r;
        enter_callee(r, {0xE1, 0xE9});
        r.step(2);
        if (!r.frames().empty()) { ok = false; bad += " JP(HL):" + describe(r.frames()); }
    }
    {   // JP (IX) (DD E9) with IX = the return address.
        Rig r;
        enter_callee(r, {0xE1, 0xDD, 0xE9});
        r.step(2);
        if (!r.frames().empty()) { ok = false; bad += " JP(IX):" + describe(r.frames()); }
    }
    check("CSF-08", "EX (SP),HL and EX (SP),IX storing the return address are noted; JP (HL) and JP (IX) to it end the frame", ok, bad);
}

void csf_09() {
    bool ok = true;
    std::string bad;
    {   // INT (IM 1) taken while executing in slot 4, page 20: the frame's caller space is that page.
        Rig r;
        r.nr(0x54, 20);
        r.poke(0x8000, {0x00, 0x00});
        r.start(0x8000);
        Z80Registers g = r.emu->cpu().get_registers();
        g.IFF1 = g.IFF2 = 1;
        g.IM = 1;
        r.emu->cpu().set_registers(g);
        r.emu->cpu().request_interrupt(0xFF);
        r.step();
        const auto& f = r.frames();
        if (!(f.size() == 1 && f[0].type == CallType::INT && f[0].caller_pc == 0x8000 &&
              f[0].target_pc == 0x0038 && f[0].caller_space == MemSpace::page(20)))
            { ok = false; bad += " INT:" + describe(f) + " space=" + std::to_string(f.empty() ? -1 : int(f[0].caller_space.index)); }
    }
    {   // NMI taken while executing in slot 6, page 30.
        Rig r;
        r.nr(0x56, 30);
        r.poke(0xC000, {0x00, 0x00});
        r.start(0xC000);
        r.emu->cpu().request_nmi();
        r.step();
        const auto& f = r.frames();
        if (!(f.size() == 1 && f[0].type == CallType::NMI && f[0].caller_pc == 0xC000 &&
              f[0].target_pc == 0x0066 && f[0].caller_space == MemSpace::page(30)))
            { ok = false; bad += " NMI:" + describe(f) + " space=" + std::to_string(f.empty() ? -1 : int(f[0].caller_space.index)); }
    }
    check("CSF-09", "an INT and an NMI taken from RAM pages 20 and 30: each frame's caller space is the interrupted PC's page", ok, bad);
}

void csf_10() {
    // The shape of SRCDBG-ID-20: RST $08 maps the DivMMC ROM in (instant entry), a NOP settles the
    // overlay, then a CALL fetched from the DivMMC ROM: its frame's caller space is the DivMMC ROM.
    Rig r;
    r.nr(0x50, 10);
    r.nr(0x0A, 0x10);
    r.nr(0xB8, 0x02);
    r.nr(0xB9, 0x02);
    r.nr(0xBA, 0x02);
    uint8_t* drom = r.emu->divmmc().rom_data();
    drom[0x0008] = 0x00;                                             // NOP
    drom[0x0009] = 0xCD; drom[0x000A] = 0x20; drom[0x000B] = 0x00;   // CALL $0020
    drom[0x0020] = 0xC9;                                             // RET
    r.poke(0x8000, {0xCF, 0x00});                                    // RST $08
    r.start(0x8000, 0xFF00);
    r.step(3);
    const auto& f = r.frames();
    check("CSF-10", "a CALL fetched from the DivMMC ROM after an RST $08 entry: its frame's caller space is the DivMMC ROM, its target the operand",
          f.size() == 2 && f[1].type == CallType::CALL && f[1].caller_pc == 0x0009 &&
              f[1].target_pc == 0x0020 && f[1].caller_space == MemSpace::divmmc_rom(),
          describe(f) + " space=" + std::to_string(f.size() > 1 ? int(f[1].caller_space.kind) : -1));
}

void csf_11() {
    // Z80Cpu::last_m1_byte(): the bytes the last execute() fetched, 0xFF past them. A NOP fetches its
    // opcode alone; an accepted INT fetches no opcode (t80na.vhd:278-279), so nothing is in the buffer.
    Rig r;
    r.poke(0xC000, {0x00, 0x00});
    r.start(0xC000);
    r.step();
    const uint8_t nop0 = r.emu->cpu().last_m1_byte(0), nop1 = r.emu->cpu().last_m1_byte(1);
    Z80Registers g = r.emu->cpu().get_registers();
    g.IFF1 = g.IFF2 = 1;
    g.IM = 1;
    r.emu->cpu().set_registers(g);
    r.emu->cpu().request_interrupt(0xFF);
    r.step();
    const uint8_t int0 = r.emu->cpu().last_m1_byte(0);
    check("CSF-11", "last_m1_byte: the opcode of a NOP at index 0 and 0xFF at index 1; 0xFF at index 0 after an accepted INT",
          nop0 == 0x00 && nop1 == 0xFF && int0 == 0xFF,
          "nop0=" + std::to_string(nop0) + " nop1=" + std::to_string(nop1) + " int0=" + std::to_string(int0));
}

}  // namespace

int main() {
    csf_01_02();
    csf_03_06();
    csf_07();
    csf_08();
    csf_09();
    csf_10();
    csf_11();
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
