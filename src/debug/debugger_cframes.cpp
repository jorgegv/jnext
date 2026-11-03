// CAP-SRC (GH #434, epic #353): the C frames at the stop. See the developer guide, 7.10.
//
// A frame's BASE `fp` is the address of the saved frame pointer: `E - 2`, where E is the SP at the
// function's entry (the address of the return address). The tracker supplies it exactly: the CALL
// that entered a frame's code left `sp_at_call == E`. Without tracker frames the entry bytes of
// frame 0 and the saved frame pointers of the older frames supply it.

#include "debug/debugger.h"
#include "debug/debugger_impl.h"
#include "debug/variable_store.h"

#include <cstdio>

namespace jnext {
namespace dbg {

namespace {

enum class Style : uint8_t { None, Push, Ix, Other };

std::string hex4(unsigned v)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04X", v & 0xFFFFu);
    return buf;
}

}  // namespace

std::vector<Debugger::CFrame> Debugger::c_frames() const
{
    impl_->recheck();
    std::vector<CFrame> out;
    const SourceMap* map = impl_->live_sources();
    const auto store = map ? map->variables() : nullptr;
    if (!store) return out;

    const auto& tracked = impl_->emu.call_stack().frames();   // oldest first
    const size_t n = tracked.size();
    const Z80Registers regs = registers();
    const CFrameHelpers& helpers = store->helpers();

    const auto cpu_byte = [&](uint16_t addr, uint8_t* b) {
        const auto r = peek(MemSpace::cpu(), addr, 1, b);
        return r.status == Result::Ok && r.value == 1;
    };
    const auto cpu_word = [&](uint16_t addr, uint16_t* w) {
        uint8_t b[2] = {0, 0};
        const auto r = peek(MemSpace::cpu(), addr, 2, b);
        if (r.status != Result::Ok || r.value != 2) return false;
        *w = static_cast<uint16_t>(b[0] | (b[1] << 8));
        return true;
    };
    // `n` bytes of a function's entry, through its own space.
    const auto entry_bytes = [&](const CFunction& f, uint8_t* b, size_t len) {
        if (!f.entry.resolved) return false;
        const auto r = peek(f.entry.at.space, space_offset(f.entry.at.space, f.entry.at.address), len, b);
        return r.status == Result::Ok && r.value == len;
    };
    const auto style_of = [&](int function, std::string* why) {
        if (function < 0) return Style::None;
        uint8_t b[8] = {};
        const CFunction& f = store->functions()[static_cast<size_t>(function)];
        if (!entry_bytes(f, b, 8)) {
            if (why) *why = "the entry bytes of " + f.name + " cannot be read";
            return Style::Other;
        }
        if (b[0] == 0xCD && helpers.push_frame.resolved &&
            (b[1] | (b[2] << 8)) == helpers.push_frame.at.address)
            return Style::Push;
        static const uint8_t ix[8] = {0xDD, 0xE5, 0xDD, 0x21, 0x00, 0x00, 0xDD, 0x39};
        bool is_ix = true;
        for (int i = 0; i < 8; ++i) is_ix = is_ix && b[i] == ix[i];
        if (is_ix) return Style::Ix;
        if (why) {
            *why = "the entry of " + f.name + " is neither a call to l_debug_push_frame nor push ix; ld ix,0; add ix,sp (";
            for (int i = 0; i < 8; ++i) *why += (i ? " " : "") + hex4(b[i]).substr(2);
            *why += ")";
        }
        return Style::Other;
    };

    // The epilogue window of a function (GH #434 review F1): after the instruction that restores the caller's
    // frame pointer (`call l_debug_pop_frame`, or `pop ix`) and up to the function's `ret`, the variable or IX
    // already hold the CALLER's base, and a `__z88dk_callee` routine runs more instructions (pops of its
    // arguments) before it returns. Found by walking the function's code from its entry to `pc`: true when the
    // newest pop seen has no `ret` after it; `start` is the address right after that pop, where SP is still E.
    const auto in_epilogue = [&](const CFunction& fn, uint16_t pc, bool ix, uint16_t* start) {
        if (!fn.entry.resolved) return false;
        uint16_t a = fn.entry.at.address;
        uint16_t pop_end = 0;
        bool open = false;
        for (int i = 0; i < 4096 && a < pc; ++i) {
            uint8_t b[3] = {};
            if (!cpu_byte(a, &b[0]) || !cpu_byte(static_cast<uint16_t>(a + 1), &b[1]) ||
                !cpu_byte(static_cast<uint16_t>(a + 2), &b[2]))
                return false;
            const int len = instruction_length(a);
            if (len <= 0) return false;
            if (ix ? (b[0] == 0xDD && b[1] == 0xE1)
                   : (b[0] == 0xCD && helpers.pop_frame.resolved &&
                      (b[1] | (b[2] << 8)) == helpers.pop_frame.at.address)) {
                open = true;
                pop_end = static_cast<uint16_t>(a + len);
            } else if (b[0] == 0xC9) {
                open = false;
            }
            a = static_cast<uint16_t>(a + len);
        }
        if (a != pc || !open) return false;
        *start = pop_end;
        return true;
    };

    const auto describe = [&](CFrame& f) {
        const auto loc = store->locate(f.pc);
        f.function = loc ? loc->function : -1;
        if (const SourceLocation* l = map->lookup(f.pc.space, f.pc.address)) f.line = *l;
        return loc;
    };

    bool prev_base = false;
    uint16_t prev_fp = 0;
    Style prev_style = Style::None;

    for (size_t k = 0; k <= CallStack::MAX_DEPTH; ++k) {
        CFrame f;
        // The PC of the frame.
        if (k == 0) {
            f.pc = {source_page(regs.PC), regs.PC};
        } else if (k <= n) {
            const CallFrame& cf = tracked[n - k];
            const bool recorded = !(cf.caller_space == MemSpace::cpu());
            f.pc = {recorded ? cf.caller_space : source_page(cf.caller_pc), cf.caller_pc};
            if (!recorded) f.note = "caller_space not recorded; the page mapped now is used";
        } else {
            uint16_t ret = 0;
            if (!prev_base || !cpu_word(static_cast<uint16_t>(prev_fp + 2), &ret)) break;
            const uint16_t at = static_cast<uint16_t>(ret - 1);
            f.pc = {source_page(at), at};
        }
        const auto loc = describe(f);
        std::string style_why;
        const Style style = style_of(f.function, &style_why);

        if (k < n) {
            // Entered by tracked[n - 1 - k]: its CALL pushed the return address at sp_at_call.
            f.how = CFrame::Base::Tracked;
            f.fp = static_cast<uint16_t>(tracked[n - 1 - k].sp_at_call - 2);
            f.floor = k == 0 ? regs.SP : static_cast<uint16_t>(tracked[n - k].sp_at_call + 2);
        } else if (k == 0) {
            f.floor = regs.SP;
            if (f.function < 0) {
                f.note = "not in a C function, and no tracked frame";
            } else if (style == Style::Push) {
                f.how = CFrame::Base::Prologue;
                uint16_t win = 0;
                const bool epi = in_epilogue(store->functions()[static_cast<size_t>(f.function)], regs.PC, false, &win);
                if (loc->offset == 0 || (epi && win == regs.PC)) {
                    f.fp = static_cast<uint16_t>(regs.SP - 2);
                } else if (epi) {
                    f.how = CFrame::Base::Unknown;
                    f.note = "in the epilogue after the frame pointer was restored: the base is not known without the call-stack tracker";
                } else if (helpers.framepointer.resolved) {
                    const auto& at = helpers.framepointer.at;
                    uint8_t w[2] = {};
                    const auto r = peek(at.space, space_offset(at.space, at.address), 2, w);
                    if (r.status == Result::Ok && r.value == 2) {
                        f.fp = static_cast<uint16_t>(w[0] | (w[1] << 8));
                    } else {
                        f.how = CFrame::Base::Unknown;
                        f.note = "__debug_framepointer cannot be read";
                    }
                } else {
                    f.how = CFrame::Base::Unknown;
                    f.note = "__debug_framepointer: " + helpers.framepointer.why;
                }
            } else if (style == Style::Ix) {
                f.how = CFrame::Base::Prologue;
                uint16_t win = 0;
                const bool epi = in_epilogue(store->functions()[static_cast<size_t>(f.function)], regs.PC, true, &win);
                if (loc->offset == 0 || (epi && win == regs.PC)) f.fp = static_cast<uint16_t>(regs.SP - 2);
                else if (epi) {
                    f.how = CFrame::Base::Unknown;
                    f.note = "in the epilogue after IX was restored: the base is not known without the call-stack tracker";
                } else if (loc->offset == 2 || loc->offset == 6) f.fp = regs.SP;
                else f.fp = regs.IX;
            } else {
                f.note = style_why;
            }
        } else if (prev_base) {
            // Through the chain: [prev_fp] is the caller's frame pointer, but only when the caller
            // keeps its frame the same way.
            if (f.function < 0) {
                f.note = "not in a C function";
            } else if (style == Style::Other) {
                f.note = style_why.empty() ? "this function keeps no saved frame pointer" : style_why;
            } else if (style != prev_style) {
                f.note = "the caller keeps its frame differently (80cc mixes IX and l_debug_push_frame "
                         "frames); its base is not known without the call-stack tracker";
            } else {
                uint16_t next = 0;
                if (cpu_word(prev_fp, &next) && next > static_cast<uint16_t>(prev_fp + 3) &&
                    next < 0xFFFC && prev_fp < next && source_page(next).kind == MemSpace::Kind::Page) {
                    f.how = CFrame::Base::Chain;
                    f.fp = next;
                    f.floor = static_cast<uint16_t>(prev_fp + 4);
                } else {
                    f.note = "the saved frame pointer is not a stack address above this frame";
                }
            }
        }

        out.push_back(f);
        if (f.how == CFrame::Base::Unknown && k >= n) break;   // nothing further can be derived
        prev_base = f.how != CFrame::Base::Unknown;
        prev_fp = f.fp;
        prev_style = style;
    }
    return out;
}

}  // namespace dbg
}  // namespace jnext
