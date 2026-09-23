#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
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
/// The card serves BOTH capacity classes, decided by what the host negotiated
/// (GH #94). ACMD41's HCS bit latches `host_supports_sdhc_`, CMD58 reports it
/// back as the OCR's CCS bit, and everything downstream follows from that one
/// bit: CMD17/18/24 take a 512-byte BLOCK address when CCS=1 and a BYTE
/// address when CCS=0 (SD Phys Layer Simplified Spec § 4.7.4), CMD9 answers
/// with a CSD Version 2.0 or Version 1.0 register respectively (§ 5.3.3 /
/// § 5.3.2), and CMD16 SET_BLOCKLEN changes the transfer length only on the
/// Standard Capacity card (§ 4.3.2). TBBlue / NextZXOS / FatFs always
/// negotiate HCS=1, so the block-addressed path is the only one the boot
/// sequence takes.
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
    using ReadOverlay = std::function<bool(uint32_t sector, uint8_t* dst)>;

    SdCardDevice();
    ~SdCardDevice() override;

    /// Mount a disk image file.  Returns true on success.
    /// Mount an SD image.
    ///
    /// @param read_only  when true the host file is opened read-only on
    ///   purpose, so the emulated machine sees a write-protected card:
    ///   CMD24 writes fail the host fstream and the card emits the 0x0D
    ///   "data rejected due to write error" token, exactly as it already
    ///   did for the accidental read-only fallback below. This is what
    ///   `--sdcard-readonly` gives a user who needs a run not to mutate a
    ///   shared image (GH #77).
    bool mount(const std::string& path, bool read_only = false);

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
        multi_block_addr_ = 0;
        pending_write_after_r1_ = false;
        write_busy_pending_ = false;
        busy_remaining_ = 0;
        persistent_response_byte_ = 0xFF;
        host_supports_sdhc_ = false;  // V17-DIVMMC-01
        // GH #94 — power-up block length is the card's own physical block,
        // 2^READ_BL_LEN = 512 (SD Phys Layer Simplified Spec § 4.3.2 and the
        // READ_BL_LEN field of both CSD versions, § 5.3.2 / § 5.3.3).
        block_len_ = kBlockLen;
    }

    /// Recreate the SD-card state inherited by a program launched through
    /// NextZXOS's NEX loader. Direct loading bypasses the OS handshake, but
    /// a NEX is entitled to find the already-mounted card initialized and
    /// using SDHC block addressing when execution begins.
    void prepare_for_direct_nex() {
        reset();
        initialized_ = mounted();
        host_supports_sdhc_ = mounted();
    }

    /// Overlay a reserved SDHC sector range with a read-only host source.
    /// Normal sectors continue to use the mounted image; writes into the
    /// overlay remain rejected by the ordinary range/write checks.
    void set_read_overlay(uint32_t first_sector, uint32_t sector_count,
                          ReadOverlay reader) {
        overlay_first_sector_ = first_sector;
        overlay_sector_count_ = sector_count;
        read_overlay_ = std::move(reader);
    }
    void clear_read_overlay() {
        overlay_first_sector_ = 0;
        overlay_sector_count_ = 0;
        read_overlay_ = {};
    }
    bool has_read_overlay() const {
        return static_cast<bool>(read_overlay_);
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
        WRITE_BUSY,     // Card busy programming an accepted block (SD spec 7.3.3.1)
    };

    // SD Phys Layer Simplified Spec § 7.3.3.1 post-write busy window. See the
    // State::WRITE_BUSY case in send_impl() for why this is the SHORTEST legal
    // busy phase rather than a modelled programming time.
    // Full 0x00 busy bytes emitted before the partial release byte. ONE is
    // enough for the firmware to observe the busy phase; jnext models the rest
    // of the SD path with zero access latency, so a realistic multi-millisecond
    // programming time would be inconsistent with that posture and would slow
    // every single sector write. The spec requires the busy phase to EXIST and
    // to END; its duration is a card property, not a protocol constant.
    static constexpr std::uint8_t kWriteBusyBytes = 1;

    // The byte the host samples as the card stops signalling busy.
    //
    // WHAT THE SPEC REQUIRES (§ 7.3.3.1): that a busy phase exists after an
    // accepted block and that it ends. The spec describes this at DAT0/bit
    // level, NOT in terms of SPI byte framing, so it does not name a byte
    // value here.
    //
    // WHAT IS AN INFERENCE (mine, not the spec's): the release is asynchronous
    // to the host's byte boundary, so the byte spanning it carries leading low
    // bits and trailing high bits — 0x01/0x03/.../0x7F. That pattern is a
    // physical argument, not a quotation.
    //
    // WHAT ACTUALLY BINDS, and is confirmed by the firmware: the byte must be
    // neither 0x00 (esxdos reads that as "still busy", $1FBD) nor 0xFF (its
    // $1F3D skip-filler primitive reads that as "nothing yet"). 0x01 is the
    // smallest value satisfying that.
    static constexpr std::uint8_t kBusyReleaseByte = 0x01;
    bool write_busy_pending_ = false;   // set when CMD24 ACCEPTED the block
    std::uint8_t busy_remaining_ = 0;

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
    // Task 26 item 3 — CRC-16 (CCITT poly 0x1021, init 0x0000) computed
    // over data_block_ when a read block (CMD9/10/17/18) is loaded; emitted
    // high-byte-first as the 2 CRC bytes after the 512 (or 16) data bytes.
    uint16_t data_crc_ = 0;
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

    // V17-DIVMMC-01 (Pass-17 verify-audit, 2026-05-10): SD Phys Layer
    // Simplified Spec § 4.2.3 / § 5.1: ACMD41's argument bit 30 is the
    // Host Capacity Support flag (HCS). When HCS=1 the host indicates it
    // supports SDHC/SDXC (the card's CCS bit in CMD58's OCR can be 1).
    // When HCS=0 the host only supports SDSC; an SDHC card must respond
    // with CCS=0 in OCR (treats card as standard capacity).
    //
    // Pre-fix the emulator IGNORED the HCS bit and unconditionally set
    // CCS=1 in CMD58 — diverging from spec for any host that issues
    // ACMD41 with HCS=0. TBBlue / NextZXOS / FatFs always set HCS=1
    // (verified in the firmware boot trace), so the divergence is
    // class-(c) latent on the boot path. The fix tracks the most-recent
    // HCS bit and reflects it in the CMD58 OCR's CCS field.
    bool host_supports_sdhc_ = false;

    // GH #94 — the card's physical block: 2^READ_BL_LEN with READ_BL_LEN = 9,
    // declared by both CSD versions this card synthesises. It is the unit the
    // SDHC block address counts in, the boundary a Standard Capacity transfer
    // may not cross (READ_BLK_MISALIGN = 0), and the only length CMD24 will
    // write (WRITE_BL_PARTIAL = 0).
    static constexpr uint32_t kBlockLen = 512;

    // GH #94 — CMD16 SET_BLOCKLEN state (SD Phys Layer Simplified Spec
    // § 4.3.2). On a Standard Capacity card CMD16 sets the length of every
    // subsequent block transfer; on a High Capacity card the length is fixed
    // at 512 and CMD16 "does not affect the memory read and write commands",
    // so this stays kBlockLen for the whole SDHC life of the card.
    uint32_t block_len_ = kBlockLen;

    // CMD18 multi-block read: true between CMD18 and CMD12/CS-deassert.
    // When a block's CRC finishes inside send(), we re-prime data_block_ from
    // multi_block_addr_ and emit another 0xFE+data+CRC block instead of
    // going IDLE.
    //
    // GH #94: a BYTE address, not a sector index — the stream advances by
    // block_len_ bytes per block, which is the sector stride in SDHC mode and
    // the CMD16 length in SDSC mode. Keeping it in bytes is what lets one
    // expression serve both addressing modes.
    bool     multi_block_      = false;
    uint64_t multi_block_addr_ = 0;  // byte address of the NEXT block to send

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
    uint32_t overlay_first_sector_ = 0;
    uint32_t overlay_sector_count_ = 0;
    ReadOverlay read_overlay_;

    // NOTE: SdCardDevice intentionally has NO save_state/load_state.  The rewind
    // snapshot ring currently skips the SD back end.  If this class is
    // ever serialised, the CMD18 stream state (multi_block_,
    // multi_block_addr_, plus state_/resp_buf_/resp_idx_/data_idx_/
    // data_crc_count_/data_block_ for mid-block snapshots) must be
    // included so rewinding mid-stream doesn't corrupt the host view —
    // and, since GH #94, host_supports_sdhc_ and block_len_, which
    // together decide how every subsequent address is interpreted.

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
    /// Fill `csd` with a CSD Version 1.0 register (Standard Capacity).
    void build_csd_v1(uint8_t csd[16]) const;
    /// Fill `csd` with a CSD Version 2.0 register (High Capacity).
    void build_csd_v2(uint8_t csd[16]) const;
    void cmd10_send_cid();
    void acmd41_sd_send_op_cond();

    // Helper: compute the 32-bit argument from command argument bytes
    uint32_t cmd_arg() const;

    /// True when the card is operating as a High Capacity (SDHC/SDXC) card,
    /// i.e. when CMD58's OCR reports CCS=1.
    ///
    /// SD Phys Layer Simplified Spec § 4.2.3 / § 5.1: CCS is 1 only when the
    /// host asked for it with ACMD41's HCS bit, so in this model CCS is
    /// exactly `host_supports_sdhc_` (see cmd58_read_ocr()). A card that has
    /// only been brought up with the legacy MMC CMD1 never sets HCS, so it
    /// reports CCS=0 and is byte-addressed — which is what FatFs's CT_MMC
    /// path assumes when it multiplies the sector by 512 itself.
    bool block_addressed() const { return host_supports_sdhc_; }

    /// Translate a CMD17/CMD18/CMD24 argument into a byte offset in the image.
    ///
    /// SD Phys Layer Simplified Spec § 4.7.4 (command descriptions for
    /// READ_SINGLE_BLOCK / READ_MULTIPLE_BLOCK / WRITE_BLOCK): the argument is
    /// "data address ... in byte units in a Standard Capacity SD Memory Card
    /// and in block (512 Byte) units in a High Capacity SD Memory Card".
    /// GH #94: jnext multiplied by 512 unconditionally, so an SDSC-negotiated
    /// host silently read and wrote 512× past where it asked.
    uint64_t arg_to_byte_addr(uint32_t arg) const;

    /// True when a transfer of `block_len_` bytes starting at `byte_addr`
    /// would cross a physical (512-byte) block boundary.
    ///
    /// Both CSDs this card synthesises declare READ_BLK_MISALIGN = 0 and
    /// WRITE_BLK_MISALIGN = 0 (§ 5.3.2 / § 5.3.3), which per § 4.3.2 forbids
    /// a data block from crossing a physical block boundary. The host is told
    /// with R1 bit 5, whose Table 7-9 definition is exactly this condition:
    /// "a misaligned address which did not match the block length was used in
    /// the command". In block-addressed mode the argument cannot express a
    /// misaligned address, so the check is inert there.
    bool crosses_block_boundary(uint64_t byte_addr) const;

    bool is_overlay_sector(uint32_t sector) const;
    /// True when `byte_addr` names the start of an overlaid sector.
    bool is_overlay_addr(uint64_t byte_addr) const;
    /// Load `block_len_` bytes at `byte_addr` into data_block_.
    bool load_read_block(uint64_t byte_addr);

    // Queue an R1 response byte
    void queue_r1(uint8_t r1);
};
