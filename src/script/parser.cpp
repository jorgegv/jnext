// jnext::script — parser. See parser.h for the rules it applies.

#include "script/parser.h"

#include <initializer_list>
#include <utility>

#include "script/lexer.h"
#include "script/names.h"

namespace jnext {
namespace script {

namespace {

/// The first error; thrown inside the parser only, caught at the entry points.
struct ParseError {
    Diagnostic d;
};

/// A nesting bound, so a pathological input (ten thousand `(`) is an error
/// and not a stack overflow.
constexpr int MAX_DEPTH = 200;

class Parser {
public:
    explicit Parser(std::vector<Token> toks) : t_(std::move(toks)) {}

    Script parse_script() {
        Script s;
        while (cur().kind != Tok::Eof) {
            if (is_kw("var")) {
                s.vars.push_back(parse_var());
            } else if (is_kw("on") || is_kw("disabled") ||
                       (cur().kind == Tok::Ident && at(1).kind == Tok::Colon)) {
                s.rules.push_back(parse_rule());
            } else {
                fail(cur().pos, "expected `var` or a rule (`on …`), found " + describe(cur()));
            }
        }
        return s;
    }

    ExprPtr parse_whole_expression() {
        ExprPtr e = parse_expr();
        if (cur().kind != Tok::Eof)
            fail(cur().pos, "unexpected " + describe(cur()) + " after the expression");
        return e;
    }

private:
    std::vector<Token> t_;
    size_t             i_ = 0;
    int                depth_ = 0;

    // ── token helpers ──────────────────────────────────────────────────────

    const Token& cur() const { return t_[i_]; }
    const Token& at(size_t k) const {
        return (i_ + k < t_.size()) ? t_[i_ + k] : t_.back();
    }
    const Token& take() {
        const Token& tk = t_[i_];
        if (tk.kind != Tok::Eof) ++i_;
        return tk;
    }
    bool is(Tok k) const { return cur().kind == k; }
    bool is_kw(const char* w) const { return cur().kind == Tok::Ident && cur().text == w; }

    [[noreturn]] static void fail(SourcePos p, std::string msg) {
        throw ParseError{Diagnostic{p, std::move(msg)}};
    }

    static std::string describe(const Token& tk) {
        if (tk.kind == Tok::Ident) return "`" + tk.text + "`";
        if (tk.kind == Tok::Symbol) return "`@" + tk.text + "`";
        if (tk.kind == Tok::Int) return "the number " + std::to_string(tk.value);
        return tok_name(tk.kind);
    }

    void expect(Tok k, const char* context) {
        if (!is(k))
            fail(cur().pos, std::string("expected ") + tok_name(k) + " " + context + ", found " +
                                describe(cur()));
        take();
    }

    void expect_kw(const char* w, const std::string& context) {
        if (!is_kw(w))
            fail(cur().pos, std::string("expected `") + w + "` " + context + ", found " +
                                describe(cur()));
        take();
    }

    /// A user identifier: a rule label, a `var`, a snapshot name.
    std::string take_user_ident(const char* what) {
        const Token& tk = cur();
        if (tk.kind != Tok::Ident)
            fail(tk.pos, std::string("expected ") + what + ", found " + describe(tk));
        if (is_reserved_word(tk.text) || is_builtin_name(tk.text))
            fail(tk.pos, "`" + tk.text + "` is a reserved name and cannot be " + what);
        return take().text;
    }

    /// Can the current token begin an expression? Used where an expression is
    /// OPTIONAL (`on frame [expr]`, `copper move [reg_spec]`, `dma byte
    /// [addr_spec]`).
    bool starts_expr() const {
        switch (cur().kind) {
            case Tok::Int: case Tok::String: case Tok::Symbol: case Tok::LParen:
            case Tok::MemL: case Tok::Mem16L: case Tok::PhysL: case Tok::NextRegL:
            case Tok::MmuL: case Tok::PageL: case Tok::StackL: case Tok::ChangedL:
            case Tok::DepthL: case Tok::Minus: case Tok::Tilde:
                return true;
            case Tok::Ident:
                return !is_reserved_word(cur().text) || cur().text == "not" ||
                       cur().text == "true" || cur().text == "false";
            default:
                return false;
        }
    }

