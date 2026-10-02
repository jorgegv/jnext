#include "core/tape_recorder.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

void put16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

void put24(std::vector<uint8_t>& v, uint32_t x) {
    put16(v, x & 0xFFFF);
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    put16(v, x & 0xFFFF);
    put16(v, x >> 16);
}

/// Two pulses of one stream are "the same length": within 1/8 of the longer.
bool near(uint32_t a, uint32_t b) {
    const uint32_t hi = std::max(a, b), lo = std::min(a, b);
    return hi - lo <= hi / 8;
}

/// A measured timing is the ROM's: within 1/16 of the standard value.
bool std_near(uint32_t measured, uint32_t standard) {
    const uint32_t d = measured > standard ? measured - standard : standard - measured;
    return d <= standard / 16;
}

uint16_t ms_capped(uint64_t master) {
    const uint64_t ms = master / TapeRecorder::MASTER_PER_MS;
    return static_cast<uint16_t>(std::min<uint64_t>(ms, TapeRecorder::MAX_PAUSE_MS));
}

/// One data block found by the segment decoder.
struct DataBlock {
    TapeRecorder::TurboParams p;
    std::vector<uint8_t> bytes;
    bool standard = false;
    size_t last_edge = 0;   ///< index of the block's last edge
};

/// Try to decode a pilot + sync + data block whose first pilot pulse is T[i].
bool try_data(const std::vector<uint32_t>& T, const std::vector<uint32_t>& run,
              size_t i, DataBlock& blk) {
    const uint32_t k = run[i];
    if (k < TapeRecorder::MIN_PILOT_PULSES) return false;

    uint64_t sum = 0;
    for (size_t j = i; j < i + k; ++j) sum += T[j];
    const uint32_t pilot = static_cast<uint32_t>((sum + k / 2) / k);
    for (size_t j = i; j < i + k; ++j)
        if (!near(T[j], pilot)) return false;

    const size_t s = i + k;                     // sync 1
    if (s + 3 >= T.size()) return false;        // sync 1, sync 2, at least one pair
    const uint32_t s1 = T[s], s2 = T[s + 1];
    if (s1 >= pilot || near(s1, pilot) || s2 >= pilot || near(s2, pilot)) return false;

    // Data: pulse pairs with equal halves.
    const size_t d = s + 2;
    std::vector<uint32_t> L;
    size_t m = d;
    while (m + 1 < T.size() && near(T[m], T[m + 1])) {
        L.push_back(T[m] + T[m + 1]);
        m += 2;
    }
    if (L.empty()) return false;

    // Two classes, short (0) and long (1), long at least 1.5 x short. A
    // single class is accepted only at the ROM's own lengths.
    const uint32_t lo = *std::min_element(L.begin(), L.end());
    const uint32_t hi = *std::max_element(L.begin(), L.end());
    uint32_t zero = 0, one = 0, thr = 0;
    if (uint64_t(hi) * 2 >= uint64_t(lo) * 3) {
        thr = (lo + hi) / 2;
        uint64_t zs = 0, os = 0, zn = 0, on = 0;
        for (uint32_t l : L) {
            if (l < thr) { zs += l; ++zn; } else { os += l; ++on; }
        }
        zero = static_cast<uint32_t>((zs + zn) / (2 * zn));
        one  = static_cast<uint32_t>((os + on) / (2 * on));
    } else {
        uint64_t s_all = 0;
        for (uint32_t l : L) s_all += l;
        const uint32_t c = static_cast<uint32_t>((s_all + L.size() / 2) / L.size());
        if (!std_near(c, 2u * TapeRecorder::STD_ZERO) && !std_near(c, 2u * TapeRecorder::STD_ONE))
            return false;
        zero = TapeRecorder::STD_ZERO;
        one  = TapeRecorder::STD_ONE;
        thr  = zero + one;
    }

    // Every pair must sit near its class; the block ends at the first that
    // does not.
    size_t pairs = 0;
    while (pairs < L.size() && near(L[pairs], L[pairs] < thr ? 2 * zero : 2 * one)) ++pairs;
    if (pairs == 0) return false;
    m = d + 2 * pairs;

    std::vector<bool> bits;
    for (size_t b = 0; b < pairs; ++b) bits.push_back(L[b] >= thr);
    size_t last_edge = m;                        // closes the last pair
    // A last bit whose second half is not a matching pulse (the segment ends,
    // or an unrelated edge closes it) is read from its first half — but only
    // when it completes a byte. After whole bytes, one more pulse is the
    // closing edge the ROM itself adds (SA/LD-RET restores the border ~855 T
    // after the last bit), not a bit.
    if (m < T.size() && pairs % 8 == 7) {
        const bool b1 = 2 * T[m] >= thr;
        if (near(T[m], b1 ? one : zero)) {
            bits.push_back(b1);
            last_edge = m + 1;
        }
    }
    if (bits.size() < 8) return false;

    blk.bytes.assign((bits.size() + 7) / 8, 0);
    for (size_t b = 0; b < bits.size(); ++b)
        if (bits[b]) blk.bytes[b / 8] |= static_cast<uint8_t>(0x80 >> (b % 8));
    const uint8_t used = static_cast<uint8_t>(bits.size() % 8 ? bits.size() % 8 : 8);

    blk.p.pilot = static_cast<uint16_t>(std::min<uint32_t>(pilot, 0xFFFF));
    blk.p.sync1 = static_cast<uint16_t>(s1);
    blk.p.sync2 = static_cast<uint16_t>(s2);
    blk.p.zero  = static_cast<uint16_t>(std::min<uint32_t>(zero, 0xFFFF));
    blk.p.one   = static_cast<uint16_t>(std::min<uint32_t>(one, 0xFFFF));
    blk.p.pilot_count = static_cast<uint16_t>(std::min<uint32_t>(k, 0xFFFF));
    blk.p.used_bits = used;
    blk.last_edge = last_edge;

    const uint32_t std_count = blk.bytes[0] < 0x80 ? TapeRecorder::STD_PILOT_HEADER
                                                   : TapeRecorder::STD_PILOT_DATA;
    const uint32_t dc = k > std_count ? k - std_count : std_count - k;
    blk.standard = used == 8 && dc <= std_count / 100 &&
                   std_near(pilot, TapeRecorder::STD_PILOT) &&
                   std_near(s1, TapeRecorder::STD_SYNC1) &&
                   std_near(s2, TapeRecorder::STD_SYNC2) &&
                   std_near(zero, TapeRecorder::STD_ZERO) &&
                   std_near(one, TapeRecorder::STD_ONE);
    return true;
}

