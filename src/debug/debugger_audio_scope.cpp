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
    if (on && !audio_scope_derived) scope_tap = std::make_unique<AudioSourceTap>();
    if (!on && audio_scope_derived) scope_tap.reset();
    if (on != audio_scope_derived || force || emu.audio_scope_tap() != (on ? scope_tap.get() : nullptr))
        emu.set_audio_scope_tap(on ? scope_tap.get() : nullptr);
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
