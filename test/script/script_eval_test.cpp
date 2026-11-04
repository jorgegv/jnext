// jnext::script — the DSL evaluator: value model, variables, snapshot stacks,
// interpolation and run-time errors (GH #26 WP2, epic #276 package S).
//
// The contract is doc/design/debug-subsystem/dsl-frontend.md §2.1 (values),
// §2.3 (state and payload names), §2.5 (snapshots), §6.5 (errors); the
// choices WP2 made where §2 is silent are its "WP2 as built" appendix (H).
//
//   SEV-TYPE-*    the static types check_script gives and refuses
//   SEV-VAR-*     variables: slots, initializers (order, wrap), rules
//   SEV-COND-*    make_condition: a `when` as the backend predicate
//   SEV-SNAP-*    the snapshot stacks: push/pop/depth/fields, both bounds
//   SEV-CHANGED-* changed() per group, exactly its fields
//   SEV-DIFF-*    dump_diff's lines
//   SEV-FMT-*, SEV-INTERP-*  interpolation and its formats
//   SEV-STR-*     string comparison
//   SEV-ERR-*     run-time errors: position, message, no escape
//   SEV-BUILTINS  every §2.3 state name read through a script
//   SEV-WORK-*    the §3(b) and §3(c) scripts evaluated end to end
//   SEV-STACK     the deepest accepted shapes on a 1 MB stack
//   EVAL-CVAR-*   `cvar(PATH)` (GH #436, E353-06): C variables of frame 0 as int32, over the committed z88dk
//                 fixtures on a real Next machine (the family name of script_parse_test's EVAL-* rows; the
//                 IDs are global and distinct)
//
// The inspection surface is a REAL `jnext::dbg::Debugger` over a 48K machine:
// §8 plans "a fake inspection surface", but `Debugger` is a concrete class in
// the frozen backend headers, so there is nothing to fake it behind.
//
// Run: ./build/test/script_eval_test

#include "script/check.h"
#include "script/evaluator.h"
#include "script/expr_compiler.h"
#include "script/parser.h"
#include "script/state.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/c_value.h"
#include "debug/debugger.h"
#include "platform/emulator_boot.h"

#include <pthread.h>

#include <climits>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"

using namespace jnext::script;
using jnext::dbg::Debugger;
using jnext::dbg::EventKind;
using jnext::dbg::EventSource;
// `Event` is also a GLOBAL name (core/scheduler.h, via core/emulator.h).
using DbgEvent = jnext::dbg::Event;

// ── Tiny test harness (matches debugger_backend_test) ──────────────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " — ", detail.c_str());
    }
}

// ── Helpers ────────────────────────────────────────────────────────────────

static std::string dstr(const std::optional<Diagnostic>& d) {
    return d ? d->to_string() : std::string("(no error)");
}

static std::string dstr(const std::vector<Diagnostic>& ds) {
    if (ds.empty()) return "(no errors)";
    std::string s;
    for (const Diagnostic& d : ds) s += "[" + d.to_string() + "] ";
    return s;
}

static bool err_at(const std::optional<Diagnostic>& d, int line, int col, const std::string& needle) {
    return d && d->pos.line == line && d->pos.column == col &&
           d->message.find(needle) != std::string::npos;
}

static bool one_err_at(const std::vector<Diagnostic>& ds, int line, int col, const std::string& needle) {
    return ds.size() == 1 && err_at(ds[0], line, col, needle);
}

static std::string rep_str(const std::string& s, int n) {
    std::string out;
    for (int k = 0; k < n; ++k) out += s;
    return out;
}

static std::string chain(int n) { return "1" + rep_str("+1", n); }

static std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof b, "%X", v);
    return b;
}

static Emulator* g_emu = nullptr;
static Debugger* g_dbg = nullptr;

/// A checked script with its interpreter state.
struct Loaded {
    ParseResult                  pr;
    std::vector<Diagnostic>      errs;
    std::shared_ptr<ScriptState> st;
    bool ok() const { return pr.ok() && errs.empty(); }
    Script& s() { return pr.script; }
};

static Loaded load(const std::string& src, const SymbolResolver& syms = {}) {
    Loaded L;
    L.pr = parse_script(src);
    if (!L.pr.ok()) return L;
    CheckOptions o;
    o.symbols = syms;
    L.errs = check_script(L.pr.script, o);
    if (L.errs.empty()) L.st = std::make_shared<ScriptState>(L.pr.script);
    return L;
}

/// Errors of loading `src` (the parse error, or the check's).
static std::vector<Diagnostic> errs_of(const std::string& src) {
    Loaded L = load(src);
    if (!L.pr.ok()) return {*L.pr.error};
    return L.errs;
}

static EvalContext ctx_of(Loaded& L, const DbgEvent* ev = nullptr, const std::string* reason = nullptr) {
    return EvalContext{*g_dbg, ev, L.st.get(), reason};
}

/// Evaluate rule `k`'s `when`. Empty + `err` on a run-time error.
static std::optional<int32_t> when_of(Loaded& L, size_t k, std::optional<Diagnostic>* err = nullptr,
                                      const DbgEvent* ev = nullptr, const std::string* reason = nullptr) {
    if (!L.ok() || k >= L.s().rules.size() || !L.s().rules[k].when) return std::nullopt;
    try {
        return eval_int(*L.s().rules[k].when, ctx_of(L, ev, reason));
    } catch (const EvalError& e) {
        if (err) *err = e.d;
        return std::nullopt;
    }
}

static std::optional<Diagnostic> when_err(Loaded& L, size_t k, const DbgEvent* ev = nullptr) {
    std::optional<Diagnostic> e;
    when_of(L, k, &e, ev);
    return e;
}

/// Interpolate, turning an unexpected run-time error into visible text so the
/// row fails by name rather than aborting its group.
static std::string safe_interp(const StringLit& s, const EvalContext& ctx) {
    try {
        return interpolate(s, ctx);
    } catch (const EvalError& e) {
        return "<EvalError " + e.d.to_string() + ">";
    }
}

static void set_regs(uint16_t af, uint16_t hl, uint16_t sp, bool iff1, bool iff2, uint8_t im) {
    Z80Registers r = g_emu->cpu().get_registers();
    r.AF  = af;     r.BC  = 0x3456; r.DE  = 0x789A; r.HL  = hl;
    r.AF2 = 0x1357; r.BC2 = 0x2468; r.DE2 = 0x369C; r.HL2 = 0x48AD;
    r.IX  = 0x5BE0; r.IY  = 0x6CF1; r.SP  = sp;     r.PC  = 0x8000;
    r.I = 0x3F; r.R = 0x2A; r.IFF1 = iff1; r.IFF2 = iff2; r.IM = im; r.halted = false;
    g_emu->cpu().set_registers(r);
}

static void poke16(uint16_t a, uint16_t v) {
    g_emu->mmu().write(a, static_cast<uint8_t>(v & 0xFF));
    g_emu->mmu().write(static_cast<uint16_t>(a + 1), static_cast<uint8_t>(v >> 8));
}

/// The standard machine state the snapshot rows start from.
static void base_state() {
    set_regs(0x1295, 0xBCDE, 0xFF00, true, false, 2);
    poke16(0xFF00, 0x1234);
}

// ── The worked scripts (dsl-frontend.md), copied verbatim ─────────────────

// §3(a) guard.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3A = R"JDS(# guard.jds — code-area write protection (ChaseTheBug ranges)
var armed = 0

disabled rom_guard: on write 0x0000..0x3FFF do
    log "write to code area ${ADDR:x4} (page ${PAGE:x2}) <- ${VALUE:x2} from PC ${PC:x4} src ${SOURCE}"
    stop "write into MMU0/1 code area"
end

disabled main_guard: on write 0x8000..(@__data_crt_head - 1) do
    log "write to main code ${ADDR:x4} <- ${VALUE:x2} from PC ${PC:x4}"
    stop "write into main code area"
end
)JDS";

// §3(b) the MMU guard — verbatim from dsl-frontend.md (v6).
static const char* const S3B = R"JDS(disabled mmu_guard: on nextreg 0x51 when not ((nextreg[0x50] + 1 == VALUE) or (nextreg[0x50] == 0xFF and VALUE == 0xFF)) do
    log "MMU0 is ${nextreg[0x50]:x2} whereas MMU1 write is ${VALUE:x2} (src ${SOURCE}, PC ${PC:x4})"
    stop "MMU1 inconsistent with MMU0"
end

on nextreg 0x51 when armed == 1 do
    log "MMU1 write ${VALUE:x2}"
end
)JDS";

// §3(c) isr.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3C = R"JDS(# isr.jds
on execute @isr do
    snap isr
    log indent (depth(isr) * 2) "==> isr at ${PC:x4}"
end

on execute @isr_exit do
    log indent (depth(isr) * 2) "<== isr at ${PC:x4}, Ret=${stack[0]:x4}"
    if depth(isr) == 0 then
        log "Warning: isr exit with empty entry stack"
    else
        if changed(isr, regs) then
            log "Warning: registers differ on exit from isr"
            dump_diff isr
            stop "isr clobbered registers"
        end
        if changed(isr, stack0) then
            log "Warning: top of stack modified ${isr.STACK0:x4} vs ${stack[0]:x4}"
            stop "isr modified return address"
        end
        if not IFF1 then
            stop "isr exit with interrupts disabled"
        end
        if changed(isr, mmu) then
            dump_diff isr
            stop "isr changed an MMU slot"
        end
        unsnap isr
    end
end
)JDS";


// ── SEV-TYPE — the value model's static types ──────────────────────────────

