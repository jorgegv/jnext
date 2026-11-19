// jnext::script - the Breakpoints panel's breakpoint file (GH #376): the codec
// that spells a GUI breakpoint as a `.jds` rule and reads one back
// (src/script/bp_script.{h,cpp}).
//
// Oracles: the DSL grammar of record (doc/design/debug-subsystem/dsl-frontend.md
// section 2.1); the ENGINE's registration of the same text, as an independent
// reader (script_engine.cpp `subscriptions_for`, rows BPJ-18/19); and the GUI's
// own subscription shapes (breakpoint_model.cpp `sub_for`) for what a GUI
// breakpoint is.
//
// Run: ./build/test/bp_script_test

#include "script/bp_script.h"
#include "script/check.h"
#include "script/parser.h"
#include "script/script_engine.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debug/long_address.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

using namespace jnext::script;
using jnext::dbg::Access;
using jnext::dbg::Debugger;
using jnext::dbg::EventKind;
using jnext::dbg::MemSpace;

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

static std::string dstr(const std::vector<Diagnostic>& ds) {
    if (ds.empty()) return "(none)";
    std::string s;
    for (const Diagnostic& d : ds) s += "[" + d.to_string() + "] ";
    return s;
}

// ---- fixture ---------------------------------------------------------------

static const char* const kDevice =
    "|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
    "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n";

// main.bas:10 is compiled at TWO places (page 20 $C000 and page 21 $C100); line
// 11 at one. Labels: `banked` (page 20 $C000), `odd-name` (page 21 $C010, a
// name outside the @name grammar) and an UNPAGED `gen` at $8002.
static const char* const kSld =
    "|SLD.data.version|1\n"
    "main.bas|1||0"
    "|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
    "main.bas|10||0|20|49152|T|\n"
    "main.bas|10||0|21|49408|T|\n"
    "main.bas|11||0|20|49154|T|\n"
    "x.asm|1||0|20|49152|L|,banked,,\n"
    "x.asm|2||0|21|49168|L|,odd-name,,\n"
    "x.asm|3||0|-1|32770|L|,gen,,\n"
    "x.asm|4||0|20|49160|L|,dup,,\n"
    "x.asm|5||0|20|49170|L|,dup,,\n";

static std::string tmp_path(const std::string& leaf) {
    struct Dir {
        std::string path = jtp::make_temp_dir("jnext_bpj_");
        ~Dir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    };
    static Dir dir;
    return (std::filesystem::path(dir.path) / leaf).generic_string();
}

struct Fix {
    Emulator                  emu;
    std::unique_ptr<Debugger> dbg;
    std::string               sld;
    Fix() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
        sld = tmp_path("prog.sld");
        std::ofstream(sld, std::ios::binary) << kSld;
        load_map();
    }
    void load_map() { dbg->load_source_map(sld, false); }
    std::vector<::SourceAddress> line10() const {
        return dbg->source_map().resolve("main.bas:10").starts;
    }
};

static BpSpec spec(BpSpec::Kind k, uint16_t addr, bool enabled = true) {
    BpSpec s;
    s.kind    = k;
    s.addr    = addr;
    s.enabled = enabled;
    return s;
}
static BpSpec qualified(MemSpace sp, uint16_t addr, bool enabled = true) {
    BpSpec s = spec(BpSpec::Execute, addr, enabled);
    s.space  = sp;
    return s;
}
static BpSpec source_line(const std::string& text, std::vector<::SourceAddress> locs,
                          bool enabled = true) {
    BpSpec s;
    s.file_line = text;
    s.locations = std::move(locs);
    s.enabled   = enabled;
    return s;
}

static const std::string kHeader =
    "; jnext debugger breakpoints (Breakpoints > Save as Script).\n"
    "; Load them with Breakpoints > Load from Script, or run them with jnext --script FILE.\n";

static std::vector<std::string> lines_of(const std::string& t) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < t.size()) {
        const size_t e = t.find('\n', i);
        if (e == std::string::npos) { out.push_back(t.substr(i)); break; }
        out.push_back(t.substr(i, e - i));
        i = e + 1;
    }
    return out;
}

