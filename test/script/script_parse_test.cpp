// jnext::script — the debugger DSL's lexer, parser, load-time checks and the
// `compile_expr` / `eval_expr` library (GH #26 WP1, epic #276 package S).
//
// The contract is doc/design/debug-subsystem/dsl-frontend.md: §2.1 the grammar
// of record, §2.3 the names, §5.4 the library, §6.5 the errors. The choices
// WP1 made where §2.1 is silent are its "WP1 as built" appendix.
//
//   LEX-*    the lexer: number forms and their errors, strings and
//            interpolation, the accessor tokens, positions
//   PARSE-COMMENTS, LEX-CMT-*  the three comment forms, and `//` vs `/`
//   PARSE-*  every production of §2.1: rule shapes, each event and its
//            filter forms, each action, the lvalues, the expression forms
//   PERR-*   every syntax-error class, each with its line:column
//   PREC-*   precedence and associativity, each row a value that the WRONG
//            reading would change
//   EVAL-*   eval_expr over a real 48K machine: arithmetic semantics and
//            every state name / accessor against the backend's own answer
//   CEXPR-*  compile_expr: scopes, refusals, symbols, run-time failures
//   PAY-*    the per-kind payload table: which names each scope admits
//   PAYV-*   what each payload name reads from a delivered Event
//   CHK-*    check_script's load-time errors and binding
//   WORK-*   every worked script of §3 (and §2.7, §7.1) parses, and checks
//
// Run: ./build/test/script_parse_test

#include "script/check.h"
#include "script/expr_compiler.h"
#include "script/lexer.h"
#include "script/parser.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"

#include <pthread.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../row_id.h"

using namespace jnext::script;
using jnext::dbg::Access;
using jnext::dbg::CopperEventKind;
using jnext::dbg::Debugger;
using jnext::dbg::DmaEventKind;
// `Event` and `EventType` are also GLOBAL names (core/scheduler.h, pulled in by
// core/emulator.h), so the DSL's and the backend's are spelled with aliases.
using DbgEvent = jnext::dbg::Event;
using SEvt     = jnext::script::EventType;
using jnext::dbg::EventKind;
using jnext::dbg::EventSource;

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

/// The error is at `line`:`col` and its message contains `needle`.
static bool err_at(const std::optional<Diagnostic>& d, int line, int col,
                   const std::string& needle) {
    return d && d->pos.line == line && d->pos.column == col &&
           d->message.find(needle) != std::string::npos;
}

static bool one_err_at(const std::vector<Diagnostic>& ds, int line, int col,
                       const std::string& needle) {
    return ds.size() == 1 && err_at(ds[0], line, col, needle);
}

/// Parse a script that must parse; on failure the returned script is empty.
static ParseResult P(const std::string& src) { return parse_script(src); }

/// The first rule's event of a one-rule script.
static const EventSpec* ev0(const ParseResult& r) {
    return (r.ok() && r.script.rules.size() == 1) ? &r.script.rules[0].event : nullptr;
}

static bool is_int(const ExprPtr& e, int32_t v) {
    return e && e->kind == ExprKind::Int && e->value == v;
}

static bool is_kind(const ExprPtr& e, ExprKind k) { return e && e->kind == k; }

/// The expression of `var x = <text>`, parsed through the SCRIPT path (so the
/// comment rules of a whole script apply).
static ExprPtr var_init(const std::string& src) {
    ParseResult r = P(src);
    return (r.ok() && r.script.vars.size() == 1) ? r.script.vars[0].init : nullptr;
}

/// A resolver over a fixed table.
static SymbolResolver table(std::map<std::string, uint16_t> t) {
    return [t](const std::string& n) -> std::optional<uint16_t> {
        auto it = t.find(n);
        if (it == t.end()) return std::nullopt;
        return it->second;
    };
}

/// parse + check with a resolver; the check's errors (or the parse error).
static std::vector<Diagnostic> P_check(const std::string& src, const SymbolResolver& syms = {}) {
    ParseResult r = P(src);
    if (!r.ok()) return {*r.error};
    CheckOptions o;
    o.symbols = syms;
    return check_script(r.script, o);
}

// The machine every EVAL/CEXPR/PAYV row reads. Set up in main().
static Debugger* g_dbg = nullptr;

static EvalResult E(const std::string& text) { return eval_expr(text, *g_dbg); }

static bool E_is(const std::string& text, int32_t v) {
    const EvalResult r = E(text);
    return r.ok && r.value == v;
}

static std::string E_str(const std::string& text) {
    const EvalResult r = E(text);
    return r.ok ? ("= " + std::to_string(r.value)) : dstr(r.errors);
}

/// Compile for `scope` and evaluate against `ev`; empty = did not compile.
static std::optional<bool> C(const std::string& text, PayloadScope scope, const DbgEvent& ev,
                             const SymbolResolver& syms = {}) {
    CompileOptions o;
    o.symbols = syms;
    CompiledPredicate cp = compile_expr(text, scope, o);
    if (!cp) return std::nullopt;
    return cp.predicate(ev, *g_dbg);
}

static bool compiles(const std::string& text, PayloadScope scope) {
    return static_cast<bool>(compile_expr(text, scope));
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

// §3(d) MemPoint — verbatim from dsl-frontend.md (v6).
static const char* const S3D = R"JDS(mempoint: on write 0x2222 when VALUE == 0xB7 do
    log "MemPoint hit at ${ADDR:x4}: forbidden value ${VALUE:x2} from PC ${PC:x4}"
    stop "MemPoint"
end
)JDS";

// §3(e) arm / disarm from host keys — verbatim from dsl-frontend.md (v6).
static const char* const S3E = R"JDS(on hostkey 1 do          # Alt+1 in the GUI, --script-key F 1 headless
    enable rom_guard
    enable main_guard
    enable mmu_guard
    enable mempoint
    set armed = 1
    log "MemWatch enabled"
end

on hostkey 2 do
    disable rom_guard
    disable main_guard
    disable mmu_guard
    disable mempoint
    set armed = 0
    log "MemWatch disabled"
end
)JDS";

// §3(f) palette_init.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3F_PALETTE = R"JDS(# palette_init.jds — CI assertion, exit code is the verdict
on execute @palette_init_done once do
    assert mem[0x9000] == 0xAA "sentinel missing in palette buffer"
    assert A == 0 "A must be 0 after palette init"
    log "PASS palette init"
    exit 0
end
on frame 300 do
    log "FAIL: palette_init_done never reached"
    exit 1
end
)JDS";

// §3(f) sprite_y.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3F_SPRITE = R"JDS(# sprite_y.jds — value-conditional port watch, reproducer for a raster bug
on io_write 0x57 when VALUE >= 192 do
    log "sprite attr write ${VALUE:x2} at CYCLE ${CYCLE} frame ${FRAME} cvc ${CVC}"
    dump_regs
    dump_mem 0x5C00 64
    stop "sprite Y >= 192"
end
)JDS";

// §3(f) latency.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3F_LATENCY = R"JDS(# latency.jds — interrupt acceptance to handler entry, in master cycles
var t_int = 0
on interrupt do
    set t_int = CYCLE
end
on execute 0x0038 do
    log "IM1 handler after ${CYCLE - t_int} master cycles (cvc ${CVC}, hc_ula ${HC_ULA})"
end
)JDS";

// §3(f) copper.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3F_COPPER = R"JDS(# copper.jds — Copper-side view: where along the frame did the palette flip land?
on copper move 0x43 do
    log "Copper MOVE NR43=${VALUE:x2} at copper PC ${CPC} on cvc ${CVC} hc_ula ${HC_ULA}"
end
on copper wait when WAIT_V == 95 do
    log "WAIT(95,${WAIT_H}) satisfied at cvc ${CVC} hc_ula ${HC_ULA}"
    assert CVC == 96 "WAIT for line 95 must be satisfied in the blanking before line 96 (GH #181)"
end
on copper halt once do log "copper HALT at ${CPC}" end
)JDS";

// §3(f) dma.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3F_DMA = R"JDS(# dma.jds — DMA-side view: a transfer must stay inside the sprite pattern upload window
on dma start do
    log "DMA ${SRC:x4} -> ${DST:x4} len ${LEN} mode ${DMA_MODE} (io dst ${IO_DST})"
end
on dma byte when not IO_DST and (DST < 0x4000) do
    stop "DMA wrote into ROM/banked code at ${DST:x4} from ${SRC:x4}"
end
on dma end do assert LEN == 256 "sprite upload must move exactly 256 bytes" end
)JDS";

// §3(f) line.jds — verbatim from dsl-frontend.md (v6).
static const char* const S3F_LINE = R"JDS(# line.jds — raster position assertions
on scanline 95 do
    assert nextreg[0x43] & 0x70 == 0x10 "palette select wrong at line 95"
end
on cycle 1000000 once do
    screenshot "/tmp/at-1M.png"
end
)JDS";

// §2.7 worked examples — verbatim from dsl-frontend.md (v6).
static const char* const S27 = R"JDS(# patch a byte at a symbol, once, as soon as the program has loaded it
on execute @main once do
    set mem[@debounce_wait] = 0xC9          ; RET — disable the debounce delay
    log "patched debounce_wait"
end

# force a register to reproduce a bug: make the sprite index wrap on entry
on execute @draw_sprite when B == 63 do
    set B = 64
    log "forced B=64 at draw_sprite (frame ${FRAME})"
end

# skip an instruction (3 bytes) that traps in the emulator under test
on execute @bad_out do
    set PC = PC + 3
end

# fault injection: corrupt the byte a DMA just wrote, to exercise the checker
on dma byte 0x8000..0x9FFF once when VALUE == 0x55 do   ; `once` before `when`, as the grammar says
    set mem[DST] = 0xAA
end

# provoke the MMU-inconsistency guard of 3(b) on purpose (a red twin without a rebuild).
# RULE: an injected fault must be UPSTREAM of the guest write the guard watches,
# never the watched write itself — a script `set nextreg[0x51]` raises no event
# (§2.7), so it can never trip `on nextreg 0x51`. Corrupt MMU0 instead — but
# AFTER the guest's own MMU0 write has committed and BEFORE its MMU1 write:
#   @page_in_level:        NEXTREG 0x50, 0x22   ; deferred; commits in this
#                                               ; instruction's device cluster
#   @page_in_level_mmu1:   NEXTREG 0x51, 0x23   ; the watched write
# Hooking @page_in_level would be overwritten: that Execute fires BEFORE its
# instruction, the script sets MMU0 = 0xFF, then the guest's NEXTREG 0x50, 0x22
# commits (emulator.cpp:10221) and the guard sees 0x22 + 1 == 0x23 — green.
# Hooking @page_in_level_mmu1 (a MAP label the WP7 demo exports) fires after
# the previous instruction's cluster committed 0x22, sets MMU0 = 0xFF, and the
# guest's NEXTREG 0x51, 0x23 then trips the guard: 0xFF + 1 != 0x23 and not both 0xFF.
on execute @page_in_level_mmu1 once do
    set nextreg[0x50] = 0xFF               ; MMU0 := ROM one instruction before the guest writes MMU1
end

# silence the AY while a DAC test runs
on frame 0 once do set AUDIO_MUTE = 0b00111 end
)JDS";

// §7.1 dapr-keyb.jds — verbatim from dsl-frontend.md (v6).
static const char* const S71 = R"JDS(# dapr-keyb.jds — generated by jnext (recorder v1), do not edit
# jds-recorder: 1
# machine=next load=test06keyb.nex rtc=2026-01-01T00:00:00 sd=cspect-next-1gb-fixed.img
# joystick: nr05=0x40 (joy0=kempston1 joy1=sinclair2)
on frame 0 once do assert MACHINE == 4 "recorded on Next" end
on frame 120 do press "q" end               # level: down until the release below
on frame 126 do release "q" end
on frame 131 do press "w" end
on frame 133 do press "caps" end            # overlapping keys are ordinary
on frame 137 do release "w" end
on frame 139 do release "caps" end
on frame 160 do compare_scr "dapr-keyb-0001.scr" "screen after q,w" end
on frame 190 do press "caps+1" end          # EDIT (a compound is two matrix bits)
on frame 197 do release "caps+1" end
on frame 230 do compare_scr "dapr-keyb-0002.scr" "screen after EDIT" end
on frame 231 do exit 0 end
)JDS";


// ── LEX — the lexer ────────────────────────────────────────────────────────

static std::vector<Tok> kinds(const std::string& src) {
    std::vector<Tok> k;
    for (const Token& t : lex(src).tokens) k.push_back(t.kind);
    return k;
}

