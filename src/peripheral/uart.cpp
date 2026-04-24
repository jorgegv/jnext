#include "peripheral/uart.h"
#include "core/log.h"
#include "core/saveable.h"

// ─── UART logger ──────────────────────────────────────────────────────

static std::shared_ptr<spdlog::logger>& uart_log() {
    static auto l = [] {
        auto existing = spdlog::get("uart");
        if (existing) return existing;
        auto logger = spdlog::stderr_color_mt("uart");
        logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return logger;
    }();
    return l;
}

// ─── UartChannel implementation ───────────────────────────────────────

void UartChannel::reset() {
    tx_fifo_.reset();
    rx_fifo_.reset();
    tx_busy_ = false;
    tx_timer_byte_ = 0;
    err_overflow_ = false;
    err_framing_ = false;
    err_break_ = false;
    // Note: prescaler and framing are NOT reset on soft reset (only hard reset)

    // Bit-level engine also resets per uart_tx.vhd:166-172 / uart_rx.vhd:216-225.
    // i_reset='1' drives TX state to S_IDLE and RX state to S_PAUSE.
    tx_state_        = TxState::S_IDLE;
    tx_state_next_   = TxState::S_IDLE;
    tx_shift_        = 0;
    tx_timer_        = 0;
    tx_bit_count_    = 0;
    tx_parity_live_  = false;
    tx_line_out_     = true;   // idle high (`not i_frame(6)` at reset with break=0)
    tx_busy_bitlevel_= false;
    tx_en_           = false;

    rx_state_        = RxState::S_PAUSE;
    rx_state_next_   = RxState::S_PAUSE;
    rx_shift_        = 0xFF;
    rx_timer_        = 0;
    rx_timer_updated_= false;
    rx_bit_count_    = 0;
    rx_parity_live_  = false;
    rx_debounce_counter_ = 0;
    rx_button_sync_  = 0x03;
    rx_raw_          = true;
    rx_debounced_    = true;
    rx_d_            = true;
    rx_edge_         = false;
    rx_byte_parity_err_  = false;
    rx_byte_framing_err_ = false;
}

void UartChannel::hard_reset() {
    reset();
    prescaler_msb_ = 0;
    prescaler_lsb_ = 0b00000011110011;  // 115200 @ 28MHz (243 decimal)
    framing_ = 0x18;                     // 8N1
    bitlevel_mode_ = false;
    cts_n_ = true;
}

uint32_t UartChannel::frame_bits() const {
    // Start bit (1) + data bits + optional parity + stop bits
    uint32_t data_bits;
    switch ((framing_ >> 3) & 0x03) {
        case 0x03: data_bits = 8; break;
        case 0x02: data_bits = 7; break;
        case 0x01: data_bits = 6; break;
        default:   data_bits = 5; break;
    }
    uint32_t total = 1 + data_bits;   // start + data
    if (framing_ & 0x04) total += 1;  // parity
    total += (framing_ & 0x01) ? 2 : 1;  // stop bits
    return total;
}

uint32_t UartChannel::byte_transfer_ticks() const {
    uint32_t ps = prescaler();
    if (ps == 0) ps = 1;  // avoid division by zero / infinite loop
    return ps * frame_bits();
}

void UartChannel::tick(uint32_t master_cycles) {
    // If framing bit 7 is set, the UART is in reset — do nothing
    if (framing_ & 0x80) return;

    for (uint32_t i = 0; i < master_cycles; ++i) {
        if (tx_busy_) {
            if (tx_timer_byte_ > 0) {
                --tx_timer_byte_;
            }
            if (tx_timer_byte_ == 0) {
                // Byte transmission complete
                tx_busy_ = false;

                // If TX FIFO has more bytes, start the next one immediately
                if (!tx_fifo_.empty()) {
                    uint8_t byte = tx_fifo_.pop();
                    tx_busy_ = true;
                    tx_timer_byte_ = byte_transfer_ticks();

                    if (on_tx_byte) {
                        on_tx_byte(byte);
                    } else {
                        // Loopback mode: feed TX output back into RX
                        inject_rx(byte);
                    }
                } else {
                    // TX FIFO is now empty and transmitter is idle
                    if (on_tx_empty) {
                        on_tx_empty();
                    }
                }
            }
        } else if (!tx_fifo_.empty()) {
            // Start transmitting the next byte from the FIFO
            uint8_t byte = tx_fifo_.pop();
            tx_busy_ = true;
            tx_timer_byte_ = byte_transfer_ticks();

            if (on_tx_byte) {
                on_tx_byte(byte);
            } else {
                // Loopback mode
                inject_rx(byte);
            }
        }
    }
}

