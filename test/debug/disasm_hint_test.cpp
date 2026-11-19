// Disassembly PC-line hint (GH #367): disasm_hint::pc_hint() / format_hint().
//
// Oracles, none of them the code under test:
//   * Zilog UM0080 (Z80 CPU User Manual): the opcode tables and the condition-code table.
//   * The FUSE test data test/fuse/tests.in / tests.expected: every case lists the address
//     of each memory read (MR) and write (MW) the instruction made. Parsed here by a reader
//     of its own, independent of fuse_z80_test.
//   * jnext's Z80Cpu (FUSE 1356/1356), executed once per swept instruction, for what the FUSE
//     data lacks (DD/FD prefix-ignored forms, the Z80N ED opcodes, every flag value).
//   * disasm_one()'s text, for the agreement of the two decoders (DHINT-20, DHINT-30).
// No VHDL oracle: the hint is frontend-facing Z80 semantics.
//
// Run: ./build/test/disasm_hint_test build/test/fuse

#include "debug/disasm_hint.h"
#include "debug/disasm.h"
#include "cpu/z80_cpu.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../row_id.h"

using namespace disasm_hint;

static int g_total = 0, g_pass = 0, g_fail = 0, g_skip = 0;

struct Row {
    bool ok = true;
    std::string why;
    void need(bool c, const std::string& w) {
        if (!c) { if (ok) why = w; ok = false; }
    }
};

static std::string hx(unsigned v) { char b[16]; std::snprintf(b, sizeof b, "$%X", v); return b; }

static void check(const char* id, const char* desc, const Row& r) {
    report_row_id(id);
    ++g_total;
    if (r.ok) ++g_pass;
    else { ++g_fail; std::printf("  FAIL %s: %s (%s)\n", id, desc, r.why.c_str()); }
}

// ---- a 64 KB image and the readers over it -------------------------------------------------
struct Img {
    std::vector<uint8_t> m = std::vector<uint8_t>(65536, 0);
    void put(uint16_t a, std::initializer_list<uint8_t> b) { for (uint8_t v : b) m[a++] = v; }
};
static DisasmReadFn reader(const Img& i, int* count = nullptr) {
    return [&i, count](uint16_t a) -> uint8_t { if (count) ++*count; return i.m[a]; };
}
static Z80Registers regs() {
    Z80Registers r{};
    r.PC = 0x8000; r.SP = 0xA000;
    r.BC = 0x1000; r.DE = 0x2000; r.HL = 0x3000; r.IX = 0x4000; r.IY = 0x5000;
    return r;
}
static Hint hint(const Z80Registers& r, const Img& i) { return pc_hint(r, reader(i), reader(i)); }
static bool is_mem(const Hint& h, uint16_t addr, int width) {
    return h.kind == Hint::Kind::Memory && h.addr == addr && h.width == width;
}

// UM0080 condition codes, index = bits 5-3: name, flag mask, taken when the flag is set.
struct CC { const char* name; uint8_t mask; bool when_set; };
static const CC kCC[8] = {
    {"NZ", 0x40, false}, {"Z", 0x40, true}, {"NC", 0x01, false}, {"C", 0x01, true},
    {"PO", 0x04, false}, {"PE", 0x04, true}, {"P", 0x80, false}, {"M", 0x80, true},
};
static bool cc_taken(int cc, uint8_t f) { return ((f & kCC[cc].mask) != 0) == kCC[cc].when_set; }
// The flag values that exercise a condition: each side, and the other flags all set / all clear.
static std::vector<uint8_t> flag_set(int cc) {
    const uint8_t m = kCC[cc].mask;
    return {0x00, m, static_cast<uint8_t>(0xFF ^ m), 0xFF};
}

// ---- rows ----------------------------------------------------------------------------------
static Row jp_cc(int cc) {
    Row row;
    for (uint8_t f : flag_set(cc)) {
        Img i; i.put(0x8000, {static_cast<uint8_t>(0xC2 | (cc << 3)), 0x34, 0x12});
        Z80Registers r = regs(); r.AF = f;
        const Hint h = hint(r, i);
        row.need(h.kind == Hint::Kind::Branch && h.taken == cc_taken(cc, f) && h.target == 0x1234 && !h.from_stack,
                 std::string("JP ") + kCC[cc].name + " F=" + hx(f));
    }
    return row;
}

static Row jr_cc() {
    Row row;
    static const uint8_t ops[4] = {0x20, 0x28, 0x30, 0x38};
    for (int cc = 0; cc < 4; ++cc)
        for (uint8_t f : flag_set(cc))
            for (uint8_t d : {uint8_t(5), uint8_t(0xFE)}) {
                Img i; i.put(0x8000, {ops[cc], d});
                Z80Registers r = regs(); r.AF = f;
                const Hint h = hint(r, i);
                const uint16_t t = d == 5 ? 0x8007 : 0x8000;
                row.need(h.kind == Hint::Kind::Branch && h.taken == cc_taken(cc, f) && h.target == t && !h.from_stack,
                         std::string("JR ") + kCC[cc].name + " F=" + hx(f) + " d=" + hx(d));
            }
    return row;
}

static Row djnz() {
    Row row;
    // taken iff B-1 (mod 256) != 0
    for (int b : {0, 1, 2, 0x80, 0xFF})
        for (uint8_t d : {uint8_t(5), uint8_t(0xFE)}) {
            Img i; i.put(0x8000, {0x10, d});
            Z80Registers r = regs(); r.BC = static_cast<uint16_t>((b << 8) | 0x12);
            const Hint h = hint(r, i);
            row.need(h.kind == Hint::Kind::Branch && h.taken == (b != 1) && h.target == (d == 5 ? 0x8007 : 0x8000),
                     "DJNZ B=" + hx(b) + " d=" + hx(d));
        }
    return row;
}

static Row call_ret_cc() {
    Row row;
    for (int cc = 0; cc < 8; ++cc)
        for (uint8_t f : flag_set(cc)) {
            Img i; i.put(0x8000, {static_cast<uint8_t>(0xC4 | (cc << 3)), 0x78, 0x56});
            Z80Registers r = regs(); r.AF = f;
            Hint h = hint(r, i);
            row.need(h.kind == Hint::Kind::Branch && h.taken == cc_taken(cc, f) && h.target == 0x5678 && !h.from_stack,
                     std::string("CALL ") + kCC[cc].name + " F=" + hx(f));
            Img j; j.put(0x8000, {static_cast<uint8_t>(0xC0 | (cc << 3))});
            j.put(0xA000, {0x34, 0x12});
            h = hint(r, j);
            row.need(h.kind == Hint::Kind::Branch && h.taken == cc_taken(cc, f) && h.target == 0x1234 && h.from_stack,
                     std::string("RET ") + kCC[cc].name + " F=" + hx(f));
            // SP = $FFFF: the high byte of the return address is at $0000
            Img k; k.put(0x8000, {static_cast<uint8_t>(0xC0 | (cc << 3))});
            k.m[0xFFFF] = 0x34; k.m[0x0000] = 0x12;
            Z80Registers rw = r; rw.SP = 0xFFFF;
            h = hint(rw, k);
            row.need(h.target == 0x1234, std::string("RET ") + kCC[cc].name + " wrap");
        }
    return row;
}

static Row none_list() {
    Row row;
    struct C { const char* what; std::vector<uint8_t> b; };
    std::vector<C> cs = {
        {"JP nn", {0xC3, 0x34, 0x12}}, {"JR e", {0x18, 0x05}}, {"CALL nn", {0xCD, 0x34, 0x12}},
        {"RET", {0xC9}}, {"RETI", {0xED, 0x4D}}, {"JP (HL)", {0xE9}}, {"JP (IX)", {0xDD, 0xE9}},
        {"JP (IY)", {0xFD, 0xE9}}, {"JP (C)", {0xED, 0x98}}, {"HALT", {0x76}},
    };
    for (uint8_t e : {0x45, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D}) cs.push_back({"RETN", {0xED, e}});
    for (int n = 0; n < 8; ++n) cs.push_back({"RST", {static_cast<uint8_t>(0xC7 | (n << 3))}});
    for (const C& c : cs) {
        Img i; for (size_t k = 0; k < c.b.size(); ++k) i.m[0x8000 + k] = c.b[k];
        Z80Registers r = regs(); r.AF = 0xFFFF;
        row.need(hint(r, i).kind == Hint::Kind::None, c.what);
    }
    return row;
}

// every instruction naming (HL), written out from UM0080 (not derived from bit fields)
static Row hl_forms() {
    Row row;
    std::vector<std::vector<uint8_t>> codes;
    for (uint8_t o : {0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E, 0x7E}) codes.push_back({o});          // LD r,(HL)
    for (uint8_t o : {0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77}) codes.push_back({o});          // LD (HL),r
    for (uint8_t o : {0x86, 0x8E, 0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE}) codes.push_back({o});    // ALU A,(HL)
    for (uint8_t o : {0x34, 0x35}) codes.push_back({o});                                          // INC/DEC (HL)
    codes.push_back({0x36, 0x99});                                                                // LD (HL),n
    for (int g = 0; g < 16; ++g) codes.push_back({0xCB, static_cast<uint8_t>((g << 4) | 0x06)});  // CB x6
    for (int g = 0; g < 16; ++g) codes.push_back({0xCB, static_cast<uint8_t>((g << 4) | 0x0E)});  // CB xE
    for (const auto& c : codes) {
        Img i; for (size_t k = 0; k < c.size(); ++k) i.m[0x8000 + k] = c[k];
        i.m[0x3000] = 0x5A;
        const Hint h = hint(regs(), i);
        row.need(is_mem(h, 0x3000, 1) && h.value == 0x5A, "HL form " + hx(c[0]) + (c.size() > 1 ? hx(c[1]) : ""));
    }
    Img h76; h76.m[0x8000] = 0x76;
    row.need(hint(regs(), h76).kind == Hint::Kind::None, "0x76 is HALT, not LD (HL),(HL)");
    Img cbr; cbr.put(0x8000, {0xCB, 0x05});
    row.need(hint(regs(), cbr).kind == Hint::Kind::None, "CB 05 (RLC L) names no memory");
    return row;
}

static Row bc_de() {
    Row row;
    struct C { uint8_t op; uint16_t a; };
    for (C c : {C{0x02, 0x1000}, C{0x0A, 0x1000}, C{0x12, 0x2000}, C{0x1A, 0x2000}}) {
        Img i; i.m[0x8000] = c.op; i.m[c.a] = 0x6B; i.m[c.a + 1] = 0xFF;
        const Hint h = hint(regs(), i);
        row.need(is_mem(h, c.a, 1) && h.value == 0x6B, "op " + hx(c.op));
    }
    return row;
}

