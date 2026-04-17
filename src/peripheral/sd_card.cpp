#include "peripheral/sd_card.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cstring>

namespace {

std::shared_ptr<spdlog::logger>& sd_log() {
    static auto logger = []() {
        auto existing = spdlog::get("sdcard");
        if (existing) return existing;
        auto l = spdlog::stderr_color_mt("sdcard");
        l->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return l;
    }();
    return logger;
}

}  // namespace

SdCardDevice::SdCardDevice() = default;

SdCardDevice::~SdCardDevice() {
    unmount();
}

bool SdCardDevice::mount(const std::string& path) {
    unmount();

    file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        // Try read-only
        file_.open(path, std::ios::in | std::ios::binary);
        if (!file_.is_open()) {
            sd_log()->error("failed to open SD image: {}", path);
            return false;
        }
        sd_log()->warn("SD image opened read-only: {}", path);
    }

    file_.seekg(0, std::ios::end);
    file_size_ = static_cast<uint64_t>(file_.tellg());
    file_.seekg(0, std::ios::beg);

    state_ = State::IDLE;
    initialized_ = false;
    app_cmd_ = false;
    cmd_idx_ = 0;

    sd_log()->info("mounted SD image: {} ({} bytes, {} MB)",
                   path, file_size_, file_size_ / (1024 * 1024));
    return true;
}

void SdCardDevice::unmount() {
    if (file_.is_open()) {
        file_.close();
        sd_log()->info("SD image unmounted");
    }
    state_ = State::IDLE;
    initialized_ = false;
    file_size_ = 0;
}

void SdCardDevice::deselect() {
    // Reset SPI protocol state on CS deassert (matches ZesarUX mmc_cs behavior).
    // The SD card goes back to idle, ready for a new command sequence.
    // This is critical: without it, after a sector read the SD card can be
    // stuck in SENDING_DATA state, causing the next command to be lost.
    state_ = State::IDLE;
    cmd_idx_ = 0;
    resp_buf_.clear();
    resp_idx_ = 0;
    data_idx_ = 0;
    data_crc_count_ = 0;
    app_cmd_ = false;
    // Note: initialized_ is NOT reset — the card stays initialized
    sd_log()->debug("CS deasserted — protocol state reset to IDLE");
}

uint8_t SdCardDevice::exchange(uint8_t tx) {
    // Legacy full-duplex exchange — not used directly by SpiMaster
    // (write_data calls receive(), read_data calls send()).
    // MISO-during-write intentionally dropped: exchange() models the
    // write-then-read as two sequential steps, not a single pipeline.
    (void)receive(tx);
    return send();
}

uint8_t SdCardDevice::receive(uint8_t tx) {
    // Write path: receive command/data bytes from the host.
    // Returns 0xFF — in real hardware the SD card holds MISO high during
    // command reception (VHDL: miso_dat captures whatever is on the wire,
    // which is 0xFF when the card isn't actively responding).
    if (!file_.is_open()) return 0xFF;

    switch (state_) {
        case State::IDLE:
            if ((tx & 0xC0) == 0x40) {
                cmd_buf_[0] = tx;
                cmd_idx_ = 1;
                state_ = State::RECEIVING_CMD;
            }
            break;

        case State::RECEIVING_CMD:
            cmd_buf_[cmd_idx_++] = tx;
            if (cmd_idx_ >= 6) {
                process_command();
            }
            break;

        case State::RECEIVING_DATA:
            if (tx == 0xFE && data_idx_ == 0 && data_crc_count_ == 0) {
                // Data token received — start collecting data
                break;
            }
            if (data_idx_ < 512) {
                data_block_[data_idx_++] = tx;
                break;
            }
            // Collecting 2 CRC bytes (ignored)
            data_crc_count_++;
            if (data_crc_count_ >= 2) {
                uint32_t sector = cmd_arg();
                uint64_t byte_addr = static_cast<uint64_t>(sector) * 512;
                if (byte_addr + 512 <= file_size_) {
                    file_.seekp(static_cast<std::streamoff>(byte_addr), std::ios::beg);
                    file_.write(reinterpret_cast<const char*>(data_block_), 512);
                    file_.flush();
                    sd_log()->debug("CMD24 wrote 512 bytes at sector {} (byte={:#010x})", sector, byte_addr);
                }
                state_ = State::WRITE_RESP;
                resp_buf_ = { 0x05 };
                resp_idx_ = 0;
            }
            break;

        default:
            // In RESPONDING, SENDING_DATA, WRITE_RESP states:
            // If host sends a new command start byte (0x40|cmd), abort
            // current response and start receiving the new command.
            // This matches real SD behavior and ZesarUX (where write path
            // always processes command bytes regardless of read state).
            if ((tx & 0xC0) == 0x40) {
                cmd_buf_[0] = tx;
                cmd_idx_ = 1;
                state_ = State::RECEIVING_CMD;
                resp_buf_.clear();
                resp_idx_ = 0;
                data_idx_ = 0;
                data_crc_count_ = 0;
            }
            break;
    }
    return 0xFF;
}

