// The skoolkit and Ghidra exports of labels and the coverage code/data map
// (GH #389, E23-26; doc/design/SOURCE-LEVEL-DEBUGGING.md section 11.2), stage A:
//
//   SKCTL-*        the control-file writer: the code/data classifier, the file set
//                  and window rule, comments, the Z80N split (pure)
//   GHSYM-*        the Ghidra symbol-file writer and its view file
//   SRCDBG-REX-*   Debugger::export_skoolkit() / export_ghidra() on a real machine:
//                  a banked label exports with its page, the coverage run, read-only,
//                  a directory export replaces its own files only, refusals, a
//                  space without readable bytes, a z88dk C symbol
//
// WHAT PROVES THE FORMATS is not this suite: the functional rows
// skoolkit-foreign-func and ghidra-foreign-func run skoolkit 10.1 and Ghidra 12.2
// themselves on the files. The expected texts below were checked against them.
//
// Oracles: design 11.1 (X/R/W), instruction_length() and the Z80N ED decode of
// cores/zxnext/src/cpu/t80n_mcode.vhd, the skoolkit 10.1 and Ghidra 12.2 readers.
//
// Run: ./build/test/reexport_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debug/disasm.h"
#include "debug/ghidra_symbols.h"
#include "debug/long_address.h"
#include "debug/mem_coverage.h"
#include "debug/skoolkit_ctl.h"
#include "peripheral/divmmc.h"
#include "../row_id.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;
using jnext::dbg::ReReport;
using jnext::dbg::Result;
using jnext::dbg::COVERAGE_EXEC;
using jnext::dbg::COVERAGE_READ;
using jnext::dbg::COVERAGE_WRITE;
namespace reexport = jnext::dbg::reexport;
namespace sk = jnext::dbg::skoolkit;
namespace gh = jnext::dbg::ghidra;

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " - ", detail.c_str());
    }
}

static std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void write_text(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary);
    out << text;
}

static bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

/// The names in a directory, sorted.
static std::vector<std::string> ls(const fs::path& dir) {
    std::vector<std::string> v;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end());
    return v;
}

static std::vector<uint8_t> bytes_of(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : " ") + x;
    return s;
}

static const reexport::File* file_of(const reexport::Output& o, const std::string& name) {
    for (const reexport::File& f : o.files) if (f.name == name) return &f;
    return nullptr;
}

static std::string text_of(const reexport::Output& o, const std::string& name) {
    const reexport::File* f = file_of(o, name);
    return f ? f->content : std::string("<no such file>");
}

static std::string names_of(const reexport::Output& o) {
    std::vector<std::string> v;
    for (const reexport::File& f : o.files) v.push_back(f.name);
    return join(v);
}

// ---------------------------------------------------------------------------
// Fixture F1: space P10, window $C000. Bytes and coverage (offsets in hex):
//   +0   C3 10 C0       JP $C010            X +0, R +1 +2
//   +3   01 02 03       data                R +3
//   +10  3A 03 C0 C9    LD A,($C003); RET   X +10 +13, R +11 +12
//   +14  "HELLO"                            W +14
//   +20  ED 91 07 03 C9 NEXTREG 7,3; RET   X +20 +24, R +21 +22 +23
// ---------------------------------------------------------------------------
static sk::Store f1_store(bool with_flags = true) {
    sk::Store s;
    s.space = MemSpace::page(0x10);
    s.bytes.assign(0x2000, 0);
    const uint8_t b0[] = {0xC3, 0x10, 0xC0, 0x01, 0x02, 0x03};
    std::copy(b0, b0 + sizeof(b0), s.bytes.begin());
    const uint8_t b10[] = {0x3A, 0x03, 0xC0, 0xC9, 'H', 'E', 'L', 'L', 'O'};
    std::copy(b10, b10 + sizeof(b10), s.bytes.begin() + 0x10);
    const uint8_t b20[] = {0xED, 0x91, 0x07, 0x03, 0xC9};
    std::copy(b20, b20 + sizeof(b20), s.bytes.begin() + 0x20);
    if (with_flags) {
        s.flags.assign(0x2000, 0);
        for (int o : {0x0, 0x10, 0x13, 0x20, 0x24}) s.flags[o] = COVERAGE_EXEC;
        for (int o : {0x1, 0x2, 0x3, 0x11, 0x12, 0x21, 0x22, 0x23}) s.flags[o] = COVERAGE_READ;
        s.flags[0x14] = COVERAGE_WRITE;
    }
    return s;
}

static std::vector<reexport::Label> f1_labels() {
    const MemSpace p10 = MemSpace::page(0x10);
    return {{p10, 0xC000, "banked"}, {p10, 0xC003, "table"}, {p10, 0xC011, "smc"}};
}

static std::vector<reexport::Comment> f1_comments() {
    const MemSpace p10 = MemSpace::page(0x10);
    return {{p10, 0xC003, "two\nlines"}, {p10, 0xC010, "Load the first byte"}};
}

static sk::Input f1_input() {
    sk::Input in;
    in.labels = f1_labels();
    in.comments = f1_comments();
    in.stores.push_back(f1_store());
    return in;
}

