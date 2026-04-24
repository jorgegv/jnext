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
///
/// Phase-1 (Task 3 UART+I2C plan) storage widening:
///   * `regs_` expanded from 8 bytes to 64 bytes per DS1307 datasheet (0x00-0x07
///     clock + control, 0x08-0x3F NVRAM).
///   * `use_real_time_` flag gates the host-clock snapshot: when true (default),
///     `start()` refreshes regs_[0..6] from the host clock; when false, Wave-E
///     tests can `poke_register` arbitrary BCD values and read them back.
///   * `osc_halt_` / `mode_12h_` feature flags are scaffolded here — the
///     transfer() path is unchanged in Phase 1 so the 58 existing passes
///     stay green. Wave E extends transfer() to honour them.
class I2cRtc : public I2cDevice {
public:
    I2cRtc();

    void reset() {
        reg_ptr_ = 0;
        addr_set_ = false;
        regs_.fill(0);
        osc_halt_ = false;
        mode_12h_ = false;
        use_real_time_ = true;
    }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    void start() override;
    uint8_t transfer(uint8_t data, bool is_read) override;
    void stop() override;

    // ══ === TEST-ONLY ACCESSORS === ═══════════════════════════════════
    //
    // Wave-E of the UART+I2C plan seeds BCD register values directly and
    // freezes the host-clock snapshot. These accessors MUST NOT be called
    // from production code paths (emulator, debugger UI). The `_dbg` / `_live`
    // naming pattern matches UartChannel's fenced block.

    /// Enable/disable the host-clock snapshot (default true).
    /// When `false`, start()/snapshot_time() do not refresh regs_[0..6],
    /// which allows Wave-E tests to seed arbitrary BCD values.
    void set_use_real_time(bool v) { use_real_time_ = v; }
    bool use_real_time() const { return use_real_time_; }

    /// Write one register directly (bypasses the I2C protocol).
    void poke_register(uint8_t addr, uint8_t val) {
        if (addr < regs_.size()) regs_[addr] = val;
    }

    /// Read one register directly (bypasses the I2C protocol).
    uint8_t peek_register(uint8_t addr) const {
        return (addr < regs_.size()) ? regs_[addr] : 0x00;
    }

    /// CH (Clock Halt) bit — bit 7 of seconds register 0x00 per DS1307 datasheet.
    bool ch_bit() const { return osc_halt_; }

    /// 12h-mode flag — bit 6 of hours register 0x02 per DS1307 datasheet.
    bool mode_12h() const { return mode_12h_; }
    // ══ === END TEST-ONLY ACCESSORS === ═══════════════════════════════

private:
    static uint8_t to_bcd(int val);
    void snapshot_time();

    uint8_t reg_ptr_ = 0;                   // current register pointer
    bool    addr_set_ = false;              // true after first write sets pointer

    // CHANGED in Phase 1: storage widened from 8 to 64 bytes so NVRAM
    // registers 0x08-0x3F are addressable per the DS1307 datasheet. Phase-1
    // behaviour: transfer() still uses the 0x07 wrap to preserve the existing
    // 58 passing rows. Wave E extends the wrap to 0x3F.
    std::array<uint8_t, 64> regs_{};        // clock/control + NVRAM (BCD)

    // Phase-1 feature-gap flags — tracked but not yet acted on by transfer().
    bool    osc_halt_      = false;         // seconds reg bit 7 (CH)
    bool    mode_12h_      = false;         // hours reg bit 6 (12/24)
    bool    use_real_time_ = true;          // false → freeze the host snapshot
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
    /// Per zxnext.vhd:3259 the read value ANDs the internal SCL with the
    /// pi_i2c1_scl input (pulled high when no Pi is attached).
    uint8_t read_scl() const;

    /// Read port 0x113B — returns SDA input state (bit 0, upper bits set).
    /// Per zxnext.vhd:3266 the read value ANDs the internal SDA with the
    /// pi_i2c1_sda input (pulled high when no Pi is attached).
    uint8_t read_sda() const;

    /// Attach a device at the given 7-bit I2C address.
    void attach_device(uint8_t address, I2cDevice* device);

    // ── Raspberry Pi I2C bus 1 inputs (pi_i2c1_scl / pi_i2c1_sda) ─────
    //
    // Per zxnext.vhd:3259, 3266: the Pi I2C bus 1 drives a second pair of
    // open-drain inputs that are AND-ed into the SCL/SDA reads. A real Pi
    // would pull these low to assert; when no Pi is attached they idle
    // high (released = 1). Defaults match the VHDL pull-up semantics: the
    // AND-gate is a no-op until tests explicitly drive them low.

    /// Drive the Pi I2C bus 1 SCL input. Default true (released, idle high).
    void set_pi_i2c1_scl(bool v) { pi_i2c1_scl_ = v; }

    /// Drive the Pi I2C bus 1 SDA input. Default true (released, idle high).
    void set_pi_i2c1_sda(bool v) { pi_i2c1_sda_ = v; }

    /// Current value of the pi_i2c1_scl input line (test-only readback).
    bool pi_i2c1_scl() const { return pi_i2c1_scl_; }

    /// Current value of the pi_i2c1_sda input line (test-only readback).
    bool pi_i2c1_sda() const { return pi_i2c1_sda_; }

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

    // Raspberry Pi I2C bus 1 inputs — AND-ed into read_scl()/read_sda()
    // per zxnext.vhd:3259, 3266. Default released (1).
    bool pi_i2c1_scl_ = true;
    bool pi_i2c1_sda_ = true;

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