static std::string specs_str(const std::vector<BpSpec>& v) {
    std::string s = "n=" + std::to_string(v.size());
    for (const BpSpec& b : v)
        s += " {k" + std::to_string(b.kind) + " a" + std::to_string(b.addr) + " " +
             jnext::dbg::space_tag(b.space) + (b.file_line.empty() ? "" : " " + b.file_line) +
             " locs" + std::to_string(b.locations.size()) + (b.enabled ? "" : " off") + "}";
    return s;
}

// ---- save --------------------------------------------------------------------

static void save_rows(Fix& f) {
    const Debugger& d = *f.dbg;
    {
        const std::vector<BpSpec> in = {
            spec(BpSpec::Execute, 0x8000),  spec(BpSpec::Read, 0x9000),   spec(BpSpec::Write, 0x9001),
            spec(BpSpec::ReadWrite, 0x9002), spec(BpSpec::IoRead, 0x00FE), spec(BpSpec::IoWrite, 0x243B),
            spec(BpSpec::IoRead, 0x00FF), spec(BpSpec::IoWrite, 0x0100)};
        const BpScript o = bp_script_text(in, d);
        const std::string want = kHeader +
            "bp1: on execute 0x8000 do stop end\n"
            "bp2: on read 0x9000 do stop end\n"
            "bp3: on write 0x9001 do stop end\n"
            "bp4: on read_write 0x9002 do stop end\n"
            "bp5: on io_read 0xFE do stop end\n"
            "bp6: on io_write 0x243B do stop end\n"
            "bp7: on io_read 0xFF do stop end\n"
            "bp8: on io_write 0x0100 do stop end\n";
        check("BPJ-01", "the six GUI types and the port spelling boundary (0xFF, 0x0100) are eight rules: execute, read, write, read_write, io_read (two digits for a "
                        "low-byte port, up to 0xFF), io_write (four, from 0x0100), each ending in a newline; nothing refused",
              o.text == want && o.refused.empty(), o.text);
    }
    {
        const BpScript o = bp_script_text({spec(BpSpec::Execute, 0x8000, false)}, d);
        check("BPJ-02", "a disabled breakpoint is written `disabled bp1: ...`",
              o.text == kHeader + "disabled bp1: on execute 0x8000 do stop end\n", o.text);
    }
    {
        // A refused spec between two written ones leaves no gap in the labels.
        const BpScript o = bp_script_text({spec(BpSpec::Execute, 0x8000), qualified(MemSpace::page(22), 0x8004),
                                           spec(BpSpec::Read, 0x9000)}, d);
        ParseResult pr = parse_script(o.text);
        std::vector<Diagnostic> errs;
        if (pr.ok()) {
            CheckOptions opts;
            opts.symbols = symbols_of(d);
            errs = check_script(pr.script, opts);
        }
        const auto ls = lines_of(o.text);
        check("BPJ-03", "the header is the two fixed comment lines, the text parses and checks, and labels count over "
                        "the WRITTEN rules only (bp1, bp2)",
              ls.size() == 5 && ls[0] + "\n" + ls[1] + "\n" == kHeader && ls[2] == "bp1: on execute 0x8000 do stop end" &&
                  ls[3].rfind("; not saved: ", 0) == 0 && ls[4] == "bp2: on read 0x9000 do stop end" && pr.ok() &&
                  errs.empty(),
              o.text + dstr(errs));
    }
    {
        const BpScript o = bp_script_text({qualified(MemSpace::page(20), 0xC000)}, d);
        check("BPJ-04", "an Execute qualified with page 20, whose symbol is `banked`, is `on execute @banked` and the "
                        "text has no `page` word",
              o.text == kHeader + "bp1: on execute @banked do stop end\n" && o.text.find("page") == std::string::npos &&
                  o.refused.empty(),
              o.text);
    }
    {
        const BpScript o = bp_script_text({qualified(MemSpace::page(22), 0x8004)}, d);
        check("BPJ-05", "a qualified Execute no symbol names is refused: named in `refused`, a `; not saved:` comment "
                        "in the file, no rule, no `page` word",
              o.refused.size() == 1 && o.refused[0].rfind("Execute $8004 P16: ", 0) == 0 &&
                  o.text.find("; not saved: Execute $8004 P16: ") != std::string::npos &&
                  o.text.find("bp1:") == std::string::npos && o.text.find("page") == std::string::npos,
              o.text);
    }
    {
        const auto fb = d.symbols().lookup(MemSpace::page(22), 0x8002);
        const auto key = d.symbols().find_name("gen");
        const BpScript o = bp_script_text({qualified(MemSpace::page(22), 0x8002)}, d);
        check("BPJ-06", "a qualified Execute whose only label is an UNPAGED one (`gen`: lookup() falls back to it, "
                        "find_name() has no memory) is refused, not written as `@gen`",
              fb && *fb == "gen" && key && !key->space && o.refused.size() == 1 && o.text.find("@gen") == std::string::npos,
              o.text);
    }
    {
        const auto nm = d.symbols().lookup(MemSpace::page(21), 0xC010);
        const BpScript o = bp_script_text({qualified(MemSpace::page(21), 0xC010)}, d);
        check("BPJ-07", "a paged label whose name is outside the @name grammar (`odd-name`) is refused",
              nm && *nm == "odd-name" && o.refused.size() == 1 && o.text.find("bp1:") == std::string::npos, o.text);
    }
    {
        const BpScript o = bp_script_text({source_line("main.bas:10", f.line10())}, d);
        check("BPJ-08", "a source line is `on execute \"FILE:LINE\"`, always quoted",
              o.text == kHeader + "bp1: on execute \"main.bas:10\" do stop end\n" && o.refused.empty(), o.text);
    }
    {
        const BpScript o = bp_script_text({source_line("a\".bas:1", {}), source_line("a${x}.bas:1", {}),
                                           source_line("a\nb.bas:1", {}), source_line("a\rb.bas:1", {})}, d);
        bool one_line = true;
        for (const std::string& l : lines_of(o.text))
            if (l.empty() || l[0] != ';') one_line = false;
        size_t notes = 0;
        for (const std::string& l : lines_of(o.text))
            if (l.rfind("; not saved: ", 0) == 0) ++notes;
        check("BPJ-09", "a file name a .jds string cannot hold (`\"`, `${`, a line end) is refused, each one, and every "
                        "comment stays on one line",
              o.refused.size() == 4 && notes == 4 && one_line, o.text);
    }
    {
        const auto starts = f.line10();
        const BpScript o = bp_script_text({source_line("main.bas:10", {starts[0]})}, d);
        check("BPJ-10", "a partial group (one of the line's two places gone) is refused",
              starts.size() == 2 && o.refused.size() == 1 && o.text.find("bp1:") == std::string::npos, o.text);
    }
    {
        const auto starts = f.line10();
        f.dbg->clear_source_map();
        const BpScript o = bp_script_text({source_line("main.bas:10", starts)}, d);
        f.load_map();
        check("BPJ-11", "a source line the map no longer resolves is refused",
              o.refused.size() == 1 && o.text.find("bp1:") == std::string::npos, o.text);
    }
}

