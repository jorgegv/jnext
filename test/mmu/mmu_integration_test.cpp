// MMU Integration Test — full-Emulator + port-dispatch verification.
//
// Hosts plan rows whose observable lives at the port-dispatch tier rather
// than inside the bare Mmu register file.  Each row drives the real Z80
// port path (port::out / port::in to ports 0xEFF7, 0x243B, 0x253B) so the
// gating that VHDL applies above the Mmu module is exercised end-to-end —
// the bare mmu_test.cpp cannot model these without bypassing the gate.
//
// Plan reference: KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md G143 (re-home).
//
// Run: ./build/test/mmu_integration_test
//
// VHDL oracle: zxnext.vhd:2441,2604 (port_eff7_io_en gate).

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "memory/contention.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    Result r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
}

// Lightweight printf-style detail formatter.
std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

} // namespace

// ── Emulator construction helper ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// NextREG read/write through the real port path — exactly like Z80
// code would do via OUT (0x243B),reg / IN A,(0x253B) / OUT (0x253B),val.
static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// ── Port 0xEFF7 NR 0x85 b2 gate (G143 re-home) ───────────────────────
//
// Observable (G143 src/ fix at emulator.cpp:2222-2227): Port 0xEFF7
// writes are forwarded to Mmu::write_port_eff7 only when NR 0x85 bit 2
// (port_eff7_io_en) is set.  When the bit is clear the write is silently
// dropped — port_eff7_disable_p1024() and port_eff7_ram_at_0000() must
// retain their previous values.
//
// VHDL oracle:
//   zxnext.vhd:2604  — port_eff7 <= port_eff7_lsb AND port_eff7_io_en
//   zxnext.vhd:2441  — port_eff7_io_en <= internal_port_enable(26)
//   zxnext.vhd:2392  — internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82)
//                       so bit 26 = NR 0x85 bit 2 (high concat → high indices)
//   zxnext.vhd:5508  — nr_85_internal_port_enable <= nr_wr_dat(3 downto 0)
// Cross-ref: doc/testing/IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md:191 and
// test/port/port_test.cpp NR85-02 (0xEFF7 / VHDL:2441).
//
// The two rows below are a discriminative pair: same port write (0x0C, =
// b2|b3 = disable_p1024 AND ram_at_0000), differing only in NR 0x85 b2 —
// must produce opposite Mmu state.  This is the exact predicate the
// bare mmu_test.cpp cannot exercise because it would have to bypass the
// gate by calling Mmu::write_port_eff7 directly.
//
// NB: NR 0x85 read returns reset_type & "000" & enable(3 downto 0)
// (zxnext.vhd:6138) — only the low nibble is the enable byte.  We
// preserve the reset_type bit (b7) on every write so soft-reset
// semantics are not perturbed by the gate exercise.