void UartChannel::write_tx(uint8_t val) {
    if (tx_fifo_.full()) {
        uart_log()->debug("TX FIFO full, byte {:#04x} dropped", val);
        return;
    }
    tx_fifo_.push(val);
    uart_log()->trace("TX write {:#04x}, FIFO size={}", val, tx_fifo_.size());
}

uint8_t UartChannel::read_rx() {
    if (rx_fifo_.empty()) {
        uart_log()->trace("RX read: FIFO empty, returning 0");
        return 0;
    }
    uint16_t entry = rx_fifo_.pop();
    uint8_t val = static_cast<uint8_t>(entry & 0xFF);
    uart_log()->trace("RX read {:#04x}, FIFO size={}", val, rx_fifo_.size());
    return val;
}

uint8_t UartChannel::read_status() const {
    // Status byte layout (from VHDL, reading port 0x133B):
    //   bit 7: RX break condition
    //   bit 6: RX framing/parity error (sticky, clears on read)
    //   bit 5: RX error flag on current byte (rx_err & rx_avail) — per uart.vhd:359
    //   bit 4: TX empty (FIFO empty AND transmitter idle)
    //   bit 3: RX near full (3/4)
    //   bit 2: RX overflow (sticky, clears on read)
    //   bit 1: TX FIFO full
    //   bit 0: RX data available
    uint8_t status = 0;
    if (err_break_)                        status |= 0x80;
    if (err_framing_)                      status |= 0x40;
    // Current-byte error — VHDL uart.vhd:359: (uart0_rx_o(8) AND rx_avail).
    // The 9th bit of the RX FIFO head carries the per-byte (overflow OR framing)
    // error flag (uart.vhd:359). Wave B (Task 3 UART+I2C) switched this from
    // the sticky err_framing_ derivation to the authoritative FIFO-head path.
    // Byte-level inject_rx() pushes with bit 8 = 0 (see push_rx_with_flag) so
    // the 58 pre-Wave-B passing rows still observe bit 5 == 0.
    if (!rx_fifo_.empty() && ((rx_fifo_.front() >> 8) & 0x01)) status |= 0x20;
    if (tx_empty())                        status |= 0x10;
    if (rx_fifo_.near_full())              status |= 0x08;
    if (err_overflow_)                     status |= 0x04;
    if (tx_fifo_.full())                   status |= 0x02;
    if (!rx_fifo_.empty())                 status |= 0x01;
    return status;
}

bool UartChannel::read_rx_err_bit() const {
    return err_overflow_ || err_framing_;
}

void UartChannel::clear_errors() {
    err_overflow_ = false;
    err_framing_ = false;
}

void UartChannel::write_prescaler_lsb(uint8_t val) {
    if (val & 0x80) {
        // Upper 7 bits of the 14-bit prescaler
        prescaler_lsb_ = (prescaler_lsb_ & 0x007F) | (static_cast<uint16_t>(val & 0x7F) << 7);
        uart_log()->debug("prescaler LSB upper = {:#04x}, full LSB = {}", val & 0x7F, prescaler_lsb_);
    } else {
        // Lower 7 bits of the 14-bit prescaler
        prescaler_lsb_ = (prescaler_lsb_ & 0x3F80) | (val & 0x7F);
        uart_log()->debug("prescaler LSB lower = {:#04x}, full LSB = {}", val & 0x7F, prescaler_lsb_);
    }
}

void UartChannel::write_prescaler_msb(uint8_t val) {
    prescaler_msb_ = val & 0x07;
    uart_log()->debug("prescaler MSB = {}, full prescaler = {}", prescaler_msb_, prescaler());
}

void UartChannel::write_frame(uint8_t val) {
    framing_ = val;

    uart_log()->debug("frame = {:#04x}: reset={} break={} flow={} bits={} parity={}{} stop={}",
                      val,
                      (val & 0x80) ? 1 : 0,
                      (val & 0x40) ? 1 : 0,
                      (val & 0x20) ? 1 : 0,
                      5 + ((val >> 3) & 3),
                      (val & 0x04) ? 1 : 0,
                      (val & 0x04) ? ((val & 0x02) ? "(odd)" : "(even)") : "",
                      (val & 0x01) ? 2 : 1);

    // Bit 7 = reset: immediately reset TX/RX to idle and empty FIFOs
    if (val & 0x80) {
        uart_log()->info("UART channel reset via framing bit 7");
        reset();
        framing_ = val;  // preserve the framing register value after reset
    }
}

void UartChannel::inject_rx(uint8_t byte) {
    // Byte-level inject: store with err-flag = 0. Wave B tests that drive the
    // bit-level RX engine route through `inject_rx_bit_frame` instead to set
    // the 9th bit.
    push_rx_with_flag(byte, false);
}

