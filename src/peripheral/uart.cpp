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
    tx_timer_ = 0;
    err_overflow_ = false;
    err_framing_ = false;
    err_break_ = false;
    // Note: prescaler and framing are NOT reset on soft reset (only hard reset)
}

void UartChannel::hard_reset() {
    reset();
    prescaler_msb_ = 0;
    prescaler_lsb_ = 0b00000011110011;  // 115200 @ 28MHz (243 decimal)
    framing_ = 0x18;                     // 8N1
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
            if (tx_timer_ > 0) {
                --tx_timer_;
            }
            if (tx_timer_ == 0) {
                // Byte transmission complete
                tx_busy_ = false;

                // If TX FIFO has more bytes, start the next one immediately
                if (!tx_fifo_.empty()) {
                    uint8_t byte = tx_fifo_.pop();
                    tx_busy_ = true;
                    tx_timer_ = byte_transfer_ticks();

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
            tx_timer_ = byte_transfer_ticks();

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
    uint8_t val = rx_fifo_.pop();
    uart_log()->trace("RX read {:#04x}, FIFO size={}", val, rx_fifo_.size());
    return val;
}

uint8_t UartChannel::read_status() const {
    // Status byte layout (from VHDL, reading port 0x133B):
    //   bit 7: RX break condition
    //   bit 6: RX framing/parity error (sticky, clears on read)
    //   bit 5: RX error flag on current byte (rx_err & rx_avail)
    //   bit 4: TX empty (FIFO empty AND transmitter idle)
    //   bit 3: RX near full (3/4)
    //   bit 2: RX overflow (sticky, clears on read)
    //   bit 1: TX FIFO full
    //   bit 0: RX data available
    uint8_t status = 0;
    if (err_break_)                        status |= 0x80;
    if (err_framing_)                      status |= 0x40;
    if (err_framing_ && !rx_fifo_.empty()) status |= 0x20;  // err on current byte
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
    uint8_t old = framing_;
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
    if (rx_fifo_.full()) {
        err_overflow_ = true;
        uart_log()->debug("RX FIFO overflow, byte {:#04x} dropped", byte);
        return;
    }
    rx_fifo_.push(byte);
    uart_log()->trace("RX inject {:#04x}, FIFO size={}", byte, rx_fifo_.size());

    if (on_rx_available) {
        on_rx_available();
    }
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
            // VHDL: uart0 returns "00000" & msb, uart1 returns "01000" & msb
            uint8_t val = (select_ ? 0x40 : 0x00) | channels_[select_].read_prescaler_msb();
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
    w.write_u32(tx_timer_);
    w.write_bool(err_overflow_);
    w.write_bool(err_framing_);
    w.write_bool(err_break_);
}

void UartChannel::load_state(StateReader& r)
{
    tx_fifo_.load_state(r);
    rx_fifo_.load_state(r);
    prescaler_msb_ = r.read_u8();
    prescaler_lsb_ = r.read_u16();
    framing_       = r.read_u8();
    tx_busy_       = r.read_bool();
    tx_timer_      = r.read_u32();
    err_overflow_  = r.read_bool();
    err_framing_   = r.read_bool();
    err_break_     = r.read_bool();
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
