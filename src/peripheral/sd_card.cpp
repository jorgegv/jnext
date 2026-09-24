#include "peripheral/sd_card.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"
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

// Task 26 item 3 — CRC-16 over an SD data block.
//
// SD Physical Layer Simplified Spec § 4.5 / § 7.2.4: every SPI-mode data
// block (start-of-block token, DATA, 16-bit CRC) carries a CRC-16
// computed with the CCITT generator polynomial
//   G(x) = x^16 + x^12 + x^5 + 1   (= 0x1021)
// and an INITIAL VALUE OF 0x0000 (this is the CRC-16/XMODEM catalogue
// variant — refin=false, refout=false, xorout=0x0000). NB: this differs
// from "CRC-16/CCITT-FALSE" (init 0xFFFF); real SD cards and any firmware
// that validates the read-data CRC expect the init-0x0000 form, so that
// is what we emit. Standard check value CRC16("123456789") = 0x31C3.
//
// Pre-fix jnext emitted a dummy 0x0000 CRC on every data block ("works by
// luck" — FatFs/TBBlue don't validate the read CRC by default, CMD59 is
// never issued), diverging from every real card.
uint16_t sd_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000) crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            else              crc = static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// Task 26 item 1 — SPI Ncr (command→response) idle-byte count.
//
// SD Physical Layer Simplified Spec § 7.5.4 (SPI bus timing): Ncr, the
// number of bytes between a command and its response, is 1..8. jnext emits
// exactly 2 leading idle ($FF) bytes before every R1 so both poll-based
// readers (FatFs send_cmd polls for the first non-$FF) and fixed-count
// firmware readers (which hardcode Ncr=2) latch R1 at a deterministic
// offset. Pre-fix jnext emitted a single idle byte, which a fixed-count
// Ncr=2 reader would mis-latch (it would read R1 one byte early).
constexpr uint8_t kIdle = 0xFF;

}  // namespace

SdCardDevice::SdCardDevice() = default;

SdCardDevice::~SdCardDevice() {
    unmount();
}

bool SdCardDevice::mount(const std::string& path, bool read_only) {
    unmount();

    if (read_only) {
        // Deliberate, not a fallback: the caller asked for a card the guest
        // cannot write (GH #77). Logged at info because nothing went wrong —
        // the warn below means "you wanted RW and did not get it".
        file_.open(path, std::ios::in | std::ios::binary);
        if (!file_.is_open()) {
            sd_log()->error("failed to open SD image read-only: {}", path);
            return false;
        }
        sd_log()->info("SD image opened read-only by request: {}", path);
    } else {
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
    }

    file_.seekg(0, std::ios::end);
    file_size_ = static_cast<uint64_t>(file_.tellg());
    file_.seekg(0, std::ios::beg);

    // Pass-5 verify-audit fix (2026-05-09): full SPI-protocol state reset
    // on mount. The pre-fix code cleared only state_/initialized_/app_cmd_/
    // cmd_idx_, leaving resp_buf_, resp_idx_, data_idx_, data_crc_count_,
    // multi_block_, multi_block_addr_, persistent_response_byte_,
    // data_block_, cmd_buf_ untouched. A runtime mount swap (e.g. user
    // changes --sdcard while a CMD18 stream is in flight, or a CMD17
    // SENDING_DATA is mid-transfer) would let send() continue emitting
    // stale data from the previous image's data_block_ or re-prime
    // multi_block_addr_ as if streaming the old card. Use the same
    // canonical full reset() the soft-reset path uses to avoid drift —
    // mount() is conceptually a hard cycle of the card.
    reset();

    sd_log()->debug("mounted SD image: {} ({} bytes, {} MB)",
                   path, file_size_, file_size_ / (1024 * 1024));
    return true;
}

void SdCardDevice::unmount() {
    if (file_.is_open()) {
        file_.close();
        sd_log()->debug("SD image unmounted");
    }
    // Pass-8 verify-audit fix (2026-05-09): symmetric with mount(). Pre-fix
    // unmount() cleared only state_ / initialized_ / file_size_ /
    // pending_write_after_r1_, leaving cmd_idx_, resp_buf_, resp_idx_,
    // data_idx_, data_crc_count_, app_cmd_, multi_block_, multi_block_addr_
    // and persistent_response_byte_ stale. After-the-fact remount via mount()
    // does call reset() and is therefore safe — but a bare unmount() (e.g.
    // user-initiated Eject SD with no follow-up mount) left the card in a
    // half-cleared state. The early-out in receive()/send() (`if
    // (!file_.is_open()) return 0xFF`) made it benign for runtime I/O but
    // surfaced as bugs in tests and cross-tool snapshot inspection. Use the
    // same canonical full reset() that mount() uses; the only difference
    // unmount() needs is closing the file (above) and clearing file_size_.
    reset();
    clear_read_overlay();
    file_size_ = 0;
}

void SdCardDevice::deselect() {
    // NextZXOS-boot fix (2026-07-09, ZXGO-COMPARISON doc): an OPEN CMD18
    // multi-block stream SURVIVES CS deassert. In SPI mode, deasserting CS
    // pauses the card; the stream is terminated by CMD12 (or a new
    // command), not by CS. NextZXOS's esxDOS driver keeps one CMD18 stream
    // open across driver calls: it deselects between blocks and later
    // reselects and token-polls port $EB for the next block WITHOUT
    // sending any command. Pre-fix jnext aborted the stream here, so the
    // resumed poll saw endless $FF → esxDOS timeout (error 2) → boot
    // aborted to $0000. Symmetric-trace diff vs the zx_go reference
    // emulator (whose sdcard model preserves multi-read across CS
    // deassert) pinned this as the first behavioral divergence of the
    // post-staging-reset boot path.
    if (multi_block_ && state_ == State::SENDING_DATA) {
        // Freeze the streaming context (state_, resp/data cursors,
        // multi-block position). A new command after reselect still works:
        // cmd_idx_ resets below and receive() accepts commands — CMD12
        // closes the stream properly.
        cmd_idx_ = 0;
        app_cmd_ = false;
        pending_write_after_r1_ = false;
        persistent_response_byte_ = 0xFF;
        sd_log()->debug(
            "CS deasserted mid-CMD18 — stream paused (next byte={:#010x})",
            multi_block_addr_);
        return;
    }
    // Otherwise: reset SPI protocol state on CS deassert (matches ZesarUX
    // mmc_cs behavior). The SD card goes back to idle, ready for a new
    // command sequence. This is critical: without it, after a CMD17 sector
    // read the SD card can be stuck in SENDING_DATA state, causing the
    // next command to be lost.
    state_ = State::IDLE;
    cmd_idx_ = 0;
    resp_buf_.clear();
    resp_idx_ = 0;
    data_idx_ = 0;
    data_crc_count_ = 0;
    data_token_received_ = false;  // V12-DIVMMC-06 (Pass-12 reviewer fix)
    app_cmd_ = false;
    multi_block_      = false;
    multi_block_addr_ = 0;
    // CMD24 R1-bridge flag also clears: a CS deassert mid-CMD24 (after R1
    // queued but before the data phase started) must not leave the card
    // primed to flip to RECEIVING_DATA on the next host activation.
    pending_write_after_r1_ = false;
    // The post-write BUSY window does NOT survive CS deassert: the generic
    // reset above already put the card in IDLE, and unlike the CMD18 stream
    // there is deliberately no special case for WRITE_RESP / WRITE_BUSY. A
    // reselect therefore reads $FF (not busy), and SD-BUSY-05 locks that in.
    //
    // That is the CORRECT behaviour for this model, not a shortcut. A real
    // card keeps programming across a CS deassert and resumes signalling busy
    // on reselect only BECAUSE programming takes real time; jnext models it as
    // instantaneous (see kWriteBusyBytes — the whole SD path here completes
    // within the byte that requests it). Under an instantaneous model the
    // programming is finished by the time the host reselects, so "not busy" is
    // the honest answer. Carrying the window across CS would report busy after
    // the write had notionally completed.
    //
    // It is also unreachable from the firmware this matters to: in
    // enNxtmmc.rom's only write routine ($1F92, single caller $1EC0) the sole
    // `out ($e7),a` CS-deassert sits at the shared exit label $1F79, reached
    // only after the busy poll has already resolved. CS stays asserted for the
    // whole write -> token -> busy-poll sequence.
    // ZEsarUX-style: clear persistent-response byte on CS deassert. ZEsarUX
    // mmc_cs() at storage/mmc.c:711-714 sets mmc_last_command=0 +
    // mmc_index_command=0 (so case 0x00 fires next, returning $FF on TBBlue).
    persistent_response_byte_ = 0xFF;
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
                // ZEsarUX-style: a new CMD invalidates any prior CMD's
                // persistent response byte. Reset to $FF (the dispatch
                // default in IDLE state).
                persistent_response_byte_ = 0xFF;
            }
            break;

        case State::RECEIVING_CMD:
            cmd_buf_[cmd_idx_++] = tx;
            if (cmd_idx_ >= 6) {
                process_command();
            }
            break;

        case State::RECEIVING_DATA:
            if (!data_token_received_ && tx == 0xFE) {
                // Data token received — start collecting data on the
                // NEXT incoming byte.
                data_token_received_ = true;
                break;
            }
            // Pass-4 verify-audit fix (2026-05-09) + V12-DIVMMC-06
            // (Pass-12 reviewer fix, 2026-05-10): per SD Physical Layer
            // Simplified Spec 6.00 § 7.3.3.2, "Following the command
            // response (R1) and one (or more) bytes of $FF (host SPI
            // clock), the data must be sent following a Data Token byte
            // ($FE for single block, $FC for multi-block)." The card
            // must wait indefinitely for the 0xFE token; any pre-token
            // byte is ignored. Pass-4 fix only skipped 0xFF gap bytes
            // and absorbed any other pre-token byte as data_block_[0],
            // silently shifting an out-of-spec host's payload by 1+.
            // V12-DIVMMC-06 (reviewer-promoted from audit catalogue)
            // tracks token-receipt explicitly via data_token_received_,
            // so EVERY pre-token byte is now ignored — VHDL/spec-
            // faithful for misbehaving hosts. Class-(c) latent for real
            // firmware (TBBlue/FatFs/esxdos all send only 0xFF gaps
            // then 0xFE).
            if (!data_token_received_) {
                break;  // pre-token byte (incl. 0xFF) — keep waiting
            }
            // GH #94 — CMD24 only ever runs with the full physical block
            // (WRITE_BL_PARTIAL = 0; cmd24_write_single_block() rejects any
            // other block length at R1), so the data phase is kBlockLen long
            // in both addressing modes.
            if (data_idx_ < static_cast<int>(kBlockLen)) {
                data_block_[data_idx_++] = tx;
                break;
            }
            // Collecting 2 CRC bytes (ignored)
            data_crc_count_++;
            if (data_crc_count_ >= 2) {
                // GH #94 — cmd_buf_ still holds the CMD24 that opened this
                // data phase, so the address is re-derived the same way the
                // command did (§ 4.7.4: byte address on SDSC, block address
                // on SDHC).
                const uint64_t byte_addr = arg_to_byte_addr(cmd_arg());
                const uint64_t sector    = byte_addr / kBlockLen;
                // V12-DIVMMC-02 (Pass-12 verify-audit fix, 2026-05-10): SD
                // Physical Layer Simplified Spec § 7.3.3.3 Data Response
                // Token format is `0bxxx0_sss1` where the status code `sss`
                // is one of:
                //   010 = data accepted              → 0x05
                //   101 = data rejected (CRC error)  → 0x0B
                //   110 = data rejected (write error)→ 0x0D
                // Pre-fix CMD24 unconditionally emitted 0x05 (data accepted)
                // even when the write was silently SKIPPED because the
                // sector was past end-of-image. That misled the host into
                // believing a successful write happened — diverging from
                // every real SD card which sets the write-error status
                // (0x0D) when the address is out of range. The boot path
                // never writes past the image, so the prior bug was
                // latent; class-(b) → corrected for spec faithfulness and
                // to surface the firmware's write-error path on past-EOF
                // attempts.
                bool write_ok = (byte_addr + kBlockLen <= file_size_);
                if (write_ok) {
                    file_.seekp(static_cast<std::streamoff>(byte_addr), std::ios::beg);
                    file_.write(reinterpret_cast<const char*>(data_block_), kBlockLen);
                    file_.flush();
                    // V15-DIVMMC-01 (Pass-15 verify-audit fix, 2026-05-10):
                    // SD Physical Layer Simplified Spec § 7.3.3.3 (Data
                    // Response Token format) — `sss=110` (= 0x0D) signals
                    // "data rejected due to write error". The host-side
                    // fstream `write()` silently sets `failbit` when the
                    // image was opened in fall-back read-only mode (mount()
                    // line 33) or when the underlying disk is full / I/O
                    // errors. Pre-fix the card unconditionally emitted 0x05
                    // (data accepted) even when the host fstream rejected
                    // the write — diverging from every real SD card with
                    // a mechanical write-protect tab (or backed by a host
                    // file with no write permission) which would emit
                    // 0x0D for any write attempt that didn't physically
                    // commit. Symmetric with the V12-DIVMMC-02 past-EOF
                    // fix and the V14-DIVMMC-01 mid-stream past-EOF fix.
                    // Class-(c) latent — the canonical NextZXOS fixture
                    // is opened RW and writes succeed; but a forensic /
                    // user-supplied RO image silently corrupts the host's
                    // expectations of a successful write, and a real
                    // hardware-stress test rig can swap in a RO mount to
                    // exercise the firmware's write-error path. After
                    // the fix the path is exercised faithfully.
                    if (!file_.good()) {
                        sd_log()->warn(
                            "CMD24 host-side write failed at sector={} "
                            "(byte={:#010x}) — emitting write-error token "
                            "0x0D (file was opened RO or I/O error)",
                            sector, byte_addr);
                        write_ok = false;
                        // Clear the failbit so subsequent CMDs that also
                        // attempt writes get a fresh attempt (and fail
                        // again, symmetrically) — without this the stream
                        // would stay sticky-failed forever and even reads
                        // (file_.seekg/read) would short-circuit on the
                        // bad bit. seekg/read paths use seekg+read which
                        // also touch the same stream state.
                        file_.clear();
                    } else {
                        sd_log()->debug(
                            "CMD24 wrote 512 bytes at sector {} (byte={:#010x})",
                            sector, byte_addr);
                    }
                } else {
                    sd_log()->warn("CMD24 write past end of image: sector={} byte={:#010x} size={} → write-error response token",
                                   sector, byte_addr, file_size_);
                }
                state_ = State::WRITE_RESP;
                // 0x05 = data accepted; 0x0D = write error.
                resp_buf_ = { static_cast<uint8_t>(write_ok ? 0x05 : 0x0D) };
                resp_idx_ = 0;
                // SD Physical Layer Simplified Spec § 7.3.3.1: a card that
                // ACCEPTED the block holds DataOut LOW (busy) after the data
                // response token while it programs, and the host polls until
                // the line returns high. A REJECTED block (0x0D write error /
                // CRC error) is never programmed, so no busy phase follows —
                // hence the flag rather than an unconditional transition.
                write_busy_pending_ = write_ok;
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
                data_token_received_ = false;  // V12-DIVMMC-06 (Pass-12 reviewer fix)
                persistent_response_byte_ = 0xFF;
                // Defensive: any new command in-flight during a CMD18
                // stream also terminates the multi-block state, even
                // if it isn't CMD12.  The SD spec only legalises
                // CMD12 or CS-deassert as abort mechanisms, but a
                // mis-behaving host (or a test) that sends a different
                // command mid-stream must not leave multi_block_ set
                // and silently resume after the new command's block.
                multi_block_      = false;
                multi_block_addr_ = 0;
                // Same logic for the CMD24 R1-bridge flag: a new command
                // arriving mid-CMD24 (between the queued R1 and the data
                // phase) must not leave the bridge armed and silently
                // promote the next response into a write-data phase.
                pending_write_after_r1_ = false;
                return 0xFF;
            }
            // V18-DIVMMC-NIT-01 (Pass-18 reviewer fix, 2026-05-10): full-
            // duplex SPI byte exchange.  Per VHDL `serial/spi_master.vhd:
            // 104-117` (oshift_r) + `:148-168` (ishift_r / miso_dat) +
            // `zxnext.vhd:3270-3298` (master instantiation), every clocked
            // 8-bit transfer captures whatever the slave is driving on
            // MISO regardless of the MOSI byte being shifted out.  When
            // state_ is RESPONDING / SENDING_DATA / WRITE_RESP and the
            // incoming `tx` is not a new-CMD start byte, the card is
            // actively shifting its response stream on MISO — the host
            // should observe the next response byte AND the response
            // stream should advance.  Pre-fix this default branch dropped
            // both effects: it returned 0xFF and left `resp_idx_` /
            // `data_idx_` un-advanced, so a `write_data(0xFF)` mid-response
            // saw 0xFF on MISO and the next `read_data()` re-emitted the
            // byte that VHDL would have already clocked out.  Boot-path
            // impact is zero — TBBlue + FatFs + esxdos poll responses via
            // `IN A,($EB)` (read-only port read → `send()`), never via
            // write-then-read — but a future host that does interleave
            // would diverge.  Delegate to `send()` which advances the
            // stream and returns the byte VHDL would have clocked.
            return send();
    }
    return 0xFF;
}