    StringLit take_string(const std::string& context) {
        if (!is(Tok::String))
            fail(cur().pos, "expected a string " + context + ", found " + describe(cur()));
        const Token& tk = take();
        StringLit s;
        s.pos = tk.pos;
        for (const StrPiece& p : tk.pieces) {
            StringPart part;
            if (!p.is_expr) {
                part.text = p.text;
            } else {
                LexResult lr = lex(p.text, p.pos, /*embedded=*/true);
                if (lr.error) throw ParseError{*lr.error};
                Parser sub(std::move(lr.tokens));
                sub.depth_ = depth_;
                part.expr = sub.parse_whole_expression_in_string();
                part.fmt  = p.fmt;
            }
            s.parts.push_back(std::move(part));
        }
        return s;
    }

    ExprPtr parse_whole_expression_in_string() {
        ExprPtr e = parse_expr();
        if (cur().kind != Tok::Eof)
            fail(cur().pos, "unexpected " + describe(cur()) + " inside `${…}`");
        return e;
    }

    // ── top level ──────────────────────────────────────────────────────────

    VarDecl parse_var() {
        VarDecl v;
        v.pos = take().pos;  // `var`
        v.name = take_user_ident("a variable name");
        expect(Tok::Assign, "after the variable name");
        v.init = parse_expr();
        return v;
    }

    Rule parse_rule() {
        Rule r;
        r.pos = cur().pos;
        if (is_kw("disabled")) {
            const SourcePos dp = take().pos;
            r.disabled = true;
            if (!(is(Tok::Ident) && at(1).kind == Tok::Colon))
                fail(dp, "`disabled` needs a rule label: `disabled NAME: on …`");
        }
        if (is(Tok::Ident) && at(1).kind == Tok::Colon) {
            r.label_pos = cur().pos;
            r.label     = take_user_ident("a rule label");
            take();  // ':'
        }
        expect_kw("on", "to start a rule");
        r.event = parse_event();
        if (is_kw("once")) {
            take();
            r.once = true;
        }
        if (is_kw("when")) {
            take();
            r.when = parse_expr();
            if (is_kw("once"))
                fail(cur().pos, "`once` must come before `when`: `on … once when … do`");
        }
        expect_kw("do", "to open the rule body");
        r.body = parse_actions({"end"}, r.pos);
        take();  // `end`
        return r;
    }

    // ── events ─────────────────────────────────────────────────────────────

