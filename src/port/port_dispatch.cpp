#include "port_dispatch.h"
#include "core/log.h"

PortDispatch::PortDispatch() {
    // Legacy stubs — registered before Emulator::init() wires real handlers.
    // For reads, the real handler (registered later) is shadowed by these
    // first-match stubs. For writes, both fire (broadcast).
    // TODO: remove once init() calls clear_handlers() before re-registering.

    // AY data: 0xBFFD (mask 0xC002, value 0xC000)
    register_handler(0xC002, 0xC000,
        [](uint16_t p) -> uint8_t { return 0xFF; },
        [](uint16_t p, uint8_t v) {});

    // AY register select: 0xFFFD (mask 0xC002, value 0x8000)
    register_handler(0xC002, 0x8000,
        [](uint16_t p) -> uint8_t { return 0xFF; },
        [](uint16_t p, uint8_t v) {});

    // 128K bank: 0x7FFD (mask 0xE002, value 0x0000)
    register_handler(0xE002, 0x0000,
        [](uint16_t p) -> uint8_t { return 0xFF; },
        [](uint16_t p, uint8_t v) {});

    // NextREG family: 0x243B / 0x253B (mask 0x00FF, value 0x003B)
    register_handler(0x00FF, 0x003B,
        [](uint16_t p) -> uint8_t { return 0xFF; },
        [](uint16_t p, uint8_t v) {});
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
