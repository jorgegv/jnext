#include "audio/beeper.h"
#include "audio/dac.h"
#include "audio/mixer.h"
#include "audio/turbosound.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "platform/emulator_boot.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

int passed = 0;
int failed = 0;

void check(const char* name, bool condition)
{
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("FAIL: %s\n", name);
    }
}

int16_t ear_sample(float gain_db)
{
    Beeper beeper;
    TurboSound turbosound;
    Dac dac;
    Mixer mixer;
    beeper.set_ear(true);
    mixer.set_output_gain_db(gain_db);
    mixer.generate_sample(beeper, turbosound, dac);
    int16_t samples[2]{};
    mixer.read_samples(samples, 1);
    return samples[0];
}

void transfer_test()
{
    Mixer mixer;
    check("default gain is 0 dB", mixer.output_gain_db() == 0.0f);
    check("0 dB preserves the existing PCM value", ear_sample(0.0f) == 2048);
    check("+6.0206 dB doubles PCM",
          std::abs(static_cast<int>(ear_sample(6.0206f)) - 4096) <= 1);
    check("-6.0206 dB halves PCM",
          std::abs(static_cast<int>(ear_sample(-6.0206f)) - 1024) <= 1);

    Beeper beeper;
    TurboSound turbosound;
    Dac dac;
    beeper.set_ear(true);
    beeper.set_mic(true);
    dac.write_channel(0, 0xFF);
    dac.write_channel(1, 0xFF);
    mixer.set_output_gain_db(24.0f);
    mixer.generate_sample(beeper, turbosound, dac);
    int16_t samples[2]{};
    mixer.read_samples(samples, 1);
    check("positive overflow saturates", samples[0] == 32767);

    Beeper negative_beeper;
    TurboSound negative_turbosound;
    Dac negative_dac;
    negative_dac.write_channel(0, 0x00);
    negative_dac.write_channel(1, 0x00);
    mixer.generate_sample(negative_beeper, negative_turbosound, negative_dac);
    mixer.read_samples(samples, 1);
    check("negative overflow saturates", samples[0] == -32768);

    Beeper silent_beeper;
    TurboSound silent_turbosound;
    Dac silent_dac;
    Mixer silent_mixer;
    silent_mixer.set_output_gain_db(24.0f);
    silent_mixer.generate_sample(silent_beeper, silent_turbosound, silent_dac);
    silent_mixer.read_samples(samples, 1);
    check("gain leaves silence at zero", samples[0] == 0 && samples[1] == 0);
}

void reset_and_cold_boot_test()
{
    Mixer mixer;
    mixer.set_output_gain_db(12.0f);
    mixer.reset();
    check("mixer reset preserves host gain", mixer.output_gain_db() == 12.0f);

    EmulatorConfig cfg;
    cfg.load_file = "direct-load.nex";
    cfg.audio_gain_db = 6.0206f;
    Emulator emulator;
    check("emulator initializes with configured gain", emulator.init(cfg));
    check("configured gain reaches the mixer",
          emulator.mixer().output_gain_db() == cfg.audio_gain_db);

    emulator_cold_boot(emulator, cfg);
    check("configured gain survives a cold boot",
          emulator.mixer().output_gain_db() == cfg.audio_gain_db);
}

}  // namespace

int main()
{
    transfer_test();
    reset_and_cold_boot_test();

    const int total = passed + failed;
    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n",
                total, passed, failed);
    return failed == 0 ? 0 : 1;
}
