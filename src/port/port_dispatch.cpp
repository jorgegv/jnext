#include "port_dispatch.h"
#include "core/log.h"
#include "debug/debug_state.h"

PortDispatch::PortDispatch() {
    // All port handlers are registered by Emulator::init() which calls
    // clear_handlers() first. No constructor stubs needed.
}

void PortDispatch::register_handler(uint16_t mask, uint16_t value,
    std::function<uint8_t(uint16_t)> rd,
    std::function<void(uint16_t, uint8_t)> wr) {
    handlers_.push_back({mask, value, std::move(rd), std::move(wr)});
}

// I/O watchpoints (GH #222) — the port-side twin of the Mmu's memory
// watchpoint sites, raising the SAME latch so the user gets the same
// behaviour: the emulator's hot loop consumes data_bp_hit() after the
// instruction that made the access has completed, and pauses there.
//
// Placed in read()/write() rather than in()/out(): those two are the ONE
// choke point every bus access reaches. `in`/`out` are only the CPU's
// IN/OUT; the DMA's own port transfers call read()/write() directly
// (emulator.cpp's dma_.read_io / dma_.write_io), and so does a handler that
// nests a port write. Watching a port and missing the DMA's accesses to it
// would be the wrong answer for a debugger.
void PortDispatch::check_io_watchpoint_(uint16_t port, WatchType type) const {
    if (!debug_state_ || !debug_state_->armed()) return;
    if (!debug_state_->breakpoints().has_any_watchpoints()) return;
    if (!debug_state_->breakpoints().has_io_watchpoint(port, type)) return;
    debug_state_->set_data_bp_hit(true);
    debug_state_->set_data_bp_addr(port);
}

// Count set bits in a 16-bit mask (handler specificity).
static int mask_specificity(uint16_t mask) {
    int n = 0;
    while (mask) { n += mask & 1; mask >>= 1; }
    return n;
}

// Most-specific-match-wins dispatch.
//
// VHDL zxnext.vhd uses exclusive one-hot decode: each port address
// maps to exactly one handler signal. When the emulator has overlapping
// mask/value ranges (e.g. 128K 0x8002/0x0000 and +3 0xF002/0x1000 both
// match 0x1FFD), the handler with the most bits in its mask is the most
// constrained decode and wins — matching the VHDL behaviour where the
// tighter equation takes priority.
//
// For reads: if the best handler has no read callback, fall through to
// the next most-specific handler that does (this models write-only ports
// like Pentagon 0xDFFD where reads go to the AY handler per VHDL 2771).

uint8_t PortDispatch::read(uint16_t port) const {
    check_io_watchpoint_(port, WatchType::IO_READ);

    // IO observers run unconditionally before dispatch (Wave 1 B2 — MF
    // port-strobe). Observers are side-effect-only; their return values
    // never affect the dispatched read value.
    for (const auto& obs : io_observers_) obs(port, /*is_read=*/true);

    // Collect all matching handlers sorted by specificity (most bits first).
    // In practice there are at most 2-3 matches; a simple scan is fine.
    const PortHandler* best = nullptr;
    int best_bits = -1;
    for (const auto& h : handlers_) {
        if ((port & h.mask) == h.value) {
            int bits = mask_specificity(h.mask);
            if (bits > best_bits && h.read) {
                best = &h;
                best_bits = bits;
            }
        }
    }
    if (best) {
        uint8_t val = best->read(port);
        Log::port()->trace("IN  port={:#06x} → {:#04x}", port, val);
        return val;
    }
    if (default_read_) {
        uint8_t val = default_read_(port);
        Log::port()->trace("IN  port={:#06x} → {:#04x} (default/floating)", port, val);
        return val;
    }
    Log::port()->trace("IN  port={:#06x} → 0xFF (unhandled)", port);
    return 0xFF;
}

