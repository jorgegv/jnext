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
#include "core/szx_saver.h"
#include "core/nex_saver.h"
#include "memory/contention.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // mkstemp (POSIX) — temp files for saver round-trip tests

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

// NR 0x03 read-back layout (VHDL zxnext.vhd:5894):
//   port_253b_dat <= palette_sub_idx & nr_03_machine_timing(2:0)
//                    & nr_03_user_dt_lock & nr_03_machine_type(2:0)
// so tim_sel is bits 6:4. Read through the real port path.
static uint8_t nextreg_tim_sel(Emulator& emu);

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

static uint8_t nextreg_tim_sel(Emulator& emu) {
    return static_cast<uint8_t>((nr_read(emu, 0x03) >> 4) & 0x07);
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
              "[zxnext.vhd:3780-3782 storage]",
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
          "[zxnext.vhd:4686-4699,6059-6060]",
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

// ── Task 26 P3: NR $03 machine-type cold-boot default ────────────────
//
// VHDL zxnext.vhd:1103 —
//   signal nr_03_machine_type : std_logic_vector(2 downto 0) := "011";
// This FPGA power-on value is machine-agnostic and is NOT re-asserted by
// the soft/hard reset block (zxnext.vhd:4926-5111 contains no
// nr_03_machine_type assignment), so on real hardware NR $03 machine-type
// reads "011" (=+3) at cold boot until firmware commits a value while
// config_mode=1 (:5137-5145). The prior jnext code pushed 0x04 for the
// Next at hard reset, so NR $03 read-back was "100" (=128K per the
// :5741-5757 decode) — a "works by luck" divergence masked on the boot
// path only because NextZXOS commits the real type early.
//
// Discriminative: with the pre-fix `ZXN_ISSUE2 → 0x04`, MT-DEF-01 sees
// mtype==0x04 and FAILS. NR $03 machine-type is read-back-only state
// (its sole consumer is the NR $03 read handler at emulator.cpp:2584);
// MMU routing is driven by cfg.type independently, so this changes only
// the register surface, not memory decode.
static void test_nr03_machine_type_cold_boot_default() {
    Emulator next_emu;
    EmulatorConfig ncfg;
    ncfg.type = MachineType::ZXN_ISSUE2;
    ncfg.rewind_buffer_frames = 0;
    next_emu.init(ncfg);
    const uint8_t next_mt = nr_read(next_emu, 0x03) & 0x07;
    check("MT-DEF-01",
          "Next (ZXN_ISSUE2) cold-boot NR $03 machine-type = 011 (+3) "
          "per the zxnext.vhd:1103 signal initialiser (the power-on default)",
          next_mt == 0x03,
          fmt("nr03_mtype=0x%02X (want 0x03)", next_mt));

    Emulator p3_emu;
    EmulatorConfig pcfg;
    pcfg.type = MachineType::ZX_PLUS3;
    pcfg.rewind_buffer_frames = 0;
    p3_emu.init(pcfg);
    const uint8_t p3_mt = nr_read(p3_emu, 0x03) & 0x07;
    check("MT-DEF-02",
          "+3 (ZX_PLUS3) cold-boot NR $03 machine-type = 011 (+3)",
          p3_mt == 0x03,
          fmt("nr03_mtype=0x%02X (want 0x03)", p3_mt));
}

// ── GH #232: a soft reset re-derives from NR 0x03, not from the CLI ───
//
// There is no machine-type input pin in the VHDL. Both NR 0x03 axes are
// plain flip-flops with initial values only —
//   nr_03_machine_timing : ... := "011"   (zxnext.vhd:1099)
//   nr_03_machine_type   : ... := "011"   (zxnext.vhd:1103)
// — neither appears anywhere in the master reset block (:4926-5111), and
// the single reset wire covers hard and soft alike (`reset <= i_RESET`
// :1730; `reset <= reset_hard or reset_soft` zxnext_top_issue2.vhd:840).
// So a reset PRESERVES both, and only firmware writing NR 0x03 changes
// them (:5124-5135 tim_sel, :5137-5145 typ_sel gated on config_mode).
//
// jnext models exactly that — mmu_.set_machine_type() in Emulator::init()
// is hard-reset-only, and both the NR 0x03 commit path and load_state()
// take the Mmu's value as canonical for ContentionModel. init()'s
// `contention_.build(cfg.type)` and its `is_48_or_p3` derivation were the
// two places that went back to the CLI type instead, so a soft reset
// unwound guest-committed state that the guest could no longer re-commit
// (the typ_sel write clears config_mode by construction).
static void test_gh232_soft_reset_uses_nextreg_state() {
    set_group("GH232-SOFT-RESET-AXES");

    // GH232-01/02/03 — machine TYPE (typ_sel). Boot a Next, let the guest
    // commit +3 through the real NR 0x03 config-mode path, soft-reset.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // A firmware-less cold boot performs the IPL's own NR 0x03 commit
        // and leaves config_mode clear (GH #226), so re-enter it first —
        // VHDL :5147-5151 sets the mode on bits[2:0] = "111". Then commit
        // typ_sel = "011" (+3). The SAME write clears config_mode again,
        // which is precisely why the guest cannot re-commit afterwards to
        // reconcile a machine type the reset unwound.
        nr_write(emu, 0x03, 0x07);        // re-enter config mode
        nr_write(emu, 0x03, 0x03);        // commit +3, exit config mode
        const bool committed = emu.mmu().machine_type() == MachineType::ZX_PLUS3;
        const bool cfg_mode_off = !emu.nextreg().nr_03_config_mode();

        nr_write(emu, 0x02, 0x01);        // RESET_SOFT

        check("GH232-01",
              "the NR 0x03 typ_sel commit survives a soft reset in the Mmu "
              "(no reset clause for nr_03_machine_type) "
              "[zxnext.vhd:1103, :4926-5111]",
              committed && cfg_mode_off &&
                  emu.mmu().machine_type() == MachineType::ZX_PLUS3,
              fmt("committed=%d cfg_mode_off=%d post=%d",
                  committed ? 1 : 0, cfg_mode_off ? 1 : 0,
                  static_cast<int>(emu.mmu().machine_type())));

        // Pre-fix: init() rebuilt from cfg.type = ZXN_ISSUE2, whose LUT is
        // all-zero and whose per-machine bank decode leaves slot 1
        // uncontended — so the model disagreed with the Mmu it is paired
        // with. Post-fix: rebuilt from the preserved +3 type.
        check("GH232-02",
              "soft reset rebuilds the contention bank decode from the "
              "PRESERVED machine type, not the CLI one "
              "[zxnext.vhd:4490-4492 mem_contend; :2981-3008 machine_type_*]",
              emu.contention().is_contended_address(0x4000) == true,
              fmt("is_contended(0x4000)=%d (want 1)",
                  emu.contention().is_contended_address(0x4000) ? 1 : 0));

        // hc(3:0)=3 → hc_adj=4 → wait_s via hc_adj(3:2)/=0 on every
        // machine; the magnitude is the discriminator (zxula.vhd:582-583 +
        // the +3 pattern): +3 → 7, 128K/48K → 6, Next → 0 (no LUT).
        const uint8_t d = emu.contention().delay(/*hc=*/3, /*vc=*/100);
        check("GH232-03",
              "soft reset rebuilds the contention LUT with the +3 pattern "
              "the preserved machine type selects [zxula.vhd:582-583]",
              d == 7, fmt("delay(hc=3,vc=100)=%u (want 7; 6=48K/128K, "
                          "0=Next-typed LUT)", static_cast<unsigned>(d)));
    }

    // GH232-04/05 — machine TIMING (tim_sel) → the pulse-mode /INT width
    // gate. VHDL :2033 reads machine_timing_48 / machine_timing_p3, which
    // come from eff_nr_03_machine_timing (:5761-5776) — never from
    // machine_type_*. Boot 128K (tim_sel "010" → 36 cycles), have the guest
    // select +3 timing (→ 32), soft-reset.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX128K;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
        const bool boot_gate = emu.cpu().machine_timing_48_or_p3();

        // bit7=1 arms the tim_sel commit (:5124), dt_lock is 0 at boot and
        // bit3=0 keeps it there, bits 2:0 = 000 is VHDL's "no change" for
        // typ_sel (:5143) so only the timing axis moves.
        nr_write(emu, 0x03, 0xB0);
        const bool after_write = emu.cpu().machine_timing_48_or_p3();

        nr_write(emu, 0x02, 0x01);        // RESET_SOFT
        const uint8_t tim = nextreg_tim_sel(emu);

        check("GH232-04",
              "the pulse-mode /INT width gate follows NR 0x03 tim_sel across "
              "a soft reset, not the CLI machine type "
              "[zxnext.vhd:2033 pulse_count_end; :5761-5776 machine_timing_*]",
              boot_gate == false && after_write == true && tim == 0x03 &&
                  emu.cpu().machine_timing_48_or_p3() == true,
              fmt("boot=%d after_write=%d tim_sel=0x%02X post_reset=%d",
                  boot_gate ? 1 : 0, after_write ? 1 : 0, tim,
                  emu.cpu().machine_timing_48_or_p3() ? 1 : 0));

        check("GH232-05",
              "Im2Controller's copy of that same gate stays in lock-step "
              "with Z80Cpu's across the soft reset [zxnext.vhd:2033 — one "
              "VHDL signal, two jnext consumers]",
              emu.im2().machine_timing_48_or_p3() ==
                  emu.cpu().machine_timing_48_or_p3() &&
                  emu.im2().machine_timing_48_or_p3() == true,
              fmt("im2=%d cpu=%d",
                  emu.im2().machine_timing_48_or_p3() ? 1 : 0,
                  emu.cpu().machine_timing_48_or_p3() ? 1 : 0));
    }

    // GH232-06 — the same seam at cold boot. The Next's NR 0x03 tim_sel
    // powers on at "011" (+3) per zxnext.vhd:1099, and jnext seeds exactly
    // that in init(); the gate must agree with the register on the very
    // first cycle. Pre-fix it did not, so a guest writing NR 0x03 back with
    // the value already in the register changed the pulse width.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
        const uint8_t tim = nextreg_tim_sel(emu);
        check("GH232-06",
              "Next cold boot: the /INT pulse-width gate agrees with the "
              "NR 0x03 tim_sel it booted with (011 = +3 → 32 cycles) "
              "[zxnext.vhd:1099 initialiser; :2033 gate]",
              tim == 0x03 && emu.cpu().machine_timing_48_or_p3() == true &&
                  emu.im2().machine_timing_48_or_p3() == true,
              fmt("tim_sel=0x%02X cpu=%d im2=%d", tim,
                  emu.cpu().machine_timing_48_or_p3() ? 1 : 0,
                  emu.im2().machine_timing_48_or_p3() ? 1 : 0));
    }
}

