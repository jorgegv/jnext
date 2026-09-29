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
#include "debug/debugger.h"

#include <pthread.h>

#include <climits>
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
        set_regs(0x1295, 0xBCDE, 0xFF00, true, true, 2);
        const auto iff2_only = ch(2);
        set_regs(0x1295, 0xBCDE, 0xFF00, false, false, 2);
        const auto iff1 = ch(2);
        check("SEV-CHANGED-IFF1", "`changed(s, iff1)` sees IFF1 and not IFF2",
              iff2_only && *iff2_only == 0 && iff1 && *iff1 == 1);
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
        if (L.ok()) got = interpolate(*L.s().rules[0].body[0].s1, ctx_of(L));
        check("SEV-INTERP", "a string's text and its `${…}` pieces, each in its format",
              got == "a 12 b BCDE c -3 d 7 e", got + dstr(L.errs));
    }
    {
        Loaded L = load("on stop do log \"why=${REASON}!\" end");
        const std::string reason = "MemPoint";
        std::string got;
        if (L.ok()) got = interpolate(*L.s().rules[0].body[0].s1, ctx_of(L, nullptr, &reason));
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

    type_rows();
    var_rows();
    cond_rows();
    snap_rows();
    changed_rows();
    diff_rows();
    interp_rows();
    err_rows();
    builtin_rows();
    work_rows();
    stack_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