static void save_rows2(Fix& f) {
    const Debugger& d = *f.dbg;
    {
        // Two labels `dup` in page 20 ($C008, $C012): find_name("dup") is the first one.
        const auto l1 = d.symbols().lookup(MemSpace::page(20), 0xC008);
        const auto l2 = d.symbols().lookup(MemSpace::page(20), 0xC012);
        const auto key = d.symbols().find_name("dup");
        const BpScript o = bp_script_text({qualified(MemSpace::page(20), 0xC008), qualified(MemSpace::page(20), 0xC012)}, d);
        check("BPJ-20", "two labels of one name in one page: the first address is written `@dup`, the second is refused "
                        "(it would reload at the first) and the refusal names the address mismatch",
              l1 && l2 && *l1 == "dup" && *l2 == "dup" && key && key->addr == 0xC008 && o.refused.size() == 1 &&
                  o.text.find("bp1: on execute @dup do stop end") != std::string::npos &&
                  o.text.find("bp2:") == std::string::npos &&
                  o.refused[0] == "Execute $C012 P14: the symbol `dup` names $C008, not $C012",
              o.text);
    }
    {
        // The right addresses, in another memory (page 30; the map builds them in pages 20 and 21).
        auto moved = f.line10();
        for (auto& l : moved) l.space = MemSpace::page(30);
        const BpScript o = bp_script_text({source_line("main.bas:10", moved)}, d);
        const BpSpec a = source_line("main.bas:10", f.line10());
        const BpSpec b = source_line("main.bas:10", moved);
        check("BPJ-21", "a source line whose places have the right addresses in another memory is refused; two specs "
                        "differing only in a place's memory are neither equal nor the same places",
              o.refused.size() == 1 && o.text.find("bp1:") == std::string::npos && !(a == b) &&
                  !same_locations(a.locations, b.locations) && a == a && same_locations(a.locations, f.line10()),
              o.text);
    }
}

