#include "contention.h"

void ContentionModel::build(MachineType type) {
    // Full reset (init-time only): clear LUT + gate state + slot mirror.
    type_ = type;
    for (auto& row : lut_) row.fill(0);
    for (int i = 0; i < 4; ++i) contended_slot_[i] = false;

    // Reset VHDL-faithful gate inputs to their power-on defaults.
    mem_active_page_           = 0;
    cpu_speed_                 = 0;
    pending_cpu_speed_         = 0;
    contention_disable_        = false;
    contention_disable_shadow_ = false;
    port_7ffd_io_en_           = false;

    // V24-MEM-01 / V25-MEM-01 fix — seed the machine_timing axis
    // (NR 0x03 bits 6:4 / VHDL eff_nr_03_machine_timing → one-hot
    // machine_timing_*) from the supplied MachineType. This matches
    // the canonical NextZXOS boot path where tim_sel == typ_sel; the
    // two axes evolve independently after init via NR 0x03's gated
    // writes (zxnext.vhd:5124-5135 vs :5137-5145). Both shadow and
    // effective fields are seeded so the build()-time gate is live
    // before the first per-frame commit edge fires.
    machine_timing_         = default_machine_timing_for(type);
    pending_machine_timing_ = machine_timing_;

    rebuild_for_type(type);
}

void ContentionModel::rebuild_for_type(MachineType type) {
    // Hot-rebuild path: update `type_` + recompute LUT. Slot mirror
    // (contended_slot_[]) is also re-seeded with the per-machine default
    // (slot 1 = bank 5 contended on 48K/128K/+3). Dynamic gate state
    // (mem_active_page / cpu_speed / pending / contention_disable / shadow)
    // is preserved so a runtime NR 0x03 commit doesn't unwind paging or
    // contention-disable state.
    type_ = type;
    for (auto& row : lut_) row.fill(0);
    for (int i = 0; i < 4; ++i) contended_slot_[i] = false;

    if (type == MachineType::ZXN_ISSUE2) return;

    // VHDL zxula.vhd:582-583 — wait_s gating window:
    //   hc_adj <= i_hc(3 downto 0) + 1;            (4-bit, mod-16 wrap)
    //   wait_s <= '1' when ((hc_adj(3:2) /= "00")
    //                    or (hc_adj(3:1) = "000" and i_timing_p3 = '1'))
    //                  and i_hc(8) = '0'
    //                  and border_active_v = '0'
    //                  and i_contention_en = '1' else '0';
    //
    // border_active_v (zxula.vhd:414):
    //   border_active_v <= i_vc(8) or (i_vc(7) and i_vc(6));
    //   i.e. border when vc>=192 (bit7=1 AND bit6=1) OR vc>=256 (bit8=1).
    //   Contention fires for vc in [0, 191].
    //
    // 48K/128K: contention when hc_adj[3:2] != 0  (hc_adj in {4..15})
    // +3:       also when hc_adj[3:1] == 0         (hc_adj in {0,1})
    // Note hc_adj is 4-bit so hc(3:0)=15 → hc_adj=0 (wraps): no contention.
    //
    // V24-MEM-01 / V25-MEM-01 fix — the `+3-only` corner of wait_s is
    // gated on VHDL `i_timing_p3 = machine_timing_p3` (zxnext.vhd:4449),
    // the tim_sel axis — NOT on `machine_type_p3`. Use the
    // effective `machine_timing_` field here. (The rebuild_for_type
    // entry-point still takes a `MachineType` for backwards
    // compatibility with NR 0x03 typ_sel-commit callers; those
    // callers must also push the matching `set_machine_timing()` if
    // they want both axes aligned — which is exactly what the
    // canonical NextZXOS boot path does.)
    // Task 54 (2026-07-13): magnitude has period 16 ticks (8 T-states),
    // keyed on hc(3:0) — see the derivation in contention_tick(). The
    // pre-fix `pattern[hc & 7]` repeated the 8-T-state pattern every
    // 4 T-states (units bug: T-state table indexed by pixel ticks).
    static const uint8_t pat48[16] = {0,0,0,6,6,5,5,4,4,3,3,2,2,1,1,0};
    static const uint8_t patp3[16] = {1,0,0,7,7,6,6,5,5,4,4,3,3,2,2,1};
    const bool is_p3 = (machine_timing_ == MachineTimingMode::TimingPlus3);

    for (int vc = 0; vc <= 191; ++vc) {
        // hc(8)=0 ⇒ hc in [0, 255] — the inner 256 ticks of each line.
        for (int hc = 0; hc <= 255; ++hc) {
            int hc_adj = ((hc & 0xF) + 1) & 0xF;          // 4-bit wrap
            bool contend = (hc_adj & 0xC) != 0;           // hc_adj[3:2] != 0
            if (is_p3) contend |= (hc_adj & 0xE) == 0;    // hc_adj[3:1] == 0
            if (contend) {
                lut_[vc][hc] = is_p3 ? patp3[hc & 0xF] : pat48[hc & 0xF];
            }
        }
    }

    // Default contended slot: slot 1 (0x4000-0x7FFF) = bank 5, always contended
    // on 48K/128K/+3 in normal paging mode.
    contended_slot_[1] = true;
}

