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
    uint8_t decoded;
    if ((val & 0x03) == 0x02) {
        // SD card 0 (bit 1 clear, bit 0 set — ignoring swap for now)
        decoded = 0xFE;  // only bit 0 low
    } else if ((val & 0x03) == 0x01) {
        // SD card 1 (bit 0 clear, bit 1 set — ignoring swap for now)
        decoded = 0xFD;  // only bit 1 low
    } else if (val == 0xFB) {
        decoded = 0xFB;  // RPI0
    } else if (val == 0xF7) {
        decoded = 0xF7;  // RPI1
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
    // Write path: feed a command/data byte TO the device.
    // Does NOT consume response bytes (matches ZesarUX/CSpect model
    // where write and read paths are independent).
    SpiDevice* dev = active_device();
    if (dev) {
        dev->receive(val);
        spi_log()->debug("write tx={:#04x}", val);
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
}

void SpiMaster::load_state(StateReader& r)
{
    cs_      = r.read_u8();
    rx_data_ = r.read_u8();
}