static void lex_rows() {
    check("LEX-01", "a decimal literal", is_int(var_init("var x = 1234"), 1234));
    check("LEX-02", "a 0x hex literal", is_int(var_init("var x = 0x1F"), 31));
    check("LEX-03", "a $ hex literal", is_int(var_init("var x = $1F"), 31));
    check("LEX-04", "a 0b binary literal", is_int(var_init("var x = 0b101"), 5));
    check("LEX-05", "a literal is its 32-bit pattern: 0xFFFFFFFF and 4294967295 are -1",
          is_int(var_init("var x = 0xFFFFFFFF"), -1) &&
              is_int(var_init("var x = 4294967295"), -1) &&
              is_int(var_init("var x = 2147483648"), INT32_MIN));
    check("LEX-06", "a literal wider than 32 bits is an error at the literal",
          err_at(P("var x = 0x100000000").error, 1, 9, "out of range"),
          dstr(P("var x = 0x100000000").error));
    check("LEX-07", "`0X10` (upper-case prefix) is a malformed number, not `0` + a name",
          err_at(P("var x = 0X10").error, 1, 9, "malformed number"),
          dstr(P("var x = 0X10").error));
    check("LEX-08", "`0x` and `$` with no digit are errors",
          err_at(P("var x = 0x").error, 1, 9, "no digits") &&
              err_at(P("var x = $G").error, 1, 9, "no digits"),
          dstr(P("var x = 0x").error) + " / " + dstr(P("var x = $G").error));
    check("LEX-09", "`0b102` is a malformed number (a digit a binary literal cannot use)",
          err_at(P("var x = 0b102").error, 1, 9, "malformed number"),
          dstr(P("var x = 0b102").error));

    {
        const LexResult r = lex("\"a ${ADDR:x4} b\"");
        const bool shape = !r.error && r.tokens.size() == 2 && r.tokens[0].kind == Tok::String &&
                           r.tokens[0].pieces.size() == 3;
        const auto& pc = r.tokens[0].pieces;
        check("LEX-10", "a string splits into text / `${expr:fmt}` / text, the expression at its own column",
              shape && !pc[0].is_expr && pc[0].text == "a " && pc[1].is_expr &&
                  pc[1].text == "ADDR" && pc[1].fmt == Fmt::X4 && pc[1].pos.line == 1 &&
                  pc[1].pos.column == 6 && !pc[2].is_expr && pc[2].text == " b");
    }
    {
        const LexResult r = lex("\"${A:x2}${B:d}${C}\"");
        const bool ok = !r.error && r.tokens[0].pieces.size() == 3;
        check("LEX-11", "the three formats and none: x2, d, (none)",
              ok && r.tokens[0].pieces[0].fmt == Fmt::X2 &&
                  r.tokens[0].pieces[1].fmt == Fmt::D && r.tokens[0].pieces[2].fmt == Fmt::None);
    }
    {
        const LexResult r = lex("\"${A: x4 }${B :x2}${C:  d}\"");
        const bool ok = !r.error && r.tokens[0].pieces.size() == 3;
        check("LEX-FMT-BLANKS", "blanks around a format, before or after it, are ignored",
              ok && r.tokens[0].pieces[0].fmt == Fmt::X4 && r.tokens[0].pieces[1].fmt == Fmt::X2 &&
                  r.tokens[0].pieces[2].fmt == Fmt::D && r.tokens[0].pieces[1].text == "B ",
              dstr(r.error));
    }
    check("LEX-12", "an unknown format is an error at the format",
          err_at(lex("\"${A:x3}\"").error, 1, 6, "unknown format 'x3'"),
          dstr(lex("\"${A:x3}\"").error));
    check("LEX-13", "an unterminated string — at end of input or at a line end — is an error at its quote",
          err_at(lex("  \"abc").error, 1, 3, "unterminated string") &&
              err_at(lex("\"ab\ncd\"").error, 1, 1, "unterminated string"),
          dstr(lex("  \"abc").error) + " / " + dstr(lex("\"ab\ncd\"").error));
    check("LEX-14", "a quote inside `${…}` is an error at that quote",
          err_at(lex("\"${ \"x\" }\"").error, 1, 5, "cannot appear inside"),
          dstr(lex("\"${ \"x\" }\"").error));
    check("LEX-15", "an unterminated `${` is an error at the `$`",
          err_at(lex("\"${A").error, 1, 2, "unterminated `${`"), dstr(lex("\"${A").error));
    check("LEX-16", "an empty `${}` is an error",
          err_at(lex("\"x${ }\"").error, 1, 3, "empty interpolation"), dstr(lex("\"x${ }\"").error));
    {
        const LexResult r = lex("\"C:\\tmp\\x.png $5 }\"");
        check("LEX-17", "no escapes: backslashes, a `$` not before `{`, a lone `}` are ordinary characters",
              !r.error && r.tokens[0].pieces.size() == 1 &&
                  r.tokens[0].pieces[0].text == "C:\\tmp\\x.png $5 }");
    }
    check("LEX-18", "`mem[` is one accessor token; `mem [` is a name and a bracket",
          kinds("mem[1]") == std::vector<Tok>{Tok::MemL, Tok::Int, Tok::RBracket, Tok::Eof} &&
              kinds("mem [1]") ==
                  std::vector<Tok>{Tok::Ident, Tok::LBracket, Tok::Int, Tok::RBracket, Tok::Eof});
    check("LEX-19", "every bracketed accessor lexes to its own token",
          kinds("mem16[ phys[ nextreg[ mmu[ page[ stack[ changed( depth(") ==
              std::vector<Tok>{Tok::Mem16L, Tok::PhysL, Tok::NextRegL, Tok::MmuL, Tok::PageL,
                               Tok::StackL, Tok::ChangedL, Tok::DepthL, Tok::Eof});
    {
        const LexResult r = lex("@main @on");
        check("LEX-20", "`@name` is one symbol token, keywords allowed as names; `@ name` is an error",
              !r.error && r.tokens.size() == 3 && r.tokens[0].kind == Tok::Symbol &&
                  r.tokens[0].text == "main" && r.tokens[1].text == "on" &&
                  err_at(lex("@ main").error, 1, 1, "symbol name"));
    }
    check("LEX-21", "an unexpected character is reported at its position",
          err_at(lex("a ? b").error, 1, 3, "unexpected character '?'") &&
              err_at(lex("!").error, 1, 1, "unexpected character '!'"),
          dstr(lex("a ? b").error));
    check("LEX-22", "columns count characters: a UTF-8 comment or string earlier on the line does not shift them",
          err_at(lex("; é comment\n  ?").error, 2, 3, "'?'") &&
              err_at(lex("\"é\" ?").error, 1, 5, "'?'"),
          dstr(lex("\"é\" ?").error));
    check("LEX-23", "a non-ASCII byte outside a string or comment is an error",
          err_at(lex("é").error, 1, 1, "byte 0xC3"), dstr(lex("é").error));
    check("LEX-24", "`..` and `.` are different tokens",
          kinds("a..b") == std::vector<Tok>{Tok::Ident, Tok::DotDot, Tok::Ident, Tok::Eof} &&
              kinds("a.b") == std::vector<Tok>{Tok::Ident, Tok::Dot, Tok::Ident, Tok::Eof} &&
              kinds("1..2") == std::vector<Tok>{Tok::Int, Tok::DotDot, Tok::Int, Tok::Eof});
    {
        std::string crlf;
        for (const char* c = S3C; *c; ++c) {
            if (*c == '\n') crlf += '\r';
            crlf += *c;
        }
        const ParseResult r = P(crlf);
        const auto errs = P_check(crlf, table({{"isr", 1}, {"isr_exit", 2}}));
        check("LEX-CRLF", "a CRLF script (a Windows checkout) loads as its LF twin, and positions are unchanged",
              r.ok() && r.script.rules.size() == 2 && r.script.rules[1].body.size() == 2 && errs.empty() &&
                  err_at(P("on frame do\r\n  ?").error, 2, 3, "'?'"),
              dstr(r.error) + dstr(errs));
    }
    check("LEX-25", "the two-character operators",
          kinds("<< >> == != <= >= < > =") ==
              std::vector<Tok>{Tok::Shl, Tok::Shr, Tok::Eq, Tok::Ne, Tok::Le, Tok::Ge, Tok::Lt,
                               Tok::Gt, Tok::Assign, Tok::Eof});
}

// ── Comments ───────────────────────────────────────────────────────────────

static void comment_rows() {
    {
        const ParseResult r = P(
            "; semicolon comment line\n"
            "// slash comment line\n"
            "# hash comment line\n"
            "var a = 8 / 2 // trailing slash comment\n"
            "var b = 3 ; trailing semicolon comment\n"
            "var c = 4 # trailing hash comment\n"
            "on frame do ; after do\n"
            "    log \"x;y//z#w\" // after an action\n"
            "end # after end\n");
        bool ok = r.ok() && r.script.vars.size() == 3 && r.script.rules.size() == 1;
        if (ok) {
            const ExprPtr& a = r.script.vars[0].init;
            ok = is_kind(a, ExprKind::Binary) && a->op == Op::Div && is_int(a->a, 8) &&
                 is_int(a->b, 2) && is_int(r.script.vars[1].init, 3) &&
                 is_int(r.script.vars[2].init, 4) && r.script.rules[0].body.size() == 1 &&
                 r.script.rules[0].body[0].s1 && r.script.rules[0].body[0].s1->parts.size() == 1 &&
                 r.script.rules[0].body[0].s1->parts[0].text == "x;y//z#w";
        }
        check("PARSE-COMMENTS", "`;`, `//` and `#` comment to end of line, alone or trailing; `a / b // c` "
              "is a division then a comment; none of them is a comment inside a string",
              ok, dstr(r.error));
    }
    check("LEX-CMT-SEMI", "`;` ends the expression: `var x = 1 ; 2` is 1",
          is_int(var_init("var x = 1 ; 2"), 1));
    check("LEX-CMT-SLASH", "`//` ends the expression: `var x = 1 // 2` is 1",
          is_int(var_init("var x = 1 // 2"), 1));
    check("LEX-CMT-HASH", "`#` ends the expression: `var x = 1 # 2` is 1",
          is_int(var_init("var x = 1 # 2"), 1));
    {
        const ExprPtr d = var_init("var x = 8 / 2");
        check("LEX-CMT-DIV", "one `/` divides; two adjacent start a comment (`var x = 8 // 2` is 8)",
              is_kind(d, ExprKind::Binary) && d->op == Op::Div &&
                  is_int(var_init("var x = 8 // 2"), 8));
    }
    check("LEX-CMT-SPACED", "`/ /` with a space is two divisions: a syntax error at the second",
          err_at(P("var x = 8 / / 2").error, 1, 13, "expected an expression, found `/`"),
          dstr(P("var x = 8 / / 2").error));
    check("LEX-CMT-INTERP", "inside `${…}` `;` and `#` are not comments but bad characters",
          err_at(P("on frame do log \"${A ; B}\" end").error, 1, 22, "unexpected character ';'") &&
              err_at(P("on frame do log \"${A # B}\" end").error, 1, 22, "unexpected character '#'"),
          dstr(P("on frame do log \"${A ; B}\" end").error));
}

// ── PARSE — the productions ────────────────────────────────────────────────

/// The body of `on frame do <body> end`.
static std::vector<Action> body_of(const std::string& body) {
    ParseResult r = P("on frame do " + body + " end");
    if (!r.ok() || r.script.rules.size() != 1) return {};
    return r.script.rules[0].body;
}

static std::optional<Diagnostic> body_err(const std::string& body) {
    return P("on frame do " + body + " end").error;
}

static void rule_rows() {
    {
        const ParseResult r = P("var armed = 0");
        check("PARSE-VAR", "`var NAME = expr`",
              r.ok() && r.script.vars.size() == 1 && r.script.vars[0].name == "armed" &&
                  is_int(r.script.vars[0].init, 0) && r.script.vars[0].pos.line == 1 &&
                  r.script.vars[0].pos.column == 1);
    }
    {
        const ParseResult r = P("on frame do end");
        const bool ok = r.ok() && r.script.rules.size() == 1;
        const Rule* x = ok ? &r.script.rules[0] : nullptr;
        check("PARSE-RULE-MIN", "the minimal rule: no label, not disabled, no once, no when, empty body",
              x && x->label.empty() && !x->disabled && !x->once && !x->when && x->body.empty() &&
                  x->event.type == SEvt::Frame && !x->event.lo);
    }
    {
        const ParseResult r = P("g: on frame do end");
        check("PARSE-RULE-LABEL", "a labelled rule `NAME: on …`",
              r.ok() && r.script.rules[0].label == "g" && !r.script.rules[0].disabled &&
                  r.script.rules[0].label_pos.column == 1);
    }
    {
        const ParseResult r = P("disabled g: on frame do end");
        check("PARSE-RULE-DISABLED", "`disabled NAME: on …`",
              r.ok() && r.script.rules[0].label == "g" && r.script.rules[0].disabled);
    }
    {
        const ParseResult a = P("on frame once do end");
        const ParseResult b = P("on frame when A == 1 do end");
        const ParseResult c = P("on frame once when 1 do end");
        check("PARSE-RULE-ONCE-WHEN", "`once`, `when`, and `once when` in that order",
              a.ok() && a.script.rules[0].once && !a.script.rules[0].when && b.ok() &&
                  !b.script.rules[0].once && is_kind(b.script.rules[0].when, ExprKind::Binary) &&
                  c.ok() && c.script.rules[0].once && is_int(c.script.rules[0].when, 1));
    }
    check("PARSE-ONCE-WHEN-ORDER", "`when … once` is rejected, at the `once`",
          err_at(P("on frame when 1 once do end").error, 1, 17, "`once` must come before `when`"),
          dstr(P("on frame when 1 once do end").error));
    check("PERR-DISABLED-NOLABEL", "`disabled` without a label is an error at `disabled`",
          err_at(P("disabled on frame do end").error, 1, 1, "needs a rule label"),
          dstr(P("disabled on frame do end").error));
    {
        const ParseResult r = P("var a = 1 var b = 2\non frame\ndo\nend on frame do end");
        check("PARSE-LINE-ENDS", "line ends are whitespace: items share a line or split across lines",
              r.ok() && r.script.vars.size() == 2 && r.script.rules.size() == 2, dstr(r.error));
    }
    check("PERR-TOPLEVEL", "an action at the top level is an error (§1: nothing runs outside an event)",
          err_at(P("log \"x\"").error, 1, 1, "expected `var` or a rule"),
          dstr(P("log \"x\"").error));
    check("PERR-VAR-ASSIGN", "`var x 1` is missing its `=`",
          err_at(P("var x 1").error, 1, 7, "expected `=`"), dstr(P("var x 1").error));
    check("PERR-LABEL-ON", "a label must be followed by `on`",
          err_at(P("g: frame do end").error, 1, 4, "expected `on`"), dstr(P("g: frame do end").error));
    check("PERR-RULE-DO", "a rule needs `do` after its event / `when`",
          err_at(P("on frame when 1 end").error, 1, 17, "expected `do`"),
          dstr(P("on frame when 1 end").error));
}