// ---------------------------------------------------------------------------
// The Z80N ED opcodes and their lengths. Oracle for the SET: the ED decode of
// cores/zxnext/src/cpu/t80n_mcode.vhd (X"91" :1668, X"92" :1690, X"30".."33"
// :1712-1730, X"23" :1754, X"24" :1760, x"27" :1778, X"28".."2C" :1789-1813,
// X"93".."95" :1819-1831, X"98" :1837, X"34".."36" :1850, X"8A" :1921, X"B7"
// :1953, X"A4"/"B4" :2098, X"A5" :2141, X"B6" :2188, X"AC"/"BC" :2230, x"90"
// :2519). The lengths are the Next opcode table's (2, the immediate forms 3 and 4).
// ---------------------------------------------------------------------------
struct Z80nOp { uint8_t op; int len; };
static const Z80nOp kZ80n[] = {
    {0x23, 2}, {0x24, 2}, {0x27, 3}, {0x28, 2}, {0x29, 2}, {0x2A, 2}, {0x2B, 2}, {0x2C, 2},
    {0x30, 2}, {0x31, 2}, {0x32, 2}, {0x33, 2}, {0x34, 4}, {0x35, 4}, {0x36, 4}, {0x8A, 4},
    {0x90, 2}, {0x91, 4}, {0x92, 3}, {0x93, 2}, {0x94, 2}, {0x95, 2}, {0x98, 2}, {0xA4, 2},
    {0xA5, 2}, {0xAC, 2}, {0xB4, 2}, {0xB6, 2}, {0xB7, 2}, {0xBC, 2},
};