// ---- load --------------------------------------------------------------------

static void load_rows(Fix& f) {
    const Debugger& d = *f.dbg;
    {
        const auto starts = f.line10();
        const std::vector<BpSpec> in = {
            spec(BpSpec::Execute, 0x8000),        spec(BpSpec::Read, 0x9000, false), spec(BpSpec::Write, 0x9001),
            spec(BpSpec::ReadWrite, 0x9002),      spec(BpSpec::IoRead, 0x00FE),      spec(BpSpec::IoWrite, 0x243B, false),
            qualified(MemSpace::page(20), 0xC000), source_line("main.bas:10", starts),
            source_line("main.bas:11", f.dbg->source_map().resolve("main.bas:11").starts, false)};
        const BpScript o = bp_script_text(in, d);
        const BpRead r = bp_specs_from_script(o.text, d);
        check("BPJ-12", "round trip: every spellable kind, two disabled, the @banked one and two source lines read back "
                        "equal to what was written",
              o.refused.empty() && r.ok() && r.specs == in, specs_str(in) + " vs " + specs_str(r.specs) + dstr(r.errors));
    }
    {
        const BpRead r = bp_specs_from_script(
            "; a comment\n"
            "on execute @gen do stop end\n"
            "on read @banked do stop end\n"
            "on write 32768 do stop end\n"
            "disabled d1: on execute 0x9000 do stop end\n"
            "on execute main.bas:10 do stop end\n"
            "on io_write 0x243B do stop end\n", d);
        const auto starts = f.line10();
        const std::vector<BpSpec> want = {
            spec(BpSpec::Execute, 0x8002), spec(BpSpec::Read, 0xC000), spec(BpSpec::Write, 32768),
            spec(BpSpec::Execute, 0x9000, false), source_line("main.bas:10", starts), spec(BpSpec::IoWrite, 0x243B)};
        check("BPJ-13", "hand-written forms decode: an unpaged `@gen` is cpu(), a Mem `@banked` is never qualified, "
                        "decimal, an unlabelled rule, a bare FILE:LINE, comments",
              r.ok() && r.specs == want, specs_str(r.specs) + dstr(r.errors));
    }
    {
        const char* const bad[] = {
            "var x = 1\non execute 0x8000 do stop end\n",
            "on execute 0x8000 once do stop end\n",
            "on execute 0x8000 when A == 1 do stop end\n",
            "on execute 0x8000 do stop \"msg\" end\n",
            "on execute 0x8000 do stop stop end\n",
            "on execute 0x8000 do log \"x\" end\n",
            "on read 0x9000..0x90FF do stop end\n",
            "on read 0x9000 page 1 do stop end\n",
            "on read page 1 do stop end\n",
            "on io_read mask 0xFF value 0xFE do stop end\n",
            "on io_read 0xFE..0xFF do stop end\n",
            "on execute 0x8000+1 do stop end\n",
            "on execute 0x10000 do stop end\n",
            "on read 0x10000 do stop end\n",
            "on frame do stop end\n",
            "on stop do stop end\n",
            "on nextreg 0x07 do stop end\n",
        };
        std::string miss;
        for (const char* t : bad) {
            const BpRead r = bp_specs_from_script(t, d);
            if (r.ok() || !r.specs.empty() || r.errors.size() != 1 || r.errors[0].pos.line != 1) miss += std::string("[") + t + "] ";
        }
        check("BPJ-14", "each of var, once, when, a message, two actions, log, a range, page, page-only, mask/value, an "
                        "io range, an expression address, an address past 0xFFFF (execute and read), on frame, on stop, on nextreg is refused with one positioned "
                        "error and no specs",
              miss.empty(), miss);
    }
    {
        const BpRead r = bp_specs_from_script("on execute 0x8000 do stop end\n"
                                              "on read 0x9000 do stop end\n"
                                              "on frame do log \"x\" end\n", d);
        check("BPJ-15", "all or nothing: two good rules then a bad one give no specs and one error, at line 3",
              !r.ok() && r.specs.empty() && r.errors.size() == 1 && r.errors[0].pos.line == 3, dstr(r.errors));
    }
    {
        f.dbg->clear_source_map();
        const BpRead r = bp_specs_from_script("on execute \"main.bas:10\" do stop end\n", d);
        f.load_map();
        check("BPJ-16", "a source line with no map loaded gives the engine's text: no source map is loaded",
              !r.ok() && r.specs.empty() && r.errors.size() == 1 &&
                  r.errors[0].message == "`main.bas:10`: no source map is loaded",
              dstr(r.errors));
    }
    {
        const BpRead a = bp_specs_from_script("on execute 0x8000 do stop\n", d);
        const BpRead b = bp_specs_from_script("on execute @nosuch do stop end\n", d);
        check("BPJ-17", "a parse error (missing `end`) and a check error (unknown @nosuch) pass through positioned",
              !a.ok() && a.specs.empty() && a.errors.size() == 1 && a.errors[0].pos.line >= 1 &&
                  !b.ok() && b.specs.empty() && !b.errors.empty() && b.errors[0].pos.line == 1 &&
                  b.errors[0].pos.column >= 12 && b.errors[0].message.find("nosuch") != std::string::npos,
              dstr(a.errors) + dstr(b.errors));
    }
}