static void event_rows() {
    {
        const ParseResult r = P("on execute 0x8000 do end");
        const EventSpec* e = ev0(r);
        check("PARSE-EV-EXECUTE", "`on execute ADDR`",
              e && e->type == SEvt::Execute && is_int(e->lo, 0x8000) && !e->hi && !e->page_lo &&
                  !e->page_only);
    }
    {
        const ParseResult r = P("on read 0x4000..0x5AFF do end");
        const EventSpec* e = ev0(r);
        check("PARSE-EV-RANGE", "`on read LO..HI`",
              e && e->type == SEvt::Read && is_int(e->lo, 0x4000) && is_int(e->hi, 0x5AFF));
    }
    {
        const ParseResult r = P("on write 0xC000..0xFFFF page 0x22 do end");
        const EventSpec* e = ev0(r);
        check("PARSE-EV-PAGE-QUAL", "`on write LO..HI page P` — a range qualified by a physical page",
              e && e->type == SEvt::Write && is_int(e->lo, 0xC000) && is_int(e->hi, 0xFFFF) &&
                  is_int(e->page_lo, 0x22) && !e->page_hi && !e->page_only);
    }
    {
        const ParseResult r = P("on write page 0x22 do end");
        const EventSpec* e = ev0(r);
        check("PARSE-EV-PAGE-ONLY", "`on write page P` — the page-only form",
              e && e->page_only && is_int(e->page_lo, 0x22) && !e->page_hi && !e->lo);
    }
    {
        const ParseResult r = P("on execute page 0x20..0x23 do end");
        const EventSpec* e = ev0(r);
        check("PARSE-EV-PAGE-RANGE", "`on execute page P1..P2` — a page range",
              e && e->type == SEvt::Execute && e->page_only && is_int(e->page_lo, 0x20) &&
                  is_int(e->page_hi, 0x23));
    }
    {
        const ParseResult r = P("on execute page[3] do end");
        const EventSpec* e = ev0(r);
        check("PARSE-EV-PAGE-ACCESSOR", "`on execute page[3]` is an ADDRESS (the page[] accessor), not the page form",
              e && !e->page_only && is_kind(e->lo, ExprKind::Page) && is_int(e->lo->a, 3));
    }
    {
        const ParseResult a = P("on io_read 0xFE do end");
        const ParseResult b = P("on io_write 0x243B..0x253B do end");
        check("PARSE-EV-IO", "`on io_read PORT`, `on io_write LO..HI`",
              ev0(a) && ev0(a)->type == SEvt::IoRead && is_int(ev0(a)->lo, 0xFE) && ev0(b) &&
                  ev0(b)->type == SEvt::IoWrite && is_int(ev0(b)->lo, 0x243B) &&
                  is_int(ev0(b)->hi, 0x253B));
    }
    {
        const ParseResult r = P("on io_write mask 0x00FF value 0x57 do end");
        const EventSpec* e = ev0(r);
        check("PARSE-EV-PORT-MASK", "`on io_write mask M value V`",
              e && is_int(e->mask, 0xFF) && is_int(e->value, 0x57) && !e->lo);
    }
    check("PERR-EV-MASK", "`mask M` without `value V` is an error at what follows",
          err_at(P("on io_write mask 0xFF 0x57 do end").error, 1, 23, "expected `value`"),
          dstr(P("on io_write mask 0xFF 0x57 do end").error));
    {
        const ParseResult a = P("on nextreg 0x51 do end");
        const ParseResult b = P("on nextreg 0x50..0x57 do end");
        check("PARSE-EV-NEXTREG", "`on nextreg R`, `on nextreg R1..R2`",
              ev0(a) && ev0(a)->type == SEvt::NextReg && is_int(ev0(a)->lo, 0x51) && ev0(b) &&
                  is_int(ev0(b)->lo, 0x50) && is_int(ev0(b)->hi, 0x57));
    }
    {
        const ParseResult a = P("on frame do end");
        const ParseResult b = P("on frame 300 do end");
        check("PARSE-EV-FRAME", "`on frame` (every frame) and `on frame N`",
              ev0(a) && !ev0(a)->lo && ev0(b) && is_int(ev0(b)->lo, 300));
    }
    {
        const ParseResult a = P("on scanline 95 do end");
        const ParseResult b = P("on cycle 1000000 do end");
        check("PARSE-EV-SCANLINE-CYCLE", "`on scanline N`, `on cycle N`",
              ev0(a) && ev0(a)->type == SEvt::Scanline && is_int(ev0(a)->lo, 95) && ev0(b) &&
                  ev0(b)->type == SEvt::Cycle && is_int(ev0(b)->lo, 1000000));
    }
    {
        const char* src[] = {"on interrupt do end", "on nmi do end", "on reset do end",
                             "on stop do end"};
        const SEvt want[] = {SEvt::Interrupt, SEvt::Nmi, SEvt::Reset,
                                  SEvt::Stop};
        bool ok = true;
        for (int k = 0; k < 4; ++k) {
            const ParseResult r = P(src[k]);
            ok = ok && ev0(r) && ev0(r)->type == want[k];
        }
        check("PARSE-EV-BARE", "`on interrupt`, `on nmi`, `on reset`, `on stop`", ok);
    }
    {
        const ParseResult a = P("on hostkey 1 do end");
        const ParseResult b = P("on hostkey 8 do end");
        check("PARSE-EV-HOSTKEY", "`on hostkey N`, N = 1 and 8",
              ev0(a) && ev0(a)->type == SEvt::HostKey && ev0(a)->hostkey == 1 && ev0(b) &&
                  ev0(b)->hostkey == 8);
    }
    check("PERR-EV-HOSTKEY", "`hostkey` takes a literal 1..8: 0, 9 and an expression are errors at the key",
          err_at(P("on hostkey 9 do end").error, 1, 12, "from 1 to 8") &&
              err_at(P("on hostkey 0 do end").error, 1, 12, "from 1 to 8") &&
              err_at(P("on hostkey (1) do end").error, 1, 12, "from 1 to 8"),
          dstr(P("on hostkey 9 do end").error));
    {
        const ParseResult a = P("on copper move do end");
        const ParseResult b = P("on copper move 0x43 do end");
        const ParseResult c = P("on copper move 0x40..0x43 at 5..9 do end");
        check("PARSE-EV-COPPER-MOVE", "`copper move`, `copper move R`, `copper move R1..R2 at P1..P2`",
              ev0(a) && ev0(a)->type == SEvt::Copper && ev0(a)->copper == CopperSub::Move &&
                  !ev0(a)->lo && ev0(b) && is_int(ev0(b)->lo, 0x43) && ev0(c) &&
                  is_int(ev0(c)->lo, 0x40) && is_int(ev0(c)->hi, 0x43) &&
                  is_int(ev0(c)->at_lo, 5) && is_int(ev0(c)->at_hi, 9));
    }
    {
        const ParseResult a = P("on copper wait at 7 do end");
        const ParseResult b = P("on copper halt do end");
        check("PARSE-EV-COPPER-WAIT-HALT", "`copper wait at P`, `copper halt`",
              ev0(a) && ev0(a)->copper == CopperSub::Wait && is_int(ev0(a)->at_lo, 7) &&
                  !ev0(a)->at_hi && ev0(b) && ev0(b)->copper == CopperSub::Halt && !ev0(b)->at_lo);
    }
    check("PERR-EV-COPPER", "an unknown copper sub-kind is an error at it",
          err_at(P("on copper jump do end").error, 1, 11, "expected `move`, `wait` or `halt`"),
          dstr(P("on copper jump do end").error));
    {
        const ParseResult a = P("on dma start do end");
        const ParseResult b = P("on dma byte do end");
        const ParseResult c = P("on dma byte 0x8000..0x9FFF do end");
        check("PARSE-EV-DMA", "`dma start`, `dma byte`, `dma byte LO..HI`",
              ev0(a) && ev0(a)->type == SEvt::Dma && ev0(a)->dma == DmaSub::Start && ev0(b) &&
                  ev0(b)->dma == DmaSub::Byte && !ev0(b)->lo && ev0(c) &&
                  is_int(ev0(c)->lo, 0x8000) && is_int(ev0(c)->hi, 0x9FFF));
    }
    {
        const ParseResult a = P("on dma byte page 5 do end");
        const ParseResult b = P("on dma byte 0x4000..0x5AFF page 5 do end");
        check("PARSE-EV-DMA-BYTE-PAGE", "`dma byte` takes a full addr_spec: `page P`, and `LO..HI page P` "
              "(registering either is WP3's, dsl-frontend.md G.4 F2)",
              ev0(a) && ev0(a)->dma == DmaSub::Byte && ev0(a)->page_only && is_int(ev0(a)->page_lo, 5) &&
                  !ev0(a)->lo && ev0(b) && !ev0(b)->page_only && is_int(ev0(b)->lo, 0x4000) &&
                  is_int(ev0(b)->hi, 0x5AFF) && is_int(ev0(b)->page_lo, 5));
    }
    {
        const ParseResult a = P("on dma end do log \"x\" end");
        const ParseResult b = P("on dma end once do end");
        check("PARSE-DMA-END", "`on dma end do … end`: the `end` after `dma` is the sub-kind, not the terminator",
              ev0(a) && ev0(a)->dma == DmaSub::End && a.script.rules[0].body.size() == 1 && ev0(b) &&
                  ev0(b)->dma == DmaSub::End && b.script.rules[0].once,
              dstr(a.error));
    }
    {
        // Where the expression is OPTIONAL (`on frame [N]`, `copper move [R]`,
        // `dma byte [RANGE]`) the parser must still see every token an
        // expression can begin with.
        static const char* const STARTS[] = {"@s", "(1)", "-1", "~0", "mem[0]", "mem16[0]",
                                             "phys[0, 0]", "nextreg[0]", "mmu[0]", "page[0]",
                                             "stack[0]", "changed(s, regs)", "depth(s)", "not 0",
                                             "true", "false", "A", "x", "7", "\"s\""};
        std::string bad;
        for (const char* s : STARTS) {
            const ParseResult f = P(std::string("on frame ") + s + " do end");
            const ParseResult m = P(std::string("on copper move ") + s + " do end");
            const ParseResult b = P(std::string("on dma byte ") + s + " do end");
            if (!(ev0(f) && ev0(f)->lo && ev0(m) && ev0(m)->lo && ev0(b) && ev0(b)->lo))
                bad += std::string(s) + " ";
        }
        check("PARSE-EV-OPTIONAL-EXPR", "an optional expression may begin with any token that starts one",
              bad.empty(), bad);
    }
    check("PERR-EV-DMA", "an unknown dma sub-kind is an error at it",
          err_at(P("on dma go do end").error, 1, 8, "expected `start`, `byte` or `end`"),
          dstr(P("on dma go do end").error));
    check("PERR-EV-UNKNOWN", "an unknown event is an error at its name",
          err_at(P("on foo do end").error, 1, 4, "unknown event `foo`"), dstr(P("on foo do end").error));
    check("PERR-EV-NOEVENT", "`on` followed by a non-word",
          err_at(P("on 5 do end").error, 1, 4, "expected an event"), dstr(P("on 5 do end").error));
    check("PERR-EV-NOADDR", "`on execute` with no address",
          err_at(P("on execute do end").error, 1, 12, "expected an address"),
          dstr(P("on execute do end").error));
}

static void action_rows() {
    {
        const auto b = body_of("log \"hi\"");
        check("PARSE-ACT-LOG", "`log STRING`",
              b.size() == 1 && b[0].kind == ActionKind::Log && !b[0].e1 && b[0].s1 &&
                  b[0].s1->parts.size() == 1 && b[0].s1->parts[0].text == "hi" &&
                  b[0].pos.column == 13);
    }
    {
        const auto b = body_of("log indent (2 * 3) \"hi\"");
        check("PARSE-ACT-LOG-INDENT", "`log indent EXPR STRING`",
              b.size() == 1 && is_kind(b[0].e1, ExprKind::Binary) && b[0].e1->op == Op::Mul && b[0].s1);
    }
    {
        const auto a = body_of("stop");
        const auto b = body_of("stop \"why\"");
        check("PARSE-ACT-STOP", "`stop` and `stop STRING`",
              a.size() == 1 && a[0].kind == ActionKind::Stop && !a[0].s1 && b.size() == 1 && b[0].s1 &&
                  b[0].s1->parts[0].text == "why");
    }
    {
        const auto b = body_of("assert A == 0 \"msg\" exit 3");
        check("PARSE-ACT-ASSERT-EXIT", "`assert EXPR STRING`, `exit EXPR`",
              b.size() == 2 && b[0].kind == ActionKind::Assert && is_kind(b[0].e1, ExprKind::Binary) &&
                  b[0].s1 && b[1].kind == ActionKind::Exit && is_int(b[1].e1, 3));
    }
    {
        const auto b = body_of("dump_regs dump_mmu dump_mem 0x5C00 64");
        check("PARSE-ACT-DUMP", "`dump_regs`, `dump_mmu`, `dump_mem ADDR LEN`",
              b.size() == 3 && b[0].kind == ActionKind::DumpRegs && b[1].kind == ActionKind::DumpMmu &&
                  b[2].kind == ActionKind::DumpMem && is_int(b[2].e1, 0x5C00) && is_int(b[2].e2, 64));
    }
    {
        const auto b = body_of("dump_mem 0x100 (-1)");
        check("PARSE-ACT-JUXTAPOSE", "two side-by-side expressions: a parenthesised negative second one",
              b.size() == 1 && is_int(b[0].e1, 0x100) && is_kind(b[0].e2, ExprKind::Unary));
    }
    check("PERR-ACT-JUXTAPOSE", "…whereas `dump_mem A -1` reads `A - 1` and misses its length",
          err_at(body_err("dump_mem 0x100 -1"), 1, 31, "expected an expression, found `end`"),
          dstr(body_err("dump_mem 0x100 -1")));
    {
        const auto b = body_of("snap s unsnap s dump_diff s");
        check("PARSE-ACT-SNAP", "`snap`, `unsnap`, `dump_diff` NAME",
              b.size() == 3 && b[0].kind == ActionKind::Snap && b[1].kind == ActionKind::Unsnap &&
                  b[2].kind == ActionKind::DumpDiff && b[2].name == "s" && b[0].name_pos.column == 18);
    }
    {
        const auto b = body_of("enable g disable g");
        check("PARSE-ACT-ENABLE", "`enable NAME`, `disable NAME`",
              b.size() == 2 && b[0].kind == ActionKind::Enable && b[1].kind == ActionKind::Disable &&
                  b[1].name == "g");
    }
    {
        const auto b = body_of("screenshot \"a.png\" save_snapshot \"b.szx\" compare_scr \"c.scr\" \"m\"");
        check("PARSE-ACT-CAPTURE", "`screenshot F`, `save_snapshot F`, `compare_scr F MSG`",
              b.size() == 3 && b[0].kind == ActionKind::Screenshot && b[0].s1->parts[0].text == "a.png" &&
                  b[1].kind == ActionKind::SaveSnapshot && b[2].kind == ActionKind::CompareScr &&
                  b[2].s1->parts[0].text == "c.scr" && b[2].s2 && b[2].s2->parts[0].text == "m");
    }
    {
        const auto b = body_of("press \"q\" press \"caps\" for 5 release \"q\"");
        check("PARSE-ACT-PRESS", "`press K` (level), `press K for N` (pulse), `release K`",
              b.size() == 3 && b[0].kind == ActionKind::Press && !b[0].e1 && b[1].kind == ActionKind::Press &&
                  is_int(b[1].e1, 5) && b[2].kind == ActionKind::Release);
    }
    {
        const auto b = body_of("joystick 2 0x0F0");
        check("PARSE-ACT-JOYSTICK", "`joystick PORT BITS`",
              b.size() == 1 && b[0].kind == ActionKind::Joystick && b[0].joystick == 2 && is_int(b[0].e1, 0xF0));
    }
    check("PERR-ACT-JOYSTICK", "the joystick port is a literal 1 or 2",
          err_at(body_err("joystick 3 1"), 1, 22, "1 or 2"), dstr(body_err("joystick 3 1")));
    {
        const auto b = body_of("out 0x243B 0x50");
        check("PARSE-ACT-OUT", "`out PORT VALUE`",
              b.size() == 1 && b[0].kind == ActionKind::Out && is_int(b[0].e1, 0x243B) && is_int(b[0].e2, 0x50));
    }
    {
        const auto a = body_of("if A then log \"a\" end");
        const auto b = body_of("if A then log \"a\" else log \"b\" log \"c\" end");
        const auto c = body_of("if A then if B then stop end else exit 1 end");
        check("PARSE-ACT-IF", "`if … then … end`, with `else`, and nested",
              a.size() == 1 && a[0].kind == ActionKind::If && a[0].then_body.size() == 1 && !a[0].has_else &&
                  b.size() == 1 && b[0].has_else && b[0].else_body.size() == 2 && c.size() == 1 &&
                  c[0].then_body.size() == 1 && c[0].then_body[0].kind == ActionKind::If &&
                  c[0].else_body.size() == 1 && c[0].else_body[0].kind == ActionKind::Exit);
    }
    {
        const auto b = body_of("set A = 1 set B = 2 log \"x\"");
        check("PARSE-ACT-SAMELINE", "several actions on one line need no separator",
              b.size() == 3 && b[2].kind == ActionKind::Log);
    }
    check("PERR-ACT-UNKNOWN", "an unknown action is an error at its name",
          err_at(body_err("jump"), 1, 13, "expected an action or `end`, found `jump`"),
          dstr(body_err("jump")));
    check("PERR-ACT-LOG-STRING", "`log` needs a string",
          err_at(body_err("log 5"), 1, 17, "expected a string"), dstr(body_err("log 5")));
    check("PERR-ACT-THEN", "`if` needs `then`",
          err_at(body_err("if A log \"x\" end"), 1, 18, "expected `then`"), dstr(body_err("if A log \"x\" end")));
    check("PERR-ACT-ELSE", "a stray `else` in a rule body",
          err_at(body_err("else"), 1, 13, "found `else`"), dstr(body_err("else")));
    check("PERR-ACT-NOEND", "a missing `end` is reported at end of input, naming the block it closes",
          err_at(P("on frame do log \"x\"").error, 1, 20, "missing `end` for the block opened at line 1, column 1") &&
              err_at(P("on frame do\nif A then\n").error, 3, 1,
                     "missing `end` for the block opened at line 2, column 1"),
          dstr(P("on frame do log \"x\"").error) + " / " + dstr(P("on frame do\nif A then\n").error));
}

