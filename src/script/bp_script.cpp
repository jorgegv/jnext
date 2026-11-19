#include "script/bp_script.h"

#include <algorithm>
#include <cstdio>
#include <tuple>

#include "debug/long_address.h"
#include "script/ast.h"
#include "script/check.h"
#include "script/expr_compiler.h"
#include "script/parser.h"

namespace jnext {
namespace script {

namespace {

// A port of 0x00-0xFF is a low-byte match, a higher one exact (the Add dialog's
// rule, and the engine's `io_read` rule): the digit count tells which.
std::string hex_for(const BpSpec& s) {
    char b[16];
    const bool port = s.kind == BpSpec::IoRead || s.kind == BpSpec::IoWrite;
    if (port && s.addr <= 0x00FF) std::snprintf(b, sizeof b, "0x%02X", s.addr);
    else                           std::snprintf(b, sizeof b, "0x%04X", s.addr);
    return b;
}

// The `@name` lexical grammar: ident(.ident)*.
bool is_symbol_name(const std::string& n) {
    auto start = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    auto more  = [&](char c) { return start(c) || (c >= '0' && c <= '9'); };
    if (n.empty() || !start(n[0])) return false;
    for (size_t i = 1; i < n.size(); ++i) {
        if (n[i] == '.') {
            if (i + 1 >= n.size() || !start(n[i + 1])) return false;
            continue;
        }
        if (!more(n[i])) return false;
    }
    return true;
}

}  // namespace

const char* bp_kind_name(BpSpec::Kind k) {
    static const char* const names[] = {"Execute", "Read", "Write", "Read/Write",
                                        "IO Read", "IO Write", "Interrupt", "NMI"};
    return k < sizeof names / sizeof *names ? names[k] : "?";
}

uint16_t bp_port_mask_for(uint16_t addr) { return addr <= 0x00FF ? 0x00FF : 0xFFFF; }

bool same_locations(std::vector<::SourceAddress> a, std::vector<::SourceAddress> b) {
    if (a.size() != b.size()) return false;
    auto key = [](const ::SourceAddress& x) {
        return std::make_tuple(static_cast<int>(x.space.kind), x.space.index, x.address);
    };
    auto less = [&](const ::SourceAddress& x, const ::SourceAddress& y) { return key(x) < key(y); };
    std::sort(a.begin(), a.end(), less);
    std::sort(b.begin(), b.end(), less);
    for (size_t i = 0; i < a.size(); ++i)
        if (!(a[i].space == b[i].space) || a[i].address != b[i].address) return false;
    return true;
}

namespace {

std::string one_line(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = '?';
    return o;
}

// The rule body of a breakpoint spec ("on EVENT FILTER"), or "" with `why` set
// when it has no faithful spelling.
std::string rule_for(const BpSpec& s, const dbg::Debugger& dbg, std::string& why) {
    switch (s.kind) {
        case BpSpec::Read:      return "on read " + hex_for(s);
        case BpSpec::Write:     return "on write " + hex_for(s);
        case BpSpec::ReadWrite: return "on read_write " + hex_for(s);
        case BpSpec::IoRead:    return "on io_read " + hex_for(s);
        case BpSpec::IoWrite:   return "on io_write " + hex_for(s);
        case BpSpec::Interrupt: return "on interrupt";
        case BpSpec::Nmi:       return "on nmi";
        case BpSpec::Execute:   break;
    }
    if (!s.file_line.empty()) {
        if (s.file_line.find('"') != std::string::npos || s.file_line.find("${") != std::string::npos ||
            s.file_line.find('\n') != std::string::npos || s.file_line.find('\r') != std::string::npos) {
            why = "a script string cannot hold this file name";
            return "";
        }
        if (!same_locations(dbg.source_map().resolve(s.file_line).starts, s.locations)) {
            why = "the line no longer resolves to the places it was set on";
            return "";
        }
        return "on execute \"" + s.file_line + "\"";
    }
    if (s.space == dbg::MemSpace::cpu()) return "on execute " + hex_for(s);
    // A qualified Execute: only a lone `@name` carries its memory into a script.
    const auto name = dbg.symbols().lookup(s.space, s.addr);
    if (!name) {
        why = "no symbol names it in that memory";
        return "";
    }
    if (!is_symbol_name(*name)) {
        why = "its symbol `" + one_line(*name) + "` cannot be written as @name";
        return "";
    }
    const auto key = dbg.symbols().find_name(*name);
    const auto addr = dbg.lookup_name(*name);
    if (!key || !key->space || !(*key->space == s.space) || !addr) {
        why = "the symbol `" + *name + "` does not name that memory";
        return "";
    }
    if (*addr != s.addr) {
        char b[48];
        std::snprintf(b, sizeof b, " names $%04X, not $%04X", *addr, s.addr);
        why = "the symbol `" + *name + "`" + b;
        return "";
    }
    return "on execute @" + *name;
}

std::string where_of(const BpSpec& s) {
    if (!s.file_line.empty()) return one_line(s.file_line);
    return dbg::long_address(s.addr, s.space, dbg::AddressStyle::Friendly, false);
}

}  // namespace

bool operator==(const BpSpec& a, const BpSpec& b) {
    return a.kind == b.kind && a.addr == b.addr && a.space == b.space && a.file_line == b.file_line &&
           a.enabled == b.enabled && same_locations(a.locations, b.locations);
}

BpScript bp_script_text(const std::vector<BpSpec>& specs, const dbg::Debugger& dbg) {
    BpScript out;
    out.text =
        "; jnext debugger breakpoints (Breakpoints > Save as Script).\n"
        "; Load them with Breakpoints > Load from Script, or run them with jnext --script FILE.\n";
    int n = 0;
    for (const BpSpec& s : specs) {
        std::string why;
        const std::string rule = rule_for(s, dbg, why);
        if (rule.empty()) {
            const std::string msg = std::string(bp_kind_name(s.kind)) + " " + where_of(s) + ": " + why;
            out.refused.push_back(msg);
            out.text += "; not saved: " + msg + "\n";
            continue;
        }
        out.text += std::string(s.enabled ? "" : "disabled ") + "bp" + std::to_string(++n) + ": " +
                    rule + " do stop end\n";
    }
    return out;
}

namespace {

// All or nothing: an error empties the specs read so far.
void fail(BpRead& r, SourcePos pos, const std::string& msg) {
    r.errors.push_back(Diagnostic{pos, msg});
    r.specs.clear();
}

std::string not_a_bp(const char* what) {
    return std::string("not a breakpoint the Breakpoints panel holds (") + what +
           "); load this file from the Script panel instead";
}

}  // namespace

BpRead bp_specs_from_script(const std::string& text, const dbg::Debugger& dbg) {
    BpRead out;
    ParseResult pr = parse_script(text);
    if (!pr.ok()) {
        out.errors.push_back(*pr.error);
        return out;
    }
    CheckOptions opts;
    opts.symbols = symbols_of(dbg);
    out.errors = check_script(pr.script, opts);
    if (!out.errors.empty()) return out;

    if (!pr.script.vars.empty()) {
        fail(out, pr.script.vars.front().pos, not_a_bp("a `var`"));
        return out;
    }
    for (const Rule& r : pr.script.rules) {
        const EventSpec& ev = r.event;
        BpSpec s;
        switch (ev.type) {
            case EventType::Execute:   s.kind = BpSpec::Execute; break;
            case EventType::Read:      s.kind = BpSpec::Read; break;
            case EventType::Write:     s.kind = BpSpec::Write; break;
            case EventType::ReadWrite: s.kind = BpSpec::ReadWrite; break;
            case EventType::IoRead:    s.kind = BpSpec::IoRead; break;
            case EventType::IoWrite:   s.kind = BpSpec::IoWrite; break;
            case EventType::Interrupt: s.kind = BpSpec::Interrupt; break;
            case EventType::Nmi:       s.kind = BpSpec::Nmi; break;
            default: fail(out, ev.pos, not_a_bp("another kind of event")); return out;
        }
        if (r.once)  { fail(out, r.pos, not_a_bp("`once`")); return out; }
        if (r.when)  { fail(out, r.when->pos, not_a_bp("`when`")); return out; }
        if (r.body.size() != 1 || r.body[0].kind != ActionKind::Stop) {
            fail(out, r.body.empty() ? r.pos : r.body[0].pos, not_a_bp("the body is not a lone `stop`"));
            return out;
        }
        if (r.body[0].s1) { fail(out, r.body[0].pos, not_a_bp("`stop` with a message")); return out; }
        s.enabled = !r.disabled;
        if (s.kind == BpSpec::Interrupt || s.kind == BpSpec::Nmi) {
            out.specs.push_back(std::move(s));
            continue;
        }

        if (!ev.file_line.empty()) {
            const ::SourceResolution res = dbg.source_map().resolve(ev.file_line);
            if (res.starts.empty()) {
                std::string why;
                if (dbg.source_map().empty())
                    why = "`" + ev.file_line + "`: no source map is loaded";
                else if (!res.error.empty())
                    why = res.error;
                else
                    why = "unknown source line `" + ev.file_line + "` (not in the loaded source map)";
                fail(out, ev.file_line_pos, why);
                return out;
            }
            s.file_line = ev.file_line;
            s.locations = res.starts;
            out.specs.push_back(std::move(s));
            continue;
        }
        // `page`-only and `mask`/`value` filters have no `lo`, so `!ev.lo` refuses them too.
        if (!ev.lo || ev.hi || ev.page_lo) {
            fail(out, ev.pos, not_a_bp("the filter is not one address"));
            return out;
        }
        if (ev.lo->kind != ExprKind::Int && ev.lo->kind != ExprKind::Symbol) {
            fail(out, ev.lo->pos, not_a_bp("the address is not a number or an @symbol"));
            return out;
        }
        if (ev.lo->value < 0 || ev.lo->value > 0xFFFF) {
            fail(out, ev.lo->pos, "the address must be in 0..0xFFFF");
            return out;
        }
        s.addr = static_cast<uint16_t>(ev.lo->value);
        // Only a lone `@name` on `execute` carries the symbol's own memory (as the engine does).
        if (s.kind == BpSpec::Execute && ev.lo->kind == ExprKind::Symbol) {
            const auto key = dbg.symbols().find_name(ev.lo->text);
            if (key && key->space) s.space = *key->space;
        }
        out.specs.push_back(std::move(s));
    }
    return out;
}

}  // namespace script
}  // namespace jnext
