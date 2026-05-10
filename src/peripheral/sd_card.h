#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include "peripheral/spi.h"

/// SD card SPI-mode emulation backend.
///
/// Implements the SpiDevice interface to emulate an SD card in SPI mode.
/// Supports CMD0 (GO_IDLE), CMD8 (SEND_IF_COND), CMD12 (STOP_TRANSMISSION),
/// CMD17 (READ_SINGLE_BLOCK), CMD18 (READ_MULTIPLE_BLOCK),
/// CMD24 (WRITE_SINGLE_BLOCK), CMD55+ACMD41 (SD_SEND_OP_COND), and CMD58
/// (READ_OCR).  This is sufficient for NextZXOS / esxdos SD card access and
/// the tbblue.fw firmware boot path which uses CMD18 to stream /TBBLUE.FW.
///
/// The backing store is an `.img` file (raw disk image) opened for read/write.
///
/// SPI SD protocol:
///   Host sends command (6 bytes: 0x40|cmd, arg[3:0], crc)
///   Card responds with R1 (1 byte), then optional data.
///   CMD17: R1, then 0xFE token, then 512 bytes data, then 2 CRC bytes.
///   CMD18: first block uses the same shape as CMD17 (NCR + R1 + 0xFE
///          + 512 + CRC); subsequent blocks skip NCR/R1 and stream only
///          0xFE + 512 + CRC after each CRC-byte finishes.  Continues
///          until host issues CMD12 STOP_TRANSMISSION or deasserts CS.
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
        data_token_received_ = false;
        initialized_ = false;
        app_cmd_ = false;
        multi_block_ = false;
        multi_block_sector_ = 0;
        pending_write_after_r1_ = false;
        persistent_response_byte_ = 0xFF;
    }

    /// Returns true if an image is mounted.
    bool mounted() const { return file_.is_open(); }

    /// SpiDevice interface — exchange one byte (legacy, used by base class defaults).
    uint8_t exchange(uint8_t tx) override;

    /// Receive a command/data byte from host (write path).
    /// Returns 0xFF (MISO idle) — the card doesn't respond on MISO during
    /// command reception in real hardware.
    uint8_t receive(uint8_t tx) override;

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
    // V12-DIVMMC-06 (Pass-12 reviewer fix, 2026-05-10): explicit
    // "data token already seen" flag for the CMD24 RECEIVING_DATA
    // state. Per SD Phys Layer Spec § 7.3.3.2 the card waits for the
    // 0xFE start-of-block token; pre-token bytes (incl. but not
    // limited to 0xFF gap bytes) must be ignored. The previous
    // (Pass-4) implementation reused `data_idx_==0 && data_crc_count_==0`
    // to detect "before token", which mishandled non-0xFF pre-token
    // bytes. This explicit flag is reset on every CMD24 dispatch
    // (process_command), reset(), and deselect().
    bool data_token_received_ = false;

    // SD card state
    bool initialized_ = false;   // After ACMD41 completes
    bool app_cmd_ = false;       // Next command is ACMD (preceded by CMD55)

    // CMD18 multi-block read: true between CMD18 and CMD12/CS-deassert.
    // When a block's CRC finishes inside send(), we re-prime data_block_ from
    // multi_block_sector_ and emit another 0xFE+data+CRC block instead of
    // going IDLE.
    bool     multi_block_        = false;
    uint32_t multi_block_sector_ = 0;  // next sector to send after current block

    // CMD24 R1-then-data bridge: cmd24_write_single_block() sets this so
    // that send() can transition RESPONDING → RECEIVING_DATA only AFTER
    // the queued R1 byte (and its NCR) has actually been emitted on MISO.
    // Without it, the previous code clobbered State::RESPONDING with
    // RECEIVING_DATA before send() ran, hanging FatFs's send_cmd which
    // polls for the first non-0xFF byte.
    bool pending_write_after_r1_ = false;

    // ZEsarUX-style persistent response byte (G46(b) 2026-05-07).
    // ZEsarUX's mmc_read() switch (storage/mmc.c:846) returns a fixed value
    // for SOME commands on EVERY read while last_command stays unchanged —
    // notably CMD0 returns $01 forever, CMD8 returns $00 forever, CMD12
    // returns $01 forever. Our previous implementation queued only NCR+R1
    // (= 2 bytes) and then transitioned to IDLE returning $FF on subsequent
    // reads. This caused the NextZXOS firmware to read garbage R7 bytes
    // for CMD8 (firmware reads R1 + 4 voltage bytes; bytes 2-5 came back
    // as $FF from our IDLE state — voltage echo mismatch → CMD8 retried
    // forever).
    //
    // `persistent_response_byte_` is the fallback byte for IDLE state.
    // CMD handlers set it post-response so subsequent reads produce the
    // correct ZEsarUX-faithful sustained byte. Reset to $FF on:
    //  - receive() of a new command start byte
    //  - deselect()
    //  - mount() / reset()
    uint8_t persistent_response_byte_ = 0xFF;

    // Backing store
    std::fstream file_;
    uint64_t file_size_ = 0;

    // NOTE: SdCardDevice is intentionally NOT a Saveable.  The rewind
    // snapshot ring currently skips the SD back end.  If this class is
    // ever serialised, the CMD18 stream state (multi_block_,
    // multi_block_sector_, plus state_/resp_buf_/resp_idx_/data_idx_/
    // data_crc_count_/data_block_ for mid-block snapshots) must be
    // included so rewinding mid-stream doesn't corrupt the host view.

    // Command processing
    void process_command();
    void cmd0_go_idle();
    void cmd1_send_op_cond();
    void cmd8_send_if_cond();
    void cmd12_stop_transmission();
    void cmd13_send_status();
    void cmd16_set_blocklen();
    void cmd17_read_single_block();
    void cmd18_read_multiple_block();
    void cmd23_set_block_count();
    void cmd24_write_single_block();
    void cmd55_app_cmd();
    void cmd58_read_ocr();
    void cmd9_send_csd();
    void cmd10_send_cid();
    void acmd41_sd_send_op_cond();

    // Helper: compute 32-bit block address from command argument bytes
    uint32_t cmd_arg() const;

    // Queue an R1 response byte
    void queue_r1(uint8_t r1);
};