// ── Task 26 item 5: Multiface window backed by external SRAM 0x0A/0x0B ─
//
// VHDL zxnext.vhd:3029-3036 hard-wires the MF memory window
// ($0000-$3FFF when mf_mem_en=1) to external SRAM: ROM half → page 0x0A
// (read-only), RAM half → page 0x0B, with sram_pre_bank5 forced '0' so it
// is the external SRAM chip, not the bank-5 VRAM. The Emulator wires the
// backing Next-only (mirroring DivMmc set_ram_backing); standalone
// machines keep the private Multiface buffers (a real standalone MF had
// its own RAM/ROM chip).
//
// Helper: force the MF memory overlay active (mf_enable) without a CPU
// run — enable the peripheral, arm NMI via the button, then present the
// 0x0066 M1 fetch which latches mf_enable per multiface.vhd:169-176.
static void mf_activate(Emulator& emu) {
    emu.multiface().set_enabled(true);
    emu.multiface().button_press();          // nmi_active=1, invisible=0
    emu.multiface().on_m1(0x0066, true);      // fetch_66 → mf_enable=1
    emu.mmu().set_boot_rom_enabled(false);    // lift the higher-priority bootrom
}

static void test_task26_mf_sram_backing() {
    // Leg A — Next: MF window reads/writes physical SRAM pages 0x0A/0x0B.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Seed the external SRAM pages the MF window should be wired to.
        emu.ram().page_ptr(0x0A)[0] = 0xA5;   // ROM half sentinel
        emu.ram().page_ptr(0x0B)[0] = 0x5A;   // RAM half sentinel
        mf_activate(emu);

        const bool active   = emu.multiface().is_mem_active();
        const uint8_t rd_rom = emu.mmu().read(0x0000);   // ROM half → page 0x0A
        const uint8_t rd_ram = emu.mmu().read(0x2000);   // RAM half → page 0x0B

        // RAM half is writable and lands in page 0x0B.
        emu.mmu().write(0x2000, 0x77);
        const uint8_t ram_after = emu.ram().page_ptr(0x0B)[0];
        // ROM half is read-only (VHDL sram_pre_rdonly = NOT cpu_a(13)) —
        // a write must NOT reach page 0x0A.
        emu.mmu().write(0x0000, 0x11);
        const uint8_t rom_after = emu.ram().page_ptr(0x0A)[0];

        check("MF-SRAM-01",
              "Next MF window reads external SRAM pages 0x0A (ROM half) / "
              "0x0B (RAM half) per VHDL :3029-3036",
              active && rd_rom == 0xA5 && rd_ram == 0x5A,
              fmt("active=%d rd_rom=0x%02X rd_ram=0x%02X", active, rd_rom, rd_ram));
        check("MF-SRAM-02",
              "Next MF RAM half writes reach SRAM page 0x0B; ROM half is "
              "read-only (page 0x0A unchanged)",
              ram_after == 0x77 && rom_after == 0xA5,
              fmt("ram[0x0B][0]=0x%02X rom[0x0A][0]=0x%02X (want 0x77/0xA5)",
                  ram_after, rom_after));
    }

    // Leg B — standalone 128K: MF window is NOT backed by SRAM (private
    // buffers). Writing the SRAM pages must not be visible through the MF
    // window, and MF RAM writes must not reach the SRAM pages.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX128K;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        emu.ram().page_ptr(0x0A)[0] = 0xA5;
        emu.ram().page_ptr(0x0B)[0] = 0x5A;
        mf_activate(emu);

        const uint8_t rd_rom = emu.mmu().read(0x0000);   // private ROM (0xFF fill)
        emu.mmu().write(0x2000, 0x77);                    // private RAM, not SRAM
        const uint8_t sram_0b = emu.ram().page_ptr(0x0B)[0];

        check("MF-SRAM-03",
              "standalone (128K) MF window is unaffected by SRAM pages "
              "0x0A/0x0B — reads the private buffer, not page 0x0A",
              rd_rom != 0xA5,
              fmt("rd_rom=0x%02X (must NOT be 0xA5)", rd_rom));
        check("MF-SRAM-04",
              "standalone (128K) MF RAM write stays in the private buffer, "
              "does NOT reach SRAM page 0x0B",
              sram_0b == 0x5A,
              fmt("sram[0x0B][0]=0x%02X (want 0x5A, unchanged)", sram_0b));
    }
}

