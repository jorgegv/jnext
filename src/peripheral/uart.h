#pragma once
#include <cstdint>
#include <array>
#include <functional>
#include <type_traits>
#include "core/saveable.h"
#include "peripheral/uart_device.h"

/// UART peripheral — dual-channel UART (ESP WiFi + Raspberry Pi).
///
/// The ZX Next has two independent UARTs sharing four I/O ports:
///   0x153B  UART Select  (R/W) — selects channel + prescaler MSB
///   0x163B  UART Frame   (R/W) — frame format configuration
///   0x133B  UART Tx      (W=send byte, R=status)
///   0x143B  UART Rx      (R=receive byte, W=prescaler LSB)
///
/// Both UARTs have a 512-byte RX FIFO and a 64-byte TX FIFO.
///
/// Prescaler is 17 bits: 3 MSBs from select port, 14 LSBs from Rx port writes.
/// Baud rate = Fsys / prescaler (Fsys typically 28 MHz).
///
/// Default: 115200 baud, 8N1 (framing = 0x18, prescaler = 243).
///
/// VHDL reference: serial/uart.vhd, serial/uart_tx.vhd, serial/uart_rx.vhd

/// Circular buffer used for TX and RX FIFOs.
///
/// Phase-1 (Task 3 UART+I2C plan) widened element type parameter so the RX FIFO
/// can hold uint16_t entries whose bit 8 carries the VHDL 9th-bit
/// `(overflow OR framing)` flag per-byte per uart.vhd:359. TX FIFO stays on
/// uint8_t — no 9th bit is modelled on TX.
template <typename T, std::size_t Capacity>
class FifoBuffer {
public:
    static_assert(std::is_integral<T>::value, "FifoBuffer<T,N> element must be integral");

    void reset() { head_ = 0; tail_ = 0; count_ = 0; }

    bool empty()      const { return count_ == 0; }
    bool full()       const { return count_ >= Capacity; }
    bool near_full()  const { return count_ >= (Capacity * 3 / 4); }
    bool almost_full()const { return count_ >= (Capacity - 2); }
    std::size_t size() const { return count_; }

    // Issue #42 — the count is written first, then ALWAYS `Capacity`
    // elements, oldest-first. Writing only `count_` elements made the
    // snapshot's width depend on how much UART traffic was in flight, and
    // RewindBuffer requires every snapshot to be exactly the size it
    // measured at construction — so a single received byte silently
    // invalidated every subsequent rewind snapshot. Entries past `count_`
    // are padding; load_state pushes only the first `count_`.
    void save_state(class StateWriter& w) const {
        w.write_u64(count_);
        for (std::size_t i = 0; i < Capacity; ++i) {
            T val = (i < count_) ? buf_[(tail_ + i) % Capacity] : T{0};
            write_elem(w, val);
        }
    }
    void load_state(class StateReader& r) {
        reset();
        const std::size_t n = static_cast<std::size_t>(r.read_u64());
        // Clamp: the stream carries exactly Capacity elements whatever the
        // count claims, so a corrupt count can neither desync the stream
        // nor push past the ring.
        const std::size_t live = (n <= Capacity) ? n : Capacity;
        for (std::size_t i = 0; i < Capacity; ++i) {
            T val = read_elem(r);
            if (i < live) push(val);
        }
    }

    /// Push a value.  Returns false if full (value is dropped).
    bool push(T val) {
        if (full()) return false;
        buf_[head_] = val;
        head_ = (head_ + 1) % Capacity;
        ++count_;
        return true;
    }

    /// Pop a value.  Returns 0 if empty.
    T pop() {
        if (empty()) return T{0};
        T val = buf_[tail_];
        tail_ = (tail_ + 1) % Capacity;
        --count_;
        return val;
    }

