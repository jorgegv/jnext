// The z88dk CDB record decoder (GH #432, E353-02 of epic #353; plan section 2.2):
//
//   CDB-*   one `__CDBINFO__` record of a z88dk -debug map becomes a CdbRecord:
//           the un-escaping, the scope split at the last '.', the type chain
//           (arrays, pointers, structs, bitfields, signedness), the storage
//           (B stack offset, E data, R registers / optimised out), the T:
//           members, the classification of what is not a user variable, every
//           refusal with a reason, and every record of every committed map.
//
// Oracle: the committed fixtures test/fixtures/z88dk (the maps the two z88dk
// installs produced from our own vars.c / other.c), whose records are quoted
// verbatim in the rows, and the writers' own sources, which fix the grammar:
// src/sccz80/cdbfile.c (z88dk, "DA%dx", "SB" absent, "S6", "DV") and SDCC's
// cdbFile.c cdbTypeInfo() ("DA%ud," where the 'd' is a literal, "SB%d$%d", "SV",
// "SQ", the D? pointer classes). A synthetic record is marked as such in its row;
// it follows the same grammar. No VHDL counterpart. Pure: no machine.
// Run: ./build/test/cdb_record_test

#include "debug/cdb_record.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "../row_id.h"

namespace fs = std::filesystem;
using namespace jnext::dbg;

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

static const fs::path kFixtures = JNEXT_Z88DK_FIXTURE_DIR;

// Decode. A refused record comes back as sentinels (four chain elements and four
// members named REFUSED) that no row expects, so a row indexes safely and fails.
static CdbRecord dec(const char* text) {
    const CdbDecoded d = cdb_decode(text);
    if (d.ok()) return d.record;
    std::printf("  note: refused '%s': %s\n", text, d.error.c_str());
    CdbRecord r;
    CdbTypeElem e;
    e.kind = CdbTypeKind::Void;
    e.count = -99;
    e.tag = "REFUSED";
    r.var.name = "REFUSED";
    r.var.type.chain.assign(4, e);
    r.members.assign(4, CdbMember{});
    return r;
}

static bool refused(const char* text, const char* reason_part) {
    const CdbDecoded d = cdb_decode(text);
    return !d.ok() && d.error.find(reason_part) != std::string::npos;
}

static bool is(const CdbTypeElem& e, CdbTypeKind k, bool uns) { return e.kind == k && e.is_unsigned == uns; }

// Every CDB symbol of a map: the name token of each `__CDBINFO__` line.
static std::vector<std::string> cdb_symbols(const fs::path& map) {
    std::vector<std::string> out;
    std::ifstream in(map);
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, kCdbPrefix.size(), kCdbPrefix) != 0) continue;
        out.push_back(line.substr(0, line.find_first_of(" \t")));
    }
    return out;
}

