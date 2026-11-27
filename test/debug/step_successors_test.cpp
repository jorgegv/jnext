// step_successors(): where the CPU can be after one instruction (the addresses a remote step drops
// temporary breakpoints on, and the local Step Over's targets).
//
// Oracles, none of them the code under test:
//   * jnext's Z80Cpu (FUSE 1356/1356, Z80N 85), executed ONCE per case on a flat 64 KB memory with a
//     stub IO: the landing PC must be a member of the set, and for the tight rows exactly the member the
//     Zilog UM0080 condition-code table names.
//   * VHDL t80n_mcode.vhd:2419-2451 and zxnext.vhd:2052,2072-2081 for the stackless-NMI return (RETN
//     substitutes C3:C2, RETI does not).
//   * The esxDOS calling convention (RST $08, DEFB function; execution resumes after the DEFB).
// Every sweep runs each opcode from states taking and not taking each branch (F = 00 and FF; BC = 1 and
// 0x0102; A equal to (HL) and not), at a PC mid-memory and at the top of the address space.
//
// Run: ./build/test/step_successors_test

#include "debug/step_successors.h"
#include "cpu/z80_cpu.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::dbg::StepKind;
using jnext::dbg::StepSuccessors;
using jnext::dbg::step_successors;

static int g_total = 0, g_pass = 0, g_fail = 0;

struct Row {
    bool ok = true;
    int bad = 0;
    std::string why;
    void need(bool c, const std::string& w) {
        if (c) return;
        ok = false;
        if (++bad <= 3) why += (why.empty() ? "" : " | ") + w;
    }
};

static void check(const char* id, const char* desc, const Row& r) {
    report_row_id(id);
    ++g_total;
    if (r.ok) { ++g_pass; return; }
    ++g_fail;
    std::printf("  FAIL %s: %s (%d bad; first: %s)\n", id, desc, r.bad, r.why.c_str());
}

static std::string hx(unsigned v) { char b[16]; std::snprintf(b, sizeof b, "$%X", v); return b; }
static std::string bytes_str(const std::vector<uint8_t>& c) {
    std::string s;
    for (size_t i = 0; i < c.size() && i < 8; ++i) s += (i ? " " : "") + hx(c[i]);
    return s;
}

// ---- the machine ---------------------------------------------------------------------------
struct Mem : MemoryInterface {
    uint8_t ram[65536];
    Mem() { std::memset(ram, 0, sizeof ram); }
    uint8_t read(uint16_t a) override { return ram[a]; }
    void write(uint16_t a, uint8_t v) override { ram[a] = v; }
};
struct Io : IoInterface {
    int calls = 0;
    uint8_t in(uint16_t) override { ++calls; return 0xFF; }
    void out(uint16_t, uint8_t) override { ++calls; }
};
struct Rig {
    Mem mem;
    Io  io;
    Z80Cpu cpu{mem, io};
    DisasmReadFn rd() { return [this](uint16_t a) -> uint8_t { return mem.ram[a]; }; }
};

static constexpr uint16_t kStackWord = 0x9ABC;

struct State { uint8_t f; uint16_t bc; bool a_match; };
static const State kStates[] = {
    {0x00, 0x0001, false}, {0xFF, 0x0001, false}, {0x00, 0x0102, true}, {0xFF, 0x0102, true},
    {0x00, 0x0102, false}, {0xFF, 0x0102, false}, {0x00, 0x0001, true}, {0xFF, 0x0001, true},
};

static void put(Rig& g, uint16_t a, const std::vector<uint8_t>& b) {
    for (uint8_t v : b) g.mem.ram[a++] = v;
}

// Reset the machine for one case: code at `pc` (wrapping), the stack word at `sp`, (HL) = 0x55.
static Z80Registers setup(Rig& g, const std::vector<uint8_t>& code, uint16_t pc, uint16_t sp, const State& st) {
    std::memset(g.mem.ram, 0, sizeof g.mem.ram);
    g.mem.ram[0x3000] = 0x55;
    g.mem.ram[sp] = kStackWord & 0xFF;
    g.mem.ram[static_cast<uint16_t>(sp + 1)] = kStackWord >> 8;
    put(g, pc, code);
    Z80Registers r{};
    r.PC = pc; r.SP = sp;
    r.AF = static_cast<uint16_t>(((st.a_match ? 0x55 : 0x00) << 8) | st.f);
    r.BC = st.bc; r.DE = 0x2000; r.HL = 0x3000; r.IX = 0x4000; r.IY = 0x5000;
    r.IM = 1; r.IFF1 = r.IFF2 = 0;
    return r;
}

static void load(Rig& g, const Z80Registers& r) {
    *fuse_z80_tstates_ptr() = 0;
    g.cpu.set_registers(r);
}

static bool in_set(const StepSuccessors& s, uint16_t a) {
    for (uint8_t i = 0; i < s.count; ++i) if (s.addr[i] == a) return true;
    return false;
}
static std::string set_str(const StepSuccessors& s) {
    if (s.status != StepSuccessors::Status::Ok) return std::string("Unknowable: ") + s.why;
    std::string t = "{";
    for (uint8_t i = 0; i < s.count; ++i) t += (i ? "," : "") + hx(s.addr[i]);
    return t + "}";
}
static bool same_set(const StepSuccessors& a, const StepSuccessors& b) {
    if (a.status != b.status || a.count != b.count) return false;
    for (uint8_t i = 0; i < a.count; ++i) if (!in_set(b, a.addr[i])) return false;
    return true;
}