uint8_t SdCardDevice::send() {
    // Read path: send the next response byte to the host.
    // This produces the sequential response matching ZesarUX's mmc_read().
    if (!file_.is_open()) return 0xFF;

    switch (state_) {
        case State::IDLE:
            // ZEsarUX-style sustained response: after a CMD's transient
            // response queue has drained (state RESPONDING → IDLE), some
            // commands continue to return a fixed byte on every read until
            // a new CMD or CS deassert. CMD0 → $01, CMD8 → $00, CMD12 →
            // $01, others → $FF (the historical default). Set by CMD
            // handlers via persistent_response_byte_.
            return persistent_response_byte_;

        case State::RECEIVING_CMD:
            // Command not fully received yet
            return 0xFF;

        case State::RESPONDING:
            if (resp_idx_ < resp_buf_.size()) {
                uint8_t b = resp_buf_[resp_idx_++];
                // Eagerly handle exhaustion when emitting the LAST byte so
                // any subsequent host activity (write data after CMD24's R1,
                // for instance) sees the post-response state immediately.
                // CMD24 sets pending_write_after_r1_ so we transition to
                // RECEIVING_DATA only AFTER R1 (and its NCR prefix) have
                // actually been emitted on MISO — see SD spec § 7.2.4 /
                // 7.3.3.1 and FatFs send_cmd in tbblue diskio.c which polls
                // for the first non-0xFF byte (the R1).
                if (resp_idx_ >= resp_buf_.size()) {
                    if (pending_write_after_r1_) {
                        pending_write_after_r1_ = false;
                        state_ = State::RECEIVING_DATA;
                    } else {
                        state_ = State::IDLE;
                    }
                }
                return b;
            }
            // Buffer was already exhausted on entry — defensive idle path.
            if (pending_write_after_r1_) {
                pending_write_after_r1_ = false;
                state_ = State::RECEIVING_DATA;
            } else {
                state_ = State::IDLE;
            }
            return 0xFF;

        case State::SENDING_DATA:
            if (resp_idx_ < resp_buf_.size()) {
                return resp_buf_[resp_idx_++];
            }
            if (data_idx_ < static_cast<int>(block_len_)) {
                return data_block_[data_idx_++];
            }
            // The synthetic file-map overlay is a compatibility stream, not
            // a standards-level card transaction. ZEsarUX's Atic-compatible
            // CMD18 path emits FE,00,FE between each 512-byte chunk and does
            // not expose CRC bytes. Keep real CRCs for ordinary image-backed
            // sectors; advance the overlay directly to its next framing
            // triplet.
            if (!(multi_block_ && multi_block_addr_ >= block_len_ &&
                  is_overlay_addr(multi_block_addr_ - block_len_)) &&
                data_crc_count_ < 2) {
                // Task 26 item 3 — emit the real CRC-16 (computed over the
                // block_len_ data bytes when the block was loaded), high byte
                // first. Pre-fix returned a dummy 0x00 for both bytes.
                const uint8_t crc_byte = (data_crc_count_ == 0)
                    ? static_cast<uint8_t>(data_crc_ >> 8)
                    : static_cast<uint8_t>(data_crc_ & 0xFF);
                data_crc_count_++;
                return crc_byte;
            }
            // Block complete.  For CMD18 multi-block mode, load the next
            // sector and re-prime for another 0xFE+data+CRC cycle.  Emit one
            // 0xFF as inter-block filler so the host's "skip until non-0xFF
            // then expect 0xFE" token-poll (see rcvr_datablock in tbblue
            // src/firmware/app/src/ff/diskio.c:156-164) finds the new token
            // on a subsequent read rather than the same byte that terminated
            // the previous block's CRC.
            if (multi_block_) {
                const uint64_t byte_addr   = multi_block_addr_;
                const bool     overlay_block = is_overlay_addr(byte_addr);
                if (!load_read_block(byte_addr)) {
                    // V14-DIVMMC-01 (Pass-14 verify-audit fix, 2026-05-10):
                    // SD Physical Layer Simplified Spec § 7.3.3.3 (Data
                    // Error Token format): when the card cannot deliver
                    // the requested data block (out-of-range, ECC failure,
                    // CC error, generic error), it sends a 1-byte error
                    // token in place of the 0xFE start-of-block token.
                    // Bit layout (MSB to LSB): 0b0000_OECR where bit 3 =
                    // O (OUT_OF_RANGE → mask 0x08), bit 2 = E (card ECC
                    // failed), bit 1 = C (CC error), bit 0 = R (geneRic
                    // error). The OUT_OF_RANGE-only token is 0x08.
                    //
                    // Pre-fix CMD18 mid-stream past-EOF silently aborted
                    // by emitting 0xFF and transitioning to IDLE. The
                    // host's "skip until non-0xFF then expect 0xFE"
                    // token-poll loop (e.g. rcvr_datablock in TBBlue
                    // diskio.c:156-164) would never see either 0xFE or
                    // 0x08 and time out — diverging from spec-compliant
                    // SD cards that emit 0x08 as a discriminative error
                    // token. Symmetric with the V12-DIVMMC-04 + V13-
                    // DIVMMC-01 CMD17/CMD18-initial-block past-EOF fix
                    // which already emits 0x08 in the start-of-stream
                    // case (cmd17_read_single_block, cmd18_read_multiple
                    // _block). Class-(c) latent for the boot path
                    // (TBBlue + FatFs never read past EOF mid-stream),
                    // but real-spec divergence — the pre-fix path
                    // produced an indistinguishable timeout instead of
                    // the documented error signal.
                    sd_log()->warn(
                        "CMD18 read past end of image at byte={:#010x}; "
                        "emitting data error token 0x08 and ending stream",
                        byte_addr);
                    multi_block_      = false;
                    multi_block_addr_ = 0;
                    state_            = State::IDLE;
                    return 0x08;  // data error token: out of range
                }
                // Guarded for the should_log() reason given in
                // PortDispatch::read (src/port/port_dispatch.cpp): unguarded,
                // an out-of-line call per streamed block (GH #244).
                if (sd_log()->should_log(spdlog::level::trace))
                    sd_log()->trace(
                        "CMD18 next block sector={} (byte={:#010x})",
                        byte_addr / kBlockLen, byte_addr);
                multi_block_addr_ = byte_addr + block_len_;
                resp_buf_ = overlay_block
                    ? std::vector<uint8_t>{ 0xFE, 0x00, 0xFE }
                    : std::vector<uint8_t>{ 0xFE };
                resp_idx_       = 0;
                data_idx_       = 0;
                data_crc_count_ = 0;
                data_crc_       = sd_crc16(data_block_, block_len_);  // Task 26 item 3
                // The direct-NEX file-map bridge mirrors ZEsarUX's
                // Atic-compatible SDHC stream: FE,00,FE is available
                // immediately after the preceding 512 data bytes. Ordinary
                // image sectors retain jnext's standards-friendly filler.
                if (overlay_block) return resp_buf_[resp_idx_++];
                return 0xFF;  // inter-block filler
            }
            state_ = State::IDLE;
            return 0xFF;

        case State::RECEIVING_DATA:
            return 0xFF;

        case State::WRITE_RESP:
            if (resp_idx_ < resp_buf_.size()) {
                return resp_buf_[resp_idx_++];
            }
            // SD Phys Layer Simplified Spec § 7.3.3.1 — after the data
            // response token an ACCEPTED block is followed by a BUSY window
            // (card drives DataOut low, i.e. reads return 0x00) which ends
            // when programming completes. Skipping it entirely, as this did,
            // is not "an infinitely fast card": esxdos polls for the busy
            // signal to APPEAR (enNxtmmc.rom $1F3D reads until a non-$FF
            // byte), so a card that never asserts it makes the firmware spin
            // its full 12800-read timeout on EVERY sector write — measured at
            // ~44% of all emulated CPU time during a NextSync transfer, and
            // the whole of GH #177's throughput gap.
            if (write_busy_pending_) {
                write_busy_pending_ = false;
                busy_remaining_     = kWriteBusyBytes;
                state_              = State::WRITE_BUSY;
                return 0x00;
            }
            state_ = State::IDLE;
            return 0xFF;

        case State::WRITE_BUSY:
            // The busy window is `busy_remaining_` bytes of 0x00 (card holding
            // DataOut low) followed by ONE PARTIAL byte, and the partial byte
            // is the whole point.
            //
            // The card releases DataOut asynchronously to the host's SPI byte
            // boundary, so the byte in which programming finishes has its
            // leading bits low and its trailing bits high — 0x01/0x03/.../0x7F,
            // never 0x00 and never 0xFF. esxdos depends on exactly that:
            //
            //   $1FB9  CALL $1F3D    ; read until a non-$FF byte (12800 tries)
            //   $1FBC  AND A
            //   $1FBD  JR Z,$1FB9    ; 0x00 => still busy, keep waiting
            //
            // so $FF means "keep polling" and 0x00 means "still busy". ONLY a
            // partial byte ends the wait. A card that steps 0x00 -> 0xFF with
            // no partial byte makes the firmware spin its full 12800-read
            // timeout and then fall back to CMD13 — which is precisely what
            // jnext did (it emitted no busy phase at all), and it cost ~44% of
            // all emulated CPU time during a NextSync transfer (GH #177).
            if (busy_remaining_ > 0) { --busy_remaining_; return 0x00; }
            state_ = State::IDLE;
            return kBusyReleaseByte;
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
        // Pass-9 verify-audit fix (2026-05-09): per SD Physical Layer
        // Simplified Spec § 4.3.9.1 / § 7.3.2.1 / § 4.3.9.5, "If the next
        // command following CMD55 is not an application-specific (ACMD)
        // command, the application-specific flag is cleared and the
        // command is treated as a regular command." Pre-fix returned R1
        // illegal-command for any non-ACMD41 sequence, which violated the
        // spec for the regular CMD bridge case (e.g. CMD55 → CMD17). The
        // ACMD set in TBBlue/NextZXOS-relevant firmware is just ACMD41
        // (init) and ACMD13/22/23/42/51 (extended status / pre-erase). For
        // anything else, fall through to the regular CMD switch below so
        // that recognized regular CMDs (CMD0/CMD8/CMD13/CMD17/etc.) get
        // their normal response, and unrecognized regular CMDs hit the
        // default-illegal branch. The boot path does CMD55 → ACMD41 only,
        // so the prior bug was latent — class-(b) → corrected for spec
        // faithfulness. (CMD13 SEND_STATUS shares its index with ACMD13
        // SD_STATUS; the regular branch handles the SEND_STATUS shape,
        // which TBBlue's MMC layer expects post-CMD55 only when issued in
        // the SD-status flow — currently unused.)
        sd_log()->debug("CMD55 not followed by recognized ACMD{} → "
                        "falling through to regular CMD switch", cmd);
        // Fall through.
    }

    switch (cmd) {
        case 0:  cmd0_go_idle(); break;
        case 1:  cmd1_send_op_cond(); break;
        case 8:  cmd8_send_if_cond(); break;
        case 9:  cmd9_send_csd(); break;
        case 10: cmd10_send_cid(); break;
        case 12: cmd12_stop_transmission(); break;
        case 13: cmd13_send_status(); break;
        case 16: cmd16_set_blocklen(); break;
        case 17: cmd17_read_single_block(); break;
        case 18: cmd18_read_multiple_block(); break;
        case 23: cmd23_set_block_count(); break;
        case 24: cmd24_write_single_block(); break;
        case 55: cmd55_app_cmd(); break;
        case 58: cmd58_read_ocr(); break;
        default:
            // Pass-8 verify-audit fix (2026-05-09): per SD Physical Layer
            // Simplified Spec § 7.3.2.1, R1 bit 2 = "Illegal Command" — set
            // when the card receives a command it does not support. Pre-fix
            // returned only the idle bit, which let firmware silently ignore
            // unsupported-command failures (CMD20/40/45/CMD59 etc.) instead
            // of triggering the firmware's illegal-command error path. Boot
            // path uses only the supported set above, so the prior behaviour
            // was latent; class-(b) → corrected for spec faithfulness and to
            // exercise the firmware's error path on unsupported commands.
            sd_log()->warn("unhandled CMD{} arg={:#010x} (R1=illegal)",
                           cmd, cmd_arg());
            queue_r1(static_cast<uint8_t>((initialized_ ? 0x00 : 0x01) | 0x04));
            break;
    }
}