static void test_eff7_io_en_gate(Emulator& emu) {
    set_group("EF7-IO-EN");

    // Helper to set NR 0x85 b2 to the given value while preserving the
    // reset_type bit (b7) and the other enable bits in the low nibble.
    auto set_eff7_gate = [&](Emulator& e, bool open) {
        const uint8_t cur = nr_read(e, 0x85);
        const uint8_t enable_low =
            static_cast<uint8_t>((cur & 0x0Fu) & ~0x04u);     // clear b2
        const uint8_t enable_open = open ? (enable_low | 0x04u) : enable_low;
        const uint8_t reset_type = cur & 0x80u;
        nr_write(e, 0x85, static_cast<uint8_t>(reset_type | enable_open));
    };

    // Bring the Mmu's port-EFF7 latches to a known-clear baseline by
    // writing 0x00 with the gate explicitly OPEN, then verify both flags
    // are false before exercising the gate-closed case.
    {
        set_eff7_gate(emu, true);
        emu.port().out(0xEFF7, 0x00);
        const bool b_dis = emu.mmu().port_eff7_disable_p1024();
        const bool b_ram = emu.mmu().port_eff7_ram_at_0000();
        check("MMU-EF7-IO-EN-00",
              "baseline: gate-open + EFF7=0x00 clears disable_p1024 + ram_at_0000 "
              "[zxnext.vhd:3777-3779 storage]",
              !b_dis && !b_ram,
              fmt("disable_p1024=%d ram_at_0000=%d", b_dis, b_ram));
    }

    // MMU-EF7-IO-EN-01: gate CLOSED (NR 0x85 b2 = 0).  Write 0x0C to
    // 0xEFF7 — the b2|b3 bits would normally flip both flags true, but
    // the AND-gate at zxnext.vhd:2604 must drop the write so both
    // flags stay false.
    {
        set_eff7_gate(emu, false);
        const uint8_t verify_85 = nr_read(emu, 0x85);
        emu.port().out(0xEFF7, 0x0C);
        const bool b_dis = emu.mmu().port_eff7_disable_p1024();
        const bool b_ram = emu.mmu().port_eff7_ram_at_0000();
        check("MMU-EF7-IO-EN-01",
              "NR 0x85 b2=0 — write 0x0C to 0xEFF7 dropped "
              "[zxnext.vhd:2604 port_eff7=lsb AND io_en, :2441/:2392 io_en=NR 0x85 b2]",
              !b_dis && !b_ram && (verify_85 & 0x04u) == 0,
              fmt("NR85=0x%02X disable_p1024=%d ram_at_0000=%d "
                  "(expected 0,0 with NR85 b2 clear)",
                  verify_85, b_dis, b_ram));
    }

    // MMU-EF7-IO-EN-02: gate OPEN (NR 0x85 b2 = 1).  Same write 0x0C
    // must now flip both flags true — discriminative pair vs row 01.
    {
        set_eff7_gate(emu, true);
        const uint8_t verify_85 = nr_read(emu, 0x85);
        emu.port().out(0xEFF7, 0x0C);
        const bool b_dis = emu.mmu().port_eff7_disable_p1024();
        const bool b_ram = emu.mmu().port_eff7_ram_at_0000();
        check("MMU-EF7-IO-EN-02",
              "NR 0x85 b2=1 — write 0x0C to 0xEFF7 sets disable_p1024 + ram_at_0000 "
              "[zxnext.vhd:2604 gate open, mmu.cpp:398 write_port_eff7 stores b2/b3]",
              b_dis && b_ram && (verify_85 & 0x04u) != 0,
              fmt("NR85=0x%02X disable_p1024=%d ram_at_0000=%d "
                  "(expected 1,1 with NR85 b2 set)",
                  verify_85, b_dis, b_ram));
    }
}

// ── V12-MEM-01: NR 0x8C / set_machine_type must NOT clobber nr_mmu_[] ─
//
// VHDL oracle:
//   * zxnext.vhd:4607-4700 — MMU<i> register process. The register is
//     written ONLY on three triggers:
//       - reset (line 4610)
//       - port_memory_change_dly='1' (line 4619)
//       - nr_mmu_we='1' (line 4686)
//   * zxnext.vhd:3813 — port_memory_change_dly composition. NR 0x8C is
//     NOT in the OR list, so an NR 0x8C write does NOT pulse the rebuild.
//   * zxnext.vhd:4880-4881 — nr_mmu_we fires ONLY on NR 0x50..0x57.
//   * zxnext.vhd:6075-6082 — NR 0x50..0x57 read-back returns the live
//     MMU<i> register byte verbatim.
//
// Pre-fix: jnext's `set_nr_8c()` / `set_machine_type()` called
// `engage_legacy_rom_paging_slot()` which unconditionally clobbered
// `nr_mmu_[slot]` to the 0xFF sentinel. This caused NR 0x50/0x51
// read-back to drop a previously-stored verbatim 0xE0..0xFE value (or
// the EFF7(3)=1-derived 0x00/0x01 value) on any NR 0x8C / NR 0x03
// machine-type-change write — diverging from the VHDL register surface.
//
// Discriminative test below: write NR 0x50 = 0xE5 (high-page legacy-ROM
// trigger), read back 0xE5; THEN write NR 0x8C with lock bits set;
// read NR 0x50 again — must STILL return 0xE5 (not 0xFF).