// UM0080 condition codes, index = bits 5-3: flag mask, taken when the flag is set.
struct CC { uint8_t mask; bool when_set; };
static const CC kCC[8] = {{0x40, false}, {0x40, true}, {0x01, false}, {0x01, true},
                          {0x04, false}, {0x04, true}, {0x80, false}, {0x80, true}};
static bool cc_taken(int cc, uint8_t f) { return ((f & kCC[cc].mask) != 0) == kCC[cc].when_set; }

// What the instruction at `c` is, by the Zilog tables, for the COUNT of Into successors: the bytes after
// the DD/FD chain. 0: not predicted here.
static int expected_into_count(const std::vector<uint8_t>& c) {
    size_t i = 0;
    while (i < c.size() && (c[i] == 0xDD || c[i] == 0xFD)) ++i;
    if (i + 1 >= c.size()) return 0;
    const uint8_t op = c[i], e = c[i + 1];
    if (op == 0xED) {
        if (e == 0x98) return 0;
        if ((e >= 0xB0 && e <= 0xB3) || (e >= 0xB8 && e <= 0xBB) || e == 0xB4 || e == 0xB6 || e == 0xB7 || e == 0xBC) return 2;
        return 1;
    }
    if (op == 0x10 || (op & 0xE7) == 0x20 || (op & 0xC7) == 0xC0 || (op & 0xC7) == 0xC2 || (op & 0xC7) == 0xC4) return 2;
    return 1;
}

// The case driver: from `code` at `pc` and the state, ask Into and Over, execute once, and say what is wrong.
// The landing must be an Into member (a HALT: the HALT byte itself, with the CPU halted, and the member
// is the next instruction; JP (C) is Unknowable). `tight_count`: the count must be the table's.
static std::string verify(Rig& g, const std::vector<uint8_t>& code, uint16_t pc, uint16_t sp, const State& st,
                          bool tight_count) {
    Z80Registers r = setup(g, code, pc, sp, st);
    const DisasmReadFn rd = g.rd();
    const StepSuccessors into = step_successors(r, rd, rd, StepKind::Into);

    size_t n = 0;
    while (n < code.size() && (code[n] == 0xDD || code[n] == 0xFD)) ++n;
    const uint8_t op = code[n], e = code[n + 1];

    if (op == 0xED && e == 0x98) {
        if (into.status == StepSuccessors::Status::Unknowable && into.why[0]) return "";
        return "JP (C) must be Unknowable with a reason";
    }
    if (into.status != StepSuccessors::Status::Ok) return "Unknowable: " + std::string(into.why);
    if (into.count < 1 || into.count > 2) return "count " + std::to_string(into.count);

    load(g, r);
    g.cpu.execute();
    const uint16_t land = g.cpu.pc();
    std::string out;
    if (op == 0x76) {
        const uint16_t halt_byte = static_cast<uint16_t>(pc + n);
        const uint16_t fall = static_cast<uint16_t>(pc + n + 1);
        if (!g.cpu.is_halted() || land != halt_byte) out = "HALT: PC " + hx(land) + " halted=" + std::to_string(g.cpu.is_halted());
        else if (into.count != 1 || into.addr[0] != fall) out = "HALT: set " + set_str(into) + " want {" + hx(fall) + "}";
        return out;
    }
    if (!in_set(into, land)) return "landing " + hx(land) + " not in " + set_str(into);
    if (tight_count) {
        const int want = expected_into_count(code);
        if (want && into.count != want) return "count " + std::to_string(into.count) + " want " + std::to_string(want) + " " + set_str(into);
    }
    return "";
}

static void sweep(Row& row, const std::vector<std::vector<uint8_t>>& codes, const std::vector<uint16_t>& pcs,
                  const std::vector<uint16_t>& sps, bool tight_count) {
    static Rig* g = new Rig;
    for (const auto& c : codes)
        for (uint16_t pc : pcs)
            for (uint16_t sp : sps)
                for (const State& st : kStates) {
                    const std::string w = verify(*g, c, pc, sp, st, tight_count);
                    row.need(w.empty(), bytes_str(c) + " @" + hx(pc) + " SP" + hx(sp) + " F" + hx(st.f) + " BC" + hx(st.bc) +
                                            (st.a_match ? " A=(HL)" : "") + ": " + w);
                }
}

static const std::vector<uint16_t> kPcs = {0x8000, 0xFFFE};
static const std::vector<uint16_t> kSps = {0xA000};

static const uint8_t kZ80nEd[30] = {
    0x23, 0x24, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x8A,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x98, 0xA4, 0xA5, 0xAC, 0xB4, 0xB6, 0xB7, 0xBC,
};
static bool is_z80n_ed(uint8_t e) { for (uint8_t z : kZ80nEd) if (z == e) return true; return false; }