static void type_rows() {
    struct T { const char* id; const char* desc; const char* src; int col; const char* msg; };
    // col 0 = must load with no error.
    static const T ROWS[] = {
        {"SEV-TYPE-STR-EQ", "a string compares with a string: `REASON == \"x\"` loads", "on stop when REASON == \"x\" do end", 0, ""},
        {"SEV-TYPE-LIT-EQ", "two string literals compare", "on frame when \"a\" == \"b\" do end", 0, ""},
        {"SEV-TYPE-INTERP-STR", "a string interpolates into a string: `${REASON}`", "on stop do log \"why: ${REASON}\" end", 0, ""},
        {"SEV-TYPE-MIXED", "a string compared with an integer is refused at the operator",
         "on stop when REASON == 1 do end", 21, "`==` compares a string with an integer"},
        {"SEV-TYPE-ORDER", "`<` on strings is refused: only `==` / `!=` compare strings",
         "on frame when \"a\" < 1 do end", 15, "an operand of `<` must be an integer, not a string"},
        {"SEV-TYPE-ARITH", "a string in arithmetic is refused at the string",
         "on frame when \"a\" + 1 == 2 do end", 15, "an operand of `+` must be an integer, not a string"},
        {"SEV-TYPE-UNARY", "`not` of a string is refused", "on stop when not REASON do end", 18,
         "the operand of `not` must be an integer"},
        {"SEV-TYPE-COND", "a string condition is refused", "on frame when \"a\" do end", 15,
         "an integer is needed here, not a string"},
        {"SEV-TYPE-SET", "a string cannot be assigned: variables are integers",
         "var v = 0 on frame do set v = \"x\" end", 31, "an integer is needed here, not a string"},
        {"SEV-TYPE-INIT", "a string cannot initialize a variable", "var v = \"x\"", 9,
         "an integer is needed here, not a string"},
        {"SEV-TYPE-INDEX", "a string index is refused", "on frame when mem[\"x\"] == 0 do end", 19,
         "an index must be an integer"},
        {"SEV-TYPE-FILTER", "a string filter bound is refused", "on execute \"x\" do end", 12,
         "an integer is needed here, not a string"},
        {"SEV-TYPE-FMT", "a format applies to integers: `${REASON:x2}` is refused at the expression",
         "on stop do log \"${REASON:x2}\" end", 19, "a format (x2, x4, d) applies to an integer"},
        {"SEV-TYPE-FMT-X4", "`${REASON:x4}` is refused", "on stop do log \"${REASON:x4}\" end", 19,
         "a format (x2, x4, d) applies to an integer"},
        {"SEV-TYPE-FMT-D", "`${REASON:d}` is refused", "on stop do log \"${REASON:d}\" end", 19,
         "a format (x2, x4, d) applies to an integer"},
        {"SEV-TYPE-ARITH-RIGHT", "a string as the RIGHT operand of arithmetic is refused at the string",
         "on frame when 1 + \"a\" == 2 do end", 19, "an operand of `+` must be an integer, not a string"},
        {"SEV-TYPE-PHYS-PAGE", "a string `phys[]` page is refused", "on frame when phys[\"a\", 0] == 0 do end", 20,
         "a `phys[]` page must be an integer"},
        {"SEV-TYPE-PHYS-OFFSET", "a string `phys[]` offset is refused", "on frame when phys[0, \"a\"] == 0 do end",
         23, "a `phys[]` offset must be an integer"},
        {"SEV-TYPE-FIELD-INDEX", "a string snapshot-field index is refused", "on frame when s.MMU[\"a\"] == 0 do end",
         21, "an index must be an integer"},
    };
    for (const T& r : ROWS) {
        const auto e = errs_of(r.src);
        const bool ok = r.col == 0 ? e.empty() : one_err_at(e, 1, r.col, r.msg);
        check(r.id, r.desc, ok, dstr(e));
    }
    {
        Loaded L = load("on stop when REASON == \"x\" do end on stop do log \"${REASON}${1}\" end");
        const bool typed = L.ok() && L.s().rules[0].when->a->type == ValueType::Str &&
                           L.s().rules[0].when->type == ValueType::Int &&
                           L.s().rules[1].body[0].s1->parts[0].expr->type == ValueType::Str &&
                           L.s().rules[1].body[0].s1->parts[1].expr->type == ValueType::Int;
        check("SEV-TYPE-ANNOTATED", "every expression carries its static type: REASON Str, the comparison Int",
              typed, dstr(L.errs));
    }
}

// ── SEV-VAR — variables ────────────────────────────────────────────────────

static void var_rows() {
    {
        Loaded L = load("var a = 2\nvar b = a * 3\nvar c = b + CYCLE - CYCLE\nvar big = 0x7FFFFFFF + 1");
        bool ok = L.ok();
        std::string got;
        if (ok) {
            try {
                init_vars(L.s(), *L.st, *g_dbg);
                ok = L.st->var(0) == 2 && L.st->var(1) == 6 && L.st->var(2) == 6 && L.st->var(3) == INT32_MIN &&
                     L.st->var_name(3) == "big";
            } catch (const EvalError& e) {
                ok  = false;
                got = e.d.to_string();
            }
        }
        check("SEV-VAR-INIT", "initializers run in declaration order, each reading the ones before, and wrap",
              ok, dstr(L.errs) + got);
    }
    check("SEV-VAR-SELF", "an initializer reading ITSELF is refused at the name (it is not declared yet)",
          one_err_at(errs_of("var x = x"), 1, 9, "variable `x` is used before its declaration"),
          dstr(errs_of("var x = x")));
    check("SEV-VAR-ORDER", "an initializer reading a LATER variable is refused at the name",
          one_err_at(errs_of("var b = a\nvar a = 1"), 1, 9, "variable `a` is used before its declaration"),
          dstr(errs_of("var b = a\nvar a = 1")));
    {
        Loaded L = load("on frame when z == 0 do set z = 1 end\nvar y = 5\nvar z = 0");
        const bool ok = L.ok() && L.s().rules[0].when->a->slot == 1 &&
                        L.s().rules[0].body[0].target->slot == 1 && L.st->var_count() == 2;
        check("SEV-VAR-SLOTS", "a rule may read any variable, wherever declared; a var's slot is its "
              "declaration index, in expressions and `set` targets alike",
              ok, dstr(L.errs));
    }
    {
        Loaded L = load("var v = 0\non frame when v == 7 do end");
        bool ok = L.ok();
        if (ok) {
            const auto a = when_of(L, 0);
            L.st->set_var(0, 7);
            const auto b = when_of(L, 0);
            ok = a && *a == 0 && b && *b == 1;
        }
        check("SEV-VAR-READ", "a variable reads the state's current value", ok);
    }
}

// ── SEV-COND — make_condition ──────────────────────────────────────────────

static void cond_rows() {
    {
        Loaded L = load("on write 0x4000 when VALUE == 5 and PREV == 3 do end");
        bool ok = L.ok();
        if (ok) {
            const auto p = make_condition(L.s().rules[0].when, L.st, {});
            DbgEvent ev;
            ev.kind  = EventKind::Mem;
            ev.value = 5;
            ev.prev  = 3;
            const bool t = p(ev, *g_dbg);
            ev.prev = 4;
            ok = t && !p(ev, *g_dbg);
        }
        check("SEV-COND-PAYLOAD", "a rule's `when` as the backend predicate reads the delivered payload", ok);
    }
    {
        Loaded L = load("var armed = 0\non nextreg 0x51 when armed == 1 do end");
        bool ok = L.ok();
        if (ok) {
            const auto p = make_condition(L.s().rules[0].when, L.st, {});
            const DbgEvent ev;
            const bool before = p(ev, *g_dbg);
            L.st->set_var(0, 1);
            ok = !before && p(ev, *g_dbg);
        }
        check("SEV-COND-STATE", "the predicate reads the script's live state (§5.2), not a copy", ok);
    }
    {
        Loaded L = load("var d = 0\non frame when 10 / d == 1 do end");
        std::vector<Diagnostic> seen;
        bool ok = L.ok();
        if (ok) {
            const auto p = make_condition(L.s().rules[0].when, L.st,
                                          [&seen](const Diagnostic& d) { seen.push_back(d); });
            const bool r = p(DbgEvent{}, *g_dbg);
            ok = !r && seen.size() == 1 && err_at(seen[0], 2, 18, "division by zero");
        }
        check("SEV-COND-ERROR", "a run-time failure makes the predicate false and is reported once, positioned",
              ok, dstr(seen));
    }
}

// ── SEV-SNAP / SEV-CHANGED / SEV-DIFF — the snapshot stacks ─────────────────

static const char* const FIELDS[] = {
    "A", "B", "C", "D", "E", "H", "L", "F", "I", "R", "AF", "BC", "DE", "HL", "IX", "IY",
    "SP", "PC", "AF2", "BC2", "DE2", "HL2", "IFF1", "IFF2", "IM", "STACK0", "FRAME", "CYCLE",
    "MMU[0]", "MMU[1]", "MMU[2]", "MMU[3]", "MMU[4]", "MMU[5]", "MMU[6]", "MMU[7]",
};

