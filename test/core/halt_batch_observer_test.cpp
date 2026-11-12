// GH #489 - the HALT batch with an ESP-01 or a debugger client attached: bit-identical equivalence
// (batching off vs on, per frame) and the engagement counter, on synthetic fixtures.
//
// Oracle: the unbatched per-slot path of the same emulator, held to the VHDL by the other suites
// (the method of halt_batch_fixtures.h / batch_ab.h). Each row also states the counter it expects:
// a refusal row is a statement that the batch stands aside, an engaged row that it does not.
//
// Rows: janko's four scenarios (HBQ-60/63/64: ESP at boot, disabled and enabled by a cold boot
// during the run; HBQ-65: the debugger window open) and the observers a client can still hold
// (HBQ-61/62: a UART device that wants ticks, channel 0 and 1; HBQ-66..74: Execute and Mem
// subscriptions, run-to-cycle, a Cycle subscription, coverage).
//
// No ROM, no SD image: a bare Next with a program written straight into RAM.
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "batch_ab.h"
#include "debug/debugger.h"
#include "peripheral/uart_device.h"
#include "platform/emulator_boot.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Bytes = std::vector<uint8_t>;

void nr(Emulator& e, uint8_t reg, uint8_t val) {
    e.port().out(0x243B, reg);
    e.port().out(0x253B, val);
}
void poke(Emulator& e, uint16_t addr, const Bytes& b) {
    for (size_t i = 0; i < b.size(); ++i) e.mmu().write(static_cast<uint16_t>(addr + i), b[i]);
}
void start_at(Emulator& e, uint16_t pc, uint16_t sp) {
    auto r = e.cpu().get_registers();
    r.PC = pc;
    r.SP = sp;
    e.cpu().set_registers(r);
}

// ── The programs ─────────────────────────────────────────────────────

constexpr uint16_t kMain = 0xC000, kIsr = 0xC1C1, kVar = 0xC200, kSub = 0xC020, kIsrSub = 0xC300;
constexpr uint8_t  kIReg = 0xFD;

void vector_table(Emulator& e) {
    for (int i = 0; i < 257; ++i) e.mmu().write(static_cast<uint16_t>((kIReg << 8) + i), 0xC1);
}