static void test_g156_boot_hold() {
    set_group("G156-HOLD");

    Emulator emu;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);

    // HOLD-01 — set_boot_hold_frames() is reflected immediately.
    emu.set_boot_hold_frames(5);
    check("G156-HOLD-01",
          "boot_hold_frames_remaining() reflects set_boot_hold_frames()",
          emu.boot_hold_frames_remaining() == 5,
          fmt("remaining=%u (want 5)", emu.boot_hold_frames_remaining()));

    // HOLD-02 — decrements by EXACTLY 1 per completed run_frame() call.
    for (int i = 0; i < 3; ++i) emu.run_frame();
    check("G156-HOLD-02",
          "boot_hold_frames_remaining() decrements by exactly 1 per run_frame()",
          emu.boot_hold_frames_remaining() == 2,
          fmt("remaining=%u after 3 run_frame() calls (want 2)",
              emu.boot_hold_frames_remaining()));

    const auto regs_mid_hold = emu.cpu().get_registers();

    // HOLD-03 — the remaining 2 frames exhaust it to exactly 0.
    for (int i = 0; i < 2; ++i) emu.run_frame();
    check("G156-HOLD-03",
          "boot_hold_frames_remaining() reaches exactly 0 after the full "
          "hold count of run_frame() calls",
          emu.boot_hold_frames_remaining() == 0,
          fmt("remaining=%u (want 0)", emu.boot_hold_frames_remaining()));

    // HOLD-04 — PC and R (Z80 refresh counter, incremented on every M1
    // fetch) are frozen across every held frame: no CPU instruction was
    // fetched/executed while boot_hold_frames_remaining_ > 0. R is the
    // key discriminator — even an instruction that happens to leave PC
    // unchanged (e.g. a self-loop already at that address) still bumps R
    // on real/FUSE Z80 hardware.
    const auto regs_end_hold = emu.cpu().get_registers();
    check("G156-HOLD-04",
          "PC and R are frozen across every held frame (no instruction "
          "executed while boot_hold_frames_remaining_ > 0)",
          regs_mid_hold.PC == regs_end_hold.PC && regs_mid_hold.R == regs_end_hold.R,
          fmt("PC %04X->%04X R %02X->%02X (expected unchanged)",
              regs_mid_hold.PC, regs_end_hold.PC, regs_mid_hold.R, regs_end_hold.R));

    // HOLD-05 — positive control: once the hold ends the CPU DOES resume
    // real execution (PC/R change over subsequent frames). Without this,
    // a "CPU never runs at all" mutation would also freeze PC/R forever
    // and slip past HOLD-04 undetected.
    for (int i = 0; i < 5; ++i) emu.run_frame();
    const auto regs_resumed = emu.cpu().get_registers();
    check("G156-HOLD-05",
          "CPU resumes real execution once the hold ends — PC/R change "
          "over post-hold frames (the hold is not permanent)",
          regs_resumed.PC != regs_end_hold.PC || regs_resumed.R != regs_end_hold.R,
          fmt("PC %04X->%04X R %02X->%02X (expected a change)",
              regs_end_hold.PC, regs_resumed.PC, regs_end_hold.R, regs_resumed.R));

    // HOLD-06/07/08/09 — save_state()/load_state() round-trip TAKEN
    // MID-HOLD preserves boot_hold_frames_remaining_ exactly and the
    // restored hold resumes correctly (not silently reset to 0, which
    // would make a rewind mid-hold jump straight into the loaded
    // program — the scenario the review flagged as untested rewind
    // correctness).
    Emulator emu2;
    EmulatorConfig cfg2;
    cfg2.type = MachineType::ZXN_ISSUE2;
    cfg2.rewind_buffer_frames = 0;
    emu2.init(cfg2);
    emu2.set_boot_hold_frames(10);
    emu2.run_frame();
    emu2.run_frame();
    check("G156-HOLD-06",
          "pre-save remaining is genuinely mid-hold (neither the initial "
          "value nor zero)",
          emu2.boot_hold_frames_remaining() == 8,
          fmt("remaining=%u (want 8)", emu2.boot_hold_frames_remaining()));

    size_t need = 0;
    {
        StateWriter measure(nullptr, 0);
        emu2.save_state(measure);
        need = measure.position();
    }
    std::vector<uint8_t> blob(need, 0);
    {
        StateWriter w(blob.data(), blob.size());
        emu2.save_state(w);
    }

    Emulator emu3;
    EmulatorConfig cfg3;
    cfg3.type = MachineType::ZXN_ISSUE2;
    cfg3.rewind_buffer_frames = 0;
    emu3.init(cfg3);
    {
        StateReader r(blob.data(), blob.size());
        emu3.load_state(r);
    }
    check("G156-HOLD-07",
          "save_state()/load_state() round-trip preserves "
          "boot_hold_frames_remaining_ exactly",
          emu3.boot_hold_frames_remaining() == 8,
          fmt("remaining after load=%u (want 8)", emu3.boot_hold_frames_remaining()));

    const auto regs_pre_resume = emu3.cpu().get_registers();
    for (int i = 0; i < 8; ++i) emu3.run_frame();
    check("G156-HOLD-08",
          "the restored hold correctly resumes: exactly the restored "
          "remaining count of run_frame() calls exhausts it to 0",
          emu3.boot_hold_frames_remaining() == 0,
          fmt("remaining=%u (want 0)", emu3.boot_hold_frames_remaining()));

    const auto regs_post_resume = emu3.cpu().get_registers();
    check("G156-HOLD-09",
          "PC/R stayed frozen for the entire restored hold — no "
          "instruction executed while resuming a mid-hold snapshot",
          regs_pre_resume.PC == regs_post_resume.PC &&
          regs_pre_resume.R == regs_post_resume.R,
          fmt("PC %04X->%04X R %02X->%02X (expected unchanged)",
              regs_pre_resume.PC, regs_post_resume.PC,
              regs_pre_resume.R, regs_post_resume.R));
}


