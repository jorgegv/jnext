// GH #531 -- INS-23, the audio write log: the ring, the sink the core's tap
// delivers into, and the six verbs. The ring exists only while some live client
// asks for the log (`sync_audio_log()` is its one writer). `seq` is never reused
// in a run, so a client that fetches by `since_seq` can tell what it missed.

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "debug/audio_log_csv.h"
#include "debug/debugger_impl.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// The ring
// ---------------------------------------------------------------------------

AudioLogEntry& Debugger::Impl::audio_at(size_t i)
{
    return audio_ring[(audio_head + i) % audio_ring.size()];
}

const AudioLogEntry& Debugger::Impl::audio_at(size_t i) const
{
    return audio_ring[(audio_head + i) % audio_ring.size()];
}

void Debugger::Impl::audio_push(const AudioLogEntry& e)
{
    if (audio_ring.empty()) return;
    if (audio_count == audio_ring.size()) {         // full: overwrite the oldest
        audio_ring[audio_head] = e;
        audio_head = (audio_head + 1) % audio_ring.size();
    } else {
        audio_ring[(audio_head + audio_count) % audio_ring.size()] = e;
        ++audio_count;
    }
}

void Debugger::Impl::AudioLogSink::audio_record(const AudioLogEntry& in)
{
    // GH #532: the AudioWrite latch comes BEFORE the ring test, so the event does not depend on the log.
    if (in.marker == AudioMarker::None && impl.events.has_kind(EventKind::AudioWrite))
        impl.latch_audio_write(in);
    // GH #538: the .psg recorder is fed before the ring test too, markers included. The ring's
    // marker dedup below does not apply to it (a repeated marker rebases to the same place).
    if (impl.psg) impl.psg->feed(in);
    if (impl.audio_ring.empty()) return;
    AudioLogEntry e = in;
    // The same marker twice at one cycle (two soft resets with no time between) is one row. A
    // different kind, or the same kind at another cycle, is its own row: a reset right after a
    // rewind landing must show as a reset.
    if (e.marker != AudioMarker::None && impl.audio_count > 0) {
        const AudioLogEntry& last = impl.audio_at(impl.audio_count - 1);
        if (last.marker == e.marker && last.cycle == e.cycle) return;
    }
    e.seq = impl.audio_next_seq++;
    impl.audio_push(e);
}

void Debugger::Impl::AudioLogSink::audio_rewind_landing(const AudioLogEntry& m)
{
    // GH #538: what the abandoned future wrote was played and recorded already, and stays in
    // the file; the landing marker continues it right after the last frame seen.
    if (impl.psg) impl.psg->feed(m);
    if (impl.audio_ring.empty()) return;
    // What follows the landing is no longer this timeline: remove every entry from it on,
    // markers included (a Rewind, Reset or StateLoad marker at or after the landing records
    // something the landing has undone). An entry's stamp is the START of the instruction slot
    // that raised it, and a landing is the state BEFORE the instruction that starts at its
    // cycle, so an entry AT the landing cycle was raised by an instruction the replay will run
    // again. The rewind ring is this session's own history on one clock: a soft reset, a
    // rewind and a state load taken from it all keep that clock (a load of a checkpoint of this
    // session puts the clock back to the checkpoint's cycle), so every marker is crossed.
    // One exception: a StateLoad marker AT the landing cycle IS the landing state (the loaded
    // machine and the snapshot at that cycle are the same machine), so it stays, in its place
    // before the new Rewind marker.
    // Limit: a state loaded from ANOTHER session carries a clock that cannot be ordered against
    // this one. Cycles are compared as if it could, so the entries around such a load may be kept
    // or removed by that order, which means nothing; the log is best-effort across it.
    bool keep_load = false;
    AudioLogEntry load_marker;
    while (impl.audio_count > 0) {
        const AudioLogEntry& n = impl.audio_at(impl.audio_count - 1);
        if (n.cycle < m.cycle) break;
        if (n.marker == AudioMarker::StateLoad && n.cycle == m.cycle && !keep_load) {
            keep_load = true;
            load_marker = n;
        }
        --impl.audio_count;
    }
    if (keep_load) impl.audio_push(load_marker);
    // The landing's own marker is ALWAYS recorded: a stream may have written the removed
    // entries already, and the rows after them are earlier in time, which only it says.
    AudioLogEntry e = m;
    e.seq = impl.audio_next_seq++;
    impl.audio_push(e);
}