void SdCardDevice::cmd1_send_op_cond() {
    sd_log()->debug("CMD1 SEND_OP_COND → card initialized, ready");
    initialized_ = true;
    queue_r1(0x00);  // R1: ready (not idle)
}

// CMD0 GO_IDLE_STATE — the software reset that returns the card to the Idle
// State.
//
// GH #94 round-3 review asked for the whole class to be enumerated rather than
// waiting for a third instance to be reported, so: the card carries exactly
// THREE pieces of session state that a reset must drop, and they are the three
// assigned below —
//   initialized_          the host has not run an init sequence yet
//   host_supports_sdhc_   no capacity class has been negotiated yet (§ 4.2.3)
//   block_len_            no CMD16 has been issued yet, so the CSD default
//                         applies (§ 4.3.2)
// Everything else in the class is either transfer state that the arrival of
// ANY new command already clears (`state_`, `cmd_buf_`/`cmd_idx_`,
// `resp_buf_`/`resp_idx_`, `data_idx_`, `data_crc_count_`,
// `data_token_received_`, `multi_block_`/`multi_block_addr_`,
// `pending_write_after_r1_` — see receive()'s default-case abort branch and
// queue_r1()), state that is written immediately before every use and so
// cannot go stale (`data_block_`, `data_crc_`, `write_busy_pending_`,
// `busy_remaining_`, `app_cmd_`), state CMD0 sets deliberately
// (`persistent_response_byte_` = 0x01, the ZEsarUX-compatible sustained
// response), or state that belongs to the MOUNTED MEDIA and not to the card
// session, which a reset must NOT touch (`file_`, `file_size_`, and the
// `read_overlay_` triple installed by the host).
void SdCardDevice::cmd0_go_idle() {
    sd_log()->debug("CMD0 GO_IDLE_STATE → card reset");
    initialized_ = false;
    // GH #94 round-2 review: CMD0 is the software reset that returns the card
    // to the idle state, so it returns the block length to its power-up value
    // as well — the CSD default of 2^READ_BL_LEN (§ 4.3.2), which is what a
    // host is entitled to assume before it has issued any CMD16. Answering
    // the reviewer's question directly: yes, CMD0 should reset it too, and
    // not only for tidiness — the class's own reset() already calls kBlockLen
    // "the power-up block length", and a software reset that left a stale
    // CMD16 length behind would make that comment false on the CMD0 path.
    // It is belt-and-braces next to the ACMD41 reset above (every init
    // sequence reaches ACMD41), which is exactly why it is cheap.
    block_len_ = kBlockLen;
    // GH #94 round-3 review: the negotiated capacity class goes with it, for
    // the same reason. A host declares whether it supports high capacity in
    // ACMD41's HCS bit (§ 4.2.3); a card that has just been reset to the idle
    // state has not been told anything about capacity yet, so carrying the
    // previous answer across CMD0 makes the card claim a class it never
    // negotiated. The legacy MMC path is where that becomes visible rather
    // than theoretical — CMD1 initialises without any capacity negotiation,
    // so after ACMD41(HCS=1) → CMD0 → CMD1 the pre-fix card reported CCS=1
    // in its OCR and block-addressed every transfer, on the strength of an
    // ACMD41 that belonged to a previous initialisation.
    host_supports_sdhc_ = false;
    queue_r1(0x01);  // R1: in idle state
    // Pass-9 verify-audit (2026-05-09): keep persistent_response_byte_ at
    // ZEsarUX-style $01 to match the upstream boot trace. SD Physical Layer
    // Simplified Spec § 7.3.2 says the card returns 0xFF (line idle) after
    // R1 and before the next command, BUT the TBBlue MMC_Init path polls
    // for the first non-0xFF byte expecting it to be the R1 (= 0x01). With
    // a strictly spec-faithful 0xFF tail, the firmware would consume the
    // queued NCR/R1 once and then sample 0xFF forever — failing its
    // (R1 & 0xFE)==0 check because $FF & $FE = $FE, which the firmware
    // treats as a CRC error. ZEsarUX's mmc_read() at storage/mmc.c:854-857
    // returns sustained $01 deliberately to keep `last_command=0x40` valid
    // until a new CMD or CS deassert. We retain that behaviour because it
    // (a) reflects real cards observed by the firmware author, (b) keeps
    // TBBlue's boot trace working, and (c) is bounded by the deselect()/
    // new-CMD reset paths — once the host issues another command, the
    // sustained byte is replaced. Mark as a deliberate ZEsarUX-compat
    // model rather than divergence from VHDL (the VHDL has no SD card —
    // i_SPI_SD_MISO is external).
    persistent_response_byte_ = 0x01;
}