    /// Peek at front without removing.
    T front() const {
        if (empty()) return T{0};
        return buf_[tail_];
    }

private:
    // Element width-specific serialisation — keeps uint8_t/uint16_t
    // rewind-buffer round-trip correct without needing a schema version
    // (the rewind buffer is in-process only, no on-disk compat required).
    static void write_elem(StateWriter& w, uint8_t v)  { w.write_u8(v); }
    static void write_elem(StateWriter& w, uint16_t v) { w.write_u16(v); }
    static uint8_t  read_elem_impl(StateReader& r, uint8_t)  { return r.read_u8(); }
    static uint16_t read_elem_impl(StateReader& r, uint16_t) { return r.read_u16(); }
    static T read_elem(StateReader& r) { return read_elem_impl(r, T{}); }

    std::array<T, Capacity> buf_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
};

/// Single UART channel — models TX/RX with FIFOs and baud-rate timing.
///
/// In emulation, we offer two time bases:
///
///   * byte-level (default, opt-out): `tick(master_cycles)` treats the entire
///     frame as a single event; the byte leaves TX and lands in RX (or in
///     `on_tx_byte`) after `prescaler * frame_bits` 28 MHz ticks. This is the
///     historical path; all pre-Phase-1 tests and any non-test consumers
///     (loopback / TCP bridge) use it.
///
///   * bit-level (opt-in, via `set_bitlevel_mode(true)`): `tick_one_bit_clock()`
///     advances a faithful model of `serial/uart_tx.vhd` / `serial/uart_rx.vhd`.
///     Enables tests that observe TX line state, CTS flow-control, framing /
///     parity errors, break detection, noise rejection and `S_PAUSE` entry.
///     Phase-1 scaffold — lands the state machines; Wave-A/B/E tests flip the
///     mode on per-row. Phase-1 MUST preserve existing byte-level behaviour.
class UartChannel {
public:
    static constexpr std::size_t RX_FIFO_SIZE = 512;
    static constexpr std::size_t TX_FIFO_SIZE = 64;

    UartChannel() = default;

    void reset();
    void hard_reset();

    /// Advance the channel by the given number of 28 MHz master ticks.
    /// Handles baud-rate timing for byte transfer from TX FIFO.
    void tick(uint32_t master_cycles);

    // ── CPU-facing interface ──────────────────────────────────

    /// Write a byte to the TX FIFO (port 0x133B write).
    void write_tx(uint8_t val);

    /// Read a byte from the RX FIFO (port 0x143B read).
    uint8_t read_rx();

    /// Read the status register (port 0x133B read).
    uint8_t read_status() const;

    /// Read the RX FIFO error bit (9th bit from VHDL BRAM word).
    bool read_rx_err_bit() const;

    /// Write prescaler LSB (port 0x143B write).
    /// bit 7=0: sets lower 7 bits; bit 7=1: sets upper 7 bits.
    void write_prescaler_lsb(uint8_t val);

    /// Write the prescaler MSB (3 bits, from select port bits 2:0).
    void write_prescaler_msb(uint8_t val);

    /// Read the prescaler MSB (3 bits).
    uint8_t read_prescaler_msb() const { return prescaler_msb_; }

    /// Write the framing register (port 0x163B write).
    void write_frame(uint8_t val);

    /// Read the framing register.
    uint8_t read_frame() const { return framing_; }

    /// Clear sticky error flags (called on status read = TX port read).
    void clear_errors();

    /// Hardware flow control enabled?
    bool hw_flow_control() const { return (framing_ & 0x20) != 0; }

    // ── External injection (for TCP bridge / loopback) ────────

    /// Push a byte into the RX FIFO from an external source.
    /// Phase-1 widening: bit 8 of the stored element is `(overflow OR framing)`
    /// per uart.vhd:359. Byte-level injection always records bit 8 = 0.
    void inject_rx(uint8_t byte);

    // ── Bit-level engine surface (Phase-1 scaffold, opt-in) ───
    //
    // Default `bitlevel_mode_ = false` — byte-level `tick()` path is active
    // and `tick_one_bit_clock()` / bit-level engines are dormant. Tests opt
    // in per-row via `set_bitlevel_mode(true)`; production code does not.

    /// Toggle the bit-level engine on/off. Default false.
    void set_bitlevel_mode(bool en) { bitlevel_mode_ = en; }
    bool bitlevel_mode() const { return bitlevel_mode_; }

