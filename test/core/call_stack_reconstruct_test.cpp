// GH #498 - CallStack::reconstruct(): the frames already on the stack are rebuilt from it when
// call tracking starts mid-run.
//
// Oracle: the Z80 encodings (Zilog Z80 CPU User Manual UM0080). CALL nn / CALL cc,nn are
// CD C4 CC D4 DC E4 EC F4 FC, 3 bytes, and push the address after the instruction (caller + 3);
// RST p is 11ppp111 (C7 CF D7 DF E7 EF F7 FF), 1 byte, pushes caller + 1 and jumps to p (0x00,
// 0x08 .. 0x38). The expected frames are written here from those encodings, never read back
// from the implementation. The stack is a 64 KB array behind set_stack_reader(); no emulator.
#include "debug/call_stack.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

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

// A 64 KB machine memory with a logging reader.
struct Mem {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x10000, 0x00);   // 0x00 = NOP: never a CALL / RST
    std::vector<uint16_t> reads;
    void w16(uint16_t a, uint16_t v) {
        b[a] = static_cast<uint8_t>(v & 0xFF);
        b[static_cast<uint16_t>(a + 1)] = static_cast<uint8_t>(v >> 8);
    }
    void set(uint16_t a, std::initializer_list<uint8_t> l) {
        for (uint8_t v : l) b[a++] = v;
    }
    void wire(CallStack& cs) {
        cs.set_stack_reader([this](uint16_t a) {
            reads.push_back(a);
            return b[a];
        });
    }
};

std::string s16(unsigned v) {
    char t[8];
    std::snprintf(t, sizeof t, "%04X", v & 0xFFFF);
    return t;
}

void csr_01() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    m.set(0x8000, {0xCD, 0x00, 0x90});
    m.w16(0xFF00, 0x8003);
    cs.set_enabled(true);
    cs.reconstruct(0xFF00);
    const auto& f = cs.frames();
    const bool ok = f.size() == 1 && f[0].type == CallType::CALL && f[0].caller_pc == 0x8000 &&
                    f[0].target_pc == 0x9000 && f[0].sp_at_call == 0xFF00 && f[0].reconstructed &&
                    f[0].target_space == jnext::dbg::MemSpace::cpu();
    check("CSR-01", "CD 00 90 at 8000, word 8003 at SP=FF00: one rebuilt CALL frame {8000 -> 9000, slot FF00}", ok,
          "n=" + std::to_string(f.size()));
}

void csr_02() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    const uint8_t ops[8] = {0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC};
    for (int i = 0; i < 8; ++i) {
        const uint16_t pc = static_cast<uint16_t>(0x8000 + 0x10 * i);
        m.set(pc, {ops[i], static_cast<uint8_t>(i), static_cast<uint8_t>(0x90 + i)});
        m.w16(static_cast<uint16_t>(0xFF00 + 2 * i), static_cast<uint16_t>(pc + 3));
    }
    cs.reconstruct(0xFF00);
    const auto& f = cs.frames();
    bool ok = f.size() == 8;
    std::string d = "n=" + std::to_string(f.size());
    for (int i = 0; ok && i < 8; ++i) {
        // oldest first: the highest slot (i = 7) is frames()[0]
        const auto& fr = f[static_cast<size_t>(7 - i)];
        const uint16_t pc = static_cast<uint16_t>(0x8000 + 0x10 * i);
        const uint16_t tgt = static_cast<uint16_t>(i | ((0x90 + i) << 8));
        if (fr.type != CallType::CALL || fr.caller_pc != pc || fr.target_pc != tgt) {
            ok = false;
            d += " op " + s16(ops[i]) + " got caller=" + s16(fr.caller_pc) + " target=" + s16(fr.target_pc);
        }
    }
    check("CSR-02", "the eight conditional CALLs C4 CC D4 DC E4 EC F4 FC each rebuild a CALL frame with its target", ok, d);
}