static Row nn_byte() {
    Row row;
    for (uint8_t op : {0x32, 0x3A}) {
        Img i; i.put(0x8000, {op, 0x40, 0x5C}); i.m[0x5C40] = 0x77; i.m[0x5C41] = 0x99;
        const Hint h = hint(regs(), i);
        row.need(is_mem(h, 0x5C40, 1) && h.value == 0x77, "op " + hx(op));
    }
    return row;
}

static Row nn_word() {
    Row row;
    std::vector<std::vector<uint8_t>> codes = {{0x22}, {0x2A}, {0xDD, 0x22}, {0xDD, 0x2A}, {0xFD, 0x22}, {0xFD, 0x2A}};
    for (uint8_t e : {0x43, 0x4B, 0x53, 0x5B, 0x63, 0x6B, 0x73, 0x7B}) codes.push_back({0xED, e});
    for (auto c : codes) {
        for (uint16_t nn : {uint16_t(0x5C40), uint16_t(0xFFFF)}) {
            std::vector<uint8_t> b = c;
            b.push_back(nn & 0xFF); b.push_back(nn >> 8);
            Img i; for (size_t k = 0; k < b.size(); ++k) i.m[0x8000 + k] = b[k];
            i.m[nn] = 0x34; i.m[static_cast<uint16_t>(nn + 1)] = 0x12;
            const Hint h = hint(regs(), i);
            row.need(is_mem(h, nn, 2) && h.value == 0x1234, "word form " + hx(c.back()) + " nn=" + hx(nn));
        }
    }
    return row;
}

static Row indexed_bytes() {
    Row row;
    std::vector<uint8_t> ops = {0x34, 0x35, 0x36};
    for (uint8_t o : {0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E, 0x7E, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77}) ops.push_back(o);
    for (uint8_t o : {0x86, 0x8E, 0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE}) ops.push_back(o);
    for (uint8_t pfx : {0xDD, 0xFD})
        for (uint8_t op : ops)
            for (uint8_t d : {uint8_t(5), uint8_t(0x80), uint8_t(0xFF)}) {
                Img i; i.put(0x8000, {pfx, op, d, 0x01});
                Z80Registers r = regs();
                const uint16_t base = pfx == 0xDD ? r.IX : r.IY;
                const uint16_t a = static_cast<uint16_t>(base + static_cast<int8_t>(d));
                i.m[a] = 0x3C;
                const Hint h = hint(r, i);
                row.need(is_mem(h, a, 1) && h.value == 0x3C, "pfx " + hx(pfx) + " op " + hx(op) + " d " + hx(d));
            }
    {
        Img i; i.put(0x8000, {0xDD, 0x7E, 0x01});
        Z80Registers r = regs(); r.IX = 0xFFFF;
        row.need(is_mem(hint(r, i), 0x0000, 1), "IX=$FFFF d=+1 wraps to $0000");
        Img j; j.put(0x8000, {0xDD, 0x7E, 0xFF});
        r.IX = 0x0000;
        row.need(is_mem(hint(r, j), 0xFFFF, 1), "IX=0 d=-1 wraps to $FFFF");
    }
    return row;
}

static Row indexed_cb() {
    Row row;
    for (uint8_t pfx : {0xDD, 0xFD})
        for (int op = 0; op < 256; ++op)
            for (uint8_t d : {uint8_t(5), uint8_t(0xFB)}) {
                Img i; i.put(0x8000, {pfx, 0xCB, d, static_cast<uint8_t>(op)});
                Z80Registers r = regs();
                const uint16_t base = pfx == 0xDD ? r.IX : r.IY;
                const uint16_t a = static_cast<uint16_t>(base + static_cast<int8_t>(d));
                i.m[a] = 0xC3;
                const Hint h = hint(r, i);
                row.need(is_mem(h, a, 1) && h.value == 0xC3, "pfx " + hx(pfx) + " sub " + hx(op) + " d " + hx(d));
            }
    return row;
}

static Row ex_sp() {
    Row row;
    for (auto c : std::vector<std::vector<uint8_t>>{{0xE3}, {0xDD, 0xE3}, {0xFD, 0xE3}}) {
        Img i; for (size_t k = 0; k < c.size(); ++k) i.m[0x8000 + k] = c[k];
        i.put(0xA000, {0x21, 0x43});
        const Hint h = hint(regs(), i);
        row.need(is_mem(h, 0xA000, 2) && h.value == 0x4321, "EX (SP) form of " + std::to_string(c.size()) + " bytes");
    }
    return row;
}

static bool text_is_db(const Img& i, uint16_t pc) {
    return std::strncmp(disasm_one(pc, reader(i)).mnemonic, "DB $", 4) == 0;
}

static Row prefixes() {
    Row row;
    {   Img i; i.put(0x8000, {0xDD, 0x0A}); i.m[0x1000] = 0x42;
        const Hint h = hint(regs(), i);
        row.need(is_mem(h, 0x1000, 1) && h.value == 0x42, "DD 0A is LD A,(BC)"); }
    {   Img i; i.put(0x8000, {0xDD, 0x20, 0x05});
        Z80Registers r = regs(); r.AF = 0x0000;
        const Hint h = hint(r, i);
        row.need(h.kind == Hint::Kind::Branch && h.taken && h.target == 0x8008, "DD 20 05 goes to PC+3+5"); }
    {   Img i; i.put(0x8000, {0xDD, 0xFD, 0x7E, 0x05}); i.m[0x5005] = 0x11;
        row.need(is_mem(hint(regs(), i), 0x5005, 1), "DD FD 7E d: the last prefix (IY) wins"); }
    {   Img i; i.put(0x8000, {0xFD, 0xDD, 0xCB, 0x05, 0x46}); i.m[0x4005] = 0x11;
        row.need(is_mem(hint(regs(), i), 0x4005, 1), "FD DD CB d 46: IX wins"); }
    // The redundant-prefix chain limit: where disasm_one() shows `DB $DD`, there is no hint.
    // The four boundary points, pinned from the text (kMaxPrefixChain = 64) ...
    auto chain = [&](int n, std::initializer_list<uint8_t> tail, Img& i) {
        for (int k = 0; k < n; ++k) i.m[0x8000 + k] = 0xDD;
        uint16_t a = 0x8000 + n; for (uint8_t b : tail) i.m[a++] = b;
    };
    struct B { int n; bool indexed; bool db; };
    for (B b : {B{64, false, false}, B{65, false, true}, B{65, true, false}, B{66, true, true}}) {
        Img i; if (b.indexed) chain(b.n, {0x7E, 0x05}, i); else chain(b.n, {0x0A}, i);
        row.need(text_is_db(i, 0x8000) == b.db, "text boundary n=" + std::to_string(b.n));
        row.need((hint(regs(), i).kind == Hint::Kind::None) == b.db, "hint boundary n=" + std::to_string(b.n));
    }
    // ... and everywhere in between agrees with the text.
    for (int n = 1; n <= 70; ++n)
        for (int form = 0; form < 3; ++form) {
            Img i;
            if (form == 0) chain(n, {0x0A}, i);
            else if (form == 1) chain(n, {0x7E, 0x05}, i);
            else chain(n, {0x20, 0x05}, i);
            row.need((hint(regs(), i).kind == Hint::Kind::None) == text_is_db(i, 0x8000),
                     "chain n=" + std::to_string(n) + " form " + std::to_string(form));
        }
    return row;
}

struct Counted { int n = 0; };

static const uint8_t kZ80nEd[30] = {   // disasm.cpp kZ80nEd, from t80n_mcode.vhd
    0x23, 0x24, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x8A,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x98, 0xA4, 0xA5, 0xAC, 0xB4, 0xB6, 0xB7, 0xBC,
};

static Row no_data_reads() {
    Row row;
    std::vector<std::vector<uint8_t>> cs = {
        {0xDB, 0xFE}, {0xD3, 0xFE},       // IN A,(n)  OUT (n),A
        {0xC5}, {0xD5}, {0xE5}, {0xF5}, {0xC1}, {0xD1}, {0xE1}, {0xF1},   // PUSH / POP
        {0xDD, 0xE5}, {0xFD, 0xE1},
    };
    for (int e = 0x40; e <= 0x79; e += 8) { cs.push_back({0xED, static_cast<uint8_t>(e)}); }          // IN r,(C) 40..70 (+ 78 below)
    for (int e = 0x41; e <= 0x79; e += 8) { cs.push_back({0xED, static_cast<uint8_t>(e)}); }          // OUT (C),r
    cs.push_back({0xED, 0x78});
    for (uint8_t e : {0xA2, 0xAA, 0xB2, 0xBA, 0xA3, 0xAB, 0xB3, 0xBB, 0xA0, 0xA8, 0xB0, 0xB8,
                      0xA1, 0xA9, 0xB1, 0xB9, 0x67, 0x6F})
        cs.push_back({0xED, e});     // INI/IND/INIR/INDR OUTI/OUTD/OTIR/OTDR LDI/LDD/LDIR/LDDR CPI/CPD/CPIR/CPDR RRD RLD
    for (uint8_t e : kZ80nEd) cs.push_back({0xED, e, 0x05, 0x70});
    for (const auto& c : cs) {
        Img i; for (size_t k = 0; k < c.size(); ++k) i.m[0x8000 + k] = c[k];
        int reads = 0;
        const Hint h = pc_hint(regs(), reader(i), reader(i, &reads));
        row.need(h.kind == Hint::Kind::None && reads == 0,
                 "ED/other " + hx(c.size() > 1 ? c[1] : c[0]) + " reads=" + std::to_string(reads));
    }
    return row;
}

static Row split_readers() {
    Row row;
    {   // the displacement comes from `code`, the value from `data`
        Img code; code.put(0x8000, {0xDD, 0x7E, 0x05}); code.m[0x4005] = 0x11;
        Img data; data.put(0x8000, {0xDD, 0x7E, 0x07}); data.m[0x4005] = 0x99; data.m[0x4007] = 0x22;
        const Hint h = pc_hint(regs(), reader(code), reader(data));
        row.need(is_mem(h, 0x4005, 1) && h.value == 0x99, "IX+d: d from code, value from data");
    }
    {   // the RET cc return address is a data read
        Img code; code.m[0x8000] = 0xC8; code.put(0xA000, {0x34, 0x12});
        Img data; data.put(0xA000, {0x78, 0x56});
        Z80Registers r = regs(); r.AF = 0x0040;
        const Hint h = pc_hint(r, reader(code), reader(data));
        row.need(h.kind == Hint::Kind::Branch && h.taken && h.target == 0x5678, "RET Z: target from data");
    }
    return row;
}