void UartChannel::inject_rx_bit_frame(uint8_t byte, bool framing_err, bool parity_err) {
    push_rx_with_flag(byte, framing_err);
    if (parity_err) err_framing_ = true;  // VHDL: err_parity contributes to sticky err_framing
}

void UartChannel::push_rx_with_flag(uint8_t byte, bool err_bit) {
    if (rx_fifo_.full()) {
        err_overflow_ = true;
        uart_log()->debug("RX FIFO overflow, byte {:#04x} dropped", byte);
        return;
    }
    uint16_t entry = static_cast<uint16_t>(byte) | (err_bit ? 0x0100u : 0u);
    rx_fifo_.push(entry);
    uart_log()->trace("RX inject {:#04x} (err={}), FIFO size={}",
                      byte, err_bit ? 1 : 0, rx_fifo_.size());

    if (err_bit) err_framing_ = true;

    if (on_rx_available) {
        on_rx_available();
    }
}

// ── Bit-level engine — RTR#/break accessors ─────────────────────────

bool UartChannel::rx_rtr_n() const {
    // Per uart.vhd:442-446: `o_Rx_rtr_n = framing(5) AND rx_fifo_almost_full`
    // (when flow control is enabled; after reset follows framing(5) directly).
    // The FPGA output is active-low so "asserted RTR" means "ready to receive".
    // We return the raw wire state: 1 when the host should pause, 0 otherwise.
    if (framing_ & 0x80) return (framing_ & 0x20) ? true : false;
    return (framing_ & 0x20) && rx_fifo_.almost_full();
}

bool UartChannel::rx_break() const {
    // Per uart_rx.vhd:314: o_err_break = '1' when state = S_ERROR and
    // rx_shift = "00000000". Combinational, held while those conditions stand.
    return bitlevel_mode_ && rx_state_ == RxState::S_ERROR && rx_shift_ == 0x00;
}

// ── Bit-level TX + RX state machine step ────────────────────────────

void UartChannel::tick_one_bit_clock() {
    if (!bitlevel_mode_) return;
    // Advance the two engines one CLK_28 rising edge.
    rx_debounce_step();
    tx_engine_step();
    rx_engine_step();
}