void SdCardDevice::cmd8_send_if_cond() {
    // CMD8 SEND_IF_COND: respond with proper R7 (R1 idle + voltage echo +
    // check pattern echo). Without proper R7, NextZXOS firmware retries
    // CMD8 forever (verified 2026-05-07 — sustained CMD8=$00 caused 279
    // retries in 5s without progressing).
    //
    // Note: ZEsarUX's `storage/mmc.c:864-879` returns sustained $00 for
    // CMD8 deliberately, with the comment "eventually times out and runs
    // the ROM". On TBBlue/NextZXOS, "runs the ROM" means the boot falls
    // through to BASIC ROM (not MMC fallback) — which is NOT what we want
    // for booting to the NextZXOS welcome menu. So keep proper R7 here.
    uint8_t check = cmd_buf_[4];  // echo back check pattern
    sd_log()->debug("CMD8 SEND_IF_COND check={:#04x} → voltage accepted", check);
    // V12-DIVMMC-03 (Pass-12 reviewer fix, 2026-05-10): R7 4-byte register
    // bits 31:28 are the command version field (`0001` per SD Physical Layer
    // Simplified Spec § 7.3.2.6 "Format R7 (Card Interface Condition)").
    // That maps to byte 0 of the register = 0x10. Pre-fix used 0x00 which
    // is reserved/illegal; class-(c) latent because TBBlue + FatFs only
    // validate the voltage-accepted nibble (byte 2 low nibble) and check-
    // pattern echo (byte 3). Reviewer-promoted from audit catalogue to a
    // direct fix because the change is a single byte and trivially
    // spec-faithful.
    //
    // V14-DIVMMC-02 (Pass-14 verify-audit fix, 2026-05-10): SD Physical
    // Layer Simplified Spec § 7.3.2.6 explicitly states R7 byte 0 has
    // the same format as R1 (Table 7-9). Pre-fix hard-coded R1 = 0x01
    // (in idle state) regardless of `initialized_`, so a CMD8 issued
    // AFTER successful ACMD41 init would still report idle — diverging
    // from spec-compliant cards that reflect the live state in R1.
    // Practical impact on the boot path is nil (TBBlue/NextZXOS / FatFs
    // only issue CMD8 once during init, before ACMD41), but a strict
    // host that re-probes CMD8 after init would see misleading idle
    // status. Class-(c) latent → corrected for spec faithfulness.
    const uint8_t r1 = initialized_ ? 0x00 : 0x01;
    // Task 26 item 1 — 2 NCR idle bytes before R1 (SD Phys Layer § 7.5.4).
    resp_buf_ = { kIdle, kIdle, r1, 0x10, 0x00, 0x01, check };  // NCR×2 + R1 + R7 (cmd ver=1)
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::cmd12_stop_transmission() {
    sd_log()->debug("CMD12 STOP_TRANSMISSION");
    // Abort any in-progress multi-block transfer and return to idle.
    data_idx_ = 0;
    data_crc_count_ = 0;
    // Terminate CMD18 streaming if active.
    multi_block_      = false;
    multi_block_addr_ = 0;
    // CMD12 has "stuff bytes" before R1 — the firmware reads 8 stuff bytes
    // before polling for R1 (see TBBLUE.FW code at 0x7AB0).
    // Provide 8 stuff bytes (0xFF) + NCR (0xFF) + R1 to ensure the R1 poll
    // after the stuff bytes finds the actual R1 response.
    uint8_t r1 = initialized_ ? 0x00 : 0x01;
    resp_buf_ = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // 8 stuff bytes
                  0xFF, r1 };  // NCR + R1
    resp_idx_ = 0;
    state_ = State::RESPONDING;
    // After R1, real SD asserts BUSY (MISO low = $00) for some time then
    // releases the line back to IDLE ($FF). The NextZXOS supervisor at
    // bank-2 $196D-$1978 (G46(b) decode) sends CMD12, reads $EB with
    // `IN A,($EB); AND A; JR Z,$1972` — i.e. polls until a NON-ZERO byte
    // arrives, treating $00 as "still busy". So our persistent post-R1
    // state must be $FF (idle), NOT R1=$00 which would look like infinite
    // busy. ZEsarUX returns $01 unconditionally; we use $FF (= no command
    // active, line idle) which is more spec-faithful and also non-zero.
    persistent_response_byte_ = 0xFF;
}

void SdCardDevice::cmd13_send_status() {
    // CMD13 SEND_STATUS → R2 response (2 bytes: R1 + status byte).
    // Idiom mirrors cmd58_read_ocr() — emit NCR + multi-byte response via
    // resp_buf_.  R2=0x00 means "no errors / card OK".
    sd_log()->debug("CMD13 SEND_STATUS → R2");
    uint8_t r1 = initialized_ ? 0x00 : 0x01;
    // Task 26 item 1 — 2 NCR idle bytes before R1 (SD Phys Layer § 7.5.4).
    resp_buf_  = { kIdle, kIdle, r1, 0x00 };  // NCR×2 + R1 + R2
    resp_idx_  = 0;
    state_     = State::RESPONDING;
}

void SdCardDevice::cmd16_set_blocklen() {
    // CMD16 SET_BLOCKLEN — SD Phys Layer Simplified Spec § 4.3.2:
    //
    //   "In the case of a Standard Capacity SD Memory Card, this command sets
    //    the block length (in bytes) for all following block commands (read,
    //    write, lock). [...] In the case of a High Capacity SD Memory Card,
    //    block length set by CMD16 command does not affect the memory read and
    //    write commands. Always 512 Bytes fixed block length is used. [...] In
    //    both cases, if block length is set larger than 512 Bytes, the card
    //    sets the BLOCK_LEN_ERROR bit."
    //
    // So the mode decides whether the card *keeps* the length, not whether it
    // accepts the command. Partial block reads are what make a shorter length
    // meaningful, and READ_BL_PARTIAL is fixed to 1 in CSD Version 1.0
    // (§ 5.3.2) — which is precisely the CSD this card reports when it is
    // Standard Capacity (see cmd9_send_csd).
    //
    // GH #94: the pre-fix handler answered any length other than 512 with the
    // illegal-command bit in BOTH modes. That refused a legal SDSC request
    // (1..512) and mis-encoded the one condition the spec does name — an
    // over-long block is BLOCK_LEN_ERROR, which SPI mode carries in R1 bit 6,
    // whose Table 7-9 definition names the field: "the command's argument
    // (e.g. address, block length) was outside the allowed range for this
    // card".
    const uint32_t arg = cmd_arg();
    // Pass-5 verify-audit fix (2026-05-09): the R1 idle bit (bit 0) reflects
    // the card's idle state and must NOT be hard-coded to 1 when the card has
    // completed ACMD41 init. SD spec § 7.3.2.1 R1 layout: bit 0 = in idle
    // state, bit 5 = address error, bit 6 = parameter error.
    const uint8_t idle_bit = initialized_ ? 0x00 : 0x01;

    // The allowed range is 1..2^READ_BL_LEN = 1..512. Anything outside it —
    // over-long (the spec's named BLOCK_LEN_ERROR case) or zero-length (no
    // transfer the card could perform) — is a parameter error and leaves the
    // current block length untouched.
    if (arg == 0 || arg > kBlockLen) {
        sd_log()->debug("CMD16 SET_BLOCKLEN arg={} out of range → "
                        "R1 PARAMETER_ERROR (block length stays {})",
                        arg, block_len_);
        queue_r1(static_cast<uint8_t>(idle_bit | 0x40));
        return;
    }

    if (block_addressed()) {
        // High Capacity: accepted, but the transfer length stays fixed at 512.
        // (The command remains effective for CMD42 LOCK_UNLOCK, which jnext
        // does not model.)
        sd_log()->debug("CMD16 SET_BLOCKLEN arg={} accepted; high-capacity "
                        "card keeps its fixed {}-byte block", arg, kBlockLen);
    } else {
        block_len_ = arg;
        sd_log()->debug("CMD16 SET_BLOCKLEN arg={} → standard-capacity block "
                        "length now {}", arg, block_len_);
    }
    queue_r1(idle_bit);
}

void SdCardDevice::cmd23_set_block_count() {
    // CMD23 SET_BLOCK_COUNT: hint to the card about how many blocks the
    // host plans to write/read (used as a pre-erase optimization).  We
    // don't model the optimization; just ack it.
    sd_log()->debug("CMD23 SET_BLOCK_COUNT count={}", cmd_arg());
    queue_r1(initialized_ ? 0x00 : 0x01);
}

void SdCardDevice::cmd17_read_single_block() {
    const uint32_t arg = cmd_arg();
    // GH #94 — § 4.7.4: block address on a High Capacity card, byte address
    // on a Standard Capacity one.
    const uint64_t byte_addr = arg_to_byte_addr(arg);
    const uint64_t sector    = byte_addr / kBlockLen;
    sd_log()->debug("CMD17 READ_SINGLE_BLOCK arg={} → byte={:#010x} "
                    "(sector={}, len={}, {})",
                    arg, byte_addr, sector, block_len_,
                    block_addressed() ? "block-addressed" : "byte-addressed");

    if (!initialized_) {
        queue_r1(0x01);  // idle state
        return;
    }

    if (crosses_block_boundary(byte_addr)) {
        // READ_BLK_MISALIGN = 0 (§ 5.3.2): the block may not cross a physical
        // block boundary. R1 bit 5 ADDRESS_ERROR (§ 7.3.2.1, Table 7-9) is the
        // documented signal. No data token follows a command the card
        // rejected, so R1 is the whole response — this is exactly what a host
        // that passed a SECTOR index to a byte-addressed card now sees,
        // instead of silently reading 512 bytes straddling two sectors.
        sd_log()->warn("CMD17 misaligned byte address {:#010x} for a "
                       "{}-byte block → R1 ADDRESS_ERROR", byte_addr,
                       block_len_);
        queue_r1(static_cast<uint8_t>(0x20));
        return;
    }

    if (!load_read_block(byte_addr)) {
        sd_log()->warn("CMD17 read past end of image: byte={:#010x} size={}", byte_addr, file_size_);
        // V12-DIVMMC-04 (Pass-12 reviewer fix, 2026-05-10): SD Physical
        // Layer Simplified Spec § 7.3.2.1 (Table 7-9) R1 layout — bit 6 =
        // PARAMETER_ERROR = "argument was out of the allowed range". When
        // the host issues CMD17 with a sector index past end-of-image the
        // card MUST set R1 bit 6 (in addition to the data error token).
        // Pre-fix returned R1=0x00 ("no errors") + data-error-token 0x08,
        // which is the weaker spec-permissible "card accepted but couldn't
        // do the transfer" path. Class-(c) latent since boot-path firmware
        // never reads past EOF; reviewer-promoted to a real fix because
        // the change is one byte (0x00 → 0x40) and surfaces the
        // parameter-error path that real cards expose.
        queue_r1(0x40);
        // Send error token instead of data
        resp_buf_.push_back(0x08);  // out of range error
        state_ = State::RESPONDING;
        return;
    }

    // Response: NCR×2 + R1 (0x00 = OK), then ≥1 idle byte (Nac — SD Physical
    // Layer Simplified Spec § 7.5.2: the card needs read-access time between
    // the R1 response and the start-of-block token; real cards ALWAYS insert
    // 0xFF filler here), then the 0xFE data start token, then 512 bytes.
    // GH #84: with zero gap, a host that clocks one flush byte after latching
    // R1 (Atic Atac Next's command sender does) swallowed the token and its
    // token-poll then consumed the whole first block, shifting the entire
    // CMD17/CMD18 delivery one sector early. Task 26 item 1 (2 NCR bytes) +
    // item 3 (real CRC computed below, over the block_len_ bytes actually
    // transferred — § 7.2.4 puts the CRC over the data field, whose length is
    // the current block length).
    resp_buf_ = { kIdle, kIdle, 0x00, kIdle, 0xFE };
    resp_idx_ = 0;
    data_idx_ = 0;
    data_crc_count_ = 0;
    data_crc_ = sd_crc16(data_block_, block_len_);  // Task 26 item 3
    state_ = State::SENDING_DATA;
}

