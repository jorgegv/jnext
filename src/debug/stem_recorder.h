// GH #539 -- the per-source WAV stems of INS-24: seven stereo WAV files fed by the mixer source tap's
// stem accumulators. INTERNAL to the debug backend (not a published header): it owns the AudioStemTap
// the Emulator's tap points at and one AudioRecorder per file. Standard headers plus audio/ only.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "audio/audio_recorder.h"
#include "audio/audio_source_tap.h"

namespace jnext {
namespace dbg {

class StemRecorder {
public:
    static constexpr size_t FILES = 7;
    /// File names, in AudioStemTap frame order: ay0 ay1 ay2 dac beeper pi mix.
    static const char* const NAMES[FILES];

    StemRecorder();
    ~StemRecorder() = default;   // each AudioRecorder finalises its own file
    StemRecorder(const StemRecorder&) = delete;
    StemRecorder& operator=(const StemRecorder&) = delete;

    /// Create `dir` (and its parents) and open the seven files, overwriting files of those names.
    /// On any failure the files this call created are closed and removed, and `why` says what.
    bool start(const std::string& dir, std::string& why);
    /// Finalise every file. False when any recorder failed (the files are closed either way).
    bool stop(std::string& why);
    bool recording() const { return recording_; }

    AudioStemTap tap;

private:
    static void sink_(void* self, const int16_t* frame);

    std::array<AudioRecorder, FILES> rec_;
    std::string dir_;
    bool recording_ = false;
};

}  // namespace dbg
}  // namespace jnext