static void lvalue_rows() {
    {
        const auto a = body_of("set A = 1");
        const auto b = body_of("set mem[0x4000] = 1");
        check("PARSE-LVALUE", "`set A = 1` and `set mem[x] = 1` parse; `set 3 = 1` is rejected at the `3`",
              a.size() == 1 && a[0].kind == ActionKind::Set && is_kind(a[0].target, ExprKind::Name) &&
                  a[0].target->text == "A" && is_int(a[0].e1, 1) && b.size() == 1 &&
                  is_kind(b[0].target, ExprKind::Mem) && is_int(b[0].target->a, 0x4000) &&
                  err_at(body_err("set 3 = 1"), 1, 17, "cannot be assigned"),
          dstr(body_err("set 3 = 1")));
    }
    {
        const auto b = body_of("set v = 1 set mem[1] = 1 set mem16[2] = 1 set phys[3, 4] = 1 set nextreg[5] = 1");
        check("PARSE-LV-FORMS", "the lvalue forms: a var, mem[], mem16[], phys[,], nextreg[]",
              b.size() == 5 && is_kind(b[0].target, ExprKind::Var) && is_kind(b[1].target, ExprKind::Mem) &&
                  is_kind(b[2].target, ExprKind::Mem16) && is_kind(b[3].target, ExprKind::Phys) &&
                  is_int(b[3].target->a, 3) && is_int(b[3].target->b, 4) &&
                  is_kind(b[4].target, ExprKind::NextReg));
    }
    {
        static const char* const NAMES[] = {
            "A", "B", "C", "D", "E", "H", "L", "F", "I", "R", "AF", "BC", "DE", "HL", "IX", "IY",
            "SP", "PC", "AF2", "BC2", "DE2", "HL2", "CF", "ZF", "SF", "PF", "HF", "NF", "IFF1",
            "IFF2", "IM", "AUDIO_MUTE",
        };
        std::string bad;
        for (const char* n : NAMES) {
            const auto b = body_of(std::string("set ") + n + " = 0");
            if (!(b.size() == 1 && is_kind(b[0].target, ExprKind::Name) && b[0].target->text == n))
                bad += std::string(n) + " ";
        }
        check("PARSE-LV-NAMES", "every assignable name of §2.1 (22 registers, 6 flags, IFF1, IFF2, IM, AUDIO_MUTE)",
              bad.empty(), bad);
    }
    {
        static const char* const NAMES[] = {"HALTED", "FRAME", "CYCLE", "TFRAME", "RAW_HC",
                                            "CVC", "MACHINE", "ADDR", "VALUE", "PC2"};
        std::string bad;
        for (const char* n : NAMES) {
            // PC2 is not a built-in: it is a (undeclared) VARIABLE, which parses.
            const bool want_ok = std::string(n) == "PC2";
            const bool got_ok  = !body_err(std::string("set ") + n + " = 0");
            const bool at_name = want_ok || err_at(body_err(std::string("set ") + n + " = 0"), 1, 17,
                                                   "cannot be assigned");
            if (want_ok != got_ok || !at_name) bad += std::string(n) + " ";
        }
        check("PERR-LV-READONLY", "read-only state and payload names cannot be assigned (error at the name)",
              bad.empty(), bad);
    }
    check("PERR-LV-ACCESSOR", "`mmu[]`, `page[]`, `stack[]` are not lvalues",
          err_at(body_err("set mmu[0] = 1"), 1, 17, "cannot be assigned") &&
              err_at(body_err("set page[0] = 1"), 1, 17, "cannot be assigned") &&
              err_at(body_err("set stack[0] = 1"), 1, 17, "cannot be assigned"),
          dstr(body_err("set mmu[0] = 1")));
    check("PERR-LV-SNAP", "a snapshot field is not an lvalue",
          err_at(body_err("set isr.A = 1"), 1, 17, "snapshot field cannot be assigned"),
          dstr(body_err("set isr.A = 1")));
    check("PERR-LV-RESERVED", "a keyword or a SOURCE constant is not an lvalue",
          err_at(body_err("set on = 1"), 1, 17, "cannot be assigned") &&
              err_at(body_err("set CPU = 1"), 1, 17, "cannot be assigned"),
          dstr(body_err("set CPU = 1")));
    check("PERR-LV-ASSIGN", "`set A 1` is missing its `=`",
          err_at(body_err("set A 1"), 1, 19, "expected `=`"), dstr(body_err("set A 1")));
}

static void expr_rows() {
    {
        const ExprKind want[] = {ExprKind::Mem, ExprKind::Mem16, ExprKind::NextReg,
                                 ExprKind::Mmu, ExprKind::Page, ExprKind::Stack};
        const char* src[] = {"mem[1]", "mem16[1]", "nextreg[1]", "mmu[1]", "page[1]", "stack[1]"};
        bool ok = true;
        for (int k = 0; k < 6; ++k) {
            const ExprPtr e = var_init(std::string("var x = ") + src[k]);
            ok = ok && is_kind(e, want[k]) && is_int(e->a, 1);
        }
        const ExprPtr p = var_init("var x = phys[1, 2]");
        check("PARSE-EX-ACCESSORS", "`mem[]`, `mem16[]`, `nextreg[]`, `mmu[]`, `page[]`, `stack[]`, `phys[,]`",
              ok && is_kind(p, ExprKind::Phys) && is_int(p->a, 1) && is_int(p->b, 2));
    }
    {
        const ExprPtr a = var_init("var x = @main");
        const ExprPtr b = var_init("var x = @on");
        check("PARSE-EX-SYMBOL", "`@name`, a keyword being a legal MAP name",
              is_kind(a, ExprKind::Symbol) && a->text == "main" && is_kind(b, ExprKind::Symbol) &&
                  b->text == "on");
    }
    {
        const ExprPtr a = var_init("var x = isr.A");
        const ExprPtr b = var_init("var x = isr.MMU[3]");
        check("PARSE-EX-SNAPFIELD", "`NAME.FIELD` and `NAME.MMU[n]`",
              is_kind(a, ExprKind::SnapField) && a->text == "isr" && a->field == "A" && !a->a &&
                  is_kind(b, ExprKind::SnapField) && b->field == "MMU" && is_int(b->a, 3));
    }
    {
        const char* g[] = {"regs", "mmu", "iff1", "stack0"};
        const SnapGroup want[] = {SnapGroup::Regs, SnapGroup::Mmu, SnapGroup::Iff1, SnapGroup::Stack0};
        bool ok = true;
        for (int k = 0; k < 4; ++k) {
            const ExprPtr e = var_init(std::string("var x = changed(s, ") + g[k] + ")");
            ok = ok && is_kind(e, ExprKind::Changed) && e->text == "s" && e->group == want[k];
        }
        const ExprPtr d = var_init("var x = depth(isr)");
        check("PARSE-EX-CHANGED-DEPTH", "`changed(NAME, regs|mmu|iff1|stack0)` and `depth(NAME)`",
              ok && is_kind(d, ExprKind::Depth) && d->text == "isr");
    }
    check("PERR-EX-CHANGED-GROUP", "an unknown `changed()` group is an error at it",
          err_at(P("var x = changed(s, foo)").error, 1, 20, "unknown `changed()` group"),
          dstr(P("var x = changed(s, foo)").error));
    check("PARSE-EX-CONSTS", "`true` 1, `false` 0, and the SOURCE constants CPU 0, DMA 1, COPPER 2",
          is_int(var_init("var x = true"), 1) && is_int(var_init("var x = false"), 0) &&
              is_int(var_init("var x = CPU"), 0) && is_int(var_init("var x = DMA"), 1) &&
              is_int(var_init("var x = COPPER"), 2));
    {
        const ExprPtr a = var_init("var x = PC");
        const ExprPtr b = var_init("var x = foo");
        const ExprPtr c = var_init("var x = ADDR");
        check("PARSE-EX-NAMES", "an upper-case built-in or payload name is a Name, anything else a variable",
              is_kind(a, ExprKind::Name) && a->text == "PC" && is_kind(b, ExprKind::Var) &&
                  b->text == "foo" && is_kind(c, ExprKind::Name));
    }
    {
        const ExprPtr e = var_init("var x = \"a${B + 1}c\"");
        check("PARSE-EX-STRING", "a string literal with an interpolation is parsed into its parts",
              is_kind(e, ExprKind::Str) && e->str.parts.size() == 3 && e->str.parts[0].text == "a" &&
                  is_kind(e->str.parts[1].expr, ExprKind::Binary) && e->str.parts[2].text == "c");
    }
    {
        const auto b = body_of("log \"${A:x2}${B:x4}${C:d}${D}\"");
        bool ok = b.size() == 1 && b[0].s1 && b[0].s1->parts.size() == 4;
        if (ok) {
            const auto& pt = b[0].s1->parts;
            ok = pt[0].expr && pt[0].fmt == Fmt::X2 && pt[1].fmt == Fmt::X4 && pt[2].fmt == Fmt::D &&
                 pt[3].fmt == Fmt::None && pt[3].expr && pt[3].expr->text == "D";
        }
        check("PARSE-EX-STRING-FMT", "each interpolation's format reaches the tree: x2, x4, d, none", ok);
    }
    check("PARSE-EX-INTERP-POS", "an error inside `${…}` reports its true line and column",
          err_at(P("on frame do\n  log \"v=${1 +}\" end").error, 2, 15,
                 "expected an expression, found end of input"),
          dstr(P("on frame do\n  log \"v=${1 +}\" end").error));
    check("PERR-EX-PAREN", "an unclosed `(`",
          err_at(P("var x = (1 + 2").error, 1, 15, "expected `)`"), dstr(P("var x = (1 + 2").error));
    check("PERR-EX-KEYWORD", "a keyword where an expression belongs",
          err_at(P("var x = do").error, 1, 9, "expected an expression, found `do`"),
          dstr(P("var x = do").error));
    {
        const std::string deep = "var x = " + std::string(1000, '(') + "1" + std::string(1000, ')');
        const std::string neg  = "var x = " + std::string(1000, '-') + "1";
        const std::string nots = "var x = " + [] {
            std::string s;
            for (int k = 0; k < 1000; ++k) s += "not ";
            return s;
        }() + "1";
        check("PERR-EX-DEPTH", "`(`, unary `-` and `not` nested 1000 deep are refused by the parser's "
              "recursion bound (bound 1), not a crash; the other shapes are the DEPTH-* rows",
              P(deep).error && P(deep).error->message.find("nested too deeply") != std::string::npos &&
                  P(neg).error && P(neg).error->message.find("nested too deeply") != std::string::npos &&
                  P(nots).error && P(nots).error->message.find("nested too deeply") != std::string::npos);
    }
    check("PERR-NAME-VAR", "a keyword, a built-in or a SOURCE constant cannot be a variable name",
          err_at(P("var on = 1").error, 1, 5, "reserved") && err_at(P("var A = 1").error, 1, 5, "reserved") &&
              err_at(P("var CPU = 1").error, 1, 5, "reserved"),
          dstr(P("var A = 1").error));
    {
        // The reserved words of the grammar (names.cpp RESERVED), spelled out
        // here rather than read from the table, so a word dropped from the
        // table turns this row red.
        static const char* const WORDS[] = {
            "var", "disabled", "on", "once", "when", "do", "end", "execute", "read", "write",
            "io_read", "io_write", "nextreg", "frame", "scanline", "cycle", "interrupt", "nmi",
            "reset", "hostkey", "stop", "copper", "move", "wait", "halt", "at", "dma", "start",
            "byte", "page", "mask", "value", "log", "indent", "assert", "exit", "dump_regs",
            "dump_mmu", "dump_mem", "snap", "unsnap", "dump_diff", "enable", "disable",
            "screenshot", "save_snapshot", "compare_scr", "press", "for", "release", "joystick",
            "set", "out", "if", "then", "else", "and", "or", "not", "true", "false", "mem",
            "mem16", "phys", "mmu", "stack", "changed", "depth",
        };
        std::string bad;
        for (const char* w : WORDS)
            if (!err_at(P(std::string("var ") + w + " = 1").error, 1, 5, "reserved")) bad += std::string(w) + " ";
        check("PERR-NAME-KEYWORDS", "every word of the grammar is reserved: none can be a variable", bad.empty(),
              bad);
    }
    check("PERR-NAME-LABEL", "…nor a rule label",
          err_at(P("PC: on frame do end").error, 1, 1, "reserved") &&
              err_at(P("frame: on frame do end").error, 1, 1, "reserved"),
          dstr(P("PC: on frame do end").error));
    check("PERR-NAME-SNAP", "…nor a snapshot name",
          err_at(body_err("snap A"), 1, 18, "reserved") && err_at(body_err("snap end"), 1, 18, "reserved"),
          dstr(body_err("snap A")));
    check("PERR-EX-TRAILING", "parse_expression refuses text after the expression",
          err_at(parse_expression("A 1").error, 1, 3, "after the expression"),
          dstr(parse_expression("A 1").error));
}

// ── PREC — precedence and associativity (each value differs under the wrong reading) ──

static void prec_rows() {
    struct Row { const char* id; const char* desc; const char* text; int32_t v; };
    static const Row ROWS[] = {
        {"PREC-01", "`*` binds tighter than `+`", "1 + 2 * 3", 7},
        {"PREC-02", "parentheses override", "(1 + 2) * 3", 9},
        {"PREC-03", "`+` binds tighter than `<<`", "1 << 2 + 1", 8},
        {"PREC-04", "`<<` binds tighter than `&`", "6 & 3 << 1", 6},
        {"PREC-05", "`&` binds tighter than `|`", "4 | 6 & 3", 6},
        {"PREC-06", "`|` and `^` share a level, left to right", "3 | 1 ^ 1", 2},
        {"PREC-07", "`&` binds tighter than `==` (unlike C): x & 0x70 == 0x10", "0x17 & 0x70 == 0x10", 1},
        {"PREC-08", "`not` is below the comparisons: not 1 == 2", "not 1 == 2", 1},
        {"PREC-09", "`not` binds tighter than `and`", "not 0 and 0", 0},
        {"PREC-10", "`and` binds tighter than `or`", "1 or 0 and 0", 1},
        {"PREC-11", "`-` is left-associative", "10 - 3 - 2", 5},
        {"PREC-12", "`/` is left-associative", "100 / 10 / 2", 5},
        {"PREC-13", "unary `-` binds tighter than `+`", "- 2 + 3", 1},
        {"PREC-14", "unary `~` binds tighter than `&`", "~0 & 0xFF", 255},
        {"PREC-15", "comparisons are left-associative", "1 < 2 == 1", 1},
        {"PREC-16", "`*` and `%` share a level, left to right", "2 * 3 % 4", 2},
        {"PREC-17", "`>>` and `<<` share a level, left to right", "8 >> 1 << 1", 8},
        {"PREC-18", "a unary minus after a binary minus", "5 - -2", 7},
    };
    for (const Row& r : ROWS) check(r.id, r.desc, E_is(r.text, r.v), E_str(r.text));
}

// ── EVAL — eval_expr over the machine ──────────────────────────────────────

