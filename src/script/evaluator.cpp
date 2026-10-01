// jnext::script — the integer evaluator. See evaluator.h.

#include "script/evaluator.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "debug/debugger.h"
#include "script/names.h"

namespace jnext {
namespace script {

namespace {

[[noreturn]] void fail(const Expr& e, std::string msg) {
    throw EvalError{Diagnostic{e.pos, std::move(msg)}};
}

std::string hex(int64_t v) {
    char buf[32];
    if (v < 0)
        std::snprintf(buf, sizeof buf, "-0x%llX", static_cast<unsigned long long>(-v));
    else
        std::snprintf(buf, sizeof buf, "0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

/// The 32-bit two's-complement value of `v` — the wrap of §2.1.
int32_t wrap(uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }
int32_t wrap(int64_t v) { return wrap(static_cast<uint64_t>(v)); }

int32_t flag(uint16_t af, int bit) { return (af >> bit) & 1; }

// The MACHINE constants of §2.3: 0=48K 1=128K 2=+3 3=Pentagon 4=Next. jnext
// has no standalone Pentagon machine type any more (emulator.cpp: "the
// standalone Pentagon machine type was dropped"), so 3 is never produced.
int32_t machine_code(MachineType t) {
    switch (t) {
        case MachineType::ZX48K:      return 0;
        case MachineType::ZX128K:     return 1;
        case MachineType::ZX_PLUS3:   return 2;
        case MachineType::ZXN_ISSUE2: return 4;
    }
    return -1;
}

// The SOURCE constants of §2.1 (CPU 0, DMA 1, COPPER 2) — NOT the backend's
// `EventSource` order (Cpu 0, Copper 1, Dma 2).
int32_t source_code(dbg::EventSource s) {
    switch (s) {
        case dbg::EventSource::Cpu:    return 0;
        case dbg::EventSource::Dma:    return 1;
        case dbg::EventSource::Copper: return 2;
        // Never delivered (events.h: a debugger write raises no event; `Any` is
        // a filter value only).
        case dbg::EventSource::Debugger:
        case dbg::EventSource::Any:
            return -1;
    }
    return -1;
}

// `Host` names are `script1`..`script8` for the DSL's keys (§4.3); KEY is N.
int32_t host_key(const dbg::Event& ev) {
    static const char PREFIX[] = "script";
    const size_t plen = sizeof(PREFIX) - 1;
    if (std::strncmp(ev.host_name, PREFIX, plen) != 0) return 0;
    const char* d = ev.host_name + plen;
    if (d[0] < '1' || d[0] > '8' || d[1] != '\0') return 0;
    return d[0] - '0';
}

uint8_t peek_cpu(const EvalContext& ctx, uint16_t addr) {
    uint8_t b = 0;
    ctx.dbg.peek(dbg::MemSpace::cpu(), addr, 1, &b);
    return b;
}

int32_t cpu_address(const Expr& e, int32_t a, const char* what) {
    if (a < 0 || a > 0xFFFF)
        fail(e, std::string(what) + " address " + hex(a) + " is outside 0..0xFFFF");
    return a;
}

int32_t word_at(const EvalContext& ctx, uint16_t addr) {
    const uint8_t lo = peek_cpu(ctx, addr);
    const uint8_t hi = peek_cpu(ctx, static_cast<uint16_t>(addr + 1));  // wraps like the Z80
    return lo | (hi << 8);
}

int32_t read_builtin(const Expr& e, const EvalContext& ctx) {
    const Builtin b = e.builtin;
    // Payload names read the delivered event.
    if (b >= Builtin::P_ADDR) {
        if (!ctx.ev) fail(e, "`" + e.text + "` has no event to read");
        const dbg::Event& ev = *ctx.ev;
        switch (b) {
            case Builtin::P_ADDR:     return ev.kind == dbg::EventKind::Execute ? ev.pc : ev.addr;
            case Builtin::P_VALUE:    return ev.value;
            case Builtin::P_PREV:     return ev.prev;
            case Builtin::P_PAGE:     return ev.phys_page;
            case Builtin::P_PORT:     return ev.port;
            case Builtin::P_REG:      return ev.reg;
            case Builtin::P_SOURCE:   return source_code(ev.source);
            case Builtin::P_KEY:      return host_key(ev);
            case Builtin::P_REASON:   fail(e, "`REASON` is a string");
            case Builtin::P_CPC:      return ev.copper_pc;
            case Builtin::P_WAIT_V:   return ev.wait_vpos;
            case Builtin::P_WAIT_H:   return ev.wait_hpos_threshold;
            case Builtin::P_SRC:      return ev.dma_src;
            case Builtin::P_DST:      return ev.dma_dst;
            case Builtin::P_LEN:
                return ev.dma_kind == dbg::DmaEventKind::End ? wrap(uint64_t{ev.dma_bytes})
                                                             : int32_t{ev.dma_length};
            case Builtin::P_DMA_MODE: return ev.dma_mode;
            case Builtin::P_IO_SRC:   return ev.dma_is_io_src ? 1 : 0;
            case Builtin::P_IO_DST:   return ev.dma_is_io_dst ? 1 : 0;
            case Builtin::P_PC:       return ev.pc;
            case Builtin::P_HC_ULA:   return ev.hc_ula;
            case Builtin::P_CVC:      return ev.cvc;
            default: break;
        }
        fail(e, "internal: unbound payload name `" + e.text + "`");
    }

    switch (b) {
        case Builtin::None: break;
        case Builtin::A: return ctx.dbg.registers().AF >> 8;
        case Builtin::F: return ctx.dbg.registers().AF & 0xFF;
        case Builtin::B: return ctx.dbg.registers().BC >> 8;
        case Builtin::C: return ctx.dbg.registers().BC & 0xFF;
        case Builtin::D: return ctx.dbg.registers().DE >> 8;
        case Builtin::E: return ctx.dbg.registers().DE & 0xFF;
        case Builtin::H: return ctx.dbg.registers().HL >> 8;
        case Builtin::L: return ctx.dbg.registers().HL & 0xFF;
        case Builtin::I: return ctx.dbg.registers().I;
        case Builtin::R: return ctx.dbg.registers().R;
        case Builtin::AF:  return ctx.dbg.registers().AF;
        case Builtin::BC:  return ctx.dbg.registers().BC;
        case Builtin::DE:  return ctx.dbg.registers().DE;
        case Builtin::HL:  return ctx.dbg.registers().HL;
        case Builtin::IX:  return ctx.dbg.registers().IX;
        case Builtin::IY:  return ctx.dbg.registers().IY;
        case Builtin::SP:  return ctx.dbg.registers().SP;
        case Builtin::PC:  return ctx.dbg.registers().PC;
        case Builtin::AF2: return ctx.dbg.registers().AF2;
        case Builtin::BC2: return ctx.dbg.registers().BC2;
        case Builtin::DE2: return ctx.dbg.registers().DE2;
        case Builtin::HL2: return ctx.dbg.registers().HL2;
        // Z80 F: S Z - H - P/V N C (bit 7 .. bit 0).
        case Builtin::CF: return flag(ctx.dbg.registers().AF, 0);
        case Builtin::NF: return flag(ctx.dbg.registers().AF, 1);
        case Builtin::PF: return flag(ctx.dbg.registers().AF, 2);
        case Builtin::HF: return flag(ctx.dbg.registers().AF, 4);
        case Builtin::ZF: return flag(ctx.dbg.registers().AF, 6);
        case Builtin::SF: return flag(ctx.dbg.registers().AF, 7);
        case Builtin::IFF1:   return ctx.dbg.registers().IFF1 ? 1 : 0;
        case Builtin::IFF2:   return ctx.dbg.registers().IFF2 ? 1 : 0;
        case Builtin::IM:     return ctx.dbg.registers().IM;
        case Builtin::HALTED: return ctx.dbg.registers().halted ? 1 : 0;
        case Builtin::FRAME:  return wrap(uint64_t{ctx.dbg.time().frame});
        case Builtin::CYCLE:  return wrap(ctx.dbg.time().master_cycle);
        case Builtin::TFRAME: return wrap(ctx.dbg.time().cycle_in_frame);
        case Builtin::RAW_HC: return ctx.dbg.raster().raw_hc;
        case Builtin::RAW_VC: return ctx.dbg.raster().raw_vc;
        case Builtin::HC_ULA: return ctx.dbg.raster().hc_ula;
        case Builtin::VC_ULA: return ctx.dbg.raster().vc_ula;
        case Builtin::CVC:    return ctx.dbg.raster().cvc;
        case Builtin::PHC:    return ctx.dbg.raster().phc;
        case Builtin::AUDIO_MUTE: return ctx.dbg.audio_mute_mask();
        case Builtin::MACHINE:    return machine_code(ctx.dbg.machine().type);
        default: break;
    }
    fail(e, "internal: unbound name `" + e.text + "`");
}

int32_t binary(const Expr& e, const EvalContext& ctx) {
    // Short-circuit forms first: the right operand may not be evaluated.
    if (e.op == Op::And) return (eval_int(*e.a, ctx) != 0 && eval_int(*e.b, ctx) != 0) ? 1 : 0;
    if (e.op == Op::Or)  return (eval_int(*e.a, ctx) != 0 || eval_int(*e.b, ctx) != 0) ? 1 : 0;

    const int32_t  l  = eval_int(*e.a, ctx);
    const int32_t  r  = eval_int(*e.b, ctx);
    const uint32_t ul = static_cast<uint32_t>(l);
    const uint32_t ur = static_cast<uint32_t>(r);
    switch (e.op) {
        case Op::Eq: return l == r;
        case Op::Ne: return l != r;
        case Op::Lt: return l < r;
        case Op::Gt: return l > r;
        case Op::Le: return l <= r;
        case Op::Ge: return l >= r;
        case Op::BitOr:  return wrap(uint64_t{ul | ur});
        case Op::BitXor: return wrap(uint64_t{ul ^ ur});
        case Op::BitAnd: return wrap(uint64_t{ul & ur});
        case Op::Shl: return wrap(uint64_t{ul << (ur & 31u)});
        case Op::Shr:
            // Arithmetic: the sign bit fills. Written without relying on the
            // implementation-defined `>>` of a negative signed value.
            return l >= 0 ? (l >> (ur & 31u))
                          : ~static_cast<int32_t>(~ul >> (ur & 31u));
        case Op::Add: return wrap(uint64_t{ul + ur});
        case Op::Sub: return wrap(uint64_t{ul - ur});
        case Op::Mul: return wrap(uint64_t{ul * ur});
        case Op::Div:
        case Op::Mod:
            if (r == 0) fail(e, e.op == Op::Div ? "division by zero" : "modulo by zero");
            if (l == std::numeric_limits<int32_t>::min() && r == -1)
                return e.op == Op::Div ? l : 0;  // the one quotient that overflows
            return e.op == Op::Div ? l / r : l % r;
        default: break;
    }
    fail(e, "internal: bad binary operator");
}

}  // namespace

int32_t eval_int(const Expr& e, const EvalContext& ctx) {
    switch (e.kind) {
        case ExprKind::Int:
            return e.value;
        case ExprKind::Name:
            return read_builtin(e, ctx);
        case ExprKind::Symbol:
            if (!e.resolved) fail(e, "internal: unresolved symbol `@" + e.text + "`");
            return e.value;
        case ExprKind::Unary: {
            const int32_t v = eval_int(*e.a, ctx);
            switch (e.op) {
                case Op::Neg:    return wrap(uint64_t{0u - static_cast<uint32_t>(v)});
                case Op::BitNot: return ~v;
                case Op::Not:    return v == 0 ? 1 : 0;
                default: break;
            }
            fail(e, "internal: bad unary operator");
        }
        case ExprKind::Binary:
            return binary(e, ctx);
        case ExprKind::Mem:
            return peek_cpu(ctx, static_cast<uint16_t>(cpu_address(e, eval_int(*e.a, ctx), "`mem[]`")));
        case ExprKind::Mem16:
            return word_at(ctx, static_cast<uint16_t>(cpu_address(e, eval_int(*e.a, ctx), "`mem16[]`")));
        case ExprKind::Stack: {
            const int32_t n    = eval_int(*e.a, ctx);
            const int64_t addr = int64_t{ctx.dbg.registers().SP} + 2 * int64_t{n};
            if (addr < 0 || addr > 0xFFFF)
                fail(e, "`stack[]` address " + hex(addr) + " is outside 0..0xFFFF");
            return word_at(ctx, static_cast<uint16_t>(addr));
        }
        case ExprKind::NextReg: {
            const int32_t r = eval_int(*e.a, ctx);
            if (r < 0 || r > 0xFF) fail(e, "`nextreg[]` register " + hex(r) + " is outside 0..0xFF");
            return ctx.dbg.nextreg_peek(static_cast<uint8_t>(r));
        }
        case ExprKind::Mmu:
        case ExprKind::Page: {
            const int32_t s = eval_int(*e.a, ctx);
            if (s < 0 || s > 7)
                fail(e, std::string(e.kind == ExprKind::Mmu ? "`mmu[]`" : "`page[]`") + " slot " +
                            std::to_string(s) + " is outside 0..7");
            const auto slot = ctx.dbg.mmu_slots()[static_cast<size_t>(s)];
            return e.kind == ExprKind::Mmu ? slot.nr_page : slot.effective_page;
        }
        case ExprKind::Phys: {
            const int32_t page = eval_int(*e.a, ctx);
            const int32_t off  = eval_int(*e.b, ctx);
            if (off < 0 || off > 0x1FFF)
                fail(e, "`phys[]` offset " + hex(off) + " is outside 0..0x1FFF");
            if (page < 0 || page > 0xFFFF)
                fail(e, "`phys[]` page " + hex(page) + " is not a page number");
            uint8_t b = 0;
            const auto r = ctx.dbg.peek(dbg::MemSpace::page(static_cast<uint16_t>(page)),
                                        static_cast<uint32_t>(off), 1, &b);
            if (!r)
                fail(e, "`phys[]` page " + hex(page) + " cannot be read (" +
                            dbg::result_name(r.status) + ")");
            return b;
        }
        case ExprKind::Str:
        case ExprKind::Var:
        case ExprKind::SnapField:
        case ExprKind::Changed:
        case ExprKind::Depth:
            break;
    }
    fail(e, "internal: this expression needs a script context");
}

}  // namespace script
}  // namespace jnext
