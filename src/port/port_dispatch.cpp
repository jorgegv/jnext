#include "port_dispatch.h"
#include "core/log.h"

PortDispatch::PortDispatch() {
    // All port handlers are registered by Emulator::init() where real
    // subsystem instances are available. The constructor no longer
    // pre-registers stubs — those collided with the real handlers
    // (VHDL one-hot violation, item 15.11).
}

void PortDispatch::register_handler(uint16_t mask, uint16_t value,
    std::function<uint8_t(uint16_t)> rd,
    std::function<void(uint16_t, uint8_t)> wr) {
    handlers_.push_back({mask, value, std::move(rd), std::move(wr)});
}

uint8_t PortDispatch::read(uint16_t port) const {
    for (const auto& h : handlers_) {
        if ((port & h.mask) == h.value && h.read) {
            uint8_t val = h.read(port);
            Log::port()->trace("IN  port={:#06x} → {:#04x}", port, val);
            return val;
        }
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
    for (const auto& h : handlers_) {
        if ((port & h.mask) == h.value && h.write) {
            h.write(port, val);
        }
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