// ---- the engine as a foreign reader ------------------------------------------

struct Expect {
    EventKind kind;
    uint16_t  lo, hi;
    MemSpace  space;
    uint16_t  port_mask, port_value;
    Access    access;
    bool      enabled;
};

static std::vector<Expect> expected_subs(const std::vector<BpSpec>& specs) {
    std::vector<Expect> out;
    for (const BpSpec& s : specs) {
        switch (s.kind) {
            case BpSpec::Execute:
                if (!s.file_line.empty()) {
                    for (const auto& l : s.locations)
                        out.push_back({EventKind::Execute, l.address, l.address, l.space, 0, 0, Access::ReadWrite, s.enabled});
                } else {
                    out.push_back({EventKind::Execute, s.addr, s.addr, s.space, 0, 0, Access::ReadWrite, s.enabled});
                }
                break;
            case BpSpec::Read:
            case BpSpec::Write:
            case BpSpec::ReadWrite:
                out.push_back({EventKind::Mem, s.addr, s.addr, MemSpace::cpu(), 0, 0,
                               s.kind == BpSpec::Read ? Access::Read : s.kind == BpSpec::Write ? Access::Write : Access::ReadWrite,
                               s.enabled});
                break;
            case BpSpec::IoRead:
            case BpSpec::IoWrite:
                out.push_back({EventKind::Port, 0, 0, MemSpace::cpu(), static_cast<uint16_t>(s.addr <= 0xFF ? 0x00FF : 0xFFFF),
                               s.addr, s.kind == BpSpec::IoRead ? Access::Read : Access::Write, s.enabled});
                break;
        }
    }
    return out;
}