    EventSpec parse_event() {
        EventSpec ev;
        ev.pos = cur().pos;
        if (!is(Tok::Ident)) fail(cur().pos, "expected an event after `on`, found " + describe(cur()));
        const std::string w = cur().text;
        if (w == "execute" || w == "read" || w == "write") {
            ev.type = w == "execute" ? EventType::Execute
                    : w == "read"    ? EventType::Read
                                     : EventType::Write;
            take();
            if (!starts_expr() && !is_kw("page"))
                fail(cur().pos, "expected an address, a range or `page` after `" + w + "`, found " +
                                    describe(cur()));
            parse_addr_spec(ev);
        } else if (w == "io_read" || w == "io_write") {
            ev.type = w == "io_read" ? EventType::IoRead : EventType::IoWrite;
            take();
            if (is_kw("mask")) {
                take();
                ev.mask = parse_expr();
                expect_kw("value", "after the port mask");
                ev.value = parse_expr();
            } else {
                parse_range(ev.lo, ev.hi);
            }
        } else if (w == "nextreg") {
            ev.type = EventType::NextReg;
            take();
            parse_range(ev.lo, ev.hi);
        } else if (w == "frame") {
            ev.type = EventType::Frame;
            take();
            if (starts_expr()) ev.lo = parse_expr();
        } else if (w == "scanline" || w == "cycle") {
            ev.type = w == "scanline" ? EventType::Scanline : EventType::Cycle;
            take();
            ev.lo = parse_expr();
        } else if (w == "interrupt" || w == "nmi" || w == "reset" || w == "stop") {
            ev.type = w == "interrupt" ? EventType::Interrupt
                    : w == "nmi"       ? EventType::Nmi
                    : w == "reset"     ? EventType::Reset
                                       : EventType::Stop;
            take();
        } else if (w == "hostkey") {
            ev.type = EventType::HostKey;
            take();
            if (!is(Tok::Int) || cur().value < 1 || cur().value > 8)
                fail(cur().pos, "`hostkey` takes a key number from 1 to 8, found " + describe(cur()));
            ev.hostkey = take().value;
        } else if (w == "copper") {
            ev.type = EventType::Copper;
            take();
            if (is_kw("move")) {
                take();
                ev.copper = CopperSub::Move;
                if (starts_expr()) parse_range(ev.lo, ev.hi);
            } else if (is_kw("wait")) {
                take();
                ev.copper = CopperSub::Wait;
            } else if (is_kw("halt")) {
                take();
                ev.copper = CopperSub::Halt;
            } else {
                fail(cur().pos, "expected `move`, `wait` or `halt` after `copper`, found " +
                                    describe(cur()));
            }
            if (is_kw("at")) {
                take();
                parse_range(ev.at_lo, ev.at_hi);
            }
        } else if (w == "dma") {
            ev.type = EventType::Dma;
            take();
            // `end` HERE is the sub-kind, not the rule terminator (§2.1, row
            // PARSE-DMA-END): the token after `dma` is always a sub-kind.
            if (is_kw("start")) {
                take();
                ev.dma = DmaSub::Start;
            } else if (is_kw("byte")) {
                take();
                ev.dma = DmaSub::Byte;
                if (starts_expr() || is_kw("page")) parse_addr_spec(ev);
            } else if (is_kw("end")) {
                take();
                ev.dma = DmaSub::End;
            } else {
                fail(cur().pos, "expected `start`, `byte` or `end` after `dma`, found " +
                                    describe(cur()));
            }
        } else {
            fail(cur().pos, "unknown event `" + w + "`");
        }
        return ev;
    }

    void parse_range(ExprPtr& lo, ExprPtr& hi) {
        lo = parse_expr();
        if (is(Tok::DotDot)) {
            take();
            hi = parse_expr();
        }
    }

    // addr_spec ::= expr [ ".." expr ] [ "page" expr ] | "page" expr [ ".." expr ]
    void parse_addr_spec(EventSpec& ev) {
        if (is_kw("page")) {
            take();
            ev.page_only = true;
            parse_range(ev.page_lo, ev.page_hi);
            return;
        }
        parse_range(ev.lo, ev.hi);
        if (is_kw("page")) {
            take();
            ev.page_lo = parse_expr();
        }
    }

    // ── actions ────────────────────────────────────────────────────────────

    /// Actions until one of `stops`, which is left unconsumed. `opened_at` is
    /// the construct whose `end` is missing if the input runs out first.
    std::vector<Action> parse_actions(std::initializer_list<const char*> stops, SourcePos opened_at) {
        std::vector<Action> out;
        for (;;) {
            if (is(Tok::Eof))
                fail(cur().pos, "missing `end` for the block opened at line " +
                                    std::to_string(opened_at.line) + ", column " +
                                    std::to_string(opened_at.column));
            for (const char* s : stops)
                if (is_kw(s)) return out;
            out.push_back(parse_action());
        }
    }