static void test_nr_8c_preserves_nr_mmu(Emulator& emu) {
    set_group("V12-MEM-01-NR8C");

    // Baseline: write NR 0x50 = 0xE5 (high-page mapping, mmu_A21_A13(8)=1
    // routes slot 0 to legacy ROM via sram_rom; nr_mmu_ stores 0xE5
    // verbatim per VHDL :4686-4699).
    nr_write(emu, 0x50, 0xE5);
    const uint8_t pre = nr_read(emu, 0x50);
    check("V12-MEM-01-A",
          "NR 0x50 read-back returns verbatim 0xE5 after high-page write "
          "[zxnext.vhd:4686-4699,6075-6082]",
          pre == 0xE5,
          fmt("expected 0xE5, got 0x%02X", pre));

    // NR 0x8C write: flip lock bits. VHDL leaves MMU0 alone (no
    // nr_mmu_we, no port_memory_change_dly). NR 0x50 read-back must
    // still return 0xE5.
    const uint8_t prev_8c = nr_read(emu, 0x8C);
    nr_write(emu, 0x8C, static_cast<uint8_t>(prev_8c | 0x10)); // set lock_rom0
    const uint8_t post_8c = nr_read(emu, 0x50);
    check("V12-MEM-01-B",
          "NR 0x8C write does NOT clobber NR 0x50 verbatim value "
          "[zxnext.vhd:3813 NR 0x8C absent from port_memory_change_dly, "
          ":4607-4700 MMU<i> only updates on listed triggers]",
          post_8c == 0xE5,
          fmt("expected 0xE5, got 0x%02X (NR 0x8C clobbered the verbatim NR-write)", post_8c));

    // Restore NR 0x8C to its prior value (avoids leaking lock bits to
    // subsequent tests).
    nr_write(emu, 0x8C, prev_8c);

    // V12-MEM fix-of-reviewer NIT-2: V12-MEM-01-A wrote NR 0x50=0xE5
    // (high-page legacy-ROM trigger). Restore slot 0 to its constructor
    // default so subsequent V12-MEM-02 / V12-MEM-03 tests start from a
    // clean MMU register surface. Writing the 0xFF "engage legacy ROM
    // paging" sentinel mirrors the boot-time default (per VHDL :4686-4699,
    // NR 0x50/0x51 with 0xFF re-engages the legacy auto-paging path) and
    // discards the verbatim 0xE5 written above.
    nr_write(emu, 0x50, 0xFF);
}

// ── V12-MEM-02: ContentionModel state survives save/load round-trip ──
//
// VHDL oracle:
//   * zxnext.vhd:5786-5828 — NR 0x07 cpu_speed shadow (line 5789, latched
//     immediately on write) and effective (line 5817, committed on
//     bus-idle CLK_CPU edge). Both flip-flops persist across any
//     non-reset edge.
//   * zxnext.vhd:5800-5823 — NR 0x08 bit 6 nr_08_contention_disable
//     shadow (line 5805) and eff_nr_08_contention_disable effective
//     (line 5823, committed on bus-idle hc(8)='1' edge).
//   * zxnext.vhd:1099-1103 / :2399 — NR 0x82 bit 1 port_7ffd_io_en gate.
//   * zxnext.vhd:5906 — NR 0x08 read returns
//       (NOT port_7ffd_locked) & eff_nr_08_contention_disable & ...
//
// jnext's ContentionModel is intentionally NOT in the save_state
// stream — it owns derived-from-NextReg state (cpu_speed, contention_
// disable shadow/effective, port_7ffd_io_en, mem_active_page latch).
// Pre-Verify12 these gates revert to constructor defaults on
// load_state, so the post-load NR 0x08 read returned bit 6 = 0 even
// when the saved snapshot had bit 6 = 1. The fix re-pushes the gates
// from canonical loaded NextReg / Mmu state in load_state(), mirroring
// the divmmc_.set_rom3_active / spi_.set_flash_cs_enable re-sync
// pattern used elsewhere in load_state.