uint8_t SdCardDevice::send() {
    // Read path: send the next response byte to the host.
    // This produces the sequential response matching ZesarUX's mmc_read().
    if (!file_.is_open()) return 0xFF;

    switch (state_) {
        case State::IDLE:
            return 0xFF;

        case State::RECEIVING_CMD:
            // Command not fully received yet
            return 0xFF;

        case State::RESPONDING:
            if (resp_idx_ < resp_buf_.size()) {
                return resp_buf_[resp_idx_++];
            }
            state_ = State::IDLE;
            return 0xFF;

        case State::SENDING_DATA:
            if (resp_idx_ < resp_buf_.size()) {
                return resp_buf_[resp_idx_++];
            }
            if (data_idx_ < 512) {
                return data_block_[data_idx_++];
            }
            if (data_crc_count_ < 2) {
                data_crc_count_++;
                return 0x00;
            }
            state_ = State::IDLE;
            return 0xFF;

        case State::RECEIVING_DATA:
            return 0xFF;

        case State::WRITE_RESP:
            if (resp_idx_ < resp_buf_.size()) {
                return resp_buf_[resp_idx_++];
            }
            state_ = State::IDLE;
            return 0xFF;
    }

    return 0xFF;
}

void SdCardDevice::process_command() {
    uint8_t cmd = cmd_buf_[0] & 0x3F;

    if (app_cmd_) {
        app_cmd_ = false;
        if (cmd == 41) {
            acmd41_sd_send_op_cond();
            return;
        }
        // Unknown ACMD — treat as illegal, return R1 with illegal command bit
        sd_log()->warn("unknown ACMD{} arg={:#010x}", cmd, cmd_arg());
        queue_r1(0x05);  // idle + illegal command
        return;
    }

    switch (cmd) {
        case 0:  cmd0_go_idle(); break;
        case 1:  cmd1_send_op_cond(); break;
        case 8:  cmd8_send_if_cond(); break;
        case 12: cmd12_stop_transmission(); break;
        case 17: cmd17_read_single_block(); break;
        case 24: cmd24_write_single_block(); break;
        case 55: cmd55_app_cmd(); break;
        case 58: cmd58_read_ocr(); break;
        default:
            sd_log()->warn("unhandled CMD{} arg={:#010x}", cmd, cmd_arg());
            queue_r1(initialized_ ? 0x00 : 0x01);
            break;
    }
}

void SdCardDevice::cmd1_send_op_cond() {
    sd_log()->debug("CMD1 SEND_OP_COND → card initialized, ready");
    initialized_ = true;
    queue_r1(0x00);  // R1: ready (not idle)
}

void SdCardDevice::cmd0_go_idle() {
    sd_log()->debug("CMD0 GO_IDLE_STATE → card reset");
    initialized_ = false;
    queue_r1(0x01);  // R1: in idle state
}