void UartChannel::tx_engine_step() {
    // Reset / break gates (uart_tx.vhd:166-171, :234, :239):
    if (framing_ & 0x80) {
        tx_state_ = TxState::S_IDLE;
        tx_state_next_ = TxState::S_IDLE;
        tx_busy_bitlevel_ = true;   // o_busy = '1' when i_frame(7) = '1'
        tx_line_out_ = !(framing_ & 0x40);  // S_IDLE emits NOT i_frame(6) per VHDL :239
        return;
    }

    // o_busy (uart_tx.vhd:234).
    tx_busy_bitlevel_ = (tx_state_ != TxState::S_IDLE) ||
                       (framing_ & 0x40) || (framing_ & 0x80);

    // ── Snapshot frame/prescaler at falling edge of CLK when state==S_IDLE
    //    (uart_tx.vhd:107-114). We collapse falling+rising into one tick so
    //    the snapshot just runs each step while state==S_IDLE.
    if (tx_state_ == TxState::S_IDLE) {
        tx_frame_parity_en_ = (framing_ & 0x04) != 0;  // frame(2)
        tx_frame_stop_bits_ = (framing_ & 0x01) != 0;  // frame(0)
        tx_parity_odd_snap_ = (framing_ & 0x02) != 0;  // frame(1)
        tx_prescaler_snap_  = prescaler();
    }

    // ── Shift / bit-count updates (uart_tx.vhd:118-159) ────────────
    const bool tx_en = !tx_fifo_.empty();
    tx_en_ = tx_en;

    // tx_timer_expired: true when tx_timer_[PRESCALER_BITS-1:1] == 0
    // i.e. tx_timer_ <= 1 (17-bit counter).
    const bool tx_timer_expired = ((tx_timer_ >> 1) == 0);

    // tx_bit_count_expired: tx_bit_count_ == 0
    const bool tx_bit_count_expired = (tx_bit_count_ == 0);

    // ── State-next combinational (uart_tx.vhd:174-230) ─────────────
    switch (tx_state_) {
    case TxState::S_IDLE:
        if (framing_ & 0x40) {
            tx_state_next_ = TxState::S_IDLE;     // break-hold: stay idle
        } else if (!tx_en) {
            tx_state_next_ = TxState::S_IDLE;
        } else if (!cts_n_ || !(framing_ & 0x20)) {
            tx_state_next_ = TxState::S_START;    // flow disabled OR CTS asserted
        } else {
            tx_state_next_ = TxState::S_RTR;      // flow-on and CTS de-asserted
        }
        break;
    case TxState::S_RTR:
        if (cts_n_ && (framing_ & 0x20))
            tx_state_next_ = TxState::S_RTR;
        else
            tx_state_next_ = TxState::S_START;
        break;
    case TxState::S_START:
        tx_state_next_ = tx_timer_expired ? TxState::S_BITS : TxState::S_START;
        break;
    case TxState::S_BITS:
        if (!tx_bit_count_expired || !tx_timer_expired)
            tx_state_next_ = TxState::S_BITS;
        else if (tx_frame_parity_en_)
            tx_state_next_ = TxState::S_PARITY;
        else
            tx_state_next_ = TxState::S_STOP_1;
        break;
    case TxState::S_PARITY:
        tx_state_next_ = tx_timer_expired ? TxState::S_STOP_1 : TxState::S_PARITY;
        break;
    case TxState::S_STOP_1:
        if (!tx_timer_expired)
            tx_state_next_ = TxState::S_STOP_1;
        else if (tx_frame_stop_bits_)
            tx_state_next_ = TxState::S_STOP_2;
        else
            tx_state_next_ = TxState::S_IDLE;
        break;
    case TxState::S_STOP_2:
        tx_state_next_ = tx_timer_expired ? TxState::S_IDLE : TxState::S_STOP_2;
        break;
    }

    // ── Registered updates (uart_tx.vhd shift/bit-count/timer) ──────
    // Bit counter & parity (uart_tx.vhd:148-159). Must run BEFORE the
    // shift-register update: VHDL registers `tx_parity <= tx_parity XOR tx_shift(0)`
    // and `tx_shift <= '0' & tx_shift(7 downto 1)` concurrently on the clock
    // edge, so parity captures the PRE-shift LSB. If we shifted first we'd
    // see bit 1 instead of bit 0, inverting every byte's computed parity.
    if (tx_state_ == TxState::S_IDLE) {
        // tx_bit_count <= '1' & i_frame(4 downto 3); i.e. 4..7.
        tx_bit_count_ = static_cast<uint8_t>(0x04 | ((framing_ >> 3) & 0x03));
        tx_parity_live_ = tx_parity_odd_snap_;
    } else if (tx_state_ == TxState::S_BITS && tx_timer_expired) {
        if (tx_bit_count_ > 0) tx_bit_count_ -= 1;
        tx_parity_live_ ^= (tx_shift_ & 0x01);   // XOR pre-shift LSB
    }

    // Shift register (uart_tx.vhd:118-127). Runs AFTER parity capture above.
    if (tx_state_ == TxState::S_IDLE) {
        // Load next byte when FIFO has one; otherwise 0.
        if (tx_en) {
            tx_shift_ = tx_fifo_.front();
        }
    } else if (tx_state_ == TxState::S_BITS && tx_timer_expired) {
        tx_shift_ = static_cast<uint8_t>(tx_shift_ >> 1);
    }

    // Baud-rate timer (uart_tx.vhd:133-142). Reloads when state transitions
    // or when tx_timer_expired.
    if (tx_state_ != tx_state_next_ || tx_timer_expired) {
        tx_timer_ = tx_prescaler_snap_;
    } else if (tx_timer_ > 0) {
        tx_timer_ -= 1;
    }

    // ── TX pin output (uart_tx.vhd:236-245) ─────────────────────────
    switch (tx_state_) {
    case TxState::S_IDLE:
        tx_line_out_ = !((framing_ & 0x40) != 0);   // break line when frame(6)
        break;
    case TxState::S_START:
        tx_line_out_ = false;
        break;
    case TxState::S_BITS:
        tx_line_out_ = (tx_shift_ & 0x01) != 0;
        break;
    case TxState::S_PARITY:
        tx_line_out_ = tx_parity_live_;
        break;
    default:
        tx_line_out_ = true;
        break;
    }

    // ── FIFO pop at START → BITS edge ──────────────────────────────
    // VHDL uses uart0_tx_en on the FIFO read address; when state advances out
    // of S_IDLE into S_START a new byte has been latched into tx_shift_ so
    // the FIFO entry can be consumed.
    if (tx_state_ == TxState::S_IDLE && tx_state_next_ == TxState::S_START) {
        if (!tx_fifo_.empty()) {
            tx_shift_ = tx_fifo_.pop();  // consume
            if (tx_fifo_.empty() && on_tx_empty) on_tx_empty();
        }
    }

    // ── On S_STOP_1/2 → S_IDLE edge, fire on_tx_byte for observers that
    //    care (mirrors byte-level path's callback). ──────────────────
    if ((tx_state_ == TxState::S_STOP_1 || tx_state_ == TxState::S_STOP_2) &&
         tx_state_next_ == TxState::S_IDLE) {
        if (on_tx_byte) on_tx_byte(tx_shift_);
    }

    // ── Commit state_next → state (uart_tx.vhd:163-172) ────────────
    tx_state_ = tx_state_next_;
}

