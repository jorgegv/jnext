/// Rewind restores the debug call-stack tracker.
///
/// The tracker (`CallStack`) is debugger side state, not emulated state, so it
/// is not in the serialised snapshot stream. Before this fix a rewind restored
/// the machine but left the tracker holding the frames of the history being
/// left: after rewinding to a point inside a subroutine the Call Stack panel
/// said the subroutine had already returned, and anything that measures call
/// depth (Step Over / Step Out by depth, a source-level step) measured the
/// wrong history.
///
/// The program (48K machine, interrupts off, no ROM needed):
///
///   8000  CD 00 90     CALL SUB
///   8003  18 FE        JR $            <- parks here, depth 0
///   9000  01 90 19     LD BC,$1990     <- SUB: about 2.4 frames of looping
///   9003  0B           DEC BC
///   9004  78           LD A,B
///   9005  B1           OR C
///   9006  20 FB        JR NZ,$9003
///   9008  C9           RET
///
/// RWCS-10/11 cover the sibling paths that replace the machine without the
/// ring — a state load and a reset — which left the tracker stale the same way.
///
/// Frame 0 starts at 8000 (depth 0); frames 1 and 2 start inside the loop (depth 1);
/// by the end of frame 2 the subroutine has returned (depth 0 again). On the
/// pre-fix tree RWCS-03, -04, -06 and -09 fail: the tracker keeps whatever the
/// abandoned history left in it.
///
/// Run: ./build/test/rewind_call_stack_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/call_stack.h"
#include "debug/rewind_buffer.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

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

static constexpr uint16_t PROG = 0x8000;
static constexpr uint16_t SUB  = 0x9000;
static constexpr uint16_t PARK = 0x8003;

static void build(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = 8;
    emu.init(cfg);
    emu.trace_log().set_enabled(true);       // step_back() needs it
    emu.call_stack().set_enabled(true);

    const uint8_t main_prog[] = { 0xCD, 0x00, 0x90, 0x18, 0xFE };
    for (size_t i = 0; i < sizeof(main_prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(PROG + i), main_prog[i]);
    const uint8_t sub[] = { 0x01, 0x90, 0x19, 0x0B, 0x78, 0xB1, 0x20, 0xFB, 0xC9 };
    for (size_t i = 0; i < sizeof(sub); ++i)
        emu.mmu().write(static_cast<uint16_t>(SUB + i), sub[i]);

    Z80Registers r = emu.cpu().get_registers();
    r.PC   = PROG;
    r.SP   = 0xFF00;
    r.IFF1 = 0;
    r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

static std::string depth_text(const Emulator& emu) {
    return "depth " + std::to_string(emu.call_stack().frames().size());
}

int main() {
    std::printf("Rewind call-stack restore\n");

    // ── rewind_to_frame() into the subroutine ───────────────────────────
    {
        Emulator emu;
        build(emu);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        const bool returned = emu.cpu().get_registers().PC == PARK &&
                              emu.call_stack().frames().empty();
        check("RWCS-01", "precondition: after three frames SUB has returned and "
              "the tracker is empty", returned, depth_text(emu));

        const bool ok = emu.rewind_to_frame(1);
        const auto& frames = emu.call_stack().frames();
        check("RWCS-02", "rewind_to_frame() into the loop reports success", ok);
        check("RWCS-03", "the restored tracker holds the CALL that is still open "
              "at that frame", frames.size() == 1, depth_text(emu));
        check("RWCS-04", "the restored frame is the CALL from $8000 to SUB",
              frames.size() == 1 && frames[0].caller_pc == PROG &&
                  frames[0].target_pc == SUB && frames[0].type == CallType::CALL);
    }

    // ── rewind_to_frame() before the CALL ───────────────────────────────
    {
        Emulator emu;
        build(emu);
        for (int i = 0; i < 2; ++i) emu.run_frame();   // still in the loop
        const bool inside = emu.call_stack().frames().size() == 1;
        check("RWCS-05", "precondition: after two frames the CALL is open",
              inside, depth_text(emu));
        emu.rewind_to_frame(0);
        check("RWCS-06", "rewind_to_frame(0), before the CALL, empties the tracker",
              emu.call_stack().frames().empty(), depth_text(emu));
    }

    // ── step_back() across the RET ──────────────────────────────────────
    {
        Emulator emu;
        build(emu);
        for (int i = 0; i < 3; ++i) emu.run_frame();   // parked at JR $
        // The newest trace entries are the JR $ spins; walk back to the RET.
        const auto& trace = emu.trace_log();
        size_t back = 0;
        for (size_t i = trace.size(); i > 0; --i) {
            if (trace.at(i - 1).pc == SUB + 8) { back = trace.size() - (i - 1); break; }
        }
        check("RWCS-07", "precondition: the RET is still in the retained trace",
              back > 0);
        const bool ok = back > 0 && emu.step_back(static_cast<int>(back));
        check("RWCS-08", "step_back() to the RET lands on it",
              ok && emu.cpu().get_registers().PC == SUB + 8);
        check("RWCS-09", "standing on the RET again, the CALL is open again",
              emu.call_stack().frames().size() == 1, depth_text(emu));
    }

    // ── the same defect on the paths that replace the machine without the
    //    ring: a state load (bookmark, DZRP/ZRCP restore, snapshot) and a
    //    reset ─────────────────────────────────────────────────────────────
    {
        Emulator emu;
        build(emu);
        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> state(measure.position(), 0);
        StateWriter w(state.data(), state.size());
        emu.save_state(w);                               // at $8000, depth 0
        emu.run_frame();                                 // into SUB
        const bool inside = emu.call_stack().frames().size() == 1;
        StateReader r(state.data(), state.size());
        const bool loaded = emu.load_state(r);
        check("RWCS-10", "a state loaded from outside the ring drops the replaced "
              "machine's frames",
              inside && loaded && emu.call_stack().frames().empty(), depth_text(emu));
    }
    {
        Emulator emu;
        build(emu);
        emu.run_frame();                                 // into SUB
        const bool inside = emu.call_stack().frames().size() == 1;
        emu.soft_reset();
        check("RWCS-11", "a reset drops the frames of the machine it reset",
              inside && emu.call_stack().frames().empty(), depth_text(emu));
    }

    std::printf("\nTotal: %d  Passed: %d  Failed: %d  Skipped: 0\n",
                g_total, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