    Action parse_action() {
        Action a;
        a.pos = cur().pos;
        if (!is(Tok::Ident)) fail(cur().pos, "expected an action or `end`, found " + describe(cur()));
        const std::string w = cur().text;
        take();
        if (w == "log") {
            a.kind = ActionKind::Log;
            if (is_kw("indent")) {
                take();
                a.e1 = parse_expr();
            }
            a.s1 = take_string("after `log`");
        } else if (w == "stop") {
            a.kind = ActionKind::Stop;
            if (is(Tok::String)) a.s1 = take_string("");
        } else if (w == "assert") {
            a.kind = ActionKind::Assert;
            a.e1   = parse_expr();
            a.s1   = take_string("(the assert message)");
        } else if (w == "exit") {
            a.kind = ActionKind::Exit;
            a.e1   = parse_expr();
        } else if (w == "dump_regs") {
            a.kind = ActionKind::DumpRegs;
        } else if (w == "dump_mmu") {
            a.kind = ActionKind::DumpMmu;
        } else if (w == "dump_mem") {
            a.kind = ActionKind::DumpMem;
            a.e1   = parse_expr();
            a.e2   = parse_expr();
        } else if (w == "snap" || w == "unsnap" || w == "dump_diff") {
            a.kind = w == "snap" ? ActionKind::Snap
                   : w == "unsnap" ? ActionKind::Unsnap
                                   : ActionKind::DumpDiff;
            a.name_pos = cur().pos;
            a.name     = take_user_ident("a snapshot name");
        } else if (w == "enable" || w == "disable") {
            a.kind     = w == "enable" ? ActionKind::Enable : ActionKind::Disable;
            a.name_pos = cur().pos;
            a.name     = take_user_ident("a rule label");
        } else if (w == "screenshot") {
            a.kind = ActionKind::Screenshot;
            a.s1   = take_string("(the screenshot file)");
        } else if (w == "save_snapshot") {
            a.kind = ActionKind::SaveSnapshot;
            a.s1   = take_string("(the snapshot file)");
        } else if (w == "compare_scr") {
            a.kind = ActionKind::CompareScr;
            a.s1   = take_string("(the .scr file)");
            a.s2   = take_string("(the compare_scr message)");
        } else if (w == "press") {
            a.kind = ActionKind::Press;
            a.s1   = take_string("(the key)");
            if (is_kw("for")) {
                take();
                a.e1 = parse_expr();
            }
        } else if (w == "release") {
            a.kind = ActionKind::Release;
            a.s1   = take_string("(the key)");
        } else if (w == "joystick") {
            a.kind = ActionKind::Joystick;
            if (!is(Tok::Int) || (cur().value != 1 && cur().value != 2))
                fail(cur().pos, "`joystick` takes a port number, 1 or 2, found " + describe(cur()));
            a.joystick = take().value;
            a.e1       = parse_expr();
        } else if (w == "set") {
            a.kind   = ActionKind::Set;
            a.target = parse_lvalue();
            expect(Tok::Assign, "after the `set` target");
            a.e1 = parse_expr();
        } else if (w == "out") {
            a.kind = ActionKind::Out;
            a.e1   = parse_expr();
            a.e2   = parse_expr();
        } else if (w == "if") {
            a.kind = ActionKind::If;
            a.e1   = parse_expr();
            expect_kw("then", "after the `if` condition");
            a.then_body = parse_actions({"else", "end"}, a.pos);
            if (is_kw("else")) {
                take();
                a.has_else  = true;
                a.else_body = parse_actions({"end"}, a.pos);
            }
            take();  // `end`
        } else {
            // Report at the word itself, not after it.
            fail(a.pos, "expected an action or `end`, found `" + w + "`");
        }
        return a;
    }

