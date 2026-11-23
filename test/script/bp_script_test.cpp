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
#include <optional>
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
            "on execute 0x8000 do stop \"msg\" end\n",
            "on execute 0x8000 do stop stop end\n",
            "on execute 0x8000 do log \"x\" end\n",
            "on read 0x9000 page 1 do stop end\n",
            "on read page 1 do stop end\n",
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
        check("BPJ-14", "each of var, once, a message, two actions, log, page, page-only, "
                        "an io range, an expression address, an address past 0xFFFF (execute and read), on frame, on stop, on nextreg is refused with one positioned "
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
    bool      conditional = false;   // GH #372: the rule has a `when`
};

static std::vector<Expect> expected_subs(const std::vector<BpSpec>& specs) {
    std::vector<Expect> out;
    for (const BpSpec& s : specs) {
        switch (s.kind) {
            case BpSpec::Execute:
                if (!s.file_line.empty()) {
                    for (const auto& l : s.locations)
                        out.push_back({EventKind::Execute, l.address, l.address, l.space, 0, 0, Access::ReadWrite, s.enabled,
                                       !s.condition.empty()});
                } else {
                    out.push_back({EventKind::Execute, s.addr, bp_effective_hi(s), s.space, 0, 0, Access::ReadWrite, s.enabled,
                                   !s.condition.empty()});
                }
                break;
            case BpSpec::Read:
            case BpSpec::Write:
            case BpSpec::ReadWrite:
                out.push_back({EventKind::Mem, s.addr, bp_effective_hi(s), MemSpace::cpu(), 0, 0,
                               s.kind == BpSpec::Read ? Access::Read : s.kind == BpSpec::Write ? Access::Write : Access::ReadWrite,
                               s.enabled, !s.condition.empty()});
                break;
            case BpSpec::IoRead:
            case BpSpec::IoWrite:
                out.push_back({EventKind::Port, 0, 0, MemSpace::cpu(), bp_effective_mask(s),
                               s.addr, s.kind == BpSpec::IoRead ? Access::Read : Access::Write, s.enabled,
                               !s.condition.empty()});
                break;
            case BpSpec::AudioWrite:   // checked by BPJ-47, not through this table
                break;
            case BpSpec::Interrupt:
            case BpSpec::Nmi:
                out.push_back({s.kind == BpSpec::Interrupt ? EventKind::IntAck : EventKind::Nmi, 0, 0, MemSpace::cpu(), 0, 0,
                               Access::ReadWrite, s.enabled, !s.condition.empty()});
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

static BpSpec counted(BpSpec b) {
    b.count_only = true;
    return b;
}

// ---- GH #372: Interrupt / NMI -----------------------------------

static void save_interrupt_rows(Fix& f) {
    const Debugger& d = *f.dbg;
    const std::vector<BpSpec> ints = {spec(BpSpec::Interrupt, 0), spec(BpSpec::Nmi, 0), spec(BpSpec::Interrupt, 0, false)};
    {
        const BpScript o = bp_script_text(ints, d);
        const std::string want = kHeader +
            "bp1: on interrupt do stop end\n"
            "bp2: on nmi do stop end\n"
            "disabled bp3: on interrupt do stop end\n";
        check("BPJ-22", "Interrupt and NMI are `on interrupt` / `on nmi` rules (the DSL's own events), a disabled one is "
                        "`disabled bp3: ...`; nothing refused",
              o.text == want && o.refused.empty(), o.text);
    }
    {
        const BpRead r = bp_specs_from_script("on interrupt do stop end\non nmi do stop end\n", *f.dbg);
        const BpRead rt = bp_specs_from_script(bp_script_text(ints, d).text, d);
        check("BPJ-23", "`on interrupt` / `on nmi` decode to kinds Interrupt / Nmi with no address, and the saved set reads back "
                        "equal",
              r.ok() && r.specs.size() == 2 && r.specs[0].kind == BpSpec::Interrupt && r.specs[1].kind == BpSpec::Nmi &&
                  r.specs[0].addr == 0 && rt.ok() && rt.specs == ints,
              specs_str(r.specs) + dstr(r.errors) + specs_str(rt.specs) + dstr(rt.errors));
    }
    {
        const char* names[] = {"Execute", "Read", "Write", "Read/Write", "IO Read", "IO Write", "Interrupt", "NMI"};
        std::string bad;
        for (int k = 0; k < 8; ++k)
            if (std::string(bp_kind_name(static_cast<BpSpec::Kind>(k))) != names[k]) bad += std::to_string(k) + " ";
        check("BPJ-25", "bp_kind_name gives the Type column's eight names for kinds 0..7 and the port rule is 00FF below 0100, "
                        "FFFF from it",
              bad.empty() && bp_port_mask_for(0x00FF) == 0x00FF && bp_port_mask_for(0x0100) == 0xFFFF, bad);
    }
}

static void save_count_rows(Fix& f) {
    const Debugger& d = *f.dbg;
    const std::vector<BpSpec> in = {counted(spec(BpSpec::Execute, 0x8000)), counted(spec(BpSpec::Read, 0x9000)),
                                    counted(spec(BpSpec::IoRead, 0x00FE)), counted(spec(BpSpec::Interrupt, 0)),
                                    counted(spec(BpSpec::Execute, 0x8001, false)), spec(BpSpec::Write, 0x9001)};
    {
        const BpScript o = bp_script_text(in, d);
        const std::string want = kHeader +
            "bp1: on execute 0x8000 do end\n"
            "bp2: on read 0x9000 do end\n"
            "bp3: on io_read 0xFE do end\n"
            "bp4: on interrupt do end\n"
            "disabled bp5: on execute 0x8001 do end\n"
            "bp6: on write 0x9001 do stop end\n";
        check("BPJ-26", "a count-only breakpoint is a rule with an EMPTY body (`do end`) for Execute, Read, IO Read, Interrupt "
                        "and a disabled one; a plain one still ends in `do stop end`",
              o.text == want && o.refused.empty(), o.text);
    }
    {
        const BpRead a = bp_specs_from_script("on execute 0x8000 do end\non execute 0x8001 do stop end\n", d);
        const BpRead rt = bp_specs_from_script(bp_script_text(in, d).text, d);
        const BpRead log = bp_specs_from_script("on execute 0x8000 do log \"x\" end\n", d);
        check("BPJ-27", "`do end` decodes to count_only and `do stop end` does not; the saved set reads back equal; a body of "
                        "`log` is still refused with one positioned error",
              a.ok() && a.specs.size() == 2 && a.specs[0].count_only && !a.specs[1].count_only && rt.ok() && rt.specs == in &&
                  !log.ok() && log.specs.empty() && log.errors.size() == 1 && log.errors[0].pos.line == 1,
              specs_str(a.specs) + dstr(a.errors) + specs_str(rt.specs) + dstr(rt.errors) + dstr(log.errors));
    }
    {
        Fix g;
        const auto starts = g.line10();
        const std::vector<BpSpec> src = {counted(source_line("main.bas:10", starts))};
        const BpScript o = bp_script_text(src, *g.dbg);
        const BpRead rt = bp_specs_from_script(o.text, *g.dbg);
        check("BPJ-29", "a count-only source line is `on execute \"FILE:LINE\" do end` and reads back with its places = "
                        "resolve().starts and count_only set",
              o.text == kHeader + "bp1: on execute \"main.bas:10\" do end\n" && rt.ok() && rt.specs == src &&
                  rt.specs.size() == 1 && rt.specs[0].count_only && same_locations(rt.specs[0].locations, starts),
              o.text + specs_str(rt.specs) + dstr(rt.errors));
    }
}


// ---- GH #372 stage 3: ranges, IO masks, conditions ------------------------------

static BpSpec ranged(BpSpec::Kind k, uint16_t lo, uint16_t hi) {
    BpSpec s = spec(k, lo);
    s.hi     = hi;
    return s;
}
static BpSpec masked(BpSpec::Kind k, uint16_t value, uint16_t mask) {
    BpSpec s = spec(k, value);
    s.mask   = mask;
    return s;
}
static BpSpec when(BpSpec s, const char* cond) {
    s.condition = cond;
    return s;
}

// Every new spelling in one set: the saver's input (BPJ-30..32), the decoder's expected output (BPJ-34) and
// the engine's input (BPJ-36).
static std::vector<BpSpec> s3_set(const std::vector<::SourceAddress>& line10) {
    return {ranged(BpSpec::Execute, 0x8000, 0x80FF),
            ranged(BpSpec::Read, 0x9000, 0x90FF),
            ranged(BpSpec::Write, 0x9100, 0x91FF),
            ranged(BpSpec::ReadWrite, 0x9200, 0x92FF),
            masked(BpSpec::IoRead, 0x00FE, 0xFFFF),
            masked(BpSpec::IoWrite, 0x0000, 0x0000),
            when(spec(BpSpec::Execute, 0x8000), "B == 2"),
            counted(when(spec(BpSpec::Read, 0x9000), "VALUE != 0")),
            when(source_line("main.bas:10", line10), "A == 1"),
            when(spec(BpSpec::Interrupt, 0), "FRAME > 3"),
            when(ranged(BpSpec::Write, 0xA000, 0xA0FF), "mem[0x9000] == 1 and A != 0"),
            spec(BpSpec::Execute, 0x8000, false)};
}

static void save_s3_rows(Fix& f) {
    const Debugger& d = *f.dbg;
    {
        BpSpec same = spec(BpSpec::Execute, 0x8000);
        same.hi     = 0x8000;
        const BpScript o = bp_script_text({ranged(BpSpec::Execute, 0x8000, 0x80FF), ranged(BpSpec::Read, 0x9000, 0x90FF),
                                           ranged(BpSpec::Write, 0x9100, 0x91FF), ranged(BpSpec::ReadWrite, 0x9200, 0x92FF), same}, d);
        const std::string want = kHeader +
            "bp1: on execute 0x8000..0x80FF do stop end\n"
            "bp2: on read 0x9000..0x90FF do stop end\n"
            "bp3: on write 0x9100..0x91FF do stop end\n"
            "bp4: on read_write 0x9200..0x92FF do stop end\n"
            "bp5: on execute 0x8000 do stop end\n";
        check("BPJ-30", "a range is `lo..hi` (four digits each) on execute, read, write and read_write; an end equal to the start is one address",
              o.text == want && o.refused.empty(), o.text);
    }
    {
        const BpScript o = bp_script_text({masked(BpSpec::IoRead, 0x00FE, 0xFFFF), masked(BpSpec::IoRead, 0x00FE, 0x00FF),
                                           masked(BpSpec::IoWrite, 0x243B, 0xFFFF), masked(BpSpec::IoWrite, 0x0000, 0x0000)}, d);
        const std::string want = kHeader +
            "bp1: on io_read mask 0xFFFF value 0x00FE do stop end\n"
            "bp2: on io_read 0xFE do stop end\n"
            "bp3: on io_write 0x243B do stop end\n"
            "bp4: on io_write mask 0x0000 value 0x0000 do stop end\n";
        check("BPJ-31", "a port mask is `mask M value V` only when it is not the address rule's own (FE with FFFF, 0 with 0); the rule's own mask keeps "
                        "the plain spelling",
              o.text == want && o.refused.empty(), o.text);
    }
    {
        const auto starts = f.line10();
        const BpScript o = bp_script_text({when(spec(BpSpec::Execute, 0x8000), "B == 2"),
                                           counted(when(spec(BpSpec::Execute, 0x8000), "B == 2")),
                                           when(source_line("main.bas:10", starts), "A == 1")}, d);
        const std::string want = kHeader +
            "bp1: on execute 0x8000 when B == 2 do stop end\n"
            "bp2: on execute 0x8000 when B == 2 do end\n"
            "bp3: on execute \"main.bas:10\" when A == 1 do stop end\n";
        check("BPJ-32", "a condition is ` when TEXT` before `do`: with a stop, count-only and on a source line",
              o.text == want && o.refused.empty(), o.text);
    }
    {
        const char* const bad[] = {"A ==\n1", "A == 1 ; x", "A == 1 do", "@nosuch == 1", "changed(A)", "PREV == 1"};
        std::vector<BpSpec> in;
        for (int i = 0; i < 6; ++i) in.push_back(when(spec(i == 5 ? BpSpec::Read : BpSpec::Execute, 0x8000 + i), bad[i]));
        const BpScript o = bp_script_text(in, d);
        int notes = 0, rules = 0;
        for (const std::string& l : lines_of(o.text)) {
            notes += l.rfind("; not saved:", 0) == 0;
            rules += l.rfind("bp", 0) == 0;
        }
        check("BPJ-33", "a condition that cannot be written back faithfully is refused, each with a `; not saved:` line and a refused entry, and "
                        "no rule is written for it: a line break, a `;` comment, a stray `do`, an unknown @symbol, a script-only form, PREV on a read",
              notes == 6 && rules == 0 && o.refused.size() == 6, o.text);
    }
    {
        const auto starts = f.line10();
        const std::vector<BpSpec> in = s3_set(starts);
        const BpScript o = bp_script_text(in, d);
        const BpRead rt = bp_specs_from_script(o.text, d);
        check("BPJ-34", "the decoder gives the ranges, masks and conditions back: the whole set (with a range-and-condition, an Interrupt "
                        "condition, a source line and plain rows) reads back equal",
              o.refused.empty() && rt.ok() && rt.specs == in, specs_str(rt.specs) + dstr(rt.errors) + o.text);
    }
    {
        struct Bad { const char* text; const char* why; };
        const Bad bad[] = {
            {"on execute 0x8000 when A == 1\n  and B == 2 do stop end\n", "multi-line when"},
            {"on execute 0x8000 when changed(A) do stop end\n", "script-only form"},
            {"on read 0x90FF..0x9000 do stop end\n", "range ends below its start"},
            {"on io_read mask 0x00FF value 0x01FE do stop end\n", "value outside the mask"},
            {"on io_read mask 0xFFFF value 0x0000 do stop end\n", "FFFF with 0"},
            {"on io_read mask 0x10000 value 0x0000 do stop end\n", "mask past 0xFFFF"},
            {"on io_read mask 0xFFFF value 0x10000 do stop end\n", "value past 0xFFFF"},
        };
        std::string miss;
        for (const Bad& b : bad) {
            const BpRead r = bp_specs_from_script(b.text, d);
            if (r.ok() || !r.specs.empty() || r.errors.size() != 1 || r.errors[0].pos.line < 1) miss += std::string("[") + b.why + "] ";
        }
        const BpRead sym = bp_specs_from_script("on execute @gen..@banked do stop end\n", d);
        const BpRead wrong = bp_specs_from_script("on read 0x90FF..0x9000 do stop end\n", d);
        check("BPJ-35", "the decoder refuses, each with one positioned error and no specs: a multi-line `when`, `when changed(A)`, a range ending "
                        "below its start (with the engine's text), a port value outside its mask, FFFF with 0; `@gen..@banked` decodes as an "
                        "unqualified range",
              miss.empty() && wrong.errors.size() == 1 &&
                  wrong.errors[0].message.find("address range ends below its start") != std::string::npos && sym.ok() &&
                  sym.specs.size() == 1 && sym.specs[0].space == MemSpace::cpu() && sym.specs[0].addr == 0x8002 &&
                  sym.specs[0].hi && *sym.specs[0].hi == 0xC000,
              miss + dstr(wrong.errors) + dstr(sym.errors));
    }
    {
        // BPJ-37: the kind-to-scope table is the checker's, not a second one.
        using jnext::script::EventType;
        const EventType types[] = {EventType::Execute, EventType::Read, EventType::Write, EventType::ReadWrite,
                                   EventType::IoRead, EventType::IoWrite, EventType::Interrupt, EventType::Nmi};
        std::string bad;
        for (int k = 0; k < 8; ++k) {
            EventSpec ev;
            ev.type = types[k];
            if (bp_scope(static_cast<BpSpec::Kind>(k)).describe() != scope_of(ev).describe()) bad += std::to_string(k) + " ";
        }
        check("BPJ-37", "bp_scope(kind) describes the same payload scope as the checker's scope_of for the same event, for all eight kinds",
              bad.empty(), bad);
    }
}

// BPJ-38 / BPJ-39: a non-ASCII file name, and the refusals the GUI cannot reach but the public codec promises.
static void save_s3_more_rows() {
    {
        Fix g;
        // main.bas replaced by a map whose file name is not ASCII (the lexer counts code points, the parser slices bytes)
        const std::string sld2 = tmp_path("nonascii.sld");
        std::ofstream(sld2, std::ios::binary)
            << "|SLD.data.version|1\n"
               "caf\xC3\xB1.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
               "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
               "caf\xC3\xB1.bas|10||0|20|49152|T|\n"
               "caf\xC3\xB1.bas|11||0|20|49154|T|\n";
        g.dbg->load_source_map(sld2, false);
        const auto starts = g.dbg->source_map().resolve("caf\xC3\xB1.bas:10").starts;
        const std::vector<BpSpec> in = {when(source_line("caf\xC3\xB1.bas:10", starts), "A == 1")};
        const BpScript o = bp_script_text(in, *g.dbg);
        const BpRead rt = bp_specs_from_script(o.text, *g.dbg);
        check("BPJ-38", "a source line whose file name is not ASCII, with a condition, is written and reads back equal",
              !starts.empty() && o.refused.empty() && rt.ok() && rt.specs == in, o.text + specs_str(rt.specs) + dstr(rt.errors));
    }
    {
        Fix g;
        const BpSpec mask_on_read = masked(BpSpec::Read, 0x9000, 0xFFFF);
        const BpSpec io_range     = ranged(BpSpec::IoRead, 0x00FE, 0x00FF);
        BpSpec qualified_range    = qualified(MemSpace::page(20), 0xC000);
        qualified_range.hi        = 0xC0FF;
        BpSpec line_range         = source_line("main.bas:10", g.line10());
        line_range.hi             = 0x80FF;
        const BpScript o = bp_script_text({mask_on_read, io_range, qualified_range, line_range}, *g.dbg);
        int notes = 0, rules = 0;
        for (const std::string& l : lines_of(o.text)) {
            notes += l.rfind("; not saved:", 0) == 0;
            rules += l.rfind("bp", 0) == 0;
        }
        check("BPJ-39", "the saver refuses, each with a `; not saved:` line and a refused entry: a port mask on a non-IO kind, an IO range, "
                        "a range tied to a memory, a range on a source line; no rule is written",
              notes == 4 && rules == 0 && o.refused.size() == 4, o.text);
    }
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

// BPJ-24: the ENGINE as the foreign reader of the Interrupt / NMI spellings.
static void engine_interrupt_rows() {
    {
        Fix g;
        const std::vector<BpSpec> in = {spec(BpSpec::Interrupt, 0), spec(BpSpec::Nmi, 0), spec(BpSpec::Interrupt, 0, false)};
        const BpScript o = bp_script_text(in, *g.dbg);
        ScriptEngine eng(*g.dbg);
        const LoadResult lr = eng.load(o.text, "int.jds");
        const auto subs = engine_subs(*g.dbg, eng);
        bool same = subs.size() == 3;
        if (same)
            same = subs[0].kind == EventKind::IntAck && subs[0].enabled && subs[1].kind == EventKind::Nmi && subs[1].enabled &&
                   subs[2].kind == EventKind::IntAck && !subs[2].enabled;
        check("BPJ-24", "the Interrupt / NMI text loads in a ScriptEngine with no error or warning and registers an IntAck, an "
                        "Nmi and a disabled IntAck subscription, in order",
              lr.ok() && lr.warnings.empty() && same, dstr(lr.errors) + dstr(lr.warnings) + std::to_string(subs.size()));
    }
    {
        Fix g;
        const auto starts = g.line10();
        const std::vector<BpSpec> in = {counted(spec(BpSpec::Execute, 0x8000)), counted(spec(BpSpec::Read, 0x9000)),
                                        counted(spec(BpSpec::IoRead, 0x00FE, false)), counted(spec(BpSpec::Interrupt, 0)),
                                        counted(source_line("main.bas:10", starts))};
        const BpScript o = bp_script_text(in, *g.dbg);
        ScriptEngine eng(*g.dbg);
        const LoadResult lr = eng.load(o.text, "count.jds");
        const auto subs = engine_subs(*g.dbg, eng);
        const auto want = expected_subs(in);
        std::string miss;
        if (subs.size() != want.size()) miss = "count " + std::to_string(subs.size()) + " vs " + std::to_string(want.size());
        for (size_t i = 0; i < subs.size() && i < want.size(); ++i) {
            const auto& sb = subs[i];
            const Expect& w = want[i];
            bool same = sb.kind == w.kind && sb.enabled == w.enabled && sb.action == jnext::dbg::Action::Continue && sb.has_handler;
            if (w.kind == EventKind::Execute || w.kind == EventKind::Mem) same = same && sb.filter.lo == w.lo && sb.filter.hi == w.hi;
            if (w.kind == EventKind::Port) same = same && sb.filter.port_mask == w.port_mask && sb.filter.port_value == w.port_value;
            if (!same) miss += " #" + std::to_string(i);
        }
        check("BPJ-28", "the count-only text loads in a ScriptEngine with no error or warning and registers every rule as "
                        "action Continue with a handler, with the specs' kind, filter and On flag (one per source-line place)",
              lr.ok() && lr.warnings.empty() && miss.empty(), dstr(lr.errors) + dstr(lr.warnings) + miss);
    }
}

// ---- Audio Write (GH #532) ---------------------------------------------------
// An audio breakpoint is `on ay_write ...` when it is an applied AY register write, and `on audio_write
// [ay_reg|ay_select|dac] ...` for every other target and for dropped-only: the codec never refuses one.

static BpSpec audio(BpAudio::Target t, uint8_t chip = 0xFF, std::optional<uint8_t> reg = {},
                    std::optional<uint8_t> value = {}, uint8_t mask = 0xFF, bool dropped = false) {
    BpSpec s;
    s.kind = BpSpec::AudioWrite;
    s.audio.target = t;
    s.audio.chip = chip;
    s.audio.reg = reg;
    s.audio.value = value;
    s.audio.mask = mask;
    s.audio.dropped_only = dropped;
    return s;
}

static void audio_rows(Fix& f) {
    const Debugger& d = *f.dbg;
    {
        BpSpec a = audio(BpAudio::AyRegister, 1, 13, 0x08, 0x0F);
        BpSpec b = a;  b.count_only = true;
        BpSpec c = a;  c.enabled = false;
        const BpScript o = bp_script_text({a, b, c}, d);
        const std::string want = kHeader +
            "bp1: on ay_write chip 1 reg 0x0D mask 0x0F value 0x08 do stop end\n"
            "bp2: on ay_write chip 1 reg 0x0D mask 0x0F value 0x08 do end\n"
            "disabled bp3: on ay_write chip 1 reg 0x0D mask 0x0F value 0x08 do stop end\n";
        const BpRead r = bp_specs_from_script(o.text, d);
        check("BPJ-40", "an AY register breakpoint {chip 1, R13, value 0x08, mask 0x0F} is `on ay_write chip 1 reg 0x0D mask 0x0F value 0x08`; count-only gives `do end`, disabled the `disabled ` prefix; all three read back equal",
              o.text == want && o.refused.empty() && r.ok() && r.specs.size() == 3 && r.specs[0] == a && r.specs[1] == b && r.specs[2] == c,
              o.text + dstr(r.errors));
    }
    {
        const BpSpec a = audio(BpAudio::AyRegister);
        const BpScript o = bp_script_text({a}, d);
        const BpRead r = bp_specs_from_script(o.text, d);
        check("BPJ-41", "an AY register breakpoint with any chip, any register and no value is `on ay_write do stop end` and reads back equal",
              o.text == kHeader + "bp1: on ay_write do stop end\n" && r.ok() && r.specs.size() == 1 && r.specs[0] == a, o.text + dstr(r.errors));
    }
    {
        const std::vector<BpSpec> in = {
            audio(BpAudio::AyChipSelect, 2),
            audio(BpAudio::AyChipSelect, 0xFF, {}, 0xFE, 0xFF),
            audio(BpAudio::Dac, 0xFF, {}, {}, 0xFF, true),
            audio(BpAudio::Dac, 0xFF, {}, 0x80, 0xC0),
            audio(BpAudio::Any),
            audio(BpAudio::Any, 0xFF, {}, {}, 0xFF, true),
            audio(BpAudio::AyRegister, 1, 7, {}, 0xFF, true),
            audio(BpAudio::AyRegister, 0xFF, 13, 0x0A, 0xFF, true),
        };
        const BpScript o = bp_script_text(in, d);
        const std::string want = kHeader +
            "bp1: on audio_write ay_select chip 2 do stop end\n"
            "bp2: on audio_write ay_select mask 0xFF value 0xFE do stop end\n"
            "bp3: on audio_write dac dropped do stop end\n"
            "bp4: on audio_write dac mask 0xC0 value 0x80 do stop end\n"
            "bp5: on audio_write do stop end\n"
            "bp6: on audio_write dropped do stop end\n"
            "bp7: on audio_write ay_reg chip 1 reg 0x07 dropped do stop end\n"
            "bp8: on audio_write ay_reg reg 0x0D mask 0xFF value 0x0A dropped do stop end\n";
        const BpRead r = bp_specs_from_script(o.text, d);
        bool same = r.ok() && r.specs.size() == in.size();
        for (size_t i = 0; same && i < in.size(); ++i) same = r.specs[i] == in[i];
        check("BPJ-42", "every other audio target is saved (AY chip select, DAC, Any, and dropped-only AY register), none refused: the eight `on audio_write` rules, each reading back equal",
              o.text == want && o.refused.empty() && same, o.text + dstr(r.errors));
    }
    {
        const BpRead a = bp_specs_from_script("on ay_write reg 8..10 do stop end\n", d);
        const BpRead b = bp_specs_from_script("on ay_write once do stop end\n", d);
        const BpRead c = bp_specs_from_script("on ay_write chip 0 do stop end\non audio_write dac chip 1+1 do stop end\n", d);
        const BpRead e = bp_specs_from_script("on audio_write ay_reg reg 5..5 mask 0x0F value 0x03 do stop end\n", d);
        check("BPJ-43", "loading refuses, all or nothing: a register range (`not one register`), `once`, and a non-literal chip (rule 1 is fine, rule 2 fails and no spec survives); `reg 5..5` is one register and loads",
              !a.ok() && a.specs.empty() && a.errors[0].message.find("not one register") != std::string::npos &&
                  !b.ok() && b.specs.empty() && b.errors[0].message.find("`once`") != std::string::npos &&
                  !c.ok() && c.specs.empty() && c.errors[0].message.find("not a number") != std::string::npos &&
                  e.ok() && e.specs.size() == 1 && e.specs[0].audio.reg == 5 && e.specs[0].audio.value == 3 && e.specs[0].audio.mask == 0x0F,
              dstr(a.errors) + dstr(b.errors) + dstr(c.errors) + dstr(e.errors));
    }
    {
        check("BPJ-44", "bp_condition_problem(AudioWrite, ...): `CHIP == 1 and ay[1, 7] == 0x38` is fine, `PORT == 1` names the payload it lacks",
              bp_condition_problem(BpSpec::AudioWrite, "CHIP == 1 and ay[1, 7] == 0x38", d).empty() &&
                  bp_condition_problem(BpSpec::AudioWrite, "TARGET == 1 and VALUE == 0xFE", d).empty() &&
                  bp_condition_problem(BpSpec::AudioWrite, "PORT == 1", d).find("PORT") != std::string::npos);
    }
    {
        BpSpec w = audio(BpAudio::Dac, 3, {}, 0x10, 0xF0);
        w.condition = "VALUE != PREV";
        const BpScript o = bp_script_text({w}, d);
        const BpRead r = bp_specs_from_script(o.text, d);
        check("BPJ-45", "an audio breakpoint with a condition is `... when VALUE != PREV do stop end` and reads back equal, condition included",
              o.text == kHeader + "bp1: on audio_write dac chip 3 mask 0xF0 value 0x10 when VALUE != PREV do stop end\n" &&
                  r.ok() && r.specs.size() == 1 && r.specs[0] == w, o.text + dstr(r.errors));
    }
    {
        BpSpec x = audio(BpAudio::AyRegister, 2, 8);
        BpSpec y = x;
        y.audio.reg = 9;
        BpSpec z = audio(BpAudio::Dac, 1, {}, 0x01, 0xFF);
        BpSpec zz = z;
        zz.audio.mask = 0x0F;   // mask matters only with a value: this one differs
        BpSpec n1 = audio(BpAudio::AyRegister);
        BpSpec n2 = n1;
        n2.audio.mask = 0x0F;   // no value: the mask is not part of the breakpoint
        check("BPJ-46", "BpSpec equality for an audio breakpoint compares target, chip, register, value (and mask with it) and dropped-only",
              x == x && !(x == y) && z == z && !(z == zz) && n1 == n2 && !(audio(BpAudio::Dac, 1) == audio(BpAudio::Dac, 2)) &&
                  !(audio(BpAudio::Any) == audio(BpAudio::Any, 0xFF, {}, {}, 0xFF, true)) &&
                  bp_audio_text(audio(BpAudio::AyRegister, 1, 13).audio) == "AY1 R13 restart");
    }
}

static void audio_dropped_dac_row(Fix& f) {
    const Debugger& d = *f.dbg;
    const BpRead a = bp_specs_from_script("on audio_write dac chip 2 dropped do stop end\n", d);
    const BpRead b = bp_specs_from_script("on audio_write dac dropped do stop end\n", d);
    const BpRead c = bp_specs_from_script("on audio_write ay_select chip 2 dropped do stop end\n", d);
    check("BPJ-48", "loading `dac chip N dropped` is refused with the reason (a dropped DAC write has no channel), all or nothing; `dac dropped` and `ay_select chip 2 dropped` load",
          !a.ok() && a.specs.empty() && a.errors[0].message.find("write `dac dropped`") != std::string::npos &&
              b.ok() && b.specs.size() == 1 && b.specs[0].audio.chip == 0xFF && b.specs[0].audio.dropped_only &&
              c.ok() && c.specs.size() == 1 && c.specs[0].audio.chip == 2,
          dstr(a.errors) + dstr(b.errors) + dstr(c.errors));
}

// BPJ-47: the ENGINE as the foreign reader of the audio spellings.
static void engine_audio_rows() {
    Fix g;
    const std::vector<BpSpec> in = {
        audio(BpAudio::AyRegister, 1, 13, 0x08, 0x0F), audio(BpAudio::AyChipSelect, 2), audio(BpAudio::Dac, 0xFF, {}, {}, 0xFF, true),
        audio(BpAudio::Any), audio(BpAudio::AyRegister, 0xFF, 7, {}, 0xFF, true)};
    const BpScript o = bp_script_text(in, *g.dbg);
    ScriptEngine eng(*g.dbg);
    const LoadResult lr = eng.load(o.text, "audio.jds");
    const auto subs = engine_subs(*g.dbg, eng);
    using namespace jnext::dbg;
    std::string miss;
    if (subs.size() != 5) miss = "count " + std::to_string(subs.size());
    else {
        const auto& s0 = subs[0].filter; const auto& s1 = subs[1].filter; const auto& s2 = subs[2].filter;
        const auto& s3 = subs[3].filter; const auto& s4 = subs[4].filter;
        if (!(s0.audio_targets == audio_target_bit(AudioTarget::AyRegister) && s0.chip == 1 && s0.regs == EventFilter::RegSet{13} &&
              s0.value_mask == 0x0F && s0.value_match == 0x08 && s0.audio_flags_none == AudioFlag::Dropped && s0.audio_flags_all == 0)) miss += " #0";
        if (!(s1.audio_targets == audio_target_bit(AudioTarget::AyChipSelect) && s1.chip == 2 && s1.regs.empty())) miss += " #1";
        if (!(s2.audio_targets == (audio_target_bit(AudioTarget::Dac) | audio_target_bit(AudioTarget::DacMirror)) && s2.chip == 0xFF &&
              s2.audio_flags_all == AudioFlag::Dropped && s2.audio_flags_none == 0)) miss += " #2";
        if (!(s3.audio_targets == AUDIO_TARGETS_ANY && s3.chip == 0xFF && s3.audio_flags_none == AudioFlag::Dropped)) miss += " #3";
        if (!(s4.audio_targets == audio_target_bit(AudioTarget::AyRegister) && s4.regs == EventFilter::RegSet{7} &&
              s4.audio_flags_all == AudioFlag::Dropped)) miss += " #4";
        for (const auto& si : subs) if (si.kind != EventKind::AudioWrite) miss += " kind";
    }
    check("BPJ-47", "the saved text of every audio target loads in a ScriptEngine with no error or warning and registers AudioWrite subscriptions with the filters the codec meant (target mask, chip, register, value/mask, applied-only or dropped-only)",
          lr.ok() && lr.warnings.empty() && miss.empty(), dstr(lr.errors) + dstr(lr.warnings) + miss + o.text);
}

// BPJ-36: the ENGINE as the foreign reader of the ranges, masks and conditions.
static void engine_s3_rows() {
    Fix g;
    const std::vector<BpSpec> in = s3_set(g.line10());
    const BpScript o = bp_script_text(in, *g.dbg);
    ScriptEngine eng(*g.dbg);
    const LoadResult lr = eng.load(o.text, "s3.jds");
    const auto subs = engine_subs(*g.dbg, eng);
    const auto want = expected_subs(in);
    std::string miss;
    if (subs.size() != want.size()) miss = "count " + std::to_string(subs.size()) + " vs " + std::to_string(want.size());
    for (size_t i = 0; i < subs.size() && i < want.size(); ++i) {
        const auto& sb = subs[i];
        const Expect& w = want[i];
        bool same = sb.kind == w.kind && sb.enabled == w.enabled && sb.has_condition == w.conditional;
        if (w.kind == EventKind::Execute || w.kind == EventKind::Mem) same = same && sb.filter.lo == w.lo && sb.filter.hi == w.hi;
        if (w.kind == EventKind::Port) same = same && sb.filter.port_mask == w.port_mask && sb.filter.port_value == w.port_value;
        if (!same) miss += " #" + std::to_string(i);
    }
    check("BPJ-36", "the text of every new spelling loads in a ScriptEngine with no error or warning, and its subscriptions carry the ranges, "
                    "the mask/value pairs and a condition on exactly the `when` rules",
          lr.ok() && lr.warnings.empty() && miss.empty(), dstr(lr.errors) + dstr(lr.warnings) + miss + o.text);
}

int main() {
    std::printf("bp_script_test (GH #376)\n");
    {
        Fix f;
        save_rows(f);
        save_rows2(f);
        load_rows(f);
        save_interrupt_rows(f);
        save_count_rows(f);
        save_s3_rows(f);
        save_s3_more_rows();
        audio_rows(f);
        audio_dropped_dac_row(f);
    }
    engine_audio_rows();
    engine_rows();
    engine_interrupt_rows();
    engine_s3_rows();
    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