/// What field `f` must read for a capture taken in the given state.
static int32_t expected_field(const std::string& f, const Z80Registers& r, uint16_t stack0,
                              const std::array<jnext::dbg::SlotInfo, 8>& slots,
                              const jnext::dbg::Time& t) {
    if (f == "A") return r.AF >> 8;
    if (f == "F") return r.AF & 0xFF;
    if (f == "B") return r.BC >> 8;
    if (f == "C") return r.BC & 0xFF;
    if (f == "D") return r.DE >> 8;
    if (f == "E") return r.DE & 0xFF;
    if (f == "H") return r.HL >> 8;
    if (f == "L") return r.HL & 0xFF;
    if (f == "I") return r.I;
    if (f == "R") return r.R;
    if (f == "AF") return r.AF;
    if (f == "BC") return r.BC;
    if (f == "DE") return r.DE;
    if (f == "HL") return r.HL;
    if (f == "IX") return r.IX;
    if (f == "IY") return r.IY;
    if (f == "SP") return r.SP;
    if (f == "PC") return r.PC;
    if (f == "AF2") return r.AF2;
    if (f == "BC2") return r.BC2;
    if (f == "DE2") return r.DE2;
    if (f == "HL2") return r.HL2;
    if (f == "IFF1") return r.IFF1 ? 1 : 0;
    if (f == "IFF2") return r.IFF2 ? 1 : 0;
    if (f == "IM") return r.IM;
    if (f == "STACK0") return stack0;
    if (f == "FRAME") return static_cast<int32_t>(t.frame);
    if (f == "CYCLE") return static_cast<int32_t>(static_cast<uint32_t>(t.master_cycle));
    return slots[static_cast<size_t>(f[4] - '0')].nr_page;  // MMU[k]
}

static void snap_rows() {
    // One rule per field, `on frame when s.<F> == 0 do end`; the rows read the
    // left operand of each `when`.
    std::string src = "on frame do snap s unsnap s dump_diff s end\n";
    for (const char* f : FIELDS) src += std::string("on frame when s.") + f + " == 0 do end\n";
    src += "on frame when depth(s) == 0 do end\n";
    Loaded L = load(src);
    const SourcePos at{9, 9};
    check("SEV-SNAP-SLOTS", "snapshot names get slots in order of first appearance, on actions and expressions",
          L.ok() && L.s().snapshots.size() == 1 && L.s().snapshots[0] == "s" && L.s().rules[0].body[0].slot == 0 &&
              L.s().rules[1].when->a->slot == 0 && L.st->stack_count() == 1,
          dstr(L.errs));
    if (!L.ok()) return;
    const size_t DEPTH_RULE = 1 + sizeof(FIELDS) / sizeof(FIELDS[0]);
    auto field = [&](size_t k, std::optional<Diagnostic>* err = nullptr) -> std::optional<int32_t> {
        try {
            return eval_int(*L.s().rules[1 + k].when->a, ctx_of(L));
        } catch (const EvalError& e) {
            if (err) *err = e.d;
            return std::nullopt;
        }
    };
    auto depth = [&]() { return eval_int(*L.s().rules[DEPTH_RULE].when->a, ctx_of(L)); };

    {
        std::optional<Diagnostic> e;
        const bool none = !field(0, &e);
        check("SEV-SNAP-EMPTY-FIELD", "a field of an empty stack is a run-time error at the field",
              none && err_at(e, 2, 15, "snapshot stack `s` is empty"), dstr(e));
        check("SEV-SNAP-EMPTY-DEPTH", "`depth()` of an empty stack is 0, not an error", depth() == 0);
    }
    {
        bool threw = false;
        try {
            L.st->unsnap(0, at);
        } catch (const EvalError& e) {
            threw = err_at(e.d, 9, 9, "`unsnap`: snapshot stack `s` is empty");
        }
        check("SEV-SNAP-UNDERFLOW", "`unsnap` of an empty stack is a run-time error at the action; the stack stays empty",
              threw && L.st->depth(0) == 0);
    }
    base_state();
    const Z80Registers r0 = g_dbg->registers();
    const auto slots0 = g_dbg->mmu_slots();
    const jnext::dbg::Time t0 = g_dbg->time();
    L.st->snap(0, *g_dbg, at);
    // Move the clock on (parked on `JR $`, interrupts off), then every
    // register and the word at SP, so a field that read the LIVE machine
    // instead of the capture would differ.
    set_regs(0x1295, 0xBCDE, 0xFF00, false, false, 2);
    g_emu->run_frame();
    set_regs(0x4321, 0x1111, 0xFF00, false, true, 1);
    {
        Z80Registers r = g_dbg->registers();
        r.BC = 0x0101; r.DE = 0x0202; r.AF2 = 0x0303; r.BC2 = 0x0404; r.DE2 = 0x0505; r.HL2 = 0x0606;
        r.IX = 0x0707; r.IY = 0x0808; r.SP = 0xFE00; r.PC = 0x9000; r.I = 0x01; r.R = 0x02;
        g_emu->cpu().set_registers(r);
    }
    poke16(0xFE00, 0x7777);
    {
        std::string bad;
        for (size_t k = 0; k < sizeof(FIELDS) / sizeof(FIELDS[0]); ++k) {
            const auto v = field(k);
            const int32_t want = expected_field(FIELDS[k], r0, 0x1234, slots0, t0);
            if (!v || *v != want) bad += std::string(FIELDS[k]) + "=" + (v ? hex(static_cast<unsigned>(*v)) : "err") +
                                         "!=" + hex(static_cast<unsigned>(want)) + " ";
        }
        check("SEV-SNAP-FIELDS", "every field of the §2.5 record reads the CAPTURE, not the live machine",
              bad.empty() && depth() == 1, bad);
    }
    {
        base_state();
        L.st->snap(0, *g_dbg, at);  // depth 2: HL = BCDE again, A = 12
        set_regs(0x9900, 0x2222, 0xFF00, true, false, 2);
        L.st->snap(0, *g_dbg, at);  // depth 3: HL = 2222, A = 99
        const auto top = field(13);  // HL
        L.st->unsnap(0, at);
        const auto mid = field(13);
        const auto a_mid = field(0);
        check("SEV-SNAP-NESTED", "fields read the TOP entry; `unsnap` exposes the one below; `depth()` counts",
              top && *top == 0x2222 && mid && *mid == 0xBCDE && a_mid && *a_mid == 0x12 && depth() == 2);
        L.st->unsnap(0, at);
        L.st->unsnap(0, at);
    }
    {
        base_state();
        bool full_threw = false;
        for (size_t k = 0; k < ScriptState::MAX_SNAPSHOT_DEPTH; ++k) L.st->snap(0, *g_dbg, at);
        const size_t at_max = L.st->depth(0);
        try {
            L.st->snap(0, *g_dbg, at);
        } catch (const EvalError& e) {
            full_threw = err_at(e.d, 9, 9, "snapshot stack `s` is full (4096 entries)");
        }
        const size_t after = L.st->depth(0);
        L.st->unsnap(0, at);
        check("SEV-SNAP-OVERFLOW", "a stack holds 4096 entries; one more is a run-time error at the action, and "
              "nothing is pushed or dropped",
              ScriptState::MAX_SNAPSHOT_DEPTH == 4096 && at_max == 4096 && full_threw && after == 4096 &&
                  L.st->depth(0) == 4095);
        while (L.st->depth(0) > 0) L.st->unsnap(0, at);
    }
}

static void names_rows() {
    Loaded L = load("on frame when a.HL == b.HL do snap b end on frame when depth(a) * 10 + depth(b) == 0 do "
                    "snap a end");
    bool ok = L.ok() && L.s().snapshots.size() == 2 && L.s().snapshots[0] == "a" && L.s().snapshots[1] == "b";
    std::string got;
    if (ok) {
        const int sa = L.s().rules[0].when->a->slot;
        const int sb = L.s().rules[0].when->b->slot;
        set_regs(0x1295, 0x1111, 0xFF00, false, false, 2);
        L.st->snap(sa, *g_dbg, SourcePos{});
        set_regs(0x1295, 0x2222, 0xFF00, false, false, 2);
        L.st->snap(sb, *g_dbg, SourcePos{});
        L.st->snap(sb, *g_dbg, SourcePos{});
        const Expr& w0 = *L.s().rules[0].when;
        const int32_t ha = eval_int(*w0.a, ctx_of(L));
        const int32_t hb = eval_int(*w0.b, ctx_of(L));
        const int32_t dd = eval_int(*L.s().rules[1].when->a, ctx_of(L));
        ok = sa == 0 && sb == 1 && ha == 0x1111 && hb == 0x2222 && dd == 12;
        got = std::to_string(sa) + "/" + std::to_string(sb) + " " + hex(static_cast<unsigned>(ha)) + " " +
              hex(static_cast<unsigned>(hb)) + " " + std::to_string(dd);
        L.st->unsnap(sa, SourcePos{});
        L.st->unsnap(sb, SourcePos{});
        L.st->unsnap(sb, SourcePos{});
    }
    check("SEV-SNAP-NAMES", "each name is its own stack: `a` and `b` keep their own entries and depths", ok,
          got + dstr(L.errs));
}

