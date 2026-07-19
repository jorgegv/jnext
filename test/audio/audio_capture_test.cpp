#include "audio/audio_recorder.h"
#include "audio/dac_trace_recorder.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "platform/emulator_boot.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

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

std::filesystem::path temp_file(const char* label, const char* extension)
{
    const auto stamp = std::chrono::high_resolution_clock::now()
                           .time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("jnext-") + label + "-" + std::to_string(stamp) + extension);
}

std::vector<uint8_t> read_binary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

uint16_t u16_le(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) |
           (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t u32_le(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

void wav_format_test()
{
    const auto path = temp_file("audio-format", ".wav");
    AudioRecorder recorder;
    const int16_t samples[] = {
        static_cast<int16_t>(0x1234), static_cast<int16_t>(-2),
        static_cast<int16_t>(-32768), static_cast<int16_t>(32767),
        0, 1,
    };

    check("WAV rejects a zero sample rate", !recorder.start(path.string(), 0));
    check("WAV starts and rejects a second start",
          recorder.start(path.string()) && !recorder.start(path.string()));
    recorder.capture(samples, 3);
    check("WAV finalizes", recorder.stop());

    const auto bytes = read_binary(path);
    check("WAV file size", bytes.size() == 56);
    const bool complete = bytes.size() >= 56;
    check("WAV container IDs", complete &&
          std::string(bytes.begin(), bytes.begin() + 4) == "RIFF" &&
          std::string(bytes.begin() + 8, bytes.begin() + 12) == "WAVE" &&
          std::string(bytes.begin() + 12, bytes.begin() + 16) == "fmt " &&
          std::string(bytes.begin() + 36, bytes.begin() + 40) == "data");
    check("WAV PCM header", complete &&
          u32_le(bytes, 4) == 48 && u32_le(bytes, 16) == 16 &&
          u16_le(bytes, 20) == 1 && u16_le(bytes, 22) == 2 &&
          u32_le(bytes, 24) == 44100 && u32_le(bytes, 28) == 176400 &&
          u16_le(bytes, 32) == 4 && u16_le(bytes, 34) == 16 &&
          u32_le(bytes, 40) == 12);
    const std::vector<uint8_t> expected = {
        0x34, 0x12, 0xFE, 0xFF,
        0x00, 0x80, 0xFF, 0x7F,
        0x00, 0x00, 0x01, 0x00,
    };
    check("WAV little-endian PCM", complete &&
          std::vector<uint8_t>(bytes.begin() + 44, bytes.end()) == expected);
    std::filesystem::remove(path);
}

void dac_callback_mapping_test()
{
    Dac dac;
    std::vector<std::pair<int, uint8_t>> writes;
    dac.set_write_callback([&writes](int channel, uint8_t value) {
        writes.emplace_back(channel, value);
    });

    dac.write_mono(10);
    dac.write_left(20);
    dac.write_right(30);
    dac.write_channel(-1, 40);
    dac.write_channel(4, 50);

    const std::vector<std::pair<int, uint8_t>> expected = {
        {0, 10}, {3, 10}, {1, 20}, {2, 30},
    };
    check("DAC callback reports physical mirror channels", writes == expected);
}

void mixer_callback_fanout_test()
{
    Beeper beeper;
    TurboSound turbosound;
    Dac dac;
    Mixer mixer;
    int video_samples = 0;
    int wav_samples = 0;
    int16_t video_pair[2]{};
    int16_t wav_pair[2]{};
    mixer.set_record_callback([&](const int16_t* samples, int count) {
        video_samples += count;
        video_pair[0] = samples[0];
        video_pair[1] = samples[1];
    });
    mixer.set_capture_callback([&](const int16_t* samples, int count) {
        wav_samples += count;
        wav_pair[0] = samples[0];
        wav_pair[1] = samples[1];
    });

    dac.write_channel(0, 0xFF);
    mixer.generate_sample(beeper, turbosound, dac);
    check("WAV and video callbacks receive the same mixed sample",
          video_samples == 1 && wav_samples == 1 &&
          video_pair[0] == wav_pair[0] && video_pair[1] == wav_pair[1]);
}

void cold_boot_continuity_test()
{
    const auto path = temp_file("audio-cold-boot", ".wav");
    AudioRecorder recorder;
    check("cold-boot WAV starts", recorder.start(path.string()));

    EmulatorConfig cfg;
    cfg.load_file = "direct-load.nex";
    int dac_writes = 0;
    cfg.audio_capture_callback = [&recorder](const int16_t* samples, int count) {
        recorder.capture(samples, count);
    };
    cfg.dac_write_callback = [&dac_writes](uint64_t, int, uint8_t) {
        ++dac_writes;
    };

    Emulator emulator;
    check("initial emulator init", emulator.init(cfg));
    emulator.dac().write_channel(0, 0xFF);
    emulator.mixer().generate_sample(emulator.beeper(), emulator.turbosound(),
                                     emulator.dac());

    emulator_cold_boot(emulator, cfg);
    emulator.dac().write_channel(2, 0x00);
    emulator.mixer().generate_sample(emulator.beeper(), emulator.turbosound(),
                                     emulator.dac());

    emulator.mixer().set_capture_callback({});
    check("cold-boot WAV finalizes", recorder.stop());
    const auto bytes = read_binary(path);
    check("WAV spans a cold boot",
          bytes.size() == 52 && bytes.size() >= 44 && u32_le(bytes, 40) == 8);
    check("DAC callback reattaches after a cold boot", dac_writes == 2);
    std::filesystem::remove(path);
}

void dac_trace_format_test()
{
    const auto path = temp_file("dac-trace", ".csv");
    DacTraceRecorder recorder;
    check("DAC trace starts and rejects a second start",
          recorder.start(path.string()) && !recorder.start(path.string()));
    recorder.capture(100, 0, 128);
    recorder.capture(101, 3, 200);
    recorder.capture(5, 1, 7);
    check("DAC trace finalizes", recorder.stop());
    check("DAC trace CSV and cold-boot segment", read_text(path) ==
          "segment,tstate,channel,value\n"
          "0,100,0,128\n"
          "0,101,3,200\n"
          "1,5,1,7\n");
    std::filesystem::remove(path);
}

}  // namespace

int main()
{
    wav_format_test();
    dac_callback_mapping_test();
    mixer_callback_fanout_test();
    cold_boot_continuity_test();
    dac_trace_format_test();

    const int total = passed + failed;
    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n",
                total, passed, failed);
    return failed == 0 ? 0 : 1;
}