// The code lists per group.
static std::vector<std::vector<uint8_t>> g_unprefixed() {
    std::vector<std::vector<uint8_t>> v;
    for (int b = 0; b < 256; ++b)
        if (b != 0xCB && b != 0xED && b != 0xDD && b != 0xFD) v.push_back({uint8_t(b), 0x34, 0x12, 0x00});
    return v;
}
static std::vector<std::vector<uint8_t>> g_cb() {
    std::vector<std::vector<uint8_t>> v;
    for (int b = 0; b < 256; ++b) v.push_back({0xCB, uint8_t(b), 0x34, 0x12});
    return v;
}
static std::vector<std::vector<uint8_t>> g_ed(bool z80n) {
    std::vector<std::vector<uint8_t>> v;
    for (int e = 0; e < 256; ++e)
        if (is_z80n_ed(uint8_t(e)) == z80n) v.push_back({0xED, uint8_t(e), 0x34, 0x12, 0x56});
    return v;
}
static std::vector<std::vector<uint8_t>> g_dd(uint8_t p) {
    std::vector<std::vector<uint8_t>> v;
    for (int b = 0; b < 256; ++b)
        if (b != 0xCB && b != 0xED && b != 0xDD && b != 0xFD) v.push_back({p, uint8_t(b), 0x05, 0x12, 0x00});
    return v;
}
static std::vector<std::vector<uint8_t>> g_ddcb(uint8_t p) {
    std::vector<std::vector<uint8_t>> v;
    for (int d : {0x05, 0x80})
        for (int b = 0; b < 256; ++b) v.push_back({p, 0xCB, uint8_t(d), uint8_t(b), 0x00});
    return v;
}
static std::vector<std::vector<uint8_t>> g_prefixed_ed(uint8_t p) {
    std::vector<std::vector<uint8_t>> v;
    for (int e = 0; e < 256; ++e) v.push_back({p, 0xED, uint8_t(e), 0x34, 0x12, 0x56});
    return v;
}
static std::vector<std::vector<uint8_t>> g_chains() {
    std::vector<std::vector<uint8_t>> v = {
        {0xDD, 0xDD, 0xC9}, {0xFD, 0xDD, 0xC9}, {0xDD, 0xFD, 0xC9}, {0xDD, 0xDD, 0xE9}, {0xFD, 0xDD, 0xE9},
        {0xDD, 0xFD, 0xE9}, {0xFD, 0xFD, 0xE9}, {0xDD, 0xDD, 0x21, 0x34, 0x12}, {0xDD, 0xFD, 0x20, 0x05},
        {0xFD, 0xDD, 0xC2, 0x34, 0x12}, {0xDD, 0xFD, 0xCD, 0x34, 0x12},
        {0xDD, 0xDD, 0xED, 0xB0}, {0xFD, 0xDD, 0xED, 0xB0}, {0xDD, 0xFD, 0x76},
    };
    for (int y = 0; y < 256; ++y) v.push_back({0xDD, 0xDD, 0xCB, 0x05, uint8_t(y)});
    for (int n : {1, 2, 3, 62, 63, 64, 65, 66, 70, 200})
        for (const std::vector<uint8_t>& tail : std::vector<std::vector<uint8_t>>{
                 {0x00}, {0xC9}, {0xE9}, {0x20, 0x05}, {0xC2, 0x34, 0x12}, {0x21, 0x34, 0x12}, {0x76},
                 {0xED, 0xB0}, {0xED, 0x45}, {0xCB, 0x05, 0x46}, {0xC8}, {0x10, 0x05}}) {
            std::vector<uint8_t> c;
            for (int i = 0; i < n; ++i) c.push_back((i & 1) ? 0xFD : 0xDD);
            for (uint8_t t : tail) c.push_back(t);
            c.push_back(0x00); c.push_back(0x00);
            v.push_back(c);
        }
    return v;
}

// ---- SSC-01..08 the sweeps ----------------------------------------------------------------
static Row ssc_01() { Row r; sweep(r, g_unprefixed(), kPcs, kSps, true); return r; }
static Row ssc_02() { Row r; sweep(r, g_cb(), kPcs, kSps, true); return r; }
static Row ssc_03() { Row r; sweep(r, g_ed(false), kPcs, kSps, true); return r; }
static Row ssc_04() { Row r; sweep(r, g_ed(true), kPcs, kSps, true); return r; }
static Row ssc_05() { Row r; sweep(r, g_dd(0xDD), kPcs, kSps, true); return r; }
static Row ssc_06() { Row r; sweep(r, g_dd(0xFD), kPcs, kSps, true); return r; }
static Row ssc_07() { Row r; sweep(r, g_ddcb(0xDD), kPcs, kSps, true); return r; }
static Row ssc_08() { Row r; sweep(r, g_ddcb(0xFD), kPcs, kSps, true); return r; }
static Row ssc_09() {
    Row r;
    sweep(r, g_chains(), kPcs, kSps, true);
    sweep(r, g_prefixed_ed(0xDD), kPcs, kSps, false);
    sweep(r, g_prefixed_ed(0xFD), kPcs, kSps, false);
    return r;
}