// ── Snapshot saver full-pipeline round trip (Task 13b, G35) ────────────
//
// Complements the structural byte-layout checks in mmu_test.cpp
// (BOOT-SNAPSAVE-02/02B/02C/03/03B/03C), which cannot link jnext_core and
// so cannot call the real Emulator::load_szx()/load_nex() consumer path.
// Here we DO have a full Emulator on both ends: build a known machine
// state, save it, reload it into a FRESH Emulator via the exact same
// path the GUI's File > Save Snapshot... / Load... menu items use, and
// compare live state — registers, classic paging ports, RAM content,
// and border.

static bool write_temp_file(const std::vector<uint8_t>& bytes, std::string& path_out) {
    char tmpl[] = "/tmp/jnext-snapsave-rt-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return false;
    ssize_t written = write(fd, bytes.data(), bytes.size());
    close(fd);
    path_out = tmpl;
    return written == static_cast<ssize_t>(bytes.size());
}

static uint32_t ru32le(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8)
         | (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

// Independent, from-scratch .szx chunk walker — deliberately NOT using
// SzxLoader (which has no page-set/upper-bound validation of its own, so
// a loader-only round trip cannot catch a wrong page SET, only wrong
// content). Walks every ZXSTBLOCK exactly per the published zx-state
// format (dwId + dwSize header, skip dwSize bytes to the next block),
// records every ZXSTRAMPAGE chPageNo seen, and (belt-and-braces) asserts
// none exceeds 63 — the real-world ceiling libspectrum's
// szx.c:read_ramp_chunk enforces for every machine ID. This is what an
// independent reviewer's "assert on file contents against the published
// spec, not jnext's own loader" demands.
struct SzxRampScan {
    bool     ok = true;
    unsigned ramp_chunk_count = 0;
    unsigned max_page_seen = 0;
    bool     page_seen[256] = {};
    std::string detail;
};

static SzxRampScan scan_szx_ramp_pages(const std::vector<uint8_t>& b) {
    SzxRampScan r;
    if (b.size() < 8 || b[0]!='Z' || b[1]!='X' || b[2]!='S' || b[3]!='T') {
        r.ok = false; r.detail = "bad/missing ZXST header"; return r;
    }
    size_t pos = 8;
    while (pos + 8 <= b.size()) {
        char id[5] = {static_cast<char>(b[pos]), static_cast<char>(b[pos+1]),
                      static_cast<char>(b[pos+2]), static_cast<char>(b[pos+3]), 0};
        uint32_t sz = ru32le(b, pos + 4);
        if (pos + 8 + sz > b.size()) {
            r.ok = false; r.detail = fmt("chunk '%s' at %zu overruns file", id, pos);
            return r;
        }
        if (std::string(id) == "RAMP") {
            ++r.ramp_chunk_count;
            if (sz < 3) { r.ok = false; r.detail = "RAMP chunk too small"; return r; }
            uint8_t page = b[pos + 8 + 2];  // wFlags(2) + chPageNo(1)
            r.page_seen[page] = true;
            if (page > r.max_page_seen) r.max_page_seen = page;
            if (page > 63) {
                r.ok = false;
                r.detail = fmt("RAMP chPageNo=%u > 63 at file offset %zu — "
                                "REJECTED by real libspectrum (szx.c:read_ramp_chunk)",
                                page, pos);
                return r;
            }
        }
        pos += 8 + sz;
    }
    return r;
}

static void test_snapsave_szx_roundtrip() {
    set_group("SNAPSAVE-SZX-RT");

    // +3 exercises both classic paging ports (7FFD + 1FFD) — the fullest
    // classic-paging path SzxSaver/SzxLoader support.
    Emulator emu1;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX_PLUS3;
    cfg.rewind_buffer_frames = 0;
    emu1.init(cfg);

    Z80Registers regs = emu1.cpu().get_registers();
    regs.AF = 0x2244; regs.BC = 0x6688; regs.DE = 0xAACC; regs.HL = 0xEE11;
    regs.AF2 = 0x3355; regs.BC2 = 0x7799; regs.DE2 = 0xBBDD; regs.HL2 = 0xFF22;
    regs.IX = 0x4466; regs.IY = 0x8899; regs.SP = 0x7000; regs.PC = 0x6500;
    regs.I = 0x21; regs.R = 0x43; regs.IFF1 = 1; regs.IFF2 = 1; regs.IM = 1;
    regs.halted = false;
    emu1.cpu().set_registers(regs);

    // Distinctive RAM content in all 8 physical banks — a supported
    // machine (+3) now saves its FULL RAM (all 8 banks), not a truncated
    // subset (see SzxSaver class doc-comment SCOPE — the redesign this
    // replaces the old 64-bank-ceiling-clamp contract with).
    for (int bank = 0; bank < 8; ++bank) {
        for (int half = 0; half < 2; ++half) {
            uint8_t* p = emu1.ram().page_ptr(static_cast<uint16_t>(bank * 2 + half));
            for (int i = 0; i < 8192; ++i)
                p[i] = static_cast<uint8_t>(bank * 17 + half * 3 + i);
        }
    }

    emu1.port().out(0x7FFD, 0x05);   // bank 5 at 0xC000, ROM0, screen=normal
    emu1.port().out(0x1FFD, 0x01);   // +3 special paging bit set
    emu1.port().out(0x00FE, 0x04);   // border = 4

    auto save_result = SzxSaver::save(emu1);
    auto& bytes = save_result.data;
    check("SNAPSAVE-SZX-RT-00", "SzxSaver::save() returns a non-empty buffer "
          "and reports success for a supported machine (+3)",
          save_result.ok && !bytes.empty(), fmt("ok=%d size=%zu", save_result.ok, bytes.size()));

    std::string path;
    bool wrote = write_temp_file(bytes, path);
    check("SNAPSAVE-SZX-RT-01", "saved .szx bytes written to disk", wrote);
    if (!wrote) return;

    Emulator emu2;
    EmulatorConfig cfg2;
    cfg2.type = MachineType::ZX_PLUS3;
    cfg2.rewind_buffer_frames = 0;
    emu2.init(cfg2);

    bool loaded = emu2.load_szx(path);
    std::remove(path.c_str());
    check("SNAPSAVE-SZX-RT-02", "Emulator::load_szx() accepts the saved file", loaded);
    if (!loaded) return;

    Z80Registers r2 = emu2.cpu().get_registers();
    bool regs_ok = r2.AF==regs.AF && r2.BC==regs.BC && r2.DE==regs.DE && r2.HL==regs.HL
        && r2.AF2==regs.AF2 && r2.BC2==regs.BC2 && r2.DE2==regs.DE2 && r2.HL2==regs.HL2
        && r2.IX==regs.IX && r2.IY==regs.IY && r2.SP==regs.SP && r2.PC==regs.PC
        && r2.I==regs.I && r2.R==regs.R && r2.IFF1==regs.IFF1 && r2.IFF2==regs.IFF2
        && r2.IM==regs.IM && r2.halted==regs.halted;
    check("SNAPSAVE-SZX-RT-REGS",
          "full register set (both AF/BC/DE/HL sets, IX/IY/SP/PC, I/R/IFF/IM/halted) "
          "round-trips through save()->file->Emulator::load_szx()",
          regs_ok,
          fmt("AF %04X/%04X PC %04X/%04X SP %04X/%04X halted %d/%d",
              r2.AF, regs.AF, r2.PC, regs.PC, r2.SP, regs.SP, r2.halted, regs.halted));

    bool paging_ok = emu2.mmu().port_7ffd() == 0x05 && emu2.mmu().port_1ffd() == 0x01;
    check("SNAPSAVE-SZX-RT-PAGING",
          "classic paging ports (0x7FFD/0x1FFD) round-trip via ZXSTSPECREGS",
          paging_ok,
          fmt("7ffd=0x%02X 1ffd=0x%02X (want 0x05/0x01)",
              emu2.mmu().port_7ffd(), emu2.mmu().port_1ffd()));

    bool ram_ok = true;
    for (int bank = 0; bank < 8 && ram_ok; ++bank) {
        for (int half = 0; half < 2 && ram_ok; ++half) {
            const uint8_t* p = emu2.ram().page_ptr(static_cast<uint16_t>(bank * 2 + half));
            for (int i = 0; i < 8192; ++i) {
                if (p[i] != static_cast<uint8_t>(bank * 17 + half * 3 + i)) { ram_ok = false; break; }
            }
        }
    }
    check("SNAPSAVE-SZX-RT-RAM",
          "all 8 physical RAM banks (0-7) round-trip byte-for-byte via "
          "ZXSTRAMPAGE — a +3 save now carries its full RAM, not a "
          "truncated subset",
          ram_ok);

    bool border_ok = emu2.ula().get_border() == 4;
    check("SNAPSAVE-SZX-RT-BORDER",
          "border colour round-trips via ZXSTSPECREGS.chFe",
          border_ok, fmt("border=%d (want 4)", emu2.ula().get_border()));
}

// SNAPSAVE-SZX-RT-REFUSED — the Emulator-level refusal case (Task 13b
// redesign, 2026-07-13): jnext's DEFAULT machine (Next, ZXN_ISSUE2) has no
// ZXSTMID_* representation and its RAM cannot be described by the
// classic-Spectrum-8-bank .szx model — SzxSaver::save() MUST refuse
// outright (ok=false, empty data, non-empty error), never truncate or
// write a misrepresenting file. See SzxSaver class doc-comment
// SCOPE/REFUSAL. This is the common path in practice, since Next is
// jnext's default --machine.
static void test_snapsave_szx_refused_for_next() {
    set_group("SNAPSAVE-SZX-RT");

    Emulator emu;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);

    auto result = SzxSaver::save(emu);

    check("SNAPSAVE-SZX-RT-REFUSED",
          "SzxSaver::save() refuses outright for a Next machine: ok=false, "
          "no data written, a non-empty error explaining why",
          !result.ok && result.data.empty() && !result.error.empty(),
          fmt("ok=%d size=%zu error='%s'",
              result.ok, result.data.size(), result.error.c_str()));
}

