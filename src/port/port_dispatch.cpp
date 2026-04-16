#include "port_dispatch.h"
#include "core/log.h"

PortDispatch::PortDispatch() {
    // All port handlers are registered by Emulator::init() which calls
    // clear_handlers() first. No constructor stubs needed.
}

void PortDispatch::register_handler(uint16_t mask, uint16_t value,
    std::function<uint8_t(uint16_t)> rd,
    std::function<void(uint16_t, uint8_t)> wr) {
    handlers_.push_back({mask, value, std::move(rd), std::move(wr)});
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
    Log::port()->trace("OUT port={:#06x} ← {:#04x}", port, val);
    const PortHandler* best = nullptr;
    int best_bits = -1;
    for (const auto& h : handlers_) {
        if ((port & h.mask) == h.value && h.write) {
            int bits = mask_specificity(h.mask);
            if (bits > best_bits) {
                best = &h;
                best_bits = bits;
            }
        }
    }
    if (best) {
        best->write(port, val);
    }
}

// IoInterface implementation
uint8_t PortDispatch::in(uint16_t port) {
    // RZX playback: override all IN reads with recorded values.
    if (rzx_in_override) return rzx_in_override(port);

    uint8_t val = read(port);

    // RZX recording: capture every IN value.
    if (rzx_in_record) rzx_in_record(val);

    return val;
}

void PortDispatch::out(uint16_t port, uint8_t val) {
    write(port, val);
}
