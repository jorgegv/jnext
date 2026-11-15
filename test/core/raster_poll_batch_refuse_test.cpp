// GH #496 - the raster-poll batch's gates and refusals, on synthetic fixtures: every row is identical per
// frame with the batch off and on, and says whether the "on" run engaged. The engaged cases are
// raster_poll_batch_test. See raster_poll_fixtures.h.
#include "raster_poll_fixtures.h"

#include "audio/pi_audio.h"

#include <memory>

namespace {

// The z88dk style: a routine that selects and reads a register, called from the wait loop (PUSH, POP,
// CALL and RET are outside the whitelist).
Bytes call_loop() {
    Asm a(0xC000);
    a.op({0xF3});
    a.label("L").op({0x3E, 0x1F}).jp(0xCD, "rd").op({0xFE, 0xB4}).jr(0x20, "L");
    count_up(a);
    a.label("W").op({0x3E, 0x1F}).jp(0xCD, "rd").op({0xFE, 0xB4}).jr(0x28, "W").jr(0x18, "L");
    a.label("rd").op({0xC5, 0x01, 0x3B, 0x24, 0xED, 0x79, 0x04, 0xED, 0x78, 0xC1, 0xC9});
    return a.done();
}

// A DJNZ-bounded wait: B counts down every pass, so the CPU is never where it was.
Bytes djnz_loop() {
    Asm a(0xC000);
    a.op({0xF3, 0x01, 0x3B, 0x24, 0x3E, 0x1F, 0xED, 0x79});
    a.label("S").op({0x06, 0x00});
    a.label("L").op({0x3E, 0x25, 0xDB, 0x3B, 0xFE, 0xB4}).jr(0x28, "T").jr(0x10, "L").jr(0x18, "S");
    a.label("T");
    count_up(a);
    a.jr(0x18, "S");
    return a.done();
}

// A real TAP of one header and one data block, for a tape playing in real time.
std::string write_tap(const char* tag) {
    const std::string path = std::string("/tmp/") + tag + "-" + std::to_string(static_cast<long>(::getpid())) + ".tap";
    Bytes tap = {19, 0, 0x00, 3, 'r', 'p', 'b', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 4, 0, 0x00, 0x80, 0x00, 0x80, 0};
    uint8_t sum = 0;
    for (size_t i = 2; i + 1 < tap.size(); ++i) sum ^= tap[i];
    tap.back() = sum;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fwrite(tap.data(), 1, tap.size(), f); std::fclose(f); }
    return path;
}

} // namespace