static void pure_rows() {
    // SKCTL-01: the classifier on F1.
    {
        const sk::Classified c = sk::classify(f1_store().bytes, f1_store().flags, 0x2000);
        struct R { sk::Class cls; uint32_t a, b; };
        const R want[] = {
            {sk::Class::Code, 0, 3},   {sk::Class::Data, 3, 4},    {sk::Class::Unknown, 4, 16},
            {sk::Class::Code, 16, 20}, {sk::Class::Data, 20, 21},  {sk::Class::Unknown, 21, 32},
            {sk::Class::Code, 32, 37}, {sk::Class::Unknown, 37, 0x2000},
        };
        bool same = c.marked && c.runs.size() == 8;
        std::string got;
        for (size_t i = 0; i < c.runs.size(); ++i) {
            got += std::string(c.runs[i].cls == sk::Class::Code ? "c" : c.runs[i].cls == sk::Class::Data ? "b" : "u") +
                   "[" + std::to_string(c.runs[i].start) + "," + std::to_string(c.runs[i].end) + ") ";
            if (i < 8) same = same && c.runs[i].cls == want[i].cls && c.runs[i].start == want[i].a &&
                             c.runs[i].end == want[i].b;
        }
        check("SKCTL-01", "F1 classifies as c[0,3) b[3,4) u[4,16) c[16,20) b[20,21) u[21,32) c[32,37) u[37,8192)",
              same && c.insns.size() == 5, got);
    }

    // SKCTL-02: precedence. R and W inside an instruction stay code (F1: +1 +2 are R inside the JP);
    // an X inside the next instruction's would-be extent clips the first; a W-only byte outside is data.
    {
        const sk::Store f1 = f1_store();
        const sk::Classified a = sk::classify(f1.bytes, f1.flags, 0x2000);
        const bool r_in_code = a.runs[0].cls == sk::Class::Code && a.runs[0].end == 3;
        sk::Store clip = f1_store(false);
        clip.flags.assign(0x2000, 0);
        clip.flags[0] = COVERAGE_EXEC;
        clip.flags[1] = COVERAGE_EXEC;                 // C3 10 C0: JP is 3 bytes, an X at +1 clips it to 1
        const sk::Classified c = sk::classify(clip.bytes, clip.flags, 0x2000);
        sk::Input in;
        in.stores.push_back(clip);
        const reexport::Output out = sk::build(in);
        const std::string t = text_of(out, "C000-P10.ctl");
        const bool clipped = c.insns.size() >= 2 && c.insns[0].len == 1 && c.insns[0].clipped &&
                             c.insns[1].off == 1 && !c.insns[1].clipped && has(t, "\nB $C000,1\n");
        bool w_data = false;                                  // F1's W-only byte (+14) is the run b[0x14,0x15)
        for (const sk::Run& r : a.runs) if (r.start == 0x14 && r.end == 0x15) w_data = r.cls == sk::Class::Data;
        check("SKCTL-02", "R and W bytes inside an executed instruction stay code; an X start inside "
                          "the next instruction's extent clips it (written B $C000,1); a W-only byte "
                          "outside any instruction is data",
              r_in_code && clipped && w_data);
    }

    // SKCTL-03: the issue's row. F1 writes exactly this text (skoolkit 10.1 reads it with no warning).
    {
        const reexport::Output out = sk::build(f1_input());
        const std::string want =
            "# jnext space P10\n"
            "@ $C000 start\n"
            "@ $C000 org\n"
            "c $C000\n"
            "@ $C000 label=banked\n"
            "b $C003\n"
            "@ $C003 label=table\n"
            "B $C003\n"
            ". two\n"
            ". lines\n"
            "u $C004\n"
            "c $C010\n"
            "B $C010,1 Load the first byte\n"
            "@ $C011 label=smc\n"
            "B $C011,2\n"
            "b $C014\n"
            "u $C015\n"
            "c $C020\n"
            "B $C020,4\n"
            "u $C025\n"
            "i $E000\n";
        check("SKCTL-03", "F1 (labels, a two-line and a one-line comment, a label inside an instruction, a "
                          "Z80N instruction) writes C000-P10.ctl exactly as the verified text",
              text_of(out, "C000-P10.ctl") == want && out.labels == 3 && out.comments == 2,
              text_of(out, "C000-P10.ctl"));
    }

    // SKCTL-04: no coverage: one code run, a C sub-block at every key but a bare label at the block start.
    {
        sk::Input in = f1_input();
        in.stores[0].flags.clear();
        const reexport::Output out = sk::build(in);
        const std::string want =
            "# jnext space P10\n"
            "@ $C000 start\n"
            "@ $C000 org\n"
            "c $C000\n"
            "@ $C000 label=banked\n"
            "@ $C003 label=table\n"
            "C $C003\n"
            ". two\n"
            ". lines\n"
            "C $C010 Load the first byte\n"
            "@ $C011 label=smc\n"
            "C $C011\n"
            "i $E000\n";
        check("SKCTL-04", "a space with no mark is one code run over the window, a C sub-block at each "
                          "key (not at a bare label at the block start), and the end line",
              text_of(out, "C000-P10.ctl") == want, text_of(out, "C000-P10.ctl"));
    }

    // SKCTL-05: the file set.
    {
        sk::Input in;
        const MemSpace p10 = MemSpace::page(0x10), p01 = MemSpace::page(1), rom0 = MemSpace::rom(0);
        in.labels = {{p10, 0xC000, "a"}, {p10, 0x4000, "b"}, {std::nullopt, 0x0008, "rst8"},
                     {std::nullopt, 0x0010, "print"}, {p01, 0xE000, "top"}};
        in.stores.push_back(f1_store());
        sk::Store s01;
        s01.space = p01;
        s01.bytes.assign(0x2000, 0x55);
        in.stores.push_back(s01);
        sk::Store r;
        r.space = rom0;
        r.bytes.assign(0x4000, 0xAA);
        r.flags.assign(0x4000, 0);
        r.flags[0] = COVERAGE_EXEC;
        in.stores.push_back(r);
        const reexport::Output out = sk::build(in);
        const std::string names = names_of(out);
        bool bins = true;
        const auto store_of = [&](const std::string& name) -> const std::vector<uint8_t>& {
            return name.find("ROM0") != std::string::npos ? r.bytes
                   : name.find("P01") != std::string::npos ? s01.bytes : in.stores[0].bytes;
        };
        for (const reexport::File& f : out.files) {
            if (f.name.size() < 4 || f.name.compare(f.name.size() - 4, 4, ".ctl") != 0 || f.name == "0000-CPU.ctl") continue;
            const reexport::File* bin = file_of(out, f.name.substr(0, f.name.size() - 4) + ".bin");
            const std::vector<uint8_t>& want = store_of(f.name);
            bins = bins && bin && bytes_of(bin->content) == want;
        }
        const std::string rom_ctl = text_of(out, "0000-ROM0.ctl");
        check("SKCTL-05", "keys at $C000 and $4000 of P10 write two windows; the unpaged labels write "
                          "0000-CPU.ctl (labels only); a window at $E000 has no end line; ROM0 writes "
                          "0000-ROM0.ctl ending i $4000; every .ctl but CPU's has its .bin of the store's size",
              names == "0000-CPU.ctl 0000-ROM0.bin 0000-ROM0.ctl 4000-P10.bin 4000-P10.ctl C000-P10.bin "
                       "C000-P10.ctl E000-P01.bin E000-P01.ctl" &&
                  text_of(out, "0000-CPU.ctl") == "# jnext space CPU\n@ $0008 label=rst8\n@ $0010 label=print\n" &&
                  !has(text_of(out, "E000-P01.ctl"), "\ni ") &&
                  rom_ctl.size() > 8 && rom_ctl.compare(rom_ctl.size() - 8, 8, "i $4000\n") == 0 &&
                  bins,
              names);
    }

    // SKCTL-06: the window rule: keys, then the mapping now, then home.
    {
        const auto coverage_only = [](MemSpace sp) {
            sk::Store s;
            s.space = sp;
            const size_t n = jnext::dbg::space_size(sp.kind);
            s.bytes.assign(n, 0);
            s.flags.assign(n, 0);
            s.flags[0] = COVERAGE_EXEC;
            return s;
        };
        sk::Input in;
        for (uint16_t p : {0x20, 0x0E, 0x0F, 0x0A, 0x05}) in.stores.push_back(coverage_only(MemSpace::page(p)));
        in.mapped_window = [](MemSpace s) -> std::optional<uint16_t> {
            if (s == MemSpace::page(0x20)) return uint16_t(0x8000);
            return std::nullopt;
        };
        const reexport::Output a = sk::build(in);
        // P20 mapped at $8000 but with a label at $C000: the key wins.
        sk::Input b;
        b.stores.push_back(coverage_only(MemSpace::page(0x20)));
        b.labels = {{MemSpace::page(0x20), 0xC000, "k"}};
        b.mapped_window = in.mapped_window;
        const reexport::Output bo = sk::build(b);
        std::vector<std::string> ctl;
        for (const reexport::File& f : a.files) if (f.name.find(".ctl") != std::string::npos) ctl.push_back(f.name);
        check("SKCTL-06", "a coverage-only space goes to the window that maps it now ($8000 for P20), else "
                          "to its home (P0E $C000, P0F $E000, P0A $4000, P05 $A000); a key's window wins "
                          "over the mapping",
              join(ctl) == "4000-P0A.ctl 8000-P20.ctl A000-P05.ctl C000-P0E.ctl E000-P0F.ctl" &&
                  names_of(bo) == "C000-P20.bin C000-P20.ctl",
              join(ctl) + " | " + names_of(bo));
    }

    // SKCTL-07: comments: one line goes on the directive, a multi-line one on dot lines, trailing
    // whitespace is dropped (and said), an empty middle line writes a bare dot.
    {
        sk::Input in;
        in.stores.push_back(f1_store(false));
        const MemSpace p10 = MemSpace::page(0x10);
        in.comments = {{p10, 0xC003, "a  \nb"}, {p10, 0xC010, "one"}, {p10, 0xC020, "x\n\ny"}};
        const reexport::Output out = sk::build(in);
        const std::string t = text_of(out, "C000-P10.ctl");
        bool noted = false;
        for (const std::string& n : out.notes) noted = noted || (has(n, "trailing whitespace") && has(n, "1 comment line"));
        check("SKCTL-07", "a one-line comment is on the directive; a multi-line one is the bare directive "
                          "and one dot line each; trailing whitespace is dropped and reported; an empty "
                          "middle line is a bare dot",
              has(t, "\nC $C003\n. a\n. b\n") && has(t, "\nC $C010 one\n") &&
                  has(t, "\nC $C020\n. x\n.\n. y\n") && noted && out.comments == 3,
              t);
    }

    // SKCTL-08: every Z80N opcode at an X start is a B sub-block of its length.
    {
        std::string bad;
        for (const Z80nOp& z : kZ80n) {
            sk::Store s;
            s.space = MemSpace::page(0x10);
            s.bytes.assign(0x2000, 0);
            s.bytes[0] = 0xED;
            s.bytes[1] = z.op;
            s.flags.assign(0x2000, 0);
            s.flags[0] = COVERAGE_EXEC;
            sk::Input in;
            in.stores.push_back(s);
            const reexport::Output out = sk::build(in);
            const std::string want = "\nc $C000\nB $C000," + std::to_string(z.len) + "\n";
            if (!has(text_of(out, "C000-P10.ctl"), want)) {
                char b[32];
                std::snprintf(b, sizeof(b), " ED%02X", z.op);
                bad += b;
            }
        }
        check("SKCTL-08", "every Z80N ED opcode (30 of them) executed at an X start is written as a B "
                          "sub-block of its length, so skoolkit (Z80 only) stays aligned",
              bad.empty(), "not split:" + bad);
    }
    // SKCTL-08a: the standard ED forms are not split.
    {
        sk::Store s = f1_store(false);
        s.bytes.assign(0x2000, 0);
        s.bytes[0] = 0xED; s.bytes[1] = 0x43;                    // LD (nn),BC: 4 bytes
        s.bytes[4] = 0xED; s.bytes[5] = 0xB0;                    // LDIR: 2 bytes
        s.flags.assign(0x2000, 0);
        s.flags[0] = COVERAGE_EXEC;
        s.flags[4] = COVERAGE_EXEC;
        sk::Input in;
        in.stores.push_back(s);
        const std::string t = text_of(sk::build(in), "C000-P10.ctl");
        check("SKCTL-08a", "ED 43 nn nn (LD (nn),BC) and ED B0 (LDIR) are standard Z80 and are not split",
              has(t, "\nc $C000\nu $C006\n") && !has(t, "\nB "), t);
    }
    // SKCTL-08b: is_z80n_extension() and the length functions, for every ED opcode.
    {
        std::string bad;
        const std::set<uint8_t> oracle = [] { std::set<uint8_t> s; for (const Z80nOp& z : kZ80n) s.insert(z.op); return s; }();
        for (int op = 0; op < 256; ++op) {
            const uint8_t mem[4] = {0xED, uint8_t(op), 0x12, 0x34};
            const auto rd = [&](uint16_t a) -> uint8_t { return a < 4 ? mem[a] : 0; };
            const bool z = is_z80n_extension(0, rd);
            const int il = instruction_length(0, rd);
            const int dl = disasm_one(0, rd).byte_count;
            int want = 0;
            for (const Z80nOp& o : kZ80n) if (o.op == op) want = o.len;
            if (z != (oracle.count(uint8_t(op)) != 0) || il != dl || (want && il != want)) {
                char b[48];
                std::snprintf(b, sizeof(b), " ED%02X(z=%d il=%d dl=%d)", op, z, il, dl);
                bad += b;
            }
        }
        const uint8_t plain[2] = {0x3E, 0xED};
        const bool not_ed = !is_z80n_extension(0, [&](uint16_t a) -> uint8_t { return a < 2 ? plain[a] : 0; });
        check("SKCTL-08b", "is_z80n_extension() holds for exactly the 30 Z80N ED opcodes of the VHDL decode, "
                           "and instruction_length() agrees with disasm_one() for all 256 ED opcodes",
              bad.empty() && not_ed, bad);
    }

    // SKCTL-08c: a label name both formats would split on is skipped, and the report says so.
    {
        const MemSpace p10 = MemSpace::page(0x10);
        sk::Input in = f1_input();
        in.labels.push_back({p10, 0xC004, "has space"});
        in.labels.push_back({std::nullopt, 0x0008, "tab\tname"});
        in.labels.push_back({std::nullopt, 0x0010, ""});
        const reexport::Output a = sk::build(in);
        const reexport::Output g = gh::build(in.labels, {{0xC004, "has space"}});
        bool noted_a = false, noted_g = false;
        for (const std::string& n : a.notes) noted_a = noted_a || has(n, "'has space'");
        for (const std::string& n : g.notes) noted_g = noted_g || has(n, "'has space'");
        check("SKCTL-08c", "a label name with whitespace, a control character or nothing is skipped by both "
                           "writers (skoolkit and Ghidra split lines on whitespace) and the report names it",
              text_of(a, "C000-P10.ctl") == text_of(sk::build(f1_input()), "C000-P10.ctl") && !file_of(a, "0000-CPU.ctl") &&
                  a.labels == 3 && noted_a && noted_g && !file_of(g, "CPU.ghidra.txt") &&
                  !has(text_of(g, "P10.ghidra.txt"), "has") && !file_of(g, "view.ghidra.txt"),
              names_of(a) + " | " + names_of(g));
    }

    // GHSYM-01: the Ghidra writer.
    {
        const MemSpace p10 = MemSpace::page(0x10);
        std::vector<reexport::Label> labels = f1_labels();
        const reexport::Output a = gh::build(labels, {});
        labels.push_back({std::nullopt, 0x0010, "print"});
        labels.push_back({std::nullopt, 0x0008, "rst8"});
        const reexport::Output b = gh::build(labels, {});
        const reexport::Output none = gh::build({}, {});
        bool clean = true;
        for (const reexport::File& f : b.files) {
            if (f.content.empty() || f.content.back() != '\n') clean = false;
            std::istringstream is(f.content);
            for (std::string l; std::getline(is, l);) if (l.empty() || l[0] == '#') clean = false;
        }
        (void)p10;
        check("GHSYM-01", "P10's labels write 'NAME P10:0xHHHH l' lines in address order; the unpaged ones "
                          "'NAME 0xHHHH l' in CPU.ghidra.txt; an empty set writes no file; no file has a "
                          "blank or # line",
              names_of(a) == "P10.ghidra.txt" &&
                  text_of(a, "P10.ghidra.txt") == "banked P10:0xC000 l\ntable P10:0xC003 l\nsmc P10:0xC011 l\n" &&
                  names_of(b) == "CPU.ghidra.txt P10.ghidra.txt" &&
                  text_of(b, "CPU.ghidra.txt") == "rst8 0x0008 l\nprint 0x0010 l\n" &&
                  none.files.empty() && clean,
              names_of(a) + " | " + names_of(b));
    }
}