void append(std::vector<uint8_t>& out, const std::vector<uint8_t>& b) {
    out.insert(out.end(), b.begin(), b.end());
}

/// Pulses as block(s) 0x13, at most 255 each.
void append_pulses(std::vector<uint8_t>& out, const std::vector<uint16_t>& pulses) {
    for (size_t at = 0; at < pulses.size(); at += 255) {
        const size_t n = std::min<size_t>(255, pulses.size() - at);
        append(out, TapeRecorder::tzx_block_13(
                        std::vector<uint16_t>(pulses.begin() + at, pulses.begin() + at + n)));
    }
}

} // namespace

// ─── Format ──────────────────────────────────────────────────────────────

TapeRecorder::Format TapeRecorder::format_for_path(const std::string& path) {
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return Format::None;
    std::string ext = path.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".tzx") return Format::Tzx;
    if (ext == ".wav") return Format::Wav;
    return Format::None;
}

// ─── Encoders ────────────────────────────────────────────────────────────

std::vector<uint8_t> TapeRecorder::tzx_header() {
    return { 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20 };
}

std::vector<uint8_t> TapeRecorder::tzx_block_10(uint16_t pause_ms, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> v{0x10};
    put16(v, pause_ms);
    put16(v, static_cast<uint32_t>(data.size()));
    append(v, data);
    return v;
}

