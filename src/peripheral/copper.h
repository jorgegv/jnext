#pragma once
#include <cstdint>
#include <array>

class NextReg;

namespace jnext { namespace save { class StateDesc; } }

/// Copper co-processor — display-synchronized instruction engine.
///
/// Executes WAIT and MOVE instructions from a 1K x 16-bit instruction RAM,
/// driven by the raster counters (hc, vc).  Writes to NextREG registers
/// bypass the CPU path, enabling palette changes and other effects at
/// precise raster positions.
///
/// VHDL reference: device/copper.vhd + zxnext.vhd (integration, write logic)
///
/// Instruction encoding (16 bits):
///   bit 15 = 1  =>  WAIT:  [15]=1, [14:9]=hpos(6 bits), [8:0]=vpos(9 bits)
///                   HALT is encoded as WAIT with vpos=511 (never matches)
///   bit 15 = 0  =>  MOVE:  [15]=0, [14:8]=nextreg(7 bits), [7:0]=value
///                   NOP is encoded as MOVE with nextreg=0 (no write pulse)
///
/// WAIT comparison (from VHDL copper.vhd:94):
///   triggered when  vc == vpos  AND  hc >= (((hpos << 3) + 12) mod 512)
/// The threshold add is done in 9-bit UNSIGNED arithmetic (the width of
/// `hcount_i`, copper.vhd:35), so it wraps — hpos=63 gives 4, not 516.
///
/// Modes (NextREG 0x62 bits 7:6):
///   00 = stop (copper disabled)
///   01 = start, reset PC to 0
///   10 = start from current PC (no reset)
///   11 = start, reset PC to 0 at each frame (vc=0, hc=0)
class DebugState;

class Copper {
public:
    Copper();

    // ── GH #276 B2 §4.3 — the three Copper event sites ──────────────────
    //
    // NO-SUBSCRIBER COST: one predicated branch on `events_armed_`, a plain
    // bool member, next to the `should_log()` branch each site already carries.
    // `Copper::execute` is 8-12 % of the copper-demo / beast profiles (Task 27),
    // which is why §4.3 requires a `make bench` row on those two workloads with
    // no subscriber before this lands.
    //
    // The flag is set by the backend from `subscribe()`/`unsubscribe()` and is
    // NOT derived from the table pointer: reading `debug_state_->event_table()
    // ->has_kind(Copper)` per master cycle would be three dependent loads in
    // the hottest loop in the emulator.
    void set_debug_state(DebugState* ds) { debug_state_ = ds; }

    /// ONE FLAG PER SUB-KIND, not one for the engine. B2 shipped a single
    /// `events_armed_` set from `has_kind(Copper) || has_kind(NextRegWrite)`, so
    /// a `Halt`-only subscriber accumulated ~16 `Move` entries per instruction
    /// slot — ring space, which is observable (see `Mmu::watch_write_`'s own
    /// argument for the same thing). `EventTable::has_copper_sub_kind()` existed
    /// for exactly this and had ZERO callers, which is the same
    /// declared-but-uncalled shape `is_halt()` had before B2 gave it one.
    ///
    /// `move` carries the `NextRegWrite` term too: a MOVE is ONE ring entry
    /// fanned out at the drain to both kinds, so an NR subscriber alone must
    /// still arm the MOVE site.
    void set_events_armed(bool move, bool wait, bool halt) {
        move_events_armed_ = move;
        wait_events_armed_ = wait;
        halt_events_armed_ = halt;
    }
    bool move_events_armed() const { return move_events_armed_; }
    bool wait_events_armed() const { return wait_events_armed_; }
    bool halt_events_armed() const { return halt_events_armed_; }
    bool events_armed() const {
        return move_events_armed_ || wait_events_armed_ || halt_events_armed_;
    }

    void reset();

    /// Execute copper instructions for the given raster position.
    /// Called once per 28 MHz tick (or batched per scanline).
    /// In the emulator's line-accurate model this is called once per
    /// scanline with the line's vc and a sweep of hc values.
    ///
    /// @param hc  VHDL `hcount_i` — the ULA 7 MHz PIXEL counter `hc_ula`
    ///            (zxnext.vhd:3949 + :6737; zxula_timing.vhd:427-438),
    ///            9-bit, zero at raw hc == c_min_hactive - 11. NOT the
    ///            28 MHz master-cycle offset into the line, and NOT
    ///            anchored at the raw line start (GH #181).
    /// @param vc  vertical counter (9-bit, 0-based) in the SAME counter
    ///            frame — VHDL `cvc` (zxnext.vhd:3950 `vcount_i => cvc`,
    ///            zxula_timing.vhd:457-472), less the NR 0x64 offset,
    ///            which execute() re-applies.
    /// @param nextreg  reference to NextReg for MOVE writes
    /// @param cvc_offset  the offset `cvc` counts from at this cycle: the
    ///            NR 0x64 value it was last RELOADED from at
    ///            `ula_min_vactive` (zxula_timing.vhd:457-462), which is not
    ///            the register once NR 0x64 has been written since (GH #290).
    ///            The reload lives in the timing block, not in the Copper, so
    ///            the Emulator passes it in.
    void execute(int hc, int vc, NextReg& nextreg, uint8_t cvc_offset);