// ---------------------------------------------------------------------------
// The backend rows: a real Next machine and a real Debugger.
// ---------------------------------------------------------------------------
static fs::path g_dir;

static void build_next(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    emu.init(cfg);
    emu.mmu().set_boot_rom_enabled(false);
    emu.mmu().set_config_mode(false);
}

/// Load labels through an SLD's `L` records. `page` -1 is an unpaged label.
struct SldLabel { const char* name; int page; unsigned addr; };
static bool load_labels(Debugger& dbg, const fs::path& file, const std::vector<SldLabel>& labels) {
    std::string text = "|SLD.data.version|1\n"
        "main.asm|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
        "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
        "main.asm|1||0|4|32768|T|\n";    // one source trace: an SLD without any is refused
    for (const SldLabel& l : labels)
        text += std::string("main.asm|2||0|") + std::to_string(l.page) + "|" + std::to_string(l.addr) +
                "|L|," + l.name + ",,\n";
    write_text(file, text);
    const auto r = dbg.load_source_map(file.string(), false);
    if (r.count < 0) std::printf("  load_labels: %s\n", r.error.c_str());
    return r.count >= 0;
}

static void backend_rows() {
    // SRCDBG-REX-01: the issue's row 3: a banked label exports with its page, in both formats.
    {
        const fs::path d = g_dir / "r01";
        fs::create_directories(d);
        Emulator emu; build_next(emu);
        Debugger dbg(emu);
        const bool loaded = load_labels(dbg, d / "l.sld", {{"banked", 0x10, 0xC000}});
        emu.mmu().set_page(6, 0x11);
        ReReport rs, rg;
        std::string why;
        const Result r1 = dbg.export_skoolkit((d / "sk11").string(), &rs, &why);
        const Result r2 = dbg.export_ghidra((d / "gh11").string(), &rg, &why);
        const std::string ctl = slurp(d / "sk11" / "C000-P10.ctl");
        const bool sk_ok = r1 == Result::Ok && ctl.rfind("# jnext space P10\n", 0) == 0 && has(ctl, "\n@ $C000 label=banked\n");
        const bool gh_paged = r2 == Result::Ok && slurp(d / "gh11" / "P10.ghidra.txt") == "banked P10:0xC000 l\n";
        const bool view_off = !fs::exists(d / "gh11" / "view.ghidra.txt");
        emu.mmu().set_page(6, 0x10);
        const Result r3 = dbg.export_ghidra((d / "gh10").string(), &rg, &why);
        const bool view_on = r3 == Result::Ok && slurp(d / "gh10" / "view.ghidra.txt") == "banked 0xC000 l\n";
        check("SRCDBG-REX-01", "a toolchain label of P10 at $C000 exports in C000-P10.ctl under '# jnext space "
                               "P10' and in P10.ghidra.txt as 'banked P10:0xC000 l'; view.ghidra.txt lacks it "
                               "while P11 is mapped there and has 'banked 0xC000 l' while P10 is",
              loaded && sk_ok && gh_paged && view_off && view_on, why + "|" + ctl);
        // GHSYM-02 is the same fixture: the view follows the mapping.
        check("GHSYM-02", "view.ghidra.txt holds the labels of the memory mapped now, unprefixed: P10 mapped at "
                          "$C000 shows the P10 label, P11 mapped there shows none",
              view_off && view_on);
    }

    // SRCDBG-REX-02: coverage on a run: the INS-21-02 program.
    {
        const fs::path d = g_dir / "r02";
        const uint8_t p1 = 0x31, p2 = 0x30;
        Emulator emu; build_next(emu);
        const uint8_t prog[] = {0x3A, 0x10, 0xC0, 0x32, 0x20, 0xC0, 0x00};
        for (uint8_t pg : {p2, p1}) {
            emu.mmu().set_page(6, pg);
            for (size_t i = 0; i < sizeof(prog); ++i) emu.mmu().write(uint16_t(0xC000 + i), prog[i]);
        }
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0xC000; r.SP = 0xFF00; r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        Debugger dbg(emu);
        dbg.mem_coverage_enable(true);
        emu.mmu().set_page(6, p1);
        for (int i = 0; i < 3; ++i) emu.execute_single_instruction();
        ReReport rep;
        std::string why;
        const Result res = dbg.export_skoolkit(d.string(), &rep, &why);
        const std::string want =
            "# jnext space P31\n@ $C000 start\n@ $C000 org\nc $C000\nu $C007\nb $C010\nu $C011\nb $C020\nu $C021\ni $E000\n";
        std::vector<uint8_t> page(0x2000);
        dbg.peek(MemSpace::page(p1), 0, page.size(), page.data());
        check("SRCDBG-REX-02", "LD A,(C010) / LD (C020),A / NOP run in Page{p1} mapped at $C000 exports "
                               "C000-P31.ctl as c $C000, u $C007, b $C010, u $C011, b $C020, u $C021, i $E000, "
                               "its .bin is peek(Page{p1}), and no other space is written",
              res == Result::Ok && slurp(d / "C000-P31.ctl") == want &&
                  bytes_of(slurp(d / "C000-P31.bin")) == page && ls(d) == std::vector<std::string>{"C000-P31.bin", "C000-P31.ctl"},
              why + "|" + slurp(d / "C000-P31.ctl") + "|" + join(ls(d)));
    }

    // SRCDBG-REX-03 / 04: an export changes nothing in the machine; a poke does (the control).
    {
        const fs::path d = g_dir / "r03";
        Emulator emu; build_next(emu);
        Debugger dbg(emu);
        jnext::dbg::ClientInfo ci;
        ci.name = "reexport";
        ci.kind = jnext::dbg::ClientKind::Test;
        const auto cid = dbg.attach(ci).value;
        dbg.mem_coverage_enable(true);
        load_labels(dbg, g_dir / "r03.sld", {{"banked", 0x10, 0xC000}, {"top", -1, 0x0008}});
        emu.mmu().set_page(6, 0x10);
        emu.mmu().write(0xC000, 0x3E);
        emu.execute_single_instruction();
        const auto stream = [&] { return dbg.save_state_bytes(cid, jnext::dbg::SaveStateMode::RefuseMidFrame); };
        const auto s0 = stream();
        std::map<uint32_t, std::vector<uint8_t>> cov0;
        for (const MemSpace& sp : dbg.mem_coverage_spaces()) {
            std::vector<uint8_t> v(jnext::dbg::space_size(sp.kind));
            dbg.mem_coverage(sp, 0, v.size(), v.data());
            cov0[uint32_t(sp.kind) << 16 | sp.index] = v;
        }
        const uint64_t gen0 = emu.debug_state().load_generation();
        const size_t syms0 = dbg.symbols().size();
        const size_t subs0 = dbg.subscriptions(true).size();
        std::string why;
        const Result a = dbg.export_skoolkit(d.string(), nullptr, &why);
        const Result b = dbg.export_ghidra(d.string(), nullptr, &why);
        const auto s1 = stream();
        std::map<uint32_t, std::vector<uint8_t>> cov1;
        for (const MemSpace& sp : dbg.mem_coverage_spaces()) {
            std::vector<uint8_t> v(jnext::dbg::space_size(sp.kind));
            dbg.mem_coverage(sp, 0, v.size(), v.data());
            cov1[uint32_t(sp.kind) << 16 | sp.index] = v;
        }
        check("SRCDBG-REX-03", "both exports leave the save stream, the coverage of every space, the load "
                               "generation, the symbol table and the subscriptions unchanged",
              a == Result::Ok && b == Result::Ok && s0.status == Result::Ok && s1.status == Result::Ok &&
                  s0.value == s1.value && !s0.value.empty() && cov0 == cov1 && !cov0.empty() &&
                  emu.debug_state().load_generation() == gen0 && dbg.symbols().size() == syms0 &&
                  dbg.subscriptions(true).size() == subs0,
              why + " stream=" + std::to_string(int(s0.status)) + "/" + std::to_string(int(s1.status)));
        const uint8_t v = 0x77;
        dbg.poke(cid, MemSpace::page(0x10), 0x100, 1, &v);
        const auto s2 = stream();
        check("SRCDBG-REX-04", "control for 03: a poke in the same fixture moves the save stream, so 03 "
                               "can tell a changed machine from an unchanged one",
              s2.status == Result::Ok && s2.value != s1.value);
    }

    // SRCDBG-REX-05: a directory export replaces its own files and nobody else's.
    {
        const fs::path d = g_dir / "r05";
        Emulator emu; build_next(emu);
        Debugger dbg(emu);
        load_labels(dbg, g_dir / "r05a.sld", {{"a", 0x10, 0xC000}, {"b", 0x11, 0xE000}, {"u", -1, 0x0008}});
        std::string why;
        const Result e1 = dbg.export_skoolkit(d.string(), nullptr, &why);
        const Result g1 = dbg.export_ghidra(d.string(), nullptr, &why);
        const std::vector<std::string> first = ls(d);
        write_text(d / "notes.ctl", "mine\n");
        write_text(d / "notes.txt", "mine\n");
        dbg.clear_symbols();
        load_labels(dbg, g_dir / "r05b.sld", {{"a2", 0x10, 0xC000}});
        const Result e2 = dbg.export_skoolkit(d.string(), nullptr, &why);
        const Result g2 = dbg.export_ghidra(d.string(), nullptr, &why);
        const std::vector<std::string> second = ls(d);
        // view.ghidra.txt went too: the second export has no label visible in the mapping now.
        const std::vector<std::string> want2 = {"C000-P10.bin", "C000-P10.ctl", "P10.ghidra.txt", "notes.ctl",
                                                "notes.txt"};
        // A foreign control file, and a foreign image, in the way: nothing is written.
        const fs::path d2 = g_dir / "r05c";
        fs::create_directories(d2);
        write_text(d2 / "C000-P10.ctl", "mine\n");
        const Result x1 = dbg.export_skoolkit(d2.string(), nullptr, &why);
        const std::string why1 = why;
        const bool ctl_kept = slurp(d2 / "C000-P10.ctl") == "mine\n" && ls(d2) == std::vector<std::string>{"C000-P10.ctl"};
        fs::remove(d2 / "C000-P10.ctl");
        write_text(d2 / "C000-P10.bin", "mine\n");
        const Result x2 = dbg.export_skoolkit(d2.string(), nullptr, &why);
        const bool bin_kept = slurp(d2 / "C000-P10.bin") == "mine\n" && ls(d2) == std::vector<std::string>{"C000-P10.bin"};
        bool no_part = true;
        for (const fs::path& dir : {d, d2}) for (const std::string& n : ls(dir)) no_part = no_part && n.find(".part") == std::string::npos;
        check("SRCDBG-REX-05", "exporting twice into one directory removes the spaces the second export lacks "
                               "(.ctl, .bin, .ghidra.txt) and keeps a foreign notes.ctl; a foreign C000-P10.ctl "
                               "or C000-P10.bin refuses the export (Unsupported, naming the file) with nothing "
                               "written; no .part is left",
              e1 == Result::Ok && g1 == Result::Ok && first.size() == 9 && e2 == Result::Ok && g2 == Result::Ok &&
                  second == want2 && slurp(d / "C000-P10.ctl").find("label=a2") != std::string::npos &&
                  x1 == Result::Unsupported && has(why1, "C000-P10.ctl") && ctl_kept &&
                  x2 == Result::Unsupported && has(why, "C000-P10.bin") && bin_kept && no_part,
              join(first) + " | " + join(second));
    }

    // SRCDBG-REX-06: refusals.
    {
        const fs::path d = g_dir / "r06";
        fs::create_directories(d);
        write_text(d / "file", "x");
        Emulator emu; build_next(emu);
        Debugger dbg(emu);
        std::string w1, w2, w3, w4;
        const Result a = dbg.export_skoolkit((d / "file").string(), nullptr, &w1);
        const Result b = dbg.export_ghidra((d / "file").string(), nullptr, &w2);
        const Result c = dbg.export_skoolkit((d / "file" / "sub").string(), nullptr, &w3);
        const Result e = dbg.export_ghidra((d / "file" / "sub").string(), nullptr, &w4);
        check("SRCDBG-REX-06", "DIR a regular file, and DIR under a regular file, refuse both exports with "
                               "RefusedUnavailable and a reason naming the path",
              a == Result::RefusedUnavailable && b == Result::RefusedUnavailable &&
                  c == Result::RefusedUnavailable && e == Result::RefusedUnavailable &&
                  has(w1, "file is not a directory") && has(w2, "file is not a directory") &&
                  has(w3, "file") && has(w4, "file") &&
                  slurp(d / "file") == "x", w1 + "|" + w3);
    }

    // SRCDBG-REX-07: a space without readable bytes has no map; the report names it.
    {
        const fs::path d = g_dir / "r07";
        Emulator emu; build_next(emu);
        Debugger dbg(emu);
        dbg.mem_coverage_enable(true);
        // INS-21-14's DivMMC automap: an RST 8 whose M1 at $0008 follows from the DivMMC ROM.
        emu.nextreg().write(0x50, 10);
        emu.nextreg().write(0x0A, 0x10);
        emu.nextreg().write(0xB8, 0x02); emu.nextreg().write(0xB9, 0x02); emu.nextreg().write(0xBA, 0x02);
        emu.divmmc().set_port_io_enable(true);
        emu.divmmc().rom_data()[8] = 0x00;
        emu.mmu().write(0x0008, 0x00);
        emu.mmu().write(0x8000, 0xCF);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        const bool marked = [&] {
            for (const MemSpace& sp : dbg.mem_coverage_spaces()) if (sp == MemSpace::divmmc_rom()) return true;
            return false;
        }();
        ReReport rep;
        std::string why;
        const Result res = dbg.export_skoolkit(d.string(), &rep, &why);
        bool named = false;
        for (const std::string& n : rep.notes) named = named || (has(n, "DIVROM") && has(n, "not mapped"));
        bool no_divrom = true;
        for (const std::string& n : ls(d)) no_divrom = no_divrom && n.find("DIVROM") == std::string::npos;
        check("SRCDBG-REX-07", "coverage marked in the DivMMC ROM (no readable bytes) writes no control "
                               "file or image for it, and the report names DIVROM",
              marked && res == Result::Ok && named && no_divrom && !ls(d).empty(), why + "|" + join(ls(d)));
    }

    // SRCDBG-REX-08: a z88dk C symbol exports as its shown name (#424).
    {
        const fs::path d = g_dir / "r08";
        fs::create_directories(d);
        Emulator emu; build_next(emu);
        Debugger dbg(emu);
        write_text(d / "c.map",
                   "_counter = $9000 ; addr, local, , main_c, code_compiler, m.c:1\n"
                   "_asmsym = $9100 ; addr, local, , asm_asm, code_compiler, a.asm:1\n");
        const auto n = dbg.load_map((d / "c.map").string(), jnext::dbg::MapFormat::Z88dk);
        std::string why;
        const Result a = dbg.export_skoolkit((d / "sk").string(), nullptr, &why);
        const Result b = dbg.export_ghidra((d / "gh").string(), nullptr, &why);
        // The map's symbols are bound to the RAM page mapped at their address when it is loaded: P04 at $9000.
        const std::string ctl = slurp(d / "sk" / "8000-P04.ctl");
        const std::string gp = slurp(d / "gh" / "P04.ghidra.txt");
        const std::string gview = slurp(d / "gh" / "view.ghidra.txt");
        check("SRCDBG-REX-08", "a z88dk C symbol _counter exports as 'counter' (the name the disassembly "
                               "shows), an assembler symbol as its own name, in both formats",
              n.status == Result::Ok && a == Result::Ok && b == Result::Ok &&
                  has(ctl, "\n@ $9000 label=counter\n") && has(ctl, "\n@ $9100 label=_asmsym\n") &&
                  gp == "counter P04:0x9000 l\n_asmsym P04:0x9100 l\n" &&
                  gview == "counter 0x9000 l\n_asmsym 0x9100 l\n",
              std::string("map=") + std::to_string(int(n.status)) + "/" + std::to_string(n.value) + " why=" + why + " " +
                  join(ls(d / "sk")) + "|" + join(ls(d / "gh")));
    }
}

int main() {
    g_dir = fs::temp_directory_path() / ("reexport_test_" + std::to_string(getpid()));
    fs::remove_all(g_dir);
    fs::create_directories(g_dir);

    pure_rows();
    backend_rows();

    fs::remove_all(g_dir);
    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
