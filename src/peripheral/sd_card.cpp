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

    // Pass-5 verify-audit fix (2026-05-09): full SPI-protocol state reset
    // on mount. The pre-fix code cleared only state_/initialized_/app_cmd_/
    // cmd_idx_, leaving resp_buf_, resp_idx_, data_idx_, data_crc_count_,
    // multi_block_, multi_block_sector_, persistent_response_byte_,
    // data_block_, cmd_buf_ untouched. A runtime mount swap (e.g. user
    // changes --sd-card while a CMD18 stream is in flight, or a CMD17
    // SENDING_DATA is mid-transfer) would let send() continue emitting
    // stale data from the previous image's data_block_ or re-prime
    // multi_block_sector_ as if streaming the old card. Use the same
    // canonical full reset() the soft-reset path uses to avoid drift —
    // mount() is conceptually a hard cycle of the card.
    reset();

    sd_log()->info("mounted SD image: {} ({} bytes, {} MB)",
                   path, file_size_, file_size_ / (1024 * 1024));
    return true;
}

void SdCardDevice::unmount() {
    if (file_.is_open()) {
        file_.close();
        sd_log()->info("SD image unmounted");
    }
    // Pass-8 verify-audit fix (2026-05-09): symmetric with mount(). Pre-fix
    // unmount() cleared only state_ / initialized_ / file_size_ /
    // pending_write_after_r1_, leaving cmd_idx_, resp_buf_, resp_idx_,
    // data_idx_, data_crc_count_, app_cmd_, multi_block_, multi_block_sector_
    // and persistent_response_byte_ stale. After-the-fact remount via mount()
    // does call reset() and is therefore safe — but a bare unmount() (e.g.
    // user-initiated Eject SD with no follow-up mount) left the card in a
    // half-cleared state. The early-out in receive()/send() (`if
    // (!file_.is_open()) return 0xFF`) made it benign for runtime I/O but
    // surfaced as bugs in tests and cross-tool snapshot inspection. Use the
    // same canonical full reset() that mount() uses; the only difference
    // unmount() needs is closing the file (above) and clearing file_size_.
    reset();
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
    data_token_received_ = false;  // V12-DIVMMC-06 (Pass-12 reviewer fix)
    app_cmd_ = false;
    // CMD18 multi-block mode is aborted by CS deassert (real cards terminate
    // the stream on CS high without needing CMD12 in SPI mode).
    multi_block_        = false;
    multi_block_sector_ = 0;
    // CMD24 R1-bridge flag also clears: a CS deassert mid-CMD24 (after R1
    // queued but before the data phase started) must not leave the card
    // primed to flip to RECEIVING_DATA on the next host activation.
    pending_write_after_r1_ = false;
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
            if (data_idx_ < 512) {
                data_block_[data_idx_++] = tx;
                break;
            }
            // Collecting 2 CRC bytes (ignored)
            data_crc_count_++;
            if (data_crc_count_ >= 2) {
                uint32_t sector = cmd_arg();
                uint64_t byte_addr = static_cast<uint64_t>(sector) * 512;
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
                bool write_ok = (byte_addr + 512 <= file_size_);
                if (write_ok) {
                    file_.seekp(static_cast<std::streamoff>(byte_addr), std::ios::beg);
                    file_.write(reinterpret_cast<const char*>(data_block_), 512);
                    file_.flush();
                    sd_log()->debug("CMD24 wrote 512 bytes at sector {} (byte={:#010x})", sector, byte_addr);
                } else {
                    sd_log()->warn("CMD24 write past end of image: sector={} byte={:#010x} size={} → write-error response token",
                                   sector, byte_addr, file_size_);
                }
                state_ = State::WRITE_RESP;
                // 0x05 = data accepted; 0x0D = write error.
                resp_buf_ = { static_cast<uint8_t>(write_ok ? 0x05 : 0x0D) };
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
                data_token_received_ = false;  // V12-DIVMMC-06 (Pass-12 reviewer fix)
                persistent_response_byte_ = 0xFF;
                // Defensive: any new command in-flight during a CMD18
                // stream also terminates the multi-block state, even
                // if it isn't CMD12.  The SD spec only legalises
                // CMD12 or CS-deassert as abort mechanisms, but a
                // mis-behaving host (or a test) that sends a different
                // command mid-stream must not leave multi_block_ set
                // and silently resume after the new command's block.
                multi_block_        = false;
                multi_block_sector_ = 0;
                // Same logic for the CMD24 R1-bridge flag: a new command
                // arriving mid-CMD24 (between the queued R1 and the data
                // phase) must not leave the bridge armed and silently
                // promote the next response into a write-data phase.
                pending_write_after_r1_ = false;
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
            if (data_idx_ < 512) {
                return data_block_[data_idx_++];
            }
            if (data_crc_count_ < 2) {
                data_crc_count_++;
                return 0x00;
            }
            // Block complete.  For CMD18 multi-block mode, load the next
            // sector and re-prime for another 0xFE+data+CRC cycle.  Emit one
            // 0xFF as inter-block filler so the host's "skip until non-0xFF
            // then expect 0xFE" token-poll (see rcvr_datablock in tbblue
            // src/firmware/app/src/ff/diskio.c:156-164) finds the new token
            // on a subsequent read rather than the same byte that terminated
            // the previous block's CRC.
            if (multi_block_) {
                uint64_t byte_addr =
                    static_cast<uint64_t>(multi_block_sector_) * 512;
                if (byte_addr + 512 > file_size_) {
                    // V14-DIVMMC-01 (Pass-14 verify-audit fix, 2026-05-10):
                    // SD Physical Layer Simplified Spec § 7.3.3.3 (Data
                    // Error Token format): when the card cannot deliver
                    // the requested data block (out-of-range, ECC failure,
                    // CC error, generic error), it sends a 1-byte error
                    // token in place of the 0xFE start-of-block token.
                    // Bit layout: 0bxxx0_xxxE_CCO_R where O = OUT_OF_RANGE,
                    // C = CC error, E = ERROR, R = card_ECC_failed. The
                    // OUT_OF_RANGE-only token is 0x08.
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
                        "CMD18 read past end of image at sector={}; "
                        "emitting data error token 0x08 and ending stream",
                        multi_block_sector_);
                    multi_block_        = false;
                    multi_block_sector_ = 0;
                    state_              = State::IDLE;
                    return 0x08;  // data error token: out of range
                }
                file_.seekg(static_cast<std::streamoff>(byte_addr),
                            std::ios::beg);
                file_.read(reinterpret_cast<char*>(data_block_), 512);
                sd_log()->trace(
                    "CMD18 next block sector={} (byte={:#010x})",
                    multi_block_sector_, byte_addr);
                ++multi_block_sector_;
                resp_buf_       = { 0xFE };   // just the data token between blocks
                resp_idx_       = 0;
                data_idx_       = 0;
                data_crc_count_ = 0;
                return 0xFF;                   // inter-block filler
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

void SdCardDevice::cmd0_go_idle() {
    sd_log()->debug("CMD0 GO_IDLE_STATE → card reset");
    initialized_ = false;
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
    resp_buf_ = { 0xFF, r1, 0x10, 0x00, 0x01, check };  // NCR + R1 + R7 (cmd ver=1)
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::cmd12_stop_transmission() {
    sd_log()->debug("CMD12 STOP_TRANSMISSION");
    // Abort any in-progress multi-block transfer and return to idle.
    data_idx_ = 0;
    data_crc_count_ = 0;
    // Terminate CMD18 streaming if active.
    multi_block_        = false;
    multi_block_sector_ = 0;
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
    resp_buf_  = { 0xFF, r1, 0x00 };  // NCR + R1 + R2
    resp_idx_  = 0;
    state_     = State::RESPONDING;
}

void SdCardDevice::cmd16_set_blocklen() {
    // CMD16 SET_BLOCKLEN: for SDHC the blocklen is fixed at 512.  Setting any
    // other value is illegal (SD spec § 4.9.1: "In CCS=1 (SDHC) cards, the
    // block length is fixed to 512 bytes").  We accept arg=512 silently and
    // reject any other value with the illegal-command bit.
    uint32_t arg = cmd_arg();
    sd_log()->debug("CMD16 SET_BLOCKLEN arg={}", arg);
    if (arg == 512) {
        queue_r1(initialized_ ? 0x00 : 0x01);
    } else {
        // Pass-5 verify-audit fix (2026-05-09): R1 idle bit (bit 0) reflects
        // the card's idle state and must NOT be hard-coded to 1 when the
        // card has completed ACMD41 init. SD spec § 4.9.1 R1 layout:
        //   bit 0 = in idle state, bit 2 = illegal command.
        // Pre-fix returned 0x05 (= idle + illegal) unconditionally, which
        // misreports a post-init card as "still in idle" to the host.
        // Boot path uses arg=512 only, so the prior bug was latent —
        // class-(b/c) — but the corrected response is spec-faithful.
        const uint8_t idle_bit = initialized_ ? 0x00 : 0x01;
        queue_r1(static_cast<uint8_t>(idle_bit | 0x04));  // illegal command bit
    }
}

void SdCardDevice::cmd23_set_block_count() {
    // CMD23 SET_BLOCK_COUNT: hint to the card about how many blocks the
    // host plans to write/read (used as a pre-erase optimization).  We
    // don't model the optimization; just ack it.
    sd_log()->debug("CMD23 SET_BLOCK_COUNT count={}", cmd_arg());
    queue_r1(initialized_ ? 0x00 : 0x01);
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

    file_.seekg(static_cast<std::streamoff>(byte_addr), std::ios::beg);
    file_.read(reinterpret_cast<char*>(data_block_), 512);

    // Response: NCR + R1 (0x00 = OK), then 0xFE data start token, then 512 bytes
    resp_buf_ = { 0xFF, 0x00, 0xFE };
    resp_idx_ = 0;
    data_idx_ = 0;
    data_crc_count_ = 0;
    state_ = State::SENDING_DATA;
}

void SdCardDevice::cmd18_read_multiple_block() {
    // CMD18 READ_MULTIPLE_BLOCK: like CMD17 for the FIRST block
    // (NCR + R1 + 0xFE + 512 + CRC), but after the block's CRC the card
    // continues streaming more (token + 512 + CRC) for sector+1, sector+2,
    // ...  until the host issues CMD12 STOP_TRANSMISSION (or deselects CS
    // in SPI mode).  See send()'s SENDING_DATA branch for the
    // between-blocks re-prime logic.
    uint32_t sector     = cmd_arg();
    uint64_t byte_addr  = static_cast<uint64_t>(sector) * 512;
    sd_log()->debug("CMD18 READ_MULTIPLE_BLOCK sector={} (byte={:#010x})",
                    sector, byte_addr);

    if (!initialized_) {
        queue_r1(0x01);
        return;
    }

    if (byte_addr + 512 > file_size_) {
        sd_log()->warn(
            "CMD18 read past end of image: sector={} byte={:#010x} size={}",
            sector, byte_addr, file_size_);
        // V12-DIVMMC-04 (Pass-12 reviewer fix, 2026-05-10): same as CMD17 —
        // R1 bit 6 PARAMETER_ERROR per SD Physical Layer Simplified Spec
        // § 7.3.2.1 (Table 7-9) when the argument is out of allowed range.
        queue_r1(0x40);
        resp_buf_.push_back(0x08);  // data error token: out of range
        state_ = State::RESPONDING;
        return;
    }

    // Load first block.
    file_.seekg(static_cast<std::streamoff>(byte_addr), std::ios::beg);
    file_.read(reinterpret_cast<char*>(data_block_), 512);

    // First-block response is the normal CMD17 shape: NCR + R1 + token +
    // 512 + CRC.  Subsequent blocks emitted in send() skip NCR/R1 and
    // send only token + 512 + CRC.
    resp_buf_           = { 0xFF, 0x00, 0xFE };
    resp_idx_           = 0;
    data_idx_           = 0;
    data_crc_count_     = 0;
    multi_block_        = true;
    multi_block_sector_ = sector + 1;  // next sector to stream
    state_              = State::SENDING_DATA;
}

void SdCardDevice::cmd24_write_single_block() {
    uint32_t sector = cmd_arg();
    uint64_t byte_addr = static_cast<uint64_t>(sector) * 512;
    sd_log()->debug("CMD24 WRITE_SINGLE_BLOCK sector={} (byte={:#010x})",
                    sector, byte_addr);

    if (!initialized_) {
        queue_r1(0x01);
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
    if (byte_addr + 512 > file_size_) {
        sd_log()->warn("CMD24 write past end of image: sector={} byte={:#010x} size={} → R1 PARAMETER_ERROR (no data phase)",
                       sector, byte_addr, file_size_);
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
// We use the SDHC CSD v2.0 layout (CSD_STRUCTURE=01) where C_SIZE encodes
// capacity in 512 KB units: capacity = (C_SIZE + 1) * 512 KB. Required by
// enNxtmmc.rom / supervisor SD-init flow at $1925/$1F3D — without it the
// firmware reads $FF for all 16 CSD bytes and aborts the boot.
// See SD Physical Layer Simplified Spec v6.00, section 5.3.3 for layout.
void SdCardDevice::cmd9_send_csd() {
    sd_log()->debug("CMD9 SEND_CSD initialized={}", initialized_);
    if (!initialized_) {
        queue_r1(0x01);
        return;
    }

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
    const uint8_t csd[16] = {
        0x40, 0x0E, 0x00, 0x32,
        0x5B, 0x59, 0x00, c_size_22_16,
        c_size_15_8, c_size_7_0, 0x7F, 0x80,
        0x0A, 0x40, 0x00, 0x01
    };

    // Response: NCR(0xFF) + R1(0x00) + token(0xFE) + 16 CSD bytes + 2 CRC bytes.
    resp_buf_.clear();
    resp_buf_.push_back(0xFF);  // NCR
    resp_buf_.push_back(0x00);  // R1: ready
    resp_buf_.push_back(0xFE);  // start-of-data token
    for (auto b : csd) resp_buf_.push_back(b);
    resp_buf_.push_back(0x00);  // CRC16 high (firmware typically ignores)
    resp_buf_.push_back(0x00);  // CRC16 low
    resp_idx_ = 0;
    state_ = State::RESPONDING;
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
    //   [13-14] Reserved + Manufacturing Date (year=2026, month=05) = 0x01 0x65
    //   [15]   CRC7<<1 | 1 = 0x01
    const uint8_t cid[16] = {
        0x03, 'S', 'D', 'J',
        'N', 'E', 'X', 'T',
        0x10, 0x12, 0x34, 0x56,
        0x78, 0x01, 0x65, 0x01
    };

    resp_buf_.clear();
    resp_buf_.push_back(0xFF);
    resp_buf_.push_back(0x00);
    resp_buf_.push_back(0xFE);
    for (auto b : cid) resp_buf_.push_back(b);
    resp_buf_.push_back(0x00);
    resp_buf_.push_back(0x00);
    resp_idx_ = 0;
    state_ = State::RESPONDING;
}

void SdCardDevice::cmd58_read_ocr() {
    sd_log()->debug("CMD58 READ_OCR → initialized={} SDHC=1", initialized_);
    // OCR: bit 31=power up complete (if initialized), bit 30=SDHC.
    //
    // 2026-05-07: reverted from earlier ZEsarUX-style {$FF, $05, $00*4}
    // experiment. TBBLUE.FW's MMC_Init at SD_SEND_CMD_2_ARGS_TEST_BUSY
    // checks R1 with `and #0xFE` and rejects any non-zero result (treats
    // it as an error). $05 has bit 2 set → init aborts → "Error
    // initializing SD card!" displayed.
    //
    // Standard SDHC R3 response: NCR + R1=$00 (ready) + 4 OCR bytes with
    // CCS=1 (bit 30 of OCR = bit 6 of byte 0). TBBLUE.FW MMC_Init line
    // 113 (mmc.s): `and #0x40` selects CCS bit. If set → SDHC, skip CMD16.
    uint8_t ocr0 = initialized_ ? 0xC0 : 0x00;  // bit 31 = power-up done, bit 30 = CCS
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