void SdCardDevice::cmd18_read_multiple_block() {
    // CMD18 READ_MULTIPLE_BLOCK: like CMD17 for the FIRST block
    // (NCR + R1 + 0xFE + 512 + CRC), but after the block's CRC the card
    // continues streaming more (token + 512 + CRC) for sector+1, sector+2,
    // ...  until the host issues CMD12 STOP_TRANSMISSION (or deselects CS
    // in SPI mode).  See send()'s SENDING_DATA branch for the
    // between-blocks re-prime logic.
    const uint32_t arg       = cmd_arg();
    const uint64_t byte_addr = arg_to_byte_addr(arg);  // GH #94, § 4.7.4
    sd_log()->debug("CMD18 READ_MULTIPLE_BLOCK arg={} → byte={:#010x} "
                    "(sector={}, len={}, {})",
                    arg, byte_addr, byte_addr / kBlockLen, block_len_,
                    block_addressed() ? "block-addressed" : "byte-addressed");

    if (!initialized_) {
        queue_r1(0x01);
        return;
    }

    if (crosses_block_boundary(byte_addr)) {
        // Same READ_BLK_MISALIGN = 0 contract as CMD17 above.
        sd_log()->warn("CMD18 misaligned byte address {:#010x} for a "
                       "{}-byte block → R1 ADDRESS_ERROR", byte_addr,
                       block_len_);
        queue_r1(static_cast<uint8_t>(0x20));
        return;
    }

    if (!load_read_block(byte_addr)) {
        sd_log()->warn(
            "CMD18 read past end of image: byte={:#010x} size={}",
            byte_addr, file_size_);
        // V12-DIVMMC-04 (Pass-12 reviewer fix, 2026-05-10): same as CMD17 —
        // R1 bit 6 PARAMETER_ERROR per SD Physical Layer Simplified Spec
        // § 7.3.2.1 (Table 7-9) when the argument is out of allowed range.
        queue_r1(0x40);
        resp_buf_.push_back(0x08);  // data error token: out of range
        state_ = State::RESPONDING;
        return;
    }

    // A host-file extent returned by the direct-NEX DISK_FILEMAP bridge is
    // consumed by existing Next software using NextZXOS's SDHC streaming
    // convention: the first byte is an immediate ready token, followed by
    // R1 and the first data token. Atic Atac Next relies on that sequence
    // (ZEsarUX has the same explicit compatibility path in mmc.c). Keep the
    // standards-oriented NCR×2 response and the GH #84 Nac gap byte for
    // ordinary card-image sectors.
    resp_buf_ = is_overlay_addr(byte_addr)
        ? std::vector<uint8_t>{ 0xFE, 0x00, 0xFE }
        : std::vector<uint8_t>{ kIdle, kIdle, 0x00, kIdle, 0xFE };
    resp_idx_         = 0;
    data_idx_         = 0;
    data_crc_count_   = 0;
    data_crc_         = sd_crc16(data_block_, block_len_);  // Task 26 item 3
    multi_block_      = true;
    // GH #94 — the stream advances one block length, which is the sector
    // stride in block-addressed mode and the CMD16 length in byte-addressed
    // mode (§ 4.3.2: "the block length ... for all following block commands").
    multi_block_addr_ = byte_addr + block_len_;
    state_            = State::SENDING_DATA;
}

void SdCardDevice::cmd24_write_single_block() {
    const uint32_t arg       = cmd_arg();
    const uint64_t byte_addr = arg_to_byte_addr(arg);  // GH #94, § 4.7.4
    sd_log()->debug("CMD24 WRITE_SINGLE_BLOCK arg={} → byte={:#010x} "
                    "(sector={}, {})",
                    arg, byte_addr, byte_addr / kBlockLen,
                    block_addressed() ? "block-addressed" : "byte-addressed");

    if (!initialized_) {
        queue_r1(0x01);
        return;
    }

    // GH #94 — WRITE_BL_PARTIAL = 0 in both CSDs this card reports (§ 5.3.2 /
    // § 5.3.3), so a write must be a whole physical block. A CMD16 that
    // shortened the block length for reads therefore makes CMD24 a
    // BLOCK_LEN_ERROR, carried in SPI mode by R1 bit 6 PARAMETER_ERROR
    // ("the command's argument (e.g. address, block length) was outside the
    // allowed range for this card", § 7.3.2.1 Table 7-9). Rejecting at R1
    // means no data phase, per § 4.3.4 — same shape as the past-EOF reject
    // below.
    if (block_len_ != kBlockLen) {
        sd_log()->warn("CMD24 with a {}-byte block length: this card declares "
                       "WRITE_BL_PARTIAL=0 → R1 PARAMETER_ERROR (no data "
                       "phase)", block_len_);
        queue_r1(0x40);
        return;
    }

    if (crosses_block_boundary(byte_addr)) {
        // WRITE_BLK_MISALIGN = 0 — same contract CMD17/CMD18 enforce for
        // reads. R1 bit 5 ADDRESS_ERROR, no data phase.
        sd_log()->warn("CMD24 misaligned byte address {:#010x} → "
                       "R1 ADDRESS_ERROR (no data phase)", byte_addr);
        queue_r1(static_cast<uint8_t>(0x20));
        return;
    }

    // V13-DIVMMC-01 (Pass-13 verify-audit fix, 2026-05-10): SD Physical
    // Layer Simplified Spec § 7.3.2.1 (Table 7-9) R1 layout — bit 6 =
    // PARAMETER_ERROR = "argument was out of the allowed range". When
    // the host issues CMD24 with a sector index past end-of-image the
    // card MUST set R1 bit 6 in the IMMEDIATE response and reject the
    // command BEFORE entering the data-write phase. Per § 4.3.4 (Data
    // Write Sequence): "If the card detects an error in the command
    // (R1 ≠ 0x00), it does not proceed to the data phase" — the data
    // phase is conditional on R1=0x00.
    //
    // Pass-12 V12-DIVMMC-02 already detected the past-EOF condition at
    // the END of the data phase (after receiving 512+CRC bytes) and
    // emitted the 0x0D (write error) data response token. That is also
    // spec-allowed when R1 was OK and the card later detected an error,
    // but it is the WEAKER variant and is asymmetric with the Pass-12
    // V12-DIVMMC-04 fix that gave CMD17/CMD18 the EARLIER R1-bit-6
    // rejection. This fix harmonises CMD24 with CMD17/18 by short-
    // circuiting on the past-EOF check before the bridge to data phase.
    //
    // Class-(c) latent for the boot path (TBBlue/FatFs/esxdos never
    // write past EOF), but real-spec divergence — the pre-fix path
    // accepted 514 wasted bytes from a misbehaving host instead of
    // rejecting at R1.
    if (byte_addr + kBlockLen > file_size_) {
        sd_log()->warn("CMD24 write past end of image: byte={:#010x} size={} → R1 PARAMETER_ERROR (no data phase)",
                       byte_addr, file_size_);
        queue_r1(0x40);  // R1 bit 6 = PARAMETER_ERROR
        // Do NOT set pending_write_after_r1_ — there is no data phase
        // for an out-of-range argument per SD spec § 4.3.4.
        return;
    }

    // Send R1, then wait for host to send 0xFE + 512 bytes + 2 CRC.
    // queue_r1() puts us in State::RESPONDING with {0xFF, 0x00}.  We set
    // the bridge flag so send() flips to RECEIVING_DATA only AFTER both
    // bytes have actually been emitted on MISO.  (Previously the next
    // line unconditionally clobbered RESPONDING with RECEIVING_DATA, so
    // send() in RECEIVING_DATA returned 0xFF and the host's R1 poll
    // hung forever — see FatFs send_cmd in tbblue diskio.c.)
    queue_r1(0x00);
    data_idx_ = 0;
    data_crc_count_ = 0;
    data_token_received_ = false;  // V12-DIVMMC-06 (Pass-12 reviewer fix)
    pending_write_after_r1_ = true;
}

void SdCardDevice::cmd55_app_cmd() {
    sd_log()->debug("CMD55 APP_CMD (next command is ACMD)");
    app_cmd_ = true;
    queue_r1(initialized_ ? 0x00 : 0x01);
}