// ---- SSC-10 tight conditionals ------------------------------------------------------------
static Row ssc_10() {
    Row row;
    Rig* g = new Rig;
    const uint8_t prefixes[3] = {0x00, 0xDD, 0xFD};
    for (uint16_t pc : kPcs)
        for (uint8_t pv : prefixes)
            for (int cc = 0; cc < 8; ++cc)
                for (uint8_t f : {uint8_t(0x00), kCC[cc].mask, uint8_t(0xFF ^ kCC[cc].mask), uint8_t(0xFF)})
                    for (int kind = 0; kind < 4; ++kind) {
                        if (kind == 3 && cc >= 4) continue;          // JR cc: NZ Z NC C only
                        std::vector<uint8_t> c;
                        if (pv) c.push_back(pv);
                        const uint16_t base = static_cast<uint16_t>(pc + (pv ? 1 : 0));
                        uint16_t fall, target;
                        if (kind == 0) { c.insert(c.end(), {uint8_t(0xC2 | (cc << 3)), 0x34, 0x12}); fall = base + 3; target = 0x1234; }
                        else if (kind == 1) { c.insert(c.end(), {uint8_t(0xC4 | (cc << 3)), 0x34, 0x12}); fall = base + 3; target = 0x1234; }
                        else if (kind == 2) { c.push_back(uint8_t(0xC0 | (cc << 3))); fall = base + 1; target = kStackWord; }
                        else { c.insert(c.end(), {uint8_t(0x20 | (cc << 3)), 0x05}); fall = base + 2; target = static_cast<uint16_t>(base + 2 + 5); }
                        const State st{f, 0x0001, false};
                        Z80Registers r = setup(*g, c, pc, 0xA000, st);
                        const DisasmReadFn rd = g->rd();
                        const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into);
                        load(*g, r);
                        g->cpu.execute();
                        const uint16_t want = cc_taken(cc, f) ? target : fall;
                        row.need(s.status == StepSuccessors::Status::Ok && s.count == 2 && in_set(s, fall) && in_set(s, target) &&
                                     g->cpu.pc() == want,
                                 bytes_str(c) + " F" + hx(f) + " land " + hx(g->cpu.pc()) + " want " + hx(want) + " set " + set_str(s));
                    }
    // DJNZ: taken iff B - 1 != 0 (UM0080)
    for (uint16_t pc : kPcs)
        for (uint8_t pv : prefixes)
            for (int b : {0, 1, 2, 0x80}) {
                std::vector<uint8_t> c;
                if (pv) c.push_back(pv);
                const uint16_t base = static_cast<uint16_t>(pc + (pv ? 1 : 0));
                c.insert(c.end(), {0x10, 0x05});
                const State st{0x00, static_cast<uint16_t>(b << 8), false};
                Z80Registers r = setup(*g, c, pc, 0xA000, st);
                const DisasmReadFn rd = g->rd();
                const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into);
                load(*g, r);
                g->cpu.execute();
                const uint16_t fall = base + 2, target = static_cast<uint16_t>(base + 2 + 5);
                const uint16_t want = ((b - 1) & 0xFF) != 0 ? target : fall;
                row.need(s.count == 2 && in_set(s, fall) && in_set(s, target) && g->cpu.pc() == want,
                         "DJNZ B=" + hx(b) + " land " + hx(g->cpu.pc()) + " want " + hx(want) + " set " + set_str(s));
            }
    delete g;
    return row;
}

// ---- SSC-11 / 12 / 13 Over ----------------------------------------------------------------
static Row ssc_11() {
    Row row;
    Rig* g = new Rig;
    for (uint16_t pc : kPcs) {
        for (int cc = 0; cc < 8; ++cc)
            for (uint8_t f : {uint8_t(0x00), uint8_t(0xFF)}) {          // CALL cc, both ways
                const std::vector<uint8_t> c = {uint8_t(0xC4 | (cc << 3)), 0x34, 0x12};
                const State st{f, 0x0001, false};
                Z80Registers r = setup(*g, c, pc, 0xA000, st);
                const DisasmReadFn rd = g->rd();
                const StepSuccessors o = step_successors(r, rd, rd, StepKind::Over);
                const uint16_t fall = static_cast<uint16_t>(pc + 3);
                load(*g, r);
                g->cpu.execute();
                const bool taken = cc_taken(cc, f);
                const uint16_t pushed = static_cast<uint16_t>(g->mem.ram[0x9FFE] | (g->mem.ram[0x9FFF] << 8));
                row.need(o.status == StepSuccessors::Status::Ok && o.count == 1 && o.addr[0] == fall &&
                             (taken ? pushed == o.addr[0] : g->cpu.pc() == o.addr[0]),
                         "CALL cc" + std::to_string(cc) + " F" + hx(f) + " set " + set_str(o));
            }
        {   // CALL nn
            const std::vector<uint8_t> c = {0xCD, 0x34, 0x12};
            Z80Registers r = setup(*g, c, pc, 0xA000, kStates[0]);
            const DisasmReadFn rd = g->rd();
            const StepSuccessors o = step_successors(r, rd, rd, StepKind::Over);
            load(*g, r);
            g->cpu.execute();
            const uint16_t pushed = static_cast<uint16_t>(g->mem.ram[0x9FFE] | (g->mem.ram[0x9FFF] << 8));
            row.need(o.count == 1 && pushed == o.addr[0] && o.addr[0] == static_cast<uint16_t>(pc + 3), "CALL nn " + set_str(o));
        }
        for (int p = 0; p < 8; ++p) {   // RST p
            const std::vector<uint8_t> c = {uint8_t(0xC7 | (p << 3)), 0x00, 0x00};
            Z80Registers r = setup(*g, c, pc, 0xA000, kStates[0]);
            const DisasmReadFn rd = g->rd();
            const StepSuccessors o = step_successors(r, rd, rd, StepKind::Over);
            load(*g, r);
            g->cpu.execute();
            const uint16_t pushed = static_cast<uint16_t>(g->mem.ram[0x9FFE] | (g->mem.ram[0x9FFF] << 8));
            const bool rst08 = p == 1;
            row.need(o.status == StepSuccessors::Status::Ok && o.count == (rst08 ? 2 : 1) && in_set(o, pushed) &&
                         (rst08 ? in_set(o, static_cast<uint16_t>(pc + 2)) : o.addr[0] == static_cast<uint16_t>(pc + 1)),
                     "RST " + hx(p * 8) + " " + set_str(o));
        }
        for (int b : {0, 1, 2}) {   // DJNZ: Over runs the loop out: {pc + 2}
            const std::vector<uint8_t> c = {0x10, 0x05};
            const State st{0x00, static_cast<uint16_t>(b << 8), false};
            Z80Registers r = setup(*g, c, pc, 0xA000, st);
            const DisasmReadFn rd = g->rd();
            const StepSuccessors o = step_successors(r, rd, rd, StepKind::Over);
            row.need(o.status == StepSuccessors::Status::Ok && o.count == 1 && o.addr[0] == static_cast<uint16_t>(pc + 2),
                     "DJNZ Over " + set_str(o));
        }
    }
    delete g;
    return row;
}