// SNAPSAVE-SZX-RT-48K — 48K round trip through the REAL Emulator/SzxLoader
// pipeline (not just the structural byte checks in mmu_test.cpp), proving
// the {5,2,0} page-set redesign (BOOT-SNAPSAVE-02C) actually restores a
// live 48K machine correctly. Also independently scans the raw saved bytes
// (scan_szx_ramp_pages(), not SzxLoader) for the exact page set, since a
// loader-only round trip would not catch a wrong SET if both sides shared
// the same (wrong) assumption.
static void test_snapsave_szx_roundtrip_48k() {
    set_group("SNAPSAVE-SZX-RT");

    Emulator emu1;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = 0;
    emu1.init(cfg);

    Z80Registers regs = emu1.cpu().get_registers();
    regs.AF = 0x1357; regs.BC = 0x2468; regs.HL = 0xACE0; regs.SP = 0x5C00; regs.PC = 0x8000;
    regs.IFF1 = 1; regs.IFF2 = 1; regs.IM = 1; regs.halted = false;
    emu1.cpu().set_registers(regs);

    // Distinctive content in banks 0, 2, 5 (48K's real RAM) AND in a bank
    // outside that set (1) — bank 1 must NOT survive the round trip, since
    // a real 48K's RAM has no such bank.
    for (int bank : {0, 1, 2, 5}) {
        for (int half = 0; half < 2; ++half) {
            uint8_t* p = emu1.ram().page_ptr(static_cast<uint16_t>(bank * 2 + half));
            for (int i = 0; i < 8192; ++i)
                p[i] = static_cast<uint8_t>(0x40 + bank * 5 + half * 2 + i);
        }
    }
    emu1.port().out(0x00FE, 0x06);   // border = 6

    auto save_result = SzxSaver::save(emu1);
    check("SNAPSAVE-SZX-RT-48K-00", "SzxSaver::save() succeeds for 48K",
          save_result.ok && !save_result.data.empty(),
          fmt("ok=%d size=%zu", save_result.ok, save_result.data.size()));

    auto scan = scan_szx_ramp_pages(save_result.data);
    bool page_set_ok = scan.ok && scan.ramp_chunk_count == 3
        && scan.page_seen[0] && scan.page_seen[2] && scan.page_seen[5]
        && !scan.page_seen[1] && !scan.page_seen[3] && !scan.page_seen[4]
        && !scan.page_seen[6] && !scan.page_seen[7];
    check("SNAPSAVE-SZX-RT-48K-PAGESET",
          "the SAVED FILE's ZXSTRAMPAGE chPageNo set is exactly {0,2,5} — "
          "independently scanned from raw bytes, not via SzxLoader",
          page_set_ok,
          fmt("ok=%d ramp_chunks=%u seen={%d,%d,%d,%d,%d,%d,%d,%d} detail='%s'",
              scan.ok, scan.ramp_chunk_count,
              scan.page_seen[0], scan.page_seen[1], scan.page_seen[2], scan.page_seen[3],
              scan.page_seen[4], scan.page_seen[5], scan.page_seen[6], scan.page_seen[7],
              scan.detail.c_str()));

    std::string path;
    bool wrote = write_temp_file(save_result.data, path);
    check("SNAPSAVE-SZX-RT-48K-01", "saved 48K .szx bytes written to disk", wrote);
    if (!wrote) return;

    Emulator emu2;
    EmulatorConfig cfg2;
    cfg2.type = MachineType::ZX48K;
    cfg2.rewind_buffer_frames = 0;
    emu2.init(cfg2);

    bool loaded = emu2.load_szx(path);
    std::remove(path.c_str());
    check("SNAPSAVE-SZX-RT-48K-02", "Emulator::load_szx() accepts the saved 48K file", loaded);
    if (!loaded) return;

    Z80Registers r2 = emu2.cpu().get_registers();
    bool regs_ok = r2.AF==regs.AF && r2.BC==regs.BC && r2.HL==regs.HL
        && r2.SP==regs.SP && r2.PC==regs.PC && r2.IFF1==regs.IFF1 && r2.halted==regs.halted;
    check("SNAPSAVE-SZX-RT-48K-REGS",
          "register set round-trips through save()->file->Emulator::load_szx() for 48K",
          regs_ok, fmt("AF %04X/%04X PC %04X/%04X", r2.AF, regs.AF, r2.PC, regs.PC));

    bool ram_ok = true;
    for (int bank : {0, 2, 5}) {
        for (int half = 0; half < 2 && ram_ok; ++half) {
            const uint8_t* p = emu2.ram().page_ptr(static_cast<uint16_t>(bank * 2 + half));
            for (int i = 0; i < 8192; ++i) {
                if (p[i] != static_cast<uint8_t>(0x40 + bank * 5 + half * 2 + i)) { ram_ok = false; break; }
            }
        }
    }
    check("SNAPSAVE-SZX-RT-48K-RAM",
          "banks 0/2/5 (48K's real RAM) round-trip byte-for-byte via ZXSTRAMPAGE",
          ram_ok);

    // Bank 1 was never saved (48K page set is {0,2,5} only). load_szx()
    // calls Emulator::reset() (which zero-fills RAM) before applying the
    // snapshot's RAMP chunks, so bank 1 must read back as all-zero — if
    // the {0,1,2}-first-N-banks bug this test guards against were
    // present, chPageNo=1's payload would carry emu1's distinctive bank-1
    // pattern (set above) and land right back in physical bank 1, making
    // this assertion fail.
    bool bank1_untouched = true;
    for (int half = 0; half < 2 && bank1_untouched; ++half) {
        const uint8_t* p = emu2.ram().page_ptr(static_cast<uint16_t>(1 * 2 + half));
        for (int i = 0; i < 8192; ++i) {
            if (p[i] != 0x00) { bank1_untouched = false; break; }
        }
    }
    check("SNAPSAVE-SZX-RT-48K-BANK1-UNTOUCHED",
          "bank 1 (not part of a 48K's RAM) is never written by load_szx() "
          "— reads back as reset()'s all-zero fill, not the distinctive "
          "pattern emu1's physical bank 1 was seeded with",
          bank1_untouched);

    bool border_ok = emu2.ula().get_border() == 6;
    check("SNAPSAVE-SZX-RT-48K-BORDER",
          "border colour round-trips via ZXSTSPECREGS.chFe for 48K",
          border_ok, fmt("border=%d (want 6)", emu2.ula().get_border()));
}