    /// Drive the TX CTS# input (per uart_tx.vhd:180, `i_cts_n`).
    /// Only observed when `framing_ & 0x20` (flow_ctrl_en) is set and
    /// `bitlevel_mode_ == true`.
    void set_cts_n(bool v) { cts_n_ = v; }
    bool cts_n() const { return cts_n_; }

    /// Drive the RX line input (per uart_rx.vhd:86, `i_Rx`). Idle = 1.
    /// Feeds through the noise-rejection debouncer per uart_rx.vhd:119-131.
    void drive_rx_line(bool v) { rx_raw_ = v; }

    /// One CLK_28 rising edge for the bit-level TX + RX engines. No effect
    /// when `bitlevel_mode_ == false`.
    void tick_one_bit_clock();

    /// Combinational TX output (per uart_tx.vhd:236-245 / line 239 break
    /// output). Only meaningful while `bitlevel_mode_ == true`.
    bool tx_line_out() const { return tx_line_out_; }

    /// Combinational TX busy flag (per uart_tx.vhd:234). Held while not
    /// S_IDLE, or while `framing(6)` break, or while `framing(7)` reset.
    /// In byte-level mode returns the legacy `tx_busy_` flag (idle=false,
    /// mid-byte-transmission=true) so non-bit-level tests still observe a
    /// consistent busy signal.
    bool tx_busy() const {
        return bitlevel_mode_ ? tx_busy_bitlevel_ : tx_busy_;
    }

    /// Direct access to the bit-level o_busy signal (no byte-level fallback).
    /// Useful for Wave-A tests that want to assert the VHDL o_busy bit
    /// regardless of the legacy byte-level path.
    bool tx_busy_bitlevel() const { return tx_busy_bitlevel_; }

    /// Combinational RX RTR# pin per uart.vhd:442-446:
    /// `o_Rx_rtr_n = framing(5) AND rx_fifo_almost_full`.
    bool rx_rtr_n() const;

    /// Combinational break-detect per uart_rx.vhd:314:
    /// `o_err_break = '1' when state = S_ERROR and rx_shift = 0x00`.
    bool rx_break() const;

    /// Push one `byte` into the RX FIFO with explicit framing/parity-error
    /// flags set as if the bit-level RX engine had seen them.
    /// `framing_err` sets the 9th-bit flag (mirrors uart.vhd:359).
    /// `parity_err` sets the sticky parity-error status bit.
    void inject_rx_bit_frame(uint8_t byte, bool framing_err, bool parity_err);

    // ── Device attachment ─────────────────────────────────────
    //
    // The backend on the far end of this channel's wire (ESP-01 on UART 0,
    // Raspberry Pi on UART 1). Non-owning — see uart_device.h for the
    // lifetime contract. Prefer the `Uart::attach_device(channel, dev)`
    // facade, which also hands the device its guest-bound RX sink; these
    // per-channel entry points only manage the pointer.

    void attach_device(UartDevice* device) { device_ = device; }
    void detach_device()                   { device_ = nullptr; }
    UartDevice* device() const             { return device_; }

    /// Declare how to find out whether this channel's module-facing pin pair is
    /// disconnected because the joystick-connector UART mux has taken the
    /// channel over (GH #251).
    ///
    /// VHDL zxnext.vhd:3340-3346, for `joy_iomode_uart_en='1'` and the channel
    /// `nr_0b_joy_iomode_0` selects:
    ///
    ///   uart0_rx     <= joy_uart_rx;   -- i_UART0_RX is NOT selected
    ///   uart0_tx_esp <= '1';           -- the module-facing TX pin idles
    ///   esp_uart_rtr_n <= '1';         -- and its RTR# is de-asserted
    ///
    /// so the attached backend can neither be heard nor be spoken to: the
    /// channel's serial traffic goes out of joystick pin 7 instead
    /// (zxnext.vhd:3526-3531). Both halves are enforced — RX in the sink
    /// `Uart::attach_device` installs, TX in `deliver_tx_byte` — because
    /// enforcing only one is worse than enforcing neither: a guest transmitting
    /// over the cable would have its bytes looped back into the very RX FIFO
    /// the cable is feeding.
    ///
    /// A PROBE, PULLED, not a flag pushed from the tick loop. Both consumers run
    /// at BYTE boundaries (≤230/frame at 115200), not per instruction, so an
    /// indirect call costs nothing measurable — and the value is then always the
    /// current NR 0x0B rather than one instruction stale, which a mid-frame
    /// write would otherwise make it. Not serialised: it is mux topology like
    /// `device_` itself, and it reads NR 0x0B, which IS serialised (in IoMode).
    void set_device_isolated_probe(std::function<bool()> probe) {
        device_isolated_probe_ = std::move(probe);
    }
    bool device_isolated() const {
        return device_isolated_probe_ && device_isolated_probe_();
    }

