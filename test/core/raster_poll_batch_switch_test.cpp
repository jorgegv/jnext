// GH #496 - the raster-poll batch's switch: one member (Emulator::batch_poll_enabled_) that the test setter
// writes, that the NR 0x1E/0x1F read handlers honour, that the HALT batch's and the Copper skip's switches
// do not touch and that a reset or a state load leaves alone (it is configuration, never machine state).
// See raster_poll_fixtures.h for the method.
#include "raster_poll_fixtures.h"

#include "core/saveable.h"

#include <memory>

namespace {

Scenario copper_poll(const char* name) {
    return poll_fx(name, 3, 0xFF, guide_loop(), 20, 0xC000, [](Emulator& e) {
        paint_paper(e);
        cu_program(e, kPaletteList);
        cu_nr(e, 0x62, 0xC0);
    });
}

} // namespace

int main() {
    std::printf("GH #496 raster-poll batch switch tests\n");
    std::printf("===============================================\n\n");

    // RPB-52: the setter is a thin layer over the one member: the getter reads back what each of its
    // arguments wrote, the default of the third is ON, and the member decides whether a run engages.
    {
        Emulator e;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        const bool init_ok = e.init(cfg);
        const bool dflt = e.batch_poll_enabled();
        e.set_cycle_batching_for_test(true, false, false);
        const bool off = !e.batch_poll_enabled();
        e.set_cycle_batching_for_test(false, true, true);
        const bool on = e.batch_poll_enabled();
        e.set_cycle_batching_for_test(false, false);
        const bool dflt3 = e.batch_poll_enabled();
        const Scenario s = poll_fx("switch", 3, 0xFF, guide_loop(), 20);
        const Run r_off = batch_ab::run(s, false, false, g_scratch, 0);
        const Run r_on = batch_ab::run(s, false, false, g_scratch, 1);
        check("RPB-52", "batch_poll_enabled() reads back each value the setter wrote (the third argument defaults to ON); a run engages with it ON and never with it OFF",
              init_ok && dflt && off && on && dflt3 && r_off.poll_batched == 0 && r_on.poll_batched > 0 &&
              batch_ab::first_diff(r_off, r_on).empty(),
              "dflt=" + std::to_string(dflt) + " off=" + std::to_string(off) + " on=" + std::to_string(on) +
              " poll_off=" + std::to_string(r_off.poll_batched) + " poll_on=" + std::to_string(r_on.poll_batched));
    }

    // RPB-53: each of the three switches (HALT batch, Copper skip, poll batch) turns only its own path:
    // under every one of the 8 combinations the run is identical to the all-off one, the poll batch engages
    // exactly when its switch is on, and the Copper skip exactly when its is.
    {
        std::string detail;
        bool ok = true;
        const std::vector<Scenario> list = {
            poll_fx("combo-guide", 3, 0xFF, guide_loop(), 20),
            poll_fx("combo-audio", 3, 0xFF, preselected_loop(0xB4, true, 1), 20),
            copper_poll("combo-copper"),
        };
        for (const Scenario& sc : list) {
            const Run oracle = batch_ab::run(sc, false, false, g_scratch, 0);
            for (int combo = 1; combo < 8; ++combo) {
                const bool halt = combo & 1, copper = combo & 2, poll = combo & 4;
                const Run r = batch_ab::run(sc, halt, copper, g_scratch, poll ? 1 : 0);
                const std::string d = batch_ab::first_diff(oracle, r);
                const bool copper_fx = std::string(sc.name) == "combo-copper";
                if (!d.empty() || (r.poll_batched != 0) != poll || (copper_fx && (r.copper_skipped != 0) != copper) ||
                    (!copper_fx && r.copper_skipped != 0 && !copper)) {
                    ok = false;
                    detail += std::string(sc.name) + " halt=" + std::to_string(halt) + " copper=" + std::to_string(copper) +
                              " poll=" + std::to_string(poll) + ": " + (d.empty() ? "counters" : d) + "; ";
                }
            }
        }
        check("RPB-53", "the guide, audio and Copper fixtures under all 8 (HALT, Copper, poll) switch combinations: identical to the all-off run, the poll batch engaged exactly when its switch is on, the Copper skip exactly when its is",
              ok, detail);
    }

    // RPB-54: the switch is configuration: a soft reset, a re-init and a state load leave it as set.
    {
        Emulator e;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        bool ok = e.init(cfg);
        e.set_cycle_batching_for_test(true, true, false);
        for (int i = 0; i < 3; ++i) e.run_frame();
        std::vector<uint8_t> buf(8u << 20);
        StateWriter w(buf.data(), buf.size());
        e.save_state(w);
        const size_t n = w.position();
        e.soft_reset();
        const bool after_soft = !e.batch_poll_enabled();
        ok = ok && e.init(cfg, /*preserve_memory=*/true);
        const bool after_init = !e.batch_poll_enabled();
        StateReader r(buf.data(), n);
        ok = ok && e.load_state(r);
        const bool after_load = !e.batch_poll_enabled();
        e.set_cycle_batching_for_test(true, true, true);
        e.soft_reset();
        const bool on_after_soft = e.batch_poll_enabled();
        check("RPB-54", "the poll switch (OFF, then ON) is unchanged by a soft reset, a re-init and a state load",
              ok && after_soft && after_init && after_load && on_after_soft,
              "soft=" + std::to_string(after_soft) + " init=" + std::to_string(after_init) +
              " load=" + std::to_string(after_load) + " on_soft=" + std::to_string(on_after_soft));
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