uint8_t ContentionModel::delay(uint16_t hc, uint16_t vc) const {
    if (vc >= 320 || hc >= 456) return 0;
    return lut_[vc][hc];
}

bool ContentionModel::is_contended_address(uint16_t addr) const {
    int slot = addr >> 14;  // 0x0000→0, 0x4000→1, 0x8000→2, 0xC000→3
    return contended_slot_[slot];
}

bool ContentionModel::is_contended_access() const {
    // VHDL zxnext.vhd:4481 — i_contention_en =
    //     (not eff_nr_08_contention_disable)
    // AND (not machine_timing_pentagon)
    // AND (not cpu_speed(1)) AND (not cpu_speed(0)).
    // Any non-zero cpu_speed disables contention.
    //
    // V24-MEM-01 / V25-MEM-01 fix — the `machine_timing_pentagon` term
    // is now sourced from `machine_timing_` (tim_sel axis, NR 0x03
    // bits 6:4 via the video-frame-latched eff_nr_03_machine_timing).
    // Pre-fix the term was treated as always-'0' since the standalone
    // Pentagon MachineType was retired (Wave 0.3 follow-up). VHDL keys
    // it on `machine_timing_pentagon`, which is independent from
    // `machine_type_*`: a user-supplied NR 0x03 with bit-4-of-tim_sel=1
    // disables contention regardless of typ_sel.
    //
    // WONT — expansion-bus override (zxnext.vhd:5816-5820):
    //     if expbus_en = '0' then cpu_speed <= nr_07_cpu_speed;
    //     else                    cpu_speed <= expbus_speed;
    // jnext has no NextBUS (`expbus_*`) emulation: see expbus probes in
    // src/peripheral/nmi_source.cpp / src/cpu/im2.cpp which only model
    // the NMI / IM2 enable bits, not the bus-master speed override. We
    // therefore drive `cpu_speed_` directly from NR 0x07 with no
    // expbus_speed substitution path. Documented as WONT per
    // feedback_wont_taxonomy.md; revisit only if NextBUS emulation is
    // ever added.
    if (contention_disable_) return false;
    if (cpu_speed_ != 0)     return false;
    if (machine_timing_ == MachineTimingMode::TimingPentagon) return false;

    // VHDL zxnext.vhd:4489 — mem_contend = '0' when mem_active_page(7:4) /= "0000".
    if ((mem_active_page_ & 0xF0) != 0) return false;

    // V24-MEM-01 / V25-MEM-01 fix — the per-machine mem_contend decode
    // is keyed on `machine_timing_*` (zxnext.vhd:4490-4492), the
    // tim_sel axis. Pre-fix this switched on `type_` (MachineType,
    // typ_sel axis), diverging when a user wrote NR 0x03 with bits 6:4
    // != bits 2:0.
    const uint8_t low = mem_active_page_ & 0x0F;
    switch (machine_timing_) {
        case MachineTimingMode::Timing48:
            // VHDL zxnext.vhd:4490 — 48K: contend iff mem_active_page(3:1) = "101".
            return ((low >> 1) & 0x07) == 0x05;
        case MachineTimingMode::Timing128:
            // VHDL zxnext.vhd:4491 — 128K: contend iff mem_active_page(1) = '1' (odd banks).
            return (low & 0x02) != 0;
        case MachineTimingMode::TimingPlus3:
            // VHDL zxnext.vhd:4492 — +3: contend iff mem_active_page(3) = '1' (banks >= 4).
            return (low & 0x08) != 0;
        case MachineTimingMode::TimingPentagon:
            // Already handled by the i_contention_en gate above.
            return false;
    }
    return false;
}

