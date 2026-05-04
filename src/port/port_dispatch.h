#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "cpu/z80_cpu.h"

struct PortHandler {
    uint16_t mask;
    uint16_t value;
    std::function<uint8_t(uint16_t port)>            read;
    std::function<void(uint16_t port, uint8_t val)>  write;
};

// ZX Spectrum I/O port dispatcher using address-line mask/value matching.
// ZX ports are decoded by address line masking, not full 16-bit compare.
class PortDispatch : public IoInterface {
public:
    PortDispatch();

    /// Remove all registered handlers (used before re-registering on reset).
    void clear_handlers() { handlers_.clear(); }

    void register_handler(uint16_t mask, uint16_t value,
                          std::function<uint8_t(uint16_t)>           rd,
                          std::function<void(uint16_t, uint8_t)>     wr);

    uint8_t read(uint16_t port) const;
    void    write(uint16_t port, uint8_t val);

    /// Set a default read callback for unmatched ports (e.g. floating bus).
    /// If not set, unmatched reads return 0xFF.
    void set_default_read(std::function<uint8_t(uint16_t)> cb) { default_read_ = std::move(cb); }

    /// IO observers — invoked unconditionally on every IN/OUT BEFORE the
    /// regular handler dispatch, regardless of whether a handler matches.
    /// Used by peripherals whose VHDL decode runs in parallel with the
    /// rest of the port matrix and produces side effects (not a returned
    /// value), e.g. the Multiface's `port_mf_enable / port_mf_disable`
    /// strobes (zxnext.vhd:2615-2616): the same port LSB also feeds
    /// Kempston joystick / DAC handlers that win the most-specific-wins
    /// dispatch, but the MF state machine must still see the strobe.
    /// Multiple observers may be installed; they run in registration
    /// order. Observer return values are ignored.
    void add_io_observer(std::function<void(uint16_t /*port*/, bool /*is_read*/)> cb) {
        io_observers_.push_back(std::move(cb));
    }
    void clear_io_observers() { io_observers_.clear(); }

    // IoInterface implementation
    uint8_t in(uint16_t port) override;
    void    out(uint16_t port, uint8_t val) override;

    /// RZX playback: if set, all IN reads return values from this callback
    /// instead of normal port dispatch.
    std::function<uint8_t(uint16_t)> rzx_in_override;

    /// RZX recording: if set, called after each IN with the returned value.
    std::function<void(uint8_t)> rzx_in_record;

private:
    std::vector<PortHandler> handlers_;
    std::function<uint8_t(uint16_t)> default_read_;
    // Observers carry side-effect-only callbacks (peripheral state mutation
    // via captured refs); read() is `const` for the public API but observer
    // calls necessarily mutate. Marking the vector mutable keeps the
    // existing const contract for the IoInterface read().
    mutable std::vector<std::function<void(uint16_t, bool)>> io_observers_;
};
