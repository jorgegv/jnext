// jnext::script — the script engine. See script_engine.h.

#include "script/script_engine.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "script/check.h"
#include "script/evaluator.h"
#include "script/expr_compiler.h"
#include "script/names.h"
#include "script/parser.h"

namespace jnext {
namespace script {

using dbg::Access;
using Verdict = dbg::Action;
using dbg::Event;
using dbg::EventKind;
using dbg::Result;
using dbg::Subscription;

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

struct ScriptEngine::RuleRec {
    Unit*       unit = nullptr;
    const Rule* rule = nullptr;
    std::vector<dbg::EventId>      subs;
    std::vector<Subscription>      templates;  ///< what was registered (re-arm)
    bool     enabled = true;
    bool     dead    = false;
    bool     fired   = false;  ///< a `once` rule that fired (backend: spent)
    uint64_t hits    = 0;
};

struct ScriptEngine::Unit {
    std::string                  file;
    ParseResult                  parsed;
    std::shared_ptr<ScriptState> state;
    std::vector<std::unique_ptr<RuleRec>> rules;
};

struct ScriptEngine::Deferred {
    enum class Kind : uint8_t { Joystick, CompareScr } kind = Kind::Joystick;
    RuleRec*    rule = nullptr;
    int         port = 1;
    uint16_t    bits = 0;
    std::string file;
    std::string msg;
};

namespace {

std::string hex(unsigned v, int width) {
    char b[16];
    std::snprintf(b, sizeof b, "%0*X", width, v);
    return b;
}

[[noreturn]] void fail(SourcePos p, std::string m) {
    throw EvalError{Diagnostic{p, std::move(m)}};
}

std::string rule_name(const Rule& r) {
    if (!r.label.empty()) return "`" + r.label + "`";
    return "at " + std::to_string(r.pos.line) + ":" + std::to_string(r.pos.column);
}

bool mentions_page_equality(const Expr* e) {
    if (!e) return false;
    if (e->kind == ExprKind::Binary && (e->op == Op::Eq || e->op == Op::Ne)) {
        if ((e->a && e->a->kind == ExprKind::Name && e->a->builtin == Builtin::P_PAGE) ||
            (e->b && e->b->kind == ExprKind::Name && e->b->builtin == Builtin::P_PAGE))
            return true;
    }
    return mentions_page_equality(e->a.get()) || mentions_page_equality(e->b.get());
}

// The RegId a register / flag / interrupt lvalue writes. Flags write F.
bool reg_of(Builtin b, dbg::RegId& out) {
    using R = dbg::RegId;
    switch (b) {
        case Builtin::A: out = R::A; return true;   case Builtin::B: out = R::B; return true;
        case Builtin::C: out = R::C; return true;   case Builtin::D: out = R::D; return true;
        case Builtin::E: out = R::E; return true;   case Builtin::H: out = R::H; return true;
        case Builtin::L: out = R::L; return true;   case Builtin::F: out = R::F; return true;
        case Builtin::I: out = R::I; return true;   case Builtin::R: out = R::R; return true;
        case Builtin::AF: out = R::AF; return true; case Builtin::BC: out = R::BC; return true;
        case Builtin::DE: out = R::DE; return true; case Builtin::HL: out = R::HL; return true;
        case Builtin::IX: out = R::IX; return true; case Builtin::IY: out = R::IY; return true;
        case Builtin::SP: out = R::SP; return true; case Builtin::PC: out = R::PC; return true;
        case Builtin::AF2: out = R::AF2; return true; case Builtin::BC2: out = R::BC2; return true;
        case Builtin::DE2: out = R::DE2; return true; case Builtin::HL2: out = R::HL2; return true;
        case Builtin::IFF1: out = R::IFF1; return true; case Builtin::IFF2: out = R::IFF2; return true;
        case Builtin::IM: out = R::IM; return true;
        default: return false;
    }
}

int flag_bit(Builtin b) {
    switch (b) {
        case Builtin::CF: return 0; case Builtin::NF: return 1; case Builtin::PF: return 2;
        case Builtin::HF: return 4; case Builtin::ZF: return 6; case Builtin::SF: return 7;
        default: return -1;
    }
}

// `row,col` (the recorder's form for a bit with no key name) or a name.
bool key_of(const std::string& s, dbg::MatrixKey& k) {
    if (s.size() == 3 && std::isdigit(static_cast<unsigned char>(s[0])) && s[1] == ',' &&
        std::isdigit(static_cast<unsigned char>(s[2]))) {
        k      = dbg::MatrixKey{};
        k.row1 = s[0] - '0';
        k.col1 = s[2] - '0';
        return k.row1 < 8 && k.col1 < 5;
    }
    return dbg::key_name_to_matrix(s, k);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ScriptEngine::ScriptEngine(dbg::Debugger& dbg, EngineHost host) : dbg_(dbg), host_(std::move(host)) {
    const auto a = dbg_.attach(dbg::ClientInfo{"script engine", dbg::ClientKind::Script});
    if (a) {
        cid_ = a.value;
        dbg_.set_listener(cid_, this);
    }
}

ScriptEngine::~ScriptEngine() {
    unload_all();
    if (cid_ != dbg::CLIENT_NONE) {
        dbg_.set_listener(cid_, nullptr);
        dbg_.detach(cid_);
    }
}

void ScriptEngine::unload_all() {
    for (auto& u : units_)
        for (auto& r : u->rules)
            for (dbg::EventId id : r->subs) dbg_.unsubscribe(cid_, id);
    units_.clear();
    if (edge_ != dbg::EVENT_NONE) dbg_.unsubscribe(cid_, edge_);
    edge_ = dbg::EVENT_NONE;
    deferred_.clear();
    saves_.clear();
    host_keys_.clear();
}

void ScriptEngine::log(dbg::LogLevel level, const std::string& text) { dbg_.log(cid_, level, text); }

std::string ScriptEngine::stamp() const {
    // `C:` is the `CYCLE` the running rule reads: the event's own cycle inside
    // a delivery (Appendix I), the live clock otherwise.
    const dbg::Time t = dbg_.time();
    const uint64_t c  = cur_cycle_ ? *cur_cycle_ : t.master_cycle;
    return "[jds F:" + std::to_string(t.frame) + " C:" + std::to_string(c) + "]";
}

// ---------------------------------------------------------------------------
// Loading and registration
// ---------------------------------------------------------------------------

LoadResult ScriptEngine::load(const std::string& text, const std::string& file) {
    LoadResult out;
    out.file = file;
    if (cid_ == dbg::CLIENT_NONE) {
        out.errors.push_back(Diagnostic{SourcePos{}, "the script engine could not attach to the debugger"});
        return out;
    }
    auto u    = std::make_unique<Unit>();
    u->file   = file;
    u->parsed = parse_script(text);
    if (!u->parsed.ok()) {
        out.errors.push_back(*u->parsed.error);
        return out;
    }
    CheckOptions opts;
    opts.symbols = symbols_of(dbg_);
    out.errors = check_script(u->parsed.script, opts);
    if (!out.errors.empty()) return out;
    u->state = std::make_shared<ScriptState>(u->parsed.script);
    try {
        init_vars(u->parsed.script, *u->state, dbg_);
    } catch (const EvalError& e) {
        out.errors.push_back(e.d);
        return out;
    }
    for (const Rule& r : u->parsed.script.rules) {
        auto rec  = std::make_unique<RuleRec>();
        rec->unit = u.get();
        rec->rule = &r;
        rec->enabled = !r.disabled;
        u->rules.push_back(std::move(rec));
    }
    // Build every subscription first: a filter error anywhere rejects the
    // whole script before anything is registered.
    for (auto& rec : u->rules) {
        if (rec->rule->event.type == EventType::Stop) continue;
        rec->templates = subscriptions_for(*u, *rec, out.errors, out.warnings);
    }
    if (!out.errors.empty()) return out;
    for (auto& rec : u->rules) {
        for (const Subscription& s : rec->templates) {
            const auto id = dbg_.subscribe(cid_, s);
            if (!id) {
                out.errors.push_back(Diagnostic{rec->rule->pos, std::string("the backend refused the "
                                                "subscription (") + dbg::result_name(id.status) + ")"});
                continue;
            }
            rec->subs.push_back(id.value);
        }
    }
    if (!out.errors.empty()) {
        for (auto& rec : u->rules)
            for (dbg::EventId id : rec->subs) dbg_.unsubscribe(cid_, id);
        return out;
    }
    for (const Diagnostic& w : out.warnings)
        log(dbg::LogLevel::Warn, "SCRIPT WARNING " + file + ":" + w.to_string());
    log(dbg::LogLevel::Info, "SCRIPT loaded " + file + ": " + std::to_string(u->rules.size()) + " rules");
    units_.push_back(std::move(u));
    return out;
}

std::vector<Subscription> ScriptEngine::subscriptions_for(Unit& u, RuleRec& rec,
                                                          std::vector<Diagnostic>& errors,
                                                          std::vector<Diagnostic>& warnings) {
    const Rule& r = *rec.rule;
    const EventSpec& ev = r.event;
    std::vector<Subscription> out;
    EvalContext ctx{dbg_, nullptr, u.state.get(), nullptr};
    bool bad = false;

    // A filter bound, evaluated now (§2.2: registered at load time).
    auto value = [&](const ExprPtr& e, int32_t lo, int32_t hi, const char* what) -> int32_t {
        if (!e || bad) return 0;
        try {
            const int32_t v = eval_int(*e, ctx);
            if (v < lo || v > hi) {
                errors.push_back(Diagnostic{e->pos, std::string(what) + " " + std::to_string(v) +
                                                        " is outside " + std::to_string(lo) + ".." +
                                                        std::to_string(hi)});
                bad = true;
            }
            return v;
        } catch (const EvalError& x) {
            errors.push_back(x.d);
            bad = true;
            return 0;
        }
    };
    // lo..hi with hi defaulting to lo, hi >= lo.
    auto range = [&](const ExprPtr& l, const ExprPtr& h, int32_t max, const char* what,
                     int32_t& lo, int32_t& hi) {
        lo = value(l, 0, max, what);
        hi = h ? value(h, 0, max, what) : lo;
        if (!bad && hi < lo) {
            errors.push_back(Diagnostic{h->pos, std::string(what) + " range ends below its start"});
            bad = true;
        }
    };

    Subscription base;
    base.action  = Verdict::Continue;
    base.once    = r.once;
    base.enabled = rec.enabled;
    RuleRec* self = &rec;
    base.handler = [this, self](const Event& e, dbg::Debugger& d) { return run_rule(*self, e, d); };
    dbg::Condition when;
    if (r.when)
        when = make_condition(r.when, u.state, [this, self](const Diagnostic& d) { runtime_error(*self, d); });
    // An engine-side refinement the backend's filter cannot express.
    std::function<bool(const Event&)> extra;

    // A condition exists iff the rule has a `when` or the engine refines the
    // filter (§8: "a non-null predicate iff `when` was written" — plus the two
    // refinements Appendix I names). A rule a run-time error disabled is
    // stopped by its disabled subscriptions (`runtime_error`) — the backend
    // skips a subscription disabled earlier in the same drain too (row
    // SCRIPT-EV-RUNTIME-SAME-BOUNDARY).
    auto finish = [&](Subscription s) {
        if (when || extra) {
            s.condition = [when, extra](const Event& e, const dbg::Debugger& d) {
                if (extra && !extra(e)) return false;
                return !when || when(e, d);
            };
        }
        out.push_back(std::move(s));
    };

    switch (ev.type) {
        case EventType::Execute:
        case EventType::Read:
        case EventType::Write: {
            Subscription s = base;
            s.kind = ev.type == EventType::Execute ? EventKind::Execute : EventKind::Mem;
            if (ev.type == EventType::Read)  s.access = Access::Read;
            if (ev.type == EventType::Write) s.access = Access::Write;
            if (ev.page_only) {
                int32_t p1 = 0, p2 = 0;
                range(ev.page_lo, ev.page_hi, 0xFF, "page", p1, p2);
                if (bad) break;
                if (ev.type == EventType::Execute) {
                    // The Execute filter takes ONE page qualifier (events.h):
                    // a page range is one subscription per page (Appendix I,
                    // WP1 finding F3), bounded.
                    if (p2 - p1 + 1 > MAX_EXECUTE_PAGES) {
                        errors.push_back(Diagnostic{ev.page_lo->pos,
                            "an `execute` page range may name at most " + std::to_string(MAX_EXECUTE_PAGES) +
                            " pages"});
                        bad = true;
                        break;
                    }
                    for (int32_t p = p1; p <= p2; ++p) {
                        Subscription one = s;
                        one.filter.page = static_cast<uint16_t>(p);
                        finish(one);
                    }
                } else {
                    for (int32_t p = p1; p <= p2; ++p) s.filter.pages.push_back(static_cast<uint16_t>(p));
                    finish(s);
                }
                break;
            }
            int32_t lo = 0, hi = 0;
            range(ev.lo, ev.hi, 0xFFFF, "address", lo, hi);
            if (ev.page_lo) s.filter.page = static_cast<uint16_t>(value(ev.page_lo, 0, 0xFF, "page"));
            if (bad) break;
            s.filter.lo = static_cast<uint16_t>(lo);
            s.filter.hi = static_cast<uint16_t>(hi);
            // §3(a): a PAGE predicate over more than one slot drains every
            // access in the range through the predicate — the page filter is
            // the cheap form. A warning, at registration where the bounds are
            // known (WP1 finding F4).
            if (!ev.page_lo && (lo >> 13) != (hi >> 13) && mentions_page_equality(r.when.get()))
                warnings.push_back(Diagnostic{r.when->pos,
                    "`PAGE ==` over a range wider than one 8K slot evaluates the condition on every "
                    "access in it; the `page` filter (`… page P`) is matched in the backend"});
            finish(s);
            break;
        }
        case EventType::IoRead:
        case EventType::IoWrite: {
            Subscription s = base;
            s.kind   = EventKind::Port;
            s.access = ev.type == EventType::IoRead ? Access::Read : Access::Write;
            if (ev.mask) {
                s.filter.port_mask  = static_cast<uint16_t>(value(ev.mask, 0, 0xFFFF, "port mask"));
                s.filter.port_value = static_cast<uint16_t>(value(ev.value, 0, 0xFFFF, "port value"));
            } else {
                int32_t lo = 0, hi = 0;
                range(ev.lo, ev.hi, 0xFFFF, "port", lo, hi);
                // GH #222 (§2.1 `port_spec`): a 0x00xx port decodes on its LOW
                // byte, any other exactly — for a single port and a range
                // alike. A range straddling 0xFF would need both at once.
                const bool low = hi <= 0xFF;
                if (!bad && !low && lo <= 0xFF) {
                    errors.push_back(Diagnostic{ev.lo->pos,
                        "a port range lies wholly in 0x00..0xFF (decoded on the low byte, GH #222) or "
                        "wholly above it"});
                    bad = true;
                } else if (lo == hi) {
                    s.filter.port_mask  = low ? 0x00FF : 0xFFFF;
                    s.filter.port_value = static_cast<uint16_t>(lo);
                } else {
                    // A range has no mask/value form: match every port, keep the range.
                    s.filter.port_mask  = 0;
                    s.filter.port_value = 0;
                    extra = [lo, hi, low](const Event& e) {
                        const int32_t p = low ? (e.port & 0xFF) : e.port;
                        return p >= lo && p <= hi;
                    };
                }
            }
            if (!bad) finish(s);
            break;
        }
        case EventType::NextReg: {
            Subscription s = base;
            s.kind = EventKind::NextRegWrite;
            int32_t lo = 0, hi = 0;
            range(ev.lo, ev.hi, 0xFF, "register", lo, hi);
            if (bad) break;
            for (int32_t k = lo; k <= hi; ++k) s.filter.regs.push_back(static_cast<uint8_t>(k));
            finish(s);
            break;
        }
        case EventType::Frame: {
            Subscription s = base;
            s.kind = EventKind::Frame;
            s.filter.frame = ev.lo ? static_cast<uint32_t>(value(ev.lo, 0, INT32_MAX, "frame"))
                                   : dbg::FRAME_EVERY;
            if (!bad) finish(s);
            break;
        }
        case EventType::Scanline: {
            Subscription s = base;
            s.kind = EventKind::Scanline;
            s.filter.scanline = static_cast<int16_t>(value(ev.lo, 0, 1023, "scanline"));
            if (!bad) finish(s);
            break;
        }
        case EventType::Cycle: {
            Subscription s = base;
            s.kind = EventKind::Cycle;
            s.filter.cycle = static_cast<uint64_t>(value(ev.lo, 0, INT32_MAX, "cycle"));
            if (!bad) finish(s);
            break;
        }
        case EventType::Interrupt: { Subscription s = base; s.kind = EventKind::IntAck; finish(s); break; }
        case EventType::Nmi:       { Subscription s = base; s.kind = EventKind::Nmi; finish(s); break; }
        case EventType::Reset: {
            Subscription s = base;
            s.kind = EventKind::Reset;
            s.filter.reset_kind = dbg::ResetKind::Any;
            finish(s);
            break;
        }
        case EventType::HostKey: {
            Subscription s = base;
            s.kind = EventKind::Host;
            std::snprintf(s.filter.host_name, sizeof s.filter.host_name, "script%d", ev.hostkey);
            finish(s);
            break;
        }
        case EventType::Copper: {
            Subscription s = base;
            s.kind = EventKind::Copper;
            s.filter.copper_kind = ev.copper == CopperSub::Move   ? dbg::CopperEventKind::Move
                                 : ev.copper == CopperSub::Wait   ? dbg::CopperEventKind::Wait
                                                                  : dbg::CopperEventKind::Halt;
            if (ev.lo) {
                int32_t lo = 0, hi = 0;
                range(ev.lo, ev.hi, 0xFF, "register", lo, hi);
                for (int32_t k = lo; k <= hi && !bad; ++k) s.filter.regs.push_back(static_cast<uint8_t>(k));
            }
            if (ev.at_lo) {
                int32_t lo = 0, hi = 0;
                range(ev.at_lo, ev.at_hi, 1023, "copper PC", lo, hi);
                s.filter.lo = static_cast<uint16_t>(lo);
                s.filter.hi = static_cast<uint16_t>(hi);
            }
            if (!bad) finish(s);
            break;
        }
        case EventType::Dma: {
            Subscription s = base;
            s.kind = EventKind::Dma;
            s.filter.dma_kind = ev.dma == DmaSub::Start ? dbg::DmaEventKind::Start
                              : ev.dma == DmaSub::Byte  ? dbg::DmaEventKind::Byte
                                                        : dbg::DmaEventKind::End;
            if (ev.page_only || ev.page_lo) {
                // The DMA filter has no page (WP1 finding F2).
                errors.push_back(Diagnostic{ev.page_lo->pos,
                    "`dma byte` takes an address range, not a page: the DMA has no page filter"});
                bad = true;
                break;
            }
            if (ev.lo) {
                int32_t lo = 0, hi = 0;
                range(ev.lo, ev.hi, 0xFFFF, "address", lo, hi);
                if (bad) break;
                // The backend's Byte filter matches EITHER endpoint (events.h);
                // §2.1 makes it the DESTINATION: the filter pre-selects, this
                // refines (WP1 finding F2).
                s.filter.lo = static_cast<uint16_t>(lo);
                s.filter.hi = static_cast<uint16_t>(hi);
                extra = [lo, hi](const Event& e) { return e.dma_dst >= lo && e.dma_dst <= hi; };
            }
            finish(s);
            break;
        }
        case EventType::Stop:
            break;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Delivery
// ---------------------------------------------------------------------------

Verdict ScriptEngine::run_rule(RuleRec& r, const Event& ev, dbg::Debugger& d) {
    if (ev.overflowed && ev.cycle != overflow_logged_cycle_) {
        overflow_logged_cycle_ = ev.cycle;
        log(dbg::LogLevel::Warn, "SCRIPT: event ring overflowed at CYCLE " + std::to_string(ev.cycle) +
                                     ", " + std::to_string(ev.dropped) + " events dropped");
    }
    ++r.hits;
    if (r.rule->once && !r.fired) {
        // §2.2: `once` is the RULE's. The backend spends the subscription
        // that fired; a rule of several (an execute page range) spends the
        // others here, and `enable` re-arms them all together.
        r.fired = true;
        for (dbg::EventId id : r.subs) dbg_.set_enabled(cid_, id, false);
    }
    Verdict verdict = Verdict::Continue;
    const bool was = in_frame_delivery_;
    const auto was_cycle = cur_cycle_;
    in_frame_delivery_ = ev.kind == EventKind::Frame;
    cur_cycle_         = ev.cycle;
    try {
        exec(r, r.rule->body, ev, d, verdict);
    } catch (const EvalError& e) {
        runtime_error(r, e.d);
    }
    in_frame_delivery_ = was;
    cur_cycle_         = was_cycle;
    return verdict;
}

void ScriptEngine::runtime_error(RuleRec& r, const Diagnostic& d) {
    if (r.dead) return;
    r.dead = true;
    ++runtime_errors_;
    for (dbg::EventId id : r.subs) dbg_.set_enabled(cid_, id, false);
    log(dbg::LogLevel::Error, "SCRIPT ERROR " + r.unit->file + ":" + d.to_string() + " — rule " +
                                  rule_name(*r.rule) + " disabled");
    error_exit_pending_ = true;
    ensure_edge();
}

void ScriptEngine::exec(RuleRec& r, const std::vector<Action>& body, const Event& ev, dbg::Debugger& d,
                        Verdict& verdict) {
    ScriptState& st = *r.unit->state;
    const std::string* reason = r.rule->event.type == EventType::Stop ? &last_stop_reason_ : nullptr;
    const EvalContext ctx{d, &ev, &st, reason};

    auto mutated = [&]() {
        if (!rewind_warned_ && d.rewind_enabled()) {
            rewind_warned_ = true;
            log(dbg::LogLevel::Warn, "SCRIPT: a script mutates the machine while the rewind buffer is on; "
                                     "a rewind into a mutated span is refused (§2.7)");
        }
    };
    auto refused = [&](SourcePos p, const std::string& what, Result res) {
        if (res != Result::Ok) fail(p, what + " was refused (" + dbg::result_name(res) + ")");
    };
    auto stop_with = [&](const std::string& why) {
        stop_reason_ = why;
        if (reason == nullptr) {   // a `stop` in an `on stop` body only logs
            ++stops_;
            last_stop_ = why;
        }
        // PC and CYCLE are the event's (the causing instruction, §6.3).
        log(dbg::LogLevel::Warn, "SCRIPT STOP: " + why + " at PC=" + hex(ev.pc, 4) + " FRAME=" +
                                     std::to_string(d.time().frame) + " CYCLE=" +
                                     std::to_string(ev.cycle));
        verdict = Verdict::Stop;
    };

    for (const Action& a : body) {
        switch (a.kind) {
            case ActionKind::Log: {
                int32_t n = a.e1 ? eval_int(*a.e1, ctx) : 0;
                if (n < 0) n = 0;
                if (n > 255) n = 255;
                log(dbg::LogLevel::Info, stamp() + " " + std::string(static_cast<size_t>(n), ' ') +
                                             interpolate(*a.s1, ctx));
                break;
            }
            case ActionKind::Stop:
                stop_with(a.s1 ? interpolate(*a.s1, ctx) : std::string("stop"));
                break;
            case ActionKind::Assert:
                if (eval_int(*a.e1, ctx) == 0) {
                    const std::string msg = interpolate(*a.s1, ctx);
                    log(dbg::LogLevel::Warn, stamp() + " ASSERT FAILED: " + msg);
                    stop_with(msg);
                }
                break;
            case ActionKind::Exit: {
                int32_t code = eval_int(*a.e1, ctx);
                log(dbg::LogLevel::Info, stamp() + " SCRIPT EXIT " + std::to_string(code));
                if (!first_exit_) first_exit_ = code;
                if (host_.exit) {
                    // §2.6: a capture this script queued that is still pending
                    // (NoFrame — dropped) or failed to write makes the exit
                    // non-zero; an explicit non-zero code is kept.
                    const Result fc = d.flush_captures(cid_);
                    if (fc != Result::Ok) {
                        log(dbg::LogLevel::Error, stamp() + " SCRIPT: a screenshot was not written (" +
                                                      dbg::result_name(fc) + ")");
                        if (code == 0) code = 1;
                    }
                    if (!saves_.empty()) {
                        log(dbg::LogLevel::Error, stamp() + " SCRIPT: " + std::to_string(saves_.size()) +
                                                      " save_snapshot(s) never written");
                        saves_.clear();
                        if (code == 0) code = 1;
                    }
                    host_.exit(code);
                }
                stop_reason_ = "exit " + std::to_string(code);
                verdict = Verdict::Stop;
                break;
            }
            case ActionKind::DumpRegs: {
                const Z80Registers g = d.registers();
                log(dbg::LogLevel::Info,
                    stamp() + " regs AF=" + hex(g.AF, 4) + " BC=" + hex(g.BC, 4) + " DE=" + hex(g.DE, 4) +
                        " HL=" + hex(g.HL, 4) + " IX=" + hex(g.IX, 4) + " IY=" + hex(g.IY, 4) +
                        " SP=" + hex(g.SP, 4) + " PC=" + hex(g.PC, 4) + " AF'=" + hex(g.AF2, 4) +
                        " BC'=" + hex(g.BC2, 4) + " DE'=" + hex(g.DE2, 4) + " HL'=" + hex(g.HL2, 4) +
                        " I=" + hex(g.I, 2) + " R=" + hex(g.R, 2) + " IFF1=" + std::to_string(g.IFF1 ? 1 : 0) +
                        " IFF2=" + std::to_string(g.IFF2 ? 1 : 0) + " IM=" + std::to_string(g.IM));
                break;
            }
            case ActionKind::DumpMmu: {
                const auto slots = d.mmu_slots();
                std::string line = stamp() + " mmu";
                for (size_t k = 0; k < 8; ++k)
                    line += " " + std::to_string(k) + ":" + hex(slots[k].nr_page, 2) + "/" +
                            hex(slots[k].effective_page, 2);
                log(dbg::LogLevel::Info, line);
                break;
            }
            case ActionKind::DumpMem: {
                const int32_t addr = eval_int(*a.e1, ctx);
                const int32_t len  = eval_int(*a.e2, ctx);
                if (addr < 0 || addr > 0xFFFF) fail(a.e1->pos, "`dump_mem` address outside 0..0xFFFF");
                if (len < 0 || len > MAX_DUMP_MEM)
                    fail(a.e2->pos, "`dump_mem` length outside 0.." + std::to_string(MAX_DUMP_MEM));
                std::vector<uint8_t> buf(static_cast<size_t>(len));
                if (len) d.peek(dbg::MemSpace::cpu(), static_cast<uint32_t>(addr), buf.size(), buf.data());
                for (int32_t off = 0; off < len; off += 16) {
                    std::string line = stamp() + " " + hex(static_cast<unsigned>((addr + off) & 0xFFFF), 4) + ":";
                    for (int32_t k = off; k < len && k < off + 16; ++k)
                        line += " " + hex(buf[static_cast<size_t>(k)], 2);
                    log(dbg::LogLevel::Info, line);
                }
                break;
            }
            case ActionKind::Snap:
                st.snap(a.slot, d, a.pos);
                break;
            case ActionKind::Unsnap:
                st.unsnap(a.slot, a.pos);
                break;
            case ActionKind::DumpDiff:
                for (const std::string& l : st.diff(a.slot, d, a.pos))
                    log(dbg::LogLevel::Info, stamp() + " dump_diff " + a.name + ": " + l);
                break;
            case ActionKind::Enable:
            case ActionKind::Disable:
                if (RuleRec* t = find_label(*r.unit, a.name)) set_rule_enabled(*t, a.kind == ActionKind::Enable);
                break;
            case ActionKind::Screenshot: {
                const std::string f = interpolate(*a.s1, ctx);
                const bool scr = f.size() >= 4 && (f.compare(f.size() - 4, 4, ".scr") == 0 ||
                                                   f.compare(f.size() - 4, 4, ".SCR") == 0);
                refused(a.pos, "`screenshot`",
                        d.screenshot(cid_, f, dbg::LAYER_MASK_ALL,
                                     scr ? dbg::ScreenshotFormat::Scr : dbg::ScreenshotFormat::Png));
                break;
            }
            case ActionKind::SaveSnapshot:
                // §2.6: at the next frame BOUNDARY. A rule body runs inside a
                // delivery, where the backend refuses a save that would have to
                // run the frame out, so the engine writes it from the next
                // `pump()` that finds the machine at a boundary.
                saves_.push_back(PendingSave{&r, interpolate(*a.s1, ctx)});
                break;
            case ActionKind::CompareScr: {
                const std::string f = interpolate(*a.s1, ctx);
                const std::string m = interpolate(*a.s2, ctx);
                if (in_frame_delivery_) {
                    if (compare_scr_now(&r, f, m) == Verdict::Stop) verdict = Verdict::Stop;
                } else {
                    Deferred q;
                    q.kind = Deferred::Kind::CompareScr;
                    q.rule = &r;
                    q.file = f;
                    q.msg  = m;
                    deferred_.push_back(q);
                    ensure_edge();
                }
                break;
            }
            case ActionKind::Press:
            case ActionKind::Release: {
                const std::string name = interpolate(*a.s1, ctx);
                dbg::MatrixKey k;
                if (!key_of(name, k)) fail(a.s1->pos, "unknown key `" + name + "`");
                if (a.kind == ActionKind::Press && a.e1) {
                    const int32_t n = eval_int(*a.e1, ctx);
                    if (n < 1) fail(a.e1->pos, "`press … for` needs at least 1 frame");
                    const auto q = d.press_key(cid_, k, n);
                    refused(a.pos, "`press`", q.status);
                } else {
                    const bool down = a.kind == ActionKind::Press;
                    refused(a.pos, "`press`/`release`", d.set_key(cid_, k.row1, k.col1, down));
                    if (k.row2 >= 0)
                        refused(a.pos, "`press`/`release`", d.set_key(cid_, k.row2, k.col2, down));
                }
                break;
            }
            case ActionKind::Joystick: {
                const uint16_t bits = static_cast<uint16_t>(eval_int(*a.e1, ctx) & 0x0FFF);
                const dbg::JoystickSide side =
                    a.joystick == 1 ? dbg::JoystickSide::Left : dbg::JoystickSide::Right;
                if (in_frame_delivery_) {
                    refused(a.pos, "`joystick`", d.set_joystick(cid_, side, bits));
                } else {
                    Deferred q;
                    q.kind = Deferred::Kind::Joystick;
                    q.rule = &r;
                    q.port = a.joystick;
                    q.bits = bits;
                    deferred_.push_back(q);
                    ensure_edge();
                }
                break;
            }
            case ActionKind::Set: {
                const int32_t v = eval_int(*a.e1, ctx);
                const Expr& t = *a.target;
                if (t.kind == ExprKind::Var) {
                    st.set_var(t.slot, v);
                    break;
                }
                mutated();
                if (t.kind == ExprKind::Name) {
                    if (t.builtin == Builtin::AUDIO_MUTE) {
                        refused(a.pos, "`set AUDIO_MUTE`",
                                d.set_audio_mute_mask(cid_, static_cast<uint8_t>(v)));
                        break;
                    }
                    const int bit = flag_bit(t.builtin);
                    if (bit >= 0) {
                        const uint16_t f = d.registers().AF & 0xFF;
                        const uint16_t nf = v ? (f | (1u << bit)) : (f & ~(1u << bit));
                        refused(a.pos, "`set " + t.text + "`", d.set_register(cid_, dbg::RegId::F, nf & 0xFF));
                        break;
                    }
                    dbg::RegId id;
                    if (!reg_of(t.builtin, id)) fail(t.pos, "internal: `" + t.text + "` is not writable");
                    if (t.builtin == Builtin::IM && (v < 0 || v > 2)) fail(a.e1->pos, "`IM` is 0, 1 or 2");
                    refused(a.pos, "`set " + t.text + "`",
                            d.set_register(cid_, id, static_cast<uint16_t>(v & 0xFFFF)));
                    break;
                }
                if (t.kind == ExprKind::Mem || t.kind == ExprKind::Mem16) {
                    const int32_t addr = eval_int(*t.a, ctx);
                    if (addr < 0 || addr > 0xFFFF) fail(t.pos, "`set` address outside 0..0xFFFF");
                    const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
                    const size_t n = t.kind == ExprKind::Mem ? 1 : 2;
                    const auto res = d.poke(cid_, dbg::MemSpace::cpu(), static_cast<uint32_t>(addr), n, b);
                    // A byte dropped on ROM is not a silent success (§2.7): the
                    // backend counts what LANDED (GH #281) and the rest of the
                    // write did not happen, so the script is told how much did.
                    if (res.status == Result::RefusedReadOnly)
                        fail(a.pos, "`set` at " + hex(static_cast<unsigned>(addr), 4) + ": " +
                                        std::to_string(res.value) + " of " + std::to_string(n) +
                                        " byte(s) landed (the rest is read-only)");
                    refused(a.pos, "`set mem`", res.status);
                    break;
                }
                if (t.kind == ExprKind::Phys) {
                    const int32_t page = eval_int(*t.a, ctx);
                    const int32_t off  = eval_int(*t.b, ctx);
                    if (page < 0 || page > 0xFFFF || off < 0 || off > 0x1FFF)
                        fail(t.pos, "`set phys[]` outside a page's 0..0x1FFF");
                    const uint8_t b = static_cast<uint8_t>(v & 0xFF);
                    const auto res = d.poke(cid_, dbg::MemSpace::page(static_cast<uint16_t>(page)),
                                            static_cast<uint32_t>(off), 1, &b);
                    refused(a.pos, "`set phys[]`", res.status);
                    if (res.value < 1) fail(a.pos, "`set phys[]`: the byte did not land");
                    break;
                }
                if (t.kind == ExprKind::NextReg) {
                    const int32_t reg = eval_int(*t.a, ctx);
                    if (reg < 0 || reg > 0xFF) fail(t.pos, "`set nextreg[]` register outside 0..0xFF");
                    refused(a.pos, "`set nextreg[]`",
                            d.nextreg_write(cid_, static_cast<uint8_t>(reg), static_cast<uint8_t>(v & 0xFF)));
                    break;
                }
                fail(t.pos, "internal: bad `set` target");
            }
            case ActionKind::Out: {
                const int32_t port = eval_int(*a.e1, ctx);
                const int32_t val  = eval_int(*a.e2, ctx);
                if (port < 0 || port > 0xFFFF) fail(a.e1->pos, "`out` port outside 0..0xFFFF");
                mutated();
                refused(a.pos, "`out`",
                        d.port_out(cid_, static_cast<uint16_t>(port), static_cast<uint8_t>(val & 0xFF)));
                break;
            }
            case ActionKind::If:
                exec(r, eval_int(*a.e1, ctx) != 0 ? a.then_body : a.else_body, ev, d, verdict);
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// enable / disable
// ---------------------------------------------------------------------------

ScriptEngine::RuleRec* ScriptEngine::find_label(Unit& u, const std::string& label) {
    for (auto& r : u.rules)
        if (r->rule->label == label) return r.get();
    return nullptr;
}

void ScriptEngine::set_rule_enabled(RuleRec& r, bool on) {
    r.enabled = on;
    if (r.dead) return;  // a rule an error disabled stays disabled
    if (on && r.fired) {
        // §2.2: `enable` re-arms a spent `once`. The backend never re-arms a
        // spent `once` (EVT-EXEC-32), so the rule is registered afresh.
        for (dbg::EventId id : r.subs) dbg_.unsubscribe(cid_, id);
        r.subs.clear();
        for (Subscription s : r.templates) {
            s.enabled = true;
            const auto id = dbg_.subscribe(cid_, s);
            if (id) r.subs.push_back(id.value);
        }
        r.fired = false;
        return;
    }
    for (dbg::EventId id : r.subs) dbg_.set_enabled(cid_, id, on);
}

// ---------------------------------------------------------------------------
// The frame edge
// ---------------------------------------------------------------------------

void ScriptEngine::ensure_edge() {
    if (edge_ != dbg::EVENT_NONE) return;
    Subscription s;
    s.kind         = EventKind::Frame;
    s.filter.frame = dbg::FRAME_EVERY;
    s.action       = Verdict::Continue;
    s.handler      = [this](const Event& e, dbg::Debugger&) {
        const auto was = cur_cycle_;
        cur_cycle_     = e.cycle;
        const Verdict v = run_edge(e.frame);
        cur_cycle_     = was;
        return v;
    };
    const auto id  = dbg_.subscribe(cid_, s);
    if (id) edge_ = id.value;
}

Verdict ScriptEngine::run_edge(uint32_t frame) {
    Verdict verdict = Verdict::Continue;
    // Host keys due at this edge FIRST (`--script-key FRAME N`): a `hostkey`
    // rule then runs at E_FRAME with FRAME == FRAME, like `on frame FRAME`,
    // and what it queues for the edge lands at this one.
    for (size_t k = 0; k < host_keys_.size();) {
        if (host_keys_[k].first <= frame) {
            const int key = host_keys_[k].second;
            host_keys_.erase(host_keys_.begin() + static_cast<std::ptrdiff_t>(k));
            dbg_.raise_host_event(cid_, "script" + std::to_string(key));
        } else {
            ++k;
        }
    }
    std::vector<Deferred> q;
    q.swap(deferred_);
    for (const Deferred& d : q) {
        if (d.kind == Deferred::Kind::Joystick) {
            dbg_.set_joystick(cid_, d.port == 1 ? dbg::JoystickSide::Left : dbg::JoystickSide::Right, d.bits);
        } else if (compare_scr_now(d.rule, d.file, d.msg) == Verdict::Stop) {
            verdict = Verdict::Stop;
        }
    }
    if (error_exit_pending_) {
        error_exit_pending_ = false;
        log(dbg::LogLevel::Error, "SCRIPT: a run-time error disabled a rule; exiting 1");
        if (host_.exit) host_.exit(1);
    }
    return verdict;
}

Verdict ScriptEngine::compare_scr_now(RuleRec* r, const std::string& file, const std::string& msg) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (r) runtime_error(*r, Diagnostic{r->rule->pos, "`compare_scr`: cannot read `" + file + "`"});
        return Verdict::Continue;
    }
    const std::vector<uint8_t> want((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::vector<uint8_t> have = dbg_.ula_screen_dump();
    size_t diff = 0;
    const size_t n = want.size() < have.size() ? want.size() : have.size();
    while (diff < n && want[diff] == have[diff]) ++diff;
    if (diff == n && want.size() == have.size()) return Verdict::Continue;
    if (diff < n)
        log(dbg::LogLevel::Warn, stamp() + " compare_scr " + file + ": first difference at offset " +
                                     std::to_string(diff) + " (file " + hex(want[diff], 2) + ", screen " +
                                     hex(have[diff], 2) + ")");
    else
        log(dbg::LogLevel::Warn, stamp() + " compare_scr " + file + ": size " + std::to_string(want.size()) +
                                     " != screen " + std::to_string(have.size()));
    log(dbg::LogLevel::Warn, stamp() + " ASSERT FAILED: " + msg);
    stop_reason_ = msg;
    ++stops_;
    last_stop_ = msg;
    return Verdict::Stop;
}

// ---------------------------------------------------------------------------
// Host keys on a schedule, and the verdicts a run never reached (WP4)
// ---------------------------------------------------------------------------

void ScriptEngine::queue_host_key(uint32_t frame, int key) {
    host_keys_.emplace_back(frame, key);
    ensure_edge();
}

namespace {

std::string hex4(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof b, "%04X", v & 0xFFFFu);
    return b;
}

}  // namespace

// The event as the Script tab lists it, with the filter as REGISTERED (bounds
// resolved, `@symbols` replaced by their addresses).
static std::string describe_event(const Rule& r, const std::vector<dbg::Subscription>& t) {
    const EventSpec& e = r.event;
    auto range = [](unsigned lo, unsigned hi) {
        return lo == hi ? hex4(lo) : hex4(lo) + ".." + hex4(hi);
    };
    std::string s;
    switch (e.type) {
        case EventType::Execute:   s = "execute"; break;
        case EventType::Read:      s = "read"; break;
        case EventType::Write:     s = "write"; break;
        case EventType::IoRead:    s = "io_read"; break;
        case EventType::IoWrite:   s = "io_write"; break;
        case EventType::NextReg:   s = "nextreg"; break;
        case EventType::Frame:     s = "frame"; break;
        case EventType::Scanline:  s = "scanline"; break;
        case EventType::Cycle:     s = "cycle"; break;
        case EventType::Interrupt: s = "interrupt"; break;
        case EventType::Nmi:       s = "nmi"; break;
        case EventType::Reset:     s = "reset"; break;
        case EventType::HostKey:   s = "hostkey " + std::to_string(e.hostkey); break;
        case EventType::Stop:      s = "stop"; break;
        case EventType::Copper:
            s = e.copper == CopperSub::Move ? "copper move" : e.copper == CopperSub::Wait ? "copper wait" : "copper halt";
            break;
        case EventType::Dma:
            s = e.dma == DmaSub::Start ? "dma start" : e.dma == DmaSub::Byte ? "dma byte" : "dma end";
            break;
    }
    if (t.empty()) return s;
    const dbg::EventFilter& f = t.front().filter;
    switch (e.type) {
        case EventType::Execute:
        case EventType::Read:
        case EventType::Write:
            if (e.page_only) {
                const unsigned p1 = e.type == EventType::Execute ? t.front().filter.page : f.pages.front();
                const unsigned p2 = e.type == EventType::Execute ? t.back().filter.page : f.pages.back();
                s += " page " + std::to_string(p1) + (p2 != p1 ? ".." + std::to_string(p2) : "");
            } else {
                s += " " + range(f.lo, f.hi);
                if (f.page != dbg::PAGE_ANY) s += " page " + std::to_string(f.page);
            }
            break;
        case EventType::IoRead:
        case EventType::IoWrite:
            if (e.mask) s += " mask " + hex4(f.port_mask) + " value " + hex4(f.port_value);
            else if (f.port_mask != 0) s += " " + hex4(f.port_value);
            else s += " (range)";
            break;
        case EventType::NextReg:
            if (!f.regs.empty()) s += " " + range(f.regs.front(), f.regs.back());
            break;
        case EventType::Frame:
            if (f.frame != dbg::FRAME_EVERY) s += " " + std::to_string(f.frame);
            break;
        case EventType::Scanline: s += " " + std::to_string(f.scanline); break;
        case EventType::Cycle:    s += " " + std::to_string(f.cycle); break;
        case EventType::Copper:
            if (!f.regs.empty()) s += " " + range(f.regs.front(), f.regs.back());
            if (e.at_lo) s += " at " + std::to_string(f.lo) + (f.hi != f.lo ? ".." + std::to_string(f.hi) : "");
            break;
        case EventType::Dma:
            if (e.lo) s += " " + range(f.lo, f.hi);
            break;
        default: break;
    }
    return s;
}

namespace {

bool has_verdict(const std::vector<Action>& body) {
    for (const Action& a : body) {
        if (a.kind == ActionKind::Exit || a.kind == ActionKind::CompareScr) return true;
        if (a.kind == ActionKind::If && (has_verdict(a.then_body) || has_verdict(a.else_body))) return true;
    }
    return false;
}

}  // namespace

size_t ScriptEngine::unreached_verdicts() const {
    size_t n = deferred_.size() + host_keys_.size();
    for (const auto& u : units_)
        for (const auto& r : u->rules)
            if (r->hits == 0 && has_verdict(r->rule->body)) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// `save_snapshot` — from `pump()`, at a frame boundary
// ---------------------------------------------------------------------------

void ScriptEngine::on_frame_ended(uint32_t) {
    // Pushed from `pump()`, outside every delivery. Mid-frame (a stop paused
    // the machine inside a frame) the save waits for a later boundary rather
    // than run the frame out under the user.
    if (saves_.empty() || !dbg_.at_frame_boundary()) return;
    std::vector<PendingSave> q;
    q.swap(saves_);
    for (const PendingSave& p : q) {
        const Result res = dbg_.save_snapshot(cid_, p.file);
        if (res != Result::Ok && p.rule)
            runtime_error(*p.rule, Diagnostic{p.rule->rule->pos, "`save_snapshot` \"" + p.file +
                                                                     "\" was not written (" +
                                                                     dbg::result_name(res) + ")"});
    }
}

// ---------------------------------------------------------------------------
// `on stop`
// ---------------------------------------------------------------------------

std::string ScriptEngine::pause_reason_text(const dbg::PausedInfo& info) const {
    using K = dbg::PauseReason::Kind;
    // The engine's own stop: the backend names it by the event kind
    // (Breakpoint for an execute rule, Watch for a mem/port rule, Script for
    // the rest) with an empty `text`, so the reason is the one the rule gave.
    if (info.reason.by == cid_ && !stop_reason_.empty()) return stop_reason_;
    const char* k = "none";
    switch (info.reason.kind) {
        case K::None:       k = "none"; break;
        case K::User:       k = "user"; break;
        case K::Breakpoint: k = "breakpoint"; break;
        case K::Watch:      k = "watch"; break;
        case K::Step:       k = "step"; break;
        case K::RunTo:      k = "run_to"; break;
        case K::Magic:      k = "magic"; break;
        case K::Corrupt:    k = "corrupt"; break;
        case K::Script:     k = "script"; break;
    }
    return info.reason.text.empty() ? std::string(k) : std::string(k) + ": " + info.reason.text;
}

void ScriptEngine::on_paused(const dbg::PausedInfo& info) {
    const std::string reason = pause_reason_text(info);
    stop_reason_.clear();
    last_stop_reason_ = reason;
    // A stop is not a backend event: the rule's payload PC is the paused PC
    // (Appendix I, WP1 finding F6), carried on a synthetic Event.
    Event ev;
    ev.kind  = EventKind::Execute;
    ev.pc    = info.pc;
    ev.cycle = info.cycle;
    cur_cycle_ = info.cycle;
    for (auto& u : units_) {
        for (auto& r : u->rules) {
            if (r->rule->event.type != EventType::Stop || r->dead || !r->enabled) continue;
            if (r->rule->once && r->fired) continue;
            try {
                const EvalContext ctx{dbg_, &ev, u->state.get(), &last_stop_reason_};
                if (r->rule->when && eval_int(*r->rule->when, ctx) == 0) continue;
            } catch (const EvalError& e) {
                runtime_error(*r, e.d);
                continue;
            }
            ++r->hits;
            if (r->rule->once) r->fired = true;
            Verdict verdict = Verdict::Continue;  // already paused: a `stop` here only logs
            try {
                exec(*r, r->rule->body, ev, dbg_, verdict);
            } catch (const EvalError& e) {
                runtime_error(*r, e.d);
            }
            last_stop_reason_ = reason;
        }
    }
    stop_reason_.clear();  // a `stop` in an `on stop` body only logged
    cur_cycle_.reset();
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

std::vector<ScriptEngine::RuleView> ScriptEngine::rules() const {
    std::vector<RuleView> out;
    for (const auto& u : units_) {
        for (const auto& r : u->rules) {
            RuleView v;
            v.file    = u->file;
            v.label   = r->rule->label;
            v.pos     = r->rule->pos;
            v.type    = r->rule->event.type;
            v.subs    = r->subs;
            v.enabled = r->enabled;
            v.dead    = r->dead;
            v.hits    = r->hits;
            v.spent   = r->rule->once && r->fired;
            v.verdict = has_verdict(r->rule->body);
            v.event   = describe_event(*r->rule, r->templates);
            out.push_back(v);
        }
    }
    return out;
}

ScriptEngine::Status ScriptEngine::status() const {
    Status s;
    s.exit_code      = first_exit_;
    s.stops          = stops_;
    s.last_stop      = last_stop_;
    s.runtime_errors = runtime_errors_;
    s.unreached      = unreached_verdicts();
    return s;
}

ScriptState* ScriptEngine::state(size_t index) {
    return index < units_.size() ? units_[index]->state.get() : nullptr;
}

}  // namespace script
}  // namespace jnext