std::vector<uint8_t> TapeRecorder::tzx_block_11(const TurboParams& p, uint16_t pause_ms,
                                                const std::vector<uint8_t>& data) {
    std::vector<uint8_t> v{0x11};
    put16(v, p.pilot);
    put16(v, p.sync1);
    put16(v, p.sync2);
    put16(v, p.zero);
    put16(v, p.one);
    put16(v, p.pilot_count);
    v.push_back(p.used_bits);
    put16(v, pause_ms);
    put24(v, static_cast<uint32_t>(data.size()));
    append(v, data);
    return v;
}

std::vector<uint8_t> TapeRecorder::tzx_block_13(const std::vector<uint16_t>& pulses) {
    std::vector<uint8_t> v{0x13, static_cast<uint8_t>(pulses.size())};
    for (uint16_t p : pulses) put16(v, p);
    return v;
}

std::vector<uint8_t> TapeRecorder::tzx_block_20(uint16_t pause_ms) {
    std::vector<uint8_t> v{0x20};
    put16(v, pause_ms);
    return v;
}

std::vector<uint8_t> TapeRecorder::wav_header(uint32_t data_bytes) {
    std::vector<uint8_t> v{'R', 'I', 'F', 'F'};
    put32(v, 36 + data_bytes);
    for (char c : std::string("WAVEfmt ")) v.push_back(static_cast<uint8_t>(c));
    put32(v, 16);              // fmt chunk size
    put16(v, 1);               // PCM
    put16(v, 1);               // mono
    put32(v, WAV_RATE);
    put32(v, WAV_RATE);        // byte rate: 1 byte per sample
    put16(v, 1);               // block align
    put16(v, 8);               // bits per sample
    for (char c : std::string("data")) v.push_back(static_cast<uint8_t>(c));
    put32(v, data_bytes);
    return v;
}

std::vector<uint16_t> TapeRecorder::standard_pulses(const std::vector<uint8_t>& data) {
    std::vector<uint16_t> p;
    if (data.empty()) return p;
    const uint16_t n = data[0] < 0x80 ? STD_PILOT_HEADER : STD_PILOT_DATA;
    p.assign(n, STD_PILOT);
    p.push_back(STD_SYNC1);
    p.push_back(STD_SYNC2);
    for (uint8_t byte : data)
        for (int b = 7; b >= 0; --b) {
            const uint16_t len = (byte >> b) & 1 ? STD_ONE : STD_ZERO;
            p.push_back(len);
            p.push_back(len);
        }
    return p;
}

uint16_t TapeRecorder::pause_ms(uint64_t gap, bool rest) {
    if (!rest) return ms_capped(gap);
    const uint64_t ms = gap / MASTER_PER_MS;
    const uint64_t r = ms > 1 ? ms - 1 : 1;    // 0 in 0x20 would stop the tape
    return static_cast<uint16_t>(std::min<uint64_t>(r, MAX_PAUSE_MS));
}