static void changed_rows() {
    Loaded L = load("on frame when changed(s, regs) do end on frame when changed(s, mmu) do end "
                    "on frame when changed(s, iff1) do end on frame when changed(s, stack0) do end");
    if (!L.ok()) {
        check("SEV-CHANGED-LOAD", "the changed() fixture loads", false, dstr(L.errs));
        return;
    }
    const SourcePos at{1, 1};
    auto ch = [&](size_t g) { return when_of(L, g); };
    {
        std::optional<Diagnostic> e;
        when_of(L, 0, &e);
        check("SEV-CHANGED-EMPTY", "`changed()` of an empty stack is a run-time error at the call",
              err_at(e, 1, 15, "snapshot stack `s` is empty"), dstr(e));
    }
    // The regs group: exactly AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP.
    {
        uint16_t Z80Registers::*const IN[] = {&Z80Registers::AF, &Z80Registers::BC, &Z80Registers::DE,
                                              &Z80Registers::HL, &Z80Registers::IX, &Z80Registers::IY,
                                              &Z80Registers::AF2, &Z80Registers::BC2, &Z80Registers::DE2,
                                              &Z80Registers::HL2, &Z80Registers::SP};
        std::string bad;
        base_state();
        L.st->snap(0, *g_dbg, at);
        const auto none = ch(0);
        int k = 0;
        for (auto f : IN) {
            Z80Registers r = g_dbg->registers();
            const uint16_t old = r.*f;
            r.*f = static_cast<uint16_t>(old ^ 0x0100);
            g_emu->cpu().set_registers(r);
            const auto v = ch(0);
            if (!v || *v != 1) bad += "in" + std::to_string(k) + " ";
            r.*f = old;
            g_emu->cpu().set_registers(r);
            ++k;
        }
        // Outside the group: PC, I, R, IFF1, IFF2, IM.
        {
            Z80Registers r = g_dbg->registers();
            r.PC = 0x1234; r.I = 0x55; r.R = 0x66; r.IFF1 = 0; r.IFF2 = 1; r.IM = 0;
            g_emu->cpu().set_registers(r);
            const auto v = ch(0);
            if (!v || *v != 0) bad += "outside ";
        }
        check("SEV-CHANGED-REGS", "`changed(s, regs)` sees each of AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP, and not "
              "PC I R IFF1 IFF2 IM",
              none && *none == 0 && bad.empty(), bad);
        L.st->unsnap(0, at);
    }
    {
        base_state();
        L.st->snap(0, *g_dbg, at);
        std::string bad;
        const auto none = ch(1);
        for (int s = 0; s < 8; ++s) {
            const uint8_t reg = static_cast<uint8_t>(0x50 + s);
            const uint8_t old = g_dbg->nextreg_peek(reg);
            g_emu->nextreg().write(reg, static_cast<uint8_t>(old == 0x10 ? 0x11 : 0x10));
            const auto v = ch(1);
            if (!v || *v != 1) bad += "slot" + std::to_string(s) + " ";
            g_emu->nextreg().write(reg, old);
        }
        const auto restored = ch(1);
        check("SEV-CHANGED-MMU", "`changed(s, mmu)` sees each of the 8 slots, slot 7 included",
              none && *none == 0 && bad.empty() && restored && *restored == 0, bad);
        L.st->unsnap(0, at);
    }
    {
        base_state();
        L.st->snap(0, *g_dbg, at);  // IFF1 = 1, IFF2 = 0
        const auto unchanged = ch(2);  // nothing moved; IFF1 != IFF2 in this fixture
        set_regs(0x1295, 0xBCDE, 0xFF00, true, true, 2);
        const auto iff2_only = ch(2);
        set_regs(0x1295, 0xBCDE, 0xFF00, false, false, 2);
        const auto iff1 = ch(2);
        check("SEV-CHANGED-IFF1", "`changed(s, iff1)` is 0 with nothing changed (IFF1 1, IFF2 0), sees IFF1, and "
              "not IFF2",
              unchanged && *unchanged == 0 && iff2_only && *iff2_only == 0 && iff1 && *iff1 == 1);
        L.st->unsnap(0, at);
    }
    {
        base_state();  // SP FF00, word 1234
        L.st->snap(0, *g_dbg, at);
        set_regs(0x1295, 0xBCDE, 0xFE00, true, false, 2);
        poke16(0xFE00, 0x1234);
        const auto same_word = ch(3);
        const auto regs      = ch(0);
        poke16(0xFE00, 0x4321);
        const auto other_word = ch(3);
        check("SEV-CHANGED-STACK0", "`changed(s, stack0)` compares the word at the CURRENT SP with the captured one: "
              "a moved SP over the same word is unchanged",
              same_word && *same_word == 0 && regs && *regs == 1 && other_word && *other_word == 1);
        L.st->unsnap(0, at);
    }
}

static void diff_rows() {
    Loaded L = load("on frame do snap s end");
    if (!L.ok()) return;
    const SourcePos at{1, 13};
    base_state();
    L.st->snap(0, *g_dbg, at);
    const auto nothing = L.st->diff(0, *g_dbg, at);
    // Change HL, IFF1, IM, the word at SP and MMU3; also PC, R and the clock,
    // which dump_diff does not report.
    const uint8_t mmu3 = g_dbg->nextreg_peek(0x53);
    const uint8_t mmu3_new = static_cast<uint8_t>(mmu3 == 0x10 ? 0x11 : 0x10);
    set_regs(0x1295, 0xBCDE, 0xFF00, false, false, 2);
    g_emu->run_frame();  // the clock moves: FRAME and CYCLE differ, unreported
    set_regs(0x1295, 0x5678, 0xFF00, false, false, 1);
    {
        Z80Registers r = g_dbg->registers();
        r.PC = 0x9999; r.R = 0x77;
        g_emu->cpu().set_registers(r);
    }
    poke16(0xFF00, 0x4321);
    g_emu->nextreg().write(0x53, mmu3_new);
    const auto lines = L.st->diff(0, *g_dbg, at);
    char m[32];
    std::snprintf(m, sizeof m, "MMU3 %02X -> %02X", mmu3, mmu3_new);
    const std::vector<std::string> want = {"HL BCDE -> 5678", "IFF1 1 -> 0", "IM 2 -> 1",
                                           "STACK0 1234 -> 4321", m};
    std::string got;
    for (const auto& l : lines) got += "[" + l + "] ";
    check("SEV-DIFF", "dump_diff: one line per differing field, in record order, `NAME old -> new`; PC, R, "
          "FRAME and CYCLE are not compared",
          nothing.empty() && lines == want, got);
    g_emu->nextreg().write(0x53, mmu3);
    L.st->unsnap(0, at);
    bool threw = false;
    try {
        L.st->diff(0, *g_dbg, at);
    } catch (const EvalError& e) {
        threw = err_at(e.d, 1, 13, "snapshot stack `s` is empty");
    }
    check("SEV-DIFF-EMPTY", "dump_diff of an empty stack is a run-time error at the action", threw);
    {
        // Nested: the diff is against the TOP entry, not the oldest.
        base_state();                                            // HL BCDE
        L.st->snap(0, *g_dbg, at);
        set_regs(0x1295, 0x1111, 0xFF00, true, false, 2);
        L.st->snap(0, *g_dbg, at);                               // top: HL 1111
        set_regs(0x1295, 0x2222, 0xFF00, true, false, 2);
        const auto lines = L.st->diff(0, *g_dbg, at);
        check("SEV-DIFF-TOP", "on a nested stack dump_diff compares with the TOP entry",
              lines == std::vector<std::string>{"HL 1111 -> 2222"}, lines.empty() ? "" : lines[0]);
        L.st->unsnap(0, at);
        L.st->unsnap(0, at);
    }
    {
        // IFF2, and a STACK0 value whose 4-digit padding shows.
        set_regs(0x1295, 0xBCDE, 0xFF00, true, false, 2);
        poke16(0xFF00, 0x0012);
        L.st->snap(0, *g_dbg, at);
        set_regs(0x1295, 0xBCDE, 0xFF00, true, true, 2);
        poke16(0xFF00, 0x0034);
        const auto lines = L.st->diff(0, *g_dbg, at);
        std::string got;
        for (const auto& l : lines) got += "[" + l + "] ";
        check("SEV-DIFF-IFF2-STACK0", "dump_diff reports IFF2, and STACK0 as 4 hex digits",
              lines == std::vector<std::string>{"IFF2 0 -> 1", "STACK0 0012 -> 0034"}, got);
        L.st->unsnap(0, at);
        poke16(0xFF00, 0x1234);
    }
}

// ── Interpolation and strings ──────────────────────────────────────────────

static void interp_rows() {
    check("SEV-FMT", "formats: none/d signed decimal; x2/x4 upper-case hex of the 32-bit pattern, at least 2/4 digits",
          format_int(5, Fmt::None) == "5" && format_int(-5, Fmt::D) == "-5" && format_int(5, Fmt::X2) == "05" &&
              format_int(255, Fmt::X2) == "FF" && format_int(0xAB, Fmt::X4) == "00AB" &&
              format_int(0x1234, Fmt::X2) == "1234" && format_int(-1, Fmt::X4) == "FFFFFFFF" &&
              format_int(INT32_MIN, Fmt::None) == "-2147483648",
          format_int(0x1234, Fmt::X2) + " " + format_int(-1, Fmt::X4));
    {
        base_state();
        Loaded L = load("on frame do log \"a ${A:x2} b ${HL:x4} c ${-3} d ${7:d} e\" end");
        std::string got;
        if (L.ok()) got = safe_interp(*L.s().rules[0].body[0].s1, ctx_of(L));
        check("SEV-INTERP", "a string's text and its `${…}` pieces, each in its format",
              got == "a 12 b BCDE c -3 d 7 e", got + dstr(L.errs));
    }
    {
        Loaded L = load("on stop do log \"why=${REASON}!\" end");
        const std::string reason = "MemPoint";
        std::string got;
        if (L.ok()) got = safe_interp(*L.s().rules[0].body[0].s1, ctx_of(L, nullptr, &reason));
        check("SEV-INTERP-STR", "a string piece is inserted as it is", got == "why=MemPoint!", got);
    }
    {
        Loaded L = load("on frame do log \"x ${1 / (A - A)}\" end");
        std::optional<Diagnostic> e;
        if (L.ok()) {
            try {
                interpolate(*L.s().rules[0].body[0].s1, ctx_of(L));
            } catch (const EvalError& x) {
                e = x.d;
            }
        }
        check("SEV-INTERP-ERR", "a run-time failure inside `${…}` is reported at its true position",
              err_at(e, 1, 24, "division by zero"), dstr(e));
    }
    {
        Loaded L = load("on frame when \"a${1 + 1}\" == \"a2\" do end\non frame when \"a\" != \"a\" do end\n"
                        "on stop when REASON == \"stop ${3}\" do end");
        const std::string reason = "stop 3";
        const auto a = when_of(L, 0);
        const auto b = when_of(L, 1);
        const auto c = when_of(L, 2, nullptr, nullptr, &reason);
        check("SEV-STR-EQ", "`==`/`!=` compare string text, interpolations evaluated",
              a && *a == 1 && b && *b == 0 && c && *c == 1, dstr(L.errs));
    }
}