static void test_snapsave_nex_roundtrip() {
    set_group("SNAPSAVE-NEX-RT");

    // Next is the format's primary real-world target.
    Emulator emu1;
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu1.init(cfg);

    // Bank 20 mapped contiguously (pages 40/41) at slots 6/7 — the entry
    // point NEX resumes into.
    emu1.mmu().set_page(6, 40);
    emu1.mmu().set_page(7, 41);
    for (int i = 0; i < 8192; ++i) {
        emu1.ram().page_ptr(40)[i] = static_cast<uint8_t>(0x10 + (i & 0xFF));
        emu1.ram().page_ptr(41)[i] = static_cast<uint8_t>(0x20 + (i & 0xFF));
    }

    Z80Registers regs = emu1.cpu().get_registers();
    regs.PC = 0xC100;   // inside the bank-20 window (0xC000-0xFFFF)
    regs.SP = 0xC500;
    emu1.cpu().set_registers(regs);
    emu1.port().out(0x00FE, 0x02);   // border = 2

    auto result = NexSaver::save(emu1);
    check("SNAPSAVE-NEX-RT-00", "NexSaver::save() returns a non-empty buffer",
          !result.data.empty(), fmt("size=%zu", result.data.size()));

    std::string path;
    bool wrote = write_temp_file(result.data, path);
    check("SNAPSAVE-NEX-RT-01", "saved .nex bytes written to disk", wrote);
    if (!wrote) return;

    Emulator emu2;
    EmulatorConfig cfg2;
    cfg2.type = MachineType::ZXN_ISSUE2;
    cfg2.rewind_buffer_frames = 0;
    emu2.init(cfg2);

    bool loaded = emu2.load_nex(path);
    std::remove(path.c_str());
    check("SNAPSAVE-NEX-RT-02", "Emulator::load_nex() accepts the saved file", loaded);
    if (!loaded) return;

    // HONEST LIMITATION (see NexSaver class doc-comment): only PC/SP
    // survive — Emulator::load_nex() resets before apply(), so every
    // other register is reset-baseline, not the original value.
    Z80Registers r2 = emu2.cpu().get_registers();
    check("SNAPSAVE-NEX-RT-PCSP",
          "PC/SP round-trip through save()->file->Emulator::load_nex() "
          "(the only two registers NEX's header carries)",
          r2.PC == regs.PC && r2.SP == regs.SP,
          fmt("PC %04X/%04X SP %04X/%04X", r2.PC, regs.PC, r2.SP, regs.SP));

    const uint8_t* p6 = emu2.ram().page_ptr(40);
    const uint8_t* p7 = emu2.ram().page_ptr(41);
    bool ram_ok = true;
    for (int i = 0; i < 8192 && ram_ok; ++i) {
        if (p6[i] != static_cast<uint8_t>(0x10 + (i & 0xFF))) ram_ok = false;
        if (p7[i] != static_cast<uint8_t>(0x20 + (i & 0xFF))) ram_ok = false;
    }
    check("SNAPSAVE-NEX-RT-RAM",
          "bank-20 (pages 40/41) content round-trips byte-for-byte through "
          "the .nex bank payload",
          ram_ok);

    bool border_ok = emu2.ula().get_border() == 2;
    check("SNAPSAVE-NEX-RT-BORDER",
          "border colour round-trips via the .nex header",
          border_ok, fmt("border=%d (want 2)", emu2.ula().get_border()));

    bool entry_bank_ok = emu2.mmu().get_page(6) == 40 && emu2.mmu().get_page(7) == 41;
    check("SNAPSAVE-NEX-RT-ENTRYBANK",
          "entry_bank re-establishes the CPU-executable mapping at "
          "0xC000-0xFFFF (MMU slots 6/7) in the freshly loaded Emulator",
          entry_bank_ok,
          fmt("slot6=%d slot7=%d (want 40/41)", emu2.mmu().get_page(6), emu2.mmu().get_page(7)));
}


