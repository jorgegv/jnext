#include "debug/stem_recorder.h"

#include <filesystem>
#include <system_error>

namespace jnext {
namespace dbg {

const char* const StemRecorder::NAMES[StemRecorder::FILES] = {
    "ay0.wav", "ay1.wav", "ay2.wav", "dac.wav", "beeper.wav", "pi.wav", "mix.wav"};

static_assert(AudioStemTap::FRAME == 2 * StemRecorder::FILES, "one stereo pair per file");

StemRecorder::StemRecorder()
{
    tap.out = &StemRecorder::sink_;
    tap.ctx = this;
}

void StemRecorder::sink_(void* self, const int16_t* frame)
{
    StemRecorder* const r = static_cast<StemRecorder*>(self);
    for (size_t i = 0; i < FILES; ++i) r->rec_[i].capture(frame + 2 * i, 1);
}

bool StemRecorder::start(const std::string& dir, std::string& why)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(dir), ec);
    if (ec || !fs::is_directory(fs::path(dir), ec)) {
        why = "cannot create the directory '" + dir + "'";
        return false;
    }
    for (size_t i = 0; i < FILES; ++i) {
        const std::string path = (fs::path(dir) / NAMES[i]).string();
        if (!rec_[i].start(path)) {
            why = rec_[i].last_error().empty() ? "cannot create '" + path + "'" : rec_[i].last_error();
            // Close and remove the files this call made; a name that is not a regular file (a directory in the
            // way, which is why the open failed) is never touched.
            for (size_t j = 0; j <= i; ++j) {
                rec_[j].stop();
                const fs::path made = fs::path(dir) / NAMES[j];
                if (fs::is_regular_file(made, ec)) fs::remove(made, ec);
            }
            return false;
        }
    }
    dir_ = dir;
    tap.written = 0;
    recording_ = true;
    return true;
}

bool StemRecorder::stop(std::string& why)
{
    bool ok = true;
    for (size_t i = 0; i < FILES; ++i) {
        if (!rec_[i].stop()) {
            ok = false;
            why = rec_[i].last_error().empty() ? std::string("write error in ") + NAMES[i] : rec_[i].last_error();
        }
    }
    recording_ = false;
    return ok;
}

}  // namespace dbg
}  // namespace jnext
