// ---------------------------------------------------------------------------
// jnext::dbg::Debugger -- INS-24, the mixer source tap (GH #536).
//
// The tap itself (`AudioSourceTap`, audio/audio_source_tap.h) is fed by
// Mixer::integrate_span_tapped() while the Emulator holds a pointer to it. This file owns
// its lifetime and its readers:
//
//   * a client's request is a flag on ITS row (`Client::audio_scope`), so a reconstruct keeps
//     it and a detach drops it, exactly as `set_call_stack_enabled`;
//   * `sync_audio_scope()` derives the machine's state from every live row (observers
//     included: a window is an observer, and it is the first asker) and from `--silent`,
//     and is the one writer of the install;
//   * the ring is `Impl`'s, never the machine's: a cold boot rebuilds the `Emulator` and the
//     backend re-installs the same tap.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include <algorithm>

#include "core/log.h"

namespace jnext {
namespace dbg {

static_assert(AudioSourceTap::SOURCES == AUDIO_SOURCE_COUNT,
              "the tap's source count is the published one");

void Debugger::Impl::sync_audio_scope(bool force) {
    bool asked = false;
    for (const auto& c : clients) {
        if (c.detached) continue;
        asked = asked || c.audio_scope;
    }
    // --silent skips the PSG and the mixer altogether (`Emulator::audio_off_`): nothing to tap.
    const bool on = asked && !emu.config().silent;
    // GH #539: the tap is installed for a scope request or a stem recording (which `--silent` refuses to start).
    const bool recording = stems && stems->recording();
    const bool install = on || recording;
    const bool installed = scope_tap != nullptr;
    if (install && !installed) scope_tap = std::make_unique<AudioSourceTap>();
    if (!install && installed) scope_tap.reset();
    AudioSourceTap* const want = install ? scope_tap.get() : nullptr;
    AudioStemTap* const want_stems = recording ? &stems->tap : nullptr;
    if (scope_tap) scope_tap->stems = want_stems;
    if (install != installed || force || emu.audio_scope_tap() != want ||
        (want && emu.turbosound().hw_chip_latch() != (want_stems != nullptr)))
        emu.set_audio_scope_tap(want);
    audio_scope_derived = on;
}

Result Debugger::set_audio_scope_enabled(ClientId by, bool enabled) {
    Impl::Client* c = impl_->find_client(by);
    if (!c || impl_->emu.config().silent) return Result::RefusedUnavailable;
    c->audio_scope = enabled;
    impl_->sync_audio_scope();
    impl_->bump_revision();
    return Result::Ok;
}

bool Debugger::audio_scope_enabled() const {
    return impl_->audio_scope_derived;
}

Result Debugger::stems_record_start(ClientId by, const std::string& dir) {
    (void)by;   // attribution only: a recording is not a view request
    Impl& im = *impl_;
    if (im.emu.config().silent || (im.stems && im.stems->recording())) return Result::RefusedUnavailable;
    auto rec = std::make_unique<StemRecorder>();
    std::string why;
    if (!rec->start(dir, why)) {
        Log::debugger()->error("Stem recording: {}", why);
        return Result::RefusedUnavailable;
    }
    im.stems = std::move(rec);
    im.sync_audio_scope();
    im.bump_revision();
    return Result::Ok;
}

Result Debugger::stems_record_stop(ClientId by) {
    (void)by;
    Impl& im = *impl_;
    if (!im.stems || !im.stems->recording()) return Result::RefusedUnavailable;
    // Detach first: the tap stops feeding the recorder before the files are finalised.
    std::unique_ptr<StemRecorder> rec = std::move(im.stems);
    im.sync_audio_scope();
    std::string why;
    const bool ok = rec->stop(why);
    if (!ok) Log::debugger()->error("Stem recording: {}", why);
    im.bump_revision();
    return ok ? Result::Ok : Result::RefusedUnavailable;
}

bool Debugger::stems_recording() const {
    return impl_->stems && impl_->stems->recording();
}

Expected<AudioScopeBlock> Debugger::audio_scope_samples(size_t count) const {
    const AudioSourceTap* tap = impl_->audio_scope_derived ? impl_->scope_tap.get() : nullptr;
    if (!tap) return make_refused<AudioScopeBlock>(Result::RefusedUnavailable);

    AudioScopeBlock b;
    b.sample_rate = 44100;
    const size_t n = static_cast<size_t>(std::min<uint64_t>(std::min<uint64_t>(count, tap->written),
                                                            AudioSourceTap::RING));
    b.first_sample = tap->written - n;
    for (size_t s = 0; s < AUDIO_SOURCE_COUNT; ++s) {
        std::vector<int16_t>& out = b.samples[s];
        out.resize(n);
        const int16_t* ring = tap->ring.data() + s * AudioSourceTap::RING;
        for (size_t i = 0; i < n; ++i)
            out[i] = ring[static_cast<size_t>((b.first_sample + i) % AudioSourceTap::RING)];
    }
    return make_ok(std::move(b));
}

} // namespace dbg
} // namespace jnext