static void test_contention_state_round_trip(Emulator& emu) {
    set_group("V12-MEM-02-CONT");

    // Set NR 0x08 bit 6 = 1 (contention disable). The write puts bit 6
    // into the SHADOW; per VHDL :5822-5823 the EFFECTIVE field commits
    // on bus-idle hc(8)='1'. Drive that commit explicitly so the
    // NR 0x08 read (which observes the effective field per :5906)
    // sees the bit.
    const uint8_t initial_08 = nr_read(emu, 0x08);
    nr_write(emu, 0x08, 0x40);                     // bit 6 only (no other writes)
    emu.contention().commit_contention_disable_on_hc(0x100);
    const uint8_t pre_save = nr_read(emu, 0x08);
    check("V12-MEM-02-A",
          "NR 0x08 bit 6 (contention_disable) reads back 1 after write+commit "
          "[zxnext.vhd:5176,5800-5823,5906]",
          (pre_save & 0x40) != 0,
          fmt("expected bit 6 set, got 0x%02X", pre_save));

    // Sanity check: the underlying ContentionModel field on the live
    // emulator is true post-commit.
    check("V12-MEM-02-B",
          "ContentionModel.contention_disable() is true post-commit on live emu "
          "[zxnext.vhd:5822-5823 commit on hc(8)='1' propagates shadow→effective]",
          emu.contention().contention_disable(),
          fmt("expected effective contention_disable=1, got %d",
              static_cast<int>(emu.contention().contention_disable())));

    // Measure save size, then save into a sized buffer.
    size_t need = 0;
    {
        StateWriter measure(nullptr, 0);
        emu.save_state(measure);
        need = measure.position();
    }
    std::vector<uint8_t> blob(need, 0);
    {
        StateWriter w(blob.data(), blob.size());
        emu.save_state(w);
    }

    // Construct a NEW Emulator + load the saved state. Use a fresh
    // EmulatorConfig so the ContentionModel is built from defaults
    // (cpu_speed=0, contention_disable=false, port_7ffd_io_en=false).
    Emulator fresh;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    fresh.init(cfg);
    {
        StateReader r(blob.data(), blob.size());
        fresh.load_state(r);
    }

    // Pre-fix: ContentionModel.contention_disable_ stays false (default)
    // → NR 0x08 read returns bit 6 = 0. Post-fix: re-pushed during
    // load_state via contention_.set_contention_disable(mmu_.contention_disabled()).
    const uint8_t post_load = nr_read(fresh, 0x08);
    check("V12-MEM-02-C",
          "NR 0x08 bit 6 survives save/load round-trip "
          "[ContentionModel re-sync from Mmu.contention_disabled() in load_state]",
          (post_load & 0x40) != 0,
          fmt("expected bit 6 set post-load, got 0x%02X "
              "(ContentionModel reverted to default; load_state re-sync missing)",
              post_load));

    // Direct assertion on the model: effective field must match what
    // VHDL would have committed on a `hc(8)='1'` edge with the shadow set.
    check("V12-MEM-02-D",
          "ContentionModel.contention_disable() (effective) is true post-load "
          "[zxnext.vhd:5823 effective committed value persists across non-reset edges]",
          fresh.contention().contention_disable(),
          fmt("contention_disable=%d (expected 1)",
              static_cast<int>(fresh.contention().contention_disable())));

    // Restore NR 0x08 to original on the live emu so other tests aren't
    // perturbed by the disabled-contention bit.
    nr_write(emu, 0x08, initial_08);
}