static Hint mk_mem(uint16_t a, int w, uint16_t v) {
    Hint h; h.kind = Hint::Kind::Memory; h.addr = a; h.width = w; h.value = v; return h;
}
static Hint mk_br(bool taken, uint16_t t, bool st) {
    Hint h; h.kind = Hint::Kind::Branch; h.taken = taken; h.target = t; h.from_stack = st; return h;
}

static Row fmt_plain() {
    Row row;
    const disasm_text::SymbolNamer none;
    row.need(format_hint(mk_br(true, 0x8007, false), none) == "[taken]", "taken");
    row.need(format_hint(mk_br(false, 0x8007, false), none) == "[not taken]", "not taken");
    row.need(format_hint(mk_br(true, 0x1234, true), none) == "[taken -> $1234]", "RET taken");
    row.need(format_hint(mk_br(false, 0x1234, true), none) == "[not taken]", "RET not taken shows no target");
    row.need(format_hint(mk_mem(0x5C40, 1, 0x3C), none) == "[($5C40)=$3C]", "byte");
    row.need(format_hint(mk_mem(0xFFFF, 2, 0x3412), none) == "[($FFFF)=$3412]", "word");
    row.need(format_hint(Hint{}, none).empty(), "None");
    return row;
}

static Row fmt_named() {
    Row row;
    const disasm_text::SymbolNamer namer = [](uint16_t a) -> std::optional<std::string> {
        if (a == 0x1234) return "ret_here";
        if (a == 0x5C3D) return "ERR_SP";
        return std::nullopt;
    };
    row.need(format_hint(mk_br(true, 0x1234, true), namer) == "[taken -> ret_here]", "RET target named");
    row.need(format_hint(mk_mem(0x5C3D, 2, 0x5C3D), namer) == "[(ERR_SP)=$5C3D]", "address named, value not");
    row.need(format_hint(mk_mem(0x1234, 1, 0x34), namer) == "[(ret_here)=$34]", "byte at a named address");
    row.need(format_hint(mk_mem(0x9000, 1, 0x34), namer) == "[($9000)=$34]", "unnamed address");
    return row;
}

// ---- the FUSE data -------------------------------------------------------------------------
struct FuseCase {
    std::string name;
    unsigned reg[13];
    int limit = 0;
    std::map<uint16_t, uint8_t> mem;
};
struct Ev { std::string type; uint16_t addr; uint8_t data; };
struct FuseExp {
    std::vector<Ev> ev;
    uint16_t pc = 0;
};

static std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> v; std::ifstream f(path); std::string l;
    while (std::getline(f, l)) { while (!l.empty() && (l.back() == '\r')) l.pop_back(); v.push_back(l); }
    return v;
}

static bool load_fuse(const std::string& dir, std::vector<FuseCase>& cases, std::map<std::string, FuseExp>& exp) {
    const auto in = read_lines(dir + "/tests.in");
    const auto ex = read_lines(dir + "/tests.expected");
    if (in.empty() || ex.empty()) return false;
    size_t i = 0;
    while (i < in.size()) {
        if (in[i].find_first_not_of(' ') == std::string::npos) { ++i; continue; }
        FuseCase c; c.name = in[i];
        { std::istringstream s(in[i + 1]); for (int k = 0; k < 13; ++k) s >> std::hex >> c.reg[k]; }
        { std::istringstream s(in[i + 2]); std::string t; std::vector<std::string> tk; while (s >> t) tk.push_back(t); c.limit = std::stoi(tk.back()); }
        i += 3;
        while (in[i] != "-1") {
            std::istringstream s(in[i]); std::string t; s >> t;
            uint16_t a = static_cast<uint16_t>(std::stoul(t, nullptr, 16));
            while (s >> t) { if (t == "-1") break; c.mem[a++] = static_cast<uint8_t>(std::stoul(t, nullptr, 16)); }
            ++i;
        }
        ++i;
        cases.push_back(c);
    }
    size_t j = 0;
    while (j < ex.size()) {
        if (ex[j].find_first_not_of(' ') == std::string::npos) { ++j; continue; }
        const std::string name = ex[j++];
        FuseExp e;
        while (j < ex.size() && !ex[j].empty() && ex[j][0] == ' ') {
            std::istringstream s(ex[j]); std::string t, ty, a, d; s >> t >> ty >> a >> d;
            Ev v; v.type = ty; v.addr = static_cast<uint16_t>(std::stoul(a, nullptr, 16));
            v.data = d.empty() ? 0 : static_cast<uint8_t>(std::stoul(d, nullptr, 16));
            e.ev.push_back(v); ++j;
        }
        { std::istringstream s(ex[j]); unsigned r[12]; for (int k = 0; k < 12; ++k) s >> std::hex >> r[k]; e.pc = static_cast<uint16_t>(r[11]); }
        j += 2;
        while (j < ex.size() && !ex[j].empty()) ++j;
        exp[name] = e;
    }
    return true;
}

struct FuseImg { Img img; };
static Z80Registers fuse_regs(const FuseCase& c) {
    Z80Registers r{};
    r.AF = c.reg[0]; r.BC = c.reg[1]; r.DE = c.reg[2]; r.HL = c.reg[3];
    r.IX = c.reg[8]; r.IY = c.reg[9]; r.SP = c.reg[10]; r.PC = c.reg[11];
    return r;
}
static void fuse_img(const FuseCase& c, Img& i) { for (auto& kv : c.mem) i.m[kv.first] = kv.second; }

static bool in_own(uint16_t a, uint16_t pc, int len) { return static_cast<uint16_t>(a - pc) < len; }

static int g_f25_total, g_f25_taken, g_f25_ambig;
static int g_f26_total, g_f26_mr;
static int g_f27_named, g_f27_none, g_f27_stack, g_f27_block, g_f27_rrd, g_f27_unexpl, g_f27_multi;

static Row fuse_rows(const std::vector<FuseCase>& cases, const std::map<std::string, FuseExp>& exp,
                     Row& r25, Row& r26, Row& r27) {
    Row dummy;
    for (const FuseCase& c : cases) {
        auto it = exp.find(c.name);
        if (it == exp.end()) { dummy.need(false, "no expected for " + c.name); continue; }
        const FuseExp& e = it->second;
        Img img; fuse_img(c, img);
        const Z80Registers r = fuse_regs(c);
        const Hint h = hint(r, img);
        const int len = instruction_length(r.PC, reader(img));

        // events after the instruction's own byte reads
        std::vector<Ev> data;
        for (const Ev& v : e.ev) if (v.type == "MR" || v.type == "MW") data.push_back(v);
        size_t k = 0;
        while (k < data.size() && data[k].type == "MR" && in_own(data[k].addr, r.PC, len)) ++k;

        if (h.kind == Hint::Kind::Branch) {
            ++g_f25_total; if (h.taken) ++g_f25_taken;
            // the next PC: the first fetch after the instruction (and, for a taken RET, its two stack reads)
            std::vector<uint16_t> mrs;
            for (const Ev& v : e.ev) if (v.type == "MR") mrs.push_back(v.addr);
            size_t m = 0;
            while (m < mrs.size() && in_own(mrs[m], r.PC, len)) ++m;
            if (h.from_stack && h.taken) m += 2;
            const uint16_t nxt = m < mrs.size() ? mrs[m] : e.pc;
            const uint16_t fall = static_cast<uint16_t>(r.PC + len);
            if (h.target == fall) ++g_f25_ambig;
            r25.need((nxt == h.target) == h.taken, "case " + c.name + " taken=" + std::to_string(h.taken));
        }
        if (h.kind == Hint::Kind::Memory) {
            ++g_f26_total;
            std::set<uint16_t> want, got;
            for (int w = 0; w < h.width; ++w) want.insert(static_cast<uint16_t>(h.addr + w));
            // a single-instruction case has no other data access: ALL the events after its own bytes are
            // the operand's (so a width reported too short is seen); in a multi-instruction case only
            // the first `width` can be attributed to the first instruction
            for (size_t w = 0; k + w < data.size() && (c.limit == 1 || w < static_cast<size_t>(h.width)); ++w)
                got.insert(data[k + w].addr);
            r26.need(want == got, "case " + c.name + " addresses");
            if (k < data.size() && data[k].type == "MR") ++g_f26_mr;
            for (int w = 0; w < h.width && k + w < data.size(); ++w) {
                const Ev& v = data[k + w];
                if (v.type != "MR") continue;
                const int off = static_cast<uint16_t>(v.addr - h.addr);
                r26.need(((h.value >> (8 * off)) & 0xFF) == v.data, "case " + c.name + " value byte");
            }
        }
        // completeness (single-instruction cases)
        if (c.limit != 1) { ++g_f27_multi; continue; }
        if (h.kind == Hint::Kind::Memory) { ++g_f27_named; continue; }
        if (k >= data.size()) { ++g_f27_none; continue; }
        uint16_t a = r.PC;
        while (img.m[a] == 0xDD || img.m[a] == 0xFD) ++a;
        const uint8_t op = img.m[a], ed = img.m[static_cast<uint16_t>(a + 1)];
        if (op == 0xC5 || op == 0xD5 || op == 0xE5 || op == 0xF5 || op == 0xC1 || op == 0xD1 || op == 0xE1 ||
            op == 0xF1 || op == 0xCD || op == 0xC9 || (op & 0xC7) == 0xC7 || (op & 0xC7) == 0xC0 || (op & 0xC7) == 0xC4 ||
            (op == 0xED && (ed & 0xC7) == 0x45)) { ++g_f27_stack; continue; }
        if (op == 0xED && (ed == 0xA0 || ed == 0xA1 || ed == 0xA2 || ed == 0xA3 || ed == 0xA8 || ed == 0xA9 ||
                           ed == 0xAA || ed == 0xAB || ed == 0xB0 || ed == 0xB1 || ed == 0xB2 || ed == 0xB3 ||
                           ed == 0xB8 || ed == 0xB9 || ed == 0xBA || ed == 0xBB)) { ++g_f27_block; continue; }
        if (op == 0xED && (ed == 0x67 || ed == 0x6F)) { ++g_f27_rrd; continue; }
        ++g_f27_unexpl;
        r27.need(false, "unexplained data access in case " + c.name);
    }
    return dummy;
}