int main() {
    // CDB-01 .. CDB-02: the un-escaping.
    {
        // Verbatim from master/sccz80-cvars/prog.map (the part after __CDBINFO__).
        const CdbUnescaped u = cdb_unescape(
            "S_3aLvars_2ec_2esum_24a_24_30_5f_30_24_30_28_7b_32_7dSI_3aS_29_2cB_2c_31_2c_31_30");
        check("CDB-01", "a verbatim sccz80 symbol un-escapes: letters stand for themselves, digits and punctuation are _hh, _5f is '_'",
              u.ok() && u.text == "S:Lvars.c.sum$a$0_0$0({2}SI:S),B,1,10");
        check("CDB-02", "a bare digit, a lone or short or non-hex '_', and an escaped NUL are refused with a reason",
              !cdb_unescape("S3").ok() && !cdb_unescape("S_").ok() && !cdb_unescape("S_3").ok() &&
              !cdb_unescape("S_zz").ok() && !cdb_unescape("S_00").ok() &&
              cdb_unescape("S_3").error.find("offset 1") != std::string::npos &&
              cdb_unescape("S_3a").ok() && cdb_unescape("S_3A").text == "S:");
    }

    // CDB-03 .. CDB-06: scope.
    {
        const CdbRecord a = dec("S:Lvars.c.sum$a$0_0$0({2}SI:S),B,1,10");  // master/sccz80-cvars
        check("CDB-03", "sccz80 S:Lvars.c.sum splits at the LAST '.': file vars.c, function sum",
              a.kind == CdbKind::Variable && a.scope == CdbScope::Local && a.scope_name == "vars.c" &&
              a.function == "sum" && a.var.name == "a");
        const CdbRecord z = dec("S:Lvars.sum$a$1_0$9({2}SI:S),B,1,4");  // master/sdcc-cvars
        check("CDB-04", "zsdcc S:Lvars.sum: module vars, function sum (one '.')",
              z.scope == CdbScope::Local && z.scope_name == "vars" && z.function == "sum");
        const CdbRecord g = dec("S:Lgame/util.c.util_twice$v$0_0$0({1}SC:U),B,1,4");  // v2.4/sccz80-subdir
        // Synthetic (the same grammar): a path with dots in a directory.
        const CdbRecord up = dec("S:L../my-src.d/main.c.main$i$1_0$4({2}SI:S),B,1,-14");
        check("CDB-05", "a path with a subdirectory and a '../my-src.d/main.c.main' path split at the last dot",
              g.scope_name == "game/util.c" && g.function == "util_twice" &&
              up.scope_name == "../my-src.d/main.c" && up.function == "main" && up.var.name == "i");
        const CdbRecord gl = dec("S:G$g_int$0_0$0({2}SI:S),E,0,0");
        const CdbRecord f1 = dec("S:Fvars.c$s_dup$0_0$0({1}SC:U),E,0,0");     // master/sccz80-cvars
        const CdbRecord f2 = dec("S:Fother$s_dup$0_0$0({1}SC:U),E,0,0");      // master/sdcc-cvars
        check("CDB-06", "S:G is global with no scope name; S:F keeps the file (vars.c) or module (other) it is private to",
              gl.scope == CdbScope::Global && gl.scope_name.empty() && gl.function.empty() &&
              f1.scope == CdbScope::File && f1.scope_name == "vars.c" && f1.function.empty() &&
              f2.scope == CdbScope::File && f2.scope_name == "other" && f2.var.name == "s_dup");
    }

    // CDB-07 .. CDB-09: scalar types.
    {
        struct Case { const char* rec; CdbTypeKind k; bool uns; int size; };
        const Case cases[] = {
            {"S:G$g_int$0_0$0({2}SI:S),E,0,0",   CdbTypeKind::Int,  false, 2},
            {"S:G$g_uint$0_0$0({2}SI:U),E,0,0",  CdbTypeKind::Int,  true,  2},
            {"S:G$g_schar$0_0$0({1}SC:S),E,0,0", CdbTypeKind::Char, false, 1},
            {"S:G$g_byte$0_0$0({1}SC:U),E,0,0",  CdbTypeKind::Char, true,  1},
            {"S:G$g_long$0_0$0({4}SL:S),E,0,0",  CdbTypeKind::Long, false, 4},
            {"S:G$g_ulong$0_0$0({4}SL:U),E,0,0", CdbTypeKind::Long, true,  4},
        };
        bool ok = true;
        for (const Case& c : cases) {
            const CdbRecord r = dec(c.rec);
            ok = ok && r.var.type.chain.size() == 1 && is(r.var.type.chain[0], c.k, c.uns) && r.var.type.size == c.size;
        }
        check("CDB-07", "int, char and long, signed and unsigned (:S / :U), with the byte size of {n}", ok);
        const CdbRecord sc = dec("S:G$g_char$0_0$0({1}SC:S),E,0,0");  // sccz80: char is signed
        const CdbRecord zc = dec("S:G$g_char$0_0$0({1}SC:U),E,0,0");  // zsdcc: char is unsigned
        check("CDB-08", "':U' is read: the same g_char is signed in sccz80 and unsigned in zsdcc",
              !sc.var.type.chain[0].is_unsigned && zc.var.type.chain[0].is_unsigned);
        const CdbRecord f6 = dec("S:G$g_float$0_0$0({6}SF:S),E,0,0");  // sccz80, 80cc: 48-bit
        const CdbRecord f4 = dec("S:G$g_float$0_0$0({4}SF:S),E,0,0");  // zsdcc: IEEE single
        check("CDB-09", "float is {6}SF (sccz80, 80cc) or {4}SF (zsdcc): the same kind, the size tells the format",
              f6.var.type.size == 6 && f4.var.type.size == 4 && f6.var.type.chain[0].kind == CdbTypeKind::Float &&
              f4.var.type.chain[0].kind == CdbTypeKind::Float);
    }

    // CDB-10 .. CDB-12: arrays.
    {
        const CdbRecord s3 = dec("S:G$g_str$0_0$0({3}DA3x,SC:S),E,0,0");   // sccz80
        const CdbRecord z3 = dec("S:G$g_str$0_0$0({3}DA3d,SC:U),E,0,0");   // zsdcc
        check("CDB-10", "DA3x (sccz80) and DA3d (zsdcc) are a 3-element array of char",
              s3.var.type.chain.size() == 2 && s3.var.type.chain[0].kind == CdbTypeKind::Array &&
              s3.var.type.chain[0].count == 3 && is(s3.var.type.chain[1], CdbTypeKind::Char, false) &&
              z3.var.type.chain[0].kind == CdbTypeKind::Array && z3.var.type.chain[0].count == 3 &&
              is(z3.var.type.chain[1], CdbTypeKind::Char, true));
        const CdbRecord m = dec("S:G$g_matrix$0_0$0({12}DA2x,DA3x,SI:S),E,0,0");
        const CdbRecord arr = dec("S:Lvars.c.main$arr$1_0$4({8}DA4x,SI:S),B,1,-13");
        check("CDB-11", "the element count comes from DAn, never from the byte size: g_matrix[2][3] is {12}, arr[4] is {8}",
              m.var.type.size == 12 && m.var.type.chain.size() == 3 && m.var.type.chain[0].count == 2 &&
              m.var.type.chain[1].count == 3 && is(m.var.type.chain[2], CdbTypeKind::Int, false) &&
              arr.var.type.size == 8 && arr.var.type.chain[0].count == 4);
        const CdbRecord pts = dec("S:G$g_pts$0_0$0({15}DA3x,STpoint:S),E,0,0");
        check("CDB-12", "an array of struct: DA3x then STpoint, tag point",
              pts.var.type.size == 15 && pts.var.type.chain.size() == 2 && pts.var.type.chain[0].count == 3 &&
              pts.var.type.chain[1].kind == CdbTypeKind::Struct && pts.var.type.chain[1].tag == "point");
    }

    // CDB-13 .. CDB-14: pointers, function pointers and structs.
    {
        const CdbRecord hp = dec("S:G$g_head$0_0$0({2}DG,STnode:S),E,0,0");
        const CdbRecord cp = dec("S:G$g_cptr$0_0$0({2}DG,SC:S),E,0,0");
        check("CDB-13", "DG is a pointer, whatever follows is the pointee: to struct node, to char",
              hp.var.type.chain.size() == 2 && hp.var.type.chain[0].kind == CdbTypeKind::Pointer &&
              hp.var.type.chain[0].pointer_class == 'G' && hp.var.type.chain[1].tag == "node" &&
              cp.var.type.chain[0].kind == CdbTypeKind::Pointer && is(cp.var.type.chain[1], CdbTypeKind::Char, false));
        const CdbRecord sf = dec("S:G$g_fnptr$0_0$0({2}DG,DF,DV),E,0,0");       // sccz80: void through DV
        const CdbRecord zf = dec("S:G$g_fnptr$0_0$0({2}DC,DF,SV:S),E,0,0");     // zsdcc: DC, void as SV
        check("CDB-14", "a function pointer is a pointer to a function returning the rest: DG,DF,DV (sccz80) and DC,DF,SV:S (zsdcc)",
              sf.var.type.chain.size() == 3 && sf.var.type.chain[0].kind == CdbTypeKind::Pointer &&
              sf.var.type.chain[1].kind == CdbTypeKind::Function && sf.var.type.chain[2].kind == CdbTypeKind::Void &&
              zf.var.type.chain[0].pointer_class == 'C' && zf.var.type.chain[1].kind == CdbTypeKind::Function &&
              zf.var.type.chain[2].kind == CdbTypeKind::Void && sf.hidden == CdbHidden::None &&
              zf.hidden == CdbHidden::None);
    }

    // CDB-15 .. CDB-18: storage.
    {
        const CdbRecord pos = dec("S:Lvars.c.sum$k$0_0$0({1}SC:U),B,1,4");        // a parameter
        const CdbRecord neg = dec("S:Lvars.c.sum$t$1_0$1({2}SI:S),B,1,-2");       // a local
        const CdbRecord big = dec("S:Lvars.c.main$big$1_0$4({4}SL:S),B,1,-19");
        check("CDB-15", "B,1,n is a stack slot: the signed offset from the frame base, positive for a parameter, negative for a local",
              pos.var.space == 'B' && pos.var.stack_offset() == 4 && neg.var.stack_offset() == -2 &&
              big.var.stack_offset() == -19 && pos.var.nums.size() == 2 && pos.var.nums[0] == 1 && !pos.var.optimised_out());
        const CdbRecord e = dec("S:G$g_int$0_0$0({2}SI:S),E,0,0");
        const CdbRecord fn = dec("S:G$main$0_0$0({2}DF,SI:S),C,0,13");            // zsdcc
        check("CDB-16", "E,0,0 is a data symbol (resolved by name later), C a code symbol; the numbers are kept as written",
              e.var.space == 'E' && e.var.nums.size() == 2 && e.var.nums[0] == 0 && e.var.nums[1] == 0 &&
              e.var.regs.empty() && fn.var.space == 'C' && fn.var.nums.size() == 2 && fn.var.nums[1] == 13);
        const CdbRecord bc = dec("S:Lvars.sum$t$1_0$10({2}SI:S),R,0,0,[c,b]");   // master/sdcc-cvars
        const CdbRecord hl = dec("S:Lvars.sum$inner$3_0$12({2}SI:S),R,0,0,[l,h]");
        const CdbRecord a = dec("S:Lutil.util_twice$r$1_0$10({1}SC:U),R,0,0,[a]");
        check("CDB-17", "R,0,0,[c,b] is the register pair BC, low byte first; [l,h] is HL; [a] is the single register A",
              bc.var.space == 'R' && bc.var.regs.size() == 2 && bc.var.regs[0] == "c" && bc.var.regs[1] == "b" &&
              !bc.var.optimised_out() && hl.var.regs[0] == "l" && hl.var.regs[1] == "h" &&
              a.var.regs.size() == 1 && a.var.regs[0] == "a" && !a.var.optimised_out());
        const CdbRecord none = dec("S:Lvars.sum$acc$1_0$10({4}SL:S),R,0,0,[]");
        check("CDB-18", "R,0,0,[] is optimised out: no registers at all, not register A",
              none.var.space == 'R' && none.var.regs.empty() && none.var.optimised_out() && none.var.nums.size() == 2);
    }

    // CDB-19 .. CDB-24: T: types.
    {
        const CdbRecord pt = dec("T:Ftypes.h$point[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)({2}S:S$y$0_0$0({2}SI:S),Z,0,0)"
                                 "({4}S:S$tag$0_0$0({1}SC:U),Z,0,0)]");
        check("CDB-19", "a struct type: kind Type, declared in types.h, tag point, members x@0 y@2 tag@4 with their types",
              pt.kind == CdbKind::Type && pt.scope_name == "types.h" && pt.var.name == "point" &&
              pt.members.size() == 3 && pt.members[0].var.name == "x" && pt.members[0].offset == 0 &&
              pt.members[1].var.name == "y" && pt.members[1].offset == 2 && pt.members[2].var.name == "tag" &&
              pt.members[2].offset == 4 && is(pt.members[2].var.type.chain[0], CdbTypeKind::Char, true) &&
              pt.members[2].var.type.size == 1 && pt.members[0].var.space == 'Z');
        const CdbRecord un = dec("T:Ftypes.h$word[({0}S:S$w$0_0$0({2}SI:U),Z,0,0)({0}S:S$b$0_0$0({2}DA2x,SC:U),Z,0,0)]");
        check("CDB-20", "a union: every member at offset 0, an array member keeps its count",
              un.members.size() == 2 && un.members[0].offset == 0 && un.members[1].offset == 0 &&
              un.members[1].var.type.chain[0].kind == CdbTypeKind::Array && un.members[1].var.type.chain[0].count == 2);
        const CdbRecord fl = dec("T:Fvars$flags[({0}S:S$lo$0_0$0({1}SB0$3:U),Z,0,0)({0}S:S$mid$0_0$0({1}SB3$5:U),Z,0,0)"
                                 "({1}S:S$hi$0_0$0({1}SB0$8:U),Z,0,0)]");  // master/sdcc-cvars
        const CdbTypeElem& lo = fl.members[0].var.type.chain[0];
        const CdbTypeElem& mid = fl.members[1].var.type.chain[0];
        const CdbTypeElem& hi = fl.members[2].var.type.chain[0];
        check("CDB-21", "zsdcc bitfields SB<start>$<width>:U carry their bit start and width and offsets 0, 0, 1; the type is scoped by module",
              fl.scope_name == "vars" && fl.members.size() == 3 && lo.kind == CdbTypeKind::Bitfield &&
              lo.bit_start == 0 && lo.bit_width == 3 && mid.bit_start == 3 && mid.bit_width == 5 &&
              hi.bit_start == 0 && hi.bit_width == 8 && mid.is_unsigned && fl.members[2].offset == 1);
        const CdbRecord sfl = dec("T:Ftypes.h$flags[({0}S:S$lo$0_0$0({2}SI:U),Z,0,0)({0}S:S$mid$0_0$0({2}SI:U),Z,0,0)"
                                  "({1}S:S$hi$0_0$0({2}SI:U),Z,0,0)]");  // master/sccz80-cvars
        check("CDB-22", "sccz80 bitfields carry no bit layout: plain SI:U members, never read as a Bitfield",
              sfl.members.size() == 3 && is(sfl.members[1].var.type.chain[0], CdbTypeKind::Int, true) &&
              sfl.members[1].var.type.chain[0].bit_width == 0 && sfl.members[2].offset == 1);
        const CdbRecord rc = dec("T:Fvars$rect[({0}S:S$a$0_0$0({5}STpoint:S),Z,0,0)({5}S:S$b$0_0$0({5}STpoint:S),Z,0,0)"
                                 "({10}S:S$name$0_0$0({4}DA4d,SC:U),Z,0,0)]");
        check("CDB-23", "a struct inside a struct: members of struct point at 0 and 5, then a 4-char array at 10",
              rc.members.size() == 3 && rc.members[0].var.type.chain[0].tag == "point" &&
              rc.members[1].offset == 5 && rc.members[2].offset == 10 &&
              rc.members[2].var.type.chain[0].kind == CdbTypeKind::Array && rc.members[2].var.type.size == 4);
        const CdbRecord emp = dec("T:Fx.h$e[]");
        check("CDB-24", "a type with no members decodes with none",
              emp.kind == CdbKind::Type && emp.members.empty() && emp.var.name == "e");
    }

    // CDB-25 .. CDB-28: classification.
    {
        const CdbRecord s0 = dec("S:Lvars.sum$sloc0$0_1$0({2}SI:S),B,1,-4");
        const CdbRecord st = dec("S:Fvars$__str_2$0_0$0({4}DA4d,SC:U),D,0,0");
        const CdbRecord cp = dec("S:Lutil.util_twice$__200000001$2_0$10({1}SC:U),R,0,0,[]");
        const CdbRecord u1 = dec("S:Lvars.sum$slocation$1_0$9({2}SI:S),B,1,4");   // synthetic user names
        const CdbRecord u2 = dec("S:Lvars.sum$sloc$1_0$9({2}SI:S),B,1,4");
        const CdbRecord u3 = dec("S:Lvars.sum$__x$1_0$9({2}SI:S),B,1,4");
        const CdbRecord u4 = dec("S:Lvars.sum$calls$1_0$10({1}SC:U),E,0,0");
        check("CDB-25", "compiler temporaries are hidden: sloc<n>, __str_<n>, __<digits>; user names like sloc, slocation, __x, calls are not",
              s0.hidden == CdbHidden::Temporary && st.hidden == CdbHidden::Temporary &&
              cp.hidden == CdbHidden::Temporary && u1.hidden == CdbHidden::None && u2.hidden == CdbHidden::None &&
              u3.hidden == CdbHidden::None && u4.hidden == CdbHidden::None);
        const CdbRecord en = dec("S:G$RED$0_0$0({0}),E,0,0");
        check("CDB-26", "an enumerator (a record with an empty type) is hidden and has an empty chain",
              en.hidden == CdbHidden::Enumerator && en.var.type.chain.empty() && en.var.type.size == 0 &&
              en.var.space == 'E');
        const CdbRecord twin = dec("S:G$g_str$0_0$0({-1}DA-1x,SC:S),E,0,0");
        const CdbRecord full = dec("S:G$g_str$0_0$0({3}DA3x,SC:S),E,0,0");
        check("CDB-27", "the sccz80 {-1}DA-1x twin of a sized array is hidden (count -1, size -1); the sized record is not",
              twin.hidden == CdbHidden::IncompleteArray && twin.var.type.size == -1 &&
              twin.var.type.chain[0].count == -1 && full.hidden == CdbHidden::None);
        const CdbRecord gm = dec("S:G$main$0_0$0({2}DF,SI:S),C,0,13");
        const CdbRecord fs_ = dec("S:Fvars$sum$0_0$0({2}DF,SI:S),C,0,0");
        check("CDB-28", "zsdcc's function-typed S: records (S:G$main, S:Fvars$sum) are hidden as Function; a function POINTER is not",
              gm.hidden == CdbHidden::Function && fs_.hidden == CdbHidden::Function &&
              dec("S:G$g_fnptr2$0_0$0({2}DC,DF,SI:S),E,0,0").hidden == CdbHidden::None);
    }

    // CDB-29 .. CDB-32: functions, modules, levels.
    {
        const CdbRecord f7 = dec("F:G$main$0_0$0({2}DF,SI:S),C,0,13,0,0,0");      // master/sdcc-cvars
        const CdbRecord f4 = dec("F:Fvars.c$sum$0_0$0({0}DF,SI:S),C,0,0");        // master/sccz80-cvars
        const CdbRecord fz = dec("F:G$other$0_0$0({2}DF,SI:S),Z,0,0,0,0,0");
        check("CDB-29", "F: records: global with 7 fields (frame size 13 in the second number), static with 4, space Z; never hidden",
              f7.kind == CdbKind::Function && f7.scope == CdbScope::Global && f7.var.name == "main" &&
              f7.var.nums.size() == 5 && f7.var.nums[1] == 13 && f4.kind == CdbKind::Function &&
              f4.scope == CdbScope::File && f4.scope_name == "vars.c" && f4.var.nums.size() == 2 &&
              fz.var.space == 'Z' && f7.hidden == CdbHidden::None && fz.hidden == CdbHidden::None &&
              f7.var.type.chain[0].kind == CdbTypeKind::Function);
        const CdbRecord m1 = dec("M:vars");
        const CdbRecord m2 = dec("M:vars.c");
        check("CDB-30", "M: is a module record (zsdcc 'vars', 80cc 'vars.c'): a name and nothing else",
              m1.kind == CdbKind::Module && m1.var.name == "vars" && m2.var.name == "vars.c" &&
              m1.scope == CdbScope::None && m1.members.empty() && m1.var.type.chain.empty());
        const CdbRecord sc = dec("S:Fvars.c$st_sum_calls$1_0$1({1}SC:U),E,0,0");   // sccz80 static local
        const CdbRecord zs = dec("S:Lvars.sum$calls$1_0$10({1}SC:U),E,0,0");       // zsdcc static local
        check("CDB-31", "a static local: sccz80 is a file-scope st_<fn>_<name>, zsdcc a local of its function in E space",
              sc.scope == CdbScope::File && sc.var.name == "st_sum_calls" && sc.var.space == 'E' &&
              zs.scope == CdbScope::Local && zs.function == "sum" && zs.var.name == "calls" && zs.var.space == 'E');
        const CdbRecord in = dec("S:Lvars.sum$inner$3_0$12({2}SI:S),R,0,0,[l,h]");
        const CdbRecord tm = dec("S:Lvars.sum$sloc0$0_1$0({2}SI:S),B,1,-4");
        const CdbRecord sz = dec("S:Lvars.c.sum$inner$2_0$2({2}SI:S),B,1,-8");
        check("CDB-32", "name$level_sub$block: zsdcc inner 3_0$12, a temporary 0_1$0, sccz80 inner 2_0$2",
              in.var.level == 3 && in.var.sub == 0 && in.var.block == 12 && tm.var.level == 0 &&
              tm.var.sub == 1 && tm.var.block == 0 && sz.var.level == 2 && sz.var.sub == 0 && sz.var.block == 2);
    }

    // CDB-33 .. CDB-34: refusals.
    {
        struct Bad { const char* rec; const char* reason; };
        const Bad bad[] = {
            {"", "no 'K:' prefix"},
            {"S:", "no 'K:' prefix"},
            {"X:G$a$0_0$0({2}SI:S),E,0,0", "unknown record kind"},
            {"S:Q$a$0_0$0({2}SI:S),E,0,0", "unknown scope"},
            {"F:Lvars.f$a$0_0$0({2}DF,SI:S),C,0,0", "unknown scope"},
            {"S:S$a$0_0$0({2}SI:S),Z,0,0", "unknown scope"},
            {"S:Lvars$a$0_0$0({2}SI:S),B,1,4", "not <file>.<function>"},
            {"S:Lvars.$a$0_0$0({2}SI:S),B,1,4", "not <file>.<function>"},
            {"S:L.sum$a$0_0$0({2}SI:S),B,1,4", "not <file>.<function>"},
            {"S:F$a$0_0$0({2}SI:S),E,0,0", "empty scope"},
            {"S:G$$0_0$0({2}SI:S),E,0,0", "empty name"},
            {"S:G$a$0$0({2}SI:S),E,0,0", "expected '_'"},
            {"S:G$a$0_0$({2}SI:S),E,0,0", "block number"},
            {"S:G$a$0_0$0", "expected '('"},
            {"S:G$a$0_0$0(2}SI:S),E,0,0", "expected {size}"},
            {"S:G$a$0_0$0({2SI:S),E,0,0", "expected {size}"},
            {"S:G$a$0_0$0({2}SI:S", "not closed by ')'"},
            {"S:G$a$0_0$0({2}SI:S)", "address space"},
            {"S:G$a$0_0$0({2}SI:S),Q,0,0", "unknown address space"},
            {"S:G$a$0_0$0({2}SI),E,0,0", "lacks :S or :U"},
            {"S:G$a$0_0$0({2}SI:X),E,0,0", "lacks :S or :U"},
            {"S:G$a$0_0$0({2}SZ:S),E,0,0", "unknown type element"},
            {"S:G$a$0_0$0({2}XX),E,0,0", "unknown type element"},
            {"S:G$a$0_0$0({2}DA3,SI:S),E,0,0", "array suffix"},
            {"S:G$a$0_0$0({2}DA3q,SI:S),E,0,0", "array suffix"},
            {"S:G$a$0_0$0({2}DAx,SI:S),E,0,0", "array count"},
            {"S:G$a$0_0$0({2}DA,SI:S),E,0,0", "without a count"},
            {"S:G$a$0_0$0({2}DGG,SI:S),E,0,0", "unknown type element"},
            {"S:G$a$0_0$0({2}SB3:U),E,0,0", "bitfield"},
            {"S:G$a$0_0$0({2}ST:S),E,0,0", "struct without a tag"},
            {"S:G$a$0_0$0({2}SI:S,),E,0,0", "ends with ','"},
            {"S:G$a$0_0$0({99999999999}SI:S),E,0,0", "number too large"},
            {"S:G$a$0_0$0({2}SI:S),E,0", "exactly two numbers"},
            {"S:G$a$0_0$0({2}SI:S),B,1", "exactly two numbers"},
            {"S:G$a$0_0$0({2}SI:S),E,0,x", "number or a register list"},
            {"S:G$a$0_0$0({2}SI:S),E,0,0,[a]", "outside register space"},
            {"S:G$a$0_0$0({2}SI:S),R,0,0", "without a register list"},
            {"S:G$a$0_0$0({2}SI:S),R,0,0,[a", "register list is not closed"},
            {"S:G$a$0_0$0({2}SI:S),R,0,0,[a,]", "empty register name"},
            {"S:G$a$0_0$0({2}SI:S),R,0,0,[a,b][c]", "text after"},
            {"S:G$a$0_0$0({2}SI:S),E,0,0)", "text after the record"},
            {"S:G$a$0_0$0({2}SI:S),E,0,0,", "number or a register list"},
            {"M:", "no 'K:' prefix"},
            {"M:a$b", "malformed name"},
            {"T:Gx.h$p[]", "scoped 'F'"},
            {"T:Fx.h$p", "expected '['"},
            {"T:F$p[]", "empty scope"},
            {"T:Fx.h$[]", "empty tag"},
            {"T:Fx.h$p[({0}S:S$a$0_0$0({2}SI:S),Z,0,0)", "not closed by ']'"},
            {"T:Fx.h$p[({0}S:S$a$0_0$0({2}SI:S),Z,0,0]", "not closed by ')'"},
            {"T:Fx.h$p[({0}S:G$a$0_0$0({2}SI:S),Z,0,0)]", "scoped 'S'"},
            {"T:Fx.h$p[(S:S$a$0_0$0({2}SI:S),Z,0,0)]", "{offset}"},
            {"T:Fx.h$p[]x", "text after the type record"},
        };
        bool all = true;
        std::string first;
        for (const Bad& b : bad) {
            if (!refused(b.rec, b.reason)) {
                first += std::string("\n    ") + b.rec + " -> " + cdb_decode(b.rec).error;
                all = false;
            }
        }
        if (!all) std::printf("  note: reasons not as expected:%s\n", first.c_str());
        check("CDB-33", "malformed records are refused, each with the reason that names what is wrong (a table of 54)", all);
        const CdbDecoded raw = cdb_decode_symbol(
            "__CDBINFO__S_3aG_24g_5fint_24_30_5f_30_24_30_28_7b_32_7dSI_3aS_29_2cE_2c_30_2c_30");
        check("CDB-34", "a map symbol decodes through the prefix and the un-escaping; without the prefix, or with a bad escape, it is refused with a reason",
              raw.ok() && raw.record.var.name == "g_int" && raw.record.var.type.size == 2 &&
              !cdb_decode_symbol("S_3aG").ok() && !cdb_decode_symbol("__CDBINFO__S_3aG_zz").ok() &&
              cdb_decode_symbol("S_3aG").error.find("__CDBINFO__") != std::string::npos);
    }

    // CDB-35 .. CDB-36: every record of every committed map.
    {
        std::vector<fs::path> maps;
        for (const auto& e : fs::recursive_directory_iterator(kFixtures))
            if (e.is_regular_file() && e.path().filename() == "prog.map") maps.push_back(e.path());
        size_t records = 0, lines = 0, refusals = 0, with_cdb = 0;
        std::map<std::string, size_t> per_map;           // relative dir -> decoded count
        std::map<std::string, std::map<CdbHidden, size_t>> hidden;
        std::string why;
        for (const fs::path& m : maps) {
            const std::vector<std::string> syms = cdb_symbols(m);
            if (!syms.empty()) ++with_cdb;
            const std::string rel = fs::relative(m.parent_path(), kFixtures).string();
            for (const std::string& s : syms) {
                ++lines;
                const CdbDecoded d = cdb_decode_symbol(s);
                if (!d.ok()) {
                    ++refusals;
                    if (why.empty()) why = rel + ": " + s + ": " + d.error;
                    continue;
                }
                ++records;
                ++per_map[rel];
                if (d.record.kind == CdbKind::Variable) ++hidden[rel][d.record.hidden];
            }
        }
        if (!why.empty()) std::printf("  note: first refusal: %s\n", why.c_str());
        check("CDB-35", "every __CDBINFO__ record of every committed map decodes (none refused, none dropped), and the three -cvars maps have the counts the README states (53, 63, 55)",
              !maps.empty() && with_cdb >= 30 && lines == records && refusals == 0 &&
              per_map["master/sccz80-cvars"] == 53 && per_map["v2.4/sccz80-cvars"] == 53 &&
              per_map["master/sdcc-cvars"] == 63 && per_map["master/80cc-cvars"] == 55);
        auto n = [&](const char* map, CdbHidden h) { return hidden[map][h]; };
        check("CDB-36", "what the views hide, counted per compiler: sccz80 3 enumerators + 1 twin, zsdcc 4 temporaries + 3 function records, 80cc like sccz80",
              n("master/sccz80-cvars", CdbHidden::Enumerator) == 3 && n("master/sccz80-cvars", CdbHidden::IncompleteArray) == 1 &&
              n("master/sccz80-cvars", CdbHidden::Temporary) == 0 && n("master/sccz80-cvars", CdbHidden::Function) == 0 &&
              n("master/sdcc-cvars", CdbHidden::Temporary) == 4 && n("master/sdcc-cvars", CdbHidden::Function) == 3 &&
              n("master/sdcc-cvars", CdbHidden::Enumerator) == 0 && n("master/sdcc-cvars", CdbHidden::IncompleteArray) == 0 &&
              n("master/80cc-cvars", CdbHidden::Enumerator) == 3 && n("master/80cc-cvars", CdbHidden::IncompleteArray) == 1);
    }

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
