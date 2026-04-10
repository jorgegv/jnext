#include "peripheral/i2c.h"
#include "core/log.h"
#include "core/saveable.h"

// Local I2C logger — created on first use, follows the same pattern as Log::make()
namespace {
    std::shared_ptr<spdlog::logger>& i2c_log() {
        static auto logger = [] {
            auto existing = spdlog::get("i2c");
            if (existing) return existing;
            auto l = spdlog::stderr_color_mt("i2c");
            l->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
            return l;
        }();
        return logger;
    }
}

// ─── I2cRtc ──────────────────────────────────────────────────────────

I2cRtc::I2cRtc() {
    snapshot_time();
}

void I2cRtc::start() {
    addr_set_ = false;
    snapshot_time();
}

uint8_t I2cRtc::transfer(uint8_t data, bool is_read) {
    if (is_read) {
        // Read: return register at current pointer, auto-increment
        uint8_t val = (reg_ptr_ < regs_.size()) ? regs_[reg_ptr_] : 0x00;
        i2c_log()->debug("RTC read reg {:#04x} = {:#04x}", reg_ptr_, val);
        reg_ptr_ = (reg_ptr_ + 1) & 0x07;
        return val;
    } else {
        // Write: first byte sets register pointer, subsequent bytes are data writes
        if (!addr_set_) {
            reg_ptr_ = data & 0x07;
            addr_set_ = true;
            i2c_log()->debug("RTC set register pointer = {:#04x}", reg_ptr_);
        } else {
            // Accept writes silently (we always return host time on reads)
            i2c_log()->debug("RTC write reg {:#04x} = {:#04x} (ignored)", reg_ptr_, data);
            reg_ptr_ = (reg_ptr_ + 1) & 0x07;
        }
        return 0; // ACK
    }
}

void I2cRtc::stop() {
    // Nothing to do
}

uint8_t I2cRtc::to_bcd(int val) {
    return static_cast<uint8_t>(((val / 10) << 4) | (val % 10));
}

void I2cRtc::snapshot_time() {
    std::time_t now = std::time(nullptr);
    std::tm* t = std::localtime(&now);
    if (!t) return;

    regs_[0] = to_bcd(t->tm_sec);             // 0x00: seconds (BCD, bit 7 = CH = 0)
    regs_[1] = to_bcd(t->tm_min);             // 0x01: minutes
    regs_[2] = to_bcd(t->tm_hour);            // 0x02: hours (24h mode, bit 6 = 0)
    regs_[3] = to_bcd(t->tm_wday + 1);        // 0x03: day of week (1-7)
    regs_[4] = to_bcd(t->tm_mday);            // 0x04: date
    regs_[5] = to_bcd(t->tm_mon + 1);         // 0x05: month (1-12)
    regs_[6] = to_bcd(t->tm_year % 100);      // 0x06: year (00-99)
    regs_[7] = 0x00;                           // 0x07: control register
}

// ─── I2cController ───────────────────────────────────────────────────

I2cController::I2cController() {
    reset();
}

void I2cController::reset() {
    scl_ = 1;
    sda_out_ = 1;
    sda_in_ = 1;
    prev_scl_ = 1;
    prev_sda_ = 1;
    state_ = State::IDLE;
    bit_count_ = 0;
    shift_reg_ = 0;
    device_addr_ = 0;
    is_read_ = false;
    read_data_ = 0xFF;
    active_device_ = nullptr;
}

void I2cController::write_scl(uint8_t val) {
    prev_scl_ = scl_;
    scl_ = val & 0x01;

    // Check for START/STOP (SDA transitions while SCL high)
    detect_start_stop();

    // Detect SCL edges
    if (prev_scl_ == 0 && scl_ == 1) {
        on_scl_rising();
    } else if (prev_scl_ == 1 && scl_ == 0) {
        on_scl_falling();
    }
}

void I2cController::write_sda(uint8_t val) {
    prev_sda_ = sda_out_;
    sda_out_ = val & 0x01;

    // Check for START/STOP (SDA transitions while SCL high)
    detect_start_stop();
}

uint8_t I2cController::read_scl() const {
    // Upper 7 bits read as 1 (pulled high), bit 0 is SCL state
    return 0xFE | scl_;
}

uint8_t I2cController::read_sda() const {
    // Upper 7 bits read as 1 (pulled high), bit 0 is SDA input
    // SDA is the AND of sda_out_ and sda_in_ (open-drain wired-AND)
    return 0xFE | (sda_out_ & sda_in_);
}

void I2cController::attach_device(uint8_t address, I2cDevice* device) {
    devices_[address & 0x7F] = device;
    i2c_log()->info("I2C device attached at address {:#04x}", address & 0x7F);
}

void I2cController::detect_start_stop() {
    // START: SDA falls while SCL is high
    if (scl_ == 1 && prev_sda_ == 1 && sda_out_ == 0) {
        i2c_log()->debug("I2C START condition");
        state_ = State::ADDRESS;
        bit_count_ = 0;
        shift_reg_ = 0;
        active_device_ = nullptr;
    }

    // STOP: SDA rises while SCL is high
    if (scl_ == 1 && prev_sda_ == 0 && sda_out_ == 1) {
        i2c_log()->debug("I2C STOP condition");
        if (active_device_) {
            active_device_->stop();
        }
        state_ = State::IDLE;
        active_device_ = nullptr;
        sda_in_ = 1; // release SDA
    }
}