// ── V12-MEM-03: ContentionModel rebuild_for_type on machine_type save/load ──
//
// VHDL oracle:
//   * zxnext.vhd:5741-5757 — `machine_type_48 / machine_type_128 /
//     machine_type_p3` derived from `nr_03_machine_type`.
//   * zxnext.vhd:4489-4493 — mem_contend per-machine bank decode:
//       48K  → page(3:1)="101"
//       128K → page(1)='1'
//       +3   → page(3)='1'
//
// Pre-Verify12: a snapshot taken with machine_type=ZX48K (committed via
// NR 0x03 typ_sel=$01) and loaded onto a fresh emulator initialised
// with cfg.type=ZXN_ISSUE2 left ContentionModel pinned to ZXN_ISSUE2
// because ContentionModel is rebuilt by `build()` only at init() time.
// Mmu's machine_type_ field WAS round-tripped (saved/loaded), but the
// derived ContentionModel.type_ was not. The fix re-runs
// `rebuild_for_type(mmu_.machine_type())` in load_state.
//
// V12-MEM fix-of-reviewer NIT-1: the original V12-MEM-03-B was
// non-discriminative. The live emu is ZXN_ISSUE2, so the saved
// `machine_type` is `ZXN_ISSUE2`, and `is_contended_access()` short-
// circuits to `false` at `contention.cpp:31` (`if (type == ZXN_ISSUE2)
// return;`) regardless of whether the V12-MEM-03 fix is present —
// the test passed even with the fix reverted.
//
// The discriminative form below switches the live emu's machine_type to
// ZX48K via `Mmu.set_machine_type(ZX48K)` BEFORE saving, then loads onto
// a fresh ZXN_ISSUE2-initialised emu, sets `mem_active_page=0x0A`
// (bits[3:1]=101 = bank 5, contended on 48K per VHDL :4490), and asserts
// `is_contended_access()==true`. Pre-Verify12 ContentionModel.type_ stays
// ZXN_ISSUE2 (init-time value) — `is_contended_access()` returns false.
// Post-fix `rebuild_for_type(ZX48K)` is called from `load_state`,
// flipping type_ to ZX48K — `is_contended_access()` correctly returns
// true.

static void test_machine_type_round_trip(Emulator& emu) {
    set_group("V12-MEM-03-MT");

    // Stash the live emu's original machine_type so we can restore it
    // after the test; switching it here would otherwise leak into any
    // subsequent test rows running on the same `emu`.
    const MachineType original_mt = emu.mmu().machine_type();

    // Switch the live emu to ZX48K. The `set_machine_type` call path is
    // the same one NR 0x03 typ_sel commits use (per Mmu.h:803), so this
    // exercises the V12-MEM-03 round-trip exactly as a runtime NR 0x03
    // commit would.
    emu.mmu().set_machine_type(MachineType::ZX48K);

    // Sanity check: live ContentionModel still pinned to ZXN_ISSUE2 at
    // this point because nothing has called rebuild_for_type on the live
    // emu (Mmu.set_machine_type does NOT touch ContentionModel — that
    // wiring lives in NR 0x03 commit at emulator.cpp:2019). The live
    // emu's contention type is irrelevant to this test; we exercise the
    // load_state path on a fresh emu.

    size_t need = 0;
    {
        StateWriter measure(nullptr, 0);
        emu.save_state(measure);
        need = measure.position();
    }
    std::vector<uint8_t> blob(need, 0);
    {
        StateWriter w(blob.data(), blob.size());
        emu.save_state(w);
    }

    // Fresh emulator, initialised at ZXN_ISSUE2 (so its ContentionModel
    // .type_ starts as ZXN_ISSUE2). Loading the ZX48K snapshot must
    // re-run rebuild_for_type(ZX48K) on the fresh ContentionModel —
    // that is exactly what the V12-MEM-03 fix wires up at
    // emulator.cpp:6292.
    Emulator fresh;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    fresh.init(cfg);
    {
        StateReader r(blob.data(), blob.size());
        fresh.load_state(r);
    }

    // V12-MEM-03-A — Mmu's machine_type_ field round-trip (sanity, was
    // already plumbed pre-Verify12; this row guards against a future
    // regression of the underlying Mmu serialisation).
    check("V12-MEM-03-A",
          "Mmu.machine_type() round-trips ZX48K through save/load",
          fresh.mmu().machine_type() == MachineType::ZX48K,
          fmt("Mmu.machine_type=%d (expected ZX48K=%d)",
              static_cast<int>(fresh.mmu().machine_type()),
              static_cast<int>(MachineType::ZX48K)));

    // V12-MEM-03-B — discriminative behavioural assertion on the
    // ContentionModel.type_ recovery. We pick mem_active_page = 0x0A:
    //   bits[7:4] = 0  (mem_contend gate at VHDL :4489 open)
    //   bits[3:1] = 101 (= 5)
    // VHDL :4490 — 48K contend iff page(3:1) = "101" → contended.
    // VHDL :4491 — 128K contend iff page(1) = '1' → 0x0A bit 1 = 1 →
    //              would also contend on 128K, but bank-decode pattern
    //              "101" is the canonical 48K bank-5 contention case.
    // ZXN_ISSUE2 short-circuits to `false` at contention.cpp:31 — so
    // this is what the post-load decode would return WITHOUT the fix.
    //
    // Pre-fix (rebuild_for_type not called from load_state): fresh
    // ContentionModel keeps init-time type_ = ZXN_ISSUE2 → returns
    // FALSE → assertion FAILS.
    //
    // Post-fix (rebuild_for_type wired into load_state): fresh
    // ContentionModel.type_ flips to ZX48K → returns TRUE → assertion
    // PASSES.
    fresh.contention().set_mem_active_page(0x0A);
    check("V12-MEM-03-B",
          "ContentionModel.type_ tracks Mmu.machine_type() across load_state — "
          "ZX48K + page=0x0A (bank 5) contends "
          "[zxnext.vhd:4490 mem_contend 48K bank-decode; "
          "rebuild_for_type wired into Emulator::load_state]",
          fresh.contention().is_contended_access() == true,
          fmt("expected ZX48K bank-5 → contended; got is_contended=%d "
              "(ContentionModel.type_ likely still ZXN_ISSUE2 — "
              "rebuild_for_type missing from load_state)",
              static_cast<int>(fresh.contention().is_contended_access())));

    // Restore the live emu's original machine_type so we don't leak
    // into any subsequent tests that share `emu`.
    emu.mmu().set_machine_type(original_mt);
}

