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
    // VHDL zxnext.vhd:3308-3309 — on `reset='1'` the FPGA process drives
    // `port_e7_reg <= (others => '1')`, which physically deasserts every
    // chip-select line (spi_ss_*_n all '1'). On real hardware that CS
    // rising edge resets each connected SPI slave's protocol state — SD
    // cards in particular abort whatever transaction was in flight (mid
    // CMD17 SENDING_DATA, mid CMD18 multi-block, mid CMD24 RECEIVING_DATA,
    // an unfinished R1 in RESPONDING, etc.) and return to their IDLE
    // command-acceptance state.
    //
    // Pass-11 verify-audit fix (2026-05-10): pre-fix `reset()` set `cs_ =
    // 0xFF` directly without invoking the `deselect()` callback on any
    // currently-selected device. The SD card backend (SdCardDevice)
    // therefore retained whatever protocol state it had at the moment of
    // reset — `state_=SENDING_DATA` post-CMD17, `multi_block_=true` post-
    // CMD18, `pending_write_after_r1_=true` post-CMD24 R1, `resp_buf_` /
    // `resp_idx_` / `data_idx_` / `data_crc_count_` non-zero — until the
    // next firmware-driven CS write. The `receive()` default case has a
    // defensive "any byte 0x40-0x7F starts a new command" branch that
    // mostly papers over this for the write path, but `send()` still
    // streams stale bytes from the prior block until the firmware writes
    // a command. That makes the divergence from VHDL semantics latent on
    // the boot path (firmware always writes before reading) but real for
    // any caller that reads first (test fixtures, future save-state-
    // restore replay, or a host bug).
    //
    // The fix mirrors the VHDL: walk every previously-selected slot and
    // pulse `deselect()` exactly the way `write_cs(0xFF)` would. That
    // resets the SD card's protocol-state FFs (state_, cmd_idx_,
    // resp_buf_, multi_block_*, pending_write_after_r1_,
    // persistent_response_byte_) but preserves `initialized_` — matching
    // the comment at emulator.cpp's reset cascade ("soft reset pulls CS
    // high momentarily but does NOT reset the card's internal init
    // state"). The PASS-7 invariant (do NOT clear `devices_[]`) is
    // preserved — the `deselect()` notifies the slaves, the wires remain
    // physically connected.
    for (int i = 0; i < kMaxDevices; ++i) {
        if (!(cs_ & (1 << i)) && devices_[i]) {
            devices_[i]->deselect();
        }
    }

    cs_ = 0xFF;       // all CS lines deasserted (active-low)
    // V12-DIVMMC-01 (Pass-12 verify-audit fix, 2026-05-10): do NOT clobber
    // `rx_data_` on system reset. VHDL zxnext.vhd:3282-3298 instantiates
    // `spi_master_mod` with `i_reset => '0'` HARDWIRED (line 3285, comment
    // "hard reset done through core load"). The internal `miso_dat`
    // register at spi_master.vhd:159-168 has a `if i_reset = '1' then
    // miso_dat <= (others => '1')` clause, but with `i_reset` fixed at '0'
    // that clause NEVER fires. miso_dat therefore retains its last latched
    // value across every system reset (only FPGA bitstream load can affect
    // it). Pre-fix `rx_data_ = 0xFF` here clobbered the previous-transfer
    // byte on every Emulator::reset() — diverging from VHDL whenever
    // firmware reads port 0xEB after a soft reset (`NR 0x02 ← 0x01`)
    // before issuing any new SPI write. Practical impact on the boot path
    // is nil (firmware always issues a CMD before reading), but
    // VHDL-faithfulness was the wrong way around. The pre-fix comment
    // ("VHDL resets miso_dat to all ones") was technically correct only
    // for a hypothetical core where `i_reset` was wired to the system
    // reset — not the actual zxnext core. Class-(c) → corrected.
    //
    // PASS-7 verify-audit fix (2026-05-09): do NOT clear devices_ on reset.
    // The devices_ array models the physical hardware connections between
    // the SPI master and its slaves (SD card on CS0). Clearing this
    // unconditionally on reset() unhooks the slave bindings — VHDL has no
    // such notion (the spi_ss_*_n outputs are wired permanently to the
    // physical pins). Today the bug is masked because Emulator::init()
    // calls attach_device() AFTER reset(), but any caller (including future
    // tests, save-state restore paths, or runtime SD swap) that calls
    // spi_.reset() without re-attaching would silently leave the bus
    // unconfigured. Reset clears FF state (cs_, rx_data_), not bindings.
    //
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
    spi_log()->debug("device attached on CS {}", cs_id);
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
    } else if (val == 0x7F && flash_cs_enable_) {
        // FPGA-flash select — VHDL zxnext.vhd:3319 gates this decode on
        //   (nr_03_config_mode='1') OR (nr_02_reset_type(2)='1')
        // Pass-8 verify-audit fix (2026-05-09): pre-fix dropped X"7F" to
        // 0xFF unconditionally, blocking the firmware's flash-IPL path
        // (the only legal time the firmware writes 0x7F to port 0xE7 is
        // during the boot-time flash-readback / config-mode flow). The
        // composite gate is fed via `set_flash_cs_enable` from Emulator,
        // mirroring the VHDL OR of the two signals. When the gate is
        // closed, X"7F" still falls through to "all deselected" (else
        // branch below), matching VHDL line 3322. Class-(b) → resolved.
        decoded = 0x7F;
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
    //
    // Verify3-Audit fix (2026-05-09): when no slave is selected, VHDL
    // zxnext.vhd:3278-3280 forces `spi_miso <= '1'` (default-else). The
    // spi_master is always fed `i_spi_wr=1` regardless of CS state, so a
    // transfer still happens and miso_dat captures all-ones (0xFF). Mirror
    // that here: a write with no active device still updates rx_data_ to
    // 0xFF instead of leaving the previous slave's stale byte hanging.
    SpiDevice* dev = active_device();
    if (dev) {
        rx_data_ = dev->receive(val);
        spi_log()->debug("write tx={:#04x}, rx={:#04x}", val, rx_data_);
    } else {
        rx_data_ = 0xFF;
    }
}

uint8_t SpiMaster::read_data() {
    // VHDL spi_master.vhd:162-166: miso_dat is latched from the input
    // shift register at state_last_d (one cycle AFTER the transfer ends).
    // This means a read returns the result of the PREVIOUS transfer, not
    // the one just started. The new transfer's result is stored for the
    // next read.
    //
    // Verify3-Audit fix (2026-05-09): same VHDL `spi_miso <= '1'` when no
    // SS line is asserted (zxnext.vhd:3278-3280). With no active device,
    // the new transfer still occurs in VHDL and miso_dat captures 0xFF.
    // The C++ pre-fix left rx_data_ unchanged → the next read would
    // surface a stale byte from the prior slave. Force 0xFF to match.
    uint8_t prev = rx_data_;
    SpiDevice* dev = active_device();
    if (dev) {
        rx_data_ = dev->send();
        spi_log()->debug("read → returning prev={:#04x}, new rx={:#04x}",
                         prev, rx_data_);
    } else {
        rx_data_ = 0xFF;
    }
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