void UartChannel::rx_debounce_step() {
    // 2-stage synchroniser (debounce.vhd:52-57): button <= button(0) & button_i
    rx_button_sync_ = static_cast<uint8_t>(((rx_button_sync_ << 1) & 0x02) | (rx_raw_ ? 1u : 0u));
    const bool sync0 = (rx_button_sync_ & 0x01) != 0;
    const bool sync1 = (rx_button_sync_ & 0x02) != 0;
    const bool noise = sync0 ^ sync1;

    // Counter (debounce.vhd:63-72): resets on noise, counts up to 2^(COUNTER_SIZE+1)-1
    // when stable. NOISE_REJECTION_BITS=COUNTER_SIZE=2 → counter is 3 bits → saturates at 0x04.
    constexpr uint8_t COUNTER_TOP_BIT = 0x04;
    if (noise) {
        rx_debounce_counter_ = 0;
    } else if ((rx_debounce_counter_ & COUNTER_TOP_BIT) == 0) {
        rx_debounce_counter_ = static_cast<uint8_t>(rx_debounce_counter_ + 1);
    }

    // Output updates when counter saturates (debounce.vhd:76-83).
    if ((rx_debounce_counter_ & COUNTER_TOP_BIT) != 0) {
        rx_debounced_ = sync1;
    }

    // Rx_d delayed (uart_rx.vhd:133-138). Compute edge BEFORE updating rx_d_,
    // so rx_edge_ compares current debounced sample against PRIOR tick's rx_d_.
    // Final rx_d_ <- rx_debounced_ assignment happens in rx_engine_step() tail
    // (uart.cpp ~594), matching VHDL Rx_d <= Rx clocked process.
    rx_edge_ = rx_debounced_ ^ rx_d_;
}

