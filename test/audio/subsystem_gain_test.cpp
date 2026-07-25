#include "audio/beeper.h"
#include "audio/dac.h"
#include "audio/mixer.h"
#include "audio/turbosound.h"
#include "core/cli_options.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "platform/emulator_boot.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int passed = 0;
int failed = 0;

void check(const char* id, bool condition, const std::string& detail = {})
{
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("FAIL %s", id);
        if (!detail.empty()) std::printf(": %s", detail.c_str());
        std::printf("\n");
    }
}

int16_t sample(Mixer& mixer, const Beeper& beeper,
               const TurboSound& turbosound, const Dac& dac)
{
    mixer.generate_sample(beeper, turbosound, dac);
    int16_t pair[2]{};
    mixer.read_samples(pair, 1);
    return pair[0];
}

void configure_ays(TurboSound& ts, bool distinct = true)
{
    ts.set_enabled(true);
    ts.set_ay_mode(true);
    const uint8_t select[3] = {0xFF, 0xFE, 0xFD};
    const uint8_t volume[3] = {0x0F, 0x09, 0x05}; // outputs 0xFF, 0x41, 0x0F
    for (int chip = 0; chip < 3; ++chip) {
        ts.reg_addr(select[chip]);   // select chip, pan both channels
        ts.reg_addr(7);
        ts.reg_write(0x3F);         // tone/noise disabled: constant high
        ts.reg_addr(8);
        ts.reg_write(distinct ? volume[chip] : 0x0F);
    }
    for (int i = 0; i < 16; ++i) ts.tick();
}