std::vector<uint8_t> TapeRecorder::decode_segment(const std::vector<uint64_t>& raw,
                                                  uint64_t next_event,
                                                  size_t* tail_pause_at,
                                                  bool* tail_is_rest,
                                                  uint64_t* tail_from) {
    std::vector<uint8_t> out;
    // Two edges closer than half a T-state make a pulse TZX cannot hold (it
    // would round to 0 T, and a 0 in 0x13 is no pulse at all). Such a glitch
    // returns the line to where it was, so the pair is dropped: at 28 MHz a
    // CPU or DMA can toggle MIC 1-3 master cycles apart.
    std::vector<uint64_t> edges;
    edges.reserve(raw.size());
    for (uint64_t t : raw) {
        if (!edges.empty() && t - edges.back() < MASTER_PER_T / 2) edges.pop_back();
        else edges.push_back(t);
    }
    const size_t n = edges.size();
    if (n == 0) return out;
    if (tail_from) *tail_from = edges[n - 1];

    std::vector<uint32_t> T(n - 1);
    for (size_t i = 0; i + 1 < n; ++i)
        T[i] = static_cast<uint32_t>((edges[i + 1] - edges[i] + MASTER_PER_T / 2) / MASTER_PER_T);
    // run[i]: consecutive pulses from i that are each near the next.
    std::vector<uint32_t> run(T.size());
    for (size_t i = T.size(); i-- > 0;)
        run[i] = (i + 1 < T.size() && near(T[i], T[i + 1])) ? run[i + 1] + 1 : 1;

    auto pause_after = [&](size_t last) -> uint16_t {
        const uint64_t t_next = last + 1 < n ? edges[last + 1] : next_event;
        return ms_capped(t_next > edges[last] ? t_next - edges[last] : 0);
    };
    auto closed_pulses = [&](size_t from, size_t to) {   // T[from..to)
        std::vector<uint16_t> p;
        for (size_t j = from; j < to; ++j) p.push_back(static_cast<uint16_t>(std::min<uint32_t>(T[j], 0xFFFF)));
        return p;
    };

    size_t unrec = 0;            // first edge of the unrecognised run
    size_t i = 0;
    while (i < T.size()) {
        DataBlock blk;
        if (!try_data(T, run, i, blk)) {
            // A pilot that failed fails the same way from any later pulse of
            // its run (same end, same sync), so skip the run: a long steady
            // tone must not cost a decode attempt per pulse.
            i += run[i] >= MIN_PILOT_PULSES ? run[i] : 1;
            continue;
        }
        append_pulses(out, closed_pulses(unrec, i));
        size_t last = blk.last_edge;
        if (last + 2 == n) last = n - 1;             // a lone closing edge
        const uint16_t pause = pause_after(last);
        if (last == n - 1) {       // this pause is measured to next_event
            if (tail_pause_at) *tail_pause_at = out.size() + (blk.standard ? 1 : 14);
            if (tail_is_rest) *tail_is_rest = false;
        }
        append(out, blk.standard ? tzx_block_10(pause, blk.bytes)
                                 : tzx_block_11(blk.p, pause, blk.bytes));
        i = last + 1;
        unrec = i;
    }
    if (unrec < n) {
        // The run's last edge is open: it is written as a 1 ms pulse (TZX's
        // own rule for finishing a last edge) and the rest of the gap as 0x20.
        std::vector<uint16_t> p = closed_pulses(unrec, n - 1);
        p.push_back(static_cast<uint16_t>(MASTER_PER_MS / MASTER_PER_T));
        append_pulses(out, p);
        const uint64_t gap = next_event > edges[n - 1] ? next_event - edges[n - 1] : 0;
        if (tail_pause_at) *tail_pause_at = out.size() + 1;
        if (tail_is_rest) *tail_is_rest = true;
        append(out, tzx_block_20(pause_ms(gap, true)));
    }
    return out;
}

// ─── Recorder ────────────────────────────────────────────────────────────