void Debugger::Impl::sync_audio_log(bool force)
{
    bool asked = false;
    for (const auto& c : clients) {
        if (c.detached) continue;
        asked = asked || c.audio_log;
    }
    const bool was = audio_log_derived;
    if (asked && !was) {
        audio_ring.assign(audio_capacity, AudioLogEntry{});
        audio_head = audio_count = 0;
    } else if (!asked && was) {
        std::vector<AudioLogEntry>().swap(audio_ring);
        audio_head = audio_count = 0;
    }
    audio_log_derived = asked;
    publish_audio_tap();
    // After a machine rebuild the new machine is a discontinuity of its own (the .psg
    // recorder needs the marker as much as the log does).
    if (force && (asked || psg)) {
        AudioLogEntry m;
        m.cycle = emu.clock().get();
        m.marker = AudioMarker::Reset;
        audio_sink.audio_record(m);
    }
}

// ---------------------------------------------------------------------------
// The verbs
// ---------------------------------------------------------------------------

// GH #538 -- INS-24, the .psg recording.
Result Debugger::psg_record_start(ClientId by, const std::string& path)
{
    (void)by;   // attribution only
    Impl& im = *impl_;
    if (im.psg) return Result::RefusedUnavailable;
    auto r = std::make_unique<PsgRecorder>();
    std::string why;
    if (!r->start(path, time().frame, why)) return Result::RefusedUnavailable;
    im.psg = std::move(r);
    im.publish_audio_tap();
    im.bump_revision();
    return Result::Ok;
}

Result Debugger::psg_record_stop(ClientId by)
{
    (void)by;
    Impl& im = *impl_;
    if (!im.psg) return Result::RefusedUnavailable;
    std::string why;
    const bool ok = im.psg->stop(time().frame, why);
    im.psg.reset();
    im.publish_audio_tap();
    im.bump_revision();
    return ok ? Result::Ok : Result::RefusedUnavailable;
}

bool Debugger::psg_recording() const { return impl_->psg != nullptr; }

Result Debugger::set_audio_log_enabled(ClientId by, bool enabled)
{
    Impl::Client* c = impl_->find_client(by);
    if (!c) return Result::RefusedUnavailable;
    c->audio_log = enabled;
    impl_->sync_audio_log();
    impl_->bump_revision();
    return Result::Ok;
}

bool Debugger::audio_log_enabled() const { return impl_->audio_log_derived; }

Result Debugger::audio_log_clear(ClientId by)
{
    if (!impl_->find_client(by)) return Result::RefusedUnavailable;
    const size_t n = impl_->audio_count;
    impl_->audio_head = impl_->audio_count = 0;
    impl_->log_mutate(by, "audio log", static_cast<long long>(n), 0);
    return Result::Ok;
}

Result Debugger::audio_log_resize(size_t entries)
{
    if (entries < 1 || entries > AUDIO_LOG_MAX_ENTRIES) return Result::RefusedUnavailable;
    Impl& im = *impl_;
    if (im.audio_log_derived) {
        const size_t keep = std::min(im.audio_count, entries);
        std::vector<AudioLogEntry> v(entries);
        for (size_t i = 0; i < keep; ++i) v[i] = im.audio_at(im.audio_count - keep + i);
        im.audio_ring.swap(v);
        im.audio_head = 0;
        im.audio_count = keep;
    }
    im.audio_capacity = entries;
    im.bump_revision();
    return Result::Ok;
}

Expected<std::vector<AudioLogEntry>> Debugger::audio_log_entries(uint64_t since_seq) const
{
    const Impl& im = *impl_;
    if (!im.audio_log_derived) return make_refused<std::vector<AudioLogEntry>>(Result::RefusedUnavailable);
    std::vector<AudioLogEntry> out;
    for (size_t i = 0; i < im.audio_count; ++i) {
        const AudioLogEntry& e = im.audio_at(i);
        if (e.seq > since_seq) out.push_back(e);
    }
    return make_ok(std::move(out));
}

Result Debugger::audio_log_export(const std::string& path) const
{
    const Impl& im = *impl_;
    if (!im.audio_log_derived) return Result::RefusedUnavailable;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return Result::RefusedUnavailable;
    std::string out = audio_log_csv_header();
    for (size_t i = 0; i < im.audio_count; ++i) {
        audio_log_csv_append(out, im.audio_at(i));
        if (out.size() > 65536) {
            if (std::fwrite(out.data(), 1, out.size(), f) != out.size()) { std::fclose(f); return Result::RefusedUnavailable; }
            out.clear();
        }
    }
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok ? Result::Ok : Result::RefusedUnavailable;
}

}  // namespace dbg
}  // namespace jnext
