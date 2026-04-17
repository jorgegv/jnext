#pragma once
#include <cstdint>
#include <array>

/// Pure virtual interface for SPI device backends (SD card, flash, etc.).
class SpiDevice {
public:
    virtual ~SpiDevice() = default;

    /// Exchange one byte: transmit tx, return the response byte.
    virtual uint8_t exchange(uint8_t tx) = 0;

    /// Receive a byte from host (command/data write path).
    /// Returns the MISO byte the device drives during this transfer
    /// (VHDL: full-duplex SPI — every write is also a read).
    /// Default implementation calls exchange() for compatibility.
    virtual uint8_t receive(uint8_t tx) { return exchange(tx); }

    /// Send next response byte to host (read path).
    /// Default implementation calls exchange(0xFF) for compatibility.
    virtual uint8_t send() { return exchange(0xFF); }

    /// Called when chip select is deasserted (CS goes high).
    /// Devices should reset their SPI protocol state.
    virtual void deselect() {}
};

/// SPI Master — byte-level exchange interface for SD card / flash access.
///
/// Emulates the hardware SPI master at the port level:
///   Port 0xE7 — chip select (active-low CS lines)
///   Port 0xEB — data register (write starts transfer, read returns result)
///
/// The VHDL implementation (serial/spi_master.vhd) shifts 8 bits MSB-first
/// with CPOL=0, CPHA=0.  For emulation purposes the transfer is instantaneous:
/// writing to the data port immediately performs the full byte exchange via
/// the attached device backend.
///
/// VHDL reference: serial/spi_master.vhd, zxnext.vhd (port decode)
class SpiMaster {
public:
    /// Maximum number of CS lines (devices) supported.
    static constexpr int kMaxDevices = 4;

    SpiMaster();

    void reset();

    /// Attach a device backend to a chip-select ID (0-3).
    /// The SpiMaster does NOT own the device — caller manages lifetime.
    void attach_device(int cs_id, SpiDevice* device);

    // ── Port handlers ─────────────────────────────────────────────

    /// Port 0xE7 write — set chip select state.
    /// Directly stores the CS register value.  Bit 0 active-low selects
    /// the SD card (cs_id 0).
    void write_cs(uint8_t val);

    /// Port 0xE7 read — return current CS state.
    uint8_t read_cs() const;

    // ── NR 0x0A bit 5: SD-card swap ───────────────────────────────
    /// When true, inverts the SD0/SD1 decoding on port 0xE7 writes
    /// (VHDL zxnext.vhd:3308-3322 — `cpu_do_swap` XORs bits 1:0 with
    /// `nr_0a_sd_swap` for the SD-select match only).
    void set_sd_swap(bool v);
    bool sd_swap() const { return sd_swap_; }

    /// Port 0xEB write — start an SPI transfer.
    /// The byte is exchanged with the currently selected device (if any)
    /// and the received byte is stored for a subsequent read_data() call.
    void write_data(uint8_t val);

    /// Port 0xEB read — triggers a new SPI transfer with MOSI=0xFF
    /// (matching VHDL: i_spi_rd starts a transfer) and returns the result.
    uint8_t read_data();

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    uint8_t cs_ = 0xFF;          // CS register — all lines deasserted (active-low)
    uint8_t rx_data_ = 0xFF;     // last byte received from device
    bool    sd_swap_ = false;    // NR 0x0A bit 5 — invert SD0/SD1 mapping

    std::array<SpiDevice*, kMaxDevices> devices_{};  // attached backends (non-owning)

    /// Return the device for the currently active CS, or nullptr if none.
    SpiDevice* active_device() const;
};