void UartChannel::rx_engine_step() {
    if (framing_ & 0x80) {
        rx_state_ = RxState::S_PAUSE;
        rx_state_next_ = RxState::S_PAUSE;
        rx_shift_ = 0xFF;
        return;
    }

    // ── Snapshot frame/prescaler at falling-edge while state==S_IDLE
    //    (uart_rx.vhd:144-154) ───────────────────────────────────────
    if (rx_state_ == RxState::S_IDLE) {
        rx_frame_bits_       = static_cast<uint8_t>((framing_ >> 3) & 0x03);
        rx_frame_parity_en_  = (framing_ & 0x04) != 0;
        rx_parity_odd_snap_  = (framing_ & 0x02) != 0;
        rx_frame_stop_bits_  = (framing_ & 0x01) != 0;
        rx_prescaler_snap_   = prescaler();
    }

    // ── Combinational flags (uart_rx.vhd:178, 199) ─────────────────
    const bool rx_timer_expired = ((rx_timer_ >> 1) == 0);
    const bool rx_bit_count_expired = (rx_bit_count_ == 0);
    const bool rx = rx_debounced_;

    // ── State-next (uart_rx.vhd:227-295) ───────────────────────────
    switch (rx_state_) {
    case RxState::S_IDLE:
        if (framing_ & 0x40)       rx_state_next_ = RxState::S_PAUSE;
        else if (!rx)              rx_state_next_ = RxState::S_START;
        else                       rx_state_next_ = RxState::S_IDLE;
        break;
    case RxState::S_START:
        if (rx)                    rx_state_next_ = RxState::S_IDLE;
        else if (rx_timer_expired) rx_state_next_ = RxState::S_BITS;
        else                       rx_state_next_ = RxState::S_START;
        break;
    case RxState::S_BITS:
        if (!rx_bit_count_expired || !rx_timer_expired)
            rx_state_next_ = RxState::S_BITS;
        else if (rx_frame_parity_en_)
            rx_state_next_ = RxState::S_PARITY;
        else
            rx_state_next_ = RxState::S_STOP_1;
        break;
    case RxState::S_PARITY:
        if (!rx_timer_expired)     rx_state_next_ = RxState::S_PARITY;
        else if (rx == rx_parity_live_) rx_state_next_ = RxState::S_STOP_1;
        else                       rx_state_next_ = RxState::S_ERROR;
        break;
    case RxState::S_STOP_1:
        if (!rx_timer_expired)     rx_state_next_ = RxState::S_STOP_1;
        else if (!rx)              rx_state_next_ = RxState::S_ERROR;
        else if (rx_frame_stop_bits_) rx_state_next_ = RxState::S_STOP_2;
        else                       rx_state_next_ = RxState::S_IDLE;
        break;
    case RxState::S_STOP_2:
        if (!rx_timer_expired)     rx_state_next_ = RxState::S_STOP_2;
        else if (!rx)              rx_state_next_ = RxState::S_ERROR;
        else                       rx_state_next_ = RxState::S_IDLE;
        break;
    case RxState::S_ERROR:
        rx_state_next_ = rx ? RxState::S_IDLE : RxState::S_ERROR;
        break;
    case RxState::S_PAUSE:
        if ((framing_ & 0x40) || !rx) rx_state_next_ = RxState::S_PAUSE;
        else                           rx_state_next_ = RxState::S_IDLE;
        break;
    }

    // ── Shift register update (uart_rx.vhd:158-174) ────────────────
    if (rx_state_ == RxState::S_START || rx_state_next_ == RxState::S_ERROR) {
        rx_shift_ = 0xFF;
        if (rx_state_next_ == RxState::S_ERROR) rx_shift_ = 0x00;  // holds all-zero for break-detect
    } else if ((rx_state_ == RxState::S_BITS || rx_state_ == RxState::S_ERROR) && rx_timer_expired) {
        rx_shift_ = static_cast<uint8_t>((rx ? 0x80 : 0x00) | (rx_shift_ >> 1));
    } else if (rx_state_ == RxState::S_STOP_1 && rx_timer_expired) {
        // Alignment shift per data-bit count (uart_rx.vhd:166-171).
        switch (rx_frame_bits_) {
        case 0x02: rx_shift_ = static_cast<uint8_t>(rx_shift_ >> 1); break;
        case 0x01: rx_shift_ = static_cast<uint8_t>(rx_shift_ >> 2); break;
        case 0x00: rx_shift_ = static_cast<uint8_t>(rx_shift_ >> 3); break;
        default: break;  // "11" → 8 data bits, no shift
        }
    }

    // ── Baud-rate timer (uart_rx.vhd:180-195) ──────────────────────
    if (rx_state_ == RxState::S_IDLE) {
        rx_timer_ = rx_prescaler_snap_ >> 1;
        rx_timer_updated_ = false;
    } else if (rx_timer_expired) {
        rx_timer_ = rx_prescaler_snap_;
        rx_timer_updated_ = false;
    } else if (rx_state_ != RxState::S_START && rx_edge_ && !rx_timer_updated_) {
        rx_timer_ = rx_prescaler_snap_ >> 1;
        rx_timer_updated_ = true;
    } else if (rx_timer_ > 0) {
        rx_timer_ -= 1;
    }

    // ── Bit counter & parity (uart_rx.vhd:201-212) ─────────────────
    if (rx_state_ == RxState::S_IDLE) {
        rx_bit_count_ = static_cast<uint8_t>(0x04 | rx_frame_bits_);
        rx_parity_live_ = rx_parity_odd_snap_;
    } else if (rx_state_ == RxState::S_BITS && rx_timer_expired) {
        if (rx_bit_count_ > 0) rx_bit_count_ -= 1;
        rx_parity_live_ = rx_parity_live_ ^ rx;
    }

    // Latch parity-error / framing-error flags for the byte that is about to
    // land in the FIFO at S_STOP_* → S_IDLE. Also OR them into the sticky
    // err_framing_ flag per VHDL uart.vhd:541 (status-bit 6 accumulates any
    // observed framing or parity error until cleared by FIFO reset or by
    // the read-side status-register side effect). Without the OR-in, S_ERROR
    // transitions orphan the latch — the byte never commits through the
    // STOP_* → IDLE path that would otherwise propagate it into sticky.
    if (rx_state_ == RxState::S_PARITY && rx_state_next_ == RxState::S_ERROR) {
        rx_byte_parity_err_ = true;
        err_framing_ = true;
    }
    if ((rx_state_ == RxState::S_STOP_1 || rx_state_ == RxState::S_STOP_2)
         && rx_state_next_ == RxState::S_ERROR) {
        rx_byte_framing_err_ = true;
        err_framing_ = true;
    }

    // ── Byte commit on STOP_x → IDLE (uart_rx.vhd:299-308, o_Rx_avail) ──
    if ((rx_state_ == RxState::S_STOP_1 || rx_state_ == RxState::S_STOP_2) &&
         rx_state_next_ == RxState::S_IDLE) {
        // Byte ready. VHDL o_Rx_byte = rx_shift. The alignment shift already
        // took place in S_STOP_1.
        push_rx_with_flag(rx_shift_, rx_byte_parity_err_ || rx_byte_framing_err_);
        rx_byte_parity_err_ = false;
        rx_byte_framing_err_ = false;
    }

    // Update delayed-Rx (Rx_d) after edge computation (so rx_edge_ this tick
    // saw the previous rx_d_). Mirrors uart_rx.vhd:133-140.
    rx_d_ = rx_debounced_;

    // Commit state.
    rx_state_ = rx_state_next_;
}