    /// 28 MHz ticks for ONE complete frame at the current framing and
    /// prescaler. Public because the joystick-connector serial source
    /// (`JoyUartSource`, GH #251) paces off the RECEIVING channel's baud, and
    /// it is not a `UartDevice`, so it cannot be served through
    /// `service_attached_device` below.
    uint32_t byte_transfer_ticks() const;

    /// Emulated-time service for the attached backend (GH #25 branch 3).
    ///
    /// Called from `Uart::tick`, NOT from `UartChannel::tick`, and that
    /// placement is MEASURED rather than aesthetic. `UartChannel::tick` runs
    /// once per Z80 instruction and gets inlined into `Uart::tick`; putting
    /// this work (a virtual call plus `byte_transfer_ticks()`) inside its body
    /// grew it past the inliner's budget and cost 4.8% of headless emulation
    /// throughput, 3.1% even with the body split out of line. Hoisted here,
    /// behind `Uart`'s single `device_attached_` flag, `UartChannel::tick` is
    /// byte-identical to what it was before this hook existed and the whole
    /// thing costs ONE predicted-not-taken branch per instruction for the
    /// entire UART: measured -0.3% (median) / -0.6% (min), i.e. inside
    /// run-to-run noise. Method: 7 interleaved pairs of
    /// `jnext --headless --machine next --delayed-automatic-exit-frames 900`
    /// against a build with the hook deleted. MEASURE BEFORE MOVING IT.
    ///
    /// The framing-reset check is repeated here on purpose: bit 7 holds the
    /// Next-side RX engine in S_PAUSE (uart_rx.vhd:219) and `reset()` empties
    /// the RX FIFO, so a device must not inject while the guest holds the UART
    /// down. A device that buffers host-side (the ESP does) loses nothing by
    /// waiting.
    void service_attached_device(uint32_t master_cycles) {
        if (framing_ & 0x80) return;
        if (device_ && device_->tick_wanted()) {
            // Computed FRESH so a mid-stream prescaler or framing change
            // (nextsync's `AT+UART_CUR` baud switch) re-paces delivery at once.
            device_->tick(master_cycles, byte_transfer_ticks());
        }
    }

    // ── Callbacks ─────────────────────────────────────────────

    /// Called when a byte has been fully transmitted from the TX FIFO,
    /// if no `UartDevice` is attached (an attached device takes precedence
    /// — see `deliver_tx_byte`). If neither is present, the byte is looped
    /// back into this channel's own RX FIFO (loopback mode).
    std::function<void(uint8_t byte)> on_tx_byte;

    /// Called when the TX FIFO becomes empty (for interrupt generation).
    std::function<void()> on_tx_empty;

    /// Called when a byte arrives in the RX FIFO (for interrupt generation).
    std::function<void()> on_rx_available;

    // ── Debug accessors ───────────────────────────────────────

    bool tx_empty() const { return tx_fifo_.empty() && !tx_busy_; }
    bool tx_full()  const { return tx_fifo_.full(); }
    bool rx_empty() const { return rx_fifo_.empty(); }
    bool rx_avail() const { return !rx_fifo_.empty(); }
    bool rx_near_full() const { return rx_fifo_.near_full(); }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    // ══ === TEST-ONLY ACCESSORS === ═══════════════════════════
    //
    // These accessors expose the bit-level engine's internal signals to
    // `test/uart/uart_test.cpp` for Wave A / Wave B / Wave E row assertions.
    // They MUST NOT be called from production code paths (Emulator,
    // PortDispatch, debugger UI). Adding the `_live` suffix keeps grep
    // distinct from production getters.