void SdCardDevice::cmd8_send_if_cond() {
    // CMD8: arg has voltage range + check pattern
    uint8_t check = cmd_buf_[4];  // echo back check pattern
    sd_log()->debug("CMD8 SEND_IF_COND check={:#04x} → voltage accepted", check);
    resp_buf_ = { 0xFF, 0x01, 0x00, 0x00, 0x01, check };  // NCR + R7: idle, voltage accepted
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::cmd12_stop_transmission() {
    sd_log()->debug("CMD12 STOP_TRANSMISSION");
    // Abort any in-progress multi-block transfer and return to idle.
    data_idx_ = 0;
    data_crc_count_ = 0;
    // CMD12 has "stuff bytes" before R1 — the firmware reads 8 stuff bytes
    // before polling for R1 (see TBBLUE.FW code at 0x7AB0).
    // Provide 8 stuff bytes (0xFF) + NCR (0xFF) + R1 to ensure the R1 poll
    // after the stuff bytes finds the actual R1 response.
    uint8_t r1 = initialized_ ? 0x00 : 0x01;
    resp_buf_ = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 8 stuff bytes
                  0xFF, r1 };  // NCR + R1
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::cmd17_read_single_block() {
    uint32_t sector = cmd_arg();
    // SDHC (CCS=1): argument is sector number, not byte address
    uint64_t byte_addr = static_cast<uint64_t>(sector) * 512;
    sd_log()->debug("CMD17 READ_SINGLE_BLOCK sector={} (byte={:#010x})", sector, byte_addr);

    if (!initialized_) {
        queue_r1(0x01);  // idle state
        return;
    }

    if (byte_addr + 512 > file_size_) {
        sd_log()->warn("CMD17 read past end of image: sector={} byte={:#010x} size={}", sector, byte_addr, file_size_);
        queue_r1(0x00);
        // Send error token instead of data
        resp_buf_.push_back(0x08);  // out of range error
        state_ = State::RESPONDING;
        return;
    }

    file_.seekg(static_cast<std::streamoff>(byte_addr), std::ios::beg);
    file_.read(reinterpret_cast<char*>(data_block_), 512);

    // Response: NCR + R1 (0x00 = OK), then 0xFE data start token, then 512 bytes
    resp_buf_ = { 0xFF, 0x00, 0xFE };
    resp_idx_ = 0;
    data_idx_ = 0;
    data_crc_count_ = 0;
    state_ = State::SENDING_DATA;
}

void SdCardDevice::cmd24_write_single_block() {
    uint32_t sector = cmd_arg();
    sd_log()->debug("CMD24 WRITE_SINGLE_BLOCK sector={} (byte={:#010x})",
                    sector, static_cast<uint64_t>(sector) * 512);

    if (!initialized_) {
        queue_r1(0x01);
        return;
    }

    // Send R1, then wait for host to send 0xFE + 512 bytes + 2 CRC
    queue_r1(0x00);
    data_idx_ = 0;
    data_crc_count_ = 0;
    state_ = State::RECEIVING_DATA;
}

void SdCardDevice::cmd55_app_cmd() {
    sd_log()->debug("CMD55 APP_CMD (next command is ACMD)");
    app_cmd_ = true;
    queue_r1(initialized_ ? 0x00 : 0x01);
}

void SdCardDevice::cmd58_read_ocr() {
    sd_log()->debug("CMD58 READ_OCR → initialized={} SDHC=1", initialized_);
    // OCR: bit 31=power up complete (if initialized), bit 30=SDHC
    uint8_t ocr0 = initialized_ ? 0xC0 : 0x00;  // CCS=1 (SDHC), power up status
    resp_buf_ = { 0xFF, static_cast<uint8_t>(initialized_ ? 0x00 : 0x01),
                  ocr0, 0xFF, 0x80, 0x00 };  // NCR + R3
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::acmd41_sd_send_op_cond() {
    sd_log()->debug("ACMD41 SD_SEND_OP_COND → card initialized, ready");
    initialized_ = true;  // Card is now initialized
    queue_r1(0x00);  // R1: ready (not idle)
}

uint32_t SdCardDevice::cmd_arg() const {
    return (static_cast<uint32_t>(cmd_buf_[1]) << 24) |
           (static_cast<uint32_t>(cmd_buf_[2]) << 16) |
           (static_cast<uint32_t>(cmd_buf_[3]) << 8)  |
           static_cast<uint32_t>(cmd_buf_[4]);
}

void SdCardDevice::queue_r1(uint8_t r1) {
    // Prepend one NCR byte (0xFF) before R1, matching ZesarUX mmc_read()
    // behavior where index 0 = 0xFF (busy/NCR) and index 1 = R1.
    resp_buf_ = { 0xFF, r1 };
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}