static void eval_semantic_rows() {
    check("EVAL-WRAP", "32-bit signed arithmetic wraps: MAX+1, 0x10000*0x10000, -MIN",
          E_is("0x7FFFFFFF + 1", INT32_MIN) && E_is("0x10000 * 0x10000", 0) &&
              E_is("-0x80000000", INT32_MIN) && E_is("0 - 0x80000000", INT32_MIN),
          E_str("0x7FFFFFFF + 1"));
    check("EVAL-DIV", "`/` truncates toward zero, `%` takes the dividend's sign, MIN / -1 wraps",
          E_is("-7 / 2", -3) && E_is("-7 % 2", -1) && E_is("7 % -2", 1) &&
              E_is("0x80000000 / -1", INT32_MIN) && E_is("0x80000000 % -1", 0),
          E_str("-7 / 2") + " " + E_str("-7 % 2"));
    {
        const EvalResult a = E("1 / 0");
        const EvalResult b = E("5 % (2 - 2)");
        check("EVAL-DIV0", "division and modulo by zero are run-time errors at the operator",
              !a.ok && a.errors.size() == 1 && err_at(a.errors[0], 1, 3, "division by zero") && !b.ok &&
                  b.errors.size() == 1 && err_at(b.errors[0], 1, 3, "modulo by zero"),
              dstr(a.errors) + dstr(b.errors));
    }
    check("EVAL-SHIFT", "shift counts use their low five bits; `>>` is arithmetic",
          E_is("1 << 33", 2) && E_is("-8 >> 1", -4) && E_is("0x80000000 >> 31", -1) &&
              E_is("1 << 31", INT32_MIN) && E_is("0x40 >> 4", 4) && E_is("0 >> 1", 0) &&
              E_is("0 >> 31", 0),
          E_str("-8 >> 1"));
    check("EVAL-LOGIC", "comparisons and `and`/`or`/`not` give 1 or 0",
          E_is("3 and 5", 1) && E_is("0 or 7", 1) && E_is("not 5", 0) && E_is("not 0", 1) &&
              E_is("3 == 3", 1) && E_is("2 > 3", 0) && E_is("3 != 3", 0) && E_is("3 <= 3", 1) &&
              E_is("4 >= 5", 0) && E_is("-1 < 0", 1));
    {
        // Every comparison at l < r, l == r and l > r: an operator swapped for
        // its neighbour (>= for >, != answering like <) differs somewhere.
        struct Cmp { const char* op; int lt, eq, gt; };
        static const Cmp CMPS[] = {{"==", 0, 1, 0}, {"!=", 1, 0, 1}, {"<", 1, 0, 0},
                                   {">", 0, 0, 1},  {"<=", 1, 1, 0}, {">=", 0, 1, 1}};
        std::string bad;
        for (const Cmp& c : CMPS) {
            const std::string op = c.op;
            if (!E_is("3 " + op + " 5", c.lt)) bad += "3" + op + "5 ";
            if (!E_is("5 " + op + " 5", c.eq)) bad += "5" + op + "5 ";
            if (!E_is("5 " + op + " 3", c.gt)) bad += "5" + op + "3 ";
            if (!E_is("-5 " + op + " 3", c.lt)) bad += "-5" + op + "3 ";
        }
        check("EVAL-CMP-TABLE", "each comparison at l<r, l==r, l>r (signed)", bad.empty(), bad);
    }
    check("EVAL-SHORT", "`and` / `or` short-circuit: the right side is not evaluated",
          E_is("0 and 1 / 0", 0) && E_is("1 or 1 / 0", 1));
    check("EVAL-BITS", "`~`, `^`, `|`, `&`",
          E_is("~0", -1) && E_is("0xF0 ^ 0xFF", 0x0F) && E_is("0xF0 | 0x0F", 0xFF) &&
              E_is("0xF0 & 0x3C", 0x30));
    check("EVAL-CONST", "`CPU`/`DMA`/`COPPER` and `true` are integers",
          E_is("CPU + DMA * 10 + COPPER * 100", 210) && E_is("true + true", 2));
    {
        const EvalResult r = E("A ==");
        check("EVAL-SYNTAX", "eval_expr reports a syntax error with its position",
              !r.ok && r.errors.size() == 1 && err_at(r.errors[0], 1, 5, "expected an expression"),
              dstr(r.errors));
    }
}


// ── EVAL — the state names and accessors, against the backend's own answer ──

static const char* const PAYLOAD_ONLY[] = {
    "ADDR", "VALUE", "PREV", "PAGE", "PORT", "REG", "SOURCE", "KEY", "REASON",
    "CPC", "WAIT_V", "WAIT_H", "SRC", "DST", "LEN", "DMA_MODE", "IO_SRC", "IO_DST",
};

static void build(Emulator& emu, MachineType type) {
    EmulatorConfig cfg;
    cfg.type = type;
    emu.init(cfg);
    emu.mmu().write(0x8000, 0x18);  // JR $
    emu.mmu().write(0x8001, 0xFE);
    Z80Registers r = emu.cpu().get_registers();
    r.PC   = 0x8000;
    r.SP   = 0xFF00;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

/// Time and raster: read while the machine is parked MID-FRAME, so the
/// counters are not all zero and not all equal.
static void eval_time_rows() {
    const jnext::dbg::Time t = g_dbg->time();
    check("EVAL-TIME", "FRAME, CYCLE, TFRAME read time() — frame tag, master cycle, cycles into the frame",
          t.frame >= 1 && t.cycle_in_frame > 0 && t.cycle_in_frame != t.master_cycle &&
              E_is("FRAME", static_cast<int32_t>(t.frame)) &&
              E_is("CYCLE", static_cast<int32_t>(t.master_cycle)) &&
              E_is("TFRAME", static_cast<int32_t>(t.cycle_in_frame)),
          E_str("FRAME") + " " + E_str("CYCLE") + " " + E_str("TFRAME"));

    const RasterState rs = g_dbg->raster();
    const int v[6] = {rs.raw_hc, rs.raw_vc, rs.hc_ula, rs.vc_ula, rs.cvc, rs.phc};
    bool distinct = true;
    for (int a = 0; a < 6; ++a)
        for (int b = a + 1; b < 6; ++b)
            if (v[a] == v[b]) distinct = false;
    check("EVAL-RASTER", "RAW_HC RAW_VC HC_ULA VC_ULA CVC PHC read raster(), each its own counter "
          "(the fixture makes all six differ)",
          distinct && E_is("RAW_HC", rs.raw_hc) && E_is("RAW_VC", rs.raw_vc) &&
              E_is("HC_ULA", rs.hc_ula) && E_is("VC_ULA", rs.vc_ula) && E_is("CVC", rs.cvc) &&
              E_is("PHC", rs.phc),
          "raster " + std::to_string(rs.raw_hc) + "/" + std::to_string(rs.raw_vc) + "/" +
              std::to_string(rs.hc_ula) + "/" + std::to_string(rs.vc_ula) + "/" +
              std::to_string(rs.cvc) + "/" + std::to_string(rs.phc));
}

static void set_regs(Emulator& emu, uint16_t af, bool iff1, bool iff2, uint8_t im, bool halted) {
    Z80Registers r = emu.cpu().get_registers();
    r.AF  = af;     r.BC  = 0x3456; r.DE  = 0x789A; r.HL  = 0xBCDE;
    r.AF2 = 0x1357; r.BC2 = 0x2468; r.DE2 = 0x369C; r.HL2 = 0x48AD;
    r.IX  = 0x5BE0; r.IY  = 0x6CF1; r.SP  = 0xFF00; r.PC  = 0x8000;
    r.I = 0x3F; r.R = 0x2A; r.IFF1 = iff1; r.IFF2 = iff2; r.IM = im; r.halted = halted;
    emu.cpu().set_registers(r);
}

static void eval_state_rows(Emulator& emu) {
    set_regs(emu, 0x1295, true, false, 2, false);
    check("EVAL-REG8", "the 8-bit registers A F B C D E H L I R",
          E_is("A", 0x12) && E_is("F", 0x95) && E_is("B", 0x34) && E_is("C", 0x56) &&
              E_is("D", 0x78) && E_is("E", 0x9A) && E_is("H", 0xBC) && E_is("L", 0xDE) &&
              E_is("I", 0x3F) && E_is("R", 0x2A),
          E_str("A") + " " + E_str("F"));
    check("EVAL-REG16", "the 16-bit registers, the alternate set included",
          E_is("AF", 0x1295) && E_is("BC", 0x3456) && E_is("DE", 0x789A) && E_is("HL", 0xBCDE) &&
              E_is("AF2", 0x1357) && E_is("BC2", 0x2468) && E_is("DE2", 0x369C) &&
              E_is("HL2", 0x48AD) && E_is("IX", 0x5BE0) && E_is("IY", 0x6CF1) &&
              E_is("SP", 0xFF00) && E_is("PC", 0x8000));
    // F = 0x95 = S . . H . P N=0 C: SF HF PF CF set, ZF NF clear.
    const bool f1 = E_is("SF", 1) && E_is("ZF", 0) && E_is("HF", 1) && E_is("PF", 1) &&
                    E_is("NF", 0) && E_is("CF", 1);
    const bool i1 = E_is("IFF1", 1) && E_is("IFF2", 0) && E_is("IM", 2) && E_is("HALTED", 0);
    set_regs(emu, 0x1242, false, true, 1, true);
    // F = 0x42 = . Z . . . . N .: only ZF and NF set.
    const bool f2 = E_is("SF", 0) && E_is("ZF", 1) && E_is("HF", 0) && E_is("PF", 0) &&
                    E_is("NF", 1) && E_is("CF", 0);
    const bool i2 = E_is("IFF1", 0) && E_is("IFF2", 1) && E_is("IM", 1) && E_is("HALTED", 1);
    check("EVAL-FLAGS", "CF NF PF HF ZF SF are bits 0 1 2 4 6 7 of F, each seen set and clear", f1 && f2);
    check("EVAL-IFF", "IFF1 IFF2 IM HALTED, each seen both ways", i1 && i2);
    set_regs(emu, 0x1295, false, false, 1, false);

    emu.mmu().write(0x9000, 0xAB);
    emu.mmu().write(0x9001, 0xCD);
    emu.mmu().write(0xFFFF, 0x34);
    uint8_t rom0 = 0;
    g_dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0000, 1, &rom0);
    check("EVAL-MEM", "mem[] is a byte, mem16[] a little-endian word, wrapping at 0xFFFF like the Z80",
          E_is("mem[0x9000]", 0xAB) && E_is("mem16[0x9000]", 0xCDAB) &&
              E_is("mem16[0xFFFF]", 0x34 | (rom0 << 8)),
          E_str("mem16[0x9000]"));
    {
        const EvalResult a = E("mem[0x10000]");
        const EvalResult b = E("mem16[-1]");
        check("EVAL-MEM-RANGE", "an address outside 0..0xFFFF is a run-time error at the accessor",
              !a.ok && one_err_at(a.errors, 1, 1, "outside 0..0xFFFF") && !b.ok &&
                  one_err_at(b.errors, 1, 1, "outside 0..0xFFFF"),
              dstr(a.errors) + dstr(b.errors));
    }
    emu.mmu().write(0xFF00, 0x34);
    emu.mmu().write(0xFF01, 0x12);
    emu.mmu().write(0xFF02, 0x78);
    emu.mmu().write(0xFF03, 0x56);
    emu.mmu().write(0xFEFE, 0xEF);
    emu.mmu().write(0xFEFF, 0xBE);
    check("EVAL-STACK", "stack[n] is the word at SP + 2n",
          E_is("stack[0]", 0x1234) && E_is("stack[1]", 0x5678) && E_is("stack[-1]", 0xBEEF),
          E_str("stack[0]") + " " + E_str("stack[1]"));
    emu.mmu().write(0xFFFE, 0x11);
    {
        uint8_t z[2] = {0, 0};
        g_dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0000, 2, z);
        const EvalResult over  = E("stack[0x80]");
        const EvalResult under = E("stack[-0x7F81]");
        check("EVAL-STACK-BOUNDARY", "SP + 2n = 0xFFFE and = 0 are the last slots in range; 0x10000 and -2 are not",
              E_is("stack[0x7F]", 0x3411) && E_is("stack[-0x7F80]", z[0] | (z[1] << 8)) && !over.ok &&
                  one_err_at(over.errors, 1, 1, "outside 0..0xFFFF") && !under.ok &&
                  one_err_at(under.errors, 1, 1, "outside 0..0xFFFF"),
              E_str("stack[0x7F]") + " " + dstr(over.errors));
    }
    check("EVAL-STACK-RANGE", "a stack slot outside 0..0xFFFF is a run-time error",
          !E("stack[0x8000]").ok && !E("stack[-0x8000]").ok, dstr(E("stack[0x8000]").errors));

    emu.nextreg().write(0x7F, 0x5A);
    check("EVAL-NEXTREG", "nextreg[r] reads nextreg_peek(r); a register outside 0..255 is a run-time error",
          E_is("nextreg[0x7F]", 0x5A) && g_dbg->nextreg_peek(0x7F) == 0x5A &&
              E_is("nextreg[0x50]", g_dbg->nextreg_peek(0x50)) && !E("nextreg[0x100]").ok,
          E_str("nextreg[0x7F]"));

    const auto slots = g_dbg->mmu_slots();
    bool mmu_ok = true, differs = false;
    for (int s = 0; s < 8; ++s) {
        mmu_ok = mmu_ok && E_is("mmu[" + std::to_string(s) + "]", slots[s].nr_page) &&
                 E_is("page[" + std::to_string(s) + "]", slots[s].effective_page);
        if (slots[s].nr_page != slots[s].effective_page) differs = true;
    }
    check("EVAL-MMU-PAGE", "mmu[s] is the NR 0x50+s value, page[s] the effective page, for all 8 slots "
          "(the 48K ROM slots make the two differ)",
          mmu_ok && differs && !E("mmu[8]").ok && !E("page[-1]").ok);

    const std::string p4 = std::to_string(slots[4].nr_page);
    emu.mmu().write(0x9FFF, 0x5C);  // slot 4's last byte: phys[p4, 0x1FFF]
    {
        const EvalResult past = E("phys[" + p4 + ", 0x2000]");
        check("EVAL-PHYS-BOUNDARY", "phys[] offset 0x1FFF is the page's last byte; 0x2000 is refused AS an offset",
              E_is("phys[" + p4 + ", 0x1FFF]", 0x5C) && !past.ok &&
                  one_err_at(past.errors, 1, 1, "offset 0x2000 is outside 0..0x1FFF"),
              E_str("phys[" + p4 + ", 0x1FFF]") + " " + dstr(past.errors));
    }
    {
        const EvalResult bad = E("phys[0xFE, 0]");
        check("EVAL-PHYS", "phys[page, off] reads the physical page; a refused page and an offset "
              "outside 0..0x1FFF are run-time errors",
              E_is("phys[" + p4 + ", 0x1000]", 0xAB) && !bad.ok &&
                  one_err_at(bad.errors, 1, 1, "invalid_page") && !E("phys[" + p4 + ", 0x2000]").ok &&
                  !E("phys[-1, 0]").ok,
              E_str("phys[" + p4 + ", 0x1000]") + " " + dstr(bad.errors));
    }

    {
        auto cid = g_dbg->attach(jnext::dbg::ClientInfo{"script_parse_test", jnext::dbg::ClientKind::Test});
        const bool set = cid && g_dbg->set_audio_mute_mask(cid.value, 0x16) == jnext::dbg::Result::Ok;
        check("EVAL-AUDIO", "AUDIO_MUTE reads the host mute mask", set && E_is("AUDIO_MUTE", 0x16),
              E_str("AUDIO_MUTE"));
        if (cid) g_dbg->detach(cid.value);
    }

    {
        struct M { MachineType t; int32_t v; };
        const M ms[] = {{MachineType::ZX128K, 1}, {MachineType::ZX_PLUS3, 2}, {MachineType::ZXN_ISSUE2, 4}};
        bool ok = E_is("MACHINE", 0);
        std::string got = E_str("MACHINE");
        for (const M& m : ms) {
            Emulator e2;
            build(e2, m.t);
            Debugger d2(e2);
            const EvalResult r = eval_expr("MACHINE", d2);
            ok = ok && r.ok && r.value == m.v;
            got += " " + std::to_string(r.value);
        }
        check("EVAL-MACHINE", "MACHINE: 48K 0, 128K 1, +3 2, Next 4 (§2.3)", ok, got);
    }

    {
        char path[] = "/tmp/jnext_script_parse_XXXXXX";
        const int fd = mkstemp(path);
        bool loaded = false;
        if (fd >= 0) {
            close(fd);
            {
                std::ofstream f(path);
                f << "MY_SYM = $9000 ; const\n";
            }
            const auto r = g_dbg->load_map(path, jnext::dbg::MapFormat::Simple);
            loaded = r.status == jnext::dbg::Result::Ok && r.value == 1;
            std::remove(path);
        }
        const EvalResult bad = E("@NOPE + 1");
        check("EVAL-SYMBOL", "`@name` resolves through the backend's symbol table; an unknown one is an "
              "error at the `@`",
              loaded && E_is("@MY_SYM", 0x9000) && E_is("mem[@MY_SYM]", 0xAB) && !bad.ok &&
                  one_err_at(bad.errors, 1, 1, "unknown symbol `@NOPE`"),
              dstr(bad.errors));
    }
    check("EVAL-PC-NONE", "with no event, PC is the CPU's PC", E_is("PC", 0x8000));
}