bool TapeRecorder::open(const std::string& path, std::string& error) {
    close(last_event_);
    const Format fmt = format_for_path(path);
    if (fmt == Format::None) {
        error = "not a .tzx or .wav file name";
        return false;
    }

    uint64_t size = 0;
    {
        std::ifstream probe(path, std::ios::binary | std::ios::ate);
        if (probe) size = static_cast<uint64_t>(probe.tellg());
        else {
            std::ofstream create(path, std::ios::binary);
            if (!create) { error = "cannot create the file"; return false; }
        }
    }
    file_.open(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file_) { error = "cannot open the file for writing"; return false; }

    if (fmt == Format::Tzx) {
        if (size == 0) {
            const auto h = tzx_header();
            file_.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
        } else {
            char sig[8] = {};
            file_.read(sig, 8);
            if (!file_ || std::memcmp(sig, "ZXTape!\x1A", 8) != 0) {
                file_.close();
                error = "the existing file is not a TZX tape";
                return false;
            }
        }
    } else {
        wav_samples_ = 0;
        wav_level_ = false;
        if (size == 0) {
            const auto h = wav_header(0);
            file_.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
        } else {
            std::vector<uint8_t> h(WAV_HEADER_SIZE);
            file_.read(reinterpret_cast<char*>(h.data()), static_cast<std::streamsize>(h.size()));
            const auto want = wav_header(0);
            bool same = static_cast<bool>(file_);
            for (size_t j = 0; same && j < WAV_HEADER_SIZE; ++j)
                if ((j < 4 || j >= 8) && j < 40 && h[j] != want[j]) same = false;
            if (!same) {
                file_.close();
                error = "the existing file is not a WAV written by jnext (PCM 8-bit mono 44100 Hz)";
                return false;
            }
            wav_samples_ = size - WAV_HEADER_SIZE;
            if (wav_samples_ > 0) {
                file_.seekg(static_cast<std::streamoff>(size - 1));
                char last = 0;
                file_.read(&last, 1);
                wav_level_ = static_cast<uint8_t>(last) >= 0x80;
            }
        }
        // The tape position whose sample index is exactly wav_samples_.
        wav_pos_ = (wav_samples_ * MASTER_HZ + WAV_RATE - 1) / WAV_RATE;
        // The first poll() rewrites the sizes: a file left by a killed run
        // still carries the sizes of its last refresh.
        wav_patched_ = ~uint64_t(0);
    }
    file_.clear();
    file_.seekp(0, std::ios::end);
    if (!file_) { file_.close(); error = "cannot write the file"; return false; }

    format_ = fmt;
    path_ = path;
    ok_ = true;
    have_level_ = have_event_ = have_pending_ = false;
    seg_.clear();
    edges_ = 0;
    rom_blocks_ = 0;
    return true;
}

uint64_t TapeRecorder::gap_since_last(uint64_t now) const {
    if (!have_event_) return 0;
    if (now < last_event_) return BACKWARDS_GAP_MASTER;
    return std::min(now - last_event_, MAX_GAP_MASTER);
}

void TapeRecorder::sample(bool level, uint64_t now) {
    if (!active()) return;
    if (!have_level_) {
        have_level_ = true;
        level_ = level;
        return;
    }
    if (level == level_) return;
    level_ = level;
    edge(now);
}

void TapeRecorder::write_bytes(const std::vector<uint8_t>& b) {
    if (b.empty()) return;
    file_.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!file_) ok_ = false;
}

void TapeRecorder::flush_tzx(uint64_t next, bool final) {
    size_t tail_at = 0;
    bool rest = false;
    const uint64_t at = static_cast<uint64_t>(file_.tellp());
    uint64_t from = 0;
    const std::vector<uint8_t> bytes = decode_segment(seg_, next, &tail_at, &rest, &from);
    write_bytes(bytes);
    seg_.clear();
    have_pending_ = !final && !bytes.empty();
    if (!final) {
        pending_at_ = at + tail_at;
        pending_from_ = from;
        pending_rest_ = rest;
    }
}

void TapeRecorder::patch_pending(uint64_t now) {
    if (!have_pending_) return;
    have_pending_ = false;
    const uint64_t gap = now < pending_from_ ? BACKWARDS_GAP_MASTER : now - pending_from_;
    const uint16_t v = pause_ms(gap, pending_rest_);
    const char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
    file_.seekp(static_cast<std::streamoff>(pending_at_));
    file_.write(b, 2);
    file_.seekp(0, std::ios::end);
    if (!file_) ok_ = false;
}