static Row ssc_12() {
    Row row;
    Rig* g = new Rig;
    std::vector<std::vector<uint8_t>> all = g_unprefixed();
    for (const auto& c : g_cb()) all.push_back(c);
    for (const auto& c : g_ed(false)) all.push_back(c);
    for (const auto& c : g_ed(true)) all.push_back(c);
    for (uint8_t p : {uint8_t(0xDD), uint8_t(0xFD)}) {
        for (const auto& c : g_dd(p)) all.push_back(c);
        for (const auto& c : g_ddcb(p)) all.push_back(c);
        for (const auto& c : g_prefixed_ed(p)) all.push_back(c);
    }
    int compared = 0;
    for (const auto& c : all)
        for (uint16_t pc : kPcs)
            for (const State& st : {kStates[0], kStates[5]}) {
                Z80Registers r = setup(*g, c, pc, 0xA000, st);
                const DisasmReadFn rd = g->rd();
                if (is_call_like(pc, rd)) continue;                    // the call-like ones: SSC-11
                const StepSuccessors i = step_successors(r, rd, rd, StepKind::Into);
                const StepSuccessors o = step_successors(r, rd, rd, StepKind::Over);
                ++compared;
                row.need(same_set(i, o), bytes_str(c) + " Into " + set_str(i) + " Over " + set_str(o));
            }
    row.need(compared > 4000, "compared only " + std::to_string(compared));
    delete g;
    return row;
}

static Row ssc_13() {
    Row row;
    Rig* g = new Rig;
    for (uint16_t pc : {uint16_t(0x8000), uint16_t(0xFFFE)}) {
        const std::vector<uint8_t> c = {0xCF, 0x9A, 0x00};          // RST $08 ; DEFB $9A ; NOP
        Z80Registers r = setup(*g, c, pc, 0xA000, kStates[0]);
        const DisasmReadFn rd = g->rd();
        const StepSuccessors o = step_successors(r, rd, rd, StepKind::Over);
        const StepSuccessors i = step_successors(r, rd, rd, StepKind::Into);
        row.need(o.status == StepSuccessors::Status::Ok && o.count == 2 && in_set(o, static_cast<uint16_t>(pc + 1)) &&
                     in_set(o, static_cast<uint16_t>(pc + 2)),
                 "Over " + set_str(o));
        row.need(i.count == 1 && i.addr[0] == 0x0008, "Into " + set_str(i));
        load(*g, r);
        g->cpu.execute();
        row.need(g->cpu.pc() == 0x0008, "Into lands on $0008, got " + hx(g->cpu.pc()));
        // the esxDOS stand-in: the second execute() is the intercept, which returns past the DEFB
        g->cpu.on_esxdos_call = [](uint8_t, Z80Registers&) { return true; };
        g->cpu.execute();
        row.need(g->cpu.pc() == static_cast<uint16_t>(pc + 2) && in_set(o, g->cpu.pc()),
                 "stand-in returns to pc+2, got " + hx(g->cpu.pc()));
        g->cpu.on_esxdos_call = nullptr;
    }
    delete g;
    return row;
}

// ---- SSC-14 HALT -------------------------------------------------------------------------
static Row ssc_14() {
    Row row;
    Rig* g = new Rig;
    for (const std::vector<uint8_t>& c : std::vector<std::vector<uint8_t>>{{0x76, 0x00}, {0xDD, 0x76, 0x00}, {0xFD, 0xDD, 0x76, 0x00}}) {
        const size_t n = c.size() - 2;                              // prefixes before the HALT
        const uint16_t pc = 0x8000, fall = static_cast<uint16_t>(pc + n + 1);
        Z80Registers r = setup(*g, c, pc, 0xA000, kStates[0]);
        r.IFF1 = r.IFF2 = 1; r.IM = 1;
        g->mem.ram[0x0038] = 0xC9;                                  // the IM 1 handler: RET
        const DisasmReadFn rd = g->rd();
        const StepSuccessors i = step_successors(r, rd, rd, StepKind::Into);
        const StepSuccessors o = step_successors(r, rd, rd, StepKind::Over);
        row.need(i.count == 1 && i.addr[0] == fall && same_set(i, o), bytes_str(c) + " Into " + set_str(i) + " Over " + set_str(o));
        load(*g, r);
        g->cpu.execute();
        row.need(g->cpu.is_halted() && g->cpu.pc() == static_cast<uint16_t>(pc + n), bytes_str(c) + " keeps PC on the HALT byte, got " + hx(g->cpu.pc()));
        g->cpu.request_interrupt(0xFF);
        g->cpu.execute();                                           // accepts the interrupt
        row.need(g->cpu.pc() == 0x0038, bytes_str(c) + " interrupt taken, PC " + hx(g->cpu.pc()));
        g->cpu.execute();                                           // RET
        row.need(g->cpu.pc() == fall && in_set(i, g->cpu.pc()), bytes_str(c) + " handler returns to " + hx(fall) + ", got " + hx(g->cpu.pc()));
    }
    delete g;
    return row;
}