    /// The stand-alone form, for a caller with no timing model: `cvc` counts
    /// from the NR 0x64 register as it is now, i.e. the reload is taken to
    /// have happened before the first line the caller steps. The Emulator
    /// never uses it (GH #290).
    void execute(int hc, int vc, NextReg& nextreg) {
        execute(hc, vc, nextreg, offset_);
    }

    // ── NextREG write handlers ──────────────────────────────────────

    /// NextREG 0x60 — copper data (8-bit write, auto-increment addr).
    /// On even addr: stores MSB.  On odd addr: commits 16-bit word.
    void write_reg_0x60(uint8_t val);

    /// NextREG 0x61 — copper write address low 8 bits.
    void write_reg_0x61(uint8_t val);

    /// NextREG 0x62 — copper control: bits 7:6 = mode, bits 2:0 = addr[10:8].
    void write_reg_0x62(uint8_t val);

    /// NextREG 0x63 — copper data (8-bit write, auto-increment addr).
    /// Same as 0x60 but nr_copper_write_8 = 0 (LSB-first pairing).
    void write_reg_0x63(uint8_t val);

    /// NextREG 0x64 — Copper vertical line offset (8 bits).
    /// VHDL: zxnext.vhd:1197 (signal), :5024 (reset to 0), :5442 (write),
    ///       :6723 (wired as i_cu_offset into zxula_timing.vhd).
    /// On the first active display line (ula_min_vactive) cvc is reloaded
    /// to unsigned('0' & i_cu_offset) — see zxula_timing.vhd:455-468.
    void write_reg_0x64(uint8_t val) { offset_ = val; }

    // ── Read-back for NextREG read path ─────────────────────────────

    /// NextREG 0x61 read — returns write address low byte.
    uint8_t read_reg_0x61() const;

    /// NextREG 0x62 read — returns mode (bits 7:6) and addr high (bits 2:0).
    uint8_t read_reg_0x62() const;

    /// NextREG 0x64 read — returns the cached vertical offset byte.
    /// VHDL: zxnext.vhd:6090 — port_253b_dat <= nr_64_copper_offset.
    uint8_t read_reg_0x64() const { return offset_; }

    /// Public accessor for the vertical offset (used by emulator wiring
    /// and tests). Returns the last value written via NR 0x64 — the
    /// register, which `cvc` samples only at `ula_min_vactive` (GH #290).
    uint8_t offset() const { return offset_; }

    /// Configure the Copper-vertical-counter wrap value (c_max_vc).
    /// Per VHDL zxula_timing.vhd the value depends on the active timing
    /// mode: 263 (VGA 60 Hz), 310 (VGA 50 Hz), 311 (other 50 Hz slots),
    /// 319 (HDMI). The Emulator should call this once at init-time with
    /// (timing_.lines_per_frame - 1). Default on construction is 311
    /// (VGA-0 50 Hz) so stand-alone unit tests have a sane wrap without
    /// having to wire the full timing model.
    void set_c_max_vc(int v) { c_max_vc_ = v; }

    // ── Accessors for debug / testing ───────────────────────────────

    uint16_t pc() const { return pc_; }
    uint8_t  mode() const { return mode_; }
    bool     is_running() const { return mode_ != 0; }
    /// A NR 0x62 mode write that execute() has not latched yet
    /// (copper.vhd:70, last_state_s /= copper_en_i). A stopped Copper must
    /// still be stepped once to latch mode 00, or a later restart sees no edge.
    bool     mode_edge_pending() const { return last_mode_ != mode_; }
    uint16_t instruction(uint16_t addr) const { return instructions_[addr & 0x3FF]; }

    /// The whole 1K x 16-bit instruction RAM, for a debugger view (GH #276
    /// INS-09 `copper()`, which hands out a {ptr, size} pair rather than
    /// copying 2 KB per panel refresh). Valid while the Copper lives.
    const uint16_t* program_data() const { return instructions_.data(); }