// ── G33 Phase 1 — SA-BYTES tape-SAVE trap (Task 57) ──────────────────
//
// Exercises the full-Emulator tier of the tape-SAVE feature: the trap
// handler itself (TapSaver::handle_sa_bytes_trap) and the run_frame()
// arming gate — --tape-save active + ROM in slot 0 + PC==0x04C2 + the
// 48K SA-BYTES ROM-identity signature (21 3F 05 E5 = LD HL,0x053F /
// PUSH HL, bytes verified against the extracted 48.rom). The identity
// check exists because a plain PC gate fired during an ordinary
// NextZXOS boot and corrupted it (Task 57 review finding); row 03 is
// the discriminative negative: same PC, same ROM-in-slot-0, same armed
// saver, different ROM bytes — trap must NOT fire.
//
// Expected TAP bytes are hand-computed from the TAP container spec
// (LE length = payload+2, flag, payload, XOR checksum), never from
// TapSaver's own output. mkstemp temp files, unlinked per row.

static const uint8_t g33_payload[5] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };
// Hand-computed data block, flag 0xFF:
// checksum FF^DE=0x21 ^AD=0x8C ^BE=0x32 ^EF=0xDD ^01=0xDC.
static const uint8_t g33_expected_block[9] = {
    0x07, 0x00, 0xFF, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0xDC,
};

// Shared per-row setup: fresh Emulator, saver armed on a mkstemp path,
// payload at 0x9000, return address 0x8123 planted at SP 0x7FF0, JR $
// (18 FE) at 0x8123 to park the CPU after the trap.
static bool g33_setup(Emulator& emu, char* tmp_path) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    int fd = mkstemp(tmp_path);
    if (fd < 0) return false;
    close(fd);
    if (!emu.tap_saver().set_output(tmp_path)) return false;
    for (size_t i = 0; i < sizeof(g33_payload); ++i)
        emu.mmu().write(static_cast<uint16_t>(0x9000 + i), g33_payload[i]);
    emu.mmu().write(0x7FF0, 0x23);   // return address LE @ SP
    emu.mmu().write(0x7FF1, 0x81);
    emu.mmu().write(0x8123, 0x18);   // JR $
    emu.mmu().write(0x8124, 0xFE);
    auto regs = emu.cpu().get_registers();
    regs.AF   = 0xFF00;              // A = flag 0xFF, carry clear
    regs.IX   = 0x9000;
    regs.DE   = 0x0005;
    regs.SP   = 0x7FF0;
    regs.IFF1 = 0;                   // no frame-interrupt redirect
    regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
    return true;
}