// ---- SSC-15 JP (C) -----------------------------------------------------------------------
static Row ssc_15() {
    Row row;
    Rig* g = new Rig;
    for (uint16_t pc : kPcs)
        for (const std::vector<uint8_t>& c : std::vector<std::vector<uint8_t>>{{0xED, 0x98}, {0xDD, 0xED, 0x98}}) {
            Z80Registers r = setup(*g, c, pc, 0xA000, kStates[0]);
            const DisasmReadFn rd = g->rd();
            const int calls = g->io.calls;
            for (StepKind k : {StepKind::Into, StepKind::Over}) {
                const StepSuccessors s = step_successors(r, rd, rd, k);
                row.need(s.status == StepSuccessors::Status::Unknowable && s.count == 0 && std::strstr(s.why, "JP (C)") &&
                             !std::strstr(s.why, "#"),
                         bytes_str(c) + " " + set_str(s));
            }
            row.need(g->io.calls == calls, "the function read a port");
        }
    delete g;
    return row;
}

// ---- SSC-16 / 17 RETN / RETI and the stackless NMI ---------------------------------------
struct NrLog { uint8_t c0 = 0; int reads_c2c3 = 0; int reads_other = 0; };
static std::function<uint8_t(uint8_t)> nr_reader(NrLog& l) {
    return [&l](uint8_t n) -> uint8_t {
        if (n == 0xC0) return l.c0;
        if (n == 0xC2) { ++l.reads_c2c3; return 0x34; }
        if (n == 0xC3) { ++l.reads_c2c3; return 0x12; }
        ++l.reads_other;
        return 0;
    };
}

// Takes a stackless NMI (callbacks installed), leaves the CPU in the handler at 0x0066 with `op2` at it.
static void into_stackless_handler(Rig& g, uint8_t op2) {
    std::memset(g.mem.ram, 0, sizeof g.mem.ram);
    g.mem.ram[0xFFFC] = 0xCD; g.mem.ram[0xFFFD] = 0xAB;                 // the REAL stack word
    g.mem.ram[0x0066] = 0xED; g.mem.ram[0x0067] = op2;
    Z80Registers r{};
    r.PC = 0x8000; r.SP = 0xFFFE; r.IM = 1;
    load(g, r);
    g.cpu.stackless_nmi_enabled = []() { return true; };
    g.cpu.stackless_nmi_return_address = []() -> uint16_t { return 0x1234; };
    g.cpu.request_nmi();
    g.cpu.execute();                                                    // the stackless acknowledge
}

static Row ssc_16() {
    Row row;
    Rig* g = new Rig;
    for (uint8_t e : {0x45, 0x55, 0x5D, 0x65, 0x6D, 0x75, 0x7D}) {
        into_stackless_handler(*g, e);
        const Z80Registers r = g->cpu.get_registers();
        const DisasmReadFn rd = g->rd();
        NrLog on; on.c0 = 0x08;
        const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into, nr_reader(on));
        row.need(r.PC == 0x0066 && g->cpu.stackless_retn_active(), "ED " + hx(e) + ": in the stackless handler");
        row.need(s.status == StepSuccessors::Status::Ok && s.count == 2 && in_set(s, 0xABCD) && in_set(s, 0x1234),
                 "ED " + hx(e) + " latch on: " + set_str(s));
        g->cpu.execute();
        row.need(g->cpu.pc() == 0x1234 && in_set(s, g->cpu.pc()), "ED " + hx(e) + ": the core lands on C3:C2, got " + hx(g->cpu.pc()));
        // bit 3 clear: the stack word only, and C2/C3 are never read
        into_stackless_handler(*g, e);
        const Z80Registers r2 = g->cpu.get_registers();
        NrLog off; off.c0 = 0x00;
        const StepSuccessors s2 = step_successors(r2, rd, rd, StepKind::Into, nr_reader(off));
        row.need(s2.status == StepSuccessors::Status::Ok && s2.count == 1 && s2.addr[0] == 0xABCD && off.reads_c2c3 == 0,
                 "ED " + hx(e) + " bit 3 clear: " + set_str(s2) + " C2/C3 reads " + std::to_string(off.reads_c2c3));
        // no reader at all: the stack word only
        const StepSuccessors s3 = step_successors(r2, rd, rd, StepKind::Into);
        row.need(s3.count == 1 && s3.addr[0] == 0xABCD, "ED " + hx(e) + " no nextreg reader: " + set_str(s3));
        g->cpu.stackless_nmi_enabled = nullptr;
        g->cpu.stackless_nmi_return_address = nullptr;
    }
    // NextREGs are read at a RETN only: an ordinary instruction never asks
    {
        const std::vector<uint8_t> c = {0x00};
        Z80Registers r = setup(*g, c, 0x8000, 0xA000, kStates[0]);
        const DisasmReadFn rd = g->rd();
        NrLog l; l.c0 = 0x08;
        (void)step_successors(r, rd, rd, StepKind::Into, nr_reader(l));
        row.need(l.reads_c2c3 == 0 && l.reads_other == 0, "a NOP read a NextREG");
    }
    delete g;
    return row;
}

static Row ssc_17() {
    Row row;
    Rig* g = new Rig;
    into_stackless_handler(*g, 0x4D);                                   // RETI at the handler
    const Z80Registers r = g->cpu.get_registers();
    const DisasmReadFn rd = g->rd();
    NrLog on; on.c0 = 0x08;
    const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into, nr_reader(on));
    row.need(r.PC == 0x0066 && g->cpu.stackless_retn_active(), "in the stackless handler");
    row.need(s.status == StepSuccessors::Status::Ok && s.count == 1 && s.addr[0] == 0xABCD && on.reads_c2c3 == 0,
             "RETI with the latch active: " + set_str(s));
    g->cpu.execute();
    row.need(g->cpu.pc() == 0xABCD, "the core lands on the stack word, got " + hx(g->cpu.pc()));
    delete g;
    return row;
}

