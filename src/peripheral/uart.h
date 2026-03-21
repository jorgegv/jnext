#pragma once
#include <cstdint>
#include <array>
#include <functional>

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
template <std::size_t Capacity>
class FifoBuffer {
public:
    void reset() { head_ = 0; tail_ = 0; count_ = 0; }

    bool empty()      const { return count_ == 0; }
    bool full()       const { return count_ >= Capacity; }
    bool near_full()  const { return count_ >= (Capacity * 3 / 4); }
    bool almost_full()const { return count_ >= (Capacity - 2); }
    std::size_t size() const { return count_; }

    /// Push a byte.  Returns false if full (byte is dropped).
    bool push(uint8_t val) {
        if (full()) return false;
        buf_[head_] = val;
        head_ = (head_ + 1) % Capacity;
        ++count_;
        return true;
    }

    /// Pop a byte.  Returns 0 if empty.
    uint8_t pop() {
        if (empty()) return 0;
        uint8_t val = buf_[tail_];
        tail_ = (tail_ + 1) % Capacity;
        --count_;
        return val;
    }

    /// Peek at front without removing.
    uint8_t front() const {
        if (empty()) return 0;
        return buf_[tail_];
    }

private:
    std::array<uint8_t, Capacity> buf_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
};

/// Single UART channel — models TX/RX with FIFOs and baud-rate timing.
///
/// In emulation, we do not model individual bit shifting.  Instead, a byte
/// written to the TX FIFO is transferred to the RX side (loopback) or
/// consumed by an external handler after a delay equal to the frame
/// bit-count times the prescaler (in 28 MHz master ticks).
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
    void inject_rx(uint8_t byte);

    // ── Callbacks ─────────────────────────────────────────────

    /// Called when a byte has been fully transmitted from the TX FIFO.
    /// The external handler receives the byte; if no handler is set,
    /// the byte is looped back into the RX FIFO (loopback mode).
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

private:
    // FIFOs
    FifoBuffer<TX_FIFO_SIZE> tx_fifo_;
    FifoBuffer<RX_FIFO_SIZE> rx_fifo_;

    // Prescaler: 17 bits = 3 MSB + 14 LSB
    uint8_t  prescaler_msb_ = 0;                    // bits 16:14
    uint16_t prescaler_lsb_ = 0b00000011110011;     // bits 13:0 — default 115200 @ 28MHz

    // Framing register (matches VHDL uart framing_r)
    // bit 7: reset, bit 6: break, bit 5: flow control,
    // bits 4:3: # bits (11=8), bit 2: parity enable,
    // bit 1: odd parity, bit 0: two stop bits
    uint8_t framing_ = 0x18;  // default: 8N1

    // TX state machine
    bool     tx_busy_ = false;
    uint32_t tx_timer_ = 0;     // countdown in 28 MHz ticks until byte is done

    // Sticky error flags (VHDL: cleared on status read or FIFO reset)
    bool err_overflow_ = false;
    bool err_framing_  = false;
    bool err_break_    = false;

    /// Compute the number of 28 MHz ticks for one complete byte transfer.
    uint32_t byte_transfer_ticks() const;

    /// Full 17-bit prescaler value.
    uint32_t prescaler() const {
        return (static_cast<uint32_t>(prescaler_msb_) << 14) | prescaler_lsb_;
    }

    /// Number of bits in one frame (start + data + parity + stop).
    uint32_t frame_bits() const;
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

    // ── Interrupt callbacks ───────────────────────────────────

    /// Fired when a UART channel generates a TX-empty interrupt.
    std::function<void(int channel)> on_tx_interrupt;

    /// Fired when a UART channel generates an RX-available interrupt.
    std::function<void(int channel)> on_rx_interrupt;

    // ── Debug accessors ───────────────────────────────────────

    const UartChannel& channel(int ch) const { return channels_[ch]; }
    int selected_channel() const { return select_; }

private:
    std::array<UartChannel, 2> channels_;
    int select_ = 0;  // 0 = ESP (uart0), 1 = Pi (uart1)
};