// CMD9 SEND_CSD — return 16-byte CSD register inside a data block.
// Response shape (SPI mode): R1 + 0xFE data token + 16 CSD bytes + 2 CRC bytes.
// Required by enNxtmmc.rom / supervisor SD-init flow at $1925/$1F3D — without
// it the firmware reads $FF for all 16 CSD bytes and aborts the boot.
//
// GH #94 — the CSD *version* follows the capacity class the host negotiated,
// because the two versions do not merely differ by a version nibble: they
// encode the capacity in completely different fields.
//   High Capacity (CCS=1) → CSD Version 2.0, § 5.3.3:
//       capacity = (C_SIZE + 1) * 512 KB, C_SIZE 22 bits
//   Standard Capacity (CCS=0) → CSD Version 1.0, § 5.3.2:
//       capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN
//       C_SIZE 12 bits, C_SIZE_MULT 3 bits, READ_BL_LEN 4 bits
// A card that reported CCS=0 in its OCR and then a v2.0 CSD would be telling
// the host two different things about what it is.
void SdCardDevice::cmd9_send_csd() {
    sd_log()->debug("CMD9 SEND_CSD initialized={} class={}", initialized_,
                    block_addressed() ? "SDHC (CSD v2.0)"
                                      : "SDSC (CSD v1.0)");
    if (!initialized_) {
        queue_r1(0x01);
        return;
    }

    uint8_t csd[16] = {};
    if (block_addressed()) build_csd_v2(csd);
    else                   build_csd_v1(csd);

    // Response: NCR×2 + R1(0x00) + ≥1 idle Nac gap byte + token(0xFE) +
    // 16 CSD bytes + 2 CRC bytes. GH #98: same SD Physical Layer Simplified
    // Spec § 7.5.2 contract as CMD17/CMD18 (GH #84) — real cards always
    // insert read-access filler between R1 and the start-of-block token.
    // Task 26 item 1 (2 NCR) + item 3 (real CRC-16 over the 16 data bytes).
    const uint16_t crc = sd_crc16(csd, sizeof(csd));
    resp_buf_.clear();
    resp_buf_.push_back(kIdle);  // NCR 1
    resp_buf_.push_back(kIdle);  // NCR 2
    resp_buf_.push_back(0x00);  // R1: ready
    resp_buf_.push_back(kIdle); // Nac gap (§ 7.5.2, GH #98)
    resp_buf_.push_back(0xFE);  // start-of-data token
    for (auto b : csd) resp_buf_.push_back(b);
    resp_buf_.push_back(static_cast<uint8_t>(crc >> 8));   // CRC16 high
    resp_buf_.push_back(static_cast<uint8_t>(crc & 0xFF)); // CRC16 low
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

// CSD Version 1.0 — Standard Capacity, SD Phys Layer Simplified Spec § 5.3.2.
//
//   device capacity = BLOCKNR * BLOCK_LEN
//     BLOCKNR   = (C_SIZE + 1) * MULT
//     MULT      = 2^(C_SIZE_MULT + 2)     C_SIZE_MULT: 3 bits, 0..7
//     BLOCK_LEN = 2^READ_BL_LEN           READ_BL_LEN: 4 bits, 9..11
//
// jnext pins READ_BL_LEN = 9 so that the physical block the CSD declares is
// the one the SPI data path actually transfers, and so that the power-up
// block length is the 512 every host expects. C_SIZE is only 12 bits, so the
// largest capacity this encoding can express is 4096 * 2^9 * 2^9 = 1 GiB —
// which covers the canonical 1 GB NextZXOS image exactly. A larger image
// mounted by a host that negotiated HCS=0 saturates at that cap and says so
// in the log: over-declaring capacity would invite the host to read past the
// end of the file, which is the worse failure.
//
// READ_BL_PARTIAL is fixed to 1 in this CSD version (partial block reads are
// always allowed on an SD card), which is what makes CMD16's shorter block
// lengths meaningful — see cmd16_set_blocklen(). WRITE_BL_PARTIAL is 0, this
// card writes whole blocks only, and CMD24 enforces that.
void SdCardDevice::build_csd_v1(uint8_t csd[16]) const {
    constexpr uint32_t kReadBlLen = 9;                 // 2^9 = 512 = kBlockLen
    constexpr uint64_t kMaxCSize  = 4095;              // 12-bit field

    uint32_t c_size_mult = 0;
    uint64_t units       = 0;
    for (; c_size_mult <= 7; ++c_size_mult) {
        // MULT * BLOCK_LEN = 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN
        const uint64_t unit = 1ULL << (c_size_mult + 2 + kReadBlLen);
        units = file_size_ / unit;
        if (units <= kMaxCSize + 1) break;
    }
    if (c_size_mult > 7) {
        c_size_mult = 7;
        units       = kMaxCSize + 1;
        sd_log()->warn(
            "SDSC CSD v1.0 cannot express {} bytes (12-bit C_SIZE caps at "
            "1 GiB with READ_BL_LEN=9); declaring the cap", file_size_);
    }
    // Every other size rounds DOWN, which is the safe direction: a declared
    // capacity below the file only hides its tail. One size cannot round
    // down — an image under one MULT unit (2 KiB at C_SIZE_MULT=0), for which
    // the smallest register this encoding can express already says 2 KiB, so
    // there is no C_SIZE/C_SIZE_MULT pair that declares less. It is declared
    // and logged rather than silently rounded.
    //
    // A capacity declared above the file is the shape of an out-of-bounds
    // read, so it is worth saying exactly why it is not one here: nothing in
    // the read path consults the CSD. Every transfer is bounded against
    // file_size_ itself — load_read_block() refuses a block that would run
    // past it, and CMD17/CMD18/CMD24 turn that refusal into the documented
    // out-of-range R1 and data-error token (§ 7.3.2.1, § 7.3.3.3), which is
    // what SD-21/SD-23/SD-26 pin. A host that believed the over-declared
    // capacity and read the tail gets those errors, not host memory.
    if (units == 0) {
        units = 1;
        sd_log()->warn(
            "SDSC CSD v1.0 cannot express {} bytes (the smallest v1.0 "
            "capacity is one MULT unit = 2048 bytes); declaring 2048 — reads "
            "past the real end of the image are still refused",
            file_size_);
    }
    const uint32_t c_size = static_cast<uint32_t>(units - 1);

    // Currents are card properties the spec does not fix; these are ordinary
    // values (VDD_R_CURR_MIN/MAX = 35/45 mA, VDD_W_CURR_MIN/MAX = 35/45 mA).
    constexpr uint8_t kVddRCurrMin = 0x5, kVddRCurrMax = 0x5;
    constexpr uint8_t kVddWCurrMin = 0x5, kVddWCurrMax = 0x5;

    //   [0]  CSD_STRUCTURE=00 (v1.0) + reserved=000000        = 0x00
    //   [1]  TAAC = 0x26 (1.5 ms)
    //   [2]  NSAC = 0x00
    //   [3]  TRAN_SPEED = 0x32 (25 MHz)
    //   [4]  CCC[11:4] = 0x5B                     (CCC = 0x5B5)
    //   [5]  CCC[3:0]<<4 | READ_BL_LEN = 0x59
    //   [6]  READ_BL_PARTIAL=1, WRITE_BLK_MISALIGN=0, READ_BLK_MISALIGN=0,
    //        DSR_IMP=0, reserved=00, C_SIZE[11:10]
    //   [7]  C_SIZE[9:2]
    //   [8]  C_SIZE[1:0]<<6 | VDD_R_CURR_MIN<<3 | VDD_R_CURR_MAX
    //   [9]  VDD_W_CURR_MIN<<5 | VDD_W_CURR_MAX<<2 | C_SIZE_MULT[2:1]
    //   [10] C_SIZE_MULT[0]<<7 | ERASE_BLK_EN=1 | SECTOR_SIZE[6:1]=0x3F
    //   [11] SECTOR_SIZE[0]=1 <<7 | WP_GRP_SIZE=0x00          = 0x80
    //   [12] WP_GRP_ENABLE=0 + reserved=00 + R2W_FACTOR=010 +
    //        WRITE_BL_LEN[3:2]=10                             = 0x0A
    //   [13] WRITE_BL_LEN[1:0]=01 <<6 | WRITE_BL_PARTIAL=0 | reserved = 0x40
    //   [14] FILE_FORMAT_GRP/COPY/PERM_WP/TMP_WP/FILE_FORMAT/reserved = 0x00
    //   [15] CRC7[6:0]<<1 | end_bit=1 — see the v2.0 note on CRC7.
    csd[0]  = 0x00;
    csd[1]  = 0x26;
    csd[2]  = 0x00;
    csd[3]  = 0x32;
    csd[4]  = 0x5B;
    csd[5]  = static_cast<uint8_t>((0x5 << 4) | kReadBlLen);
    csd[6]  = static_cast<uint8_t>(0x80 | ((c_size >> 10) & 0x03));
    csd[7]  = static_cast<uint8_t>((c_size >> 2) & 0xFF);
    csd[8]  = static_cast<uint8_t>(((c_size & 0x03) << 6) |
                                   (kVddRCurrMin << 3) | kVddRCurrMax);
    csd[9]  = static_cast<uint8_t>((kVddWCurrMin << 5) | (kVddWCurrMax << 2) |
                                   ((c_size_mult >> 1) & 0x03));
    csd[10] = static_cast<uint8_t>(((c_size_mult & 0x01) << 7) | 0x40 | 0x3F);
    csd[11] = 0x80;
    csd[12] = 0x0A;
    csd[13] = 0x40;
    csd[14] = 0x00;
    csd[15] = 0x01;

    sd_log()->debug("CSD v1.0: C_SIZE={} C_SIZE_MULT={} READ_BL_LEN={} → "
                    "declared capacity {} bytes (image {} bytes)",
                    c_size, c_size_mult, kReadBlLen,
                    static_cast<uint64_t>(c_size + 1) <<
                        (c_size_mult + 2 + kReadBlLen),
                    file_size_);
}

// CSD Version 2.0 — High Capacity, SD Phys Layer Simplified Spec § 5.3.3.
// C_SIZE encodes capacity in 512 KB units: capacity = (C_SIZE + 1) * 512 KB.
void SdCardDevice::build_csd_v2(uint8_t csd[16]) const {
    // Compute C_SIZE for the actual SD-image size: capacity = (C_SIZE + 1) * 512 KB.
    uint64_t c_size = (file_size_ / (512ULL * 1024ULL));
    if (c_size > 0) c_size -= 1;
    const uint8_t c_size_22_16 = static_cast<uint8_t>((c_size >> 16) & 0x3F);
    const uint8_t c_size_15_8  = static_cast<uint8_t>((c_size >> 8) & 0xFF);
    const uint8_t c_size_7_0   = static_cast<uint8_t>(c_size & 0xFF);

    // CSD v2.0 register (16 bytes). Layout per SD spec § 5.3.3:
    //   [0]  CSD_STRUCTURE=01 + reserved=000000   = 0x40
    //   [1]  TAAC=0x0E (1ms; ignored in SDHC)
    //   [2]  NSAC=0x00
    //   [3]  TRAN_SPEED=0x32 (25 MHz)
    //   [4]  CCC[11:4]=0x5B
    //   [5]  CCC[3:0]=0xB | READ_BL_LEN=0x9 (=512)  → 0xBB? Actually CCC=0x5B5,
    //        so CCC[3:0]=0x5 and READ_BL_LEN=0x9 → 0x59? Use standard 0x5B5
    //        layout: byte 4 = CCC[11:4] = 0x5B, byte 5 = (CCC[3:0]<<4) | READ_BL_LEN = 0x59.
    //   [6]  flags=0x00 (READ_BL_PARTIAL=0, WRITE_BLK_MISALIGN=0, READ_BLK_MISALIGN=0,
    //        DSR_IMP=0, reserved=0)
    //   [7]  reserved=00 + C_SIZE[21:16]
    //   [8]  C_SIZE[15:8]
    //   [9]  C_SIZE[7:0]
    //   [10] reserved + ERASE_BLK_EN=1 + SECTOR_SIZE[6:1]   = 0x7F (ERASE_BLK_EN=1, SECTOR_SIZE=0x7F)
    //   [11] SECTOR_SIZE[0]=1 + WP_GRP_SIZE=0x00            = 0x80
    //   [12] WP_GRP_ENABLE=0 + reserved + R2W_FACTOR=2 + WRITE_BL_LEN[3:2]
    //        = 0x0A (R2W_FACTOR=010, WRITE_BL_LEN[3:2]=10)
    //   [13] WRITE_BL_LEN[1:0]=01 + WRITE_BL_PARTIAL=0 + reserved=00000 = 0x40
    //   [14] FILE_FORMAT_GRP=0 + COPY=0 + PERM_WRITE_PROTECT=0 +
    //        TMP_WRITE_PROTECT=0 + FILE_FORMAT=00 + reserved=00 = 0x00
    //   [15] CRC7[6:0]<<1 | end_bit=1 — most readers ignore CRC; use 0x01.
    //        (The same placeholder is used by the v1.0 builder above, so the
    //        two CSDs stay consistent with each other.)
    const uint8_t bytes[16] = {
        0x40, 0x0E, 0x00, 0x32,
        0x5B, 0x59, 0x00, c_size_22_16,
        c_size_15_8, c_size_7_0, 0x7F, 0x80,
        0x0A, 0x40, 0x00, 0x01
    };
    for (int i = 0; i < 16; ++i) csd[i] = bytes[i];

    sd_log()->debug("CSD v2.0: C_SIZE={} → declared capacity {} bytes "
                    "(image {} bytes)", c_size,
                    (c_size + 1) * 512ULL * 1024ULL, file_size_);
}

// CMD10 SEND_CID — return 16-byte CID register. Same shape as CMD9.
// Required by some firmware paths after CMD9 succeeds.
void SdCardDevice::cmd10_send_cid() {
    sd_log()->debug("CMD10 SEND_CID initialized={}", initialized_);
    if (!initialized_) {
        queue_r1(0x01);
        return;
    }

    // CID register (16 bytes). Generic-but-valid SDHC CID per SD spec § 5.2:
    //   [0]    Manufacturer ID = 0x03 (SanDisk-equivalent)
    //   [1-2]  OEM/App ID = "SD"
    //   [3-7]  Product Name = "JNEXT"
    //   [8]    Product Revision = 0x10 (1.0)
    //   [9-12] Product Serial Number = 0x12345678
    //   [13-14] Reserved + Manufacturing Date (year=2026, month=05) = 0x01 0xA5
    //   [15]   CRC7<<1 | 1 = 0x01
    //
    // V24-DIVMMC-01 (Pass-24 convergence pressure-test fix, 2026-05-11): per
    // SD Physical Layer Simplified Spec v6.00 § 5.2 Table 5-1, the
    // Manufacturing Date (MDT) field is 12 bits at CID bits [19:8] in format
    // `year_offset[11:4] | month[3:0]`. Year offset is from 2000. For year
    // 2026 / month 5: year_offset = 26 = 0x1A, MDT = (0x1A << 4) | 0x5 =
    // 0x1A5 = `0001 1010 0101`. Maps to:
    //   CID[13] = `reserved[3:0] | MDT[11:8]` = 0x00 | 0x1 = 0x01
    //   CID[14] = MDT[7:0] = 0xA5
    // Pre-fix CID[14] was 0x65 (= MDT[7:0] = `0110 0101`), which decoded to
    // year_offset = (CID[13][3:0] << 4) | MDT[7:4] = (0x1 << 4) | 0x6 = 0x16
    // = 22 -> year 2022. The block comment claimed 2026 but the bytes
    // encoded 2022 -- off by 4 years. Class-(c) cosmetic latent: TBBlue /
    // NextZXOS / FatFs do not inspect the CID's MDT, but a forensic firmware
    // / test rig / mmls-style tool that decodes the CID would report the
    // wrong manufacturing date. Fix: CID[14] -> 0xA5.
    const uint8_t cid[16] = {
        0x03, 'S', 'D', 'J',
        'N', 'E', 'X', 'T',
        0x10, 0x12, 0x34, 0x56,
        0x78, 0x01, 0xA5, 0x01
    };

    // NCR×2 + R1 + ≥1 idle Nac gap byte + token + 16 CID bytes + 2 CRC
    // bytes (§ 7.5.2, GH #98 — see cmd9_send_csd). Task 26 item 1 (2 NCR)
    // + item 3 (real CRC-16 over the 16 data bytes).
    const uint16_t crc = sd_crc16(cid, sizeof(cid));
    resp_buf_.clear();
    resp_buf_.push_back(kIdle);  // NCR 1
    resp_buf_.push_back(kIdle);  // NCR 2
    resp_buf_.push_back(0x00);
    resp_buf_.push_back(kIdle);  // Nac gap (§ 7.5.2, GH #98)
    resp_buf_.push_back(0xFE);
    for (auto b : cid) resp_buf_.push_back(b);
    resp_buf_.push_back(static_cast<uint8_t>(crc >> 8));
    resp_buf_.push_back(static_cast<uint8_t>(crc & 0xFF));
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::cmd58_read_ocr() {
    sd_log()->debug("CMD58 READ_OCR → initialized={} HCS={}",
                    initialized_, host_supports_sdhc_ ? 1 : 0);
    // OCR: bit 31=power up complete (if initialized), bit 30=SDHC (CCS).
    //
    // 2026-05-07: reverted from earlier ZEsarUX-style {$FF, $05, $00*4}
    // experiment. TBBLUE.FW's MMC_Init at SD_SEND_CMD_2_ARGS_TEST_BUSY
    // checks R1 with `and #0xFE` and rejects any non-zero result (treats
    // it as an error). $05 has bit 2 set → init aborts → "Error
    // initializing SD card!" displayed.
    //
    // Standard SDHC R3 response: NCR + R1=$00 (ready) + 4 OCR bytes.
    // TBBLUE.FW MMC_Init line 113 (mmc.s): `and #0x40` selects CCS bit.
    // If set → SDHC, skip CMD16.
    //
    // V17-DIVMMC-01 (Pass-17 verify-audit, 2026-05-10): per SD Phys
    // Layer Simplified Spec § 4.2.3 / § 5.1, CCS=1 only when ACMD41 was
    // issued with HCS=1 (host capacity support = SDHC). Pre-fix the
    // emulator unconditionally set CCS=1 — diverging from spec for any
    // host that issued ACMD41 with HCS=0 (SDSC compatibility mode).
    // The fix sources the CCS bit from `host_supports_sdhc_`, latched
    // by acmd41_sd_send_op_cond() from the most-recent ACMD41 arg bit
    // 30. TBBlue / NextZXOS / FatFs always set HCS=1, so the divergence
    // is class-(c) latent on the boot path.
    uint8_t ocr0 = 0x00;
    if (initialized_) {
        ocr0 = 0x80;  // bit 31 = power-up done
        if (host_supports_sdhc_) ocr0 |= 0x40;  // bit 30 = CCS (SDHC)
    }
    // Task 26 item 2 — no OCR payload byte may equal the $FF idle sentinel.
    // tbblue.fw's OCR reader skips $FF as idle (the same primitive it uses
    // to poll for R1), so an OCR byte of $FF is dropped and the remaining
    // OCR bytes misalign by one. Pre-fix the voltage-window byte [23:16]
    // was 0xFF (2.7-3.6 V, all 9 window bits set). We clear bit 23
    // (3.5-3.6 V) → 0x7F, still a valid, plausible voltage window
    // (2.7-3.5 V per SD Phys Layer Spec § 5.1 OCR) but no longer $FF.
    // ocr0/byte2(0x80)/byte3(0x00) and R1 (0x00/0x01) are already non-$FF.
    // This is a "works by luck" gap — the boot path currently succeeds
    // because the CCS check reads ocr0 before the misalignment bites.
    // Task 26 item 1 — 2 NCR idle bytes before R1.
    resp_buf_ = { kIdle, kIdle, static_cast<uint8_t>(initialized_ ? 0x00 : 0x01),
                  ocr0, 0x7F, 0x80, 0x00 };  // NCR×2 + R1 + 4-byte OCR (no $FF)
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::acmd41_sd_send_op_cond() {
    // V17-DIVMMC-01 (Pass-17 verify-audit, 2026-05-10): SD Phys Layer
    // Simplified Spec § 4.2.3 / § 5.1: capture the Host Capacity Support
    // bit (arg bit 30). The next CMD58 (READ_OCR) must reflect this in
    // the CCS bit of OCR — when HCS=0 the host only supports SDSC and the
    // card MUST report CCS=0 even if it is internally SDHC. Pre-fix the
    // emulator unconditionally set CCS=1 — diverging from spec for any
    // host that requests SDSC compatibility mode. Class-(c) latent on
    // boot path (TBBlue / NextZXOS / FatFs always set HCS=1).
    const uint32_t arg = cmd_arg();
    host_supports_sdhc_ = (arg & 0x40000000u) != 0;  // bit 30 = HCS
    // GH #94 round-2 review: completing ACMD41 puts the card in its defined
    // post-initialization state, and the block length that state carries is
    // the CSD default, 2^READ_BL_LEN = kBlockLen (§ 4.3.2). Resetting it here
    // is what makes "block_addressed() ⟹ block_len_ == kBlockLen" TRUE rather
    // than merely usual: for a card that has just negotiated high capacity
    // the 512 is definitional — § 4.3.2 says a High Capacity card "always"
    // uses a 512-byte fixed block length — and without this line the
    // sequence
    //     ACMD41(HCS=0) → CMD16(256) → ACMD41(HCS=1) → CMD18
    // re-enters block-addressed mode still carrying a 256-byte stride, so a
    // block-addressed stream walks onto addresses that are not block
    // boundaries. cmd16_set_blocklen() only ever assigns while the card is
    // byte-addressed, so this is the one place the stale value could survive
    // a change of capacity class.
    block_len_ = kBlockLen;
    sd_log()->debug("ACMD41 SD_SEND_OP_COND arg={:#010x} HCS={} → card initialized, ready",
                    arg, host_supports_sdhc_ ? 1 : 0);
    initialized_ = true;  // Card is now initialized
    queue_r1(0x00);  // R1: ready (not idle)
}

uint32_t SdCardDevice::cmd_arg() const {
    return (static_cast<uint32_t>(cmd_buf_[1]) << 24) |
           (static_cast<uint32_t>(cmd_buf_[2]) << 16) |
           (static_cast<uint32_t>(cmd_buf_[3]) << 8)  |
           static_cast<uint32_t>(cmd_buf_[4]);
}

uint64_t SdCardDevice::arg_to_byte_addr(uint32_t arg) const {
    // SD Phys Layer Simplified Spec § 4.7.4: the CMD17/CMD18/CMD24 argument is
    // a BYTE address on a Standard Capacity card and a 512-byte BLOCK address
    // on a High Capacity card. Which one applies is decided by CCS in the OCR,
    // and CCS is what the host negotiated with ACMD41's HCS bit (§ 4.2.3).
    return block_addressed() ? static_cast<uint64_t>(arg) * kBlockLen
                             : static_cast<uint64_t>(arg);
}

bool SdCardDevice::crosses_block_boundary(uint64_t byte_addr) const {
    if (block_addressed()) return false;  // argument cannot express a misalign
    if (block_len_ == 0) return false;
    return (byte_addr / kBlockLen) !=
           ((byte_addr + block_len_ - 1) / kBlockLen);
}

bool SdCardDevice::is_overlay_sector(uint32_t sector) const {
    return read_overlay_ && overlay_sector_count_ != 0 &&
           sector >= overlay_first_sector_ &&
           static_cast<uint64_t>(sector - overlay_first_sector_) <
               overlay_sector_count_;
}

bool SdCardDevice::is_overlay_addr(uint64_t byte_addr) const {
    // The direct-NEX file-map bridge indexes its overlay by SECTOR. A
    // byte-addressed card's addresses are offsets into the image and have
    // nothing to do with that numbering, so the overlay answers only a
    // block-addressed card — this is the guard SDSC-OVL-01 pins.
    //
    // The alignment test below is DEFENCE IN DEPTH, deliberately kept even
    // though acmd41_sd_send_op_cond() and cmd0_go_idle() now hold the
    // invariant that makes every block-addressed address a multiple of
    // kBlockLen. It is here because the failure it prevents is silent: an
    // address that merely DIVIDES into an overlaid sector is not that sector,
    // and serving the overlay for it hands the host 512 bytes of the wrong
    // file with no error anywhere. A round-2 review probe demonstrated
    // exactly that against a build where this line had been deleted on the
    // strength of the invariant — which at the time nothing enforced. The
    // invariant is enforced now; the guard stays, because the cost is one
    // comparison and the thing on the other side of it is a wrong-data bug
    // that no caller can detect.
    if (!block_addressed()) return false;
    if (byte_addr % kBlockLen != 0) return false;
    const uint64_t sector = byte_addr / kBlockLen;
    if (sector > 0xFFFFFFFFull) return false;
    return is_overlay_sector(static_cast<uint32_t>(sector));
}

bool SdCardDevice::load_read_block(uint64_t byte_addr) {
    if (is_overlay_addr(byte_addr))
        return read_overlay_(static_cast<uint32_t>(byte_addr / kBlockLen),
                             data_block_);

    // The same bound, written so it cannot WRAP: `byte_addr` comes from
    // `multi_block_addr_`, which a restore takes from a stream, and
    // `byte_addr + block_len_` overflows for an address near UINT64_MAX and
    // then passes. Subtracting instead cannot.
    //
    // HONEST SCOPE: this is the bound written correctly, not a second guard,
    // and with `block_len_` now checked at restore the wrap is UNREACHABLE —
    // it needs an address within 512 of UINT64_MAX, and such an address
    // fails at `seekg` anyway, because the cast to a signed `streamoff` goes
    // negative. A mutation confirmed no row can tell the two spellings
    // apart. It is kept because the correct spelling costs nothing and the
    // incorrect one was relying on that `seekg` fallback to be safe.
    if (byte_addr > file_size_ || block_len_ > file_size_ - byte_addr)
        return false;
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(byte_addr), std::ios::beg);
    if (!file_) return false;
    file_.read(reinterpret_cast<char*>(data_block_), block_len_);
    return file_.gcount() == static_cast<std::streamsize>(block_len_);
}

void SdCardDevice::queue_r1(uint8_t r1) {
    // Task 26 item 1 — prepend exactly 2 NCR idle bytes (0xFF) before R1
    // per SD Phys Layer Spec § 7.5.4 (Ncr = 1..8). Pre-fix used a single
    // idle byte; a fixed-count Ncr=2 firmware reader would then latch the
    // byte before R1. Poll-based readers (FatFs) skip the extra $FF
    // transparently. See kIdle note above.
    resp_buf_ = { kIdle, kIdle, r1 };
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

// ─────────────────────────────────────────────────────────────────────────
// State serialisation — GH #27 stage S6, design §10.2 P1 (defect D1)
// ─────────────────────────────────────────────────────────────────────────

namespace {

// §6.2 — an enum travels as a NAME in JSON, so an FSM renumbering is a visible
// name change in a schema diff instead of a silent re-interpretation of old
// files. Ordinals are `SdCardDevice::State`'s declaration order; a hole would
// be spelled `nullptr` and refused in both directions.
const char* const kSdStateNames[] = {
    "idle", "receiving_cmd", "responding", "sending_data",
    "receiving_data", "write_resp", "write_busy",
};
const jnext::save::EnumNames kSdStateEnum{
    kSdStateNames, sizeof(kSdStateNames) / sizeof(kSdStateNames[0])};

}  // namespace

// ── THE PER-FIELD SWEEP (GH #27 S6, after review) ───────────────────────
//
// Every field below is restored from a stream, so every field below is
// HOSTILE INPUT, and the question for each is not "is it saved" but "what
// does it size or index, and what happens when it is out of range". That
// question is asked here field by field rather than for the fields someone
// thought of, because thinking of them is exactly what has failed four times
// in this issue: S1's 167-byte archive forcing a 4.29 GB allocation, S3's
// `Ram::load_state`, S5's staging array, S6's own `resp_count` — and then
// `block_len_`, which the first version of this declaration left unchecked
// while its comments claimed to have audited for precisely this.
//
//   state_                  enum8; `BinReadDesc::do_enum8` REFUSES an
//                           ordinal outside the declared name set and leaves
//                           the member untouched. Bounded by the layer.
//   cmd_buf_[6]             fixed width, every byte value legal.
//   cmd_idx_                WRITE CURSOR into cmd_buf_. `receive()` has no
//                           bound of its own -> clamped here to sizeof-1.
//   resp_count              COUNT for resp_buf_ -> checked, never obeyed.
//   resp_buf staging[32]    fixed width, every byte value legal.
//   resp_idx_               read cursor; every consumer guards with
//                           `resp_idx_ < resp_buf_.size()`, so out of range
//                           reads as "exhausted". Bounded by consumer.
//   data_block_[512]        fixed width, every byte value legal.
//   data_idx_               cursor into data_block_; clamped here. A
//                           mutation shows the clamp is currently REDUNDANT
//                           — the read path guards `< block_len_` and the
//                           CMD24 write path `< kBlockLen` — and it is kept
//                           anyway, as policy: an index-like field is
//                           bounded where it ENTERS from a file, because
//                           there is one entry point and many consumers, and
//                           `block_len_` is what relying on the consumers
//                           looks like when one of them does not check.
//   data_crc_count_         consumers test `< 2`; any larger value means
//                           "done". Bounded by consumer.
//   data_crc_               a value; indexes nothing.
//   data_token_received_,   booleans; `read_bool` yields 0/1.
//   initialized_, app_cmd_,
//   host_supports_sdhc_,
//   multi_block_,
//   pending_write_after_r1_,
//   write_busy_pending_
//   block_len_              SIZES A WRITE — `file_.read(data_block_,
//                           block_len_)` into a 512-byte array — and two
//                           READS besides. Checked here against CMD16's own
//                           1..512 invariant. This is the one that was
//                           missed; see its declaration.
//   multi_block_addr_       byte address; bounded by `load_read_block()`,
//                           whose bound is now written so it cannot wrap —
//                           unreachable given the `block_len_` check above,
//                           and a mutation confirms no row can tell the two
//                           spellings apart.
//   busy_remaining_         a countdown of emitted bytes; u8-bounded, and a
//                           larger value only lengthens the busy phase.
//   persistent_response_byte_  a value; indexes nothing.
//   overlay_first_sector_,  compared, never used as an index, and inert
//   overlay_sector_count_   unless a host reader is installed.
//
// A field added to this declaration gets a line here, or the sweep stops
// being one.

void SdCardDevice::describe_state(jnext::save::StateDesc& d)
{
    // ── DECLARED DEFAULTS (§12.2) ────────────────────────────────────────
    //
    // The scalars below are the FIRST declarations in the tree to carry one,
    // and they carry one for a reason rather than as a habit: `state/
    // sdcard.json` is a new member whose field set has already grown twice
    // (GH #94 added `host_supports_sdhc_` and `block_len_`), so a reader
    // meeting a PARTIAL sdcard object is the realistic case, and the right
    // answer to a missing key is a coherent power-on card — not a refusal
    // and not a half-initialised FSM.
    //
    // Every default below is the value `SdCardDevice::reset()` establishes,
    // and row S6-SD-DEFAULTS-01 ASSERTS that field by field: §12.2's gate
    // exists because the default is a SECOND COPY of a power-on value with
    // nothing comparing it to the first, which is the shape of GH #246. The
    // gate runs here with no exemption — `reset()` is non-destructive and
    // value-establishing for every one of these.
    //
    // The AGGREGATES (`cmd_buf`, `resp_buf`, `data_block`) deliberately have
    // none: §12.2 marks a key required when no honest default exists, and a
    // missing command buffer or data block is "this file is not a snapshot",
    // not "take the power-on value".

    uint8_t st = static_cast<uint8_t>(state_);
    d.enum8("state", st, kSdStateEnum, static_cast<uint8_t>(State::IDLE));
    state_ = static_cast<State>(st);

    d.bytes("cmd_buf", cmd_buf_, sizeof(cmd_buf_));

    // `cmd_idx_` is an `int` indexing a 6-byte array; it travels as the u8 it
    // is, marshalled through a local. The read direction clamps, because the
    // value has come from a file by then and an index past `cmd_buf_` would
    // be a write out of bounds at the next command byte — `receive()`'s
    // `RECEIVING_CMD` arm does `cmd_buf_[cmd_idx_++] = tx` with no bound of
    // its own (unlike the `data_block_` write beside it, which guards with
    // `< kBlockLen`).
    //
    // The bound is the LAST WRITABLE SLOT, not the size: this is a write
    // cursor, so the largest legal in-flight value is `sizeof - 1`. An
    // earlier `> sizeof(cmd_buf_)` let a forged 6 through untouched and
    // wrote one byte past the array — the clamp was there and was off by
    // one, which is worse than none because it reads as covered.
    uint8_t cmd_idx = static_cast<uint8_t>(cmd_idx_);
    d.u8("cmd_idx", cmd_idx, 0);
    if (cmd_idx >= sizeof(cmd_buf_)) cmd_idx = sizeof(cmd_buf_) - 1;
    cmd_idx_ = static_cast<int>(cmd_idx);

    // `resp_buf_` is the one variable-length member, and it is declared at a
    // FIXED capacity: a count plus `kRespBufCapacity` bytes, padded. The
    // count is a description, never a size — the staging array is sized by
    // the declaration and the loop below is bounded by it, so a file
    // claiming 4 billion response bytes resizes nothing.
    {
        // Marshalled UNCONDITIONALLY in both directions — the idiom
        // `Keyboard::describe_state` uses for `auto_queue_`, and for the same
        // reason: a declaration must not branch on `d.writing()`, or the
        // schema walk and the two data walks stop being provably the same
        // field list. On the write path the rebuild at the foot stores back
        // exactly what it just took, which is `state_desc.h`'s write-back
        // shape (c) and is pinned by row S6-SD-SAVE-PURE.
        if (resp_buf_.size() > kRespBufCapacity) {
            // Unreachable for every response this class builds — the longest
            // is CMD9/CMD10's 23 bytes — but a future longer one must be a
            // LOUD failure and not a silently truncated stream.
            d.fail("sdcard.resp_buf longer than the declared capacity");
        }
        uint8_t staging[kRespBufCapacity] = {};
        std::size_t live = resp_buf_.size();
        if (live > kRespBufCapacity) live = kRespBufCapacity;
        for (std::size_t i = 0; i < live; ++i) staging[i] = resp_buf_[i];
        uint8_t resp_n = static_cast<uint8_t>(live);

        d.u8("resp_count", resp_n, 0);
        d.bytes("resp_buf", staging, kRespBufCapacity);

        // The count is CHECKED, never obeyed. `staging` is sized by the
        // DECLARATION and the stream always carries exactly
        // `kRespBufCapacity` bytes whatever the count claims, so a forged
        // count can neither desync the stream nor size a write — it only
        // decides how many of the declared bytes become live.
        std::size_t restored = resp_n;
        if (restored > kRespBufCapacity) restored = kRespBufCapacity;
        resp_buf_.assign(staging, staging + restored);
    }

    uint16_t resp_idx = static_cast<uint16_t>(resp_idx_);
    d.u16("resp_idx", resp_idx, 0);
    resp_idx_ = resp_idx;

    // The block in flight. 512 bytes, so §6.1 case 3 would make it a blob on
    // a peripheral store >= 8 KB — it is well under that, so it is `bytes`
    // and travels as a hex string in the JSON, like the sprite attributes.
    d.bytes("data_block", data_block_, sizeof(data_block_));

    uint16_t data_idx = static_cast<uint16_t>(data_idx_);
    d.u16("data_idx", data_idx, 0);
    if (data_idx > sizeof(data_block_)) data_idx = sizeof(data_block_);
    data_idx_ = static_cast<int>(data_idx);

    uint8_t data_crc_count = static_cast<uint8_t>(data_crc_count_);
    d.u8("data_crc_count", data_crc_count, 0);
    data_crc_count_ = static_cast<int>(data_crc_count);

    // NO DECLARED DEFAULT, and the §12.2 gate is what found that: `reset()`
    // does not clear `data_crc_` — it is recomputed by `load_read_block()`
    // whenever a block is primed, so there is no power-on value for it to
    // establish. Declaring 0 here would have been a second copy of a value
    // the first copy does not hold, which is exactly the drift the gate
    // exists to catch. It is REQUIRED instead, which is also the right
    // coupling: it is the CRC of `data_block`, and that aggregate is
    // required too.
    d.u16("data_crc", data_crc_);
    d.boolean("data_token_received", data_token_received_, false);

    d.boolean("initialized", initialized_, false);
    d.boolean("app_cmd", app_cmd_, false);
    d.boolean("host_supports_sdhc", host_supports_sdhc_, false);

    // `block_len_` SIZES A WRITE, and it is the one field in this
    // declaration that does: `load_read_block()` passes it straight to
    // `file_.read(data_block_, block_len_)`, and `data_block_` is
    // `uint8_t[512]`. A value from a file must therefore be checked against
    // the class's own invariant, not merely carried.
    //
    // That invariant already exists and is stated at the one place the
    // RUNTIME sets this field: CMD16 (`cmd16_set_blocklen()`) answers
    // `arg == 0 || arg > kBlockLen` with R1 PARAMETER_ERROR and leaves the
    // length untouched, because the allowed range is 1..2^READ_BL_LEN and
    // an over-long block is the spec's named BLOCK_LEN_ERROR. The loader
    // simply did not enforce what the command enforces, which is how a
    // forged 4 bytes produced a 3 584-byte overflow that ran through
    // `data_idx_`, the booleans, the overlay `std::function` and into the
    // `std::fstream` declared after the array — corrupting its locale and
    // crashing in the destructor.
    //
    // It is also an out-of-range READ for values too small to reach the
    // write: `sd_crc16(data_block_, block_len_)` and the `SENDING_DATA`
    // loop's `data_idx_ < block_len_` both walk the same array.
    //
    // Out of range restores the power-on length, which is the only value a
    // block-addressed card — the one every Next negotiates — ever has.
    uint32_t block_len = block_len_;
    d.u32("block_len", block_len, kBlockLen);
    if (block_len == 0 || block_len > kBlockLen) block_len = kBlockLen;
    block_len_ = block_len;

    d.boolean("multi_block", multi_block_, false);
    d.u64("multi_block_addr", multi_block_addr_, 0);

    d.boolean("pending_write_after_r1", pending_write_after_r1_, false);
    d.boolean("write_busy_pending", write_busy_pending_, false);
    d.u8("busy_remaining", busy_remaining_, 0);
    d.u8("persistent_response_byte", persistent_response_byte_, 0xFF);

    // The read-overlay WINDOW, not the overlay itself (§10.2 P1 asks for the
    // window). The `ReadOverlay` is a host-side `std::function` installed by
    // the extended-NEX file bridge and cannot travel; `is_overlay_sector()`
    // tests it first, so a restored window with no reader installed is inert
    // rather than wrong — the card simply serves the image, which is what a
    // machine with no bridge has.
    d.u32("overlay_first_sector", overlay_first_sector_, 0);
    d.u32("overlay_sector_count", overlay_sector_count_, 0);
}

void SdCardDevice::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void SdCardDevice::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
}

bool SdCardDevice::transfer_in_flight() const
{
    // IDLE with nothing queued is the only state in which the card owes the
    // host nothing.
    //
    // A `multi_block_ ||` term was written here first, on the reasoning that
    // an open CMD18 stream survives a CS deassert (row CMD18-05) and would
    // therefore be in flight with `state_` back at IDLE. A MUTATION proved it
    // could not change any outcome, and reading `deselect()` says why: the
    // pause path returns EARLY and freezes `state_` at SENDING_DATA, and
    // every other path that reaches `state_ = IDLE` clears `multi_block_` in
    // the same breath. `multi_block_` therefore implies `state_ != IDLE`, and
    // a term that cannot decide anything is a term nobody can test.
    return state_ != State::IDLE || resp_idx_ < resp_buf_.size();
}