void TapeRecorder::poll(uint64_t now) {
    if (!active()) return;
    if (format_ == Format::Tzx) {
        if (!seg_.empty() && now > seg_.back() &&
            now - seg_.back() > uint64_t(SEGMENT_GAP_T) * MASTER_PER_T)
            flush_tzx(now, false);
    } else if (wav_patched_ != wav_samples_) {
        wav_patch_header();
    }
    file_.flush();
}

void TapeRecorder::edge(uint64_t now) {
    ++edges_;
    if (format_ == Format::Tzx) {
        if (!seg_.empty()) {
            const uint64_t prev = seg_.back();
            if (now < prev)
                flush_tzx(prev + BACKWARDS_GAP_MASTER, true);
            else if (now - prev > uint64_t(SEGMENT_GAP_T) * MASTER_PER_T ||
                     seg_.size() >= MAX_SEGMENT_EDGES)
                flush_tzx(now, true);
        } else {
            patch_pending(now);
        }
        seg_.push_back(now);
    } else {
        wav_pos_ += gap_since_last(now);
        wav_advance(wav_pos_);
        wav_toggle();
    }
    have_event_ = true;
    last_event_ = now;
}

bool TapeRecorder::rom_block(const std::vector<uint8_t>& data, uint64_t now) {
    if (!active()) return false;
    if (format_ == Format::Tzx) {
        if (!seg_.empty())
            flush_tzx(now < seg_.back() ? seg_.back() + BACKWARDS_GAP_MASTER : now, true);
        else
            patch_pending(now);
        // Written at once, its pause provisional (0) until the next event, so
        // a run that is killed still has the block.
        pending_at_ = static_cast<uint64_t>(file_.tellp()) + 1;
        write_bytes(tzx_block_10(0, data));
        have_pending_ = true;
        pending_from_ = now;
        pending_rest_ = false;
    } else {
        wav_pos_ += gap_since_last(now);
        wav_advance(wav_pos_);
        for (uint16_t d : standard_pulses(data)) {
            wav_toggle();
            wav_pos_ += uint64_t(d) * MASTER_PER_T;
            wav_advance(wav_pos_);
        }
        wav_toggle();                              // closes the last pulse
        wav_patch_header();
    }
    ++rom_blocks_;
    have_event_ = true;
    last_event_ = now;
    file_.flush();
    return ok_;
}

void TapeRecorder::wav_advance(uint64_t tape_pos) {
    const uint64_t target = tape_pos * WAV_RATE / MASTER_HZ;
    if (target <= wav_samples_) return;
    static constexpr size_t CHUNK = 4096;
    char buf[CHUNK];
    std::memset(buf, wav_level_ ? WAV_HIGH : WAV_LOW, CHUNK);
    uint64_t left = target - wav_samples_;
    while (left > 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(left, CHUNK));
        file_.write(buf, static_cast<std::streamsize>(n));
        left -= n;
    }
    if (!file_) ok_ = false;
    wav_samples_ = target;
}

void TapeRecorder::wav_toggle() {
    wav_level_ = !wav_level_;
}

void TapeRecorder::wav_patch_header() {
    const auto data = static_cast<uint32_t>(std::min<uint64_t>(wav_samples_, 0xFFFFFFFFu - 36));
    const auto h = wav_header(data);
    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
    file_.seekp(0, std::ios::end);
    if (!file_) ok_ = false;
    wav_patched_ = wav_samples_;
}

void TapeRecorder::close(uint64_t now) {
    if (!active()) return;
    if (format_ == Format::Tzx) {
        if (!seg_.empty())
            flush_tzx(now < seg_.back() ? seg_.back() + BACKWARDS_GAP_MASTER : now, true);
        else
            patch_pending(now);
    } else {
        if (have_event_) {
            wav_pos_ += gap_since_last(now);
            wav_advance(wav_pos_);
        }
        wav_patch_header();
    }
    file_.close();
    format_ = Format::None;
    path_.clear();
    seg_.clear();
    have_pending_ = have_event_ = have_level_ = false;
}