// ── V13-MEM-01: NR 0x69 bit 7 must fan out into port 0x123B bit 1 ─────
//
// VHDL oracle:
//   * zxnext.vhd:3924-3925 — NR 0x69 write fans `nr_wr_dat(7)` into the
//     SAME `port_123b_layer2_en` flip-flop that port 0x123B bit 1 latches
//     at :3916. There is one and only one FF for "Layer2 display enable";
//     both writers feed it.
//   * zxnext.vhd:3933 — port_123b_dat read-back composition surfaces this
//     FF as bit 1 of the port 0x123B read byte (port_123b_dat layout:
//     {seg(7:6), "00", shadow(3), rd_en(2), enable(1), wr_en(0)}).
//
// Pre-fix: jnext mirrored the FF in TWO places:
//   * `Layer2::enabled_` (used by NR 0x69 read handler at :2179-2185)
//   * `Mmu::l2_enable_` (used by `Mmu::l2_port_readback()` at :1046-1054,
//     in turn used by the port 0x123B read handler at :2634-2638)
//
// The port 0x123B WRITE handler updated BOTH (Mmu via `set_l2_port`,
// Layer2 via `layer2_.set_enabled` at :2651-2653). The NR 0x69 write
// handler updated ONLY Layer2 — leaving `Mmu::l2_enable_` stale until
// the next port 0x123B write. So a `NEXTREG $69,$80` followed by
// `IN A,(0x123B)` returned bit 1 = 0 even though VHDL would return 1.
//
// V13-MEM-01 fix (mmu.h `set_l2_enable` + emulator.cpp NR 0x69 handler):
// the NR 0x69 write handler now mirrors bit 7 into both shadows.
//
// Discriminative test:
//   1. Reset state — NR 0x69 = 0x00 → port 0x123B bit 1 = 0 (Mmu
//      mirror) AND NR 0x69 read bit 7 = 0 (Layer2 mirror).
//   2. Write NR 0x69 = 0x80 (bit 7 = 1).
//   3. Read port 0x123B bit 1 → must be 1 (the bug surface).
//   4. Read NR 0x69 bit 7 → must be 1 (parallel verification — has
//      always worked).
//   5. Write NR 0x69 = 0x00 (bit 7 = 0, sweep back).
//   6. Re-read port 0x123B bit 1 → must be 0 (sweep verification —
//      confirms the fix isn't a one-shot raise).