// ---- the CPU sweeps ------------------------------------------------------------------------
struct RecMem : MemoryInterface {
    uint8_t ram[65536];
    std::vector<uint16_t> acc;
    RecMem() { std::memset(ram, 0, sizeof ram); }
    uint8_t read(uint16_t a) override { acc.push_back(a); return ram[a]; }
    void write(uint16_t a, uint8_t v) override { acc.push_back(a); ram[a] = v; }
};
struct NullIo : IoInterface {
    uint8_t in(uint16_t) override { return 0xFF; }
    void out(uint16_t, uint8_t) override {}
};

static int g_s28_cases;
static Row sweep_branches() {
    Row row;
    RecMem mem; NullIo io; Z80Cpu cpu(mem, io);
    struct Op { std::vector<uint8_t> b; int tail; bool djnz; };   // opcode bytes, then operands
    std::vector<uint8_t> ops;
    for (uint8_t o : {0x20, 0x28, 0x30, 0x38}) ops.push_back(o);
    for (int cc = 0; cc < 8; ++cc) { ops.push_back(0xC2 | (cc << 3)); ops.push_back(0xC4 | (cc << 3)); ops.push_back(0xC0 | (cc << 3)); }
    ops.push_back(0x10);
    for (int pv = 0; pv < 3; ++pv) {
        const int plen = pv == 0 ? 0 : 1;
        for (uint8_t op : ops) {
            const bool rel = (op == 0x10 || (op & 0xE7) == 0x20);
            const bool ret = (op & 0xC7) == 0xC0;
            const int n = op == 0x10 ? 256 : 256;
            for (int v = 0; v < n; ++v) {
                std::memset(mem.ram, 0, sizeof mem.ram);
                uint16_t a = 0x8000;
                if (plen) mem.ram[a++] = pv == 1 ? 0xDD : 0xFD;
                mem.ram[a++] = op;
                uint16_t target;
                if (rel) { mem.ram[a++] = 0x20; target = static_cast<uint16_t>(0x8000 + plen + 2 + 0x20); }
                else if (ret) { target = 0x9000; mem.ram[0xA000] = 0x00; mem.ram[0xA001] = 0x90; }
                else { mem.ram[a++] = 0x00; mem.ram[a++] = 0x90; target = 0x9000; }
                Z80Registers r{};
                r.PC = 0x8000; r.SP = 0xA000;
                if (op == 0x10) r.BC = static_cast<uint16_t>(v << 8); else r.AF = static_cast<uint16_t>(v);
                *fuse_z80_tstates_ptr() = 0;
                cpu.set_registers(r);
                RecMem& m = mem;
                const DisasmReadFn rdf = [&m](uint16_t x) -> uint8_t { return m.ram[x]; };
                const Hint h = pc_hint(r, rdf, rdf);
                cpu.execute();
                const bool took = cpu.pc() == target;
                ++g_s28_cases;
                row.need(h.kind == Hint::Kind::Branch && h.taken == took && h.target == target,
                         "op " + hx(op) + " pv " + std::to_string(pv) + " v " + hx(v));
            }
        }
    }
    return row;
}

static int g_s29_named, g_s29_silent, g_s29_stack, g_s29_z80n, g_s29_total;
static Row sweep_memory() {
    Row row;
    RecMem mem; NullIo io; Z80Cpu cpu(mem, io);
    auto one = [&](std::initializer_list<uint8_t> code, uint8_t op_after_prefix, bool z80n, const std::string& what) {
        std::memset(mem.ram, 0, sizeof mem.ram);
        uint16_t a = 0x8000; for (uint8_t b : code) mem.ram[a++] = b;
        Z80Registers r = regs(); r.SP = 0x6000;
        *fuse_z80_tstates_ptr() = 0;
        cpu.set_registers(r);
        const DisasmReadFn rdf = [&mem](uint16_t x) -> uint8_t { return mem.ram[x]; };
        const Hint h = pc_hint(r, rdf, rdf);
        mem.acc.clear();
        cpu.execute();
        std::set<uint16_t> A;
        for (uint16_t x : mem.acc) if (!in_own(x, 0x8000, 8)) A.insert(x);
        ++g_s29_total;
        if (h.kind == Hint::Kind::Memory) {
            std::set<uint16_t> want;
            for (int w = 0; w < h.width; ++w) want.insert(static_cast<uint16_t>(h.addr + w));
            ++g_s29_named;
            row.need(A == want, what + ": accessed set != named operand");
            return;
        }
        if (A.empty()) { ++g_s29_silent; return; }
        const uint8_t op = op_after_prefix;
        bool stack = op == 0xC5 || op == 0xD5 || op == 0xE5 || op == 0xF5 || op == 0xC1 || op == 0xD1 || op == 0xE1 ||
                     op == 0xF1 || op == 0xCD || op == 0xC9 || (op & 0xC7) == 0xC7 || (op & 0xC7) == 0xC0 || (op & 0xC7) == 0xC4;
        bool inside = true;
        if (z80n) {
            // PUSH nn writes the stack; the block-like ops touch (HL) / (DE) only
            for (uint16_t x : A)
                if (!((x >= 0x5FFE && x <= 0x6001) || (x >= 0x2FFF && x <= 0x3001) || (x >= 0x1FFF && x <= 0x2001))) inside = false;
            ++g_s29_z80n;
            row.need(inside, what + ": Z80N access outside (HL)/(DE)/stack");
            return;
        }
        for (uint16_t x : A) if (x < 0x5FFE || x > 0x6001) inside = false;
        ++g_s29_stack;
        row.need(stack && inside, what + ": unexplained data access");
    };
    for (uint8_t pfx : {0xDD, 0xFD})
        for (int op = 0; op < 256; ++op)
            one({pfx, static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, static_cast<uint8_t>(op), false,
                "prefix " + hx(pfx) + " op " + hx(op));
    for (uint8_t e : kZ80nEd) one({0xED, e, 0x05, 0x70, 0x00}, e, true, "Z80N ED " + hx(e));
    return row;
}

// ---- text agreement ------------------------------------------------------------------------
static int g_t30_cases;
static Row text_agreement() {
    Row row;
    auto one = [&](std::initializer_list<uint8_t> code, const std::string& what) {
        Img i; uint16_t a = 0x8000; for (uint8_t b : code) i.m[a++] = b;
        Z80Registers r = regs();
        for (int k = 0; k < 16; ++k) i.m[0x3000 + k] = 0x40;   // any value; the readers agree
        const DisasmReadFn rdf = reader(i);
        const DisasmLine l = disasm_one(0x8000, rdf);
        const std::string m = l.mnemonic;
        const Hint h = pc_hint(r, rdf, rdf);
        ++g_t30_cases;
        // Branch iff the mnemonic is JR/JP/CALL cc, RET cc or DJNZ
        const bool br_text = (m.rfind("JR ", 0) == 0 && m.find(',') != std::string::npos) ||
                             (m.rfind("JP ", 0) == 0 && m.find(',') != std::string::npos) ||
                             (m.rfind("CALL ", 0) == 0 && m.find(',') != std::string::npos) ||
                             m.rfind("RET ", 0) == 0 || m.rfind("DJNZ", 0) == 0;
        row.need((h.kind == Hint::Kind::Branch) == br_text, what + " '" + m + "' branch");
        // a named memory operand, outside JP ( / IN / OUT
        bool has_mem = false; uint16_t addr = 0; int width = 1;
        if (m.rfind("JP (", 0) != 0 && m.rfind("IN ", 0) != 0 && m.rfind("OUT ", 0) != 0) {
            for (size_t p = m.find('('); p != std::string::npos; p = m.find('(', p + 1)) {
                const size_t q = m.find(')', p);
                if (q == std::string::npos) break;
                const std::string t = m.substr(p + 1, q - p - 1);
                bool hit = true;
                if (t == "HL") addr = r.HL;
                else if (t == "BC") addr = r.BC;
                else if (t == "DE") addr = r.DE;
                else if (t == "SP") { addr = r.SP; width = 2; }
                else if ((t.rfind("IX", 0) == 0 || t.rfind("IY", 0) == 0) && t.size() == 6 && (t[2] == '+' || t[2] == '-') && t[3] == '$') {
                    const int d = static_cast<int>(std::stoul(t.substr(4), nullptr, 16));
                    addr = static_cast<uint16_t>((t[1] == 'X' ? r.IX : r.IY) + (t[2] == '+' ? d : -d));
                } else if (t.size() == 5 && t[0] == '$') addr = static_cast<uint16_t>(std::stoul(t.substr(1), nullptr, 16));
                else hit = false;
                if (!hit) continue;
                has_mem = true;
                if (t.size() == 5 && t[0] == '$') {   // LD (nn),rr / LD rr,(nn): the other operand's width
                    std::string other;
                    if (m.rfind("LD (", 0) == 0) other = m.substr(q + 2);
                    else if (m.rfind("LD ", 0) == 0) other = m.substr(3, m.find(',') - 3);
                    width = (other == "BC" || other == "DE" || other == "HL" || other == "SP" || other == "IX" || other == "IY") ? 2 : 1;
                }
                break;
            }
        }
        row.need((h.kind == Hint::Kind::Memory) == has_mem, what + " '" + m + "' memory");
        if (has_mem && h.kind == Hint::Kind::Memory)
            row.need(h.addr == addr && h.width == width, what + " '" + m + "' addr " + hx(h.addr) + " want " + hx(addr) +
                     " width " + std::to_string(h.width) + " want " + std::to_string(width));
    };
    for (int op = 0; op < 256; ++op) one({static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "plain " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xCB, static_cast<uint8_t>(op)}, "CB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xED, static_cast<uint8_t>(op), 0x05, 0x70}, "ED " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "DD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, static_cast<uint8_t>(op), 0xFB, 0x70, 0x00}, "FD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, 0xCB, 0x05, static_cast<uint8_t>(op)}, "DDCB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, 0xCB, 0xFB, static_cast<uint8_t>(op)}, "FDCB " + hx(op));
    return row;
}


// ---- GH #366: Follow ------------------------------------------------------------------------
// Oracle: UM0080 (the transfer instructions' targets), jnext's Z80Cpu for where a transfer really goes
// (FOLW-08), disasm_one()'s text for what is a transfer at all (FOLW-09), pc_hint() for the memory operand.
static Follow fol(const Z80Registers& r, uint16_t at, const Img& i) { return follow_target(r, at, reader(i), reader(i)); }
static bool is_code(const Follow& f, uint16_t a) { return f.kind == Follow::Kind::Code && f.addr == a; }

