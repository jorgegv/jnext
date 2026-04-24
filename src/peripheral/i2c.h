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
/// I2C address 0x68.  Supports the full DS1307 register map:
///   0x00-0x06 time/date (BCD: seconds, minutes, hours, day-of-week, date,
///             month, year)
///   0x07      control register
///   0x08-0x3F NVRAM (56 bytes)
///
/// Address pointer auto-increments on each byte transfer and wraps
/// 0x3F → 0x00 (DS1307 datasheet §Address Autoincrement).
///
/// In the default configuration `use_real_time_` is true and calls to
/// `snapshot_time()` refresh regs_[0..6] with the host time.  Setting
/// `use_real_time_` to false (tests or deterministic replay) freezes
/// the clock registers so writes can be read back verbatim. `osc_halt_`
/// (seconds bit 7, CH bit) also freezes the clock when set, per the
/// DS1307 datasheet; `mode_12h_` tracks hours-reg bit 6.
///
/// Phase-1 scaffold (main) added the 64-byte storage, CH / 12h / use_real_time
/// flags, and poke/peek test accessors. Wave E activates them in `transfer()`
/// (write-path, pointer wrap at 0x3F, 12h mode honouring, CH freeze).
class I2cRtc : public I2cDevice {
public:
    I2cRtc();

    void reset();

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    void start() override;
    uint8_t transfer(uint8_t data, bool is_read) override;
    void stop() override;

    // ══ === TEST-ONLY ACCESSORS === ═══════════════════════════════════
    //
    // Wave-E of the UART+I2C plan seeds BCD register values directly and
    // freezes the host-clock snapshot. These accessors MUST NOT be called
    // from production code paths (emulator, debugger UI).

    /// Enable/disable the host-clock snapshot (default true).
    /// When `false`, start()/snapshot_time() do not refresh regs_[0..6],
    /// which allows Wave-E tests to seed arbitrary BCD values.
    void set_use_real_time(bool v) { use_real_time_ = v; }
    bool use_real_time() const { return use_real_time_; }

    /// Write one register directly (bypasses the I2C protocol).
    void poke_register(uint8_t addr, uint8_t val) { regs_[addr & 0x3F] = val; }

    /// Read one register directly (bypasses the I2C protocol).
    uint8_t peek_register(uint8_t addr) const { return regs_[addr & 0x3F]; }

    /// CH (Clock Halt) bit — bit 7 of seconds register 0x00 per DS1307 datasheet.
    bool ch_bit() const { return osc_halt_; }

    /// 12h-mode flag — bit 6 of hours register 0x02 per DS1307 datasheet.
    bool mode_12h() const { return mode_12h_; }
    // ══ === END TEST-ONLY ACCESSORS === ═══════════════════════════════

private:
    static uint8_t to_bcd(int val);
    void snapshot_time();

    uint8_t reg_ptr_  = 0;              // current register pointer (0x00-0x3F)
    bool    addr_set_ = false;          // true after first write sets register pointer
    bool    osc_halt_ = false;          // CH bit (reg 0x00 bit 7); when set the
                                        // time registers are frozen
    bool    mode_12h_ = false;          // reg 0x02 bit 6 — 12-hour mode
    bool    use_real_time_ = true;      // if false, snapshot_time() is a no-op
    std::array<uint8_t, 64> regs_{};    // full DS1307 register map + NVRAM (BCD
                                        // time at 0x00..0x06, ctrl at 0x07,
                                        // NVRAM 0x08..0x3F)
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

    /// Drive the Raspberry Pi I2C bridge inputs (zxnext.vhd:3259/3266).
    /// Both signals are open-drain / wired-AND with the local SCL/SDA
    /// outputs; pulling either line low on the Pi side forces the
    /// corresponding CPU read to 0.  Default state is high (released).
    void set_pi_i2c1(bool scl, bool sda) {
        pi_i2c1_scl_ = scl ? 1 : 0;
        pi_i2c1_sda_ = sda ? 1 : 0;
    }

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

    // Raspberry Pi I2C bridge inputs (zxnext.vhd:3259/3266). Default
    // released high; set to 0 to pull the corresponding line low on
    // the read path only (AND-gate).
    uint8_t pi_i2c1_scl_ = 1;
    uint8_t pi_i2c1_sda_ = 1;

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
