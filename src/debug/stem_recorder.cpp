#include "debug/stem_recorder.h"

#include <cstdio>
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
    // A refused start changes nothing: every name that already exists must be a regular file that can be
    // opened for writing, checked WITHOUT truncating (a probe open for update), before any file is touched.
    bool existed[FILES];
    for (size_t i = 0; i < FILES; ++i) {
        const fs::path p = fs::path(dir) / NAMES[i];
        existed[i] = fs::exists(fs::symlink_status(p, ec));
        if (!existed[i]) continue;
        std::FILE* probe = fs::is_regular_file(p, ec) ? std::fopen(p.string().c_str(), "rb+") : nullptr;
        if (!probe) {
            why = "cannot write '" + p.string() + "'";
            return false;
        }
        std::fclose(probe);
    }
    // A name that is missing must be creatable (a read-only directory): create the FIRST missing stem name itself, so a refused
    // start has not truncated an existing file before the first create fails. No foreign name is touched; the empty file is one
    // this call made (existed[] is false), start truncates it next, and any later refusal takes it back.
    for (size_t i = 0; i < FILES; ++i) {
        if (existed[i]) continue;
        const std::string path = (fs::path(dir) / NAMES[i]).string();
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            why = "cannot create '" + path + "'";
            return false;
        }
        std::fclose(f);
        break;
    }
    for (size_t i = 0; i < FILES; ++i) {
        const std::string path = (fs::path(dir) / NAMES[i]).string();
        if (!rec_[i].start(path)) {
            why = rec_[i].last_error().empty() ? "cannot create '" + path + "'" : rec_[i].last_error();
            // Take back only what this call created (the empty file of the create probe included): a file that was there before is left as it is (the
            // probes above make reaching this a race), and so is anything that is not a regular file.
            for (size_t j = 0; j < FILES; ++j) {
                if (j <= i) rec_[j].stop();
                const fs::path made = fs::path(dir) / NAMES[j];
                if (!existed[j] && fs::is_regular_file(made, ec)) fs::remove(made, ec);
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