static void test_nr_69_b7_to_port_123b_b1(Emulator& emu) {
    set_group("V13-MEM-01-L2EN");

    // Baseline: clear NR 0x69 and any prior port-0x123B latches.
    nr_write(emu, 0x69, 0x00);
    emu.port().out(0x123B, 0x00);

    const uint8_t base_123b = emu.port().in(0x123B);
    check("V13-MEM-01-A",
          "Baseline port 0x123B bit 1 = 0 after clearing both NR 0x69 "
          "and port 0x123B [zxnext.vhd:3933 read-back]",
          (base_123b & 0x02) == 0,
          fmt("expected bit 1 = 0, got 0x%02X", base_123b));

    // Bug surface: write NR 0x69 bit 7 = 1 (display-enable on).
    // VHDL :3924-3925 sets port_123b_layer2_en <= 1.
    nr_write(emu, 0x69, 0x80);

    const uint8_t after_set_123b = emu.port().in(0x123B);
    check("V13-MEM-01-B",
          "NR 0x69 bit 7 = 1 fans out into port 0x123B bit 1 = 1 "
          "[zxnext.vhd:3924-3925 nr_69_we drives port_123b_layer2_en]",
          (after_set_123b & 0x02) != 0,
          fmt("expected bit 1 = 1, got 0x%02X (Mmu::l2_enable_ stale "
              "after NR 0x69 fan-out)", after_set_123b));

    // Parallel verification: NR 0x69 read uses Layer2's mirror, which
    // has always tracked NR 0x69 writes. This row guards that the
    // V13-MEM-01 fix did not regress the existing path.
    const uint8_t nr69_after_set = nr_read(emu, 0x69);
    check("V13-MEM-01-C",
          "NR 0x69 bit 7 read-back = 1 after NR 0x69 = 0x80 write "
          "(Layer2 mirror — pre-fix path, regression guard) "
          "[zxnext.vhd:6095-6096]",
          (nr69_after_set & 0x80) != 0,
          fmt("expected bit 7 = 1, got 0x%02X", nr69_after_set));

    // Sweep back: clear bit 7. Both mirrors must follow.
    nr_write(emu, 0x69, 0x00);

    const uint8_t after_clear_123b = emu.port().in(0x123B);
    check("V13-MEM-01-D",
          "NR 0x69 bit 7 = 0 clears port 0x123B bit 1 (sweep guard — "
          "fix must not be a one-shot raise) [zxnext.vhd:3924-3925]",
          (after_clear_123b & 0x02) == 0,
          fmt("expected bit 1 = 0, got 0x%02X", after_clear_123b));

    // Discriminative independence guard: the OTHER bits in port 0x123B
    // (seg, shadow, rd_en, wr_en) must NOT be perturbed by NR 0x69
    // writes — VHDL :3924-3925 touches only port_123b_layer2_en. We
    // pre-load segment=11 + shadow + rd_en + wr_en via a non-offset-mode
    // port 0x123B write, then write NR 0x69 = 0x80 and verify the other
    // bits round-trip unchanged.
    //
    // Pre-load: bit 0 (wr_en) | bit 2 (rd_en) | bit 3 (shadow) | seg "11"
    //   = 0xC0 | 0x08 | 0x04 | 0x01 = 0xCD
    emu.port().out(0x123B, 0xCD);
    const uint8_t pre_other = emu.port().in(0x123B);
    // Clear NR 0x69 bit 7 first so the test of "NR 0x69 toggles only
    // bit 1" sees a 0→1 transition, not a 1→1 idempotent write.
    nr_write(emu, 0x69, 0x00);
    // The above 0x123B port write (without bit 4) just set bit 1 = 0
    // (display-enable cleared) and re-set the segment/shadow/rd/wr bits.
    // Re-read to capture the canonical pre-state.
    (void)pre_other;
    const uint8_t pre_69 = emu.port().in(0x123B);
    nr_write(emu, 0x69, 0x80);
    const uint8_t post_69 = emu.port().in(0x123B);
    // Mask bits except bit 1 — they must be identical.
    const uint8_t other_mask = static_cast<uint8_t>(~0x02u);
    check("V13-MEM-01-E",
          "NR 0x69 fan-out only touches port 0x123B bit 1 (other bits "
          "unchanged) [zxnext.vhd:3924-3925 port_123b_layer2_en is the "
          "ONLY field nr_69_we writes]",
          (pre_69 & other_mask) == (post_69 & other_mask),
          fmt("expected (pre & ~0x02)=0x%02X == (post & ~0x02)=0x%02X",
              pre_69 & other_mask, post_69 & other_mask));

    // Restore reset defaults so downstream tests start clean.
    nr_write(emu, 0x69, 0x00);
    emu.port().out(0x123B, 0x00);
}