// ─── Uart implementation ──────────────────────────────────────────────

Uart::Uart() {
    hard_reset();

    // Wire up loopback and interrupt forwarding for each channel
    for (int ch = 0; ch < 2; ++ch) {
        channels_[ch].on_tx_empty = [this, ch]() {
            if (on_tx_interrupt) {
                on_tx_interrupt(ch);
            }
        };
        channels_[ch].on_rx_available = [this, ch]() {
            if (on_rx_interrupt) {
                on_rx_interrupt(ch);
            }
        };
    }
}

void Uart::reset() {
    select_ = 0;
    for (auto& ch : channels_) {
        ch.reset();
    }
    uart_log()->info("UART soft reset");
}

void Uart::hard_reset() {
    select_ = 0;
    for (auto& ch : channels_) {
        ch.hard_reset();
    }
    uart_log()->info("UART hard reset");
}

void Uart::tick(uint32_t master_cycles) {
    channels_[0].tick(master_cycles);
    channels_[1].tick(master_cycles);
}

void Uart::write(int port_reg, uint8_t val) {
    switch (port_reg) {
        case 0: {
            // 0x143B Rx port write — prescaler LSB
            channels_[select_].write_prescaler_lsb(val);
            uart_log()->debug("ch{} prescaler LSB write {:#04x}", select_, val);
            break;
        }
        case 1: {
            // 0x153B Select port write
            // bit 6 = channel select
            // bit 4 = 1 means bits 2:0 are prescaler MSB to write
            int new_select = (val & 0x40) ? 1 : 0;
            if (val & 0x10) {
                // Write prescaler MSB to the channel indicated by bit 6
                channels_[new_select].write_prescaler_msb(val & 0x07);
                uart_log()->debug("ch{} prescaler MSB write {}", new_select, val & 0x07);
            }
            select_ = new_select;
            uart_log()->debug("UART select = {} ({})", select_, select_ == 0 ? "ESP" : "Pi");
            break;
        }
        case 2: {
            // 0x163B Frame port write
            channels_[select_].write_frame(val);
            uart_log()->debug("ch{} frame write {:#04x}", select_, val);
            break;
        }
        case 3: {
            // 0x133B Tx port write — send byte
            channels_[select_].write_tx(val);
            uart_log()->debug("ch{} TX write {:#04x}", select_, val);
            break;
        }
    }
}

uint8_t Uart::read(int port_reg) {
    switch (port_reg) {
        case 0: {
            // 0x143B Rx port read — read byte from RX FIFO
            uint8_t val = channels_[select_].read_rx();
            uart_log()->trace("ch{} RX read {:#04x}", select_, val);
            return val;
        }
        case 1: {
            // 0x153B Select port read — returns channel select + prescaler MSB
            // VHDL uart.vhd:371: uart0 returns "00000" & msb,
            // uart1 returns "01000" & msb → bit 3 (0x08), not bit 6.
            uint8_t val = (select_ ? 0x08 : 0x00) | channels_[select_].read_prescaler_msb();
            uart_log()->trace("ch{} select read {:#04x}", select_, val);
            return val;
        }
        case 2: {
            // 0x163B Frame port read
            uint8_t val = channels_[select_].read_frame();
            uart_log()->trace("ch{} frame read {:#04x}", select_, val);
            return val;
        }
        case 3: {
            // 0x133B Tx port read — status register
            uint8_t val = channels_[select_].read_status();
            // Reading the status clears sticky error flags (from VHDL: uart0_tx_rd_fe)
            channels_[select_].clear_errors();
            uart_log()->trace("ch{} status read {:#04x}", select_, val);
            return val;
        }
    }
    return 0xFF;
}

void Uart::inject_rx(int channel, uint8_t byte) {
    if (channel < 0 || channel > 1) return;
    channels_[channel].inject_rx(byte);
}