void transfer_tests()
{
    Mixer defaults;
    check("SG-01", defaults.output_gain_db() == 0.0f &&
                   defaults.beeper_gain_db() == 0.0f &&
                   defaults.ay_gain_db(0) == 0.0f &&
                   defaults.ay_gain_db(1) == 0.0f &&
                   defaults.ay_gain_db(2) == 0.0f &&
                   defaults.dac_gain_db() == 0.0f);

    Beeper beeper;
    TurboSound silent_ts;
    Dac silent_dac;
    beeper.set_ear(true);
    Mixer beeper_base;
    Mixer beeper_gain;
    beeper_gain.set_beeper_gain_db(6.0206f);
    check("SG-02", sample(beeper_base, beeper, silent_ts, silent_dac) == 2048);
    check("SG-03", std::abs(sample(beeper_gain, beeper, silent_ts, silent_dac) -
                            4096) <= 1);

    TurboSound ts;
    configure_ays(ts);
    Beeper quiet;
    Mixer ay_base;
    const int base = sample(ay_base, quiet, ts, silent_dac);
    const int chip_contribution[3] = {0xFF, 0x41, 0x0F};
    const int expected_base = (0xFF + 0x41 + 0x0F) * 4;
    for (int chip = 0; chip < 3; ++chip) {
        Mixer gained;
        gained.set_ay_gain_db(chip, 6.0206f);
        const int actual = sample(gained, quiet, ts, silent_dac);
        const int expected = expected_base + chip_contribution[chip] * 4;
        check(chip == 0 ? "SG-04" : chip == 1 ? "SG-05" : "SG-06",
              base == expected_base && std::abs(actual - expected) <= 1,
              "actual=" + std::to_string(actual) +
              " expected=" + std::to_string(expected));
    }

    const uint16_t hardware_l = ts.pcm_left();
    Mixer host_only;
    host_only.set_ay_gain_db(0, 12.0f);
    (void)sample(host_only, quiet, ts, silent_dac);
    check("SG-07", ts.pcm_left() == hardware_l);

    Mixer ay_clip;
    for (int chip = 0; chip < 3; ++chip) ay_clip.set_ay_gain_db(chip, 24.0f);
    TurboSound loud;
    configure_ays(loud, false);
    check("SG-08", sample(ay_clip, quiet, loud, silent_dac) == 32767);

    Dac max_dac;
    max_dac.write_channel(0, 0xFF);
    max_dac.write_channel(1, 0xFF);
    Mixer dac_base;
    Mixer dac_gain;
    dac_gain.set_dac_gain_db(6.0206f);
    check("SG-09", sample(dac_base, quiet, silent_ts, max_dac) == 4064);
    check("SG-10", std::abs(sample(dac_gain, quiet, silent_ts, max_dac) -
                            8128) <= 1);

    Dac min_dac;
    min_dac.write_channel(0, 0x00);
    min_dac.write_channel(1, 0x00);
    Mixer dac_negative;
    dac_negative.set_dac_gain_db(6.0206f);
    check("SG-11", std::abs(sample(dac_negative, quiet, silent_ts, min_dac) +
                            8192) <= 1);

    Mixer dac_silence;
    dac_silence.set_dac_gain_db(24.0f);
    check("SG-12", sample(dac_silence, quiet, silent_ts, silent_dac) == 0);

    Mixer composed;
    composed.set_beeper_gain_db(6.0206f);
    composed.set_output_gain_db(6.0206f);
    check("SG-13", std::abs(sample(composed, beeper, silent_ts, silent_dac) -
                            8192) <= 2);

    TurboSound timed;
    configure_ays(timed);
    Mixer timed_gain;
    timed_gain.set_ay_gain_db(0, 6.0206f);
    const int before_pan_write = sample(timed_gain, quiet, timed, silent_dac);
    timed.reg_addr(0x9F); // AY #0 pan=00; hardware applies it on the next tick
    const int pending_pan_write = sample(timed_gain, quiet, timed, silent_dac);
    timed.tick();
    const int after_pan_tick = sample(timed_gain, quiet, timed, silent_dac);
    check("SG-19", timed.chip_pcm_valid() &&
                   before_pan_write == pending_pan_write &&
                   after_pan_tick != pending_pan_write,
          "before=" + std::to_string(before_pan_write) +
          " pending=" + std::to_string(pending_pan_write) +
          " after=" + std::to_string(after_pan_tick));

    TurboSound saved;
    configure_ays(saved);
    saved.reg_addr(0x9F); // leave the new pan pending before the PSG tick
    const uint16_t serialized_pcm = saved.pcm_left();
    StateWriter measure;
    saved.save_state(measure);
    std::vector<uint8_t> state(measure.position());
    StateWriter writer(state.data(), state.size());
    saved.save_state(writer);
    TurboSound restored;
    StateReader reader(state.data(), state.size());
    restored.load_state(reader);
    const bool invalid_after_load = !restored.chip_pcm_valid();
    Mixer restored_gain;
    restored_gain.set_ay_gain_db(0, 6.0206f);
    const int fallback = sample(restored_gain, quiet, restored, silent_dac);
    restored.tick();
    const int rebuilt = sample(restored_gain, quiet, restored, silent_dac);
    check("SG-20", !writer.overflow() && !reader.out_of_bounds() &&
                   invalid_after_load &&
                   restored.pcm_left() != serialized_pcm &&
                   fallback == static_cast<int>(serialized_pcm) * 4 &&
                   restored.chip_pcm_valid() && rebuilt != fallback,
          "serialized=" + std::to_string(serialized_pcm) +
          " fallback=" + std::to_string(fallback) +
          " rebuilt=" + std::to_string(rebuilt));
}