void PortDispatch::write(uint16_t port, uint8_t val) {
    check_io_watchpoint_(port, WatchType::IO_WRITE);

    Log::port()->trace("OUT port={:#06x} ← {:#04x}", port, val);
    for (const auto& obs : io_observers_) obs(port, /*is_read=*/false);
    // Most-specific-match-wins, with decline fall-through: a handler whose
    // hardware decode gate (io_en) is off may call decline_write() to have
    // dispatch retry with the next-most-specific matching handler. This
    // mirrors the VHDL parallel decodes, where a decode ANDed with a cleared
    // internal_port_enable bit drops out and an overlapping decode fires
    // instead (see decline_write() in port_dispatch.h; G146).
    //
    // Handler order: descending mask specificity; among equal specificity,
    // registration order (same tie-break as the single-winner scan used
    // before decline support: strict `bits > best_bits` keeps the earliest).
    int prev_bits = 17;      // sentinel above any 16-bit mask specificity
    size_t prev_idx = 0;
    for (;;) {
        const PortHandler* best = nullptr;
        int best_bits = -1;
        size_t best_idx = 0;
        for (size_t i = 0; i < handlers_.size(); ++i) {
            const auto& h = handlers_[i];
            if ((port & h.mask) != h.value || !h.write) continue;
            const int bits = mask_specificity(h.mask);
            // Skip handlers already tried (ordered at or before the previous
            // pick in the descending-specificity / registration order).
            if (bits > prev_bits || (bits == prev_bits && i <= prev_idx)) continue;
            if (bits > best_bits) {
                best = &h;
                best_bits = bits;
                best_idx = i;
            }
        }
        if (!best) {
            // No (further) handler — write dropped. Reset the flag so the
            // invariant "write_declined_ is false whenever write() returns"
            // holds even on this path: a handler may legally nest a full
            // port write (e.g. a DMA burst triggered from an OUT, via
            // Emulator's dma_.write_io -> port_.write), and a leaked TRUE
            // from the nested dispatch would make the outer loop believe
            // ITS handler had declined.
            write_declined_ = false;
            return;
        }
        write_declined_ = false;
        // GH #230: copy the callable out of the vector before invoking it, for
        // the same reason NextReg::write does — a port write that reaches
        // NR 0x02 bit 0 re-enters Emulator::init() through soft_reset(), and
        // init() calls clear_handlers() here, destroying `handlers_` and with
        // it the std::function currently executing.
        //
        // The route is narrow but real: the CPU's own OUT ($253B) cannot get
        // here synchronously, because Emulator holds defer_cpu_nr_writes_ for
        // the whole of cpu_.execute() and the port handler only queues the
        // write. What remains is a DMA burst with an I/O destination of port
        // 0x253B (Dma::execute_burst -> dma_.write_io -> here), which runs
        // outside that window.
        //
        // `best` itself is not dereferenced after the call — best_bits /
        // best_idx are already copies — so the callable is the only thing
        // needing to outlive the invocation. The copy allocates nothing for
        // any handler registered today: all but one capture `this` alone,
        // and the exception — the magic-port writer at emulator.cpp:5822 —
        // captures a lone `MagicPortMode`, an `enum class : uint8_t`. Both
        // are trivially copyable and well inside libstdc++'s 16-byte
        // small-object buffer, so neither leaves it.
        const auto handler = best->write;
        handler(port, val);
        if (!write_declined_) return;   // handled
        prev_bits = best_bits;
        prev_idx = best_idx;
    }
}

// IoInterface implementation
uint8_t PortDispatch::in(uint16_t port) {
    // RZX playback: override all IN reads with recorded values.
    //
    // The only bus access that does NOT reach read(), so it carries its own
    // watchpoint check (GH #222). The CPU really did execute `IN A,(n)`; only
    // the VALUE comes from the recording, and a user single-stepping a replay
    // still expects a watched port to stop the machine.
    if (rzx_in_override) {
        check_io_watchpoint_(port, WatchType::IO_READ);
        return rzx_in_override(port);
    }

    uint8_t val = read(port);

    // RZX recording: capture every IN value.
    if (rzx_in_record) rzx_in_record(val);

    return val;
}

void PortDispatch::out(uint16_t port, uint8_t val) {
    write(port, val);
}