// ---- SSC-18 the top of the address space -------------------------------------------------
static Row ssc_18() {
    Row row;
    const std::vector<uint16_t> pcs = {0xFFFD, 0xFFFE, 0xFFFF};
    const std::vector<uint16_t> sps = {0xFFFF, 0xFFFE, 0x0000};
    std::vector<std::vector<uint8_t>> all = g_unprefixed();
    for (const auto& c : g_cb()) all.push_back(c);
    for (const auto& c : g_ed(false)) all.push_back(c);
    for (const auto& c : g_ed(true)) all.push_back(c);
    for (uint8_t p : {uint8_t(0xDD), uint8_t(0xFD)}) {
        for (const auto& c : g_dd(p)) all.push_back(c);
        for (const auto& c : g_ddcb(p)) all.push_back(c);
    }
    sweep(row, all, pcs, sps, false);
    return row;
}

// ---- SSC-19 duplicates -------------------------------------------------------------------
static Row ssc_19() {
    Row row;
    Rig* g = new Rig;
    struct Case { std::vector<uint8_t> c; uint8_t f; uint16_t want; };
    const uint16_t pc = 0x8000;
    const Case cases[] = {
        {{0x20, 0x00}, 0x00, 0x8002},              // JR NZ,$+2
        {{0x18, 0x00}, 0x00, 0x8002},              // JR $+2 (unconditional: one anyway)
        {{0xCA, 0x03, 0x80}, 0x40, 0x8003},        // JP Z,pc+3
        {{0xCC, 0x03, 0x80}, 0x40, 0x8003},        // CALL Z,pc+3
        {{0xC8}, 0x40, 0x8001},                    // RET Z with the stack word = pc+1 (set below)
        {{0xDD, 0x20, 0x00}, 0x00, 0x8003},        // DD JR NZ,$+2
    };
    for (const Case& k : cases) {
        const State st{k.f, 0x0001, false};
        Z80Registers r = setup(*g, k.c, pc, 0xA000, st);
        g->mem.ram[0xA000] = 0x01; g->mem.ram[0xA001] = 0x80;
        const DisasmReadFn rd = g->rd();
        const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into);
        row.need(s.status == StepSuccessors::Status::Ok && s.count == 1 && s.addr[0] == k.want, bytes_str(k.c) + " " + set_str(s));
    }
    delete g;
    return row;
}

// ---- SSC-20 hostile streams --------------------------------------------------------------
static uint32_t g_rng = 0x2545F491u;
static uint32_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static Row ssc_20() {
    Row row;
    Rig* g = new Rig;
    // memory full of prefixes: Unknowable, and it returns after reading a bounded number of bytes
    for (int mode = 0; mode < 3; ++mode) {
        for (int i = 0; i < 65536; ++i) g->mem.ram[i] = mode == 0 ? 0xDD : mode == 1 ? 0xFD : ((i & 1) ? 0xFD : 0xDD);
        Z80Registers r{};
        r.PC = 0x8000; r.SP = 0xA000;
        long reads = 0;
        const DisasmReadFn rd = [&](uint16_t a) -> uint8_t { ++reads; return g->mem.ram[a]; };
        for (StepKind k : {StepKind::Into, StepKind::Over}) {
            reads = 0;
            const StepSuccessors s = step_successors(r, rd, rd, k);
            row.need(s.status == StepSuccessors::Status::Unknowable && s.why[0] && std::strstr(s.why, "prefix") && reads <= 65536 + 16,
                     "mode " + std::to_string(mode) + " " + set_str(s) + " reads " + std::to_string(reads));
        }
    }
    // the longest chain that ends: every byte a prefix except the one before the PC
    for (int i = 0; i < 65536; ++i) g->mem.ram[i] = 0xDD;
    g->mem.ram[0x7FFF] = 0x00;
    {
        Z80Registers r{};
        r.PC = 0x8000; r.SP = 0xA000;
        const DisasmReadFn rd = g->rd();
        const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into);
        row.need(s.status == StepSuccessors::Status::Ok && s.count == 1 && s.addr[0] == 0x8000, "65535 prefixes then NOP: " + set_str(s));
    }
    // random streams, random registers: always Ok with 1..2 members or Unknowable, and the core stays inside
    int ok_cases = 0, unknowable = 0;
    for (int n = 0; n < 100000; ++n) {
        std::memset(g->mem.ram, 0, sizeof g->mem.ram);
        std::vector<uint8_t> c(8);
        for (uint8_t& b : c) b = static_cast<uint8_t>(rnd());
        const uint16_t pc = (n & 1) ? static_cast<uint16_t>(0xFFFC + (rnd() & 3)) : static_cast<uint16_t>(0x8000 + (rnd() & 0xFF));
        State st{static_cast<uint8_t>(rnd()), static_cast<uint16_t>(rnd()), (rnd() & 1) != 0};
        const uint16_t sp = static_cast<uint16_t>(0x6000 + (rnd() & 0xFF));
        Z80Registers r = setup(*g, c, pc, sp, st);
        r.HL = static_cast<uint16_t>(0x3000 + (rnd() & 0xFF));
        r.DE = static_cast<uint16_t>(0x2000 + (rnd() & 0xFF));
        r.IX = static_cast<uint16_t>(rnd()); r.IY = static_cast<uint16_t>(rnd());
        const DisasmReadFn rd = g->rd();
        const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into);
        if (s.status == StepSuccessors::Status::Unknowable) { ++unknowable; row.need(s.why[0] != 0, "Unknowable without a reason"); continue; }
        ++ok_cases;
        row.need(s.count >= 1 && s.count <= 2, "count " + std::to_string(s.count) + " " + bytes_str(c));
        size_t p = 0;
        while (p < c.size() && (c[p] == 0xDD || c[p] == 0xFD)) ++p;
        if (p + 1 >= c.size()) continue;
        if (c[p] == 0x76) continue;                                      // HALT: its own row
        load(*g, r);
        g->cpu.execute();
        row.need(in_set(s, g->cpu.pc()), bytes_str(c) + " @" + hx(pc) + " landing " + hx(g->cpu.pc()) + " not in " + set_str(s));
    }
    row.need(ok_cases > 90000, "ok cases " + std::to_string(ok_cases) + " unknowable " + std::to_string(unknowable));
    delete g;
    return row;
}