static Row folw_unconditional() {
    Row row;
    {   Img i; i.put(0x8000, {0xC3, 0x34, 0x12}); row.need(is_code(fol(regs(), 0x8000, i), 0x1234), "JP nn"); }
    {   Img i; i.put(0x8000, {0xCD, 0x78, 0x56}); row.need(is_code(fol(regs(), 0x8000, i), 0x5678), "CALL nn"); }
    for (uint8_t d : {uint8_t(5), uint8_t(0xFE)}) {
        const uint16_t t = d == 5 ? 0x8007 : 0x8000;
        Img i; i.put(0x8000, {0x18, d}); row.need(is_code(fol(regs(), 0x8000, i), t), "JR e d=" + hx(d));
        Img j; j.put(0x8000, {0x10, d}); row.need(is_code(fol(regs(), 0x8000, j), t), "DJNZ d=" + hx(d));
    }
    for (int n = 0; n < 8; ++n) {
        Img i; i.m[0x8000] = static_cast<uint8_t>(0xC7 | (n << 3));
        row.need(is_code(fol(regs(), 0x8000, i), static_cast<uint16_t>(n * 8)), "RST " + hx(n * 8));
    }
    return row;
}

static Row folw_conditional() {
    Row row;
    for (int cc = 0; cc < 8; ++cc)
        for (uint8_t f : {uint8_t(0x00), uint8_t(0xFF)}) {
            Z80Registers r = regs(); r.AF = f;
            Img a; a.put(0x8000, {static_cast<uint8_t>(0xC2 | (cc << 3)), 0x34, 0x12});
            Img b; b.put(0x8000, {static_cast<uint8_t>(0xC4 | (cc << 3)), 0x34, 0x12});
            row.need(is_code(fol(r, 0x8000, a), 0x1234), std::string("JP ") + kCC[cc].name + " F=" + hx(f));
            row.need(is_code(fol(r, 0x8000, b), 0x1234), std::string("CALL ") + kCC[cc].name + " F=" + hx(f));
            if (cc < 4) {
                Img c; c.put(0x8000, {static_cast<uint8_t>(0x20 | (cc << 3)), 0x05});
                row.need(is_code(fol(r, 0x8000, c), 0x8007), std::string("JR ") + kCC[cc].name + " F=" + hx(f));
            }
        }
    return row;
}

static Row folw_register_jumps() {
    Row row;
    { Img i; i.m[0x8000] = 0xE9; row.need(is_code(fol(regs(), 0x8000, i), 0x3000), "JP (HL)"); }
    { Img i; i.put(0x8000, {0xDD, 0xE9}); row.need(is_code(fol(regs(), 0x8000, i), 0x4000), "JP (IX)"); }
    { Img i; i.put(0x8000, {0xFD, 0xE9}); row.need(is_code(fol(regs(), 0x8000, i), 0x5000), "JP (IY)"); }
    { Img i; i.put(0x8000, {0xED, 0x98}); row.need(fol(regs(), 0x8000, i).kind == Follow::Kind::None, "JP (C) has no target"); }
    return row;
}

static Row folw_returns() {
    Row row;
    std::vector<std::vector<uint8_t>> codes = {{0xC9}, {0xED, 0x4D}};
    for (uint8_t e : {0x45, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D}) codes.push_back({0xED, e});
    for (int cc = 0; cc < 8; ++cc) codes.push_back({static_cast<uint8_t>(0xC0 | (cc << 3))});
    for (const auto& c : codes) {
        Img i; for (size_t k = 0; k < c.size(); ++k) i.m[0x8000 + k] = c[k];
        i.put(0xA000, {0x34, 0x12});
        row.need(is_code(fol(regs(), 0x8000, i), 0x1234), "return " + hx(c.back()));
        Img w; for (size_t k = 0; k < c.size(); ++k) w.m[0x8000 + k] = c[k];
        w.m[0xFFFF] = 0x34; w.m[0x0000] = 0x12;
        Z80Registers r = regs(); r.SP = 0xFFFF;
        row.need(is_code(fol(r, 0x8000, w), 0x1234), "return wrap " + hx(c.back()));
        // the return address is a DATA read: the code image holds nothing at SP
        Img code_only; for (size_t k = 0; k < c.size(); ++k) code_only.m[0x8000 + k] = c[k];
        Img data_only; data_only.put(0xA000, {0x34, 0x12});
        row.need(is_code(follow_target(regs(), 0x8000, reader(code_only), reader(data_only)), 0x1234),
                 "return " + hx(c.back()) + " reads its address through `data`");
    }
    return row;
}

static Row folw_at() {
    Row row;
    Img i; i.put(0x8000, {0xCD, 0x34, 0x12});
    Z80Registers r = regs(); r.PC = 0;
    int reads_at_pc = 0;
    const DisasmReadFn code = [&](uint16_t a) -> uint8_t { if (a == 0) ++reads_at_pc; return i.m[a]; };
    const Follow f = follow_target(r, 0x8000, code, reader(i));
    row.need(is_code(f, 0x1234), "decoded at `at`, not at PC");
    row.need(reads_at_pc == 0, "the code reader was never asked for r.PC");
    return row;
}

static int g_f6_cases, g_f6_data;
static Row folw_data_set() {
    Row row;
    auto one = [&](std::initializer_list<uint8_t> code, const std::string& what) {
        Img i; uint16_t a = 0x8000; for (uint8_t b : code) i.m[a++] = b;
        Z80Registers r = regs();
        const Hint h = pc_hint(r, reader(i), reader(i));
        const Follow f = fol(r, 0x8000, i);
        ++g_f6_cases;
        const bool mem = h.kind == Hint::Kind::Memory;
        if (mem) ++g_f6_data;
        row.need((f.kind == Follow::Kind::Data) == mem, what + " data iff memory hint");
        if (mem) row.need(f.addr == h.addr, what + " address");
    };
    for (int op = 0; op < 256; ++op) one({static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "plain " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xCB, static_cast<uint8_t>(op)}, "CB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xED, static_cast<uint8_t>(op), 0x05, 0x70}, "ED " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "DD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, static_cast<uint8_t>(op), 0xFB, 0x70, 0x00}, "FD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, 0xCB, 0x05, static_cast<uint8_t>(op)}, "DDCB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, 0xCB, 0xFB, static_cast<uint8_t>(op)}, "FDCB " + hx(op));
    return row;
}

// disasm_one()'s text: is this a transfer Follow takes to a code address?
static bool transfer_text(const std::string& m) {
    return (m.rfind("JP ", 0) == 0 && m != "JP (C)") || m.rfind("JR ", 0) == 0 || m.rfind("CALL ", 0) == 0 ||
           m.rfind("DJNZ", 0) == 0 || m.rfind("RST ", 0) == 0 || m.rfind("RET", 0) == 0;
}

static int g_f7_none, g_f9_cases, g_f9_code;
static Row folw_none_and_text(bool text_row) {
    Row row;
    auto one = [&](std::initializer_list<uint8_t> code, const std::string& what) {
        Img i; uint16_t a = 0x8000; for (uint8_t b : code) i.m[a++] = b;
        Z80Registers r = regs();
        const DisasmReadFn rdf = reader(i);
        const std::string m = disasm_one(0x8000, rdf).mnemonic;
        int data_reads = 0;
        const Follow f = follow_target(r, 0x8000, rdf, reader(i, &data_reads));
        const bool mem = pc_hint(r, rdf, rdf).kind == Hint::Kind::Memory;
        const bool code_text = transfer_text(m);
        if (text_row) {
            ++g_f9_cases;
            if (f.kind == Follow::Kind::Code) ++g_f9_code;
            row.need((f.kind == Follow::Kind::Code) == code_text, what + " '" + m + "' code iff transfer text");
        } else {
            if (!code_text && !mem) {
                ++g_f7_none;
                row.need(f.kind == Follow::Kind::None && data_reads == 0, what + " '" + m + "' none, no data read");
            } else if (code_text && m.rfind("RET", 0) != 0) {
                row.need(data_reads == 0, what + " '" + m + "' no data read");
            }
        }
    };
    for (int op = 0; op < 256; ++op) one({static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "plain " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xCB, static_cast<uint8_t>(op)}, "CB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xED, static_cast<uint8_t>(op), 0x05, 0x70}, "ED " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "DD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, static_cast<uint8_t>(op), 0xFB, 0x70, 0x00}, "FD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, 0xCB, 0x05, static_cast<uint8_t>(op)}, "DDCB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, 0xCB, 0xFB, static_cast<uint8_t>(op)}, "FDCB " + hx(op));
    return row;
}

static int g_f8_runs;
static Row folw_sweep() {
    Row row;
    RecMem mem; NullIo io; Z80Cpu cpu(mem, io);
    // every transfer opcode: unprefixed, behind DD and behind FD (ED forms unprefixed only)
    std::vector<std::vector<uint8_t>> ops;
    for (uint8_t o : {0xC3, 0xCD, 0x18, 0x10, 0xC9, 0xE9}) ops.push_back({o, 0x20, 0x90});
    for (int n = 0; n < 8; ++n) ops.push_back({static_cast<uint8_t>(0xC7 | (n << 3)), 0, 0});
    for (int cc = 0; cc < 8; ++cc) {
        ops.push_back({static_cast<uint8_t>(0xC2 | (cc << 3)), 0x20, 0x90});
        ops.push_back({static_cast<uint8_t>(0xC4 | (cc << 3)), 0x20, 0x90});
        ops.push_back({static_cast<uint8_t>(0xC0 | (cc << 3)), 0, 0});
        if (cc < 4) ops.push_back({static_cast<uint8_t>(0x20 | (cc << 3)), 0x20, 0});
    }
    std::vector<std::vector<uint8_t>> ed_ops;
    for (uint8_t e : {0x4D, 0x45, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D}) ed_ops.push_back({0xED, e, 0, 0});
    auto run = [&](const std::vector<uint8_t>& bytes, uint8_t f, const std::string& what) {
        std::memset(mem.ram, 0, sizeof mem.ram);
        for (size_t k = 0; k < bytes.size(); ++k) mem.ram[0x8000 + k] = bytes[k];
        mem.ram[0xA000] = 0x00; mem.ram[0xA001] = 0x90;
        Z80Registers r{};
        r.PC = 0x8000; r.SP = 0xA000; r.AF = f; r.BC = 0x0200; r.HL = 0x3000; r.IX = 0x4000; r.IY = 0x5000;
        *fuse_z80_tstates_ptr() = 0;
        cpu.set_registers(r);
        RecMem& m = mem;
        const DisasmReadFn rdf = [&m](uint16_t x) -> uint8_t { return m.ram[x]; };
        const Follow fl = follow_target(r, 0x8000, rdf, rdf);
        cpu.execute();
        ++g_f8_runs;
        row.need(fl.kind == Follow::Kind::Code && cpu.pc() == fl.addr, what + " F=" + hx(f) + " -> " + hx(fl.addr) +
                 " but the CPU went to " + hx(cpu.pc()));
    };
    for (int pv = 0; pv < 3; ++pv)
        for (const auto& o : ops) {
            std::vector<uint8_t> b;
            if (pv) b.push_back(pv == 1 ? 0xDD : 0xFD);
            b.insert(b.end(), o.begin(), o.end());
            const uint8_t op = o[0];
            // a flag byte that takes the condition (0xFF takes M, C, PE, Z; 0x00 takes NZ, NC, PO, P)
            const bool cond = op != 0xC3 && op != 0xCD && op != 0x18 && op != 0xC9 && op != 0xE9 && op != 0x10 &&
                              (op & 0xC7) != 0xC7;
            if (!cond) { run(b, 0x00, "op " + hx(op) + " pv " + std::to_string(pv)); continue; }
            for (uint8_t f : {uint8_t(0x00), uint8_t(0xFF)}) {
                // run only when the condition holds: the CPU then lands on the target
                const int cc = (op >> 3) & 7;
                const int base = (op & 0xE7) == 0x20 ? ((op >> 3) & 3) : cc;
                const uint8_t mask[8] = {0x40, 0x40, 0x01, 0x01, 0x04, 0x04, 0x80, 0x80};
                const bool when_set = (op & 0xE7) == 0x20 ? (base & 1) : (base & 1);
                const bool holds = ((f & mask[(op & 0xE7) == 0x20 ? base : cc]) != 0) == when_set;
                if (holds) run(b, f, "op " + hx(op) + " pv " + std::to_string(pv));
            }
        }
    for (const auto& b : ed_ops) run(b, 0x00, "ED " + hx(b[1]));
    return row;
}

