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
    static const uint8_t pattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};
    const bool is_p3 = (type == MachineType::ZX_PLUS3);

    for (int vc = 0; vc <= 191; ++vc) {
        // hc(8)=0 ⇒ hc in [0, 255] — the inner 256 ticks of each line.
        for (int hc = 0; hc <= 255; ++hc) {
            int hc_adj = ((hc & 0xF) + 1) & 0xF;          // 4-bit wrap
            bool contend = (hc_adj & 0xC) != 0;           // hc_adj[3:2] != 0
            if (is_p3) contend |= (hc_adj & 0xE) == 0;    // hc_adj[3:1] == 0
            if (contend) {
                lut_[vc][hc] = pattern[hc & 7];
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
    // The `machine_timing_pentagon` term is gated by NR 0x03 timing-mode
    // bit 2 inside the Next FPGA. jnext does not currently wire that
    // signal into the contention model — the standalone Pentagon machine
    // type was dropped (Wave 0.3 follow-up, 2026-05-04), and the NR 0x03
    // Pentagon timing-mode commit path does not yet feed
    // ContentionModel. Equivalent to treating the term as always-true
    // (i.e. machine_timing_pentagon='0'), which is correct for every
    // currently supported machine.
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

    // VHDL zxnext.vhd:4489 — mem_contend = '0' when mem_active_page(7:4) /= "0000".
    if ((mem_active_page_ & 0xF0) != 0) return false;

    const uint8_t low = mem_active_page_ & 0x0F;
    switch (type_) {
        case MachineType::ZX48K:
            // VHDL zxnext.vhd:4490 — 48K: contend iff mem_active_page(3:1) = "101".
            return ((low >> 1) & 0x07) == 0x05;
        case MachineType::ZX128K:
            // VHDL zxnext.vhd:4491 — 128K: contend iff mem_active_page(1) = '1' (odd banks).
            return (low & 0x02) != 0;
        case MachineType::ZX_PLUS3:
            // VHDL zxnext.vhd:4492 — +3: contend iff mem_active_page(3) = '1' (banks >= 4).
            return (low & 0x08) != 0;
        case MachineType::ZXN_ISSUE2:
            // ZXN_ISSUE2 has no timing-mode line here.
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
    // AND `port_ulap_io_en` (NR 0x82 bit 8 → internal_port_enable(24),
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
    if (port_7ffd_io_en_ &&
        (type_ == MachineType::ZX128K || type_ == MachineType::ZX_PLUS3) &&
        (cpu_a & 0x8000) == 0 &&                        // cpu_a(15)='0'
        (cpu_a & 0x0003) == 0x0001 &&                   // port_fd: A1:0="01"
        (cpu_a & 0x3000) != 0x1000 &&                   // NOT port_1ffd: A13:12 != "01"
        (type_ != MachineType::ZX_PLUS3 ||
         (cpu_a & 0x4000) != 0)) {                      // +3: also require A14='1'
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
    // (`machine_timing_pentagon` always treated as '0' here — see
    // is_contended_access() comment for the standalone-Pentagon-removal
    // note.)
    if (contention_disable_) return 0;
    if (cpu_speed_ != 0)     return 0;

    // --- Window gate (zxula.vhd:582-583) ------------------------------
    // wait_s = '1' iff:
    //   ((hc_adj(3:2) /= "00") OR (hc_adj(3:1)=000 AND timing_p3=1))
    //   AND hc(8)=0 AND border_active_v=0 AND contention_en=1
    //
    // hc_adj is 4-bit, so the +1 wraps at 16. hc(3:0)=15 → hc_adj=0,
    // hc(3:0)=0..14 → hc_adj=1..15.
    if ((hc & 0x100) != 0) return 0;                  // hc(8)=1 → border
    // border_active_v = vc(8) | (vc(7) & vc(6)) = vc>=192 OR vc>=256.
    if ((vc & 0x100) != 0) return 0;
    if ((vc & 0xC0) == 0xC0) return 0;
    const int hc_adj = ((hc & 0x0F) + 1) & 0x0F;       // 4-bit wrap
    const bool is_p3 = (type_ == MachineType::ZX_PLUS3);
    bool wait_s = (hc_adj & 0xC) != 0;                 // hc_adj(3:2) != 0
    if (is_p3) wait_s |= (hc_adj & 0xE) == 0;          // hc_adj(3:1) = 000
    if (!wait_s) return 0;

    // --- mem_contend (zxnext.vhd:4489-4493) ---------------------------
    // mem_contend evaluated against current mem_active_page/type.
    //   '0' when page(7:4) /= "0000"
    //   '1' for 48K  & page(3:1)=101  (bank 5 only)
    //   '1' for 128K & page(1)=1      (odd banks)
    //   '1' for +3   & page(3)=1      (banks >= 4)
    bool mem_c = false;
    if ((mem_active_page_ & 0xF0) == 0) {
        const uint8_t low = mem_active_page_ & 0x0F;
        switch (type_) {
            case MachineType::ZX48K:
                mem_c = ((low >> 1) & 0x07) == 0x05;
                break;
            case MachineType::ZX128K:
                mem_c = (low & 0x02) != 0;
                break;
            case MachineType::ZX_PLUS3:
                mem_c = (low & 0x08) != 0;
                break;
            case MachineType::ZXN_ISSUE2:
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
    static constexpr uint8_t kPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

    if (is_p3) {
        // +3: memory-side WAIT_n only (port-side commented out in VHDL).
        if (mreq_n == false && mem_c) {
            return kPattern[hc & 7];
        }
        return 0;
    }

    // 48K/128K: memory-side OR port-side both fire o_cpu_contend.
    if ((mreq_n == false && mem_c) || port_c) {
        return kPattern[hc & 7];
    }
    return 0;
}
