#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include "peripheral/spi.h"

/// SD card SPI-mode emulation backend.
///
/// Implements the SpiDevice interface to emulate an SD card in SPI mode.
/// Supports CMD0 (GO_IDLE), CMD8 (SEND_IF_COND), CMD17 (READ_SINGLE_BLOCK),
/// CMD24 (WRITE_SINGLE_BLOCK), CMD55+ACMD41 (SD_SEND_OP_COND), and CMD58
/// (READ_OCR).  This is sufficient for NextZXOS / esxdos SD card access.
///
/// The backing store is an `.img` file (raw disk image) opened for read/write.
///
/// SPI SD protocol:
///   Host sends command (6 bytes: 0x40|cmd, arg[3:0], crc)
///   Card responds with R1 (1 byte), then optional data.
///   CMD17: R1, then 0xFE token, then 512 bytes data, then 2 CRC bytes.
///   CMD24: R1, host sends 0xFE token, 512 bytes data, 2 CRC bytes,
///          card responds with data response token.
class SdCardDevice : public SpiDevice {
public:
    SdCardDevice();
    ~SdCardDevice() override;

    /// Mount a disk image file.  Returns true on success.
    bool mount(const std::string& path);

    /// Unmount the current image (if any).
    void unmount();

    /// Reset SPI protocol state (keeps mounted image).
    void reset() {
        state_ = State::IDLE;
        cmd_idx_ = 0;
        resp_buf_.clear();
        resp_idx_ = 0;
        data_idx_ = 0;
        data_crc_count_ = 0;
        initialized_ = false;
        app_cmd_ = false;
    }

    /// Returns true if an image is mounted.
    bool mounted() const { return file_.is_open(); }

    /// SpiDevice interface — exchange one byte (legacy, used by base class defaults).
    uint8_t exchange(uint8_t tx) override;

    /// Receive a command/data byte from host (write path).
    void receive(uint8_t tx) override;

    /// Send next response byte to host (read path).
    uint8_t send() override;

    /// Called when CS is deasserted — reset SPI protocol state.
    void deselect() override;

private:
    // SD card state machine
    enum class State {
        IDLE,           // Waiting for command start byte (0x40 | cmd)
        RECEIVING_CMD,  // Collecting command bytes
        RESPONDING,     // Sending response bytes
        SENDING_DATA,   // Sending data block (CMD17)
        RECEIVING_DATA, // Receiving data block from host (CMD24)
        WRITE_RESP,     // Sending write response token
    };

    State state_ = State::IDLE;

    // Command buffer (6 bytes: cmd, arg×4, crc)
    uint8_t cmd_buf_[6] = {};
    int cmd_idx_ = 0;

    // Response buffer
    std::vector<uint8_t> resp_buf_;
    size_t resp_idx_ = 0;

    // Data block for CMD17 read / CMD24 write
    uint8_t data_block_[512] = {};
    int data_idx_ = 0;
    int data_crc_count_ = 0;  // CRC bytes remaining for CMD24

    // SD card state
    bool initialized_ = false;   // After ACMD41 completes
    bool app_cmd_ = false;       // Next command is ACMD (preceded by CMD55)

    // Backing store
    std::fstream file_;
    uint64_t file_size_ = 0;

    // Command processing
    void process_command();
    void cmd0_go_idle();
    void cmd1_send_op_cond();
    void cmd8_send_if_cond();
    void cmd12_stop_transmission();
    void cmd17_read_single_block();
    void cmd24_write_single_block();
    void cmd55_app_cmd();
    void cmd58_read_ocr();
    void acmd41_sd_send_op_cond();

    // Helper: compute 32-bit block address from command argument bytes
    uint32_t cmd_arg() const;

    // Queue an R1 response byte
    void queue_r1(uint8_t r1);
};
