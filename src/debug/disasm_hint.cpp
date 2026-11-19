#include "debug/disasm_hint.h"

#include <cstdio>

namespace disasm_hint {

namespace {

// disasm.cpp's kMaxPrefixChain: past this many redundant DD/FD prefixes the disassembler shows a
// `DB $DD` line of its own, which is not an instruction with an operand.
constexpr int kMaxPrefixChain = 64;

bool cond(int cc, uint8_t f) {
    switch (cc & 7) {
    case 0: return !(f & 0x40);   // NZ
    case 1: return  (f & 0x40);   // Z
    case 2: return !(f & 0x01);   // NC
    case 3: return  (f & 0x01);   // C
    case 4: return !(f & 0x04);   // PO
    case 5: return  (f & 0x04);   // PE
    case 6: return !(f & 0x80);   // P
    default: return (f & 0x80);   // M
    }
}

uint16_t u16(const DisasmReadFn& rd, uint16_t a) {
    return static_cast<uint16_t>(rd(a) | (rd(static_cast<uint16_t>(a + 1)) << 8));
}

uint16_t rel(const DisasmReadFn& code, uint16_t at, uint16_t base) {
    return static_cast<uint16_t>(base + static_cast<int8_t>(code(at)));
}

Hint memory(const DisasmReadFn& data, uint16_t addr, int width) {
    Hint h;
    h.kind  = Hint::Kind::Memory;
    h.addr  = addr;
    h.width = width;
    h.value = width == 2 ? u16(data, addr) : data(addr);
    return h;
}

// What `op` (at `a`, the byte after any prefixes) is as an UNPREFIXED instruction, or with
// `idx` = IX/IY the value of the index register when a prefix applies to it (HL-using forms).
// `indexed` is true when the prefix is consumed by the opcode (it names (IX+d) or IX/IY itself).
Hint decode(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data,
            uint16_t a, bool prefixed, uint16_t idx, bool* consumed) {
    const uint8_t op = code(a);
    const int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    const uint8_t f = static_cast<uint8_t>(r.AF & 0xFF);
    *consumed = false;

    if (prefixed) {
        // The opcodes the prefix changes: the ones that name (IX+d), IX/IY, or (SP) with IX/IY.
        const bool disp = (x == 0 && (z == 4 || z == 5 || z == 6) && y == 6)           // INC/DEC/LD (IX+d)
                       || (x == 1 && (y == 6 || z == 6) && op != 0x76)                // LD r,(IX+d) / LD (IX+d),r
                       || (x == 2 && z == 6);                                         // ALU (IX+d)
        if (op == 0xCB) {
            *consumed = true;
            // DD CB d op: the displacement is at +1, the sub-opcode at +2, for every sub-opcode
            return memory(data, rel(code, static_cast<uint16_t>(a + 1), idx), 1);
        }
        if (disp) {
            *consumed = true;
            return memory(data, rel(code, static_cast<uint16_t>(a + 1), idx), 1);
        }
        if (op == 0x22 || op == 0x2A) {   // LD (nn),IX / LD IX,(nn)
            *consumed = true;
            return memory(data, u16(code, static_cast<uint16_t>(a + 1)), 2);
        }
        if (op == 0xE3) {                 // EX (SP),IX
            *consumed = true;
            return memory(data, r.SP, 2);
        }
        if (op == 0xE9 || op == 0x76) { *consumed = true; return Hint{}; }   // JP (IX); HALT ignores it
        // everything else: the prefix is ignored, decode as unprefixed (below)
    }

    if (op == 0xCB) {
        const uint8_t sub = code(static_cast<uint16_t>(a + 1));
        if ((sub & 7) == 6) return memory(data, r.HL, 1);
        return Hint{};
    }
    if (op == 0xED) {
        const uint8_t e = code(static_cast<uint16_t>(a + 1));
        if ((e >> 6) == 1 && (e & 7) == 3)   // LD (nn),rr / LD rr,(nn)
            return memory(data, u16(code, static_cast<uint16_t>(a + 2)), 2);
        return Hint{};
    }

    Hint h;
    if (x == 0 && z == 0 && y == 2) {          // DJNZ d
        h.kind = Hint::Kind::Branch;
        h.taken = ((((r.BC >> 8) - 1) & 0xFF) != 0);
        h.target = rel(code, static_cast<uint16_t>(a + 1), static_cast<uint16_t>(a + 2));
        return h;
    }
    if (x == 0 && z == 0 && y >= 4) {          // JR cc,d (NZ, Z, NC, C)
        h.kind = Hint::Kind::Branch;
        h.taken = cond(y - 4, f);
        h.target = rel(code, static_cast<uint16_t>(a + 1), static_cast<uint16_t>(a + 2));
        return h;
    }
    if (x == 3 && (z == 2 || z == 4)) {        // JP cc,nn / CALL cc,nn
        h.kind = Hint::Kind::Branch;
        h.taken = cond(y, f);
        h.target = u16(code, static_cast<uint16_t>(a + 1));
        return h;
    }
    if (x == 3 && z == 0) {                    // RET cc
        h.kind = Hint::Kind::Branch;
        h.taken = cond(y, f);
        h.from_stack = true;
        h.target = u16(data, r.SP);
        return h;
    }

    switch (op) {
    case 0x02: case 0x0A: return memory(data, r.BC, 1);
    case 0x12: case 0x1A: return memory(data, r.DE, 1);
    case 0x22: case 0x2A: return memory(data, u16(code, static_cast<uint16_t>(a + 1)), 2);
    case 0x32: case 0x3A: return memory(data, u16(code, static_cast<uint16_t>(a + 1)), 1);
    case 0x34: case 0x35: case 0x36: return memory(data, r.HL, 1);
    case 0xE3: return memory(data, r.SP, 2);
    default: break;
    }
    if ((x == 1 && (y == 6 || z == 6) && op != 0x76) || (x == 2 && z == 6))
        return memory(data, r.HL, 1);
    return Hint{};
}

} // namespace

namespace {

// The DD/FD chain at `at`: where the opcode is (`a`), how many prefixes precede it, and which index
// register the last one names. false: past kMaxPrefixChain disasm_one() shows `DB $DD`, which is no
// instruction.
struct Walk { uint16_t a; int prefixes; uint16_t idx; };
bool walk_prefixes(const Z80Registers& r, uint16_t at, const DisasmReadFn& code, Walk* w) {
    uint16_t a = at;
    int prefixes = 0;
    uint8_t last = 0;
    while (prefixes <= kMaxPrefixChain + 2) {
        const uint8_t b = code(a);
        if (b != 0xDD && b != 0xFD) break;
        last = b;
        ++prefixes;
        a = static_cast<uint16_t>(a + 1);
    }
    if (prefixes > kMaxPrefixChain + 1) return false;
    w->a = a;
    w->prefixes = prefixes;
    w->idx = last == 0xDD ? r.IX : r.IY;
    return true;
}

} // namespace

Hint pc_hint(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data) {
    // The DD/FD chain: the last prefix picks IX or IY.
    Walk w;
    if (!walk_prefixes(r, r.PC, code, &w)) return Hint{};

    bool consumed = false;
    Hint h = decode(r, code, data, w.a, w.prefixes > 0, w.idx, &consumed);
    // disasm_ddfd() recurses once per prefix an opcode does not consume; past kMaxPrefixChain it
    // shows a `DB $DD` line of its own instead. The prefixes before the last never consume, which
    // is what the `kMaxPrefixChain + 1` limit above enforces; the last one only if it consumes.
    if (w.prefixes > 0 && !consumed && w.prefixes - 1 >= kMaxPrefixChain) return Hint{};
    return h;
}

Follow follow_target(const Z80Registers& r, uint16_t at, const DisasmReadFn& code, const DisasmReadFn& data) {
    Follow f;
    Walk w;
    if (!walk_prefixes(r, at, code, &w)) return f;

    bool consumed = false;
    const Hint h = decode(r, code, data, w.a, w.prefixes > 0, w.idx, &consumed);
    if (w.prefixes > 0 && !consumed && w.prefixes - 1 >= kMaxPrefixChain) return f;   // `DB $DD`

    auto code_at = [&f](uint16_t a) { f.kind = Follow::Kind::Code; f.addr = a; return f; };
    // A conditional branch is followed whether or not its condition holds: Follow is about the
    // operand, and the PC-line hint already says taken or not.
    if (h.kind == Hint::Kind::Branch) return code_at(h.target);
    if (h.kind == Hint::Kind::Memory) {
        f.kind = Follow::Kind::Data;
        f.addr = h.addr;
        return f;
    }

    // The unconditional transfers pc_hint() does not report (it hints conditionals only).
    const uint16_t a = w.a;
    const uint8_t op = code(a);
    switch (op) {
    case 0xC3: case 0xCD:                                   // JP nn / CALL nn
        return code_at(u16(code, static_cast<uint16_t>(a + 1)));
    case 0x18:                                              // JR e
        return code_at(rel(code, static_cast<uint16_t>(a + 1), static_cast<uint16_t>(a + 2)));
    case 0xC9:                                              // RET
        return code_at(u16(data, r.SP));
    case 0xE9:                                              // JP (HL) / JP (IX) / JP (IY)
        return code_at(w.prefixes > 0 ? w.idx : r.HL);
    case 0xED: {
        const uint8_t e = code(static_cast<uint16_t>(a + 1));
        if ((e & 0xC7) == 0x45) return code_at(u16(data, r.SP));   // RETN / RETI and the aliases
        return f;                                                  // ED 98 JP (C) needs a port read
    }
    default: break;
    }
    if ((op & 0xC7) == 0xC7)                                // RST p
        return code_at(static_cast<uint16_t>(op & 0x38));
    return f;
}

Transfer pc_transfer(const Z80Registers& r, const DisasmReadFn& code, const DisasmReadFn& data) {
    Transfer t;
    const Hint h = pc_hint(r, code, data);
    bool hidden = false;
    if (h.kind == Hint::Kind::Branch) {
        if (!h.taken) return t;
        t.target = h.target;
    } else {
        const Follow f = follow_target(r, r.PC, code, data);
        if (f.kind != Follow::Kind::Code) return t;
        t.target = f.addr;
        Walk w;
        if (!walk_prefixes(r, r.PC, code, &w)) return Transfer{};
        const uint8_t op = code(w.a);
        hidden = op == 0xC9 || op == 0xE9
              || (op == 0xED && (code(static_cast<uint16_t>(w.a + 1)) & 0xC7) == 0x45);
    }
    t.taken  = true;
    t.hidden = hidden;
    t.dir    = t.target < r.PC ? Transfer::Dir::Up : t.target > r.PC ? Transfer::Dir::Down : Transfer::Dir::Here;
    return t;
}

std::string format_transfer(const Transfer& t, const disasm_text::SymbolNamer& namer) {
    if (!t.taken || !t.hidden) return std::string();
    if (namer) {
        if (auto s = namer(t.target)) return "[-> " + *s + "]";
    }
    char b[16];
    std::snprintf(b, sizeof b, "[-> $%04X]", t.target);
    return b;
}

std::string format_hint(const Hint& h, const disasm_text::SymbolNamer& namer) {
    auto name = [&](uint16_t a) {
        if (namer) {
            if (auto s = namer(a)) return *s;
        }
        char b[8];
        std::snprintf(b, sizeof b, "$%04X", a);
        return std::string(b);
    };
    switch (h.kind) {
    case Hint::Kind::None:
        return std::string();
    case Hint::Kind::Branch:
        if (!h.taken) return "[not taken]";
        return h.from_stack ? "[taken -> " + name(h.target) + "]" : "[taken]";
    case Hint::Kind::Memory: {
        char v[8];
        if (h.width == 2) std::snprintf(v, sizeof v, "$%04X", h.value);
        else              std::snprintf(v, sizeof v, "$%02X", h.value & 0xFF);
        return "[(" + name(h.addr) + ")=" + v + "]";
    }
    }
    return std::string();
}

} // namespace disasm_hint
