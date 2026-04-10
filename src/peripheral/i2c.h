#pragma once
#include <cstdint>
#include <unordered_map>
#include <ctime>
#include <array>

/// I2C device interface — base class for devices attached to the I2C bus.
class I2cDevice {
public:
    virtual ~I2cDevice() = default;

    /// Called on I2C START condition.
    virtual void start() = 0;

    /// Transfer one byte.  For writes (is_read=false), the device receives
    /// data and returns 0 for ACK, 1 for NACK.  For reads (is_read=true),
    /// the device returns the data byte.
    virtual uint8_t transfer(uint8_t data, bool is_read) = 0;

    /// Called on I2C STOP condition.
    virtual void stop() = 0;
};

/// DS1307-compatible RTC backed by the host system clock.
///
/// I2C address 0x68.  Supports reading time registers 0x00-0x06
/// (seconds, minutes, hours, day-of-week, date, month, year) in BCD.
/// Writes to registers are accepted but ignored (always returns host time).
class I2cRtc : public I2cDevice {
public:
    I2cRtc();

    void reset() { reg_ptr_ = 0; addr_set_ = false; regs_.fill(0); }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    void start() override;
    uint8_t transfer(uint8_t data, bool is_read) override;
    void stop() override;

private:
    static uint8_t to_bcd(int val);
    void snapshot_time();

    uint8_t reg_ptr_ = 0;         // current register pointer
    bool    addr_set_ = false;    // true after first write sets register pointer
    std::array<uint8_t, 8> regs_{}; // cached time registers (BCD)
};

/// I2C bus controller — decodes bit-bang protocol from port writes.
///
/// The ZX Next I2C is bit-banged via two ports:
///   Port 0x103B — SCL (clock line), bit 0
///   Port 0x113B — SDA (data line), bit 0 write / read
///
/// VHDL reference: zxnext.vhd (search for "I2C MASTER (bit-banged)")
///
/// The controller watches SCL/SDA transitions to detect START, data bits,
/// ACK/NACK, and STOP conditions, then delegates byte transfers to
/// attached I2cDevice instances.
class I2cController {
public:
    I2cController();

    void reset();

    /// Write to port 0x103B — set SCL line (bit 0).
    void write_scl(uint8_t val);

    /// Write to port 0x113B — set SDA output line (bit 0).
    void write_sda(uint8_t val);

    /// Read port 0x103B — returns SCL state (bit 0, upper bits set).
    uint8_t read_scl() const;

    /// Read port 0x113B — returns SDA input state (bit 0, upper bits set).
    uint8_t read_sda() const;

    /// Attach a device at the given 7-bit I2C address.
    void attach_device(uint8_t address, I2cDevice* device);

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    enum class State {
        IDLE,       // waiting for START
        ADDRESS,    // receiving 7-bit address + R/W bit
        ACK_ADDR,   // sending ACK for address byte
        DATA,       // receiving or transmitting data byte
        ACK_DATA,   // ACK phase after data byte
    };

    void on_scl_rising();
    void on_scl_falling();
    void detect_start_stop();

    // Line states (active-high; VHDL uses active-low but we invert at boundaries)
    uint8_t scl_ = 1;
    uint8_t sda_out_ = 1;   // CPU-driven SDA output
    uint8_t sda_in_ = 1;    // device-driven SDA input (directly from device or pull-up)
    uint8_t prev_scl_ = 1;
    uint8_t prev_sda_ = 1;

    // Protocol state
    State    state_ = State::IDLE;
    uint8_t  bit_count_ = 0;
    uint8_t  shift_reg_ = 0;
    uint8_t  device_addr_ = 0;   // 7-bit address of selected device
    bool     is_read_ = false;   // true if current transaction is a read
    uint8_t  read_data_ = 0xFF;  // data byte loaded from device for reads

    // Attached devices
    std::unordered_map<uint8_t, I2cDevice*> devices_;
    I2cDevice* active_device_ = nullptr;
};