// ── SEV-ERR — run-time errors ──────────────────────────────────────────────

static void evaluate_rows() {
    Loaded L = load("on stop when REASON == \"x\" and depth(s) == 300 do end on frame do snap s end");
    const std::string reason = "MemPoint";
    bool ok = L.ok();
    std::string got;
    if (ok) {
        const Expr& w = *L.s().rules[0].when;  // (REASON == "x") and (depth(s) == 300)
        try {
            const Value vs = evaluate(*w.a->a, ctx_of(L, nullptr, &reason));
            const Value vi = evaluate(*w.b->b, ctx_of(L));
            ok = vs.type == ValueType::Str && vs.s == "MemPoint" && vi.type == ValueType::Int && vi.i == 300;
            got = vs.s + " " + std::to_string(vi.i);
        } catch (const EvalError& e) {
            ok  = false;
            got = e.d.to_string();
        }
    }
    check("SEV-EVALUATE", "evaluate() returns a string for a string expression and an integer for an integer one",
          ok, got + dstr(L.errs));
    if (!L.ok()) return;
    base_state();
    for (int k = 0; k < 300; ++k) L.st->snap(0, *g_dbg, SourcePos{});
    int32_t d = -1;
    try {
        d = eval_int(*L.s().rules[0].when->b->a, ctx_of(L));
    } catch (const EvalError&) {
    }
    check("SEV-SNAP-DEPTH-WIDE", "`depth()` is not limited to 8 bits: 300 entries read 300", d == 300,
          std::to_string(d));
    while (L.st->depth(0) > 0) L.st->unsnap(0, SourcePos{});
}

static void err_rows() {
    {
        Loaded L = load("on frame when @foo == 1 do end");  // no resolver: left unresolved
        check("SEV-ERR-UNRESOLVED", "an `@symbol` left unresolved (no MAP) is a run-time error at the `@`, not 0",
              L.ok() && err_at(when_err(L, 0), 1, 15, "`@foo` is not resolved"), dstr(when_err(L, 0)));
    }
    {
        Loaded L = load("on stop when REASON == \"x\" do end");
        std::optional<Diagnostic> e;
        when_of(L, 0, &e);  // no reason supplied
        check("SEV-ERR-REASON", "`REASON` evaluated without a stop delivery is a run-time error, not \"\"",
              err_at(e, 1, 14, "`REASON` has no value here"), dstr(e));
    }
    {
        Loaded L = load("on frame do snap s end on frame when s.MMU[8] == 0 do end on frame when s.MMU[-1] == 0 do end");
        base_state();
        bool ok = L.ok();
        if (ok) {
            L.st->snap(0, *g_dbg, SourcePos{});
            ok = err_at(when_err(L, 1), 1, 38, "a slot is 0..7") && err_at(when_err(L, 2), 1, 73, "a slot is 0..7");
            L.st->unsnap(0, SourcePos{});
        }
        check("SEV-ERR-MMU-INDEX", "a snapshot `MMU[n]` outside 0..7 is a run-time error at the field", ok,
              dstr(when_err(L, 1)));
    }
    {
        Loaded L = load("var d = 0\non frame when mem[d - 1] == 0 do end");
        check("SEV-ERR-ACCESSOR", "script accessors keep their run-time checks (mem[] outside 0..0xFFFF)",
              L.ok() && err_at(when_err(L, 0), 2, 15, "outside 0..0xFFFF"), dstr(when_err(L, 0)));
    }
}

// ── SEV-BUILTINS — every §2.3 state name through a script ──────────────────

static void builtin_rows() {
    static const char* const NAMES[] = {
        "A", "B", "C", "D", "E", "H", "L", "F", "I", "R", "AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC",
        "AF2", "BC2", "DE2", "HL2", "CF", "ZF", "SF", "PF", "HF", "NF", "IFF1", "IFF2", "IM", "HALTED",
        "FRAME", "CYCLE", "TFRAME", "RAW_HC", "RAW_VC", "HC_ULA", "VC_ULA", "CVC", "PHC", "AUDIO_MUTE",
        "MACHINE", "mem[0x9000]", "mem16[0x9000]", "nextreg[0x7F]", "mmu[3]", "page[3]", "stack[0]",
        "phys[5, 0]",
    };
    base_state();
    std::string src;
    for (const char* n : NAMES) src += std::string("var v") + std::to_string(src.size()) + " = " + n + "\n";
    Loaded L = load(src);
    std::string bad;
    if (L.ok()) {
        init_vars(L.s(), *L.st, *g_dbg);
        for (size_t k = 0; k < sizeof(NAMES) / sizeof(NAMES[0]); ++k) {
            const EvalResult r = eval_expr(NAMES[k], *g_dbg);
            if (!r.ok || r.value != L.st->var(static_cast<int>(k))) bad += std::string(NAMES[k]) + " ";
        }
    } else {
        bad = dstr(L.errs);
    }
    check("SEV-BUILTINS", "every §2.3 state name and accessor reads the same through a script as through eval_expr",
          bad.empty(), bad);
}


// ── SEV-WORK — worked scripts evaluated end to end ─────────────────────────

static void work_rows() {
    {
        // §3(c): the ISR entry/exit span invariants (#279(c)).
        Loaded L = load(S3C, [](const std::string& n) -> std::optional<uint16_t> {
            if (n == "isr") return 0x8100;
            if (n == "isr_exit") return 0x8140;
            return std::nullopt;
        });
        bool ok = L.ok();
        std::string got;
        if (ok) {
            base_state();
            const Rule& entry = L.s().rules[0];
            const Rule& exit_ = L.s().rules[1];
            DbgEvent ev;
            ev.kind = EventKind::Execute;
            ev.pc   = 0x8100;
            // entry: `snap isr` then `log indent (depth(isr) * 2) "==> isr at ${PC:x4}"`
            L.st->snap(entry.body[0].slot, *g_dbg, entry.body[0].pos);
            const int32_t indent = eval_int(*entry.body[1].e1, ctx_of(L, &ev));
            const std::string in_line = interpolate(*entry.body[1].s1, ctx_of(L, &ev));
            // exit, clean: `if depth(isr) == 0` false; `changed(isr, regs)` false
            ev.pc = 0x8140;
            const Action& iff = exit_.body[1];
            const int32_t empty = eval_int(*iff.e1, ctx_of(L, &ev));
            const Action& regs_if = iff.else_body[0];
            const int32_t clean = eval_int(*regs_if.e1, ctx_of(L, &ev));
            const std::string out_line = interpolate(*exit_.body[0].s1, ctx_of(L, &ev));
            // exit, clobbered: HL changed -> changed(isr, regs), dump_diff names it
            set_regs(0x1295, 0x0000, 0xFF00, true, false, 2);
            const int32_t dirty = eval_int(*regs_if.e1, ctx_of(L, &ev));
            const auto diff = L.st->diff(regs_if.then_body[1].slot, *g_dbg, regs_if.then_body[1].pos);
            // the unsnap at the end of the else branch pairs the entry
            L.st->unsnap(iff.else_body[4].slot, iff.else_body[4].pos);
            const int32_t after = eval_int(*iff.e1, ctx_of(L, &ev));
            ok = indent == 2 && in_line == "==> isr at 8100" && empty == 0 && clean == 0 &&
                 out_line == "<== isr at 8140, Ret=1234" && dirty == 1 && diff.size() == 1 &&
                 diff[0] == "HL BCDE -> 0000" && after == 1;
            got = in_line + " | " + out_line + " | " + (diff.empty() ? "" : diff[0]);
        }
        check("SEV-WORK-3C", "§3(c) isr.jds end to end: snap at entry, indented logs, clean exit, a clobbered HL "
              "seen by changed() and named by dump_diff, unsnap pairs the entry",
              ok, got + dstr(L.errs));
    }
    {
        // §3(a)+(b): the MMU guard as a backend predicate, and `armed`.
        Loaded L = load(std::string(S3A) + S3B, [](const std::string& n) -> std::optional<uint16_t> {
            if (n == "__data_crt_head") return 0xA000;
            return std::nullopt;
        });
        bool ok = L.ok();
        if (ok) {
            const auto guard = make_condition(L.s().rules[2].when, L.st, {});
            const auto armed = make_condition(L.s().rules[3].when, L.st, {});
            DbgEvent ev;
            ev.kind = EventKind::NextRegWrite;
            ev.reg  = 0x51;
            g_emu->nextreg().write(0x50, 0x22);
            ev.value = 0x23;
            const bool consistent = guard(ev, *g_dbg);
            ev.value = 0x30;
            const bool broken = guard(ev, *g_dbg);
            g_emu->nextreg().write(0x50, 0xFF);
            ev.value = 0xFF;
            const bool both_rom = guard(ev, *g_dbg);
            const bool unarmed = armed(ev, *g_dbg);
            init_vars(L.s(), *L.st, *g_dbg);
            L.st->set_var(0, 1);  // `set armed = 1` (§3(e))
            ok = !consistent && broken && !both_rom && !unarmed && armed(ev, *g_dbg);
        }
        check("SEV-WORK-3B", "§3(b): `mmu_guard` holds for MMU0+1 == VALUE and both 0xFF, fires otherwise; the "
              "`armed` rule follows the variable",
              ok, dstr(L.errs));
    }
}