    // TX engine state machine (per uart_tx.vhd:82-247).
    enum class TxState : uint8_t {
        S_IDLE   = 0,
        S_RTR    = 1,
        S_START  = 2,
        S_BITS   = 3,
        S_PARITY = 4,
        S_STOP_1 = 5,
        S_STOP_2 = 6,
    };

    // RX engine state machine (per uart_rx.vhd:90-316).
    enum class RxState : uint8_t {
        S_IDLE   = 0,
        S_START  = 1,
        S_BITS   = 2,
        S_PARITY = 3,
        S_STOP_1 = 4,
        S_STOP_2 = 5,
        S_ERROR  = 6,
        S_PAUSE  = 7,
    };

    TxState  tx_state_live()            const { return tx_state_; }
    RxState  rx_state_live()            const { return rx_state_; }
    uint8_t  tx_shift_reg_live()        const { return tx_shift_; }
    uint8_t  rx_shift_reg_live()        const { return rx_shift_; }
    uint8_t  tx_bit_count_live()        const { return tx_bit_count_; }
    uint8_t  rx_bit_count_live()        const { return rx_bit_count_; }
    bool     tx_parity_live_snap()      const { return tx_parity_live_; }
    bool     rx_parity_live_snap()      const { return rx_parity_live_; }
    uint8_t  rx_debounce_counter_live() const { return rx_debounce_counter_; }
    uint32_t tx_timer_live()            const { return tx_timer_; }
    uint32_t rx_timer_live()            const { return rx_timer_; }
    // ══ === END TEST-ONLY ACCESSORS === ═══════════════════════

private:
    // FIFOs — TX is byte-only, RX widens to 9-bit elements so bit 8 carries
    // the VHDL (overflow OR framing) flag per uart.vhd:359.
    FifoBuffer<uint8_t,  TX_FIFO_SIZE> tx_fifo_;
    FifoBuffer<uint16_t, RX_FIFO_SIZE> rx_fifo_;

    // Prescaler: 17 bits = 3 MSB + 14 LSB
    uint8_t  prescaler_msb_ = 0;                    // bits 16:14
    uint16_t prescaler_lsb_ = 0b00000011110011;     // bits 13:0 — default 115200 @ 28MHz

    // Framing register (matches VHDL uart framing_r)
    // bit 7: reset, bit 6: break, bit 5: flow control,
    // bits 4:3: # bits (11=8), bit 2: parity enable,
    // bit 1: odd parity, bit 0: two stop bits
    uint8_t framing_ = 0x18;  // default: 8N1

    // Byte-level TX timing (legacy, default path)
    bool     tx_busy_ = false;
    uint32_t tx_timer_byte_ = 0;  // countdown in 28 MHz ticks until byte is done

    // Sticky error flags (VHDL: cleared on status read or FIFO reset)
    bool err_overflow_ = false;
    bool err_framing_  = false;
    bool err_break_    = false;

    // ── Bit-level engine state (opt-in, default off) ──────────
    //
    // Per the plan §Phase-1A, a faithful model of uart_tx.vhd:82-247 and
    // uart_rx.vhd:90-316. Dormant while `bitlevel_mode_ == false`; Wave-A/B/E
    // tests flip the mode per-row.

    bool     bitlevel_mode_ = false;