bool ContentionModel::port_contend(uint16_t cpu_a, bool port_ulap_io_en) const {
    // VHDL zxnext.vhd:4496 —
    //     port_contend <= (not cpu_a(0)) or port_7ffd_active
    //                                    or port_bf3b
    //                                    or port_ff3b;
    //
    // Term 1 — even-port: every CPU IORQ to an even port asserts
    // contention regardless of timing mode or NR gating.
    if ((cpu_a & 0x0001) == 0) return true;

    // Term 2 — ULA+ index/data ports (zxnext.vhd:2685-2686):
    //     port_bf3b <= port_bfxx_msb and port_3b_lsb and port_ulap_io_en;
    //     port_ff3b <= port_ffxx_msb and port_3b_lsb and port_ulap_io_en;
    // i.e. asserted iff the full 16-bit address is exactly 0xBF3B / 0xFF3B
    // AND `port_ulap_io_en` (NR 0x85 bit 0 → internal_port_enable(24),
    // zxnext.vhd:2439) is set. The ULA+ ports are odd (low byte 0x3B),
    // so without this OR-term the even-port test above would already
    // have rejected them.
    if (port_ulap_io_en && (cpu_a == 0xBF3B || cpu_a == 0xFF3B)) {
        return true;
    }

    // Term 3 — port_7ffd_active (zxnext.vhd:2594):
    //     port_7ffd_active <= '1' when port_7ffd = '1'
    //                              and (s128_timing_hw_en = '1'
    //                                or p3_timing_hw_en = '1')
    //                         else '0';
    // port_7ffd address decode (zxnext.vhd:2593):
    //     port_7ffd <= cpu_a(15)='0' AND (cpu_a(14)='1' OR NOT p3_timing)
    //                  AND port_fd (cpu_a(1:0)="01")
    //                  AND NOT port_1ffd (cpu_a(13:12) /= "01")
    //                  AND port_7ffd_io_en (NR 0x82 bit 1).
    //
    // Verify9-memory class-(c) → class-(a) fix: pre-fix the bare-class
    // accessor dropped this term, so port 0x7FFD writes on 128K/+3
    // missed contention. The gate inputs (`port_7ffd_io_en_` shadow +
    // `type_`) are now state on the ContentionModel, pushed by the
    // runtime caller (Emulator) on every NR 0x82 write.
    // V24-MEM-01 / V25-MEM-01 fix — `port_7ffd_active` gating
    // (zxnext.vhd:2594) consults `s128_timing_hw_en OR p3_timing_hw_en`,
    // which are direct mirrors of `machine_timing_128 / machine_timing_p3`
    // (zxnext.vhd:2457-2458 — assigned inside the per-cycle FSM block).
    // The address-decode gate at :2593 also consults `p3_timing_hw_en`
    // (the `cpu_a(14)='1' OR NOT p3_timing` clause). Both must key on
    // the tim_sel axis (`machine_timing_`), not on `type_` (typ_sel).
    const bool tim_128 = (machine_timing_ == MachineTimingMode::Timing128);
    const bool tim_p3  = (machine_timing_ == MachineTimingMode::TimingPlus3);
    if (port_7ffd_io_en_ &&
        (tim_128 || tim_p3) &&
        (cpu_a & 0x8000) == 0 &&                        // cpu_a(15)='0'
        (cpu_a & 0x0003) == 0x0001 &&                   // port_fd: A1:0="01"
        (cpu_a & 0x3000) != 0x1000 &&                   // NOT port_1ffd: A13:12 != "01"
        (!tim_p3 || (cpu_a & 0x4000) != 0)) {           // +3 timing: also require A14='1'
        return true;
    }

    return false;
}