// ---- SSC-21 block repeats ----------------------------------------------------------------
static Row ssc_21() {
    Row row;
    Rig* g = new Rig;
    // LDI.. repeats: BC counts; CP.. also stops on a match; IN/OUT repeats count B
    struct Op { uint8_t e; bool io; bool cp; };
    const Op ops[] = {{0xB0, false, false}, {0xB8, false, false}, {0xB1, false, true}, {0xB9, false, true},
                      {0xB2, true, false}, {0xBA, true, false}, {0xB3, true, false}, {0xBB, true, false},
                      {0xB4, false, false}, {0xBC, false, false}, {0xB6, false, false}, {0xB7, false, false}};
    for (uint16_t pc : kPcs)
        for (const Op& o : ops) {
            const uint16_t fall = static_cast<uint16_t>(pc + 2);
            for (int again = 0; again < 2; ++again) {
                const uint16_t bc = o.io ? (again ? 0x0210 : 0x0110) : (again ? 0x0002 : 0x0001);
                const State st{0x00, bc, false};                        // A != (HL): CPIR does not match
                Z80Registers r = setup(*g, {0xED, o.e, 0x00, 0x00}, pc, 0xA000, st);
                const DisasmReadFn rd = g->rd();
                const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into);
                load(*g, r);
                g->cpu.execute();
                const uint16_t want = again ? pc : fall;
                row.need(s.status == StepSuccessors::Status::Ok && s.count == 2 && in_set(s, fall) && in_set(s, pc) && g->cpu.pc() == want,
                         "ED " + hx(o.e) + " BC " + hx(bc) + " " + set_str(s) + " land " + hx(g->cpu.pc()) + " want " + hx(want));
            }
            if (o.cp) {                                                 // CPIR/CPDR on a match stops
                const State st{0x00, 0x0002, true};
                Z80Registers r = setup(*g, {0xED, o.e, 0x00, 0x00}, pc, 0xA000, st);
                const DisasmReadFn rd = g->rd();
                const StepSuccessors s = step_successors(r, rd, rd, StepKind::Into);
                load(*g, r);
                g->cpu.execute();
                row.need(g->cpu.pc() == fall && in_set(s, fall), "ED " + hx(o.e) + " match lands " + hx(g->cpu.pc()));
            }
        }
    delete g;
    return row;
}

int main() {
    check("SSC-01", "every unprefixed opcode lands inside its Into set; count 1, or 2 for a conditional (Z80Cpu x 8 states x 2 PCs)", ssc_01());
    check("SSC-02", "every CB opcode lands inside its Into set", ssc_02());
    check("SSC-03", "every ED opcode but the Z80N ones, defined or not, lands inside its Into set", ssc_03());
    check("SSC-04", "every Z80N ED opcode lands inside its Into set", ssc_04());
    check("SSC-05", "every DD-prefixed opcode lands inside its Into set", ssc_05());
    check("SSC-06", "every FD-prefixed opcode lands inside its Into set", ssc_06());
    check("SSC-07", "every DD CB d op lands inside its Into set", ssc_07());
    check("SSC-08", "every FD CB d op lands inside its Into set", ssc_08());
    check("SSC-09", "DD/FD chains of 1 to 200 prefixes, mixed, and DD/FD ED xx: the core's landing is in the set", ssc_09());
    check("SSC-10", "a conditional lands on exactly the member the UM0080 condition table names, set = {next, target}", ssc_10());
    check("SSC-11", "Over a CALL / CALL cc / RST / DJNZ: the pushed word or the fall-through is the one member (RST $08: two)", ssc_11());
    check("SSC-12", "Over of every non-call-like instruction equals its Into set", ssc_12());
    check("SSC-13", "RST $08: Over = {pc+1, pc+2}, the esxDOS stand-in returns to pc+2; Into = {$0008}", ssc_13());
    check("SSC-14", "HALT (any prefix): {next}; the CPU keeps PC on the HALT byte and an interrupt handler returns to next", ssc_14());
    check("SSC-15", "JP (C) is Unknowable with a reason, Into and Over, and the function reads no port", ssc_15());
    check("SSC-16", "RETN (7 opcodes) with NR 0xC0 bit 3: {stack word, C3:C2} and the core lands on C3:C2; bit 3 clear: stack word, C2/C3 unread", ssc_16());
    check("SSC-17", "RETI with the stackless latch active: {stack word}, and the core lands there", ssc_17());
    check("SSC-18", "instructions at 0xFFFD..0xFFFF with SP at 0xFFFF / 0xFFFE / 0x0000: operands, targets and the stack word wrap like the core", ssc_18());
    check("SSC-19", "duplicate members collapse: JR NZ,$+2, JP Z,pc+3, CALL Z,pc+3, RET Z to pc+1", ssc_19());
    check("SSC-20", "hostile streams: memory full of prefixes is Unknowable and bounded; 100000 random streams stay inside the set", ssc_20());
    check("SSC-21", "block repeats (12 opcodes): {next, itself}; the core repeats on the second count and falls through on the last", ssc_21());

    std::printf("\nstep_successors results\n=======================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