// ── CEXPR — compile_expr ───────────────────────────────────────────────────

static DbgEvent mem_event(uint16_t addr, uint8_t value) {
    DbgEvent ev;
    ev.kind   = EventKind::Mem;
    ev.addr   = addr;
    ev.value  = value;
    ev.access = Access::Write;
    ev.pc     = 0x1234;
    return ev;
}

static void cexpr_rows() {
    {
        std::string bad;
        for (const char* n : PAYLOAD_ONLY) {
            const CompiledPredicate cp = compile_expr(std::string(n) + " == 0", PayloadScope::none());
            if (cp || cp.errors.size() != 1 ||
                !err_at(cp.errors[0], 1, 1, std::string("`") + n + "` is event payload") ||
                cp.errors[0].message.find("no event") == std::string::npos)
                bad += std::string(n) + " ";
        }
        check("CEXPR-NONE-PAYLOAD", "with None, every payload name is a compile-time error at the name",
              bad.empty(), bad);
    }
    {
        DbgEvent ev;
        ev.kind = EventKind::Execute;
        ev.pc   = 0x1234;  // the registers say 0x8000
        const auto a = C("PC == 0x8000", PayloadScope::none(), ev);
        const auto b = C("PC == 0x1234", PayloadScope(EventKind::Execute), ev);
        const auto c = C("PC == 0x8000", PayloadScope(EventKind::Execute), ev);
        check("CEXPR-PC", "PC is the CPU's PC with None and the event's (causing) PC in an event scope",
              a && *a && b && *b && c && !*c);
    }
    {
        const DbgEvent ev;
        const auto t = C("-1", PayloadScope::none(), ev);
        const auto f = C("2 - 2", PayloadScope::none(), ev);
        check("CEXPR-TRUTH", "a predicate is true iff its value is non-zero", t && *t && f && !*f);
    }
    {
        const CompiledPredicate a = compile_expr("x == 1", {});
        const CompiledPredicate b = compile_expr("isr.A == 1", {});
        const CompiledPredicate c = compile_expr("changed(isr, regs)", {});
        const CompiledPredicate d = compile_expr("depth(isr) > 0", {});
        check("CEXPR-SCRIPT-ONLY", "a variable, a snapshot field, changed() and depth() need a script",
              !a && one_err_at(a.errors, 1, 1, "unknown name `x`") && !b &&
                  one_err_at(b.errors, 1, 1, "only available inside a script") && !c &&
                  one_err_at(c.errors, 1, 1, "only available inside a script") && !d &&
                  one_err_at(d.errors, 1, 1, "only available inside a script"),
              dstr(a.errors) + dstr(b.errors));
    }
    {
        const CompiledPredicate a = compile_expr("\"a\" == \"a\"", {});
        check("CEXPR-STRING", "a string is refused: a condition is an integer expression",
              !a && !a.errors.empty() && a.errors[0].message.find("string is not valid here") != std::string::npos,
              dstr(a.errors));
    }
    check("CEXPR-SCOPE-REFUSED", "the script-only `stop` scope and the EventKind::Count sentinel are refused",
          !compile_expr("1", PayloadScope::script_stop()) && !compile_expr("1", PayloadScope(EventKind::Count)));
    {
        const DbgEvent ev;
        std::map<std::string, uint16_t> tbl{{"main", 0x8000}};
        SymbolResolver res = [&tbl](const std::string& n) -> std::optional<uint16_t> {
            auto it = tbl.find(n);
            if (it == tbl.end()) return std::nullopt;
            return it->second;
        };
        CompileOptions o;
        o.symbols = res;
        const CompiledPredicate p = compile_expr("PC == @main", {}, o);
        const bool before = p && p.predicate(ev, *g_dbg);
        tbl["main"] = 0x1111;  // resolution happened at compile time
        const bool after = p && p.predicate(ev, *g_dbg);
        const CompiledPredicate u = compile_expr("PC == @nope", {}, o);
        const CompiledPredicate n = compile_expr("PC == @main", {});
        check("CEXPR-SYMBOL", "`@name` resolves once, at compile time; unknown, or no resolver, is an "
              "error at the `@`",
              before && after && !u && one_err_at(u.errors, 1, 7, "unknown symbol `@nope`") && !n &&
                  one_err_at(n.errors, 1, 7, "no symbol table"),
              dstr(u.errors) + dstr(n.errors));
    }
    {
        std::vector<Diagnostic> seen;
        CompileOptions o;
        o.on_runtime_error = [&seen](const Diagnostic& d) { seen.push_back(d); };
        const CompiledPredicate p = compile_expr("VALUE / (ADDR - ADDR) == 1", PayloadScope::mem(Access::Write), o);
        const CompiledPredicate q = compile_expr("mem[ADDR + 0x10000] == 0", PayloadScope::mem(Access::Write), o);
        const DbgEvent ev = mem_event(0x4000, 5);
        const bool r1 = p && p.predicate(ev, *g_dbg);
        const size_t n1 = seen.size();
        const bool r2 = q && q.predicate(ev, *g_dbg);
        check("CEXPR-RUNTIME", "a run-time failure makes the predicate false and is reported once, with "
              "the failing node's position",
              p && q && !r1 && n1 == 1 && err_at(seen[0], 1, 7, "division by zero") && !r2 &&
                  seen.size() == 2 && err_at(seen[1], 1, 1, "outside 0..0xFFFF"),
              dstr(seen));
        const CompiledPredicate s = compile_expr("VALUE / 0", PayloadScope::mem(Access::Write));
        check("CEXPR-RUNTIME-SILENT", "with no handler a failing predicate is simply false",
              s && !s.predicate(ev, *g_dbg));
    }
    {
        const CompiledPredicate a = compile_expr("A == ", {});
        check("CEXPR-SYNTAX", "a syntax error is a compile error with its position",
              !a && one_err_at(a.errors, 1, 6, "expected an expression"), dstr(a.errors));
    }
    check("CEXPR-KIND-SPELLING", "§5.4's spelling `compile_expr(text, EventKind)` works: the kind converts",
          static_cast<bool>(compile_expr("PORT == 0xFE", EventKind::Port)));
}

// ── PAY — which payload names each scope admits ────────────────────────────

static std::string legality(const PayloadScope& s, const std::set<std::string>& want) {
    std::string bad;
    for (const char* n : PAYLOAD_ONLY) {
        const bool got = compiles(n, s);
        if (got != (want.count(n) != 0)) bad += std::string(n) + (got ? "(admitted) " : "(refused) ");
    }
    if (!compiles("PC", s)) bad += "PC(refused) ";
    return bad;
}

static void payload_rows() {
    struct Row { const char* id; const char* desc; PayloadScope scope; std::set<std::string> want; };
    const Row ROWS[] = {
        {"PAY-EXECUTE", "execute: ADDR PAGE (+PC)", PayloadScope(EventKind::Execute), {"ADDR", "PAGE"}},
        {"PAY-READ", "read: ADDR VALUE PAGE SOURCE — no PREV", PayloadScope::mem(Access::Read),
         {"ADDR", "VALUE", "PAGE", "SOURCE"}},
        {"PAY-WRITE", "write: ADDR VALUE PAGE SOURCE PREV", PayloadScope::mem(Access::Write),
         {"ADDR", "VALUE", "PAGE", "SOURCE", "PREV"}},
        {"PAY-MEM-BARE", "a bare Mem scope admits only what a read also carries — no PREV",
         PayloadScope(EventKind::Mem), {"ADDR", "VALUE", "PAGE", "SOURCE"}},
        {"PAY-PORT", "io_read / io_write / bare Port: PORT VALUE SOURCE", PayloadScope::port(Access::Read),
         {"PORT", "VALUE", "SOURCE"}},
        {"PAY-PORT-WRITE", "io_write: PORT VALUE SOURCE", PayloadScope::port(Access::Write),
         {"PORT", "VALUE", "SOURCE"}},
        {"PAY-NEXTREG", "nextreg: REG VALUE PREV SOURCE", PayloadScope(EventKind::NextRegWrite),
         {"REG", "VALUE", "PREV", "SOURCE"}},
        {"PAY-HOST", "hostkey: KEY", PayloadScope(EventKind::Host), {"KEY"}},
        {"PAY-COPPER-BARE", "a bare Copper scope: CPC (HC_ULA/CVC shadow, see PAYV)",
         PayloadScope(EventKind::Copper), {"CPC"}},
        {"PAY-COPPER-MOVE", "copper move: CPC REG VALUE", PayloadScope::copper_sub(CopperEventKind::Move),
         {"CPC", "REG", "VALUE"}},
        {"PAY-COPPER-WAIT", "copper wait: CPC WAIT_V WAIT_H", PayloadScope::copper_sub(CopperEventKind::Wait),
         {"CPC", "WAIT_V", "WAIT_H"}},
        {"PAY-COPPER-HALT", "copper halt: CPC", PayloadScope::copper_sub(CopperEventKind::Halt), {"CPC"}},
        {"PAY-DMA-BARE", "a bare Dma scope: SRC DST", PayloadScope(EventKind::Dma), {"SRC", "DST"}},
        {"PAY-DMA-START", "dma start: SRC DST LEN DMA_MODE", PayloadScope::dma_sub(DmaEventKind::Start),
         {"SRC", "DST", "LEN", "DMA_MODE"}},
        {"PAY-DMA-BYTE", "dma byte: SRC DST VALUE IO_SRC IO_DST", PayloadScope::dma_sub(DmaEventKind::Byte),
         {"SRC", "DST", "VALUE", "IO_SRC", "IO_DST"}},
        {"PAY-DMA-END", "dma end: SRC DST LEN DMA_MODE", PayloadScope::dma_sub(DmaEventKind::End),
         {"SRC", "DST", "LEN", "DMA_MODE"}},
    };
    for (const Row& r : ROWS) {
        const std::string bad = legality(r.scope, r.want);
        check(r.id, r.desc, bad.empty(), bad);
    }
    {
        const EventKind bare[] = {EventKind::Frame, EventKind::Scanline, EventKind::Cycle, EventKind::Reset,
                                  EventKind::IntAck, EventKind::Nmi, EventKind::Magic};
        std::string bad;
        for (EventKind k : bare) bad += legality(PayloadScope(k), {});
        check("PAY-NOPAYLOAD", "frame scanline cycle reset interrupt nmi magic: no payload name but PC",
              bad.empty(), bad);
    }
    {
        const std::vector<Diagnostic> a = P_check("on frame when CPC == 1 do end");
        const std::vector<Diagnostic> b = P_check("on copper halt when CPC == 1 do end");
        check("PARSE-PAYLOAD-KIND", "`CPC` outside a copper rule is a compile error at the name; inside one it is not",
              one_err_at(a, 1, 15, "`CPC` is event payload and is not available in a `frame` event") &&
                  b.empty(),
              dstr(a) + dstr(b));
    }
}

// ── PAYV — what each payload name reads from a delivered Event ─────────────

static bool T(const std::string& text, PayloadScope s, const DbgEvent& ev) {
    const auto r = C(text, s, ev);
    return r && *r;
}