void lifecycle_tests()
{
    Mixer mixer;
    mixer.set_output_gain_db(1.0f);
    mixer.set_beeper_gain_db(2.0f);
    mixer.set_ay_gain_db(0, 3.0f);
    mixer.set_ay_gain_db(1, 4.0f);
    mixer.set_ay_gain_db(2, 5.0f);
    mixer.set_dac_gain_db(6.0f);
    mixer.reset();
    check("SG-14", mixer.output_gain_db() == 1.0f &&
                   mixer.beeper_gain_db() == 2.0f &&
                   mixer.ay_gain_db(0) == 3.0f &&
                   mixer.ay_gain_db(1) == 4.0f &&
                   mixer.ay_gain_db(2) == 5.0f &&
                   mixer.dac_gain_db() == 6.0f);

    EmulatorConfig cfg;
    cfg.audio_gain_db = -1.0f;
    cfg.audio_gain_beeper_db = -2.0f;
    cfg.audio_gain_ay_db[0] = -3.0f;
    cfg.audio_gain_ay_db[1] = -4.0f;
    cfg.audio_gain_ay_db[2] = -5.0f;
    cfg.audio_gain_dac_db = -6.0f;
    Emulator emulator;
    check("SG-15", emulator.init(cfg));
    check("SG-16", emulator.mixer().output_gain_db() == -1.0f &&
                   emulator.mixer().beeper_gain_db() == -2.0f &&
                   emulator.mixer().ay_gain_db(0) == -3.0f &&
                   emulator.mixer().ay_gain_db(1) == -4.0f &&
                   emulator.mixer().ay_gain_db(2) == -5.0f &&
                   emulator.mixer().dac_gain_db() == -6.0f);

    emulator.mixer().set_output_gain_db(1.0f);
    emulator.mixer().set_beeper_gain_db(2.0f);
    emulator.mixer().set_ay_gain_db(0, 3.0f);
    emulator.mixer().set_ay_gain_db(1, 4.0f);
    emulator.mixer().set_ay_gain_db(2, 5.0f);
    emulator.mixer().set_dac_gain_db(6.0f);
    EmulatorConfig seen;
    ColdBootHooks hooks;
    hooks.rewire_host = [&seen](const EmulatorConfig& used) { seen = used; };
    emulator_frontend_cold_boot(emulator, cfg, "", hooks);
    check("SG-17", seen.audio_gain_db == 1.0f &&
                   seen.audio_gain_beeper_db == 2.0f &&
                   seen.audio_gain_ay_db[0] == 3.0f &&
                   seen.audio_gain_ay_db[1] == 4.0f &&
                   seen.audio_gain_ay_db[2] == 5.0f &&
                   seen.audio_gain_dac_db == 6.0f);
    check("SG-18", emulator.mixer().output_gain_db() == 1.0f &&
                   emulator.mixer().beeper_gain_db() == 2.0f &&
                   emulator.mixer().ay_gain_db(0) == 3.0f &&
                   emulator.mixer().ay_gain_db(1) == 4.0f &&
                   emulator.mixer().ay_gain_db(2) == 5.0f &&
                   emulator.mixer().dac_gain_db() == 6.0f);
}

void cli_assignment_tests()
{
    struct GainCase {
        const char* id;
        cli::OptId option;
        int slot;
    };
    const GainCase cases[] = {
        { "SG-21", cli::OptId::AudioGainDb,       0 },
        { "SG-22", cli::OptId::AudioGainBeeperDb, 1 },
        { "SG-23", cli::OptId::AudioGainAy0Db,    2 },
        { "SG-24", cli::OptId::AudioGainAy1Db,    3 },
        { "SG-25", cli::OptId::AudioGainAy2Db,    4 },
        { "SG-26", cli::OptId::AudioGainDacDb,    5 },
    };
    for (const GainCase& c : cases) {
        cli::AudioGainArgs gains;
        const float value = static_cast<float>(c.slot + 1);
        const bool assigned = cli::assign_audio_gain(c.option, value, gains);
        const float values[] = {
            gains.master, gains.beeper,
            gains.ay[0], gains.ay[1], gains.ay[2], gains.dac
        };
        const bool set[] = {
            gains.master_set, gains.beeper_set,
            gains.ay_set[0], gains.ay_set[1], gains.ay_set[2], gains.dac_set
        };
        bool exact = assigned;
        for (int slot = 0; slot < 6; ++slot) {
            exact = exact &&
                values[slot] == (slot == c.slot ? value : 0.0f) &&
                set[slot] == (slot == c.slot);
        }
        check(c.id, exact);
    }
}

} // namespace

int main()
{
    transfer_tests();
    lifecycle_tests();
    cli_assignment_tests();
    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n",
                passed + failed, passed, failed);
    return failed == 0 ? 0 : 1;
}