static std::vector<uint8_t> g33_read_file(const char* path) {
    std::vector<uint8_t> bytes;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f) {
        bytes.resize(static_cast<size_t>(f.tellg()));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

static void test_g33_tapesave_trap() {
    set_group("G33-TAPESAVE-TRAP");

    // MMU-G33-TRAP-01 — the trap handler itself, called directly:
    // reads A/IX/DE, appends the hand-computed TAP block, and exits with
    // the LD-BYTES-trap return mechanics (IX advanced, DE=0, carry set,
    // return address popped into PC, SP+=2).
    {
        Emulator emu;
        char tmp_path[] = "/tmp/jnext-mmu-int-tapesave-XXXXXX";
        bool ok = g33_setup(emu, tmp_path);
        bool handled = ok && emu.tap_saver().handle_sa_bytes_trap(emu);
        auto file = g33_read_file(tmp_path);
        auto regs = emu.cpu().get_registers();
        bool file_ok = file.size() == sizeof(g33_expected_block) &&
                       std::memcmp(file.data(), g33_expected_block,
                                   sizeof(g33_expected_block)) == 0;
        bool regs_ok = regs.PC == 0x8123 && regs.SP == 0x7FF2 &&
                       regs.IX == 0x9005 && regs.DE == 0x0000 &&
                       (regs.AF & 0x0001) != 0;
        check("MMU-G33-TRAP-01",
              "handle_sa_bytes_trap: A/IX/DE -> hand-computed TAP block on "
              "file; exit state PC=popped ret, SP+=2, IX+=DE, DE=0, carry set "
              "(mirrors the LD-BYTES trap return mechanics)",
              ok && handled && file_ok && regs_ok,
              fmt("ok=%d handled=%d file_size=%zu file_ok=%d PC=0x%04X SP=0x%04X "
                  "IX=0x%04X DE=0x%04X carry=%d",
                  static_cast<int>(ok), static_cast<int>(handled), file.size(),
                  static_cast<int>(file_ok), regs.PC, regs.SP, regs.IX, regs.DE,
                  static_cast<int>(regs.AF & 1)));
        unlink(tmp_path);
    }

    // MMU-G33-TRAP-02 — run_frame() gate, positive: slot-0 ROM carries
    // the genuine SA-BYTES prologue at 0x04C2 (boot-ROM overlay used as
    // the controllable ROM image; is_slot_rom(0) holds at reset). With
    // PC=0x04C2 the trap must fire exactly once and park at the ret.
    {
        Emulator emu;
        char tmp_path[] = "/tmp/jnext-mmu-int-tapesave-XXXXXX";
        bool ok = g33_setup(emu, tmp_path);
        std::vector<uint8_t> fake_rom(0x2000, 0x00);
        fake_rom[0x04C2] = 0x21; fake_rom[0x04C3] = 0x3F;   // LD HL,0x053F
        fake_rom[0x04C4] = 0x05; fake_rom[0x04C5] = 0xE5;   // PUSH HL
        emu.mmu().set_boot_rom(fake_rom.data(), fake_rom.size());
        emu.mmu().set_boot_rom_enabled(true);
        auto regs = emu.cpu().get_registers();
        regs.PC = TapSaver::SA_BYTES_ADDR;
        emu.cpu().set_registers(regs);
        emu.run_frame();
        auto file = g33_read_file(tmp_path);
        auto post = emu.cpu().get_registers();
        bool file_ok = file.size() == sizeof(g33_expected_block) &&
                       std::memcmp(file.data(), g33_expected_block,
                                   sizeof(g33_expected_block)) == 0;
        check("MMU-G33-TRAP-02",
              "run_frame gate positive: SA-BYTES signature in slot-0 ROM + "
              "PC=0x04C2 + armed saver -> trap fires once, block on file, "
              "CPU parked at popped return address",
              ok && emu.tap_saver().blocks_written() == 1 && file_ok &&
                  post.PC == 0x8123,
              fmt("ok=%d blocks=%zu file_size=%zu file_ok=%d PC=0x%04X",
                  static_cast<int>(ok), emu.tap_saver().blocks_written(),
                  file.size(), static_cast<int>(file_ok), post.PC));
        unlink(tmp_path);
    }

    // MMU-G33-TRAP-03 — run_frame() gate negative (the NextZXOS-boot
    // false-fire class): everything identical to row 02 EXCEPT the ROM
    // bytes at 0x04C2 are not the SA-BYTES prologue (JR $ here — any
    // non-48K ROM). The trap must NOT fire: zero blocks, empty file,
    // and the CPU actually executes the ROM code at 0x04C2.
    {
        Emulator emu;
        char tmp_path[] = "/tmp/jnext-mmu-int-tapesave-XXXXXX";
        bool ok = g33_setup(emu, tmp_path);
        std::vector<uint8_t> fake_rom(0x2000, 0x00);
        fake_rom[0x04C2] = 0x18; fake_rom[0x04C3] = 0xFE;   // JR $
        emu.mmu().set_boot_rom(fake_rom.data(), fake_rom.size());
        emu.mmu().set_boot_rom_enabled(true);
        auto regs = emu.cpu().get_registers();
        regs.PC = TapSaver::SA_BYTES_ADDR;
        emu.cpu().set_registers(regs);
        emu.run_frame();
        auto file = g33_read_file(tmp_path);
        auto post = emu.cpu().get_registers();
        check("MMU-G33-TRAP-03",
              "run_frame gate negative: non-48K ROM bytes at 0x04C2 with the "
              "saver armed and PC=0x04C2 -> trap does NOT fire (zero blocks, "
              "empty file, CPU executes the real ROM code) — the ungated trap "
              "corrupted a plain NextZXOS boot (Task 57 review)",
              ok && emu.tap_saver().blocks_written() == 0 && file.empty() &&
                  post.PC == TapSaver::SA_BYTES_ADDR,
              fmt("ok=%d blocks=%zu file_size=%zu PC=0x%04X",
                  static_cast<int>(ok), emu.tap_saver().blocks_written(),
                  file.size(), post.PC));
        unlink(tmp_path);
    }
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

    test_nr03_machine_type_cold_boot_default();
    std::printf("  Group: MT-DEF (NR $03 cold-boot machine-type) — done\n");

    test_task26_mf_sram_backing();
    std::printf("  Group: MF-SRAM (Task 26 MF external-SRAM backing) — done\n");

    test_g156_boot_hold();
    std::printf("  Group: G156-HOLD (NEX boot-hold run_frame() branch) — done\n");

    test_snapsave_szx_roundtrip();
    std::printf("  Group: SNAPSAVE-SZX-RT (Task 13b .szx +3 full round trip) — done\n");

    test_snapsave_szx_refused_for_next();
    std::printf("  Group: SNAPSAVE-SZX-RT-REFUSED (Task 13b .szx refused for Next) — done\n");

    test_snapsave_szx_roundtrip_48k();
    std::printf("  Group: SNAPSAVE-SZX-RT-48K (Task 13b .szx 48K page-set round trip) — done\n");

    test_snapsave_nex_roundtrip();
    std::printf("  Group: SNAPSAVE-NEX-RT (Task 13b .nex full round trip) — done\n");

    test_g33_tapesave_trap();
    std::printf("  Group: G33-TAPESAVE-TRAP (Task 57 SA-BYTES SAVE trap + gate) — done\n");

    // Last: several groups above never call set_group(), so their rows land
    // in whatever bucket was current. Running this one at the end keeps the
    // per-group breakdown's existing attribution untouched.
    test_gh232_soft_reset_uses_nextreg_state();
    std::printf("  Group: GH232-SOFT-RESET-AXES — done\n");

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