    /// `hcount_i` of the MOVE currently being issued, or -1 when no
    /// Copper MOVE is in flight (GH #270).
    ///
    /// `execute()` calls `NextReg::write()` synchronously from inside the
    /// per-28 MHz-cycle loop, so any NextREG write handler that needs to
    /// know WHERE along the scanline the write landed can ask for it here.
    /// The value is the same `hc` argument `execute()` was called with —
    /// i.e. the VHDL `hc_ula` 7 MHz pixel counter (zxnext.vhd:3949,
    /// copper.vhd:35; GH #181), NOT the raw frame counter. Callers that
    /// need raw `hc` add `VideoTiming::hc_ula_zero_raw_hc()` and wrap.
    ///
    /// Set only for the duration of the `nextreg.write()` call, so a CPU
    /// write (which reaches the same handlers) reads -1 and falls back to
    /// its own raster position. Deliberately NOT part of save/load state:
    /// it is never non-negative outside that call.
    int active_move_hc() const { return move_hc_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// GH #27 S4 — the ONE field list (design §9.2).
    void describe_state(jnext::save::StateDesc& d);

private:
    std::array<uint16_t, 1024> instructions_{};  // 1K instruction RAM
    uint16_t pc_ = 0;          // program counter (10-bit, 0-1023)
    uint8_t  mode_ = 0;        // 2-bit mode from NextREG 0x62
    uint8_t  last_mode_ = 0;   // previous mode (for edge detection)
    bool     move_pending_ = false;  // MOVE output needs one cycle to clear
    int      move_hc_ = -1;    // GH #270 — see active_move_hc()

    // Write address state machine
    uint16_t write_addr_ = 0;         // 11-bit byte address into instruction RAM
    uint8_t  write_data_stored_ = 0;  // stored MSB byte for 16-bit writes

    // ── Vertical timing (NR 0x64 + c_max_vc wrap) ───────────────────
    //
    // VHDL model (zxula_timing.vhd):
    //   cvc reloads to ('0' & i_cu_offset) on the first active display
    //   line; increments per line; wraps to 0 when cvc == c_max_vc.
    //   c_max_vc is 9-bit, timing-mode dependent:
    //     HDMI     = 319  (zxula_timing.vhd:168)
    //     VGA 50Hz = 310  (zxula_timing.vhd:204)
    //     VGA 60Hz = 263  (zxula_timing.vhd:238)
    //     (other 50 Hz slot values up to 311 — see the remaining case
    //      branches in zxula_timing.vhd).
    //
    // The emulator today passes a rebased vc already equal to 0 at the
    // first active display line, so we only need to model the offset
    // reload and the wrap. Copper::execute() computes
    //   cvc_effective = (vc + cvc_offset) % (c_max_vc_ + 1)
    // and uses that for WAIT vpos compares. `cvc_offset` is the value of
    // the last reload, which the Emulator passes in; `offset_` below is the
    // NR 0x64 register the reload samples (GH #290).
    //
    // Defaults:
    //   offset_   = 0    — matches VHDL reset (zxnext.vhd:5024).
    //   c_max_vc_ = 311  — sane stand-alone default (VGA-0 50 Hz).
    //                      The Emulator overrides this at init-time.
    //   c_max_vc_ is configuration (timing mode), NOT hardware state,
    //   so reset() leaves it untouched.
    uint8_t offset_ = 0;
    int     c_max_vc_ = 311;

    // APPENDED (GH #276 B2) — host-side debugger wiring, not machine state, so
    // neither appears in save_state/load_state.
    DebugState* debug_state_ = nullptr;
    bool        move_events_armed_ = false;
    bool        wait_events_armed_ = false;
    bool        halt_events_armed_ = false;
    /// §4.3 `Copper{Halt}` fires on the EDGE: a HALT stalls for every remaining
    /// master cycle, so one latch per cycle would fill the ring by itself. NOT
    /// machine state — it is the event site's own edge memory — but it IS
    /// cleared everywhere the PC is reset, so a restarted program latches again.
    bool        halt_stalling_ = false;

    /// Out-of-line: latch one of the three sub-kinds. `hc_ula` and `cvc` are
    /// execute()'s own two arguments — the counters the Copper compares
    /// against, NOT the raw frame counters (GH #181).
    void latch_move_(uint8_t reg, uint8_t val, uint8_t prev, int hc_ula, int cvc);
    void latch_wait_(int vpos, int hthresh, int hc_ula, int cvc);
    void latch_halt_(int hc_ula, int cvc);
};