// ── SEV-STACK — the recursion bounds hold through the evaluator ─────────────

struct StackJob {
    std::string result = "(not run)";
};

static void* stack_job(void* arg) {
    std::string bad;
    // The deepest accepted shapes (parser.h bound 2 = 200) in every new
    // evaluator path: a string comparison whose sides interpolate 198-tall
    // chains (string node 199, `==` 200); a snapshot field whose index is 198
    // tall; a variable initializer and an action string at the bound.
    const std::string src =
        "var v = " + chain(199) + "\n" +
        "on frame when \"${" + chain(197) + "}\" == \"${" + chain(197) + "}\" do end\n" +
        "on frame when s.MMU[" + chain(196) + " - 197] == 0 do log \"${" + chain(199) + "}\" end\n" +
        "on frame do snap s end\n";
    Loaded L = load(src);
    if (!L.ok()) {
        static_cast<StackJob*>(arg)->result = "load " + dstr(L.pr.error) + dstr(L.errs);
        return nullptr;
    }
    try {
        init_vars(L.s(), *L.st, *g_dbg);
        if (L.st->var(0) != 200) bad += "var ";
        if (eval_int(*L.s().rules[0].when, ctx_of(L)) != 1) bad += "streq ";
        L.st->snap(0, *g_dbg, SourcePos{});
        (void)eval_int(*L.s().rules[1].when, ctx_of(L));
        if (interpolate(*L.s().rules[1].body[0].s1, ctx_of(L)) != "200") bad += "log ";
        L.st->unsnap(0, SourcePos{});
    } catch (const EvalError& e) {
        bad += "threw " + e.d.to_string();
    }
    static_cast<StackJob*>(arg)->result = bad;
    return nullptr;
}

static void stack_rows() {
    StackJob job;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1u << 20);
    pthread_t th;
    const bool started = pthread_create(&th, &attr, stack_job, &job) == 0;
    if (started) pthread_join(th, nullptr);
    pthread_attr_destroy(&attr);
    check("SEV-STACK", "the evaluator's new paths (string comparison, interpolation, snapshot fields, var "
          "initializers) at the parser's height bound run within a 1 MB stack",
          started && job.result.empty(), job.result);
}

// ── main ───────────────────────────────────────────────────────────────────


// ── EVAL-CVAR: cvar(PATH) (GH #436) ───────────────────────────────────────
// Oracle: the C source of the fixtures (test/fixtures/z88dk/src/vars.c, types.h, bk.c, bkmain.c), their maps (the
// address of `vars.c:41` = $8D3E on both sccz80 fixtures, $8C97 on master/sdcc-cvars; `bk.c:10` = P20:$C04C), and
// independent reads through `mem[]` / `phys[]`. The text of a REFUSAL is the facade's (`read_c_value(0, path).why`):
// the atom must pass it on unchanged, and the wording is #434's.
//
// At the `vars.c:41` stop of the FIRST call of `sum(i, g_int, pp, c)` (i = 0): a = 0, b = -5, k = 120 ('x'), t = a + b + k
// + inner = 0 - 5 + 120 + 7 = 122, acc = 3 * (a + b + k) = 345, calls = 1, `*p` = {3, 4, 5}; g_int = -5, g_uint = 40000,
// g_schar = -3, g_char = 'A' = 65, g_long = 0x12345678 = 305419896, g_ulong = 0xCAFEBABE, g_colour = BLUE = 6, s_file = 9.
// At the third call (a = 2): t = 2 - 5 + 120 + 7 = 124.

struct CvarRig {
    Emulator                  emu;
    std::unique_ptr<Debugger> dbg;
    jnext::dbg::ClientId      cid = jnext::dbg::CLIENT_NONE;
    bool                      loaded = false;

    explicit CvarRig(const char* fixture = nullptr, MachineType type = MachineType::ZXN_ISSUE2) {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
        jnext::dbg::ClientInfo ci;
        ci.name = "cvar";
        ci.kind = jnext::dbg::ClientKind::Test;
        cid     = dbg->attach(ci).value;
        dbg->pause(cid);
        if (fixture) {
            const std::string nex = std::string(JNEXT_Z88DK_FIXTURE_DIR) + "/" + fixture + "/prog.nex";
            loaded = emulator_apply_load(emu, nex, false);
            dbg->load_program_sidecars(nex);
        }
    }
    uint16_t pc() const { return dbg->registers().PC; }
    /// This machine has no ROM: an interrupt would run away to RST 38 (banked_call enables them).
    void no_interrupts() {
        Z80Registers r = emu.cpu().get_registers();
        r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
    }
    /// Run to bk.c:10 inside bk_sum ($C04C, page 20 mapped at slot 6), through the banked entry $C005.
    bool run_to_bk10() {
        if (!run_to(0xC005)) return false;
        no_interrupts();
        return run_to(0xC04C);
    }
    /// Single-step, interrupts off, until PC is `addr` (back in main's loop, with page 20 unmapped again).
    bool step_to(uint16_t addr, int max = 4000) {
        for (int i = 0; i < max && pc() != addr; ++i) {
            no_interrupts();
            emu.execute_single_instruction();
        }
        return pc() == addr;
    }
    /// Run to the `nth` arrival at `addr` (a CPU address).
    bool run_to(uint16_t addr, int nth = 1) {
        for (int k = 0; k < nth; ++k) {
            jnext::dbg::Subscription sub;
            sub.kind      = EventKind::Execute;
            sub.filter.lo = sub.filter.hi = addr;
            sub.action    = jnext::dbg::Action::Stop;
            const auto id = dbg->subscribe(cid, sub).value;
            dbg->run(cid);
            for (int i = 0; i < 100 && !dbg->state().paused; ++i) emu.run_frame();
            dbg->unsubscribe(cid, id);
            if (!(dbg->state().paused && pc() == addr)) return false;
        }
        return true;
    }
};

/// `eval_expr` of `cvar(path)` and what it says: "ok:<value>" or "err:<message>".
static std::string cv(CvarRig& r, const std::string& expr) {
    const EvalResult e = eval_expr(expr, *r.dbg);
    if (e.ok) return "ok:" + std::to_string(e.value);
    return "err:" + (e.errors.empty() ? std::string("?") : e.errors[0].message);
}
static int32_t cvv(CvarRig& r, const std::string& expr, bool* ok) {
    const EvalResult e = eval_expr(expr, *r.dbg);
    *ok = e.ok;
    return e.value;
}

