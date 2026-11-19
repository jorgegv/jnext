// Debugger CPU panel register-edit policy test (GitHub issue #368).
//
// No VHDL oracle: this is host debugger UI. The oracles are the issue's
// contract, UM0080's register and F layout (S7 Z6 H4 PV2 N1 C0), and the
// documented behaviour of the address boxes' symbol rule (SymbolTable::resolve).
//
// Run: ./build/test/reg_edit_test

#include "debugger/reg_edit.h"

#include <cstdio>
#include <string>
#include "../row_id.h"

using namespace jnext::regedit;
using jnext::dbg::RegId;
using jnext::dbg::Result;

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string hex(unsigned v) { char b[16]; std::snprintf(b, sizeof b, "%X", v); return b; }

/// "text -> value/refused" for a failure message.
std::string show(FieldKind k, const char* text, const SymbolTable& st)
{
    const Parsed p = parse(k, text, st);
    return std::string("'") + text + "' -> " + (p.ok ? hex(p.v0) + "/" + hex(p.v1) : "refused") + "; ";
}

}  // namespace

int main()
{
    SymbolTable none;
    SymbolTable syms;
    syms.add_label("main", 0x8123, std::nullopt);
    syms.add_label("FE", 0x9000, std::nullopt);

    {
        struct { const char* t; bool ok; unsigned v; } c[] = {
            {"1234", true, 0x1234}, {"$1234", true, 0x1234}, {"0x1234", true, 0x1234},
            {"ffff", true, 0xFFFF}, {"10000", false, 0}, {"", false, 0},
        };
        std::string bad;
        for (auto& x : c) {
            const Parsed p = parse(FieldKind::Word, x.t, none);
            if (p.ok != x.ok || (x.ok && p.v0 != x.v)) bad += show(FieldKind::Word, x.t, none);
        }
        check("REGED-01", "Word: 1234 $1234 0x1234 ffff are numbers; 10000 and empty refused", bad.empty(), bad);
    }
    {
        struct { const char* t; bool ok; unsigned v; } c[] = {
            {"main", true, 0x8123}, {"FE", true, 0x00FE}, {"@FE", true, 0x9000}, {"nosuch", false, 0},
        };
        std::string bad;
        for (auto& x : c) {
            const Parsed p = parse(FieldKind::Word, x.t, syms);
            if (p.ok != x.ok || (x.ok && p.v0 != x.v)) bad += show(FieldKind::Word, x.t, syms);
        }
        check("REGED-02", "Word with symbols: a name resolves, FE is the number, @FE the symbol, unknown refused",
              bad.empty(), bad);
    }
    {
        struct { const char* t; bool ok; unsigned v; } c[] = {
            {"7F", true, 0x7F}, {"$7F", true, 0x7F}, {"0x7f", true, 0x7F}, {"5", true, 5},
            {"100", false, 0}, {"main", false, 0}, {"", false, 0},
        };
        std::string bad;
        for (auto& x : c) {
            const Parsed p = parse(FieldKind::Byte, x.t, syms);
            if (p.ok != x.ok || (x.ok && p.v0 != x.v)) bad += show(FieldKind::Byte, x.t, syms);
        }
        check("REGED-03", "Byte: 7F $7F 0x7f are 0x7F; 100, a symbol and empty refused", bad.empty(), bad);
    }
    {
        struct { const char* t; bool ok; unsigned a, b; } c[] = {
            {"0/0", true, 0, 0}, {"0/1", true, 0, 1}, {"1/0", true, 1, 0}, {"1/1", true, 1, 1},
            {" 1 / 0 ", true, 1, 0},
            {"2/0", false, 0, 0}, {"1", false, 0, 0}, {"1/", false, 0, 0}, {"/1", false, 0, 0}, {"", false, 0, 0},
        };
        std::string bad;
        for (auto& x : c) {
            const Parsed p = parse(FieldKind::Iff, x.t, none);
            if (p.ok != x.ok || (x.ok && (p.v0 != x.a || p.v1 != x.b))) bad += show(FieldKind::Iff, x.t, none);
        }
        check("REGED-04", "Iff: a/b with 0/1 each (blanks trimmed) -> IFF1, IFF2; anything else refused", bad.empty(), bad);
    }
    {
        struct { const char* t; bool ok; unsigned v; } c[] = {
            {"0", true, 0}, {"1", true, 1}, {"2", true, 2},
            {"3", false, 0}, {"-1", false, 0}, {"x", false, 0}, {"", false, 0},
        };
        std::string bad;
        for (auto& x : c) {
            const Parsed p = parse(FieldKind::Im, x.t, none);
            if (p.ok != x.ok || (x.ok && p.v0 != x.v)) bad += show(FieldKind::Im, x.t, none);
        }
        check("REGED-05", "Im: 0 1 2 accepted; 3, -1, x and empty refused", bad.empty(), bad);
    }
    {
        const int bits[] = {7, 6, 4, 2, 1, 0};
        std::string bad;
        for (unsigned f = 0; f < 256; ++f)
            for (int b : bits)
                if (toggle_flag(static_cast<uint8_t>(f), b) != static_cast<uint8_t>(f ^ (1u << b)))
                    bad += hex(f) + "/" + std::to_string(b) + " ";
        check("REGED-06", "toggle_flag flips exactly the bit, for all 256 F and the six shown bits", bad.empty(), bad);
    }
    {
        const char* labels[16] = {"AF", "BC", "DE", "HL", "AF'", "BC'", "DE'", "HL'",
                                  "IX", "IY", "SP", "PC", "I", "R", "IFF", "IM"};
        const RegId regs[16] = {RegId::AF, RegId::BC, RegId::DE, RegId::HL,
                                RegId::AF2, RegId::BC2, RegId::DE2, RegId::HL2,
                                RegId::IX, RegId::IY, RegId::SP, RegId::PC,
                                RegId::I, RegId::R, RegId::IFF1, RegId::IM};
        const FieldKind kinds[16] = {
            FieldKind::Word, FieldKind::Word, FieldKind::Word, FieldKind::Word,
            FieldKind::Word, FieldKind::Word, FieldKind::Word, FieldKind::Word,
            FieldKind::Word, FieldKind::Word, FieldKind::Word, FieldKind::Word,
            FieldKind::Byte, FieldKind::Byte, FieldKind::Iff, FieldKind::Im};
        // navigable: BC DE HL BC' DE' HL' IX IY SP PC
        const bool nav[16] = {false, true, true, true, false, true, true, true,
                              true, true, true, true, false, false, false, false};
        std::string bad;
        const auto& f = fields();
        for (size_t i = 0; i < 16; ++i) {
            if (std::string(f[i].label) != labels[i] || f[i].reg != regs[i] ||
                f[i].kind != kinds[i] || f[i].navigable != nav[i])
                bad += std::string(f[i].label) + " ";
        }
        check("REGED-07", "fields(): 16 labels in panel order, each with its RegId, kind and navigable flag",
              bad.empty(), bad);
    }
    {
        const std::string rzx = refusal_text("PC", Result::RefusedRzx);
        const std::string uns = refusal_text("PC", Result::Unsupported);
        check("REGED-08", "refusal_text: the RZX sentence and the result-name fallback",
              rzx == "PC not changed: registers cannot be edited while an RZX recording is being made or played (stop it first)" &&
              uns == "PC not changed: unsupported", rzx + " | " + uns);
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