void I2cController::on_scl_rising() {
    // Data is sampled on SCL rising edge
    switch (state_) {
    case State::IDLE:
        break;

    case State::ADDRESS:
    case State::DATA:
        // Sample SDA into shift register (MSB first)
        shift_reg_ = (shift_reg_ << 1) | sda_out_;
        bit_count_++;

        if (bit_count_ >= 8) {
            if (state_ == State::ADDRESS) {
                device_addr_ = (shift_reg_ >> 1) & 0x7F;
                is_read_ = (shift_reg_ & 0x01) != 0;
                i2c_log()->debug("I2C address={:#04x} R/W={}", device_addr_, is_read_ ? "R" : "W");

                // Look up device
                auto it = devices_.find(device_addr_);
                if (it != devices_.end()) {
                    active_device_ = it->second;
                    active_device_->start();
                    // If read, pre-load first data byte
                    if (is_read_) {
                        read_data_ = active_device_->transfer(0, true);
                    }
                } else {
                    active_device_ = nullptr;
                    i2c_log()->debug("I2C no device at address {:#04x}", device_addr_);
                }
                state_ = State::ACK_ADDR;
            } else {
                // DATA phase (write direction)
                if (active_device_ && !is_read_) {
                    active_device_->transfer(shift_reg_, false);
                }
                state_ = State::ACK_DATA;
            }
        }
        break;

    case State::ACK_ADDR:
        // ACK bit is being sampled — we set sda_in_ on falling edge
        break;

    case State::ACK_DATA:
        // ACK bit is being sampled
        if (is_read_) {
            // Master drives ACK/NACK for reads; sample it
            uint8_t master_ack = sda_out_;
            if (master_ack == 1) {
                // NACK — master doesn't want more data
                i2c_log()->debug("I2C master NACK (read done)");
            }
        }
        break;
    }
}

void I2cController::on_scl_falling() {
    // Output data on SCL falling edge (device drives SDA for reads/ACKs)
    switch (state_) {
    case State::IDLE:
        break;

    case State::ADDRESS:
    case State::DATA:
        if (is_read_ && state_ == State::DATA) {
            // Device drives SDA with current bit of read data
            uint8_t bit_idx = 7 - bit_count_;
            sda_in_ = (read_data_ >> bit_idx) & 0x01;
        } else {
            sda_in_ = 1; // release SDA for master to drive
        }
        break;

    case State::ACK_ADDR:
        // Device drives ACK (low) or NACK (high)
        if (active_device_) {
            sda_in_ = 0; // ACK
            i2c_log()->debug("I2C ACK address");
        } else {
            sda_in_ = 1; // NACK — no device
            i2c_log()->debug("I2C NACK address (no device)");
        }
        // Transition to DATA phase
        state_ = State::DATA;
        bit_count_ = 0;
        shift_reg_ = 0;
        break;

    case State::ACK_DATA:
        if (is_read_) {
            // Master has ACKed/NACKed; prepare next byte
            sda_in_ = 1; // release for master ACK
            if (active_device_ && sda_out_ == 0) {
                // Master ACKed — load next byte
                read_data_ = active_device_->transfer(0, true);
            }
        } else {
            // Write direction: device drives ACK
            sda_in_ = (active_device_) ? 0 : 1;
            i2c_log()->debug("I2C {} data write", active_device_ ? "ACK" : "NACK");
        }
        state_ = State::DATA;
        bit_count_ = 0;
        shift_reg_ = 0;
        break;
    }
}

void I2cRtc::save_state(StateWriter& w) const
{
    w.write_u8(reg_ptr_);
    w.write_bool(addr_set_);
    w.write_bytes(regs_.data(), regs_.size());
}

void I2cRtc::load_state(StateReader& r)
{
    reg_ptr_  = r.read_u8();
    addr_set_ = r.read_bool();
    r.read_bytes(regs_.data(), regs_.size());
}

void I2cController::save_state(StateWriter& w) const
{
    w.write_u8(scl_);
    w.write_u8(sda_out_);
    w.write_u8(sda_in_);
    w.write_u8(prev_scl_);
    w.write_u8(prev_sda_);
    w.write_u8(static_cast<uint8_t>(state_));
    w.write_u8(bit_count_);
    w.write_u8(shift_reg_);
    w.write_u8(device_addr_);
    w.write_bool(is_read_);
    w.write_u8(read_data_);
}

void I2cController::load_state(StateReader& r)
{
    scl_         = r.read_u8();
    sda_out_     = r.read_u8();
    sda_in_      = r.read_u8();
    prev_scl_    = r.read_u8();
    prev_sda_    = r.read_u8();
    state_       = static_cast<State>(r.read_u8());
    bit_count_   = r.read_u8();
    shift_reg_   = r.read_u8();
    device_addr_ = r.read_u8();
    is_read_     = r.read_bool();
    read_data_   = r.read_u8();
}