uint8_t ContentionModel::contention_tick(bool mreq_n, bool iorq_n,
                                         bool /*rd_n*/, bool /*wr_n*/,
                                         uint16_t cpu_a,
                                         uint16_t hc, uint16_t vc,
                                         bool port_ulap_io_en) const {
    // VHDL zxula.vhd:579-600 mirror.
    //
    // The VHDL emits two outputs — `o_cpu_contend` (48K/128K clock-stretch)
    // and `o_cpu_wait_n` (+3 WAIT_n stall). Both fire only when `wait_s='1'`
    // (window gate at zxula.vhd:582-583) AND the appropriate `i_contention_*`
    // signal is high AND the cycle's MREQ/IORQ matches the path. The two
    // paths are mutually exclusive on `i_timing_p3`.
    //
    // jnext folds both paths into a single returned T-state count. The
    // caller (FUSE-callback memory/I/O cycle) adds it to the bus-cycle
    // budget regardless of machine type — the path discrimination is
    // internal to this function.

    // --- Enable gate (zxnext.vhd:4481) -------------------------------
    // i_contention_en = (not eff_nr_08_contention_disable)
    //               AND (not machine_timing_pentagon)
    //               AND (not cpu_speed(1)) AND (not cpu_speed(0))
    //
    // V24-MEM-01 / V25-MEM-01 fix — the Pentagon-disables-contention
    // term is now sourced from `machine_timing_` (tim_sel axis). See
    // is_contended_access() comment.
    if (contention_disable_) return 0;
    if (cpu_speed_ != 0)     return 0;
    if (machine_timing_ == MachineTimingMode::TimingPentagon) return 0;

    // --- Window gate (zxula.vhd:582-583) ------------------------------
    // wait_s = '1' iff:
    //   ((hc_adj(3:2) /= "00") OR (hc_adj(3:1)=000 AND timing_p3=1))
    //   AND hc(8)=0 AND border_active_v=0 AND contention_en=1
    //
    // hc_adj is 4-bit, so the +1 wraps at 16. hc(3:0)=15 → hc_adj=0,
    // hc(3:0)=0..14 → hc_adj=1..15.
    //
    // V24-MEM-01 / V25-MEM-01 fix — `i_timing_p3` per zxnext.vhd:4449 is
    // `machine_timing_p3` (tim_sel axis). Use machine_timing_ here.
    if ((hc & 0x100) != 0) return 0;                  // hc(8)=1 → border
    // border_active_v = vc(8) | (vc(7) & vc(6)) = vc>=192 OR vc>=256.
    if ((vc & 0x100) != 0) return 0;
    if ((vc & 0xC0) == 0xC0) return 0;
    const int hc_adj = ((hc & 0x0F) + 1) & 0x0F;       // 4-bit wrap
    const bool is_p3 = (machine_timing_ == MachineTimingMode::TimingPlus3);
    bool wait_s = (hc_adj & 0xC) != 0;                 // hc_adj(3:2) != 0
    if (is_p3) wait_s |= (hc_adj & 0xE) == 0;          // hc_adj(3:1) = 000
    if (!wait_s) return 0;

    // --- mem_contend (zxnext.vhd:4489-4493) ---------------------------
    // mem_contend evaluated against current mem_active_page + machine_timing_.
    //   '0' when page(7:4) /= "0000"
    //   '1' for tim_48  & page(3:1)=101  (bank 5 only)
    //   '1' for tim_128 & page(1)=1      (odd banks)
    //   '1' for tim_p3  & page(3)=1      (banks >= 4)
    //
    // V24-MEM-01 / V25-MEM-01 fix — switch on `machine_timing_` (tim_sel
    // axis) not on `type_` (typ_sel axis). VHDL :4490-4492 keys on
    // `machine_timing_*`. Pentagon timing has no entry here — its
    // `i_contention_en` term above already returned 0.
    bool mem_c = false;
    if ((mem_active_page_ & 0xF0) == 0) {
        const uint8_t low = mem_active_page_ & 0x0F;
        switch (machine_timing_) {
            case MachineTimingMode::Timing48:
                mem_c = ((low >> 1) & 0x07) == 0x05;
                break;
            case MachineTimingMode::Timing128:
                mem_c = (low & 0x02) != 0;
                break;
            case MachineTimingMode::TimingPlus3:
                mem_c = (low & 0x08) != 0;
                break;
            case MachineTimingMode::TimingPentagon:
                mem_c = false;
                break;
        }
    }

    // --- port_contend (zxnext.vhd:4496) -------------------------------
    // The bare-class `port_contend()` accessor handles even-port + ULA+
    // OR-terms; the `port_7ffd_active` term is driven separately at
    // higher levels and OR-ed in by callers when appropriate.
    //
    // V15-CPU-NIT-03 (reviewer-promoted): the CPU-side callers (e.g.
    // `fuse_z80_readport`/`fuse_z80_writeport` in src/cpu/z80_cpu.cpp)
    // do not have access to NR 0x85 bit 0, so they fall back to the
    // default `port_ulap_io_en=false` parameter. We therefore OR the
    // parameter with the model's `port_ulap_io_en_` shadow (mirror of
    // NR 0x85 bit 0, set by Emulator's NR 0x85 write handler) so the
    // ULA+ contention term fires correctly regardless of which seam
    // calls us. This mirrors the `port_7ffd_io_en_` shadow pattern
    // (Verify9-memory class-(a) fix) one-to-one.
    const bool ulap_eff = port_ulap_io_en || port_ulap_io_en_;
    const bool port_c = (iorq_n == false) && port_contend(cpu_a, ulap_eff);

    // --- Active path ------------------------------------------------
    // 48K/128K (`o_cpu_contend`, zxula.vhd:587-595):
    //   stretch when ((mem_c AND mreq23_n='1' AND ioreqtw3_n='1')
    //              OR (port_c AND iorq_n='0' AND ioreqtw3_n='1'))
    //              AND timing_p3='0' AND wait_s='1'.
    //
    // +3 (`o_cpu_wait_n`, zxula.vhd:599-600):
    //   stall when (mreq_n='0' AND mem_c) AND timing_p3='1' AND wait_s='1'.
    //   I/O-side WAIT is commented out in the live VHDL — memory only.
    //
    // jnext approximation: we don't track the registered `mreq23_n` /
    // `ioreqtw3_n` (last-cycle versions) — the FUSE callback fires
    // exactly once per bus cycle so the prior-cycle latches collapse
    // into "trigger on every contended access". This matches what the
    // FUSE table-based path was doing before this change, but now
    // gated on the VHDL-faithful (hc, vc) window + per-machine page
    // decode.
    //
    // Returned delay magnitude follows the per-phase 7-cycle stretch
    // pattern, which is the same on 48K/128K (`o_cpu_contend` triggers
    // a clock-low stretch of N T-states matching the LUT) and on +3
    // (`o_cpu_wait_n` holds WAIT_n low for N T-states matching the LUT).
    // Task 54 (2026-07-13) — the stretch magnitude is a function of
    // hc(3:0): period 16 ticks = 8 T-states, NOT hc(2:0) (8 ticks = 4 T).
    // The pre-fix `kPattern[hc & 7]` indexed FUSE's per-T-STATE pattern
    // with a PIXEL-TICK counter, repeating it twice per contention octet
    // and under-charging 5 of the 6 contended phases (14 T instead of
    // 21 T per octet of continuous access). Contention-locked engines
    // (BIFROST/Nirvana multicolour) equilibrate against this grid, so the
    // wrong magnitudes displaced their raster lock ~22 T early and their
    // attribute writes crossed the ULA fetch boundary one line early.
    //
    // Derivation (zxula.vhd:582-583 + clock-stretch consumption in
    // zxnext_top_issue2.vhd:1024-1140): wait_s='1' for hc(3:0)=3..14
    // (hc_adj(3:2)/="00"); the CPU clock-low transition is deferred one
    // 7 MHz tick at a time while the gate holds, and a T-state spans the
    // tick pair {odd p, even p+1}. Counting consecutive held samples from
    // start phase p to the release window gives:
    //   48K/128K (release at hc 15,0,1,2):
    //     {3,4}->6  {5,6}->5  {7,8}->4  {9,10}->3  {11,12}->2  {13,14}->1
    //   +3: wait_s also covers hc 15,0 (hc_adj(3:1)="000" term,
    //   zxula.vhd:583), so release shrinks to {1,2} and the wrap phases
    //   gain one — the classic +3 {1,0,7,6,5,4,3,2} pattern at even hc.
    // Verified against real FUSE (Task 54): BIFROST's attribute writes
    // lock at a constant ts_in_line on both emulators and the rendered
    // frame is pixel-identical (modulo FLASH phase).
    static constexpr uint8_t kPat48[16] = {0,0,0,6,6,5,5,4,4,3,3,2,2,1,1,0};
    static constexpr uint8_t kPatP3[16] = {1,0,0,7,7,6,6,5,5,4,4,3,3,2,2,1};

    if (is_p3) {
        // +3: memory-side WAIT_n only (port-side commented out in VHDL).
        if (mreq_n == false && mem_c) {
            return kPatP3[hc & 0xF];
        }
        return 0;
    }

    // 48K/128K: memory-side OR port-side both fire o_cpu_contend.
    if ((mreq_n == false && mem_c) || port_c) {
        return kPat48[hc & 0xF];
    }
    return 0;
}
