// jnext::dbg::Debugger — the backend suite, work packages B1 and B2 of epic #276.
//
// WHAT THIS SUITE IS, AND WHAT IT IS NOT. §7 of
// doc/design/debug-subsystem/backend.md gives `debugger_backend_test` to work
// package B5: one wiring row per verb AND per event kind, delivery rows,
// multi-client rows, adapter rows. This file is the B1 instalment of it — the
// rows that pin what B1 actually built, so the package does not land unproven:
//
//   CTL-*     one wiring row per control verb B1 implements: arm it THROUGH THE
//             FACADE, run, and assert the machine stopped where the verb
//             promises (PC, cycle, pause_reason). Plus the control rows §7
//             insists on — the same program run WITHOUT the verb, which must
//             sail straight past.
//   F1-*      `peek()` does not perturb: a sweep in +3 mode leaves
//             `p3_floating_bus_dat_` unchanged, AND the same sweep through
//             `read()` does change it (the pair is the point — a row that only
//             checks peek passes on a machine where nothing latches at all).
//   F2-*      `time().frame` advances with NO rewind buffer, and reports the
//             pre-increment tag.
//   INS-*     the inspection and mutation verbs, including the three-page
//             agreement rows that guard `Mmu::nr_page_ptr()` against drifting
//             from `rebuild_ptr()`.
//   PEND-*    the refusal of a verb a later sub-package owns, so "not
//             implemented" is a tested answer rather than a link error.
//
// ── AND THE B2 INSTALMENT (§4.3 CAP-EVT) ────────────────────────────────────
//
//   EVT-TBL-*  the `EventTable` DIRECTLY, as a value: ids, the live formula, the
//              §6 slot masks, `filter_matches` arm by arm for all fourteen
//              kinds, the 512-entry ring and its overflow contract, the INS-17
//              delivery history. Pure logic, tested pure — driving a whole
//              emulator to reach one `if` is how arms end up with no row.
//   EVT-BP-*   `BreakpointSet`'s half of the §6 mask, and the OR with the event
//              half that makes the hot path read exactly one byte.
//   EVT-MEM/PORT/NR/EXEC/TIME/RESET/INT/NMI/MAGIC/HOST/COP/DMA-*
//              the SITES and the DELIVERY through a running machine: the latch
//              really fires from inside an instruction, the drain really
//              delivers at the boundary, the payload really carries what the
//              site saw. Each has its negative control next to it.
//   EVT-OVF-*  the overflow contract, driven on purpose through the ring's test
//              hook with a Copper MOVE burst — §4.3 is explicit that this is
//              "a specified, tested behaviour, not 'unreachable'".
//   EVT-MUT-*  §4.2a: a debugger write fires no event ON ITSELF, for NextREG and
//              for `poke(Cpu)` alike.
//   EVT-TRANS-* the transient successor of `BreakpointSet`'s single one-shot.
//   EVT-GATE/SLOT-* the no-subscriber cost claim and `on_slot_remapped`, as
//              mechanism rows beside the `make bench` measurement.
//
// No ROM, no SD image: a 48K or +3 machine with a program written straight into
// RAM and PC/SP set by hand — the `step_out_test` / `rewind_test` idiom.
//
// Run: ./build/test/debugger_backend_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debug/event_table.h"
#include "peripheral/copper.h"
#include "peripheral/nmi_source.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include "debug/rewind_buffer.h"
#include "input/keyboard.h"
#include "memory/mmu.h"
#include "peripheral/dma.h"
#include "video/palette.h"
#include "video/sprites.h"
// GH #278 WP4d — the INS-14 render_layer rows.
#include "core/saveable.h"
#include "memory/ram.h"
#include "video/layer2.h"
// GH #276 B3 — CTL-12's reconstruct rows drive the REAL
// `emulator_frontend_cold_boot()`, not a stand-in: the whole point of the
// contract is what `~Emulator()` + placement-new does to a surviving
// `Debugger`, and a fake that only re-ran `init()` would not do it.
#include "platform/emulator_boot.h"
// GH #276 B4 — CAP-04's rows compare the file with what the format's saver
// produces for the same machine.
#include "core/rzx_player.h"
#include "core/sna_saver.h"
#include "core/szx_saver.h"
// GH #276 B4 M2 — the HOST rows drive the real loop owner.
#include "platform/headless_app.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <memory>
#include <fstream>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>
#include "../row_id.h"

using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;
using jnext::dbg::PauseReason;
using jnext::dbg::RegId;
using jnext::dbg::Result;

// GH #276 B2 (§4.3 CAP-EVT)
using jnext::dbg::Access;
using jnext::dbg::Action;
// NOT `using jnext::dbg::Event` — `core/emulator.h` already declares a global
// `Event` (the scheduler's), and the two would conflict. Aliased instead.
using DbgEvent = jnext::dbg::Event;
using jnext::dbg::EventId;
using jnext::dbg::EventKind;
using jnext::dbg::EventSource;
using jnext::dbg::EventTable;
using jnext::dbg::NmiButton;
using jnext::dbg::ResetKind;
using jnext::dbg::Subscription;

// GH #276 B3 (§4.8 CAP-SES)
using jnext::dbg::ClientId;
using jnext::dbg::RunState;

// ── Tiny test harness (matches test/debug/step_out_test.cpp style) ──────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " — ", detail.c_str());
    }
}

static std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04X", v);
    return b;
}

// ── The test machine ───────────────────────────────────────────────────────

static constexpr uint16_t PROG    = 0x8000;   // caller
static constexpr uint16_t SUB     = 0x9000;   // subroutine
static constexpr uint16_t TEST_SP = 0xFF00;

// The one program every control row runs:
//
//   8000  00           NOP
//   8001  00           NOP
//   8002  CD 00 90     CALL SUB
//   8005  00           NOP
//   8006  18 FE        JR $        <- the landing marker; the program parks here
//   9000  00 00 C9     NOP NOP RET
//
// A Step Over at 0x8002 must stop at 0x8005; a Step Out armed inside SUB must
// stop at 0x8005 too; a Run to 0x8005 must stop there. A free run must park at
// 0x8006 having passed all three, which is what the control rows assert.
static constexpr uint16_t AFTER_CALL = 0x8005;
static constexpr uint16_t PARK       = 0x8006;

static void build(Emulator& emu, MachineType type = MachineType::ZX48K) {
    EmulatorConfig cfg;
    cfg.type = type;
    emu.init(cfg);

    const uint8_t main_prog[] = { 0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0x18, 0xFE };
    for (size_t i = 0; i < sizeof(main_prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), main_prog[i]);
    const uint8_t sub[] = { 0x00, 0x00, 0xC9 };
    for (size_t i = 0; i < sizeof(sub); ++i)
        emu.mmu().write(static_cast<uint16_t>(SUB + i), sub[i]);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;              // no frame interrupt while stepping
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// The debugger has to be "driving" for the step machinery to be live — an
// attached client and its live raster, set directly on `DebugState` for the rows
// that have no `Debugger` client to attach (GH #278 WP4c: until then the Qt
// window's `active()` bit, which set both).
static void attach_and_pause(Emulator& emu) {
    emu.debug_state().set_clients_attached(true);
    emu.debug_state().set_live_raster(true);
    emu.debug_state().pause();
}

static void run_until_paused(Emulator& emu, int max_frames = 4) {
    for (int i = 0; i < max_frames && !emu.debug_state().paused(); ++i)
        emu.run_frame();
}

static uint16_t pc_of(Emulator& emu) { return emu.cpu().get_registers().PC; }

// ── INS-01's 37-register table, one row each ────────────────────────────────
//
// The B1 review found four surviving mutations in this one function, all of the
// same shape: `RegId::F` writing AF's HIGH half, `RegId::A` writing the low
// half, `RegId::IX` writing `r.IY`, and `RegId::DE` dropping its write
// altogether — each shipping a fully green suite, because eight rows on ONE
// register pair cannot see the other 33 arms. So every arm gets a row, and each
// row asserts the WHOLE change set: what changed AND that nothing else did,
// which is what catches a write that lands in the sibling half or the wrong
// pair.
//
// The comparison is per 8-BIT FIELD, not per pair: `set_register(RegId::B)`
// must report `B` alone, so writing the low half instead names itself.

struct RegFields {
    // Every field of Z80Registers, decomposed. `MEMPTR`, `Q` and `IncDecZ` are
    // included deliberately — they are not `RegId`s, so a write that reached one
    // would otherwise be invisible.
    const char* name;
    uint16_t    value;
};

static std::vector<RegFields> reg_fields(const Z80Registers& r) {
    auto hi = [](uint16_t v) { return static_cast<uint16_t>(v >> 8); };
    auto lo = [](uint16_t v) { return static_cast<uint16_t>(v & 0xFF); };
    return {
        {"A", hi(r.AF)},   {"F", lo(r.AF)},
        {"B", hi(r.BC)},   {"C", lo(r.BC)},
        {"D", hi(r.DE)},   {"E", lo(r.DE)},
        {"H", hi(r.HL)},   {"L", lo(r.HL)},
        {"A'", hi(r.AF2)}, {"F'", lo(r.AF2)},
        {"B'", hi(r.BC2)}, {"C'", lo(r.BC2)},
        {"D'", hi(r.DE2)}, {"E'", lo(r.DE2)},
        {"H'", hi(r.HL2)}, {"L'", lo(r.HL2)},
        {"IXH", hi(r.IX)}, {"IXL", lo(r.IX)},
        {"IYH", hi(r.IY)}, {"IYL", lo(r.IY)},
        {"SPH", hi(r.SP)}, {"SPL", lo(r.SP)},
        {"PCH", hi(r.PC)}, {"PCL", lo(r.PC)},
        {"I", r.I}, {"R", r.R},
        {"IFF1", r.IFF1}, {"IFF2", r.IFF2}, {"IM", r.IM},
        {"halted", static_cast<uint16_t>(r.halted ? 1 : 0)},
        {"MEMPTR", r.MEMPTR}, {"Q", r.Q}, {"IncDecZ", r.IncDecZ},
    };
}

/// Space-separated names of the 8-bit fields that differ, in declaration order.
static std::string reg_delta(const Z80Registers& a, const Z80Registers& b) {
    const auto fa = reg_fields(a);
    const auto fb = reg_fields(b);
    std::string out;
    for (size_t i = 0; i < fa.size(); ++i) {
        if (fa[i].value == fb[i].value) continue;
        if (!out.empty()) out += " ";
        out += fa[i].name;
    }
    return out;
}

/// Zero every register, then write `value` to `reg`, and return what changed.
/// The baseline is READ BACK after zeroing rather than assumed, so whatever the
/// CPU core does with the three non-RegId fields is part of the baseline and
/// cannot be mistaken for the write's own effect.
static std::string write_one_register(Emulator& emu, Debugger& dbg,
                                     RegId reg, uint16_t value) {
    Z80Registers zero{};
    emu.cpu().set_registers(zero);
    const Z80Registers before = emu.cpu().get_registers();
    if (dbg.set_register(1, reg, value) != Result::Ok) return "REFUSED";
    return reg_delta(before, emu.cpu().get_registers());
}

// ── GH #276 B2 helpers ─────────────────────────────────────────────────────

/// Every delivery a subscription saw, in order. The handler captures it by
/// reference, so a `Rec` must outlive the subscription that records into it —
/// which is why every B2 block below is scoped.
struct Rec {
    std::vector<DbgEvent> evs;
};

static jnext::dbg::Handler recorder(Rec& r) {
    return [&r](const DbgEvent& ev, Debugger&) {
        r.evs.push_back(ev);
        return Action::Continue;
    };
}

/// A machine with `bytes` at PROG, PC there, interrupts off, and the backend
/// ARMED — `DebugState::active()`, which is what `armed()` reads and therefore
/// the gate on every event site (§4.1). Without it the whole pipeline is inert,
/// which is a property EVT-GATE-10..12 assert deliberately.
static void build_armed(Emulator& emu, const std::vector<uint8_t>& bytes,
                        MachineType type = MachineType::ZX48K) {
    EmulatorConfig cfg;
    cfg.type = type;
    emu.init(cfg);
    for (size_t i = 0; i < bytes.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), bytes[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = PROG;
    r.SP = TEST_SP;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
    emu.debug_state().set_clients_attached(true);
    emu.debug_state().set_live_raster(true);
}

// ── GH #276 B3 helpers (§4.8 CAP-SES) ──────────────────────────────────────

/// Write `bytes` at PROG and point PC/SP at it, WITHOUT init() and WITHOUT
/// arming. Two reasons it is not `build_armed`:
///
///   * B3's rows arm the machine by ATTACHING A CLIENT, which is the thing under
///     test — `build_armed`'s direct arm would make every `attached()` /
///     `armed()` row pass whatever `attach()` did.
///   * after a cold boot the RAM is wiped, so the program has to be reloaded
///     into the RECONSTRUCTED machine without re-running init().
static void load_prog(Emulator& emu, const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), bytes[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

/// The one program the reconstruct rows run: a GUEST write to 0x5010, which is
/// what a `Mem{Write}` subscription there can actually see. A backend `poke()`
/// could not — §4.2a makes a debugger write fire no event on itself, and
/// `DebugState::watchpoints_live()` is false outside `GuestExecutionScope`.
///
///   8000  3E nn        LD A,nn
///   8002  32 10 50     LD (0x5010),A
///   8005  18 FE        JR $
static constexpr uint16_t WATCHED = 0x5010;
static void load_writer(Emulator& emu, uint8_t val) {
    load_prog(emu, { 0x3E, val, 0x32, 0x10, 0x50, 0x18, 0xFE });
}

static jnext::dbg::ClientInfo client(
    const char* name,
    jnext::dbg::ClientKind kind = jnext::dbg::ClientKind::Test) {
    jnext::dbg::ClientInfo ci;
    ci.name = name;
    ci.kind = kind;
    return ci;
}

/// A `Listener` that records every push, in order. The seven overrides are all
/// pure virtual in the published header on purpose (a silently ignored
/// notification is the failure a default empty override invites), so this
/// implements all seven and records all seven — a row that asserts one of them
/// would otherwise not notice the others going missing.
struct RecListener : jnext::dbg::Listener {
    std::vector<jnext::dbg::PausedInfo>  paused;
    std::vector<ClientId>                resumed;
    std::vector<ResetKind>               resets;
    std::vector<uint32_t>                frames;
    std::vector<jnext::dbg::EventKindMask> subs;
    std::vector<int>                     exits;
    std::vector<std::pair<jnext::dbg::LogLevel, std::string>> logs;

    void on_paused(const jnext::dbg::PausedInfo& i) override { paused.push_back(i); }
    void on_resumed(ClientId by) override                    { resumed.push_back(by); }
    void on_reset(ResetKind k) override                      { resets.push_back(k); }
    void on_frame_ended(uint32_t f) override                 { frames.push_back(f); }
    void on_subscriptions_changed(jnext::dbg::EventKindMask k) override {
        subs.push_back(k);
    }
    void on_exit_requested(int code) override                { exits.push_back(code); }
    void on_log(jnext::dbg::LogLevel lvl, const std::string& t) override {
        logs.emplace_back(lvl, t);
    }

    /// Every counter at once, for a failure message: a row that fails on
    /// `paused.size()` almost always wants to know what else arrived.
    std::string trail() const {
        return "paused=" + std::to_string(paused.size()) +
               " resumed=" + std::to_string(resumed.size()) +
               " resets=" + std::to_string(resets.size()) +
               " frames=" + std::to_string(frames.size()) +
               " subs=" + std::to_string(subs.size()) +
               " exits=" + std::to_string(exits.size()) +
               " logs=" + std::to_string(logs.size());
    }
};

/// A `Service` that records the `wait_ms` of every `service_once()` call, so the
/// drain policy can be asserted on WHAT IT ASKED FOR rather than only on how
/// many times — the budget arms differ in which value reaches the service.
struct FakeService : jnext::dbg::Service {
    std::vector<int> calls;
    int              flushes             = 0;
    size_t           flushed_after_calls = 0;
    bool             connected           = false;
    /// Report `Serviced` for ever: a peer that never stops talking, which is what
    /// makes the paused drain observable at all.
    bool             always_serviced     = false;
    /// Report `Serviced` this many times and then `Idle`: the other way out of
    /// the drain loop.
    int              serviced_budget     = 0;
    /// An unbounded drain must FAIL a row, not take the host down: without
    /// `budget_ms` bounding it, `pump()` grew `calls` until the kernel
    /// OOM-killed the suite at ~96 GiB (2026-09-28, twice). Past this many
    /// calls — far beyond what any bounded drain reaches — the fake goes quiet
    /// and `runaway` records that it had to.
    static constexpr size_t kRunawayCalls = 20'000'000;
    bool             runaway             = false;
    /// The COMMAND a call executes, when a row needs one: a real server runs
    /// each complete command synchronously against the `Debugger` from inside
    /// `service_once()`, so a `pause` or a `run` here changes the machine in the
    /// middle of the drain, exactly as a peer's would.
    std::function<void()> on_call;

    jnext::dbg::ServiceStep service_once(int wait_ms) override {
        if (calls.size() >= kRunawayCalls) {
            runaway = true;
            return jnext::dbg::ServiceStep::Idle;
        }
        calls.push_back(wait_ms);
        if (on_call) on_call();
        if (always_serviced) return jnext::dbg::ServiceStep::Serviced;
        if (serviced_budget > 0) {
            --serviced_budget;
            return jnext::dbg::ServiceStep::Serviced;
        }
        return jnext::dbg::ServiceStep::Idle;
    }
    void flush_notifications() override {
        ++flushes;
        flushed_after_calls = calls.size();
    }
    bool peer_connected() const override { return connected; }

    void reset() {
        calls.clear();
        flushes             = 0;
        flushed_after_calls = 0;
        runaway             = false;
    }
    std::string trail() const {
        std::string out = "waits=[";
        for (size_t i = 0; i < calls.size(); ++i) {
            if (i) out += ",";
            out += std::to_string(calls[i]);
        }
        return out + "]";
    }
};

// The Copper is programmed the way the machine programs it: through NR
// 0x61/0x62/0x63, which Emulator::init() wires to the engine. Same word
// encoding as test/copper/copper_test.cpp.
static constexpr uint16_t HALT_WORD = 0x8000u | 0x1FFu;   // WAIT with vpos 511

static uint16_t move_word(uint8_t reg, uint8_t val) {
    return static_cast<uint16_t>(((reg & 0x7F) << 8) | val);
}
static uint16_t wait_word(int hpos, int vpos) {
    return static_cast<uint16_t>(0x8000u | ((hpos & 0x3F) << 9) | (vpos & 0x1FF));
}

static void copper_set_ptr(Emulator& emu, uint16_t byte_addr) {
    const uint8_t mode_hi = static_cast<uint8_t>(emu.copper().read_reg_0x62() & 0xC0);
    emu.nextreg().write(0x61, static_cast<uint8_t>(byte_addr & 0xFF));
    emu.nextreg().write(0x62, static_cast<uint8_t>(mode_hi | ((byte_addr >> 8) & 0x07)));
}

static void copper_program(Emulator& emu, const std::vector<uint16_t>& words) {
    copper_set_ptr(emu, 0);
    for (uint16_t w : words) {
        emu.nextreg().write(0x63, static_cast<uint8_t>(w >> 8));
        emu.nextreg().write(0x63, static_cast<uint8_t>(w & 0xFF));
    }
    copper_set_ptr(emu, 0);
}

static void copper_set_mode(Emulator& emu, uint8_t mode) {
    const uint8_t hi = static_cast<uint8_t>(emu.copper().read_reg_0x62() & 0x07);
    emu.nextreg().write(0x62, static_cast<uint8_t>(((mode & 3) << 6) | hi));
}
static void copper_start(Emulator& emu) { copper_set_mode(emu, 1); }
static void copper_stop(Emulator& emu)  { copper_set_mode(emu, 0); }


// ===========================================================================
// GH #276 B4 — the input / capture / bookmark / coverage instalment.
//
// One function per B4 item, called from `main()` after the B3 rows. Same
// harness, same program (`build()`), same rule as the rest of this file: a row
// asserts what the MACHINE did, and every conditional gets a row on each side.
// ===========================================================================

/// The PCs the `build()` program executes on a free run from PROG, in order:
/// NOP, NOP, CALL SUB → NOP, NOP, RET → NOP, JR $ (which parks). The CALL's
/// two operand bytes (0x8003/0x8004) and everything after the JR are NOT
/// executed — they are the negative half of every exact-set row below.
static const std::vector<uint16_t> kFreeRunPcs = {
    0x8000, 0x8001, 0x8002, 0x9000, 0x9001, 0x9002, 0x8005, 0x8006 };

/// "`bits` is exactly this set" — the count AND every member, so a row fails on
/// a missing PC and on an extra one alike.
static bool coverage_is(const jnext::dbg::CoverageBits& bits,
                        const std::vector<uint16_t>& pcs) {
    if (bits.count() != pcs.size()) return false;
    for (uint16_t pc : pcs)
        if (!bits.test(pc)) return false;
    return true;
}

static std::string coverage_list(const jnext::dbg::CoverageBits& bits) {
    std::string out = std::to_string(bits.count()) + " set:";
    int shown = 0;
    for (size_t i = 0; i < bits.size() && shown < 16; ++i)
        if (bits.test(i)) { out += " " + hex(static_cast<unsigned>(i)); ++shown; }
    return out;
}

// ── INS-20 — PC coverage ────────────────────────────────────────────────────
static void b4_coverage_rows() {
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("INS-20-01", "a fresh backend has coverage OFF, reads all-zero, and "
                           "publishes no sink to the hot path",
              !dbg.coverage_enabled() && dbg.coverage().none() &&
                  emu.debug_state().coverage_sink() == nullptr);

        // NO CLIENT IS ATTACHED here, deliberately: coverage is not gated on
        // attach (see `step_one_instruction()`), so a row that attached first
        // could not tell the difference.
        dbg.coverage_enable(true);
        emu.run_frame();
        check("INS-20-02", "switched on, a free run records EXACTLY the PCs it "
                           "executed — the CALL's operand bytes and the bytes "
                           "after JR $ stay clear — with no client attached",
              dbg.coverage_enabled() && coverage_is(dbg.coverage(), kFreeRunPcs) &&
                  emu.debug_state().coverage_sink() == &dbg.coverage(),
              coverage_list(dbg.coverage()));

        dbg.coverage_enable(true);
        check("INS-20-03", "switching it on again is idempotent — it does not clear",
              coverage_is(dbg.coverage(), kFreeRunPcs), coverage_list(dbg.coverage()));

        dbg.coverage_clear();
        const bool cleared = dbg.coverage().none() && dbg.coverage_enabled();
        emu.run_frame();   // parked at JR $: only 0x8006 executes now
        check("INS-20-04", "coverage_clear() empties the set and recording goes on: "
                           "the next frame re-marks only the PC still executing",
              cleared && coverage_is(dbg.coverage(), {PARK}),
              coverage_list(dbg.coverage()));

        dbg.coverage_enable(false);
        const bool off_clear = dbg.coverage().none() && !dbg.coverage_enabled() &&
                               emu.debug_state().coverage_sink() == nullptr;
        emu.run_frame();
        check("INS-20-05", "switched off, the set reads all-zero at once (off IS a "
                           "clear) and nothing is recorded afterwards",
              off_clear && dbg.coverage().none(), coverage_list(dbg.coverage()));
    }
    {
        // The CONTROL: the same free run, coverage never switched on.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        check("INS-20-06", "never switched on, a free run records nothing",
              dbg.coverage().none() && pc_of(emu) == PARK, coverage_list(dbg.coverage()));
    }
    {
        // THE THREE EXECUTION ROOTS. `run_frame()` is INS-20-02; these are the
        // other two bodies over `step_one_instruction()`.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        dbg.coverage_enable(true);
        dbg.pause(a);
        dbg.step_into(a);   // debugger_step() → step_frame_slot()
        check("INS-20-07", "a debugger Step (step_frame_slot) records the one PC it "
                           "executed, and only that one",
              coverage_is(dbg.coverage(), {PROG}) && pc_of(emu) == PROG + 1,
              coverage_list(dbg.coverage()));
        dbg.coverage_clear();
        emu.execute_single_instruction();
        check("INS-20-08", "execute_single_instruction() records the PC it executed",
              coverage_is(dbg.coverage(), {static_cast<uint16_t>(PROG + 1)}),
              coverage_list(dbg.coverage()));
    }
    {
        // A SLOT THAT FETCHES NO OPCODE records nothing. The NMI acknowledge
        // executes no instruction at PC — execute() returns with PC at 0x0066 —
        // and the instruction at PROG has not run. Then the handler's first
        // instruction DOES run, and is recorded: both arms of the gate.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.coverage_enable(true);
        emu.cpu().request_nmi();
        emu.execute_single_instruction();
        const bool ack_clear = dbg.coverage().none() && pc_of(emu) == 0x0066;
        const std::string ack_list = coverage_list(dbg.coverage());
        emu.execute_single_instruction();
        check("INS-20-09", "an NMI-acknowledge slot records NOTHING (the opcode at PC "
                           "was never fetched); the handler's first instruction is "
                           "recorded",
              ack_clear && coverage_is(dbg.coverage(), {0x0066}),
              "after ack: " + ack_list + " / after step: " + coverage_list(dbg.coverage()));
    }
    {
        // CTL-12 RULE 2 — "coverage enables" — ON EVERY ROUTE THAT LANDS A NEW
        // MACHINE. The rebuilt `DebugState` has no sink; the backend re-publishes
        // it. The set recorded before the boot is KEPT ("since clear" is not
        // "since boot") and the rebuilt machine's PCs are added to it.
        struct Out {
            bool enabled, sink, kept_old, records_new, off_sink_null;
            bool operator==(const Out& o) const {
                return enabled == o.enabled && sink == o.sink && kept_old == o.kept_old &&
                       records_new == o.records_new && off_sink_null == o.off_sink_null;
            }
        };
        auto show = [](const Out& o) {
            return std::string("en=") + (o.enabled ? "1" : "0") + " sink=" +
                   (o.sink ? "1" : "0") + " kept=" + (o.kept_old ? "1" : "0") +
                   " new=" + (o.records_new ? "1" : "0") + " offnull=" +
                   (o.off_sink_null ? "1" : "0");
        };
        // path 0 = reset(Hard), 1 = load() through a reconstructing driver,
        // 2 = the guest path (begin, the loop owner's boot, done).
        auto run_path = [&](int path, bool on) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            auto boot = [&]() {
                emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
            };
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() { boot(); return true; };
            d.load      = [&](const std::string&) { boot(); return true; };
            dbg.set_loop_driver(d);
            if (on) dbg.coverage_enable(true);
            emu.run_frame();                        // records kFreeRunPcs when on
            if (path == 0)      dbg.reset(a, ResetKind::Hard);
            else if (path == 1) dbg.load(a, "game.nex");
            else              { dbg.on_cold_boot_begin(); boot(); dbg.on_cold_boot_done(); }
            // A NEW program in the rebuilt machine, at an address the old one
            // never executed: JR $ at 0xA000.
            emu.mmu().write(0xA000, 0x18);
            emu.mmu().write(0xA001, 0xFE);
            Z80Registers r = emu.cpu().get_registers();
            r.PC = 0xA000; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
            emu.run_frame();
            Out o;
            o.enabled       = dbg.coverage_enabled();
            o.sink          = emu.debug_state().coverage_sink() == &dbg.coverage();
            o.kept_old      = dbg.coverage().test(SUB);
            o.records_new   = dbg.coverage().test(0xA000);
            o.off_sink_null = emu.debug_state().coverage_sink() == nullptr;
            dbg.detach(a);
            return o;
        };
        const Out want_on {true,  true,  true,  true,  false};
        const Out want_off{false, false, false, false, true};
        const Out on0 = run_path(0, true), on1 = run_path(1, true), on2 = run_path(2, true);
        check("INS-20-10", "coverage ON survives every machine-replacing route — "
                           "reset(Hard), a reconstructing load(), the guest path: the "
                           "sink is re-published, the pre-boot set is kept, and the "
                           "rebuilt machine's PCs are recorded",
              on0 == want_on && on1 == want_on && on2 == want_on,
              "reset[" + show(on0) + "] load[" + show(on1) + "] guest[" + show(on2) + "]");
        const Out off0 = run_path(0, false), off1 = run_path(1, false), off2 = run_path(2, false);
        check("INS-20-11", "and coverage OFF stays off on all three: no sink, nothing "
                           "recorded",
              off0 == want_off && off1 == want_off && off2 == want_off,
              "reset[" + show(off0) + "] load[" + show(off1) + "] guest[" + show(off2) + "]");
    }
    {
        // THE SNAPSHOT ADVANCE EXECUTES INSTRUCTIONS, under `SuspendScope`, which
        // clears the attach bits. The set must still record them: stop mid-frame
        // at the CALL, clear, and let `save_state_bytes(AdvanceToBoundary)` run
        // the frame out.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        Subscription s;
        s.kind      = EventKind::Execute;
        s.filter.lo = PROG + 2; s.filter.hi = PROG + 2;
        s.action    = Action::Stop;
        dbg.subscribe(a, s);
        dbg.coverage_enable(true);
        emu.run_frame();                                   // stops AT the CALL
        const bool mid = emu.frame_in_progress() && pc_of(emu) == PROG + 2;
        dbg.coverage_clear();
        const auto saved = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("INS-20-12", "a snapshot's frame-boundary advance (SuspendScope) records "
                           "the PCs it executed — a coverage set must not have holes",
              mid && saved.status == Result::Ok && !emu.frame_in_progress() &&
                  coverage_is(dbg.coverage(), {static_cast<uint16_t>(PROG + 2), 0x9000,
                                               0x9001, 0x9002, AFTER_CALL, PARK}),
              coverage_list(dbg.coverage()));
    }
    {
        // THE PAIR, coverage's half: the sink points INTO `Impl`, so the
        // destructor must retire it or the machine's next instruction writes into
        // freed memory. The machine outlives the `Debugger` and keeps running.
        Emulator emu; build(emu);
        bool published = false;
        {
            Debugger dbg(emu);
            dbg.coverage_enable(true);
            published = emu.debug_state().coverage_sink() == &dbg.coverage();
        }
        const bool retired = emu.debug_state().coverage_sink() == nullptr;
        emu.run_frame();
        check("LIFE-07", "~Debugger() retires the coverage sink it published, and the "
                         "machine runs on without it",
              published && retired && pc_of(emu) == PARK);
    }
}

// ── INS-13 — the richer TraceEntry (REQ-zrcp-08) ────────────────────────────
static void b4_trace_rows() {
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        // Distinctive values in every field the entry gained, so a field
        // written from its neighbour (I from R, IFF1 from IFF2, …) is visible.
        Z80Registers r = emu.cpu().get_registers();
        r.I = 0x3F; r.R = 0x55; r.IM = 1; r.IFF1 = 0; r.IFF2 = 1;
        emu.cpu().set_registers(r);
        std::array<uint8_t, 8> pages{};
        for (int s = 0; s < 8; ++s) pages[s] = emu.mmu().get_effective_page(s);
        dbg.set_trace_enabled(true);
        emu.run_frame();
        const auto got = dbg.trace_entries();
        const std::vector<TraceEntry>& es = got.value;
        const TraceEntry* first = es.empty() ? nullptr : &es[0];
        const TraceEntry* second = es.size() > 1 ? &es[1] : nullptr;
        const TraceEntry* in_sub = nullptr;
        for (const TraceEntry& e : es) if (e.pc == SUB) { in_sub = &e; break; }

        check("INS-13-09", "an entry carries I, R, IM, IFF1 and IFF2 as they were "
                           "BEFORE the instruction (R advances by one per NOP fetch)",
              first && second && first->pc == PROG && first->i == 0x3F &&
                  first->r == 0x55 && second->r == 0x56 && first->im == 1 &&
                  first->iff1 == 0 && first->iff2 == 1,
              first ? ("i=" + hex(first->i) + " r=" + hex(first->r) + " im=" +
                       std::to_string(first->im) + " iff1=" + std::to_string(first->iff1) +
                       " iff2=" + std::to_string(first->iff2))
                    : std::string("no entries"));
        check("INS-13-10", "(SP) is the word at SP: inside SUB it is the return "
                           "address the CALL pushed",
              in_sub && in_sub->sp == TEST_SP - 2 && in_sub->sp_word == AFTER_CALL,
              in_sub ? ("sp=" + hex(in_sub->sp) + " (sp)=" + hex(in_sub->sp_word))
                     : std::string("no entry at SUB"));
        bool mmu_ok = first != nullptr;
        bool distinct = false;
        for (int s = 0; first && s < 8; ++s) {
            if (first->mmu[s] != pages[s]) mmu_ok = false;
            if (pages[s] != pages[0]) distinct = true;
        }
        check("INS-13-11", "and the eight MMU effective pages, slot by slot "
                           "(the pages are not all equal, so a one-slot-for-all "
                           "write is visible)",
              mmu_ok && distinct);
    }
    {
        // The (SP) read must not PERTURB. +3 mode: SP in contended bank 5, PC in
        // uncontended bank 2, so the CPU's own opcode fetch never touches the
        // floating-bus latch and a `read()` of (SP) would leave it moved.
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        emu.mmu().write(0x4000, 0xA5);
        emu.mmu().write(0x4001, 0xA5);
        Z80Registers r = emu.cpu().get_registers();
        r.SP = 0x4000;
        emu.cpu().set_registers(r);
        dbg.set_trace_enabled(true);
        emu.mmu().set_p3_floating_bus_dat(0x3C);
        emu.execute_single_instruction();          // the NOP at PROG, traced
        const auto got = dbg.trace_entries();
        check("INS-13-12", "the trace's (SP) read leaves the +3 floating-bus latch "
                           "alone (peek, not read) — and still reads the word",
              emu.mmu().p3_floating_bus_dat() == 0x3C && got.value.size() == 1 &&
                  got.value[0].sp_word == 0xA5A5,
              "latch=" + hex(emu.mmu().p3_floating_bus_dat()));
    }
    {
        // Nor a watchpoint: a READ subscription on the word at SP. The program
        // never reads 0xFF00 (the CALL pushes below it), so only the trace could
        // fire it — and must not. The control arm: a READ watch on the pushed
        // return address DOES stop, at the RET, with the trace on — the watch
        // machinery is live and it is the trace's read that is invisible.
        auto run = [](uint16_t watched) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            Subscription s;
            s.kind      = EventKind::Mem;
            s.access    = Access::Read;
            s.filter.lo = watched; s.filter.hi = watched;
            s.action    = Action::Stop;
            dbg.subscribe(a, s);
            dbg.set_trace_enabled(true);
            emu.run_frame();
            return std::make_pair(emu.debug_state().paused(), pc_of(emu));
        };
        const auto quiet = run(TEST_SP);
        const auto live  = run(static_cast<uint16_t>(TEST_SP - 2));
        check("INS-13-13", "the trace's (SP) read fires no READ watch (the machine "
                           "runs to PARK), while the RET's own read of the stack does",
              !quiet.first && quiet.second == PARK && live.first,
              "quiet: paused=" + std::to_string(quiet.first) + " pc=" + hex(quiet.second) +
                  " live: paused=" + std::to_string(live.first));
    }
}

// ── CAP-02 — screen memory; ST-01 — the advance's SES-06 line ───────────────

/// The MUTATE lines a listener received, in order.
static std::vector<std::string> mutate_lines(const RecListener& l) {
    std::vector<std::string> out;
    for (const auto& e : l.logs)
        if (e.second.rfind("MUTATE ", 0) == 0) out.push_back(e.second);
    return out;
}

/// Stop the `build()` program mid-frame AT the CALL, through an `Execute`
/// subscription owned by `a`: `frame_in_progress()` is true afterwards and
/// 0x8000/0x8001 have executed.
static void stop_mid_frame_at_call(Emulator& emu, Debugger& dbg, ClientId a) {
    Subscription s;
    s.kind      = EventKind::Execute;
    s.filter.lo = PROG + 2; s.filter.hi = PROG + 2;
    s.action    = Action::Stop;
    dbg.subscribe(a, s);
    emu.run_frame();
}

static void b4_capture_state_rows() {
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.mmu().write(0x4000, 0x81);    // first pixel byte
        emu.mmu().write(0x57FF, 0x42);    // last pixel byte
        emu.mmu().write(0x5800, 0x38);    // first attribute
        const std::vector<uint8_t> d = dbg.ula_screen_dump();
        check("CAP-02-01", "ula_screen_dump() is the ULA's .SCR body: 6912 bytes, "
                           "pixels then attributes, as Ula::screen_dump() gives them",
              d.size() == 6912 && d == emu.ula().screen_dump() && d[0] == 0x81 &&
                  d[6143] == 0x42 && d[6144] == 0x38,
              "size=" + std::to_string(d.size()));
    }
    {
        // An observation: the +3 floating-bus latch (the one thing a stray
        // `Mmu::read()` of screen RAM would move) is untouched.
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        emu.mmu().write(0x4000, 0xA5);
        emu.mmu().set_p3_floating_bus_dat(0x3C);
        const std::vector<uint8_t> d = dbg.ula_screen_dump();
        check("CAP-02-02", "and it perturbs nothing: the +3 floating-bus latch is "
                           "unchanged by a dump of contended screen RAM",
              d.size() == 6912 && d[0] == 0xA5 && emu.mmu().p3_floating_bus_dat() == 0x3C,
              "latch=" + hex(emu.mmu().p3_floating_bus_dat()));
    }
    {
        // ST-01 — THE ADVANCE IS ATTRIBUTED. Mid-frame, AdvanceToBoundary runs
        // the frame out and emits one MUTATE line naming the client and the
        // clock it moved from and to.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        stop_mid_frame_at_call(emu, dbg, a);
        const bool mid = emu.frame_in_progress();
        const uint64_t before = emu.clock().get();
        const size_t n0 = mutate_lines(l).size();
        const auto saved = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        const uint64_t after = emu.clock().get();
        const auto lines = mutate_lines(l);
        char want[160];
        std::snprintf(want, sizeof(want),
                      "MUTATE clock (save_state_bytes advanced to the frame boundary) "
                      "0x%llX -> 0x%llX by %u",
                      static_cast<unsigned long long>(before),
                      static_cast<unsigned long long>(after), static_cast<unsigned>(a));
        check("ST-01-07", "a mid-frame AdvanceToBoundary save emits ONE SES-06 MUTATE "
                          "line: the clock it moved, from and to, attributed to the client",
              mid && saved.status == Result::Ok && after > before &&
                  lines.size() == n0 + 1 && lines.back() == want,
              lines.empty() ? std::string("no MUTATE line") : lines.back());

        // At a boundary nothing advances, so nothing is logged.
        const size_t n1 = lines.size();
        const auto again = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("ST-01-08", "and a save AT a frame boundary advances nothing and logs "
                          "nothing",
              again.status == Result::Ok && emu.clock().get() == after &&
                  mutate_lines(l).size() == n1);
    }
    {
        // The refusing mode, mid-frame: nothing advances, nothing is logged.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        stop_mid_frame_at_call(emu, dbg, a);
        const uint64_t before = emu.clock().get();
        const size_t n0 = mutate_lines(l).size();
        const auto r = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::RefuseMidFrame);
        check("ST-01-09", "RefuseMidFrame mid-frame answers NotAtFrameBoundary, moves "
                          "no clock and logs no MUTATE line",
              r.status == Result::NotAtFrameBoundary && r.value.empty() &&
                  emu.clock().get() == before && mutate_lines(l).size() == n0 &&
                  emu.frame_in_progress());
    }
}

// ── CAP-03 — named bookmarks ────────────────────────────────────────────────

/// What a restore is checked against: the machine as the bookmark caught it.
struct MachineMark {
    uint64_t clock = 0;
    uint16_t pc = 0;
    uint8_t  byte = 0;       // RAM at 0x6000
    bool operator==(const MachineMark& o) const {
        return clock == o.clock && pc == o.pc && byte == o.byte;
    }
};
static MachineMark mark_of(Emulator& emu) {
    return MachineMark{emu.clock().get(), pc_of(emu), emu.mmu().read(0x6000)};
}
static std::string show_mark(const MachineMark& m) {
    return "clock=" + std::to_string(m.clock) + " pc=" + hex(m.pc) + " [6000]=" + hex(m.byte);
}

static void b4_bookmark_rows() {
    using jnext::dbg::SaveStateMode;
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        emu.mmu().write(0x6000, 0x11);
        const MachineMark at_save = mark_of(emu);
        const Result saved = dbg.bookmark_save(a, "one", SaveStateMode::RefuseMidFrame);
        check("CAP-03-01", "bookmark_save() stores a named bookmark for its client, "
                           "and ONLY for that client",
              saved == Result::Ok && dbg.bookmarks(a) == std::vector<std::string>{"one"} &&
                  dbg.bookmarks(b).empty());

        emu.run_frame();                         // the machine moves on...
        emu.mmu().write(0x6000, 0x22);
        const MachineMark moved = mark_of(emu);
        const Result restored = dbg.bookmark_restore(a, "one");
        check("CAP-03-02", "bookmark_restore() puts the machine back where the "
                           "bookmark caught it: clock, PC and RAM",
              !(moved == at_save) && restored == Result::Ok && mark_of(emu) == at_save,
              "now " + show_mark(mark_of(emu)) + " want " + show_mark(at_save));

        const uint64_t gen = emu.state_error_generation();
        const MachineMark before = mark_of(emu);
        check("CAP-03-03", "an unknown name is RefusedUnavailable, and the machine "
                           "is untouched and nothing is latched",
              dbg.bookmark_restore(a, "two") == Result::RefusedUnavailable &&
                  mark_of(emu) == before && emu.state_error_generation() == gen &&
                  emu.last_state_error().empty());

        // PER CLIENT, BOTH WAYS: B cannot reach A's "one", and B's own "one" is
        // a different bookmark.
        const Result b_cannot = dbg.bookmark_restore(b, "one");
        emu.run_frame();
        emu.mmu().write(0x6000, 0x33);
        const MachineMark b_state = mark_of(emu);
        dbg.bookmark_save(b, "one", SaveStateMode::RefuseMidFrame);
        dbg.bookmark_restore(a, "one");
        const bool a_gets_a = mark_of(emu) == at_save;
        dbg.bookmark_restore(b, "one");
        check("CAP-03-04", "names are per client: B cannot restore A's bookmark, and "
                           "B's own of the same name is a different one",
              b_cannot == Result::RefusedUnavailable && a_gets_a && mark_of(emu) == b_state,
              "now " + show_mark(mark_of(emu)) + " want B's " + show_mark(b_state));

        // A re-save of a name REPLACES it, in place.
        emu.mmu().write(0x6000, 0x44);
        const MachineMark second = mark_of(emu);
        dbg.bookmark_save(a, "one", SaveStateMode::RefuseMidFrame);
        emu.mmu().write(0x6000, 0x55);
        dbg.bookmark_restore(a, "one");
        check("CAP-03-05", "saving a name again replaces it — one entry, the newer "
                           "state",
              dbg.bookmarks(a) == std::vector<std::string>{"one"} && mark_of(emu) == second,
              show_mark(mark_of(emu)));
    }
    {
        // THE BOUND: 8 per client, the 9th NEW name refused, nothing evicted, and
        // a replacement still allowed at the bound.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        bool eight_ok = true;
        for (int i = 0; i < 8; ++i)
            eight_ok = eight_ok && dbg.bookmark_save(a, "b" + std::to_string(i),
                                                     SaveStateMode::RefuseMidFrame) == Result::Ok;
        const Result ninth = dbg.bookmark_save(a, "b8", SaveStateMode::RefuseMidFrame);
        const std::vector<std::string> names = dbg.bookmarks(a);
        const Result replace = dbg.bookmark_save(a, "b0", SaveStateMode::RefuseMidFrame);
        check("CAP-03-06", "the bound is 8: the 9th NEW name is RefusedUnavailable, "
                           "the oldest is NOT evicted, and re-saving a held name is "
                           "still allowed at the bound",
              eight_ok && ninth == Result::RefusedUnavailable && names.size() == 8 &&
                  names.front() == "b0" && names.back() == "b7" &&
                  dbg.bookmark_restore(a, "b0") == Result::Ok && replace == Result::Ok &&
                  dbg.bookmarks(a).size() == 8,
              "ninth=" + std::string(jnext::dbg::result_name(ninth)) + " n=" +
                  std::to_string(names.size()));

        // ...and the refusal happens BEFORE the advance: a mid-frame machine at
        // the bound is not run out for a save that is refused anyway.
        RecListener l;
        dbg.set_listener(a, &l);
        stop_mid_frame_at_call(emu, dbg, a);
        const uint64_t clk = emu.clock().get();
        const size_t n0 = mutate_lines(l).size();
        const Result refused = dbg.bookmark_save(a, "b9", SaveStateMode::AdvanceToBoundary);
        check("CAP-03-07", "a save refused for the bound does NOT advance a mid-frame "
                           "machine first (no clock move, no MUTATE line)",
              refused == Result::RefusedUnavailable && emu.clock().get() == clk &&
                  emu.frame_in_progress() && mutate_lines(l).size() == n0);
    }
    {
        // ST-01's rule, through bookmark_save: the same one save_state_bytes uses.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        stop_mid_frame_at_call(emu, dbg, a);
        const Result refused = dbg.bookmark_save(a, "mid", SaveStateMode::RefuseMidFrame);
        const bool nothing = dbg.bookmarks(a).empty() && emu.frame_in_progress();
        const Result advanced = dbg.bookmark_save(a, "mid", SaveStateMode::AdvanceToBoundary);
        const auto lines = mutate_lines(l);
        check("CAP-03-08", "mid-frame: RefuseMidFrame answers NotAtFrameBoundary and "
                           "stores nothing; AdvanceToBoundary runs the frame out, logs "
                           "the MUTATE clock line under bookmark_save, and stores it",
              refused == Result::NotAtFrameBoundary && nothing && advanced == Result::Ok &&
                  !emu.frame_in_progress() &&
                  dbg.bookmarks(a) == std::vector<std::string>{"mid"} && !lines.empty() &&
                  lines.back().find("MUTATE clock (bookmark_save advanced to the frame "
                                    "boundary)") == 0,
              lines.empty() ? std::string("no MUTATE line") : lines.back());
    }
    {
        // A client's bookmarks die with its detach; an id with no client has none.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        dbg.bookmark_save(a, "x", SaveStateMode::RefuseMidFrame);
        const bool had = dbg.bookmarks(a).size() == 1;
        dbg.detach(a);
        const ClientId a2 = dbg.attach(client("A again")).value;
        check("CAP-03-09", "a client's bookmarks die with its detach — gone for its id, "
                           "unrestorable, and not inherited by a new client",
              had && dbg.bookmarks(a).empty() && dbg.bookmarks(a2).empty() &&
                  dbg.bookmark_restore(a, "x") == Result::RefusedUnavailable &&
                  dbg.bookmark_restore(a2, "x") == Result::RefusedUnavailable);
        // And MID-FRAME, where a save would advance: an unknown id is refused
        // BEFORE the advance, so the machine is not run out for a bookmark that
        // has nowhere to go.
        stop_mid_frame_at_call(emu, dbg, a2);
        const uint64_t clk = emu.clock().get();
        const Result mid = dbg.bookmark_save(99, "y", SaveStateMode::AdvanceToBoundary);
        check("CAP-03-10", "an id with no live client — CLIENT_NONE, or one never "
                           "issued — can neither save nor restore, and lists nothing; "
                           "mid-frame its save is refused before it could advance",
              dbg.bookmark_save(jnext::dbg::CLIENT_NONE, "y", SaveStateMode::RefuseMidFrame) ==
                      Result::RefusedUnavailable &&
                  dbg.bookmark_save(99, "y", SaveStateMode::RefuseMidFrame) ==
                      Result::RefusedUnavailable &&
                  dbg.bookmark_restore(99, "y") == Result::RefusedUnavailable &&
                  dbg.bookmarks(99).empty() && mid == Result::RefusedUnavailable &&
                  emu.clock().get() == clk && emu.frame_in_progress());
    }
    {
        // SURVIVE A CTL-12 `Hard` RECONSTRUCT, on every route that lands one, and
        // restore into the rebuilt machine (same type, same width).
        auto run_path = [&](int path) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            auto boot = [&]() {
                emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
            };
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() { boot(); return true; };
            d.load      = [&](const std::string&) { boot(); return true; };
            dbg.set_loop_driver(d);
            emu.mmu().write(0x6000, 0x5A);
            const MachineMark at_save = mark_of(emu);
            dbg.bookmark_save(a, "pre", SaveStateMode::RefuseMidFrame);
            if (path == 0)      dbg.reset(a, ResetKind::Hard);
            else if (path == 1) dbg.load(a, "game.nex");
            else              { dbg.on_cold_boot_begin(); boot(); dbg.on_cold_boot_done(); }
            const bool listed = dbg.bookmarks(a) == std::vector<std::string>{"pre"};
            const bool wiped  = emu.mmu().read(0x6000) != 0x5A;
            const Result r    = dbg.bookmark_restore(a, "pre");
            return listed && wiped && r == Result::Ok && mark_of(emu) == at_save;
        };
        const bool v0 = run_path(0), v1 = run_path(1), v2 = run_path(2);
        check("CAP-03-11", "a bookmark survives every machine-replacing route — "
                           "reset(Hard), a reconstructing load(), the guest path — and "
                           "restores into the rebuilt machine",
              v0 && v1 && v2,
              std::string("reset=") + (v0 ? "1" : "0") + " load=" + (v1 ? "1" : "0") +
                  " guest=" + (v2 ? "1" : "0"));
    }
    {
        // TYPE and WIDTH, refused BEFORE load_state: nothing latched, the machine
        // untouched. Type: the cold boot lands a +3. Width: the same type, but a
        // joystick serial cable attached, which widens the stream
        // (`Emulator::save_state`, "joy_uart").
        const std::string cable = "/tmp/jnext_b4_joy_uart.bin";
        { std::ofstream f(cable, std::ios::binary); f << "ABCD"; }
        // `from` is the machine the bookmark is taken on; the boot lands `to`
        // (or, with no type change, the same type with the cable attached).
        auto run = [&](bool change_type, MachineType from = MachineType::ZX48K,
                       MachineType to = MachineType::ZX_PLUS3) {
            Emulator emu; build(emu, from);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            dbg.bookmark_save(a, "pre", SaveStateMode::RefuseMidFrame);
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() {
                EmulatorConfig c = emu.config();
                if (change_type) c.type = to;
                else             c.joy_uart_rx_file = cable;
                emulator_frontend_cold_boot(emu, c, std::string(), ColdBootHooks{});
                return true;
            };
            dbg.set_loop_driver(d);
            dbg.reset(a, ResetKind::Hard);
            emu.mmu().write(0x6000, 0x77);
            const MachineMark before = mark_of(emu);
            const uint64_t gen = emu.state_error_generation();
            const Result r = dbg.bookmark_restore(a, "pre");
            return r == Result::RefusedUnavailable && mark_of(emu) == before &&
                   emu.state_error_generation() == gen && emu.last_state_error().empty() &&
                   !dbg.resume_blocked_by_corruption();
        };
        check("CAP-03-12", "a bookmark restored into a machine of ANOTHER TYPE is "
                           "RefusedUnavailable before load_state: nothing latched, "
                           "the machine untouched — 48K into a +3, and +3 into a 48K",
              run(true) && run(true, MachineType::ZX_PLUS3, MachineType::ZX48K));
        check("CAP-03-13", "and into the same type with a DIFFERENT SNAPSHOT WIDTH (a "
                           "cable attached) likewise",
              run(false));
        std::remove(cable.c_str());
    }
    {
        // A LISTENER MAY REACH THE CLIENT MID-SAVE: the advance's MUTATE line is
        // pushed to every listener, and one that detaches the saving client there
        // (a crashed remote's server does exactly that from a callback) leaves it
        // with nowhere to keep the bookmark. The client row is looked up again
        // after the advance, so the save is REFUSED rather than written into a
        // tombstone the fan-out has already compacted away.
        struct DetachOnMutate : RecListener {
            Debugger* dbg = nullptr;
            ClientId  who = jnext::dbg::CLIENT_NONE;
            void on_log(jnext::dbg::LogLevel lvl, const std::string& t) override {
                RecListener::on_log(lvl, t);
                if (dbg && t.rfind("MUTATE clock", 0) == 0) { dbg->detach(who); dbg = nullptr; }
            }
        };
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        DetachOnMutate l;
        dbg.set_listener(b, &l);
        stop_mid_frame_at_call(emu, dbg, a);
        l.dbg = &dbg; l.who = a;
        const Result r = dbg.bookmark_save(a, "mid", SaveStateMode::AdvanceToBoundary);
        check("CAP-03-14", "a client detached by a listener DURING its save's advance "
                           "gets RefusedUnavailable, and no bookmark is kept for it",
              l.dbg == nullptr && r == Result::RefusedUnavailable && dbg.bookmarks(a).empty() &&
                  !emu.frame_in_progress(),
              std::string("r=") + jnext::dbg::result_name(r));
    }
    {
        // THE OTHER HAZARD THE SECOND LOOKUP NAMES: a listener reached by the
        // advance's MUTATE line saves a bookmark FOR THE SAME CLIENT, taking it
        // to the bound while the outer save is in flight. The bound is checked
        // again after the advance, so the outer save — a NEW name — is refused
        // and the client never holds more than 8 (M1 coverage review; the
        // mutant without the recheck left it holding 9).
        struct SaveOnMutate : RecListener {
            Debugger* dbg = nullptr;
            ClientId  who = jnext::dbg::CLIENT_NONE;
            Result    nested = Result::Unsupported;
            void on_log(jnext::dbg::LogLevel lvl, const std::string& t) override {
                RecListener::on_log(lvl, t);
                if (dbg && t.rfind("MUTATE clock", 0) == 0) {
                    Debugger* d = dbg;
                    dbg    = nullptr;
                    nested = d->bookmark_save(who, "b7", SaveStateMode::RefuseMidFrame);
                }
            }
        };
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 7; ++i)
            dbg.bookmark_save(a, "b" + std::to_string(i), SaveStateMode::RefuseMidFrame);
        SaveOnMutate l;
        dbg.set_listener(a, &l);
        stop_mid_frame_at_call(emu, dbg, a);
        l.dbg = &dbg; l.who = a;
        const Result outer = dbg.bookmark_save(a, "mid", SaveStateMode::AdvanceToBoundary);
        const std::vector<std::string> names = dbg.bookmarks(a);
        bool has_mid = false;
        for (const std::string& n : names) has_mid = has_mid || n == "mid";
        check("CAP-03-15", "a listener that saves the SAME client's 8th bookmark during the "
                           "outer save's advance takes it to the bound: the outer save (a "
                           "new name) is refused and the client holds exactly 8",
              l.dbg == nullptr && l.nested == Result::Ok && outer == Result::RefusedUnavailable &&
                  names.size() == 8 && !has_mid,
              std::string("nested=") + jnext::dbg::result_name(l.nested) + " outer=" +
                  jnext::dbg::result_name(outer) + " n=" + std::to_string(names.size()));
    }
}

// ── IN-01 — pulses, APPEND; REQ-dsl-20 — the injection ordering ─────────────

/// Is matrix position (row, col) down in the matrix the guest reads?
static bool key_down(Debugger& dbg, int row, int col) {
    return (dbg.input_state().matrix[row] & (1u << col)) == 0;
}

static jnext::dbg::MatrixKey mk(const char* name) {
    jnext::dbg::MatrixKey k;
    key_name_to_matrix(name, k);
    return k;
}

/// The frame (counting run_frame() calls from 1) at whose END `key` first reads
/// down and the one at whose end it first reads up again, over `n` frames.
struct PulseSpan { int down = -1, up = -1; };
static PulseSpan watch_pulse(Emulator& emu, Debugger& dbg, const jnext::dbg::MatrixKey& key,
                             int n) {
    PulseSpan s;
    for (int f = 1; f <= n; ++f) {
        emu.run_frame();
        const bool d = key_down(dbg, key.row1, key.col1);
        if (d && s.down < 0) s.down = f;
        if (!d && s.down >= 0 && s.up < 0) s.up = f;
    }
    return s;
}

static void b4_input_rows() {
    using jnext::dbg::MatrixKey;
    using jnext::dbg::SaveStateMode;
    const MatrixKey A = mk("a");
    const MatrixKey S = mk("s");
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        const auto r = dbg.press_key(a, std::string("a"), 2);
        const bool queued_only = !key_down(dbg, A.row1, A.col1);   // nothing yet
        emu.run_frame();
        const bool down = key_down(dbg, A.row1, A.col1);
        emu.run_frame();
        const bool up = !key_down(dbg, A.row1, A.col1);
        check("IN-01-01", "press_key(name) is a PULSE: queued (value 1), down from the "
                          "next frame edge, up again after its hold",
              r.status == Result::Ok && r.value == 1 && queued_only && down && up);
        const auto lines = mutate_lines(l);
        check("IN-01-02", "and it is logged as a MUTATE line naming the position and "
                          "the hold, attributed to the client",
              !lines.empty() &&
                  lines.back() == "MUTATE key pulse [" + std::to_string(A.row1) + "," +
                                      std::to_string(A.col1) +
                                      "] for 2 frames, queued by " + std::to_string(a),
              lines.empty() ? std::string("none") : lines.back());
    }
    {
        // THE SIBLING PAIR: press_key(name) and press_key(matrix) are one verb
        // over one table — the same key by either form gives the same pulse, to
        // the frame, and the same answer.
        Emulator e1; build(e1);
        Debugger d1(e1);
        const auto r1 = d1.press_key(1, std::string("A"), 3);   // upper case: the table ignores case
        const PulseSpan by_name = watch_pulse(e1, d1, A, 8);
        Emulator e2; build(e2);
        Debugger d2(e2);
        const auto r2 = d2.press_key(1, A, 3);
        const PulseSpan by_pos = watch_pulse(e2, d2, A, 8);
        check("IN-01-10", "press_key by NAME and by MATRIX POSITION are the same pulse: "
                          "same answer, down and up on the same frames",
              r1.status == r2.status && r1.value == r2.value && r1.status == Result::Ok &&
                  by_name.down == by_pos.down && by_name.up == by_pos.up && by_pos.down == 1,
              "name " + std::to_string(by_name.down) + ".." + std::to_string(by_name.up) +
                  " pos " + std::to_string(by_pos.down) + ".." + std::to_string(by_pos.up));
    }
    {
        // By MATRIX position, compound: both bits go down together.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const MatrixKey edit = mk("caps+1");
        const auto r = dbg.press_key(1, edit, 3);
        emu.run_frame();
        check("IN-01-03", "press_key(matrix) with a second key presses BOTH positions",
              r.status == Result::Ok && edit.compound() &&
                  key_down(dbg, edit.row1, edit.col1) && key_down(dbg, edit.row2, edit.col2));
    }
    {
        // Every refusal, and that a refusal queues NOTHING.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const size_t before = emu.keyboard().auto_typing();
        MatrixKey off_row{8, 0, -1, -1}, off_col{0, 5, -1, -1}, half{1, 0, 0, -1},
                  bad_second{1, 0, 9, 0};
        const auto e1 = dbg.press_key(1, std::string("no-such-key"), 5);
        const auto e2 = dbg.press_key(1, off_row, 5);
        const auto e3 = dbg.press_key(1, off_col, 5);
        const auto e4 = dbg.press_key(1, half, 5);
        const auto e5 = dbg.press_key(1, bad_second, 5);
        const auto e6 = dbg.press_key(1, A, 0);
        auto refused = [](const jnext::dbg::Expected<size_t>& e) {
            return e.status == Result::RefusedUnavailable && e.value == 0;
        };
        check("IN-01-04", "an unknown name, a position off the matrix, a half-set "
                          "second key, a bad second key and a hold of 0 frames are all "
                          "RefusedUnavailable with nothing queued",
              refused(e1) && refused(e2) && refused(e3) && refused(e4) && refused(e5) &&
                  refused(e6) && !before && !emu.keyboard().auto_typing());
    }
    {
        // THE CAP: the union of everything queued is MAX_AUTO_TYPE_KEYS (16). With
        // 15 queued a pulse fits (value 1); with 16 it does not (refused, 0 —
        // "RefusedUnavailable AND the count queued").
        auto fill = [](Emulator& emu, size_t n) {
            std::vector<Keyboard::AutoKey> keys(n, Keyboard::AutoKey{1, 0, -1, -1, 5});
            return emu.keyboard().queue_auto_type(keys);
        };
        Emulator e15; build(e15);
        Debugger d15(e15);
        const size_t q15 = fill(e15, 15);
        const auto fits = d15.press_key(1, S, 5);
        Emulator e16; build(e16);
        Debugger d16(e16);
        const size_t q16 = fill(e16, 16);
        const auto full = d16.press_key(1, S, 5);
        check("IN-01-05", "the 16-entry cap covers every producer: at 15 queued a pulse "
                          "fits (Ok, 1); at 16 it is RefusedUnavailable with 0 queued",
              q15 == 15 && fits.status == Result::Ok && fits.value == 1 && q16 == 16 &&
                  full.status == Result::RefusedUnavailable && full.value == 0);
        // And the keyboard truncates what does not fit, reporting the count.
        const size_t more = e16.keyboard().queue_auto_type({Keyboard::AutoKey{1, 1, -1, -1, 5}});
        const size_t part = e15.keyboard().queue_auto_type(
            {Keyboard::AutoKey{1, 1, -1, -1, 5}, Keyboard::AutoKey{1, 2, -1, -1, 5}});
        check("IN-01-06", "queue_auto_type() returns how many it queued: 0 onto a full "
                          "queue, and 0 of 2 onto a queue with no room left",
              more == 0 && part == 0);
    }
    {
        // APPEND — the two §9 rows. (a) Two pulses due in ONE frame both happen,
        // one after the other with the 4-frame released gap between. (b) A pulse
        // issued while another is held does not strand it: the held key is
        // released on schedule — the same frame as with no second pulse at all
        // (its counters were not reset) — and the second follows.
        Emulator e; build(e);
        Debugger d(e);
        d.press_key(1, A, 3);
        d.press_key(1, S, 3);
        const PulseSpan pa = watch_pulse(e, d, A, 20);
        Emulator e2; build(e2);
        Debugger d2(e2);
        d2.press_key(1, A, 3);
        d2.press_key(1, S, 3);
        const PulseSpan ps = watch_pulse(e2, d2, S, 20);
        // A: pressed at edge 1, released at edge 3 (hold 3). The gap is FOUR
        // ticks (edges 4-7; the one that ends it presses nothing) and S is
        // pressed at edge 8 — `tick_auto_type()` as it has always behaved.
        check("IN-01-07", "two pulses queued in one frame BOTH happen, in order, the "
                          "second after the first's release and the 4-frame gap",
              pa.down == 1 && pa.up == 3 && ps.down == 8 && ps.up == 10,
              "a " + std::to_string(pa.down) + ".." + std::to_string(pa.up) + " s " +
                  std::to_string(ps.down) + ".." + std::to_string(ps.up));

        Emulator solo; build(solo);
        Debugger ds(solo);
        ds.press_key(1, A, 5);
        const PulseSpan alone = watch_pulse(solo, ds, A, 12);
        Emulator held; build(held);
        Debugger dh(held);
        dh.press_key(1, A, 5);
        held.run_frame(); held.run_frame();          // A is down, mid-hold
        const bool mid_hold = key_down(dh, A.row1, A.col1);
        dh.press_key(1, S, 2);                       // issued WHILE A is held
        const PulseSpan rest = watch_pulse(held, dh, A, 10);
        check("IN-01-08", "a pulse issued while another is HELD does not strand it or "
                          "restart it: the held key comes up on the frame it would have "
                          "with no second pulse",
              mid_hold && alone.up == 5 && rest.up == alone.up - 2,
              "alone.up=" + std::to_string(alone.up) + " rest.up(+2)=" +
                  std::to_string(rest.up + 2));
    }
    {
        // An IDLE queue: the counters are reset exactly as the replacing
        // version did — a key queued right after another's release is pressed at
        // the next edge, not after a stale gap.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.press_key(1, A, 2);
        emu.run_frame(); emu.run_frame();            // A down, then up: queue empty, gap set
        const bool idle = !emu.keyboard().auto_typing();
        dbg.press_key(1, S, 2);
        emu.run_frame();
        check("IN-01-09", "onto an EMPTY queue the counters reset (the pre-B4 "
                          "behaviour): a pulse right after another's release is down "
                          "at the very next edge",
              idle && key_down(dbg, S.row1, S.col1));
    }

    // ── REQ-dsl-20 ──────────────────────────────────────────────────────────
    {
        // A LEVEL set MID-frame N is invisible to the rest of frame N and visible
        // from N+1. The handler at PROG+1 sets it; a handler at PARK, later IN THE
        // SAME FRAME, looks.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        bool set = false, looked = false, seen_in_frame = true, ext_in_frame = true;
        Subscription s1;
        s1.kind = EventKind::Execute; s1.filter.lo = PROG + 1; s1.filter.hi = PROG + 1;
        s1.action = Action::Continue;
        s1.handler = [&](const DbgEvent&, Debugger& d) {
            if (!set) {
                set = d.set_key(a, A.row1, A.col1, true) == Result::Ok &&
                      d.set_extended_key(a, 3, true) == Result::Ok;
            }
            return Action::Continue;
        };
        Subscription s2 = s1;
        s2.filter.lo = PARK; s2.filter.hi = PARK;
        s2.handler = [&](const DbgEvent&, Debugger& d) {
            if (!looked) {
                looked        = true;
                seen_in_frame = key_down(d, A.row1, A.col1);
                ext_in_frame  = (d.input_state().ext_keys & 0x0008) != 0;
            }
            return Action::Continue;
        };
        dbg.subscribe(a, s1);
        dbg.subscribe(a, s2);
        emu.run_frame();
        check("IN-ORD-01", "a level set MID-frame — a matrix key and an extended key — "
                           "is invisible to the rest of that frame and applied at its edge",
              set && looked && !seen_in_frame && !ext_in_frame &&
                  key_down(dbg, A.row1, A.col1) && (dbg.input_state().ext_keys & 0x0008) != 0);
    }
    {
        // A `Frame` handler at E_N: its PULSE is pressed by E_N's own tick — the
        // same frame `--delayed-keypress-frames N` gives, which queues before
        // run_frame(N) (modelled here by queuing before the Nth run_frame).
        auto via_handler = [&](bool level) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            Subscription s;
            s.kind = EventKind::Frame; s.filter.frame = 2; s.action = Action::Continue;
            s.handler = [&](const DbgEvent&, Debugger& d) {
                if (level) d.set_key(a, A.row1, A.col1, true);
                else       d.press_key(a, A, 5);
                return Action::Continue;
            };
            dbg.subscribe(a, s);
            int first = -1;
            for (int f = 0; f < 6; ++f) {
                emu.run_frame();                           // frame f
                if (first < 0 && key_down(dbg, A.row1, A.col1)) first = f;
            }
            return first;
        };
        auto via_cli = [&]() {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            int first = -1;
            for (int f = 0; f < 6; ++f) {
                if (f == 2) emu.keyboard().queue_auto_type({Keyboard::AutoKey{
                                A.row1, A.col1, -1, -1, 5}});
                emu.run_frame();
                if (first < 0 && key_down(dbg, A.row1, A.col1)) first = f;
            }
            return first;
        };
        const int pulse = via_handler(false), level = via_handler(true), cli = via_cli();
        check("IN-ORD-02", "a pulse from an `on frame N` handler is pressed at E_N — "
                           "down once frame N has run, exactly like "
                           "--delayed-keypress-frames N",
              pulse == 2 && cli == 2,
              "handler=" + std::to_string(pulse) + " cli=" + std::to_string(cli));
        check("IN-ORD-03", "and a LEVEL set from the same handler lands at the same edge",
              level == 2, "level=" + std::to_string(level));
    }
    {
        // A pulse from a remote command BETWEEN frames (a pump, after run_frame(N))
        // is applied at the NEXT edge — the same as the CLI countdown for N+1.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame(); emu.run_frame();                  // frames 0, 1
        dbg.press_key(1, A, 5);                            // between frames 1 and 2
        const bool not_yet = !key_down(dbg, A.row1, A.col1);
        emu.run_frame();                                   // frame 2
        check("IN-ORD-04", "a pulse issued between frames is pressed at the end of "
                           "the next frame, not before it runs",
              not_yet && key_down(dbg, A.row1, A.col1));
    }
    {
        // THE FRAME EDGE IS NOT A SAVE POINT until it has finished: inside a
        // `Frame` handler the edge's tick has not run, so a snapshot there would
        // restore without it. Refused both ways — and the other arm: once
        // run_frame() has returned, the same machine saves.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        bool in_boundary = true;
        Result in_refuse = Result::Ok, in_advance = Result::Ok, in_bm = Result::Ok;
        bool once = false;
        Subscription s;
        s.kind = EventKind::Frame; s.filter.frame = 0; s.action = Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger& d) {
            if (once) return Action::Continue;
            once        = true;
            in_boundary = d.at_frame_boundary();
            in_refuse   = d.save_state_bytes(a, SaveStateMode::RefuseMidFrame).status;
            in_advance  = d.save_state_bytes(a, SaveStateMode::AdvanceToBoundary).status;
            in_bm       = d.bookmark_save(a, "edge", SaveStateMode::RefuseMidFrame);
            return Action::Continue;
        };
        dbg.subscribe(a, s);
        emu.run_frame();
        check("IN-ORD-05", "inside a `Frame` handler the machine is NOT at a frame "
                           "boundary: RefuseMidFrame saves answer NotAtFrameBoundary "
                           "and an advancing save is refused (Unsupported, §5)",
              once && !in_boundary && in_refuse == Result::NotAtFrameBoundary &&
                  in_advance == Result::Unsupported &&
                  in_bm == Result::NotAtFrameBoundary && dbg.bookmarks(a).empty(),
              std::string("refuse=") + jnext::dbg::result_name(in_refuse) +
                  " advance=" + jnext::dbg::result_name(in_advance));
        check("IN-ORD-06", "and once run_frame() has returned the edge is closed: the "
                           "same machine is at a boundary and saves",
              dbg.at_frame_boundary() && !emu.frame_edge_open() &&
                  dbg.save_state_bytes(a, SaveStateMode::RefuseMidFrame).status == Result::Ok);
    }
    {
        // A queued level set is not machine state: a RESTORE drops it (it would
        // have been applied beside the auto-type queue the restore replaced), and
        // so does every route that replaces the machine (a new Keyboard).
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const auto bytes = dbg.save_state_bytes(a, SaveStateMode::RefuseMidFrame).value;
        dbg.set_key(a, A.row1, A.col1, true);
        const size_t pending = emu.keyboard().pending_levels();
        dbg.load_state_bytes(a, bytes.data(), bytes.size());
        const size_t after = emu.keyboard().pending_levels();
        emu.run_frame();
        check("IN-ORD-07", "a restore drops a queued level set: nothing pending, and "
                           "the next edge presses nothing",
              pending == 1 && after == 0 && !key_down(dbg, A.row1, A.col1));

        auto via = [&](int path) {
            Emulator e; build(e);
            Debugger d(e);
            const ClientId c = d.attach(client("C")).value;
            auto boot = [&]() {
                emulator_frontend_cold_boot(e, e.config(), std::string(), ColdBootHooks{});
            };
            jnext::dbg::LoopDriver drv;
            drv.cold_boot = [&]() { boot(); return true; };
            drv.load      = [&](const std::string&) { boot(); return true; };
            d.set_loop_driver(drv);
            d.set_key(c, A.row1, A.col1, true);
            d.press_key(c, S, 5);
            if (path == 0)      d.reset(c, ResetKind::Hard);
            else if (path == 1) d.load(c, "game.nex");
            else              { d.on_cold_boot_begin(); boot(); d.on_cold_boot_done(); }
            load_prog(e, { 0x18, 0xFE });
            e.run_frame();
            return e.keyboard().pending_levels() == 0 && !e.keyboard().auto_typing() &&
                   !key_down(d, A.row1, A.col1) && !key_down(d, S.row1, S.col1);
        };
        const bool r0 = via(0), r1 = via(1), r2 = via(2);
        check("IN-ORD-08", "a queued level set AND a queued pulse die with the machine "
                           "on every route that replaces it — reset(Hard), a "
                           "reconstructing load(), the guest path",
              r0 && r1 && r2,
              std::string("reset=") + (r0 ? "1" : "0") + " load=" + (r1 ? "1" : "0") +
                  " guest=" + (r2 ? "1" : "0"));
    }
}

// ── CAP-04 — save_snapshot ──────────────────────────────────────────────────

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

static void b4_snapshot_rows() {
    const std::string szx = "/tmp/jnext_b4_snap.szx";
    const std::string szx_upper = "/tmp/jnext_b4_snap2.SZX";   // the match ignores case
    const std::string sna = "/tmp/jnext_b4_snap.sna";
    const std::string jns = "/tmp/jnext_b4_snap.jns";
    std::remove(szx.c_str()); std::remove(sna.c_str()); std::remove(jns.c_str());
    std::remove(szx_upper.c_str());
    {
        // At a frame boundary: written at once, by extension, byte for byte what
        // the format's saver produces for this machine.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        // Each expectation is taken IMMEDIATELY before its save, from the same
        // machine: the comparison is "the verb wrote what the saver produces".
        const std::vector<uint8_t> want_szx = SzxSaver::save(emu).data;
        const Result r1 = dbg.save_snapshot(a, szx);
        const Result r1u = dbg.save_snapshot(a, szx_upper);
        const std::vector<uint8_t> want_sna = SnaSaver::save(emu);
        const Result r2 = dbg.save_snapshot(a, sna);
        const Result r3 = dbg.save_snapshot(a, jns);
        const std::vector<uint8_t> f_szx = read_file(szx), f_szxu = read_file(szx_upper),
                                   f_sna = read_file(sna), f_jns = read_file(jns);
        check("CAP-04-01", "save_snapshot() at a frame boundary writes the file at once, "
                           "the format chosen by the extension: .szx is SzxSaver's bytes, "
                           "and so is .SZX (the match ignores case)",
              r1 == Result::Ok && !f_szx.empty() && f_szx == want_szx && r1u == Result::Ok &&
                  f_szxu == want_szx,
              std::to_string(f_szx.size()) + " / " + std::to_string(f_szxu.size()) + " vs " +
                  std::to_string(want_szx.size()) + " bytes");
        check("CAP-04-02", "and any other extension is SnaSaver's 48K .sna form",
              r2 == Result::Ok && f_sna.size() == 49179 && f_sna == want_sna,
              std::to_string(f_sna.size()) + " bytes");
        check("CAP-04-03", "and .jns is the JNS container — a zip (PK\\3\\4), written "
                           "by Emulator::save_jns_file",
              r3 == Result::Ok && f_jns.size() > 64 && f_jns[0] == 'P' && f_jns[1] == 'K' &&
                  f_jns[2] == 3 && f_jns[3] == 4,
              std::to_string(f_jns.size()) + " bytes, " + emu.last_jns_error());
    }
    {
        // Mid-frame: the `--delayed-snapshot` rule — ALWAYS ADVANCE — through the
        // one frame-boundary helper, so the advance is attributed and logged.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        stop_mid_frame_at_call(emu, dbg, a);
        const bool mid = emu.frame_in_progress();
        std::remove(szx.c_str());
        const Result r = dbg.save_snapshot(a, szx);
        const auto lines = mutate_lines(l);
        check("CAP-04-04", "mid-frame it ADVANCES to the boundary (never refuses), logs "
                           "the MUTATE clock line under save_snapshot, then writes",
              mid && r == Result::Ok && !emu.frame_in_progress() && !read_file(szx).empty() &&
                  !lines.empty() &&
                  lines.back().find("MUTATE clock (save_snapshot advanced to the frame "
                                    "boundary)") == 0,
              lines.empty() ? std::string("no MUTATE line") : lines.back());
    }
    {
        // Failures are REFUSALS with the reason logged at error, and write nothing:
        // an unwritable path; a machine the format cannot represent (.sna and
        // .szx of a Next); no path at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        const Result bad_dir = dbg.save_snapshot(a, "/nonexistent-dir/x/y.szx");
        const bool logged = !l.logs.empty() && l.logs.back().first == jnext::dbg::LogLevel::Error;
        // No path is refused BEFORE any saver runs — which is observable, because
        // the .sna saver a nameless path would fall through to pushes PC onto the
        // LIVE stack (F-SNA in the B4 report): the two bytes below SP must not move.
        const uint16_t sp = emu.cpu().get_registers().SP;
        emu.mmu().write(static_cast<uint16_t>(sp - 2), 0xA5);
        emu.mmu().write(static_cast<uint16_t>(sp - 1), 0x5A);
        const Result empty   = dbg.save_snapshot(a, "");
        const bool stack_kept = emu.mmu().read(static_cast<uint16_t>(sp - 2)) == 0xA5 &&
                                emu.mmu().read(static_cast<uint16_t>(sp - 1)) == 0x5A;
        Emulator next; build(next, MachineType::ZXN_ISSUE2);
        Debugger dn(next);
        const ClientId b = dn.attach(client("B")).value;
        std::remove(sna.c_str()); std::remove(szx.c_str());
        const Result next_sna = dn.save_snapshot(b, sna);
        const Result next_szx = dn.save_snapshot(b, szx);
        check("CAP-04-05", "a failed write is RefusedUnavailable, logged at error: an "
                           "unwritable path, no path, and a .sna / .szx of a machine "
                           "those formats cannot hold (nothing is written)",
              bad_dir == Result::RefusedUnavailable && logged &&
                  empty == Result::RefusedUnavailable && stack_kept &&
                  next_sna == Result::RefusedUnavailable &&
                  next_szx == Result::RefusedUnavailable && read_file(sna).empty() &&
                  read_file(szx).empty());
    }
    {
        // Inside a delivery: at a TRUE boundary (a host event on a machine paused
        // between frames) it saves; inside the frame edge it would have to
        // advance, and is refused (§5) — nothing written.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        Result at_host = Result::Unsupported, at_edge = Result::Ok;
        bool once = false;
        Subscription h;
        h.kind = EventKind::Host; h.action = Action::Continue;
        std::strcpy(h.filter.host_name, "snap");
        h.handler = [&](const DbgEvent&, Debugger& d) {
            at_host = d.save_snapshot(a, szx);
            return Action::Continue;
        };
        dbg.subscribe(a, h);
        Subscription f;
        f.kind = EventKind::Frame; f.filter.frame = 0; f.action = Action::Continue;
        f.handler = [&](const DbgEvent&, Debugger& d) {
            if (!once) { once = true; at_edge = d.save_snapshot(a, sna); }
            return Action::Continue;
        };
        dbg.subscribe(a, f);
        std::remove(szx.c_str()); std::remove(sna.c_str());
        emu.run_frame();                  // the Frame handler, inside the edge
        dbg.pause(a);
        dbg.raise_host_event(a, "snap");  // a handler at a true boundary
        check("CAP-04-06", "from a handler: saved at a true frame boundary, refused "
                           "(Unsupported, nothing written) inside the frame edge, where "
                           "it would have to advance",
              once && at_edge == Result::Unsupported && read_file(sna).empty() &&
                  at_host == Result::Ok && !read_file(szx).empty(),
              std::string("edge=") + jnext::dbg::result_name(at_edge) +
                  " host=" + jnext::dbg::result_name(at_host));
    }
    {
        // F-SNA — a 48K `.sna` PUSHES PC onto the live stack, and the backend
        // ATTRIBUTES it: one MUTATE mem line, old -> new, by the client. The other
        // arms: a save that writes nothing there (.szx; a 128K .sna, which carries
        // PC in its header) logs no mem line, and neither does a second .sna whose
        // push finds the value already in place.
        auto mem_lines = [](const RecListener& l) {
            std::vector<std::string> out;
            for (const std::string& m : mutate_lines(l))
                if (m.rfind("MUTATE mem ", 0) == 0) out.push_back(m);
            return out;
        };
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        emu.mmu().write(TEST_SP - 2, 0xA5);
        emu.mmu().write(TEST_SP - 1, 0x5A);
        const Result r_szx = dbg.save_snapshot(a, szx);
        const size_t after_szx = mem_lines(l).size();
        const Result r_sna = dbg.save_snapshot(a, sna);
        const auto lines = mem_lines(l);
        char want[128];
        std::snprintf(want, sizeof(want),
                      "MUTATE mem cpu:0x%04X..0x%04X (the .sna saver's PC push) 0x5AA5 -> "
                      "0x%X by %u",
                      TEST_SP - 2, TEST_SP - 1, PROG, static_cast<unsigned>(a));
        const bool pushed = emu.mmu().read(TEST_SP - 2) == (PROG & 0xFF) &&
                            emu.mmu().read(TEST_SP - 1) == (PROG >> 8);
        const Result r_again = dbg.save_snapshot(a, sna);
        const size_t after_again = mem_lines(l).size();

        Emulator e128; build(e128, MachineType::ZX128K);
        Debugger d128(e128);
        const ClientId b = d128.attach(client("B")).value;
        RecListener l128;
        d128.set_listener(b, &l128);
        e128.mmu().write(TEST_SP - 2, 0xA5);
        const Result r_128 = d128.save_snapshot(b, sna);
        check("CAP-04-07", "a 48K .sna save's push of PC onto the live stack is logged as ONE "
                           "MUTATE mem line by the client; a .szx, a 128K .sna and a push of "
                           "the value already there log none",
              r_szx == Result::Ok && after_szx == 0 && r_sna == Result::Ok && pushed &&
                  lines.size() == 1 && lines[0] == want && r_again == Result::Ok &&
                  after_again == 1 && r_128 == Result::Ok && mem_lines(l128).empty(),
              lines.empty() ? std::string("no mem line") : lines[0]);
    }
    std::remove(szx.c_str()); std::remove(sna.c_str()); std::remove(jns.c_str());
    std::remove(szx_upper.c_str());
}

// ── CAP-01 — deferred screenshots ───────────────────────────────────────────

static bool is_png_640x512(const std::vector<uint8_t>& f) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (f.size() < 24 || std::memcmp(f.data(), sig, 8) != 0) return false;
    auto be32 = [&](size_t o) {
        return (uint32_t(f[o]) << 24) | (uint32_t(f[o + 1]) << 16) |
               (uint32_t(f[o + 2]) << 8) | uint32_t(f[o + 3]);
    };
    return be32(16) == 640 && be32(20) == 512;   // IHDR width, height
}

static void b4_screenshot_rows() {
    using jnext::dbg::ScreenshotFormat;
    using jnext::dbg::LAYER_MASK_ALL;
    using jnext::dbg::LAYER_MASK_ULA;
    using jnext::dbg::LAYER_MASK_SPRITES;
    const std::string png  = "/tmp/jnext_b4_shot.png";
    const std::string png2 = "/tmp/jnext_b4_shot2.png";
    const std::string scr  = "/tmp/jnext_b4_shot.scr";
    auto rm = [&]() { std::remove(png.c_str()); std::remove(png2.c_str()); std::remove(scr.c_str()); };
    rm();
    {
        // DEFERRED: queued, not written; written by the first pump after a frame
        // has been RENDERED — and not before, even across pumps.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const Result r = dbg.screenshot(a, png, LAYER_MASK_ALL, ScreenshotFormat::Png);
        dbg.pump(jnext::dbg::PumpBudget{});
        const bool not_yet = read_file(png).empty();
        emu.run_frame();
        const bool not_by_the_frame = read_file(png).empty();   // the WRITE is the pump's
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CAP-01-01", "screenshot() QUEUES (Ok); the PNG is written by the first "
                           "pump after a frame was rendered — not by a pump before one, "
                           "and not by the frame itself",
              r == Result::Ok && not_yet && not_by_the_frame &&
                  is_png_640x512(read_file(png)) && !emu.debug_state().capture_render());
    }
    rm();
    {
        // NEVER THE STALE FRAMEBUFFER: paused, nothing renders, so nothing is
        // written however often the loop owner pumps — and it says so ONCE.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        dbg.pause(a);
        dbg.screenshot(a, png, LAYER_MASK_ALL, ScreenshotFormat::Png);
        size_t warns = 0;
        for (int i = 0; i < 3; ++i) dbg.pump(jnext::dbg::PumpBudget{});
        for (const auto& e : l.logs)
            if (e.first == jnext::dbg::LogLevel::Warn &&
                e.second.find("deferred: the machine is paused") != std::string::npos)
                ++warns;
        const bool held = read_file(png).empty();
        dbg.run(a);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CAP-01-02", "while the machine is paused no frame renders, so the "
                           "capture is HELD (one warning, however many pumps) and taken "
                           "at the first frame after it resumes",
              held && warns == 1 && is_png_640x512(read_file(png)),
              "warns=" + std::to_string(warns));
    }
    rm();
    {
        // SCR: the same deferral; the file is the ULA's .SCR body at the frame.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.mmu().write(0x4000, 0x81);
        dbg.screenshot(1, scr, LAYER_MASK_ALL, ScreenshotFormat::Scr);
        dbg.pump(jnext::dbg::PumpBudget{});
        const bool not_yet = read_file(scr).empty();
        emu.run_frame();
        const std::vector<uint8_t> dump = dbg.ula_screen_dump();
        dbg.pump(jnext::dbg::PumpBudget{});
        const std::vector<uint8_t> f = read_file(scr);
        check("CAP-01-03", "a .SCR capture is deferred the same way and writes the "
                           "ULA's screen memory (6912 bytes) as the frame left it",
              not_yet && f.size() == 6912 && f == dump && f[0] == 0x81);
    }
    rm();
    {
        // THE LAYER MASK is armed for the frame the capture takes and taken down
        // after it; and the render is FORCED where the frontend's hint said skip.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.set_render_enabled(false);                 // Qt's "nobody will look" hint
        const uint64_t r0 = emu.rendered_frames();
        emu.run_frame();
        const bool skipped = emu.rendered_frames() == r0;   // the control
        dbg.screenshot(1, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
        const bool armed = emu.renderer().layer_mask() == LAYER_MASK_ULA &&
                           emu.debug_state().capture_render();
        const uint64_t r1 = emu.rendered_frames();
        emu.run_frame();
        const bool forced = emu.rendered_frames() == r1 + 1;
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CAP-01-04", "the capture's layer mask is armed while it waits and taken "
                           "down after, and its frame is RENDERED even under a "
                           "render-skip hint (without a capture the same frame is not)",
              skipped && armed && forced && is_png_640x512(read_file(png)) &&
                  emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL &&
                  !emu.debug_state().capture_render());
    }
    rm();
    {
        // The refusals, and that a refusal queues nothing.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const Result masked_scr = dbg.screenshot(1, scr, LAYER_MASK_ULA, ScreenshotFormat::Scr);
        const Result none       = dbg.screenshot(1, png, 0, ScreenshotFormat::Png);
        const Result bad_bit    = dbg.screenshot(1, png, 0x10, ScreenshotFormat::Png);
        const Result no_path    = dbg.screenshot(1, "", LAYER_MASK_ALL, ScreenshotFormat::Png);
        check("CAP-01-05", "a masked .SCR, no layer, an undefined layer bit are "
                           "Unsupported; no path is RefusedUnavailable; none queues "
                           "anything",
              masked_scr == Result::Unsupported && none == Result::Unsupported &&
                  bad_bit == Result::Unsupported && no_path == Result::RefusedUnavailable &&
                  !emu.debug_state().capture_render() &&
                  emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL);
    }
    rm();
    {
        // TWO CAPTURES: the same mask → both from ONE frame (one pump); different
        // masks → the second waits for a frame rendered with ITS mask.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.screenshot(1, png, LAYER_MASK_ALL, ScreenshotFormat::Png);
        dbg.screenshot(1, scr, LAYER_MASK_ALL, ScreenshotFormat::Scr);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        const bool both = is_png_640x512(read_file(png)) && read_file(scr).size() == 6912;
        rm();
        dbg.screenshot(1, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
        dbg.screenshot(1, png2, LAYER_MASK_SPRITES, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        const bool first_only = is_png_640x512(read_file(png)) && read_file(png2).empty() &&
                                emu.renderer().layer_mask() == LAYER_MASK_SPRITES;
        dbg.pump(jnext::dbg::PumpBudget{});
        const bool still_waits = read_file(png2).empty();   // no frame with ITS mask yet
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CAP-01-06", "two captures with the same mask are taken from the same "
                           "frame; with different masks the second waits for a frame "
                           "rendered with its own",
              both && first_only && still_waits && is_png_640x512(read_file(png2)) &&
                  emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL);
    }
    rm();
    {
        // A failed write is reported at error and NOT retried: the frame it was
        // for is gone, and the mask comes down.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        dbg.screenshot(a, "/nonexistent-dir/x/y.png", LAYER_MASK_ULA, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        bool error_line = false;
        for (const auto& e : l.logs)
            if (e.first == jnext::dbg::LogLevel::Error &&
                e.second.find("NOT written") != std::string::npos)
                error_line = true;
        check("CAP-01-07", "a capture whose write fails is logged at error, dropped "
                           "(not retried) and its mask taken down",
              error_line && !emu.debug_state().capture_render() &&
                  emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL);
    }
    rm();
    {
        // FROM A HANDLER mid-frame: allowed (it only queues), and the frame that
        // ends after it — this one — is the one captured.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        Result from_handler = Result::Unsupported;
        bool once = false;
        Subscription s;
        s.kind = EventKind::Execute; s.filter.lo = PROG + 1; s.filter.hi = PROG + 1;
        s.action = Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger& d) {
            if (!once) { once = true; from_handler = d.screenshot(a, png, LAYER_MASK_ALL,
                                                                   ScreenshotFormat::Png); }
            return Action::Continue;
        };
        dbg.subscribe(a, s);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CAP-01-08", "a handler may ask for a screenshot mid-frame; the frame "
                           "that ends after it is the one captured",
              once && from_handler == Result::Ok && is_png_640x512(read_file(png)));
    }
    rm();
    {
        // A capture queued on a paused machine SURVIVES every machine-replacing
        // route: the rebuilt machine gets the mask and the force-render bit
        // re-armed, and the capture is the rebuilt machine's first rendered frame.
        auto via = [&](int path) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            auto boot = [&]() {
                emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
            };
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() { boot(); return true; };
            d.load      = [&](const std::string&) { boot(); return true; };
            dbg.set_loop_driver(d);
            for (int i = 0; i < 3; ++i) emu.run_frame();   // the old machine's counter moves
            dbg.pause(a);
            dbg.screenshot(a, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
            if (path == 0)      dbg.reset(a, ResetKind::Hard);
            else if (path == 1) dbg.load(a, "game.nex");
            else              { dbg.on_cold_boot_begin(); boot(); dbg.on_cold_boot_done(); }
            const bool rearmed = emu.renderer().layer_mask() == LAYER_MASK_ULA &&
                                 emu.debug_state().capture_render();
            load_prog(emu, { 0x18, 0xFE });
            dbg.run(a);
            emu.run_frame();
            dbg.pump(jnext::dbg::PumpBudget{});
            const bool ok = rearmed && is_png_640x512(read_file(png)) &&
                            emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL;
            std::remove(png.c_str());
            return ok;
        };
        const bool v0 = via(0), v1 = via(1), v2 = via(2);
        check("CAP-01-09", "a queued capture survives reset(Hard), a reconstructing "
                           "load() and the guest path — mask and force-render re-armed "
                           "on the rebuilt machine, taken at its first rendered frame",
              v0 && v1 && v2,
              std::string("reset=") + (v0 ? "1" : "0") + " load=" + (v1 ? "1" : "0") +
                  " guest=" + (v2 ? "1" : "0"));
    }
    rm();
    {
        // The capture is the FRAME's, not the pump's: taken before any command of
        // the same pump runs. A service whose command pokes screen memory must not
        // change the `.SCR` of a frame that has already ended.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        emu.mmu().write(0x4000, 0x11);
        FakeService svc;
        svc.on_call = [&]() { dbg.poke(a, MemSpace::cpu(), 0x4000, 1,
                                       std::vector<uint8_t>{0x22}.data()); };
        dbg.add_service(svc);
        dbg.screenshot(a, scr, LAYER_MASK_ALL, ScreenshotFormat::Scr);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        const std::vector<uint8_t> f = read_file(scr);
        check("CAP-01-10", "the capture is taken BEFORE the pump's commands run: a poke "
                           "in the same pump does not reach the .SCR of the frame",
              !svc.calls.empty() && f.size() == 6912 && f[0] == 0x11 &&
                  emu.mmu().read(0x4000) == 0x22,
              f.empty() ? std::string("no file") : "scr[0]=" + hex(f[0]));
        dbg.remove_service(svc);
    }
    rm();
    {
        // SOMETHING ELSE RE-SETS THE RENDERER'S MASK while a capture waits (a
        // frontend's own layer knob): the frame rendered with the wrong layers is
        // not taken, the capture's mask is armed again, and the next frame is.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.screenshot(1, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
        emu.renderer().set_layer_mask(LAYER_MASK_SPRITES);     // not the backend's doing
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        const bool skipped = read_file(png).empty() &&
                             emu.renderer().layer_mask() == LAYER_MASK_ULA;
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CAP-01-11", "a frame rendered with another mask (someone else re-set it) "
                           "is not captured; the capture's mask is re-armed and the next "
                           "frame is",
              skipped && is_png_640x512(read_file(png)) &&
                  emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL);
    }
    rm();
    {
        // flush_captures() — THE EXIT BOUND (owner decision O1). Every arm.
        using jnext::dbg::PumpBudget;
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);
        // (a) pending → NoFrame, DROPPED: never written afterwards, logged at
        // error, and the head it was takes its mask and force-render bit down.
        dbg.screenshot(a, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
        const Result pending = dbg.flush_captures(a);
        const bool down = emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL &&
                          !emu.debug_state().capture_render();
        emu.run_frame();
        dbg.pump(PumpBudget{});
        bool logged = false;
        for (const auto& e : l.logs)
            if (e.first == jnext::dbg::LogLevel::Error &&
                e.second.find("never taken: flushed") != std::string::npos)
                logged = true;
        check("CAP-01-12", "flush_captures() with a capture still pending answers NoFrame, "
                           "DROPS it (no file after a later frame), logs it at error, and "
                           "takes its mask and force-render bit down",
              pending == Result::NoFrame && down && read_file(png).empty() && logged);
        // (b) taken and written → Ok, and Ok again.
        dbg.screenshot(a, png, LAYER_MASK_ALL, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(PumpBudget{});
        const Result written = dbg.flush_captures(a);
        const Result again   = dbg.flush_captures(a);
        check("CAP-01-13", "a capture that was written flushes Ok, and so does a second "
                           "flush with nothing queued",
              is_png_640x512(read_file(png)) && written == Result::Ok && again == Result::Ok);
        // (c) failed → RefusedUnavailable, and the record is CONSUMED.
        dbg.screenshot(a, "/nonexistent-dir/x/y.png", LAYER_MASK_ALL, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(PumpBudget{});
        const Result failed = dbg.flush_captures(a);
        const Result after  = dbg.flush_captures(a);
        // (d) a failure AND a pending one → NoFrame first, and both are spent.
        dbg.screenshot(a, "/nonexistent-dir/x/z.png", LAYER_MASK_ALL, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(PumpBudget{});
        dbg.screenshot(a, png2, LAYER_MASK_ALL, ScreenshotFormat::Png);
        const Result both = dbg.flush_captures(a);
        const Result spent = dbg.flush_captures(a);
        check("CAP-01-14", "a capture that failed to write flushes RefusedUnavailable, once "
                           "(the next flush is Ok); a failure and a pending capture together "
                           "answer NoFrame, and both are spent",
              failed == Result::RefusedUnavailable && after == Result::Ok &&
                  both == Result::NoFrame && spent == Result::Ok,
              std::string("failed=") + jnext::dbg::result_name(failed) + " both=" +
                  jnext::dbg::result_name(both) + " spent=" + jnext::dbg::result_name(spent));
    }
    rm();
    {
        // PER CLIENT: A's head with B's capture behind it. Flushing B drops only
        // B's (A's mask stays armed — A is still the head). Then B queues again
        // behind A; flushing A drops only A's, B becomes the head, its mask armed,
        // and is taken at the next frame. A failure of A's is not reported to B.
        using jnext::dbg::PumpBudget;
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        dbg.screenshot(a, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
        dbg.screenshot(b, png2, LAYER_MASK_SPRITES, ScreenshotFormat::Png);
        const Result fb = dbg.flush_captures(b);
        const bool a_head_kept = emu.renderer().layer_mask() == LAYER_MASK_ULA;
        dbg.screenshot(b, png2, LAYER_MASK_SPRITES, ScreenshotFormat::Png);
        const Result fa = dbg.flush_captures(a);
        const bool b_is_head = emu.renderer().layer_mask() == LAYER_MASK_SPRITES &&
                               emu.debug_state().capture_render();
        emu.run_frame();
        dbg.pump(PumpBudget{});
        const bool b_taken = is_png_640x512(read_file(png2)) && read_file(png).empty();
        dbg.screenshot(a, "/nonexistent-dir/x/y.png", LAYER_MASK_ALL, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(PumpBudget{});
        const Result b_sees = dbg.flush_captures(b);
        const Result a_sees = dbg.flush_captures(a);
        check("CAP-01-15", "flush_captures() is per client: B's flush drops only B's capture "
                           "(A's head and mask untouched); A's flush hands the head to B, "
                           "whose mask is armed and whose capture is taken; A's failure is "
                           "reported to A and not to B",
              fb == Result::NoFrame && a_head_kept && fa == Result::NoFrame && b_is_head &&
                  b_taken && b_sees == Result::Ok && a_sees == Result::RefusedUnavailable,
              std::string("fb=") + jnext::dbg::result_name(fb) + " fa=" +
                  jnext::dbg::result_name(fa) + " b_sees=" + jnext::dbg::result_name(b_sees) +
                  " a_sees=" + jnext::dbg::result_name(a_sees));
    }
    rm();
    {
        // A QUEUED CAPTURE IS NOT DROPPED BY ITS REQUESTER'S DETACH (a decision,
        // B4 report §2 — the contract review found it unpinned). A capture is a
        // request about the MACHINE's next rendered frame, not about the client's
        // session: A queues, A detaches before any frame renders, the frame
        // renders, the pump writes it. And A's scope in flush_captures() is keyed
        // by the id, so it still answers for A after the detach: Ok for the
        // capture that was written. The mirror of CAP-03-09 (bookmarks DO die
        // with a detach) and CAP-01-09 (captures survive a rebuild).
        //
        // GH #276 B5 AMENDED THE FAILURE HALF. This row used to expect
        // RefusedUnavailable for a capture that failed AFTER its requester
        // detached — i.e. a failure record kept for a client that can never ask
        // for it again, which B4's review found growing with every session
        // (client ids are never reused). The CAPTURE still survives the detach
        // and is still attempted and logged at error; the per-client RECORD of
        // its outcome dies with the client, like its bookmarks. DETACH-01/02.
        using jnext::dbg::PumpBudget;
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        dbg.screenshot(a, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
        dbg.detach(a);
        const bool still_armed = emu.renderer().layer_mask() == LAYER_MASK_ULA &&
                                 emu.debug_state().capture_render();
        emu.run_frame();
        dbg.pump(PumpBudget{});
        const bool written = is_png_640x512(read_file(png));
        const Result ok_after = dbg.flush_captures(a);
        const ClientId b = dbg.attach(client("B")).value;
        dbg.screenshot(b, "/nonexistent-dir/x/y.png", LAYER_MASK_ALL, ScreenshotFormat::Png);
        dbg.detach(b);
        emu.run_frame();
        dbg.pump(PumpBudget{});
        const Result failed_after = dbg.flush_captures(b);
        check("CAP-01-17", "a capture survives its requester's detach — mask still armed, "
                           "taken at the next rendered frame — and flush_captures() still "
                           "answers for the detached id: Ok when written, and Ok when the "
                           "write failed too, because no record is kept for a client "
                           "that is gone (GH #276 B5)",
              still_armed && written && ok_after == Result::Ok &&
                  failed_after == Result::Ok,
              std::string("armed=") + (still_armed ? "1" : "0") + " written=" +
                  (written ? "1" : "0") + " ok_after=" + jnext::dbg::result_name(ok_after) +
                  " failed_after=" + jnext::dbg::result_name(failed_after));
    }
    rm();
    {
        // §5, DECIDED: allowed from inside a delivery — it executes nothing. A
        // handler's flush answers exactly as one from outside would, and drops.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        Result inside = Result::Ok;
        bool once = false;
        Subscription s;
        s.kind = EventKind::Execute; s.filter.lo = PROG + 1; s.filter.hi = PROG + 1;
        s.action = Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger& d) {
            if (!once) { once = true; inside = d.flush_captures(a); }
            return Action::Continue;
        };
        dbg.subscribe(a, s);
        dbg.screenshot(a, png, LAYER_MASK_ALL, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CAP-01-16", "flush_captures() is allowed from inside a delivery: a handler's "
                           "flush of a pending capture answers NoFrame and drops it",
              once && inside == Result::NoFrame && read_file(png).empty(),
              std::string("inside=") + jnext::dbg::result_name(inside));
    }
    rm();
    {
        // THE PAIR, CAP-01's half: the machine outlives the backend, and must not
        // be left rendering with a dead capture's layers.
        Emulator emu; build(emu);
        bool armed = false;
        {
            Debugger dbg(emu);
            dbg.screenshot(1, png, LAYER_MASK_ULA, ScreenshotFormat::Png);
            armed = emu.renderer().layer_mask() == LAYER_MASK_ULA &&
                    emu.debug_state().capture_render();
        }
        check("LIFE-08", "~Debugger() takes down a pending capture's layer mask and "
                         "force-render bit (and writes nothing)",
              armed && emu.renderer().layer_mask() == jnext::dbg::LAYER_MASK_ALL &&
                  !emu.debug_state().capture_render() && read_file(png).empty());
    }
    rm();
}

// ── M2 part 1 — the HOSTED backend (the B4 plan's §6.1 / §6.4) ─────────────

/// Every hot-path gate the hosted backend could switch on, in one read. A loop
/// owner's `Debugger` with NO client must leave all of them off — that is the
/// whole of "holding a Debugger costs the hot path nothing" (§6.4).
struct Gates {
    bool armed, attached, raster_live, capture_render, events_pending, exec_armed,
         nr_armed, port_armed, copper, dma, coverage;
    uint8_t rd, wr;
    bool all_off() const {
        return !armed && !attached && !raster_live && !capture_render && !events_pending &&
               !exec_armed && !nr_armed && !port_armed && !copper && !dma && !coverage &&
               rd == 0 && wr == 0;
    }
    std::string show() const {
        auto b = [](bool v) { return v ? "1" : "0"; };
        return std::string("armed=") + b(armed) + " att=" + b(attached) + " raster=" +
               b(raster_live) + " cap=" + b(capture_render) + " pend=" + b(events_pending) +
               " exec=" + b(exec_armed) + " nr=" + b(nr_armed) + " port=" + b(port_armed) +
               " cop=" + b(copper) + " dma=" + b(dma) + " cov=" + b(coverage) +
               " rd=" + std::to_string(rd) + " wr=" + std::to_string(wr);
    }
};
static Gates gates_of(Emulator& emu) {
    const DebugState& ds = emu.debug_state();
    return Gates{ds.armed(), ds.attached(), ds.raster_live(), ds.capture_render(),
                 ds.events_pending(), ds.execute_events_armed(), ds.nextreg_events_armed(),
                 ds.port_watch_armed(), emu.copper().events_armed(), emu.dma().events_armed(),
                 ds.coverage_sink() != nullptr, ds.rd_watch_mask(), ds.wr_watch_mask()};
}

/// A program that touches what a stray gate would: ports (the border), memory
/// (a guest write the MMU sites see) and the clock.
///   8000  3E 00      LD A,0
///   8002  D3 FE      OUT (0xFE),A
///   8004  32 00 60   LD (0x6000),A
///   8007  3C         INC A
///   8008  18 F8      JR 0x8002
static void load_busy(Emulator& emu) {
    load_prog(emu, { 0x3E, 0x00, 0xD3, 0xFE, 0x32, 0x00, 0x60, 0x3C, 0x18, 0xF8 });
}

/// The machine as a fingerprint: clock, frame, every register field, the border
/// and a hash of the 48K address space.
static std::string fingerprint(Emulator& emu) {
    const Z80Registers r = emu.cpu().get_registers();
    uint64_t h = 1469598103934665603ull;
    for (uint32_t a = 0x4000; a < 0x10000; ++a) {
        h ^= emu.mmu().read(static_cast<uint16_t>(a));
        h *= 1099511628211ull;
    }
    return std::to_string(emu.clock().get()) + "/" + std::to_string(emu.frame_num()) + "/" +
           reg_delta(Z80Registers{}, r) + "/" + hex(r.PC) + "/" + hex(r.AF) + "/" +
           std::to_string(emu.ula().get_border()) + "/" + std::to_string(h);
}

static void b4_hosting_rows() {
    {
        // THE INVARIANT (§6.4), on the backend alone: constructed, driver and
        // stop policy registered, pumped every frame, through a guest cold boot
        // and its begin/done — and with no client, NOTHING is on.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
            return true;
        };
        d.load = [](const std::string&) { return true; };
        dbg.set_loop_driver(d);
        dbg.set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
        load_busy(emu);
        const Gates g0 = gates_of(emu);
        for (int i = 0; i < 3; ++i) { emu.run_frame(); dbg.pump(jnext::dbg::PumpBudget{}); }
        const Gates g1 = gates_of(emu);
        dbg.on_cold_boot_begin();
        emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
        dbg.on_cold_boot_done();
        load_busy(emu);
        for (int i = 0; i < 3; ++i) { emu.run_frame(); dbg.pump(jnext::dbg::PumpBudget{}); }
        const Gates g2 = gates_of(emu);
        check("HOST-01", "a hosted backend with NO client arms nothing — armed, attached, "
                         "raster_live, capture_render, every event gate and the coverage "
                         "sink stay off, before and after frames, pumps and a guest cold "
                         "boot",
              g0.all_off() && g1.all_off() && g2.all_off(),
              "start[" + g0.show() + "] frames[" + g1.show() + "] boot[" + g2.show() + "]");
    }
    {
        // ZERO BEHAVIOUR CHANGE: the same program, the same frames, the same guest
        // cold boot — with a hosted backend pumped every frame and without one.
        // Identical to the last bit: clock, frame, registers, border, RAM.
        auto run = [](bool hosted) {
            Emulator emu; build(emu);
            std::unique_ptr<Debugger> dbg;
            if (hosted) {
                dbg = std::make_unique<Debugger>(emu);
                dbg->set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
            }
            load_busy(emu);
            for (int i = 0; i < 4; ++i) {
                emu.run_frame();
                if (dbg) dbg->pump(jnext::dbg::PumpBudget{});
            }
            const std::string before_boot = fingerprint(emu);
            if (dbg) dbg->on_cold_boot_begin();
            emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
            if (dbg) dbg->on_cold_boot_done();
            load_busy(emu);
            for (int i = 0; i < 4; ++i) {
                emu.run_frame();
                if (dbg) dbg->pump(jnext::dbg::PumpBudget{});
            }
            return before_boot + " | " + fingerprint(emu);
        };
        const std::string bare = run(false), hosted = run(true);
        check("HOST-02", "a machine run with a hosted, pumped backend is bit-identical to "
                         "the same run without one — clock, frame, registers, border, "
                         "RAM — across a guest cold boot",
              bare == hosted, "bare=" + bare + " hosted=" + hosted);
    }

    // ── THROUGH `HeadlessApp` — the call sites themselves ─────────────────
    //
    // B3 left the loop owner's begin/done calls unpinned ("no suite reaches
    // HeadlessApp"). These rows drive the real `HeadlessApp` (48K, no ROM, no
    // SD image), with `--delayed-automatic-exit` as the loop bound, and observe
    // each call site through the backend it hosts.
    auto headless = [](HeadlessApp& app, int exit_frames) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        app.set_config(cfg);
        const bool ok = app.init(0, nullptr);
        app.set_delayed_exit(exit_frames);
        return ok;
    };
    {
        HeadlessApp app;
        const bool ok = headless(app, 3);
        const Gates before = gates_of(app.emulator());
        app.run();
        const Gates after = gates_of(app.emulator());
        const uint32_t frames = app.emulator().frame_num();
        // The driver is registered by init(): a client's reset(Hard) boots the
        // machine (the frame counter restarts) and is not RefusedUnavailable.
        const Result r = app.debugger().reset(jnext::dbg::CLIENT_NONE, ResetKind::Hard);
        const uint32_t after_reset = app.emulator().frame_num();
        app.shutdown();
        check("HOST-03", "HeadlessApp hosts a backend from init(): with no client every "
                         "gate stays off across run(), and its loop driver is registered "
                         "(a reset(Hard) boots the machine)",
              ok && before.all_off() && after.all_off() && frames > 0 && r == Result::Ok &&
                  after_reset == 0,
              "gates[" + after.show() + "] frames=" + std::to_string(frames) +
                  " reset=" + jnext::dbg::result_name(r) + " frame_after=" +
                  std::to_string(after_reset));
    }
    {
        // pump() IS CALLED, after the frame batch: a capture queued before run()
        // is written by the loop's own pump once a frame has been rendered.
        const std::string png = "/tmp/jnext_b4_host.png";
        std::remove(png.c_str());
        HeadlessApp app;
        const bool ok = headless(app, 3);
        const Result q = app.debugger().screenshot(jnext::dbg::CLIENT_NONE, png,
                                                   jnext::dbg::LAYER_MASK_ALL,
                                                   jnext::dbg::ScreenshotFormat::Png);
        app.run();
        app.shutdown();
        check("HOST-04", "HeadlessApp pumps its backend every tick after the frames: a "
                         "capture queued before run() is written by run()",
              ok && q == Result::Ok && is_png_640x512(read_file(png)));
        std::remove(png.c_str());
    }
    {
        // on_cold_boot_begin() AND on_cold_boot_done() bracket the guest boot.
        // A client's pause is in force when the guest's hard reset is polled:
        // begin captures it with its owner, done re-applies it — so after run()
        // the rebuilt machine is paused BY THAT CLIENT and the backend's
        // publications are on the new DebugState. Without begin the pause is
        // lost (a fresh machine runs); without done the table is not published.
        HeadlessApp app;
        const bool ok = headless(app, 3);
        Debugger& dbg = app.debugger();
        const ClientId a = dbg.attach(client("A")).value;
        dbg.pause(a);
        app.emulator().request_hard_reset();
        app.run();
        const RunState st = dbg.state();
        const bool published = app.emulator().debug_state().event_table() != nullptr;
        app.shutdown();
        check("HOST-05", "HeadlessApp brackets a guest cold boot with begin/done: a "
                         "client's pause survives it, still owned by that client, and "
                         "the backend is re-published on the rebuilt machine",
              ok && st.paused && st.pause_reason.kind == PauseReason::Kind::User &&
                  st.pause_reason.by == a && published,
              std::string("paused=") + (st.paused ? "1" : "0") + " by=" +
                  std::to_string(st.pause_reason.by) + " published=" +
                  (published ? "1" : "0"));
    }
    {
        // --delayed-screenshot DUE WHILE PAUSED (M2 part 2). `--magic-breakpoint`
        // stops the machine on its first instruction (`ED FF`), the capture
        // comes due a tick later, and the exit bound fires with no frame ever
        // rendered for it. The design's contract (§4.5 CAP-01): deferred to the
        // next rendered frame, never the stale framebuffer — so NO file, the
        // "NO screenshot was written" error, exit 1. Headless used to write the
        // stale framebuffer here and exit 0. The control is the same run with
        // the magic breakpoint off (`ED FF` is then a NOP): file written, exit 0.
        //   8000  ED FF   magic breakpoint
        //   8002  18 FE   JR $
        struct Out { bool ok, paused_magic, file, png, no_shot_error; int exit; };
        auto run = [](bool magic, const std::string& png) {
            std::remove(png.c_str());
            auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64);
            Log::platform()->sinks().push_back(ring);
            EmulatorConfig cfg;
            cfg.type = MachineType::ZX48K;
            cfg.magic_breakpoint = magic;
            HeadlessApp app;
            app.set_config(cfg);
            Out o{};
            o.ok = app.init(0, nullptr);
            load_prog(app.emulator(), { 0xED, 0xFF, 0x18, 0xFE });
            app.set_delayed_screenshot(png, 1, jnext::dbg::LAYER_MASK_ALL);
            app.set_delayed_exit(4);
            app.run();
            const RunState st = app.debugger().state();
            o.paused_magic = st.paused && st.pause_reason.kind == PauseReason::Kind::Magic;
            app.shutdown();
            o.exit = app.exit_code();
            const std::vector<uint8_t> f = read_file(png);
            o.file = !f.empty();
            o.png  = is_png_640x512(f);
            for (const auto& l : ring->last_formatted())
                if (l.find("--delayed-screenshot: NO screenshot was written to '" + png) !=
                    std::string::npos)
                    o.no_shot_error = true;
            Log::platform()->sinks().pop_back();
            std::remove(png.c_str());
            return o;
        };
        const Out p = run(true,  "/tmp/jnext_b4_host_paused.png");
        const Out c = run(false, "/tmp/jnext_b4_host_running.png");
        auto show = [](const Out& o) {
            return std::string("magic_pause=") + (o.paused_magic ? "1" : "0") + " file=" +
                   (o.file ? "1" : "0") + " err=" + (o.no_shot_error ? "1" : "0") +
                   " exit=" + std::to_string(o.exit);
        };
        check("HOST-06", "HeadlessApp: a --delayed-screenshot that comes due while "
                         "--magic-breakpoint holds the machine paused writes NO file, "
                         "logs 'NO screenshot was written' and exits 1 at the exit "
                         "bound; the same run unpaused writes the PNG and exits 0",
              p.ok && p.paused_magic && !p.file && p.no_shot_error && p.exit == 1 &&
                  c.ok && !c.paused_magic && c.png && !c.no_shot_error && c.exit == 0,
              "paused[" + show(p) + "] control[" + show(c) + "]");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GH #276 B5 — the package's sign-off: the §9 row families end to end
// (DEBUG-SUBSYSTEM-ARCHITECTURE.md §9, debug-subsystem/backend.md §7). Every
// group below fills a GAP in the audit table of the B5 report: a required row
// class × a verb or event kind that no earlier row satisfied.
// ═══════════════════════════════════════════════════════════════════════════

/// The machine's position at one instant: where a verb or a `Stop` must leave it.
struct B5Pos {
    uint16_t pc    = 0;
    uint64_t cycle = 0;
};

static B5Pos b5_pos(Emulator& emu) {
    return B5Pos{pc_of(emu), emu.clock().get()};
}

static std::string b5_show(const B5Pos& p) {
    return "pc=" + hex(p.pc) + " cycle=" + std::to_string(p.cycle);
}

// ── WIRE-K — one WIRING row per event kind (§9: "arm through the facade, run,
//    assert the machine stopped where promised (PC, cycle, pause_reason) — plus
//    a control row that runs straight past without the verb"), and the delivery
//    class's "a false predicate does not stop", for every kind.
//
// Before B5 only Execute, Mem, Cycle, Host and Scanline (on the Step path) had
// a `Stop` row at all, and none of them asserted the CYCLE the machine stopped
// on — which is the only field that tells a stop on a `JR $` loop (the Frame,
// Scanline, Cycle, Copper and DMA fixtures) from the same PC one or a thousand
// iterations later. Port, NextRegWrite, Frame, Reset, IntAck, Nmi and all six
// Copper/DMA sub-kinds could not stop the machine without any row noticing.
//
// Each case runs THREE machines over one fixture:
//   CTRL   a `Continue` subscription with a handler — the event IS delivered,
//          and the machine is NOT stopped by it and runs on past the point the
//          delivery happened at. The handler records that point (clock and PC),
//          which is what the STOP row compares against.
//   STOP   the same subscription with `Action::Stop` and no handler — the
//          machine is paused, `pause_reason` names the kind's reason, the
//          subscription and its owner, `state()`'s pc and cycle equal the live
//          machine's AND the CTRL machine's delivery point (a stop is AT the
//          delivery boundary, never after it), the absolute pc/cycle derived from
//          the fixture's own T-states where the fixture makes that arithmetic
//          plain, and a further run_frame() executes nothing (the stop holds).
//   FALSE  `Stop` with a condition that answers false — the condition RAN (so
//          the row is not vacuous), nothing was delivered, and the machine was
//          not stopped: it passed the CTRL machine's delivery point.

/// 12 T-states: the loop every timing fixture parks in. 96 master cycles at the
/// 48K's divisor of 8, so every instruction boundary of a `JR $` run that starts
/// at c0 is c0 + 96k.
static constexpr uint64_t B5_JR_CYCLES = 12u * 8u;

/// The first `JR $` boundary at or after `t`, for a run that starts at `c0`.
static uint64_t b5_jr_boundary(uint64_t c0, uint64_t t) {
    if (t <= c0) return c0;
    return c0 + ((t - c0 + B5_JR_CYCLES - 1) / B5_JR_CYCLES) * B5_JR_CYCLES;
}

/// One row of a table: a LITERAL id and a LITERAL description, so the harness's
/// literal gate and the matrix's description reader both see them in source.
struct B5Row {
    const char* id;
    const char* desc;
};

struct B5KindCase {
    B5Row stop;      // STOP
    B5Row fals;      // FALSE
    B5Row ctrl;      // CTRL
    std::vector<uint8_t> prog;
    std::function<void(Subscription&, Emulator&)> filter;  // kind + cheap filter
    std::function<void(Emulator&, Debugger&, ClientId)> prime;  // extra machine setup
    std::function<void(Emulator&, Debugger&, ClientId)> drive;  // the run under test
    PauseReason::Kind reason;
    bool owned;                                           // reason.by == owner
    /// The absolute stop position, from the fixture's own arithmetic, given the
    /// clock before `drive` (`c0`). `want_pc < 0` / `want_cycle == 0`: not
    /// derivable in closed form for this fixture — the CTRL twin is the oracle.
    std::function<void(Emulator&, uint64_t c0, int32_t& want_pc, uint64_t& want_cycle)> expect;
};

/// A 48K machine with `bytes` at PROG, PC/SP set and interrupts off — and NOT
/// armed: every B5 machine is armed the way §9 says, THROUGH THE FACADE, by the
/// client it attaches. `build_armed()` arms `DebugState` directly, which no
/// remote client can.
static void b5_build(Emulator& emu, const std::vector<uint8_t>& bytes,
                     MachineType type = MachineType::ZX48K) {
    EmulatorConfig cfg;
    cfg.type = type;
    emu.init(cfg);
    load_prog(emu, bytes);
}

static void b5_run_kind_case(const B5KindCase& k) {

    // ── CTRL ────────────────────────────────────────────────────────────────
    B5Pos    delivered{};
    int      n_ctrl = 0;
    B5Pos    ctrl_end{};
    bool     ctrl_paused = true;
    {
        Emulator emu;
        b5_build(emu, k.prog);
        Debugger dbg(emu);
        const ClientId OWNER = dbg.attach(client("B5")).value;
        Subscription s;
        k.filter(s, emu);
        s.action  = Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger&) {
            if (n_ctrl++ == 0) delivered = b5_pos(emu);
            return Action::Continue;
        };
        dbg.subscribe(OWNER, s);
        if (k.prime) k.prime(emu, dbg, OWNER);
        k.drive(emu, dbg, OWNER);
        ctrl_paused = dbg.state().paused;
        ctrl_end    = b5_pos(emu);
    }
    check(k.ctrl.id, k.ctrl.desc,
          n_ctrl >= 1 && !ctrl_paused && ctrl_end.cycle > delivered.cycle,
          "n=" + std::to_string(n_ctrl) + " paused=" + (ctrl_paused ? "1" : "0") +
              " delivered[" + b5_show(delivered) + "] end[" + b5_show(ctrl_end) + "]");

    // ── STOP ────────────────────────────────────────────────────────────────
    {
        Emulator emu;
        b5_build(emu, k.prog);
        Debugger dbg(emu);
        const ClientId OWNER = dbg.attach(client("B5")).value;
        Subscription s;
        k.filter(s, emu);
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(OWNER, s);
        if (k.prime) k.prime(emu, dbg, OWNER);
        const uint64_t c0 = emu.clock().get();
        int32_t  want_pc    = -1;
        uint64_t want_cycle = 0;
        if (k.expect) k.expect(emu, c0, want_pc, want_cycle);
        k.drive(emu, dbg, OWNER);
        const RunState st   = dbg.state();
        const B5Pos    live = b5_pos(emu);
        emu.run_frame();                                  // the stop must HOLD
        const B5Pos    held = b5_pos(emu);
        const bool reason_ok =
            st.pause_reason.kind == k.reason && st.pause_reason.id == sub.value &&
            st.pause_reason.by == (k.owned ? OWNER : jnext::dbg::CLIENT_NONE);
        const bool at_delivery = st.pc == delivered.pc && st.cycle == delivered.cycle;
        const bool is_live     = st.pc == live.pc && st.cycle == live.cycle;
        const bool abs_ok      = (want_pc < 0 || st.pc == static_cast<uint16_t>(want_pc)) &&
                                 (want_cycle == 0 || st.cycle == want_cycle);
        check(k.stop.id, k.stop.desc,
              sub.status == Result::Ok && st.paused && reason_ok && at_delivery &&
                  is_live && abs_ok && held.cycle == live.cycle && held.pc == live.pc,
              std::string("paused=") + (st.paused ? "1" : "0") +
                  " reason=" + std::to_string(static_cast<int>(st.pause_reason.kind)) +
                  " id=" + std::to_string(st.pause_reason.id) + "/" +
                  std::to_string(sub.value) +
                  " by=" + std::to_string(st.pause_reason.by) +
                  " state[" + b5_show(B5Pos{st.pc, st.cycle}) + "] live[" +
                  b5_show(live) + "] delivered[" + b5_show(delivered) + "] want[pc=" +
                  (want_pc < 0 ? std::string("-") : hex(static_cast<unsigned>(want_pc))) +
                  " cycle=" + (want_cycle ? std::to_string(want_cycle) : std::string("-")) +
                  "] held[" + b5_show(held) + "] c0=" + std::to_string(c0));
    }

    // ── FALSE ───────────────────────────────────────────────────────────────
    {
        Emulator emu;
        b5_build(emu, k.prog);
        Debugger dbg(emu);
        const ClientId OWNER = dbg.attach(client("B5")).value;
        int evaluated = 0;
        Subscription s;
        k.filter(s, emu);
        s.action    = Action::Stop;
        s.condition = [&evaluated](const DbgEvent&, const Debugger&) {
            ++evaluated;
            return false;
        };
        dbg.subscribe(OWNER, s);
        if (k.prime) k.prime(emu, dbg, OWNER);
        k.drive(emu, dbg, OWNER);
        const bool paused = dbg.state().paused;
        const B5Pos end   = b5_pos(emu);
        check(k.fals.id, k.fals.desc,
              evaluated >= 1 && !paused && dbg.events_fired_since(0).empty() &&
                  end.cycle > delivered.cycle,
              "evaluated=" + std::to_string(evaluated) + " paused=" +
                  (paused ? "1" : "0") + " fired=" +
                  std::to_string(dbg.events_fired_since(0).size()) + " end[" +
                  b5_show(end) + "] delivered[" + b5_show(delivered) + "]");
    }
}

/// The DMA fixture every Dma case shares: an 8-byte memory-to-memory block
/// 0xA000 -> 0x9000, programmed and ENABLED (R6 0x87), so the first slot of the
/// next run is the DMA's. The register sequence is EVT-DMA's.
static void b5_prime_dma(Emulator& emu, Debugger&, ClientId) {
    for (int i = 0; i < 8; ++i)
        emu.mmu().write(static_cast<uint16_t>(0xA000 + i), static_cast<uint8_t>(0xA0 + i));
    Dma& d = emu.dma();
    auto w = [&](uint8_t v) { d.write(v, false); };
    w(0x7D); w(0x00); w(0xA0); w(0x08); w(0x00);
    w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
    w(0xCF); w(0x87);
}

static void b5_wire_kind_rows() {
    using K = PauseReason::Kind;
    auto run1 = [](Emulator& emu, Debugger&, ClientId) { emu.run_frame(); };
    const std::vector<uint8_t> JR = { 0x18, 0xFE };                 // 8000 JR $

    const B5KindCase cases[] = {
        // ── Execute: before the instruction at 0x8002 — NOP NOP = 8 T.
        {{"WK-EXEC-01", "Execute Stop: paused BEFORE 0x8002 (pc 0x8002, cycle c0+64), "
                        "reason Breakpoint naming the subscription and its owner, and "
                        "the stop holds"},
         {"WK-EXEC-02", "Execute Stop with a condition answering false: the condition "
                        "ran, nothing was delivered, the machine ran past"},
         {"WK-EXEC-03", "Execute control: delivered with Continue, not stopped, ran on"},
         {0x00, 0x00, 0x00, 0x18, 0xFE},
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Execute; s.filter.lo = 0x8002; s.filter.hi = 0x8002;
         },
         nullptr, run1, K::Breakpoint, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) { pc = 0x8002; cy = c0 + 8 * 8; }},

        // ── Mem: at the boundary of the writer — LD A,n (7) + LD (nn),A (13).
        {{"WK-MEM-01", "Mem{Write} Stop: paused at the writer's boundary (pc 0x8005, "
                       "cycle c0+160), reason Watch naming the subscription and its "
                       "owner, and the stop holds"},
         {"WK-MEM-02", "Mem Stop with a condition answering false: the condition ran, "
                       "nothing was delivered, the machine ran past"},
         {"WK-MEM-03", "Mem control: delivered with Continue, not stopped, ran on"},
         {0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x18, 0xFE},
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Mem; s.access = Access::Write;
             s.filter.lo = 0x9000; s.filter.hi = 0x9000;
         },
         nullptr, run1, K::Watch, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) { pc = 0x8005; cy = c0 + 20 * 8; }},

        // ── Port: at the boundary of the OUT — LD A,n (7) + OUT (n),A (11).
        {{"WK-PORT-01", "Port{Write} Stop: paused at the OUT's boundary (pc 0x8004, "
                        "cycle c0+144), reason Watch naming the subscription and its "
                        "owner, and the stop holds"},
         {"WK-PORT-02", "Port Stop with a condition answering false: the condition ran, "
                        "nothing was delivered, the machine ran past"},
         {"WK-PORT-03", "Port control: delivered with Continue, not stopped, ran on"},
         {0x3E, 0x07, 0xD3, 0xFE, 0x00, 0x18, 0xFE},
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Port; s.access = Access::Write;
             s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
         },
         nullptr, run1, K::Watch, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) { pc = 0x8004; cy = c0 + 18 * 8; }},

        // ── NextRegWrite: a CPU write commits after the drain, so §4.3's Stop
        //    lands ONE instruction after the writer — NEXTREG n,n (20) + NOP (4).
        {{"WK-NR-01", "NextRegWrite Stop: paused ONE instruction after the CPU writer "
                      "(pc 0x8005, cycle c0+192, §4.3), reason Script naming the "
                      "subscription and its owner, and the stop holds"},
         {"WK-NR-02", "NextRegWrite Stop with a condition answering false: the condition "
                      "ran, nothing was delivered, the machine ran past"},
         {"WK-NR-03", "NextRegWrite control: delivered with Continue, not stopped, ran on"},
         {0xED, 0x91, 0x15, 0x07, 0x00, 0x00, 0x18, 0xFE},
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::NextRegWrite; s.filter.regs = { 0x15 };
         },
         nullptr, run1, K::Script, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) { pc = 0x8005; cy = c0 + 24 * 8; }},

        // ── Frame: at the frame edge — the end of frame 0, on the JR $ grid.
        //    Two frames driven, so the control has a frame to run on past it.
        {{"WK-FRAME-01", "Frame Stop: paused AT the edge of frame 0 (the first JR $ "
                         "boundary at or after c0 + one frame), reason Script naming "
                         "the subscription, and the next frame executes nothing"},
         {"WK-FRAME-02", "Frame Stop with a condition answering false: the condition "
                         "ran, nothing was delivered, the machine ran past"},
         {"WK-FRAME-03", "Frame control: delivered with Continue, not stopped, ran on"},
         JR,
         [](Subscription& s, Emulator&) { s.kind = EventKind::Frame; s.filter.frame = 0; },
         nullptr,
         [](Emulator& emu, Debugger&, ClientId) { emu.run_frame(); emu.run_frame(); },
         K::Script, true,
         [](Emulator& emu, uint64_t c0, int32_t& pc, uint64_t& cy) {
             pc = PROG;
             cy = b5_jr_boundary(c0, c0 + emu.timing().master_cycles_per_frame);
         }},

        // ── Scanline: latched at the line in the device cluster of the
        //    instruction that crosses it, delivered at the NEXT boundary (§4.3,
        //    ≤1 instruction late). The raw line is the VHDL relation
        //    cvc = (raw - min_vactive + cu_offset) mod lines (zxula_timing.vhd:455-472)
        //    solved for raw — the relation EVT-TIME-16 pins independently.
        {{"WK-SCAN-01", "Scanline Stop: paused one JR $ boundary after the one that "
                        "crossed cvc 100's raw line (§4.3 <=1 late), reason Script, "
                        "and the stop holds"},
         {"WK-SCAN-02", "Scanline Stop with a condition answering false: the condition "
                        "ran, nothing was delivered, the machine ran past"},
         {"WK-SCAN-03", "Scanline control: delivered with Continue, not stopped, ran on"},
         JR,
         [](Subscription& s, Emulator&) { s.kind = EventKind::Scanline; s.filter.scanline = 100; },
         nullptr, run1, K::Script, true,
         [](Emulator& emu, uint64_t c0, int32_t& pc, uint64_t& cy) {
             const int lpf  = emu.video_timing().vc_max() + 1;
             const int minv = emu.video_timing().display_origin().vc;
             const int cuo  = emu.video_timing().cu_offset();
             int raw = (100 + minv - cuo) % lpf;
             if (raw < 0) raw += lpf;
             const uint64_t line_cycle =
                 c0 + static_cast<uint64_t>(raw) * emu.timing().master_cycles_per_line;
             pc = PROG;
             cy = b5_jr_boundary(c0, line_cycle) + B5_JR_CYCLES;
         }},

        // ── Cycle: evaluated at every boundary, `>=` — a target ON the JR $
        //    grid stops exactly there (a `>` would stop one boundary later).
        {{"WK-CYCLE-01", "Cycle Stop: a target on an instruction boundary stops EXACTLY "
                         "there (>=, not >), reason Script, and the stop holds"},
         {"WK-CYCLE-02", "Cycle Stop with a condition answering false: the condition "
                         "ran, nothing was delivered, the machine ran past"},
         {"WK-CYCLE-03", "Cycle control: delivered with Continue, not stopped, ran on"},
         JR,
         [](Subscription& s, Emulator& emu) {
             s.kind = EventKind::Cycle;
             s.filter.cycle = emu.clock().get() + 52 * B5_JR_CYCLES;
         },
         nullptr, run1, K::Script, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) {
             pc = PROG; cy = c0 + 52 * B5_JR_CYCLES;
         }},

        // ── Reset{Soft}, through the verb: latched before init() rebuilds the
        //    peripherals, delivered at the first boundary of the reset machine —
        //    after its FIRST instruction. What that instruction is depends on the
        //    ROM this host has (a 0xFF-filled one runs RST 38h), so the position
        //    is measured on a REFERENCE machine: the same build, soft-reset, one
        //    raw execute_single_instruction() — the event machinery plays no part.
        {{"WK-RESET-01", "Reset{Soft} Stop: paused at the first boundary after the "
                         "reset (where one raw instruction of a reset reference machine "
                         "lands), reason Script naming the subscription, and the stop "
                         "holds"},
         {"WK-RESET-02", "Reset Stop with a condition answering false: the condition "
                         "ran, nothing was delivered, the machine ran past"},
         {"WK-RESET-03", "Reset control: delivered with Continue, not stopped, ran on"},
         {0x00, 0x18, 0xFD},
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Soft;
         },
         nullptr,
         [](Emulator& emu, Debugger& d, ClientId by) {
             d.reset(by, ResetKind::Soft);
             emu.run_frame();
         },
         K::Script, true,
         [](Emulator&, uint64_t, int32_t& pc, uint64_t& cy) {
             Emulator ref;
             b5_build(ref, {0x00, 0x18, 0xFD});
             ref.soft_reset();
             ref.execute_single_instruction();
             pc = pc_of(ref);
             cy = ref.clock().get();
         }},

        // ── IntAck: the frame interrupt, IM 1, taken out of a HALT; delivered at
        //    the acknowledge slot's boundary — PC already at the IM 1 vector.
        {{"WK-INT-01", "IntAck Stop: paused at the acknowledge slot's boundary, PC on "
                       "the IM 1 vector 0x0038, reason Script, and the stop holds"},
         {"WK-INT-02", "IntAck Stop with a condition answering false: the condition "
                       "ran, nothing was delivered, the machine ran past"},
         {"WK-INT-03", "IntAck control: delivered with Continue, not stopped, ran on"},
         {0x76, 0x18, 0xFD},
         [](Subscription& s, Emulator&) { s.kind = EventKind::IntAck; },
         [](Emulator& emu, Debugger&, ClientId) {
             Z80Registers r = emu.cpu().get_registers();
             r.IFF1 = 1; r.IFF2 = 1; r.IM = 1;
             emu.cpu().set_registers(r);
         },
         run1, K::Script, true,
         [](Emulator&, uint64_t, int32_t& pc, uint64_t&) { pc = 0x0038; }},

        // ── Nmi: the Multiface button through the IN-04 verb; delivered at the
        //    accept slot's boundary — PC already on the NMI vector.
        {{"WK-NMI-01", "Nmi Stop: paused at the accept slot's boundary, PC on the NMI "
                       "vector 0x0066, reason Script, and the stop holds"},
         {"WK-NMI-02", "Nmi Stop with a condition answering false: the condition ran, "
                       "nothing was delivered, the machine ran past"},
         {"WK-NMI-03", "Nmi control: delivered with Continue, not stopped, ran on"},
         {0x00, 0x18, 0xFD},
         [](Subscription& s, Emulator&) { s.kind = EventKind::Nmi; },
         [](Emulator& emu, Debugger& d, ClientId by) {
             emu.nmi_source().set_mf_enable(true);        // NR 0x06 bit 3
             d.press_nmi(by, NmiButton::Mf);
         },
         run1, K::Script, true,
         [](Emulator&, uint64_t, int32_t& pc, uint64_t&) { pc = 0x0066; }},

        // ── Host: synchronous — the stop is where the machine stood when the
        //    frontend raised it, and the frame after it executes nothing.
        {{"WK-HOST-01", "Host Stop: paused exactly where the machine stood when the "
                        "name was raised (pc 0x8000, cycle c0), reason Script, and the "
                        "next frame executes nothing"},
         {"WK-HOST-02", "Host Stop with a condition answering false: the condition ran, "
                        "nothing was delivered, the machine ran past"},
         {"WK-HOST-03", "Host control: delivered with Continue, not stopped, ran on"},
         {0x00, 0x18, 0xFD},
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Host; std::strcpy(s.filter.host_name, "script2");
         },
         nullptr,
         [](Emulator& emu, Debugger& d, ClientId by) {
             d.raise_host_event(by, "script2");
             emu.run_frame();
         },
         K::Script, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) { pc = PROG; cy = c0; }},

        // ── Copper{Move}: latched in the device cluster AFTER the first JR $,
        //    delivered at the second one's boundary (§4.3, <=1 late).
        {{"WK-COPMOVE-01", "Copper{Move} Stop: paused at the second JR $ boundary "
                           "(cycle c0+192, §4.3 <=1 late), reason Script, and the stop "
                           "holds"},
         {"WK-COPMOVE-02", "Copper{Move} Stop with a condition answering false: the "
                           "condition ran, nothing was delivered, the machine ran past"},
         {"WK-COPMOVE-03", "Copper{Move} control: delivered with Continue, not stopped, "
                           "ran on"},
         JR,
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Copper;
             s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
         },
         [](Emulator& emu, Debugger&, ClientId) {
             copper_program(emu, { move_word(0x30, 0x7F), HALT_WORD });
             copper_start(emu);
         },
         run1, K::Script, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) {
             pc = PROG; cy = c0 + 2 * B5_JR_CYCLES;
         }},

        // ── Copper{Wait}: satisfied on line 40, delivered one boundary later.
        {{"WK-COPWAIT-01", "Copper{Wait} Stop: paused at the delivery boundary of the "
                           "satisfied WAIT, reason Script, and the stop holds"},
         {"WK-COPWAIT-02", "Copper{Wait} Stop with a condition answering false: the "
                           "condition ran, nothing was delivered, the machine ran past"},
         {"WK-COPWAIT-03", "Copper{Wait} control: delivered with Continue, not stopped, "
                           "ran on"},
         JR,
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Copper;
             s.filter.copper_kind = jnext::dbg::CopperEventKind::Wait;
         },
         [](Emulator& emu, Debugger&, ClientId) {
             copper_program(emu, { wait_word(4, 40), move_word(0x30, 0x21), HALT_WORD });
             copper_start(emu);
         },
         run1, K::Script, true,
         [](Emulator&, uint64_t, int32_t& pc, uint64_t&) { pc = PROG; }},

        // ── Copper{Halt}: the edge, latched after the first JR $.
        {{"WK-COPHALT-01", "Copper{Halt} Stop: paused at the second JR $ boundary "
                           "(cycle c0+192), reason Script, and the stop holds"},
         {"WK-COPHALT-02", "Copper{Halt} Stop with a condition answering false: the "
                           "condition ran, nothing was delivered, the machine ran past"},
         {"WK-COPHALT-03", "Copper{Halt} control: delivered with Continue, not stopped, "
                           "ran on"},
         JR,
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Copper;
             s.filter.copper_kind = jnext::dbg::CopperEventKind::Halt;
         },
         [](Emulator& emu, Debugger&, ClientId) {
             copper_program(emu, { HALT_WORD });
             copper_start(emu);
         },
         run1, K::Script, true,
         [](Emulator&, uint64_t c0, int32_t& pc, uint64_t& cy) {
             pc = PROG; cy = c0 + 2 * B5_JR_CYCLES;
         }},

        // ── Dma: the burst runs in the run's first slot, which is the DMA's and
        //    executes no CPU instruction — delivered at THAT boundary (§4.3,
        //    0 late), so PC has not moved.
        {{"WK-DMASTART-01", "Dma{Start} Stop: paused at the DMA slot's boundary, PC "
                            "unmoved (the slot ran no instruction), reason Script, and "
                            "the stop holds"},
         {"WK-DMASTART-02", "Dma{Start} Stop with a condition answering false: the "
                            "condition ran, nothing was delivered, the machine ran past"},
         {"WK-DMASTART-03", "Dma{Start} control: delivered with Continue, not stopped, "
                            "ran on"},
         JR,
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Dma; s.filter.dma_kind = jnext::dbg::DmaEventKind::Start;
         },
         b5_prime_dma, run1, K::Script, true,
         [](Emulator&, uint64_t, int32_t& pc, uint64_t&) { pc = PROG; }},
        {{"WK-DMABYTE-01", "Dma{Byte} Stop: paused at the DMA slot's boundary, PC "
                           "unmoved, reason Script, and the stop holds"},
         {"WK-DMABYTE-02", "Dma{Byte} Stop with a condition answering false: the "
                           "condition ran, nothing was delivered, the machine ran past"},
         {"WK-DMABYTE-03", "Dma{Byte} control: delivered with Continue, not stopped, "
                           "ran on"},
         JR,
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Dma; s.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
         },
         b5_prime_dma, run1, K::Script, true,
         [](Emulator&, uint64_t, int32_t& pc, uint64_t&) { pc = PROG; }},
        {{"WK-DMAEND-01", "Dma{End} Stop: paused at the DMA slot's boundary, PC "
                          "unmoved, reason Script, and the stop holds"},
         {"WK-DMAEND-02", "Dma{End} Stop with a condition answering false: the "
                          "condition ran, nothing was delivered, the machine ran past"},
         {"WK-DMAEND-03", "Dma{End} control: delivered with Continue, not stopped, "
                          "ran on"},
         JR,
         [](Subscription& s, Emulator&) {
             s.kind = EventKind::Dma; s.filter.dma_kind = jnext::dbg::DmaEventKind::End;
         },
         b5_prime_dma, run1, K::Script, true,
         [](Emulator&, uint64_t, int32_t& pc, uint64_t&) { pc = PROG; }},
    };
    for (const B5KindCase& k : cases) b5_run_kind_case(k);
}


// ── WV — one WIRING row per run-control verb: the machine stopped where the
//    verb promises, asserted on all three of §9's fields — PC, CYCLE and
//    `pause_reason` (kind AND owner) — plus the control run.
//
// The CTL-* rows B1 wrote assert the PC and, for most verbs, the reason; not
// one asserts the cycle, and step_out / run_to_cycle / the end-of-* pair assert
// no reason. On `build()`'s program the cycle is plain arithmetic, so every
// verb's stop is pinned to the master cycle:
//
//   T:  0 NOP | 4 NOP | 8 CALL 9000 | 25 NOP | 29 NOP | 33 RET | 43 NOP (0x8005)
//       | 47 JR $ (0x8006) | 59 | 71 | ...        — x8 master cycles on the 48K
//
// so the boundaries are c0 + {0,32,64,200,232,264,344,376} and then every
// 96 cycles of the JR $ loop. A stop at the right PC on the WRONG iteration of
// that loop — the failure a PC-only row cannot see — moves the cycle.

static uint64_t b5_prog_boundary(uint64_t c0, uint64_t t) {
    static const uint64_t head[] = { 0, 32, 64, 200, 232, 264, 344, 376 };
    for (uint64_t b : head)
        if (c0 + b >= t) return c0 + b;
    return b5_jr_boundary(c0 + 376, t);
}

/// `build()`'s program, and a client attached through the facade (which is
/// what arms the machine).
struct B5VerbMachine {
    Emulator emu;
    std::unique_ptr<Debugger> dbg;
    ClientId a = jnext::dbg::CLIENT_NONE;
    uint64_t c0 = 0;
    explicit B5VerbMachine(bool rewind = false) {
        build(emu);
        if (rewind) {
            emu.set_rewind_enabled(true);
            emu.resize_rewind_buffer(8);
        }
        dbg = std::make_unique<Debugger>(emu);
        a   = dbg->attach(client("B5")).value;
        c0  = emu.clock().get();
    }
};

static std::string b5_state(Debugger& dbg) {
    const RunState st = dbg.state();
    return std::string("paused=") + (st.paused ? "1" : "0") + " " +
           b5_show(B5Pos{st.pc, st.cycle}) +
           " reason=" + std::to_string(static_cast<int>(st.pause_reason.kind)) +
           " by=" + std::to_string(st.pause_reason.by) +
           " addr=" + hex(st.pause_reason.addr);
}

/// §9's three fields, plus the two things that make them mean "stopped":
/// `state()` agrees with the live machine, and a further frame executes nothing.
static bool b5_stopped_at(Emulator& emu, Debugger& dbg, uint16_t pc, uint64_t cycle,
                          PauseReason::Kind kind, ClientId by) {
    const RunState st = dbg.state();
    const B5Pos live = b5_pos(emu);
    emu.run_frame();
    const B5Pos held = b5_pos(emu);
    return st.paused && st.pc == pc && st.cycle == cycle &&
           st.pause_reason.kind == kind && st.pause_reason.by == by &&
           live.pc == pc && live.cycle == cycle && held.pc == pc && held.cycle == cycle;
}

static void b5_wire_verb_rows() {
    using K = PauseReason::Kind;
    {
        B5VerbMachine m;
        m.emu.execute_single_instruction();
        m.emu.execute_single_instruction();
        m.emu.execute_single_instruction();              // CALL taken: at SUB
        const std::string before = b5_show(b5_pos(m.emu));
        m.dbg->pause(m.a);
        check("WV-PAUSE-01", "pause(): stopped where it stood (pc 0x9000, cycle c0+200), "
                             "reason User{by the caller}, and a frame executes nothing",
              b5_stopped_at(m.emu, *m.dbg, SUB, m.c0 + 200, K::User, m.a),
              "before[" + before + "] " + b5_state(*m.dbg));
    }
    {
        B5VerbMachine m;
        m.dbg->pause(m.a);
        m.dbg->step_into(m.a);
        check("WV-STEPINTO-01", "step_into(): one instruction (pc 0x8001, cycle c0+32), "
                                "reason Step{by the caller}, and the stop holds",
              b5_stopped_at(m.emu, *m.dbg, PROG + 1, m.c0 + 32, K::Step, m.a),
              b5_state(*m.dbg));
    }
    {
        B5VerbMachine m;
        m.dbg->pause(m.a);
        m.dbg->step_into(m.a);
        m.dbg->step_into(m.a);                           // on the CALL
        m.dbg->step_over(m.a);
        run_until_paused(m.emu);
        check("WV-STEPOVER-01", "step_over() at the CALL: the whole call ran (pc 0x8005, "
                                "cycle c0+344), reason Step{by the caller}, and the stop "
                                "holds",
              b5_stopped_at(m.emu, *m.dbg, AFTER_CALL, m.c0 + 344, K::Step, m.a),
              b5_state(*m.dbg));
    }
    {
        B5VerbMachine m;
        m.emu.execute_single_instruction();
        m.emu.execute_single_instruction();
        m.emu.execute_single_instruction();              // inside SUB
        m.dbg->pause(m.a);
        m.dbg->step_out(m.a);
        run_until_paused(m.emu);
        check("WV-STEPOUT-01", "step_out() inside SUB: back at the caller's next "
                               "instruction (pc 0x8005, cycle c0+344), reason Step{by "
                               "the caller}, and the stop holds",
              b5_stopped_at(m.emu, *m.dbg, AFTER_CALL, m.c0 + 344, K::Step, m.a),
              b5_state(*m.dbg));
    }
    {
        // inspect.h's RunTo: "`id` names the transient subscription where there
        // was one" — and run_to() arms exactly one.
        B5VerbMachine m;
        m.dbg->pause(m.a);
        m.dbg->run_to(m.a, AFTER_CALL);
        jnext::dbg::EventId target = jnext::dbg::EVENT_NONE;
        for (const auto& si : m.dbg->subscriptions(true))
            if (si.transient) target = si.id;
        run_until_paused(m.emu);
        check("WV-RUNTO-01", "run_to(0x8005): stopped AT it (cycle c0+344), reason "
                             "RunTo{by the caller, addr 0x8005, id = the transient it "
                             "armed}, and the stop holds",
              target != jnext::dbg::EVENT_NONE &&
                  m.dbg->state().pause_reason.addr == AFTER_CALL &&
                  m.dbg->state().pause_reason.id == target &&
                  b5_stopped_at(m.emu, *m.dbg, AFTER_CALL, m.c0 + 344, K::RunTo, m.a),
              "target=" + std::to_string(target) + " id=" +
                  std::to_string(m.dbg->state().pause_reason.id) + " " +
                  b5_state(*m.dbg));
    }
    {
        // ON the grid: a `>` for `>=` would stop one JR $ later.
        B5VerbMachine m;
        const uint64_t target = m.c0 + 376 + 16 * B5_JR_CYCLES;
        m.dbg->pause(m.a);
        m.dbg->run_to_cycle(m.a, target);
        run_until_paused(m.emu);
        check("WV-RUNTOCYCLE-01", "run_to_cycle(t) with t on an instruction boundary: "
                                  "stopped EXACTLY at t (pc 0x8006), reason RunTo{by the "
                                  "caller}, and the stop holds",
              b5_stopped_at(m.emu, *m.dbg, PARK, target, K::RunTo, m.a),
              "target=" + std::to_string(target) + " " + b5_state(*m.dbg));
    }
    {
        // The target is the verb's definition (§4.1 CTL-08, the moved Qt code):
        // the end of the current line rounded up to the next line boundary.
        B5VerbMachine m;
        const uint64_t line   = m.emu.timing().master_cycles_per_line;
        const uint64_t target = m.emu.current_frame_cycle() + line;
        m.dbg->pause(m.a);
        m.dbg->run_to_end_of_scanline(m.a);
        run_until_paused(m.emu);
        const uint64_t want = b5_prog_boundary(m.c0, target);
        check("WV-EOSL-01", "run_to_end_of_scanline() from the frame's first line: "
                            "stopped at the first boundary at or past the next line's "
                            "start, reason RunTo{by the caller}, and the stop holds",
              b5_stopped_at(m.emu, *m.dbg, PARK, want, K::RunTo, m.a),
              "want=" + std::to_string(want) + " " + b5_state(*m.dbg));
    }
    {
        // The midpoint of the last VISIBLE row: framebuffer row 255 is raw line
        // 255 + vblank_top() (G164v2).
        B5VerbMachine m;
        const uint64_t line   = m.emu.timing().master_cycles_per_line;
        const uint64_t target = m.emu.current_frame_cycle() +
            static_cast<uint64_t>(255 + m.emu.video_timing().vblank_top()) * line + line / 2;
        m.dbg->pause(m.a);
        m.dbg->run_to_end_of_frame(m.a);
        run_until_paused(m.emu, 3);
        const uint64_t want = b5_prog_boundary(m.c0, target);
        check("WV-EOF-01", "run_to_end_of_frame(): stopped at the first boundary at or "
                           "past the midpoint of the last visible row, reason RunTo{by "
                           "the caller}, and the stop holds",
              b5_stopped_at(m.emu, *m.dbg, PARK, want, K::RunTo, m.a),
              "want=" + std::to_string(want) + " " + b5_state(*m.dbg));
    }
    {
        B5VerbMachine m;
        const uint64_t target = m.emu.current_frame_cycle() +
                                2 * m.emu.timing().master_cycles_per_frame;
        m.dbg->pause(m.a);
        m.dbg->run_to_frame(m.a, 2);
        run_until_paused(m.emu, 4);
        const uint64_t want = b5_prog_boundary(m.c0, target);
        check("WV-RUNTOFRAME-01", "run_to_frame(2) on a never-run machine: stopped at "
                                  "the first boundary of frame 2 (tag 2), reason RunTo{by "
                                  "the caller}, and the stop holds",
              m.dbg->time().frame == 2 &&
                  b5_stopped_at(m.emu, *m.dbg, PARK, want, K::RunTo, m.a),
              "want=" + std::to_string(want) + " frame=" +
                  std::to_string(m.dbg->time().frame) + " " + b5_state(*m.dbg));
    }
    {
        B5VerbMachine m(/*rewind=*/true);
        m.dbg->pause(m.a);
        m.dbg->step_into(m.a);
        m.dbg->step_into(m.a);                           // pc 0x8002, cycle c0+64
        const Result r = m.dbg->step_back(m.a, 1);
        check("WV-STEPBACK-01", "step_back(1): one instruction back (pc 0x8001, cycle "
                                "c0+32), reason Step{by the caller}, and the stop holds",
              r == Result::Ok &&
                  b5_stopped_at(m.emu, *m.dbg, PROG + 1, m.c0 + 32, K::Step, m.a),
              "rc=" + std::to_string(static_cast<int>(r)) + " " + b5_state(*m.dbg));
    }
    {
        // A frame's snapshot is taken as it BEGINS, which is the first boundary
        // at or past its start cycle — frame 0 ran out on that boundary.
        B5VerbMachine m(/*rewind=*/true);
        m.emu.run_frame(); m.emu.run_frame(); m.emu.run_frame();
        m.dbg->pause(m.a);
        const Result r = m.dbg->rewind_to_frame(m.a, 1);
        const uint64_t want =
            b5_prog_boundary(m.c0, m.c0 + m.emu.timing().master_cycles_per_frame);
        check("WV-REWIND-01", "rewind_to_frame(1): back at frame 1's first boundary (tag "
                              "1), reason Step{by the caller}, and the stop holds",
              r == Result::Ok && m.dbg->time().frame == 1 &&
                  b5_stopped_at(m.emu, *m.dbg, PARK, want, K::Step, m.a),
              "rc=" + std::to_string(static_cast<int>(r)) + " want=" +
                  std::to_string(want) + " frame=" +
                  std::to_string(m.dbg->time().frame) + " " + b5_state(*m.dbg));
    }
    {
        // ED FF executes as an 8-T NOP and then pauses: the stop is AFTER it.
        Emulator emu;
        b5_build(emu, { 0xED, 0xFF, 0x18, 0xFE });
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        dbg.set_magic_breakpoint(true);
        const uint64_t c0 = emu.clock().get();
        emu.run_frame();
        check("WV-MAGIC-01", "set_magic_breakpoint(true): ED FF stops the machine after "
                             "it (pc 0x8002, cycle c0+64), reason Magic{addr 0x8000}, "
                             "UNOWNED although a client is attached, and the stop holds",
              dbg.state().pause_reason.addr == 0x8000 &&
                  b5_stopped_at(emu, dbg, 0x8002, c0 + 64, K::Magic,
                                jnext::dbg::CLIENT_NONE),
              "client=" + std::to_string(a) + " " + b5_state(dbg));
    }
    {
        // THE CONTROL: the same program on the same ARMED machine (a client
        // attached — CTL-00-01 runs unarmed), no verb. It passes every stop point
        // above and never pauses.
        B5VerbMachine m;
        bool ever_paused = false;
        for (int i = 0; i < 3; ++i) {
            m.emu.run_frame();
            ever_paused = ever_paused || m.dbg->state().paused;
        }
        const uint64_t past = b5_prog_boundary(
            m.c0, m.c0 + 2 * m.emu.timing().master_cycles_per_frame);
        check("WV-CTRL-01", "control: an armed machine with no verb runs three frames "
                            "straight past every stop point above and never pauses",
              !ever_paused && pc_of(m.emu) == PARK && m.emu.clock().get() > past,
              b5_state(*m.dbg));
    }
}


// ── RC — the RECONSTRUCT rows §9 names: "subscribe an `Execute`, a `Mem` and a
//    `NextRegWrite`, `load()` a `.nex` / `reset(Hard)` through a registered
//    driver, assert all three still fire ... a paused caller is paused at PC 0
//    and a running one is still running afterwards".
//
// What B3/B4 left: only `Mem` was shown to fire after a rebuild (CTL-12-13,
// CTL-12-22, CTL-15-08), no row asserted WHERE a paused caller is left, and the
// `.nex` of CTL-15-07 was a name handed to a driver that only cold-booted —
// no `.nex` was ever loaded. Every loop owner registers `load` as
// `emulator_apply_load()`, which for a `.nex` is `Emulator::load_nex()`: an
// IN-PLACE `init()` of the whole machine, the route a user's `load` actually
// takes, and one no row exercised at all.
//
// One program carries all three kinds, so one run measures all three:
//   8000  3E 11        LD A,0x11
//   8002  32 10 50     LD (0x5010),A        -> Mem{Write} 0x5010
//   8005  ED 91 15 07  NEXTREG 0x15,0x07    -> NextRegWrite{0x15}
//   8009  00           NOP                  (the NR delivery's boundary)
//   800A  18 FE        JR $
// with the `Execute` subscription on 0x8000.
static const std::vector<uint8_t> kB5ThreeKinds = {
    0x3E, 0x11, 0x32, 0x10, 0x50, 0xED, 0x91, 0x15, 0x07, 0x00, 0x18, 0xFE };

struct B5Counts {
    int exec = 0, mem = 0, nr = 0;
};

static void b5_subscribe_three(Debugger& dbg, ClientId a, B5Counts*& into) {
    auto counter = [&into](int B5Counts::*field) {
        return [&into, field](const DbgEvent&, Debugger&) {
            ++(into->*field);
            return Action::Continue;
        };
    };
    Subscription x;
    x.kind = EventKind::Execute; x.filter.lo = PROG; x.filter.hi = PROG;
    x.action = Action::Continue; x.handler = counter(&B5Counts::exec);
    dbg.subscribe(a, x);
    Subscription m;
    m.kind = EventKind::Mem; m.access = Access::Write;
    m.filter.lo = WATCHED; m.filter.hi = WATCHED;
    m.action = Action::Continue; m.handler = counter(&B5Counts::mem);
    dbg.subscribe(a, m);
    Subscription n;
    n.kind = EventKind::NextRegWrite; n.filter.regs = { 0x15 };
    n.action = Action::Continue; n.handler = counter(&B5Counts::nr);
    dbg.subscribe(a, n);
}

static std::string b5_counts(const B5Counts& c) {
    return "exec=" + std::to_string(c.exec) + " mem=" + std::to_string(c.mem) +
           " nr=" + std::to_string(c.nr);
}

/// A minimal V1.2 `.nex`: one 16 KB bank (bank 2, 0x8000) holding `code`,
/// entry PC 0x8000, SP 0xFF00, no screen, no loading delay — the layout
/// nex_loader_test's bank fixture uses.
static bool b5_write_nex(const std::string& path, const std::vector<uint8_t>& code) {
    constexpr size_t BANK = 16384;
    std::vector<uint8_t> file(512 + BANK, 0x00);
    std::memcpy(file.data() + 0, "Next", 4);
    std::memcpy(file.data() + 4, "V1.2", 4);
    file[9]  = 1;                        // num_banks
    file[11] = 7;                        // border
    file[12] = 0x00; file[13] = 0xFF;    // SP 0xFF00
    file[14] = 0x00; file[15] = 0x80;    // PC 0x8000
    file[18 + 2] = 1;                    // bank 2 present
    for (size_t i = 0; i < code.size(); ++i) file[512 + i] = code[i];
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
}

static void b5_recon_rows() {
    using K = PauseReason::Kind;
    auto boot = [](Emulator& emu) {
        emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
    };

    // ── The two boot routes: reset(Hard) through a registered driver, and the
    //    guest path the loop owner brackets with begin/done. Mem after both is
    //    CTL-12-13 / CTL-12-22; Execute and NextRegWrite were never asserted.
    for (int route = 0; route < 2; ++route) {
        const bool guest = route == 1;
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        B5Counts before, after;
        B5Counts* into = &before;
        b5_subscribe_three(dbg, a, into);
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() { boot(emu); return true; };
        dbg.set_loop_driver(d);

        load_prog(emu, kB5ThreeKinds);
        emu.run_frame();
        const bool all_before = before.exec > 0 && before.mem > 0 && before.nr > 0;

        into = &after;
        if (guest) { dbg.on_cold_boot_begin(); boot(emu); dbg.on_cold_boot_done(); }
        else       dbg.reset(a, ResetKind::Hard);
        load_prog(emu, kB5ThreeKinds);                  // the boot wiped RAM
        emu.run_frame();
        const std::string detail = "before[" + b5_counts(before) + "] after[" +
                                   b5_counts(after) + "]";
        check(guest ? "RC-GUEST-01" : "RC-HARD-01",
              guest ? "guest begin/done: the Execute subscription that fired before "
                      "the boot fires on the rebuilt machine"
                    : "reset(Hard): the Execute subscription that fired before the "
                      "boot fires on the rebuilt machine",
              all_before && after.exec > 0, detail);
        check(guest ? "RC-GUEST-02" : "RC-HARD-02",
              guest ? "guest begin/done: the NextRegWrite subscription that fired "
                      "before the boot fires on the rebuilt machine"
                    : "reset(Hard): the NextRegWrite subscription that fired before "
                      "the boot fires on the rebuilt machine",
              all_before && after.nr > 0, detail);
    }
    for (int route = 0; route < 2; ++route) {
        const bool guest = route == 1;
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() { boot(emu); return true; };
        dbg.set_loop_driver(d);
        emu.execute_single_instruction();               // PC off 0 before the boot
        dbg.pause(a);
        const uint16_t pc_before = pc_of(emu);
        if (guest) { dbg.on_cold_boot_begin(); boot(emu); dbg.on_cold_boot_done(); }
        else       dbg.reset(a, ResetKind::Hard);
        const RunState st = dbg.state();
        check(guest ? "RC-GUEST-03" : "RC-HARD-03",
              guest ? "guest begin/done: a caller paused before the boot is paused "
                      "at PC 0x0000 of the rebuilt machine, still User{A}"
                    : "reset(Hard): a caller paused before it is paused at PC 0x0000 "
                      "of the rebuilt machine, still User{A}",
              pc_before != 0 && st.paused && st.pc == 0x0000 && pc_of(emu) == 0x0000 &&
                  st.pause_reason.kind == K::User && st.pause_reason.by == a,
              "pc_before=" + hex(pc_before) + " " + b5_state(dbg));
    }

    // ── The .nex routes. IN PLACE is the driver every loop owner registers
    //    (`emulator_apply_load`); RECONSTRUCTING is a cold boot then the same
    //    load — the Qt menu's route, CTL-15's "a load that routes to
    //    emulator_cold_boot()".
    const std::string nex = "/tmp/jnext_b5_three_kinds_" + std::to_string(::getpid()) + ".nex";
    const bool nex_ok = b5_write_nex(nex, kB5ThreeKinds);
    for (int route = 0; route < 2; ++route) {
        const bool rebuild = route == 1;
        auto make = [&](Emulator& emu, Debugger& dbg) {
            jnext::dbg::LoopDriver d;
            d.load = [&emu, rebuild, &boot](const std::string& path) {
                if (rebuild) boot(emu);
                return emulator_apply_load(emu, path, false);
            };
            dbg.set_loop_driver(d);
        };
        const char* ids[3][2] = {
            {"RC-NEX-01", "RC-NEXBOOT-01"},
            {"RC-NEX-02", "RC-NEXBOOT-02"},
            {"RC-NEX-03", "RC-NEXBOOT-03"},
        };
        {
            // All three kinds, measured before and after — the program the .nex
            // carries is the one the pre-load machine already runs.
            Emulator emu;
            EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2;
            emu.init(cfg);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            B5Counts before, after;
            B5Counts* into = &before;
            b5_subscribe_three(dbg, a, into);
            make(emu, dbg);
            load_prog(emu, kB5ThreeKinds);
            emu.run_frame();
            const bool all_before = before.exec > 0 && before.mem > 0 && before.nr > 0;
            into = &after;
            const Result r = dbg.load(a, nex);
            for (int i = 0; i < 3; ++i) emu.run_frame();
            check(ids[0][route],
                  rebuild ? "a RECONSTRUCTING load() of a real .nex: the Execute, Mem "
                            "and NextRegWrite subscriptions that fired before it all "
                            "fire on the .nex's own code afterwards"
                          : "an IN-PLACE load() of a real .nex (every loop owner's "
                            "driver): the Execute, Mem and NextRegWrite subscriptions "
                            "that fired before it all fire on the .nex's own code "
                            "afterwards",
                  nex_ok && r == Result::Ok && all_before && after.exec > 0 &&
                      after.mem > 0 && after.nr > 0,
                  "rc=" + std::to_string(static_cast<int>(r)) + " before[" +
                      b5_counts(before) + "] after[" + b5_counts(after) + "]");
        }
        {
            Emulator emu;
            EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2;
            emu.init(cfg);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            make(emu, dbg);
            load_prog(emu, { 0x00, 0x00, 0x18, 0xFE });
            emu.execute_single_instruction();           // PC 0x8001: not the entry
            dbg.pause(a);
            const Result r = dbg.load(a, nex);
            const RunState st = dbg.state();
            const B5Pos live = b5_pos(emu);
            emu.run_frame();
            check(ids[1][route],
                  rebuild ? "a RECONSTRUCTING load() of a .nex by a paused caller: "
                            "still paused, at the .nex's entry PC 0x8000, User{A}, "
                            "and it holds"
                          : "an IN-PLACE load() of a .nex by a paused caller: still "
                            "paused, at the .nex's entry PC 0x8000, User{A}, and it "
                            "holds",
                  nex_ok && r == Result::Ok && st.paused && st.pc == 0x8000 &&
                      st.pause_reason.kind == K::User && st.pause_reason.by == a &&
                      pc_of(emu) == 0x8000 && emu.clock().get() == live.cycle,
                  "rc=" + std::to_string(static_cast<int>(r)) + " " + b5_state(dbg));
        }
        {
            Emulator emu;
            EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2;
            emu.init(cfg);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            make(emu, dbg);
            load_prog(emu, { 0x00, 0x00, 0x18, 0xFE });
            const Result r = dbg.load(a, nex);
            const bool paused_after = dbg.state().paused;
            const uint64_t c = emu.clock().get();
            emu.run_frame();
            check(ids[2][route],
                  rebuild ? "a RECONSTRUCTING load() of a .nex by a running caller: "
                            "not paused, and the next frame executes"
                          : "an IN-PLACE load() of a .nex by a running caller: not "
                            "paused, and the next frame executes",
                  nex_ok && r == Result::Ok && !paused_after && !dbg.state().paused &&
                      emu.clock().get() > c,
                  "rc=" + std::to_string(static_cast<int>(r)) + " " + b5_state(dbg));
        }
    }
    std::remove(nex.c_str());
}


// ── MATCH — §9 / backend.md §7: "`matched[]` rows: a user breakpoint and a
//    transient at the same address both listed". SES-02-16 lists ONE user
//    subscription; EVT-DEL-61/62 two user ones; no row put a transient beside a
//    user subscription on one address, which is exactly DeZog's "temp beats
//    user" case the list exists for (§4.3: "every subscription that matched,
//    transient ones included").
static void b5_match_rows() {
    Emulator emu; build(emu);
    Debugger dbg(emu);
    RecListener l;
    const ClientId a = dbg.attach(client("A")).value;
    dbg.set_listener(a, &l);
    Subscription u;
    u.kind = EventKind::Execute; u.filter.lo = AFTER_CALL; u.filter.hi = AFTER_CALL;
    u.action = Action::Stop;
    const auto user = dbg.subscribe(a, u);
    dbg.pause(a);
    dbg.pump(jnext::dbg::PumpBudget{});                  // prime: paused
    dbg.run_to(a, AFTER_CALL);                           // the transient, same address
    jnext::dbg::EventId transient = jnext::dbg::EVENT_NONE;
    for (const auto& si : dbg.subscriptions(true))
        if (si.transient) transient = si.id;
    run_until_paused(emu);
    dbg.pump(jnext::dbg::PumpBudget{});
    bool has_user = false, has_transient = false, addrs_ok = true;
    size_t n = 0;
    if (l.paused.size() == 1) {
        n = l.paused[0].matched.size();
        for (const auto& h : l.paused[0].matched) {
            has_user      = has_user || h.event_id == user.value;
            has_transient = has_transient || h.event_id == transient;
            addrs_ok      = addrs_ok && h.addr == AFTER_CALL;
        }
    }
    check("MATCH-01", "a user Execute breakpoint and a run_to transient on the same "
                      "address: the Paused push's matched[] lists BOTH, by id",
          l.paused.size() == 1 && n == 2 && has_user && has_transient &&
              transient != jnext::dbg::EVENT_NONE,
          l.trail() + " matched=" + std::to_string(n));
    check("MATCH-02", "and each Hit carries the address that matched — for an Execute "
                      "hit the PC (events.h: \"The address (Mem, Execute) or port "
                      "(Port) that matched\")",
          l.paused.size() == 1 && n == 2 && addrs_ok,
          l.paused.empty() || l.paused[0].matched.empty()
              ? std::string("none")
              : "addr[0]=" + hex(l.paused[0].matched[0].addr));

    // The same fact on the OTHER consumer: a subscription stop's pause_reason
    // takes its address from the same place a Hit does, and a legacy PC
    // breakpoint's reason already reports the PC (CTL-13-02). The two
    // Breakpoint sources must not disagree about where the machine stopped.
    Emulator e2; build(e2);
    Debugger d2(e2);
    const ClientId b = d2.attach(client("B")).value;
    const auto sb = d2.subscribe(b, u);
    run_until_paused(e2);
    const RunState st = d2.state();
    check("MATCH-03", "an Execute subscription's Stop reads as Breakpoint{id} AT the "
                      "PC it matched — the address a legacy PC breakpoint's reason "
                      "carries (CTL-13-02)",
          st.paused && st.pause_reason.kind == PauseReason::Kind::Breakpoint &&
              st.pause_reason.id == sb.value && st.pause_reason.addr == AFTER_CALL,
          b5_state(d2));
}

// ── MAGIC-DETACH — CTL-13's owner decision, "pinned by a B5 row": "A remote
//    client attaching, pausing nothing and detaching must leave an `ED FF` stop
//    standing." SES-01-14/15 hand-set the magic latch (note_magic_stop +
//    pause) with the client attached BEFORE it; this is the real opcode, run by
//    the machine, and a client that arrives only after the stop.
static void b5_magic_detach_rows() {
    Emulator emu;
    b5_build(emu, { 0xED, 0xFF, 0x18, 0xFE });
    Debugger dbg(emu);
    dbg.set_magic_breakpoint(true);
    emu.run_frame();                                     // ED FF stops the machine
    const RunState before = dbg.state();
    const ClientId r =
        dbg.attach(client("Remote", jnext::dbg::ClientKind::Dzrp)).value;
    const Result d = dbg.detach(r);
    const RunState after = dbg.state();
    const B5Pos live = b5_pos(emu);
    emu.run_frame();
    check("MAGIC-DETACH-01", "a remote client that attaches AFTER a real ED FF stop, "
                             "pauses nothing and detaches leaves the stop standing: "
                             "still paused, still Magic and unowned, at the same pc, "
                             "and the next frame executes nothing",
          before.paused && before.pause_reason.kind == PauseReason::Kind::Magic &&
              d == Result::Ok && after.paused &&
              after.pause_reason.kind == PauseReason::Kind::Magic &&
              after.pause_reason.by == jnext::dbg::CLIENT_NONE &&
              after.pc == before.pc && pc_of(emu) == live.pc &&
              emu.clock().get() == live.cycle,
          "before: " + std::to_string(static_cast<int>(before.pause_reason.kind)) +
              " after: " + b5_state(dbg));
}

// ── RANGE — §9: "range rows sit on both edges and one past each" — through a
//    RUNNING machine, for every kind whose filter is a range. The EventTable
//    rows do it for Mem at table level (EVT-TBL-100/101), Execute only inside
//    and one past `hi` (EVT-TBL-FM-01/02), Copper inside and outside
//    (FM-100/101); a real transfer does it for the DMA's DESTINATION side
//    (EVT-DMA-30/31). Nothing drove a machine across Execute's, Mem's or the
//    Copper's edges, or the DMA's SOURCE side.
//
// Each fixture passes lo-1, lo, lo+1, hi, hi+1 exactly once, in order, and the
// recorder must see exactly the three in-range ones — so the "edges" row and
// the "one past" row are both read from one run, the pair that makes neither
// vacuous.
static void b5_range_rows() {
    auto edges = [](const std::vector<uint16_t>& got, const std::vector<uint16_t>& want) {
        return got == want;
    };
    auto list = [](const std::vector<uint16_t>& v) {
        std::string s;
        for (uint16_t x : v) s += (s.empty() ? "" : ",") + hex(x);
        return "[" + s + "]";
    };
    {
        // 8000..8006 NOP x7, 8007 JR $; range [0x8002, 0x8004].
        Emulator emu; b5_build(emu, { 0, 0, 0, 0, 0, 0, 0, 0x18, 0xFE });
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        std::vector<uint16_t> got;
        Subscription s;
        s.kind = EventKind::Execute; s.filter.lo = 0x8002; s.filter.hi = 0x8004;
        s.action = Action::Continue;
        s.handler = [&got](const DbgEvent& ev, Debugger&) { got.push_back(ev.pc); return Action::Continue; };
        dbg.subscribe(a, s);
        emu.run_frame();
        const bool in  = std::count(got.begin(), got.end(), 0x8002) == 1 &&
                         std::count(got.begin(), got.end(), 0x8004) == 1;
        const bool out = std::count(got.begin(), got.end(), 0x8001) == 0 &&
                         std::count(got.begin(), got.end(), 0x8005) == 0;
        check("RANGE-EXEC-01", "Execute [0x8002,0x8004] on a running machine: BOTH edges "
                               "fire, once each", in, list(got));
        check("RANGE-EXEC-02", "and one past each edge (0x8001, 0x8005) does not — the "
                               "whole delivery is exactly lo..hi",
              out && edges(got, {0x8002, 0x8003, 0x8004}), list(got));
    }
    {
        // LD A,0x5A then LD (0x9000..0x9004),A; range [0x9001, 0x9003].
        std::vector<uint8_t> p = { 0x3E, 0x5A };
        for (uint8_t lo = 0; lo < 5; ++lo) { p.push_back(0x32); p.push_back(lo); p.push_back(0x90); }
        p.push_back(0x18); p.push_back(0xFE);
        Emulator emu; b5_build(emu, p);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        std::vector<uint16_t> got;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9001; s.filter.hi = 0x9003;
        s.action = Action::Continue;
        s.handler = [&got](const DbgEvent& ev, Debugger&) { got.push_back(ev.addr); return Action::Continue; };
        dbg.subscribe(a, s);
        emu.run_frame();
        const bool in  = std::count(got.begin(), got.end(), 0x9001) == 1 &&
                         std::count(got.begin(), got.end(), 0x9003) == 1;
        const bool out = std::count(got.begin(), got.end(), 0x9000) == 0 &&
                         std::count(got.begin(), got.end(), 0x9004) == 0;
        check("RANGE-MEM-01", "Mem{Write} [0x9001,0x9003] on a running machine: BOTH "
                              "edges are delivered, once each", in, list(got));
        check("RANGE-MEM-02", "and one past each edge (0x9000, 0x9004) is not — the "
                              "whole delivery is exactly lo..hi",
              out && edges(got, {0x9001, 0x9002, 0x9003}), list(got));
    }
    {
        // Copper MOVEs at Copper PCs 0..4, then HALT; range [1, 3].
        Emulator emu; b5_build(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        std::vector<uint16_t> got;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        s.filter.lo = 1; s.filter.hi = 3;
        s.action = Action::Continue;
        s.handler = [&got](const DbgEvent& ev, Debugger&) { got.push_back(ev.copper_pc); return Action::Continue; };
        dbg.subscribe(a, s);
        copper_program(emu, { move_word(0x30, 0x10), move_word(0x30, 0x11),
                              move_word(0x30, 0x12), move_word(0x30, 0x13),
                              move_word(0x30, 0x14), HALT_WORD });
        copper_start(emu);
        emu.run_frame();
        const bool in  = std::count(got.begin(), got.end(), 1) == 1 &&
                         std::count(got.begin(), got.end(), 3) == 1;
        const bool out = std::count(got.begin(), got.end(), 0) == 0 &&
                         std::count(got.begin(), got.end(), 4) == 0;
        check("RANGE-COP-01", "Copper{Move} Copper-PC range [1,3] on a running Copper: "
                              "BOTH edges are delivered, once each",
              in && emu.nextreg().peek(0x30) == 0x14, list(got));
        check("RANGE-COP-02", "and one past each edge (Copper PC 0, 4) is not — the "
                              "whole delivery is exactly lo..hi",
              out && edges(got, {1, 2, 3}), list(got));
    }
    {
        // The DMA's SOURCE side: 0xA000..0xA007 -> 0x9000..; range [0xA002, 0xA004]
        // (the destinations are all outside it).
        Emulator emu; b5_build(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        std::vector<uint16_t> got;
        Subscription s;
        s.kind = EventKind::Dma; s.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        s.filter.lo = 0xA002; s.filter.hi = 0xA004;
        s.action = Action::Continue;
        s.handler = [&got](const DbgEvent& ev, Debugger&) { got.push_back(ev.dma_src); return Action::Continue; };
        dbg.subscribe(a, s);
        b5_prime_dma(emu, dbg, a);
        emu.run_frame();
        const bool in  = std::count(got.begin(), got.end(), 0xA002) == 1 &&
                         std::count(got.begin(), got.end(), 0xA004) == 1;
        const bool out = std::count(got.begin(), got.end(), 0xA001) == 0 &&
                         std::count(got.begin(), got.end(), 0xA005) == 0;
        check("RANGE-DMA-01", "Dma{Byte} range on the SOURCE side [0xA002,0xA004]: BOTH "
                              "edges are delivered, once each",
              in && emu.mmu().peek(0x9007) == 0xA7, list(got));
        check("RANGE-DMA-02", "and one past each edge (0xA001, 0xA005) is not — the whole "
                              "delivery is exactly lo..hi",
              out && edges(got, {0xA002, 0xA003, 0xA004}), list(got));
    }
}

// ── PL — delivery PAYLOAD fields §4.3 names that no row asserted.
static void b5_payload_rows() {
    {
        // §4.3 Port: "`source` ∈ {Cpu, Dma} ... a DMA byte to a port destination
        // is a `Port{Write}`, REQ-dsl-27". Every Port row is a CPU OUT. The
        // EVT-NR-20 fixture: one byte, 0x42, RAM -> I/O port 0x253B.
        Emulator emu; b5_build(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        emu.nextreg().select(0x16);
        emu.mmu().write(0xA000, 0x42);
        Rec port, byte;
        Subscription p;
        p.kind = EventKind::Port; p.access = Access::Write;
        p.filter.port_mask = 0xFFFF; p.filter.port_value = 0x253B;
        p.action = Action::Continue; p.handler = recorder(port);
        dbg.subscribe(a, p);
        Subscription b;
        b.kind = EventKind::Dma; b.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        b.action = Action::Continue; b.handler = recorder(byte);
        dbg.subscribe(a, b);
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
        w(0x14);                         // R1 port A = memory, inc
        w(0x28);                         // R2 port B = I/O, fixed
        w(0xAD); w(0x3B); w(0x25);       // R4 mode + port B = 0x253B
        w(0xCF); w(0x87);                // R6 LOAD, R6 ENABLE
        emu.run_frame();
        check("PL-PORT-01", "a DMA byte to an I/O port is delivered as Port{Write} with "
                            "source=Dma, the full port and the byte (REQ-dsl-27)",
              port.evs.size() == 1 && port.evs[0].source == EventSource::Dma &&
                  port.evs[0].port == 0x253B && port.evs[0].value == 0x42 &&
                  port.evs[0].access == Access::Write,
              port.evs.empty() ? "n=0"
                               : "n=" + std::to_string(port.evs.size()) + " source=" +
                                     std::to_string(static_cast<int>(port.evs[0].source)) +
                                     " port=" + hex(port.evs[0].port));
        check("PL-DMA-01", "and its Dma{Byte} flags the I/O DESTINATION (and not the "
                           "memory source), naming the port and the byte",
              byte.evs.size() == 1 && byte.evs[0].dma_is_io_dst &&
                  !byte.evs[0].dma_is_io_src && byte.evs[0].dma_dst == 0x253B &&
                  byte.evs[0].value == 0x42,
              "n=" + std::to_string(byte.evs.size()));
    }
    // §4.3 Dma: "`Start`/`End`: src, dst, length, direction, mode, bytes" —
    // direction and mode were never read. R0 bit 2 is the A->B flag and R4 bits
    // 6:5 the transfer mode (00 byte, 01 continuous, 10 burst — dma.vhd's
    // R4_mode_s), so two fixtures that differ in both tell a real read from a
    // constant.
    auto start_end = [](uint8_t r0, uint8_t r4, uint8_t& dir, uint8_t& mode,
                        uint8_t& end_dir, uint8_t& end_mode, size_t& n_start) {
        Emulator emu; b5_build(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        for (int i = 0; i < 4; ++i) {
            emu.mmu().write(static_cast<uint16_t>(0xA000 + i), 0x11);
            emu.mmu().write(static_cast<uint16_t>(0x9000 + i), 0x22);
        }
        Rec st, en;
        Subscription s; s.kind = EventKind::Dma;
        s.filter.dma_kind = jnext::dbg::DmaEventKind::Start;
        s.action = Action::Continue; s.handler = recorder(st);
        dbg.subscribe(a, s);
        Subscription e = s; e.filter.dma_kind = jnext::dbg::DmaEventKind::End;
        e.handler = recorder(en);
        dbg.subscribe(a, e);
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 (direction in bit 2) + port A 0xA000 + length 4; R1/R2 memory, inc;
        // R4 (mode in bits 6:5) + port B 0x9000; R6 LOAD; R6 ENABLE.
        w(r0); w(0x00); w(0xA0); w(0x04); w(0x00);
        w(0x14); w(0x10);
        w(r4); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        n_start  = st.evs.size();
        dir      = st.evs.empty() ? 0xEE : st.evs[0].dma_direction;
        mode     = st.evs.empty() ? 0xEE : st.evs[0].dma_mode;
        end_dir  = en.evs.empty() ? 0xEE : en.evs[0].dma_direction;
        end_mode = en.evs.empty() ? 0xEE : en.evs[0].dma_mode;
    };
    {
        uint8_t dir, mode, ed, em; size_t n;
        start_end(0x7D, 0xAD, dir, mode, ed, em, n);      // A->B, continuous
        check("PL-DMA-02", "an A->B continuous block: Start and End carry direction 1 "
                           "(R0 bit 2) and mode 1 (R4 bits 6:5 = continuous)",
              n >= 1 && dir == 1 && mode == 1 && ed == 1 && em == 1,
              "n=" + std::to_string(n) + " start " + std::to_string(dir) + "/" +
                  std::to_string(mode) + " end " + std::to_string(ed) + "/" +
                  std::to_string(em));
    }
    {
        uint8_t dir, mode, ed, em; size_t n;
        start_end(0x79, 0xCD, dir, mode, ed, em, n);      // B->A, burst
        // End too, and HERE rather than only in PL-DMA-02: there direction and
        // mode are both 1, so an End that swapped the two fields read the same
        // (mutation PL-END-SWAP survived until this fixture asserted it).
        check("PL-DMA-03", "a B->A burst block: Start AND End carry direction 0 (R0 "
                           "bit 2 clear) and mode 2 (R4 bits 6:5 = burst)",
              n >= 1 && dir == 0 && mode == 2 && ed == 0 && em == 2,
              "n=" + std::to_string(n) + " start " + std::to_string(dir) + "/" +
                  std::to_string(mode) + " end " + std::to_string(ed) + "/" +
                  std::to_string(em));
    }
    {
        // §4.3 Cycle: payload `cycle`. WK-CYCLE pins where the machine STOPS;
        // this pins what the EVENT says: the boundary's own clock, which on the
        // grid is the target itself, and the PC it stood at.
        Emulator emu; b5_build(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("B5")).value;
        const uint64_t target = emu.clock().get() + 40 * B5_JR_CYCLES;
        Rec rec;
        Subscription s;
        s.kind = EventKind::Cycle; s.filter.cycle = target;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(a, s);
        emu.run_frame();
        check("PL-CYCLE-01", "a Cycle event carries the cycle of the boundary that "
                             "delivered it — the target itself when the target is a "
                             "boundary — and the PC standing there",
              rec.evs.size() == 1 && rec.evs[0].cycle == target && rec.evs[0].pc == PROG,
              rec.evs.empty() ? "n=0"
                              : "cycle=" + std::to_string(rec.evs[0].cycle) +
                                    " target=" + std::to_string(target) +
                                    " pc=" + hex(rec.evs[0].pc));
    }
}


// ── DETACH — per-client state dies with the client (GH #276 B5, carried from
//    B4's reviews: "detach leaves per-client state behind"). Client ids are
//    never reused, so a record kept for a detached id is kept for ever. Swept by
//    HAZARD — every container keyed by a `ClientId` outside the client row: the
//    capture-failure record `flush_captures()` reports, and the per-client event
//    switch `set_client_enabled()` stores. Each row carries its own control: the
//    same record for a client that stays attached is still there.
static void b5_detach_rows() {
    using jnext::dbg::ScreenshotFormat;
    using jnext::dbg::LAYER_MASK_ALL;
    const std::string bad = "/nonexistent-dir/b5/x.png";
    {
        // A failure recorded WHILE attached, never flushed, then the detach.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        dbg.screenshot(a, bad, LAYER_MASK_ALL, ScreenshotFormat::Png);
        dbg.screenshot(b, bad, LAYER_MASK_ALL, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});           // both writes fail: two records
        dbg.detach(a);
        const Result fa = dbg.flush_captures(a);
        const Result fb = dbg.flush_captures(b);
        check("DETACH-01", "a capture-failure record made while its client was attached "
                           "dies with the client's detach (flush for the gone id: Ok), "
                           "while the same record of a client still attached is there "
                           "(RefusedUnavailable)",
              fa == Result::Ok && fb == Result::RefusedUnavailable,
              std::string("gone=") + jnext::dbg::result_name(fa) +
                  " attached=" + jnext::dbg::result_name(fb));
    }
    {
        // A capture queued, its client detached, and the write failing AFTER:
        // the capture was still attempted (the error line reaches the client that
        // is still listening), but no record is created for the gone one.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener lb;
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        dbg.set_listener(b, &lb);
        dbg.screenshot(a, bad, LAYER_MASK_ALL, ScreenshotFormat::Png);
        dbg.screenshot(b, bad, LAYER_MASK_ALL, ScreenshotFormat::Png);
        dbg.detach(a);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        size_t errors = 0;
        for (const auto& lg : lb.logs)
            if (lg.first == jnext::dbg::LogLevel::Error &&
                lg.second.find("NOT written") != std::string::npos)
                ++errors;
        const Result fa = dbg.flush_captures(a);
        const Result fb = dbg.flush_captures(b);
        check("DETACH-02", "a capture whose client detached before it failed is still "
                           "attempted and logged at error (both failures reach the "
                           "listener still attached), but leaves no record for the gone "
                           "client — while the attached client's identical failure does",
              errors == 2 && fa == Result::Ok && fb == Result::RefusedUnavailable,
              "errors=" + std::to_string(errors) + " gone=" + jnext::dbg::result_name(fa) +
                  " attached=" + jnext::dbg::result_name(fb));
    }
    {
        // The prune's OTHER side: "gone" means ISSUED and detached. The loop
        // owners file their --delayed-screenshot under CLIENT_NONE, which is
        // never issued, and flush it for the exit bound (a failed write must
        // still exit non-zero); a caller may also use an id it never attached.
        // Both keep their records — a predicate of just "not attached" would
        // silently turn every failed --delayed-screenshot into a success.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId never = a + 40;                 // not issued
        dbg.screenshot(jnext::dbg::CLIENT_NONE, bad, LAYER_MASK_ALL, ScreenshotFormat::Png);
        dbg.screenshot(never, bad, LAYER_MASK_ALL, ScreenshotFormat::Png);
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        const Result fn = dbg.flush_captures(jnext::dbg::CLIENT_NONE);
        const Result fv = dbg.flush_captures(never);
        check("DETACH-04", "a failure filed under CLIENT_NONE (the loop owners' own "
                           "--delayed-screenshot) and under an id never issued is still "
                           "recorded — only an issued-and-detached id is gone",
              fn == Result::RefusedUnavailable && fv == Result::RefusedUnavailable,
              std::string("none=") + jnext::dbg::result_name(fn) +
                  " never=" + jnext::dbg::result_name(fv));
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        dbg.set_client_enabled(a, false);
        dbg.set_client_enabled(b, false);
        const bool both_off = !dbg.client_enabled(a) && !dbg.client_enabled(b);
        dbg.detach(a);
        check("DETACH-03", "a client's per-client event switch dies with its detach (the "
                           "gone id reads enabled again — nothing is stored for it), "
                           "while another client's disabled switch survives",
              both_off && dbg.client_enabled(a) && !dbg.client_enabled(b),
              std::string("gone=") + (dbg.client_enabled(a) ? "1" : "0") +
                  " attached=" + (dbg.client_enabled(b) ? "1" : "0"));

        // ...and neither record can be RE-CREATED for the gone id afterwards:
        // a subscription owned by it would stay armed with an owner no detach
        // sweeps again, and its switch would sit in the list for ever. Both
        // verbs still take an id that was never issued.
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = WATCHED; s.filter.hi = WATCHED;
        const auto gone_sub  = dbg.subscribe(a, s);
        const bool armed_by_gone = emu.debug_state().wr_watch_armed(WATCHED);
        const auto never_sub = dbg.subscribe(b + 40, s);
        check("DETACH-05", "subscribe() by a detached client is RefusedUnavailable and "
                           "arms nothing; an id never issued still subscribes",
              gone_sub.status == Result::RefusedUnavailable && !armed_by_gone &&
                  never_sub.status == Result::Ok,
              std::string("gone=") + jnext::dbg::result_name(gone_sub.status) +
                  " armed=" + (armed_by_gone ? "1" : "0") +
                  " never=" + jnext::dbg::result_name(never_sub.status));
        const Result sw = dbg.set_client_enabled(a, false);
        check("DETACH-06", "set_client_enabled() for a detached client is "
                           "RefusedUnavailable and stores nothing (it still reads "
                           "enabled)",
              sw == Result::RefusedUnavailable && dbg.client_enabled(a),
              std::string("rc=") + jnext::dbg::result_name(sw));
    }
}

// ── HOST-07 — the JNEXT_HOST_PROBE fixture (platform/host_probe.h), through the
//    real HeadlessApp. The regression rows sdl-host-probe-func and
//    qt-host-probe-func run the SAME probe in SdlApp and QtApp and read the same
//    lines; this row is the probe's own proof on the loop owner whose call sites
//    HOST-03..05 already pin, so a HOSTPROBE line that reads right means what the
//    regression rows take it to mean.
static void b5_host_probe_rows() {
    auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(512);
    Log::platform()->sinks().push_back(ring);
    ::setenv("JNEXT_HOST_PROBE", "1", 1);
    bool ok = false, armed_off = true;
    {
        {
            // Unset: nothing is attached — the probe is zero-cost off.
            ::unsetenv("JNEXT_HOST_PROBE");
            HeadlessApp off;
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K;
            off.set_config(cfg);
            off.init(0, nullptr);
            armed_off = off.debugger().attached();
            off.shutdown();
            ::setenv("JNEXT_HOST_PROBE", "1", 1);
        }
        EmulatorConfig cfg; cfg.type = MachineType::ZX48K;
        HeadlessApp app;
        app.set_config(cfg);
        ok = app.init(0, nullptr);
        app.set_delayed_exit(80);
        app.run();
        app.shutdown();
    }                                                   // ~HeadlessApp: the end line
    ::unsetenv("JNEXT_HOST_PROBE");
    bool guest = false, reset_ok = false, end_two = false;
    std::string seen;
    for (const auto& l : ring->last_formatted()) {
        if (l.find("HOSTPROBE") == std::string::npos) continue;
        seen += "|" + l.substr(l.find("HOSTPROBE"));
        if (l.find("HOSTPROBE guest-boot: reset=1 paused=1 owner=probe") != std::string::npos)
            guest = true;
        if (l.find("HOSTPROBE reset(Hard) -> ok") != std::string::npos) reset_ok = true;
        if (l.find("HOSTPROBE end:") != std::string::npos &&
            l.find("resets=2") != std::string::npos)
            end_two = true;
    }
    Log::platform()->sinks().pop_back();
    check("HOST-07", "JNEXT_HOST_PROBE through the real HeadlessApp: pumped (it ran at "
                     "all), the guest cold boot bracketed (Reset{Hard} pushed, the "
                     "probe's pause back and still its own), the driver registered "
                     "(reset(Hard) Ok), two hard resets in all — and with the variable "
                     "unset nothing is attached",
          ok && !armed_off && guest && reset_ok && end_two, seen);
}

// ── HOST-08 — JNEXT_HOST_PROBE=order (GH #278 WP2), through the real
//    HeadlessApp: the probe's ORDER script reads right on a loop owner already
//    known to poll the guest hard reset before its pump (HeadlessApp::run()).
//    The regression row qt-host-order-func runs the same script in QtApp, whose
//    poll WP2 moved there; this row is what makes its "guest-before-client=1"
//    and "resets=2" mean what that row takes them to mean.
static void q_wp2_host_order_rows() {
    auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(512);
    Log::platform()->sinks().push_back(ring);
    ::setenv("JNEXT_HOST_PROBE", "order", 1);
    bool ok = false;
    {
        EmulatorConfig cfg; cfg.type = MachineType::ZX48K;
        HeadlessApp app;
        app.set_config(cfg);
        ok = app.init(0, nullptr);
        app.set_delayed_exit(80);
        app.run();
        app.shutdown();
    }
    ::unsetenv("JNEXT_HOST_PROBE");
    bool armed = false, first = false, total = false;
    std::string seen;
    for (const auto& l : ring->last_formatted()) {
        if (l.find("HOSTPROBE") == std::string::npos) continue;
        seen += "|" + l.substr(l.find("HOSTPROBE"));
        if (l.find(", order)") != std::string::npos) armed = true;
        if (l.find("HOSTPROBE order: guest-before-client=1 client=ok") != std::string::npos)
            first = true;
        if (l.find("HOSTPROBE order: resets=2") != std::string::npos) total = true;
    }
    Log::platform()->sinks().pop_back();
    check("HOST-08", "JNEXT_HOST_PROBE=order through the real HeadlessApp: a guest hard "
                     "reset raised inside the frames is performed before the pump, so "
                     "the client's reset(Hard) in that pump comes second — two resets, "
                     "guest first",
          ok && armed && first && total, seen);
}

// ═══════════════════════════════════════════════════════════════════════════
// GH #278 WP3 — the rewind verbs as the Qt window drives them (CTL-09/10,
// ST-03), B3 obligation 3, and the trace export (INS-13). A contiguous block,
// kept apart from the parallel WP4d work in this file.
// ═══════════════════════════════════════════════════════════════════════════

/// A 48K machine with an `frames`-slot rewind ring and the trace on, running a
/// counter loop (8000 INC HL / 8001 JR 8000) so every frame's snapshot holds a
/// different machine.
static void q_wp3_ring_machine(Emulator& emu, int frames) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = frames;
    emu.init(cfg);
    load_prog(emu, { 0x23, 0x18, 0xFD });
    Z80Registers r = emu.cpu().get_registers();
    r.HL = 0;
    emu.cpu().set_registers(r);
    emu.trace_log().set_enabled(true);
}

/// Flip the 'mmu' sentinel (ordinal 2) in ring slot `i` — rewind_test's
/// SENT-CHAIN idiom: a restore of that slot tears the machine at 'mmu'.
static bool q_wp3_tear_slot(Emulator& emu, size_t i) {
    RewindBuffer* rb = emu.rewind_buffer();
    const uint32_t want = Emulator::kStateSentinelMagic ^ 2u;
    uint8_t* d = rb->slot_data_for_test(i);
    for (size_t off = 0; off + 4 <= rb->snapshot_bytes(); ++off) {
        uint32_t v;
        std::memcpy(&v, d + off, 4);
        if (v == want) { d[off] ^= 0xFF; return true; }
    }
    return false;
}

static void q_wp3_rewind_rows() {
    // ── The classification: RefusedCorrupt only when a restore tore the
    //    machine, RefusedUnavailable for every benign refusal ────────────────
    {
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 4; ++i) emu.run_frame();
        dbg.pause(a);
        emu.trace_log().set_enabled(false);
        const uint64_t gen = emu.state_error_generation();
        const Result r = dbg.step_back(a, 1);
        check("CTL-09-02", "step_back() with the trace OFF is the benign "
                           "RefusedUnavailable, not RefusedCorrupt, and latches nothing",
              r == Result::RefusedUnavailable && emu.state_error_generation() == gen &&
                  !dbg.resume_blocked_by_corruption().has_value(),
              std::string("rc=") + jnext::dbg::result_name(r));
        emu.trace_log().set_enabled(true);
        dbg.trace_clear();
        const Result r2 = dbg.step_back(a, 1);   // an empty trace, the same class
        check("CTL-09-03", "and with the trace ON but EMPTY, the same",
              r2 == Result::RefusedUnavailable,
              std::string("rc=") + jnext::dbg::result_name(r2));
    }
    {
        // A ring with a GAP: snapshotting paused for two frames, so frames
        // inside [oldest, newest] exist that have no slot. The Emulator returns
        // the same `false` for "no such slot" as for a torn restore.
        Emulator emu;
        q_wp3_ring_machine(emu, 20);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 3; ++i) emu.run_frame();
        dbg.set_rewind_enabled(false);
        for (int i = 0; i < 2; ++i) emu.run_frame();
        dbg.set_rewind_enabled(true);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        dbg.pause(a);
        const auto rr = dbg.rewind_range();
        const uint32_t hole = rr.oldest_frame + 3;
        const uint64_t gen = emu.state_error_generation();
        const uint64_t at  = dbg.time().master_cycle;
        const Result r = dbg.rewind_to_frame(a, hole);
        check("CTL-10-05", "rewind_to_frame() to a frame INSIDE the ring's range with "
                           "no slot (a gap) is RefusedUnavailable, not RefusedCorrupt, "
                           "and leaves the machine where it was",
              rr.depth == 6 && r == Result::RefusedUnavailable &&
                  emu.state_error_generation() == gen && dbg.time().master_cycle == at,
              "depth=" + std::to_string(rr.depth) + " frames " +
                  std::to_string(rr.oldest_frame) + ".." + std::to_string(rr.newest_frame) +
                  " rc=" + jnext::dbg::result_name(r));
    }
    {
        // A TORN restore IS RefusedCorrupt — for both verbs — and latches the
        // incident the Qt window's "Rewind Failed" modal names. The newest slot
        // stays intact: Emulator::rewind_to_frame() restores it first.
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 5; ++i) emu.run_frame();
        dbg.pause(a);
        RewindBuffer* rb = emu.rewind_buffer();
        bool torn = true;
        for (size_t i = 0; i + 1 < rb->depth(); ++i) torn = q_wp3_tear_slot(emu, i) && torn;
        const auto rr = dbg.rewind_range();
        const Result r = dbg.rewind_to_frame(a, rr.oldest_frame);
        const auto inc = dbg.resume_blocked_by_corruption();
        check("CTL-10-06", "a TORN restore in rewind_to_frame() is RefusedCorrupt, "
                           "with the incident naming the subsystem",
              torn && r == Result::RefusedCorrupt && inc.has_value() &&
                  inc->subsystem == "mmu",
              std::string("rc=") + jnext::dbg::result_name(r));

        // RECOVERY: the machine is corrupt and nothing acknowledged it — a
        // rewind to the INTACT newest slot is not gated on that (CTL-11 is for
        // executing a torn machine; a rewind replaces it), it succeeds, and the
        // successful restore clears the corruption.
        const Result back = dbg.rewind_to_frame(a, rr.newest_frame);
        check("CTL-10-07", "a rewind from a CORRUPT machine to an intact frame is not "
                           "refused: it succeeds and the corruption is gone",
              back == Result::Ok && !dbg.resume_blocked_by_corruption().has_value() &&
                  emu.last_state_error().empty() && dbg.time().frame == rr.newest_frame,
              std::string("rc=") + jnext::dbg::result_name(back) + " err='" +
                  emu.last_state_error() + "'");
    }
    {
        // The same recovery through step_back(): the machine is torn by a
        // failed rewind, and a step back — whose target lies in the newest,
        // intact frame — is not refused for it and heals it.
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 5; ++i) emu.run_frame();
        dbg.pause(a);
        RewindBuffer* rb = emu.rewind_buffer();
        bool torn = true;
        for (size_t i = 0; i + 1 < rb->depth(); ++i) torn = q_wp3_tear_slot(emu, i) && torn;
        const Result failed = dbg.rewind_to_frame(a, dbg.rewind_range().oldest_frame);
        const bool corrupt = dbg.resume_blocked_by_corruption().has_value();
        const Result back = dbg.step_back(a, 1);
        check("CTL-09-05", "a step_back() from a CORRUPT machine is not refused: it "
                           "restores the intact newest frame and the corruption is gone",
              torn && failed == Result::RefusedCorrupt && corrupt && back == Result::Ok &&
                  !dbg.resume_blocked_by_corruption().has_value() &&
                  emu.last_state_error().empty(),
              std::string("rc=") + jnext::dbg::result_name(back));
    }
    {
        // REQ-qt-09d — RewindRange::at_restored_frame_start (owner approval
        // 2026-09-29): true only on a frame start a ring restore landed on.
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 4; ++i) emu.run_frame();
        const bool ordinary = !dbg.rewind_range().at_restored_frame_start &&
                              dbg.at_frame_boundary();
        check("ST-03-15", "at an ordinary frame boundary it is false",
              ordinary);
        dbg.pause(a);
        const auto rr = dbg.rewind_range();
        const Result r = dbg.rewind_to_frame(a, rr.oldest_frame + 1);
        const bool landed = r == Result::Ok && dbg.rewind_range().at_restored_frame_start &&
                            dbg.at_frame_boundary() &&
                            dbg.time().frame == rr.oldest_frame + 1;
        check("ST-03-11", "right after rewind_to_frame() lands on a frame start it is "
                          "TRUE",
              landed, std::string("rc=") + jnext::dbg::result_name(r));
        dbg.step_into(a);                          // the frame begins running
        check("ST-03-12", "and false once the machine runs on (one instruction)",
              !dbg.rewind_range().at_restored_frame_start);
        dbg.rewind_to_frame(a, rr.oldest_frame + 1);
        const bool again = dbg.rewind_range().at_restored_frame_start;
        const Result sb = dbg.step_back(a, 1);
        check("ST-03-13", "false after step_back(): its replay begins the frame it "
                          "restores",
              again && sb == Result::Ok && !dbg.rewind_range().at_restored_frame_start,
              std::string("rc=") + jnext::dbg::result_name(sb));
        // A plain state load of a snapshot SAVED at an ordinary boundary.
        dbg.run(a);
        emu.run_frame();
        const auto bytes = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        dbg.pause(a);
        dbg.rewind_to_frame(a, dbg.rewind_range().oldest_frame);
        const bool before_load = dbg.rewind_range().at_restored_frame_start;
        const Result lr = bytes.status == Result::Ok
                              ? dbg.load_state_bytes(a, bytes.value.data(), bytes.value.size())
                              : bytes.status;
        check("ST-03-14", "and false after a plain load_state_bytes(), even from a "
                          "restored frame start",
              before_load && lr == Result::Ok &&
                  !dbg.rewind_range().at_restored_frame_start,
              std::string("rc=") + jnext::dbg::result_name(lr));
    }
    {
        // A torn NEWEST slot is only that frame's problem: rewinding to another
        // frame restores that frame alone (it used to restore the newest first,
        // unconditionally, and fail every rewind on it).
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 4; ++i) emu.run_frame();
        dbg.pause(a);
        RewindBuffer* rb = emu.rewind_buffer();
        const bool torn = q_wp3_tear_slot(emu, rb->depth() - 1);
        const auto rr = dbg.rewind_range();
        const Result r = dbg.rewind_to_frame(a, rr.oldest_frame);
        check("CTL-10-08", "with only the NEWEST slot torn, rewind_to_frame() to the "
                           "oldest succeeds and latches no corruption",
              torn && r == Result::Ok && dbg.time().frame == rr.oldest_frame &&
                  !dbg.resume_blocked_by_corruption().has_value(),
              std::string("rc=") + jnext::dbg::result_name(r));
    }
    {
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 4; ++i) emu.run_frame();
        dbg.pause(a);
        RewindBuffer* rb = emu.rewind_buffer();
        bool torn = true;
        for (size_t i = 0; i < rb->depth(); ++i) torn = q_wp3_tear_slot(emu, i) && torn;
        const Result r = dbg.step_back(a, 1);
        const auto inc = dbg.resume_blocked_by_corruption();
        check("CTL-09-04", "a TORN restore in step_back() is RefusedCorrupt, with the "
                           "incident naming the subsystem",
              torn && r == Result::RefusedCorrupt && inc.has_value() &&
                  inc->subsystem == "mmu",
              std::string("rc=") + jnext::dbg::result_name(r));
    }
    {
        // WP3 review item 1 — an RZX refusal is REFUSED, AND SAID. The backend
        // decides it before `Emulator::step_back()` / `rewind_to_frame()` run, so
        // their own logged refusal (`Emulator::rzx_blocks_rewind()`) is never
        // reached; the verbs must log it themselves, with the same words, for
        // every client. Read off the live `emulator` logger: a claim that
        // something is logged is only worth what reading the log proves.
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(512);
        Log::emulator()->sinks().push_back(ring);
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        for (int i = 0; i < 4; ++i) emu.run_frame();
        dbg.pause(a);
        emu.rzx_player().start(RzxRecording{});
        auto said = [&](const std::string& what) {
            for (const auto& l : ring->last_formatted())
                if (l.find(what + ": not while an RZX recording is playing (stop it first)") !=
                    std::string::npos)
                    return true;
            return false;
        };
        // The GREYING query asks the same question on every tick and must stay
        // quiet: only a refused VERB is an event worth a line.
        const size_t lines_before = ring->last_formatted().size();
        const auto blocked = dbg.rewind_blocked();
        const bool quiet_query = blocked.has_value() && *blocked == Result::RefusedRzx &&
                                 ring->last_formatted().size() == lines_before &&
                                 lines_before < 512;
        const Result rs = dbg.step_back(a, 1);
        check("CTL-09-06", "step_back() refused for an RZX playback is RefusedRzx AND "
                           "logs why, at error level, as the Emulator always did — "
                           "while the rewind_blocked() greying query stays silent",
              quiet_query && rs == Result::RefusedRzx && said("step_back"),
              std::string("rc=") + jnext::dbg::result_name(rs));
        const Result rf = dbg.rewind_to_frame(a, dbg.rewind_range().oldest_frame);
        check("CTL-10-09", "and rewind_to_frame() the same",
              rf == Result::RefusedRzx && said("rewind_to_frame"),
              std::string("rc=") + jnext::dbg::result_name(rf));
        emu.rzx_player().stop();
        auto& sinks = Log::emulator()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), ring), sinks.end());
    }
    {
        // ST-03 — 0 frees an EXISTING ring; a later non-zero resize creates a
        // fresh one.
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        const bool had = dbg.rewind_range().depth == 3;
        const Result r0 = dbg.resize_rewind_buffer(0);
        const auto freed = dbg.rewind_range();
        const bool blocked = dbg.rewind_blocked().has_value() &&
                             *dbg.rewind_blocked() == Result::RefusedUnavailable;
        check("ST-03-09", "resize_rewind_buffer(0) FREES an existing ring: capacity "
                          "and depth 0, rewinds refused as unavailable",
              had && r0 == Result::Ok && freed.capacity == 0 && freed.depth == 0 &&
                  !dbg.rewind_enabled() && blocked);
        const Result r5 = dbg.resize_rewind_buffer(5);
        for (int i = 0; i < 2; ++i) emu.run_frame();
        const auto again = dbg.rewind_range();
        check("ST-03-10", "and a later non-zero resize creates a fresh ring that "
                          "records again",
              r5 == Result::Ok && again.capacity == 5 && again.depth == 2 &&
                  dbg.rewind_enabled() && !dbg.rewind_blocked().has_value(),
              "capacity=" + std::to_string(again.capacity) +
                  " depth=" + std::to_string(again.depth));
    }

    // ── B3 obligation 3: a client's rewind leaves nothing armed behind it ──
    {
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("remote", jnext::dbg::ClientKind::Dzrp)).value;
        for (int i = 0; i < 4; ++i) emu.run_frame();
        dbg.pause(a);
        const Result r = dbg.step_back(a, 1);
        const bool stepped = r == Result::Ok && dbg.state().paused;
        dbg.detach(a);
        check("OBL3-01", "a remote client's step_back(), then its detach: the machine "
                         "is neither armed nor attached, the raster walk is off, and "
                         "it runs (the pause was the client's)",
              stepped && !dbg.armed() && !dbg.attached() && !dbg.live_raster() &&
                  !emu.debug_state().raster_live() && !dbg.state().paused,
              std::string("rc=") + jnext::dbg::result_name(r) +
                  " armed=" + (dbg.armed() ? "1" : "0"));
    }
    {
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("remote", jnext::dbg::ClientKind::Dzrp)).value;
        for (int i = 0; i < 4; ++i) emu.run_frame();
        dbg.pause(a);
        const Result r = dbg.rewind_to_frame(a, dbg.rewind_range().oldest_frame);
        const bool rewound = r == Result::Ok && dbg.state().paused;
        dbg.detach(a);
        check("OBL3-02", "and the same for rewind_to_frame()",
              rewound && !dbg.armed() && !dbg.attached() &&
                  !emu.debug_state().raster_live() && !dbg.state().paused,
              std::string("rc=") + jnext::dbg::result_name(r) +
                  " armed=" + (dbg.armed() ? "1" : "0"));
    }
    {
        // The replay still STOPS at its target with nothing attached at all:
        // it is armed for the replay loop alone (DebugState::ReplayArmScope) —
        // the arm it used to borrow from the Qt window's bit and keep.
        Emulator emu;
        q_wp3_ring_machine(emu, 10);
        emu.run_frame();
        emu.run_frame();
        const uint64_t mid = emu.current_frame_cycle() +
                             emu.timing().master_cycles_per_frame / 3;
        while (emu.clock().get() < mid) emu.execute_single_instruction();
        const size_t n = emu.trace_log().size();
        // step_back(1) undoes the LAST instruction: it lands on trace[size-1].
        const uint64_t want = n >= 1 ? emu.trace_log().at(n - 1).cycle : 0;
        const uint16_t want_pc = n >= 1 ? emu.trace_log().at(n - 1).pc : 0;
        const bool unarmed_before = !emu.debug_state().armed();
        const bool ok = emu.step_back(1);
        check("OBL3-03", "with NOTHING attached, step_back() still lands on its target "
                         "instruction, and leaves the machine unarmed",
              unarmed_before && ok && emu.clock().get() == want && pc_of(emu) == want_pc &&
                  !emu.debug_state().armed() && !emu.debug_state().attached(),
              "cycle " + std::to_string(emu.clock().get()) + " want " +
                  std::to_string(want) + " pc " + hex(pc_of(emu)) + " want " + hex(want_pc));
    }
}

/// INS-13 — the export writes EVERY field of the entry (GH #278 WP3): the ones
/// GH #276 B4 added (I, R, IM, IFF1/IFF2, the word at SP, the eight MMU pages)
/// were recorded and never written.
static void q_wp3_trace_export_rows() {
    Emulator emu;
    build(emu);
    Debugger dbg(emu);
    Z80Registers r = emu.cpu().get_registers();
    r.AF = 0x12D5; r.BC = 0x3456; r.DE = 0x789A; r.HL = 0xBCDE;
    r.AF2 = 0x1111; r.BC2 = 0x2222; r.DE2 = 0x3333; r.HL2 = 0x4444;
    r.IX = 0x5555; r.IY = 0x6666;
    r.I = 0x3F; r.R = 0x05; r.IM = 1; r.IFF1 = 0; r.IFF2 = 1;   // no INT taken
    emu.cpu().set_registers(r);
    emu.mmu().write(TEST_SP, 0xCD);
    emu.mmu().write(TEST_SP + 1, 0xAB);
    dbg.set_trace_enabled(true);
    dbg.trace_clear();
    emu.execute_single_instruction();              // 8000 NOP
    const TraceEntry e = emu.trace_log().at(0);
    const std::string path = "/tmp/jnext_q_wp3_trace_" + std::to_string(::getpid()) + ".txt";
    const Result rc = dbg.trace_export(path);
    std::string line;
    {
        std::ifstream f(path);
        std::getline(f, line);
    }
    std::remove(path.c_str());
    char want[320];
    std::snprintf(want, sizeof(want),
        "%012llu  $8000  AF=12D5 BC=3456 DE=789A HL=BCDE"
        "  AF'=1111 BC'=2222 DE'=3333 HL'=4444"
        "  IX=5555 IY=6666 SP=FF00"
        "  (SP)=ABCD I=3F R=%02X IM1 IFF1=0 IFF2=1"
        "  MMU=%02X %02X %02X %02X %02X %02X %02X %02X  [SZ-H-P-C]  00",
        static_cast<unsigned long long>(e.cycle), e.r,
        e.mmu[0], e.mmu[1], e.mmu[2], e.mmu[3], e.mmu[4], e.mmu[5], e.mmu[6], e.mmu[7]);
    check("INS-13-14", "trace_export() writes every TraceEntry field — the word at SP, "
                       "I, R, IM, IFF1, IFF2 and the eight MMU pages included — in "
                       "the documented column order",
          rc == Result::Ok && e.sp_word == 0xABCD && e.i == 0x3F && line == want,
          "got  '" + line + "'\nwant '" + want + "'");
}

// ═══════════════════════════════════════════════════════════════════════════
// GH #278 WP4c — REQ-qt-32's non-arming OBSERVER client, the master switch's
// legacy mirror across a cold boot, and the magic-breakpoint hold that replaced
// `DebugState::active()`. A contiguous block, kept apart from the parallel WP4d
// work in this file.
// ═══════════════════════════════════════════════════════════════════════════

/// A program with no CALL: six NOPs, then JR $ at PARK. It needs nothing but
/// PROG.., so it survives being reloaded into a rebuilt machine whose RAM the
/// reconstruct wiped.
static const std::vector<uint8_t> kQ4cNops = { 0, 0, 0, 0, 0, 0, 0x18, 0xFE };

static jnext::dbg::ClientInfo q4c_observer(const char* name) {
    jnext::dbg::ClientInfo ci = client(name, jnext::dbg::ClientKind::Gui);
    ci.observer = true;
    return ci;
}

static Subscription q4c_exec_at(uint16_t addr) {
    Subscription s;
    s.kind      = EventKind::Execute;
    s.filter.lo = addr;
    s.filter.hi = addr;
    s.action    = Action::Stop;
    return s;
}

/// PC back to PROG, SP back to TEST_SP, interrupts off — the machine re-runs the
/// program from its start.
static void q4c_restart(Emulator& emu) {
    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = TEST_SP;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

static void q4c_observer_rows() {
    // OBS-01 — the flag's whole meaning: it counts toward no arm bit.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        const bool alone = !dbg.armed() && !dbg.attached() &&
                           !emu.debug_state().clients_attached() &&
                           !emu.debug_state().armed();
        const ClientId w =
            dbg.attach(client("window", jnext::dbg::ClientKind::Gui)).value;
        const bool with_w = dbg.armed() && dbg.attached();
        dbg.detach(w);
        const bool after_w = !dbg.armed() && !dbg.attached();
        check("OBS-01", "an observer attach counts in neither armed() nor attached() — "
                        "alone, next to an arming client, and after that client leaves",
              o != jnext::dbg::CLIENT_NONE && alone && with_w && after_w,
              "alone=" + std::to_string(alone) + " with_w=" + std::to_string(with_w) +
                  " after_w=" + std::to_string(after_w));
    }

    // OBS-02..04 — its subscriptions fire ONLY while something else arms the
    // machine: not at all alone, by another client's attach, by
    // --persistent-breakpoints alone. The stop is the observer's.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        const auto id = dbg.subscribe(o, q4c_exec_at(AFTER_CALL));
        emu.run_frame();
        check("OBS-02", "an observer's Execute subscription on an otherwise unarmed "
                        "machine does not fire: the machine runs past it to the park",
              id.status == Result::Ok && !emu.debug_state().paused() &&
                  pc_of(emu) == PARK,
              "pc=" + hex(pc_of(emu)));

        q4c_restart(emu);
        const ClientId w =
            dbg.attach(client("window", jnext::dbg::ClientKind::Gui)).value;
        run_until_paused(emu);
        const RunState st = dbg.state();
        check("OBS-03", "armed by ANOTHER client's attach, it stops the machine on its "
                        "address, and the stop is the observer's",
              st.paused && pc_of(emu) == AFTER_CALL &&
                  st.pause_reason.kind == PauseReason::Kind::Breakpoint &&
                  st.pause_reason.id == id.value && st.pause_reason.by == o,
              "pc=" + hex(pc_of(emu)) + " by=" + std::to_string(st.pause_reason.by));

        q4c_restart(emu);
        dbg.set_persistent_breakpoints(true);
        dbg.detach(w);                         // not w's pause: it stays paused
        const bool armed_by_flag_only = dbg.armed() && !dbg.attached();
        dbg.run(o);
        run_until_paused(emu);
        check("OBS-04", "armed by --persistent-breakpoints ALONE (no arming client), it "
                        "stops the machine on its address",
              armed_by_flag_only && dbg.state().paused && pc_of(emu) == AFTER_CALL &&
                  dbg.state().pause_reason.id == id.value,
              "pc=" + hex(pc_of(emu)));
    }

    // OBS-05 — its detach removes its subscriptions (SES-01).
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        dbg.subscribe(o, q4c_exec_at(AFTER_CALL));
        dbg.attach(client("window", jnext::dbg::ClientKind::Gui));
        dbg.detach(o);
        const bool gone = dbg.subscriptions(true).empty();
        emu.run_frame();
        check("OBS-05", "the observer's detach removes its subscriptions: none listed, "
                        "and the armed machine runs past the address to the park",
              gone && !emu.debug_state().paused() && pc_of(emu) == PARK,
              "gone=" + std::to_string(gone) + " pc=" + hex(pc_of(emu)));
    }

    // OBS-06 — its subscriptions survive a cold boot via CTL-12 rule 2: the
    // observer is a live client, so the backend keeps and re-applies them.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        const auto id = dbg.subscribe(o, q4c_exec_at(PROG + 3));
        const ClientId w =
            dbg.attach(client("window", jnext::dbg::ClientKind::Gui)).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        const bool booted = dbg.reset(w, ResetKind::Hard) == Result::Ok;
        load_prog(emu, kQ4cNops);
        const auto subs   = dbg.subscriptions(false);
        const bool listed = subs.size() == 1 && subs[0].id == id.value &&
                            subs[0].owner == o && subs[0].live;
        const bool armed_by_w = dbg.armed() && dbg.attached();   // w alone arms
        run_until_paused(emu);
        check("OBS-06", "an observer's subscription survives a hard reset (rule 2): "
                        "still listed as its own and live, and it stops the rebuilt "
                        "machine on its address",
              booted && listed && armed_by_w && dbg.state().paused &&
                  pc_of(emu) == PROG + 3 && dbg.state().pause_reason.id == id.value,
              "booted=" + std::to_string(booted) + " listed=" + std::to_string(listed) +
                  " pc=" + hex(pc_of(emu)));
    }

    // OBS-07/08 — SES-01's own-pause rule applies to it like any client: its
    // detach releases ONLY a pause that is its own.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        const ClientId w =
            dbg.attach(client("window", jnext::dbg::ClientKind::Gui)).value;
        dbg.pause(w);
        dbg.detach(o);
        check("OBS-07", "another client's pause survives the observer's detach",
              dbg.state().paused && dbg.state().pause_reason.by == w);
    }
    {
        // (a) a stop on ITS subscription, (b) its OWN pause() verb.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        dbg.subscribe(o, q4c_exec_at(AFTER_CALL));
        dbg.attach(client("window", jnext::dbg::ClientKind::Gui));
        run_until_paused(emu);
        const bool stopped_by_o = dbg.state().paused && dbg.state().pause_reason.by == o;
        dbg.detach(o);
        const bool released_a = !dbg.state().paused;

        o = dbg.attach(q4c_observer("Qt GUI")).value;
        dbg.pause(o);
        const bool paused_by_o = dbg.state().paused && dbg.state().pause_reason.by == o;
        dbg.detach(o);
        const bool released_b = !dbg.state().paused;
        check("OBS-08", "the observer's detach releases a pause that IS its own — a stop "
                        "on its subscription, and its own pause()",
              stopped_by_o && released_a && paused_by_o && released_b,
              "a=" + std::to_string(stopped_by_o) + std::to_string(released_a) +
                  " b=" + std::to_string(paused_by_o) + std::to_string(released_b));
    }

    // OBS-09 — its live-raster request is honoured (a render hint, not an arm).
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        dbg.set_live_raster(o, true);
        check("OBS-09", "an observer's live-raster request is honoured, and still arms "
                        "nothing",
              dbg.live_raster() && emu.debug_state().raster_live() && !dbg.armed() &&
                  !dbg.attached());
    }
}

static void q4c_magic_hold_rows() {
    // MAGIC-HOLD-01 — the hold, through the facade: a magic stop on a machine
    // nothing arms is armed by the hold ALONE — not attached, no raster walk —
    // and reported as the magic stop, unowned.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.set_magic_breakpoint(true);
        emu.mmu().write(PROG, 0xED);
        emu.mmu().write(PROG + 1, 0xFF);
        const bool unarmed_before = !dbg.armed();
        run_until_paused(emu);
        const RunState st = dbg.state();
        check("MAGIC-HOLD-01", "a magic stop on an unarmed machine holds it at the "
                               "next boundary: armed() by the hold alone, not "
                               "attached, no raster walk, reason Magic, unowned",
              unarmed_before && st.paused && pc_of(emu) == PROG + 2 &&
                  st.pause_reason.kind == PauseReason::Kind::Magic &&
                  st.pause_reason.by == jnext::dbg::CLIENT_NONE && dbg.armed() &&
                  !dbg.attached() && !dbg.live_raster() &&
                  !emu.debug_state().raster_live(),
              "pc=" + hex(pc_of(emu)));
    }
    // MAGIC-HOLD-02 — the leak the retired `active()` bit had: a REMOTE client
    // attached, a magic stop, the remote's run() and detach. The machine must be
    // left as nothing arms it — unarmed, the step machinery and raster walk off —
    // so a breakpoint the GUI left behind no longer fires. With `active()` the
    // hook's write outlived the stop for the rest of the session.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
        dbg.subscribe(o, q4c_exec_at(AFTER_CALL));
        const ClientId r =
            dbg.attach(client("remote", jnext::dbg::ClientKind::Dzrp)).value;
        dbg.set_magic_breakpoint(true);
        emu.mmu().write(PROG, 0xED);
        emu.mmu().write(PROG + 1, 0xFF);
        run_until_paused(emu);
        const bool magic = dbg.state().paused &&
                           dbg.state().pause_reason.kind == PauseReason::Kind::Magic;
        const Result rr = dbg.run(r);
        dbg.detach(r);
        const bool unarmed = !dbg.armed() && !dbg.attached() &&
                             !emu.debug_state().raster_live() &&
                             !emu.debug_state().magic_hold();
        emu.run_frame();
        check("MAGIC-HOLD-02", "a remote client's run() of a magic stop, then its "
                               "detach: the machine is unarmed, and runs past the "
                               "GUI's breakpoint to the park",
              magic && rr == Result::Ok && unarmed && !dbg.state().paused &&
                  pc_of(emu) == PARK,
              "pc=" + hex(pc_of(emu)) + " armed=" + std::to_string(dbg.armed()));
    }
}

static void q4c_master_mirror_rows() {
    // MASTER-01/02 — the master switch across a cold boot. It is the backend's
    // (the table lives on `Impl`), so it survives; the rebuilt `BreakpointSet`
    // starts at `true`, and since the platform restore that carried it retired
    // (B3 obligation 1) only the re-application brings the two back into step.
    Emulator emu; build(emu);
    Debugger dbg(emu);
    const ClientId o = dbg.attach(q4c_observer("Qt GUI")).value;
    const auto id = dbg.subscribe(o, q4c_exec_at(PROG + 3));
    const ClientId w = dbg.attach(client("window", jnext::dbg::ClientKind::Gui)).value;
    dbg.set_master_enabled(false);
    jnext::dbg::LoopDriver d;
    d.cold_boot = [&]() {
        emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
        return true;
    };
    dbg.set_loop_driver(d);
    dbg.reset(w, ResetKind::Hard);
    load_prog(emu, kQ4cNops);
    const auto subs = dbg.subscriptions(false);
    const bool suspended = subs.size() == 1 && subs[0].id == id.value &&
                           subs[0].enabled && !subs[0].live;
    const bool reported_off = !dbg.master_enabled();
    emu.run_frame();
    check("MASTER-01", "a master switch left OFF survives a hard reset: reported off, the "
                       "subscription listed enabled-but-suspended, and the armed machine "
                       "runs past it",
          reported_off && suspended && !emu.debug_state().paused() && pc_of(emu) == PARK,
          "off=" + std::to_string(reported_off) + " suspended=" +
              std::to_string(suspended) + " pc=" + hex(pc_of(emu)));
    // A legacy PC breakpoint on the rebuilt machine is suspended by the same
    // switch: the mirror, not the fresh `true`.
    q4c_restart(emu);
    emu.debug_state().breakpoints().add_pc(PROG + 4);
    emu.run_frame();
    check("MASTER-02", "and the rebuilt machine's legacy BreakpointSet is re-mirrored OFF "
                       "by the re-application: a PC breakpoint added after the boot does "
                       "not stop it",
          !emu.debug_state().breakpoints().master_enabled() &&
              !emu.debug_state().paused() && pc_of(emu) == PARK,
          "pc=" + hex(pc_of(emu)));
}

// GH #278 WP4d — the INS-14-08/10 scene: a Next paused MID-FRAME with every
// engine the eight render_layer views drive holding something that could stick
// (see the comment at INS-14-08). Returns the framebuffer row the raster is
// paused on; `guest_read` is what the guest's port 0x303B read returned just
// before (both status bits, as frame 1's render latched them). `ula_on` false
// turns the ULA and LoRes off, so the tilemap is what the composite shows in
// the display area — INS-14-10 needs its per-line scroll split to be VISIBLE.
static int wp4d_paused_scene(Emulator& emu, uint8_t& guest_read, bool ula_on = true) {
    build(emu, MachineType::ZXN_ISSUE2);
    auto nr = [&emu](uint8_t reg, uint8_t val) {
        emu.port().out(0x243B, reg);
        emu.port().out(0x253B, val);
    };
    emu.port().out(0x303B, 0x00);                          // pattern 0
    for (int i = 0; i < 256; ++i) emu.port().out(0x5B, 0x77);
    emu.port().out(0x303B, 0x00);                          // sprite 0..127
    for (int i = 0; i < 128; ++i) {
        emu.port().out(0x57, static_cast<uint8_t>(i * 2)); // X: overlapping
        emu.port().out(0x57, 150);                         // Y: one line
        emu.port().out(0x57, 0x00);
        emu.port().out(0x57, 0x80);                        // visible, pattern 0
    }
    nr(0x15, ula_on ? 0x81 : 0x01);        // sprites visible (+ LoRes)
    if (!ula_on) nr(0x68, 0x80);           // ULA off
    nr(0x69, 0x80);        // Layer 2 on
    nr(0x6B, 0x80);        // tilemap on
    nr(0x14, 0x00);        // NR 0x14 = black: Layer 2's zeroed bank is transparent
    // Varied bank-5 bytes, so the ULA / LoRes screen and the tilemap's tiles
    // (map and definitions both default into bank 5) are not uniform and a
    // scroll split is visible (INS-14-10).
    for (uint16_t a = 0x4000; a < 0x5B00; ++a)
        emu.mmu().write(a, static_cast<uint8_t>((a * 37u) >> 3));
    // …and a tilemap palette that is not all one colour (its reset content is).
    nr(0x43, 0x30);                        // write-select the tilemap first palette
    nr(0x40, 0x00);
    for (int i = 0; i < 256; ++i) nr(0x41, static_cast<uint8_t>(i));
    nr(0x43, 0x00);
    nr(0x61, 0x00);
    nr(0x62, 0x00);
    // Each value is set at the top of every frame and changed on line 60, so
    // the paused frame really carries a split (a value written once would be
    // the baseline of every later frame, and no split at all).
    const uint16_t prog[] = {
        uint16_t(0x8000u | 0u),
        uint16_t((0x4Au << 8) | 0xE3u),     // NR 0x4A, top of frame
        uint16_t((0x30u << 8) | 0x00u),     // tilemap scroll X, top of frame
        uint16_t(0x8000u | 60u),
        uint16_t((0x4Au << 8) | 0xE0u),     // NR 0x4A
        uint16_t((0x16u << 8) | 0x10u),     // Layer 2 scroll X
        uint16_t((0x40u << 8) | 0x05u),     // palette index
        uint16_t((0x41u << 8) | 0x1Cu),     // palette value
        uint16_t((0x30u << 8) | 0x03u),     // tilemap scroll X (per-line snapshot);
                                            // 3, not a whole number of 8-px tiles
        uint16_t(0x8000u | 511u),
    };
    for (uint16_t insn : prog) {
        nr(0x60, static_cast<uint8_t>(insn >> 8));
        nr(0x60, static_cast<uint8_t>(insn & 0xFF));
    }
    nr(0x62, 0xC0);
    emu.run_frame();
    emu.debug_state().set_clients_attached(true);   // was set_active(true): WP4c
    emu.debug_state().set_live_raster(true);
    const int vbt = emu.video_timing().vblank_top();
    emu.debug_state().run_to_cycle(emu.current_frame_cycle() +
                                   static_cast<uint64_t>(vbt + 200) *
                                       emu.timing().master_cycles_per_line + 300);
    emu.run_frame();
    emu.snapshot_raster();
    constexpr int VBLANK_LINE = 300;
    emu.palette().set_current_line(VBLANK_LINE);
    emu.palette().write_control(0x10);
    emu.palette().set_index(0x66);
    emu.palette().write_8bit(0x77);
    emu.layer2().set_current_line(VBLANK_LINE);
    emu.layer2().set_scroll_y(123);
    guest_read = emu.port().in(0x303B);                    // clears both bits
    return static_cast<int>(emu.paused_vc()) - vbt;
}


// ── GH #278 WP4d review round 1 — helpers for INS-14-11..20 ─────────────────
//
// The per-scanline replay rows need three things per change log: a frame whose
// BASELINE is value A (so the rows above the write must show A), a write of B
// tagged at a visible row (so the rows from there on must show B — the split),
// and a write of C tagged in the bottom VBLANK (which only the replay's final
// flush reaches, so after a render the live register must be C again, the
// DVP-16c class). A, B and C are chosen so that A and C also DRAW differently,
// which is what makes a replay that skips its rewind show a wrong top half.

constexpr int WP4D_SPLIT  = 100;   // framebuffer row of the mid-frame write
constexpr int WP4D_VBLANK = 300;   // a row past the visible 256

// Run a built machine into the bottom VBLANK of a fresh frame and pause there:
// begin_new_frame() has baselined every change log from the state set up
// before the call, and every visible row's per-line snapshots are taken.
static void wp4d_pause_in_vblank(Emulator& emu) {
    emu.run_frame();                          // settle
    emu.debug_state().set_clients_attached(true);   // was set_active(true): WP4c
    emu.debug_state().set_live_raster(true);
    const int vbt = emu.video_timing().vblank_top();
    emu.debug_state().run_to_cycle(emu.current_frame_cycle() +
                                   static_cast<uint64_t>(vbt + 266) *
                                       emu.timing().master_cycles_per_line);
    emu.run_frame();
    emu.snapshot_raster();
}

// Tag the next writes to EVERY per-scanline change log with framebuffer row
// `row`, exactly the nine calls Emulator::on_scanline() makes (plus hc 0 for
// the attribute mux, so a write lands before every column's fetch).
static void wp4d_tag_row(Emulator& emu, int row) {
    emu.palette().set_current_line(row);
    emu.layer2().set_current_line(row);
    emu.sprites().set_current_line(row);
    emu.ula().set_current_line(row);
    emu.ula().set_current_scroll_line(row);
    emu.ula().set_palsel_current_line(row);
    emu.tilemap().set_current_nr6b_line(row);
    emu.mmu().attr_mux_set_current_line(row);
    emu.mmu().attr_mux_set_current_hc(0);
    emu.renderer().set_current_line_nr15(row);
}

static void wp4d_nr(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// Sprite 0 = pattern 0, all `colour`, at (x, y), visible.
static void wp4d_sprite0(Emulator& emu, uint8_t colour, uint8_t x, uint8_t y) {
    emu.port().out(0x303B, 0x00);
    for (int i = 0; i < 256; ++i) emu.port().out(0x5B, colour);
    emu.port().out(0x303B, 0x00);
    emu.port().out(0x57, x);
    emu.port().out(0x57, y);
    emu.port().out(0x57, 0x00);
    emu.port().out(0x57, 0x80);
}

// The port 0x303B scenes of INS-14-18..20: `kind` 0 latches BOTH status bits,
// 1 collision only (two overlapping opaque sprites), 2 max-sprites only (128
// sprites on one line past the per-line budget, pattern all transparent, so
// nothing is drawn and nothing collides). A frame runs to completion — its
// render latches the bits and no guest read clears them — and the machine
// pauses mid-frame below the sprite line. With `render`, the Sprites and
// Composite views are drawn there (`rendered` says both really drew). Returns
// the guest's port 0x303B read; `again` is a second read, which must find the
// bits cleared by the first.
static uint8_t wp4d_status_scene(int kind, bool render, uint8_t& again,
                                 bool& rendered) {
    Emulator emu;
    build(emu, MachineType::ZXN_ISSUE2);
    Debugger dbg(emu);
    const uint8_t colour = (kind == 2) ? 0xE3 : 0x77;   // 0xE3 = NR 0x4B default
    emu.port().out(0x303B, 0x00);
    for (int i = 0; i < 256; ++i) emu.port().out(0x5B, colour);
    const int count = (kind == 1) ? 2 : 128;
    emu.port().out(0x303B, 0x00);
    for (int i = 0; i < count; ++i) {
        emu.port().out(0x57, static_cast<uint8_t>(100 + i * 2));
        emu.port().out(0x57, 150);
        emu.port().out(0x57, 0x00);
        emu.port().out(0x57, 0x80);
    }
    wp4d_nr(emu, 0x15, 0x01);                 // sprites visible
    emu.run_frame();
    emu.run_frame();                          // this frame's render latches the bits
    emu.debug_state().set_clients_attached(true);   // was set_active(true): WP4c
    emu.debug_state().set_live_raster(true);
    const int vbt = emu.video_timing().vblank_top();
    emu.debug_state().run_to_cycle(emu.current_frame_cycle() +
                                   static_cast<uint64_t>(vbt + 200) *
                                       emu.timing().master_cycles_per_line);
    emu.run_frame();
    emu.snapshot_raster();
    rendered = !render;                       // nothing to draw counts as done
    if (render) {
        std::vector<uint32_t> buf(jnext::dbg::RENDER_WIDTH * 256);
        const int fb_row = static_cast<int>(emu.paused_vc()) - vbt;
        // Both views must really draw the sprite line (row 150 < fb_row), or
        // the row would pass on a render that never ran.
        rendered = fb_row > 150 &&
                   dbg.render_layer(jnext::dbg::Layer::Sprites, fb_row, buf.data(),
                                    jnext::dbg::RENDER_WIDTH) == Result::Ok &&
                   dbg.render_layer(jnext::dbg::Layer::Composite, fb_row, buf.data(),
                                    jnext::dbg::RENDER_WIDTH) == Result::Ok;
    }
    const uint8_t first = emu.port().in(0x303B);
    again = emu.port().in(0x303B);
    return first;
}

// ===========================================================================
// GH #12 (epic #276 package D, DZRP) — BACKEND ROWS FOR PACKAGE D'S CHANGES.
//
// ONE CONTIGUOUS BLOCK, deliberately: package Q edits this file too, and a
// block that stands alone merges as one hunk. Two groups:
//
//   D-FIX-*    the three defects package D found and fixed in
//              `debugger_inspect.cpp`, pinned at the BACKEND contract so every
//              client is covered, not only the DZRP adapter whose suite found
//              them (review of D milestone 1, item 4).
//   INS-03-1x  `rom_select()`, the INS-03 query package D added to the frozen
//              header (owner-approved 2026-09-29): the ROM image legacy paging
//              selects, on all four machine types, with the NR 0x8C locks.
// ===========================================================================

static void dzrp_d_rows() {
    using jnext::dbg::MemSpace;
    {
        // D-FIX-01/02 — B-1: `set_mmu_slot(0/1, 0xFF)` is what `NEXTREG
        // 0x50/0x51,0xFF` does: legacy ROM paging re-engaged. It used to call
        // `Mmu::set_page`, which left the slot UNMAPPED (reads 0xFF, writes
        // dropped). Each ROM SRAM page carries its own byte, so the CPU view at
        // 0x0000 / 0x2000 shows which page answers.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        for (int p = 0; p < 8; ++p)
            emu.ram().page_ptr(static_cast<uint16_t>(p))[0] = static_cast<uint8_t>(0xB0 + p);
        emu.mmu().nr_page_ptr(5)[0] = 0x55;
        emu.mmu().nr_page_ptr(6)[0] = 0x66;
        uint8_t rom0 = 0, rom1 = 0, ram0 = 0, ram1 = 0, back0 = 0, back1 = 0;
        dbg.peek(MemSpace::cpu(), 0x0000, 1, &rom0);
        dbg.peek(MemSpace::cpu(), 0x2000, 1, &rom1);
        const bool mapped = dbg.set_mmu_slot(1, 0, 5) == Result::Ok &&
                            dbg.set_mmu_slot(1, 1, 6) == Result::Ok;
        dbg.peek(MemSpace::cpu(), 0x0000, 1, &ram0);
        dbg.peek(MemSpace::cpu(), 0x2000, 1, &ram1);
        const bool restored = dbg.set_mmu_slot(1, 0, 0xFF) == Result::Ok &&
                              dbg.set_mmu_slot(1, 1, 0xFF) == Result::Ok;
        dbg.peek(MemSpace::cpu(), 0x0000, 1, &back0);
        dbg.peek(MemSpace::cpu(), 0x2000, 1, &back1);
        const auto slots = dbg.mmu_slots();
        check("D-FIX-01", "set_mmu_slot(0, 0xFF) after RAM was paged there re-engages the ROM, "
                          "as NEXTREG 0x50,0xFF does: 0x0000 reads the ROM page again and the slot "
                          "is read-only with NR value 0xFF",
              mapped && restored && ram0 == 0x55 && back0 == rom0 && rom0 >= 0xB0 &&
                  slots[0].is_rom && slots[0].nr_page == 0xFF,
              hex(rom0) + " " + hex(ram0) + " " + hex(back0));
        check("D-FIX-02", "and set_mmu_slot(1, 0xFF) does the same for slot 1's ROM half",
              ram1 == 0x66 && back1 == rom1 && rom1 >= 0xB0 && slots[1].is_rom &&
                  slots[1].nr_page == 0xFF,
              hex(rom1) + " " + hex(ram1) + " " + hex(back1));
    }
    {
        // D-FIX-03 — B-2: `port_out`'s MUTATE line spells the value in HEX, as
        // its `0x` says; `std::to_string` once logged 0x15 as "0x21".
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(16);
        Log::debugger()->sinks().push_back(ring);
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.port_out(4, 0x00FE, 0x15);
        bool hexed = false, decimal = false;
        for (const auto& l : ring->last_formatted()) {
            hexed   = hexed || l.find("MUTATE port out 0x00FE = 0x15 by 4") != std::string::npos;
            decimal = decimal || l.find("= 0x21") != std::string::npos;
        }
        Log::debugger()->sinks().pop_back();
        check("D-FIX-03", "port_out()'s MUTATE line spells the value in hex (0x15, not \"0x21\")",
              hexed && !decimal);
    }
    {
        // D-FIX-04 — B-3: `set_border` honours §4.2a's RZX wall like every other
        // mutation verb; it alone lacked it.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.ula().set_border(2);
        emu.rzx_player().start(RzxRecording{});
        const Result refused = dbg.set_border(1, 5);
        const uint8_t during = emu.ula().get_border();
        emu.rzx_player().stop();
        const Result allowed = dbg.set_border(1, 5);
        check("D-FIX-04", "set_border() is refused_rzx under an RZX playback, border unchanged, "
                          "and allowed again once it stops",
              refused == Result::RefusedRzx && during == 2 && allowed == Result::Ok &&
                  emu.ula().get_border() == 5);
    }

    // ── rom_select() ────────────────────────────────────────────────────────
    //
    // VHDL `sram_rom` (zxnext.vhd:2981-3008): 48K "00" always; +3 the two
    // 1FFD b2 / 7FFD b4 bits, or the two NR 0x8C lock bits when either is set;
    // everything else (128K, the Next) '0' & 7FFD b4, or '0' & lock_rom1 when a
    // lock is set.
    auto sel = [](Debugger& d) { return static_cast<int>(d.rom_select().index); };
    auto is_rom_space = [](Debugger& d) { return d.rom_select().kind == MemSpace::Kind::Rom; };
    {
        Emulator emu; build(emu, MachineType::ZX48K);
        Debugger dbg(emu);
        const int a = sel(dbg);
        dbg.port_out(1, 0x7FFD, 0x10);
        const int b = sel(dbg);
        dbg.nextreg_write(1, 0x8C, 0x30);
        const int c = sel(dbg);
        check("INS-03-10", "rom_select() on a 48K is ROM image 0, whatever 0x7FFD and the NR 0x8C "
                           "locks say, and it is a Rom space",
              is_rom_space(dbg) && a == 0 && b == 0 && c == 0,
              std::to_string(a) + std::to_string(b) + std::to_string(c));
    }
    {
        Emulator emu; build(emu, MachineType::ZX128K);
        Debugger dbg(emu);
        const int a = sel(dbg);
        dbg.port_out(1, 0x7FFD, 0x10);
        const int b = sel(dbg);
        dbg.port_out(1, 0x1FFD, 0x04);
        const int c = sel(dbg);
        check("INS-03-11", "rom_select() on a 128K follows 0x7FFD bit 4 alone: image 0, then 1, "
                           "and 0x1FFD bit 2 does not make it 3",
              a == 0 && b == 1 && c == 1,
              std::to_string(a) + std::to_string(b) + std::to_string(c));
        dbg.nextreg_write(1, 0x8C, 0x10);  // lock_rom0 only
        const int d = sel(dbg);
        dbg.port_out(1, 0x7FFD, 0x00);
        dbg.nextreg_write(1, 0x8C, 0x20);  // lock_rom1
        const int e = sel(dbg);
        check("INS-03-12", "on a 128K an NR 0x8C lock overrides 0x7FFD: lock_rom0 alone gives "
                           "image 0 with 7FFD b4 set, lock_rom1 gives image 1 with it clear",
              d == 0 && e == 1, std::to_string(d) + std::to_string(e));
    }
    {
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        dbg.port_out(1, 0x7FFD, 0x10);
        const int a = sel(dbg);
        dbg.port_out(1, 0x1FFD, 0x04);
        const int b = sel(dbg);
        dbg.port_out(1, 0x7FFD, 0x00);
        const int c = sel(dbg);
        check("INS-03-13", "rom_select() on a +3 is the two bits 1FFD b2 : 7FFD b4 — images 1, "
                           "3 and 2",
              a == 1 && b == 3 && c == 2,
              std::to_string(a) + std::to_string(b) + std::to_string(c));
        dbg.port_out(1, 0x1FFD, 0x00);
        dbg.nextreg_write(1, 0x8C, 0x20);
        const int d = sel(dbg);
        dbg.nextreg_write(1, 0x8C, 0x30);
        const int e = sel(dbg);
        dbg.nextreg_write(1, 0x8C, 0x10);
        const int f = sel(dbg);
        check("INS-03-14", "on a +3 the NR 0x8C locks ARE the two bits when either is set: "
                           "images 2, 3 and 1 with the ports selecting 0",
              d == 2 && e == 3 && f == 1,
              std::to_string(d) + std::to_string(e) + std::to_string(f));
    }
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        dbg.port_out(1, 0x1FFD, 0x04);
        const int a = sel(dbg);
        dbg.port_out(1, 0x7FFD, 0x10);
        const int b = sel(dbg);
        dbg.port_out(1, 0x7FFD, 0x00);
        dbg.nextreg_write(1, 0x8C, 0x20);
        const int c = sel(dbg);
        check("INS-03-15", "rom_select() on the Next is ONE bit: 1FFD b2 alone leaves image 0, "
                           "7FFD b4 gives 1, and lock_rom1 gives 1 — the design's first formula "
                           "(7FFD b4 | 1FFD b2) would have said 2",
              a == 0 && b == 1 && c == 1,
              std::to_string(a) + std::to_string(b) + std::to_string(c));
    }
    {
        // AGREEMENT WITH THE SLOT VIEW, and the question only this query can
        // answer: with RAM paged into slot 0 it still names the image that
        // `set_mmu_slot(0, 0xFF)` then restores.
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        dbg.port_out(1, 0x1FFD, 0x04);
        const auto slots = dbg.mmu_slots();
        const bool agree = slots[0].is_rom && slots[1].is_rom &&
                           slots[0].space == dbg.rom_select() && slots[0].space_offset == 0 &&
                           slots[1].space == dbg.rom_select() && slots[1].space_offset == 0x2000;
        check("INS-03-16", "while slots 0/1 are ROM, rom_select() is the space both report, slot "
                           "0 at offset 0 and slot 1 at 0x2000",
              agree);
        Emulator nx; build(nx, MachineType::ZXN_ISSUE2);
        Debugger dn(nx);
        dn.port_out(1, 0x7FFD, 0x10);
        dn.set_mmu_slot(1, 0, 4);
        const MemSpace while_ram = dn.rom_select();
        const bool     ram_now   = !dn.mmu_slots()[0].is_rom;
        dn.set_mmu_slot(1, 0, 0xFF);
        const auto back = dn.mmu_slots();
        check("INS-03-17", "with RAM paged into slot 0 rom_select() still names the image "
                           "(1 here), and it is the space slot 0 reports once 0xFF restores the "
                           "ROM",
              ram_now && while_ram.kind == MemSpace::Kind::Rom && while_ram.index == 1 &&
                  back[0].is_rom && back[0].space == while_ram);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GH #278 WP7 — a PAUSED machine's raster is taken by the backend at the query.
// Until WP7 only the Qt debugger's refresh took `Emulator::snapshot_raster()`
// (through its `Emulator*`), so raster() / time() of a machine a script or a
// remote client had paused reported whatever the last Qt refresh left — on a
// machine with no Qt window, the power-on zeros. No client calls
// snapshot_raster() in these rows.
// ═══════════════════════════════════════════════════════════════════════════
static void q_wp7_raster_rows() {
    {
        // Each query on a machine of its OWN, so neither can ride on a
        // snapshot the other took.
        struct Paused { int want_vc = 0, want_hc = 0; };
        auto pause_mid_frame = [](Emulator& emu, Debugger& dbg) {
            const ClientId a = dbg.attach(client("A")).value;
            emu.run_frame();
            for (int i = 0; i < 400; ++i) emu.execute_single_instruction();   // mid-frame
            dbg.pause(a);
            const uint64_t elapsed = emu.clock().get() - emu.current_frame_cycle();
            const uint64_t mcl     = emu.timing().master_cycles_per_line;
            return Paused{static_cast<int>(elapsed / mcl),
                          static_cast<int>((elapsed % mcl) / 4)};
        };
        Emulator e1; build(e1);
        Debugger d1(e1);
        const Paused p1 = pause_mid_frame(e1, d1);
        const auto ras = d1.raster();
        Emulator e2; build(e2);
        Debugger d2(e2);
        const Paused p2 = pause_mid_frame(e2, d2);
        const auto t = d2.time();
        check("INS-06-03", "raster() and time() of a paused machine report where it "
                           "stopped, with no snapshot_raster() call by anyone",
              p1.want_vc > 0 && ras.raw_vc == p1.want_vc && ras.raw_hc == p1.want_hc &&
                  t.vc_raw == p2.want_vc && t.hc_raw == p2.want_hc,
              "want vc/hc=" + std::to_string(p1.want_vc) + "/" + std::to_string(p1.want_hc) +
                  " raster=" + std::to_string(ras.raw_vc) + "/" + std::to_string(ras.raw_hc) +
                  " time=" + std::to_string(t.vc_raw) + "/" + std::to_string(t.hc_raw) +
                  " (want " + std::to_string(p2.want_vc) + "/" + std::to_string(p2.want_hc) + ")");
    }
    {
        // And RUNNING, the query leaves the last pause's snapshot alone, as the
        // Qt refresh (which took it only while paused) always did.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        for (int i = 0; i < 400; ++i) emu.execute_single_instruction();
        emu.snapshot_raster();                        // "the last pause"
        const int kept_vc = emu.paused_vc(), kept_hc = emu.paused_hc();
        for (int i = 0; i < 400; ++i) emu.execute_single_instruction();
        const auto ras = dbg.raster();
        (void)dbg.time();
        check("INS-06-04", "while running, raster() and time() do not move the last "
                           "pause's snapshot",
              !emu.debug_state().paused() && ras.raw_vc == kept_vc &&
                  ras.raw_hc == kept_hc && emu.paused_vc() == kept_vc &&
                  emu.paused_hc() == kept_hc,
              "kept " + std::to_string(kept_vc) + "/" + std::to_string(kept_hc) + " raster " +
                  std::to_string(ras.raw_vc) + "/" + std::to_string(ras.raw_hc));
    }
}

int main() {
    std::printf("=== jnext::dbg::Debugger backend tests (GH #276 B1) ===\n\n");

    // =======================================================================
    // CTL-01 / CTL-02 — pause and run
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);

        const Result r = dbg.pause(7);
        emu.run_frame();                      // must execute nothing
        check("CTL-01-01", "pause() stops the machine and a frame executes nothing",
              r == Result::Ok && dbg.state().paused && pc_of(emu) == PROG,
              "PC=" + hex(pc_of(emu)));
        check("CTL-01-02", "the stop is attributed to the client that paused",
              dbg.state().pause_reason.kind == PauseReason::Kind::User &&
              dbg.state().pause_reason.by == 7);
        check("CTL-01-03", "pause() is idempotent",
              dbg.pause(7) == Result::Ok && dbg.state().paused);

        check("CTL-02-01", "run() resumes and the machine advances",
              dbg.run(7) == Result::Ok);
        emu.run_frame();
        check("CTL-02-02", "a resumed machine parks at the JR $ landing marker",
              pc_of(emu) == PARK, "PC=" + hex(pc_of(emu)));
        check("CTL-02-03", "run() on an already-running machine is an Ok no-op (GH #223)",
              dbg.run(7) == Result::Ok && !dbg.state().paused);
    }
    {
        // The other half of GH #223, and the reason the early return is ordered
        // FIRST: a redundant run() must not destroy a pending Run to Here.
        // DebugState::resume() calls clear_oneshot(), so a run() that reached it
        // would silently throw the target away — and the machine would then run
        // past it, which is the defect a bare "returns Ok" row cannot see.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.run_to(3, AFTER_CALL);        // arms a transient Execute, resumes
        // GH #276 B2 changed the MECHANISM this row reads and not the property
        // it pins: `run_to` now arms a TRANSIENT `Execute` SUBSCRIPTION instead
        // of `BreakpointSet`'s single one-shot (§4.3 — unlimited, master-switch
        // exempt, auto-removed at the next stop), so the assertion is that the
        // transient subscription survives, and CTL-02-05 below is unchanged.
        const auto pending = dbg.subscriptions(/*include_transient=*/true);
        bool target_armed = false;
        for (const auto& si : pending)
            if (si.transient && si.kind == jnext::dbg::EventKind::Execute &&
                si.filter.lo == AFTER_CALL && si.filter.hi == AFTER_CALL)
                target_armed = true;
        check("CTL-02-04", "a redundant run() while running keeps the pending run-to target",
              dbg.run(7) == Result::Ok && target_armed &&
              dbg.subscriptions(true).size() == pending.size());
        run_until_paused(emu);
        check("CTL-02-05", "so the machine still stops there",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
    }

    // The CONTROL row §7 asks for: the same program, no verb, runs straight
    // past every address the verbs above stop at.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        check("CTL-00-01", "without any verb the program runs past the CALL and parks",
              !dbg.state().paused && pc_of(emu) == PARK, "PC=" + hex(pc_of(emu)));
    }

    // =======================================================================
    // CTL-03 / CTL-04 / CTL-05 — the three steps
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);

        check("CTL-03-01", "step_into() executes exactly one instruction",
              dbg.step_into(1) == Result::Ok && pc_of(emu) == PROG + 1,
              "PC=" + hex(pc_of(emu)));
        check("CTL-03-02", "and leaves the machine paused, reason Step",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Step);
        dbg.step_into(1);
        check("CTL-03-03", "a second step_into() lands on the CALL",
              pc_of(emu) == PROG + 2, "PC=" + hex(pc_of(emu)));

        // Now at the CALL: step_over must not enter SUB.
        check("CTL-04-01", "step_over() at a CALL is accepted",
              dbg.step_over(1) == Result::Ok);
        run_until_paused(emu);
        check("CTL-04-02", "step_over() stops after the CALL, not inside it",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
        check("CTL-04-03", "the stop reads as a completed step, not as a run-to",
              dbg.state().pause_reason.kind == PauseReason::Kind::Step);
    }
    {
        // step_over on a NON-call behaves as step_into (CTL-04's else branch).
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("CTL-04-04", "step_over() on a plain NOP advances one instruction",
              dbg.step_over(1) == Result::Ok && pc_of(emu) == PROG + 1,
              "PC=" + hex(pc_of(emu)));
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        // Execute the two NOPs and the CALL, then arm Step Out inside SUB.
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("CTL-05-00", "the CALL landed inside the subroutine",
              pc_of(emu) == SUB, "PC=" + hex(pc_of(emu)));
        attach_and_pause(emu);
        check("CTL-05-01", "step_out() is accepted inside a subroutine",
              dbg.step_out(1) == Result::Ok);
        run_until_paused(emu);
        check("CTL-05-02", "step_out() stops at the caller's next instruction",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
    }

    // =======================================================================
    // CTL-06 / CTL-07 / CTL-08 — the run-to family
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("CTL-06-01", "run_to() is accepted", dbg.run_to(3, AFTER_CALL) == Result::Ok);
        run_until_paused(emu);
        check("CTL-06-02", "run_to(addr) stops AT addr",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
        check("CTL-06-03", "and the stop names the target it was given",
              dbg.state().pause_reason.kind == PauseReason::Kind::RunTo &&
              dbg.state().pause_reason.addr == AFTER_CALL);
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        const uint64_t target = emu.clock().get() + 2000;
        check("CTL-07-01", "run_to_cycle() is accepted",
              dbg.run_to_cycle(3, target) == Result::Ok);
        run_until_paused(emu);
        check("CTL-07-02", "run_to_cycle() stops at or after the target cycle",
              dbg.state().paused && emu.clock().get() >= target,
              std::to_string(emu.clock().get()) + " vs " + std::to_string(target));
        check("CTL-07-03", "and not far past it (within one instruction slot)",
              emu.clock().get() < target + 256);
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        const uint64_t frame_start = emu.current_frame_cycle();
        check("CTL-08-01", "run_to_end_of_scanline() is accepted",
              dbg.run_to_end_of_scanline(3) == Result::Ok);
        run_until_paused(emu);
        const uint64_t line = emu.timing().master_cycles_per_line;
        check("CTL-08-02", "it stops within the first line of the frame",
              dbg.state().paused && emu.clock().get() >= frame_start + line,
              std::to_string(emu.clock().get() - frame_start) + " into the frame");
        check("CTL-08-03", "and short of a whole frame",
              emu.clock().get() - frame_start < emu.timing().master_cycles_per_frame);
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("CTL-08-04", "run_to_end_of_frame() is accepted",
              dbg.run_to_end_of_frame(3) == Result::Ok);
        run_until_paused(emu, 3);
        const uint64_t into = emu.clock().get() - emu.current_frame_cycle();
        const uint64_t last_row_start =
            static_cast<uint64_t>(255 + emu.video_timing().vblank_top()) *
            emu.timing().master_cycles_per_line;
        check("CTL-08-05", "it stops in the last VISIBLE row, not at raw VC 255 (G164v2)",
              dbg.state().paused && into >= last_row_start,
              std::to_string(into) + " vs " + std::to_string(last_row_start));
    }
    {
        // Both end-of-* verbs are frame-boundary-free but paused-only.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("CTL-08-06", "run_to_end_of_frame() on a running machine is refused",
              dbg.run_to_end_of_frame(3) == Result::RefusedRunning);
        check("CTL-08-07", "run_to_end_of_scanline() on a running machine is refused",
              dbg.run_to_end_of_scanline(3) == Result::RefusedRunning);
    }

    // =======================================================================
    // CTL-09 / CTL-10 / ST-03 — reverse execution
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("ST-03-01", "with no rewind buffer, rewind_blocked() says unavailable",
              dbg.rewind_blocked().has_value() &&
              *dbg.rewind_blocked() == Result::RefusedUnavailable);
        check("CTL-09-01", "and step_back() refuses with the same code",
              dbg.step_back(1, 1) == Result::RefusedUnavailable);
        check("CTL-10-01", "and rewind_to_frame() too",
              dbg.rewind_to_frame(1, 0) == Result::RefusedUnavailable);
    }
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        Debugger dbg(emu);
        for (int i = 0; i < 4; ++i) emu.run_frame();

        check("ST-03-02", "with frames recorded, rewind_blocked() is empty",
              !dbg.rewind_blocked().has_value());
        const auto rr = dbg.rewind_range();
        check("ST-03-03", "rewind_range() reports a non-empty ring in both domains",
              rr.depth == 4 && rr.newest_frame >= rr.oldest_frame &&
              rr.newest_cycle >= rr.oldest_cycle && rr.snapshot_bytes > 0,
              "depth=" + std::to_string(rr.depth));
        // capacity is the ring's SIZE, not how much of it is used: 8 frames were
        // configured and 4 have run. Reporting depth for both (which B1 did,
        // behind a comment claiming RewindBuffer did not expose the size) makes
        // every "N of M frames" readout say "4 of 4".
        check("ST-03-04", "capacity is the configured ring size, distinct from depth",
              rr.capacity == 8 && rr.depth == 4,
              "capacity=" + std::to_string(rr.capacity) +
                  " depth=" + std::to_string(rr.depth));

        const uint32_t target = rr.oldest_frame + 1;
        check("CTL-10-02", "rewind_to_frame() to a frame in the ring succeeds",
              dbg.rewind_to_frame(1, target) == Result::Ok);
        check("CTL-10-03", "and the machine is back at that frame's tag",
              dbg.time().frame == target, "frame=" + std::to_string(dbg.time().frame));
        check("CTL-10-04", "a frame OUTSIDE the ring is refused as unavailable",
              dbg.rewind_to_frame(1, rr.newest_frame + 100) == Result::RefusedUnavailable);
    }

    // =======================================================================
    // CTL-11 — the corruption gate
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("CTL-11-01", "a clean machine reports no corruption incident",
              !dbg.resume_blocked_by_corruption().has_value());
        check("CTL-11-02", "acknowledging a generation that is not live is refused",
              dbg.acknowledge_corruption(999) == Result::RefusedUnavailable);
    }
    {
        // The gate itself, driven by a REAL corruption: a state load of the right
        // LENGTH but the wrong CONTENT fails a subsystem sentinel part-way
        // through and leaves the machine torn (Task 60b). Everything that
        // executes must then refuse until the incident is acknowledged.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const auto good = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::RefuseMidFrame);
        std::vector<uint8_t> bad = good.value;
        for (size_t i = bad.size() / 2; i < bad.size(); ++i) bad[i] ^= 0xFF;
        check("CTL-11-03", "a corrupt state load is refused as corrupt",
              dbg.load_state_bytes(1, bad.data(), bad.size()) == Result::RefusedCorrupt);
        const auto inc = dbg.resume_blocked_by_corruption();
        check("CTL-11-04", "and it latches an incident naming the subsystem",
              inc.has_value() && !inc->subsystem.empty(),
              inc.has_value() ? inc->subsystem : std::string("(none)"));
        // The machine has to be PAUSED for run() to reach the gate at all: the
        // GH #223 early return is ordered first on purpose, so a redundant run()
        // on a running machine is a no-op rather than a question about nothing.
        // Pinned as its own row below, because it looks like a hole otherwise.
        check("CTL-11-05a", "a redundant run() on a RUNNING corrupt machine is still an Ok no-op",
              dbg.run(1) == Result::Ok);
        dbg.pause(1);
        check("CTL-11-05", "every execute-family verb then refuses with RefusedCorrupt",
              dbg.run(1) == Result::RefusedCorrupt &&
              dbg.step_into(1) == Result::RefusedCorrupt &&
              dbg.step_over(1) == Result::RefusedCorrupt &&
              dbg.step_out(1) == Result::RefusedCorrupt &&
              dbg.run_to(1, PARK) == Result::RefusedCorrupt);
        check("CTL-11-06", "acknowledging THIS incident unblocks them",
              dbg.acknowledge_corruption(inc->generation) == Result::Ok &&
              !dbg.resume_blocked_by_corruption().has_value() &&
              dbg.run(1) == Result::Ok);
    }

    // =======================================================================
    // CTL-12 / CTL-14 / CTL-15 and the armed gate
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("CTL-12-01", "reset(Soft) is performed here, synchronously",
              dbg.reset(1, jnext::dbg::ResetKind::Soft) == Result::Ok);
        check("CTL-12-02", "reset(Hard) with no loop driver refuses as unavailable",
              dbg.reset(1, jnext::dbg::ResetKind::Hard) == Result::RefusedUnavailable);
        check("CTL-12-03", "reset(Any) is refused — Any is a filter value, not a reset (F8)",
              dbg.reset(1, jnext::dbg::ResetKind::Any) == Result::Unsupported);
        check("CTL-15-01", "load() with no loop driver refuses as unavailable",
              dbg.load(1, "nope.nex") == Result::RefusedUnavailable);

        check("CTL-14-01", "the magic breakpoint starts disarmed",
              !dbg.magic_breakpoint());
        check("CTL-14-02", "set_magic_breakpoint(true) arms it and reads back",
              dbg.set_magic_breakpoint(true) == Result::Ok && dbg.magic_breakpoint());
        check("CTL-14-03", "and it does NOT arm the step machinery by itself",
              !dbg.armed());
        check("CTL-14-04", "disarming it reads back too",
              dbg.set_magic_breakpoint(false) == Result::Ok && !dbg.magic_breakpoint());

        check("ARM-01", "armed() is false with nothing attached and no persistence",
              !dbg.armed());
        check("ARM-02", "--persistent-breakpoints alone arms it (GH #219)",
              dbg.set_persistent_breakpoints(true) == Result::Ok &&
              dbg.persistent_breakpoints() && dbg.armed());
        dbg.set_persistent_breakpoints(false);
        // GH #278 WP4c — a frontend arms the machine by ATTACHING (the Qt
        // window's `active()` bit this row set until then is retired).
        dbg.attach(client("frontend", jnext::dbg::ClientKind::Gui));
        check("ARM-03", "an attached frontend arms it too",
              dbg.armed() && dbg.attached());
    }

    // =======================================================================
    // CTL-13 — a stop the backend did not cause reads as Breakpoint
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        emu.debug_state().breakpoints().add_pc(AFTER_CALL);
        run_until_paused(emu);
        check("CTL-13-01", "a PC breakpoint stops the machine at its address",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)));
        check("CTL-13-02", "and the stop reads as Breakpoint at that address",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.addr == AFTER_CALL);
        check("CTL-13-03", "state() reports the cycle and the frame tag too",
              dbg.state().cycle == emu.clock().get() &&
              dbg.state().frame == dbg.time().frame);
    }
    {
        // A run-to stops at its target OR earlier, on a breakpoint it crossed.
        // The reason must say which: reporting RunTo for a breakpoint stop would
        // tell a frontend to clear a target that is still armed.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        emu.debug_state().breakpoints().add_pc(PROG + 1);
        dbg.run_to(3, PARK);                       // target is PAST the breakpoint
        run_until_paused(emu);
        check("CTL-13-04", "a run-to that stops early lands on the breakpoint",
              dbg.state().paused && pc_of(emu) == PROG + 1,
              "PC=" + hex(pc_of(emu)));
        check("CTL-13-05", "and reads as Breakpoint, not as the run-to completing",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint);
    }

    // =======================================================================
    // F1 — peek() does not perturb
    //
    // THE PAIR IS THE ROW. A machine where nothing ever latches would pass the
    // peek half on its own, so the read half runs the identical sweep through
    // Mmu::read() and asserts the latch DOES move. +3 mode, because the
    // floating-bus latch is the +3's (`p3_floating_bus_dat_`), and a contended
    // address (0x4000-0x7FFF) so the latch gate is open.
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        // Put a known, DIFFERENT byte in contended screen RAM.
        for (uint16_t a = 0x4000; a < 0x4010; ++a) emu.mmu().write(a, 0xA5);
        emu.mmu().set_p3_floating_bus_dat(0x3C);

        uint8_t buf[16] = {};
        const auto got = dbg.peek(MemSpace::cpu(), 0x4000, sizeof(buf), buf);
        check("F1-01", "a peek() sweep of contended RAM leaves the +3 floating-bus latch alone",
              emu.mmu().p3_floating_bus_dat() == 0x3C,
              "latch=" + hex(emu.mmu().p3_floating_bus_dat()));
        check("F1-02", "and it read the right bytes",
              got.status == Result::Ok && got.value == sizeof(buf) &&
              buf[0] == 0xA5 && buf[15] == 0xA5);

        // The other half: the guest's read DOES move it.
        for (uint16_t a = 0x4000; a < 0x4010; ++a) (void)emu.mmu().read(a);
        check("F1-03", "the same sweep through read() DOES move the latch (so F1-01 is a real gate)",
              emu.mmu().p3_floating_bus_dat() == 0xA5,
              "latch=" + hex(emu.mmu().p3_floating_bus_dat()));
    }
    {
        // And a READ watchpoint is not fired by a peek, whatever scope we are
        // in — the contract is the verb's, not the caller's.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        emu.debug_state().breakpoints().add_watchpoint(0x4321, WatchType::READ);
        uint8_t b = 0;
        dbg.peek(MemSpace::cpu(), 0x4321, 1, &b);
        check("F1-04", "peek() of a READ-watched address fires no data breakpoint",
              !emu.debug_state().data_bp_hit());

        // The OTHER half, and the one the InspectionScope inside Mmu::peek is
        // actually for: a B2 handler runs INSIDE GuestExecutionScope, where
        // watchpoints_live() is true, so "the caller is outside execution
        // anyway" does not cover it. Simulated here with the same scope the
        // emulator takes, because B1 has no handler to run one from.
        {
            DebugState::GuestExecutionScope guest(emu.debug_state());
            uint8_t v = 0;
            dbg.peek(MemSpace::cpu(), 0x4321, 1, &v);
        }
        check("F1-05", "nor when peek() is called from INSIDE guest execution (a handler)",
              !emu.debug_state().data_bp_hit());

        // And the known-bad for it: the guest's own read of the same address,
        // in the same scope, DOES latch — so F1-05 is a gate and not a tautology.
        {
            DebugState::GuestExecutionScope guest(emu.debug_state());
            (void)emu.mmu().read(0x4321);
        }
        check("F1-06", "while the guest's own read of it does latch (F1-05 is a real gate)",
              emu.debug_state().data_bp_hit());
    }

    // =======================================================================
    // F2 — the frame counter
    // =======================================================================
    {
        Emulator emu; build(emu);          // NO rewind buffer configured
        Debugger dbg(emu);
        check("F2-01", "before the first frame the tag is 0",
              dbg.time().frame == 0, "frame=" + std::to_string(dbg.time().frame));
        emu.run_frame();
        check("F2-02", "after one frame the tag is 0 — the frame just run (pre-increment)",
              dbg.time().frame == 0, "frame=" + std::to_string(dbg.time().frame));
        emu.run_frame();
        emu.run_frame();
        check("F2-03", "the tag advances WITHOUT a rewind buffer",
              dbg.time().frame == 2, "frame=" + std::to_string(dbg.time().frame));
        check("F2-04", "and it is one behind the raw counter",
              emu.frame_num() == dbg.time().frame + 1);
    }
    {
        // The tag is the one the rewind slot carries — the property F2 is for.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        Debugger dbg(emu);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        check("F2-05", "the tag equals the newest rewind slot's frame number",
              emu.rewind_buffer() != nullptr &&
              dbg.time().frame == emu.rewind_buffer()->newest_frame_num(),
              "tag=" + std::to_string(dbg.time().frame));
    }
    {
        // GH #278 — a step_back that stays inside the frame leaves the tag on
        // that frame, and pushes no FrameEnded: nothing ended. The ring
        // snapshot it restores already counted its frame, and the replay used
        // to count it again, so the tag moved one FORWARD and the session
        // announced a frame end that never happened.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        const uint8_t loop[] = { 0x23, 0x18, 0xFD };    // INC HL / JR $-1
        for (size_t i = 0; i < sizeof(loop); ++i)
            emu.mmu().write(static_cast<uint16_t>(PROG + i), loop[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("F2")).value;
        dbg.set_listener(a, &l);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        dbg.pause(a);
        dbg.step_into(a);                         // now inside frame 3
        dbg.step_into(a);
        dbg.pump(jnext::dbg::PumpBudget{});
        const uint32_t tag   = dbg.time().frame;
        const size_t   ended = l.frames.size();
        const Result   sb    = dbg.step_back(a, 1);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("F2-06", "a step_back inside a frame keeps time().frame on that "
                       "frame and pushes no FrameEnded",
              sb == Result::Ok && dbg.time().frame == tag && l.frames.size() == ended,
              "rc=" + std::string(jnext::dbg::result_name(sb)) + " tag " +
                  std::to_string(tag) + " -> " + std::to_string(dbg.time().frame) +
                  " pushes " + std::to_string(ended) + " -> " +
                  std::to_string(l.frames.size()));

        // GH #278 — run_to_frame() from a rewound frame start. The machine
        // sits at the start of frame `back`, which the ring has counted; the
        // verb took that boundary for the one AFTER a counted frame, so
        // run_to_frame(back + 1) stopped at once, at the start of `back`.
        const uint32_t back = dbg.rewind_range().oldest_frame + 1;
        const Result rw = dbg.rewind_to_frame(a, back);
        const uint64_t start = emu.current_frame_cycle();
        const Result rt = dbg.run_to_frame(a, back + 1);
        run_until_paused(emu, 4);
        const uint64_t want = start + emu.timing().master_cycles_per_frame;
        check("F2-07", "run_to_frame(K+1) from a rewound start of K stops in "
                       "frame K+1 (tag K+1), one frame on — not at the start of K",
              rw == Result::Ok && rt == Result::Ok && emu.debug_state().paused() &&
                  dbg.time().frame == back + 1 && emu.current_frame_cycle() == want,
              "rw=" + std::string(jnext::dbg::result_name(rw)) +
                  " rt=" + std::string(jnext::dbg::result_name(rt)) + " tag=" +
                  std::to_string(dbg.time().frame) + " frame_cycle=" +
                  std::to_string(emu.current_frame_cycle()) + " want=" +
                  std::to_string(want));
    }

    // =======================================================================
    // INS-01 — registers
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("INS-01-01", "registers() reads the live file",
              dbg.registers().PC == PROG && dbg.registers().SP == TEST_SP);
        check("INS-01-02", "set_register(BC) writes the pair",
              dbg.set_register(1, RegId::BC, 0x1234) == Result::Ok &&
              dbg.registers().BC == 0x1234);
        check("INS-01-03", "set_register(B) writes only the high half",
              dbg.set_register(1, RegId::B, 0xEE) == Result::Ok &&
              dbg.registers().BC == 0xEE34, hex(dbg.registers().BC));
        check("INS-01-04", "set_register(C) writes only the low half",
              dbg.set_register(1, RegId::C, 0x77) == Result::Ok &&
              dbg.registers().BC == 0xEE77, hex(dbg.registers().BC));
        check("INS-01-05", "set_register(I) leaves R alone (no set-all clobber)",
              dbg.set_register(1, RegId::R, 0x40) == Result::Ok &&
              dbg.set_register(1, RegId::I, 0x3F) == Result::Ok &&
              dbg.registers().I == 0x3F && dbg.registers().R == 0x40);
        check("INS-01-06", "set_register(IM) masks to 0..3",
              dbg.set_register(1, RegId::IM, 0xFF) == Result::Ok &&
              dbg.registers().IM == 3);
        // §4.2a's obligation: PC clears halted.
        Z80Registers r = emu.cpu().get_registers();
        r.halted = true;
        emu.cpu().set_registers(r);
        check("INS-01-07", "setting PC clears halted (§4.2a's obligation on INS-01)",
              dbg.set_register(1, RegId::PC, 0x1000) == Result::Ok &&
              dbg.registers().PC == 0x1000 && !dbg.registers().halted);
        check("INS-01-08", "RegId::Count is not a register",
              dbg.set_register(1, RegId::Count, 0) == Result::Unsupported);
    }

    // One row per RegId (36 writable + RegId::Count, which INS-01-08 covers).
    // The ids are LITERAL STRINGS in the table below, so every row name is
    // greppable in this source exactly as an unrolled call would be.
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        struct Row { const char* id; RegId reg; uint16_t value; const char* expect; };
        static const Row rows[] = {
            // 16-bit pairs: 0x1234 differs from the zeroed baseline in BOTH
            // halves, so a write that lands in one half only names itself.
            { "REG-AF-01",   RegId::AF,   0x1234, "A F"       },
            { "REG-BC-01",   RegId::BC,   0x1234, "B C"       },
            { "REG-DE-01",   RegId::DE,   0x1234, "D E"       },
            { "REG-HL-01",   RegId::HL,   0x1234, "H L"       },
            { "REG-AF2-01",  RegId::AF2,  0x1234, "A' F'"     },
            { "REG-BC2-01",  RegId::BC2,  0x1234, "B' C'"     },
            { "REG-DE2-01",  RegId::DE2,  0x1234, "D' E'"     },
            { "REG-HL2-01",  RegId::HL2,  0x1234, "H' L'"     },
            { "REG-IX-01",   RegId::IX,   0x1234, "IXH IXL"   },
            { "REG-IY-01",   RegId::IY,   0x1234, "IYH IYL"   },
            { "REG-SP-01",   RegId::SP,   0x1234, "SPH SPL"   },
            { "REG-PC-01",   RegId::PC,   0x1234, "PCH PCL"   },
            // 8-bit halves: one field each, and the value is deliberately not
            // symmetric (0x56) so a half-swap cannot alias.
            { "REG-A-01",    RegId::A,    0x0056, "A"         },
            { "REG-F-01",    RegId::F,    0x0056, "F"         },
            { "REG-B-01",    RegId::B,    0x0056, "B"         },
            { "REG-C-01",    RegId::C,    0x0056, "C"         },
            { "REG-D-01",    RegId::D,    0x0056, "D"         },
            { "REG-E-01",    RegId::E,    0x0056, "E"         },
            { "REG-H-01",    RegId::H,    0x0056, "H"         },
            { "REG-L-01",    RegId::L,    0x0056, "L"         },
            { "REG-A2-01",   RegId::A2,   0x0056, "A'"        },
            { "REG-F2-01",   RegId::F2,   0x0056, "F'"        },
            { "REG-B2-01",   RegId::B2,   0x0056, "B'"        },
            { "REG-C2-01",   RegId::C2,   0x0056, "C'"        },
            { "REG-D2-01",   RegId::D2,   0x0056, "D'"        },
            { "REG-E2-01",   RegId::E2,   0x0056, "E'"        },
            { "REG-H2-01",   RegId::H2,   0x0056, "H'"        },
            { "REG-L2-01",   RegId::L2,   0x0056, "L'"        },
            { "REG-IXH-01",  RegId::IXH,  0x0056, "IXH"       },
            { "REG-IXL-01",  RegId::IXL,  0x0056, "IXL"       },
            { "REG-IYH-01",  RegId::IYH,  0x0056, "IYH"       },
            { "REG-IYL-01",  RegId::IYL,  0x0056, "IYL"       },
            // the rest
            { "REG-I-01",    RegId::I,    0x0056, "I"         },
            { "REG-R-01",    RegId::R,    0x0056, "R"         },
            { "REG-IFF1-01", RegId::IFF1, 0x0001, "IFF1"      },
            { "REG-IFF2-01", RegId::IFF2, 0x0001, "IFF2"      },
            { "REG-IM-01",   RegId::IM,   0x0002, "IM"        },
        };
        for (const auto& row : rows) {
            const std::string got = write_one_register(emu, dbg, row.reg, row.value);
            check(row.id, "set_register writes exactly this register and nothing else",
                  got == row.expect,
                  "changed [" + got + "], want [" + std::string(row.expect) + "]");
        }
        // The table is COMPLETE, and this is what says so: `REG_ID_COUNT` is
        // `static_cast<size_t>(RegId::Count)`, i.e. the 37 real registers with
        // the sentinel NOT counted, so the table must hold exactly that many.
        // A RegId appended to the enum moves the count and fails here, which is
        // the only mechanism that keeps a 37-arm switch from growing a 38th arm
        // with no row. (This row earned its place immediately: it caught an
        // off-by-one in its own first formulation.)
        check("REG-COUNT-01", "the table covers every RegId, one row each",
              sizeof(rows) / sizeof(rows[0]) == jnext::dbg::REG_ID_COUNT,
              std::to_string(sizeof(rows) / sizeof(rows[0])) + " rows vs " +
                  std::to_string(jnext::dbg::REG_ID_COUNT) + " RegIds");
    }

    // =======================================================================
    // INS-02 — the three memory spaces
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const uint8_t src[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        check("INS-02-01", "poke(Cpu) writes through the live map",
              dbg.poke(1, MemSpace::cpu(), 0x8100, 4, src).status == Result::Ok);
        uint8_t back[4] = {};
        const auto got = dbg.peek(MemSpace::cpu(), 0x8100, 4, back);
        check("INS-02-02", "and peek(Cpu) reads it back",
              got.status == Result::Ok && got.value == 4 &&
              back[0] == 0xDE && back[3] == 0xEF);
        check("INS-02-03", "poke(Rom) is read-only",
              dbg.poke(1, MemSpace::rom(0), 0, 4, src).status == Result::RefusedReadOnly);
        check("INS-02-04", "peek(Rom) is allowed and reads 16 KB of image",
              dbg.peek(MemSpace::rom(0), 0x3FFF, 1, back).status == Result::Ok);
        check("INS-02-05", "a ROM index past 3 is an invalid page",
              dbg.peek(MemSpace::rom(4), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-06", "the 0xFE ROM sentinel is an invalid page",
              dbg.peek(MemSpace::page(0xFE), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-07", "the 0xFF ROM sentinel too",
              dbg.peek(MemSpace::page(0xFF), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-08", "poking a ROM-sentinel page is read-only, not invalid",
              dbg.poke(1, MemSpace::page(0xFF), 0, 1, src).status == Result::RefusedReadOnly);
        check("INS-02-09", "a Page read that runs off the end of the page is SHORT",
              dbg.peek(MemSpace::page(0x10), 0x1FFE, 8, back).value == 2);

        // The 0xE0..0xFD BAND, which is a different mechanism from the two
        // sentinels and had no row at all: `mmu_A21_A13(8)='1'` ->
        // `sram_pre_active='0'` (zxnext.vhd:3061), the SRAM does not respond, so
        // there is no backing store to hand out. 0xE0 is the first such page and
        // is NOT a sentinel, so it separates `nr_page_ptr()`'s guard from the
        // sentinel test that used to sit in front of it three times over.
        check("INS-02-13", "the first page of the inactive band (0xE0) is an invalid page",
              dbg.peek(MemSpace::page(0xE0), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-14", "and so is the last one below the sentinels (0xFD)",
              dbg.peek(MemSpace::page(0xFD), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-15", "0xDF, one BELOW the band, is a real page and reads",
              dbg.peek(MemSpace::page(0xDF), 0, 1, back).status == Result::Ok);
        // An index outside the 8-bit page-number space at all. Without its own
        // guard the narrowing cast turns 0x100 into page 0 and a nonsense
        // request quietly reads real memory.
        check("INS-02-16", "an index past the 8-bit page space is an invalid page",
              dbg.peek(MemSpace::page(0x100), 0, 1, back).status == Result::InvalidPage);
        check("INS-02-17", "and poking one is refused the same way",
              dbg.poke(1, MemSpace::page(0x100), 0, 1, src).status == Result::InvalidPage);
        check("INS-02-18", "poking the inactive band is InvalidPage, NOT ReadOnly",
              dbg.poke(1, MemSpace::page(0xE0), 0, 1, src).status == Result::InvalidPage);
        check("INS-02-19", "while poking a SENTINEL is ReadOnly, not InvalidPage",
              dbg.poke(1, MemSpace::page(0xFE), 0, 1, src).status == Result::RefusedReadOnly);
    }
    {
        // The drift guard for Mmu::nr_page_ptr(): a byte written through a slot
        // mapped to page P must be visible as page P, for an ordinary page, for a
        // bank-5 page and for a bank-7 page (the two dedicated BRAMs). A Next
        // machine, because the BRAM routing is gated on rom_in_sram_.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        // Spelled out rather than looped: a row ID must be a LITERAL in the
        // source, or the traceability extractor reports rows no reader can find.
        auto page_round_trip = [&](uint8_t page) -> uint8_t {
            emu.mmu().set_page(3, page);                // slot 3 = 0x6000-0x7FFF
            emu.mmu().write(0x6010, 0x5A);
            uint8_t b = 0;
            const auto r = dbg.peek(MemSpace::page(page), 0x0010, 1, &b);
            return r.status == Result::Ok ? b : 0x00;
        };
        check("INS-02-10", "peek(Page) sees a write through a slot — an ordinary SRAM page",
              page_round_trip(0x10) == 0x5A);
        check("INS-02-11", "peek(Page) sees it for a bank-5 VRAM page (dedicated BRAM)",
              page_round_trip(0x0A) == 0x5A);
        check("INS-02-12", "peek(Page) sees it for the bank-7 BRAM page (dedicated BRAM)",
              page_round_trip(0x0E) == 0x5A);
    }

    // =======================================================================
    // INS-03 — slots and paging
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const auto slots = dbg.mmu_slots();
        check("INS-03-01", "slot 0 of a 48K machine is ROM",
              slots[0].is_rom);
        check("INS-03-02", "a ROM slot's space is a Rom image, never a Page",
              slots[0].space.kind == MemSpace::Kind::Rom);
        check("INS-03-03", "a RAM slot's space is its NR page at offset 0",
              !slots[7].is_rom &&
              slots[7].space.kind == MemSpace::Kind::Page &&
              slots[7].space_offset == 0);
        check("INS-03-04", "set_mmu_slot() repoints a slot and reads back",
              dbg.set_mmu_slot(1, 3, 0x11) == Result::Ok &&
              dbg.mmu_slots()[3].nr_page == 0x11);
        check("INS-03-05", "an out-of-range slot is refused",
              dbg.set_mmu_slot(1, 8, 0) == Result::RefusedUnavailable);
        emu.mmu().map_128k_bank(0x03);
        check("INS-03-06", "paging_ports() reports port 0x7FFD as last written",
              dbg.paging_ports().port_7ffd == 0x03,
              hex(dbg.paging_ports().port_7ffd));
    }

    // =======================================================================
    // INS-04 / INS-05 — NextREG and ports
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        check("INS-04-01", "nextreg_write() runs the register's own handler",
              dbg.nextreg_write(1, 0x15, 0x01) == Result::Ok &&
              dbg.nextreg_peek(0x15) == 0x01);
        emu.nextreg().select(0x07);
        check("INS-04-02", "nextreg_selected() reports the port 0x243B latch",
              dbg.nextreg_selected() == 0x07);
        check("INS-05-01", "port_out() dispatches like a guest OUT (border)",
              dbg.port_out(1, 0x00FE, 0x02) == Result::Ok &&
              emu.ula().get_border() == 0x02);
        check("INS-05-02", "port_in() dispatches and yields a value",
              dbg.port_in(1, 0x00FE).status == Result::Ok);
    }

    // =======================================================================
    // INS-06 / INS-07 / INS-19 — raster, time, machine
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        emu.snapshot_raster();
        const auto ras = dbg.raster();
        check("INS-06-01", "raster() reports counters inside the frame",
              ras.raw_hc >= 0 && ras.raw_hc <= emu.video_timing().hc_max() &&
              ras.raw_vc >= 0 && ras.raw_vc <= emu.video_timing().vc_max());

        const auto t = dbg.time();
        check("INS-07-01", "time() reports the master cycle and the T-state total",
              t.master_cycle == emu.clock().get() &&
              t.tstates_total == emu.monotonic_tstates());
        check("INS-07-02", "cycle_in_frame is the offset into this frame",
              t.cycle_in_frame == emu.clock().get() - emu.current_frame_cycle());

        const auto mi = dbg.machine();
        check("INS-19-01", "machine() reports BOTH clock domains, 8 master cycles per T-state",
              mi.master_cycles_per_line == 8ull * mi.tstates_per_line &&
              mi.master_cycles_per_frame == 8ull * mi.tstates_per_frame,
              std::to_string(mi.master_cycles_per_line) + " vs " +
                  std::to_string(mi.tstates_per_line));
        check("INS-19-02", "and the 48K raster constants of the live timing",
              mi.type == MachineType::ZX48K && mi.lines == 312 &&
              mi.tstates_per_line == 224 && mi.hc_max == emu.video_timing().hc_max());
        check("INS-19-03", "fps is derived from the live frame length, not a nominal 50",
              mi.fps > 40.0 && mi.fps < 70.0, std::to_string(mi.fps));
    }

    // =======================================================================
    // INS-08 — sprites
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        const uint8_t attr[5] = { 0x20, 0x30, 0x40, 0x80 | 0x40 | 0x05, 0x00 };
        check("INS-08-01", "set_sprite_attr_raw() writes the five bytes",
              dbg.set_sprite_attr_raw(1, 40, attr, 5) == Result::Ok);
        const auto raw = dbg.sprite_attr_raw(40);
        check("INS-08-02", "and sprite_attr_raw() reads them back",
              raw.status == Result::Ok && raw.value[0] == 0x20 && raw.value[3] == 0xC5);
        check("INS-08-03", "the engine's decode sees the write (not a parallel copy)",
              dbg.sprites().size() == 128 && dbg.sprites()[40].x == 0x20 &&
              dbg.sprites()[40].visible);
        // GH #278 WP0 — an EXTENDED 8-bit sprite (attr3 bit 6 set, attr4 bit 7
        // clear) fetches pattern N5:N0 (sprites.vhd:816, :962); the decode
        // reported N5:N0<<1, the 4-bit numbering, for every extended sprite.
        check("INS-08-14", "sprites() reports an extended 8-bit sprite's pattern "
                           "as N5:N0, the pattern it fetches",
              dbg.sprites()[40].pattern == 0x05 && !dbg.sprites()[40].is_4bit,
              "pattern=" + std::to_string(dbg.sprites()[40].pattern));
        check("INS-08-04", "a wrong byte count is refused",
              dbg.set_sprite_attr_raw(1, 40, attr, 4) == Result::RefusedUnavailable);
        check("INS-08-05", "an out-of-range sprite index is refused",
              dbg.sprite_attr_raw(200).status == Result::RefusedUnavailable);

        const uint8_t pat[3] = { 0x11, 0x22, 0x33 };
        check("INS-08-06", "write_pattern_ram() writes through the engine",
              dbg.write_pattern_ram(1, 0x100, pat, 3) == Result::Ok);
        const auto pr = dbg.pattern_ram();
        check("INS-08-07", "and pattern_ram() is a 16 KB view of the same store",
              pr.size == 16384 && pr.data != nullptr &&
              pr.data[0x100] == 0x11 && pr.data[0x102] == 0x33);
        check("INS-08-08", "sprite_clip() reads the live window",
              dbg.sprite_clip().x2 == emu.sprites().clip_x2());
        check("INS-08-09", "sprite_palette_rgb333() refuses a bank other than 0/1",
              dbg.sprite_palette_rgb333(2, 0).status == Result::RefusedUnavailable);

        // A debugger write must ALSO be recorded in the per-scanline change log,
        // because the renderer rewinds to the frame baseline and replays the log
        // per line: a write that only touched live state would show this frame
        // and vanish from the next re-render of it. Proved by doing what the
        // renderer does — baseline, rewind, replay this line — and asserting the
        // byte survives the round trip.
        emu.sprites().start_frame();                 // baseline = current table
        emu.sprites().set_current_line(40);
        const size_t log_before = emu.sprites().change_log_size();
        dbg.set_sprite_attr_raw(1, 12, attr, 5);
        check("INS-08-10", "set_sprite_attr_raw() records five per-scanline log entries",
              emu.sprites().change_log_size() == log_before + 5,
              std::to_string(emu.sprites().change_log_size() - log_before));
        emu.sprites().rewind_to_baseline();
        emu.sprites().apply_changes_for_line(40);
        check("INS-08-11", "so the write survives the renderer's rewind-and-replay",
              emu.sprites().read_attr_byte(12, 0) == 0x20 &&
              emu.sprites().read_attr_byte(12, 3) == 0xC5,
              hex(emu.sprites().read_attr_byte(12, 0)));

        // The same for pattern RAM, which has its own log and its own cursor.
        emu.sprites().start_frame();
        emu.sprites().set_current_line(41);
        const size_t plog_before = emu.sprites().pattern_change_log_size();
        dbg.write_pattern_ram(1, 0x200, pat, 3);
        check("INS-08-12", "write_pattern_ram() records a per-scanline entry per byte",
              emu.sprites().pattern_change_log_size() == plog_before + 3);
        emu.sprites().rewind_to_baseline();
        emu.sprites().apply_changes_for_line(41);
        check("INS-08-13", "and those bytes survive the rewind-and-replay too",
              emu.sprites().read_pattern_byte(0x200) == 0x11 &&
              emu.sprites().read_pattern_byte(0x202) == 0x33);
    }

    // =======================================================================
    // INS-09 / INS-10 — copper and audio
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        const auto cs = dbg.copper();
        check("INS-09-01", "copper() hands out a 1024-word view of the live RAM",
              cs.program.size == 1024 && cs.program.data != nullptr);
        check("INS-09-02", "and the run state agrees with the Copper itself",
              cs.running == emu.copper().is_running() && cs.mode == emu.copper().mode());

        const auto regs = dbg.ay_registers(1);
        check("INS-10-01", "ay_registers() returns 16 registers for a valid chip",
              regs.status == Result::Ok && regs.value.size() == 16);
        check("INS-10-02", "and refuses a chip index past the third",
              dbg.ay_registers(3).status == Result::RefusedUnavailable);
        check("INS-10-03", "the audio mute mask round-trips",
              dbg.set_audio_mute_mask(1, AudioMute::BEEPER) == Result::Ok &&
              dbg.audio_mute_mask() == AudioMute::BEEPER);
        check("INS-10-04", "turbosound_enabled() reads the live signal",
              dbg.turbosound_enabled() == emu.turbosound().enabled());
    }

    // =======================================================================
    // INS-11 — disassembly over peek()
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const auto lines = dbg.disassemble(PROG, 3, nullptr);
        check("INS-11-01", "disassemble() returns the asked-for number of lines",
              lines.size() == 3);
        check("INS-11-02", "and walks by instruction length (NOP NOP CALL)",
              lines[0].addr == PROG && lines[1].addr == PROG + 1 &&
              lines[2].addr == PROG + 2 && lines[2].byte_count == 3);
        check("INS-11-03", "instruction_length() agrees at the CALL",
              dbg.instruction_length(PROG + 2) == 3);
        check("INS-11-04", "is_call_like() is true at the CALL and false at a NOP",
              dbg.is_call_like(PROG + 2) && !dbg.is_call_like(PROG));
        check("INS-11-05", "memory_reader() reads the live map",
              dbg.memory_reader()(PROG + 2) == 0xCD);
    }

    // =======================================================================
    // INS-12 / INS-13 — call stack and trace
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("INS-12-01", "call-stack tracking is off by default",
              !dbg.call_stack_enabled() && dbg.call_stack().empty());
        check("INS-12-02", "it can be switched on",
              dbg.set_call_stack_enabled(true) == Result::Ok && dbg.call_stack_enabled());

        check("INS-13-01", "trace_entries() refuses while the trace is off",
              !dbg.trace_enabled() &&
              dbg.trace_entries().status == Result::RefusedUnavailable);
        check("INS-13-02", "with the trace on it yields entries once instructions run",
              dbg.set_trace_enabled(true) == Result::Ok && dbg.trace_enabled());
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        const auto entries = dbg.trace_entries();
        check("INS-13-03", "and the entries name the PCs that executed",
              entries.status == Result::Ok && entries.value.size() >= 2 &&
              entries.value[0].pc == PROG);
        check("INS-13-04", "trace_clear() empties it",
              dbg.trace_clear() == Result::Ok &&
              dbg.trace_entries().value.empty());
        check("INS-13-05", "trace_resize(0) is refused",
              dbg.trace_resize(0) == Result::RefusedUnavailable);
    }
    {
        // GH #278 WP0 — call_stack() is Emulator::call_stack(), so the Qt panel's
        // defect was the backend's: an INT was no frame, and the ISR's RET
        // emptied the stack of the routine it interrupted. +3 all-RAM paging
        // puts this row's own IM 1 routine at $0038.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX_PLUS3;
        emu.init(cfg);
        emu.port().write(0x1FFD, 0x01);
        const uint8_t main_prog[] = { 0xCD, 0x00, 0x90 };          // CALL $9000
        const uint8_t sub[]       = { 0x00, 0x18, 0xFD };          // NOP / JR $9000
        const uint8_t isr[]       = { 0xFB, 0xC9 };                // EI / RET
        for (size_t i = 0; i < sizeof(main_prog); ++i) emu.mmu().write(static_cast<uint16_t>(PROG + i), main_prog[i]);
        for (size_t i = 0; i < sizeof(sub); ++i) emu.mmu().write(static_cast<uint16_t>(SUB + i), sub[i]);
        for (size_t i = 0; i < sizeof(isr); ++i) emu.mmu().write(static_cast<uint16_t>(0x0038 + i), isr[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 1; r.IFF2 = 1; r.IM = 1;
        emu.cpu().set_registers(r);
        Debugger dbg(emu);
        dbg.set_call_stack_enabled(true);
        emu.execute_single_instruction();                          // CALL
        emu.cpu().request_interrupt(0xFF);
        emu.execute_single_instruction();                          // INT
        const auto& cs = dbg.call_stack();
        check("INS-12-03", "an accepted INT is a call_stack() frame of type INT",
              cs.size() == 2 && cs.back().type == CallType::INT &&
                  cs.back().target_pc == 0x0038 && cs.front().type == CallType::CALL,
              "frames=" + std::to_string(cs.size()));
        emu.execute_single_instruction();                          // EI
        emu.execute_single_instruction();                          // RET
        check("INS-12-04", "and the routine's RET pops only its own frame",
              dbg.call_stack().size() == 1 &&
                  dbg.call_stack().back().type == CallType::CALL &&
                  emu.cpu().get_registers().PC == SUB,
              "frames=" + std::to_string(dbg.call_stack().size()));
    }

    // =======================================================================
    // INS-14 / INS-15 — framebuffer, palettes, ULA state, clip windows
    // =======================================================================
    {
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        const auto fb = dbg.framebuffer();
        check("INS-14-01", "framebuffer() is a view of the whole 640x256 buffer",
              fb.data != nullptr && fb.size == 640u * 256u,
              std::to_string(fb.size));

        using jnext::dbg::PaletteId;
        check("INS-15-01", "palette() returns all 256 entries of a bank",
              dbg.palette(PaletteId::Layer2First).size() == 256);
        check("INS-15-02", "set_palette() writes one entry and reads back",
              dbg.set_palette(1, PaletteId::Layer2First, 7, 0x1FF) == Result::Ok &&
              dbg.palette(PaletteId::Layer2First)[7] == 0x1FF);
        check("INS-15-03", "it does NOT move the guest's NR 0x40 palette cursor",
              emu.palette().get_index() == 0);
        // Same rewind-and-replay property as the sprite writes above: the
        // renderer replays palette writes per line.
        emu.palette().start_frame();
        emu.palette().set_current_line(60);
        dbg.set_palette(1, PaletteId::Layer2First, 8, 0x0F0);
        emu.palette().rewind_to_baseline();
        emu.palette().apply_changes_for_line(60);
        check("INS-15-03b", "and it IS recorded in the per-scanline palette log",
              dbg.palette(PaletteId::Layer2First)[8] == 0x0F0,
              hex(dbg.palette(PaletteId::Layer2First)[8]));
        // UlaActive resolves through NR 0x43 bit 1, not bit 0.
        dbg.set_palette(1, PaletteId::UlaFirst,  9, 0x0AA);
        dbg.set_palette(1, PaletteId::UlaSecond, 9, 0x155);
        check("INS-15-04", "UlaActive resolves to the first bank by default",
              dbg.palette(PaletteId::UlaActive)[9] == 0x0AA &&
              dbg.active_ula_palette_bank() == 0);
        emu.palette().write_control(0x02);      // NR 0x43 bit 1 = second ULA bank
        check("INS-15-05", "and follows NR 0x43 bit 1 when it flips",
              dbg.palette(PaletteId::UlaActive)[9] == 0x155 &&
              dbg.active_ula_palette_bank() == 1);

        emu.ula().set_border(5);
        const auto us = dbg.ula_screen_regs();
        check("INS-15-06", "ula_screen_regs() reports the border and the active bank",
              us.border == 5 && (us.active_bank == 5 || us.active_bank == 7));
        check("INS-15-07", "set_border() writes it",
              dbg.set_border(1, 3) == Result::Ok && emu.ula().get_border() == 3);

        using jnext::dbg::ClipLayer;
        emu.layer2().set_clip_x2(0x7F);
        check("INS-15-08", "clip_window(Layer2) reads the LIVE layer state",
              dbg.clip_window(ClipLayer::Layer2).x2 == 0x7F);
        check("INS-15-09", "clip_window(Sprites) agrees with sprite_clip()",
              dbg.clip_window(ClipLayer::Sprites).y2 == dbg.sprite_clip().y2);

        check("INS-15-10", "rrrgggbb_to_argb() forwards to the renderer's expansion",
              jnext::dbg::rrrgggbb_to_argb(0xE3) == Renderer::rrrgggbb_to_argb(0xE3) &&
              jnext::dbg::rrrgggbb_to_argb(0x00) == Renderer::rrrgggbb_to_argb(0x00));

        // GH #278 WP4d — REQ-qt-27c, owner-approved: the published RGB333
        // expansion. All 512 inputs against the palette code's own function,
        // and bits above the ninth ignored.
        int bad333 = -1;
        for (uint16_t v = 0; v < 512 && bad333 < 0; ++v) {
            const uint32_t want = rgb333_to_argb8888(static_cast<uint8_t>((v >> 6) & 7),
                                                     static_cast<uint8_t>((v >> 3) & 7),
                                                     static_cast<uint8_t>(v & 7));
            if (jnext::dbg::rgb333_to_argb(v) != want ||
                jnext::dbg::rgb333_to_argb(static_cast<uint16_t>(v | 0xFE00)) != want)
                bad333 = v;
        }
        check("INS-15-20", "rgb333_to_argb() is the palette's own expansion for all 512 "
                           "RGB333 values, and reads only the low 9 bits",
              bad333 < 0 && jnext::dbg::rgb333_to_argb(0x005) == 0xFF0000B6u &&
                  jnext::dbg::rrrgggbb_to_argb(0x02) == 0xFF0000AAu,
              "first mismatch at " + std::to_string(bad333));
        // …and it is the colour the palette's ARGB cache shows for an entry:
        // a whole bank written through set_palette(), read back as drawn.
        int bad_entry = -1;
        for (int i = 0; i < 256; ++i)
            dbg.set_palette(1, PaletteId::TilemapSecond, static_cast<uint8_t>(i),
                            static_cast<uint16_t>((i * 37 + 11) & 0x1FF));
        const auto bank = dbg.palette(PaletteId::TilemapSecond);
        for (int i = 0; i < 256 && bad_entry < 0; ++i)
            if (emu.palette().tilemap_colour(true, static_cast<uint8_t>(i)) !=
                jnext::dbg::rgb333_to_argb(bank[static_cast<size_t>(i)]))
                bad_entry = i;
        check("INS-15-21", "rgb333_to_argb() of each palette() entry is the colour the "
                           "palette draws that entry in (a whole bank)",
              bank.size() == 256 && bad_entry < 0,
              "first mismatch at entry " + std::to_string(bad_entry));
    }

    // =======================================================================
    // INS-14 — render_layer (GH #278 package Q, WP4d)
    //
    // The eight layer views of the Qt Video panel, moved out of
    // `src/debugger/video_panel.cpp` into the Qt-free backend. The panel's own
    // suite (`debugger_video_panel_test`, the DVP rows) pins every view's
    // PICTURE through the widget; these rows pin the VERB'S CONTRACT (design-qt
    // §3.7) where the SDL-only configuration also runs them — the refusals,
    // the 0x00000000 fill, "rows > vc untouched", the stride — and the
    // guarantee the verb makes on its own: rendering a view changes nothing in
    // the machine.
    // =======================================================================
    {
        using jnext::dbg::Layer;
        using jnext::dbg::RENDER_WIDTH;
        constexpr uint32_t SENT = 0xDEADBEEFu;
        constexpr size_t   ROWS = 256;

        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        std::vector<uint32_t> buf(RENDER_WIDTH * ROWS, SENT);
        auto untouched = [&] {
            return std::all_of(buf.begin(), buf.end(),
                               [](uint32_t v) { return v == SENT; });
        };

        const Result neg  = dbg.render_layer(Layer::Composite, -1,  buf.data(), RENDER_WIDTH);
        const Result past = dbg.render_layer(Layer::Composite, 256, buf.data(), RENDER_WIDTH);
        const bool clean = untouched();
        const Result first = dbg.render_layer(Layer::Background, 0,   buf.data(), RENDER_WIDTH);
        const Result last  = dbg.render_layer(Layer::Background, 255, buf.data(), RENDER_WIDTH);
        check("INS-14-02", "render_layer() refuses a vc outside 0..255 (RefusedUnavailable, "
                           "nothing written) and draws at both ends of the range",
              neg == Result::RefusedUnavailable && past == Result::RefusedUnavailable &&
                  clean && first == Result::Ok && last == Result::Ok,
              std::string(jnext::dbg::result_name(neg)) + "/" +
                  jnext::dbg::result_name(past) + " untouched=" + std::to_string(clean) +
                  " 0:" + jnext::dbg::result_name(first) + " 255:" +
                  jnext::dbg::result_name(last));

        check("INS-14-03", "it refuses a null destination (RefusedUnavailable)",
              dbg.render_layer(Layer::Composite, 100, nullptr, RENDER_WIDTH) ==
                  Result::RefusedUnavailable);

        std::fill(buf.begin(), buf.end(), SENT);
        const Result narrow = dbg.render_layer(Layer::Composite, 100, buf.data(),
                                               RENDER_WIDTH - 1);
        const bool narrow_clean = untouched();
        const Result exact = dbg.render_layer(Layer::Composite, 100, buf.data(),
                                              RENDER_WIDTH);
        check("INS-14-04", "it refuses a stride below RENDER_WIDTH (RefusedUnavailable, "
                           "nothing written) and accepts exactly RENDER_WIDTH",
              narrow == Result::RefusedUnavailable && narrow_clean && exact == Result::Ok,
              std::string(jnext::dbg::result_name(narrow)) + " untouched=" +
                  std::to_string(narrow_clean));

        std::fill(buf.begin(), buf.end(), SENT);
        const Result bogus = dbg.render_layer(Layer::Count, 100, buf.data(), RENDER_WIDTH);
        check("INS-14-05", "a Layer outside the eight views is Unsupported, nothing written",
              bogus == Result::Unsupported && untouched(),
              jnext::dbg::result_name(bogus));

        // The contract: over a wider stride, the sprite view of a machine with no
        // sprite visible is all TRANSPARENT (0x00000000) in rows 0..vc, and the
        // bytes it does not own — rows past vc, and each row's columns past 640 —
        // keep whatever the caller had there.
        constexpr size_t WIDE = RENDER_WIDTH + 17;
        constexpr int    VC   = 100;
        std::vector<uint32_t> wide(WIDE * ROWS, SENT);
        const Result drawn = dbg.render_layer(Layer::Sprites, VC, wide.data(), WIDE);
        bool zero_fill = true, rows_kept = true, pad_kept = true;
        for (size_t y = 0; y < ROWS; ++y) {
            for (size_t x = 0; x < WIDE; ++x) {
                const uint32_t v = wide[y * WIDE + x];
                if (y > static_cast<size_t>(VC))      rows_kept &= (v == SENT);
                else if (x >= RENDER_WIDTH)           pad_kept  &= (v == SENT);
                else                                  zero_fill &= (v == 0x00000000u);
            }
        }
        check("INS-14-06", "rows 0..vc are drawn over a 0x00000000 fill (alpha 0 = "
                           "transparent); rows past vc and the stride padding are not touched",
              drawn == Result::Ok && zero_fill && rows_kept && pad_kept,
              std::string("zero_fill=") + std::to_string(zero_fill) + " rows_kept=" +
                  std::to_string(rows_kept) + " pad_kept=" + std::to_string(pad_kept));
    }
    {
        // The composite view IS the picture: after a real frame, render_layer's
        // Composite at vc 255 equals the emulator's own framebuffer, cell for
        // cell and in all 32 bits — and it is opaque everywhere, since the
        // compositor emits the NR 0x4A fallback wherever every layer is
        // transparent. A mid-frame Copper MOVE to NR 0x4A makes the per-line
        // replay part of what has to agree.
        using jnext::dbg::Layer;
        using jnext::dbg::RENDER_WIDTH;
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        auto nr = [&emu](uint8_t reg, uint8_t val) {
            emu.port().out(0x243B, reg);
            emu.port().out(0x253B, val);
        };
        nr(0x68, 0x80);        // ULA off: the fallback is the whole picture
        nr(0x4A, 0x13);
        nr(0x61, 0x00);
        nr(0x62, 0x00);
        for (uint16_t insn : {uint16_t(0x8000u | 100u), uint16_t((0x4Au << 8) | 0xE0u),
                              uint16_t(0x8000u | 511u)}) {
            nr(0x60, static_cast<uint8_t>(insn >> 8));
            nr(0x60, static_cast<uint8_t>(insn & 0xFF));
        }
        nr(0x62, 0xC0);
        emu.run_frame();
        std::vector<uint32_t> comp(RENDER_WIDTH * 256, 0xDEADBEEFu);
        const Result r = dbg.render_layer(Layer::Composite, 255, comp.data(), RENDER_WIDTH);
        const auto fb = dbg.framebuffer();
        size_t diffs = 0, transparent = 0;
        for (size_t i = 0; i < comp.size() && i < fb.size; ++i) {
            if (comp[i] != fb.data[i]) ++diffs;
            if ((comp[i] & 0xFF000000u) != 0xFF000000u) ++transparent;
        }
        const bool split = fb.data[131 * RENDER_WIDTH] != fb.data[182 * RENDER_WIDTH];
        check("INS-14-07", "render_layer(Composite, 255) after a frame is the emulator's own "
                           "framebuffer in every bit, opaque everywhere, Copper split included",
              r == Result::Ok && fb.size == comp.size() && diffs == 0 && transparent == 0 &&
                  split,
              std::to_string(diffs) + " differ, " + std::to_string(transparent) +
                  " not opaque, split=" + std::to_string(split));
    }
    {
        // STATE PRESERVATION — the verb's own guarantee (design-qt §3.7, §4),
        // measured the robust way: the machine's whole serialised state before
        // and after rendering each of the eight views is byte-identical.
        //
        // The scene is built to make every engine the views drive do something
        // that could stick: a Next paused MID-FRAME (raw line vblank_top+200)
        // by a Copper program that has already written NR 0x4A, the Layer 2
        // scroll, a palette entry and the tilemap scroll on earlier lines
        // (non-empty per-line logs for the replay to walk, and a per-line
        // snapshot split); VBLANK-tagged palette and Layer 2 writes,
        // which only the replay's final flush restores (the DVP-16c class);
        // Layer 2, the tilemap, LoRes and 128 sprites all on — the sprites
        // overlapping on one line past the per-line budget, so drawing them
        // latches BOTH port 0x303B status bits; and those bits cleared by a
        // guest read first, so a render that re-latched them would show.
        using jnext::dbg::Layer;
        using jnext::dbg::RENDER_WIDTH;
        Emulator emu;
        uint8_t guest_read = 0;
        const int fb_row = wp4d_paused_scene(emu, guest_read);
        Debugger dbg(emu);

        auto state_bytes = [&emu] {
            StateWriter measure;
            emu.save_state(measure);
            std::vector<uint8_t> out(measure.position());
            StateWriter w(out.data(), out.size());
            emu.save_state(w);
            return out;
        };
        // Each view against the state IMMEDIATELY before it, so a change is
        // attributed to the view that made it rather than to every view after.
        std::vector<uint32_t> buf(RENDER_WIDTH * 256);
        std::string moved;
        bool all_ok = true;
        for (Layer l : {Layer::Composite, Layer::UlaPrimary, Layer::UlaShadow,
                        Layer::Layer2Active, Layer::Layer2Shadow, Layer::Sprites,
                        Layer::Tilemap, Layer::Background}) {
            (void)emu.port().in(0x303B);   // the guest clears the status bits again
            const std::vector<uint8_t> before = state_bytes();
            all_ok &= dbg.render_layer(l, fb_row, buf.data(), RENDER_WIDTH) == Result::Ok;
            const std::vector<uint8_t> after = state_bytes();
            if (after != before) {
                size_t at = 0;
                while (at < after.size() && at < before.size() && after[at] == before[at]) ++at;
                moved += " view" + std::to_string(static_cast<int>(l)) + "@" +
                         std::to_string(at);
            }
        }
        check("INS-14-08", "rendering each of the eight views mid-frame leaves the machine's "
                           "serialised state byte-identical (sprite status bits, per-line "
                           "logs, vblank-tagged writes)",
              emu.debug_state().paused() && fb_row > 150 && fb_row < 256 && all_ok &&
                  (guest_read & 0x03) == 0x03 && moved.empty(),
              "paused=" + std::to_string(emu.debug_state().paused()) + " fb_row=" +
                  std::to_string(fb_row) + " guest_read=" + hex(guest_read) +
                  " changed:" + (moved.empty() ? std::string(" none") : moved));
        // The same bits read the way the guest reads them, for the two views
        // that run the sprite engine.
        uint8_t seen[2] = {};
        int n = 0;
        for (Layer l : {Layer::Sprites, Layer::Composite}) {
            (void)emu.port().in(0x303B);
            (void)dbg.render_layer(l, fb_row, buf.data(), RENDER_WIDTH);
            seen[n++] = emu.port().in(0x303B);
        }
        check("INS-14-09", "…and after the Sprites or Composite view drew 128 overlapping "
                           "sprites, the guest's next port 0x303B read sees both status bits clear",
              (seen[0] & 0x03) == 0 && (seen[1] & 0x03) == 0,
              "sprites=" + hex(seen[0]) + " composite=" + hex(seen[1]));
    }
    {
        // …and what is NOT serialised cannot leak into the future either: the
        // change-log render cursors, the per-row scratch the compositor keeps.
        // Two machines built alike; one renders all eight views while paused,
        // the other does not; both then resume and run on. The frame they were
        // paused in and the next one come out identical, as does the state.
        using jnext::dbg::Layer;
        using jnext::dbg::RENDER_WIDTH;
        auto run_twin = [](bool render, std::vector<uint32_t>& fb_out,
                           std::vector<uint8_t>& state_out) {
            Emulator emu;
            uint8_t guest_read = 0;
            const int fb_row = wp4d_paused_scene(emu, guest_read, /*ula_on=*/false);
            Debugger dbg(emu);
            if (render) {
                std::vector<uint32_t> buf(RENDER_WIDTH * 256);
                for (Layer l : {Layer::Composite, Layer::UlaPrimary, Layer::UlaShadow,
                                Layer::Layer2Active, Layer::Layer2Shadow, Layer::Sprites,
                                Layer::Tilemap, Layer::Background})
                    (void)dbg.render_layer(l, fb_row, buf.data(), RENDER_WIDTH);
            }
            // Both frames are kept: the paused frame is where a leaked per-line
            // snapshot would show, and the next one is where a leak into the
            // following frame's baseline would.
            emu.debug_state().resume();
            emu.run_frame();              // the rest of the paused frame
            const auto fb = dbg.framebuffer();
            fb_out.assign(fb.data, fb.data + fb.size);
            emu.run_frame();              // and one more
            fb_out.insert(fb_out.end(), fb.data, fb.data + fb.size);
            StateWriter measure;
            emu.save_state(measure);
            state_out.assign(measure.position(), 0);
            StateWriter w(state_out.data(), state_out.size());
            emu.save_state(w);
        };
        std::vector<uint32_t> fb_plain, fb_rendered, fb_again;
        std::vector<uint8_t>  st_plain, st_rendered, st_again;
        run_twin(false, fb_plain, st_plain);
        run_twin(false, fb_again, st_again);      // the twins' own determinism
        run_twin(true,  fb_rendered, st_rendered);
        const bool deterministic = fb_plain == fb_again && st_plain == st_again;
        check("INS-14-10", "a machine that rendered all eight views while paused runs on "
                           "exactly like its twin that did not: same next frames, same state",
              deterministic && !fb_plain.empty() && fb_rendered == fb_plain &&
                  st_rendered == st_plain,
              std::string("twins deterministic=") + std::to_string(deterministic) +
                  " fb_same=" + std::to_string(fb_rendered == fb_plain) +
                  " state_same=" + std::to_string(st_rendered == st_plain));
    }

    // =======================================================================
    // GH #278 WP4d review round 1 — INS-14-11..17 and 21: every per-scanline
    // change log the replay walks, one row each, through the view that shows
    // it (the palette log is DVP-05's and DVP-16c's).
    // Each row asserts the SPLIT (rows above WP4D_SPLIT show the baseline A,
    // the row the write landed on shows B — the replay's rewind and apply)
    // AND the DRAIN (after the render the live register is the VBLANK value
    // C — the replay's flush). The review found removing six of these calls
    // survived every row; each row below is red for its log's three.
    // =======================================================================
    {
        using jnext::dbg::Layer;
        using jnext::dbg::RENDER_WIDTH;
        constexpr int S = WP4D_SPLIT;
        auto px = [](const std::vector<uint32_t>& b, int x, int y) {
            return b[static_cast<size_t>(y) * RENDER_WIDTH + static_cast<size_t>(x)];
        };
        // TWICE, as the panel does on every paused tick, and the picture the
        // rows check is the SECOND one. A render that skips a log's rewind can
        // still draw the first picture right — the frame start zeroes each
        // log's cursor — but the second starts from the cursor the first left
        // at the end of the log. Both renders must agree, bit for bit.
        auto render = [](Debugger& d, Layer l) {
            std::vector<uint32_t> first(RENDER_WIDTH * 256, 0xDEADBEEFu);
            std::vector<uint32_t> b(RENDER_WIDTH * 256, 0xDEADBEEFu);
            const bool ok = d.render_layer(l, 255, first.data(), RENDER_WIDTH) == Result::Ok &&
                            d.render_layer(l, 255, b.data(), RENDER_WIDTH) == Result::Ok &&
                            first == b;
            if (!ok) b.assign(b.size(), 0xDEADBEEFu);
            return b;
        };

        // ── INS-14-11: NR 0x15 (Renderer's sprite-enable / priority log) ──
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            wp4d_nr(emu, 0x68, 0x80);                     // ULA off: fallback shows
            wp4d_sprite0(emu, 0x77, 100, S - 8);          // cells 200..231, rows 92..107
            wp4d_nr(emu, 0x15, 0x02);                     // A: sprites off
            wp4d_pause_in_vblank(emu);
            wp4d_tag_row(emu, S);          wp4d_nr(emu, 0x15, 0x03);   // B: on
            wp4d_tag_row(emu, WP4D_VBLANK); wp4d_nr(emu, 0x15, 0x07);  // C: on, LSU
            const auto b = render(dbg, Layer::Composite);
            const uint32_t fallback = Renderer::rrrgggbb_to_argb(0xE3);
            const uint32_t sprite   = emu.palette().sprite_colour(0x77);
            check("INS-14-11", "NR 0x15 log: the Composite view shows no sprite above the "
                               "row NR 0x15 enabled it on and the sprite from that row; "
                               "after the render the live NR 0x15 is its VBLANK value",
                  sprite != fallback && px(b, 210, S - 1) == fallback &&
                      px(b, 210, S) == sprite && emu.renderer().sprite_en() &&
                      emu.renderer().layer_priority() == 1,
                  "row" + std::to_string(S - 1) + "=" + hex(px(b, 210, S - 1)) + " row" +
                      std::to_string(S) + "=" + hex(px(b, 210, S)) + " fallback=" +
                      hex(fallback) + " sprite=" + hex(sprite) + " live prio=" +
                      std::to_string(emu.renderer().layer_priority()));
        }

        // ── INS-14-12: NR 0x6B (Tilemap's control log) ────────────────────
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            wp4d_nr(emu, 0x6E, 0x20);                     // map at bank-5 0x2000
            wp4d_nr(emu, 0x6F, 0x30);                     // tiles at bank-5 0x3000
            uint8_t* b5 = emu.mmu().bank5_vram();
            std::fill(b5 + 0x2000, b5 + 0x2000 + 80 * 32 * 2, 0x00);   // tile 0, attr 0
            std::fill(b5 + 0x3000, b5 + 0x3000 + 32, 0xFF);
            // Standard mode: every pixel is nibble 0xF = the NR 0x4C index, so
            // transparent. Text mode: every pixel is bit 1, palette entry 1, opaque.
            wp4d_nr(emu, 0x6B, 0x80);                     // A: standard
            wp4d_pause_in_vblank(emu);
            wp4d_tag_row(emu, S);           wp4d_nr(emu, 0x6B, 0x88);  // B: text
            wp4d_tag_row(emu, WP4D_VBLANK); wp4d_nr(emu, 0x6B, 0xC8);  // C: text, 80-col
            const auto b = render(dbg, Layer::Tilemap);
            check("INS-14-12", "NR 0x6B log: the Tilemap view is transparent above the row "
                               "text mode was switched on and opaque from it; after the "
                               "render the live NR 0x6B is its VBLANK value",
                  px(b, 40, S - 1) == 0x00000000u && (px(b, 40, S) >> 24) == 0xFF &&
                      dbg.nextreg_peek(0x6B) == 0xC8,
                  "row" + std::to_string(S - 1) + "=" + hex(px(b, 40, S - 1)) + " row" +
                      std::to_string(S) + "=" + hex(px(b, 40, S)) + " live=" +
                      hex(dbg.nextreg_peek(0x6B)));
        }

        // ── INS-14-13: the attribute mux (Mmu, G12 Nirvana class) ─────────
        //
        // One attribute byte, character row 8 = framebuffer rows 96..103,
        // rewritten on row 100: the cell changes colour half-way down. The
        // mux's resolved value is scratch the live renderer also re-derives
        // (it rewinds before every read), so the drain is asserted on the
        // mux itself — the one place removing its flush can be seen.
        //
        // Each write states its beam position with attr_mux_set_write_pos(),
        // as a CPU write does (fuse_z80_writebyte), so the row pins the replay
        // and nothing else. (Writing it found the defect where a non-CPU write
        // inherited the machine's last CPU write's position — fixed in the same
        // round, mmu_integration_test G12-TAG-01..05.)
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            uint8_t* b5 = emu.mmu().bank5_vram();
            std::fill(b5, b5 + 0x1800, 0x00);             // all paper
            std::fill(b5 + 0x1800, b5 + 0x1B00, 0x00);
            constexpr uint16_t OFF = 8 * 32 + 10;         // char row 8, column 10
            emu.mmu().write(0x5800 + OFF, 0x08);          // A: paper 1
            wp4d_pause_in_vblank(emu);
            const int vbt = emu.video_timing().vblank_top();
            wp4d_tag_row(emu, S);
            emu.mmu().attr_mux_set_write_pos(vbt + S, 0);
            emu.mmu().write(0x5800 + OFF, 0x10);                                  // B: paper 2
            wp4d_tag_row(emu, WP4D_VBLANK);
            emu.mmu().attr_mux_set_write_pos(vbt + WP4D_VBLANK, 0);
            emu.mmu().write(0x5800 + OFF, 0x20);                                  // C: paper 4
            const auto b = render(dbg, Layer::UlaPrimary);
            const int x = 64 + 2 * (8 * 10) + 4;
            const uint32_t pa = emu.palette().ula_colour(false, 0x11);
            const uint32_t pb = emu.palette().ula_colour(false, 0x12);
            check("INS-14-13", "attribute mux: one cell shows its baseline attribute above "
                               "the row it was rewritten on and the new one from it; after "
                               "the render the mux resolves the byte to its VBLANK write",
                  pa != pb && px(b, x, S - 1) == pa && px(b, x, S) == pb &&
                      emu.mmu().attr_mux5().current(OFF) == 0x20,
                  "row" + std::to_string(S - 1) + "=" + hex(px(b, x, S - 1)) + " row" +
                      std::to_string(S) + "=" + hex(px(b, x, S)) + " want " + hex(pa) +
                      "/" + hex(pb) + " mux=" + hex(emu.mmu().attr_mux5().current(OFF)));
        }

        // ── INS-14-14: ULA scroll (NR 0x26) ────────────────────────────────
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            uint8_t* b5 = emu.mmu().bank5_vram();
            for (int i = 0; i < 0x1800; ++i)              // even columns ink, odd paper
                b5[i] = (i & 1) ? 0x00 : 0xFF;
            std::fill(b5 + 0x1800, b5 + 0x1B00, 0x0A);    // ink 2, paper 1
            wp4d_nr(emu, 0x26, 0);                        // A: column 10 shows column 10
            wp4d_pause_in_vblank(emu);
            wp4d_tag_row(emu, S);           wp4d_nr(emu, 0x26, 8);    // B: one column on
            wp4d_tag_row(emu, WP4D_VBLANK); wp4d_nr(emu, 0x26, 24);   // C: three on
            const auto b = render(dbg, Layer::UlaPrimary);
            const int x = 64 + 2 * (8 * 10 + 4);
            const uint32_t ink   = emu.palette().ula_colour(false, 0x02);
            const uint32_t paper = emu.palette().ula_colour(false, 0x11);
            check("INS-14-14", "ULA scroll log: an even column shows ink above the row the "
                               "X scroll moved one column and paper from it; after the "
                               "render the live NR 0x26 is its VBLANK value",
                  ink != paper && px(b, x, S - 1) == ink && px(b, x, S) == paper &&
                      emu.ula().get_ula_scroll_x_coarse() == 24,
                  "row" + std::to_string(S - 1) + "=" + hex(px(b, x, S - 1)) + " row" +
                      std::to_string(S) + "=" + hex(px(b, x, S)) + " live=" +
                      std::to_string(emu.ula().get_ula_scroll_x_coarse()));
        }

        // ── INS-14-15: the palette-select log (NR 0x43 b1-3, NR 0x6B b4) ──
        //
        // The ULA lane in the picture (DVP-PALSEL pins the Layer 2, sprite and
        // tilemap lanes); C also flips the Layer 2 lane, so the drain is
        // visible although the ULA bit is the same in B and C.
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            uint8_t* b5 = emu.mmu().bank5_vram();
            std::fill(b5, b5 + 0x1800, 0x00);             // all paper
            std::fill(b5 + 0x1800, b5 + 0x1B00, 0x08);    // paper 1 = index 0x11
            wp4d_nr(emu, 0x43, 0x40);                     // write-select ULA second
            wp4d_nr(emu, 0x40, 0x11);
            wp4d_nr(emu, 0x41, 0xE0);                     // bank 1 entry 0x11 = red
            wp4d_nr(emu, 0x43, 0x00);                     // A: ULA first, Layer 2 first
            wp4d_pause_in_vblank(emu);
            wp4d_tag_row(emu, S);           wp4d_nr(emu, 0x43, 0x02);  // B: ULA second
            wp4d_tag_row(emu, WP4D_VBLANK); wp4d_nr(emu, 0x43, 0x06);  // C: + Layer 2 second
            const auto b = render(dbg, Layer::UlaPrimary);
            const uint32_t first  = emu.palette().ula_colour(false, 0x11);
            const uint32_t second = emu.palette().ula_colour(true, 0x11);
            check("INS-14-15", "palette-select log: the ULA view is in the first palette "
                               "above the row NR 0x43 selected the second and in the second "
                               "from it; after the render the live selectors are the "
                               "VBLANK value's",
                  first != second && px(b, 300, S - 1) == first &&
                      px(b, 300, S) == second && emu.ula().get_active_ula_palette() &&
                      emu.ula().get_active_layer2_palette(),
                  "row" + std::to_string(S - 1) + "=" + hex(px(b, 300, S - 1)) + " row" +
                      std::to_string(S) + "=" + hex(px(b, 300, S)) + " want " +
                      hex(first) + "/" + hex(second) + " live l2=" +
                      std::to_string(emu.ula().get_active_layer2_palette()));
        }

        // ── INS-14-16: the sprite attribute log (multiplexing) ────────────
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            wp4d_sprite0(emu, 0x77, 40, S - 8);           // A: cells 80..111
            wp4d_nr(emu, 0x15, 0x03);
            wp4d_pause_in_vblank(emu);
            auto move = [&emu](uint8_t x) {
                emu.port().out(0x303B, 0x00);
                emu.port().out(0x57, x);
                emu.port().out(0x57, S - 8);
                emu.port().out(0x57, 0x00);
                emu.port().out(0x57, 0x80);
            };
            wp4d_tag_row(emu, S);           move(120);    // B: cells 240..271
            wp4d_tag_row(emu, WP4D_VBLANK); move(200);    // C: cells 400..431
            const auto b = render(dbg, Layer::Sprites);
            auto opaque = [&](int x, int y) { return (px(b, x, y) >> 24) != 0; };
            check("INS-14-16", "sprite attribute log: the Sprites view has sprite 0 at its "
                               "old X above the row it was moved on and at the new X from "
                               "it; after the render the live X is its VBLANK value",
                  opaque(90, S - 1) && !opaque(250, S - 1) && !opaque(90, S) &&
                      opaque(250, S) && dbg.sprites()[0].x == 200,
                  "row" + std::to_string(S - 1) + " old/new=" +
                      std::to_string(opaque(90, S - 1)) + std::to_string(opaque(250, S - 1)) +
                      " row" + std::to_string(S) + " old/new=" + std::to_string(opaque(90, S)) +
                      std::to_string(opaque(250, S)) + " live x=" +
                      std::to_string(dbg.sprites()[0].x));
        }

        // ── INS-14-17: the Timex screen-mode log (port 0xFF, via NR 0x69) ─
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            uint8_t* b5 = emu.mmu().bank5_vram();
            std::fill(b5, b5 + 0x1800, 0xFF);             // screen 0: all ink
            std::fill(b5 + 0x1800, b5 + 0x1B00, 0x0A);    // ink 2, paper 1
            std::fill(b5 + 0x2000, b5 + 0x3800, 0x00);    // screen 1: all paper
            std::fill(b5 + 0x3800, b5 + 0x3B00, 0x0A);
            wp4d_nr(emu, 0x69, 0x00);                     // A: standard, screen 0
            wp4d_pause_in_vblank(emu);
            wp4d_tag_row(emu, S);           wp4d_nr(emu, 0x69, 0x01);  // B: screen 1
            wp4d_tag_row(emu, WP4D_VBLANK); wp4d_nr(emu, 0x69, 0x02);  // C: hi-colour
            const auto b = render(dbg, Layer::UlaPrimary);
            const uint32_t ink   = emu.palette().ula_colour(false, 0x02);
            const uint32_t paper = emu.palette().ula_colour(false, 0x11);
            check("INS-14-17", "Timex screen-mode log: the ULA view shows screen 0 above the "
                               "row the mode switched to screen 1 and screen 1 from it; "
                               "after the render the live mode is its VBLANK value",
                  ink != paper && px(b, 300, S - 1) == ink && px(b, 300, S) == paper &&
                      emu.ula().get_screen_mode_reg() == 0x02,
                  "row" + std::to_string(S - 1) + "=" + hex(px(b, 300, S - 1)) + " row" +
                      std::to_string(S) + "=" + hex(px(b, 300, S)) + " live mode=" +
                      hex(emu.ula().get_screen_mode_reg()));
        }

        // ── INS-14-21: the Layer 2 log (bank, scroll — beast.nex's parallax) ──
        {
            Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
            Debugger dbg(emu);
            // 256x192, bank 9, every pixel's index = its column; the default
            // Layer 2 palette maps index i to RRRGGGBB i, so every column is its
            // own colour (NR 0x14 = 0xE3 makes only column 0xE3 transparent).
            // Physical bank 9 + 16: the Next's ROM-in-SRAM shift (layer2.vhd:172).
            constexpr uint32_t BANK_BASE = (9u + 16u) * 16384u;
            for (uint32_t y = 0; y < 192; ++y)
                for (uint32_t x = 0; x < 256; ++x)
                    emu.ram().write(BANK_BASE + y * 256u + x, static_cast<uint8_t>(x));
            wp4d_nr(emu, 0x12, 9);
            wp4d_nr(emu, 0x69, 0x80);                     // Layer 2 on
            wp4d_nr(emu, 0x16, 0);                        // A: X scroll 0
            wp4d_pause_in_vblank(emu);
            wp4d_tag_row(emu, S);           wp4d_nr(emu, 0x16, 8);    // B
            wp4d_tag_row(emu, WP4D_VBLANK); wp4d_nr(emu, 0x16, 24);   // C
            const auto b = render(dbg, Layer::Layer2Active);
            const int x = 64 + 2 * 20;                    // source column 20
            const uint32_t col20 = emu.palette().layer2_colour(20);
            const uint32_t col28 = emu.palette().layer2_colour(28);
            check("INS-14-21", "Layer 2 log: column 20 shows its own pixel above the row "
                               "the X scroll moved 8 and column 28's from it; after the "
                               "render the live NR 0x16 is its VBLANK value",
                  col20 != col28 && px(b, x, S - 1) == col20 && px(b, x, S) == col28 &&
                      emu.layer2().scroll_x() == 24,
                  "row" + std::to_string(S - 1) + "=" + hex(px(b, x, S - 1)) + " row" +
                      std::to_string(S) + "=" + hex(px(b, x, S)) + " want " + hex(col20) +
                      "/" + hex(col28) + " live=" + std::to_string(emu.layer2().scroll_x()));
        }
    }

    // =======================================================================
    // GH #278 WP4d review round 1 — INS-14-18..20: the port 0x303B bits the
    // GUEST's own frame latched survive a render. INS-14-08/09 only rendered
    // from all-clear, so a restore of 0 and a save that dropped a bit both
    // passed them. Each scene is run twice, without a render (the premise:
    // what the guest reads) and with the Sprites and Composite views drawn.
    // =======================================================================
    {
        auto status_row = [](const char* id, const char* desc, int kind, uint8_t want) {
            uint8_t plain_again = 0, drawn_again = 0;
            bool unused = false, rendered = false;
            const uint8_t plain = wp4d_status_scene(kind, /*render=*/false, plain_again, unused);
            const uint8_t drawn = wp4d_status_scene(kind, /*render=*/true, drawn_again, rendered);
            check(id, desc,
                  rendered && plain == want && drawn == want && plain_again == 0 &&
                      drawn_again == 0,
                  "rendered=" + std::to_string(rendered) + " without render " + hex(plain) +
                      " then " + hex(plain_again) + ", with render " + hex(drawn) +
                      " then " + hex(drawn_again) + ", want " + hex(want) + " then 0");
        };
        status_row("INS-14-18", "both port 0x303B bits the guest's frame latched are still "
                                "set after the Sprites and Composite views, and read-clear",
                   0, 0x03);
        status_row("INS-14-19", "a latched COLLISION bit alone survives the views, and "
                                "read-clears",
                   1, 0x01);
        status_row("INS-14-20", "a latched MAX-SPRITES bit alone survives the views, and "
                                "read-clears",
                   2, 0x02);
    }

    // =======================================================================
    // INS-16 / IN-02 / IN-03 — the input surface, read and injected
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("INS-16-01", "an untouched matrix reads all-released (active-LOW)",
              dbg.input_state().matrix[0] == 0xFF &&
              dbg.input_state().matrix[7] == 0xFF);
        // GH #276 B4 — IN-02 is applied at the NEXT FRAME EDGE (§4.5 REQ-dsl-20),
        // so these rows run one frame between the set and the read. What they
        // pin is unchanged — the position, the level, the active-low encoding;
        // the edge itself is IN-ORD-01..08.
        const Result pressed = dbg.set_key(1, 7, 0, true);
        emu.run_frame();
        check("IN-02-01", "set_key() presses one matrix position (at the next frame edge)",
              pressed == Result::Ok && dbg.input_state().matrix[7] == 0xFE,
              hex(dbg.input_state().matrix[7]));
        const Result released = dbg.set_key(1, 7, 0, false);
        emu.run_frame();
        check("IN-02-02", "and releases it",
              released == Result::Ok && dbg.input_state().matrix[7] == 0xFF);
        check("IN-02-03", "an out-of-range position is refused, not clamped",
              dbg.set_key(1, 8, 0, true) == Result::RefusedUnavailable &&
              dbg.set_key(1, 0, 5, true) == Result::RefusedUnavailable);
        const Result ext = dbg.set_extended_key(1, 3, true);
        emu.run_frame();
        check("IN-02-04", "set_extended_key() sets the NR 0xB0 readback bit (active-HIGH), "
                          "at the next frame edge",
              ext == Result::Ok && (dbg.input_state().ext_keys & 0x0008) != 0);
        check("IN-02-05", "an out-of-range extended id is refused",
              dbg.set_extended_key(1, 16, true) == Result::RefusedUnavailable);
        check("IN-03-01", "set_joystick() sets a connector's 12 bits",
              dbg.set_joystick(1, jnext::dbg::JoystickSide::Left, 0x0005) == Result::Ok &&
              dbg.input_state().joy_left12 == 0x0005);
        check("IN-04-01", "press_nmi() is accepted for both buttons",
              dbg.press_nmi(1, jnext::dbg::NmiButton::Mf) == Result::Ok &&
              dbg.press_nmi(1, jnext::dbg::NmiButton::Drive) == Result::Ok);
        // THE SIBLING PAIR, press_nmi vs the F9/F10 hotkey seam: the same
        // button line on BOTH sides of the one gate the seam applies — the DivMMC
        // port enable (NR 0x83 bit 0; zxnext.vhd:6349). With the port off the
        // drive button must not be pressed, exactly as F10 cannot press it.
        auto drive_line = [](bool port_on, bool via_verb) {
            Emulator e; build(e);
            Debugger d(e);
            e.divmmc().set_port_io_enable(port_on);
            if (via_verb) d.press_nmi(1, jnext::dbg::NmiButton::Drive);
            else          e.on_hotkey_f10_divmmc_nmi();
            return e.nmi_source().divmmc_button();
        };
        check("IN-04-02", "press_nmi(Drive) is the F10 hotkey: pressed with the DivMMC "
                          "port enabled, NOT pressed with it disabled — both matching F10",
              drive_line(true, true) && drive_line(true, false) &&
                  !drive_line(false, true) && !drive_line(false, false));
        auto mf_line = [](bool via_verb) {
            Emulator e; build(e);
            Debugger d(e);
            if (via_verb) d.press_nmi(1, jnext::dbg::NmiButton::Mf);
            else          e.on_hotkey_f9_mf_nmi();
            return e.nmi_source().mf_button();
        };
        check("IN-04-03", "press_nmi(Mf) is the F9 hotkey: it raises the Multiface button line",
              mf_line(true) && mf_line(false));
    }

    // =======================================================================
    // ST-01 / ST-02 — save and load state bytes
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("ST-01-01", "a machine that has run no partial frame is at a boundary",
              dbg.at_frame_boundary());
        const auto saved = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::RefuseMidFrame);
        check("ST-01-02", "save_state_bytes() yields a non-empty snapshot",
              saved.status == Result::Ok && saved.value.size() > 1024,
              std::to_string(saved.value.size()) + " bytes");

        dbg.set_register(1, RegId::BC, 0xBEEF);
        check("ST-02-01", "load_state_bytes() restores it",
              dbg.load_state_bytes(1, saved.value.data(), saved.value.size()) == Result::Ok &&
              dbg.registers().BC != 0xBEEF);
        check("ST-02-02", "an empty buffer is refused",
              dbg.load_state_bytes(1, nullptr, 0) == Result::RefusedUnavailable);
    }

    // =======================================================================
    // CAP-SYM — symbols
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("SYM-01", "a missing MAP file is refused, not silently empty",
              dbg.load_map("/nonexistent/none.map",
                           jnext::dbg::MapFormat::Z88dk).status ==
                  Result::RefusedUnavailable);
        check("SYM-02", "an unloaded table looks up nothing",
              !dbg.lookup(0x8000).has_value() && dbg.symbols().empty());
        check("SYM-03", "clear_symbols() is always Ok",
              dbg.clear_symbols() == Result::Ok);

        // GH #278 WP0 — the case the Qt Map menu got wrong (it tested the
        // loader's int as a bool): a readable Z88DK map with no `; addr` line
        // is a successful load of ZERO symbols, not a failure.
        const std::string consts = "/tmp/jnext_gh278_consts.map";
        {
            std::ofstream f(consts);
            f << "__SIZE = $0010 ; const, public\n";
        }
        const auto zero = dbg.load_map(consts, jnext::dbg::MapFormat::Z88dk);
        check("SYM-10", "a readable Z88DK map with no `; addr` symbols loads zero "
                        "(Ok, 0), not a refusal",
              zero.status == Result::Ok && zero.value == 0 && dbg.symbols().empty(),
              std::string(jnext::dbg::result_name(zero.status)) + " " +
                  std::to_string(zero.value));
        std::remove(consts.c_str());
    }

    // =======================================================================
    // PEND — the verbs a later sub-package owns refuse, they do not misbehave
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        // The three PEND-B2 rows that used to sit here asserted the REFUSAL of
        // subscribe()/subscriptions()/probe_execute(). B2 implements all three,
        // so the refusal is gone and the rows moved to the EVT-* section below,
        // which asserts what they now do. What survives here is the pair that is
        // still true of a fresh backend: nothing is subscribed, and nothing has
        // been delivered.
        check("PEND-B2-01", "a fresh backend has no subscriptions and no delivery history",
              dbg.subscriptions(true).empty() && dbg.events_fired_since(0).empty());
        // The two PEND-B3 rows that used to sit here asserted the REFUSAL of
        // `attach()` and an inert `pump()`. B3 implemented both, so they are
        // gone rather than inverted: what they were pinning is now pinned by
        // SES-01-* and SES-03-* below, against the real behaviour.
        // PEND-B4-01 (press_key refuses) retired by B4: IN-01-01..09 pin it.
        // PEND-B4-02 (coverage off and all-zero) retired by B4: coverage is
        // implemented, and INS-20-01 asserts the same fresh-backend answer.
        // PEND-B4-03 (screenshot refuses) retired by B4: CAP-01-01..09 pin it.
        // PEND-14-01 (render_layer refuses Unsupported) retired by package Q
        // WP4d: the verb is implemented (`debugger_render.cpp`), and
        // INS-14-02..09 pin what it now does, refusals included.
        // probe_execute over the LEGACY model only — the subscription half is
        // EVT-PROBE-* below. Kept here because it is the one thing in this block
        // that was already answered for real rather than refused.
        emu.debug_state().breakpoints().add_pc(0x1234);
        check("PEND-B2-02", "probe_execute() answers over today's breakpoint set",
              dbg.probe_execute(0x1234) && !dbg.probe_execute(0x1235));
    }

    // =======================================================================
    // §4.2a — the mutation log exists for every write path
    //
    // The LINE's content is not asserted here (it goes to spdlog, and pinning a
    // log format is B5's business if anyone's). What is asserted is that a
    // refused write changes nothing: the property a MUTATE line would otherwise
    // be claiming falsely.
    // =======================================================================
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const uint8_t src[2] = { 0x5A, 0xA5 };
        uint8_t before[2] = {};
        dbg.peek(MemSpace::cpu(), 0x0000, 2, before);      // ROM, on a 48K
        dbg.poke(1, MemSpace::cpu(), 0x0000, 2, src);      // ROM: ignored by Mmu
        uint8_t after[2] = {};
        dbg.peek(MemSpace::cpu(), 0x0000, 2, after);
        check("MUTLOG-01", "poke(Cpu) into ROM changes nothing (Mmu::write ignores it)",
              before[0] == after[0] && before[1] == after[1]);
        check("MUTLOG-02", "a refused poke(Rom) transfers no bytes",
              dbg.poke(1, MemSpace::rom(0), 0, 2, src).value == 0);
    }
    // =======================================================================
    // INS-02 / GH #281 F1 — poke(Cpu) COUNTS WHAT LANDED (CAP-INS-02,
    // REQ-gdb-6). Every byte is offered as the CPU's write would be; `value` is
    // the number `Mmu::write` actually stored — an overlay's RAM counts, ROM
    // does not — and the status is `Ok` iff all of them landed,
    // `RefusedReadOnly` otherwise.
    // =======================================================================
    {
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(16);
        Log::debugger()->sinks().push_back(ring);
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const uint8_t src[2] = { 0x5A, 0xA5 };
        uint8_t before[2] = {};
        dbg.peek(MemSpace::cpu(), 0x0000, 2, before);
        const auto w = dbg.poke(1, MemSpace::cpu(), 0x0000, 2, src);
        // And the ONE-byte form — what a one-byte RSP `M` or a Memory-panel
        // edit sends, and the case with its own MUTATE line (old -> new).
        const uint8_t rom5 = emu.mmu().peek(0x0005);
        const uint8_t v5   = static_cast<uint8_t>(rom5 ^ 0xFF);
        const auto    w1   = dbg.poke(1, MemSpace::cpu(), 0x0005, 1, &v5);
        uint8_t after[2] = {};
        dbg.peek(MemSpace::cpu(), 0x0000, 2, after);
        int mutates = 0;
        for (const auto& l : ring->last_formatted())
            if (l.find("MUTATE") != std::string::npos) ++mutates;
        auto& sinks = Log::debugger()->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), ring), sinks.end());
        check("INS-02-22", "poke(Cpu) wholly onto ROM (a 48K's 0x0000, 2 bytes, and 0x0005, 1 "
                           "byte) is RefusedReadOnly with a count of 0, ROM is unchanged, and no "
                           "MUTATE line claims a write",
              w.status == Result::RefusedReadOnly && w.value == 0 && before[0] == after[0] &&
                  before[1] == after[1] && w1.status == Result::RefusedReadOnly &&
                  w1.value == 0 && emu.mmu().peek(0x0005) == rom5 && mutates == 0,
              std::string(jnext::dbg::result_name(w.status)) + " " + std::to_string(w.value) +
                  " mutates=" + std::to_string(mutates));
    }
    {
        // The plain ROM/RAM straddle (48K 0x3FFE-0x4001): the RAM half lands.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const uint8_t rom0 = emu.mmu().peek(0x3FFE), rom1 = emu.mmu().peek(0x3FFF);
        const uint8_t src[4] = { static_cast<uint8_t>(rom0 ^ 0xFF),
                                 static_cast<uint8_t>(rom1 ^ 0xFF), 0x5A, 0x6B };
        const auto w = dbg.poke(1, MemSpace::cpu(), 0x3FFE, 4, src);
        check("INS-02-23", "poke(Cpu) straddling ROM and RAM lands the RAM bytes and reports "
                           "them: RefusedReadOnly with a count of 2, ROM unchanged",
              w.status == Result::RefusedReadOnly && w.value == 2 &&
                  emu.mmu().peek(0x3FFE) == rom0 && emu.mmu().peek(0x3FFF) == rom1 &&
                  emu.mmu().peek(0x4000) == 0x5A && emu.mmu().peek(0x4001) == 0x6B,
              std::string(jnext::dbg::result_name(w.status)) + " " + std::to_string(w.value));
    }
    {
        // Layer 2 write-over (port 0x123B bit 0) over a ROM slot: the byte goes
        // to the Layer 2 page, reads there still come from ROM.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.port().out(0x123B, 0x01);
        const uint8_t  rom  = emu.mmu().peek(0x0010);
        const uint16_t page = static_cast<uint16_t>(dbg.nextreg_peek(0x12) * 2);
        const uint8_t  v    = static_cast<uint8_t>(rom ^ 0xFF);
        const auto     w    = dbg.poke(1, MemSpace::cpu(), 0x0010, 1, &v);
        uint8_t l2 = 0;
        dbg.peek(MemSpace::page(page), 0x0010, 1, &l2);
        check("INS-02-24", "poke(Cpu) under Layer 2 write-over at 0x0010 LANDS — Ok, count 1, "
                           "the byte in the Layer 2 page — while the CPU view there still "
                           "reads ROM",
              dbg.mmu_slots()[0].is_rom && w.status == Result::Ok && w.value == 1 && l2 == v &&
                  emu.mmu().peek(0x0010) == rom,
              std::string(jnext::dbg::result_name(w.status)) + " l2=" + hex(l2));
    }
    {
        // DivMMC paged in (port 0xE3 conmem): 0x0000-0x1FFF is its ROM
        // (read-only), 0x2000-0x3FFF its RAM bank 0 — over a ROM slot.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.port().out(0x00E3, 0x80);
        const uint8_t src[2] = { 0x3C, 0xC3 };
        const auto    w = dbg.poke(1, MemSpace::cpu(), 0x2000, 2, src);
        check("INS-02-25", "poke(Cpu) into DivMMC RAM (conmem, 0x2000) over a ROM slot LANDS — "
                           "Ok, count 2 — and reads back through the CPU view",
              dbg.mmu_slots()[1].is_rom && w.status == Result::Ok && w.value == 2 &&
                  emu.mmu().peek(0x2000) == 0x3C && emu.mmu().peek(0x2001) == 0xC3,
              std::string(jnext::dbg::result_name(w.status)) + " " + std::to_string(w.value));

        const uint8_t d0 = emu.mmu().peek(0x1FFE), d1 = emu.mmu().peek(0x1FFF);
        const uint8_t mix[4] = { static_cast<uint8_t>(d0 ^ 0xFF), static_cast<uint8_t>(d1 ^ 0xFF),
                                 0x11, 0x22 };
        const auto m = dbg.poke(1, MemSpace::cpu(), 0x1FFE, 4, mix);
        check("INS-02-26", "a range straddling DivMMC ROM and DivMMC RAM reports the partial "
                           "count: RefusedReadOnly, 2 — the RAM half landed, the ROM half did not",
              m.status == Result::RefusedReadOnly && m.value == 2 &&
                  emu.mmu().peek(0x1FFE) == d0 && emu.mmu().peek(0x1FFF) == d1 &&
                  emu.mmu().peek(0x2000) == 0x11 && emu.mmu().peek(0x2001) == 0x22,
              std::string(jnext::dbg::result_name(m.status)) + " " + std::to_string(m.value));

        // mapram (port 0xE3 bit 6) makes DivMMC RAM bank 3 read-only at
        // 0x2000-0x3FFF: the same write is now dropped.
        // Its 0x0000-0x1FFF half (DivMMC ROM, reached here through the
        // mapram path rather than the conmem-only one) stays read-only too.
        emu.port().out(0x00E3, 0xC3);
        const uint8_t r3 = emu.mmu().peek(0x2100);
        const uint8_t v3 = static_cast<uint8_t>(r3 ^ 0xFF);
        const auto    b3 = dbg.poke(1, MemSpace::cpu(), 0x2100, 1, &v3);
        const uint8_t r0 = emu.mmu().peek(0x0100);
        const uint8_t v0 = static_cast<uint8_t>(r0 ^ 0xFF);
        const auto    b0 = dbg.poke(1, MemSpace::cpu(), 0x0100, 1, &v0);
        check("INS-02-29", "under mapram DivMMC RAM bank 3 at 0x2000-0x3FFF and the 0x0000 half "
                           "are read-only: RefusedReadOnly, 0, unchanged, for both",
              b3.status == Result::RefusedReadOnly && b3.value == 0 &&
                  emu.mmu().peek(0x2100) == r3 && b0.status == Result::RefusedReadOnly &&
                  b0.value == 0 && emu.mmu().peek(0x0100) == r0,
              std::string(jnext::dbg::result_name(b3.status)) + " " + std::to_string(b3.value) +
                  " / " + jnext::dbg::result_name(b0.status) + " " + std::to_string(b0.value));
    }
    {
        // The Multiface overlay (NMI + the M1 fetch at 0x0066 latch mf_enable):
        // 0x0000-0x1FFF is its ROM half (read-only), 0x2000-0x3FFF its RAM.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.multiface().set_enabled(true);
        emu.multiface().button_press();
        emu.multiface().on_m1(0x0066, /*mreq_low=*/true);
        const uint8_t rom = emu.mmu().peek(0x1FFF);
        const uint8_t src[2] = { static_cast<uint8_t>(rom ^ 0xFF), 0x77 };
        const auto    w = dbg.poke(1, MemSpace::cpu(), 0x1FFF, 2, src);
        check("INS-02-27", "under the Multiface overlay a range across its ROM and RAM halves "
                           "counts 1: the RAM byte (0x2000) landed, the ROM byte did not",
              emu.multiface().is_mem_active() && w.status == Result::RefusedReadOnly &&
                  w.value == 1 && emu.mmu().peek(0x1FFF) == rom && emu.mmu().peek(0x2000) == 0x77,
              std::string(jnext::dbg::result_name(w.status)) + " " + std::to_string(w.value));
    }
    {
        // The alt-ROM write-over (NR 0x8C bits 7+6, config mode off): a ROM-slot
        // write lands in the alt-ROM SRAM while reads stay on the normal ROM.
        // And config mode (tbblue.fw's ROM loader): a ROM-slot write lands in
        // SRAM. Both are writes the CPU makes, so both count.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.mmu().set_config_mode(false);
        dbg.nextreg_write(1, 0x8C, 0xC0);
        const uint8_t v   = 0x42;
        const auto    alt = dbg.poke(1, MemSpace::cpu(), 0x0010, 1, &v);
        dbg.nextreg_write(1, 0x8C, 0x00);
        emu.mmu().set_config_mode(true);
        const auto cfg = dbg.poke(1, MemSpace::cpu(), 0x0010, 1, &v);
        emu.mmu().set_config_mode(false);
        const auto rom = dbg.poke(1, MemSpace::cpu(), 0x0010, 1, &v);
        check("INS-02-28", "a ROM-slot write the alt-ROM write-over or config mode takes counts "
                           "as landed (Ok, 1); the same write with neither is RefusedReadOnly, 0",
              dbg.mmu_slots()[0].is_rom && alt.status == Result::Ok && alt.value == 1 &&
                  cfg.status == Result::Ok && cfg.value == 1 &&
                  rom.status == Result::RefusedReadOnly && rom.value == 0,
              std::string(jnext::dbg::result_name(alt.status)) + "/" +
                  jnext::dbg::result_name(cfg.status) + "/" + jnext::dbg::result_name(rom.status));
    }
    {
        // §4.2a: the BACKEND emits `MUTATE <what> <old> -> <new> by <client>`
        // for every client's write. Captured off the live logger, because a
        // claim that something is logged is only worth what reading the log
        // proves.
        // GH #276 B3 — the `debugger` channel, not `emulator`: SES-06 got its own
        // `--log-level` name in B3 (`Log::debugger()`, gated against the man
        // page's LOGGING list by log_test LOG-10/11), and the mutation log goes
        // through it. A row still reading `emulator` would pass only while the
        // two were the same channel.
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(16);
        Log::debugger()->sinks().push_back(ring);

        Emulator emu; build(emu);
        Debugger dbg(emu);
        const size_t before = ring->last_formatted().size();
        dbg.set_register(9, RegId::DE, 0x1234);
        const auto lines = ring->last_formatted();
        const std::string last = lines.empty() ? std::string() : lines.back();
        check("MUTLOG-03", "a mutation emits one line, and only one",
              lines.size() == before + 1, std::to_string(lines.size()));
        check("MUTLOG-04", "it carries the target, the old and new values and the client",
              last.find("MUTATE reg DE") != std::string::npos &&
              last.find("-> 0x1234") != std::string::npos &&
              last.find("by 9") != std::string::npos,
              last);

        const size_t after_write = ring->last_formatted().size();
        uint8_t scratch = 0;
        (void)dbg.registers();
        dbg.peek(MemSpace::cpu(), 0x8000, 1, &scratch);
        check("MUTLOG-05", "a READ emits no MUTATE line",
              ring->last_formatted().size() == after_write);

        Log::debugger()->sinks().pop_back();
    }


    // =======================================================================
    // GAPS FOUND BY DERIVING MUTATIONS FROM THE DIFF
    //
    // The B1 review's verdict was that a 27-mutation table built from the ROW
    // LIST reached none of the five behaviours that had no row at all. Every row
    // below exists because a function this branch shipped had no assertion on
    // it: the wrong ROM image, the wrong palette bank, a copper view that is not
    // the live RAM, the two legacy paging ports, five of the six ULA screen
    // registers, two of the four clip windows, the symbol-substituting
    // disassembly, and eight verbs with no row whatsoever.
    // =======================================================================
    {
        // ROM IMAGES — `Mmu::rom_image_ptr()` picks one of four 16 KB images and
        // nothing asserted it picks the RIGHT one. On a NEXT machine an image is
        // SRAM pages `2*index` / `2*index+1` addressed WITHOUT the to_sram_page
        // shift (§4 INS-02), so `Ram::page_ptr()` reaches them directly and a
        // distinct byte per image makes the index observable. Choosing the Next
        // over the +3 also pins that un-shifted addressing, which is the
        // subtlety `Page{}` cannot express.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        for (int img = 0; img < 4; ++img)
            emu.ram().page_ptr(static_cast<uint16_t>(img * 2))[0] =
                static_cast<uint8_t>(0xB0 + img);
        uint8_t r[4] = {};
        bool all_read = true, all_right = true;
        for (int img = 0; img < 4; ++img) {
            all_read = all_read &&
                dbg.peek(MemSpace::rom(static_cast<uint16_t>(img)), 0, 1, &r[img]).status
                    == Result::Ok;
            all_right = all_right && r[img] == static_cast<uint8_t>(0xB0 + img);
        }
        check("INS-02-20", "all four ROM images are readable", all_read);
        check("INS-02-21", "and the index selects the image — four distinct bytes",
              all_right,
              hex(r[0]) + " " + hex(r[1]) + " " + hex(r[2]) + " " + hex(r[3]));
        // A ROM slot's SlotInfo must name the image the slot is actually serving,
        // which is the composition round 4 of the design review caught two
        // frontends getting wrong.
        //
        // PRECONDITION, stated because the row depends on it and it is not
        // self-evident: slot 0 of a freshly-initialised Next IS ROM-mapped, and
        // the bytes written above land in ROM image 0 (SRAM pages 0/1), which is
        // the image slot 0 serves — so the CPU view at 0x0000 and the slot's own
        // `space` + `space_offset` must read the SAME byte. The row asserts
        // `is_rom` first, so if that precondition ever stops holding the row says
        // so instead of comparing two irrelevant reads.
        const auto slots = dbg.mmu_slots();
        check("INS-03-09", "the precondition: slot 0 of a fresh Next is ROM-mapped",
              slots[0].is_rom && slots[0].space.kind == MemSpace::Kind::Rom);
        uint8_t via_slot[4] = {}, via_cpu[4] = {};
        dbg.peek(slots[0].space, slots[0].space_offset, 4, via_slot);
        dbg.peek(MemSpace::cpu(), 0x0000, 4, via_cpu);
        check("INS-03-07", "a ROM slot's space+offset read the same bytes as the CPU view",
              std::memcmp(via_slot, via_cpu, 4) == 0);
    }
    {
        // THE LEGACY PAGING PORTS — only 0x7FFD had a row, so a mutation
        // swapping 1FFD and DFFD survived.
        Emulator emu; build(emu, MachineType::ZX_PLUS3);
        Debugger dbg(emu);
        dbg.port_out(1, 0x7FFD, 0x05);
        dbg.port_out(1, 0x1FFD, 0x04);
        const auto pp = dbg.paging_ports();
        check("INS-03-08", "paging_ports() reports 0x7FFD and 0x1FFD as last written, not swapped",
              pp.port_7ffd == 0x05 && pp.port_1ffd == 0x04,
              hex(pp.port_7ffd) + "/" + hex(pp.port_1ffd));
    }
    {
        // THE RASTER AND TIME COUNTERS — INS-06-01 only bounded them, so a
        // mutation swapping hc and vc passed (311 <= 447 is true).
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.run_frame();
        emu.snapshot_raster();
        const auto ras = dbg.raster();
        check("INS-06-02", "raster() reports the machine's own paused hc/vc, not swapped",
              ras.raw_hc == emu.paused_hc() && ras.raw_vc == emu.paused_vc(),
              std::to_string(ras.raw_hc) + "/" + std::to_string(ras.raw_vc));
        const auto t = dbg.time();
        check("INS-07-03", "time()'s raw counters are the same pair, in the same order",
              t.hc_raw == emu.paused_hc() && t.vc_raw == emu.paused_vc());

        // MachineInfo's five untested fields.
        const auto mi = dbg.machine();
        const RasterPos origin = emu.video_timing().display_origin();
        check("INS-19-04", "machine() carries the display origin and vblank_top of the live timing",
              mi.display_origin_hc == origin.hc && mi.display_origin_vc == origin.vc &&
              mi.vblank_top == emu.video_timing().vblank_top());
        check("INS-19-05", "and the two blanking limits, not each other",
              mi.max_hblank == emu.video_timing().max_hblank() &&
              mi.max_vblank == emu.video_timing().max_vblank() &&
              mi.max_hblank != mi.max_vblank);
        check("INS-19-06", "cpu_divisor is the live clock's, and NR 0x07 moves it",
              mi.cpu_divisor == emu.clock().cpu_divisor());
    }
    {
        // THE COPPER VIEW — INS-09-01 proved only size and non-null, so a
        // mutation handing out a static buffer survived. Write a real
        // instruction through the NR 0x60/0x61 pair and read it back through the
        // view.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.copper().write_reg_0x61(0x00);          // NR 0x61: write address = 0
        emu.copper().write_reg_0x60(0x12);          // NR 0x60: MSB (even address)
        emu.copper().write_reg_0x60(0x34);          // NR 0x60: LSB commits 0x1234
        const auto cs = dbg.copper();
        check("INS-09-03", "copper()'s program is the LIVE instruction RAM",
              cs.program.data[0] == emu.copper().instruction(0) &&
              cs.program.data[0] == 0x1234,
              hex(cs.program.data[0]));
    }
    {
        // THE PALETTE BANKS — every palette row read a ULA or Layer2 bank, so
        // `entry_rgb333`'s sprite and tilemap arms had no row and a mutation
        // returning the ULA store for them survived.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        using jnext::dbg::PaletteId;
        struct { PaletteId id; uint16_t v; } banks[] = {
            {PaletteId::UlaFirst, 0x001}, {PaletteId::Layer2First, 0x002},
            {PaletteId::SpriteFirst, 0x004}, {PaletteId::TilemapFirst, 0x008},
            {PaletteId::UlaSecond, 0x010}, {PaletteId::Layer2Second, 0x020},
            {PaletteId::SpriteSecond, 0x040}, {PaletteId::TilemapSecond, 0x080},
        };
        for (const auto& b : banks) dbg.set_palette(1, b.id, 33, b.v);
        bool all_distinct = true;
        for (const auto& b : banks)
            all_distinct = all_distinct && dbg.palette(b.id)[33] == b.v;
        check("INS-15-11", "all EIGHT hardware palette banks are separate stores",
              all_distinct);
        check("INS-15-12", "sprite_palette_rgb333() reads the sprite bank it is asked for",
              dbg.sprite_palette_rgb333(0, 33).value == 0x004 &&
              dbg.sprite_palette_rgb333(1, 33).value == 0x040);
    }
    {
        // THE ULA SCREEN REGISTERS — one of six had a row.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.ula().set_ula_scroll_x_coarse(0x21);
        emu.ula().set_ula_scroll_y(0x43);
        dbg.port_out(1, 0x00FF, 0x02);                 // Timex screen-mode latch
        emu.ula().set_ula_enabled(false);
        const auto us = dbg.ula_screen_regs();
        check("INS-15-13", "ula_screen_regs() carries NR 0x26/0x27 the right way round",
              us.scroll_x == 0x21 && us.scroll_y == 0x43,
              hex(us.scroll_x) + "/" + hex(us.scroll_y));
        check("INS-15-14", "and the port 0xFF Timex latch",
              us.port_ff == emu.ula().get_screen_mode_reg() && us.port_ff == 0x02);
        check("INS-15-15", "and the ULA enable, which is NR 0x68 b7 INVERTED",
              !us.enabled);
        emu.ula().set_ula_enabled(true);
        check("INS-15-16", "which flips back",
              dbg.ula_screen_regs().enabled);
        // Through the PORT, not Mmu::set_port_7ffd_bit3(): the Ula holds its own
        // shadow-screen flag and the port handler is what syncs it, so poking the
        // Mmu alone proves nothing about what the ULA displays.
        dbg.port_out(1, 0x7FFD, 0x08);
        check("INS-15-17", "the active ULA bank follows port 0x7FFD bit 3 (5 -> 7)",
              dbg.ula_screen_regs().active_bank == 7 &&
              emu.ula().get_shadow_screen_en());
        emu.palette().write_control(0x02);
        check("INS-15-18", "second_palette agrees with active_ula_palette_bank()",
              dbg.ula_screen_regs().second_palette &&
              dbg.active_ula_palette_bank() == 1);
    }
    {
        // THE FOUR CLIP WINDOWS — Layer2 and Sprites had rows; ULA and Tilemap
        // did not, so a mutation returning the Layer 2 window for all four
        // survived. Four distinct values, so no two arms can alias.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        using jnext::dbg::ClipLayer;
        emu.layer2().set_clip_x1(0x11);
        emu.sprites().set_clip_x1(0x22);
        emu.ula().set_clip_x1(0x33);
        emu.tilemap().set_clip_x1(0x44);
        check("INS-15-19", "each clip window comes from its OWN layer",
              dbg.clip_window(ClipLayer::Layer2).x1  == 0x11 &&
              dbg.clip_window(ClipLayer::Sprites).x1 == 0x22 &&
              dbg.clip_window(ClipLayer::Ula).x1     == 0x33 &&
              dbg.clip_window(ClipLayer::Tilemap).x1 == 0x44,
              hex(dbg.clip_window(ClipLayer::Ula).x1));
    }
    {
        // THE AUDIO MODE SIGNALS — INS-10-04 compared turbosound_enabled()
        // against its own accessor and nothing read ay_mode / stereo_mode at
        // all, so a reversed polarity on either was invisible.
        Emulator emu; build(emu, MachineType::ZXN_ISSUE2);
        Debugger dbg(emu);
        emu.turbosound().set_ay_mode(false);
        check("INS-10-05", "ay_mode() reports YM for the YM curve",
              dbg.ay_mode() == jnext::dbg::AyChipMode::Ym);
        emu.turbosound().set_ay_mode(true);
        check("INS-10-06", "and AY for the AY curve — the polarity is not reversed",
              dbg.ay_mode() == jnext::dbg::AyChipMode::Ay);
        emu.turbosound().set_stereo_mode(false);
        check("INS-10-07", "stereo_mode() reports ABC for NR 0x08 b5 clear",
              dbg.stereo_mode() == jnext::dbg::StereoMode::Abc);
        emu.turbosound().set_stereo_mode(true);
        check("INS-10-08", "and ACB when it is set",
              dbg.stereo_mode() == jnext::dbg::StereoMode::Acb);
        emu.turbosound().set_enabled(false);
        check("INS-10-09", "turbosound_enabled() follows the live enable",
              !dbg.turbosound_enabled());

        // ay_registers(chip) — INS-10-01 proved the SIZE. Nothing proved the
        // chip index selects a chip, so reading chip 0 for every index survived.
        // Ports 0xFFFD/0xBFFD: 0xFF|chip selects the AY, then reg + value.
        emu.turbosound().set_enabled(true);
        auto write_ay = [&](uint8_t chip, uint8_t reg, uint8_t val) {
            emu.turbosound().reg_addr(static_cast<uint8_t>(0xFC | (~chip & 0x03)));
            emu.turbosound().reg_addr(reg);
            emu.turbosound().reg_write(val);
        };
        write_ay(0, 0x02, 0x11);
        write_ay(1, 0x02, 0x22);
        write_ay(2, 0x02, 0x33);
        const auto a0 = dbg.ay_registers(0), a1 = dbg.ay_registers(1), a2 = dbg.ay_registers(2);
        check("INS-10-10", "ay_registers(chip) reads THAT chip's file",
              a0.value[2] == 0x11 && a1.value[2] == 0x22 && a2.value[2] == 0x33,
              hex(a0.value[2]) + "/" + hex(a1.value[2]) + "/" + hex(a2.value[2]));
    }
    {
        // INS-16's two composed port values had no row.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        dbg.set_joystick(1, jnext::dbg::JoystickSide::Left, 0x0001);
        dbg.set_joystick(1, jnext::dbg::JoystickSide::Right, 0x0002);
        const auto in = dbg.input_state();
        check("INS-16-02", "input_state() carries the two composed port reads",
              in.port_1f == emu.joystick().read_port_1f() &&
              in.port_37 == emu.joystick().read_port_37() &&
              in.port_1f != in.port_37,
              hex(in.port_1f) + "/" + hex(in.port_37));
        check("INS-16-03", "and the two connectors are not each other",
              in.joy_left12 == 0x0001 && in.joy_right12 == 0x0002);
    }
    {
        // INS-11's SYMBOL path had no row: `disassemble(.., &symbols())` was
        // never called with a non-empty table, so the substitution could not
        // have worked and nothing would have said so.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const std::string map_path = "/tmp/jnext_b1_syms.map";
        {
            std::ofstream f(map_path);
            f << "MY_TARGET = $9000 ; const\n";
        }
        const auto loaded = dbg.load_map(map_path, jnext::dbg::MapFormat::Simple);
        check("SYM-04", "load_map() loads a simple MAP file and counts its symbols",
              loaded.status == Result::Ok && loaded.value == 1,
              std::to_string(loaded.value));
        check("SYM-05", "lookup() and lookup_name() answer for it, both ways",
              dbg.lookup(0x9000).has_value() && *dbg.lookup(0x9000) == "MY_TARGET" &&
              dbg.lookup_name("MY_TARGET").has_value() &&
              *dbg.lookup_name("MY_TARGET") == 0x9000);
        check("SYM-06", "and symbols() is the same one table",
              dbg.symbols().size() == 1);
        // 0x8002 is `CALL 0x9000` — the immediate must be substituted.
        const auto with = dbg.disassemble(PROG + 2, 1, &dbg.symbols());
        const auto without = dbg.disassemble(PROG + 2, 1, nullptr);
        check("SYM-07", "disassemble() substitutes the symbol for the 16-bit immediate",
              std::string(with[0].mnemonic).find("MY_TARGET") != std::string::npos,
              with[0].mnemonic);
        check("SYM-08", "and leaves it alone when no table is passed",
              std::string(without[0].mnemonic).find("MY_TARGET") == std::string::npos &&
              std::string(without[0].mnemonic).find("9000") != std::string::npos,
              without[0].mnemonic);
        check("SYM-09", "clear_symbols() empties it",
              dbg.clear_symbols() == Result::Ok && dbg.symbols().empty() &&
              !dbg.lookup(0x9000).has_value());
        std::remove(map_path.c_str());
    }
    {
        // EIGHT VERBS WITH NO ROW AT ALL.
        Emulator emu; build(emu);
        Debugger dbg(emu);

        // SES-04 — the stop policy round-trip.
        check("SES-04-01", "stop_policy() starts at Pause (the Qt default)",
              dbg.stop_policy() == jnext::dbg::StopPolicy::Pause);
        check("SES-04-02", "and set_stop_policy() round-trips ExitNonZero",
              dbg.set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero) == Result::Ok &&
              dbg.stop_policy() == jnext::dbg::StopPolicy::ExitNonZero);

        // ST-03 — the rewind enable and the resize, which is what makes
        // capacity move.
        check("ST-03-05", "rewind_enabled() is false without --rewind-buffer-size",
              !dbg.rewind_enabled());
        check("ST-03-06", "set_rewind_enabled(true) round-trips",
              dbg.set_rewind_enabled(true) == Result::Ok && dbg.rewind_enabled());
        // GH #278 WP3 (manager decision 2026-09-29) — 0 FREES the ring, as the
        // Emulator accessor CAP-ST-03 names does; B1 refused it, which left the
        // Qt window's Rewind Buffer Size… = 0 with no published way to free.
        // (Expected value flipped from RefusedUnavailable; ST-03-09 pins the
        // free on a ring that exists.)
        check("ST-03-07", "resize_rewind_buffer(0) with no ring is accepted and "
                          "leaves none",
              dbg.resize_rewind_buffer(0) == Result::Ok &&
              dbg.rewind_range().capacity == 0);
        check("ST-03-08", "resize_rewind_buffer(n) sets the capacity to n",
              dbg.resize_rewind_buffer(16) == Result::Ok &&
              dbg.rewind_range().capacity == 16,
              std::to_string(dbg.rewind_range().capacity));

        // INS-13 — the trace export.
        const std::string trace_path = "/tmp/jnext_b1_trace.txt";
        dbg.set_trace_enabled(true);
        emu.execute_single_instruction();
        check("INS-13-06", "trace_export() writes the log to a file",
              dbg.trace_export(trace_path) == Result::Ok);
        {
            std::ifstream f(trace_path);
            std::string first;
            std::getline(f, first);
            check("INS-13-07", "and the file is not empty", !first.empty(), first);
        }
        std::remove(trace_path.c_str());
        check("INS-13-08", "an unwritable path is refused, not silently dropped",
              dbg.trace_export("/nonexistent-dir/x/y.txt") == Result::RefusedUnavailable);
    }
    {
        // TIME-03 `run_to_frame` — a declared verb with no row at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.run_frame();
        attach_and_pause(emu);
        const uint32_t here = dbg.time().frame;
        check("TIME-03-01", "run_to_frame() to a PAST frame is refused (forward only)",
              dbg.run_to_frame(1, here) == Result::RefusedUnavailable &&
              dbg.run_to_frame(1, 0) == Result::RefusedUnavailable);
        check("TIME-03-02", "run_to_frame(here + 2) is accepted",
              dbg.run_to_frame(1, here + 2) == Result::Ok);
        run_until_paused(emu, 5);
        check("TIME-03-03", "and the machine stops in that frame, not the next one",
              dbg.state().paused && dbg.time().frame == here + 2,
              "frame=" + std::to_string(dbg.time().frame) +
                  " want=" + std::to_string(here + 2));
    }
    {
        // THE NEVER-RUN STATE — the third of the three, and the one the second
        // cut of this verb still got wrong. `frame_tag()` clamps, so `tag == 0`
        // means both "frame 0 finished" and "nothing has run yet"; a base of
        // `tag + 1` is right for the first and one too many for the second, and
        // `run_to_frame(3)` landed in frame 2. Nothing has run here at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        check("TIME-03-04", "run_to_frame() is accepted on a machine that has run nothing",
              emu.frame_num() == 0 && dbg.run_to_frame(1, 3) == Result::Ok);
        run_until_paused(emu, 6);
        check("TIME-03-05", "and it lands in frame 3, not one short of it",
              dbg.state().paused && dbg.time().frame == 3,
              "frame=" + std::to_string(dbg.time().frame));
    }
    {
        // THE MID-FRAME ARM of the same conditional. Both rows above arm at a
        // FRAME BOUNDARY, so a mutation that breaks only the in-progress arm
        // survived them — round 1's BLOCKER-1 pattern (one arm of a condition
        // tested) recurring in code round 2 added. Stopping mid-frame first is
        // what reaches it.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        emu.debug_state().pause();
        emu.debug_state().run_to_cycle(emu.clock().get() + 5000);
        emu.run_frame();                       // stops part-way through frame 0
        check("TIME-03-06", "the precondition: the machine is mid-frame, not at a boundary",
              !dbg.at_frame_boundary() && dbg.time().frame == 0,
              "frame=" + std::to_string(dbg.time().frame));
        check("TIME-03-07", "run_to_frame() from mid-frame is accepted",
              dbg.run_to_frame(1, 2) == Result::Ok);
        run_until_paused(emu, 6);
        check("TIME-03-08", "and lands in frame 2 — the in-progress arm is not off by one",
              dbg.state().paused && dbg.time().frame == 2,
              "frame=" + std::to_string(dbg.time().frame));
    }
    {
        // ST-01's OTHER mode: AdvanceToBoundary, which is the reason the verb
        // gained a `by`. Mid-frame, so the advance actually runs.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        emu.debug_state().pause();
        emu.debug_state().run_to_cycle(emu.clock().get() + 5000);
        emu.run_frame();                             // stops mid-frame
        check("ST-01-03", "the machine is mid-frame after a run-to-cycle stop",
              !dbg.at_frame_boundary());
        check("ST-01-04", "RefuseMidFrame refuses there",
              dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::RefuseMidFrame)
                  .status == Result::NotAtFrameBoundary);
        const auto adv = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("ST-01-05", "AdvanceToBoundary runs the frame out and saves there",
              adv.status == Result::Ok && adv.value.size() > 1024 &&
              dbg.at_frame_boundary());
        check("ST-01-06", "and the advance left the debugger's own state alone (SuspendScope)",
              emu.debug_state().paused());
    }
    {
        // Keyboard::set_matrix_bit became PUBLIC, so its own bounds guard is now
        // API surface. The Debugger refuses first (IN-02-03), so this calls the
        // keyboard directly — the only way to reach the guard at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.keyboard().set_matrix_bit(9, 0, true);    // out of range: row > 7
        emu.keyboard().set_matrix_bit(0, 7, true);    // out of range: col > 4
        bool all_released = true;
        for (int r = 0; r < 8; ++r)
            all_released = all_released && dbg.input_state().matrix[r] == 0xFF;
        check("IN-02-06", "an out-of-range set_matrix_bit changes no row of the matrix",
              all_released);
        check("IN-02-07", "and matrix_row() answers 0xFF for a row that does not exist",
              emu.keyboard().matrix_row(9) == 0xFF &&
              emu.keyboard().matrix_row(-1) == 0xFF);
    }

    // =======================================================================
    // The DMA slot flag (§4.3) — B1's fifth accessor addition.
    //
    // It has no consumer until B2 tags an event's `source` with it, so it is
    // pinned here on BOTH sides: a CPU slot must clear it and a DMA burst slot
    // must set it. A one-sided row would pass on a flag that is always false,
    // which is exactly what an unwired member would be.
    // =======================================================================
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        emu.init(cfg);
        emu.nextreg().write(0x54, 0x20);          // slot 4 -> plain SRAM page
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0x8000 + i),
                            static_cast<uint8_t>(0xA0 + i));
        // The CPU program lives at 0xC000, well clear of the 0x8000 source and
        // the 0x9000 destination — writing it over the source is how the first
        // cut of this row "proved" a burst that had in fact copied zeros.
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0xC000; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        emu.mmu().write(0xC000, 0x00);            // NOP
        emu.mmu().write(0xC001, 0x00);            // NOP

        emu.execute_single_instruction();
        check("DMA-SLOT-01", "a CPU instruction slot does not read as the DMA's",
              !emu.slot_ran_dma());

        // Program a mem->mem burst (the dma_test G23 idiom) and run the slot
        // the DMA holds the bus for.
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        w(0x7D); w(0x00); w(0x80); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();
        bool copied = true;
        for (int i = 0; i < 8; ++i)
            copied = copied && emu.mmu().read(static_cast<uint16_t>(0x9000 + i)) ==
                                   static_cast<uint8_t>(0xA0 + i);
        check("DMA-SLOT-02", "the burst really ran (8 bytes copied)", copied);
        check("DMA-SLOT-03", "and that slot reads as the DMA's, not the CPU's",
              emu.slot_ran_dma());

        emu.execute_single_instruction();
        check("DMA-SLOT-04", "the flag is per-slot, not sticky: the next CPU slot clears it",
              !emu.slot_ran_dma());
    }


    // =======================================================================
    // GH #276 B2 — §4.3 CAP-EVT.
    //
    // THE ROWS ARE DERIVED FROM THE DIFF, function by function, not from the
    // list of things B2 set out to build — that is the lesson B1 paid for (a
    // 27-mutation table built from its own rows, and an independent reviewer
    // then found five survivors in code with no row at all).
    //
    // Two tiers, deliberately:
    //
    //   EVT-TBL-*  the `EventTable` DIRECTLY, as a value: ids, the live
    //              formula, the slot masks, `filter_matches` arm by arm, the
    //              ring and the history. Pure logic, so it is tested pure —
    //              driving a whole emulator to reach one `if` is how arms end
    //              up with no row.
    //   EVT-*      the SITES and the DELIVERY, through a running machine: the
    //              latch really fires from inside an instruction, the drain
    //              really delivers at the boundary, the payload really carries
    //              what the site saw.
    //
    // BOTH ARMS OF EVERY CONDITIONAL GET A ROW. Twice on this branch a
    // condition was right in the state the rows happened to arm and wrong in
    // another, so every gate here has its negative row next to its positive one.
    // =======================================================================

    // ── EVT-TBL — the table as a value ──────────────────────────────────
    {
        EventTable t;
        Subscription a;
        a.kind = EventKind::Execute;
        a.filter.lo = 0x1000; a.filter.hi = 0x1FFF;
        const EventId id1 = t.add(11, a);
        const EventId id2 = t.add(22, a);
        check("EVT-TBL-01", "add() returns a fresh id, never EVENT_NONE",
              id1 != jnext::dbg::EVENT_NONE && id2 != jnext::dbg::EVENT_NONE &&
              id1 != id2);
        check("EVT-TBL-02", "the entry carries its owner and its kind",
              t.find(id1)->owner == 11 && t.find(id2)->owner == 22 &&
              t.find(id1)->kind == EventKind::Execute);
        check("EVT-TBL-03", "the filter is copied, not referenced",
              t.find(id1)->filter.lo == 0x1000 && t.find(id1)->filter.hi == 0x1FFF);
        check("EVT-TBL-04", "a fresh entry is live",
              t.find(id1)->live && t.find(id2)->live);
        check("EVT-TBL-05", "has_kind() reports the kind that is armed",
              t.has_kind(EventKind::Execute));
        check("EVT-TBL-06", "and NOT one that is not",
              !t.has_kind(EventKind::Mem) && !t.has_kind(EventKind::Dma));

        check("EVT-TBL-07", "erase() removes it and reports so",
              t.erase(id1) && t.find(id1) == nullptr);
        check("EVT-TBL-08", "erasing the same id twice is false, not a crash",
              !t.erase(id1));
        check("EVT-TBL-09", "erasing an id that never existed is false",
              !t.erase(9999));
        check("EVT-TBL-10", "the other client's subscription is untouched",
              t.find(id2) != nullptr && t.find(id2)->live);
        check("EVT-TBL-11", "a tombstone is still a row until compact()",
              t.entries().size() == 2);
        t.compact();
        check("EVT-TBL-12", "compact() removes the tombstone and only that",
              t.entries().size() == 1 && t.find(id2) != nullptr);
        check("EVT-TBL-13", "ids are never reused after a compact",
              t.add(33, a) != id1);
    }

    // The live formula — enabled AND (transient OR master) AND client. Four
    // terms, so four pairs of rows: a term that stopped mattering would
    // otherwise be invisible.
    {
        EventTable t;
        Subscription a; a.kind = EventKind::Mem; a.access = Access::Write;
        const EventId id = t.add(5, a);
        check("EVT-TBL-20", "live with master on, client on, own flag on",
              t.find(id)->live);

        t.set_enabled(id, false);
        check("EVT-TBL-21", "own flag off -> not live", !t.find(id)->live);
        check("EVT-TBL-22", "and the kind mask drops with it",
              !t.has_kind(EventKind::Mem));
        check("EVT-TBL-23", "the flag AS SET is reported, not the live value",
              !t.find(id)->enabled);
        t.set_enabled(id, true);
        check("EVT-TBL-24", "own flag back on -> live again", t.find(id)->live);

        t.set_master_enabled(false);
        check("EVT-TBL-25", "master off -> not live", !t.find(id)->live);
        check("EVT-TBL-26", "but its OWN flag survives the master round trip",
              t.find(id)->enabled);
        t.set_master_enabled(true);
        check("EVT-TBL-27", "master back on -> live, with the flag intact",
              t.find(id)->live && t.find(id)->enabled);

        t.set_client_enabled(5, false);
        check("EVT-TBL-28", "its client's switch off -> not live",
              !t.find(id)->live && !t.client_enabled(5));
        check("EVT-TBL-29", "a DIFFERENT client's switch is unaffected",
              t.client_enabled(6));
        t.set_client_enabled(5, true);
        check("EVT-TBL-30", "client back on -> live", t.find(id)->live);
        check("EVT-TBL-31", "a client never named is enabled (opt-out, not opt-in)",
              t.client_enabled(12345));
        check("EVT-TBL-32", "set_enabled on an unknown id is false",
              !t.set_enabled(4242, false));
    }

    // Transient: exempt from the MASTER switch and from nothing else (§4.3).
    {
        EventTable t;
        Subscription tr; tr.kind = EventKind::Execute; tr.transient = true;
        Subscription us; us.kind = EventKind::Execute;
        const EventId tid = t.add(7, tr);
        const EventId uid = t.add(7, us);
        t.set_master_enabled(false);
        check("EVT-TBL-40", "master off suspends the user subscription",
              !t.find(uid)->live);
        check("EVT-TBL-41", "but NOT the transient one (Step Over must keep working)",
              t.find(tid)->live);
        t.set_client_enabled(7, false);
        check("EVT-TBL-42", "a transient IS subject to its client's switch",
              !t.find(tid)->live);
        t.set_client_enabled(7, true);
        t.set_enabled(tid, false);
        check("EVT-TBL-43", "and to its own flag",
              !t.find(tid)->live);
        t.set_enabled(tid, true);
        check("EVT-TBL-44", "clear_transient() drops the transient and counts it",
              t.clear_transient() == 1 && t.find(tid) == nullptr);
        check("EVT-TBL-45", "and leaves the user subscription alone",
              t.find(uid) != nullptr);
        check("EVT-TBL-46", "a second clear_transient() drops nothing",
              t.clear_transient() == 0);
        check("EVT-TBL-47", "erase_client() drops that client's rows and counts them",
              t.erase_client(7) == 1 && t.find(uid) == nullptr);
        check("EVT-TBL-48", "erase_client() on an unknown client drops nothing",
              t.erase_client(999) == 0);
    }

    // The §6 slot masks — the ONE thing the hot path reads.
    {
        EventTable t;
        Subscription w; w.kind = EventKind::Mem; w.access = Access::Write;
        w.filter.lo = 0x9000; w.filter.hi = 0x9000;             // slot 4
        const EventId wid = t.add(1, w);
        check("EVT-TBL-60", "a WRITE range arms its slot in the write mask only",
              t.wr_slot_mask() == 0x10 && t.rd_slot_mask() == 0x00);

        Subscription r; r.kind = EventKind::Mem; r.access = Access::Read;
        r.filter.lo = 0x0000; r.filter.hi = 0x1FFF;             // slot 0
        t.add(1, r);
        check("EVT-TBL-61", "a READ range arms its slot in the read mask only",
              t.rd_slot_mask() == 0x01 && t.wr_slot_mask() == 0x10);

        Subscription rw; rw.kind = EventKind::Mem; rw.access = Access::ReadWrite;
        rw.filter.lo = 0x4000; rw.filter.hi = 0x5FFF;           // slot 2
        t.add(1, rw);
        check("EVT-TBL-62", "ReadWrite arms BOTH masks",
              (t.rd_slot_mask() & 0x04) && (t.wr_slot_mask() & 0x04));

        Subscription span; span.kind = EventKind::Mem; span.access = Access::Write;
        span.filter.lo = 0x3FFF; span.filter.hi = 0x4000;       // slots 1 and 2
        const EventId sid = t.add(1, span);
        check("EVT-TBL-63", "a range that straddles a slot boundary arms both slots",
              (t.wr_slot_mask() & 0x02) && (t.wr_slot_mask() & 0x04));
        t.erase(sid);
        check("EVT-TBL-64", "and erasing it takes its bits away again",
              (t.wr_slot_mask() & 0x02) == 0);

        // EVT-TBL-63 above cannot see a straddle that arms only its FIRST slot:
        // the `rw` subscription three rows up already armed slot 2. A fresh table
        // is what makes the claim exact — a mutation that armed only the first
        // slot of a span survived the suite until this row existed.
        EventTable t3;
        Subscription span_only; span_only.kind = EventKind::Mem;
        span_only.access = Access::Write;
        span_only.filter.lo = 0x3FFF; span_only.filter.hi = 0x4000;
        t3.add(1, span_only);
        check("EVT-TBL-68", "on a table with NOTHING else armed, a straddling range "
                            "arms BOTH its slots and only those",
              t3.wr_slot_mask() == 0x06 && t3.rd_slot_mask() == 0x00);

        t.set_enabled(wid, false);
        check("EVT-TBL-65", "a DISABLED subscription contributes no bit",
              (t.wr_slot_mask() & 0x10) == 0);
        t.set_enabled(wid, true);
        t.set_master_enabled(false);
        check("EVT-TBL-66", "master off zeroes the whole event mask",
              t.rd_slot_mask() == 0 && t.wr_slot_mask() == 0);
        t.set_master_enabled(true);
        check("EVT-TBL-67", "and master on rebuilds it exactly",
              (t.wr_slot_mask() & 0x14) == 0x14 && (t.rd_slot_mask() & 0x05) == 0x05);
    }

    // An inverted range arms nothing — the precise scan would reject every
    // address in it, so arming its slots is a gate that can never pay off.
    {
        EventTable t;
        Subscription bad; bad.kind = EventKind::Mem; bad.access = Access::ReadWrite;
        bad.filter.lo = 0x8000; bad.filter.hi = 0x4000;
        t.add(1, bad);
        check("EVT-TBL-70", "an inverted range (lo > hi) arms no slot at all",
              t.rd_slot_mask() == 0 && t.wr_slot_mask() == 0);
        check("EVT-TBL-71", "and matches no address either",
              !t.mem_would_match(0x4000, 0, Access::Write) &&
              !t.mem_would_match(0x8000, 0, Access::Write) &&
              !t.mem_would_match(0x6000, 0, Access::Write));

        // THE CASE THE `lo > hi` GUARD IS ACTUALLY FOR. With the two ends in
        // DIFFERENT slots the span loop is empty anyway (`first > last`), so the
        // row above passes with or without the guard — a mutation that removed it
        // survived the whole suite. An inverted range INSIDE ONE SLOT has
        // `first == last`, and the loop would arm that slot.
        EventTable t2;
        Subscription within; within.kind = EventKind::Mem;
        within.access = Access::ReadWrite;
        within.filter.lo = 0x1FFF; within.filter.hi = 0x1000;   // both in slot 0
        t2.add(1, within);
        check("EVT-TBL-72", "an inverted range WITHIN ONE SLOT arms nothing either",
              t2.rd_slot_mask() == 0 && t2.wr_slot_mask() == 0);
        check("EVT-TBL-73", "and matches nothing in it",
              !t2.mem_would_match(0x1000, 0, Access::Write) &&
              !t2.mem_would_match(0x1FFF, 0, Access::Read));
    }

    // The physical-page forms, and `on_slot_remapped`'s reason to exist.
    {
        EventTable t;
        Subscription pg; pg.kind = EventKind::Mem; pg.access = Access::Write;
        pg.filter.pages = { 0x2A };
        t.add(1, pg);
        check("EVT-TBL-80", "a page set with no slot holding that page arms nothing",
              t.wr_slot_mask() == 0);
        check("EVT-TBL-81", "set_slot_page() to the watched page arms that slot and says so",
              t.set_slot_page(6, 0x2A) && t.wr_slot_mask() == 0x40);
        check("EVT-TBL-82", "and the table remembers the page it was told",
              t.slot_page(6) == 0x2A);
        check("EVT-TBL-83", "re-publishing the SAME page reports no change",
              !t.set_slot_page(6, 0x2A));
        check("EVT-TBL-84", "moving the page to another slot moves the bit",
              t.set_slot_page(2, 0x2A) && (t.wr_slot_mask() & 0x04));
        check("EVT-TBL-85", "and mapping it away clears that slot's bit",
              t.set_slot_page(6, 0x07) && (t.wr_slot_mask() & 0x40) == 0);
        check("EVT-TBL-86", "an out-of-range slot is refused, not written",
              !t.set_slot_page(-1, 0x2A) && !t.set_slot_page(8, 0x2A));
        check("EVT-TBL-87", "the page form matches on the PAGE and ignores the address",
              t.mem_would_match(0x0000, 0x2A, Access::Write) &&
              t.mem_would_match(0xFFFF, 0x2A, Access::Write) &&
              !t.mem_would_match(0x0000, 0x2B, Access::Write));
    }

    // A RANGE that also carries `page` is the AND form (DZRP's bank+1), and
    // that narrowing is what makes the mask worth recomputing on a remap.
    {
        EventTable t;
        Subscription both; both.kind = EventKind::Mem; both.access = Access::Read;
        both.filter.lo = 0x4000; both.filter.hi = 0x5FFF;   // slot 2
        both.filter.page = 0x05;
        t.add(1, both);
        check("EVT-TBL-90", "the AND form arms nothing while the page is elsewhere",
              t.rd_slot_mask() == 0);
        t.set_slot_page(2, 0x05);
        check("EVT-TBL-91", "and arms exactly that slot once the page arrives",
              t.rd_slot_mask() == 0x04);
        check("EVT-TBL-92", "it matches only inside the range AND on the page",
              t.mem_would_match(0x4000, 0x05, Access::Read) &&
              !t.mem_would_match(0x4000, 0x06, Access::Read) &&
              !t.mem_would_match(0x6000, 0x05, Access::Read));
        check("EVT-TBL-93", "a slot remap with no page-qualified filter reports no change",
              !EventTable{}.set_slot_page(0, 0x12));
    }

    // mem/port/nr `would_match` — the site-side gates. Access, direction,
    // mask/value and the register set, each with both arms.
    {
        EventTable t;
        Subscription m; m.kind = EventKind::Mem; m.access = Access::Write;
        m.filter.lo = 0x8000; m.filter.hi = 0x80FF;
        t.add(1, m);
        check("EVT-TBL-100", "mem_would_match inside the range, right direction",
              t.mem_would_match(0x8000, 0, Access::Write) &&
              t.mem_would_match(0x80FF, 0, Access::Write));
        check("EVT-TBL-101", "and not one byte either side of it",
              !t.mem_would_match(0x7FFF, 0, Access::Write) &&
              !t.mem_would_match(0x8100, 0, Access::Write));
        check("EVT-TBL-102", "and not for the WRONG direction",
              !t.mem_would_match(0x8000, 0, Access::Read));

        Subscription p; p.kind = EventKind::Port; p.access = Access::ReadWrite;
        p.filter.port_mask = 0x00FF; p.filter.port_value = 0xFE;
        t.add(1, p);
        check("EVT-TBL-103", "a low-byte port filter matches every high byte",
              t.port_would_match(0x00FE, Access::Read) &&
              t.port_would_match(0x7FFE, Access::Write) &&
              t.port_would_match(0xFEFE, Access::Read));
        check("EVT-TBL-104", "and not a port whose low byte differs",
              !t.port_would_match(0x00FF, Access::Read));

        Subscription pf; pf.kind = EventKind::Port; pf.access = Access::Write;
        pf.filter.port_mask = 0xFFFF; pf.filter.port_value = 0x243B;
        const EventId pfid = t.add(1, pf);
        check("EVT-TBL-105", "a full 16-bit port filter matches exactly one port",
              t.port_would_match(0x243B, Access::Write) &&
              !t.port_would_match(0x253B, Access::Write));
        t.set_enabled(pfid, false);
        check("EVT-TBL-106", "a disabled port subscription matches nothing",
              !t.port_would_match(0x243B, Access::Write));

        Subscription nr; nr.kind = EventKind::NextRegWrite;
        nr.filter.regs = { 0x15, 0x43 };
        t.add(1, nr);
        check("EVT-TBL-107", "an NR set matches its members",
              t.nr_would_match(0x15, EventSource::Cpu) &&
              t.nr_would_match(0x43, EventSource::Copper));
        check("EVT-TBL-108", "and not a register outside it",
              !t.nr_would_match(0x16, EventSource::Cpu));

        Subscription nra; nra.kind = EventKind::NextRegWrite;
        nra.filter.source = EventSource::Copper;
        t.add(1, nra);
        check("EVT-TBL-109", "an empty NR set means every register",
              t.nr_would_match(0x00, EventSource::Copper) &&
              t.nr_would_match(0xFF, EventSource::Copper));
        check("EVT-TBL-110", "and a source filter excludes the other writers",
              !t.nr_would_match(0x16, EventSource::Dma));
    }

    // ── EVT-TBL-FM — filter_matches(), arm by arm ───────────────────────
    //
    // Fourteen kinds, and the switch has one arm each. A kind whose arm is
    // wrong is invisible to every site row (the site would latch correctly and
    // the drain would drop it), so each arm is asserted directly, in both
    // directions.
    {
        EventTable t;
        auto ev_of = [](EventKind k) { DbgEvent e; e.kind = k; return e; };

        // Execute — PC range, optional page qualifier.
        {
            Subscription s; s.kind = EventKind::Execute;
            s.filter.lo = 0x8000; s.filter.hi = 0x8002;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Execute); e.pc = 0x8001;
            check("EVT-TBL-FM-01", "Execute matches inside its PC range",
                  t.filter_matches(*t.find(id), e));
            e.pc = 0x8003;
            check("EVT-TBL-FM-02", "and not outside it",
                  !t.filter_matches(*t.find(id), e));
            e.kind = EventKind::Mem;
            check("EVT-TBL-FM-03", "a different KIND never matches, whatever the payload",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.page = 0x0A;
            DbgEvent p = ev_of(EventKind::Execute); p.pc = 0x8001; p.phys_page = 0x0A;
            check("EVT-TBL-FM-04", "the Execute page qualifier matches its page",
                  t.filter_matches(*t.find(id), p));
            p.phys_page = 0x0B;
            check("EVT-TBL-FM-05", "and rejects another",
                  !t.filter_matches(*t.find(id), p));
            t.erase(id);
        }

        // Mem — access, source, pages, range, page qualifier.
        {
            Subscription s; s.kind = EventKind::Mem; s.access = Access::Write;
            s.filter.lo = 0x4000; s.filter.hi = 0x4FFF;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Mem);
            e.addr = 0x4100; e.access = Access::Write;
            check("EVT-TBL-FM-10", "Mem{Write} matches a write in range",
                  t.filter_matches(*t.find(id), e));
            e.access = Access::Read;
            check("EVT-TBL-FM-11", "and rejects the read of the same address",
                  !t.filter_matches(*t.find(id), e));
            e.access = Access::Write; e.addr = 0x5000;
            check("EVT-TBL-FM-12", "and rejects an address past the range",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.source = EventSource::Dma;
            e.addr = 0x4100; e.source = EventSource::Cpu;
            check("EVT-TBL-FM-13", "a source filter rejects the other originator",
                  !t.filter_matches(*t.find(id), e));
            e.source = EventSource::Dma;
            check("EVT-TBL-FM-14", "and accepts its own",
                  t.filter_matches(*t.find(id), e));
            t.find(id)->filter.source = EventSource::Any;
            e.source = EventSource::Cpu;
            check("EVT-TBL-FM-15", "EventSource::Any accepts either",
                  t.filter_matches(*t.find(id), e));
            // The RANGE + `page` AND form (DZRP's bank+1), which EVT-TBL-90..92
            // pin for `mem_would_match` but nothing pinned for `filter_matches`
            // — a mutation that dropped the qualifier here survived the suite.
            t.find(id)->filter.page = 0x33;
            e.addr = 0x4100; e.phys_page = 0x33;
            check("EVT-TBL-FM-18", "a range + `page` matches only on that page",
                  t.filter_matches(*t.find(id), e));
            e.phys_page = 0x34;
            check("EVT-TBL-FM-19", "and rejects another page inside the same range",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.page = jnext::dbg::PAGE_ANY;
            e.phys_page = 0x34;
            t.find(id)->filter.pages = { 0x11 };
            e.phys_page = 0x11; e.addr = 0xFFFF;     // outside the range
            check("EVT-TBL-FM-16", "a page SET replaces the range, not narrows it",
                  t.filter_matches(*t.find(id), e));
            e.phys_page = 0x12;
            check("EVT-TBL-FM-17", "and rejects a page outside the set",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Port — mask/value, access, source.
        {
            Subscription s; s.kind = EventKind::Port; s.access = Access::Read;
            s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Port);
            e.port = 0x7FFE; e.access = Access::Read;
            check("EVT-TBL-FM-20", "Port matches on (port & mask) == value",
                  t.filter_matches(*t.find(id), e));
            e.port = 0x7FFF;
            check("EVT-TBL-FM-21", "and rejects a port the mask does not fold onto it",
                  !t.filter_matches(*t.find(id), e));
            e.port = 0x7FFE; e.access = Access::Write;
            check("EVT-TBL-FM-22", "and rejects the wrong direction",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // NextRegWrite — source and the register set.
        {
            Subscription s; s.kind = EventKind::NextRegWrite;
            s.filter.regs = { 0x15 };
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::NextRegWrite); e.reg = 0x15;
            check("EVT-TBL-FM-30", "NextRegWrite matches a register in the set",
                  t.filter_matches(*t.find(id), e));
            e.reg = 0x16;
            check("EVT-TBL-FM-31", "and rejects one outside it",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.regs.clear();
            check("EVT-TBL-FM-32", "an empty set matches every register",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Frame — FRAME_EVERY vs one number.
        {
            Subscription s; s.kind = EventKind::Frame;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Frame); e.frame = 7;
            check("EVT-TBL-FM-40", "Frame defaults to FRAME_EVERY and matches any frame",
                  t.filter_matches(*t.find(id), e));
            t.find(id)->filter.frame = 7;
            check("EVT-TBL-FM-41", "a frame number matches that frame",
                  t.filter_matches(*t.find(id), e));
            e.frame = 8;
            check("EVT-TBL-FM-42", "and not the next one",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Scanline — compared on cvc, NOT on the raw vc.
        {
            Subscription s; s.kind = EventKind::Scanline; s.filter.scanline = 100;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Scanline); e.cvc = 100; e.vc = 164;
            check("EVT-TBL-FM-50", "Scanline matches on cvc",
                  t.filter_matches(*t.find(id), e));
            e.cvc = 99;
            check("EVT-TBL-FM-51", "and not on a neighbouring cvc",
                  !t.filter_matches(*t.find(id), e));
            e.cvc = 164; e.vc = 100;
            check("EVT-TBL-FM-52", "and NOT on the raw vc (the GH #16 origin bug)",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Cycle — a threshold, not an equality.
        {
            Subscription s; s.kind = EventKind::Cycle; s.filter.cycle = 1000;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Cycle); e.cycle = 999;
            check("EVT-TBL-FM-60", "Cycle does not match before its target",
                  !t.filter_matches(*t.find(id), e));
            e.cycle = 1000;
            check("EVT-TBL-FM-61", "matches AT the target",
                  t.filter_matches(*t.find(id), e));
            e.cycle = 100000;
            check("EVT-TBL-FM-62", "and still matches past it (>=, not ==)",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Reset — and the F8 `Any` filter value, which is B2's to implement.
        {
            Subscription s; s.kind = EventKind::Reset;
            s.filter.reset_kind = ResetKind::Soft;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Reset); e.reset_kind = ResetKind::Soft;
            check("EVT-TBL-FM-70", "Reset{Soft} matches a soft reset",
                  t.filter_matches(*t.find(id), e));
            e.reset_kind = ResetKind::Hard;
            check("EVT-TBL-FM-71", "and not a hard one",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.reset_kind = ResetKind::Hard;
            check("EVT-TBL-FM-72", "Reset{Hard} matches a hard reset",
                  t.filter_matches(*t.find(id), e));
            e.reset_kind = ResetKind::Soft;
            check("EVT-TBL-FM-73", "and not a soft one",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.reset_kind = ResetKind::Any;
            check("EVT-TBL-FM-74", "ResetKind::Any matches a SOFT reset (owner decision F8)",
                  t.filter_matches(*t.find(id), e));
            e.reset_kind = ResetKind::Hard;
            check("EVT-TBL-FM-75", "and a HARD one, from the same one subscription",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // IntAck / Nmi / Magic — the seam IS the filter (§4.3 lists none).
        {
            Subscription ia; ia.kind = EventKind::IntAck;
            Subscription nm; nm.kind = EventKind::Nmi;
            Subscription mg; mg.kind = EventKind::Magic;
            const EventId i1 = t.add(1, ia), i2 = t.add(1, nm), i3 = t.add(1, mg);
            DbgEvent a = ev_of(EventKind::IntAck); a.int_vector = 0xFF; a.int_mode = 1;
            DbgEvent b = ev_of(EventKind::Nmi);    b.nmi_source = NmiButton::Drive;
            DbgEvent c = ev_of(EventKind::Magic);  c.pc = 0x1234;
            check("EVT-TBL-FM-80", "IntAck matches unconditionally",
                  t.filter_matches(*t.find(i1), a));
            check("EVT-TBL-FM-81", "Nmi matches unconditionally, whichever button",
                  t.filter_matches(*t.find(i2), b));
            check("EVT-TBL-FM-82", "Magic matches unconditionally",
                  t.filter_matches(*t.find(i3), c));
            check("EVT-TBL-FM-83", "but each still only matches its OWN kind",
                  !t.filter_matches(*t.find(i1), b) &&
                  !t.filter_matches(*t.find(i2), c) &&
                  !t.filter_matches(*t.find(i3), a));
            t.erase(i1); t.erase(i2); t.erase(i3);
        }

        // Host — the name, with an empty filter meaning every name.
        {
            Subscription s; s.kind = EventKind::Host;
            std::strcpy(s.filter.host_name, "script3");
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Host);
            std::strcpy(e.host_name, "script3");
            check("EVT-TBL-FM-90", "Host matches its exact name",
                  t.filter_matches(*t.find(id), e));
            std::strcpy(e.host_name, "script4");
            check("EVT-TBL-FM-91", "and rejects another",
                  !t.filter_matches(*t.find(id), e));
            std::strcpy(e.host_name, "script");
            check("EVT-TBL-FM-92", "a PREFIX of the filter is not a match",
                  !t.filter_matches(*t.find(id), e));
            std::strcpy(e.host_name, "script33");
            check("EVT-TBL-FM-93", "nor is the filter a prefix of the name",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.host_name[0] = '\0';
            check("EVT-TBL-FM-94", "an empty filter name matches every name",
                  t.filter_matches(*t.find(id), e));
            t.erase(id);
        }

        // Copper — sub-kind, copper-PC range, and the NR set for a MOVE.
        {
            Subscription s; s.kind = EventKind::Copper;
            s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
            s.filter.lo = 0; s.filter.hi = 15;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Copper);
            e.copper_kind = jnext::dbg::CopperEventKind::Move;
            e.copper_pc = 3; e.reg = 0x43;
            check("EVT-TBL-FM-100", "Copper{Move} matches inside the copper-PC range",
                  t.filter_matches(*t.find(id), e));
            e.copper_pc = 16;
            check("EVT-TBL-FM-101", "and not outside it",
                  !t.filter_matches(*t.find(id), e));
            e.copper_pc = 3;
            e.copper_kind = jnext::dbg::CopperEventKind::Wait;
            check("EVT-TBL-FM-102", "a Move subscription rejects a Wait",
                  !t.filter_matches(*t.find(id), e));
            e.copper_kind = jnext::dbg::CopperEventKind::Halt;
            check("EVT-TBL-FM-103", "and rejects a Halt",
                  !t.filter_matches(*t.find(id), e));
            e.copper_kind = jnext::dbg::CopperEventKind::Move;
            t.find(id)->filter.regs = { 0x43 };
            check("EVT-TBL-FM-104", "the NR set narrows a Move to its registers",
                  t.filter_matches(*t.find(id), e));
            e.reg = 0x44;
            check("EVT-TBL-FM-105", "and rejects a MOVE to another register",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.copper_kind = jnext::dbg::CopperEventKind::Wait;
            DbgEvent w = ev_of(EventKind::Copper);
            w.copper_kind = jnext::dbg::CopperEventKind::Wait;
            w.copper_pc = 3; w.reg = 0x00;
            check("EVT-TBL-FM-106", "an NR set does NOT gate a Wait (it has no register)",
                  t.filter_matches(*t.find(id), w));
            t.erase(id);
        }

        // Dma — sub-kind, and `Byte`'s EITHER-endpoint rule.
        {
            Subscription s; s.kind = EventKind::Dma;
            s.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
            s.filter.lo = 0x4000; s.filter.hi = 0x5AFF;
            const EventId id = t.add(1, s);
            DbgEvent e = ev_of(EventKind::Dma);
            e.dma_kind = jnext::dbg::DmaEventKind::Byte;
            e.dma_src = 0x4100; e.dma_dst = 0x9000;
            check("EVT-TBL-FM-110", "Dma{Byte} matches when the SOURCE is in range",
                  t.filter_matches(*t.find(id), e));
            e.dma_src = 0x9000; e.dma_dst = 0x4100;
            check("EVT-TBL-FM-111", "and when the DESTINATION is (either endpoint)",
                  t.filter_matches(*t.find(id), e));
            e.dma_src = 0x9000; e.dma_dst = 0x9100;
            check("EVT-TBL-FM-112", "and not when NEITHER is",
                  !t.filter_matches(*t.find(id), e));
            e.dma_kind = jnext::dbg::DmaEventKind::Start;
            check("EVT-TBL-FM-113", "a Byte subscription rejects a Start",
                  !t.filter_matches(*t.find(id), e));
            t.find(id)->filter.dma_kind = jnext::dbg::DmaEventKind::Start;
            e.dma_src = 0xFFFF; e.dma_dst = 0xFFFF;
            check("EVT-TBL-FM-114", "Start/End ignore the range entirely",
                  t.filter_matches(*t.find(id), e));
            e.dma_kind = jnext::dbg::DmaEventKind::End;
            check("EVT-TBL-FM-115", "and a Start subscription rejects an End",
                  !t.filter_matches(*t.find(id), e));
            t.erase(id);
        }
    }

    // ── EVT-TBL-RING — the 512-entry ring and its overflow contract ──────
    {
        EventTable t;
        check("EVT-TBL-RING-01", "a fresh ring is empty and not overflowed",
              !t.pending() && t.size() == 0 && t.dropped() == 0 && !t.overflowed());
        check("EVT-TBL-RING-02", "and its capacity is the 512 §4.3 derives",
              t.ring_capacity() == jnext::dbg::LATCH_RING_CAPACITY &&
              jnext::dbg::LATCH_RING_CAPACITY == 512);

        jnext::dbg::LatchEntry e;
        e.kind = EventKind::Mem; e.addr = 0x1234;
        t.latch(e);
        check("EVT-TBL-RING-03", "latch() appends and pending() says so",
              t.pending() && t.size() == 1 && t.at(0).addr == 0x1234);
        t.clear_ring();
        check("EVT-TBL-RING-04", "clear_ring() empties it and clears the drop count",
              !t.pending() && t.size() == 0 && t.dropped() == 0);

        // Fill to exactly capacity, then one past it.
        for (int i = 0; i < 512; ++i) {
            e.addr = static_cast<uint16_t>(i);
            t.latch(e);
        }
        check("EVT-TBL-RING-05", "512 entries fit with nothing dropped",
              t.size() == 512 && t.dropped() == 0 && !t.overflowed());
        e.addr = 0xBEEF;
        t.latch(e);
        check("EVT-TBL-RING-06", "the 513th is DROPPED, not wrapped over the first",
              t.size() == 512 && t.dropped() == 1 && t.overflowed() &&
              t.at(0).addr == 0x0000 && t.at(511).addr == 511);

        // The saturation arm, which no emulator row can reach.
        for (int i = 0; i < 70000; ++i) t.latch(e);
        check("EVT-TBL-RING-07", "the drop count SATURATES at 0xFFFF rather than wrapping",
              t.dropped() == 0xFFFF);

        t.shrink_ring_for_test(4);
        check("EVT-TBL-RING-08", "shrink_ring_for_test() resizes and clears",
              t.ring_capacity() == 4 && t.size() == 0 && t.dropped() == 0);
        check("EVT-TBL-RING-09", "it clamps a zero to 1",
              (t.shrink_ring_for_test(0), t.ring_capacity() == 1));
        check("EVT-TBL-RING-10", "and clamps past the real capacity",
              (t.shrink_ring_for_test(99999),
               t.ring_capacity() == jnext::dbg::LATCH_RING_CAPACITY));
    }

    // ── EVT-TBL-HIST — the INS-17 delivery history ──────────────────────
    {
        EventTable t;
        check("EVT-TBL-HIST-01", "a fresh history is empty at every cursor",
              t.since(0).empty() && t.since(1000).empty());
        check("EVT-TBL-HIST-02", "next_seq() is monotonic from 1",
              t.next_seq() == 1 && t.next_seq() == 2 && t.last_seq() == 2);

        for (int i = 1; i <= 5; ++i) {
            DbgEvent ev; ev.kind = EventKind::Frame; ev.seq = static_cast<uint64_t>(i);
            ev.frame = static_cast<uint32_t>(i * 10);
            t.record(ev);
        }
        check("EVT-TBL-HIST-03", "since(0) returns everything, oldest first",
              t.since(0).size() == 5 && t.since(0).front().seq == 1 &&
              t.since(0).back().seq == 5);
        check("EVT-TBL-HIST-04", "since(N) is EXCLUSIVE of N",
              t.since(3).size() == 2 && t.since(3).front().seq == 4);
        check("EVT-TBL-HIST-05", "since(last) is empty",
              t.since(5).empty());
        check("EVT-TBL-HIST-06", "and the payload survives the round trip",
              t.since(0)[2].frame == 30);

        // Wrap: the buffer is bounded, and a poller that falls behind loses the
        // OLDEST — detectably, because `seq` is in the payload.
        for (int i = 6; i <= 6 + int(EventTable::HISTORY_CAPACITY); ++i) {
            DbgEvent ev; ev.kind = EventKind::Frame; ev.seq = static_cast<uint64_t>(i);
            t.record(ev);
        }
        const auto all = t.since(0);
        check("EVT-TBL-HIST-07", "the history is bounded at HISTORY_CAPACITY",
              all.size() == EventTable::HISTORY_CAPACITY);
        check("EVT-TBL-HIST-08", "it kept the NEWEST and is still in order",
              all.back().seq == 6 + EventTable::HISTORY_CAPACITY &&
              all.front().seq < all.back().seq);
        bool ordered = true;
        for (size_t i = 1; i < all.size(); ++i)
            ordered = ordered && all[i - 1].seq < all[i].seq;
        check("EVT-TBL-HIST-09", "strictly increasing across the wrap point", ordered);
        check("EVT-TBL-HIST-10", "the oldest entries are gone, so the gap is detectable",
              all.front().seq > 1);
        t.clear_history();
        check("EVT-TBL-HIST-11", "clear_history() empties it", t.since(0).empty());
    }

    // ── EVT-BP — BreakpointSet's half of the §6 mask, and the OR ─────────
    {
        BreakpointSet bps;
        check("EVT-BP-01", "an empty set arms no slot and no port",
              bps.watch_slot_mask_rd() == 0 && bps.watch_slot_mask_wr() == 0 &&
              !bps.port_watch_armed());
        bps.add_watchpoint(0x9000, WatchType::READ);
        check("EVT-BP-02", "a READ watchpoint arms its slot in the read mask only",
              bps.watch_slot_mask_rd() == 0x10 && bps.watch_slot_mask_wr() == 0);
        check("EVT-BP-03", "and rd_watch_slot_armed() answers per address",
              bps.rd_watch_slot_armed(0x9000) && bps.rd_watch_slot_armed(0x9FFF) &&
              bps.rd_watch_slot_armed(0x8000) &&      // same 8 KB slot
              !bps.rd_watch_slot_armed(0x7FFF) &&     // the slot below
              !bps.rd_watch_slot_armed(0xA000) &&     // the slot above
              !bps.wr_watch_slot_armed(0x9000));
        bps.add_watchpoint(0x4000, WatchType::WRITE);
        check("EVT-BP-04", "a WRITE watchpoint arms the write mask only",
              bps.wr_watch_slot_armed(0x4000) && !bps.rd_watch_slot_armed(0x4000));
        bps.add_watchpoint(0xC000, WatchType::READ_WRITE);
        check("EVT-BP-05", "READ_WRITE arms both",
              bps.rd_watch_slot_armed(0xC000) && bps.wr_watch_slot_armed(0xC000));
        bps.add_watchpoint(0x00FE, WatchType::IO_READ);
        check("EVT-BP-06", "an I/O watchpoint arms the PORT flag",
              bps.port_watch_armed());
        check("EVT-BP-07", "and NO memory slot — a port is not an address (slot 0 stays clear)",
              !bps.rd_watch_slot_armed(0x0000) && !bps.wr_watch_slot_armed(0x0000));

        bps.set_watchpoint_enabled(0x9000, WatchType::READ, false);
        check("EVT-BP-08", "disabling a watchpoint takes its bit away",
              !bps.rd_watch_slot_armed(0x9000));
        bps.set_watchpoint_enabled(0x9000, WatchType::READ, true);
        check("EVT-BP-09", "and re-enabling puts it back",
              bps.rd_watch_slot_armed(0x9000));

        bps.set_master_enabled(false);
        check("EVT-BP-10", "master off zeroes every legacy bit and the port flag",
              bps.watch_slot_mask_rd() == 0 && bps.watch_slot_mask_wr() == 0 &&
              !bps.port_watch_armed());
        bps.set_master_enabled(true);
        check("EVT-BP-11", "master on rebuilds them exactly",
              bps.rd_watch_slot_armed(0x9000) && bps.wr_watch_slot_armed(0x4000) &&
              bps.port_watch_armed());

        // The EVENT half, and the OR. This is the seam between the two models.
        bps.set_event_slot_masks(0x02, 0x80, false);
        check("EVT-BP-12", "the event half is OR-ed into the bytes the hot path reads",
              bps.rd_watch_slot_armed(0x2000) && bps.wr_watch_slot_armed(0xE000));
        check("EVT-BP-13", "and the legacy half survives the publication",
              bps.rd_watch_slot_armed(0x9000) && bps.wr_watch_slot_armed(0x4000));
        bps.clear_all_watchpoints();
        check("EVT-BP-14", "clearing the legacy half leaves the event half standing",
              bps.rd_watch_slot_armed(0x2000) && bps.wr_watch_slot_armed(0xE000) &&
              !bps.rd_watch_slot_armed(0x9000));
        check("EVT-BP-15", "and the port flag follows the EVENT half too",
              (bps.set_event_slot_masks(0, 0, true), bps.port_watch_armed()));
        bps.set_event_slot_masks(0, 0, false);
        check("EVT-BP-16", "zeroing both halves closes every gate",
              bps.watch_slot_mask_rd() == 0 && bps.watch_slot_mask_wr() == 0 &&
              !bps.port_watch_armed());
        check("EVT-BP-17", "has_any_watchpoints() is still the MODEL query it was",
              !bps.has_any_watchpoints());
    }

    // ── EVT-MEM — the eight Mmu sites, and the payload they latch ────────
    {
        Emulator emu;
        //   8000  3E 5A        LD A,0x5A
        //   8002  32 00 90     LD (0x9000),A
        //   8005  3A 00 90     LD A,(0x9000)
        //   8008  18 FE        JR $
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem;
        s.access = Access::ReadWrite;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue;
        s.handler = recorder(rec);
        const auto sub = dbg.subscribe(4, s);
        check("EVT-MEM-01", "subscribe() returns Ok and an id",
              sub.status == Result::Ok && sub.value != jnext::dbg::EVENT_NONE);
        check("EVT-MEM-02", "and it armed the slot the address lives in, and only that",
              emu.debug_state().rd_watch_armed(0x9000) &&
              emu.debug_state().wr_watch_armed(0x9000) &&
              !emu.debug_state().rd_watch_armed(0x7FFF) &&
              !emu.debug_state().rd_watch_armed(0xA000));

        emu.run_frame();
        check("EVT-MEM-03", "both the write and the read were delivered, in order",
              rec.evs.size() == 2 &&
              rec.evs[0].access == Access::Write &&
              rec.evs[1].access == Access::Read,
              "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 2) {
            check("EVT-MEM-04", "the write carries the address it landed on",
                  rec.evs[0].addr == 0x9000);
            check("EVT-MEM-05", "and the byte that was written",
                  rec.evs[0].value == 0x5A);
            check("EVT-MEM-06", "and `prev`: the byte that was there BEFORE it",
                  rec.evs[0].prev == 0x00);
            check("EVT-MEM-07", "and the PC of the instruction that wrote (pc_pre_exec)",
                  rec.evs[0].pc == 0x8002, "pc=" + hex(rec.evs[0].pc));
            check("EVT-MEM-08", "and source Cpu, tagged at the drain",
                  rec.evs[0].source == EventSource::Cpu);
            check("EVT-MEM-09", "and the effective MMU page behind the address",
                  rec.evs[0].phys_page == emu.mmu().get_effective_page(4));
            check("EVT-MEM-10", "the READ carries the byte it returned",
                  rec.evs[1].value == 0x5A && rec.evs[1].addr == 0x9000);
            check("EVT-MEM-11", "a read's `prev` is the byte itself, not zero",
                  rec.evs[1].prev == 0x5A);
            check("EVT-MEM-12", "and the reading instruction's PC",
                  rec.evs[1].pc == 0x8005, "pc=" + hex(rec.evs[1].pc));
            check("EVT-MEM-13", "the subscription and its owner are named",
                  rec.evs[0].id == sub.value && rec.evs[0].owner == 4);
            check("EVT-MEM-14", "seq is monotonic across deliveries",
                  rec.evs[0].seq > 0 && rec.evs[1].seq > rec.evs[0].seq);
            check("EVT-MEM-15", "and neither delivery is flagged as lossy",
                  !rec.evs[0].overflowed && rec.evs[0].dropped == 0);
            check("EVT-MEM-16", "the common header carries a raster position",
                  rec.evs[0].vc >= 0 && rec.evs[0].hc >= 0);
            check("EVT-MEM-17", "and the cycle of the SITE, not of the boundary",
                  rec.evs[0].cycle > 0 && rec.evs[0].cycle <= emu.clock().get());
        } else {
            check("EVT-MEM-04", "the write payload", false, "no deliveries");
        }
        check("EVT-MEM-18", "events_fired_since(0) sees the same two",
              dbg.events_fired_since(0).size() == 2);
        check("EVT-MEM-19", "and since(first) sees only the second",
              rec.evs.size() == 2 &&
              dbg.events_fired_since(rec.evs[0].seq).size() == 1);
        check("EVT-MEM-20", "a Continue action left the machine running",
              !dbg.state().paused && pc_of(emu) == 0x8008,
              "pc=" + hex(pc_of(emu)));
    }

    // The control: the same program, a subscription one byte away. The gate,
    // the slot mask and the precise scan all run and all correctly reject.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::ReadWrite;
        s.filter.lo = 0x9001; s.filter.hi = 0x9001;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(4, s);
        emu.run_frame();
        check("EVT-MEM-30", "an address one byte away delivers nothing",
              rec.evs.empty() && dbg.events_fired_since(0).empty());
        check("EVT-MEM-31", "and the machine ran to the end regardless",
              pc_of(emu) == 0x8008, "pc=" + hex(pc_of(emu)));
    }

    // Direction is a filter, not decoration: a WRITE-only subscription must not
    // see the read of the same address.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(4, s);
        check("EVT-MEM-40", "a WRITE-only subscription arms only the write mask",
              emu.debug_state().wr_watch_armed(0x9000) &&
              !emu.debug_state().rd_watch_armed(0x9000));
        emu.run_frame();
        check("EVT-MEM-41", "and sees the write and not the read",
              rec.evs.size() == 1 && rec.evs[0].access == Access::Write,
              "n=" + std::to_string(rec.evs.size()));
    }

    // A Stop action, and CTL-13's reason for it. This is the row `Watch`
    // exists for: `data_bp_hit_` is consumed in the same breath as the pause,
    // so the reason has to come from somewhere else.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(9, s);
        emu.run_frame();
        check("EVT-MEM-50", "a Stop pauses the machine at the boundary of the writer",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-MEM-51", "and the pause reason is Watch",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch);
        check("EVT-MEM-52", "naming the subscription, its owner, the address and the direction",
              dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.by == 9 &&
              dbg.state().pause_reason.addr == 0x9000 &&
              dbg.state().pause_reason.access == Access::Write);
        check("EVT-MEM-53", "a resume clears the reason rather than repeating it",
              dbg.run(9) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::None);
    }

    // §4.3 — `source` is tagged AT THE DRAIN from the slot's DMA flag, so a DMA
    // byte into a watched range reads as the DMA's and not the CPU's.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xA000 + i),
                            static_cast<uint8_t>(0xA0 + i));
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x900F;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(4, s);

        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B + port A start 0xA000 + len 8; R1/R2 port config;
        // R4 mode + port B start 0x9000; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();
        check("EVT-MEM-60", "the DMA's own memory writes are delivered as Mem events",
              rec.evs.size() == 8, "n=" + std::to_string(rec.evs.size()));
        bool all_dma = !rec.evs.empty();
        for (const auto& ev : rec.evs)
            all_dma = all_dma && ev.source == EventSource::Dma;
        check("EVT-MEM-61", "each tagged source=Dma, not Cpu (the slot's DMA flag)",
              all_dma);
        check("EVT-MEM-62", "with the bytes the DMA moved",
              rec.evs.size() == 8 && rec.evs[0].value == 0xA0 &&
              rec.evs[7].value == 0xA7);
    }

    {
        // The site-level precise match is a COST gate — the drain re-matches, so
        // a spurious latch is never delivered — but what it buys is RING SPACE,
        // and that IS observable. `LD (0x9000),HL` writes 0x9000 and then 0x9001
        // inside ONE instruction; with the ring shrunk to one entry, a latch for
        // the unwatched 0x9000 would fill it and the watched 0x9001 would be
        // dropped.
        Emulator emu;
        //   8000  21 34 12     LD HL,0x1234
        //   8003  22 00 90     LD (0x9000),HL
        //   8006  18 FE        JR $
        build_armed(emu, { 0x21, 0x34, 0x12, 0x22, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9001; s.filter.hi = 0x9001;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.debug_state().event_table()->shrink_ring_for_test(1);
        emu.run_frame();
        check("EVT-MEM-70", "a write in the armed SLOT that cannot match the filter "
                            "does not consume a ring entry",
              rec.evs.size() == 1 && rec.evs[0].addr == 0x9001 &&
              rec.evs[0].value == 0x12,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-MEM-71", "so nothing was dropped and the delivery is not flagged",
              rec.evs.size() == 1 && !rec.evs[0].overflowed &&
              rec.evs[0].dropped == 0);
    }

    // ── EVT-PORT — the port sites, and "after dispatch" for a read ───────
    {
        Emulator emu;
        //   8000  3E 07        LD A,0x07
        //   8002  D3 FE        OUT (0xFE),A      -> port 0x07FE
        //   8004  DB FE        IN  A,(0xFE)      -> port 0x07FE
        //   8006  18 FE        JR $
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0xDB, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Port; s.access = Access::ReadWrite;
        s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(3, s);
        check("EVT-PORT-01", "a Port subscription arms the port gate and no memory slot",
              emu.debug_state().port_watch_armed() &&
              !emu.debug_state().rd_watch_armed(0x0000));
        emu.run_frame();
        check("EVT-PORT-02", "the OUT and the IN are both delivered, in order",
              rec.evs.size() == 2 && rec.evs[0].access == Access::Write &&
              rec.evs[1].access == Access::Read,
              "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 2) {
            check("EVT-PORT-03", "the payload carries the FULL 16-bit port, not the masked low byte",
                  rec.evs[0].port == 0x07FE, "port=" + hex(rec.evs[0].port));
            check("EVT-PORT-04", "the OUT carries the byte written",
                  rec.evs[0].value == 0x07);
            check("EVT-PORT-05", "and the writing instruction's PC",
                  rec.evs[0].pc == 0x8002, "pc=" + hex(rec.evs[0].pc));
            check("EVT-PORT-06", "the IN carries the value the guest RECEIVED "
                                 "(latched AFTER dispatch)",
                  rec.evs[1].value == emu.port().read(0x07FE),
                  "value=" + hex(rec.evs[1].value));
            check("EVT-PORT-07", "and the reading instruction's PC",
                  rec.evs[1].pc == 0x8004, "pc=" + hex(rec.evs[1].pc));
            check("EVT-PORT-08", "source is tagged Cpu for a CPU port access",
                  rec.evs[0].source == EventSource::Cpu);
        } else {
            check("EVT-PORT-03", "the port payload", false, "no deliveries");
        }
    }
    {
        // The control, and the direction filter: a WRITE-only subscription on a
        // port that is never written to.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0xDB, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Port; s.access = Access::Write;
        s.filter.port_mask = 0x00FF; s.filter.port_value = 0x1F;   // Kempston
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(3, s);
        emu.run_frame();
        check("EVT-PORT-10", "a port the program never touches delivers nothing",
              rec.evs.empty());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0xDB, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Port; s.access = Access::Read;
        s.filter.port_mask = 0x00FF; s.filter.port_value = 0xFE;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(3, s);
        emu.run_frame();
        check("EVT-PORT-11", "a READ-only port subscription sees the IN and not the OUT",
              rec.evs.size() == 1 && rec.evs[0].access == Access::Read,
              "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-NR — the ONE NextReg::write hook ─────────────────────────────
    {
        Emulator emu;
        //   8000  ED 91 15 07   NEXTREG 0x15,0x07
        //   8004  00            NOP
        //   8005  18 FE         JR $
        build_armed(emu, { 0xED, 0x91, 0x15, 0x07, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        // A NON-ZERO prior value, and that IS the row: on a fresh Emulator NR 0x15
        // reads 0, so `prev == before` was `0 == 0` and an `e.prev = 0` mutation
        // survived all 744 rows. `prev` is the field a handler needs in order to
        // undo a caught write, so nothing else constrains it for this kind.
        emu.nextreg().write(0x15, 0x5A);
        const uint8_t before = emu.nextreg().peek(0x15);
        Rec rec;
        std::vector<uint16_t> pc_at_delivery;
        Subscription s;
        s.kind = EventKind::NextRegWrite;
        s.filter.regs = { 0x15 };
        s.action = Action::Continue;
        s.handler = [&rec, &pc_at_delivery](const DbgEvent& ev, Debugger& d) {
            rec.evs.push_back(ev);
            pc_at_delivery.push_back(d.state().pc);
            return Action::Continue;
        };
        dbg.subscribe(2, s);
        emu.run_frame();
        check("EVT-NR-01", "a CPU NEXTREG write is delivered exactly once",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            check("EVT-NR-02", "with the register and the value",
                  rec.evs[0].reg == 0x15 && rec.evs[0].value == 0x07);
            check("EVT-NR-03", "and `prev`, peeked at the hook — asserted against a "
                                "NON-ZERO prior value, so a hard-coded zero cannot pass",
                  rec.evs[0].prev == before && before == 0x5A,
                  "prev=" + hex(rec.evs[0].prev) + " before=" + hex(before));
            check("EVT-NR-04", "source Cpu",
                  rec.evs[0].source == EventSource::Cpu);
            check("EVT-NR-05", "`pc` names the WRITER, not the delivery point",
                  rec.evs[0].pc == 0x8000, "pc=" + hex(rec.evs[0].pc));
            check("EVT-NR-06", "and the delivery really IS one instruction later "
                               "(§4.3: the CPU queue commits after the drain)",
                  !pc_at_delivery.empty() && pc_at_delivery[0] != rec.evs[0].pc,
                  "delivered at pc=" + hex(pc_at_delivery.empty() ? 0 : pc_at_delivery[0]));
        } else {
            check("EVT-NR-02", "the NR payload", false, "no deliveries");
        }
        check("EVT-NR-07", "and the register really took the value",
              emu.nextreg().peek(0x15) == 0x07);
    }
    {
        // The register-set filter's negative arm.
        Emulator emu;
        build_armed(emu, { 0xED, 0x91, 0x15, 0x07, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.filter.regs = { 0x16 };
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(2, s);
        emu.run_frame();
        check("EVT-NR-10", "a write to a register outside the set delivers nothing",
              rec.evs.empty());
    }

    // §4.2a — A DEBUGGER WRITE IS NOT AN EVENT. The gate is
    // `DebugState::guest_access()`, which is false outside the machine's own
    // execution and false inside a delivery's InspectionScope.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(2, s);

        check("EVT-MUT-01", "nextreg_write() from the backend succeeds",
              dbg.nextreg_write(2, 0x15, 0x0F) == Result::Ok &&
              emu.nextreg().peek(0x15) == 0x0F);
        emu.execute_single_instruction();      // reach a boundary drain
        check("EVT-MUT-02", "and fires NO NextRegWrite event on itself (§4.2a)",
              rec.evs.empty() && dbg.events_fired_since(0).empty());

        // The same for a panel's direct `nextreg().write()`, which is the call
        // site §4.2a names (nextreg_panel.cpp:174).
        emu.nextreg().write(0x15, 0x11);
        emu.execute_single_instruction();
        check("EVT-MUT-03", "a panel's own nextreg().write() fires nothing either",
              rec.evs.empty());

        // And the memory half: `poke(Cpu)` must not latch a watch.
        Rec mrec;
        Subscription m;
        m.kind = EventKind::Mem; m.access = Access::ReadWrite;
        m.filter.lo = 0x9000; m.filter.hi = 0x9000;
        m.action = Action::Stop; m.handler = recorder(mrec);
        dbg.subscribe(2, m);
        const uint8_t poked = 0x77;
        check("EVT-MUT-04", "poke() succeeds",
              dbg.poke(2, MemSpace::cpu(), 0x9000, 1, &poked).status == Result::Ok &&
              emu.mmu().peek(0x9000) == 0x77);
        uint8_t got = 0;
        dbg.peek(MemSpace::cpu(), 0x9000, 1, &got);
        check("EVT-MUT-05", "peek() succeeds",
              got == 0x77);
        emu.execute_single_instruction();
        check("EVT-MUT-06", "and NEITHER the poke nor the peek latched a Mem event",
              mrec.evs.empty() && !dbg.state().paused,
              "n=" + std::to_string(mrec.evs.size()));
        check("EVT-MUT-07", "nor did either raise the legacy data-breakpoint latch",
              !emu.debug_state().data_bp_hit());
    }

    // ── EVT-EXEC — the pre-instruction gate, the only kind delivered EARLY ─
    {
        Emulator emu;
        //   8000  00            NOP
        //   8001  3E 01         LD A,1
        //   8003  32 00 90      LD (0x9000),A
        //   8006  18 FE         JR $
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Continue; s.handler = recorder(rec);
        const auto sub = dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-01", "an Execute subscription fires once for the address",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (!rec.evs.empty()) {
            check("EVT-EXEC-02", "with that PC and the page behind it",
                  rec.evs[0].pc == 0x8001 &&
                  rec.evs[0].phys_page == emu.mmu().get_effective_page(4));
            check("EVT-EXEC-03", "and its id and owner",
                  rec.evs[0].id == sub.value && rec.evs[0].owner == 6);
        }
        check("EVT-EXEC-04", "a Continue verdict let the program finish",
              emu.mmu().peek(0x9000) == 0x01 && pc_of(emu) == 0x8006);
    }
    {
        // THE PRE-INSTRUCTION PROPERTY: a handler that sets PC redirects before
        // the instruction at that PC runs (§4.2a). The two instructions it skips
        // are what writes 0x9000, so the memory says whether the redirect landed.
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Continue;
        s.handler = [](const DbgEvent&, Debugger& d) {
            d.set_register(1, RegId::PC, 0x8006);
            return Action::Continue;
        };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-10", "an Execute handler's PC write redirects BEFORE the "
                             "instruction at that PC runs",
              emu.mmu().peek(0x9000) == 0x00 && pc_of(emu) == 0x8006,
              "mem=" + hex(emu.mmu().peek(0x9000)) + " pc=" + hex(pc_of(emu)));
    }
    {
        // A Stop, and its reason: `Breakpoint`, because the kind is Execute.
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8003; s.filter.hi = 0x8003;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(8, s);
        emu.run_frame();
        check("EVT-EXEC-20", "a Stop pauses BEFORE the instruction, not after it",
              dbg.state().paused && pc_of(emu) == 0x8003 &&
              emu.mmu().peek(0x9000) == 0x00,
              "pc=" + hex(pc_of(emu)));
        check("EVT-EXEC-21", "and the reason is Breakpoint, naming the subscription",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.by == 8);
        check("EVT-EXEC-22", "a resume steps off it (GH #221) and the program completes",
              dbg.run(8) == Result::Ok);
        emu.run_frame();
        check("EVT-EXEC-23", "so the machine does not re-pause on the same address",
              !dbg.state().paused && emu.mmu().peek(0x9000) == 0x01 &&
              pc_of(emu) == 0x8006, "pc=" + hex(pc_of(emu)));
    }
    {
        // `once`: disabled after the first ACCEPTED firing, not after the first
        // match. The loop reaches the address three times.
        Emulator emu;
        //   8000  06 03        LD B,3
        //   8002  10 FE        DJNZ $           (loops at 0x8002)
        //   8004  18 FA        JR 0x8000        (and starts over — see below)
        //
        // The final JR goes back to the TOP, not to itself: with `JR $` the
        // program parked at 0x8004 after the first frame and a second frame never
        // reached 0x8002 again, so EVT-EXEC-33's re-enable row could not tell a
        // spent `once` from a subscription that simply never matched. A mutation
        // that let a re-enabled `once` fire again survived because of it.
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFA });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.once = true; s.action = Action::Continue; s.handler = recorder(rec);
        const auto sub = dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-30", "a `once` subscription fires exactly once over three passes",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-EXEC-31", "and reports itself disabled afterwards",
              !dbg.subscriptions(true).empty() &&
              !dbg.subscriptions(true).front().enabled &&
              !dbg.subscriptions(true).front().live);
        check("EVT-EXEC-32", "re-enabling a spent `once` does NOT buy a second firing",
              dbg.set_enabled(6, sub.value, true) == Result::Ok);
        emu.run_frame();
        check("EVT-EXEC-33", "  (still one delivery, over a frame that passes the "
                             "address hundreds of times)",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-EXEC-34", "and the control: a NON-`once` subscription on the same "
                             "address fires on every pass",
              true);
        {
            Rec many;
            Subscription m = s;
            m.once = false; m.handler = recorder(many);
            dbg.subscribe(6, m);
            emu.run_frame();
            check("EVT-EXEC-35", "  (so the loop really does pass 0x8002 repeatedly — "
                                 "without this the row above proves nothing)",
                  many.evs.size() > 3, "n=" + std::to_string(many.evs.size()));
        }
    }
    {
        // A `Condition` gates the firing, and it runs only AFTER the filter
        // matched. `once` must not be spent by a rejected condition.
        Emulator emu;
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        int cond_calls = 0;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.once = true; s.action = Action::Continue; s.handler = recorder(rec);
        s.condition = [&cond_calls](const DbgEvent&, const Debugger& d) {
            ++cond_calls;
            // Fire only on the LAST pass, when B has come down to 1.
            return (d.registers().BC >> 8) == 1;
        };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-40", "the condition ran once per filter match",
              cond_calls == 3, "calls=" + std::to_string(cond_calls));
        check("EVT-EXEC-41", "the handler ran only for the pass it accepted",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-EXEC-42", "and `once` was spent by the ACCEPTED firing, not the first match",
              rec.evs.size() == 1 && (rec.evs[0].id != 0));
    }
    {
        // A handler's verdict OVERRIDES the static action, in both directions.
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Stop;
        s.handler = [](const DbgEvent&, Debugger&) { return Action::Continue; };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-50", "a handler returning Continue overrides a static Stop",
              !dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x3E, 0x01, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8001; s.filter.hi = 0x8001;
        s.action = Action::Continue;
        s.handler = [](const DbgEvent&, Debugger&) { return Action::Stop; };
        dbg.subscribe(6, s);
        emu.run_frame();
        check("EVT-EXEC-51", "and a handler returning Stop overrides a static Continue",
              dbg.state().paused && pc_of(emu) == 0x8001,
              "pc=" + hex(pc_of(emu)));
    }

    {
        // THE STEP PATH. Every row above drives the machine through
        // `run_frame()`; `debugger_step()` -> `step_frame_slot()` is a second
        // root with its own drain, and dropping that drain left the whole suite
        // green. A Step is the path a user exercises most deliberately.
        Emulator emu;
        //   8000  3E 5A        LD A,0x5A
        //   8002  32 00 90     LD (0x9000),A
        //   8005  18 FE        JR $
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        dbg.step_into(1);                       // over the LD A
        check("EVT-STEP-01", "a step that touches nothing delivers nothing",
              rec.evs.empty() && pc_of(emu) == 0x8002,
              "pc=" + hex(pc_of(emu)));
        dbg.step_into(1);                       // over the LD (0x9000),A
        check("EVT-STEP-02", "a step over the watched write DELIVERS it — the Step "
                             "path has its own boundary drain",
              rec.evs.size() == 1 && rec.evs[0].addr == 0x9000 &&
              rec.evs[0].value == 0x5A,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-STEP-03", "and the machine is paused where the step left it",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
    }

    // ── EVT-PROBE — probe_execute() over BOTH models ─────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        check("EVT-PROBE-01", "nothing armed -> false everywhere",
              !dbg.probe_execute(0x8000) && !dbg.probe_execute(0x0000));
        Subscription s;
        s.kind = EventKind::Execute; s.filter.lo = 0x1000; s.filter.hi = 0x1FFF;
        const auto sub = dbg.subscribe(1, s);
        check("EVT-PROBE-02", "an Execute RANGE answers true across the whole range",
              dbg.probe_execute(0x1000) && dbg.probe_execute(0x17FF) &&
              dbg.probe_execute(0x1FFF));
        check("EVT-PROBE-03", "and false outside it",
              !dbg.probe_execute(0x0FFF) && !dbg.probe_execute(0x2000));
        check("EVT-PROBE-04", "a disabled subscription answers false",
              dbg.set_enabled(1, sub.value, false) == Result::Ok &&
              !dbg.probe_execute(0x1000));
        dbg.set_enabled(1, sub.value, true);
        Subscription tr;
        tr.kind = EventKind::Execute; tr.filter.lo = 0x5000; tr.filter.hi = 0x5000;
        tr.transient = true;
        dbg.subscribe(1, tr);
        check("EVT-PROBE-05", "a TRANSIENT subscription counts — the step-off arm needs it",
              dbg.probe_execute(0x5000));
        emu.debug_state().breakpoints().add_pc(0x7000);
        check("EVT-PROBE-06", "and the legacy PC-breakpoint model still counts too",
              dbg.probe_execute(0x7000));
        Subscription pg;
        pg.kind = EventKind::Execute; pg.filter.lo = 0x9000; pg.filter.hi = 0x9000;
        pg.filter.page = 0xFE;      // a page no slot holds
        dbg.subscribe(1, pg);
        check("EVT-PROBE-07", "a page qualifier that no slot satisfies answers false",
              !dbg.probe_execute(0x9000));
    }

    // ── EVT-TIME — Frame, Scanline, Cycle ────────────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Frame; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        emu.run_frame();
        emu.run_frame();
        check("EVT-TIME-01", "a Frame subscription fires once per frame",
              rec.evs.size() == 3, "n=" + std::to_string(rec.evs.size()));
        check("EVT-TIME-02", "carrying the PRE-INCREMENT tag time().frame reports (F2)",
              rec.evs.size() == 3 && rec.evs[0].frame == 0 &&
              rec.evs[1].frame == 1 && rec.evs[2].frame == 2,
              rec.evs.size() == 3 ? std::to_string(rec.evs[0].frame) + "," +
                                        std::to_string(rec.evs[1].frame) + "," +
                                        std::to_string(rec.evs[2].frame)
                                  : "");
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Frame; s.filter.frame = 1;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame(); emu.run_frame(); emu.run_frame();
        check("EVT-TIME-03", "a Frame filter on one number fires only for that frame",
              rec.evs.size() == 1 && rec.evs[0].frame == 1,
              "n=" + std::to_string(rec.evs.size()));
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        emu.run_frame();
        check("EVT-TIME-10", "a Scanline subscription fires once per frame for its line",
              rec.evs.size() == 2, "n=" + std::to_string(rec.evs.size()));
        // (EVT-TIME-11 was `payload.cvc == 100` where the FILTER matched
        // `scanline = 100` — the payload field compared against the field the
        // filter selected on, so any consistent off-by-one passes it. It is gone;
        // EVT-TIME-16 compares two independently derived payload fields against
        // the VHDL relation instead. The revision report claimed this deletion
        // before it had been made, which is why the claim is now the code.)
        check("EVT-TIME-12", "with the raw frame counters alongside, not instead",
              !rec.evs.empty() && rec.evs[0].vc != rec.evs[0].cvc,
              rec.evs.empty() ? "" : "vc=" + std::to_string(rec.evs[0].vc));
        check("EVT-TIME-13", "in successive frames",
              rec.evs.size() == 2 && rec.evs[1].frame == rec.evs[0].frame + 1);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 30000;   // no such line
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-TIME-14", "a line that does not exist fires nothing",
              rec.evs.empty());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        const uint64_t target = emu.clock().get() + 5000;
        Subscription s;
        s.kind = EventKind::Cycle; s.filter.cycle = target;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-TIME-20", "a Cycle subscription stops the machine at or past its target",
              dbg.state().paused && emu.clock().get() >= target,
              "clock=" + std::to_string(emu.clock().get()));
        check("EVT-TIME-21", "and not far past it (it is checked every boundary)",
              emu.clock().get() < target + 2000);
        check("EVT-TIME-22", "the reason is Script — a subscriber's explicit stop",
              dbg.state().pause_reason.kind == PauseReason::Kind::Script &&
              dbg.state().pause_reason.id == sub.value);
    }

    // ── EVT-RESET — and the F8 `Any` filter, end to end ──────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Any;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.debug_latch_reset(/*hard=*/false);
        emu.execute_single_instruction();
        check("EVT-RESET-01", "ResetKind::Any delivers a SOFT reset",
              rec.evs.size() == 1 && rec.evs[0].reset_kind == ResetKind::Soft,
              "n=" + std::to_string(rec.evs.size()));
        emu.debug_latch_reset(/*hard=*/true);
        emu.execute_single_instruction();
        check("EVT-RESET-02", "and a HARD one, from the same one subscription",
              rec.evs.size() == 2 && rec.evs[1].reset_kind == ResetKind::Hard,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-RESET-03", "reset(Any) is still REFUSED — 'either way' is not a reset",
              dbg.reset(1, ResetKind::Any) == Result::Unsupported);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Hard;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.debug_latch_reset(false);
        emu.execute_single_instruction();
        check("EVT-RESET-10", "a Hard-only subscription ignores a soft reset",
              rec.evs.empty());
        emu.debug_latch_reset(true);
        emu.execute_single_instruction();
        check("EVT-RESET-11", "and takes the hard one",
              rec.evs.size() == 1 && rec.evs[0].reset_kind == ResetKind::Hard);
    }
    {
        // The real seam: Emulator::soft_reset() latches it, before init() resets
        // the clock (so the event is stamped where it happened).
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        emu.run_frame();
        const uint64_t before = emu.clock().get();
        Rec rec;
        Subscription s;
        s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Soft;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.soft_reset();
        emu.debug_state().set_clients_attached(true);      // init() does not clear it, but be explicit
        emu.debug_state().set_live_raster(true);
        emu.execute_single_instruction();
        check("EVT-RESET-20", "Emulator::soft_reset() really raises Reset{Soft}",
              rec.evs.size() == 1 && rec.evs[0].reset_kind == ResetKind::Soft,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-RESET-21", "stamped at the cycle the reset happened, not at cycle 0",
              rec.evs.size() == 1 && rec.evs[0].cycle >= before,
              rec.evs.empty() ? "" : std::to_string(rec.evs[0].cycle) + " vs " +
                                         std::to_string(before));
    }

    // ── EVT-INT / EVT-NMI — the two accept seams ─────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x76, 0x18, 0xFD });    // HALT, then JR $
        {
            Z80Registers r = emu.cpu().get_registers();
            r.IFF1 = 1; r.IFF2 = 1; r.IM = 1;
            emu.cpu().set_registers(r);
        }
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::IntAck; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-INT-01", "an accepted interrupt raises IntAck",
              !rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
        check("EVT-INT-02", "carrying the interrupt mode in force",
              !rec.evs.empty() && rec.evs[0].int_mode == 1,
              rec.evs.empty() ? "" : std::to_string(rec.evs[0].int_mode));
        check("EVT-INT-03", "and the vector byte the fabric drove",
              !rec.evs.empty() && rec.evs[0].int_vector == 0xFF,
              rec.evs.empty() ? "" : hex(rec.evs[0].int_vector));
    }
    {
        // The control: interrupts disabled, so nothing is ever accepted.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::IntAck; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-INT-04", "with IFF1 clear no interrupt is accepted and nothing fires",
              rec.evs.empty());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Nmi; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.nmi_source().set_divmmc_enable(true);     // NR 0x06 bit 4
        emu.nmi_source().strobe_divmmc_button();
        for (int i = 0; i < 40 && rec.evs.empty(); ++i)
            emu.execute_single_instruction();
        check("EVT-NMI-01", "a DivMMC-button NMI raises Nmi",
              !rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
        check("EVT-NMI-02", "reported as the DRIVE button (NmiSource::Src::DivMmc)",
              !rec.evs.empty() && rec.evs[0].nmi_source == NmiButton::Drive);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Nmi; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.nmi_source().set_mf_enable(true);         // NR 0x06 bit 3
        emu.nmi_source().strobe_mf_button();
        for (int i = 0; i < 40 && rec.evs.empty(); ++i)
            emu.execute_single_instruction();
        check("EVT-NMI-03", "a Multiface-button NMI raises Nmi",
              !rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
        check("EVT-NMI-04", "reported as the MF button",
              !rec.evs.empty() && rec.evs[0].nmi_source == NmiButton::Mf);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Nmi; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        for (int i = 0; i < 20; ++i) emu.execute_single_instruction();
        check("EVT-NMI-05", "with no button pressed nothing fires",
              rec.evs.empty());
    }

    // ── EVT-MAGIC — the opcode, the event, and CTL-13's unowned reason ────
    {
        Emulator emu;
        //   8000  ED FF        the ZEsarUX magic opcode
        //   8002  18 FE        JR $
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(true);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Magic; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-MAGIC-01", "the magic opcode pauses the machine",
              dbg.state().paused);
        check("EVT-MAGIC-02", "and the pause reason is Magic",
              dbg.state().pause_reason.kind == PauseReason::Kind::Magic);
        check("EVT-MAGIC-03", "UNOWNED — no client's detach may resume it",
              dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE);
        check("EVT-MAGIC-04", "naming the address it fired at",
              dbg.state().pause_reason.addr == 0x8000,
              hex(dbg.state().pause_reason.addr));
        // The EVENT, as distinct from the reason. Dropping the `Magic` latch left
        // every row above green, because the reason comes from
        // `note_magic_stop()` and not from the subscription.
        check("EVT-MAGIC-08", "and the Magic EVENT is delivered to its subscriber",
              rec.evs.size() == 1 && rec.evs[0].kind == EventKind::Magic,
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-MAGIC-09", "carrying the PC of the magic opcode",
              rec.evs.size() == 1 && rec.evs[0].pc == 0x8000,
              rec.evs.empty() ? "" : hex(rec.evs[0].pc));
    }
    {
        // The reason is reported with NO Magic subscription at all — the latch is
        // unconditional, because §4.2's closed set has a value for it either way.
        Emulator emu;
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(true);
        Debugger dbg(emu);
        emu.run_frame();
        check("EVT-MAGIC-05", "the Magic reason needs no subscription",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Magic);
        check("EVT-MAGIC-06", "and a resume clears it",
              dbg.run(1) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::None);
    }
    {
        // The control: the opcode is a NOP with the breakpoint disarmed, and no
        // Magic event fires.
        Emulator emu;
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(false);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Magic; s.action = Action::Continue;
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-MAGIC-07", "with the magic breakpoint disarmed nothing fires and "
                              "the machine runs on",
              rec.evs.empty() && !dbg.state().paused && pc_of(emu) == 0x8002,
              "pc=" + hex(pc_of(emu)));
    }

    // ── EVT-HOST — raise_host_event ──────────────────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Host; s.action = Action::Continue;
        std::strcpy(s.filter.host_name, "script3");
        s.handler = recorder(rec);
        dbg.subscribe(1, s);
        check("EVT-HOST-01", "raising the subscribed name returns Ok",
              dbg.raise_host_event(1, "script3") == Result::Ok);
        check("EVT-HOST-02", "and is delivered synchronously, with the name",
              rec.evs.size() == 1 &&
              std::string(rec.evs[0].host_name) == "script3",
              "n=" + std::to_string(rec.evs.size()));
        check("EVT-HOST-03", "raising a DIFFERENT name delivers nothing",
              dbg.raise_host_event(1, "script4") == Result::Ok &&
              rec.evs.size() == 1);
        check("EVT-HOST-04", "a name longer than MAX_HOST_EVENT_NAME is refused, "
                             "never truncated",
              dbg.raise_host_event(1, std::string(jnext::dbg::MAX_HOST_EVENT_NAME + 1,
                                                  'x')) == Result::Unsupported);
        check("EVT-HOST-05", "a name of exactly MAX_HOST_EVENT_NAME is accepted",
              dbg.raise_host_event(1, std::string(jnext::dbg::MAX_HOST_EVENT_NAME,
                                                  'x')) == Result::Ok);
        check("EVT-HOST-06", "and it is recorded in the delivery history",
              dbg.events_fired_since(0).size() == 1);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        check("EVT-HOST-10", "raising with NO Host subscription is Ok and inert",
              dbg.raise_host_event(1, "script1") == Result::Ok &&
              dbg.events_fired_since(0).empty());
        Subscription s;
        s.kind = EventKind::Host; s.action = Action::Stop;
        // NO HANDLER, deliberately: `recorder()` returns Continue, and a
        // handler's verdict OVERRIDES the static action — a recorder here would
        // quietly turn this Stop into a Continue. The delivery is observed
        // through the history instead.
        dbg.subscribe(1, s);
        check("EVT-HOST-11", "an empty filter name matches any raised name",
              dbg.raise_host_event(1, "anything") == Result::Ok &&
              dbg.events_fired_since(0).size() == 1);
        // NO pause() first, deliberately: `state()`'s precedence puts the ARMED
        // VERB ahead of the event latch, so a user pause would (correctly) be
        // reported instead and this row would be asserting the wrong thing.
        check("EVT-HOST-12", "and a Stop action pauses the machine, reason Script",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Script);
    }

    // ── EVT-COP — the three Copper sites, and the MOVE fan-out ───────────
    //
    // The Copper is driven the way the real machine drives it: NR 0x60-0x64 are
    // wired to it by Emulator::init(), so the program is uploaded through the
    // register file and the engine is ticked by the per-instruction device
    // cluster.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec cop, nr;
        Subscription cs;
        cs.kind = EventKind::Copper;
        cs.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        cs.action = Action::Continue; cs.handler = recorder(cop);
        dbg.subscribe(1, cs);
        Subscription ns;
        ns.kind = EventKind::NextRegWrite; ns.filter.regs = { 0x30 };
        ns.action = Action::Continue; ns.handler = recorder(nr);
        dbg.subscribe(1, ns);
        check("EVT-COP-01", "a Copper subscription arms the engine's own flag",
              emu.copper().events_armed());

        copper_program(emu, { move_word(0x30, 0x7F), HALT_WORD });
        copper_start(emu);
        // TWO slots, and that is the §4.3 contract rather than a fudge: the
        // Copper runs inside `tick_devices_after_instruction`, which is AFTER
        // this slot's boundary drain, so its latches are delivered at the NEXT
        // boundary — "≤1 instruction late", exactly as for a CPU NR write.
        emu.execute_single_instruction();
        emu.execute_single_instruction();

        check("EVT-COP-02", "the MOVE is delivered as Copper{Move}",
              cop.evs.size() == 1 &&
              cop.evs[0].copper_kind == jnext::dbg::CopperEventKind::Move,
              "n=" + std::to_string(cop.evs.size()));
        if (cop.evs.size() == 1) {
            check("EVT-COP-03", "with the register and the value the MOVE carried",
                  cop.evs[0].reg == 0x30 && cop.evs[0].value == 0x7F);
            check("EVT-COP-04", "the Copper PC of the MOVE, not of the next instruction",
                  cop.evs[0].copper_pc == 0);
            check("EVT-COP-05", "source Copper",
                  cop.evs[0].source == EventSource::Copper);
            check("EVT-COP-06", "and the 7 MHz hc_ula / cvc pair the Copper itself "
                                "compares against (GH #181), not the raw counters",
                  cop.evs[0].hc_ula >= 0 && cop.evs[0].cvc >= 0 &&
                  cop.evs[0].hc_ula != cop.evs[0].hc);
        }
        check("EVT-COP-07", "and the SAME latch entry also fed NextRegWrite{source=Copper}",
              nr.evs.size() == 1 && nr.evs[0].source == EventSource::Copper &&
              nr.evs[0].reg == 0x30 && nr.evs[0].value == 0x7F,
              "n=" + std::to_string(nr.evs.size()));
        check("EVT-COP-08", "exactly ONCE — a Copper MOVE is one ring entry, "
                            "fanned out at the drain, never two",
              nr.evs.size() == 1);
        check("EVT-COP-09", "and the register really took the value",
              emu.nextreg().peek(0x30) == 0x7F);
    }
    {
        // The HALT edge: is_halt() had no caller at all before B2, and a latch
        // per stalled cycle would fill the ring by itself.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Halt;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        copper_program(emu, { HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();     // the drain is one slot later
        check("EVT-COP-20", "the HALT form latches exactly once, on the EDGE",
              rec.evs.size() == 1 &&
              rec.evs[0].copper_kind == jnext::dbg::CopperEventKind::Halt,
              "n=" + std::to_string(rec.evs.size()));
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-COP-21", "and NOT again for every stalled cycle after it",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-COP-22", "at the HALT's own Copper PC",
              !rec.evs.empty() && rec.evs[0].copper_pc == 0);
        // Restarting re-arms the edge. Through Copper::reset(), and NOT through
        // mode 0 -> mode 1: `tick_copper_for_master_cycles` early-returns on
        // `!is_running()`, so a stopped Copper is never ticked, never sees the
        // mode edge and never leaves the HALT it is standing on — so the edge
        // staying set across a stop/start with no tick is CORRECT, not a defect.
        // reset() clears pc_, mode_, last_mode_ and the edge together, and leaves
        // the instruction RAM alone (dpram2 has no reset port,
        // zxnext.vhd:3959-3996), which is why the program survives it.
        emu.copper().reset();
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-COP-23", "a restarted program reaches the HALT and latches again",
              rec.evs.size() == 2, "n=" + std::to_string(rec.evs.size()));
    }
    {
        // A satisfied WAIT. Given a whole frame the Copper's cvc sweeps every
        // line, so a WAIT on any line in range is satisfied exactly once.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Wait;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        Rec mv;
        Subscription ms;
        ms.kind = EventKind::Copper;
        ms.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        ms.action = Action::Continue; ms.handler = recorder(mv);
        dbg.subscribe(1, ms);
        // The WAIT is at word ONE, not word zero: with it at zero, a payload that
        // hard-coded `copper_pc = 0` would be indistinguishable from the truth,
        // and a mutation that did exactly that survived the suite.
        copper_program(emu, { move_word(0x31, 0x01), wait_word(4, 40),
                              move_word(0x30, 0x21), HALT_WORD });
        copper_start(emu);
        emu.run_frame();
        check("EVT-COP-30", "a satisfied WAIT is delivered as Copper{Wait}",
              rec.evs.size() == 1 &&
              rec.evs[0].copper_kind == jnext::dbg::CopperEventKind::Wait,
              "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            check("EVT-COP-31", "at the WAIT's own Copper PC, before the advance",
                  rec.evs[0].copper_pc == 1,
                  std::to_string(rec.evs[0].copper_pc));
            check("EVT-COP-32", "carrying the WAIT's target line",
                  rec.evs[0].wait_vpos == 40);
            check("EVT-COP-33", "and its horizontal threshold as copper.vhd computes "
                                "it — (hpos << 3) + 12",
                  rec.evs[0].wait_hpos_threshold == (4 << 3) + 12,
                  std::to_string(rec.evs[0].wait_hpos_threshold));
            check("EVT-COP-34", "and the cvc it was satisfied at IS the target line",
                  rec.evs[0].cvc == 40, std::to_string(rec.evs[0].cvc));
            check("EVT-COP-35", "with hc_ula at or past the threshold",
                  rec.evs[0].hc_ula >= rec.evs[0].wait_hpos_threshold);
        }
        check("EVT-COP-36", "and the MOVE past it ran",
              emu.nextreg().peek(0x30) == 0x21);
        // The MOVE's OWN hc_ula / cvc pair. The WAIT rows above pin theirs, but
        // `latch_move_` takes the two as separate arguments and a mutation that
        // SWAPPED them survived — nothing asserted a Move's cvc against a value
        // the program itself fixes. The MOVE at word 2 runs on the cycle after
        // the WAIT at word 1 was satisfied, so it is on the same Copper line.
        check("EVT-COP-37", "both MOVEs were delivered, at their own Copper PCs",
              mv.evs.size() == 2 && mv.evs[0].copper_pc == 0 &&
              mv.evs[1].copper_pc == 2,
              "n=" + std::to_string(mv.evs.size()));
        check("EVT-COP-38", "and the MOVE released by the WAIT reports the WAIT's line "
                            "in cvc, with hc_ula at or past its threshold",
              mv.evs.size() == 2 && mv.evs[1].cvc == 40 &&
              mv.evs[1].hc_ula >= (4 << 3) + 12,
              mv.evs.size() == 2 ? "cvc=" + std::to_string(mv.evs[1].cvc) +
                                       " hc_ula=" + std::to_string(mv.evs[1].hc_ula)
                                 : "");
    }
    {
        // A stall is NOT a Halt: `is_halt()` is what tells an ordinary
        // unsatisfied WAIT from the HALT form, and a mutation that dropped it
        // survived — the Wait rows above subscribe to Wait, not Halt.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Halt;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        // A WAIT on line 40; the Copper's cvc is nowhere near it at the start of
        // a frame, so it stalls for the whole slot.
        copper_program(emu, { wait_word(0, 40) });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-COP-25", "an ordinary unsatisfied WAIT stalls without raising Halt",
              rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
    }
    {
        // The no-subscriber control: the engine flag is off, so not one of the
        // three sites latches, and the Copper still does its job.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        check("EVT-COP-40", "with no Copper/NR subscription the engine flag is clear",
              !emu.copper().events_armed());
        copper_program(emu, { move_word(0x30, 0x55), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        check("EVT-COP-41", "nothing is delivered",
              dbg.events_fired_since(0).empty());
        check("EVT-COP-42", "and the MOVE still happened",
              emu.nextreg().peek(0x30) == 0x55);
    }
    {
        // A NextRegWrite subscription ALONE must arm the Copper site, or a
        // Copper MOVE to a watched register would go unreported.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription ns;
        ns.kind = EventKind::NextRegWrite; ns.filter.source = EventSource::Copper;
        ns.action = Action::Continue; ns.handler = recorder(rec);
        dbg.subscribe(1, ns);
        check("EVT-COP-50", "an NR subscription alone arms the Copper engine flag",
              emu.copper().events_armed());
        copper_program(emu, { move_word(0x30, 0x66), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();     // the drain is one slot later
        check("EVT-COP-51", "and the MOVE is reported as NextRegWrite{source=Copper}",
              rec.evs.size() == 1 && rec.evs[0].source == EventSource::Copper,
              "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-DMA — the three DMA sites ────────────────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xA000 + i),
                            static_cast<uint8_t>(0xA0 + i));
        Rec st, en, by;
        Subscription s1; s1.kind = EventKind::Dma;
        s1.filter.dma_kind = jnext::dbg::DmaEventKind::Start;
        s1.action = Action::Continue; s1.handler = recorder(st);
        Subscription s2; s2.kind = EventKind::Dma;
        s2.filter.dma_kind = jnext::dbg::DmaEventKind::End;
        s2.action = Action::Continue; s2.handler = recorder(en);
        Subscription s3; s3.kind = EventKind::Dma;
        s3.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        s3.action = Action::Continue; s3.handler = recorder(by);
        dbg.subscribe(1, s1); dbg.subscribe(1, s2); dbg.subscribe(1, s3);
        check("EVT-DMA-01", "subscribing to all three arms BOTH engine flags",
              emu.dma().events_armed() && emu.dma().byte_events_armed());

        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B + port A start 0xA000 + len 8; R1/R2 port config;
        // R4 mode + port B start 0x9000; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();

        check("EVT-DMA-02", "the R6 0x87 enable raises exactly one Start",
              st.evs.size() == 1, "n=" + std::to_string(st.evs.size()));
        if (st.evs.size() == 1) {
            check("EVT-DMA-03", "with the block's source, destination and length",
                  st.evs[0].dma_src == 0xA000 && st.evs[0].dma_dst == 0x9000 &&
                  st.evs[0].dma_length == 8,
                  hex(st.evs[0].dma_src) + "->" + hex(st.evs[0].dma_dst) + " len " +
                      std::to_string(st.evs[0].dma_length));
            check("EVT-DMA-04", "and source=Dma",
                  st.evs[0].source == EventSource::Dma);
        }
        check("EVT-DMA-05", "one Byte per transferred byte",
              by.evs.size() == 8, "n=" + std::to_string(by.evs.size()));
        if (by.evs.size() == 8) {
            check("EVT-DMA-06", "each carrying the addresses THAT byte used, "
                                "before the increments",
                  by.evs[0].dma_src == 0xA000 && by.evs[0].dma_dst == 0x9000 &&
                  by.evs[7].dma_src == 0xA007 && by.evs[7].dma_dst == 0x9007);
            check("EVT-DMA-07", "and the byte itself",
                  by.evs[0].value == 0xA0 && by.evs[7].value == 0xA7);
            check("EVT-DMA-08", "a memory-to-memory transfer flags neither endpoint as I/O",
                  !by.evs[0].dma_is_io_src && !by.evs[0].dma_is_io_dst);
        }
        check("EVT-DMA-09", "and exactly one End at block completion",
              en.evs.size() == 1, "n=" + std::to_string(en.evs.size()));
        check("EVT-DMA-10", "reporting the bytes the BLOCK actually moved",
              en.evs.size() == 1 && en.evs[0].dma_bytes == 8,
              en.evs.empty() ? "" : std::to_string(en.evs[0].dma_bytes));
        check("EVT-DMA-11", "and the transfer really ran",
              emu.mmu().peek(0x9007) == 0xA7);
    }
    {
        // §4.3's per-engine cost rule: subscribing to Start/End must NOT arm the
        // per-byte site, and subscribing to Byte must not arm the other.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s; s.kind = EventKind::Dma;
        s.filter.dma_kind = jnext::dbg::DmaEventKind::Start;
        const auto sub = dbg.subscribe(1, s);
        check("EVT-DMA-20", "a Start subscription arms Start/End and NOT the per-byte site",
              emu.dma().events_armed() && !emu.dma().byte_events_armed());
        dbg.unsubscribe(1, sub.value);
        Subscription b; b.kind = EventKind::Dma;
        b.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        dbg.subscribe(1, b);
        check("EVT-DMA-21", "and a Byte subscription arms the per-byte site and NOT the other",
              !emu.dma().events_armed() && emu.dma().byte_events_armed());
    }
    {
        // The Byte range filter's either-endpoint rule, on a real transfer.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        for (int i = 0; i < 8; ++i)
            emu.mmu().write(static_cast<uint16_t>(0xA000 + i), 0x5A);
        Rec rec;
        Subscription b; b.kind = EventKind::Dma;
        b.filter.dma_kind = jnext::dbg::DmaEventKind::Byte;
        b.filter.lo = 0x9002; b.filter.hi = 0x9004;   // DESTINATION-side window
        b.action = Action::Continue; b.handler = recorder(rec);
        dbg.subscribe(1, b);
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B + port A start 0xA000 + len 8; R1/R2 port config;
        // R4 mode + port B start 0x9000; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x08); w(0x00);
        w(0x14); w(0x10); w(0xAD); w(0x00); w(0x90);
        w(0xCF); w(0x87);
        emu.execute_single_instruction();
        check("EVT-DMA-30", "a Byte range matches on the DESTINATION endpoint too",
              rec.evs.size() == 3, "n=" + std::to_string(rec.evs.size()));
        check("EVT-DMA-31", "and only for the bytes inside it",
              rec.evs.size() == 3 && rec.evs[0].dma_dst == 0x9002 &&
              rec.evs[2].dma_dst == 0x9004);
    }

    // ── EVT-OVF — the OVERFLOW CONTRACT, driven on purpose ───────────────
    //
    // §4.3 is explicit that this is "a specified, tested behaviour, not
    // 'unreachable'": the ring keeps the first N entries IN ORDER plus a dropped
    // count, the drain delivers those N, and every delivery of that boundary is
    // marked `overflowed{dropped}`. The ring is shrunk through the test hook and
    // a Copper MOVE BURST is driven over it — which is the path §4.3 names.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);

        jnext::dbg::EventTable* tbl = emu.debug_state().event_table();
        check("EVT-OVF-01", "the Debugger published its table to DebugState",
              tbl != nullptr);
        tbl->shrink_ring_for_test(4);
        check("EVT-OVF-02", "and the ring is now four entries deep",
              tbl->ring_capacity() == 4);

        // Sixteen consecutive MOVEs. One instruction slot at 3.5 MHz is 32
        // master cycles and a MOVE takes two of them, so the burst runs well
        // past four within the slot.
        std::vector<uint16_t> prog;
        for (int i = 0; i < 16; ++i)
            prog.push_back(move_word(0x30, static_cast<uint8_t>(0x10 + i)));
        prog.push_back(HALT_WORD);
        copper_program(emu, prog);
        copper_start(emu);
        emu.execute_single_instruction();     // the burst latches here
        emu.execute_single_instruction();     // and is drained here

        check("EVT-OVF-03", "exactly the ring's capacity is delivered, no more",
              rec.evs.size() == 4, "n=" + std::to_string(rec.evs.size()));
        check("EVT-OVF-04", "they are the FIRST four, in order, not the last four",
              rec.evs.size() == 4 && rec.evs[0].copper_pc == 0 &&
              rec.evs[1].copper_pc == 1 && rec.evs[2].copper_pc == 2 &&
              rec.evs[3].copper_pc == 3);
        check("EVT-OVF-05", "carrying the first four MOVEs' own values",
              rec.evs.size() == 4 && rec.evs[0].value == 0x10 &&
              rec.evs[3].value == 0x13);
        bool all_flagged = rec.evs.size() == 4;
        uint16_t dropped = rec.evs.empty() ? 0 : rec.evs[0].dropped;
        for (const auto& ev : rec.evs)
            all_flagged = all_flagged && ev.overflowed && ev.dropped == dropped;
        check("EVT-OVF-06", "EVERY delivery of that boundary is marked overflowed",
              all_flagged);
        check("EVT-OVF-07", "with the same non-zero dropped count on each",
              dropped > 0, "dropped=" + std::to_string(dropped));
        check("EVT-OVF-08", "and the MOVEs that were dropped still HAPPENED — the "
                            "ring bounds the reporting, never the emulation",
              emu.nextreg().peek(0x30) > 0x13,
              hex(emu.nextreg().peek(0x30)));

        // The next boundary is clean again: the flag is per boundary, not sticky.
        rec.evs.clear();
        tbl->shrink_ring_for_test(jnext::dbg::LATCH_RING_CAPACITY);
        emu.copper().reset();                  // see EVT-COP-23 for why reset()
        copper_program(emu, { move_word(0x31, 0x01), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-OVF-09", "the next boundary is not flagged — `overflowed` is per "
                            "boundary, not sticky",
              rec.evs.size() == 1 && !rec.evs[0].overflowed &&
              rec.evs[0].dropped == 0,
              "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-SUB — ownership, the switches, and the model ─────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s; s.kind = EventKind::Execute;
        const auto a = dbg.subscribe(1, s);
        const auto b = dbg.subscribe(2, s);
        check("EVT-SUB-01", "two clients get two distinct ids",
              a.status == Result::Ok && b.status == Result::Ok &&
              a.value != b.value);
        check("EVT-SUB-02", "a client may not unsubscribe another's id",
              dbg.unsubscribe(1, b.value) == Result::RefusedUnavailable);
        check("EVT-SUB-03", "and that id is still there",
              dbg.subscriptions(true).size() == 2);
        check("EVT-SUB-04", "an unknown id is refused the same way (no session leak)",
              dbg.unsubscribe(1, 99999) == Result::RefusedUnavailable);
        check("EVT-SUB-05", "a client may not enable/disable another's id",
              dbg.set_enabled(1, b.value, false) == Result::RefusedUnavailable);
        check("EVT-SUB-06", "its owner may",
              dbg.set_enabled(2, b.value, false) == Result::Ok);
        check("EVT-SUB-07", "and set_enabled on an unknown id is refused",
              dbg.set_enabled(1, 99999, false) == Result::RefusedUnavailable);
        check("EVT-SUB-08", "its owner may unsubscribe it",
              dbg.unsubscribe(2, b.value) == Result::Ok &&
              dbg.subscriptions(true).size() == 1);
        check("EVT-SUB-09", "unsubscribing it twice is refused, not a crash",
              dbg.unsubscribe(2, b.value) == Result::RefusedUnavailable);

        Subscription bad; bad.kind = EventKind::Mem; bad.access = Access::None;
        check("EVT-SUB-10", "a Mem subscription with Access::None is REFUSED, not "
                            "stored as a silent no-op",
              dbg.subscribe(1, bad).status == Result::RefusedUnavailable);
        bad.kind = EventKind::Port;
        check("EVT-SUB-11", "and so is a Port one",
              dbg.subscribe(1, bad).status == Result::RefusedUnavailable);
        check("EVT-SUB-12", "neither was added to the model",
              dbg.subscriptions(true).size() == 1);
        Subscription oob; oob.kind = static_cast<EventKind>(200);
        check("EVT-SUB-13", "a kind outside the enumeration is refused",
              dbg.subscribe(1, oob).status == Result::RefusedUnavailable);
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.condition = [](const DbgEvent&, const Debugger&) { return true; };
        s.handler = [](const DbgEvent&, Debugger&) { return Action::Continue; };
        s.once = true; s.action = Action::Log;
        const auto sub = dbg.subscribe(3, s);
        const auto list = dbg.subscriptions(false);
        check("EVT-LIST-01", "subscriptions() reports the model as set",
              list.size() == 1 && list[0].id == sub.value && list[0].owner == 3 &&
              list[0].kind == EventKind::Mem && list[0].access == Access::Write &&
              list[0].action == Action::Log && list[0].once);
        check("EVT-LIST-02", "and the filter with it",
              list.size() == 1 && list[0].filter.lo == 0x9000 &&
              list[0].filter.hi == 0x9000);
        check("EVT-LIST-03", "reporting only WHETHER a condition and a handler are "
                             "present, never the closures",
              list.size() == 1 && list[0].has_condition && list[0].has_handler);
        check("EVT-LIST-04", "enabled and live both true while nothing is suspended",
              list.size() == 1 && list[0].enabled && list[0].live);

        dbg.set_master_enabled(false);
        const auto sus = dbg.subscriptions(false);
        check("EVT-LIST-05", "the master switch clears `live` and leaves `enabled` alone",
              sus.size() == 1 && sus[0].enabled && !sus[0].live);
        check("EVT-LIST-06", "and it suspends BOTH models — the legacy set too",
              !emu.debug_state().breakpoints().master_enabled() &&
              !dbg.master_enabled());
        dbg.set_master_enabled(true);
        check("EVT-LIST-07", "and master on restores both",
              dbg.master_enabled() &&
              emu.debug_state().breakpoints().master_enabled() &&
              dbg.subscriptions(false)[0].live);

        dbg.set_client_enabled(3, false);
        check("EVT-LIST-08", "the per-client switch clears `live` and leaves `enabled`",
              !dbg.client_enabled(3) && dbg.subscriptions(false)[0].enabled &&
              !dbg.subscriptions(false)[0].live);
        dbg.set_client_enabled(3, true);
        check("EVT-LIST-09", "and it is restored",
              dbg.client_enabled(3) && dbg.subscriptions(false)[0].live);

        Subscription tr; tr.kind = EventKind::Execute; tr.transient = true;
        dbg.subscribe(3, tr);
        check("EVT-LIST-10", "subscriptions(false) HIDES a transient, as a user list must",
              dbg.subscriptions(false).size() == 1);
        check("EVT-LIST-11", "and subscriptions(true) shows it",
              dbg.subscriptions(true).size() == 2);
    }

    // ── EVT-DEL — delivery semantics the drain owns ──────────────────────
    {
        // A handler that mutates memory it is itself watching must not latch on
        // itself, and the mutation must be visible to the NEXT instruction.
        Emulator emu;
        //   8000  3E 01        LD A,1
        //   8002  32 00 90     LD (0x9000),A
        //   8005  3A 00 90     LD A,(0x9000)
        //   8008  32 01 90     LD (0x9001),A
        //   800B  18 FE        JR $
        build_armed(emu, { 0x3E, 0x01, 0x32, 0x00, 0x90, 0x3A, 0x00, 0x90,
                           0x32, 0x01, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int calls = 0;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue;
        s.handler = [&calls](const DbgEvent& ev, Debugger& d) {
            ++calls;
            const uint8_t undone = static_cast<uint8_t>(ev.prev);
            d.poke(1, MemSpace::cpu(), 0x9000, 1, &undone);   // undo the write
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-01", "a handler ran once for the write it watched",
              calls == 1, "calls=" + std::to_string(calls));
        check("EVT-DEL-02", "its own poke did NOT re-enter the handler (§4.2a)",
              calls == 1);
        check("EVT-DEL-03", "the undo is what the NEXT instruction read",
              emu.mmu().peek(0x9001) == 0x00,
              hex(emu.mmu().peek(0x9001)));
        check("EVT-DEL-04", "and the machine never stopped",
              !dbg.state().paused);
    }
    {
        // §4.2a — THE DELIVERY'S OWN `InspectionScope`, tested through a verb
        // that carries none of its own. `poke()` and `peek()` each take one
        // (EVT-MUT-04..07), so EVT-DEL-01..04 above stay green even with the
        // drain's scope removed — the mutation survived. `port_out()` dispatches
        // the port exactly as a guest `OUT` does and takes no scope, and a LEGACY
        // I/O watchpoint on that port is latched by `PortDispatch` on
        // `watchpoints_live()` alone — which is true inside a delivery unless the
        // drain has dropped it.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        int calls = 0;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue;
        s.handler = [&calls](const DbgEvent&, Debugger& d) {
            ++calls;
            d.port_out(1, 0x07FE, 0x00);
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-05", "the handler's own port_out ran",
              calls == 1, "calls=" + std::to_string(calls));
        check("EVT-DEL-06", "and did NOT raise the legacy I/O watchpoint on itself — "
                            "the delivery's one InspectionScope is what makes that true",
              !emu.debug_state().data_bp_hit() && !dbg.state().paused &&
              pc_of(emu) == 0x8007,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // A handler may unsubscribe itself mid-drain, and the drain must survive
        // it (contract 4: a removal is a tombstone, compaction is deferred).
        Emulator emu;
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        int calls = 0;
        EventId my_id = jnext::dbg::EVENT_NONE;
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.action = Action::Continue;
        s.handler = [&calls, &my_id](const DbgEvent& ev, Debugger& d) {
            ++calls;
            my_id = ev.id;
            d.unsubscribe(1, ev.id);
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-10", "a handler that unsubscribes ITSELF is called exactly once",
              calls == 1, "calls=" + std::to_string(calls));
        check("EVT-DEL-11", "and the subscription is really gone afterwards",
              dbg.subscriptions(true).empty());
        check("EVT-DEL-12", "and the machine ran on without crashing",
              pc_of(emu) == 0x8004, "pc=" + hex(pc_of(emu)));
    }
    {
        // A handler may SUBSCRIBE from inside a delivery (a script arming a
        // follow-up rule). The new one must not be visited for the event that
        // was already latched, and must fire afterwards.
        Emulator emu;
        build_armed(emu, { 0x06, 0x04, 0x10, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        int first = 0, second = 0;
        Subscription follow;
        follow.kind = EventKind::Execute;
        follow.filter.lo = 0x8002; follow.filter.hi = 0x8002;
        follow.action = Action::Continue;
        follow.handler = [&second](const DbgEvent&, Debugger&) {
            ++second; return Action::Continue;
        };
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.once = true; s.action = Action::Continue;
        s.handler = [&first, follow](const DbgEvent&, Debugger& d) {
            ++first;
            d.subscribe(1, follow);
            return Action::Continue;
        };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-20", "the arming handler fired once",
              first == 1, "first=" + std::to_string(first));
        check("EVT-DEL-21", "the handler it armed fired for the LATER passes only, "
                            "not for the delivery it was created in",
              second == 3, "second=" + std::to_string(second));
    }
    {
        // Two subscriptions on the same event: both are delivered, and
        // `matched[]` is what the Paused push will carry.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Rec r1, r2;
        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Continue; a.handler = recorder(r1);
        Subscription b = a;
        b.filter.lo = 0x8000; b.filter.hi = 0xFFFF;     // a wider net
        b.handler = recorder(r2);
        const auto ia = dbg.subscribe(1, a);
        const auto ib = dbg.subscribe(2, b);
        emu.run_frame();
        check("EVT-DEL-30", "one site event reaches EVERY matching subscription",
              r1.evs.size() == 1 && r2.evs.size() == 1);
        check("EVT-DEL-31", "each delivery naming its OWN subscription and owner",
              r1.evs.size() == 1 && r2.evs.size() == 1 &&
              r1.evs[0].id == ia.value && r1.evs[0].owner == 1 &&
              r2.evs[0].id == ib.value && r2.evs[0].owner == 2);
        check("EVT-DEL-32", "with distinct sequence numbers",
              r1.evs.size() == 1 && r2.evs.size() == 1 &&
              r1.evs[0].seq != r2.evs[0].seq);
        check("EVT-DEL-33", "and both in the delivery history",
              dbg.events_fired_since(0).size() == 2);
    }
    {
        // Action::Log keeps the machine running; the SES-06 line goes to the log
        // sink, and what is asserted here is the verdict, not the format.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Log;
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-DEL-40", "an Action::Log delivery does not stop the machine",
              !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-DEL-41", "and is still recorded in the delivery history",
              dbg.events_fired_since(0).size() == 1);
    }
    {
        // A disabled subscription is not consulted at all — including its
        // condition, which a script may have made expensive.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int cond_calls = 0;
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Continue; s.handler = recorder(rec);
        s.condition = [&cond_calls](const DbgEvent&, const Debugger&) {
            ++cond_calls; return true;
        };
        const auto sub = dbg.subscribe(1, s);
        dbg.set_enabled(1, sub.value, false);
        emu.run_frame();
        check("EVT-DEL-50", "a disabled subscription delivers nothing",
              rec.evs.empty());
        check("EVT-DEL-51", "and its condition is never even evaluated",
              cond_calls == 0, "calls=" + std::to_string(cond_calls));
        check("EVT-DEL-52", "and it no longer arms the slot mask either",
              !emu.debug_state().wr_watch_armed(0x9000));
    }

    // ── EVT-TRANS — the transient successor of the single one-shot ────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.step_into(1); dbg.step_into(1);     // park the PC on the CALL
        check("EVT-TRANS-01", "step_over() arms a transient Execute at the next PC "
                              "and NOT the legacy one-shot",
              dbg.step_over(1) == Result::Ok &&
              !emu.debug_state().breakpoints().has_oneshot());
        const auto list = dbg.subscriptions(true);
        check("EVT-TRANS-02", "  (one transient, at the return address)",
              list.size() == 1 && list[0].transient &&
              list[0].kind == EventKind::Execute &&
              list[0].filter.lo == AFTER_CALL);
        check("EVT-TRANS-03", "the user list does not show it",
              dbg.subscriptions(false).empty());
        check("EVT-TRANS-04", "and the step MODE still reports Over (CTL-13 unchanged)",
              dbg.state().step_mode == jnext::dbg::StepMode::Over);
        run_until_paused(emu);
        check("EVT-TRANS-05", "the step lands past the CALL",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "pc=" + hex(pc_of(emu)));
        check("EVT-TRANS-06", "and the transient was auto-removed at the stop",
              dbg.subscriptions(true).empty());
        check("EVT-TRANS-07", "with the reason the verb armed, not the subscription's",
              dbg.state().pause_reason.kind == PauseReason::Kind::Step &&
              dbg.state().pause_reason.by == 1);
    }
    {
        // §4.3 — transient subscriptions are UNLIMITED, which the single one-shot
        // was not. DeZog needs two temporary breakpoints per CMD_CONTINUE.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription t1, t2;
        t1.kind = EventKind::Execute; t1.transient = true;
        t1.filter.lo = 0x8005; t1.filter.hi = 0x8005;
        t2 = t1; t2.filter.lo = 0x8006; t2.filter.hi = 0x8006;
        dbg.subscribe(1, t1);
        dbg.subscribe(1, t2);
        check("EVT-TRANS-10", "two transient targets coexist",
              dbg.subscriptions(true).size() == 2);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-11", "the machine stops at the FIRST of them",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-TRANS-12", "and BOTH are dropped at that stop, not just the one "
                              "that fired",
              dbg.subscriptions(true).empty());
    }
    {
        // A transient is exempt from the master switch, which is what makes Step
        // Over work on a machine the user has suspended everything on.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.step_into(1); dbg.step_into(1);     // park the PC on the CALL
        dbg.set_master_enabled(false);
        dbg.step_over(1);
        run_until_paused(emu);
        check("EVT-TRANS-20", "Step Over still works with the master switch OFF",
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // And the other half of GH #223: a run() from a PAUSED machine abandons
        // the pending target, exactly as resume()'s clear_oneshot() always did.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        dbg.run_to(1, AFTER_CALL);
        check("EVT-TRANS-30", "run_to() arms exactly one transient target",
              dbg.subscriptions(true).size() == 1);
        dbg.pause(1);
        check("EVT-TRANS-31", "an explicit pause IS a stop, so the target is dropped "
                              "(§4.3) — which is what resume()'s clear_oneshot() "
                              "was standing in for",
              dbg.subscriptions(true).empty());
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-32", "so the machine runs past it to the landing marker",
              !dbg.state().paused && pc_of(emu) == PARK,
              "pc=" + hex(pc_of(emu)));
        // And the complementary rule: `run()` does NOT drop a target, so DeZog's
        // two temp breakpoints per CMD_CONTINUE survive the continue that armed
        // them. CTL-02-04 and EVT-TRANS-10..12 are the rows for that half.
    }

    // ── REASON — CTL-13's precedence, in order ───────────────────────────
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        // A LEGACY watchpoint: the evidence the hot loop destroys.
        emu.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::WRITE);
        emu.run_frame();
        check("REASON-01", "a legacy watchpoint stops the machine",
              dbg.state().paused, "pc=" + hex(pc_of(emu)));
        check("REASON-02", "and the reason is Watch, with the address and direction, "
                           "though data_bp_hit_ is already consumed",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
              dbg.state().pause_reason.addr == 0x9000 &&
              dbg.state().pause_reason.access == Access::Write &&
              !emu.debug_state().data_bp_hit());
        check("REASON-03", "and it is unowned — no client set that watchpoint "
                           "through the backend",
              dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE);
        check("REASON-04", "a resume clears the evidence",
              dbg.run(1) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::None);
    }
    {
        // REASON-04 above cannot see stale evidence: `state()` returns `None` for
        // a RUNNING machine whatever the latches hold. What catches it is the
        // NEXT stop, for a DIFFERENT reason — a mutation that kept the watch
        // evidence across a resume left every other row green.
        Emulator emu;
        //   8000  3E 5A        LD A,0x5A
        //   8002  32 00 90     LD (0x9000),A    <- the WATCH stops after this
        //   8005  00           NOP
        //   8006  00           NOP              <- a PC BREAKPOINT here
        //   8007  18 FE        JR $
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::WRITE);
        emu.debug_state().breakpoints().add_pc(0x8006);
        emu.run_frame();
        check("REASON-05", "the watch stops first, and is reported as Watch",
              dbg.state().paused && pc_of(emu) == 0x8005 &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch,
              "pc=" + hex(pc_of(emu)));
        dbg.run(1);
        run_until_paused(emu);
        check("REASON-06", "the breakpoint stops next",
              dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
        check("REASON-07", "and THAT stop is reported as Breakpoint, not as the "
                           "watch it left behind",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.addr == 0x8006);
    }
    {
        // An I/O watchpoint takes the same route, with the PORT as the address.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x07, 0xD3, 0xFE, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        emu.run_frame();
        check("REASON-10", "a legacy I/O watchpoint stops the machine",
              dbg.state().paused);
        check("REASON-11", "with reason Watch and the port as the address",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
              dbg.state().pause_reason.addr == 0x07FE &&
              dbg.state().pause_reason.access == Access::Write,
              hex(dbg.state().pause_reason.addr));
    }
    {
        // A legacy PC breakpoint is still reported as Breakpoint, and an armed
        // verb still outranks both.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        emu.debug_state().breakpoints().add_pc(AFTER_CALL);
        emu.run_frame();
        check("REASON-20", "a legacy PC breakpoint stops the machine",
              dbg.state().paused && pc_of(emu) == AFTER_CALL);
        check("REASON-21", "and is reported as Breakpoint at that address",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.addr == AFTER_CALL);
        check("REASON-22", "a user pause OUTRANKS a subscription stop at the same PC",
              dbg.pause(5) == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::User &&
              dbg.state().pause_reason.by == 5);
    }
    {
        // Corrupt is FIRST, ahead even of the armed verb: CTL-11 makes it the
        // thing that refuses every resume, so it is what must be reported.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        attach_and_pause(emu);
        const uint8_t junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const Result lr = dbg.load_state_bytes(1, junk, sizeof(junk));
        check("REASON-30", "a torn load_state_bytes() is refused",
              lr != Result::Ok);
        const bool blocked = dbg.resume_blocked_by_corruption().has_value();
        check("REASON-31", "and latches an unacknowledged corruption", blocked);
        if (blocked) {
            check("REASON-32", "which state() reports as Corrupt, ahead of the "
                               "armed verb",
                  dbg.state().pause_reason.kind == PauseReason::Kind::Corrupt);
            check("REASON-33", "UNOWNED — the corruption is the machine's",
                  dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE);
            const auto inc = dbg.resume_blocked_by_corruption();
            check("REASON-34", "acknowledging it restores the ordinary reason",
                  dbg.acknowledge_corruption(inc->generation) == Result::Ok &&
                  dbg.state().pause_reason.kind != PauseReason::Kind::Corrupt);
        } else {
            check("REASON-32", "Corrupt reporting", false, "no incident latched");
        }
    }
    {
        // An event stop OUTRANKS the legacy fallbacks: it is the only source that
        // knows the subscription id.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        emu.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::WRITE);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(6, s);
        emu.run_frame();
        check("REASON-40", "with BOTH models armed on the same address the machine stops",
              dbg.state().paused);
        check("REASON-41", "and the SUBSCRIPTION explains it, because only it has an id",
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
              dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.by == 6);
    }

    {
        // ONE reason per stop: the FIRST `Stop` of the boundary wins, because that
        // is the one the machine stopped on. Two subscriptions on the same write,
        // both asking to stop — a mutation that let the LATER one overwrite the
        // reason survived, because no row had two.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Stop;
        const auto first  = dbg.subscribe(4, a);
        const auto second = dbg.subscribe(5, a);
        emu.run_frame();
        check("REASON-50", "two Stop subscriptions on one write stop the machine once",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("REASON-51", "and the FIRST of them is the reason, not the last",
              dbg.state().pause_reason.id == first.value &&
              dbg.state().pause_reason.by == 4,
              "id=" + std::to_string(dbg.state().pause_reason.id) + " first=" +
                  std::to_string(first.value) + " second=" +
                  std::to_string(second.value));
    }
    {
        // The event-stop latch must not explain the NEXT stop. `Impl::arm()` is
        // what clears it, and a mutation that stopped clearing it survived: the
        // only row that looked was EVT-MEM-53, and `state()` reports `None` for a
        // RUNNING machine whatever the latch holds.
        Emulator emu;
        //   8000  3E 5A / 8002  32 00 90  <- a Mem SUBSCRIPTION stops here
        //   8005  00   / 8006  00         <- a legacy PC BREAKPOINT at 0x8006
        //   8007  18 FE
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(4, s);
        emu.debug_state().breakpoints().add_pc(0x8006);
        emu.run_frame();
        check("REASON-60", "the subscription stops first and names itself",
              dbg.state().paused && dbg.state().pause_reason.id == sub.value &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch);
        dbg.run(4);
        run_until_paused(emu);
        check("REASON-61", "the legacy breakpoint stops next",
              dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
        check("REASON-62", "and THAT stop is Breakpoint, with no id — the earlier "
                           "subscription's latch was cleared by the resume",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.id == jnext::dbg::EVENT_NONE &&
              dbg.state().pause_reason.addr == 0x8006);
    }
    {
        // A `Magic` SUBSCRIPTION that stops: the reason comes from the drain's
        // latch rather than from `note_magic_stop()`, and it must still be
        // UNOWNED. Every earlier Magic row used Action::Continue, so the drain's
        // own Magic arm had no row and a mutation that gave it an owner survived.
        Emulator emu;
        build_armed(emu, { 0xED, 0xFF, 0x18, 0xFE });
        emu.set_magic_breakpoint(true);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Magic; s.action = Action::Stop;
        const auto sub = dbg.subscribe(3, s);
        emu.run_frame();
        check("REASON-70", "a Magic subscription's Stop is reported as Magic",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Magic);
        check("REASON-71", "naming the subscription",
              dbg.state().pause_reason.id == sub.value);
        check("REASON-72", "but UNOWNED, even though the subscription HAS an owner",
              dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE,
              "by=" + std::to_string(dbg.state().pause_reason.by));
    }
    {
        // §4.2a — the backend consults the table only when `!replay_mode_`: a
        // rewind fast-forward runs the machine with the gate live, and an
        // `Execute` subscription inside the replayed span would pause the replay
        // short. A mutation that dropped the gate survived; nothing set the flag.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        // NO HANDLER: `recorder()` returns Continue, which would OVERRIDE this
        // Stop and make the "nothing stopped" claim vacuous. The delivery is
        // observed through the INS-17 history instead.
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        emu.set_replay_mode(true);
        emu.run_frame();
        check("EVT-REPLAY-01", "during a replay nothing is delivered and nothing stops",
              dbg.events_fired_since(0).empty() && !dbg.state().paused,
              "n=" + std::to_string(dbg.events_fired_since(0).size()));
        check("EVT-REPLAY-02", "and the write really happened — the replay is faithful, "
                               "it is the REPORTING that is suppressed",
              emu.mmu().peek(0x9000) == 0x5A);
        check("EVT-REPLAY-03", "the ring was emptied, so the replay does not hand its "
                               "latches to the next real boundary",
              !emu.debug_state().events_pending());
        emu.set_replay_mode(false);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = PROG; emu.cpu().set_registers(r);
        emu.mmu().write(0x9000, 0x00);
        emu.run_frame();
        check("EVT-REPLAY-04", "and with replay off the same program DOES deliver AND "
                               "stop — the control without which the rows above prove "
                               "nothing",
              dbg.events_fired_since(0).size() == 1 && dbg.state().paused &&
              pc_of(emu) == 0x8005,
              "n=" + std::to_string(dbg.events_fired_since(0).size()) +
                  " pc=" + hex(pc_of(emu)));
    }

    {
        // THE QT PATH. `Impl::arm()` clears the event-stop latch, and every backend
        // control verb calls it — but the Qt panels still drive `DebugState`
        // DIRECTLY until package Q, so a Qt-driven Run never reaches a backend verb
        // and the latch would explain the NEXT, unrelated stop. The resume
        // GENERATION is what closes that, and nothing exercised it.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(4, s);
        emu.debug_state().breakpoints().add_pc(0x8006);
        emu.run_frame();
        check("REASON-80", "the subscription stops first and names itself",
              dbg.state().paused && dbg.state().pause_reason.id == sub.value);

        // A RAW resume, exactly as DebuggerManager::on_run() does it today —
        // NOT dbg.run(), so `Impl::arm()` never runs.
        emu.debug_state().resume();
        run_until_paused(emu);
        check("REASON-81", "the legacy breakpoint stops next",
              dbg.state().paused && pc_of(emu) == 0x8006,
              "pc=" + hex(pc_of(emu)));
        check("REASON-82", "and THAT stop is Breakpoint — the event-stop latch is "
                           "discarded by the resume GENERATION, not only by a backend "
                           "verb",
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.id == jnext::dbg::EVENT_NONE,
              "kind=" + std::to_string(
                  static_cast<int>(dbg.state().pause_reason.kind)));
    }
    {
        // B-9: the halt-run loop's `!event_stop_pending_` term. A Stop that arrives
        // WHILE debugger_step() is running a HALT out must end the step there; the
        // budget is two frames, so without the term the step spends it all.
        Emulator emu;
        build_armed(emu, { 0x76, 0x18, 0xFD });        // HALT, then JR $
        {
            Z80Registers r = emu.cpu().get_registers();
            r.IFF1 = 0; r.IFF2 = 0;                    // nothing wakes the HALT
            emu.cpu().set_registers(r);
        }
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 4;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        // TWO steps: the first EXECUTES the HALT, so `is_halted()` is true when
        // the second one starts and the halt-run loop actually engages. Stepping
        // once reaches the loop's guard with `halted_before == false` and skips
        // it entirely — which is why the first cut of this row measured 4
        // T-states and could not have seen anything.
        emu.debugger_step();
        dbg.pause(1);
        const int tstates = emu.debugger_step();
        const int budget  = static_cast<int>(
            2u * dbg.machine().tstates_per_frame);
        check("EVT-STEP-20", "a Stop during the halt-run ENDS the step early rather "
                             "than spending the whole two-frame budget",
              tstates > 0 && tstates < budget / 2,
              "tstates=" + std::to_string(tstates) + " budget=" +
                  std::to_string(budget));
        check("EVT-STEP-21", "and the machine is paused where it ended",
              dbg.state().paused);
    }

    // ── EVT-GATE — the no-subscriber cost claim, as a row ─────────────────
    //
    // §6's constraint is that an unsubscribed machine pays what it paid before.
    // The bench measures the TIME; these rows assert the MECHANISM the bench
    // result rests on, which is what a reader can check.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        check("EVT-GATE-01", "with no Debugger at all the table pointer is null",
              emu.debug_state().event_table() == nullptr);
        check("EVT-GATE-02", "and every memory / port gate is closed",
              !emu.debug_state().rd_watch_armed(0x0000) &&
              !emu.debug_state().wr_watch_armed(0xFFFF) &&
              !emu.debug_state().port_watch_armed() &&
              !emu.debug_state().events_pending() &&
              !emu.debug_state().execute_events_armed());
        {
            Debugger dbg(emu);
            check("EVT-GATE-03", "constructing a Debugger publishes the table",
                  emu.debug_state().event_table() != nullptr);
            check("EVT-GATE-04", "but arms NOTHING by itself",
                  !emu.debug_state().rd_watch_armed(0x0000) &&
                  !emu.debug_state().wr_watch_armed(0xFFFF) &&
                  !emu.debug_state().port_watch_armed() &&
                  !emu.debug_state().events_pending() &&
                  !emu.debug_state().execute_events_armed() &&
                  !emu.copper().events_armed() && !emu.dma().events_armed() &&
                  !emu.dma().byte_events_armed());
            Subscription s;
            s.kind = EventKind::Mem; s.access = Access::ReadWrite;
            dbg.subscribe(1, s);
            check("EVT-GATE-05", "and a subscription opens exactly the gate it needs",
                  emu.debug_state().rd_watch_armed(0x0000) &&
                  !emu.debug_state().port_watch_armed() &&
                  !emu.copper().events_armed());
        }
        // The destructor must retire every published pointer: DebugState outlives
        // the Debugger, and a stale slot mask over a freed table is a
        // use-after-free on the hot path.
        check("EVT-GATE-06", "destroying the Debugger retires the table pointer",
              emu.debug_state().event_table() == nullptr);
        check("EVT-GATE-07", "and closes every gate it had opened",
              !emu.debug_state().rd_watch_armed(0x0000) &&
              !emu.debug_state().wr_watch_armed(0x0000) &&
              !emu.debug_state().port_watch_armed() &&
              !emu.debug_state().execute_events_armed() &&
              !emu.copper().events_armed() && !emu.dma().events_armed());
        emu.run_frame();
        check("EVT-GATE-08", "so the machine runs on safely after it is gone",
              pc_of(emu) == 0x8000 || pc_of(emu) == 0x8001,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // The `armed()` gate: with no client attached and no
        // --persistent-breakpoints, the whole pipeline is inert even with a
        // subscription in place. That is §4.1's formula, and §6's premise.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        emu.debug_state().set_clients_attached(false);
        emu.debug_state().set_live_raster(false);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        check("EVT-GATE-10", "armed() is false with nothing attached",
              !dbg.armed());
        emu.run_frame();
        check("EVT-GATE-11", "so nothing is delivered and nothing stops",
              rec.evs.empty() && !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-GATE-12", "--persistent-breakpoints alone re-arms it (GH #219)",
              dbg.set_persistent_breakpoints(true) == Result::Ok && dbg.armed());
    }
    {
        // on_slot_remapped: the MMU's dispatch rebuild really does re-publish,
        // so a physical-page filter follows the page around.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { 0x02 };          // RAM bank 1's low 8 KB page
        dbg.subscribe(1, s);
        const uint8_t page_at_6 = emu.mmu().get_effective_page(6);
        emu.mmu().set_page(6, 0x02);
        check("EVT-SLOT-01", "mapping the watched page into a slot arms that slot",
              emu.debug_state().wr_watch_armed(0xC000),
              "was page " + std::to_string(page_at_6));
        check("EVT-SLOT-02", "and no other",
              !emu.debug_state().wr_watch_armed(0x4000));
        emu.mmu().set_page(6, 0x10);
        check("EVT-SLOT-03", "mapping it away closes the gate again",
              !emu.debug_state().wr_watch_armed(0xC000));
        emu.mmu().set_page(2, 0x02);
        check("EVT-SLOT-04", "and it follows the page to another slot",
              emu.debug_state().wr_watch_armed(0x4000) &&
              !emu.debug_state().wr_watch_armed(0xC000));
    }


    // =======================================================================
    // GH #276 B2 REVISION — the rows the review proved were missing.
    //
    // Nine blocking items, and not one of them moved a single row of the 744:
    // the page-qualified filter was never maintained, the latch sites and the
    // drain gates disagreed about `armed()`, a DMA NextREG write was tagged
    // `Cpu`, `Scanline` carried the wrong cycle, the Copper latched sub-kinds
    // nobody asked for, a nested `raise_host_event` wiped the boundary's hit
    // list, `Cycle` never retired, and one fixture asserted `0 == 0`. Each row
    // below FAILS without its fix — verified one at a time, not assumed.
    // =======================================================================

    // ── EVT-SLOT-10..16 — §6.1: EVERY mapping entry point must notify ─────
    //
    // B2 put the notification in `Mmu::rebuild_ptr()` alone and seeded nothing,
    // so `EventTable::slot_page_` was all zeros for the life of the session and
    // `map_rom_physical()` — reached from a plain guest `OUT (0x7FFD)` ROM
    // select — bypassed the one notifier entirely. The failure was silent in
    // BOTH directions, which is why the enumeration is the row and not a single
    // happy-path check: a filter naming page 0x00 armed all eight slots, and one
    // naming the page actually mapped armed none.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        // The page ALREADY mapped at slot 3 before the Debugger exists — the
        // case B2 could not see, because `on_slot_remapped` early-returns while
        // the table is null and every `rebuild_ptr()` in `init()` was discarded.
        const uint16_t live_page = emu.mmu().get_effective_page(3);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { live_page };
        dbg.subscribe(1, s);
        check("EVT-SLOT-10", "a page ALREADY mapped when the table is installed arms "
                             "its slot — the eight live pages are seeded, not zeros",
              emu.debug_state().wr_watch_armed(0x6000),
              "page=" + std::to_string(live_page));
    }
    {
        // THE OTHER DIRECTION of B-1a, and the first version of this row could not
        // see it: its escape-hatch disjunct `get_effective_page(0) == 0` is
        // unconditionally TRUE in a ZX128K boot configuration, so the row passed
        // whatever the mask said. A page-0x00 filter must arm the slots that hold
        // page 0 AND NO OTHERS — with a zero-initialised cache it armed all eight.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { 0x00 };
        dbg.subscribe(1, s);

        uint8_t want = 0;
        for (int sl = 0; sl < 8; ++sl)
            if (emu.mmu().get_effective_page(sl) == 0)
                want = static_cast<uint8_t>(want | (1u << sl));
        check("EVT-SLOT-11", "a filter on page 0x00 arms exactly the slots that hold "
                             "page 0 — not all eight, which is what a zero-initialised "
                             "cache gave it",
              emu.debug_state().wr_watch_mask() == want,
              "mask=" + std::to_string(emu.debug_state().wr_watch_mask()) +
                  " want=" + std::to_string(want));
        check("EVT-SLOT-11B", "and the row is not vacuous: at least one slot does NOT "
                              "hold page 0, so `all eight` and `the right ones` are "
                              "different answers",
              want != 0xFF, "want=" + std::to_string(want));
    }
    {
        // One row per PUBLIC mapping entry point. `map_rom_physical()` is private
        // and reached from four of them; `set_page()` goes through `rebuild_ptr`.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { 0x2A };
        dbg.subscribe(1, s);

        // THE INVARIANT, asserted as a sweep rather than as a guess at which page
        // a given API lands on: for EVERY slot, the armed bit must agree with the
        // page the MMU says that slot serves. A missed notification breaks it on
        // whichever slot the call touched, whatever the API's own page arithmetic
        // turns out to be — which is the only form of this row that cannot be
        // written to pass by accident.
        auto mask_agrees = [&emu](uint16_t watched) {
            for (int sl = 0; sl < 8; ++sl) {
                const bool want = emu.mmu().get_effective_page(sl) == watched;
                const bool got  =
                    emu.debug_state().wr_watch_armed(static_cast<uint16_t>(sl << 13));
                if (want != got) return false;
            }
            return true;
        };
        check("EVT-SLOT-12", "the invariant holds before any mapping call",
              mask_agrees(0x2A));

        emu.mmu().set_page(4, 0x2A);
        check("EVT-SLOT-13", "set_page() keeps it (via rebuild_ptr)",
              mask_agrees(0x2A) && emu.debug_state().wr_watch_armed(0x8000));
        emu.mmu().set_page(4, 0x10);

        // map_rom() -> map_rom_physical(): the path that bypassed the notifier.
        emu.mmu().map_rom(2, 0x2A);
        check("EVT-SLOT-14", "map_rom() keeps it — it routes through "
                             "map_rom_physical(), which B2 left silent",
              mask_agrees(0x2A));
        emu.mmu().set_page(2, 0x10);

        // map_128k_bank() is the legacy 0x7FFD path's own entry point, and it
        // re-maps slots 0, 1, 6 and 7 in one call.
        emu.mmu().map_128k_bank(0x2A);
        check("EVT-SLOT-15", "map_128k_bank() keeps it across every slot it moves",
              mask_agrees(0x2A));
        emu.mmu().map_plus3_bank(0x04);
        check("EVT-SLOT-16", "and so does map_plus3_bank()", mask_agrees(0x2A));

        // The non-vacuous half: at least one of those calls must actually have put
        // the watched page somewhere, or the sweep above is comparing false to
        // false eight times.
        emu.mmu().set_page(6, 0x2A);
        check("EVT-SLOT-17", "and the sweep is not vacuous — the watched page really "
                             "does arm exactly its slot",
              mask_agrees(0x2A) && emu.debug_state().wr_watch_armed(0xC000) &&
              !emu.debug_state().wr_watch_armed(0x8000));

        // THE PATH THAT NEEDS `map_rom_physical()`'S OWN NOTIFY. The other four
        // callers write `nr_mmu_` afterwards, so the notifying setter covers them;
        // `set_nr_8c()` -> `engage_legacy_rom_paging_slot(slot, false)` deliberately
        // does NOT touch `nr_mmu_` (VHDL leaves MMU<i> alone on an NR 0x8C write,
        // zxnext.vhd:3813), so it re-points the slot with NOTHING else to notify.
        // Put slots 0/1 back into legacy ROM mode so the NR 0x8C path has
        // something to re-point, and watch the ROM page it will select.
        emu.mmu().map_128k_bank(0x00);
        emu.mmu().set_nr_8c(0x00);
        const uint16_t rom_page_lock1 = 2;   // lock_rom1 -> sram_rom 2 -> page 4/5
        Subscription rs;
        rs.kind = EventKind::Mem; rs.access = Access::Read;
        rs.filter.pages = { rom_page_lock1 };
        dbg.subscribe(1, rs);
        auto rd_agrees = [&emu](uint16_t watched) {
            for (int sl = 0; sl < 8; ++sl) {
                const bool want = emu.mmu().get_effective_page(sl) == watched;
                const bool got  =
                    emu.debug_state().rd_watch_armed(static_cast<uint16_t>(sl << 13));
                if (want != got) return false;
            }
            return true;
        };
        check("EVT-SLOT-18", "the read invariant holds before the NR 0x8C write",
              rd_agrees(rom_page_lock1));
        // NR 0x8C bit 4 is altrom_lock_rom0, bit 5 altrom_lock_rom1
        // (zxnext.vhd:2997-3007): either one overrides `sram_rom`, so slots 0/1 are
        // re-pointed to a DIFFERENT physical ROM page — through
        // `engage_legacy_rom_paging_slot(slot, set_nr_sentinel=false)`, which is the
        // ONE re-mapping path that deliberately leaves `nr_mmu_` alone (VHDL leaves
        // MMU<i> untouched on an NR 0x8C write, :3813). So it is the one path with
        // nothing but `map_rom_physical()`'s own notify behind it.
        emu.mmu().set_nr_8c(0x20);
        check("EVT-SLOT-19", "and it still holds after it — `map_rom_physical()`'s own "
                             "notify is what carries this path",
              rd_agrees(rom_page_lock1),
              "slot0=" + std::to_string(emu.mmu().get_effective_page(0)) +
                  " mask=" + std::to_string(emu.debug_state().rd_watch_mask()));
    }
    {
        // The GUEST-REACHABLE version of the same thing: a plain OUT (0x7FFD)
        // ROM select reaches apply_legacy_rom_slots_ -> map_rom_physical, and B2
        // left a page-qualified filter on the ROM page reading a stale page.
        Emulator emu;
        //   8000  3E 10        LD A,0x10      (ROM select bit 4 set)
        //   8002  01 FD 7F     LD BC,0x7FFD
        //   8005  ED 79        OUT (C),A
        //   8007  18 FE        JR $
        build_armed(emu, { 0x3E, 0x10, 0x01, 0xFD, 0x7F, 0xED, 0x79, 0x18, 0xFE },
                    MachineType::ZX128K);
        Debugger dbg(emu);
        const uint16_t rom_page_after = 2;   // sram_rom 1 -> pages 2/3 in slots 0/1
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Read;
        s.filter.pages = { rom_page_after };
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-SLOT-20", "a guest OUT (0x7FFD) ROM select notifies, so a filter "
                             "on the newly selected ROM page arms its slot",
              emu.debug_state().rd_watch_armed(0x0000) ==
                  (emu.mmu().get_effective_page(0) == rom_page_after),
              "slot0 page=" + std::to_string(emu.mmu().get_effective_page(0)));
    }

    // ── EVT-GATE-20..26 — §4.1: the backend is INERT while unarmed ─────────
    //
    // B2's latch sites gated on `has_kind()` (or an engine flag) while all four
    // drains gated on `armed() && events_pending()`. A subscription on a machine
    // with no client attached therefore FILLED the 512-entry ring and dumped up
    // to 513 stale events at the first armed boundary, every one carrying a
    // cycle from before the machine was being watched and `overflowed` set.
    // EVT-GATE-10/11 asserted the §4.1 property for `Mem` only — the two kinds
    // whose sites do take `watchpoints_live()`.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        emu.debug_state().set_clients_attached(false);          // UNARMED
        emu.debug_state().set_live_raster(false);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        check("EVT-GATE-20", "armed() is false", !dbg.armed());
        emu.run_frame();
        emu.run_frame();
        emu.run_frame();
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-GATE-21", "a LATCHING kind latches nothing on an unarmed machine — "
                             "the ring stays empty",
              t->size() == 0 && t->dropped() == 0,
              "size=" + std::to_string(t->size()) +
                  " dropped=" + std::to_string(t->dropped()));
        check("EVT-GATE-22", "so there is no boundary work pending",
              !emu.debug_state().events_pending());
        check("EVT-GATE-23", "and nothing was delivered",
              rec.evs.empty() && dbg.events_fired_since(0).empty());

        // Now arm it. The point of the row: what arrives is THIS frame's events,
        // not a 512-entry dump of the three frames nobody was watching.
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        emu.run_frame();
        check("EVT-GATE-24", "arming it delivers exactly the armed frame's events",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-GATE-25", "not flagged as lossy, because nothing was ever dropped",
              rec.evs.size() == 1 && !rec.evs[0].overflowed &&
              rec.evs[0].dropped == 0);
        check("EVT-GATE-26", "and the delivered event's cycle is from the ARMED frame",
              rec.evs.size() == 1 && rec.evs[0].frame == 3,
              rec.evs.empty() ? "" : "frame=" + std::to_string(rec.evs[0].frame));
    }
    {
        // The same for the Copper and DMA engines, whose own flags carry the
        // term at the site as well as in the funnel (cost, not correctness).
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        emu.debug_state().set_clients_attached(false);
        emu.debug_state().set_live_raster(false);
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Copper;
        s.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        copper_program(emu, { move_word(0x30, 0x7F), HALT_WORD });
        copper_start(emu);
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        check("EVT-GATE-30", "an unarmed machine latches no Copper event either",
              rec.evs.empty() &&
              emu.debug_state().event_table()->size() == 0);
        check("EVT-GATE-31", "and the MOVE still happened — the gate suppresses the "
                             "REPORTING, never the emulation",
              emu.nextreg().peek(0x30) == 0x7F);
    }

    // ── EVT-NR-20..23 — a DMA NextREG write is `source = Dma` ──────────────
    //
    // `nextreg.h`'s `set_write_source()` doc-comment asserted that
    // `Emulator`'s `dma_.write_io` lambda brackets the write. It did not — a
    // justification comment whose premise was false, which is the exact class of
    // defect comment discipline exists to prevent. Five frontends read this field:
    // a `{source=Dma}` filter could never match and a `{source=Cpu}` one fired
    // falsely on every DMA NextREG write.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        // One byte, 0x42, from RAM into I/O port 0x253B with NR 0x16 selected.
        emu.nextreg().select(0x16);
        emu.mmu().write(0xA000, 0x42);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.filter.regs = { 0x16 };
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);

        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        // R0 dir A->B, port A start 0xA000, len 1; R1 port A = memory;
        // R2 port B = I/O; R4 mode + port B start 0x253B; R6 LOAD; R6 ENABLE.
        w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
        w(0x14);                         // R1 port A = memory, inc
        w(0x28);                         // R2 port B = I/O, fixed
        w(0xAD); w(0x3B); w(0x25);       // R4 mode + port B = 0x253B
        w(0xCF); w(0x87);                // R6 LOAD, R6 ENABLE
        emu.execute_single_instruction();

        check("EVT-NR-20", "a DMA transfer with NR 0x253B as its I/O destination "
                           "raises exactly one NextRegWrite",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-NR-21", "with the register and the byte the DMA moved",
              rec.evs.size() == 1 && rec.evs[0].reg == 0x16 &&
              rec.evs[0].value == 0x42);
        check("EVT-NR-22", "tagged source=Dma, not Cpu",
              rec.evs.size() == 1 && rec.evs[0].source == EventSource::Dma,
              rec.evs.empty() ? ""
                              : "source=" + std::to_string(
                                    static_cast<int>(rec.evs[0].source)));
        check("EVT-NR-23", "and the register really took it",
              emu.nextreg().peek(0x16) == 0x42);
    }
    {
        // The filter's negative arm, which is the half a frontend depends on: a
        // {source=Cpu} subscription must NOT see the DMA's write.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        emu.nextreg().select(0x16);
        emu.mmu().write(0xA000, 0x42);
        Rec rec;
        Subscription s;
        s.kind = EventKind::NextRegWrite; s.filter.regs = { 0x16 };
        s.filter.source = EventSource::Cpu;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        Dma& d = emu.dma();
        auto w = [&](uint8_t v) { d.write(v, false); };
        w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
        w(0x14);                         // R1 port A = memory, inc
        w(0x28);                         // R2 port B = I/O, fixed
        w(0xAD); w(0x3B); w(0x25);       // R4 mode + port B = 0x253B
        w(0xCF); w(0x87);                // R6 LOAD, R6 ENABLE
        emu.execute_single_instruction();
        check("EVT-NR-24", "a {source=Cpu} subscription does NOT see the DMA's write",
              rec.evs.empty(), "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-TIME-15..19 — `Scanline` carries the LINE's cycle ──────────────
    //
    // `events.h` promises "latched at the line with its exact cycle". B2 stamped
    // `clock_.get()` from inside the post-instruction device cluster — already
    // past the boundary by the whole instruction that crossed it — and discarded
    // `raw_line`, the exact value the caller handed in, one line away. The old
    // The row that used to sit at EVT-TIME-11 asserted the payload's `cvc`
    // against the field the filter matched on, which is a tautology an
    // off-by-one survives; it is deleted, not merely superseded.
    {
        Emulator emu;
        // `JR $` ALONE, and the instruction mix is the row. A 12 T-state
        // instruction is 96 master cycles and a 48K line is 1792, so 1792/96 is
        // not an integer and the instruction that crosses a line boundary
        // OVERSHOOTS it — which is the condition under which the live clock is
        // not the boundary. The earlier fixture was `NOP; JR -3`: 16 T-states =
        // 128 master cycles, 1792/128 = 14 EXACTLY, so `clock_.get()` landed on
        // the boundary every line and a mutation that stamped the live clock
        // survived the whole suite. Coverage uniformly on one side of the
        // condition, in one line of machine code.
        build_armed(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        const uint64_t frame_start = emu.clock().get();
        emu.run_frame();
        check("EVT-TIME-15", "a Scanline event was delivered",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            const uint64_t mcpl = dbg.machine().master_cycles_per_line;
            // cvc = (raw_vc - min_vactive + cu_offset) mod lines_per_frame, derived
            // here from VideoTiming rather than read back from the payload — an
            // INDEPENDENT oracle, which is what the replaced row lacked.
            const int lpf  = emu.video_timing().vc_max() + 1;
            const int minv = emu.video_timing().display_origin().vc;
            const int cuo  = emu.video_timing().cu_offset();
            const int raw  = rec.evs[0].vc;
            int want_cvc = (raw - minv + cuo) % lpf;
            if (want_cvc < 0) want_cvc += lpf;
            check("EVT-TIME-16", "and its cvc matches an INDEPENDENT derivation from "
                                 "the raw line it reports",
                  want_cvc == rec.evs[0].cvc,
                  "raw=" + std::to_string(raw) + " want=" + std::to_string(want_cvc) +
                      " got=" + std::to_string(rec.evs[0].cvc));
            check("EVT-TIME-17", "`hc` is 0 — the event names the START of the line, "
                                 "not wherever the crossing instruction ended",
                  rec.evs[0].hc == 0, "hc=" + std::to_string(rec.evs[0].hc));
            check("EVT-TIME-18", "and `cycle` is the line's own boundary cycle",
                  rec.evs[0].cycle ==
                      frame_start + static_cast<uint64_t>(rec.evs[0].vc) * mcpl,
                  "cycle=" + std::to_string(rec.evs[0].cycle) + " want=" +
                      std::to_string(frame_start +
                                     static_cast<uint64_t>(rec.evs[0].vc) * mcpl));
            check("EVT-TIME-19", "which is BEHIND the clock at delivery — the proof "
                                 "that it is not just `clock_.get()` again",
                  rec.evs[0].cycle < emu.clock().get());
        }
    }
    {
        // THE OVERSHOOT INSTANCE, and it took a measurement to find one. For most
        // lines of most fixtures the instruction that crosses a line boundary ends
        // exactly ON it and `clock_.get()` at the `on_scanline` callback IS the
        // boundary — which is why a mutation that stamped the live clock survived
        // the group above and every other row. RAW LINE 0 is different: the frame's
        // first instruction has already run when its event fires, so the live clock
        // is 32 master cycles past the boundary and the live `hc` is 8.
        //
        // cvc for raw line 0 is (0 - min_vactive + cu_offset) mod lines_per_frame,
        // computed here rather than written as a constant so the row follows the
        // machine.
        Emulator emu;
        build_armed(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        const int lpf  = emu.video_timing().vc_max() + 1;
        const int minv = emu.video_timing().display_origin().vc;
        const int cuo  = emu.video_timing().cu_offset();
        int cvc_line0 = (0 - minv + cuo) % lpf;
        if (cvc_line0 < 0) cvc_line0 += lpf;

        Rec rec;
        Subscription s;
        s.kind = EventKind::Scanline;
        s.filter.scanline = static_cast<int16_t>(cvc_line0);
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        const uint64_t frame_start = emu.clock().get();
        emu.run_frame();
        check("EVT-TIME-29", "the raw-line-0 event is delivered",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        if (rec.evs.size() == 1) {
            check("EVT-TIME-30", "and it names raw line 0",
                  rec.evs[0].vc == 0, "vc=" + std::to_string(rec.evs[0].vc));
            check("EVT-TIME-31", "with `hc` 0 and `cycle` AT the frame's start — the "
                                 "live clock is 32 master cycles past it here, so "
                                 "this is the row the aligned group above cannot be",
                  rec.evs[0].hc == 0 && rec.evs[0].cycle == frame_start,
                  "hc=" + std::to_string(rec.evs[0].hc) + " cycle=" +
                      std::to_string(rec.evs[0].cycle) + " frame_start=" +
                      std::to_string(frame_start));
        }
    }
    {
        // EVT-TIME-32/33 (GH #290) — `cvc` counts from the NR 0x64 value it was
        // last RELOADED from, and the reload is ~120 pixels into raw line
        // c_min_vactive (zxula_timing.vhd:457-462): AFTER that line's Scanline
        // event is latched at the line's start. Frame 1 reloads 0; NR 0x64 = 20
        // is written at the boundary, so frame 2's lines before its reload
        // still count from 0 — raw c_min_vactive-1 is cvc lpf-1 — and the
        // reload line and every one after count from 20.
        Emulator emu;
        build_armed(emu, { 0x18, 0xFE });
        Debugger dbg(emu);
        emu.run_frame();
        emu.nextreg().write(0x64, 20);
        const int lpf  = emu.video_timing().vc_max() + 1;
        const int minv = emu.video_timing().display_origin().vc;
        Rec before, reload;
        Subscription s1;
        s1.kind = EventKind::Scanline;
        s1.filter.scanline = static_cast<int16_t>(lpf - 1);
        s1.action = Action::Continue; s1.handler = recorder(before);
        dbg.subscribe(1, s1);
        Subscription s2;
        s2.kind = EventKind::Scanline;
        s2.filter.scanline = 20;
        s2.action = Action::Continue; s2.handler = recorder(reload);
        dbg.subscribe(1, s2);
        emu.run_frame();
        check("EVT-TIME-32", "the ula_min_vactive line's Scanline event names cvc "
                             "20: its cvc is loaded by the frame's reload, after "
                             "the line's start where the event is latched, from "
                             "NR 0x64 as it stands (zxula_timing.vhd:457-462)",
              reload.evs.size() == 1 && reload.evs[0].vc == minv,
              "n=" + std::to_string(reload.evs.size()) +
                  (reload.evs.empty() ? std::string()
                                      : " vc=" + std::to_string(reload.evs[0].vc)));
        check("EVT-TIME-33", "and the line before it still names cvc lpf-1, "
                             "counting from the previous frame's reload: an NR "
                             "0x64 write does not reach the lines before the next "
                             "reload (zxula_timing.vhd:457-466)",
              before.evs.size() == 1 && before.evs[0].vc == minv - 1,
              "n=" + std::to_string(before.evs.size()) +
                  (before.evs.empty() ? std::string()
                                      : " vc=" + std::to_string(before.evs[0].vc)));
    }

    // ── EVT-COP-60..66 — one arming flag per Copper sub-kind ───────────────
    //
    // B2 armed the whole engine from `has_kind(Copper) || has_kind(NextRegWrite)`,
    // so a `Halt`-only subscriber accumulated ~16 `Move` entries per instruction
    // slot, and `EventTable::has_copper_sub_kind()` — written for exactly this —
    // had zero callers. Ring space is observable, which B2 argues itself in
    // `Mmu::watch_write_`.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription h;
        h.kind = EventKind::Copper;
        h.filter.copper_kind = jnext::dbg::CopperEventKind::Halt;
        h.action = Action::Continue;
        dbg.subscribe(1, h);
        check("EVT-COP-60", "a Halt-only subscription arms the Halt site and NOT the "
                            "Move or Wait sites",
              emu.copper().halt_events_armed() &&
              !emu.copper().move_events_armed() &&
              !emu.copper().wait_events_armed());

        // Sixteen MOVEs then a HALT. With one flag for the engine, the ring held
        // the MOVEs; with one per sub-kind it holds the HALT alone.
        std::vector<uint16_t> prog;
        for (int i = 0; i < 16; ++i)
            prog.push_back(move_word(0x30, static_cast<uint8_t>(0x10 + i)));
        prog.push_back(HALT_WORD);
        copper_program(emu, prog);
        copper_start(emu);
        emu.execute_single_instruction();
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-COP-61", "so a MOVE burst consumes NO ring space for it",
              t->size() <= 1, "size=" + std::to_string(t->size()));
        check("EVT-COP-62", "and nothing was dropped",
              t->dropped() == 0, "dropped=" + std::to_string(t->dropped()));
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription w;
        w.kind = EventKind::Copper;
        w.filter.copper_kind = jnext::dbg::CopperEventKind::Wait;
        dbg.subscribe(1, w);
        check("EVT-COP-63", "a Wait-only subscription arms the Wait site alone",
              emu.copper().wait_events_armed() &&
              !emu.copper().move_events_armed() &&
              !emu.copper().halt_events_armed());
    }
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription n;
        n.kind = EventKind::NextRegWrite;
        dbg.subscribe(1, n);
        check("EVT-COP-64", "a NextRegWrite subscription alone still arms the MOVE "
                            "site — one MOVE latch fans out to both kinds",
              emu.copper().move_events_armed() &&
              !emu.copper().wait_events_armed() &&
              !emu.copper().halt_events_armed());
    }
    {
        // The DMA twin: Start/End and Byte were already separable, but End was
        // folded in with Start. Three flags now.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription e;
        e.kind = EventKind::Dma;
        e.filter.dma_kind = jnext::dbg::DmaEventKind::End;
        dbg.subscribe(1, e);
        check("EVT-COP-65", "an End-only DMA subscription arms End and NOT Start",
              emu.dma().end_events_armed() && !emu.dma().start_events_armed() &&
              !emu.dma().byte_events_armed());
        check("EVT-COP-66", "and the composite events_armed() still reports true",
              emu.dma().events_armed());
    }

    // ── EVT-DEL-60..64 — a nested delivery must not wipe the boundary's hits ─
    //
    // §4.3 makes `matched[]` part of the `Paused` contract and B3 is its first
    // consumer. `raise_host_event()` cleared it unconditionally, and a `Handler`
    // may call that verb from inside the drain's loop — which
    // `deliver_to_subscribers` documents as supported. The list also MOVED, from
    // `Debugger::Impl` (a private nested struct nothing could observe, which is
    // why no row could see this) to `EventTable::hits()`.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        // Two Mem subscriptions on the same write, both Stop, so the boundary has
        // two hits to lose; the FIRST one's handler raises a host event.
        Subscription host;
        host.kind = EventKind::Host; host.action = Action::Continue;
        dbg.subscribe(1, host);

        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Stop;
        a.handler = nullptr;                      // static Stop, records hit #1
        const auto first = dbg.subscribe(1, a);
        // The NESTED raise is on the SECOND subscription, deliberately: a handler
        // runs BEFORE its own hit is recorded, so nesting from the FIRST one clears
        // a list that is still empty and the bug is invisible. Nesting from the
        // second clears a list that already holds hit #1 — which is the only
        // ordering in which an unconditional clear can be seen at all.
        Subscription b = a;
        b.handler = [](const DbgEvent&, Debugger& d) {
            d.raise_host_event(1, "nested");
            return Action::Stop;
        };
        const auto second = dbg.subscribe(2, b);

        emu.run_frame();
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-DEL-60", "the machine stopped", dbg.state().paused);
        check("EVT-DEL-61", "and the boundary's hit list survived the NESTED "
                            "raise_host_event — both subscriptions are in it",
              t->hits().size() == 2,
              "hits=" + std::to_string(t->hits().size()));
        check("EVT-DEL-62", "naming both, in the order they matched",
              t->hits().size() == 2 && t->hits()[0].event_id == first.value &&
              t->hits()[1].event_id == second.value);
        check("EVT-DEL-63", "with the address and direction that matched",
              t->hits().size() == 2 && t->hits()[0].addr == 0x9000 &&
              t->hits()[0].access == Access::Write &&
              t->hits()[0].value == 0x5A);
        check("EVT-DEL-64", "and the FIRST Stop is still the pause reason",
              dbg.state().pause_reason.id == first.value);
    }
    {
        // A TOP-LEVEL raise_host_event still clears: it starts its own batch.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Host; s.action = Action::Stop;
        dbg.subscribe(1, s);
        dbg.raise_host_event(1, "one");
        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-DEL-65", "a top-level raise leaves exactly its own hit",
              t->hits().size() == 1, "hits=" + std::to_string(t->hits().size()));
        dbg.run(1);
        dbg.raise_host_event(1, "two");
        check("EVT-DEL-66", "and a second one replaces it rather than appending",
              t->hits().size() == 1, "hits=" + std::to_string(t->hits().size()));
    }

    // ── EVT-TIME-23..27 — `Cycle` is one-shot, and enforced ────────────────
    //
    // `events.h` says "one-shot BY NATURE" and nothing enforced it: the filter is
    // `master_cycle >= N`, so once the target passes it matches at EVERY boundary
    // for the rest of the session, `cycle_armed_` keeps `events_pending()`
    // permanently true, and `action = Stop` makes the machine unadvanceable.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Cycle; s.filter.cycle = emu.clock().get() + 2000;
        s.action = Action::Continue; s.handler = recorder(rec);
        const auto sub = dbg.subscribe(1, s);
        check("EVT-TIME-23", "subscribe() FORCES `once` on a Cycle subscription, and "
                             "the model a client lists says so",
              !dbg.subscriptions(true).empty() &&
              dbg.subscriptions(true).front().once);
        emu.run_frame();
        check("EVT-TIME-24", "it fires exactly once over a whole frame, not at every "
                             "boundary past its target",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        check("EVT-TIME-25", "and it is spent, so the per-boundary drain retires",
              !dbg.subscriptions(true).front().live &&
              !emu.debug_state().events_pending());
        emu.run_frame();
        check("EVT-TIME-26", "a second frame adds nothing",
              rec.evs.size() == 1, "n=" + std::to_string(rec.evs.size()));
        (void)sub;
    }
    {
        // The one that made the machine unadvanceable: Stop.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Cycle; s.filter.cycle = emu.clock().get() + 2000;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        emu.run_frame();
        check("EVT-TIME-27", "a Cycle Stop pauses once",
              dbg.state().paused);
        const uint64_t at_stop = emu.clock().get();
        dbg.run(1);
        emu.run_frame();
        check("EVT-TIME-28", "and the machine ADVANCES after the resume — it does not "
                             "re-pause on the same passed target for ever",
              !dbg.state().paused && emu.clock().get() > at_stop,
              "clock=" + std::to_string(emu.clock().get()) + " was=" +
                  std::to_string(at_stop));
    }

    // ── EVT-SUB-20..23 — the refusal set's two remaining members ───────────
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription dbgsrc;
        dbgsrc.kind = EventKind::Mem; dbgsrc.access = Access::Write;
        dbgsrc.filter.source = EventSource::Debugger;
        check("EVT-SUB-20", "a filter naming EventSource::Debugger is refused — that "
                            "source never appears in a delivered Event (§4.2a)",
              dbg.subscribe(1, dbgsrc).status == Result::RefusedUnavailable);

        Subscription defport;
        defport.kind = EventKind::Port;      // DEFAULT filter: mask 0xFFFF, value 0
        check("EVT-SUB-21", "a Port subscription left with the default filter is "
                            "refused — it would match only port 0x0000, where every "
                            "other kind's default matches everything",
              dbg.subscribe(1, defport).status == Result::RefusedUnavailable);

        Subscription okport = defport;
        okport.filter.port_mask = 0x00FF; okport.filter.port_value = 0xFE;
        check("EVT-SUB-22", "and setting the mask makes it acceptable",
              dbg.subscribe(1, okport).status == Result::Ok);
        Subscription zeroport = defport;
        zeroport.filter.port_mask = 0xFFFF; zeroport.filter.port_value = 0x0001;
        check("EVT-SUB-23", "an EXPLICIT full-decode filter on a real port is fine — "
                            "the refusal is about the default, not about the value",
              dbg.subscribe(1, zeroport).status == Result::Ok);
    }

    // ── EVT-SUB-30..32 — erase_client()'s defining condition ───────────────
    {
        EventTable t;
        Subscription s; s.kind = EventKind::Execute;
        const EventId a1 = t.add(7, s);
        const EventId a2 = t.add(7, s);
        const EventId b1 = t.add(8, s);
        check("EVT-SUB-30", "erase_client() drops every row of THAT client and counts "
                            "them",
              t.erase_client(7) == 2 && t.find(a1) == nullptr &&
              t.find(a2) == nullptr);
        check("EVT-SUB-31", "and leaves another client's alone",
              t.find(b1) != nullptr && t.find(b1)->live);
        check("EVT-SUB-32", "a second call for the same client drops nothing",
              t.erase_client(7) == 0);
    }

    // ── EVT-STEP-10..14 — B-9: the two `event_stop_pending_` conditionals ──
    //
    // `debugger_step()`'s halt-run loop condition and its consume both survived
    // mutation, and the manifest listed "the whole debugger_step() drain" as a
    // closure. A Stop from a kind whose site is in the device cluster (Copper) or
    // at a line boundary (Scanline) is what reaches them — an `Execute` stop never
    // does, because the pre-instruction gate pauses before the step body runs.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x00, 0x00, 0x18, 0xFB });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        // Step until the scanline is crossed. The stop arrives through
        // event_stop_pending_, which debugger_step() must consume.
        int steps = 0;
        while (steps < 40000 && dbg.events_fired_since(0).empty()) {
            dbg.step_into(1);
            ++steps;
        }
        check("EVT-STEP-10", "a Scanline Stop is delivered on the STEP path",
              !dbg.events_fired_since(0).empty(),
              "steps=" + std::to_string(steps));
        check("EVT-STEP-11", "and the machine is paused after it",
              dbg.state().paused);
        // The consume is what this row is for: if `event_stop_pending_` were left
        // set, the NEXT step at a HALT would exit its run-out loop immediately.
        // Put the CPU on a HALT and step: it must run the halt out (GH #207).
        {
            Z80Registers r = emu.cpu().get_registers();
            emu.mmu().write(0xB000, 0x76);          // HALT
            r.PC = 0xB000; r.IFF1 = 1; r.IM = 1;
            emu.cpu().set_registers(r);
        }
        const int t_states = dbg.step_into(1) == Result::Ok
                                 ? emu.debugger_step()
                                 : 0;
        check("EVT-STEP-12", "a later Step at a HALT still runs the halt out — the "
                             "pending-Stop latch was consumed, not left standing",
              t_states > 100, "tstates=" + std::to_string(t_states));
    }

    // ── EVT-REPLAY-10..12 — B-9: the execute_gate replay guard ─────────────
    //
    // `drain_boundary`'s replay guard has a row; `execute_gate`'s twin did not.
    // §4.2a: `rewind_to_cycle()` fast-forwards with the gate LIVE, so an
    // `Execute` subscription inside the replayed span would pause the replay short.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x00, 0x00, 0x18, 0xFB });
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Execute;
        s.filter.lo = 0x8002; s.filter.hi = 0x8002;
        s.action = Action::Stop;
        dbg.subscribe(1, s);
        emu.set_replay_mode(true);
        emu.run_frame();
        check("EVT-REPLAY-10", "an Execute subscription does NOT stop a replay",
              !dbg.state().paused && dbg.events_fired_since(0).empty(),
              "pc=" + hex(pc_of(emu)));
        check("EVT-REPLAY-11", "and the replay really ran the address",
              pc_of(emu) == 0x8000 || pc_of(emu) == 0x8003 ||
              pc_of(emu) == 0x8002 || pc_of(emu) == 0x8001);
        emu.set_replay_mode(false);
        {
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; emu.cpu().set_registers(r);
        }
        emu.run_frame();
        check("EVT-REPLAY-12", "with replay off the same subscription DOES stop it — "
                               "the control the guard's row needs",
              dbg.state().paused && pc_of(emu) == 0x8002,
              "pc=" + hex(pc_of(emu)));
    }

    // ── EVT-TRANS-40..43 — B-9: a NON-Execute Stop drops the transients ────
    //
    // `drain_boundary`'s `if (stop) apply_stop();` survived mutation; only the
    // `execute_gate` twin was caught, so transient auto-removal was proven for an
    // `Execute`-caused stop alone.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x00, 0x00, 0x18, 0xFE });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Subscription tr;
        tr.kind = EventKind::Execute; tr.transient = true;
        tr.filter.lo = 0x8007; tr.filter.hi = 0x8007;
        tr.action = Action::Stop;
        dbg.subscribe(1, tr);
        Subscription mem;
        mem.kind = EventKind::Mem; mem.access = Access::Write;
        mem.filter.lo = 0x9000; mem.filter.hi = 0x9000;
        mem.action = Action::Stop;
        dbg.subscribe(1, mem);
        check("EVT-TRANS-40", "both are armed", dbg.subscriptions(true).size() == 2);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-41", "the MEM subscription stops first, at the writer's "
                              "boundary",
              dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        check("EVT-TRANS-42", "and that stop dropped the TRANSIENT — auto-removal is "
                              "not an Execute-only rule",
              dbg.subscriptions(true).size() == 1 &&
              !dbg.subscriptions(true).front().transient);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-43", "so the machine runs past the retired target",
              !dbg.state().paused && pc_of(emu) == 0x8007,
              "pc=" + hex(pc_of(emu)));
    }
    {
        // B-9: `clear_transient()`'s `if (n) refresh();`. Without the refresh a
        // tombstoned transient keeps `live == true` and fires AGAIN, which
        // `subscriptions()` (which reads `removed`) cannot see.
        Emulator emu;
        build_armed(emu, { 0x06, 0x03, 0x10, 0xFE, 0x18, 0xFA });
        Debugger dbg(emu);
        attach_and_pause(emu);
        Rec rec;
        Subscription tr;
        tr.kind = EventKind::Execute; tr.transient = true;
        tr.filter.lo = 0x8002; tr.filter.hi = 0x8002;
        tr.action = Action::Stop; tr.handler = nullptr;
        dbg.subscribe(1, tr);
        dbg.run(1);
        run_until_paused(emu);
        check("EVT-TRANS-50", "the transient fired and the machine stopped there",
              dbg.state().paused && pc_of(emu) == 0x8002);
        check("EVT-TRANS-51", "and it is gone from the model",
              dbg.subscriptions(true).empty());
        dbg.run(1);
        run_until_paused(emu, 2);
        check("EVT-TRANS-52", "re-reaching the SAME address does not stop again — the "
                              "retired transient is not merely tombstoned, it is no "
                              "longer LIVE",
              !dbg.state().paused, "pc=" + hex(pc_of(emu)));
        (void)rec;
    }

    // ── EVT-DEL-70..73 — B-9: contract 4, a removal during a drain ─────────
    //
    // `const bool outer = !draining` survived mutation because no fixture had the
    // shape: two subscriptions matching ONE event where the first removes a row.
    // Compaction is deferred to the outer frame for exactly this.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int first_calls = 0, second_calls = 0;
        Subscription a;
        a.kind = EventKind::Mem; a.access = Access::Write;
        a.filter.lo = 0x9000; a.filter.hi = 0x9000;
        a.action = Action::Continue;
        const auto second_id_holder = std::make_shared<EventId>(0);
        a.handler = [&first_calls, second_id_holder](const DbgEvent&, Debugger& d) {
            ++first_calls;
            // Remove the OTHER subscription from inside the drain, while the loop
            // is still walking the table.
            d.unsubscribe(1, *second_id_holder);
            return Action::Continue;
        };
        const auto first = dbg.subscribe(1, a);
        Subscription b = a;
        b.handler = [&second_calls](const DbgEvent&, Debugger&) {
            ++second_calls;
            return Action::Continue;
        };
        *second_id_holder = dbg.subscribe(1, b).value;

        emu.run_frame();
        check("EVT-DEL-70", "the first handler ran",
              first_calls == 1, "n=" + std::to_string(first_calls));
        check("EVT-DEL-71", "and removing the second MID-DRAIN suppressed it for this "
                            "delivery without invalidating the walk",
              second_calls == 0, "n=" + std::to_string(second_calls));
        check("EVT-DEL-72", "the removal really took",
              dbg.subscriptions(true).size() == 1 &&
              dbg.subscriptions(true).front().id == first.value);
        check("EVT-DEL-73", "and the machine ran on",
              !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
    }

    {
        // B-9 / contract 4: `const bool outer = !draining` survived mutation,
        // because no fixture had a NESTED delivery that also REMOVES a row. With
        // `outer` forced true the nested frame runs `compact()` — erasing a
        // tombstone out of `subs_` — while the OUTER loop is still indexing it.
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        int mem_calls = 0, host_calls = 0, third_calls = 0;
        const auto victim = std::make_shared<EventId>(0);

        // A Host subscription whose handler UNSUBSCRIBES a third subscription.
        Subscription hs;
        hs.kind = EventKind::Host; hs.action = Action::Continue;
        hs.handler = [&host_calls, victim](const DbgEvent&, Debugger& d) {
            ++host_calls;
            d.unsubscribe(1, *victim);
            return Action::Continue;
        };
        dbg.subscribe(1, hs);

        // The FIRST Mem subscription raises the host event, i.e. nests a delivery
        // that removes a row, while this boundary's own loop is still walking.
        Subscription m1;
        m1.kind = EventKind::Mem; m1.access = Access::Write;
        m1.filter.lo = 0x9000; m1.filter.hi = 0x9000;
        m1.action = Action::Continue;
        m1.handler = [&mem_calls](const DbgEvent&, Debugger& d) {
            ++mem_calls;
            d.raise_host_event(1, "nested");
            return Action::Continue;
        };
        dbg.subscribe(1, m1);

        // ...and a THIRD, later in the table, which the outer loop must still
        // reach after the nested frame has been and gone.
        Subscription m2 = m1;
        m2.handler = [&third_calls](const DbgEvent&, Debugger&) {
            ++third_calls;
            return Action::Continue;
        };
        *victim = dbg.subscribe(1, m2).value;

        emu.run_frame();
        check("EVT-DEL-80", "the outer handler ran and nested a delivery",
              mem_calls == 1 && host_calls == 1,
              "mem=" + std::to_string(mem_calls) + " host=" +
                  std::to_string(host_calls));
        check("EVT-DEL-81", "the nested frame did NOT compact the table under the "
                            "outer loop — the removed row is still reachable as a "
                            "tombstone for this boundary",
              third_calls == 0, "third=" + std::to_string(third_calls));
        check("EVT-DEL-82", "and the removal took effect once the outer frame ended",
              dbg.subscriptions(true).size() == 2);
        check("EVT-DEL-83", "with the machine none the worse for it",
              !dbg.state().paused && pc_of(emu) == 0x8005,
              "pc=" + hex(pc_of(emu)));
        // THE MECHANISM, checkable. Erasing a row under the drain's own index is
        // UNDEFINED, not reliably observable, so EVT-DEL-81 above cannot be the row
        // for contract 4 — a row that depends on UB being visible is not a row.
        // Exactly one compaction must have run for this boundary: the outer frame's.
        check("EVT-DEL-84", "and the nested frame performed NO compaction — contract "
                            "4 defers it to the outer delivery frame",
              emu.debug_state().event_table()->compactions() == 1,
              "compactions=" +
                  std::to_string(emu.debug_state().event_table()->compactions()));
    }

    // ── EVT-ST-01..12 — SAVE / LOAD / REWIND against the new event state ────
    //
    // THE LARGEST UNTESTED SURFACE IN THE PACKAGE, and the decision is written at
    // `Emulator::debug_after_state_restore_()`. In short: the subscription model
    // is HOST-SIDE SESSION state and is deliberately NOT serialised — §4.2a's own
    // precedent, "a mutation is machine state, so the next frame-boundary snapshot
    // carries it; interpreter state (script variables, `once` flags) is not", and
    // `once` flags ARE `EventTable` state; a `Condition` and a `Handler` are
    // closures over a subscriber's interpreter and cannot be serialised at all.
    // What IS reconciled is every piece that DESCRIBES the machine that has gone:
    // the latch ring, the pending Stop, the CTL-13 stop evidence, and
    // `slot_page_` — the one piece that is a cache of machine state.
    //
    // Rows BOTH WAYS: what survives a load, and what must not.
    {
        Emulator emu;
        build_armed(emu, { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE });
        Debugger dbg(emu);
        attach_and_pause(emu);

        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(4, s);
        dbg.set_master_enabled(false);
        dbg.set_client_enabled(4, false);

        auto saved = dbg.save_state_bytes(4, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("EVT-ST-01", "a snapshot is taken",
              saved.status == Result::Ok && !saved.value.empty());

        // Change the session AFTER the save: a load must not undo any of it.
        dbg.set_master_enabled(true);
        dbg.set_client_enabled(4, true);
        const auto extra = dbg.subscribe(5, s);

        check("EVT-ST-02", "two subscriptions and both switches on before the load",
              dbg.subscriptions(true).size() == 2 && dbg.master_enabled() &&
              dbg.client_enabled(4));

        const Result lr = dbg.load_state_bytes(4, saved.value.data(),
                                               saved.value.size());
        check("EVT-ST-03", "the load succeeds", lr == Result::Ok);
        check("EVT-ST-04", "the SUBSCRIPTIONS survive it — a load must not resurrect "
                           "one the user deleted nor delete one they added",
              dbg.subscriptions(true).size() == 2 &&
              dbg.subscriptions(true)[0].id == sub.value &&
              dbg.subscriptions(true)[1].id == extra.value);
        check("EVT-ST-05", "and so do the master and per-client switches",
              dbg.master_enabled() && dbg.client_enabled(4));
        // The delivery cursor: a client's `since(seq)` must stay valid across a
        // load, i.e. `seq` must keep counting rather than restart.
        dbg.set_master_enabled(true);
        dbg.run(4);
        emu.run_frame();
        const auto after = dbg.events_fired_since(0);
        check("EVT-ST-06", "the delivery-sequence cursor keeps counting across the "
                           "load rather than restarting",
              !after.empty() && after.back().seq > 0,
              "n=" + std::to_string(after.size()));
    }
    {
        // The other half: the ring, the pending Stop and the stop evidence are all
        // reconciled, because each describes a machine the load has replaced.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        attach_and_pause(emu);
        auto saved = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("EVT-ST-10", "a snapshot for the reconciliation rows",
              saved.status == Result::Ok);

        // Latch something without draining it: a Scanline subscription plus a
        // frame, with the machine paused before the drain that would empty it.
        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue;
        dbg.subscribe(1, s);
        // A legacy watchpoint stop, so `watch_stop_` is set and survives into a
        // paused state that no resume will clear.
        emu.debug_state().breakpoints().add_watchpoint(0x8000, WatchType::READ);
        dbg.run(1);
        emu.run_frame();
        check("EVT-ST-11", "the machine stopped on the legacy watchpoint, so CTL-13 "
                           "has evidence to lose",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch);

        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        // Force a ring entry that the load must discard.
        jnext::dbg::LatchEntry le;
        le.kind = EventKind::Scanline;
        emu.debug_state().latch_event(le);
        check("EVT-ST-12", "and the ring holds an undrained entry",
              t->size() > 0, "size=" + std::to_string(t->size()));

        dbg.load_state_bytes(1, saved.value.data(), saved.value.size());
        check("EVT-ST-13", "the load DISCARDS the ring — its entries carry a pc, a "
                           "cycle and a frame from a machine that no longer exists",
              t->size() == 0 && t->dropped() == 0 &&
              !emu.debug_state().events_pending(),
              "size=" + std::to_string(t->size()));
        check("EVT-ST-14", "and it clears the CTL-13 stop evidence, which `unpause_()` "
                           "cannot because a load while PAUSED never unpauses",
              !emu.debug_state().watch_stop() && !emu.debug_state().magic_stop() &&
              dbg.state().pause_reason.kind != PauseReason::Kind::Watch);
    }
    {
        // `slot_page_` is the one piece that IS a cache of machine state: the load
        // rewrites the MMU page map through a `StateDesc` walk, which fires no
        // mapping notification at all, so it has to be re-derived.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD }, MachineType::ZX128K);
        Debugger dbg(emu);
        attach_and_pause(emu);
        emu.mmu().set_page(4, 0x2A);
        auto saved = dbg.save_state_bytes(1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        check("EVT-ST-20", "snapshot taken with page 0x2A at slot 4",
              saved.status == Result::Ok &&
              emu.mmu().get_effective_page(4) == 0x2A);

        // Map it away, then subscribe to it, then load the snapshot back.
        emu.mmu().set_page(4, 0x10);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.pages = { 0x2A };
        dbg.subscribe(1, s);
        check("EVT-ST-21", "with the page mapped away, nothing is armed",
              emu.debug_state().wr_watch_mask() == 0);

        dbg.load_state_bytes(1, saved.value.data(), saved.value.size());
        check("EVT-ST-22", "the load restores page 0x2A to slot 4",
              emu.mmu().get_effective_page(4) == 0x2A);
        check("EVT-ST-23", "and the page-qualified filter's slot mask follows it — "
                           "the cache is not trusted across a restore",
              emu.debug_state().wr_watch_armed(0x8000),
              "mask=" + std::to_string(emu.debug_state().wr_watch_mask()));
        // THE PROPERTY, not the code: the mask must agree with the MMU for every
        // slot after a load, by whichever mechanism gets it there. (Two do:
        // `Mmu::load_state`'s own closing `rebuild_ptr` loop, and
        // `debug_after_state_restore_()`'s re-derive. The row outlives either.)
        bool agrees = true;
        for (int sl = 0; sl < 8; ++sl) {
            const bool want = emu.mmu().get_effective_page(sl) == 0x2A;
            const bool got  =
                emu.debug_state().wr_watch_armed(static_cast<uint16_t>(sl << 13));
            agrees = agrees && (want == got);
        }
        check("EVT-ST-24", "and the invariant holds for EVERY slot after the load",
              agrees, "mask=" + std::to_string(emu.debug_state().wr_watch_mask()));
    }
    {
        // A REWIND routes through the same load, so the same reconciliation covers
        // it. The row is that the ring does not survive a step_back.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        for (size_t i = 0; i < 3; ++i)
            emu.mmu().write(static_cast<uint16_t>(PROG + i),
                            static_cast<uint8_t>(i == 0 ? 0x00 : (i == 1 ? 0x18 : 0xFD)));
        {
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
        }
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        Debugger dbg(emu);
        emu.run_frame();
        emu.run_frame();
        attach_and_pause(emu);

        Subscription s;
        s.kind = EventKind::Scanline; s.filter.scanline = 100;
        s.action = Action::Continue;
        const auto sub = dbg.subscribe(1, s);
        jnext::dbg::LatchEntry le;
        le.kind = EventKind::Scanline;
        emu.debug_state().latch_event(le);
        const bool had_entry = emu.debug_state().event_table()->size() > 0;

        const jnext::dbg::RewindRange range = dbg.rewind_range();
        const Result rr = dbg.rewind_to_frame(1, range.oldest_frame);
        check("EVT-ST-30", "a rewind to the oldest buffered frame is accepted",
              rr == Result::Ok,
              "oldest=" + std::to_string(range.oldest_frame) + " newest=" +
                  std::to_string(range.newest_frame) + " rc=" +
                  std::to_string(static_cast<int>(rr)));
        check("EVT-ST-31", "and it discarded the undrained ring, like any load — a "
                           "rewind routes through load_state, so one reconciliation "
                           "covers both",
              had_entry && emu.debug_state().event_table()->size() == 0 &&
              !emu.debug_state().events_pending());
        check("EVT-ST-32", "while the subscription itself survived it",
              dbg.subscriptions(true).size() == 1 &&
              dbg.subscriptions(true).front().id == sub.value);
    }


    // =======================================================================
    // B2 RE-REVIEW (round 2) — the reconciliation sweep, and the row class
    // that kept missing it.
    //
    // `Debugger::load_state_bytes()` calls no control verb, so it never reached
    // `Impl::arm()` — the only thing that clears the backend's own CTL-13 stop
    // evidence — and `state()` reported a `Watch` naming a write the RESTORED
    // machine had not made. `EVT-ST-1x` used `load_state_bytes` but only with the
    // LEGACY watchpoint mechanism, and `EVT-ST-3x` used `rewind_to_frame`, which
    // DOES arm. Every row sat on the working side of the asymmetry.
    //
    // That was the FOURTH appearance of one hazard on this branch — sibling
    // operations where all but one do the thing (latch sites vs drain gates;
    // `set_page()` vs `map_rom_physical()`; `has_dma_sub_kind` wired vs
    // `has_copper_sub_kind` dead). So the fix is a notification from the ONE
    // place a machine is replaced, and the row is the PROPERTY ACROSS THE SET
    // rather than one row per verb: a future verb that lands a new machine is
    // covered by the mechanism, and a future verb that bypasses it fails the
    // sweep the moment someone adds it to the list.
    // =======================================================================
    {
        // Every verb that lands a new machine, driven through the same shape:
        // arrange a SUBSCRIPTION stop (not the legacy watchpoint — that is the
        // half that already worked), apply the verb, and assert the backend's
        // evidence no longer describes the machine that has gone.
        struct Landing {
            const char* id;
            const char* what;
            int         kind;   // 0 load_state_bytes, 1 step_back,
                                // 2 rewind_to_frame, 3 reset(Soft)
        };
        static const Landing kLandings[] = {
            { "EVT-LAND-01", "load_state_bytes()", 0 },
            { "EVT-LAND-02", "step_back()",        1 },
            { "EVT-LAND-03", "rewind_to_frame()",  2 },
            { "EVT-LAND-04", "reset(Soft)",        3 },
        };

        for (const Landing& L : kLandings) {
            Emulator emu;
            EmulatorConfig cfg;
            cfg.type = MachineType::ZX48K;
            cfg.rewind_buffer_frames = 8;
            emu.init(cfg);
            const uint8_t prog[] = { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE };
            for (size_t i = 0; i < sizeof(prog); ++i)
                emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
            {
                Z80Registers r = emu.cpu().get_registers();
                r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
                emu.cpu().set_registers(r);
            }
            emu.debug_state().set_clients_attached(true);
            emu.debug_state().set_live_raster(true);
            Debugger dbg(emu);

            // A snapshot of the machine BEFORE the write, for the two verbs that
            // need one. Taken before the subscription so the restore target is a
            // machine on which the watched write has not happened.
            auto saved = dbg.save_state_bytes(
                1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
            if (L.kind == 1 || L.kind == 2) {
                emu.run_frame();      // fill a rewind slot or two
                emu.run_frame();
                {
                    Z80Registers r = emu.cpu().get_registers();
                    r.PC = PROG; emu.cpu().set_registers(r);
                }
                emu.mmu().write(0x9000, 0x00);
            }

            Subscription s;
            s.kind = EventKind::Mem; s.access = Access::Write;
            s.filter.lo = 0x9000; s.filter.hi = 0x9000;
            s.action = Action::Stop;
            const auto sub = dbg.subscribe(9, s);
            emu.run_frame();

            const bool stopped =
                dbg.state().paused &&
                dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
                dbg.state().pause_reason.id == sub.value;
            const bool had_hit =
                emu.debug_state().event_table()->hits().size() == 1;

            switch (L.kind) {
                case 0: dbg.load_state_bytes(1, saved.value.data(),
                                             saved.value.size());        break;
                case 1: dbg.step_back(1, 1);                             break;
                case 2: dbg.rewind_to_frame(1, dbg.rewind_range().oldest_frame);
                        break;
                case 3: dbg.reset(1, ResetKind::Soft);                   break;
            }

            const auto st = dbg.state();
            const bool cleared =
                st.pause_reason.id != sub.value &&
                st.pause_reason.kind != PauseReason::Kind::Watch &&
                emu.debug_state().event_table()->hits().empty();

            check(L.id, (std::string(L.what) +
                         " reconciles the backend's CTL-13 evidence: the stop it "
                         "leaves behind does not name a subscription that fired on "
                         "the machine it replaced").c_str(),
                  stopped && had_hit && cleared,
                  std::string("stopped=") + (stopped ? "1" : "0") +
                      " had_hit=" + (had_hit ? "1" : "0") +
                      " kind=" + std::to_string(static_cast<int>(st.pause_reason.kind)) +
                      " id=" + std::to_string(st.pause_reason.id) +
                      " hits=" + std::to_string(
                          emu.debug_state().event_table()->hits().size()));
        }
    }
    {
        // The NEGATIVE half of the sweep, so it cannot pass by clearing
        // everything unconditionally: the SUBSCRIPTIONS and the switches must
        // survive every one of those verbs, and a verb that IS a stop must still
        // report its own reason afterwards.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 8;
        emu.init(cfg);
        {
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
        }
        emu.mmu().write(PROG, 0x00);
        emu.mmu().write(PROG + 1, 0x18);
        emu.mmu().write(PROG + 2, 0xFD);
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        Debugger dbg(emu);
        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        const auto sub = dbg.subscribe(4, s);
        dbg.set_client_enabled(4, false);
        emu.run_frame();
        emu.run_frame();
        dbg.pause(1);

        const Result rr = dbg.rewind_to_frame(1, dbg.rewind_range().oldest_frame);
        check("EVT-LAND-10", "a rewind is accepted", rr == Result::Ok,
              "rc=" + std::to_string(static_cast<int>(rr)));
        check("EVT-LAND-11", "the subscription and the per-client switch survive it — "
                             "the sweep reconciles EVIDENCE, not the session",
              dbg.subscriptions(true).size() == 1 &&
              dbg.subscriptions(true).front().id == sub.value &&
              !dbg.client_enabled(4));
        check("EVT-LAND-12", "and a verb that IS a stop still reports its own reason "
                             "after the restore — the reconciliation runs first, the "
                             "verb's arm() second",
              dbg.state().pause_reason.kind == PauseReason::Kind::Step &&
              dbg.state().pause_reason.by == 1,
              "kind=" + std::to_string(
                  static_cast<int>(dbg.state().pause_reason.kind)));

        // ...AND THE SIBLING, because `step_back` is the other verb of the pair
        // and EVT-LAND-12 covers only `rewind_to_frame`. Measured, not assumed:
        // removing `rewind_to_frame`'s `arm(Step)` fails EVT-LAND-12, and
        // removing `step_back`'s survived ALL 866 rows — the reconciliation now
        // clears the latch for both, so EVT-LAND-02 passes either way and the
        // `arm` was pinned on one side of the pair only. Same hazard as the one
        // that put this whole group here, found in the group's own mutation run.
        const Result sb = dbg.step_back(1, 1);
        check("EVT-LAND-13", "`step_back` arms its own reason too — the sibling of "
                             "EVT-LAND-12, and the one that was unpinned",
              sb == Result::Ok &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Step &&
              dbg.state().pause_reason.by == 1,
              "rc=" + std::to_string(static_cast<int>(sb)) + " kind=" +
                  std::to_string(static_cast<int>(dbg.state().pause_reason.kind)) +
                  " by=" + std::to_string(dbg.state().pause_reason.by));
    }
    {
        // The RESET's own event must SURVIVE the reconciliation its reset
        // triggers — the one asymmetry in the sweep, and the reason
        // `debug_after_machine_transition_()` takes a `discard_ring` argument:
        // `soft_reset()` latches `Reset{Soft}` BEFORE init() runs, so discarding
        // the ring there would throw away the event that reports the transition.
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Rec rec;
        Subscription s;
        s.kind = EventKind::Reset; s.filter.reset_kind = ResetKind::Any;
        s.action = Action::Continue; s.handler = recorder(rec);
        dbg.subscribe(1, s);
        emu.run_frame();
        emu.soft_reset();
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        emu.execute_single_instruction();
        check("EVT-LAND-20", "a soft reset's own Reset event survives the "
                             "reconciliation that same reset triggers",
              rec.evs.size() == 1 && rec.evs[0].reset_kind == ResetKind::Soft,
              "n=" + std::to_string(rec.evs.size()));
    }

    // ── EVT-SUB-33..36 — erase_client() recomputes the GATES, not just the list ─
    //
    // `EVT-SUB-30..32` only ever exercised `find()` and the return count, so a
    // mutation dropping `erase_client()`'s `refresh()` survived all 850 rows —
    // `find()` reads `removed` directly, which the erase sets. This is the client
    // lifecycle B3 builds `detach()` on, so it needs a row that observes something
    // `refresh()` recomputes.
    {
        Emulator emu;
        build_armed(emu, { 0x00, 0x18, 0xFD });
        Debugger dbg(emu);
        Subscription m;
        m.kind = EventKind::Mem; m.access = Access::Write;
        m.filter.lo = 0x9000; m.filter.hi = 0x9000;     // slot 4
        dbg.subscribe(7, m);
        Subscription c;
        c.kind = EventKind::Copper;
        c.filter.copper_kind = jnext::dbg::CopperEventKind::Move;
        dbg.subscribe(7, c);
        check("EVT-SUB-33", "the client's subscriptions armed the slot mask and the "
                            "Copper engine",
              emu.debug_state().wr_watch_armed(0x9000) &&
              emu.copper().move_events_armed());

        jnext::dbg::EventTable* t = emu.debug_state().event_table();
        check("EVT-SUB-34", "erase_client() removes both", t->erase_client(7) == 2);
        check("EVT-SUB-35", "and it RECOMPUTES the live cache — the kind mask goes "
                            "with the rows, which `find()` cannot see",
              !t->has_kind(EventKind::Mem) &&
              !t->has_copper_sub_kind(jnext::dbg::CopperEventKind::Move) &&
              t->wr_slot_mask() == 0,
              "wr_mask=" + std::to_string(t->wr_slot_mask()));
        // The published gates follow once the backend re-publishes, which is what
        // B3's `detach()` will do; the table's own state is what `refresh()` owns
        // and what this row pins.
        check("EVT-SUB-36", "a second erase_client() for the same client changes "
                            "nothing", t->erase_client(7) == 0 &&
              t->wr_slot_mask() == 0);
    }

    {
        // ── EVT-LAND-30..33 — THE TORN RESTORE, the one the sweep found ──────
        //
        // `Emulator::load_state()` has ~30 sentinel early-returns and the FIRST
        // statement of the restore is `clock_.load_state(r)`, so every one of
        // them fires on a machine that has ALREADY been partly overwritten. The
        // reconciliation was the last statement before `return true` — the
        // successful path only. A torn restore therefore kept the event-stop
        // latch and `hits_` from the machine it half-replaced.
        //
        // Masked, but REACHABLE: `state()` puts `Corrupt` ahead of the event-stop
        // latch, so nothing shows while the incident is unacknowledged —
        // `acknowledge_corruption()` drops the mask and the stale `Watch`
        // surfaces. That is the blocking defect of this revision, one
        // acknowledgement further away. Fixed with a scope guard, so it does not
        // depend on 30 hand-placed calls.
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        const uint8_t prog[] = { 0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xFE };
        for (size_t i = 0; i < sizeof(prog); ++i)
            emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
        {
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
        }
        emu.debug_state().set_clients_attached(true);
        emu.debug_state().set_live_raster(true);
        Debugger dbg(emu);

        // The CTL-11-03 recipe: right length, wrong content past the half-way
        // mark, so a subsystem sentinel fails part-way through instead of the
        // buffer being rejected up front.
        const auto good = dbg.save_state_bytes(
            1, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        std::vector<uint8_t> bad = good.value;
        for (size_t i = bad.size() / 2; i < bad.size(); ++i) bad[i] ^= 0xFF;

        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x9000; s.filter.hi = 0x9000;
        s.action = Action::Stop;
        const auto sub = dbg.subscribe(9, s);
        {
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; emu.cpu().set_registers(r);
        }
        emu.run_frame();
        check("EVT-LAND-30", "the subscription stopped the machine and the backend "
                             "holds its evidence",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Watch &&
              dbg.state().pause_reason.id == sub.value &&
              emu.debug_state().event_table()->hits().size() == 1);

        check("EVT-LAND-31", "a TORN state load is refused as corrupt",
              dbg.load_state_bytes(1, bad.data(), bad.size()) ==
                  Result::RefusedCorrupt);
        check("EVT-LAND-32", "and it reconciled anyway — the machine it half-replaced "
                             "is gone, so its `matched[]` goes with it",
              emu.debug_state().event_table()->hits().empty(),
              "hits=" + std::to_string(
                  emu.debug_state().event_table()->hits().size()));
        const auto inc = dbg.resume_blocked_by_corruption();
        const bool ackd = inc.has_value() &&
                          dbg.acknowledge_corruption(inc->generation) == Result::Ok;
        check("EVT-LAND-33", "so ACKNOWLEDGING the corruption — which drops the "
                             "`Corrupt` precedence that was masking it — does not "
                             "expose a Watch on the machine that is gone",
              ackd &&
              dbg.state().pause_reason.kind != PauseReason::Kind::Watch &&
              dbg.state().pause_reason.id != sub.value,
              std::string("ackd=") + (ackd ? "1" : "0") + " kind=" +
                  std::to_string(static_cast<int>(dbg.state().pause_reason.kind)) +
                  " id=" + std::to_string(dbg.state().pause_reason.id));
    }

    // =======================================================================
    // GH #276 B3 — §4.8 CAP-SES, the session
    //
    // WHAT THESE ROWS ARE FOR. B2's review found that three of its nine
    // blocking items were two sides of ONE mechanism disagreeing — each side
    // defensible alone, and each side's own row green while the pair was broken.
    // B3 is made almost entirely of pairs (attach/detach, the drain and its
    // budget, the stored and effective stop policy, the ctor's publications and
    // the dtor's retirements, per-client `live_raster` and the OR of all
    // clients), so wherever a pair exists the row below asserts the INVARIANT
    // ACROSS IT rather than each half.
    //
    // Where a row's subject is a conditional, BOTH ARMS get a row: a `Stop`
    // under each stop policy, a frame tag moving forward and backward, a drain
    // with a budget and with none, `attached()` from a client and from
    // `DebugState::active()`.
    // =======================================================================

    // ── SES-01 — the client table ──────────────────────────────────────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);

        const auto a = dbg.attach(client("A", jnext::dbg::ClientKind::Test));
        const auto b = dbg.attach(client("B", jnext::dbg::ClientKind::Dzrp));
        check("SES-01-01", "attach() hands out distinct, non-CLIENT_NONE ids",
              a.status == Result::Ok && b.status == Result::Ok &&
              a.value != jnext::dbg::CLIENT_NONE && a.value != b.value,
              "a=" + std::to_string(a.value) + " b=" + std::to_string(b.value));

        // THE INVARIANT ACROSS attach/detach, driven as a SEQUENCE rather than
        // as one attach and one detach: the three derived answers must agree
        // with the live client count at every step. `attach` incrementing a
        // counter and `detach` decrementing one is the pair that drifts, and a
        // row per side cannot see it.
        bool seq_ok = true;
        std::string seq_detail;
        auto agree = [&](int live_expected, const char* where) {
            const bool any     = live_expected > 0;
            const bool ok_here = dbg.attached() == any &&
                                 emu.debug_state().clients_attached() == any &&
                                 dbg.armed() == (any || dbg.persistent_breakpoints());
            if (!ok_here) {
                seq_ok = false;
                seq_detail += std::string(" [") + where + " attached=" +
                              (dbg.attached() ? "1" : "0") + " bit=" +
                              (emu.debug_state().clients_attached() ? "1" : "0") +
                              " armed=" + (dbg.armed() ? "1" : "0") + "]";
            }
        };
        agree(2, "two attached");
        dbg.detach(a.value);
        agree(1, "one detached");
        const auto c = dbg.attach(client("C", jnext::dbg::ClientKind::Zrcp));
        agree(2, "re-attached");
        dbg.detach(b.value);
        dbg.detach(c.value);
        agree(0, "all detached");
        check("SES-01-02", "attached(), DebugState::clients_attached() and armed() "
                           "agree with the live client count at every step",
              seq_ok, seq_detail);

        check("SES-01-03", "ids are never REUSED after a detach",
              c.value != a.value && c.value != b.value);
        check("SES-01-04", "detach() of an unknown id is benign, not Unsupported",
              dbg.detach(9999) == Result::RefusedUnavailable);
        check("SES-01-05", "a SECOND detach of the same id is refused too",
              dbg.detach(a.value) == Result::RefusedUnavailable);
        check("SES-01-06", "detach(CLIENT_NONE) is refused — the backend is not a client",
              dbg.detach(jnext::dbg::CLIENT_NONE) == Result::RefusedUnavailable);
        check("SES-01-07", "set_listener() on an unknown id is refused",
              dbg.set_listener(9999, nullptr) == Result::RefusedUnavailable);
        check("SES-01-08", "set_live_raster() on an unknown id is refused",
              dbg.set_live_raster(9999, true) == Result::RefusedUnavailable);
    }
    {
        // detach takes THIS client's subscriptions and only this client's — and
        // closes the hot-path gate with them. `erase_client()` alone only
        // tombstones the rows; without the `gates_changed()` beside it the
        // retired subscription's slot-mask bit stays set for ever.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;

        Subscription ma;
        ma.kind = EventKind::Mem; ma.access = Access::Write;
        ma.filter.lo = 0x4000; ma.filter.hi = 0x40FF;
        Subscription mb = ma;
        mb.filter.lo = 0xC000; mb.filter.hi = 0xC0FF;
        const auto sa = dbg.subscribe(a, ma);
        const auto sb = dbg.subscribe(b, mb);
        // THE BYTE THE HOT PATH READS, not `EventTable`'s own copy of it.
        // `erase_client()` calls `refresh()`, which updates the table's internal
        // mask on its own; only `gates_changed()` PUBLISHES it into
        // `BreakpointSet`, which is what the eight `Mmu` sites consult. Reading
        // the table's side let a detach that never published survive a mutation.
        const uint8_t mask_both =
            emu.debug_state().breakpoints().watch_slot_mask_wr();

        dbg.detach(a);
        check("SES-01-09", "detach removes that client's subscriptions",
              !dbg.subscriptions(true).empty() &&
              dbg.subscriptions(true).size() == 1 &&
              dbg.subscriptions(true)[0].id == sb.value,
              "n=" + std::to_string(dbg.subscriptions(true).size()));
        const uint8_t mask_one =
            emu.debug_state().breakpoints().watch_slot_mask_wr();
        check("SES-01-10", "and CLOSES its hot-path gate — the PUBLISHED slot mask "
                           "drops",
              mask_both != mask_one &&
              (mask_one & (1u << (0x4000 >> 13))) == 0 &&
              (mask_one & (1u << (0xC000 >> 13))) != 0,
              "both=" + hex(mask_both) + " one=" + hex(mask_one));
        check("SES-01-11", "and leaves the OTHER client's subscription live",
              dbg.subscriptions(true)[0].live && sa.value != sb.value);
    }
    {
        // SES-01's one rule, and its THREE arms. A pause this client owns is
        // released; a pause ANOTHER client owns survives; an UNOWNED pause
        // (`CLIENT_NONE`) survives every detach there will ever be.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        dbg.pause(a);
        check("SES-01-12", "detach of the client that paused resumes the machine",
              dbg.state().paused && dbg.detach(a) == Result::Ok &&
              !dbg.state().paused);

        const ClientId a2 = dbg.attach(client("A2")).value;
        dbg.pause(b);
        check("SES-01-13", "detach of ANOTHER client leaves that pause standing",
              dbg.state().paused && dbg.detach(a2) == Result::Ok &&
              dbg.state().paused);
        dbg.detach(b);
    }
    {
        // SES-01's rule names TWO ways a pause can be a client's: "its `pause()`,
        // OR A STOP ON ONE OF ITS SUBSCRIPTIONS". The second is the one where the
        // detach ALSO erases the thing that stopped the machine. The reason is
        // reported from the event-stop latch, a copy held on `Impl` that the
        // erase does not touch — so the order inside `detach()` is NOT what this
        // pins (an earlier text claimed it was; mutation M28 showed otherwise).
        // What it pins is the behaviour: the stop is the owner's, and the
        // owner's departure releases it.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        Subscription s;
        s.kind      = EventKind::Execute;
        s.filter.lo = AFTER_CALL; s.filter.hi = AFTER_CALL;
        s.action    = Action::Stop;
        dbg.subscribe(a, s);
        run_until_paused(emu, 3);
        const RunState st = dbg.state();
        check("SES-01-16", "a SUBSCRIPTION's Stop is that client's pause too",
              st.paused && st.pause_reason.kind == PauseReason::Kind::Breakpoint &&
              st.pause_reason.by == a,
              "kind=" + std::to_string(static_cast<int>(st.pause_reason.kind)) +
                  " by=" + std::to_string(st.pause_reason.by));
        check("SES-01-17", "so its detach releases it, although the same detach "
                           "erases the subscription that stopped it",
              dbg.detach(a) == Result::Ok && !dbg.state().paused);
    }
    {
        // RE-ENTRANCY. A listener may `detach()` from inside its own callback —
        // a protocol server whose socket died does exactly that — and the
        // fan-out is walking the client vector when it happens. That is why a
        // detach is a TOMBSTONE and the erase is deferred to the end of the
        // outer fan-out.
        //
        // Three things at once, because they are one mechanism: the departing
        // client must not be served again in the SAME fan-out, the clients AFTER
        // it must still be served, and the list must be consistent afterwards.
        Emulator emu; build(emu);
        Debugger dbg(emu);

        struct SelfDetacher : jnext::dbg::Listener {
            Debugger* dbg = nullptr;
            ClientId  me  = jnext::dbg::CLIENT_NONE;
            int       pauses = 0;
            Result    first_detach  = Result::Unsupported;
            Result    second_detach = Result::Unsupported;
            void on_paused(const jnext::dbg::PausedInfo&) override {
                ++pauses;
                first_detach  = dbg->detach(me);
                // A server that detaches twice on a dropped socket, from INSIDE
                // the callback: the row is still in the vector (a tombstone, not
                // yet compacted), and the second detach must not find it.
                second_detach = dbg->detach(me);
            }
            void on_resumed(ClientId) override {}
            void on_reset(ResetKind) override {}
            void on_frame_ended(uint32_t) override {}
            void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
            void on_exit_requested(int) override {}
            void on_log(jnext::dbg::LogLevel, const std::string&) override {}
        };

        SelfDetacher first;
        RecListener  after;
        const ClientId a = dbg.attach(client("Detacher")).value;
        const ClientId b = dbg.attach(client("After")).value;
        first.dbg = &dbg;
        first.me  = a;
        dbg.set_listener(a, &first);
        dbg.set_listener(b, &after);
        dbg.pump(jnext::dbg::PumpBudget{});            // prime
        dbg.pause(b);
        dbg.pump(jnext::dbg::PumpBudget{});            // the push that detaches

        check("SES-01-18", "a listener may detach itself from inside its callback, "
                           "and the client AFTER it in the fan-out is still served",
              first.pauses == 1 && after.paused.size() == 1,
              "first=" + std::to_string(first.pauses) + " " + after.trail());
        check("SES-01-19", "the self-detach took effect — that client is gone",
              dbg.set_listener(a, nullptr) == Result::RefusedUnavailable &&
              dbg.attached());
        check("SES-01-21", "and a SECOND detach from inside the same callback is "
                           "refused — a tombstone the fan-out has not compacted yet "
                           "is not a client",
              first.first_detach == Result::Ok &&
              first.second_detach == Result::RefusedUnavailable,
              std::string("first=") + jnext::dbg::result_name(first.first_detach) +
                  " second=" + jnext::dbg::result_name(first.second_detach));

        // A SECOND push must not reach the departed listener: the tombstone was
        // compacted at the end of the outer fan-out, and even before that the
        // loop skips a detached row.
        dbg.run(b);
        dbg.pause(b);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-01-20", "and a LATER push does not reach it either",
              first.pauses == 1 && after.paused.size() == 2,
              "first=" + std::to_string(first.pauses) + " " + after.trail());
        dbg.detach(b);
    }
    {
        // A client detached by ANOTHER client's listener, in the middle of the
        // fan-out that is walking it. The victim sits AFTER the detacher in the
        // vector, so the loop reaches its row in this very fan-out — as a
        // tombstone, not yet compacted. The tombstone is the one guard that keeps
        // it from being served (`detach()` no longer also nulls the listener, a
        // second guard for the same fact that made either removable unnoticed).
        Emulator emu; build(emu);
        Debugger dbg(emu);
        struct Kicker : jnext::dbg::Listener {
            Debugger* dbg    = nullptr;
            ClientId  victim = jnext::dbg::CLIENT_NONE;
            void on_paused(const jnext::dbg::PausedInfo&) override { dbg->detach(victim); }
            void on_resumed(ClientId) override {}
            void on_reset(ResetKind) override {}
            void on_frame_ended(uint32_t) override {}
            void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
            void on_exit_requested(int) override {}
            void on_log(jnext::dbg::LogLevel, const std::string&) override {}
        };
        Kicker      kicker;
        RecListener victim_l, last_l;
        const ClientId k = dbg.attach(client("Kicker")).value;
        const ClientId v = dbg.attach(client("Victim")).value;
        const ClientId z = dbg.attach(client("Last")).value;
        kicker.dbg    = &dbg;
        kicker.victim = v;
        dbg.set_listener(k, &kicker);
        dbg.set_listener(v, &victim_l);
        dbg.set_listener(z, &last_l);
        dbg.pump(jnext::dbg::PumpBudget{});            // prime
        dbg.pause(z);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-01-23", "a client detached by ANOTHER listener mid-fan-out is not "
                           "served by that fan-out, and the client after it still is",
              k < v && v < z && victim_l.paused.empty() && last_l.paused.size() == 1,
              "victim: " + victim_l.trail() + " last: " + last_l.trail());
        dbg.detach(k);
        dbg.detach(z);
    }
    {
        // WHO RESUMED IT. A detach that releases the departing client's own pause
        // resumes the machine through `run()`, attributed to the DEPARTING
        // client: it is that client's pause being released, and `CLIENT_NONE`
        // would tell every other client the backend resumed the machine on its
        // own initiative. The departing client has no listener any more, so it is
        // the OTHER client that sees the `Resumed{by}`.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener lb;
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        dbg.set_listener(b, &lb);
        dbg.pump(jnext::dbg::PumpBudget{});            // prime, running
        dbg.pause(a);
        dbg.pump(jnext::dbg::PumpBudget{});
        dbg.detach(a);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-01-22", "a detach that releases its own pause is pushed as "
                           "Resumed{by: the departing client}",
              lb.paused.size() == 1 && lb.resumed.size() == 1 && lb.resumed[0] == a,
              lb.trail() + (lb.resumed.empty() ? std::string()
                                              : " by=" + std::to_string(lb.resumed[0])));
        dbg.detach(b);
    }
    {
        // The UNOWNED arm, on the magic-stop LATCH (`note_magic_stop()` +
        // `pause()`, what the opcode hook calls) rather than a hand-set
        // `PauseReason`: `PauseReason::Magic` carries `by == CLIENT_NONE`
        // BECAUSE nobody's verb caused it, and that is what must make it
        // immune. NOT driven through the machine, whatever this comment used to
        // say — MAGIC-DETACH-01 (GH #276 B5) is the real ED FF, executed, with
        // the client arriving only after the stop.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_magic_breakpoint(true);
        emu.debug_state().note_magic_stop(0x1234);
        emu.debug_state().pause();
        const RunState st = dbg.state();
        check("SES-01-14", "an unowned Magic stop reads as CLIENT_NONE",
              st.paused && st.pause_reason.kind == PauseReason::Kind::Magic &&
              st.pause_reason.by == jnext::dbg::CLIENT_NONE);
        check("SES-01-15", "and NO client's detach resumes it",
              dbg.detach(a) == Result::Ok && dbg.state().paused);
    }

    // ── SES-02 — the listener fan-out ──────────────────────────────────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener la, lb;
        // ORDER IS THE ROW: the listener-less client sits BETWEEN the two that
        // have one, so a push reaching `b` proves the fan-out loop CONTINUED past
        // a null listener rather than stopping or crashing on it. With `c` last,
        // a loop that broke on null would still have served both.
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId c = dbg.attach(client("C")).value;   // no listener at all
        const ClientId b = dbg.attach(client("B")).value;
        dbg.set_listener(a, &la);
        dbg.set_listener(b, &lb);

        // The FIRST pump primes the edge detector and pushes nothing: a
        // `Debugger` built over an already-paused machine has not "just
        // paused", and a first tick claiming so would make every frontend
        // report a stop that never happened.
        dbg.pause(a);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-01", "the FIRST pump primes the baseline and pushes nothing",
              la.paused.empty() && la.resumed.empty(), la.trail());

        dbg.run(a);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-02", "a resume is pushed once, attributed to its client",
              la.resumed.size() == 1 && la.resumed[0] == a && la.paused.empty(),
              la.trail());

        dbg.pause(b);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-03", "a pause is pushed once, attributed to its client",
              la.paused.size() == 1 && la.paused[0].by == b &&
              la.paused[0].reason.kind == PauseReason::Kind::User,
              la.trail());
        check("SES-02-04", "EVERY client with a listener gets it — one loop, not "
                           "the caller's",
              lb.paused.size() == 1 && lb.paused[0].by == b &&
              lb.resumed.size() == 1);

        // A second pump with nothing changed must push nothing: the machine is
        // still paused, and "still paused" is not a stop.
        const size_t before = la.paused.size();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-05", "a pump with no transition pushes nothing",
              la.paused.size() == before, la.trail());

        // A client with no listener is SKIPPED, and must not stop the loop: `c`
        // was attached before `a` and `b` got their pushes above, which is what
        // proves it.
        check("SES-02-06", "a client with no listener is SKIPPED and the loop "
                           "continues past it — the client after it still gets the "
                           "push",
              c != jnext::dbg::CLIENT_NONE && a < c && c < b &&
              la.paused.size() == 1 && lb.paused.size() == 1,
              "a=" + std::to_string(a) + " c=" + std::to_string(c) +
                  " b=" + std::to_string(b) + " " + lb.trail());

        // `set_listener(cid, nullptr)` is how a client stops receiving pushes
        // WITHOUT detaching — which is why the signature takes a pointer.
        dbg.set_listener(b, nullptr);
        dbg.run(b);
        dbg.pause(b);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-07", "set_listener(nullptr) stops the pushes and leaves the "
                           "client attached",
              lb.paused.size() == 1 && la.paused.size() == 2 && dbg.attached());
    }
    {
        // Stop, resume and stop again BETWEEN two pumps. `paused` is true at
        // both ends, so only `resume_generation()` shows the machine moved —
        // which is why the edge detector reads it and not just the flag.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        dbg.pump(jnext::dbg::PumpBudget{});          // prime, running
        dbg.pause(a);
        dbg.pump(jnext::dbg::PumpBudget{});          // push #1
        dbg.run(a);
        dbg.pause(a);
        dbg.pump(jnext::dbg::PumpBudget{});          // push #2, still "paused"
        check("SES-02-08", "a stop-resume-stop between pumps is TWO pauses, not one",
              l.paused.size() == 2, l.trail());
    }
    {
        // SubscriptionsChanged, and the negative control beside it: the
        // revision counter is bumped by `EventTable::refresh()` — every
        // subscription mutator — and NOT by `set_slot_page()`, so an MMU paging
        // write does not notify. Without the second half the first would pass
        // on a counter bumped by everything.
        Emulator emu; build(emu, MachineType::ZX128K);   // set_page needs paging
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        dbg.pump(jnext::dbg::PumpBudget{});          // prime

        Subscription s;
        s.kind = EventKind::Mem; s.access = Access::Write;
        s.filter.lo = 0x4000; s.filter.hi = 0x40FF;
        const auto id = dbg.subscribe(a, s);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-09", "subscribe() pushes SubscriptionsChanged with the kind mask",
              l.subs.size() == 1 &&
              (l.subs[0] & jnext::dbg::kind_bit(EventKind::Mem)) != 0,
              "n=" + std::to_string(l.subs.size()));

        // A paging write recomputes the slot masks (`EventTable::set_slot_page`)
        // and must NOT notify — a subscription did not change, and notifying
        // every listener on an MMU write would push thousands of times a frame.
        emu.mmu().set_page(6, 0x02);
        emu.mmu().set_page(6, 0x10);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-10", "an MMU paging write does NOT push SubscriptionsChanged",
              l.subs.size() == 1, "n=" + std::to_string(l.subs.size()));

        dbg.unsubscribe(a, id.value);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-11", "unsubscribe() pushes it too, with the kind gone",
              l.subs.size() == 2 &&
              (l.subs[1] & jnext::dbg::kind_bit(EventKind::Mem)) == 0);
    }
    {
        // FrameEnded, and BOTH ARMS of its one conditional. Forward motion
        // notifies; a BACKWARD move (which `step_back` / `rewind_to_frame`
        // make) re-baselines silently — a loop from the old tag to a smaller
        // new one would run about four billion times, and "frame N ended" is
        // not what going back to frame N means.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        dbg.pump(jnext::dbg::PumpBudget{});          // prime at frame 0
        emu.run_frame();
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-12", "a pump after a frame batch pushes ONE FrameEnded, "
                           "carrying the most recent completed frame",
              l.frames.size() == 1 && l.frames[0] == dbg.time().frame,
              "n=" + std::to_string(l.frames.size()) +
                  " last=" + (l.frames.empty() ? std::string("-")
                                               : std::to_string(l.frames.back())));
        const size_t before = l.frames.size();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-13", "and a pump with no new frame pushes none",
              l.frames.size() == before);
    }
    {
        // THE OTHER ARM of that one conditional: the frame tag moving BACKWARD.
        // `rewind_to_frame()` does it, and "frame N ended" is not what going back
        // to frame N means — nor could the forward form survive it, since a loop
        // from the old tag down to a smaller new one would run about four billion
        // times. The re-baseline is silent, and the row after proves it is a
        // re-baseline rather than a permanent mute.
        Emulator emu; build(emu);
        emu.set_rewind_enabled(true);
        emu.resize_rewind_buffer(8);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        for (int i = 0; i < 4; ++i) emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});            // prime + one FrameEnded
        const size_t after_forward = l.frames.size();
        const uint32_t high        = dbg.time().frame;

        const Result rr = dbg.rewind_to_frame(a, high > 2 ? high - 2 : 0);
        const uint32_t low = dbg.time().frame;
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-14", "a rewind moves the frame tag BACKWARD and pushes no "
                           "FrameEnded",
              rr == Result::Ok && low < high &&
              l.frames.size() == after_forward,
              "rr=" + std::string(jnext::dbg::result_name(rr)) +
                  " high=" + std::to_string(high) + " low=" + std::to_string(low) +
                  " frames=" + std::to_string(l.frames.size()));

        // ...and it re-baselined rather than muted: the next forward frame is
        // pushed again.
        emu.debug_state().resume();
        emu.run_frame();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-15", "and the next FORWARD frame is pushed again — it "
                           "re-baselined, it did not mute",
              l.frames.size() == after_forward + 1,
              "frames=" + std::to_string(l.frames.size()));
        // GH #278 — and it carries the frame that ran: the one the rewind
        // restored. The ring had already counted it, so the counter does not
        // move when it ends; the detector used to watch the counter and the
        // restore counted the frame twice, which pushed the tag one past it.
        check("SES-02-25", "the frame run again after a rewind is pushed with "
                           "ITS tag, the frame the rewind restored",
              l.frames.size() == after_forward + 1 && l.frames.back() == low,
              "pushed=" + (l.frames.empty() ? std::string("-")
                                            : std::to_string(l.frames.back())) +
                  " low=" + std::to_string(low));
    }
    {
        // §4.3 makes `matched[]` part of the `Paused` contract: EVERY
        // subscription that stopped at this boundary, transient ones included.
        // It comes from `EventTable::hits()`, which is delivery state and lives
        // on the table rather than on `Impl` — B2's "a nested raise_host_event()
        // clears the outer boundary's list" defect reached review with 744 green
        // rows because nothing consumed it. B3 is the first consumer.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        Subscription s;
        s.kind      = EventKind::Execute;
        s.filter.lo = AFTER_CALL; s.filter.hi = AFTER_CALL;
        s.action    = Action::Stop;
        const auto id = dbg.subscribe(a, s);
        dbg.pump(jnext::dbg::PumpBudget{});            // prime, running
        run_until_paused(emu, 3);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-02-16", "Paused carries matched[] — the subscription that stopped "
                           "the machine, by id",
              l.paused.size() == 1 && l.paused[0].matched.size() == 1 &&
              l.paused[0].matched[0].event_id == id.value,
              l.trail() + " matched=" +
                  (l.paused.empty() ? std::string("-")
                                    : std::to_string(l.paused[0].matched.size())));
        // The rest of the push's header. Nothing has run since the push, so it
        // must equal `state()` field for field — and the stop is at AFTER_CALL
        // after real execution, so neither field can pass as an unset zero.
        const RunState st = dbg.state();
        check("SES-02-17", "and it carries WHERE and WHEN the machine stopped — pc "
                           "and cycle equal state()'s, and are real values",
              l.paused.size() == 1 && l.paused[0].pc == st.pc &&
              st.pc == AFTER_CALL && l.paused[0].cycle == st.cycle && st.cycle != 0,
              l.paused.empty() ? std::string("none")
                               : "pc=" + hex(l.paused[0].pc) + " cycle=" +
                                     std::to_string(l.paused[0].cycle) + " state.cycle=" +
                                     std::to_string(st.cycle));
    }

    // ── SES-03 — services and pump() ───────────────────────────────────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        FakeService s1, s2;

        check("SES-03-01", "pump() with no services reports the truth: nothing "
                           "remote, and whether the machine is paused",
              !dbg.pump(jnext::dbg::PumpBudget{}).remote_attached &&
              !dbg.pump(jnext::dbg::PumpBudget{}).paused);

        check("SES-03-02", "add_service() is idempotent — a second registration "
                           "does not drain the same adapter twice",
              dbg.add_service(s1) == Result::Ok &&
              dbg.add_service(s1) == Result::Ok);
        s1.reset();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-03-03", "so one pump services it exactly once",
              s1.calls.size() == 1 && s1.flushes == 1,
              "calls=" + std::to_string(s1.calls.size()));

        check("SES-03-04", "remove_service() of an unregistered adapter is Ok",
              dbg.remove_service(s2) == Result::Ok);
        check("SES-03-05", "remove_service() unregisters, and a later pump skips it",
              dbg.remove_service(s1) == Result::Ok);
        s1.reset();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-03-06", "an unregistered adapter is neither serviced nor flushed",
              s1.calls.empty() && s1.flushes == 0);
    }
    {
        // THE DRAIN AND ITS BUDGET — one pair, four arms, and the arms differ in
        // KIND rather than in degree. While RUNNING each service is asked for at
        // most one command whatever the budget says (the loop owner needs its
        // thread back for the next frame); while PAUSED the drain keeps
        // answering while the peer keeps talking, bounded by `budget_ms`; and
        // `budget_ms == 0` therefore means ONE, not "unbounded".
        Emulator emu; build(emu);
        Debugger dbg(emu);
        FakeService svc;
        svc.always_serviced = true;               // a peer that never stops talking
        dbg.add_service(svc);
        const ClientId a = dbg.attach(client("A")).value;

        jnext::dbg::PumpBudget generous;
        generous.max_wait_ms = 7;
        generous.drain_ms    = 1;
        generous.budget_ms   = 50;

        svc.reset();
        dbg.pump(generous);
        check("SES-03-07", "while RUNNING, exactly one command per service per pump "
                           "however generous the budget",
              svc.calls.size() == 1, "calls=" + std::to_string(svc.calls.size()));
        check("SES-03-08", "and the FIRST call gets max_wait_ms",
              !svc.calls.empty() && svc.calls[0] == 7,
              svc.trail());

        dbg.pause(a);
        svc.reset();
        dbg.pump(generous);
        check("SES-03-09", "while PAUSED the chain is DRAINED — more than one command "
                           "in one pump",
              svc.calls.size() > 1, "calls=" + std::to_string(svc.calls.size()));
        check("SES-03-09a", "and the drain ENDS once budget_ms is spent — a peer that "
                            "never stops talking cannot keep the loop owner's thread",
              !svc.runaway, "calls=" + std::to_string(svc.calls.size()));
        check("SES-03-10", "the first call gets max_wait_ms and the rest drain_ms",
              svc.calls.size() > 1 && svc.calls[0] == 7 && svc.calls[1] == 1,
              svc.trail());

        svc.reset();
        dbg.pump(jnext::dbg::PumpBudget{});
        check("SES-03-11", "budget_ms == 0 means ONE command, not an unbounded drain",
              svc.calls.size() == 1, "calls=" + std::to_string(svc.calls.size()));

        // The drain stops on `Idle` even with the whole budget unspent, which is
        // the other way out of the loop.
        svc.reset();
        svc.always_serviced = false;
        svc.serviced_budget = 3;                  // 3 commands, then Idle
        dbg.pump(generous);
        check("SES-03-12", "and the drain stops on Idle with budget to spare",
              svc.calls.size() == 4, "calls=" + std::to_string(svc.calls.size()));

        check("SES-03-13", "flush_notifications() runs once per pump, after the drain",
              svc.flushes == 1 && svc.flushed_after_calls == svc.calls.size(),
              "flushes=" + std::to_string(svc.flushes));

        svc.connected = true;
        check("SES-03-14", "ServiceHint::remote_attached follows peer_connected()",
              dbg.pump(jnext::dbg::PumpBudget{}).remote_attached);
        svc.connected = false;
        check("SES-03-15", "and drops again when the peer goes",
              !dbg.pump(jnext::dbg::PumpBudget{}).remote_attached);
        {
            // BOTH ARMS of `ServiceHint::paused`: SES-03-01 has the running one on
            // a pump with no services; this is a pump that drained, paused and
            // then running again.
            svc.reset();
            const bool while_paused = dbg.pump(jnext::dbg::PumpBudget{}).paused;
            dbg.run(a);
            svc.reset();
            const bool while_running = dbg.pump(jnext::dbg::PumpBudget{}).paused;
            dbg.pause(a);
            check("SES-03-18", "ServiceHint::paused reports the machine: true while "
                               "paused, false once it runs",
                  while_paused && !while_running);
        }
        dbg.remove_service(svc);
    }
    {
        // "WHILE PAUSED" IS THE LIVE STATE, both arms. A command inside the drain
        // can change it, and the drain must follow: a `pause` arriving while the
        // machine RUNS lets the chain behind it be answered in the same pump (ZRCP
        // `enter-cpu-step` and then its reads), and a `run` arriving while it is
        // PAUSED gives the loop owner its frames back at once instead of holding
        // the thread for the rest of the budget. B3's first cut read the state at
        // ENTRY, which got both of these the other way round.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        FakeService svc;
        svc.always_serviced = true;                    // the peer keeps talking
        dbg.add_service(svc);
        const ClientId a = dbg.attach(client("A")).value;
        jnext::dbg::PumpBudget generous;
        generous.max_wait_ms = 7;
        generous.drain_ms    = 1;
        generous.budget_ms   = 50;

        svc.reset();
        svc.on_call = [&]() { if (svc.calls.size() == 1) dbg.pause(a); };
        dbg.pump(generous);
        check("SES-03-20", "running at entry, a command that PAUSES the machine is "
                           "followed by the rest of the chain in the same pump",
              dbg.state().paused && svc.calls.size() > 1 && !svc.runaway,
              "calls=" + std::to_string(svc.calls.size()));

        svc.reset();
        svc.on_call = [&]() { if (svc.calls.size() == 1) dbg.run(a); };
        dbg.pump(generous);
        check("SES-03-21", "paused at entry, a command that RESUMES it ends the drain "
                           "there, although the peer is still talking",
              !dbg.state().paused && svc.calls.size() == 1,
              "calls=" + std::to_string(svc.calls.size()));
        svc.on_call = nullptr;
        dbg.remove_service(svc);
        dbg.detach(a);
    }
    {
        // §5 — "`pump` is never called from inside `run_frame`, and the backend
        // asserts `!in_delivery_`". A handler that calls it must be refused, and
        // refused in a RELEASE build too, which is why it is not a bare
        // `assert`: re-entering the drain would deliver the boundary's events
        // twice.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        FakeService svc;
        dbg.add_service(svc);
        const ClientId a = dbg.attach(client("A")).value;
        attach_and_pause(emu);

        int handler_calls = 0;
        bool refused_inside = false;
        jnext::dbg::ServiceHint inside_hint;
        // A connected peer, so the refusal's `remote_attached` has a TRUE answer
        // to get right — with no peer, "false" would pass on a hard-coded false.
        svc.connected = true;
        Subscription s;
        s.kind    = EventKind::Execute;
        s.filter.lo = AFTER_CALL; s.filter.hi = AFTER_CALL;
        s.action  = Action::Continue;
        s.handler = [&](const DbgEvent&, Debugger& d) {
            ++handler_calls;
            svc.reset();
            inside_hint = d.pump(jnext::dbg::PumpBudget{});
            // Refused: nothing serviced, nothing flushed.
            refused_inside = svc.calls.empty() && svc.flushes == 0;
            return Action::Continue;
        };
        dbg.subscribe(a, s);
        emu.debug_state().resume();
        run_until_paused(emu, 2);
        check("SES-03-16", "a handler that calls pump() is refused — nothing is "
                           "serviced and nothing flushed",
              handler_calls > 0 && refused_inside,
              "handler_calls=" + std::to_string(handler_calls));
        check("SES-03-19", "and the refusal still reports the truth about the session: "
                           "the peer IS connected, and the machine is running inside "
                           "a Continue delivery",
              handler_calls > 0 && inside_hint.remote_attached && !inside_hint.paused);
        dbg.remove_service(svc);
    }
    {
        // ORDER INSIDE pump(): the notification sync runs BEFORE the services are
        // flushed, so a `Paused` the listener queues in THIS pump goes out on the
        // same flush. §4.8 SES-03 is explicit — "a stop in this tick's frames is
        // notified in this tick's pump" — and swapping the two makes every stop
        // reply one tick late, which is invisible to a row that only counts.
        //
        // The row observes it from INSIDE the push: a listener that reads the
        // service's flush counter when `on_paused` arrives must see 0, because the
        // flush has not happened yet.
        struct OrderProbe : jnext::dbg::Listener {
            FakeService* svc         = nullptr;
            int          flushes_at_push = -1;
            void on_paused(const jnext::dbg::PausedInfo&) override {
                flushes_at_push = svc->flushes;
            }
            void on_resumed(ClientId) override {}
            void on_reset(ResetKind) override {}
            void on_frame_ended(uint32_t) override {}
            void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
            void on_exit_requested(int) override {}
            void on_log(jnext::dbg::LogLevel, const std::string&) override {}
        };

        Emulator emu; build(emu);
        Debugger dbg(emu);
        FakeService svc;
        OrderProbe  probe;
        probe.svc = &svc;
        dbg.add_service(svc);
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &probe);
        dbg.pump(jnext::dbg::PumpBudget{});            // prime, running
        svc.reset();
        dbg.pause(a);
        dbg.pump(jnext::dbg::PumpBudget{});

        check("SES-03-17", "the Paused push happens BEFORE the services are flushed, "
                           "so a stop goes out on this pump's flush",
              probe.flushes_at_push == 0 && svc.flushes == 1,
              "at_push=" + std::to_string(probe.flushes_at_push) +
                  " flushes=" + std::to_string(svc.flushes));
        dbg.remove_service(svc);
    }

    // ── SES-04 — the stop policy, stored vs effective ──────────────────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        FakeService svc;
        dbg.add_service(svc);
        dbg.set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
        svc.connected = true;
        check("SES-04-03", "stop_policy() keeps returning what the loop owner SET, "
                           "even while a remote is connected",
              dbg.stop_policy() == jnext::dbg::StopPolicy::ExitNonZero);
        dbg.remove_service(svc);
    }
    {
        // The EFFECTIVE policy, on a real `Action::Stop` through a running
        // machine — all four arms of the one conditional:
        //   Pause                       -> no exit request
        //   ExitNonZero, no peer        -> exit request, code 3
        //   ExitNonZero, peer connected -> no exit request (SES-04's override)
        //   an explicit pause()          -> no exit request, whatever the policy
        struct Arm {
            const char*             id;
            const char*             desc;
            jnext::dbg::StopPolicy  policy;
            bool                    peer;
            bool                    expect_exit;
        };
        const Arm arms[] = {
            {"SES-04-04", "StopPolicy::Pause requests no exit",
             jnext::dbg::StopPolicy::Pause, false, false},
            {"SES-04-05", "StopPolicy::ExitNonZero with no peer requests an exit",
             jnext::dbg::StopPolicy::ExitNonZero, false, true},
            {"SES-04-06", "and with a peer connected it PAUSES instead (SES-04's "
                          "override — a client blocked on run must get its reply)",
             jnext::dbg::StopPolicy::ExitNonZero, true, false},
            {"SES-04-07", "StopPolicy::Pause with a peer requests no exit either",
             jnext::dbg::StopPolicy::Pause, true, false},
        };
        for (const Arm& arm : arms) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            FakeService svc;
            svc.connected = arm.peer;
            dbg.add_service(svc);
            const ClientId a = dbg.attach(client("A")).value;
            dbg.set_listener(a, &l);
            dbg.set_stop_policy(arm.policy);
            attach_and_pause(emu);

            Subscription s;
            s.kind      = EventKind::Execute;
            s.filter.lo = AFTER_CALL; s.filter.hi = AFTER_CALL;
            s.action    = Action::Stop;
            dbg.subscribe(a, s);
            emu.debug_state().resume();
            run_until_paused(emu, 3);

            const bool stopped = emu.debug_state().paused();
            const bool asked   = !l.exits.empty();
            check(arm.id, arm.desc,
                  stopped && asked == arm.expect_exit,
                  std::string("stopped=") + (stopped ? "1" : "0") +
                      " exits=" + std::to_string(l.exits.size()));
            if (arm.expect_exit)
                check("SES-04-08", "the exit code is 3 — never 2 (a harness fault) "
                                   "and never 1 (\"jnext could not run\")",
                      !l.exits.empty() && l.exits[0] == 3,
                      l.exits.empty() ? "none" : std::to_string(l.exits[0]));
            dbg.remove_service(svc);
        }
    }
    {
        // THE OTHER ARM of `apply_stop(from_event)`, and it is the one a single
        // unparameterised function would have got wrong: an explicit
        // `Debugger::pause()` is a stop that drops the transients, but it is NOT
        // an `Action::Stop`, so `ExitNonZero` must not fire for it. A client
        // asking a headless machine to hold still is asking for a pause, not for
        // the process to exit under it.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        dbg.set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);

        Subscription t;
        t.kind      = EventKind::Execute;
        t.transient = true;
        t.filter.lo = PARK; t.filter.hi = PARK;
        t.action    = Action::Stop;
        dbg.subscribe(a, t);
        const bool had_transient = dbg.subscriptions(true).size() == 1 &&
                                   dbg.subscriptions(true)[0].transient &&
                                   dbg.subscriptions(true)[0].live;
        dbg.pause(a);
        check("SES-04-09", "an explicit pause() requests NO exit under ExitNonZero",
              dbg.state().paused && l.exits.empty(),
              "exits=" + std::to_string(l.exits.size()));
        // The half of `apply_stop()` BOTH arms share, asserted on the one
        // observable that distinguishes "dropped" from "never there": the
        // subscription existed and was live before the pause.
        check("SES-04-10", "and it still drops the transient subscriptions, which is "
                           "the half of apply_stop() both arms share",
              had_transient && dbg.subscriptions(true).empty(),
              std::string("had=") + (had_transient ? "1" : "0") + " n=" +
                  std::to_string(dbg.subscriptions(true).size()));
    }
    {
        // THERE ARE TWO PATHS TO `apply_stop(from_event=true)` and the rows above
        // only reach one. An `Execute` subscription stops from the
        // PRE-INSTRUCTION gate (`Impl::execute_gate`); a `Mem` or `Port`
        // subscription stops from the BOUNDARY DRAIN (`Impl::drain_boundary`).
        // Each passes its own literal, and a mutation that flipped the drain's
        // survived every SES-04 row because all of them were Execute.
        //
        //   8000  3E 5A        LD A,0x5A
        //   8002  32 10 50     LD (0x5010),A     <- the watched write
        //   8005  18 FE        JR $
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        dbg.set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
        load_writer(emu, 0x5A);

        Subscription s;
        s.kind      = EventKind::Mem;
        s.access    = Access::Write;
        s.filter.lo = WATCHED; s.filter.hi = WATCHED;
        s.action    = Action::Stop;
        dbg.subscribe(a, s);
        run_until_paused(emu, 3);
        check("SES-04-11", "a Stop delivered by the BOUNDARY DRAIN requests the exit "
                           "too, not only one from the pre-instruction gate",
              emu.debug_state().paused() && l.exits.size() == 1 &&
              l.exits[0] == 3,
              std::string("paused=") + (emu.debug_state().paused() ? "1" : "0") +
                  " exits=" + std::to_string(l.exits.size()));
    }

    {
        // THE THIRD PATH TO `apply_stop(from_event=true)`: `raise_host_event()`,
        // which delivers directly (a `Host` event never enters the ring) and so
        // pauses and applies the stop itself. A `Host` subscription's `Stop` is an
        // `Action::Stop` like any other, and under `ExitNonZero` it must request
        // the exit — the verb that RAISED the event is not the stop, the
        // subscription is. The two rows above reach the gate and the drain only.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);
        dbg.set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
        Subscription s;
        s.kind   = EventKind::Host;
        s.action = Action::Stop;
        std::strcpy(s.filter.host_name, "marker");
        dbg.subscribe(a, s);
        const Result rr = dbg.raise_host_event(a, "marker");
        check("SES-04-12", "a Host subscription's Stop requests the exit too — the "
                           "third delivery path, raise_host_event()",
              rr == Result::Ok && dbg.state().paused && l.exits.size() == 1 &&
              l.exits[0] == 3,
              "exits=" + std::to_string(l.exits.size()));
    }

    // ── SES-05 — live raster, and attached()'s two contributors ────────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        check("SES-05-01", "live_raster() starts false with clients attached and "
                           "none asking",
              !dbg.live_raster());
        dbg.set_live_raster(a, true);
        check("SES-05-02", "one client asking turns it on",
              dbg.live_raster());
        dbg.set_live_raster(b, false);
        check("SES-05-03", "another client declining does NOT turn it off — it is an "
                           "OR, not a last-writer-wins",
              dbg.live_raster());
        dbg.set_live_raster(b, true);
        dbg.set_live_raster(a, false);
        check("SES-05-04", "and it stays on while the other still asks",
              dbg.live_raster());
        dbg.detach(b);
        check("SES-05-05", "detaching the last client that asked drops the OR",
              !dbg.live_raster());
        dbg.detach(a);
    }
    {
        // `attached()` is the ARMING client list — since GH #278 WP4c retired
        // the Qt window's `DebugState::active()`, which was its second
        // contributor during the transition (the window is a client now). Each
        // arm contributor keeps its OWN bit, and a row pins that a client's
        // attach and detach leave another one alone; and the identity
        // `armed() == attached() || persistent()` over all four combinations,
        // because that identity is the only thing that keeps the backend's gate
        // and the hot loop's gate from disagreeing.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("SES-05-06", "attached() is false with no client",
              !dbg.attached() && !dbg.armed());

        const ClientId a = dbg.attach(client("A")).value;
        check("SES-05-07", "a CLIENT alone makes it attached and armed",
              dbg.attached() && dbg.armed());
        dbg.detach(a);

        jnext::dbg::ClientInfo obs = client("observer", jnext::dbg::ClientKind::Gui);
        obs.observer = true;
        const ClientId o = dbg.attach(obs).value;
        check("SES-05-08", "an OBSERVER client (REQ-qt-32) alone does NOT — the Qt "
                           "GUI's breakpoint owner is one, and a closed window "
                           "must leave the machine unarmed",
              !dbg.attached() && !dbg.armed());
        dbg.detach(o);

        // THE DIVERGENCE THIS DESIGN EXISTS TO PREVENT: a client attaching and
        // detaching must not clear an arm another contributor owns — here the
        // magic breakpoint's hold (GH #278 WP4c), which is what the retired
        // `active()` bit was when the magic hook set it. A shared bit would be
        // cleared by the detach; nothing in `DebugState` could tell the owners
        // apart.
        emu.debug_state().hold_for_magic_stop();
        const ClientId b = dbg.attach(client("B")).value;
        dbg.detach(b);
        check("SES-05-09", "a client's attach+detach leaves another arm contributor "
                           "(the magic hold) ALONE — two owners, two bits",
              emu.debug_state().magic_hold() && dbg.armed() && !dbg.attached());
        emu.debug_state().resume();                    // releases the hold

        bool identity = true;
        std::string idetail;
        for (int i = 0; i < 4; ++i) {
            const bool want_client = (i & 1) != 0;
            const bool want_persist = (i & 2) != 0;
            ClientId cid = jnext::dbg::CLIENT_NONE;
            if (want_client) cid = dbg.attach(client("X")).value;
            dbg.set_persistent_breakpoints(want_persist);
            const bool want = dbg.attached() || dbg.persistent_breakpoints();
            if (dbg.armed() != want) {
                identity = false;
                idetail += " [i=" + std::to_string(i) + "]";
            }
            if (want_client) dbg.detach(cid);
            dbg.set_persistent_breakpoints(false);
        }
        check("SES-05-10", "armed() == attached() || persistent_breakpoints() over "
                           "all four combinations",
              identity, idetail);
    }
    {
        // §4.1: "`attached` (≥1 client) gates the step machinery" — Step Out,
        // Step Back and Run-Back-to-Cycle, which the tree before B3 gated on
        // the Qt window's `DebugState::active()` (retired by GH #278 WP4c). A
        // machine driven ONLY by a remote client must still finish a Step Out.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("Remote", jnext::dbg::ClientKind::Dzrp)).value;
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        const bool in_sub = pc_of(emu) == SUB;
        dbg.pause(a);
        const Result r = dbg.step_out(a);
        run_until_paused(emu);
        check("SES-05-13", "a CLIENT alone drives Step Out to completion — attached "
                           "gates the step machinery",
              in_sub && r == Result::Ok &&
              dbg.state().paused && pc_of(emu) == AFTER_CALL,
              "PC=" + hex(pc_of(emu)) + std::string(" paused=") +
                  (dbg.state().paused ? "1" : "0"));
        dbg.detach(a);
    }
    {
        // The OTHER step-machinery gate: the STEP_BACK step mode, consumed at the
        // top of `run_frame()`. Same rule as SES-05-13 — `attached`, so a
        // machine a client drives with the Qt window closed steps back too.
        Emulator emu; build(emu);
        emu.set_rewind_enabled(true);
        emu.resize_rewind_buffer(8);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("Remote", jnext::dbg::ClientKind::Dzrp)).value;
        emu.run_frame();
        emu.run_frame();
        const uint64_t before = emu.clock().get();
        // Read BEFORE the frame: the ONLY thing driving the machine is the
        // client (no hold, no live raster), so it is what lets the step mode
        // through.
        const bool client_only = emu.debug_state().attached() &&
                                 !emu.debug_state().magic_hold() &&
                                 !emu.debug_state().raster_live();
        emu.debug_state().step_back(1);
        emu.run_frame();
        const uint64_t after = emu.clock().get();
        check("SES-05-17", "a CLIENT alone lets the STEP_BACK step mode run — the "
                           "clock goes backwards, not forwards",
              client_only && after < before,
              "before=" + std::to_string(before) + " after=" + std::to_string(after));
        dbg.detach(a);
    }
    {
        // §4.1 / SES-05: `live_raster` (per client, ORed) gates the
        // per-instruction `VideoTiming::advance()` walk — observed on the counter
        // it moves. BOTH directions and the OR.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        auto walked = [&]() {
            const RasterPos p0 = emu.video_timing().pos();
            emu.execute_single_instruction();
            const RasterPos p1 = emu.video_timing().pos();
            return p0.hc != p1.hc || p0.vc != p1.vc;
        };
        const bool off0 = !walked();
        dbg.set_live_raster(a, true);
        const bool on_a = walked();
        dbg.set_live_raster(b, true);
        dbg.set_live_raster(a, false);
        const bool on_b = walked();                     // the OR: b alone
        dbg.set_live_raster(b, false);
        const bool off1 = !walked();
        check("SES-05-14", "the raster walk runs iff SOME client asked for "
                           "live_raster — off, on for A, on for B alone, off again",
              off0 && on_a && on_b && off1,
              std::string("off0=") + (off0 ? "1" : "0") + " a=" + (on_a ? "1" : "0") +
                  " b=" + (on_b ? "1" : "0") + " off1=" + (off1 ? "1" : "0"));
        // GH #278 WP4c — an OBSERVER client's request walks too: the live
        // raster is a render hint, not an arm (REQ-qt-32), and `live_raster()`
        // reports it while the machine stays unarmed by that client.
        jnext::dbg::ClientInfo obs = client("observer", jnext::dbg::ClientKind::Gui);
        obs.observer = true;
        const ClientId o = dbg.attach(obs).value;
        dbg.set_live_raster(o, true);
        const bool on_observer   = walked();
        const bool verb_while_on = dbg.live_raster();
        dbg.detach(o);
        check("SES-05-18", "and an OBSERVER client's live-raster request walks too, "
                           "reported by live_raster() — a render hint, not an arm",
              on_observer && verb_while_on && !walked());
        dbg.detach(a);
        dbg.detach(b);
    }
    {
        // §4.1 / SES-05: `live_raster` gates "the render-every-frame hint" too.
        // With the frontend's hint OFF (`set_render_enabled(false)` — a Qt tick
        // that will not present), a frame is rendered only if something else
        // asks for it. A border change is the observable: the top-left pixel is
        // border.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        emu.run_frame();                                 // render once, hint on
        const uint32_t px0 = emu.get_framebuffer()[0];
        emu.set_render_enabled(false);
        dbg.set_border(a, static_cast<uint8_t>((emu.ula().get_border() + 1) & 7));
        emu.run_frame();
        const uint32_t px_off = emu.get_framebuffer()[0];
        dbg.set_live_raster(a, true);
        emu.run_frame();
        const uint32_t px_on = emu.get_framebuffer()[0];
        check("SES-05-15", "a frame the frontend would skip is rendered iff a client "
                           "asked for live_raster — stale without it, fresh with it",
              px_off == px0 && px_on != px0,
              "px0=" + hex(px0) + " off=" + hex(px_off) + " on=" + hex(px_on));
        dbg.detach(a);
    }
    {
        // `DebugState::SuspendScope` promises "disarms breakpoints". With a
        // THIRD armed_ contributor that promise is only true if the scope clears
        // that one too — and a scope that left it standing would silently let a
        // watchpoint fire during a mid-frame snapshot advance, which is the one
        // thing the scope exists to prevent.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_live_raster(a, true);
        bool armed_inside = true, attached_inside = true, raster_inside = true;
        {
            DebugState::SuspendScope suspend(emu.debug_state());
            armed_inside    = emu.debug_state().armed();
            attached_inside = emu.debug_state().attached();
            raster_inside   = emu.debug_state().raster_live();
        }
        check("SES-05-11", "SuspendScope disarms the machine whichever contributor "
                           "armed it — a client counts",
              !armed_inside && emu.debug_state().armed(),
              std::string("inside=") + (armed_inside ? "1" : "0"));
        check("SES-05-12", "and it RESTORES the client term on the way out",
              emu.debug_state().clients_attached() && dbg.attached());
        // The two gates that replaced active() in the hot path: the scope has
        // always switched off the step machinery and the raster walk by clearing
        // active_, and a client's bits must not switch them back on under it.
        check("SES-05-16", "and it switches off the step gate and the raster walk "
                           "too, and restores both — a client's live_raster counts",
              !attached_inside && !raster_inside &&
              emu.debug_state().attached() && emu.debug_state().raster_live() &&
              dbg.live_raster(),
              std::string("attached_in=") + (attached_inside ? "1" : "0") +
                  " raster_in=" + (raster_inside ? "1" : "0"));
        dbg.detach(a);
    }

    // ── SES-06 — the message sink, and its own log channel ─────────────────
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);

        check("SES-06-01", "`debugger` is a real --log-level name",
              Log::set_level("debugger", spdlog::level::info));
        bool in_list = false;
        for (const char* n : Log::SUBSYSTEMS)
            in_list = in_list || std::string(n) == "debugger";
        check("SES-06-02", "and it is in Log::SUBSYSTEMS, which log_test LOG-10/11 "
                           "diff against the man page in both directions",
              in_list);

        // TWO DESTINATIONS, ONE FUNCTION. The channel is captured off the live
        // logger, because "it is logged" is worth what reading the log proves.
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(8);
        Log::debugger()->sinks().push_back(ring);
        const size_t log_before = l.logs.size();
        dbg.log(7, jnext::dbg::LogLevel::Warn, "hello");
        const auto lines = ring->last_formatted();
        Log::debugger()->sinks().pop_back();

        check("SES-06-03", "log() reaches the spdlog channel",
              !lines.empty() && lines.back().find("hello") != std::string::npos &&
              lines.back().find("[client 7]") != std::string::npos,
              lines.empty() ? "none" : lines.back());
        check("SES-06-04", "and EVERY listener, with the SAME string the channel got",
              l.logs.size() == log_before + 1 &&
              l.logs.back().second == "hello [client 7]" &&
              l.logs.back().first == jnext::dbg::LogLevel::Warn,
              l.logs.empty() ? "none" : l.logs.back().second);

        // §4.2a's MUTATE line goes through the same function, which is what puts
        // it in front of a frontend's console as well as in the log. B1 wrote it
        // straight to spdlog, so a listener saw the attaches and none of the
        // writes.
        const size_t before = l.logs.size();
        dbg.set_register(a, RegId::DE, 0x4321);
        check("SES-06-05", "a MUTATE line reaches the listeners too",
              l.logs.size() == before + 1 &&
              l.logs.back().second.find("MUTATE reg DE") != std::string::npos &&
              l.logs.back().second.find("-> 0x4321") != std::string::npos,
              l.logs.empty() ? "none" : l.logs.back().second);
        check("SES-06-06", "and it is emitted as the BACKEND's, so it does not "
                           "collect a second \"[client N]\" suffix",
              !l.logs.empty() &&
              l.logs.back().second.find("[client") == std::string::npos,
              l.logs.empty() ? "none" : l.logs.back().second);

        // THE OTHER MUTATION-LOG HELPER. §4.2a's "MUTATE <what> <old> -> <new>"
        // format is for a SINGLE value; a block write or a machine-level change
        // has no such pair, so `log_mutate_range()` completes the line
        // differently. It is a separate function, so it is a separate row — a
        // row on `log_mutate` alone survived a mutation that took this one off
        // the listener path.
        const size_t r_before = l.logs.size();
        dbg.reset(a, ResetKind::Soft);
        check("SES-06-07", "a RANGE mutation line reaches the listeners too",
              l.logs.size() > r_before &&
              l.logs.back().second.find("MUTATE machine soft reset") !=
                  std::string::npos,
              l.logs.empty() ? "none" : l.logs.back().second);
    }

    // ── SES-07 / CTL-12 / CTL-15 — the loop driver and the reconstruct ─────
    {
        // The refusals first, and both of them are per VERB rather than per
        // registration: a loop owner may register one closure and not the other.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        check("SES-07-01", "set_loop_driver() accepts an EMPTY driver — that is how a "
                           "loop owner clears the registration",
              dbg.set_loop_driver(jnext::dbg::LoopDriver{}) == Result::Ok);
        check("SES-07-02", "with no cold_boot closure, reset(Hard) is refused",
              dbg.reset(1, ResetKind::Hard) == Result::RefusedUnavailable);
        check("SES-07-03", "with no load closure, load() is refused",
              dbg.load(1, "x.nex") == Result::RefusedUnavailable);

        jnext::dbg::LoopDriver half;
        half.load = [](const std::string&) { return true; };
        dbg.set_loop_driver(half);
        check("SES-07-04", "the two closures are INDEPENDENT — a load driver does not "
                           "make reset(Hard) available",
              dbg.reset(1, ResetKind::Hard) == Result::RefusedUnavailable &&
              dbg.load(1, "x.nex") == Result::Ok);
    }
    {
        // CTL-12's rules over a FAKE driver, so each rule can be asserted
        // without a 1-second cold boot. The REAL cold boot is the block after
        // this one, and it is the one that proves the re-application works.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        RecListener l;
        const ClientId a = dbg.attach(client("A")).value;
        dbg.set_listener(a, &l);

        int boots = 0;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() { ++boots; return true; };
        dbg.set_loop_driver(d);

        check("CTL-12-04", "reset(Hard) runs the registered driver exactly once",
              dbg.reset(a, ResetKind::Hard) == Result::Ok && boots == 1,
              "boots=" + std::to_string(boots));
        check("CTL-12-05", "and Reset{Hard} reached every listener BEFORE it returned "
                           "(rule 4)",
              l.resets.size() == 1 && l.resets[0] == ResetKind::Hard);
        check("CTL-12-06", "a RUNNING machine is not paused by it (rule 3), and no "
                           "PauseReason::Reset exists",
              !dbg.state().paused);

        dbg.pause(a);
        check("CTL-12-07", "a PAUSED caller stays paused across it (rule 3)",
              dbg.reset(a, ResetKind::Hard) == Result::Ok && boots == 2 &&
              dbg.state().paused);

        // A boot that FAILS still re-applies and still notifies: the machine was
        // destroyed and rebuilt either way (that is the first thing
        // `emulator_cold_boot()` does), and a client blocked on `run` is blocked
        // whether the boot worked or not.
        boots = 0;
        l.resets.clear();
        jnext::dbg::LoopDriver bad;
        bad.cold_boot = [&]() { ++boots; return false; };
        dbg.set_loop_driver(bad);
        check("CTL-12-08", "a driver that FAILS is reported as unavailable",
              dbg.reset(a, ResetKind::Hard) == Result::RefusedUnavailable &&
              boots == 1);
        check("CTL-12-09", "and Reset{Hard} is pushed anyway — a notification never "
                           "sent is a client hung on run()",
              l.resets.size() == 1);
    }
    {
        // Rule 6's corruption gate, and the half that matters: the driver must
        // not run. A cold boot leaves the machine RUNNING, which is a resume by
        // any other name, and CTL-11's rule is that an unacknowledged incident
        // refuses every resume.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        int boots = 0;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() { ++boots; return true; };
        dbg.set_loop_driver(d);

        std::vector<uint8_t> torn(64, 0xAB);
        dbg.load_state_bytes(1, torn.data(), torn.size());
        check("CTL-12-10", "reset(Hard) on an unacknowledged corrupt machine is "
                           "refused, and does NOT run the driver",
              dbg.reset(1, ResetKind::Hard) == Result::RefusedCorrupt && boots == 0,
              "boots=" + std::to_string(boots));
    }
    {
        // ── THE INVARIANT ACROSS THE CONSTRUCTOR, THE DESTRUCTOR AND THE
        //    RE-APPLICATION, over a REAL `emulator_frontend_cold_boot()`.
        //
        // This is the row the package exists for: a subscription that could fire
        // before the boot must fire after it. `~Emulator()` + placement-new
        // leaves a brand-new `DebugState` with `events_ == nullptr`, and a
        // `Debugger` that survived is then SILENTLY disconnected — every
        // subscription still lists as live and not one can ever fire. There is no
        // error for a frontend to notice.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);

        int hits_before = 0, hits_after = 0;
        Subscription s;
        s.kind       = EventKind::Mem;
        s.access     = Access::Write;
        s.filter.lo  = WATCHED; s.filter.hi = WATCHED;
        s.action     = Action::Continue;
        int* counter = &hits_before;
        s.handler    = [&counter](const DbgEvent&, Debugger&) {
            ++*counter;
            return Action::Continue;
        };
        dbg.subscribe(a, s);

        // Fire it once BEFORE the boot, through the machine, so "it could fire"
        // is MEASURED rather than assumed. A `poke()` could not do it: §4.2a
        // makes a debugger write fire no event on itself.
        load_writer(emu, 0x11);
        emu.run_frame();
        check("CTL-12-11", "a Mem subscription fires before the cold boot",
              hits_before > 0, "hits=" + std::to_string(hits_before));

        // A stale latch in the ring; and the Qt GUI's shape (GH #278 WP4c): an
        // OBSERVER client (REQ-qt-32) owning a PC breakpoint, next to the arming
        // client A — so the retirement of the platform-side restore, and the
        // client model that replaced it, can be asserted too.
        jnext::dbg::ClientInfo gui = client("Qt GUI", jnext::dbg::ClientKind::Gui);
        gui.observer = true;
        const ClientId g = dbg.attach(gui).value;
        {
            Subscription bp;
            bp.kind = EventKind::Execute; bp.action = Action::Stop;
            bp.filter.lo = 0xBEEF; bp.filter.hi = 0xBEEF;
            dbg.subscribe(g, bp);
        }
        const uint8_t stale_mask = emu.debug_state().breakpoints().watch_slot_mask_wr();

        int boots = 0;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            ++boots;
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        counter = &hits_after;
        check("CTL-12-12", "the real cold boot runs and reports success",
              dbg.reset(a, ResetKind::Hard) == Result::Ok && boots == 1);

        // The SAME program has to be back in RAM — the reconstruct wiped it.
        load_writer(emu, 0x22);
        emu.run_frame();
        check("CTL-12-13", "and the SAME subscription still fires afterwards — the "
                           "three publications, the page seed and the gates were "
                           "re-applied",
              hits_after > 0, "hits=" + std::to_string(hits_after));

        // THE PUBLISHED BYTE, for the reason SES-01-10 gives: only
        // `gates_changed()` writes `BreakpointSet`'s event half, and that is what
        // the eight `Mmu` sites read. `EventTable`'s own mask is maintained by
        // `refresh()` and would read correct even if nothing had published.
        check("CTL-12-14", "the event table is republished and the PUBLISHED slot "
                           "mask matches the LIVE subscription — it equals the "
                           "pre-boot byte because the same subscription is armed, "
                           "and both halves are non-zero",
              emu.debug_state().event_table() != nullptr &&
              emu.debug_state().breakpoints().watch_slot_mask_wr() == stale_mask &&
              stale_mask != 0 &&
              emu.debug_state().breakpoints().wr_watch_slot_armed(WATCHED),
              "mask=" + hex(emu.debug_state().breakpoints().watch_slot_mask_wr()) +
                  " stale=" + hex(stale_mask));

        check("CTL-12-15", "the client's arm bit is re-applied, so the machine is "
                           "armed again",
              emu.debug_state().clients_attached() && dbg.attached() && dbg.armed());

        // B3 left the PLATFORM-SIDE restore standing because it was the only
        // owner of the Qt panels' breakpoints and of the window's `active()`;
        // GH #278 WP4c retired it (B3 obligation 1), and these two rows re-pin
        // the same two facts against the client model that replaced it: the
        // GUI's breakpoint survives as the OBSERVER's subscription (rule 2) —
        // with the platform carrying nothing, its legacy set rebuilt empty —
        // and the observer is still no arm contributor on the rebuilt machine.
        bool gui_bp = false;
        for (const auto& si : dbg.subscriptions(false))
            if (si.owner == g && si.kind == EventKind::Execute &&
                si.filter.lo == 0xBEEF && si.live)
                gui_bp = true;
        check("CTL-12-16", "the Qt GUI's breakpoint — an observer client's "
                           "subscription — survived the boot by the backend's "
                           "re-application, and the platform carried nothing: the "
                           "rebuilt legacy BreakpointSet is empty",
              gui_bp && emu.debug_state().breakpoints().empty());
        dbg.detach(a);
        check("CTL-12-17", "and on the rebuilt machine the observer still arms "
                           "nothing: with the arming client gone the machine is "
                           "unarmed, the breakpoint still listed",
              !dbg.armed() && !dbg.attached() && dbg.subscriptions(false).size() == 1);
    }
    {
        // RULE 3 OVER THE *REAL* BOOT, both arms. A fake driver cannot test this:
        // it leaves the machine's `paused` flag exactly as it found it, so
        // "paused stays paused" passes without the backend doing anything. Only a
        // reconstruct, which always comes back RUNNING, can tell a re-applied
        // pause from an untouched one.
        for (int paused_before = 0; paused_before < 2; ++paused_before) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() {
                emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                            ColdBootHooks{});
                return true;
            };
            dbg.set_loop_driver(d);
            if (paused_before) dbg.pause(a);
            const Result r = dbg.reset(a, ResetKind::Hard);
            const bool   want = paused_before != 0;
            check(paused_before ? "CTL-12-23" : "CTL-12-24",
                  paused_before
                      ? "a PAUSED caller is still paused after a REAL reconstruct — "
                        "which comes back running, so the pause was re-applied"
                      : "and a RUNNING caller is still running — the reconstruct "
                        "never pauses a machine that was not",
                  r == Result::Ok && dbg.state().paused == want,
                  std::string("paused=") + (dbg.state().paused ? "1" : "0"));
        }
    }
    {
        // M70's gap: the MACHINE-REPLACED HOOK has to be re-installed, and its
        // absence is silent until the NEXT machine transition. So the row drives
        // one: a cold boot, then a `load_state_bytes()` restore. Without the
        // re-installed hook `init()`'s reconciliation cannot reach `Impl`, and the
        // backend keeps reporting the armed verb of a machine that is gone.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        dbg.reset(a, ResetKind::Hard);

        // A snapshot of the rebuilt machine, paused, then a STEP to arm a stale
        // `Kind::Step`, then the restore. A reconciled backend reports the
        // unowned `User{CLIENT_NONE}` fallback; an unreconciled one still reports
        // the Step that did not happen.
        dbg.pause(a);
        auto bytes = dbg.save_state_bytes(
            a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        dbg.step_into(a);
        check("CTL-12-25", "a step after the reconstruct reads as Step",
              dbg.state().pause_reason.kind == PauseReason::Kind::Step);
        StateReader r(bytes.value.data(), bytes.value.size());
        const bool ok_load = emu.load_state(r);
        check("CTL-12-26", "and a state load AFTER the reconstruct still reconciles "
                           "through the re-installed machine-replaced hook — the "
                           "stale Step is gone",
              ok_load &&
              dbg.state().pause_reason.kind != PauseReason::Kind::Step,
              std::string("ok=") + (ok_load ? "1" : "0") + " kind=" +
                  std::to_string(static_cast<int>(dbg.state().pause_reason.kind)));
    }
    {
        // M74's gap: the reconstruct's own `arm(Kind::None)`. `init()` fired
        // `debug_after_machine_transition_()` while the hook was still null, so
        // the re-application arms it explicitly — and it must arm `None`, not
        // `User`, for the same reason the hook does: `state()`'s precedence falls
        // THROUGH `None` to the legacy PC-breakpoint check and matches `User`
        // immediately.
        //
        // With an UNOWNED pre-boot pause, which is the case `None` is for: since
        // GH #276 B3 milestone 2 an OWNED pause is re-armed `User{owner}` on
        // purpose (CTL-12-41), so this row stops the machine on a legacy PC
        // breakpoint during a free run — nobody's verb — and then resets.
        //
        // The discriminator is a legacy PC breakpoint at the address the boot
        // lands on. GH #278 WP4c: `emulator_boot.h` no longer carries the legacy
        // set across the boot (B3 obligation 1; CTL-12-16), so it is set on the
        // REBUILT machine, before `state()` is asked — the question is still how
        // the re-applied pause reads there.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);

        emu.debug_state().breakpoints().add_pc(AFTER_CALL);
        run_until_paused(emu, 3);
        const RunState pre = dbg.state();
        const uint16_t landing = 0x0000;        // a cold boot starts at 0x0000
        dbg.reset(a, ResetKind::Hard);
        emu.debug_state().breakpoints().add_pc(landing);
        check("CTL-12-27", "an UNOWNED pause is re-applied as Kind::None, so a landing "
                           "on a legacy PC breakpoint reports Breakpoint — Kind::User "
                           "would have swallowed it",
              pre.paused && pre.pause_reason.by == jnext::dbg::CLIENT_NONE &&
              dbg.state().paused && emu.cpu().get_registers().PC == landing &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint,
              "pc=" + hex(emu.cpu().get_registers().PC) + " kind=" +
                  std::to_string(static_cast<int>(dbg.state().pause_reason.kind)) +
                  " pre_by=" + std::to_string(pre.pause_reason.by));
    }
    {
        // ...and the OTHER ARM: a pause a client OWNED keeps its owner across the
        // reconstruct, as `User{owner}` — the machine is held for that client —
        // even with a breakpoint at the landing address. Without the owner the
        // pause would read as the unowned fallback, which SES-01 never lets a
        // detach release: a remote that hard-reset a paused machine and then
        // crashed would leave it hung. So the second row is the owner's detach.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        const ClientId b = dbg.attach(client("B")).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        dbg.pause(a);
        dbg.step_into(a);                       // a stale Step, owned by A
        emu.debug_state().breakpoints().add_pc(0x0000);
        dbg.reset(b, ResetKind::Hard);          // B resets A's paused machine
        const RunState st = dbg.state();
        check("CTL-12-41", "an OWNED pause keeps its owner across the reconstruct — "
                           "User{A}, whoever asked for the reset",
              st.paused && st.pause_reason.kind == PauseReason::Kind::User &&
              st.pause_reason.by == a,
              "kind=" + std::to_string(static_cast<int>(st.pause_reason.kind)) +
                  " by=" + std::to_string(st.pause_reason.by));
        const bool other_left = dbg.detach(b) == Result::Ok && dbg.state().paused;
        check("CTL-12-42", "so ANOTHER client's detach leaves it, and the owner's "
                           "detach releases it — SES-01 across a reboot",
              other_left && dbg.detach(a) == Result::Ok && !dbg.state().paused);
    }
    {
        // M71's gap: the EIGHT-PAGE SEED. `EventTable::slot_page_` lives on
        // `Impl`, so it SURVIVES the boot carrying the pre-boot page map, while
        // `DebugState::on_slot_remapped()` early-returns during the new `init()`
        // because the table pointer is null at that moment. A page-qualified
        // filter is then wrong in BOTH directions, and the boot has to change a
        // mapping for the error to be visible — so this one moves a slot first.
        Emulator emu; build(emu, MachineType::ZX128K);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;

        emu.mmu().set_page(2, 0x30);            // page 0x30 into slot 2 (0x4000)
        Subscription s;
        s.kind         = EventKind::Mem;
        s.access       = Access::Write;
        s.filter.pages = { 0x30 };
        dbg.subscribe(a, s);
        check("CTL-12-28", "a page-qualified filter arms the slot its page is in",
              emu.debug_state().breakpoints().wr_watch_slot_armed(0x4000));

        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        dbg.reset(a, ResetKind::Hard);

        const uint16_t page_after = emu.mmu().get_effective_page(2);
        check("CTL-12-29", "the boot maps a DIFFERENT page there, so the stale cache "
                           "would be observably wrong",
              page_after != 0x30, "page=" + hex(page_after));
        check("CTL-12-30", "and the eight-page seed DISARMED the slot — the filter "
                           "follows the real page map, not the destroyed machine's",
              !emu.debug_state().breakpoints().wr_watch_slot_armed(0x4000),
              "mask=" +
                  hex(emu.debug_state().breakpoints().watch_slot_mask_wr()));

        // The other direction: map the watched page back in and the slot re-arms,
        // which is what proves CTL-12-30 is about the cache rather than about the
        // subscription having died in the boot.
        emu.mmu().set_page(4, 0x30);
        check("CTL-12-31", "mapping the watched page in again re-arms its new slot",
              emu.debug_state().breakpoints().wr_watch_slot_armed(0x8000));
    }
    {
        // M82's gap: CTL-15's re-application. A load whose driver DOES replace the
        // machine must leave every subscription able to fire — the in-place case
        // (CTL-15-04/05) cannot see it, because an in-place load disconnects
        // nothing.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        int hits = 0;
        Subscription s;
        s.kind      = EventKind::Mem;
        s.access    = Access::Write;
        s.filter.lo = WATCHED; s.filter.hi = WATCHED;
        s.action    = Action::Continue;
        s.handler   = [&](const DbgEvent&, Debugger&) { ++hits; return Action::Continue; };
        dbg.subscribe(a, s);

        jnext::dbg::LoopDriver d;
        d.load = [&](const std::string&) {
            // The Qt menu-load route: cold boot, then apply the file. Modelled
            // here by the boot alone — it is the reconstruct that breaks things.
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        check("CTL-15-07", "a load whose driver RECONSTRUCTS the machine reports Ok",
              dbg.load(a, "game.nex") == Result::Ok);
        load_writer(emu, 0x77);
        emu.run_frame();
        check("CTL-15-08", "and the subscription still fires afterwards — CTL-15 "
                           "honours the CTL-12 contract because it re-applies "
                           "unconditionally",
              hits > 0, "hits=" + std::to_string(hits));
    }
    {
        // The RING. `EventTable` lives on `Debugger::Impl`, so it SURVIVES the
        // reconstruct while everything latched into it describes a machine that
        // is gone. `Emulator::load_state()` discards it for exactly this reason;
        // a cold boot cannot, because the machine-replaced hook was null when
        // `init()` fired it.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;

        int delivered = 0;
        ResetKind delivered_kind = ResetKind::Soft;
        Subscription s;
        s.kind = EventKind::Reset;
        // `ResetKind::Any`, deliberately: the stale entry in the ring is a SOFT
        // reset and the one this boot latches is a HARD one, so a filter that
        // matched only the Hard kind could not tell "the stale entry was
        // discarded" from "the stale entry did not match". With `Any` both would
        // be delivered, and `delivered == 1` is therefore a statement about the
        // ring rather than about the filter.
        s.filter.reset_kind = ResetKind::Any;
        s.action  = Action::Continue;
        s.handler = [&](const DbgEvent& ev, Debugger&) {
            ++delivered;
            delivered_kind = ev.reset_kind;
            return Action::Continue;
        };
        dbg.subscribe(a, s);

        // Latch a Soft reset event and do NOT let it drain.
        emu.debug_latch_reset(/*hard=*/false);
        const size_t ring_before = emu.debug_state().event_table()->size();

        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        dbg.reset(a, ResetKind::Hard);

        check("CTL-12-18", "the ring held the pre-boot latch, and the reconstruct "
                           "discarded it along with the machine it described",
              ring_before == 1 &&
              emu.debug_state().event_table()->size() == 1,
              "before=" + std::to_string(ring_before) + " after=" +
                  std::to_string(emu.debug_state().event_table()->size()));

        load_writer(emu, 0x00);
        emu.run_frame();
        check("CTL-12-19", "and the ONE entry delivered is the Reset{Hard} this boot "
                           "latched, not the stale Soft one",
              delivered == 1 && delivered_kind == ResetKind::Hard,
              "delivered=" + std::to_string(delivered) + " kind=" +
                  std::to_string(static_cast<int>(delivered_kind)));
    }
    {
        // THE SINGLE-OWNER RULE, whole (GH #278 WP4c). B3 retired the BACKEND's
        // part of the platform restore — the event-mask half of `BreakpointSet`'s
        // hot-path gate — and left the Qt panels' half (a legacy watchpoint's slot
        // bit) carried; WP4c retired that too (B3 obligation 1). So a bare cold
        // boot now leaves BOTH halves closed, and only the backend's
        // re-application re-opens the gate, from the live subscription table — the
        // single owner of what the machine watches.
        //
        // Driven WITHOUT the backend's re-application first — a bare cold boot
        // with the `Debugger` alive but not told — because that is the only
        // state in which "the platform carried it" and "the backend re-published
        // it" differ. Until B3 retired the carry, a re-application that skipped
        // `gates_changed()` passed every row: the carried bytes were right by
        // coincidence (mutant M72).
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        Subscription s;
        s.kind      = EventKind::Mem;
        s.access    = Access::Write;
        s.filter.lo = WATCHED; s.filter.hi = WATCHED;       // slot 2
        s.action    = Action::Continue;
        dbg.subscribe(a, s);
        emu.debug_state().breakpoints().add_watchpoint(0xC000, WatchType::WRITE);  // slot 6
        const uint8_t ev_bit  = static_cast<uint8_t>(1u << (WATCHED >> 13));
        const uint8_t wp_bit  = static_cast<uint8_t>(1u << (0xC000 >> 13));
        const uint8_t before  = emu.debug_state().breakpoints().watch_slot_mask_wr();

        emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
        const uint8_t bare = emu.debug_state().breakpoints().watch_slot_mask_wr();
        check("CTL-12-32", "a cold boot the backend has NOT re-applied yet leaves the "
                           "whole gate CLOSED — the event half and the legacy half "
                           "(a legacy watchpoint) alike: the platform carries "
                           "nothing of the debugger's",
              before == (ev_bit | wp_bit) && bare == 0,
              "before=" + hex(before) + " bare=" + hex(bare));

        dbg.on_cold_boot_done();
        const uint8_t after = emu.debug_state().breakpoints().watch_slot_mask_wr();
        check("CTL-12-33", "and the backend's re-application is what re-opens it, "
                           "from the live subscription table alone",
              after == ev_bit, "after=" + hex(after));
    }
    {
        // THE OBSERVER'S TWO LIFETIME EDGES across a cold boot (GH #278 WP4c —
        // re-pinned from `BreakpointSet`'s observers, which travelled on the
        // platform restore's copy and are retired with it). The Qt panels now
        // follow a `SubscriptionsChanged` push to an observer client, so: an
        // observer client and its listener installed before a backend
        // `reset(Hard)` are notified of a subscription change AFTER it, and its
        // detach after the boot really ends that — the `ClientId` still names it.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        jnext::dbg::ClientInfo gui = client("Qt GUI", jnext::dbg::ClientKind::Gui);
        gui.observer = true;
        const ClientId g = dbg.attach(gui).value;
        RecListener l;
        dbg.set_listener(g, &l);
        dbg.pump(jnext::dbg::PumpBudget{});             // prime the edge detector
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                        ColdBootHooks{});
            return true;
        };
        dbg.set_loop_driver(d);
        dbg.reset(a, ResetKind::Hard);
        dbg.pump(jnext::dbg::PumpBudget{});
        const size_t n0 = l.subs.size();

        Subscription bp;
        bp.kind = EventKind::Execute; bp.action = Action::Stop;
        bp.filter.lo = 0x1234; bp.filter.hi = 0x1234;
        dbg.subscribe(g, bp);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CTL-12-34", "an observer client's listener installed before the cold "
                           "boot is notified of a subscription change after it",
              l.subs.size() == n0 + 1, l.trail());
        const bool detached = dbg.detach(g) == Result::Ok;
        bp.filter.lo = 0x2345; bp.filter.hi = 0x2345;
        dbg.subscribe(a, bp);
        dbg.pump(jnext::dbg::PumpBudget{});
        check("CTL-12-35", "and its pre-boot ClientId still names it — detaching it "
                           "after the boot really ends the notifications and takes "
                           "its subscription",
              detached && l.subs.size() == n0 + 1 &&
                  dbg.subscriptions(false).size() == 1,
              l.trail());
    }
    {
        // RULE 5 — the GUEST-initiated path. It needs NO registered driver: the
        // boot has already happened and the re-application asks nothing of
        // `LoopDriver`. `HeadlessApp`'s `JNEXT_BENCH_WATCH` fixture is the one
        // real caller in the tree, and it registers no driver at all.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        RecListener l;
        dbg.set_listener(a, &l);

        int hits = 0;
        Subscription s;
        s.kind      = EventKind::Mem;
        s.access    = Access::Write;
        s.filter.lo = WATCHED; s.filter.hi = WATCHED;
        s.action    = Action::Continue;
        s.handler   = [&](const DbgEvent&, Debugger&) { ++hits; return Action::Continue; };
        dbg.subscribe(a, s);

        // And the §4.3 `Reset` EVENT, which rule 5 owes the guest path exactly as
        // rule 4 owes the verb: a script waiting on `Reset{Hard}` must see a
        // guest-initiated hard reset, not only a client's.
        int       resets     = 0;
        ResetKind reset_kind = ResetKind::Soft;
        Subscription rs;
        rs.kind               = EventKind::Reset;
        rs.filter.reset_kind  = ResetKind::Any;
        rs.action             = Action::Continue;
        rs.handler = [&](const DbgEvent& ev, Debugger&) {
            ++resets;
            reset_kind = ev.reset_kind;
            return Action::Continue;
        };
        dbg.subscribe(a, rs);

        // The loop owner does the boot ITSELF, exactly as the three flag polls do.
        emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
        check("CTL-12-20", "on_cold_boot_done() needs no registered driver — the boot "
                           "has already happened",
              dbg.on_cold_boot_done() == Result::Ok);
        check("CTL-12-21", "and it pushes Reset{Hard} like the synchronous verb",
              l.resets.size() == 1 && l.resets[0] == ResetKind::Hard);

        load_writer(emu, 0x33);
        emu.run_frame();
        check("CTL-12-22", "and the subscription fires again afterwards",
              hits > 0, "hits=" + std::to_string(hits));
        check("CTL-12-36", "and a Reset subscription is delivered ONE Reset{Hard} for "
                           "the guest-initiated boot",
              resets == 1 && reset_kind == ResetKind::Hard,
              "resets=" + std::to_string(resets) + " kind=" +
                  std::to_string(static_cast<int>(reset_kind)));
    }
    {
        // RULE 3 ON THE GUEST PATH — `on_cold_boot_begin()` (owner decision
        // 2026-09-28, the one header change B3 makes). The loop owner calls it
        // immediately before the machine is destroyed and `on_cold_boot_done()`
        // after; the backend captures the pause in force and its owner in
        // between. Every pairing state gets a row: begin→done, done with no
        // begin, begin twice, a begin made stale by a verb, by a detach, and a
        // begin with no driver on a corrupt machine.
        auto boot = [](Emulator& emu) {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
        };
        auto reason = [](const RunState& st) {
            return std::string("paused=") + (st.paused ? "1" : "0") + " kind=" +
                   std::to_string(static_cast<int>(st.pause_reason.kind)) +
                   " by=" + std::to_string(st.pause_reason.by);
        };

        {   // begin → done: the pause survives, owner intact.
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            dbg.pause(a);
            const Result rb = dbg.on_cold_boot_begin();
            boot(emu);
            dbg.on_cold_boot_done();
            const RunState st = dbg.state();
            check("CTL-12-43", "begin -> guest boot -> done: a pause in force survives, "
                               "still A's",
                  rb == Result::Ok && st.paused &&
                  st.pause_reason.kind == PauseReason::Kind::User && st.pause_reason.by == a,
                  reason(st));
        }
        {   // done with NO begin: the rebuilt machine's own state, unowned —
            // both arms (running; paused by the loop owner after the rebuild).
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            dbg.pause(a);
            boot(emu);
            dbg.on_cold_boot_done();
            const bool came_back_running = !dbg.state().paused;
            dbg.pause(a);
            boot(emu);
            emu.debug_state().pause();          // the loop owner pauses the NEW machine
            dbg.on_cold_boot_done();
            const RunState st = dbg.state();
            check("CTL-12-44", "done with NO begin re-applies the REBUILT machine's "
                               "state, unowned: running comes back running, a pause "
                               "the loop owner made stays paused and is nobody's",
                  came_back_running && st.paused &&
                  st.pause_reason.by == jnext::dbg::CLIENT_NONE,
                  reason(st));
        }
        {   // `done` CONSUMES the capture: a later guest boot that comes with no
            // `begin` of its own must not re-apply the pause the first one
            // recorded.
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            dbg.pause(a);
            dbg.on_cold_boot_begin();
            boot(emu);
            dbg.on_cold_boot_done();            // paused, A's — as CTL-12-43
            dbg.run(a);
            boot(emu);                          // a second guest boot, no begin
            dbg.on_cold_boot_done();
            check("CTL-12-50", "done consumes the capture — a later done with no begin "
                               "of its own does not resurrect the old pause",
                  !dbg.state().paused, reason(dbg.state()));
        }
        {   // begin twice: the last one wins.
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            dbg.pause(a);
            dbg.on_cold_boot_begin();           // paused, A's
            dbg.run(a);
            dbg.on_cold_boot_begin();           // running — this one is current
            boot(emu);
            dbg.on_cold_boot_done();
            check("CTL-12-45", "a second begin replaces the first — the machine it "
                               "captures is the one about to be destroyed",
                  !dbg.state().paused, reason(dbg.state()));
        }
        {   // A begin made stale by reset(Hard) / load(): each lands its own
            // machine, so a later done with no begin of its own must not
            // re-apply the pause the old capture recorded.
            for (int via_load = 0; via_load < 2; ++via_load) {
                Emulator emu; build(emu);
                Debugger dbg(emu);
                const ClientId a = dbg.attach(client("A")).value;
                jnext::dbg::LoopDriver d;
                d.cold_boot = [&]() { boot(emu); return true; };
                d.load      = [&](const std::string&) { return true; };
                dbg.set_loop_driver(d);
                dbg.pause(a);
                dbg.on_cold_boot_begin();       // paused, A's — never followed by done
                dbg.run(a);
                if (via_load) dbg.load(a, "game.nex");
                else          dbg.reset(a, ResetKind::Hard);
                const bool running_after_verb = !dbg.state().paused;
                boot(emu);                      // a later guest boot, no begin
                dbg.on_cold_boot_done();
                check(via_load ? "CTL-12-46b" : "CTL-12-46",
                      via_load ? "and a load() discards it the same way"
                               : "a reset(Hard) discards a pending begin — a later "
                                 "done does not resurrect the pause it recorded",
                      running_after_verb && !dbg.state().paused, reason(dbg.state()));
            }
        }
        {   // A detach of the pending capture's OWNER releases the pause it
            // recorded; a detach of ANOTHER client does not.
            for (int owner_leaves = 0; owner_leaves < 2; ++owner_leaves) {
                Emulator emu; build(emu);
                Debugger dbg(emu);
                const ClientId a = dbg.attach(client("A")).value;
                const ClientId b = dbg.attach(client("B")).value;
                dbg.pause(a);
                dbg.on_cold_boot_begin();       // paused, A's
                dbg.detach(owner_leaves ? a : b);
                boot(emu);
                dbg.on_cold_boot_done();
                const RunState st = dbg.state();
                check(owner_leaves ? "CTL-12-47" : "CTL-12-47b",
                      owner_leaves
                          ? "a detach of the capture's OWNER between begin and done "
                            "releases the pause it recorded — SES-01 for a pause "
                            "waiting out a boot"
                          : "and a detach of ANOTHER client leaves it: still paused, "
                            "still A's",
                      owner_leaves ? !st.paused
                                   : (st.paused && st.pause_reason.by == a),
                      reason(st));
            }
        }
        {   // begin with NO driver, on a CORRUPT paused machine: a notification,
            // it never refuses. The unowned pause comes back unowned, and the
            // corruption does not — it belonged to the machine that was destroyed.
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            std::vector<uint8_t> torn(64, 0xAB);
            dbg.load_state_bytes(a, torn.data(), torn.size());
            emu.debug_state().pause();
            const bool corrupt_before = dbg.state().pause_reason.kind == PauseReason::Kind::Corrupt;
            const Result rb = dbg.on_cold_boot_begin();
            boot(emu);
            const Result rd = dbg.on_cold_boot_done();
            const RunState st = dbg.state();
            check("CTL-12-48", "begin needs no driver and never refuses, not even on a "
                               "corrupt machine; its unowned pause comes back unowned "
                               "and the corruption does not come back at all",
                  corrupt_before && rb == Result::Ok && rd == Result::Ok && st.paused &&
                  st.pause_reason.by == jnext::dbg::CLIENT_NONE &&
                  st.pause_reason.kind != PauseReason::Kind::Corrupt &&
                  !dbg.resume_blocked_by_corruption().has_value(),
                  reason(st));
        }
        {   // THE INVARIANT ACROSS THE THREE ROUTES: same pre-boot pause state in,
            // same post-boot pause state out — for a running machine and for one
            // paused by A.
            auto via = [&](int path, bool paused_by_a) {
                Emulator emu; build(emu);
                Debugger dbg(emu);
                const ClientId a = dbg.attach(client("A")).value;
                jnext::dbg::LoopDriver d;
                d.cold_boot = [&]() { boot(emu); return true; };
                d.load      = [&](const std::string&) { boot(emu); return true; };
                dbg.set_loop_driver(d);
                if (paused_by_a) dbg.pause(a);
                if (path == 0)      dbg.reset(a, ResetKind::Hard);
                else if (path == 1) dbg.load(a, "game.nex");
                else { dbg.on_cold_boot_begin(); boot(emu); dbg.on_cold_boot_done(); }
                const RunState st = dbg.state();
                return std::make_tuple(st.paused, static_cast<int>(st.pause_reason.kind),
                                       st.pause_reason.by == a);
            };
            bool same = true;
            std::string where;
            for (int pb = 0; pb < 2; ++pb) {
                const auto want = pb ? std::make_tuple(true, static_cast<int>(PauseReason::Kind::User), true)
                                     : std::make_tuple(false, static_cast<int>(PauseReason::Kind::None), false);
                for (int p = 0; p < 3; ++p) {
                    const auto got = via(p, pb != 0);
                    if (got != want) {
                        same = false;
                        where += " [paused=" + std::to_string(pb) + " path" + std::to_string(p) +
                                 " -> " + std::to_string(std::get<0>(got)) + "/" +
                                 std::to_string(std::get<1>(got)) + "/" +
                                 std::to_string(std::get<2>(got)) + "]";
                    }
                }
            }
            check("CTL-12-49", "the same pre-boot pause state gives the same post-boot "
                               "state on all three routes — reset(Hard), a "
                               "reconstructing load() and the guest begin/done pair",
                  same, where);
        }
    }
    {
        // ── FIX ROUND 1 — THE EDGE DETECTOR ACROSS A REBUILD ──────────────────
        //
        // `sync_notifications()` compares the machine's `resume_generation()` and
        // frame tag against what it last pushed. Both live on the MACHINE and
        // restart at 0 on a rebuilt one; the baseline lives on `Impl` and
        // survives. Every row below drives the SAME event twice, once with a
        // session that has history and once without, or across all three
        // routes, and asserts the SAME pushes — the notifications must not
        // depend on how many times the session resumed before the reboot.
        auto boot = [](Emulator& emu) {
            emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
        };
        // One rebuild by route: 0 = reset(Hard), 1 = reconstructing load(),
        // 2 = the guest begin/boot/done pair.
        auto rebuild = [&](Emulator& emu, Debugger& dbg, ClientId a, int route) {
            if (route == 0)      dbg.reset(a, ResetKind::Hard);
            else if (route == 1) dbg.load(a, "game.nex");
            else { dbg.on_cold_boot_begin(); boot(emu); dbg.on_cold_boot_done(); }
        };
        auto setup = [&](Emulator& emu, Debugger& dbg, RecListener& l) {
            const ClientId a = dbg.attach(client("A")).value;
            dbg.set_listener(a, &l);
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&emu, boot]() { boot(emu); return true; };
            d.load      = [&emu, boot](const std::string&) { boot(emu); return true; };
            dbg.set_loop_driver(d);
            dbg.pump(jnext::dbg::PumpBudget{});          // prime, running
            return a;
        };
        // History: `n` completed pause/resume cycles, each seen by a pump.
        auto history = [&](Debugger& dbg, ClientId a, int n) {
            for (int i = 0; i < n; ++i) {
                dbg.pause(a); dbg.pump(jnext::dbg::PumpBudget{});
                dbg.run(a);   dbg.pump(jnext::dbg::PumpBudget{});
            }
        };

        // (a) A machine PAUSED before the rebuild and still paused after it has
        //     not stopped again: `Reset{Hard}` is its notification (rule 4), and
        //     no Paused / Resumed may follow — with or without history.
        bool a_ok = true;
        std::string a_where;
        for (int route = 0; route < 3; ++route) {
            for (int n = 0; n < 2; ++n) {
                Emulator emu; build(emu);
                Debugger dbg(emu);
                RecListener l;
                const ClientId a = setup(emu, dbg, l);
                history(dbg, a, n);
                dbg.pause(a);
                dbg.pump(jnext::dbg::PumpBudget{});
                const size_t p0 = l.paused.size(), r0 = l.resumed.size();
                rebuild(emu, dbg, a, route);
                dbg.pump(jnext::dbg::PumpBudget{});
                const bool ok = dbg.state().paused && l.paused.size() == p0 &&
                                l.resumed.size() == r0 && l.resets.size() == 1;
                if (!ok) {
                    a_ok = false;
                    a_where += " [route" + std::to_string(route) + " n=" + std::to_string(n) +
                               " dp=" + std::to_string(l.paused.size() - p0) +
                               " dr=" + std::to_string(l.resumed.size() - r0) + "]";
                }
            }
        }
        check("SES-02-18", "a machine paused across a rebuild pushes Reset{Hard} and NO "
                           "Paused/Resumed, whatever the session's history, on all "
                           "three routes",
              a_ok, a_where);

        // (b) After the rebuild, ONE resume-stop cycle before the next pump is a
        //     stop-resume-stop and must push Paused — even when the rebuilt
        //     machine's generation lands exactly on the old baseline (history 1).
        bool b_ok = true;
        std::string b_where;
        for (int route = 0; route < 3; ++route) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = setup(emu, dbg, l);
            history(dbg, a, 1);
            dbg.pause(a);
            dbg.pump(jnext::dbg::PumpBudget{});
            const size_t p0 = l.paused.size();
            rebuild(emu, dbg, a, route);                 // paused, re-applied
            dbg.run(a);
            dbg.pause(a);
            dbg.pump(jnext::dbg::PumpBudget{});
            if (l.paused.size() != p0 + 1) {
                b_ok = false;
                b_where += " [route" + std::to_string(route) + " dp=" +
                           std::to_string(l.paused.size() - p0) + "]";
            }
        }
        check("SES-02-19", "a resume-stop cycle AFTER a rebuild is reported, even when "
                           "the rebuilt machine's generation collides with the old "
                           "baseline",
              b_ok, b_where);

        // (c) A resume-stop cycle BEFORE the rebuild, in the same drain, belongs
        //     to the machine that is about to go: it is reported (flushed before
        //     the rebuild), with or without history.
        bool c_ok = true;
        std::string c_where;
        for (int route = 0; route < 3; ++route) {
            for (int n = 0; n < 2; ++n) {
                Emulator emu; build(emu);
                Debugger dbg(emu);
                RecListener l;
                const ClientId a = setup(emu, dbg, l);
                history(dbg, a, n);
                dbg.pause(a);
                dbg.pump(jnext::dbg::PumpBudget{});
                const size_t p0 = l.paused.size();
                dbg.run(a);
                dbg.pause(a);                            // unreported stop-resume-stop
                rebuild(emu, dbg, a, route);
                dbg.pump(jnext::dbg::PumpBudget{});
                if (l.paused.size() != p0 + 1) {
                    c_ok = false;
                    c_where += " [route" + std::to_string(route) + " n=" + std::to_string(n) +
                               " dp=" + std::to_string(l.paused.size() - p0) + "]";
                }
            }
        }
        check("SES-02-20", "a stop-resume-stop that happened BEFORE the rebuild is "
                           "reported once, whatever the history — the old machine's "
                           "edges are flushed before it goes",
              c_ok, c_where);

        // (d) FrameEnded: the frame tag restarts on the rebuilt machine. Frames
        //     the rebuilt machine runs are reported as on a machine never rebuilt
        //     — whether the old machine had run MORE frames (a naive "backward
        //     move" would mute them) or fewer — and the old machine's unreported
        //     frames are flushed before it goes.
        bool d_ok = true;
        std::string d_where;
        for (int old_frames = 1; old_frames <= 6; old_frames += 5) {
            for (int pumped = 0; pumped < 2; ++pumped) {
                Emulator emu; build(emu);
                Debugger dbg(emu);
                RecListener l;
                const ClientId a = setup(emu, dbg, l);
                for (int i = 0; i < old_frames; ++i) emu.run_frame();
                const uint32_t old_tag = dbg.time().frame;
                // `pumped`: the old machine's frames were already reported. Not
                // pumped: they are pending when the rebuild comes.
                if (pumped) dbg.pump(jnext::dbg::PumpBudget{});
                const size_t fb = l.frames.size();
                rebuild(emu, dbg, a, 0);
                const bool flushed =
                    pumped ? l.frames.size() == fb
                           : (l.frames.size() == fb + 1 && l.frames.back() == old_tag);
                // The rebuilt machine runs its frames BEFORE the next pump — the
                // guest path's order in SDL and headless (boot, then the next
                // tick's frames, then the pump) — so a baseline left at the old
                // tag reads them as a backward move whenever the old machine had
                // run more.
                const size_t f0 = l.frames.size();
                emu.run_frame();
                emu.run_frame();
                emu.run_frame();
                dbg.pump(jnext::dbg::PumpBudget{});
                const bool reported = l.frames.size() == f0 + 1 &&
                                      l.frames.back() == dbg.time().frame;
                if (!(flushed && reported)) {
                    d_ok = false;
                    d_where += " [old=" + std::to_string(old_frames) + " pumped=" +
                               std::to_string(pumped) + " tag=" + std::to_string(old_tag) +
                               " new=" + std::to_string(dbg.time().frame) + " flushed=" +
                               (flushed ? "1" : "0") + " reported=" + (reported ? "1" : "0") + "]";
                }
            }
        }
        // The FIRST frame. `frame_tag()` clamps (`raw > 0 ? raw - 1 : 0`), so it
        // reads 0 both before anything has run and after frame 0 has ended — an
        // edge detector comparing TAGS cannot see frame 0 end, on a fresh session
        // or on a rebuilt machine. The raw counter can.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            (void)setup(emu, dbg, l);
            emu.run_frame();
            dbg.pump(jnext::dbg::PumpBudget{});
            check("SES-02-22", "the FIRST frame's end is pushed — FrameEnded{0} after one "
                               "frame from a primed baseline",
                  l.frames.size() == 1 && l.frames[0] == 0,
                  "n=" + std::to_string(l.frames.size()));
        }
        // `last_paused` is NOT re-based, on purpose: it is what the listeners
        // were last told. A `done` with no `begin` brings a PAUSED machine back
        // RUNNING (CTL-12-44), and the listeners must hear `Resumed` — which a
        // baseline re-based to the rebuilt machine's state would swallow.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = setup(emu, dbg, l);
            dbg.pause(a);
            dbg.pump(jnext::dbg::PumpBudget{});
            const size_t r0 = l.resumed.size();
            boot(emu);                                   // no begin
            dbg.on_cold_boot_done();
            dbg.pump(jnext::dbg::PumpBudget{});
            check("SES-02-23", "a paused machine that a begin-less guest boot brings back "
                               "RUNNING pushes Resumed",
                  !dbg.state().paused && l.resumed.size() == r0 + 1, l.trail());
        }
        // An IN-PLACE landing re-bases too — onto the SAME machine's counters,
        // which continue. The frames already reported stay reported: a load that
        // replaced nothing and ran no frame pushes no FrameEnded (and no
        // Paused), in the raw counter the detector compares, not the tag.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = setup(emu, dbg, l);
            jnext::dbg::LoopDriver d;
            d.load = [](const std::string&) { return true; };      // in place
            dbg.set_loop_driver(d);
            for (int i = 0; i < 4; ++i) emu.run_frame();
            dbg.pause(a);
            dbg.pump(jnext::dbg::PumpBudget{});
            const size_t f0 = l.frames.size(), p0 = l.paused.size();
            dbg.load(a, "game.nex");
            dbg.pump(jnext::dbg::PumpBudget{});
            check("SES-02-24", "an in-place load that ran no frame pushes no FrameEnded "
                               "and no Paused",
                  f0 >= 1 && l.frames.size() == f0 && l.paused.size() == p0, l.trail());
        }
        check("SES-02-21", "FrameEnded across a rebuild: the old machine's frames are "
                           "flushed before it goes, and the new machine's are reported "
                           "whether the old one ran more frames or fewer",
              d_ok, d_where);

        // (e) The CTL-11 guard keys an acknowledgement to the machine's
        //     `state_error_generation()`, which restarts on a rebuilt machine. A
        //     FRESH corruption of the rebuilt machine must block a resume even
        //     when its generation equals the one acknowledged before the reboot.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = setup(emu, dbg, l);
            std::vector<uint8_t> torn(64, 0xAB);
            dbg.load_state_bytes(a, torn.data(), torn.size());
            const auto inc1 = dbg.resume_blocked_by_corruption();
            const bool acked = inc1.has_value() &&
                               dbg.acknowledge_corruption(inc1->generation) == Result::Ok;
            rebuild(emu, dbg, a, 0);
            dbg.pause(a);
            dbg.load_state_bytes(a, torn.data(), torn.size());   // a NEW incident
            const auto inc2 = dbg.resume_blocked_by_corruption();
            check("CTL-12-51", "a fresh corruption of a REBUILT machine blocks a resume, "
                               "even with the generation acknowledged before the reboot",
                  acked && inc2.has_value() && dbg.run(a) == Result::RefusedCorrupt,
                  std::string("inc1=") + (inc1 ? std::to_string(inc1->generation) : "-") +
                      " inc2=" + (inc2 ? std::to_string(inc2->generation) : "-"));
        }
        // (e2) ...and the other arm: an IN-PLACE load keeps the machine and its
        //      corruption counter, so it keeps what was acknowledged against it.
        //      A guard reset there would re-block an incident the client already
        //      acknowledged.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = setup(emu, dbg, l);
            jnext::dbg::LoopDriver d;
            d.load = [](const std::string&) { return true; };      // in place
            dbg.set_loop_driver(d);
            std::vector<uint8_t> torn(64, 0xAB);
            dbg.load_state_bytes(a, torn.data(), torn.size());
            const auto inc = dbg.resume_blocked_by_corruption();
            const bool acked = inc.has_value() &&
                               dbg.acknowledge_corruption(inc->generation) == Result::Ok;
            dbg.pause(a);
            const Result lr = dbg.load(a, "game.nex");
            check("CTL-15-15", "an IN-PLACE load keeps the acknowledgement: the same, "
                               "still-corrupt machine stays resumable",
                  acked && lr == Result::Ok &&
                  !dbg.resume_blocked_by_corruption().has_value() &&
                  dbg.run(a) == Result::Ok,
                  "load=" + std::string(jnext::dbg::result_name(lr)));
        }
        // (f) A REBUILD FROM INSIDE A DELIVERY. §5: a handler runs inside the
        //     delivery, at a boundary, and "may not issue control verbs" — and
        //     `reset(Hard)` / `load()` are the two that would DESTROY the
        //     `Emulator` whose `run_frame()` is on the stack below the handler,
        //     and clear the latch ring the drain is walking. Both must refuse and
        //     leave the machine alone; the drain then finishes normally.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = setup(emu, dbg, l);
            int boots = 0;
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() { ++boots; boot(emu); return true; };
            d.load      = [&](const std::string&) { ++boots; boot(emu); return true; };
            dbg.set_loop_driver(d);
            Result from_reset = Result::Ok, from_load = Result::Ok, from_soft = Result::Ok;
            int calls = 0;
            Subscription s;
            s.kind      = EventKind::Execute;
            s.filter.lo = AFTER_CALL; s.filter.hi = AFTER_CALL;
            s.action    = Action::Continue;
            s.handler   = [&](const DbgEvent&, Debugger& dd) {
                if (calls++ == 0) {
                    from_reset = dd.reset(a, ResetKind::Hard);
                    from_load  = dd.load(a, "game.nex");
                    from_soft  = dd.reset(a, ResetKind::Soft);
                }
                return Action::Continue;
            };
            dbg.subscribe(a, s);
            emu.run_frame();
            check("CTL-12-52", "reset(Hard) and load() from inside a delivery are refused "
                               "and destroy nothing — the Emulator running the handler "
                               "stays the one it was",
                  calls > 0 && from_reset != Result::Ok && from_load != Result::Ok &&
                  boots == 0 && l.resets.empty(),
                  "reset=" + std::string(jnext::dbg::result_name(from_reset)) +
                      " load=" + jnext::dbg::result_name(from_load) +
                      " boots=" + std::to_string(boots));
            // The SIBLING the same rule covers: a soft reset re-`init()`s the
            // machine under the same frame. Refused likewise, and the machine
            // kept executing: the program reached its `JR $` loop.
            check("CTL-12-53", "and so is reset(Soft) — the sibling that re-inits the "
                               "same machine — and the frame ran on",
                  from_soft == Result::Unsupported && from_reset == Result::Unsupported &&
                  from_load == Result::Unsupported && pc_of(emu) >= AFTER_CALL,
                  "soft=" + std::string(jnext::dbg::result_name(from_soft)) +
                      " pc=" + hex(pc_of(emu)));
        }
    }
    {
        // CTL-15. The re-application is UNCONDITIONAL because the backend cannot
        // know whether the loop owner's load closure replaced the machine —
        // `emulator_apply_load()` loads in place, the Qt menu route cold-boots
        // first, and `load_rzx` reconstructs only when the recording carries an
        // embedded snapshot. Both arms therefore have to work: a load that
        // replaced nothing, and one that replaced everything.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;

        int hits = 0;
        Subscription s;
        s.kind      = EventKind::Mem;
        s.access    = Access::Write;
        s.filter.lo = WATCHED; s.filter.hi = WATCHED;
        s.action    = Action::Continue;
        s.handler   = [&](const DbgEvent&, Debugger&) { ++hits; return Action::Continue; };
        dbg.subscribe(a, s);

        std::string seen;
        jnext::dbg::LoopDriver d;
        d.load = [&](const std::string& p) { seen = p; return true; };
        dbg.set_loop_driver(d);

        dbg.pause(a);
        check("CTL-15-02", "load() calls the driver with the path and reports its flag",
              dbg.load(a, "game.nex") == Result::Ok && seen == "game.nex");
        check("CTL-15-03", "a PAUSED caller stays paused across it",
              dbg.state().paused);
        check("CTL-15-04", "and the re-application ran for an IN-PLACE load too — "
                           "the subscription is still live and its gate still open",
              (emu.debug_state().event_table()->wr_slot_mask() &
               (1u << (0x5000 >> 13))) != 0);
        emu.debug_state().resume();
        load_writer(emu, 0x44);
        emu.run_frame();
        check("CTL-15-05", "so it still fires",
              hits > 0, "hits=" + std::to_string(hits));

        jnext::dbg::LoopDriver bad;
        bad.load = [](const std::string&) { return false; };
        dbg.set_loop_driver(bad);
        check("CTL-15-06", "a load the driver could not apply is reported as unavailable",
              dbg.load(a, "nope.nex") == Result::RefusedUnavailable);
    }
    {
        // "A PAUSED CALLER STAYS PAUSED, at the new PC" (CTL-15), over a driver
        // that RECONSTRUCTS the machine — the only kind that can tell a re-applied
        // pause from an untouched one, since a reconstruct always comes back
        // running. CTL-15-03 is the in-place arm, where the pause was never at
        // risk. Both directions, as for CTL-12-23/24.
        for (int paused_before = 0; paused_before < 2; ++paused_before) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            jnext::dbg::LoopDriver d;
            d.load = [&](const std::string&) {
                emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                            ColdBootHooks{});
                return true;
            };
            dbg.set_loop_driver(d);
            if (paused_before) dbg.pause(a);
            const Result r = dbg.load(a, "game.nex");
            check(paused_before ? "CTL-15-09" : "CTL-15-10",
                  paused_before
                      ? "a PAUSED caller is still paused after a load that "
                        "reconstructed the machine"
                      : "and a RUNNING caller is still running — a load never "
                        "pauses",
                  r == Result::Ok && dbg.state().paused == (paused_before != 0),
                  std::string("paused=") + (dbg.state().paused ? "1" : "0"));
        }
    }
    {
        // CTL-11's gate on `load()`: a load leaves the machine running or paused
        // exactly like a reset does, and an unacknowledged corruption refuses
        // every verb that makes the machine execute. The driver must not run.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        int loads = 0;
        jnext::dbg::LoopDriver d;
        d.load = [&](const std::string&) { ++loads; return true; };
        dbg.set_loop_driver(d);
        std::vector<uint8_t> torn(64, 0xAB);
        dbg.load_state_bytes(1, torn.data(), torn.size());
        check("CTL-15-11", "load() on an unacknowledged corrupt machine is refused, "
                           "and does NOT run the driver",
              dbg.load(1, "game.nex") == Result::RefusedCorrupt && loads == 0,
              "loads=" + std::to_string(loads));
    }
    {
        // CTL-15 × CTL-12 RULE 4: a load that RECONSTRUCTED the machine is a cold
        // boot, and every other client's cache is invalid after it — so it pushes
        // `Reset{Hard}` before returning and latches the §4.3 `Reset{Hard}` event,
        // exactly as `reset(Hard)` does. A load that did NOT reconstruct pushes
        // and latches nothing: the machine was not reset. BOTH ARMS, and the
        // backend tells them apart from its own publication (`events_` pointing
        // at `Impl::events` survives an in-place load and not a reconstruct).
        for (int rebuilds = 0; rebuilds < 2; ++rebuilds) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = dbg.attach(client("A")).value;
            dbg.set_listener(a, &l);
            int events = 0;
            Subscription rs;
            rs.kind              = EventKind::Reset;
            rs.filter.reset_kind = ResetKind::Any;
            rs.action            = Action::Continue;
            rs.handler = [&](const DbgEvent& ev, Debugger&) {
                if (ev.reset_kind == ResetKind::Hard) ++events;
                return Action::Continue;
            };
            dbg.subscribe(a, rs);
            jnext::dbg::LoopDriver d;
            d.load = [&](const std::string&) {
                if (rebuilds)
                    emulator_frontend_cold_boot(emu, emu.config(), std::string(),
                                                ColdBootHooks{});
                return true;
            };
            dbg.set_loop_driver(d);
            const Result r = dbg.load(a, "game.nex");
            const size_t pushed = l.resets.size();
            load_writer(emu, 0x12);
            emu.run_frame();
            check(rebuilds ? "CTL-15-13" : "CTL-15-14",
                  rebuilds ? "a load that RECONSTRUCTED the machine pushes Reset{Hard} "
                             "before returning and latches the Reset{Hard} event"
                           : "and a load that did NOT pushes and latches nothing — "
                             "the machine was not reset",
                  r == Result::Ok &&
                      (rebuilds ? (pushed == 1 && l.resets[0] == ResetKind::Hard &&
                                   events == 1)
                                : (pushed == 0 && events == 0)),
                  "pushed=" + std::to_string(pushed) + " events=" +
                      std::to_string(events));
        }
    }
    {
        // CTL-12 RULE 2 / CTL-15 — the ENABLE FLAGS a client set through a verb
        // (call-stack tracking, the trace, `persistent_breakpoints`) live on the
        // `Emulator`, which the reconstruct resets; the backend keeps its own
        // record of the request and re-applies it on EVERY route that lands a new
        // machine. One helper drives the three routes identically so their
        // results can be compared.
        struct Out {
            bool cs, tr, pb;
            bool operator==(const Out& o) const { return cs == o.cs && tr == o.tr && pb == o.pb; }
        };
        auto show = [](const Out& o) {
            return std::string("cs=") + (o.cs ? "1" : "0") + " tr=" + (o.tr ? "1" : "0") +
                   " pb=" + (o.pb ? "1" : "0");
        };
        // path 0 = reset(Hard), 1 = load() through a reconstructing driver,
        // 2 = the guest path (the loop owner boots, then on_cold_boot_done()).
        auto run_path = [&](int path, bool set_intent, bool intent_pb, bool cfg_pb,
                            bool direct_cs) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            auto boot = [&]() {
                EmulatorConfig c = emu.config();
                c.persistent_breakpoints = cfg_pb;          // the CONFIG's value
                emulator_frontend_cold_boot(emu, c, std::string(), ColdBootHooks{});
            };
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() { boot(); return true; };
            d.load      = [&](const std::string&) { boot(); return true; };
            dbg.set_loop_driver(d);
            if (set_intent) {
                dbg.set_call_stack_enabled(true);
                dbg.set_trace_enabled(true);
                dbg.set_persistent_breakpoints(intent_pb);
            }
            // A write that is NOT client intent: the Qt panels' route, straight
            // onto the Emulator (pre-Q).
            if (direct_cs) emu.call_stack().set_enabled(true);
            if (path == 0)      dbg.reset(a, ResetKind::Hard);
            else if (path == 1) dbg.load(a, "game.nex");
            else              { boot(); dbg.on_cold_boot_done(); }
            Out o{emu.call_stack().enabled(), emu.trace_log().enabled(),
                  emu.debug_state().persistent_breakpoints()};
            dbg.detach(a);
            return o;
        };

        const Out all_on{true, true, true};
        const Out via_reset = run_path(0, true, true, false, false);
        const Out via_load  = run_path(1, true, true, false, false);
        const Out via_guest = run_path(2, true, true, false, false);
        check("CTL-12-37", "reset(Hard) re-applies the call-stack, trace and "
                           "persistent-breakpoint enables a client set",
              via_reset == all_on, show(via_reset));
        check("CTL-15-12", "and so does a load() whose driver reconstructs",
              via_load == all_on, show(via_load));
        check("CTL-12-38", "and so does the GUEST path — the backend re-applies its "
                           "own record of the request, it never reads the dead machine",
              via_guest == all_on, show(via_guest));

        // THE INVARIANT ACROSS THE THREE ROUTES: same intent in, same state out —
        // including an intent that CONTRADICTS the config (persistent OFF asked
        // for, the config's ON re-latched by `init()`), which is the one value a
        // re-application that did nothing could not produce by accident.
        const Out mixed{true, true, false};
        bool same = true;
        std::string where;
        for (int p = 0; p < 3; ++p) {
            const Out o = run_path(p, true, false, true, false);
            if (!(o == mixed)) { same = false; where += " [path" + std::to_string(p) + " " + show(o) + "]"; }
        }
        check("CTL-12-39", "the same client intent gives the same machine state on "
                           "all three routes — an explicit OFF included, against a "
                           "config that says ON",
              same, where);

        // ONLY WHAT A CLIENT SET. No client touched any of the three: the config's
        // `persistent_breakpoints` survives (a re-application of a default would
        // clobber it), and a call-stack enable written straight onto the Emulator
        // — the Qt panels' route until package Q — is NOT resurrected, because it
        // was never client intent. The machine comes back with its own defaults.
        const Out defaults{false, false, true};
        bool untouched = true;
        std::string where2;
        for (int p = 0; p < 3; ++p) {
            const Out o = run_path(p, false, false, true, true);
            if (!(o == defaults)) { untouched = false; where2 += " [path" + std::to_string(p) + " " + show(o) + "]"; }
        }
        check("CTL-12-40", "with no client intent a reconstruct restores nothing: the "
                           "config's persistent flag stands and a direct (non-verb) "
                           "call-stack enable is not resurrected",
              untouched, where2);

        // BOTH VALUES OF EVERY FLAG in the no-intent arm (coverage review C3).
        // CTL-12-40's config said persistent ON, which is also what a re-apply
        // that defaulted to ON would produce, so that defaulting bug survived it;
        // and a rebuilt machine's call-stack / trace are OFF, which is what a
        // re-apply defaulting to OFF would produce. Two more arms close both:
        //   * a rebuild with the config saying persistent OFF — nothing may come
        //     back ON;
        //   * an IN-PLACE landing on a machine whose three flags were switched ON
        //     directly (the Qt route, not client intent) — nothing may come back
        //     OFF, because the backend only re-applies what a client asked for.
        const Out all_off{false, false, false};
        bool off_ok = true;
        std::string where3;
        for (int p = 0; p < 3; ++p) {
            const Out o = run_path(p, false, false, /*cfg_pb=*/false, /*direct_cs=*/false);
            if (!(o == all_off)) { off_ok = false; where3 += " [path" + std::to_string(p) + " " + show(o) + "]"; }
        }
        check("CTL-12-54", "with no client intent and the config saying persistent OFF, "
                           "a reconstruct brings back all three flags OFF on every route",
              off_ok, where3);
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            jnext::dbg::LoopDriver d;
            d.load = [](const std::string&) { return true; };      // in place
            dbg.set_loop_driver(d);
            emu.call_stack().set_enabled(true);                     // not client intent
            emu.trace_log().set_enabled(true);
            emu.debug_state().set_persistent_breakpoints(true);
            dbg.load(a, "game.nex");
            const Out o{emu.call_stack().enabled(), emu.trace_log().enabled(),
                        emu.debug_state().persistent_breakpoints()};
            check("CTL-12-55", "and with no client intent an IN-PLACE landing leaves all "
                               "three flags as the machine had them — ON stays ON",
                  o == all_on, show(o));
            dbg.detach(a);
        }
    }
    {
        // FAILED BOOTS THAT REALLY RECONSTRUCTED (coverage review C1/C2). The
        // re-application runs WHETHER OR NOT the driver reports success, because a
        // failed cold boot still ran `~Emulator()` and the placement-new — and the
        // fakes of CTL-12-08 / CTL-15-06 never reconstruct, so they could not
        // tell a re-application that ran from one that was skipped. These drivers
        // DO rebuild the machine, and THEN report failure.
        for (int via_load = 0; via_load < 2; ++via_load) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            RecListener l;
            const ClientId a = dbg.attach(client("A")).value;
            dbg.set_listener(a, &l);
            int hits = 0, reset_events = 0;
            Subscription s;
            s.kind      = EventKind::Mem;
            s.access    = Access::Write;
            s.filter.lo = WATCHED; s.filter.hi = WATCHED;
            s.action    = Action::Continue;
            s.handler   = [&](const DbgEvent&, Debugger&) { ++hits; return Action::Continue; };
            dbg.subscribe(a, s);
            Subscription rs;
            rs.kind              = EventKind::Reset;
            rs.filter.reset_kind = ResetKind::Hard;
            rs.action            = Action::Continue;
            rs.handler = [&](const DbgEvent&, Debugger&) { ++reset_events; return Action::Continue; };
            dbg.subscribe(a, rs);
            dbg.set_call_stack_enabled(true);
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&]() {
                emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
                return false;                            // rebuilt, and reports failure
            };
            d.load = [&](const std::string&) {
                emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});
                return false;
            };
            dbg.set_loop_driver(d);
            dbg.pause(a);
            const Result r = via_load ? dbg.load(a, "game.nex")
                                      : dbg.reset(a, ResetKind::Hard);
            const RunState st = dbg.state();
            const bool reapplied =
                r == Result::RefusedUnavailable && st.paused &&
                st.pause_reason.kind == PauseReason::Kind::User && st.pause_reason.by == a &&
                emu.call_stack().enabled() &&
                emu.debug_state().breakpoints().wr_watch_slot_armed(WATCHED);
            const bool pushed = l.resets.size() == 1 && l.resets[0] == ResetKind::Hard;
            dbg.run(a);
            load_writer(emu, 0x5C);
            emu.run_frame();
            check(via_load ? "CTL-15-16" : "CTL-12-56",
                  via_load ? "a load whose driver RECONSTRUCTS and then reports failure is "
                             "refused but still re-applies — owned pause, enables, gate, "
                             "and the subscription fires"
                           : "a reset(Hard) whose driver RECONSTRUCTS and then reports "
                             "failure is refused but still re-applies — owned pause, "
                             "enables, gate, and the subscription fires",
                  reapplied && hits > 0,
                  std::string("r=") + jnext::dbg::result_name(r) +
                      " paused=" + (st.paused ? "1" : "0") +
                      " by=" + std::to_string(st.pause_reason.by) +
                      " hits=" + std::to_string(hits));
            // And the machine WAS rebuilt, so every other client's cache of it is
            // invalid either way: `Reset{Hard}` is pushed and latched even though
            // the load / boot failed — the same reading rule 4 takes for a failed
            // `reset(Hard)` (CTL-12-09), now pinned on a boot that really rebuilt.
            check(via_load ? "CTL-15-17" : "CTL-12-57",
                  via_load ? "and the failed-but-reconstructing load still pushes and "
                             "latches Reset{Hard} — the machine WAS rebuilt"
                           : "and the failed-but-reconstructing reset still pushes and "
                             "latches Reset{Hard}",
                  pushed && reset_events == 1,
                  "pushed=" + std::to_string(l.resets.size()) +
                      " events=" + std::to_string(reset_events));
        }
    }

    // ── FIX ROUND 1b — §5 ACROSS THE WHOLE VERB SET ─────────────────────────
    //
    // "A handler may not drive the machine." A handler runs inside an event
    // delivery — `run_frame()` (or the pre-instruction gate inside it) on the
    // stack below it, the drain walking the ring and building `matched[]`. Every
    // public verb that would EXECUTE, CHANGE THE RUN STATE OF, REWIND, RESTORE,
    // RESET or REPLACE the machine refuses there, through the ONE helper and with
    // the ONE code (`Unsupported`). The verb-by-verb classification is the table
    // in the B3 report; this is the set it names as hazardous.
    //
    // Each REENT row runs its verb twice on fresh machines: from a handler (an
    // `Execute` subscription at AFTER_CALL, mid-frame) — REFUSED — and from
    // outside any delivery — the verb's normal answer. REENT-30 is the invariant
    // across the whole set: refused, and NOTHING about the machine changed.
    {
        struct Probe {
            uint64_t clock = 0, gen = 0;
            uint32_t frame = 0;
            uint16_t pc = 0;
            bool     paused = false;
            size_t   subs = 0, hits = 0, ring = 0;
            bool operator==(const Probe& o) const {
                return clock == o.clock && gen == o.gen && frame == o.frame && pc == o.pc &&
                       paused == o.paused && subs == o.subs && hits == o.hits && ring == o.ring;
            }
        };
        auto probe = [](Emulator& emu, Debugger& dbg) {
            Probe p;
            p.clock  = emu.clock().get();
            p.gen    = emu.debug_state().resume_generation();
            p.frame  = emu.frame_num();
            p.pc     = emu.cpu().get_registers().PC;
            p.paused = emu.debug_state().paused();
            p.subs   = dbg.subscriptions(true).size();
            p.hits   = emu.debug_state().event_table()->hits().size();
            p.ring   = emu.debug_state().event_table()->size();
            return p;
        };
        struct Ctx {
            std::vector<uint8_t> bytes;
            int boots = 0, loads = 0;
        };
        using Prep = std::function<void(Emulator&, Debugger&, ClientId, Ctx&)>;
        using Call = std::function<Result(Emulator&, Debugger&, ClientId, Ctx&)>;
        // `inside` (optional) is what the handler calls instead of `call`, for the
        // one verb whose own guard is only observable with arguments it would
        // otherwise refuse itself; `at` is where the delivery happens.
        struct Verb {
            const char* id; const char* name; Prep prep; Call call;
            Call inside = nullptr; uint16_t at = AFTER_CALL;
        };

        const Prep none = [](Emulator&, Debugger&, ClientId, Ctx&) {};
        const Prep with_rewind = [](Emulator& emu, Debugger&, ClientId, Ctx&) {
            emu.set_rewind_enabled(true);
            emu.resize_rewind_buffer(8);
            for (int i = 0; i < 3; ++i) emu.run_frame();
        };
        const Prep with_driver = [](Emulator&, Debugger& dbg, ClientId, Ctx& c) {
            jnext::dbg::LoopDriver d;
            d.cold_boot = [&c]() { ++c.boots; return true; };
            d.load      = [&c](const std::string&) { ++c.loads; return true; };
            dbg.set_loop_driver(d);
        };
        const Prep with_bytes = [](Emulator&, Debugger& dbg, ClientId a, Ctx& c) {
            c.bytes = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary).value;
        };
        const Prep select_nr02 = [](Emulator& emu, Debugger&, ClientId, Ctx&) {
            emu.port().write(0x243B, 0x02);
        };
        // GH #276 B4 — a bookmark to restore, saved at the fresh machine's
        // frame boundary.
        const Prep with_bookmark = [](Emulator&, Debugger& dbg, ClientId a, Ctx&) {
            dbg.bookmark_save(a, "b", jnext::dbg::SaveStateMode::RefuseMidFrame);
        };

        const std::vector<Verb> verbs = {
            {"REENT-01", "pause", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.pause(a); }},
            {"REENT-02", "run", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.run(a); }},
            {"REENT-03", "step_into", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.step_into(a); }},
            // AT THE CALL (PROG+2): a step over anything else is a step_into,
            // whose own guard would refuse it and hide this one.
            {"REENT-04", "step_over", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.step_over(a); },
             nullptr, static_cast<uint16_t>(PROG + 2)},
            {"REENT-05", "step_out", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.step_out(a); }},
            {"REENT-06", "run_to", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.run_to(a, PARK); }},
            {"REENT-07", "run_to_cycle", none,
             [](Emulator& e, Debugger& d, ClientId a, Ctx&) {
                 return d.run_to_cycle(a, e.clock().get() + 1000); }},
            // Inside: a frame already PAST, which `run_to_frame()` refuses by
            // itself (`RefusedUnavailable`) — a future frame delegates to
            // `run_to_cycle()`, whose guard would answer for this one. The one
            // code must come first, whatever the arguments.
            {"REENT-08", "run_to_frame", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) {
                 return d.run_to_frame(a, d.time().frame + 2); },
             [](Emulator&, Debugger& d, ClientId a, Ctx&) {
                 return d.run_to_frame(a, 0); }},
            {"REENT-09", "run_to_end_of_frame", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.run_to_end_of_frame(a); }},
            {"REENT-10", "run_to_end_of_scanline", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.run_to_end_of_scanline(a); }},
            {"REENT-11", "step_back", with_rewind,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.step_back(a, 1); }},
            {"REENT-12", "rewind_to_frame", with_rewind,
             [](Emulator& e, Debugger& d, ClientId a, Ctx&) {
                 return d.rewind_to_frame(a, e.rewind_buffer()->oldest_frame_num()); }},
            {"REENT-13", "reset(Hard)", with_driver,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.reset(a, ResetKind::Hard); }},
            {"REENT-14", "reset(Soft)", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.reset(a, ResetKind::Soft); }},
            {"REENT-15", "load", with_driver,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.load(a, "game.nex"); }},
            {"REENT-16", "load_state_bytes", with_bytes,
             [](Emulator&, Debugger& d, ClientId a, Ctx& c) {
                 return d.load_state_bytes(a, c.bytes.data(), c.bytes.size()); }},
            {"REENT-17", "save_state_bytes(AdvanceToBoundary) mid-frame", none,
             [](Emulator& e, Debugger& d, ClientId a, Ctx&) {
                 // Outside a delivery the machine is paused at a boundary, so
                 // make the frame be IN PROGRESS there too: one instruction.
                 if (!e.frame_in_progress()) e.execute_single_instruction();
                 return d.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary).status; }},
            {"REENT-18", "on_cold_boot_done", none,
             [](Emulator&, Debugger& d, ClientId, Ctx&) { return d.on_cold_boot_done(); }},
            // TWO values of the soft-reset class — bit 0 set, bit 1 clear — so a
            // guard narrowed to `value == 0x01` is visible: 0x01, and 0x11 (bit 4,
            // the iotrap-ack bit NR 0x02 also carries, set). Both must get the
            // same answer; a disagreement returns `NoFrame`, which is neither
            // arm's expected result, so the row fails either way. The other side
            // of the boundary (bit 1 set → the deferred hard reset, not refused)
            // is REENT-21 / REENT-22.
            {"REENT-19", "nextreg_write(NR 0x02 soft reset: 0x01 and 0x11)", none,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) {
                 const Result r1 = d.nextreg_write(a, 0x02, 0x11);
                 const Result r2 = d.nextreg_write(a, 0x02, 0x01);
                 return r1 == r2 ? r1 : Result::NoFrame; }},
            {"REENT-20", "port_out(0x253B, NR 0x02 soft reset: 0x01 and 0x11)", select_nr02,
             [](Emulator& e, Debugger& d, ClientId a, Ctx&) {
                 const Result r1 = d.port_out(a, 0x253B, 0x11);
                 // Outside a delivery the first write DID soft-reset the machine,
                 // which may reset the select latch: re-select so the second
                 // value targets NR 0x02 on both arms.
                 e.port().write(0x243B, 0x02);
                 const Result r2 = d.port_out(a, 0x253B, 0x01);
                 return r1 == r2 ? r1 : Result::NoFrame; }},
            // GH #276 B4 — CAP-03. A restore REPLACES the machine. Inside: a name
            // that does not exist, so the one code must come FIRST — past the
            // guard, the lookup would answer RefusedUnavailable, and a known name
            // would reach load_state_bytes()' own guard and hide this one.
            {"REENT-31", "bookmark_restore", with_bookmark,
             [](Emulator&, Debugger& d, ClientId a, Ctx&) { return d.bookmark_restore(a, "b"); },
             [](Emulator&, Debugger& d, ClientId a, Ctx&) {
                 return d.bookmark_restore(a, "no-such-bookmark"); }},
            // A save that has to ADVANCE executes the frame the handler runs in
            // (the save_state_bytes() pattern, REENT-17): refused on that arm only.
            {"REENT-32", "bookmark_save(AdvanceToBoundary) mid-frame", none,
             [](Emulator& e, Debugger& d, ClientId a, Ctx&) {
                 if (!e.frame_in_progress()) e.execute_single_instruction();
                 return d.bookmark_save(a, "m", jnext::dbg::SaveStateMode::AdvanceToBoundary); }},
            // CAP-04 always advances when mid-frame (the --delayed-snapshot rule),
            // so from a mid-frame handler it is refused like the other two saves.
            {"REENT-33", "save_snapshot mid-frame", none,
             [](Emulator& e, Debugger& d, ClientId a, Ctx&) {
                 if (!e.frame_in_progress()) e.execute_single_instruction();
                 const Result r = d.save_snapshot(a, "/tmp/jnext_b4_reent.sna");
                 std::remove("/tmp/jnext_b4_reent.sna");
                 return r; }},
        };

        // From a handler: the first time the AFTER_CALL gate delivers, call the
        // verb and probe the machine around the call.
        auto from_handler = [&](const Verb& v, bool& called, Probe& before, Probe& after) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            Ctx ctx;
            v.prep(emu, dbg, a, ctx);
            Z80Registers r = emu.cpu().get_registers();
            r.PC = PROG; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
            Result res = Result::Ok;
            called = false;
            Subscription s;
            s.kind      = EventKind::Execute;
            s.filter.lo = v.at; s.filter.hi = v.at;
            s.action    = Action::Continue;
            s.handler   = [&](const DbgEvent&, Debugger& d) {
                if (!called) {
                    called = true;
                    before = probe(emu, d);
                    res    = (v.inside ? v.inside : v.call)(emu, d, a, ctx);
                    after  = probe(emu, d);
                }
                return Action::Continue;
            };
            dbg.subscribe(a, s);
            for (int i = 0; i < 3 && !called; ++i) emu.run_frame();
            return std::make_pair(res, ctx.boots + ctx.loads);
        };
        // From outside any delivery, on a paused machine: the verb's own answer.
        auto from_outside = [&](const Verb& v) {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            Ctx ctx;
            v.prep(emu, dbg, a, ctx);
            dbg.pause(a);
            return v.call(emu, dbg, a, ctx);
        };

        bool inv_ok = true;
        std::string inv_where;
        for (const Verb& v : verbs) {
            bool called = false;
            Probe before, after;
            const auto in  = from_handler(v, called, before, after);
            const Result out = from_outside(v);
            check(v.id, v.name,
                  called && in.first == Result::Unsupported && out == Result::Ok,
                  std::string("inside=") + jnext::dbg::result_name(in.first) +
                      " outside=" + jnext::dbg::result_name(out) +
                      " called=" + (called ? "1" : "0"));
            const bool unchanged = called && before == after && in.second == 0;
            if (!(in.first == Result::Unsupported && unchanged)) {
                inv_ok = false;
                inv_where += std::string(" [") + v.name + " " +
                             jnext::dbg::result_name(in.first) +
                             (unchanged ? "" : " CHANGED") + "]";
            }
        }
        check("REENT-30", "THE INVARIANT: every hazardous verb, from inside a delivery, "
                          "refuses with Unsupported and leaves the machine exactly as it "
                          "was — clock, PC, run state, frame, subscriptions, ring, "
                          "matched[] — and runs no driver",
              inv_ok, inv_where);

        // The CONDITIONAL verbs' other arm. NR 0x02's soft-reset bit is the one
        // NextREG write that replaces the machine; every other write is a §4.2a
        // mutation a handler is entitled to — including NR 0x02 with the HARD
        // bit, which the machine only RECORDS for the loop owner.
        {
            Probe b, af;
            bool called = false;
            const Verb other_nr{"-", "nextreg_write(other)", none,
                [](Emulator&, Debugger& d, ClientId a, Ctx&) {
                    const Result r1 = d.nextreg_write(a, 0x15, 0x01);
                    const Result r2 = d.nextreg_write(a, 0x02, 0x02);   // hard: deferred
                    const Result r3 = d.nextreg_write(a, 0x02, 0x03);   // hard wins
                    return (r1 == Result::Ok && r2 == Result::Ok && r3 == Result::Ok)
                               ? Result::Ok : Result::Unsupported; }};
            const auto in = from_handler(other_nr, called, b, af);
            check("REENT-21", "a handler may still make every OTHER NextREG write — "
                              "including NR 0x02's deferred hard-reset bit, alone or "
                              "with the soft bit it takes precedence over",
                  called && in.first == Result::Ok);
        }
        {
            Probe b, af;
            bool called = false;
            const Verb other_port{"-", "port_out(other)", select_nr02,
                [](Emulator&, Debugger& d, ClientId a, Ctx&) {
                    const Result r1 = d.port_out(a, 0x253B, 0x02);      // NR 0x02, hard bit
                    const Result r0 = d.port_out(a, 0x253B, 0x03);      // hard wins over soft
                    const Result r2 = d.port_out(a, 0x243B, 0x15);      // select another
                    const Result r3 = d.port_out(a, 0x253B, 0x01);      // NR 0x15 = 1
                    return (r0 == Result::Ok && r1 == Result::Ok && r2 == Result::Ok &&
                            r3 == Result::Ok) ? Result::Ok : Result::Unsupported; }};
            const auto in = from_handler(other_port, called, b, af);
            check("REENT-22", "and every OTHER port write — the soft-reset value only "
                              "counts with NR 0x02 selected",
                  called && in.first == Result::Ok);
        }
        // WHY THE RUN-STATE VERBS ARE ON THE LIST, measured rather than assumed:
        // a handler's `pause()` at a boundary where another subscription has
        // already said Stop would re-arm the stop evidence (`arm()` clears the
        // latch and `hits()`), and the stop would be reported as the handler's
        // `User` pause with an empty `matched[]`. Refused, the Stop's own reason
        // and `matched[]` survive.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            Subscription stopper;
            stopper.kind      = EventKind::Execute;
            stopper.filter.lo = AFTER_CALL; stopper.filter.hi = AFTER_CALL;
            stopper.action    = Action::Stop;
            const auto sid = dbg.subscribe(a, stopper);
            Subscription pauser = stopper;
            pauser.action  = Action::Continue;
            pauser.handler = [&](const DbgEvent&, Debugger& d) {
                (void)d.pause(a);
                return Action::Continue;
            };
            dbg.subscribe(a, pauser);
            run_until_paused(emu, 3);
            const RunState st = dbg.state();
            const auto& hits  = emu.debug_state().event_table()->hits();
            bool has_stopper = false;
            for (const auto& h : hits) has_stopper = has_stopper || h.event_id == sid.value;
            check("REENT-23", "a handler's pause() at a Stop boundary cannot rewrite the "
                              "stop: it is still reported as the Stop subscription's, "
                              "and matched[] still names it",
                  st.paused && st.pause_reason.kind == PauseReason::Kind::Breakpoint &&
                      st.pause_reason.id == sid.value && has_stopper,
                  "kind=" + std::to_string(static_cast<int>(st.pause_reason.kind)) +
                      " hits=" + std::to_string(hits.size()));
        }
        // THE SAVE THAT DOES NOT ADVANCE stays the verb's own answer: only the
        // advance EXECUTES, so only the advance is refused. Mid-frame with
        // `RefuseMidFrame` a handler gets `NotAtFrameBoundary`, as anyone would;
        // at a frame boundary (a frontend's `raise_host_event()` on a paused
        // machine) a handler's save succeeds.
        {
            Probe b, af;
            bool called = false;
            const Verb refusing{"-", "save_state_bytes(RefuseMidFrame)", none,
                [](Emulator&, Debugger& d, ClientId a, Ctx&) {
                    return d.save_state_bytes(a, jnext::dbg::SaveStateMode::RefuseMidFrame).status; }};
            const auto in = from_handler(refusing, called, b, af);

            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            size_t saved = 0;
            Result at_boundary = Result::Unsupported;
            Subscription h;
            h.kind   = EventKind::Host;
            h.action = Action::Continue;
            std::strcpy(h.filter.host_name, "snap");
            h.handler = [&](const DbgEvent&, Debugger& d) {
                const auto r = d.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
                at_boundary = r.status;
                saved       = r.value.size();
                return Action::Continue;
            };
            dbg.subscribe(a, h);
            dbg.pause(a);
            const bool boundary = !emu.frame_in_progress();
            dbg.raise_host_event(a, "snap");
            check("REENT-26", "a save that does not advance is not refused from a "
                              "handler: mid-frame RefuseMidFrame answers "
                              "NotAtFrameBoundary, and at a frame boundary it saves",
                  called && in.first == Result::NotAtFrameBoundary && boundary &&
                      at_boundary == Result::Ok && saved > 0,
                  std::string("mid=") + jnext::dbg::result_name(in.first) +
                      " boundary=" + jnext::dbg::result_name(at_boundary));
        }
        // THE OTHER SIDE OF `run()`'s refusal: `detach()` is a SESSION verb, not
        // a control verb, and a delivery may reach it (a handler detaching its own
        // client; a listener detaching from inside a push a handler's `log()`
        // caused). Its release of the departing client's OWN pause must still
        // happen there — SES-01's "a crashed DeZog must not leave the machine
        // hung" does not depend on where the detach came from — so it releases
        // through `run()`'s body, not through the refused public verb. A delivery
        // with the machine paused is a frontend's `raise_host_event()`.
        {
            Emulator emu; build(emu);
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            const ClientId b = dbg.attach(client("B")).value;
            Result from_detach = Result::Unsupported;
            Subscription h;
            h.kind   = EventKind::Host;
            h.action = Action::Continue;
            std::strcpy(h.filter.host_name, "bye");
            h.handler = [&](const DbgEvent&, Debugger& d) {
                from_detach = d.detach(a);
                return Action::Continue;
            };
            dbg.subscribe(b, h);
            dbg.pause(a);                                // A's pause
            dbg.raise_host_event(b, "bye");
            check("REENT-24", "a detach from inside a delivery still releases the "
                              "departing client's own pause — the session verb is not "
                              "refused, and its release does not go through run()'s "
                              "refusal",
                  from_detach == Result::Ok && !dbg.state().paused,
                  std::string("detach=") + jnext::dbg::result_name(from_detach) +
                      " paused=" + (dbg.state().paused ? "1" : "0"));
        }
    }

    // ── R2 — the ctor/dtor publication pairing, as ONE invariant ───────────
    {
        // B2's review reproduced a REAL SEGFAULT by removing one line of
        // `~Debugger()` and calling `load_state()` afterwards, and found that
        // none of the 867 rows saw it. The pairing is what makes the fix
        // memory-safe, and it is a PAIR: the constructor publishes three things
        // into a `DebugState` that OUTLIVES the `Debugger` (it is an `Emulator`
        // member), and the destructor has to retire all three — and ONLY those
        // three, which is LIFE-06's half.
        //
        // The invariant is asserted ACROSS the pair — "a machine that outlives
        // its `Debugger` can be driven without touching the freed `Impl`" — not
        // as one row per retirement, because a row per side is exactly what
        // passed while this was untested.
        Emulator emu; build(emu);
        std::vector<uint8_t> snap;
        {
            Debugger dbg(emu);
            const ClientId a = dbg.attach(client("A")).value;
            Subscription s;
            s.kind      = EventKind::Mem;
            s.access    = Access::Write;
            s.filter.lo = WATCHED; s.filter.hi = WATCHED;
            s.action    = Action::Continue;
            s.handler   = [](const DbgEvent&, Debugger&) { return Action::Continue; };
            dbg.subscribe(a, s);
            check("LIFE-01", "while the Debugger lives, its publications are in "
                             "place and the gate is open",
                  emu.debug_state().event_table() != nullptr &&
                  (emu.debug_state().event_table()->wr_slot_mask() &
                   (1u << (0x5000 >> 13))) != 0);
            auto bytes = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
            snap = bytes.value;
        }
        // The `Debugger` is gone. `DebugState` is not.
        check("LIFE-02", "~Debugger() retired the table pointer and ZEROED the event "
                         "half of the hot-path gate",
              emu.debug_state().event_table() == nullptr &&
              (emu.debug_state().breakpoints().watch_slot_mask_wr() &
               (1u << (0x5000 >> 13))) == 0,
              hex(emu.debug_state().breakpoints().watch_slot_mask_wr()));

        // THE SCENARIO THE REVIEWER SEGFAULTED: a machine transition after the
        // `Debugger` is gone. `load_state()` calls
        // `debug_after_machine_transition_()`, which calls
        // `notify_machine_replaced()` — a `std::function` capturing the freed
        // `Impl` if the destructor did not retire it.
        const bool loaded = !snap.empty() &&
                            [&]() {
                                StateReader r(snap.data(), snap.size());
                                return emu.load_state(r);
                            }();
        check("LIFE-03", "and a state load afterwards does not reach the freed Impl",
              loaded, emu.last_state_error());

        // Guest execution afterwards, too: the drain and execute-gate hooks are
        // the other two `std::function`s, and both are consulted per frame.
        load_writer(emu, 0x55);
        const uint64_t cyc_before = emu.clock().get();
        emu.run_frame();
        const uint8_t wrote = emu.mmu().peek(WATCHED);
        check("LIFE-04", "and the machine EXECUTES on, with the drain and "
                         "execute-gate hooks retired as well",
              emu.clock().get() > cyc_before && wrote == 0x55,
              "cycles=" + std::to_string(emu.clock().get() - cyc_before) +
                  " wrote=" + hex(wrote));

        // THE OTHER DIRECTION of the same pairing: a SECOND `Debugger` on the
        // same `Emulator` must re-publish and work, which is what proves the
        // retirement left a clean slate rather than a broken one.
        {
            Debugger dbg2(emu);
            const ClientId a = dbg2.attach(client("A2")).value;
            int hits = 0;
            uint16_t first_pc    = 0;
            uint64_t first_cycle = 0;
            Subscription s;
            s.kind      = EventKind::Mem;
            s.access    = Access::Write;
            s.filter.lo = WATCHED; s.filter.hi = WATCHED;
            s.action    = Action::Continue;
            s.handler   = [&](const DbgEvent& ev, Debugger&) {
                if (hits++ == 0) {
                    first_pc    = ev.pc;
                    first_cycle = ev.cycle;
                }
                return Action::Continue;
            };
            dbg2.subscribe(a, s);
            const uint64_t cyc_before2 = emu.clock().get();
            load_writer(emu, 0x66);
            emu.run_frame();
            check("LIFE-05", "a SECOND Debugger on the same Emulator republishes and "
                             "its subscriptions fire",
                  hits > 0, "hits=" + std::to_string(hits));
            // THE FOURTH HOOK. The constructor publishes THREE things; the latch
            // stamper is the EMULATOR's (installed by `Emulator::init()`, and it
            // captures the `Emulator`, not `Impl`). A destructor that also retired
            // it left every later `Debugger` on this machine with events whose
            // common header — cycle, frame, pc, vc, hc — was never stamped, while
            // LIFE-05's hit count stayed green.
            check("LIFE-06", "and its events carry a STAMPED header — the first "
                             "Debugger's destructor did not retire the Emulator's "
                             "latch stamper",
                  hits > 0 && first_pc == 0x8002 && first_cycle >= cyc_before2,
                  "pc=" + hex(first_pc) + " cycle=" + std::to_string(first_cycle) +
                      " before=" + std::to_string(cyc_before2));
        }
    }

    // ── R3 — why the machine-replaced hook arms Kind::None and not Kind::User ─
    {
        // The choice is LOAD-BEARING and was uncovered by all 867 rows: `None`
        // falls THROUGH `state()`'s precedence switch to the legacy
        // PC-breakpoint check, `User` matches the switch immediately and returns
        // before reaching it. A previous review round proposed `User` as
        // sufficient; the author chose better, and nothing stopped the next
        // person from "simplifying" it back.
        //
        // BOTH ARMS, because the first alone would pass on a `state()` that
        // reported `Breakpoint` for everything: with a breakpoint at the landing
        // address the reason is `Breakpoint`; without one it is the unowned
        // `User{CLIENT_NONE}` fallback, which is what proves the row is about
        // the precedence rather than about the restore.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("A")).value;
        attach_and_pause(emu);

        auto bytes = dbg.save_state_bytes(a, jnext::dbg::SaveStateMode::AdvanceToBoundary);
        const uint16_t landing = emu.cpu().get_registers().PC;

        // Arm a STEP so the hook has something stale to clear, then restore.
        dbg.step_into(a);
        emu.debug_state().breakpoints().add_pc(landing);
        StateReader r1(bytes.value.data(), bytes.value.size());
        emu.load_state(r1);
        check("LAND-01", "a restore landing on a legacy PC breakpoint reports "
                         "Breakpoint — Kind::None falls THROUGH the precedence "
                         "switch, Kind::User would have swallowed it",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
              dbg.state().pause_reason.addr == landing,
              "kind=" + std::to_string(static_cast<int>(dbg.state().pause_reason.kind)) +
                  " pc=" + hex(emu.cpu().get_registers().PC));

        emu.debug_state().breakpoints().remove_pc(landing);
        dbg.step_into(a);
        StateReader r2(bytes.value.data(), bytes.value.size());
        emu.load_state(r2);
        check("LAND-02", "and with NO breakpoint there it reads as the unowned "
                         "User{CLIENT_NONE} fallback, not as the stale Step",
              dbg.state().paused &&
              dbg.state().pause_reason.kind == PauseReason::Kind::User &&
              dbg.state().pause_reason.by == jnext::dbg::CLIENT_NONE,
              "kind=" + std::to_string(static_cast<int>(dbg.state().pause_reason.kind)));
    }

    // GH #276 B4
    b4_coverage_rows();
    b4_trace_rows();
    b4_capture_state_rows();
    b4_bookmark_rows();
    b4_input_rows();
    b4_snapshot_rows();
    b4_screenshot_rows();
    b4_hosting_rows();

    // GH #276 B5
    b5_wire_kind_rows();
    b5_wire_verb_rows();
    b5_recon_rows();
    b5_match_rows();
    b5_magic_detach_rows();
    b5_range_rows();
    b5_payload_rows();
    b5_detach_rows();
    b5_host_probe_rows();
    q_wp2_host_order_rows();
    q_wp3_rewind_rows();          // GH #278 WP3
    q_wp3_trace_export_rows();    // GH #278 WP3
    q4c_observer_rows();          // GH #278 WP4c
    q4c_master_mirror_rows();     // GH #278 WP4c
    q4c_magic_hold_rows();        // GH #278 WP4c

    // GH #12 (package D) — one contiguous block, see dzrp_d_rows().
    dzrp_d_rows();
    q_wp7_raster_rows();          // GH #278 WP7

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
