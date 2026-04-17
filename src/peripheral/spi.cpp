#include "peripheral/spi.h"
#include "core/saveable.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace {

std::shared_ptr<spdlog::logger>& spi_log() {
    static auto logger = []() {
        auto existing = spdlog::get("spi");
        if (existing) return existing;
        auto l = spdlog::stderr_color_mt("spi");
        l->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return l;
    }();
    return logger;
}

}  // namespace

SpiMaster::SpiMaster() {
    reset();
}

void SpiMaster::reset() {
    cs_ = 0xFF;       // all CS lines deasserted (active-low)
    rx_data_ = 0xFF;  // VHDL resets miso_dat to all ones
    devices_.fill(nullptr);
    // NB: sd_swap_ is NOT cleared on soft reset. It tracks NR 0x0A bit 5,
    // which in VHDL lives in the NextREG register file and is driven by
    // software writes, not by the spi-master/port_e7 reset path
    // (zxnext.vhd:3308-3322 only resets port_e7_reg on `reset`).
}

void SpiMaster::set_sd_swap(bool v) {
    if (sd_swap_ != v) {
        spi_log()->debug("sd_swap={}", v ? 1 : 0);
    }
    sd_swap_ = v;
}

void SpiMaster::attach_device(int cs_id, SpiDevice* device) {
    if (cs_id < 0 || cs_id >= kMaxDevices) {
        spi_log()->warn("attach_device: invalid cs_id {}", cs_id);
        return;
    }
    devices_[cs_id] = device;
    spi_log()->info("device attached on CS {}", cs_id);
}

void SpiMaster::write_cs(uint8_t val) {
    // VHDL zxnext.vhd:3311-3322 decodes the CPU byte into a recognized
    // CS pattern. Unrecognized values collapse to 0xFF (all deselected).
    //
    // NR 0x0A bit 5 (sd_swap) inverts the SD0/SD1 mapping. In VHDL:
    //   cpu_do(1 downto 0) = "10"  → "111111" & not sd_swap & sd_swap
    //   cpu_do(1 downto 0) = "01"  → "111111" & sd_swap & not sd_swap
    // so the decode still *matches* the raw cpu_do bits (no swap on the
    // match), but the stored CS pattern flips when sd_swap=1. RPI/Flash/
    // else branches all compare against the raw cpu_do — sd_swap is SD-only.
    uint8_t decoded;
    if ((val & 0x03) == 0x02) {
        // SD card select: raw bits 1:0 = "10" → SD0, or SD1 when swapped.
        decoded = sd_swap_ ? 0xFD : 0xFE;
    } else if ((val & 0x03) == 0x01) {
        // SD card select: raw bits 1:0 = "01" → SD1, or SD0 when swapped.
        decoded = sd_swap_ ? 0xFE : 0xFD;
    } else if (val == 0xFB) {
        decoded = 0xFB;  // RPI0 — not affected by sd_swap
    } else if (val == 0xF7) {
        decoded = 0xF7;  // RPI1 — not affected by sd_swap
    // 0x7F = Flash select, but gated by config mode (zxnext.vhd:3319).
    // Config mode is not modelled at this level, so Flash select is not
    // recognized here. If needed, the caller can set cs_ directly.
    } else {
        decoded = 0xFF;  // all deselected
    }

    if (decoded != cs_) {
        spi_log()->debug("CS change {:#04x} → {:#04x} (raw={:#04x}, SD card {})",
                         cs_, decoded, val, (decoded & 0x01) ? "deselected" : "SELECTED");
        // Notify devices that lost chip select (CS went from low to high)
        for (int i = 0; i < kMaxDevices; ++i) {
            bool was_selected = !(cs_ & (1 << i));
            bool now_selected = !(decoded & (1 << i));
            if (was_selected && !now_selected && devices_[i]) {
                devices_[i]->deselect();
            }
        }
    }
    cs_ = decoded;
}

uint8_t SpiMaster::read_cs() const {
    return cs_;
}

void SpiMaster::write_data(uint8_t val) {
    // VHDL spi_master.vhd:82,111-112: writes trigger a full-duplex SPI
    // exchange — MOSI sends val, MISO is sampled, and miso_dat is updated
    // at state_last_d. Capture the device's MISO byte in rx_data_ so that
    // a subsequent read_data() returns it (pipeline delay).
    SpiDevice* dev = active_device();
    if (dev) {
        rx_data_ = dev->receive(val);
        spi_log()->debug("write tx={:#04x}, rx={:#04x}", val, rx_data_);
    }
}

uint8_t SpiMaster::read_data() {
    // VHDL spi_master.vhd:162-166: miso_dat is latched from the input
    // shift register at state_last_d (one cycle AFTER the transfer ends).
    // This means a read returns the result of the PREVIOUS transfer, not
    // the one just started. The new transfer's result is stored for the
    // next read.
    uint8_t prev = rx_data_;
    SpiDevice* dev = active_device();
    if (dev) {
        rx_data_ = dev->send();
        spi_log()->debug("read → returning prev={:#04x}, new rx={:#04x}",
                         prev, rx_data_);
    }
    // No device → rx_data_ unchanged (VHDL: miso_dat only updates on
    // state_last_d, which requires an active transfer).
    return prev;
}

SpiDevice* SpiMaster::active_device() const {
    // CS lines are active-low: bit 0 low = device 0 selected, etc.
    for (int i = 0; i < kMaxDevices; ++i) {
        if (!(cs_ & (1 << i)) && devices_[i]) {
            return devices_[i];
        }
    }
    return nullptr;
}

void SpiMaster::save_state(StateWriter& w) const
{
    w.write_u8(cs_);
    w.write_u8(rx_data_);
    // sd_swap_ appended at end. The save/load pair feeds the in-process
    // rewind ring buffer (size measured per-run in Emulator::init), so
    // cross-build snapshot compatibility is not a concern here — every
    // run writes and reads using the same layout.
    w.write_bool(sd_swap_);
}

void SpiMaster::load_state(StateReader& r)
{
    cs_      = r.read_u8();
    rx_data_ = r.read_u8();
    sd_swap_ = r.read_bool();
}