void UartChannel::save_state(StateWriter& w) const
{
    tx_fifo_.save_state(w);
    rx_fifo_.save_state(w);
    w.write_u8(prescaler_msb_);
    w.write_u16(prescaler_lsb_);
    w.write_u8(framing_);
    w.write_bool(tx_busy_);
    w.write_u32(tx_timer_byte_);
    w.write_bool(err_overflow_);
    w.write_bool(err_framing_);
    w.write_bool(err_break_);

    // Phase-1: bit-level engine state. Rewind buffer is in-process, no on-disk
    // compat required — adding fields here is safe (plan R2/R3).
    w.write_bool(bitlevel_mode_);
    w.write_u8(static_cast<uint8_t>(tx_state_));
    w.write_u8(static_cast<uint8_t>(tx_state_next_));
    w.write_u8(tx_shift_);
    w.write_u32(tx_timer_);
    w.write_u32(tx_prescaler_snap_);
    w.write_u8(tx_bit_count_);
    w.write_bool(tx_parity_live_);
    w.write_bool(tx_frame_parity_en_);
    w.write_bool(tx_frame_stop_bits_);
    w.write_bool(tx_parity_odd_snap_);
    w.write_bool(cts_n_);
    w.write_bool(tx_line_out_);
    w.write_bool(tx_busy_bitlevel_);
    w.write_bool(tx_en_);

    w.write_u8(static_cast<uint8_t>(rx_state_));
    w.write_u8(static_cast<uint8_t>(rx_state_next_));
    w.write_u8(rx_shift_);
    w.write_u32(rx_timer_);
    w.write_u32(rx_prescaler_snap_);
    w.write_bool(rx_timer_updated_);
    w.write_u8(rx_bit_count_);
    w.write_bool(rx_parity_live_);
    w.write_u8(rx_frame_bits_);
    w.write_bool(rx_frame_parity_en_);
    w.write_bool(rx_frame_stop_bits_);
    w.write_bool(rx_parity_odd_snap_);
    w.write_u8(rx_debounce_counter_);
    w.write_u8(rx_button_sync_);
    w.write_bool(rx_raw_);
    w.write_bool(rx_debounced_);
    w.write_bool(rx_d_);
    w.write_bool(rx_edge_);
    w.write_bool(rx_byte_parity_err_);
    w.write_bool(rx_byte_framing_err_);
}

void UartChannel::load_state(StateReader& r)
{
    tx_fifo_.load_state(r);
    rx_fifo_.load_state(r);
    prescaler_msb_ = r.read_u8();
    prescaler_lsb_ = r.read_u16();
    framing_       = r.read_u8();
    tx_busy_       = r.read_bool();
    tx_timer_byte_ = r.read_u32();
    err_overflow_  = r.read_bool();
    err_framing_   = r.read_bool();
    err_break_     = r.read_bool();

    bitlevel_mode_     = r.read_bool();
    tx_state_          = static_cast<TxState>(r.read_u8());
    tx_state_next_     = static_cast<TxState>(r.read_u8());
    tx_shift_          = r.read_u8();
    tx_timer_          = r.read_u32();
    tx_prescaler_snap_ = r.read_u32();
    tx_bit_count_      = r.read_u8();
    tx_parity_live_    = r.read_bool();
    tx_frame_parity_en_= r.read_bool();
    tx_frame_stop_bits_= r.read_bool();
    tx_parity_odd_snap_= r.read_bool();
    cts_n_             = r.read_bool();
    tx_line_out_       = r.read_bool();
    tx_busy_bitlevel_  = r.read_bool();
    tx_en_             = r.read_bool();

    rx_state_          = static_cast<RxState>(r.read_u8());
    rx_state_next_     = static_cast<RxState>(r.read_u8());
    rx_shift_          = r.read_u8();
    rx_timer_          = r.read_u32();
    rx_prescaler_snap_ = r.read_u32();
    rx_timer_updated_  = r.read_bool();
    rx_bit_count_      = r.read_u8();
    rx_parity_live_    = r.read_bool();
    rx_frame_bits_     = r.read_u8();
    rx_frame_parity_en_= r.read_bool();
    rx_frame_stop_bits_= r.read_bool();
    rx_parity_odd_snap_= r.read_bool();
    rx_debounce_counter_ = r.read_u8();
    rx_button_sync_    = r.read_u8();
    rx_raw_            = r.read_bool();
    rx_debounced_      = r.read_bool();
    rx_d_              = r.read_bool();
    rx_edge_           = r.read_bool();
    rx_byte_parity_err_  = r.read_bool();
    rx_byte_framing_err_ = r.read_bool();
}

void Uart::save_state(StateWriter& w) const
{
    w.write_i32(select_);
    for (const auto& ch : channels_) ch.save_state(w);
}

void Uart::load_state(StateReader& r)
{
    select_ = r.read_i32();
    for (auto& ch : channels_) ch.load_state(r);
}