    ExprPtr parse_lvalue() {
        static const char* const WHAT =
            "expected a variable, a register, a flag, IFF1, IFF2, IM, AUDIO_MUTE, "
            "mem[], mem16[], phys[] or nextreg[]";
        const Token& tk = cur();
        auto node = std::make_shared<Expr>();
        node->pos = tk.pos;
        switch (tk.kind) {
            case Tok::Ident: {
                if (is_reserved_word(tk.text) || source_constant(tk.text))
                    fail(tk.pos, "`" + tk.text + "` cannot be assigned: " + WHAT);
                if (auto b = live_builtin(tk.text)) {
                    if (!is_assignable(*b))
                        fail(tk.pos, "`" + tk.text + "` cannot be assigned: " + WHAT);
                    node->kind = ExprKind::Name;
                    node->text = tk.text;
                    take();
                    return node;
                }
                if (payload_builtin(tk.text))
                    fail(tk.pos, "`" + tk.text + "` cannot be assigned: " + WHAT);
                if (at(1).kind == Tok::Dot)
                    fail(tk.pos, "a snapshot field cannot be assigned: " + std::string(WHAT));
                node->kind = ExprKind::Var;
                node->text = tk.text;
                take();
                return node;
            }
            case Tok::MemL:
            case Tok::Mem16L:
            case Tok::NextRegL: {
                node->kind = tk.kind == Tok::MemL     ? ExprKind::Mem
                           : tk.kind == Tok::Mem16L   ? ExprKind::Mem16
                                                      : ExprKind::NextReg;
                take();
                node->a = parse_expr();
                expect(Tok::RBracket, "to close the index");
                return node;
            }
            case Tok::PhysL: {
                node->kind = ExprKind::Phys;
                take();
                node->a = parse_expr();
                expect(Tok::Comma, "between the page and the offset");
                node->b = parse_expr();
                expect(Tok::RBracket, "to close `phys[`");
                return node;
            }
            default:
                fail(tk.pos, describe(tk) + " cannot be assigned: " + WHAT);
        }
    }

    // ── expressions ────────────────────────────────────────────────────────
    //
    // §2.1 precedence, low to high: or; and; not; == != < > <= >=; | ^; &;
    // << >>; + -; * / %; unary - ~.

    struct DepthGuard {
        Parser& p;
        explicit DepthGuard(Parser& pp) : p(pp) {
            if (++p.depth_ > MAX_DEPTH)
                fail(p.cur().pos, "expression nested too deeply");
        }
        ~DepthGuard() { --p.depth_; }
    };

    static ExprPtr binary(Op op, SourcePos pos, ExprPtr l, ExprPtr r) {
        auto e  = std::make_shared<Expr>();
        e->kind = ExprKind::Binary;
        e->op   = op;
        e->pos  = pos;
        e->a    = std::move(l);
        e->b    = std::move(r);
        return e;
    }

    static ExprPtr unary(Op op, SourcePos pos, ExprPtr a) {
        auto e  = std::make_shared<Expr>();
        e->kind = ExprKind::Unary;
        e->op   = op;
        e->pos  = pos;
        e->a    = std::move(a);
        return e;
    }

    ExprPtr parse_expr() {
        DepthGuard g(*this);
        return parse_or();
    }

    ExprPtr parse_or() {
        ExprPtr l = parse_and();
        while (is_kw("or")) {
            const SourcePos p = take().pos;
            l = binary(Op::Or, p, l, parse_and());
        }
        return l;
    }

    ExprPtr parse_and() {
        ExprPtr l = parse_not();
        while (is_kw("and")) {
            const SourcePos p = take().pos;
            l = binary(Op::And, p, l, parse_not());
        }
        return l;
    }

    ExprPtr parse_not() {
        if (is_kw("not")) {
            DepthGuard g(*this);
            const SourcePos p = take().pos;
            return unary(Op::Not, p, parse_not());
        }
        return parse_cmp();
    }

    ExprPtr parse_cmp() {
        ExprPtr l = parse_bor();
        for (;;) {
            Op op;
            switch (cur().kind) {
                case Tok::Eq: op = Op::Eq; break;
                case Tok::Ne: op = Op::Ne; break;
                case Tok::Lt: op = Op::Lt; break;
                case Tok::Gt: op = Op::Gt; break;
                case Tok::Le: op = Op::Le; break;
                case Tok::Ge: op = Op::Ge; break;
                default: return l;
            }
            const SourcePos p = take().pos;
            l = binary(op, p, l, parse_bor());
        }
    }