    // TX engine (uart_tx.vhd:84-99)
    TxState  tx_state_        = TxState::S_IDLE;  // state (98)
    TxState  tx_state_next_   = TxState::S_IDLE;  // state_next (99)
    uint8_t  tx_shift_        = 0;     // tx_shift (88), 8-bit data shift
    uint32_t tx_timer_        = 0;     // tx_timer (91), 17-bit prescaler counter
    uint32_t tx_prescaler_snap_ = 0;   // tx_prescaler (86), snapshot of i_prescaler
    uint8_t  tx_bit_count_    = 0;     // tx_bit_count (94), 3-bit down-counter
    bool     tx_parity_live_  = false; // tx_parity (95), running XOR
    bool     tx_frame_parity_en_ = false;  // snapshot of frame(2) (uart_tx.vhd:84)
    bool     tx_frame_stop_bits_ = false;  // snapshot of frame(0) (uart_tx.vhd:85)
    bool     tx_parity_odd_snap_ = false;  // snapshot of frame(1) (uart_tx.vhd:153)
    bool     cts_n_           = true;  // i_cts_n input, default released
    bool     tx_line_out_     = true;  // o_Tx output pin
    bool     tx_busy_bitlevel_= false; // o_busy output
    bool     tx_en_           = false; // i_Tx_en — asserted when TX FIFO has bytes

    // RX engine (uart_rx.vhd:90-113)
    RxState  rx_state_        = RxState::S_PAUSE;  // reset lands in S_PAUSE (uart_rx.vhd:220)
    RxState  rx_state_next_   = RxState::S_PAUSE;
    uint8_t  rx_shift_        = 0xFF;  // rx_shift (101)
    uint32_t rx_timer_        = 0;     // rx_timer (104)
    uint32_t rx_prescaler_snap_ = 0;   // rx_prescaler (99)
    bool     rx_timer_updated_= false; // rx_timer_updated (105)
    uint8_t  rx_bit_count_    = 0;     // rx_bit_count (108)
    bool     rx_parity_live_  = false; // rx_parity (109)
    uint8_t  rx_frame_bits_   = 0x03;  // snapshot of frame(4:3) (96), default 8 data bits
    bool     rx_frame_parity_en_ = false;
    bool     rx_frame_stop_bits_ = false;
    bool     rx_parity_odd_snap_ = false;

    // Noise-rejection debouncer (uart_rx.vhd:117-131 + misc/debounce.vhd).
    // NOISE_REJECTION_BITS = 2 per uart.vhd:34; counter is COUNTER_SIZE+1 bits
    // wide. Short pulses < 2^NOISE_REJECTION_BITS ticks don't propagate.
    uint8_t  rx_debounce_counter_ = 0;  // 3-bit counter (COUNTER_SIZE+1=3)
    uint8_t  rx_button_sync_      = 0x03;  // 2-stage sync register (INITIAL_STATE=1)
    bool     rx_raw_              = true;  // i_Rx raw input
    bool     rx_debounced_        = true;  // post-debounce Rx
    bool     rx_d_                = true;  // one-cycle delayed Rx_d
    bool     rx_edge_             = false; // Rx_e = Rx XOR Rx_d

    // Latched per-bit errors emitted by the RX engine, consumed when the byte
    // lands in the FIFO at the S_STOP_1/2 → S_IDLE transition.
    bool     rx_byte_parity_err_  = false;
    bool     rx_byte_framing_err_ = false;

    // Attached backend (non-owning). Deliberately NOT part of save_state /
    // load_state: which device is plugged in is host topology, not machine
    // state, and a rewind snapshot must not resurrect a stale pointer.
    UartDevice* device_ = nullptr;

    // NR 0x0B joystick-UART mux probe for this channel — see
    // set_device_isolated_probe(). Empty means "no mux wired up", which is every
    // bare-peripheral unit test and keeps them byte-identical.
    std::function<bool()> device_isolated_probe_;

    /// Hand one outbound byte to whoever is listening: an attached
    /// UartDevice first, else `on_tx_byte`. Returns false when neither is
    /// present, leaving the caller to decide what an unheard byte means
    /// (the byte-level engine loops it back; the bit-level one drops it).
    bool deliver_tx_byte(uint8_t byte);



    /// Pop the next TX FIFO byte and begin its transmission (byte-level
    /// engine): sets tx_busy_, arms tx_timer_byte_, emits via on_tx_byte
    /// or loopback inject_rx(). Caller guarantees !tx_fifo_.empty().
    void start_tx_from_fifo();

