// GH #489 - fixtures shared by halt_batch_observer_test and halt_batch_observer_native_test
// (the rows that need a live ESP-01 worker thread, which the WebAssembly build has not).
#pragma once
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

namespace hbq {

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;

inline int g_pass = 0, g_fail = 0, g_total = 0;

inline void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

inline void nr(Emulator& e, uint8_t reg, uint8_t val) {
    e.port().out(0x243B, reg);
    e.port().out(0x253B, val);
}
inline void poke(Emulator& e, uint16_t addr, const Bytes& b) {
    for (size_t i = 0; i < b.size(); ++i) e.mmu().write(static_cast<uint16_t>(addr + i), b[i]);
}
inline void start_at(Emulator& e, uint16_t pc, uint16_t sp) {
    auto r = e.cpu().get_registers();
    r.PC = pc;
    r.SP = sp;
    e.cpu().set_registers(r);
}

// ── The programs ─────────────────────────────────────────────────────

constexpr uint16_t kMain = 0xC000, kIsr = 0xC1C1, kVar = 0xC200, kSub = 0xC020, kIsrSub = 0xC300;
constexpr uint8_t  kIReg = 0xFD;

inline void vector_table(Emulator& e) {
    for (int i = 0; i < 257; ++i) e.mmu().write(static_cast<uint16_t>((kIReg << 8) + i), 0xC1);
}

// The shared program: IM 2 interrupt that counts itself (var+1) and toggles the beeper; main = EI; HALT.
inline void install_plain(Emulator& e) {
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
inline void install_calls(Emulator& e) {
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
inline void install_uart_rx(Emulator& e) {
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
inline void attach_window(Rig& r) {
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

inline std::string debug_snapshot(Emulator& e) {
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

inline Result run_spec(const Spec& sc, bool batch) {
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

inline Pair run_pair(const Spec& sc) {
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
inline bool grew(const Pair& p, int a, int b) { return p.on.counter[static_cast<size_t>(b)] > p.on.counter[static_cast<size_t>(a)]; }

inline std::string detail_of(const Pair& p) {
    return p.diff + " counter=" + std::to_string(p.on.run.halt_batched);
}

// ── Fixtures ─────────────────────────────────────────────────────────

inline Spec plain(const char* name, int frames) {
    Spec s;
    s.name = name;
    s.frames = frames;
    s.setup = [](Rig& r) { install_plain(*r.emu); };
    s.alive = [](Rig& r) { return r.emu->mmu().peek(kVar + 1) > 10; };
    return s;
}

inline Spec with_window(const char* name, int frames) {
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
inline Spec scripted_uart(const char* name, int channel) {
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
inline void resume_each_frame(Rig& r, int) {
    if (r.dbg) r.dbg->run(r.client);
}

inline jnext::dbg::Subscription exec_at(uint16_t pc) {
    jnext::dbg::Subscription s;
    s.kind = jnext::dbg::EventKind::Execute;
    s.filter.lo = s.filter.hi = pc;
    s.action = jnext::dbg::Action::Stop;
    return s;
}

} // namespace hbq