// ── Main ─────────────────────────────────────────────────────────────


// ── Live machine-type switch: rom_in_sram must clear (review 2026-07-10) ──
//
// The GUI's on_machine_type() re-init()s the SAME Emulator object with a
// different cfg.type. Pre-fix, Emulator::init set mmu_.rom_in_sram(true)
// for ZXN_ISSUE2 with no else-branch, so a live Next→128K switch left the
// flag stuck true: every to_sram_page() translation (and the bank-7 BRAM
// gate, which now depends on the same flag) kept behaving as Next mode on
// a standalone machine — bank-7 writes were split away from where the
// (correctly re-wired) ULA/tilemap read. Discriminative: revert the
// `else { mmu_.set_rom_in_sram(false); }` in Emulator::init → SWITCH-01/02
// FAIL.

static void test_machine_switch_clears_rom_in_sram() {
    Emulator emu;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    const bool next_flag = emu.mmu().rom_in_sram();

    // Live switch to a standalone 128K on the SAME object.
    cfg.type = MachineType::ZX128K;
    emu.init(cfg);
    const bool legacy_flag = emu.mmu().rom_in_sram();
    check("SWITCH-01",
          "live Next→128K machine switch clears Mmu::rom_in_sram",
          next_flag && !legacy_flag,
          fmt("next_flag=%d legacy_flag=%d", next_flag, legacy_flag));

    // Bank-7 content lands in flat RAM pages 0x0E/0x0F, not the BRAM.
    emu.mmu().map_128k_bank(0x07);
    emu.mmu().write(0xC000, 0xAB);
    emu.mmu().write(0xE000, 0xCD);
    const uint8_t lo   = emu.ram().page_ptr(0x0E)[0];
    const uint8_t hi   = emu.ram().page_ptr(0x0F)[0];
    const uint8_t bram = emu.mmu().bank7_bram()[0];
    check("SWITCH-02",
          "post-switch standalone bank-7 writes land in flat RAM, not the "
          "Next-only BRAM buffer",
          lo == 0xAB && hi == 0xCD && bram != 0xAB,
          fmt("ram[0x0E][0]=0x%02X ram[0x0F][0]=0x%02X bram[0]=0x%02X",
              lo, hi, bram));
}

int main() {
    std::printf("MMU Integration Tests (full-Emulator + port-dispatch)\n");
    std::printf("====================================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_eff7_io_en_gate(emu);
    std::printf("  Group: EF7-IO-EN — done\n");

    test_nr_8c_preserves_nr_mmu(emu);
    std::printf("  Group: V12-MEM-01-NR8C — done\n");

    test_contention_state_round_trip(emu);
    std::printf("  Group: V12-MEM-02-CONT — done\n");

    test_machine_type_round_trip(emu);
    std::printf("  Group: V12-MEM-03-MT — done\n");

    test_nr_69_b7_to_port_123b_b1(emu);
    std::printf("  Group: V13-MEM-01-L2EN — done\n");

    test_machine_switch_clears_rom_in_sram();
    std::printf("  Group: SWITCH (live machine-type re-init) — done\n");

    std::printf("\n====================================\n");
    std::printf("Total: %d Passed: %d Failed: %d Skipped: %zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-10s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