static std::vector<jnext::dbg::SubscriptionInfo> engine_subs(const Debugger& d, const ScriptEngine& e) {
    std::vector<jnext::dbg::SubscriptionInfo> out;
    for (const auto& s : d.subscriptions(true))
        if (s.owner == e.client()) out.push_back(s);
    return out;
}

static void engine_rows() {
    {
        Fix g;
        const auto starts = g.line10();
        const std::vector<BpSpec> in = {
            spec(BpSpec::Execute, 0x8000),        spec(BpSpec::Read, 0x9000, false), spec(BpSpec::Write, 0x9001),
            spec(BpSpec::ReadWrite, 0x9002),      spec(BpSpec::IoRead, 0x00FE),      spec(BpSpec::IoWrite, 0x243B, false),
            qualified(MemSpace::page(20), 0xC000), source_line("main.bas:10", starts)};
        const BpScript o = bp_script_text(in, *g.dbg);
        ScriptEngine eng(*g.dbg);
        const LoadResult lr = eng.load(o.text, "bp.jds");
        const auto subs = engine_subs(*g.dbg, eng);
        const auto want = expected_subs(in);
        std::string miss;
        if (subs.size() != want.size()) miss = "count " + std::to_string(subs.size()) + " vs " + std::to_string(want.size());
        for (size_t i = 0; i < subs.size() && i < want.size(); ++i) {
            const auto& s = subs[i];
            const Expect& w = want[i];
            bool same = s.kind == w.kind && s.enabled == w.enabled &&
                        s.filter.space == w.space;
            if (w.kind != EventKind::Port) same = same && s.filter.lo == w.lo && s.filter.hi == w.hi;
            if (w.kind == EventKind::Port) same = same && s.filter.port_mask == w.port_mask && s.filter.port_value == w.port_value;
            if (w.kind != EventKind::Execute) same = same && s.access == w.access;
            if (w.kind == EventKind::Execute) same = same && !s.has_handler && s.action == jnext::dbg::Action::Stop;
            if (!same) miss += " #" + std::to_string(i);
        }
        check("BPJ-18", "the saved text loads in a ScriptEngine (jnext --script FILE) with no error or warning and "
                        "registers exactly the specs' subscriptions (kind, filter, space, port decode, access, enabled; "
                        "Execute ones static Stop with no handler; one per source-line place)",
              lr.ok() && lr.warnings.empty() && miss.empty(), dstr(lr.errors) + dstr(lr.warnings) + miss);
    }
    {
        Fix g;
        const BpRead lo = bp_specs_from_script("on io_read 0xFE do stop end\n", *g.dbg);
        const BpRead hi = bp_specs_from_script("on io_read 0x7FFE do stop end\n", *g.dbg);
        ScriptEngine e1(*g.dbg);
        e1.load("on io_read 0xFE do stop end\n", "a.jds");
        const auto s1 = engine_subs(*g.dbg, e1);
        ScriptEngine e2(*g.dbg);
        e2.load("on io_read 0x7FFE do stop end\n", "b.jds");
        const auto s2 = engine_subs(*g.dbg, e2);
        check("BPJ-19", "ports: 0xFE decodes to 0xFE and the engine registers mask 0x00FF value 0xFE; 0x7FFE decodes to "
                        "0x7FFE and registers mask 0xFFFF",
              lo.ok() && lo.specs.size() == 1 && lo.specs[0].addr == 0xFE && hi.ok() && hi.specs.size() == 1 &&
                  hi.specs[0].addr == 0x7FFE && s1.size() == 1 && s1[0].filter.port_mask == 0x00FF &&
                  s1[0].filter.port_value == 0xFE && s2.size() == 1 && s2[0].filter.port_mask == 0xFFFF &&
                  s2[0].filter.port_value == 0x7FFE,
              dstr(lo.errors) + dstr(hi.errors));
    }
}

int main() {
    std::printf("bp_script_test (GH #376)\n");
    {
        Fix f;
        save_rows(f);
        save_rows2(f);
        load_rows(f);
    }
    engine_rows();
    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