static void payload_value_rows() {
    {
        DbgEvent ev = mem_event(0x4321, 0x5A);
        ev.prev      = 0xA5;
        ev.phys_page = 0x0B;
        ev.source    = EventSource::Dma;
        check("PAYV-MEM", "a write's ADDR VALUE PREV PAGE SOURCE PC",
              T("ADDR == 0x4321 and VALUE == 0x5A and PREV == 0xA5 and PAGE == 0x0B and "
                "SOURCE == DMA and PC == 0x1234",
                PayloadScope::mem(Access::Write), ev) &&
                  !T("ADDR == 0x1234", PayloadScope::mem(Access::Write), ev));
    }
    {
        DbgEvent ev;
        ev.kind   = EventKind::NextRegWrite;
        ev.reg    = 0x51;
        ev.value  = 0x23;
        ev.prev   = 0x0A;
        ev.source = EventSource::Copper;
        const PayloadScope s(EventKind::NextRegWrite);
        const bool copper = T("SOURCE == COPPER and SOURCE == 2", s, ev);
        ev.source = EventSource::Dma;
        const bool dma = T("SOURCE == DMA and SOURCE == 1", s, ev);
        ev.source = EventSource::Cpu;
        const bool cpu = T("SOURCE == CPU and SOURCE == 0", s, ev);
        check("PAYV-SOURCE", "SOURCE maps the backend's Cpu/Copper/Dma onto §2.1's CPU 0, DMA 1, COPPER 2",
              copper && dma && cpu);
        check("PAYV-NEXTREG", "a NextREG write's REG VALUE PREV",
              T("REG == 0x51 and VALUE == 0x23 and PREV == 0x0A", s, ev));
    }
    {
        DbgEvent ev;
        ev.kind  = EventKind::Port;
        ev.port  = 0x243B;
        ev.value = 0x51;
        check("PAYV-PORT", "a port access's PORT VALUE", T("PORT == 0x243B and VALUE == 0x51", PayloadScope::port(Access::Write), ev));
    }
    {
        DbgEvent ev;
        ev.kind      = EventKind::Execute;
        ev.pc        = 0x8123;
        ev.phys_page = 0x0E;
        check("PAYV-EXECUTE", "execute: ADDR is the PC hit, PAGE the page behind it",
              T("ADDR == 0x8123 and PAGE == 0x0E and PC == 0x8123", EventKind::Execute, ev));
    }
    {
        DbgEvent ev;
        ev.kind = EventKind::Host;
        std::snprintf(ev.host_name, sizeof ev.host_name, "%s", "script5");
        const bool five = T("KEY == 5", EventKind::Host, ev);
        std::snprintf(ev.host_name, sizeof ev.host_name, "%s", "script1");
        check("PAYV-HOST", "hostkey: KEY is N of `scriptN`", five && T("KEY == 1", EventKind::Host, ev));
        std::string bad;
        for (const char* n : {"script9", "script0", "script", "script10", "key1"}) {
            std::snprintf(ev.host_name, sizeof ev.host_name, "%s", n);
            if (!T("KEY == 0", EventKind::Host, ev)) bad += std::string(n) + " ";
        }
        check("PAYV-HOST-OTHER", "a host name that is not script1..script8 has KEY 0", bad.empty(), bad);
    }
    const RasterState rs = g_dbg->raster();
    {
        DbgEvent ev;
        ev.kind        = EventKind::Copper;
        ev.copper_kind = CopperEventKind::Move;
        ev.copper_pc   = 513;
        ev.reg         = 0x43;
        ev.value       = 0x10;
        ev.hc_ula      = static_cast<int16_t>(rs.hc_ula + 7);
        ev.cvc         = static_cast<int16_t>(rs.cvc + 9);
        const std::string want = "CPC == 513 and REG == 0x43 and VALUE == 0x10 and HC_ULA == " +
                                 std::to_string(rs.hc_ula + 7) + " and CVC == " + std::to_string(rs.cvc + 9);
        const bool move = T(want, PayloadScope::copper_sub(CopperEventKind::Move), ev);
        ev.copper_kind         = CopperEventKind::Wait;
        ev.wait_vpos           = 95;
        ev.wait_hpos_threshold = 428;
        const bool wait = T("WAIT_V == 95 and WAIT_H == 428", PayloadScope::copper_sub(CopperEventKind::Wait), ev);
        check("PAYV-COPPER", "copper: CPC REG VALUE, WAIT_V and WAIT_H (the threshold), HC_ULA/CVC of the step",
              move && wait);
        const bool live = T("HC_ULA == " + std::to_string(rs.hc_ula) + " and CVC == " + std::to_string(rs.cvc),
                            EventKind::Frame, ev);
        check("PAYV-SHADOW", "outside a copper rule HC_ULA/CVC are the live raster, not the event's", live);
    }
    {
        DbgEvent ev;
        ev.kind       = EventKind::Dma;
        ev.dma_kind   = DmaEventKind::Start;
        ev.dma_src    = 0x8000;
        ev.dma_dst    = 0x005B;
        ev.dma_length = 256;
        ev.dma_bytes  = 7;
        ev.dma_mode   = 2;
        const bool start = T("SRC == 0x8000 and DST == 0x5B and LEN == 256 and DMA_MODE == 2",
                             PayloadScope::dma_sub(DmaEventKind::Start), ev);
        ev.dma_kind  = DmaEventKind::End;
        ev.dma_bytes = 200;
        const bool end = T("LEN == 200", PayloadScope::dma_sub(DmaEventKind::End), ev);
        ev.dma_kind      = DmaEventKind::Byte;
        ev.value         = 0x77;
        ev.dma_is_io_src = false;
        ev.dma_is_io_dst = true;
        const bool byte = T("VALUE == 0x77 and IO_SRC == 0 and IO_DST == 1",
                            PayloadScope::dma_sub(DmaEventKind::Byte), ev);
        check("PAYV-DMA", "dma: SRC DST DMA_MODE; LEN is the programmed length on start and the bytes moved "
              "on end; a byte's VALUE IO_SRC IO_DST",
              start && end && byte);
    }
}

// ── CHK — check_script ─────────────────────────────────────────────────────

static void check_rows() {
    check("CHK-LABEL-DUP", "a label used twice is an error at the second, naming the first's line",
          one_err_at(P_check("g: on frame do end\ng: on frame do end"), 2, 1, "already used at line 1"),
          dstr(P_check("g: on frame do end\ng: on frame do end")));
    check("CHK-ENABLE", "`enable`/`disable` naming no labelled rule is an error at the name",
          one_err_at(P_check("on frame do enable nope end"), 1, 20, "names no labelled rule") &&
              one_err_at(P_check("on frame do disable nope end"), 1, 21, "names no labelled rule") &&
              P_check("g: on frame do end on frame do enable g disable g end").empty(),
          dstr(P_check("on frame do enable nope end")));
    check("CHK-VAR-DUP", "a variable declared twice",
          one_err_at(P_check("var a = 1\nvar a = 2"), 2, 1, "already declared"),
          dstr(P_check("var a = 1\nvar a = 2")));
    check("CHK-VAR-UNDECLARED", "a variable read or `set` but never declared",
          one_err_at(P_check("on frame when x == 1 do end"), 1, 15, "unknown variable `x`") &&
              one_err_at(P_check("on frame do set y = 1 end"), 1, 17, "unknown variable `y`") &&
              P_check("var x = 0\non frame when x == 1 do set x = 2 end").empty(),
          dstr(P_check("on frame do set y = 1 end")));
    {
        static const char* const FIELDS[] = {
            "A", "B", "C", "D", "E", "H", "L", "F", "I", "R", "AF", "BC", "DE", "HL", "IX", "IY",
            "SP", "PC", "AF2", "BC2", "DE2", "HL2", "IFF1", "IFF2", "IM", "STACK0", "MMU[0]",
            "FRAME", "CYCLE",
        };
        std::string bad;
        for (const char* f : FIELDS)
            if (!P_check(std::string("on frame when s.") + f + " == 0 do end").empty()) bad += std::string(f) + " ";
        check("CHK-SNAP-FIELD", "every field of the §2.5 record is accepted; an unknown one, MMU without an "
              "index or another field with one are errors at the reference",
              bad.empty() && one_err_at(P_check("on frame when s.XYZ == 0 do end"), 1, 15, "unknown snapshot field") &&
                  one_err_at(P_check("on frame when s.MMU == 0 do end"), 1, 15, "needs a slot index") &&
                  one_err_at(P_check("on frame when s.A[1] == 0 do end"), 1, 15, "takes no index"),
              bad);
    }
    check("CHK-FILTER-NOEVENT", "filter bounds have no event: a payload name there is an error",
          one_err_at(P_check("on write ADDR do end"), 1, 10, "not available in no event") &&
              one_err_at(P_check("on io_write mask VALUE value 1 do end"), 1, 18, "not available in no event"),
          dstr(P_check("on write ADDR do end")));
    {
        // Every registration-time operand of EventSpec, each holding a payload
        // name: lo, hi, page_lo, page_hi, mask, value, at_lo, at_hi.
        struct Op8 { const char* src; int col; const char* name; };
        static const Op8 OPS[] = {
            {"on write ADDR do end", 10, "ADDR"},
            {"on write 0..ADDR do end", 13, "ADDR"},
            {"on write 0 page PAGE do end", 17, "PAGE"},
            {"on execute page 1..PAGE do end", 20, "PAGE"},
            {"on io_write mask VALUE value 1 do end", 18, "VALUE"},
            {"on io_write mask 0xFF value VALUE do end", 29, "VALUE"},
            {"on copper wait at CPC do end", 19, "CPC"},
            {"on copper wait at 0..CPC do end", 22, "CPC"},
        };
        std::string bad;
        for (const Op8& o : OPS)
            if (!one_err_at(P_check(o.src), 1, o.col, std::string("`") + o.name + "` is event payload"))
                bad += std::string("[") + o.src + " -> " + dstr(P_check(o.src)) + "] ";
        check("CHK-FILTER-OPERANDS", "every filter operand (lo hi page page_hi mask value at at_hi) is bound "
              "with no event", bad.empty(), bad);
    }
    {
        ParseResult r = P("on write 0 do if 1 then set B = PREV else set A = VALUE end end");
        const auto errs = r.ok() ? check_script(r.script) : std::vector<Diagnostic>{{}};
        const bool bound = r.ok() && errs.empty() &&
                           r.script.rules[0].body[0].then_body[0].e1->builtin == Builtin::P_PREV &&
                           r.script.rules[0].body[0].else_body[0].e1->builtin == Builtin::P_VALUE;
        check("CHK-IF-SCOPE", "both branches of an `if` are in the rule's event scope", bound, dstr(errs));
    }
    check("CHK-VAR-INIT", "a `var` initializer has no event",
          one_err_at(P_check("var v = VALUE"), 1, 9, "not available in no event"), dstr(P_check("var v = VALUE")));
    check("CHK-INTERP", "the per-kind check reaches inside `${…}`, at the name's true column",
          one_err_at(P_check("on frame do log \"a ${ADDR} b\" end"), 1, 22, "`ADDR` is event payload"),
          dstr(P_check("on frame do log \"a ${ADDR} b\" end")));
    check("CHK-REASON", "REASON is a `stop` rule's payload only",
          P_check("on stop do log \"${REASON}\" end").empty() &&
              one_err_at(P_check("on frame do log \"${REASON}\" end"), 1, 20, "`REASON` is event payload"),
          dstr(P_check("on frame do log \"${REASON}\" end")));
    {
        ParseResult r = P("var v = PC\non execute 0 do set PC = PC + 3 end\n"
                          "on copper halt when CVC == 1 do end\non frame when CVC == 1 do end\n"
                          "on stop when PC == 1 do end");
        const bool parsed = r.ok() && r.script.rules.size() == 4;
        const auto errs   = parsed ? check_script(r.script) : std::vector<Diagnostic>{};
        bool ok = parsed && errs.empty();
        if (ok) {
            const Action& set = r.script.rules[0].body[0];
            ok = r.script.vars[0].init->builtin == Builtin::PC && set.target->builtin == Builtin::PC &&
                 set.e1->a->builtin == Builtin::P_PC &&
                 r.script.rules[1].when->a->builtin == Builtin::P_CVC &&
                 r.script.rules[2].when->a->builtin == Builtin::CVC &&
                 r.script.rules[3].when->a->builtin == Builtin::P_PC;
        }
        check("CHK-BIND", "binding: PC live with no event, payload PC in a rule, a `set PC` target the CPU's; "
              "CVC the step's in a copper rule, live elsewhere",
              ok, dstr(errs));
    }
    {
        CheckOptions o;
        o.symbols = table({{"a", 1}});
        ParseResult r = P("on execute @a do end");
        const auto e1 = r.ok() ? check_script(r.script, o) : std::vector<Diagnostic>{};
        const bool resolved = r.ok() && e1.empty() && r.script.rules[0].event.lo->resolved &&
                              r.script.rules[0].event.lo->value == 1;
        ParseResult u = P("on execute @b do end");
        const auto e2 = u.ok() ? check_script(u.script) : std::vector<Diagnostic>{{}, {}};
        check("CHK-SYMBOLS", "with a resolver, `@name` is resolved and an unknown one is an error at the `@`; "
              "without one, symbols are left for the loader",
              resolved && one_err_at(P_check("on execute @b do end", table({{"a", 1}})), 1, 12,
                                     "unknown symbol `@b`") &&
                  e2.empty() && !u.script.rules[0].event.lo->resolved,
              dstr(e1));
    }
    {
        const auto errs = P_check("var a = 1\nvar a = 2\non frame when ADDR == q do enable nope end");
        check("CHK-ALL-ERRORS", "every load-time error is reported, not only the first",
              errs.size() == 4, dstr(errs));
    }
}

// ── WORK — the worked scripts of dsl-frontend.md ───────────────────────────


static SymbolResolver work_symbols() {
    return table({{"__data_crt_head", 0xA000}, {"isr", 0x8100}, {"isr_exit", 0x8140},
                  {"palette_init_done", 0x8200}, {"main", 0x8000}, {"debounce_wait", 0x8300},
                  {"draw_sprite", 0x8400}, {"bad_out", 0x8500}, {"page_in_level_mmu1", 0x8600}});
}

static void work_rows() {
    {
        const ParseResult r = P(S3A);
        bool ok = r.ok() && r.script.vars.size() == 1 && r.script.rules.size() == 2;
        if (ok) {
            const Rule& a = r.script.rules[0];
            const Rule& b = r.script.rules[1];
            ok = a.disabled && a.label == "rom_guard" && a.event.type == SEvt::Write &&
                 is_int(a.event.lo, 0) && is_int(a.event.hi, 0x3FFF) && a.body.size() == 2 &&
                 b.disabled && b.label == "main_guard" && is_kind(b.event.hi, ExprKind::Binary) &&
                 b.event.hi->op == Op::Sub && is_kind(b.event.hi->a, ExprKind::Symbol) &&
                 b.event.hi->a->text == "__data_crt_head";
        }
        check("WORK-3A", "§3(a) guard.jds parses: two disabled labelled write guards, the second bounded by a MAP symbol",
              ok, dstr(r.error));
    }
    {
        const ParseResult r = P(S3B);
        const bool ok = r.ok() && r.script.rules.size() == 2 && r.script.rules[0].disabled &&
                        r.script.rules[0].label == "mmu_guard" &&
                        r.script.rules[0].event.type == SEvt::NextReg &&
                        is_int(r.script.rules[0].event.lo, 0x51) &&
                        is_kind(r.script.rules[0].when, ExprKind::Unary) &&
                        r.script.rules[0].when->op == Op::Not && r.script.rules[1].when;
        check("WORK-3B", "§3(b) parses: `on nextreg 0x51 when not (…)`", ok, dstr(r.error));
    }
    {
        const ParseResult r = P(S3C);
        bool ok = r.ok() && r.script.rules.size() == 2;
        if (ok) {
            const auto& body = r.script.rules[1].body;
            ok = body.size() == 2 && body[1].kind == ActionKind::If && body[1].has_else &&
                 body[1].else_body.size() == 5 && body[1].else_body[4].kind == ActionKind::Unsnap;
        }
        check("WORK-3C", "§3(c) isr.jds parses: snapshots, indented logs, nested if/else, changed()", ok,
              dstr(r.error));
        check("WORK-3C-CHECK", "§3(c) passes the load-time checks", P_check(S3C, work_symbols()).empty(),
              dstr(P_check(S3C, work_symbols())));
    }
    {
        const ParseResult r = P(S3D);
        check("WORK-3D", "§3(d) MemPoint parses: a labelled write at one address with a VALUE predicate",
              r.ok() && r.script.rules.size() == 1 && r.script.rules[0].label == "mempoint" &&
                  is_int(r.script.rules[0].event.lo, 0x2222) && r.script.rules[0].when,
              dstr(r.error));
    }
    {
        const ParseResult r = P(S3E);
        check("WORK-3E", "§3(e) parses: two hostkey rules of six actions each",
              r.ok() && r.script.rules.size() == 2 && r.script.rules[0].event.hostkey == 1 &&
                  r.script.rules[1].event.hostkey == 2 && r.script.rules[0].body.size() == 6 &&
                  r.script.rules[1].body.size() == 6,
              dstr(r.error));
    }
    {
        // 3(a), (b), (d) and (e) are ONE script in §3 — (b) reads `armed`, which
        // (a) declares, and (e) enables the rules the other three label.
        const std::string all = std::string(S3A) + S3B + S3D + S3E;
        check("WORK-3ABDE-CHECK", "§3(a)+(b)+(d)+(e), the ChaseTheBug set, passes the load-time checks",
              P_check(all, work_symbols()).empty(), dstr(P_check(all, work_symbols())));
    }
    struct F { const char* id; const char* desc; const char* src; size_t rules; };
    const F FS[] = {
        {"WORK-3F-PALETTE", "§3(f) palette_init.jds parses and checks", S3F_PALETTE, 2},
        {"WORK-3F-SPRITE", "§3(f) sprite_y.jds parses and checks", S3F_SPRITE, 1},
        {"WORK-3F-LATENCY", "§3(f) latency.jds parses and checks", S3F_LATENCY, 2},
        {"WORK-3F-COPPER", "§3(f) copper.jds parses and checks", S3F_COPPER, 3},
        {"WORK-3F-LINE", "§3(f) line.jds parses and checks", S3F_LINE, 2},
        {"WORK-27", "§2.7's worked mutation examples parse and check", S27, 6},
        {"WORK-71", "§7.1's recorded replay script parses and checks", S71, 12},
    };
    for (const F& f : FS) {
        const ParseResult r = P(f.src);
        const auto errs = P_check(f.src, work_symbols());
        check(f.id, f.desc, r.ok() && r.script.rules.size() == f.rules && errs.empty(),
              dstr(r.error) + " " + dstr(errs));
    }
    {
        const ParseResult r = P(S3F_DMA);
        const auto errs = P_check(S3F_DMA);
        check("WORK-3F-DMA", "§3(f) dma.jds PARSES (start/byte/end); its check reports exactly one error — "
              "`IO_DST` read in a `dma start` rule, which the backend's Start payload does not carry "
              "(a design finding, see dsl-frontend.md \"WP1 as built\")",
              r.ok() && r.script.rules.size() == 3 && r.script.rules[2].event.dma == DmaSub::End &&
                  one_err_at(errs, 3, 75, "`IO_DST` is event payload and is not available in a `dma start` event"),
              dstr(r.error) + " " + dstr(errs));
    }
    {
        const ParseResult r = P(S3F_LINE);
        bool ok = r.ok() && r.script.rules.size() == 2;
        if (ok) {
            const ExprPtr& c = r.script.rules[0].body[0].e1;
            ok = is_kind(c, ExprKind::Binary) && c->op == Op::Eq && is_kind(c->a, ExprKind::Binary) &&
                 c->a->op == Op::BitAnd;
        }
        check("WORK-3F-LINE-PREC", "§3(f) line.jds: `nextreg[0x43] & 0x70 == 0x10` is `(… & 0x70) == 0x10`", ok);
    }
    {
        const ParseResult r = P(S27);
        bool ok = r.ok() && r.script.rules.size() == 6;
        if (ok) {
            const Rule& d = r.script.rules[3];
            ok = d.event.type == SEvt::Dma && d.event.dma == DmaSub::Byte && is_int(d.event.lo, 0x8000) &&
                 is_int(d.event.hi, 0x9FFF) && d.once && d.when &&
                 r.script.rules[2].body[0].target->text == "PC";
        }
        check("WORK-27-SHAPE", "§2.7: `on dma byte LO..HI once when … do` and `set PC = PC + 3`", ok);
    }
}