// The shared program: IM 2 interrupt that counts itself (var+1) and toggles the beeper; main = EI; HALT.
void install_plain(Emulator& e) {
    vector_table(e);
    poke(e, kIsr, {0xF5, 0x3A, 0x00, 0xC2, 0xEE, 0x10, 0x32, 0x00, 0xC2, 0xD3, 0xFE,
                   0x3A, 0x01, 0xC2, 0x3C, 0x32, 0x01, 0xC2, 0xF1, 0xFB, 0xED, 0x4D});
    poke(e, kMain, {0x3E, kIReg, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
    nr(e, 0x03, 0x01);
    nr(e, 0x07, 0x03);   // 28 MHz, the speed NextZXOS runs at
    start_at(e, kMain, 0xBFF0);
}

// Call-stack material: main CALLs a routine whose first instruction is the HALT (so the newest
// frame's target_pc is the HALT's pc and its target space is filled by the first repeat); the ISR
// CALLs a subroutine that counts and RETs.
void install_calls(Emulator& e) {
    vector_table(e);
    poke(e, kIsr, {0xF5, 0xCD, static_cast<uint8_t>(kIsrSub & 0xFF), static_cast<uint8_t>(kIsrSub >> 8),
                   0xF1, 0xFB, 0xED, 0x4D});
    poke(e, kIsrSub, {0x3A, 0x01, 0xC2, 0x3C, 0x32, 0x01, 0xC2, 0xC9});
    poke(e, kSub, {0x76, 0x18, 0xFD});   // HALT; JR $-1 (to the HALT)
    poke(e, kMain, {0x3E, kIReg, 0xED, 0x47, 0xED, 0x5E, 0xFB,
                    0xCD, static_cast<uint8_t>(kSub & 0xFF), static_cast<uint8_t>(kSub >> 8),
                    0x18, 0xFE});
    nr(e, 0x03, 0x01);
    nr(e, 0x07, 0x03);
    start_at(e, kMain, 0xBFF0);
}
constexpr uint16_t kHaltPc = kSub;

// UART RX interrupt handler: reads the byte (port 0x143B), stores it (var+2), counts (var+1).
void install_uart_rx(Emulator& e) {
    vector_table(e);
    poke(e, kIsr, {0xF5, 0xC5, 0x01, 0x3B, 0x14, 0xED, 0x78, 0x32, 0x02, 0xC2,
                   0x3A, 0x01, 0xC2, 0x3C, 0x32, 0x01, 0xC2, 0xC1, 0xF1, 0xFB, 0xED, 0x4D});
    poke(e, kMain, {0x3E, kIReg, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
    nr(e, 0x03, 0x01);
    nr(e, 0x07, 0x03);
    nr(e, 0x22, 0x04);   // ULA interrupt off: the UART is the only source
    start_at(e, kMain, 0xBFF0);
}

// A device that wants ticks only while told to, and delivers one byte every `every` master cycles
// (150000: its 8 bytes span a little over two frames).
class ScriptedDevice : public UartDevice {
public:
    void receive(uint8_t) override {}
    void tick(uint32_t master_cycles, uint32_t) override {
        acc_ += master_cycles;
        while (acc_ >= every && left_ > 0) {
            acc_ -= every;
            send_to_guest(static_cast<uint8_t>(0xA0 + sent_++));
            if (--left_ == 0) set_tick_wanted(false);
        }
    }
    void start(int bytes) { left_ = bytes; acc_ = 0; set_tick_wanted(true); }
    uint32_t every = 150000;
    int sent_ = 0;
private:
    uint64_t acc_ = 0;
    int left_ = 0;
};

// ── The rig: one machine, optionally a debugger client, run with batching off or on ──

struct Rig {
    std::unique_ptr<ScriptedDevice> stub;   // declared first: outlives the emulator
    std::unique_ptr<Emulator> emu;
    std::unique_ptr<Debugger> dbg;          // declared last: dies first
    ClientId client = jnext::dbg::CLIENT_NONE;
    bool batch = false;
    std::vector<std::string> log;           // per-run notes the two runs must agree on
    jnext::dbg::EventId id = jnext::dbg::EVENT_NONE;
};

// What the debugger window's attach switches on (DebuggerManager::attach_backend()).
void attach_window(Rig& r) {
    r.dbg = std::make_unique<Debugger>(*r.emu);
    r.client = r.dbg->attach(jnext::dbg::ClientInfo{"Qt GUI", jnext::dbg::ClientKind::Gui}).value;
    r.dbg->set_live_raster(r.client, true);
    r.dbg->set_call_stack_enabled(r.client, true);
}

struct Spec {
    const char* name = "";
    int frames = 60;
    std::function<void(EmulatorConfig&)> config;
    std::function<void(Rig&)> setup;
    std::function<void(Rig&, int)> before;
    std::function<void(Rig&, int)> after;
    std::function<bool(Rig&)> alive;
    bool watch_debug = false;   // also compare the call stack and the raster walk per frame
};

struct Result {
    // log: notes the two runs must agree on; a note starting with '#' is a counter mark of that run, not compared.
    batch_ab::Run run;
    std::vector<uint64_t> counter;       // halt_repeats_batched_for_test() after each frame
    std::vector<std::string> extra;      // call stack + raster per frame (watch_debug)
    std::vector<std::string> log;
};

std::string debug_snapshot(Emulator& e) {
    std::string s;
    for (const CallFrame& f : e.call_stack().frames()) {
        s += std::to_string(f.caller_pc) + "," + std::to_string(f.target_pc) + "," +
             std::to_string(f.sp_at_call) + "," + std::to_string(static_cast<int>(f.type)) + "," +
             std::to_string(static_cast<int>(f.caller_space.index)) + "," +
             std::to_string(static_cast<int>(f.target_space.index)) + "," +
             std::to_string(f.ret_stored) + "," + std::to_string(f.ret_stored_at) + ";";
    }
    const VideoTiming& t = e.video_timing();
    s += "|" + std::to_string(t.pos().hc) + "," + std::to_string(t.pos().vc) + "," +
         std::to_string(t.frame_complete()) + "," + std::to_string(t.ula_int_pulse_count()) + "," +
         std::to_string(t.line_int_pulse_count());
    return s;
}

batch_ab::Scratch g_scratch;

Result run_spec(const Spec& sc, bool batch) {
    Result res;
    Rig rig;
    rig.batch = batch;
    rig.emu = std::make_unique<Emulator>();
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    if (sc.config) sc.config(cfg);
    if (!rig.emu->init(cfg)) return res;
    rig.emu->set_cycle_batching_for_test(batch, batch);
    if (sc.setup) sc.setup(rig);
    res.run.built = true;
    for (int f = 0; f < sc.frames; ++f) {
        if (sc.before) sc.before(rig, f);
        rig.emu->run_frame();
        if (rig.dbg) rig.dbg->pump(jnext::dbg::PumpBudget{});
        if (sc.after) sc.after(rig, f);
        res.run.frames.push_back(batch_ab::capture(*rig.emu, g_scratch, true));
        res.counter.push_back(rig.emu->halt_repeats_batched_for_test());
        if (sc.watch_debug) res.extra.push_back(debug_snapshot(*rig.emu));
    }
    res.run.alive = sc.alive ? sc.alive(rig) : true;
    res.run.halt_batched = rig.emu->halt_repeats_batched_for_test();
    res.log = rig.log;
    return res;
}

struct Pair {
    Result off, on;
    std::string diff;   // "" = identical per frame (hashes, call stack and raster, notes)
};

Pair run_pair(const Spec& sc) {
    Pair p;
    p.off = run_spec(sc, false);
    p.on = run_spec(sc, true);
    p.diff = batch_ab::first_diff(p.off.run, p.on.run);
    if (p.diff.empty() && p.off.extra != p.on.extra) {
        for (size_t i = 0; i < p.off.extra.size() && i < p.on.extra.size(); ++i)
            if (p.off.extra[i] != p.on.extra[i]) {
                p.diff = "frame " + std::to_string(i) + " call stack / raster: [" + p.off.extra[i] +
                         "] vs [" + p.on.extra[i] + "]";
                break;
            }
    }
    auto same_notes = [](const std::vector<std::string>& v) {
        std::vector<std::string> out;
        for (const auto& s : v) if (s.empty() || s[0] != '#') out.push_back(s);
        return out;
    };
    if (p.diff.empty() && same_notes(p.off.log) != same_notes(p.on.log)) {
        std::string a, b;
        for (const auto& s : same_notes(p.off.log)) a += s + ";";
        for (const auto& s : same_notes(p.on.log)) b += s + ";";
        p.diff = "notes differ: off{" + a + "} on{" + b + "}";
    }
    if (!p.off.run.alive || !p.on.run.alive) p.diff += " (fixture dead)";
    if (p.off.run.halt_batched != 0) p.diff += " (the OFF run batched)";
    return p;
}

// The counter did / did not grow between the end of frame `a` and the end of frame `b` (the "on" run).
bool grew(const Pair& p, int a, int b) { return p.on.counter[static_cast<size_t>(b)] > p.on.counter[static_cast<size_t>(a)]; }

std::string detail_of(const Pair& p) {
    return p.diff + " counter=" + std::to_string(p.on.run.halt_batched);
}

// ── Fixtures ─────────────────────────────────────────────────────────

Spec plain(const char* name, int frames) {
    Spec s;
    s.name = name;
    s.frames = frames;
    s.setup = [](Rig& r) { install_plain(*r.emu); };
    s.alive = [](Rig& r) { return r.emu->mmu().peek(kVar + 1) > 10; };
    return s;
}

Spec with_window(const char* name, int frames) {
    Spec s;
    s.name = name;
    s.frames = frames;
    s.watch_debug = true;
    s.setup = [](Rig& r) {
        install_calls(*r.emu);
        attach_window(r);
    };
    s.alive = [](Rig& r) {
        return r.emu->mmu().peek(kVar + 1) > 10 && r.emu->call_stack().frames().size() >= 1 &&
               r.emu->cpu().is_halted();
    };
    return s;
}

// A stub UART device on `channel`, wanting ticks from frame 10 (8 bytes, 150000 master cycles apart),
// the guest on EI;HALT with the UART RX interrupt on. The notes record the byte count each frame.
Spec scripted_uart(const char* name, int channel) {
    Spec s;
    s.name = name;
    s.frames = 40;
    s.setup = [channel](Rig& r) {
        install_uart_rx(*r.emu);
        r.stub = std::make_unique<ScriptedDevice>();
        r.emu->uart().attach_device(channel, r.stub.get());
        if (channel == 1) r.emu->port().out(0x153B, 0x40);   // read channel 1's RX port
        nr(*r.emu, 0xC6, channel == 0 ? 0x01 : 0x10);
    };
    s.before = [](Rig& r, int f) {
        if (f == 10) r.stub->start(8);
    };
    s.alive = [](Rig& r) { return r.emu->mmu().peek(kVar + 1) == 8 && r.stub->sent_ == 8; };
    return s;
}

// A frame-by-frame resume: the machine paused by a subscription runs on to the next stop.
void resume_each_frame(Rig& r, int) {
    if (r.dbg) r.dbg->run(r.client);
}

jnext::dbg::Subscription exec_at(uint16_t pc) {
    jnext::dbg::Subscription s;
    s.kind = jnext::dbg::EventKind::Execute;
    s.filter.lo = s.filter.hi = pc;
    s.action = jnext::dbg::Action::Stop;
    return s;
}

} // namespace

int main() {
    std::printf("GH #489 HALT batch with an ESP-01 / debugger client attached (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // HBQ-60: janko's scenario 1 - ESP enabled at boot (the production setup_esp(): the real
    // ThreadedEsp, the adapter, attach_device(0)). An idle ESP wants no tick, so it must not
    // refuse the batch (uart.h Uart::batch_quiescent()). The idle ESP is deterministic.
    {
        Spec s = plain("esp-at-boot", 60);
        s.config = [](EmulatorConfig& c) { c.esp_enabled = true; };
        auto alive = s.alive;
        s.alive = [=](Rig& r) { return alive(r) && r.emu->uart().device(0) != nullptr; };
        const Pair p = run_pair(s);
        check("HBQ-60", "ESP enabled at boot, EI;HALT at 28 MHz: identical per frame, the HALT batch engaged with the device attached",
              p.diff.empty() && p.on.run.halt_batched > 0, detail_of(p));
    }

    // HBQ-61: a UART device on channel 0 that wants ticks for a stretch (one byte every 150000 master
    // cycles, from frame 10: wanted through frames 10 and 11): the batch stands aside while it wants
    // them (the bytes land at the same cycles as unbatched) and engages before and after.
    {
        const Spec s = scripted_uart("uart-device-ch0", 0);
        const Pair p = run_pair(s);
        check("HBQ-61", "a UART device on channel 0 that wants ticks for a stretch: identical per frame (every byte lands at the same cycle), the batch engaged before and after",
              p.diff.empty() && p.on.counter[9] > 0 && !grew(p, 9, 11) && grew(p, 11, 39), detail_of(p));
    }

    // HBQ-62: the same on channel 1 (the Pi's channel): kills "only channel 0's device is checked".
    {
        const Spec s = scripted_uart("uart-device-ch1", 1);
        const Pair p = run_pair(s);
        check("HBQ-62", "a UART device on channel 1 that wants ticks for a stretch: identical per frame, the batch engaged before and after",
              p.diff.empty() && p.on.counter[9] > 0 && !grew(p, 9, 11) && grew(p, 11, 39), detail_of(p));
    }

    // HBQ-63: janko's scenario 2 - the ESP "disabled during the run": the machine keeps its device
    // until the next cold boot (Preferences cannot detach it), then the cold boot builds none.
    // One machine per run, 40 frames each side; the batch engaged on both.
    {
        Spec s = plain("esp-then-cold-boot-without", 80);
        s.config = [](EmulatorConfig& c) { c.esp_enabled = true; };
        s.before = [](Rig& r, int f) {
            if (f == 0) r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
            if (f != 40) return;
            EmulatorConfig off = r.emu->config();
            off.esp_enabled = false;
            emulator_cold_boot(*r.emu, off);
            r.emu->set_cycle_batching_for_test(r.batch, r.batch);
            install_plain(*r.emu);
            r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
        };
        const Pair p = run_pair(s);
        check("HBQ-63", "ESP enabled, then a cold boot without it during the run: identical per frame across the whole sequence, the HALT batch engaged in both halves, the device present then absent",
              p.diff.empty() && p.on.counter[39] > 0 && grew(p, 40, 79) &&
                  p.on.log.size() == 2 && p.on.log[0] == "device=1" && p.on.log[1] == "device=0",
              detail_of(p));
    }

    // HBQ-64: janko's scenario 3 - ESP enabled during the run after a start without it: a cold boot
    // with it (the only way a running machine gets one). Batch engaged in both halves.
    {
        Spec s = plain("no-esp-then-cold-boot-with", 80);
        s.before = [](Rig& r, int f) {
            if (f == 0) r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
            if (f != 40) return;
            EmulatorConfig on = r.emu->config();
            on.esp_enabled = true;
            emulator_cold_boot(*r.emu, on);
            r.emu->set_cycle_batching_for_test(r.batch, r.batch);
            install_plain(*r.emu);
            r.log.push_back(std::string("device=") + (r.emu->uart().device(0) ? "1" : "0"));
        };
        const Pair p = run_pair(s);
        check("HBQ-64", "no ESP, then a cold boot with it during the run: identical per frame across the whole sequence, the HALT batch engaged in both halves, the device absent then present",
              p.diff.empty() && p.on.counter[39] > 0 && grew(p, 40, 79) &&
                  p.on.log.size() == 2 && p.on.log[0] == "device=0" && p.on.log[1] == "device=1",
              detail_of(p));
    }

    // HBQ-65: janko's scenario 4 - the debugger window open: a Gui client attached with the live raster
    // and the call-stack tracker (DebuggerManager::attach_backend()). An idle client observes nothing per
    // instruction: the batch engages, and everything it stopped doing is still right: the call stack
    // (the CALL into the HALT routine and the ISR's CALL/RETI frames, every field) and the raster walk
    // (position, frame flag, both interrupt pulse counts), compared per frame.
    {
        Spec s = with_window("window-open", 60);
        const Pair p = run_pair(s);
        check("HBQ-65", "debugger window open (client + live raster + call stack) on EI;HALT at 28 MHz: identical per frame (state, video, audio, call-stack frames, raster walk), the HALT batch engaged",
              p.diff.empty() && p.on.run.halt_batched > 0, detail_of(p));
    }

    // HBQ-66: an Execute subscription AT the HALT, a resume before each frame: each resume stops after
    // exactly one repeat (a batch would carry the stop past the subscription). Refused while it is live,
    // engaged once it is removed (unsubscribe at frame 20).
    {
        Spec s = with_window("window-execute-at-halt", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.id = r.dbg->subscribe(r.client, exec_at(kHaltPc)).value;
        };
        s.before = [](Rig& r, int f) {
            if (f == 20) r.dbg->unsubscribe(r.client, r.id);
            resume_each_frame(r, f);
        };
        const Pair p = run_pair(s);
        check("HBQ-66", "an Execute subscription at the HALT with a resume per frame, then removed at frame 20: identical per frame, no batching while it is live, batched after",
              p.diff.empty() && p.on.counter[19] == 0 && grew(p, 20, 39), detail_of(p));
    }

    // HBQ-67: an Execute subscription at an UNRELATED address: the batch stays off (deliberately
    // conservative: any live subscription refuses); disabled at frame 20 it cannot fire and the batch
    // engages.
    {
        Spec s = with_window("window-execute-elsewhere", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.id = r.dbg->subscribe(r.client, exec_at(0x9000)).value;
        };
        s.before = [](Rig& r, int f) {
            if (f == 20) r.dbg->set_enabled(r.client, r.id, false);
        };
        const Pair p = run_pair(s);
        check("HBQ-67", "an Execute subscription elsewhere: identical per frame and the HALT batch refused while it is live, engaged once it is disabled at frame 20",
              p.diff.empty() && p.on.counter[19] == 0 && grew(p, 20, 39), detail_of(p));
    }

    // HBQ-68: a read watch (a Mem subscription) on the HALT's own address: the M1 fetch is a read for
    // watches, so each resume stops after one repeat, identically; the batch stays off.
    {
        Spec s = with_window("window-read-watch-at-halt", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            jnext::dbg::Subscription w;
            w.kind = jnext::dbg::EventKind::Mem;
            w.filter.lo = w.filter.hi = kHaltPc;
            w.access = jnext::dbg::Access::Read;
            w.action = jnext::dbg::Action::Stop;
            r.id = r.dbg->subscribe(r.client, w).value;
        };
        s.before = resume_each_frame;
        s.alive = [](Rig& r) { return r.emu->call_stack().frames().size() >= 1 && r.emu->debug_state().paused(); };
        const Pair p = run_pair(s);
        check("HBQ-68", "a read watch on the HALT's address with a resume per frame: identical per frame, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0, detail_of(p));
    }

    // HBQ-69: run-to-cycle (StepMode RUN_TO_CYCLE) aimed 100000 master cycles into a halted stretch of
    // frame 5: the stop lands on the identical clock and PC with the batching on as off, the counter
    // does not move while it is in flight, and the batch engages after the resume.
    {
        Spec s = with_window("window-run-to-cycle", 20);
        s.watch_debug = false;
        s.before = [](Rig& r, int f) {
            if (f != 5) return;
            r.dbg->run_to_cycle(r.client, r.emu->clock().get() + 100000);
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
        };
        s.after = [](Rig& r, int f) {
            if (f != 5) return;
            r.log.push_back("stop paused=" + std::to_string(r.emu->debug_state().paused()) +
                            " clock=" + std::to_string(r.emu->clock().get()) +
                            " pc=" + std::to_string(r.emu->cpu().pc()));
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
            r.dbg->run(r.client);
        };
        const Pair p = run_pair(s);
        // log = {#counter when armed, stop note, #counter at the stop}: frozen while in flight.
        const bool frozen = p.on.log.size() == 3 && p.on.log[0].substr(1) == p.on.log[2].substr(1);
        check("HBQ-69", "run-to-cycle into a halted stretch: the stop lands on the identical clock and PC batched as unbatched, the counter frozen while in flight, the batch engaged after the resume",
              p.diff.empty() && frozen && p.on.log[1].find("paused=1") != std::string::npos && grew(p, 5, 19),
              detail_of(p) + " " + (p.on.log.size() == 3 ? p.on.log[0] + " | " + p.on.log[1] + " | " + p.on.log[2] : "log?"));
    }

    // HBQ-70: a Cycle subscription (a boundary comparison against the master clock) aimed into a halted
    // stretch of frame 5: the stop lands on the identical boundary, the counter does not move while
    // it is armed, the batch engages after it fires and is removed.
    {
        Spec s = with_window("window-cycle-subscription", 20);
        s.watch_debug = false;
        s.before = [](Rig& r, int f) {
            if (f != 5) return;
            jnext::dbg::Subscription c;
            c.kind = jnext::dbg::EventKind::Cycle;
            c.filter.cycle = r.emu->clock().get() + 100000;
            c.action = jnext::dbg::Action::Stop;
            c.once = true;
            r.id = r.dbg->subscribe(r.client, c).value;
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
        };
        s.after = [](Rig& r, int f) {
            if (f != 5) return;
            r.log.push_back("stop paused=" + std::to_string(r.emu->debug_state().paused()) +
                            " clock=" + std::to_string(r.emu->clock().get()) +
                            " pc=" + std::to_string(r.emu->cpu().pc()));
            r.log.push_back("#" + std::to_string(r.emu->halt_repeats_batched_for_test()));
            r.dbg->unsubscribe(r.client, r.id);
            r.dbg->run(r.client);
        };
        const Pair p = run_pair(s);
        const bool frozen = p.on.log.size() == 3 && p.on.log[0].substr(1) == p.on.log[2].substr(1);
        check("HBQ-70", "a Cycle subscription into a halted stretch: the stop lands on the identical boundary batched as unbatched, the counter frozen while it is armed, the batch engaged after",
              p.diff.empty() && frozen && p.on.log[1].find("paused=1") != std::string::npos && grew(p, 5, 19),
              detail_of(p) + " " + (p.on.log.size() == 3 ? p.on.log[0] + " | " + p.on.log[1] + " | " + p.on.log[2] : "log?"));
    }

    // HBQ-71: PC coverage and memory coverage on: the batch stays off (explicit statements of the
    // contract: both are idempotent bit sets), identical per frame, and the sets are identical.
    {
        Spec s = with_window("window-coverage", 30);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.dbg->coverage_enable(true);
            r.dbg->mem_coverage_enable(true);
        };
        s.after = [](Rig& r, int f) {
            if (f != 29) return;
            size_t pcs = 0;
            for (size_t i = 0; i < 65536; ++i) pcs += r.dbg->coverage().test(i) ? 1 : 0;
            uint8_t flags[64] = {};
            uint32_t sum = 0;
            for (uint32_t a = 0xC000; a < 0xC400; a += 64) {
                for (uint32_t i = 0; i < 64; ++i) flags[i] = r.dbg->mem_coverage_fetch(static_cast<uint16_t>(a + i));
                for (uint8_t b : flags) sum = sum * 31 + b;
            }
            r.log.push_back("pc_bits=" + std::to_string(pcs) + " mem_sum=" + std::to_string(sum));
        };
        const Pair p = run_pair(s);
        check("HBQ-71", "PC and memory coverage on: identical per frame, the coverage sets identical, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0 && p.on.log.size() == 1, detail_of(p));
    }

    // HBQ-73: a LEGACY PC breakpoint (the BreakpointSet model, not a subscription) AT the HALT with a
    // client attached and a resume per frame: each resume stops after one repeat; the batch stays off.
    {
        Spec s = with_window("window-legacy-breakpoint-at-halt", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.emu->debug_state().breakpoints().add_pc(kHaltPc);
        };
        s.before = resume_each_frame;
        s.alive = [](Rig& r) { return r.emu->call_stack().frames().size() >= 1 && r.emu->debug_state().paused(); };
        const Pair p = run_pair(s);
        check("HBQ-73", "a legacy PC breakpoint at the HALT with a client attached and a resume per frame: identical per frame, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0, detail_of(p));
    }

    // HBQ-74: a LEGACY I/O watchpoint (the same set's wp_all_, on the port the ISR writes) with a client
    // attached: the batch stays off (any watchpoint refuses, D3), identical per frame.
    {
        Spec s = with_window("window-legacy-io-watchpoint", 40);
        s.watch_debug = false;
        s.setup = [](Rig& r) {
            install_calls(*r.emu);
            attach_window(r);
            r.emu->debug_state().breakpoints().add_watchpoint(0x00FE, WatchType::IO_WRITE);
        };
        const Pair p = run_pair(s);
        check("HBQ-74", "a legacy I/O watchpoint with a client attached: identical per frame, the HALT batch never engaged",
              p.diff.empty() && p.on.run.halt_batched == 0, detail_of(p));
    }

    // HBQ-72: the window CLOSED with the user's breakpoints kept: a non-arming observer client (the Qt
    // GUI's lifetime client) holds a live Execute subscription at the HALT and nothing arms the machine,
    // so the hot loop never reads it (GH #219): the batch must stay engaged, identical per frame.
    {
        Spec s = plain("observer-breakpoint-unarmed", 40);
        s.setup = [](Rig& r) {
            install_plain(*r.emu);
            r.dbg = std::make_unique<Debugger>(*r.emu);
            jnext::dbg::ClientInfo info{"Qt GUI breakpoints", jnext::dbg::ClientKind::Gui};
            info.observer = true;
            r.client = r.dbg->attach(info).value;
            r.dbg->subscribe(r.client, exec_at(static_cast<uint16_t>(kMain + 7)));
        };
        auto alive = s.alive;
        s.alive = [=](Rig& r) { return alive(r) && !r.emu->debug_state().armed(); };
        const Pair p = run_pair(s);
        check("HBQ-72", "a non-arming observer client with a live Execute subscription at the HALT (window closed, breakpoints kept): identical per frame, the HALT batch engaged",
              p.diff.empty() && p.on.run.halt_batched > 0, detail_of(p));
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