    ExprPtr parse_bor() {
        ExprPtr l = parse_band();
        while (is(Tok::Pipe) || is(Tok::Caret)) {
            const Op op = is(Tok::Pipe) ? Op::BitOr : Op::BitXor;
            const SourcePos p = take().pos;
            l = binary(op, p, l, parse_band());
        }
        return l;
    }

    ExprPtr parse_band() {
        ExprPtr l = parse_shift();
        while (is(Tok::Amp)) {
            const SourcePos p = take().pos;
            l = binary(Op::BitAnd, p, l, parse_shift());
        }
        return l;
    }

    ExprPtr parse_shift() {
        ExprPtr l = parse_add();
        while (is(Tok::Shl) || is(Tok::Shr)) {
            const Op op = is(Tok::Shl) ? Op::Shl : Op::Shr;
            const SourcePos p = take().pos;
            l = binary(op, p, l, parse_add());
        }
        return l;
    }

    ExprPtr parse_add() {
        ExprPtr l = parse_mul();
        while (is(Tok::Plus) || is(Tok::Minus)) {
            const Op op = is(Tok::Plus) ? Op::Add : Op::Sub;
            const SourcePos p = take().pos;
            l = binary(op, p, l, parse_mul());
        }
        return l;
    }

    ExprPtr parse_mul() {
        ExprPtr l = parse_unary();
        while (is(Tok::Star) || is(Tok::Slash) || is(Tok::Percent)) {
            const Op op = is(Tok::Star) ? Op::Mul : is(Tok::Slash) ? Op::Div : Op::Mod;
            const SourcePos p = take().pos;
            l = binary(op, p, l, parse_unary());
        }
        return l;
    }

    ExprPtr parse_unary() {
        if (is(Tok::Minus) || is(Tok::Tilde)) {
            DepthGuard g(*this);
            const Op op = is(Tok::Minus) ? Op::Neg : Op::BitNot;
            const SourcePos p = take().pos;
            return unary(op, p, parse_unary());
        }
        return parse_primary();
    }

    ExprPtr index_node(ExprKind k, SourcePos p, const char* close_what) {
        auto e  = std::make_shared<Expr>();
        e->kind = k;
        e->pos  = p;
        e->a    = parse_expr();
        expect(Tok::RBracket, close_what);
        return e;
    }