static void cvar_rows() {
    // EVAL-CVAR-01
    {
        struct Fx { const char* dir; uint16_t at; bool sccz80; };
        const Fx fxs[] = {{"master/sccz80-cvars", 0x8D3E, true}, {"v2.4/sccz80-cvars", 0x8D3E, true},
                          {"master/sdcc-cvars", 0x8C97, false}};
        bool all = true;
        std::string detail;
        for (const Fx& f : fxs) {
            CvarRig r(f.dir);
            bool ok = r.loaded && r.run_to(f.at);
            const struct { const char* e; int32_t v; } want[] = {
                {"cvar(a)", 0}, {"cvar(b)", -5}, {"cvar(k)", 120}, {"cvar(t)", 122}, {"cvar(calls)", 1},
                {"cvar(g_int)", -5}, {"cvar(g_uint)", 40000}, {"cvar(g_schar)", -3}, {"cvar(g_char)", 65},
                {"cvar(g_long)", 305419896}, {"cvar(g_ulong) == 0xCAFEBABE", 1}, {"cvar(g_ulong)", -889275714},
                {"cvar(g_colour)", 6}, {"cvar(s_file)", 9}};
            for (const auto& w : want) {
                bool one = false;
                const int32_t v = cvv(r, w.e, &one);
                if (!one || v != w.v) {
                    ok = false;
                    detail += std::string(f.dir) + " " + w.e + " -> " + cv(r, w.e) + "; ";
                }
            }
            if (f.sccz80) {
                bool one = false;
                const int32_t v = cvv(r, "cvar(acc)", &one);
                if (!one || v != 345) {
                    ok = false;
                    detail += std::string(f.dir) + " cvar(acc) -> " + cv(r, "cvar(acc)") + "; ";
                }
            }
            all = all && ok;
        }
        check("EVAL-CVAR-01", "cvar() of scalars at vars.c:41 of the first call, on master/sccz80, v2.4/sccz80 and "
                              "master/sdcc (t from BC): a=0 b=-5 k=120 t=122 calls=1 g_int=-5 g_uint=40000 (not sign-extended) "
                              "g_schar=-3 g_char=65 g_long=305419896 g_ulong==0xCAFEBABE colour=6 s_file=9, acc=345 on sccz80",
              all, detail);
    }
    // EVAL-CVAR-02
    {
        CvarRig r("master/sccz80-cvars");
        bool ok = r.loaded && r.run_to(0x8D3E);
        const struct { const char* e; int32_t v; } want[] = {
            {"cvar(p->x)", 3}, {"cvar(p->y)", 4}, {"cvar(p->tag)", 5}, {"cvar( p -> y )", 4}, {"cvar(g_pt.tag)", 3},
            {"cvar(*g_cptr)", 97}, {"cvar(p->x) + cvar(p->y)", 7}};
        std::string detail;
        for (const auto& w : want) {
            bool one = false;
            const int32_t v = cvv(r, w.e, &one);
            if (!one || v != w.v) {
                ok = false;
                detail += std::string(w.e) + " -> " + cv(r, w.e) + "; ";
            }
        }
        // The pointer is a value: p points at main's `lp`, whose first byte (x = 3) `mem[]` reads on its own.
        bool one = false;
        const int32_t viamem = cvv(r, "mem[cvar(p)]", &one);
        ok = ok && one && viamem == 3;
        check("EVAL-CVAR-02", "cvar() through members and dereferences, whitespace included: p->x=3 p->y=4 p->tag=5 "
                              "`p -> y`=4 g_pt.tag=3 *g_cptr=97 ('a'); the pointer p is its address: mem[cvar(p)] is lp.x = 3",
              ok, detail);
    }
    // EVAL-CVAR-03
    {
        CvarRig r("master/sccz80-cvars");
        bool ok = r.loaded && r.run_to(0x8D3E);
        const struct { const char* e; int32_t v; } want[] = {
            {"cvar(g_matrix[1][2])", 6}, {"cvar(g_matrix[0][0x1])", 2}, {"cvar(g_str[1])", 105}, {"cvar(g_cptr[2])", 99},
            {"cvar(vars.c::s_dup)", 0}, {"cvar(other.c::s_dup)", 4}, {"cvar(sum::calls)", 1}};
        std::string detail;
        for (const auto& w : want) {
            bool one = false;
            const int32_t v = cvv(r, w.e, &one);
            if (!one || v != w.v) {
                ok = false;
                detail += std::string(w.e) + " -> " + cv(r, w.e) + "; ";
            }
        }
        // Banked: btable lives in page 20, mapped at $C000 inside bk_sum: btable[2] is phys[20, 2].
        CvarRig b("master/sccz80-cvars-banked");
        bool bok = b.loaded && b.run_to_bk10() && b.step_to(0x8CE8);   // bk.c:10, then back in main's loop (bkmain.c:8)
        bool one = false, two = false, three = false;
        const int32_t t2 = cvv(b, "cvar(btable[2])", &one);
        const int32_t ph = cvv(b, "phys[20, 2]", &two);
        const int32_t other = cvv(b, "mem[0xC002]", &three);
        bok = bok && t2 == 30 && other != 30;
        check("EVAL-CVAR-03", "cvar() of elements and qualified names: g_matrix[1][2]=6 g_matrix[0][0x1]=2 g_str[1]=105 g_cptr[2]=99 "
                              "vars.c::s_dup=0 other.c::s_dup=4 sum::calls=1; a banked array element is read from its own "
                              "page, which is not mapped: btable[2] = phys[20, 2] = 30, not mem[0xC002]",
              ok && bok && one && two && three && t2 == ph, detail + " banked: " + std::to_string(bok) + cv(b, "cvar(btable[2])") + " vs " + cv(b, "phys[20, 2]") + " pc=" + hex(b.pc()));
    }
    // EVAL-CVAR-04
    {
        CvarRig r("master/sccz80-cvars");
        CvarRig z("master/sdcc-cvars");
        bool ok = r.loaded && r.run_to(0x8D3E) && z.loaded && z.run_to(0x8C97);
        std::string detail;
        const auto refused = [&](CvarRig& rig, const char* path) {
            const EvalResult e = eval_expr(std::string("cvar(") + path + ")", *rig.dbg);
            const auto why = rig.dbg->read_c_value(0, path);
            const std::string want = std::string("cvar(") + path + "): " + why.why;
            const bool good = !e.ok && e.errors.size() == 1 && e.errors[0].message == want && !why.ok && !why.why.empty() &&
                              e.errors[0].pos.column == 1;
            if (!good) detail += std::string(path) + " -> " + dstr(e.errors) + "; ";
            return good;
        };
        ok = refused(r, "inner") && ok;           // out of scope at vars.c:41
        ok = refused(r, "nosuch") && ok;
        ok = refused(r, "g_matrix[2][0]") && ok;   // index past the end
        ok = refused(r, "g_head->val") && ok;      // null pointer
        ok = refused(r, "g_int.x") && ok;          // not a struct
        ok = refused(z, "acc") && ok;              // zsdcc keeps it nowhere
        // As a `when`: the predicate is false and the failure reaches on_error.
        Loaded L = load("on frame when cvar(inner) == 1 do end");
        std::vector<Diagnostic> seen;
        bool cond_ok = L.ok();
        if (cond_ok) {
            const auto p = make_condition(L.s().rules[0].when, L.st, [&seen](const Diagnostic& d) { seen.push_back(d); });
            const bool v = p(DbgEvent{}, *r.dbg);
            cond_ok = !v && seen.size() == 1 && seen[0].message.find("cvar(inner): ") == 0;
        }
        check("EVAL-CVAR-04", "every refusal of cvar() is a run-time error at the token, with the facade's reason and never a "
                              "value: inner (out of scope), nosuch, g_matrix[2][0], g_head->val (null), g_int.x, zsdcc's acc "
                              "(optimised out); as a `when` it is false and reaches on_error",
              ok && cond_ok, detail + dstr(seen));
    }
    // EVAL-CVAR-05
    {
        CvarRig r("master/sccz80-cvars");
        bool ok = r.loaded && r.run_to(0x8D3E);
        std::string detail;
        const struct { const char* path; const char* what; } bad[] = {
            {"*p", "struct point is an aggregate"}, {"g_pt", "struct point is an aggregate"}, {"g_matrix", "is an aggregate"},
            {"g_matrix[1]", "is an aggregate"}, {"g_str", "is an aggregate"}, {"g_float", "float is not an integer"}};
        for (const auto& b : bad) {
            const EvalResult e = eval_expr(std::string("cvar(") + b.path + ")", *r.dbg);
            const bool good = !e.ok && e.errors.size() == 1 && e.errors[0].message.find(std::string("cvar(") + b.path + "): ") == 0 &&
                              e.errors[0].message.find(b.what) != std::string::npos;
            if (!good) detail += std::string(b.path) + " -> " + dstr(e.errors) + "; ";
            ok = ok && good;
        }
        check("EVAL-CVAR-05", "cvar() of what is not an int32 scalar is a run-time error naming the type, never the first "
                              "bytes: *p and g_pt (struct point), g_matrix and g_matrix[1] and g_str (arrays), g_float",
              ok, detail);
    }
    // EVAL-CVAR-06
    {
        CvarRig none;                    // no program, no map
        CvarRig sld;
        sld.dbg->load_source_map(JNEXT_SJASMEQU_DIR "/equlabels.sld", false);
        const auto why_none = none.dbg->c_variables_status().why;
        const auto why_sld  = sld.dbg->c_variables_status().why;
        const EvalResult en = eval_expr("cvar(t)", *none.dbg);
        const EvalResult es = eval_expr("cvar(t)", *sld.dbg);
        bool ok = !none.dbg->c_variables_status().available && !sld.dbg->c_variables_status().available && !why_none.empty() &&
                  !why_sld.empty() && why_none != why_sld;
        ok = ok && !en.ok && en.errors.size() == 1 && en.errors[0].message == "cvar(t): " + why_none;
        ok = ok && !es.ok && es.errors.size() == 1 && es.errors[0].message == "cvar(t): " + why_sld;
        // A predicate compiled BEFORE the program loaded is false with that error, and the same predicate reads
        // 122 once the sidecars attach and the run reaches vars.c:41: the store is looked up per evaluation.
        CvarRig late;
        std::vector<Diagnostic> seen;
        const CompiledPredicate cp2 = compile_expr("cvar(t) == 122", PayloadScope{},
                                                   CompileOptions{{}, [&seen](const Diagnostic& d) { seen.push_back(d); }});
        const bool before = cp2.predicate(DbgEvent{}, *late.dbg);
        const std::string nex = std::string(JNEXT_Z88DK_FIXTURE_DIR) + "/master/sccz80-cvars/prog.nex";
        const bool loaded = emulator_apply_load(late.emu, nex, false);
        late.dbg->load_program_sidecars(nex);
        const bool reached = late.run_to(0x8D3E);
        const bool after = cp2.predicate(DbgEvent{}, *late.dbg);
        ok = ok && cp2 && !before && seen.size() == 1 && loaded && reached && after && seen.size() == 1;
        check("EVAL-CVAR-06", "with no C variables cvar() fails with c_variables_status().why (no map; an SLD, which has none): "
                              "a predicate compiled before the program loaded is false with that error and the same "
                              "predicate is true (t == 122) once the map attaches and the run reaches vars.c:41",
              ok, "none=" + dstr(en.errors) + " sld=" + dstr(es.errors) + " seen=" + dstr(seen));
    }
    // EVAL-CVAR-07
    {
        // `cvar(` starts at column 15 of these one-line scripts; the stray `(` of `cvar((x))` is column 20
        const struct { const char* src; int col; } sources[] = {
            {"on frame when cvar(a.) == 0 do end", 15}, {"on frame when cvar() == 0 do end", 15},
            {"on frame when cvar(p->x == 0 do end", 15}, {"on frame when cvar((x)) == 0 do end", 20},
            {"on frame when cvar(a[-1]) == 0 do end", 15}};
        bool ok = true;
        std::string detail;
        for (const auto& sc : sources) {
            const std::string src = sc.src;
            ParseResult pr = parse_script(src);
            std::vector<Diagnostic> errs;
            if (pr.ok()) errs = check_script(pr.script, CheckOptions{});
            const bool refused = !pr.ok() || !errs.empty();
            const Diagnostic* d = !pr.ok() ? &*pr.error : (errs.empty() ? nullptr : &errs[0]);
            if (!refused || !d || d->pos.line != 1 || d->pos.column != sc.col) {
                ok = false;
                detail += src + " -> " + (d ? d->to_string() : "accepted") + "; ";
            }
        }
        const ParseResult as = parse_script("on frame do set cvar(t) = 1 end");
        const bool assign_refused = !as.ok() && as.error->message.find("cannot be assigned") != std::string::npos;
        const ParseResult sp = parse_script("on frame when cvar (t) == 1 do end");
        const bool spaced_not_accessor = !sp.ok();
        Loaded L = load("var cvar = 2\non frame when cvar + 1 == 3 do end");
        bool var_ok = L.ok();
        if (var_ok) {
            const auto p = make_condition(L.s().rules[0].when, L.st, {});
            L.st->set_var(0, 2);
            var_ok = p(DbgEvent{}, *g_dbg);
        }
        check("EVAL-CVAR-07", "cvar() paths are checked at compile time with a position: `a.`, ``, an unclosed `p->x`, `(x)`, "
                              "`a[-1]` are errors at the token; `set cvar(t) = 1` is refused (read-only); `cvar (t)` with a "
                              "space is not the accessor; a script variable called cvar still works",
              ok && assign_refused && spaced_not_accessor && var_ok, detail);
    }
    // EVAL-CVAR-08
    {
        CvarRig r("master/sccz80-cvars");
        // interpolation: `${cvar(t)}` at vars.c:41 of the first call
        bool ok = r.loaded && r.run_to(0x8D3E);
        Loaded L = load("on frame do log \"t=${cvar(t)} s=${cvar(sum::calls)} x=${cvar(g_uint):x4}\" end");
        std::string text;
        if (L.ok()) {
            EvalContext ctx{*r.dbg, nullptr, L.st.get(), nullptr};
            try { text = interpolate(*L.s().rules[0].body[0].s1, ctx); } catch (const EvalError& e) { text = "<" + e.d.to_string() + ">"; }
        }
        ok = ok && L.ok() && text == "t=122 s=1 x=9C40";
        // a `when cvar(a) == 2` predicate on an Execute subscription at vars.c:41 stops on the THIRD call
        CvarRig w("master/sccz80-cvars");
        Loaded W = load("on execute 0x8D3E when cvar(a) == 2 do end");
        bool third = false;
        std::string t3;
        if (w.loaded && W.ok()) {
            jnext::dbg::Subscription sub;
            sub.kind      = EventKind::Execute;
            sub.filter.lo = sub.filter.hi = 0x8D3E;
            sub.action    = jnext::dbg::Action::Stop;
            sub.condition = make_condition(W.s().rules[0].when, W.st, {});
            const auto id = w.dbg->subscribe(w.cid, sub).value;
            w.dbg->run(w.cid);
            for (int i = 0; i < 100 && !w.dbg->state().paused; ++i) w.emu.run_frame();
            w.dbg->unsubscribe(w.cid, id);
            third = w.dbg->state().paused && w.pc() == 0x8D3E;
            t3    = cv(w, "cvar(t)");
        }
        check("EVAL-CVAR-08", "cvar() inside a script: `${cvar(t)}` is 122, `${cvar(sum::calls)}` 1 and `${cvar(g_uint):x4}` 9C40 "
                              "(a `::` is not a format); a `when cvar(a) == 2` on an Execute subscription at vars.c:41 stops on "
                              "the third call, where cvar(t) is 124",
              ok && third && t3 == "ok:124", text + " " + t3);
    }
    // EVAL-CVAR-09
    {
        // A1/A9 of the plan (reading from inside an event delivery): the first `cvar()` of a predicate after a tape program
        // runs is the first reader to run recheck() inside a delivery, which applies the verdict the verify hook recorded
        // during guest execution. Oracle: the committed v2.4/sccz80-tape fixture (prog.tap carries prog_CODE.bin at $8000;
        // main.c:19 starts at $8CC4, `counter` is a global byte that is 0 there, GH #344 stage 3) and its identity rule: a
        // matching program keeps the map, one wrong byte of main.c:19 drops it.
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / ("jnext-cvar-tape-" + std::to_string(::getpid()));
        std::string detail;
        bool all = true;
        for (int variant = 0; variant < 2; ++variant) {
            const bool wrong = variant == 1;
            const fs::path d = root / (wrong ? "bad" : "good");
            fs::create_directories(d);
            const fs::path src = fs::path(JNEXT_Z88DK_FIXTURE_DIR) / "v2.4/sccz80-tape";
            fs::copy_file(src / "prog.map", d / "prog.map", fs::copy_options::overwrite_existing);
            fs::copy_file(src / "prog_CODE.bin", d / "prog_CODE.bin", fs::copy_options::overwrite_existing);   // the identity
            { std::ofstream(d / "prog.tap"); }
            std::vector<uint8_t> code;
            {
                std::ifstream in(src / "prog_CODE.bin", std::ios::binary);
                code.assign(std::istreambuf_iterator<char>(in), {});
            }
            if (wrong) code[0xCC4] ^= 0xFF;   // main.c:19's first byte
            CvarRig r(nullptr, MachineType::ZX48K);   // as the tape rows of source_debug_test
            r.emu.mmu().write(0x6000, 0x18);   // park outside the code range while the map attaches
            r.emu.mmu().write(0x6001, 0xFE);
            Z80Registers regs = r.emu.cpu().get_registers();
            regs.PC = 0x6000;
            regs.SP = 0xFF00;
            r.emu.cpu().set_registers(regs);
            r.dbg->load_program_sidecars((d / "prog.tap").string());
            const bool pending = r.dbg->source_map_status().pending;
            r.dbg->poke(r.cid, jnext::dbg::MemSpace::cpu(), 0x8000, code.size(), code.data());   // the tape loads
            regs.PC = 0x8000;
            r.emu.cpu().set_registers(regs);
            r.no_interrupts();
            Loaded L = load("on execute 0x8CC4 when cvar(counter) == 0 do end");
            std::vector<Diagnostic> seen;
            bool ok = pending && L.ok();
            bool stopped = false;
            if (ok) {
                jnext::dbg::Subscription sub;
                sub.kind      = EventKind::Execute;
                sub.filter.lo = sub.filter.hi = 0x8CC4;
                sub.action    = jnext::dbg::Action::Stop;
                sub.condition = make_condition(L.s().rules[0].when, L.st, [&seen](const Diagnostic& e) { seen.push_back(e); });
                const auto id = r.dbg->subscribe(r.cid, sub).value;
                r.dbg->run(r.cid);
                for (int i = 0; i < 10 && !r.dbg->state().paused; ++i) r.emu.run_frame();
                stopped = r.dbg->state().paused && r.pc() == 0x8CC4;
                r.dbg->unsubscribe(r.cid, id);
            }
            const auto st = r.dbg->source_map_status();
            const bool subs_ok = r.dbg->subscriptions(true).empty();   // the table is intact and the unsubscribe landed
            if (!wrong) {
                // the verdict was applied inside the delivery: the map is kept and verified, and the read worked
                ok = ok && stopped && seen.empty() && !st.pending &&
                     st.state == Debugger::SourceMapStatus::State::Attached && cv(r, "cvar(counter)") == "ok:0";
            } else {
                // the map was dropped inside the delivery: the predicate is false with the facade's reason, no stop
                ok = ok && !stopped && st.state == Debugger::SourceMapStatus::State::None && !seen.empty() &&
                     seen[0].message.find("cvar(counter): ") == 0;
            }
            ok = ok && subs_ok;
            if (!ok) detail += std::string(wrong ? "wrong byte" : "right bytes") + ": pending=" + std::to_string(pending) +
                               " stopped=" + std::to_string(stopped) + " st.pending=" + std::to_string(st.pending) + " " + dstr(seen) + "; ";
            all = all && ok;
        }
        fs::remove_all(root);
        check("EVAL-CVAR-09", "the first cvar() inside a delivery after a tape program runs (a recheck() that applies the verify hook's "
                              "verdict) is safe: with the right bytes the map is verified there, kept, and the predicate reads "
                              "counter = 0 and stops at main.c:19; with one wrong byte the map is dropped there, the predicate is "
                              "false with the facade's reason, nothing stops; the event table is intact either way",
              all, detail);
    }
}