void csr_03() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    const uint8_t ops[8] = {0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF};
    for (int i = 0; i < 8; ++i) {
        const uint16_t pc = static_cast<uint16_t>(0x8000 + 0x10 * i);
        m.set(pc, {ops[i]});
        m.w16(static_cast<uint16_t>(0xFF00 + 2 * i), static_cast<uint16_t>(pc + 1));
    }
    cs.reconstruct(0xFF00);
    const auto& f = cs.frames();
    bool ok = f.size() == 8;
    std::string d = "n=" + std::to_string(f.size());
    for (int i = 0; ok && i < 8; ++i) {
        const auto& fr = f[static_cast<size_t>(7 - i)];
        const uint16_t pc = static_cast<uint16_t>(0x8000 + 0x10 * i);
        if (fr.type != CallType::RST || fr.caller_pc != pc || fr.target_pc != 8 * i) {
            ok = false;
            d += " op " + s16(ops[i]) + " got caller=" + s16(fr.caller_pc) + " target=" + s16(fr.target_pc);
        }
    }
    check("CSR-03", "the eight RSTs C7..FF rebuild RST frames: caller = return - 1, targets 00 08 .. 38", ok, d);
}

void csr_04() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    m.set(0x8000, {0xCD, 0x00, 0x90});   // A: returns to 8003
    m.set(0x8100, {0xCD, 0x00, 0xA0});   // B: returns to 8103
    const uint16_t sp = 0xFF00;
    m.w16(sp, 0x1234);
    m.w16(sp + 2, 0x8003);
    m.w16(sp + 4, 0x5678);
    m.w16(sp + 6, 0x8103);
    cs.reconstruct(sp);
    const auto& f = cs.frames();
    const bool ok = f.size() == 2 && f[0].caller_pc == 0x8100 && f[0].sp_at_call == sp + 6 &&
                    f[1].caller_pc == 0x8000 && f[1].sp_at_call == sp + 2;
    check("CSR-04", "stack [data, A, data, B] from SP: two frames, oldest first (B at slot SP+6, then A at SP+2)", ok,
          "n=" + std::to_string(f.size()));
}

void csr_05() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    m.set(0x8000, {0xCD, 0x00, 0xFF});   // the high operand byte FF is also the RST 38 opcode
    m.w16(0xFF00, 0x8003);
    cs.reconstruct(0xFF00);
    const auto& f = cs.frames();
    const bool ok = f.size() == 1 && f[0].type == CallType::CALL && f[0].caller_pc == 0x8000 &&
                    f[0].target_pc == 0xFF00;
    check("CSR-05", "CD 00 FF before the return address: one CALL frame to FF00, not an RST 38 frame", ok,
          "n=" + std::to_string(f.size()) + (f.empty() ? "" : " type=" + std::to_string(static_cast<int>(f[0].type))));
}

void csr_06() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    m.set(0x8000, {0xCD, 0x00, 0x90});
    m.w16(0xFFFE, 0x8003);
    cs.reconstruct(0xFFF0);
    const bool top = cs.frames().size() == 1 && cs.frames()[0].sp_at_call == 0xFFFE;
    // SP = FFFF: the word at FFFF would wrap to 0000; it is not a word, nothing is read.
    m.reads.clear();
    m.b[0xFFFF] = 0x03;
    m.b[0x0000] = 0x80;
    cs.reconstruct(0xFFFF);
    bool read0 = false;
    for (uint16_t a : m.reads) read0 = read0 || a == 0x0000;
    check("CSR-06", "a return address in the word at FFFE is found; SP=FFFF yields no frame and reads no byte at 0000",
          top && cs.frames().empty() && !read0,
          "top=" + std::to_string(top) + " n=" + std::to_string(cs.frames().size()) + " read0=" + std::to_string(read0));
}