int main() {
    std::printf("GH #496 raster-poll batch gates and refusals (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // RPB-12: at 3.5 MHz a page that is contended while the display is drawn makes a pass cost what the beam
    // says (zxnext.vhd:4481, :4489-4493): refused while contention can apply.
    check_poll("RPB-12", "the RPB-01 loop in bank 5 at 3.5 MHz: identical per frame, REFUSED (contention can apply)",
               {poll_fx("bank5-3.5MHz", 0, 0xFF, guide_loop(0x6000, 0x6210), 30, 0x6000, {}, 0x6210)}, Poll::Refused);

    // RPB-20: a store each pass (LD (nn),A) is outside the whitelist: its target could be the loop's own code.
    check_poll("RPB-20", "guide loop with LD (nn),A in the pass: identical per frame, REFUSED (a store)",
               {poll_fx("store", 3, 0xFF, guide_loop(0xC000, kCount, {0x32, 0x20, 0xC2}))}, Poll::Refused);

    // RPB-21: an IN of another port: reading the DMA port advances its read sequence, a side effect a skipped
    // pass would not make.
    check_poll("RPB-21", "guide loop that also reads the DMA port 0x6B: identical per frame, REFUSED (another IN)",
               {poll_fx("other-in", 3, 0xFF, guide_loop(0xC000, kCount, {0x57, 0xDB, 0x6B, 0x7A}))}, Poll::Refused);

    // RPB-22: an OUT other than the select: a write to the sprite attribute port 0x57 advances the upload
    // index every time.
    check_poll("RPB-22", "guide loop that also writes the sprite attribute port 0x57: identical per frame, REFUSED (another OUT)",
               {poll_fx("other-out", 3, 0xFF, guide_loop(0xC000, kCount, {0x57, 0x3E, 0x05, 0xD3, 0x57, 0x7A}))}, Poll::Refused);

    // RPB-33: an OUT (C),r that is not the select: through the shadow BC the pass writes to the sprite attribute
    // port 0x57 (EXX; LD BC,$0057; LD A,5; OUT (C),A; EXX), a write whose index advance a skipped pass would
    // not make. Only the select of NR 0x1E / 0x1F is allowed.
    check_poll("RPB-33", "guide loop that also does OUT (C),A to the sprite attribute port 0x57 through BC': identical per frame, REFUSED (an OUT (C),r that is not the select)",
               {poll_fx("out-c-other", 3, 0xFF, guide_loop(0xC000, kCount, {0x57, 0xD9, 0x01, 0x57, 0x00, 0x3E, 0x05, 0xED, 0x79, 0xD9, 0x7A}))}, Poll::Refused);

    // RPB-34: the machine edited from outside between two slots of a watched pass (the debugger paused it mid-frame,
    // and the user sets PC, or patches the loop's own code, and resumes): the pass watched before the edit is not
    // the pass that runs after it, so run_frame() drops it. Two stops per case, identical per frame with the batch on.
    {
        auto edited = [](const char* name, std::function<void(Emulator&)> edit) {
            Scenario s = poll_fx(name, 3, 0xFF, guide_loop(), 40);
            auto stops = std::make_shared<int>(0);
            s.setup = [=, base = s.setup](Emulator& e) { base(e); *stops = 0; };
            s.before_frame = [=](Emulator& e, int f) {
                if (f < 10 || f > 30 || (f % 2) != 0) return;
                e.debug_state().set_persistent_breakpoints(true);
                e.debug_state().run_to_cycle(e.clock().get() + 300000 + static_cast<uint64_t>(f) * 37);
                e.run_frame();                       // stops mid-frame
                if (e.debug_state().paused()) ++*stops;
                edit(e);
                e.debug_state().resume();
                e.debug_state().set_persistent_breakpoints(false);
            };
            s.alive = [=](Emulator& e) { return *stops > 0; };
            return s;
        };
        check_poll("RPB-34", "Set PC back to the loop head, and a patch of the loop's JR offset, while paused mid-pass: identical per frame, engaged on the other frames",
                   {edited("host-set-pc", [](Emulator& e) {
                        Z80Registers r = e.cpu().get_registers();
                        if (r.PC > 0xC001 && r.PC < 0xC00F) { r.PC = 0xC001; e.cpu().set_registers(r); }
                    }),
                    edited("host-patch-code", [](Emulator& e) {
                        e.mmu().write(0xC00E, e.mmu().peek(0xC00E) == 0xF2 ? 0x00 : 0xF2);
                    })});
    }

    // RPB-23: the CPU must be in the state it started the pass in: DJNZ moves B every pass.
    check_poll("RPB-23", "DJNZ-bounded poll (B changes every pass): identical per frame, REFUSED (the state differs)",
               {poll_fx("djnz", 3, 0xFF, djnz_loop())}, Poll::Refused);

    // RPB-24: LD A,R reads the refresh counter, which a skipped pass would not have advanced as far.
    check_poll("RPB-24", "guide loop with LD A,R in the pass: identical per frame, REFUSED (R is read)",
               {poll_fx("ld-a-r", 3, 0xFF, guide_loop(0xC000, kCount, {0x57, 0xED, 0x5F, 0x7A}))}, Poll::Refused);

    // RPB-25: every IN of the pass must be a read of NR 0x1E / 0x1F: here an IN A,(n) of the AY register
    // port 0xFFFD (A = 0xFF on the high byte) is not.
    check_poll("RPB-25", "guide loop that also reads the AY port 0xFFFD with IN A,(n): identical per frame, REFUSED (an IN that is no raster read)",
               {poll_fx("ay-port-in", 3, 0xFF, guide_loop(0xC000, kCount, {0x57, 0x3E, 0xFF, 0xDB, 0xFD, 0x7A}))}, Poll::Refused);

    // RPB-26: the esxDOS shim and the NMI source react to the address of a fetch (0x3Dxx is a DivMMC automap
    // entry): a pass with one M1 on such an address is refused, whatever the DivMMC's state.
    // The second variant puts the ED prefix of the wait's IN at 0x04C5 and its second M1 at 0x04C6, a tape trap
    // address: only the second M1 of a prefixed instruction is a candidate, so it pins that half of the test.
    check_poll("RPB-26", "guide loop whose wait crosses 0x3D00 (RAM over the ROM slot), and one whose IN A,(C) has its second M1 on 0x04C6 (RAM over slot 0): identical per frame, REFUSED (an automap address)",
               {poll_fx("automap-address", 3, 0xFF, guide_loop(0x3CF8), 30, 0x3CF8, [](Emulator& e) {
                    nr(e, 0x51, 0x10);
                    poke(e, 0x3CF8, guide_loop(0x3CF8));
                }),
                poll_fx("automap-second-m1", 3, 0xFF, guide_loop(0x04BC), 30, 0x04BC, [](Emulator& e) {
                    nr(e, 0x50, 0x12);
                    poke(e, 0x04BC, guide_loop(0x04BC));
                })}, Poll::Refused);

    // RPB-28: D1 - a read routine called from the loop (PUSH, POP, CALL, RET) is not batched.
    check_poll("RPB-28", "z88dk-style loop calling a register-read routine: identical per frame, REFUSED (stack instructions)",
               {poll_fx("call-readreg", 3, 0xFF, call_loop())}, Poll::Refused);

    // RPB-29: every per-instruction observer refuses (G2, G4): the trace the rewind buffer turns on, a
    // persistent-breakpoint machine, a tape playing in real time.
    {
        Scenario rew = poll_fx("observer-rewind", 3, 0xFF, guide_loop());
        rew.config = [](EmulatorConfig& c) { c.rewind_buffer_frames = 4; };
        Scenario brk = poll_fx("observer-breakpoints", 3, 0xFF, guide_loop(), 30, 0xC000,
                               [](Emulator& e) { e.debug_state().set_persistent_breakpoints(true); });
        const std::string tap = write_tap("rpb29");
        Scenario tp = poll_fx("tape-playing", 3, 0xFF, guide_loop(), 30, 0xC000, [=](Emulator& e) {
            e.load_tap(tap, /*fast_load=*/false);
            e.tape().start_realtime_playback();
        });
        check_poll("RPB-29", "the rewind buffer (trace), persistent breakpoints, a tape playing in real time: identical per frame, REFUSED (a per-instruction observer)",
                   {rew, brk, tp}, Poll::Refused);
        std::remove(tap.c_str());
    }

    // RPB-30: an NR 0x07 write just before the loop leaves a speed commit pending (zxnext.vhd:5796-5828): the
    // batch waits for it, and engages after.
    check_poll("RPB-30", "NEXTREG 0x07 = 28 MHz just before the guide loop (a pending speed commit): identical per frame, engaged afterwards",
               {poll_fx("speed-commit", 0, 0xFF, guide_loop(0xC000, kCount, {}, 0, {0xED, 0x91, 0x07, 0x03}))});

    // RPB-31: the Pi's audio input is latched at each output-sample boundary, which the replay must still
    // reach: a live stream consumed across batched windows.
    {
        std::vector<std::unique_ptr<PiAudio>> rings;
        Scenario s = poll_fx("pi-audio-stream", 3, 0xFF, preselected_loop(0xB4, true));
        s.config = [&rings](EmulatorConfig& c) {
            rings.push_back(std::make_unique<PiAudio>());
            PiAudio& pa = *rings.back();
            for (int i = 0; i < 12000; ++i) {
                const int16_t v = static_cast<int16_t>(i * 3 - 18000);
                pa.push_for_test(v, static_cast<int16_t>(-v));
            }
            c.pi_audio = &pa;
        };
        s.alive = [](Emulator& e) {
            return e.mmu().peek(kCount) >= 5 && e.config().pi_audio && e.config().pi_audio->available() == 0;
        };
        check_poll("RPB-31", "a live Pi audio stream across batched windows: identical per frame (the I2S latch included), engaged",
                   {s});
    }

    // RPB-32: a debugger step between two slots of a watched pass (the frame is stopped half-way, one
    // instruction is stepped, the machine resumed): the step is a slot the batch never saw, so what it
    // watched is dropped, and the run stays identical.
    {
        Scenario s = poll_fx("step-mid-observation", 3, 0xFF, guide_loop(), 40);
        auto stops = std::make_shared<int>(0);
        s.setup = [=, base = s.setup](Emulator& e) { base(e); *stops = 0; };
        s.before_frame = [=](Emulator& e, int f) {
            if (f != 10 && f != 20 && f != 30) return;
            e.debug_state().set_persistent_breakpoints(true);
            e.debug_state().run_to_cycle(e.clock().get() + 300000 + static_cast<uint64_t>(f) * 37);
            e.run_frame();                       // stops mid-frame
            if (e.debug_state().paused()) ++*stops;
            e.debugger_step();
            e.debug_state().resume();
            e.debug_state().set_persistent_breakpoints(false);
        };
        s.alive = [=](Emulator& e) { return *stops == 3 && e.mmu().peek(kCount) >= 10; };
        check_poll("RPB-32", "a debugger step in the middle of a watched pass, three times: identical per frame, engaged on the other frames",
                   {s});
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
