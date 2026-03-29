#include "peripheral/spi.h"

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
    if (val != cs_) {
        spi_log()->debug("CS change {:#04x} → {:#04x} (SD card {})",
                         cs_, val, (val & 0x01) ? "deselected" : "SELECTED");
    }
    cs_ = val;
}

uint8_t SpiMaster::read_cs() const {
    return cs_;
}

void SpiMaster::write_data(uint8_t val) {
    SpiDevice* dev = active_device();
    if (dev) {
        rx_data_ = dev->exchange(val);
        spi_log()->debug("exchange tx={:#04x} rx={:#04x}", val, rx_data_);
    } else {
        // No device selected — MISO floats high (all ones)
        rx_data_ = 0xFF;
        spi_log()->debug("exchange tx={:#04x} (no device) rx=0xFF", val);
    }
}

uint8_t SpiMaster::read_data() const {
    return rx_data_;
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