void csr_07() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    m.set(0x8000, {0xCD, 0x00, 0x90});
    const uint16_t sp = 0xC000;
    m.w16(sp + 2 * 127, 0x8003);          // word 127 (the 128th word): inside the bound
    cs.reconstruct(sp);
    const bool in127 = cs.frames().size() == 1 && cs.frames()[0].sp_at_call == sp + 254;
    m.w16(sp + 2 * 127, 0x0000);
    m.w16(sp + 2 * 128, 0x8003);          // word 128: outside
    cs.reconstruct(sp);
    check("CSR-07", "a return address in word 127 from SP is found, one in word 128 is not (the 128-word scan bound)",
          in127 && cs.frames().empty(), "in127=" + std::to_string(in127) + " n=" + std::to_string(cs.frames().size()));
}

void csr_08() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    m.set(0x8000, {0xCD, 0x00, 0x90});
    m.w16(0xFF00, 0x8003);
    cs.reconstruct(0xFF00);
    // The routine at 9000 now runs a tracked CALL A000 (SP FF00 -> FEFE).
    cs.on_instruction_pre(0x9000, 0xFF00, 0xCD, 0x00, 0xA0);
    cs.on_instruction_post(0xFEFE, 0xA000);
    const auto& f = cs.frames();
    const bool ok = f.size() == 2 && f[0].reconstructed && f[0].caller_pc == 0x8000 && !f[1].reconstructed &&
                    f[1].caller_pc == 0x9000 && f[1].target_pc == 0xA000 && f[1].sp_at_call == 0xFEFE;
    check("CSR-08", "a tracked CALL pushed over a rebuilt frame is not marked rebuilt; the rebuilt one keeps its mark", ok,
          "n=" + std::to_string(f.size()));
}

void csr_09() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    m.set(0x8000, {0xCD, 0x00, 0x90});
    m.w16(0xFF00, 0x8003);
    cs.reconstruct(0xFF00);
    // RET (C9) from 9010: SP FF00 -> FF02, lands on caller + 3.
    cs.on_instruction_pre(0x9010, 0xFF00, 0xC9, 0, 0);
    cs.on_instruction_post(0xFF02, 0x8003);
    const bool by_point = cs.frames().empty();
    // A second rebuild; a RET to somewhere else that raises SP past the slot also ends it.
    cs.reconstruct(0xFF00);
    const bool again = cs.frames().size() == 1;
    cs.on_instruction_pre(0x9010, 0xFF00, 0xC9, 0, 0);
    cs.on_instruction_post(0xFF02, 0x7777);
    check("CSR-09", "a RET to a rebuilt frame's return point pops it; so does an SP rise past its slot",
          by_point && again && cs.frames().empty(),
          "by_point=" + std::to_string(by_point) + " again=" + std::to_string(again) + " n=" + std::to_string(cs.frames().size()));
}

void csr_10() {
    Mem m;
    CallStack cs;
    m.wire(cs);
    cs.set_enabled(true);
    m.set(0x8000, {0xCD, 0x00, 0x90});
    m.w16(0xFF00, 0x8003);
    cs.reconstruct(0xFF00);
    const std::vector<CallFrame> saved = cs.frames();
    cs.set_enabled(false);
    const bool dropped = cs.frames().empty();
    cs.set_enabled(true);
    cs.restore_frames(saved);
    const bool kept = cs.frames().size() == 1 && cs.frames()[0].reconstructed;
    check("CSR-10", "set_enabled(false) drops rebuilt frames; restore_frames() keeps the rebuilt flag", dropped && kept,
          "dropped=" + std::to_string(dropped) + " kept=" + std::to_string(kept));
}

void csr_11() {
    CallStack cs;                          // no stack reader
    cs.set_enabled(true);
    cs.reconstruct(0xFF00);
    check("CSR-11", "no stack reader: reconstruct() yields no frames and does not crash", cs.frames().empty(),
          "n=" + std::to_string(cs.frames().size()));
}

}  // namespace

int main() {
    csr_01();
    csr_02();
    csr_03();
    csr_04();
    csr_05();
    csr_06();
    csr_07();
    csr_08();
    csr_09();
    csr_10();
    csr_11();
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