// ---- GH #542: where the PC goes -------------------------------------------------------------
// Oracle: UM0080 (transfer targets, the cc table), the FUSE final PC of every single-instruction case
// (PCXF-11) and jnext's Z80Cpu (FUSE 1356/1356) for where each swept opcode really lands (PCXF-12).
static Transfer xf(const Z80Registers& r, const Img& i) { return pc_transfer(r, reader(i), reader(i)); }
static bool is_xf(const Transfer& t, uint16_t target, Transfer::Dir d, bool hidden) {
    return t.taken && t.target == target && t.dir == d && t.hidden == hidden;
}
using Dir = Transfer::Dir;

static Row pcxf_unconditional() {
    Row row;
    {   Img i; i.put(0x8000, {0xC3, 0x34, 0x12}); row.need(is_xf(xf(regs(), i), 0x1234, Dir::Up, false), "JP nn"); }
    {   Img i; i.put(0x8000, {0xCD, 0x00, 0x90}); row.need(is_xf(xf(regs(), i), 0x9000, Dir::Down, false), "CALL nn"); }
    {   Img i; i.put(0x8000, {0x18, 0x05});       row.need(is_xf(xf(regs(), i), 0x8007, Dir::Down, false), "JR +5"); }
    for (int n = 0; n < 8; ++n) {
        Img i; i.m[0x8000] = static_cast<uint8_t>(0xC7 | (n << 3));
        row.need(is_xf(xf(regs(), i), static_cast<uint16_t>(n * 8), Dir::Up, false), "RST " + hx(n * 8));
    }
    return row;
}

static Row pcxf_conditional() {
    Row row;
    for (int cc = 0; cc < 8; ++cc)
        for (uint8_t f : flag_set(cc)) {
            Z80Registers r = regs(); r.AF = f;
            const bool t = cc_taken(cc, f);
            Img a; a.put(0x8000, {static_cast<uint8_t>(0xC2 | (cc << 3)), 0x34, 0x12});
            Img b; b.put(0x8000, {static_cast<uint8_t>(0xC4 | (cc << 3)), 0x00, 0x90});
            const std::string w = std::string(kCC[cc].name) + " F=" + hx(f);
            const Transfer ta = xf(r, a), tb = xf(r, b);
            row.need(ta.taken == t && (!t || is_xf(ta, 0x1234, Dir::Up, false)), "JP " + w);
            row.need(tb.taken == t && (!t || is_xf(tb, 0x9000, Dir::Down, false)), "CALL " + w);
            row.need(ta.taken == hint(r, a).taken, "JP agrees with pc_hint " + w);
            if (cc < 4) {
                Img c; c.put(0x8000, {static_cast<uint8_t>(0x20 | (cc << 3)), 0x05});
                const Transfer tc = xf(r, c);
                row.need(tc.taken == t && (!t || is_xf(tc, 0x8007, Dir::Down, false)), "JR " + w);
            }
        }
    for (int b : {2, 1}) {
        Z80Registers r = regs(); r.BC = static_cast<uint16_t>(b << 8);
        Img i; i.put(0x8000, {0x10, 0x05});
        const Transfer t = xf(r, i);
        row.need(t.taken == (b == 2) && (b != 2 || is_xf(t, 0x8007, Dir::Down, false)), "DJNZ B=" + std::to_string(b));
    }
    return row;
}

static Row pcxf_ret_cc() {
    Row row;
    for (int cc = 0; cc < 8; ++cc)
        for (uint8_t f : flag_set(cc)) {
            Z80Registers r = regs(); r.AF = f;
            Img i; i.m[0x8000] = static_cast<uint8_t>(0xC0 | (cc << 3)); i.put(0xA000, {0x34, 0x12});
            const Transfer t = xf(r, i);
            row.need(t.taken == cc_taken(cc, f), std::string("RET ") + kCC[cc].name + " F=" + hx(f));
            if (t.taken) row.need(is_xf(t, 0x1234, Dir::Up, false), "RET cc taken, not hidden");
            else row.need(t.target == 0 && !t.hidden && t.dir == Dir::Here, "RET cc not taken is empty");
        }
    return row;
}

static Row pcxf_returns() {
    Row row;
    std::vector<std::vector<uint8_t>> codes = {{0xC9}, {0xED, 0x4D}};
    for (uint8_t e : {0x45, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D}) codes.push_back({0xED, e});
    for (const auto& c : codes) {
        Img i; for (size_t k = 0; k < c.size(); ++k) i.m[0x8000 + k] = c[k];
        i.put(0xA000, {0x34, 0x12});
        row.need(is_xf(xf(regs(), i), 0x1234, Dir::Up, true), "return " + hx(c.back()));
        Img w; for (size_t k = 0; k < c.size(); ++k) w.m[0x8000 + k] = c[k];
        w.m[0xFFFF] = 0x34; w.m[0x0000] = 0x12;
        Z80Registers r = regs(); r.SP = 0xFFFF;
        row.need(is_xf(xf(r, w), 0x1234, Dir::Up, true), "return wrap " + hx(c.back()));
    }
    return row;
}

static Row pcxf_register_jumps() {
    Row row;
    { Img i; i.m[0x8000] = 0xE9;                 row.need(is_xf(xf(regs(), i), 0x3000, Dir::Up, true), "JP (HL)"); }
    { Img i; i.put(0x8000, {0xDD, 0xE9});        row.need(is_xf(xf(regs(), i), 0x4000, Dir::Up, true), "JP (IX)"); }
    { Img i; i.put(0x8000, {0xFD, 0xE9});        row.need(is_xf(xf(regs(), i), 0x5000, Dir::Up, true), "JP (IY)"); }
    return row;
}

static Row pcxf_not_transfers() {
    Row row;
    std::vector<std::vector<uint8_t>> ops = {{0xED, 0x98}, {0x76}, {0xDD, 0x76}, {0x00}, {0x7E}, {0xE5}};
    for (uint8_t e : {0xB0, 0xB1, 0xB2, 0xB3, 0xB8, 0xB9, 0xBA, 0xBB, 0xB4, 0xBC, 0xB6, 0xB7}) ops.push_back({0xED, e});
    for (const auto& o : ops) {
        Img i; for (size_t k = 0; k < o.size(); ++k) i.m[0x8000 + k] = o[k];
        const Transfer t = xf(regs(), i);
        row.need(!t.taken && !t.hidden, "not a transfer " + hx(o.back()));
    }
    return row;
}

static Row pcxf_direction() {
    Row row;
    {   Img i; i.put(0x8000, {0xC3, 0xFF, 0x7F}); row.need(xf(regs(), i).dir == Dir::Up, "$7FFF is Up"); }
    {   Img i; i.put(0x8000, {0xC3, 0x01, 0x80}); row.need(xf(regs(), i).dir == Dir::Down, "$8001 is Down"); }
    {   Img i; i.put(0x8000, {0x18, 0xFE});       row.need(xf(regs(), i).dir == Dir::Here, "JR $"); }
    {   Z80Registers r = regs(); r.BC = 0x0200;
        Img i; i.put(0x8000, {0x10, 0xFE});       row.need(xf(r, i).taken && xf(r, i).dir == Dir::Here, "DJNZ $"); }
    {   Img i; i.put(0x8000, {0xC3, 0x00, 0x80}); row.need(xf(regs(), i).dir == Dir::Here, "JP self"); }
    {   Z80Registers r = regs(); r.PC = 0xFFF0;
        Img i; i.put(0xFFF0, {0x18, 0x20});       row.need(is_xf(xf(r, i), 0x0012, Dir::Up, false), "JR wraps up"); }
    {   Z80Registers r = regs(); r.PC = 0x0005;
        Img i; i.put(0x0005, {0x18, 0xF0});       row.need(is_xf(xf(r, i), 0xFFF7, Dir::Down, false), "JR wraps down"); }
    return row;
}

static Row pcxf_format() {
    Row row;
    const disasm_text::SymbolNamer none;
    const disasm_text::SymbolNamer namer = [](uint16_t a) -> std::optional<std::string> {
        if (a == 0x1234) return "ret_here";
        return std::nullopt;
    };
    Img ret; ret.m[0x8000] = 0xC9; ret.put(0xA000, {0x34, 0x12});
    row.need(format_transfer(xf(regs(), ret), none) == "[-> $1234]", "RET plain");
    row.need(format_transfer(xf(regs(), ret), namer) == "[-> ret_here]", "RET named");
    Img jx; jx.put(0x8000, {0xDD, 0xE9});
    row.need(format_transfer(xf(regs(), jx), none) == "[-> $4000]", "JP (IX) plain");
    Img jp; jp.put(0x8000, {0xC3, 0x34, 0x12});
    row.need(format_transfer(xf(regs(), jp), namer).empty(), "JP nn is empty");
    Img rz; rz.m[0x8000] = 0xC8; rz.put(0xA000, {0x34, 0x12});
    Z80Registers z = regs(); z.AF = 0x40;
    row.need(format_transfer(xf(z, rz), namer).empty(), "RET cc taken is empty");
    z.AF = 0x00;
    row.need(format_transfer(xf(z, rz), namer).empty(), "RET cc not taken is empty");
    return row;
}