    ExprPtr parse_primary() {
        const Token& tk = cur();
        const SourcePos p = tk.pos;
        switch (tk.kind) {
            case Tok::Int: {
                auto e   = std::make_shared<Expr>();
                e->kind  = ExprKind::Int;
                e->pos   = p;
                e->value = take().value;
                return e;
            }
            case Tok::String: {
                auto e  = std::make_shared<Expr>();
                e->kind = ExprKind::Str;
                e->pos  = p;
                e->str  = take_string("");
                return e;
            }
            case Tok::Symbol: {
                auto e  = std::make_shared<Expr>();
                e->kind = ExprKind::Symbol;
                e->pos  = p;
                e->text = take().text;
                return e;
            }
            case Tok::LParen: {
                take();
                ExprPtr e = parse_expr();
                expect(Tok::RParen, "to close `(`");
                return e;
            }
            case Tok::MemL:     take(); return index_node(ExprKind::Mem, p, "to close `mem[`");
            case Tok::Mem16L:   take(); return index_node(ExprKind::Mem16, p, "to close `mem16[`");
            case Tok::NextRegL: take(); return index_node(ExprKind::NextReg, p, "to close `nextreg[`");
            case Tok::MmuL:     take(); return index_node(ExprKind::Mmu, p, "to close `mmu[`");
            case Tok::PageL:    take(); return index_node(ExprKind::Page, p, "to close `page[`");
            case Tok::StackL:   take(); return index_node(ExprKind::Stack, p, "to close `stack[`");
            case Tok::PhysL: {
                take();
                auto e  = std::make_shared<Expr>();
                e->kind = ExprKind::Phys;
                e->pos  = p;
                e->a    = parse_expr();
                expect(Tok::Comma, "between the page and the offset");
                e->b = parse_expr();
                expect(Tok::RBracket, "to close `phys[`");
                return e;
            }
            case Tok::ChangedL: {
                take();
                auto e  = std::make_shared<Expr>();
                e->kind = ExprKind::Changed;
                e->pos  = p;
                e->text = take_user_ident("a snapshot name");
                expect(Tok::Comma, "after the snapshot name");
                const Token& g = cur();
                if (g.kind != Tok::Ident)
                    fail(g.pos, "expected a `changed()` group (regs, mmu, iff1 or stack0), found " +
                                    describe(g));
                if (g.text == "regs")        e->group = SnapGroup::Regs;
                else if (g.text == "mmu")    e->group = SnapGroup::Mmu;
                else if (g.text == "iff1")   e->group = SnapGroup::Iff1;
                else if (g.text == "stack0") e->group = SnapGroup::Stack0;
                else
                    fail(g.pos, "unknown `changed()` group `" + g.text +
                                    "` (expected regs, mmu, iff1 or stack0)");
                take();
                expect(Tok::RParen, "to close `changed(`");
                return e;
            }
            case Tok::DepthL: {
                take();
                auto e  = std::make_shared<Expr>();
                e->kind = ExprKind::Depth;
                e->pos  = p;
                e->text = take_user_ident("a snapshot name");
                expect(Tok::RParen, "to close `depth(`");
                return e;
            }
            case Tok::Ident: {
                const std::string w = tk.text;
                auto e = std::make_shared<Expr>();
                e->pos = p;
                if (w == "true" || w == "false") {
                    take();
                    e->kind  = ExprKind::Int;
                    e->value = (w == "true") ? 1 : 0;
                    return e;
                }
                if (auto c = source_constant(w)) {
                    take();
                    e->kind  = ExprKind::Int;
                    e->value = *c;
                    return e;
                }
                if (is_reserved_word(w)) fail(p, "expected an expression, found `" + w + "`");
                if (live_builtin(w) || payload_builtin(w)) {
                    take();
                    e->kind = ExprKind::Name;
                    e->text = w;
                    return e;
                }
                take();
                if (is(Tok::Dot)) {
                    take();
                    e->kind = ExprKind::SnapField;
                    e->text = w;
                    if (!is(Tok::Ident))
                        fail(cur().pos, "expected a snapshot field after `.`, found " + describe(cur()));
                    e->field = take().text;
                    if (is(Tok::LBracket)) {
                        take();
                        e->a = parse_expr();
                        expect(Tok::RBracket, "to close the field index");
                    }
                    return e;
                }
                e->kind = ExprKind::Var;
                e->text = w;
                return e;
            }
            default:
                fail(p, "expected an expression, found " + describe(tk));
        }
    }
};

}  // namespace

ParseResult parse_script(const std::string& text) {
    ParseResult out;
    LexResult lr = lex(text);
    if (lr.error) {
        out.error = lr.error;
        return out;
    }
    try {
        Parser p(std::move(lr.tokens));
        out.script = p.parse_script();
    } catch (const ParseError& e) {
        out.script = Script{};
        out.error  = e.d;
    }
    return out;
}

ExprParseResult parse_expression(const std::string& text) {
    ExprParseResult out;
    LexResult lr = lex(text);
    if (lr.error) {
        out.error = lr.error;
        return out;
    }
    try {
        Parser p(std::move(lr.tokens));
        out.expr = p.parse_whole_expression();
    } catch (const ParseError& e) {
        out.expr  = nullptr;
        out.error = e.d;
    }
    return out;
}

}  // namespace script
}  // namespace jnext