int main() {
    std::printf("script_eval_test — the debugger DSL evaluator (GH #26 WP2)\n");

    // A 48K parked on `JR $` with interrupts off, run two frames so the clocks
    // are non-trivial.
    Emulator emu;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    emu.init(cfg);
    emu.mmu().write(0x8000, 0x18);
    emu.mmu().write(0x8001, 0xFE);
    emu.mmu().write(0x9000, 0xAB);
    emu.mmu().write(0x9001, 0xCD);
    emu.nextreg().write(0x7F, 0x5A);
    Debugger dbg(emu);
    g_emu = &emu;
    g_dbg = &dbg;
    set_regs(0x1295, 0xBCDE, 0xFF00, false, false, 2);
    for (int k = 0; k < 2; ++k) emu.run_frame();

    // An EvalError no row expected (a defect in the evaluator) must not end
    // the run: the group is reported as failed and the rest still run. Its
    // unreached rows are missing from `Total:`, which the manifest refuses.
    auto run_group = [](const char* name, void (*fn)()) {
        try {
            fn();
        } catch (const EvalError& e) {
            ++g_fail;
            std::printf("  FAIL (group %s aborted): unexpected EvalError %s\n", name, e.d.to_string().c_str());
        }
    };
    run_group("type", type_rows);
    run_group("var", var_rows);
    run_group("cond", cond_rows);
    run_group("snap", snap_rows);
    run_group("names", names_rows);
    run_group("changed", changed_rows);
    run_group("diff", diff_rows);
    run_group("interp", interp_rows);
    run_group("evaluate", evaluate_rows);
    run_group("err", err_rows);
    run_group("builtin", builtin_rows);
    run_group("work", work_rows);
    run_group("stack", stack_rows);
    run_group("cvar", cvar_rows);

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