static Row pcxf_prefixes() {
    Row row;
    {   Img i; i.put(0x8000, {0xDD, 0xC3, 0x34, 0x12}); row.need(is_xf(xf(regs(), i), 0x1234, Dir::Up, false), "DD JP nn"); }
    {   Img i; i.put(0x8000, {0xDD, 0x18, 0x05});       row.need(is_xf(xf(regs(), i), 0x8008, Dir::Down, false), "DD JR"); }
    {   Img i; i.put(0x8000, {0xDD, 0xC9}); i.put(0xA000, {0x34, 0x12});
        row.need(is_xf(xf(regs(), i), 0x1234, Dir::Up, true), "DD RET"); }
    {   Img i; i.put(0x8000, {0xDD, 0xFD, 0xE9}); row.need(is_xf(xf(regs(), i), 0x5000, Dir::Up, true), "DD FD JP (IY)"); }
    {   Img i; i.put(0x8000, {0xFD, 0xDD, 0xE9}); row.need(is_xf(xf(regs(), i), 0x4000, Dir::Up, true), "FD DD JP (IX)"); }
    {   Img i; for (int k = 0; k < 65; ++k) i.m[0x8000 + k] = 0xDD;
        i.put(0x8000 + 65, {0xC3, 0x34, 0x12});
        row.need(!xf(regs(), i).taken, "65-prefix chain is DB, no transfer"); }
    {   Img i; for (int k = 0; k < 64; ++k) i.m[0x8000 + k] = 0xDD;
        i.put(0x8000 + 64, {0xC3, 0x34, 0x12});
        row.need(is_xf(xf(regs(), i), 0x1234, Dir::Up, false), "64-prefix chain is still a JP"); }
    return row;
}

static Row pcxf_readers() {
    Row row;
    Img code; code.m[0x8000] = 0xC9;
    Img data; data.put(0xA000, {0x34, 0x12});
    row.need(is_xf(pc_transfer(regs(), reader(code), reader(data)), 0x1234, Dir::Up, true), "RET: opcode from code, stack from data");
    Img onlydata; onlydata.m[0x8000] = 0xC9;
    row.need(!pc_transfer(regs(), reader(Img()), reader(onlydata)).taken, "a C9 only in data is no transfer");
    return row;
}

static int g_x11_taken, g_x11_fall, g_x11_halt, g_x11_repeat, g_x11_multi;
static Row pcxf_fuse(const std::vector<FuseCase>& cases, const std::map<std::string, FuseExp>& exp) {
    Row row;
    for (const FuseCase& c : cases) {
        auto it = exp.find(c.name);
        if (it == exp.end()) { row.need(false, "no expected for " + c.name); continue; }
        if (c.limit != 1) { ++g_x11_multi; continue; }
        Img img; fuse_img(c, img);
        const Z80Registers r = fuse_regs(c);
        const Transfer t = xf(r, img);
        const uint16_t fall = static_cast<uint16_t>(r.PC + instruction_length(r.PC, reader(img)));
        const uint16_t fin = it->second.pc;
        uint16_t a = r.PC;
        while (img.m[a] == 0xDD || img.m[a] == 0xFD) ++a;
        const uint8_t op = img.m[a], ed = img.m[static_cast<uint16_t>(a + 1)];
        const bool rep = op == 0xED && (ed & 0xE4) == 0xA0 && (ed & 0x10);   // LDIR/CPIR/INIR/OTIR and the D forms
        if (t.taken) { ++g_x11_taken; row.need(fin == t.target, "case " + c.name + " final PC " + hx(fin) + " target " + hx(t.target)); }
        else if (op == 0x76) { ++g_x11_halt; }
        else if (rep) { ++g_x11_repeat; row.need(fin == r.PC || fin == fall, "case " + c.name + " repeat"); }
        else { ++g_x11_fall; row.need(fin == fall, "case " + c.name + " fall-through " + hx(fin) + " vs " + hx(fall)); }
    }
    return row;
}

static int g_x12_runs, g_x12_taken, g_x12_fall, g_x12_halt, g_x12_repeat, g_x12_jpc;
static Row pcxf_sweep() {
    Row row;
    RecMem mem; NullIo io; Z80Cpu cpu(mem, io);
    struct St { uint8_t f; uint16_t bc; };
    auto one = [&](std::initializer_list<uint8_t> code, const std::string& what) {
        for (St st : {St{0x00, 0x0200}, St{0xFF, 0x0100}}) {
            std::memset(mem.ram, 0, sizeof mem.ram);
            uint16_t a = 0x8000; for (uint8_t b : code) mem.ram[a++] = b;
            mem.ram[0xA000] = 0x00; mem.ram[0xA001] = 0x90;
            Z80Registers r{};
            r.PC = 0x8000; r.SP = 0xA000; r.AF = static_cast<uint16_t>(st.f << 0); r.BC = st.bc;
            r.HL = 0x3000; r.IX = 0x4000; r.IY = 0x5000;
            *fuse_z80_tstates_ptr() = 0;
            cpu.set_registers(r);
            RecMem& m = mem;
            const DisasmReadFn rdf = [&m](uint16_t x) -> uint8_t { return m.ram[x]; };
            const Transfer t = pc_transfer(r, rdf, rdf);
            const uint16_t fall = static_cast<uint16_t>(0x8000 + instruction_length(0x8000, rdf));
            uint16_t p = 0x8000;
            while (mem.ram[p] == 0xDD || mem.ram[p] == 0xFD) ++p;
            const uint8_t op = mem.ram[p], ed = mem.ram[static_cast<uint16_t>(p + 1)];
            const bool halt = op == 0x76;
            const bool jpc = op == 0xED && ed == 0x98;
            const bool rep = op == 0xED && (((ed & 0xE4) == 0xA0 && (ed & 0x10)) || ed == 0xB4 || ed == 0xBC || ed == 0xB6 || ed == 0xB7);
            cpu.execute();
            ++g_x12_runs;
            const std::string w = what + " F=" + hx(st.f) + " BC=" + hx(st.bc);
            if (t.taken) { ++g_x12_taken; row.need(cpu.pc() == t.target, w + " target " + hx(t.target) + " cpu " + hx(cpu.pc())); }
            else if (halt) { ++g_x12_halt; }
            else if (jpc) { ++g_x12_jpc; }
            else if (rep) { ++g_x12_repeat; row.need(cpu.pc() == 0x8000 || cpu.pc() == fall, w + " repeat " + hx(cpu.pc())); }
            else { ++g_x12_fall; row.need(cpu.pc() == fall, w + " fall-through " + hx(cpu.pc()) + " vs " + hx(fall)); }
        }
    };
    for (int op = 0; op < 256; ++op) one({static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "plain " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xCB, static_cast<uint8_t>(op)}, "CB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xED, static_cast<uint8_t>(op), 0x05, 0x70}, "ED " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, static_cast<uint8_t>(op), 0x05, 0x70, 0x00}, "DD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, static_cast<uint8_t>(op), 0xFB, 0x70, 0x00}, "FD " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xDD, 0xCB, 0x05, static_cast<uint8_t>(op)}, "DDCB " + hx(op));
    for (int op = 0; op < 256; ++op) one({0xFD, 0xCB, 0xFB, static_cast<uint8_t>(op)}, "FDCB " + hx(op));
    return row;
}