    /// Full 17-bit prescaler value.
    uint32_t prescaler() const {
        return (static_cast<uint32_t>(prescaler_msb_) << 14) | prescaler_lsb_;
    }

    /// Number of bits in one frame (start + data + parity + stop).
    uint32_t frame_bits() const;

    // Bit-level engine helpers.
    void tx_engine_step();
    void rx_engine_step();
    void rx_debounce_step();

    // Commit a fully-received byte to the RX FIFO with its 9th bit flag.
    void push_rx_with_flag(uint8_t byte, bool err_bit);
};

/// Dual-channel UART controller.
///
/// Wraps two UartChannel instances and handles the shared port interface
/// (channel selection via port 0x153B bit 6).
class Uart {
public:
    Uart();

    void reset();
    void hard_reset();

    /// Advance both channels by the given number of 28 MHz master ticks.
    void tick(uint32_t master_cycles);

    // ── Port interface (from PortDispatch) ────────────────────

    /// Write to a UART port.
    /// @param port_reg  register selector: 0=Rx, 1=Select, 2=Frame, 3=Tx
    ///   (derived from port address: 0x143B->0, 0x153B->1, 0x163B->2, 0x133B->3)
    void write(int port_reg, uint8_t val);

    /// Read from a UART port.
    /// @param port_reg  register selector (same encoding as write)
    uint8_t read(int port_reg);

    // ── External RX injection ─────────────────────────────────

    /// Push a byte into the RX FIFO of the specified channel.
    /// @param channel  0 = ESP, 1 = Pi
    void inject_rx(int channel, uint8_t byte);

    // ── Device attachment ─────────────────────────────────────

    /// Attach a backend to a channel (0 = ESP, 1 = Pi).
    ///
    /// The Uart does NOT own the device — caller manages lifetime, exactly
    /// as with `SpiMaster::attach_device`. Two things happen: the channel's
    /// TX bytes start going to `device->receive()` instead of looping back,
    /// and the device is handed an RxSink bound to this Uart + channel so it
    /// can inject toward the guest. Attaching over an existing device
    /// replaces it (the displaced device's sink is cleared first, so it
    /// cannot keep injecting).
    void attach_device(int channel, UartDevice* device);

    /// Detach whatever is on `channel`, clearing its RxSink and restoring
    /// the channel to loopback. Safe to call when nothing is attached.
    void detach_device(int channel);

    /// The device attached to `channel`, or nullptr.
    UartDevice* device(int channel) const;

    /// GH #251 — wire both channels' joystick-UART mux probes from one source of
    /// truth. `probe` answers "which channel has the joystick connector taken
    /// over?" as 0, 1, or -1 for "the mux is off" (the reset state, and the only
    /// state production reaches unless a guest sets NR 0x0B bits 7 and 5). See
    /// `UartChannel::set_device_isolated_probe`.
    void set_joy_uart_channel_probe(std::function<int()> probe) {
        channels_[0].set_device_isolated_probe(
            [probe] { return probe && probe() == 0; });
        channels_[1].set_device_isolated_probe(
            [probe = std::move(probe)] { return probe && probe() == 1; });
    }

    // ── Interrupt callbacks ───────────────────────────────────

    /// Fired when a UART channel generates a TX-empty interrupt.
    std::function<void(int channel)> on_tx_interrupt;

    /// Fired when a UART channel generates an RX-available interrupt.
    std::function<void(int channel)> on_rx_interrupt;

    // ── Debug accessors ───────────────────────────────────────

    const UartChannel& channel(int ch) const { return channels_[ch]; }
    UartChannel&       channel(int ch)       { return channels_[ch]; }
    int selected_channel() const { return select_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::array<UartChannel, 2> channels_;
    int select_ = 0;  // 0 = ESP (uart0), 1 = Pi (uart1)

    /// True while ANY channel has a backend attached. The one gate the
    /// per-instruction `tick` pays for; see `UartChannel::service_attached_device`
    /// for why the check lives here rather than per channel. Maintained by
    /// attach/detach and deliberately NOT serialised — which device is plugged
    /// in is host topology, like `UartChannel::device_` itself.
    bool device_attached_ = false;
};