// ── DEPTH — the recursion bounds (parser.h), review round 1 B1 ──────────────
//
// Every recursive pass in src/script/ recurses over expression nesting or `if`
// nesting, and nothing else. These rows pin, for each shape that can nest,
// that the deepest input the bounds accept is ACCEPTED and one level more is
// REFUSED with a positioned error — and, for the two shapes the first version
// did not bound (a left-associative chain, nested `if`s), that inputs large
// enough to overflow the stack without the bound are refused, not a crash.
// A crash here takes the whole binary down: the harness reads that as a FAIL.

// The documented limits, spelled as LITERALS: the rows below derive their
// boundaries and error columns from these, never from the header's constants,
// so a changed limit turns rows red instead of moving them along with it.
static constexpr int kDepth   = 200;  // MAX_EXPR_DEPTH
static constexpr int kIfDepth = 64;   // MAX_IF_DEPTH

static std::string rep_str(const std::string& s, int n) {
    std::string out;
    out.reserve(s.size() * static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) out += s;
    return out;
}

/// `1` followed by `n` copies of `+1` — a left-deep tree n+1 nodes tall.
static std::string chain(int n) { return "1" + rep_str("+1", n); }

/// `on frame do` with `n` nested `if 1 then … end`.
static std::string nested_ifs(int n) {
    return "on frame do " + rep_str("if 1 then ", n) + "stop " + rep_str("end ", n) + "end";
}

/// The deepest accepted shapes, run through parse, check, compile, evaluate and
/// destruction. Returns "" or what failed. Used on a small thread stack by
/// DEPTH-STACK.
static std::string deepest_accepted() {
    const int D = kDepth - 1;
    std::string bad;
    const std::string exprs[] = {
        chain(D),                                                // tree height 200
        rep_str("(", D) + "1" + rep_str(")", D),                 // parser depth 200
        rep_str("-", D) + "1",
        rep_str("not ", D) + "1",
        rep_str("mem[", D) + "0" + rep_str("]", D),
        rep_str("(1+", D) + "1" + rep_str(")", D),               // both bounds at once
    };
    for (const std::string& e : exprs) {
        const EvalResult r = E(e);
        const CompiledPredicate c = compile_expr(e, {});
        if (!r.ok || !c) bad += "[" + e.substr(0, 12) + "… " + dstr(r.errors) + "] ";
        else (void)c.predicate(DbgEvent{}, *g_dbg);
    }
    {
        // A script at the `if` bound (63 around the innermost one = 64) whose
        // innermost condition and log are at the expression bounds.
        const std::string s2 = "on frame do " + rep_str("if 1 then ", kIfDepth - 1) +
                               "if " + rep_str("(", D - 1) + "1" + rep_str(")", D - 1) + " then " +
                               "log \"${" + chain(D) + "}\" end " + rep_str("end ", kIfDepth - 1) +
                               "end";
        ParseResult r = P(s2);
        const auto errs = r.ok() ? check_script(r.script) : std::vector<Diagnostic>{};
        if (!r.ok() || !errs.empty()) bad += "[if-script " + dstr(r.error) + dstr(errs) + "] ";
    }
    return bad;
}

struct StackJob {
    std::string result = "(not run)";
};

static void* stack_job(void* arg) {
    static_cast<StackJob*>(arg)->result = deepest_accepted();
    return nullptr;
}

static void depth_rows() {
    const int D = kDepth;
    check("DEPTH-LIMITS", "the bounds are the documented ones: expressions 200 levels, `if` nesting 64 (parser.h)",
          MAX_EXPR_DEPTH == kDepth && MAX_IF_DEPTH == kIfDepth);
    {
        // Without bound 2: SIGSEGV in bind_expr / the evaluator (review: 100 KB).
        const std::string big = chain(50000);
        const ExprParseResult pr = parse_expression(big);
        const CompiledPredicate c = compile_expr(big, {});
        const EvalResult r = E(big);
        const ParseResult s = P("var x = " + big);
        check("DEPTH-CHAIN-CRASH", "a 100 KB `1+1+…+1` is refused at the 200th `+`, through every entry point",
              err_at(pr.error, 1, 2 * D, "nested too deeply") && !c && one_err_at(c.errors, 1, 2 * D, "nested too deeply") &&
                  !r.ok && one_err_at(r.errors, 1, 2 * D, "nested too deeply") &&
                  err_at(s.error, 1, 8 + 2 * D, "nested too deeply"),
              dstr(pr.error));
    }
    {
        const std::string big = "1" + rep_str(" or 1", 200000);  // 1 MB
        const EvalResult r = E(big);
        check("DEPTH-OR-CRASH", "a 1 MB `1 or 1 or …` is refused at the 200th `or` (column 5k-2)",
              !r.ok && one_err_at(r.errors, 1, 5 * D - 2, "nested too deeply"), dstr(r.errors));
    }
    {
        // Without bound 3: SIGSEGV in parse_actions / check_actions.
        const ParseResult r = P(nested_ifs(50000));
        check("DEPTH-IF-CRASH", "50000 nested `if`s are refused at the 65th `if`",
              err_at(r.error, 1, 13 + 10 * kIfDepth, "`if` nested too deeply"), dstr(r.error));
    }
    {
        ParseResult ok = P(nested_ifs(kIfDepth));
        const auto errs = ok.ok() ? check_script(ok.script) : std::vector<Diagnostic>{};
        const ParseResult no = P(nested_ifs(kIfDepth + 1));
        check("DEPTH-IF-BOUNDARY", "64 nested `if`s load; 65 are refused at the 65th",
              ok.ok() && errs.empty() && err_at(no.error, 1, 13 + 10 * kIfDepth, "`if` nested too deeply"),
              dstr(ok.error) + dstr(no.error));
    }
    {
        const EvalResult ok = E(chain(D - 1));
        const EvalResult no = E(chain(D));
        check("DEPTH-CHAIN-BOUNDARY", "a chain of 199 operators (tree height 200) evaluates; 200 are refused at the last",
              ok.ok && ok.value == D && !no.ok && one_err_at(no.errors, 1, 2 * D, "nested too deeply"),
              E_str(chain(D - 1)).substr(0, 60) + " " + dstr(no.errors));
    }
    {
        const EvalResult ok = E(rep_str("(", D - 1) + "7" + rep_str(")", D - 1));
        const EvalResult no = E(rep_str("(", D) + "7" + rep_str(")", D));
        check("DEPTH-PAREN-BOUNDARY", "199 nested `(` evaluate; 200 are refused at what the 200th opens",
              ok.ok && ok.value == 7 && !no.ok && one_err_at(no.errors, 1, D + 1, "nested too deeply"),
              dstr(ok.errors) + dstr(no.errors));
    }
    {
        const EvalResult ok = E(rep_str("-", D - 1) + "1");
        const EvalResult no = E(rep_str("-", D) + "1");
        check("DEPTH-UNARY-BOUNDARY", "199 unary `-` evaluate (to -1); 200 are refused at the 200th",
              ok.ok && ok.value == -1 && !no.ok && one_err_at(no.errors, 1, D, "nested too deeply"),
              dstr(ok.errors) + dstr(no.errors));
    }
    {
        const EvalResult ok = E(rep_str("not ", D - 1) + "0");
        const EvalResult no = E(rep_str("not ", D) + "0");
        check("DEPTH-NOT-BOUNDARY", "199 `not` evaluate (to 1); 200 are refused at the 200th",
              ok.ok && ok.value == 1 && !no.ok && one_err_at(no.errors, 1, 4 * (D - 1) + 1, "nested too deeply"),
              dstr(ok.errors) + dstr(no.errors));
    }
    {
        const EvalResult ok = E(rep_str("mem[", D - 1) + "0" + rep_str("]", D - 1));
        const EvalResult no = E(rep_str("mem[", D) + "0" + rep_str("]", D));
        check("DEPTH-INDEX-BOUNDARY", "199 nested `mem[` evaluate; 200 are refused at what the 200th indexes",
              ok.ok && !no.ok && one_err_at(no.errors, 1, 4 * D + 1, "nested too deeply"),
              dstr(ok.errors) + dstr(no.errors));
    }
    {
        // A string node is one level above its interpolations (bound 2).
        const ParseResult ok = P("on frame do log \"${" + chain(D - 1) + "}\" end");
        const ParseResult no = P("var x = \"${" + chain(D - 1) + "}\"");
        check("DEPTH-STRING", "a 200-tall interpolation loads in a `log`, but not inside a string EXPRESSION, "
              "whose node would be the 201st level",
              ok.ok() && no.error && no.error->message.find("nested too deeply") != std::string::npos,
              dstr(ok.error) + dstr(no.error));
    }
    {
        // What the bounds cost in stack: the deepest accepted shapes, all of
        // them, on a thread with a 1 MB stack (the main thread has 8 MB).
        StackJob job;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 1u << 20);
        pthread_t th;
        const bool started = pthread_create(&th, &attr, stack_job, &job) == 0;
        if (started) pthread_join(th, nullptr);
        pthread_attr_destroy(&attr);
        check("DEPTH-STACK", "the deepest accepted shapes parse, check, compile, evaluate and are destroyed "
              "within a 1 MB stack",
              started && job.result.empty(), job.result);
    }
    {
        // Bound 2 must count BOTH operands: a right operand made of unaries is
        // within bound 1 (199 levels) but makes the `+` the 201st.
        const EvalResult ok = E("1+" + rep_str("-", D - 2) + "1");
        const EvalResult no = E("1+" + rep_str("-", D - 1) + "1");
        check("DEPTH-RIGHT-OPERAND", "a binary node's height counts its right operand: `1+--…-1` with a 200-tall "
              "right side is refused at the `+`",
              ok.ok && ok.value == 2 && !no.ok && one_err_at(no.errors, 1, 2, "nested too deeply"),
              dstr(ok.errors) + dstr(no.errors));
    }
    {
        const ParseResult ok = P("on frame do set mem[" + chain(D - 2) + "] = 1 end");
        const ParseResult no = P("on frame do set mem[" + chain(D - 1) + "] = 1 end");
        check("DEPTH-LVALUE", "an lvalue's index node is bounded like any other: a 200-tall index is refused at `mem[`",
              ok.ok() && err_at(no.error, 1, 17, "nested too deeply"), dstr(ok.error) + dstr(no.error));
    }
    {
        const ParseResult ifs = P("on frame do " + rep_str("if 1 then stop end ", 100) + "end");
        const EvalResult parens = E("(1)" + rep_str("+(1)", 150));
        // 250 rules, each condition one `(` deep: 500 parser levels in all, one
        // or two at a time — a guard that leaked would refuse this.
        const ParseResult rules = P(rep_str("on frame when (1) do end ", 250));
        check("DEPTH-SIBLINGS", "the bounds count NESTING, not siblings: 100 `if`s in a row, 151 parenthesised "
              "terms, and 250 rules each with a parenthesised condition load",
              ifs.ok() && ifs.script.rules[0].body.size() == 100 && parens.ok && parens.value == 151 &&
                  rules.ok() && rules.script.rules.size() == 250,
              dstr(ifs.error) + dstr(parens.errors) + dstr(rules.error));
    }
    {
        const std::string many = rep_str("on frame do log \"x\" end\n", 20000);
        const ParseResult r = P(many);
        check("DEPTH-FLAT", "a flat script (20000 rules) is not a nesting and loads: the bounds are on depth, not size",
              r.ok() && r.script.rules.size() == 20000, dstr(r.error));
    }
}

// ── main ───────────────────────────────────────────────────────────────────

int main() {
    std::printf("script_parse_test — the debugger DSL front end and expression library (GH #26 WP1)\n");

    lex_rows();
    comment_rows();
    rule_rows();
    event_rows();
    action_rows();
    lvalue_rows();
    expr_rows();
    check_rows();
    work_rows();

    // The machine the evaluation rows read: a 48K parked on `JR $`, run two
    // frames and then 700 instructions into the next, so the clocks and raster
    // counters are all non-trivial (the frame tag is still 1 there: it moves
    // when run_frame() begins a frame). NR 0x64 offsets CVC from VC_ULA so the
    // two differ.
    Emulator emu;
    build(emu, MachineType::ZX48K);
    emu.nextreg().write(0x64, 0x10);
    Debugger dbg(emu);
    g_dbg = &dbg;
    for (int k = 0; k < 2; ++k) emu.run_frame();
    for (int k = 0; k < 700; ++k) emu.execute_single_instruction();
    emu.snapshot_raster();  // what a pause does: the raster the debugger reports

    eval_time_rows();
    prec_rows();
    eval_semantic_rows();
    eval_state_rows(emu);
    cexpr_rows();
    payload_rows();
    payload_value_rows();
    depth_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