int main(int argc, char** argv) {
    std::printf("\n======================================================\n");
    std::printf("Disassembly PC-line hint tests (GH #367)\n");
    std::printf("======================================================\n\n");

    check("DHINT-01", "JP NZ,nn: taken iff Z clear (UM0080 cc table)", jp_cc(0));
    check("DHINT-02", "JP Z,nn: taken iff Z set", jp_cc(1));
    check("DHINT-03", "JP NC,nn: taken iff C clear", jp_cc(2));
    check("DHINT-04", "JP C,nn: taken iff C set", jp_cc(3));
    check("DHINT-05", "JP PO,nn: taken iff P/V clear", jp_cc(4));
    check("DHINT-06", "JP PE,nn: taken iff P/V set", jp_cc(5));
    check("DHINT-07", "JP P,nn: taken iff S clear", jp_cc(6));
    check("DHINT-08", "JP M,nn: taken iff S set; the other flags never change an answer", jp_cc(7));
    check("DHINT-09", "JR NZ/Z/NC/C: the four conditions, target PC+2+d", jr_cc());
    check("DHINT-10", "DJNZ: taken iff B-1 (mod 256) != 0, target PC+2+d", djnz());
    check("DHINT-11", "CALL cc / RET cc: all eight conditions both ways; RET target is the word at SP (wraps)", call_ret_cc());
    check("DHINT-12", "no Branch hint for JP nn, JR e, CALL nn, RET, RETI/RETN, RST, JP (HL/IX/IY/C), HALT", none_list());
    check("DHINT-13", "the (HL) byte of every LD r,(HL), LD (HL),r, LD (HL),n, ALU, INC/DEC and CB x6/xE; 0x76 is none", hl_forms());
    check("DHINT-14", "(BC) / (DE) byte of LD (BC),A, LD A,(BC), LD (DE),A, LD A,(DE)", bc_de());
    check("DHINT-15", "LD (nn),A / LD A,(nn): one byte at nn", nn_byte());
    check("DHINT-16", "LD (nn),rr / LD rr,(nn) (plain, ED, DD/FD): a little-endian word, the high byte wraps at 64K", nn_word());
    check("DHINT-17", "every DD/FD (IX/IY+d) byte form, d = +5 / -128 / -1, wrapping at 64K", indexed_bytes());
    check("DHINT-18", "DD CB d op / FD CB d op, all 256 sub-opcodes: IX/IY+d with d at PC+2", indexed_cb());
    check("DHINT-19", "EX (SP),HL / IX / IY: the word at SP", ex_sp());
    check("DHINT-20", "prefix chains: last prefix wins, ignored prefixes, target from the inner address, 64-prefix limit as the text", prefixes());
    check("DHINT-21", "ports, block ops, RRD/RLD, PUSH/POP and the 30 Z80N ED ops: no hint and no data read", no_data_reads());
    check("DHINT-22", "the code and data readers are kept apart", split_readers());
    check("DHINT-23", "format_hint exact strings, no namer", fmt_plain());
    check("DHINT-24", "format_hint names the address and the RET target, never the value", fmt_named());

    // FUSE oracle
    std::vector<FuseCase> cases; std::map<std::string, FuseExp> exp;
    Row r25, r26, r27;
    const bool loaded = argc > 1 && load_fuse(argv[1], cases, exp);
    r25.need(loaded, "FUSE data not loaded (argv[1] = directory of tests.in)");
    if (loaded) fuse_rows(cases, exp, r25, r26, r27);
    r25.need(g_f25_total == 56 && g_f25_taken == 28 && g_f25_ambig == 0,
             "branch cases " + std::to_string(g_f25_total) + " taken " + std::to_string(g_f25_taken) + " ambiguous " + std::to_string(g_f25_ambig));
    r26.need(g_f26_total == 650 && g_f26_mr == 614,
             "memory cases " + std::to_string(g_f26_total) + " with a first MR " + std::to_string(g_f26_mr));
    r27.need(g_f27_named == 650 && g_f27_none == 608 && g_f27_stack == 46 && g_f27_block == 27 && g_f27_rrd == 2 &&
                 g_f27_unexpl == 0 && g_f27_multi == 23,
             "classes named " + std::to_string(g_f27_named) + " none " + std::to_string(g_f27_none) + " stack " +
                 std::to_string(g_f27_stack) + " block " + std::to_string(g_f27_block) + " rrd " + std::to_string(g_f27_rrd) +
                 " unexplained " + std::to_string(g_f27_unexpl) + " multi " + std::to_string(g_f27_multi));
    check("DHINT-25", "FUSE oracle: the next PC of every conditional-branch case agrees with taken/target (56: 28 taken, 28 not)", r25);
    check("DHINT-26", "FUSE oracle: the first data events of every named-operand case are the hint's address (650, 614 reads)", r26);
    check("DHINT-27", "FUSE oracle: every single-instruction data access is a named operand or an implicit class (1333 + 23 multi)", r27);

    Row r28 = sweep_branches();
    r28.need(g_s28_cases == 22272, "cases " + std::to_string(g_s28_cases));
    check("DHINT-28", "Z80Cpu sweep: every conditional opcode (also DD/FD-prefixed) x 256 flag or B values (22272 runs)", r28);
    Row r29 = sweep_memory();
    // Per class, derived by hand: 512 prefixed opcodes + 30 Z80N = 542. Named: 35 per prefix (25 (IX+d) byte forms,
    // DD CB, EX (SP), LD (nn),IX / IX,(nn), and the six BC/DE/nn forms the prefix ignores) = 70. Stack: 26 per
    // prefix (8 PUSH/POP, CALL, RET, 8 RST, and the 4 + 4 CALL/RET cc taken at F=0) = 52. Z80N with a data access: 9.
    r29.need(g_s29_total == 542 && g_s29_named == 70 && g_s29_stack == 52 && g_s29_z80n == 9 && g_s29_silent == 411,
             "sweep classes named " + std::to_string(g_s29_named) + " stack " + std::to_string(g_s29_stack) + " z80n " +
                 std::to_string(g_s29_z80n) + " silent " + std::to_string(g_s29_silent) + " total " + std::to_string(g_s29_total));
    check("DHINT-29", "Z80Cpu sweep: DD/FD 00-FF and the 30 Z80N ED ops access exactly the named operand, else an implicit class", r29);
    std::printf("  sweep classes: named %d silent %d stack %d z80n %d total %d\n", g_s29_named, g_s29_silent, g_s29_stack,
                g_s29_z80n, g_s29_total);
    Row r30 = text_agreement();
    r30.need(g_t30_cases == 1792, "opcode cases " + std::to_string(g_t30_cases));
    check("DHINT-30", "text agreement with disasm_one() over the plain, CB, ED, DD, FD, DDCB and FDCB tables (1792 opcodes)", r30);

    check("FOLW-01", "Follow: JP nn, CALL nn, JR e and DJNZ (d = +5, -2) at+2+d, RST 00..38", folw_unconditional());
    check("FOLW-02", "Follow: JP cc / CALL cc / JR cc go to the operand with the flags clear and set", folw_conditional());
    check("FOLW-03", "Follow: JP (HL) / (IX) / (IY) are the register; JP (C) has no target", folw_register_jumps());
    check("FOLW-04", "Follow: RET, RET cc, RETI, RETN and the aliases are the word at SP, wrapping at 64K", folw_returns());
    check("FOLW-05", "Follow decodes at `at`, never reading the code at r.PC", folw_at());
    // Counts derived by hand from UM0080: Code opcodes 42 transfers (JP nn, CALL nn, JR, DJNZ, 8 RST, RET, JP (HL), 8 JP cc,
    // 8 CALL cc, 4 JR cc, 8 RET cc) plain, behind DD and behind FD = 126, + 8 ED RETI/RETN forms = 134 (FOLW-09).
    // Data: 34 plain + 32 CB + 8 ED + 35 per prefix (70) + 256 per DDCB/FDCB (512) = 656 (FOLW-06). None: 1792 - 134 - 656 = 1002
    // (FOLW-07). FOLW-08 runs each Code opcode once with the condition holding: 14 unconditional + 28 conditional per
    // variant x 3 + 8 ED = 134.
    Row f6 = folw_data_set();
    f6.need(g_f6_cases == 1792 && g_f6_data == 656, "cases " + std::to_string(g_f6_cases) + " data " + std::to_string(g_f6_data));
    check("FOLW-06", "Follow: Data iff pc_hint is Memory, with its address, over the 1792 table opcodes", f6);
    Row f7 = folw_none_and_text(false);
    f7.need(g_f7_none == 1002, "none opcodes " + std::to_string(g_f7_none));
    check("FOLW-07", "Follow: None, with no data read, for every opcode that is neither a transfer nor a memory operand", f7);
    Row f8 = folw_sweep();
    f8.need(g_f8_runs == 134, "runs " + std::to_string(g_f8_runs));
    check("FOLW-08", "Follow: Z80Cpu executes every transfer (also behind DD/FD) and lands on the followed address", f8);
    Row f9 = folw_none_and_text(true);
    f9.need(g_f9_cases == 1792 && g_f9_code == 134, "cases " + std::to_string(g_f9_cases) + " code " + std::to_string(g_f9_code));
    check("FOLW-09", "Follow: Code iff disasm_one()'s text is a JP/JR/CALL/DJNZ/RST/RET (not JP (C)), over 1792 opcodes", f9);

    check("PCXF-01", "pc_transfer: JP nn, CALL nn, JR e and the 8 RSTs are taken, with target and direction, not hidden", pcxf_unconditional());
    check("PCXF-02", "pc_transfer: JP cc / CALL cc / JR cc / DJNZ taken iff their condition holds, target as pc_hint", pcxf_conditional());
    check("PCXF-03", "pc_transfer: RET cc taken iff its condition holds, target the word at SP, not hidden (the hint names it)", pcxf_ret_cc());
    check("PCXF-04", "pc_transfer: RET, RETI, RETN and the aliases: the word at SP (wrapping at 64K), hidden", pcxf_returns());
    check("PCXF-05", "pc_transfer: JP (HL) / (IX) / (IY): the register, hidden", pcxf_register_jumps());
    check("PCXF-06", "pc_transfer: JP (C), HALT, the repeating block ops, NOP, LD A,(HL) and PUSH are no transfer", pcxf_not_transfers());
    check("PCXF-07", "pc_transfer: direction is the unsigned compare (Up, Down, Here, wrapping JR)", pcxf_direction());
    check("PCXF-08", "format_transfer: exact [-> T] strings, named or $XXXX, empty unless taken and hidden", pcxf_format());
    check("PCXF-09", "pc_transfer: DD/FD prefixes (last wins, ignored by JP/JR/RET, 64 vs 65 chain)", pcxf_prefixes());
    check("PCXF-10", "pc_transfer: the opcode is read through code, the stack word through data", pcxf_readers());
    Row x11 = pcxf_fuse(cases, exp);
    x11.need(loaded, "FUSE data not loaded");
    // 1333 single-instruction cases (DHINT-27's domain) = 51 taken + 1281 fall-through + 1 HALT. The block ops are all
    // multi-instruction FUSE cases (a tstate limit above 1), so the repeat class is empty here; the 23 multi are the domain bound.
    // 51 taken = DHINT-25's 28 taken conditionals + 23 unconditional transfers.
    x11.need(g_x11_taken == 51 && g_x11_fall == 1281 && g_x11_halt == 1 && g_x11_repeat == 0 && g_x11_multi == 23,
             "taken " + std::to_string(g_x11_taken) + " fall " + std::to_string(g_x11_fall) + " halt " + std::to_string(g_x11_halt) +
             " repeat " + std::to_string(g_x11_repeat) + " multi " + std::to_string(g_x11_multi));
    check("PCXF-11", "FUSE oracle: every single-instruction case ends at pc_transfer's target, or falls through, or is HALT / a block repeat", x11);
    // 3584 = 1792 opcodes x 2 states. taken 181 = per state-pair 13 unconditional (JP, CALL, JR, RET, JP (HL), 8 RST) x 3 variants x 2
    // + DJNZ (B=2 only) x 3 + 8 ED RETI/RETN x 2 + 14 conditionals per state x 2 x 3 variants (28 JP/CALL/RET cc + 4 JR cc, half hold per state).
    // halt 6 = (76, DD 76, FD 76) x 2; jpc 2 = ED 98 x 2; repeat 24 = (ED B0-B3, B8-BB, LDIRX, LDDRX, LDIRSCALE, LDPIRX) 12 x 2.
    Row x12 = pcxf_sweep();
    x12.need(g_x12_runs == 3584 && g_x12_taken == 181 && g_x12_fall == 3371 && g_x12_halt == 6 &&
                 g_x12_repeat == 24 && g_x12_jpc == 2,
             "runs " + std::to_string(g_x12_runs) + " taken " + std::to_string(g_x12_taken) + " fall " + std::to_string(g_x12_fall) +
             " halt " + std::to_string(g_x12_halt) + " repeat " + std::to_string(g_x12_repeat) + " jpc " + std::to_string(g_x12_jpc));
    check("PCXF-12", "Z80Cpu sweep: every table opcode from two states lands on pc_transfer's target, or falls through, or is a named class (3584 runs)", x12);

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
