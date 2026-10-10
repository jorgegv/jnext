// Audio — the Pi I2S source's POSIX-only rows (`# os: posix`, GH #214).
//
// `PiAudio` reads QEMU's WAV stream from a FIFO, which Windows does not have,
// so the rows that drive it against a real FIFO live here rather than in
// audio_test (whose row count must be the same on every OS). Their plan rows
// are in doc/testing/AUDIO-TEST-PLAN-DESIGN.md with the rest of Audio's.
//
// Run: ./build/test/audio_posix_test

#include "audio/beeper.h"
#include "audio/dac.h"
#include "audio/i2s.h"
#include "audio/mixer.h"
#include "audio/pi_audio.h"
#include "audio/turbosound.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../row_id.h"

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

} // namespace

static void g_pi_audio() {
    // MX-41 and MX-31..40 — THE PI I2S SOURCE. MX-30 (retired below, as G29 /
    // GH #201, because jnext had no Raspberry Pi to produce samples) keeps its
    // retirement: its ID is not reused (owner, PR #315). NextPi under QEMU is
    // that producer now, and MX-41 is the stream MX-30 asked for: its sound
    // arrives as a WAV stream on a FIFO, PiAudio reads it,
    // and the emulator latches one frame per mixer sample into I2s
    // (uart_posix_test PI-48/49 cover that path end to end). These rows pin
    // PiAudio itself against a real FIFO, which is why they are POSIX-only
    // (GH #214's `# os: posix`).
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("jnext-piaudio-" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        auto header = [] {
            std::string h("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x44\xac\0\0\x10\xb1\x02\0\x04\0\x10\0data\0\0\0\0", 44);
            return h;
        };
        auto frames_bytes = [](int count, int start) {
            std::string b;
            for (int i = 0; i < count; ++i) {
                const int16_t l = static_cast<int16_t>(start + i), r = static_cast<int16_t>(-(start + i));
                b.push_back(static_cast<char>(l)); b.push_back(static_cast<char>(l >> 8));
                b.push_back(static_cast<char>(r)); b.push_back(static_cast<char>(r >> 8));
            }
            return b;
        };
        auto wait_available = [](PiAudio& a, uint32_t n) {
            for (int i = 0; i < 300 && a.available() < n; ++i) ::usleep(10000);
            return a.available();
        };

        // MX-41 — a continuous stream, header and frames split across writes,
        // arrives frame for frame; to_i2s maps signed 16-bit to the 10-bit
        // offset binary of i2s.vhd:177-180; a Pi frame in I2s reaches the mix.
        {
            const std::string fifo = (dir / "mx30.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string stream = header() + frames_bytes(3000, 100);
            for (std::size_t off = 0; w >= 0 && off < stream.size();) {
                const std::size_t chunk = off < 120 ? 7 : 4096;           // split header and frames
                const ssize_t n = ::write(w, stream.data() + off, std::min(chunk, stream.size() - off));
                if (n <= 0) break;
                off += static_cast<std::size_t>(n);
            }
            const uint32_t got = wait_available(a, 3000);
            bool in_order = got == 3000;
            for (int i = 0; in_order && i < 3000; ++i) {
                int16_t l = 0, r = 0;
                in_order = a.pop(l, r) && l == 100 + i && r == -(100 + i);
            }
            if (w >= 0) ::close(w);
            const bool mapping = PiAudio::to_i2s(-32768) == 0 && PiAudio::to_i2s(0) == 0x200 &&
                                 PiAudio::to_i2s(32767) == 1023;
            Beeper bp; TurboSound ts; Dac dac; Mixer mx; I2s i2s;
            i2s.set_nr_a2_ctl(0xC0);
            i2s.set_sample(PiAudio::to_i2s(16384), PiAudio::to_i2s(-16384));
            mx.set_i2s_source(&i2s);
            mx.generate_sample(bp, ts, dac);
            int16_t s[2];
            mx.read_samples(s, 1);
            check("MX-41", "the Pi I2S source delivers a continuous stream: PiAudio reads QEMU's WAV "
                  "stream from a FIFO frame for frame (header and frames split across writes), "
                  "to_i2s maps it to 10-bit offset binary, and a Pi frame reaches the mix; "
                  "all 3000 frames are counted received and none dropped (i2s.vhd:177-180)",
                  opened && in_order && mapping && s[0] == 1024 && s[1] == -1024 &&
                      a.frames_received() == 3000 && a.frames_dropped() == 0,
                  fmt("opened=%d (%s) got=%u in_order=%d mapping=%d mix L=%d R=%d (want 1024/-1024) "
                      "received=%llu dropped=%llu (want 3000/0)",
                      opened ? 1 : 0, err.c_str(), got, in_order ? 1 : 0, mapping ? 1 : 0, s[0], s[1],
                      static_cast<unsigned long long>(a.frames_received()),
                      static_cast<unsigned long long>(a.frames_dropped())));
        }

        // MX-31 — PREBUFFER and UNDERRUN: nothing plays until 2205 frames (50 ms,
        // the documented prebuffer) are in; running dry is silence (false),
        // counted, and refills again. Literal numbers, not the constant: the
        // row pins the latency the docs promise, so changing it fails here.
        {
            const std::string fifo = (dir / "mx31.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string first = header() + frames_bytes(2204, 0);
            const bool w1 = w >= 0 && ::write(w, first.data(), first.size()) == static_cast<ssize_t>(first.size());
            wait_available(a, 2204);
            int16_t l, r;
            const bool waits = !a.pop(l, r);                                  // one short of the prebuffer
            const std::string one = frames_bytes(1, 0);
            const bool w2 = w >= 0 && ::write(w, one.data(), one.size()) == 4;
            wait_available(a, 2205);
            int played = 0;
            while (a.pop(l, r)) ++played;                                      // plays all, then runs dry
            const bool dry = a.underruns() == 1;
            const std::string again = frames_bytes(10, 0);
            const bool w3 = w >= 0 && ::write(w, again.data(), again.size()) == 40;
            wait_available(a, 10);
            const bool refills = !a.pop(l, r);                                 // prebuffering again
            if (w >= 0) ::close(w);
            check("MX-31", "the Pi I2S source prebuffers 2205 frames (50 ms) before playing, reports "
                  "an underrun as silence when it runs dry, and prebuffers again before resuming "
                  "(jnext-only buffering, no VHDL counterpart)",
                  opened && w1 && w2 && w3 && waits && played == 2205 && dry && refills,
                  fmt("waits=%d played=%d (want 2205) underruns=%llu refills=%d", waits ? 1 : 0, played,
                      static_cast<unsigned long long>(a.underruns()),
                      refills ? 1 : 0));
        }

        // MX-32 — LATENCY TRIM: a backlog past the maximum latency is cut to 4410
        // frames (100 ms, the documented target) by dropping the OLDEST, so the
        // Pi's sound never lags far behind. Literal, like MX-31.
        {
            const std::string fifo = (dir / "mx32.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const int total = 20000;
            const std::string stream = header() + frames_bytes(total, 0);
            std::size_t off = 0;
            while (w >= 0 && off < stream.size()) {
                const ssize_t n = ::write(w, stream.data() + off, stream.size() - off);
                if (n <= 0) break;
                off += static_cast<std::size_t>(n);
            }
            wait_available(a, total);
            int16_t l = 0, r = 0;
            const bool popped = a.pop(l, r);
            const uint32_t left = a.available();
            if (w >= 0) ::close(w);
            check("MX-32", "a Pi I2S backlog beyond the maximum latency is trimmed to 4410 frames "
                  "(100 ms) by dropping the oldest frames (jnext-only buffering, no VHDL counterpart)",
                  opened && popped && l == 20000 - 4410 && left == 4410 - 1 &&
                      a.frames_dropped() == 20000 - 4410,
                  fmt("popped=%d first=%d (want %d) left=%u dropped=%llu", popped ? 1 : 0, l,
                      20000 - 4410, left,
                      static_cast<unsigned long long>(a.frames_dropped())));
        }

        // MX-33 — RING FULL: with nothing popping, the reader keeps exactly
        // 131072 frames (~3 s) and drops (and counts) every one past that, so
        // a paused emulator never blocks QEMU. Literal numbers, like MX-31/32,
        // so a changed kCapacity fails. jnext-only buffering: no VHDL
        // counterpart.
        {
            const std::string fifo = (dir / "mx33.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const uint32_t extra = 1000;
            const uint32_t capacity = 131072;
            const uint32_t total = capacity + extra;
            const std::string stream = header() + frames_bytes(static_cast<int>(total), 0);
            std::size_t off = 0;
            while (w >= 0 && off < stream.size()) {
                const ssize_t n = ::write(w, stream.data() + off, stream.size() - off);
                if (n <= 0) break;
                off += static_cast<std::size_t>(n);
            }
            for (int i = 0; i < 500 && a.frames_received() + a.frames_dropped() < total; ++i)
                ::usleep(10000);
            if (w >= 0) ::close(w);
            check("MX-33", "with nothing consuming, the Pi I2S ring keeps exactly its capacity "
                  "(131072 frames) and drops, counting them, the frames past it (jnext-only "
                  "buffering, no VHDL counterpart)",
                  opened && off == stream.size() && a.available() == capacity &&
                      a.frames_received() == capacity && a.frames_dropped() == extra,
                  fmt("opened=%d wrote=%zu/%zu available=%u received=%llu dropped=%llu (want %u/%u/%u)",
                      opened ? 1 : 0, off, stream.size(), a.available(),
                      static_cast<unsigned long long>(a.frames_received()),
                      static_cast<unsigned long long>(a.frames_dropped()), capacity, capacity, extra));
        }

        // MX-42 — A PAUSE LONGER THAN THE RING: 140000 frames arrive while
        // nothing pops, so the ring fills and frames are lost. On resume the
        // stale contents are flushed (all 140000 counted as dropped) and
        // playback restarts on fresh audio after the prebuffer, instead of
        // trimming to a moment from deep inside the pause. jnext-only, no VHDL
        // counterpart.
        {
            const std::string fifo = (dir / "mx42.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            auto write_all = [&](const std::string& b) {
                std::size_t off = 0;
                while (w >= 0 && off < b.size()) {
                    const ssize_t n = ::write(w, b.data() + off, b.size() - off);
                    if (n <= 0) return false;
                    off += static_cast<std::size_t>(n);
                }
                return w >= 0;
            };
            const bool w1 = write_all(header() + frames_bytes(140000, 0));
            for (int i = 0; i < 500 && a.frames_received() + a.frames_dropped() < 140000; ++i)
                ::usleep(10000);
            int16_t l = 0, r = 0;
            const bool flushed = !a.pop(l, r) && a.available() == 0 && a.frames_dropped() == 140000;
            const bool w2 = write_all(frames_bytes(2205, 20000));
            wait_available(a, 2205);
            const bool fresh = a.pop(l, r) && l == 20000 && r == -20000;
            if (w >= 0) ::close(w);
            check("MX-42", "after a pause longer than the Pi I2S ring, the stale frames are flushed "
                  "and playback resumes on fresh audio after the prebuffer (jnext-only, no VHDL "
                  "counterpart)",
                  opened && w1 && w2 && flushed && fresh,
                  fmt("opened=%d writes=%d/%d flushed=%d (available=%u dropped=%llu, want 0/140000) "
                      "fresh=%d first L=%d (want 20000)", opened ? 1 : 0, w1 ? 1 : 0, w2 ? 1 : 0,
                      flushed ? 1 : 0, a.available(),
                      static_cast<unsigned long long>(a.frames_dropped()), fresh ? 1 : 0, l));
        }

        // MX-43 — BACK-TO-BACK STREAMS: writer B opens the FIFO before writer A
        // closes it, so the reader never sees an EOF between them. B's WAV
        // header must still be recognised (in-stream, by RIFF...WAVE) and not
        // played: exactly 6000 frames arrive, A's then B's, exact. B's header
        // is split across two reads (6 bytes, a 150 ms pause, the rest), so the
        // reader must hold a possible header back until it can tell. jnext-only,
        // no VHDL counterpart.
        {
            const std::string fifo = (dir / "mx43.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            auto write_all = [](int fd, const std::string& b) {
                std::size_t off = 0;
                while (fd >= 0 && off < b.size()) {
                    const ssize_t n = ::write(fd, b.data() + off, b.size() - off);
                    if (n <= 0) return false;
                    off += static_cast<std::size_t>(n);
                }
                return fd >= 0;
            };
            const int wa = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const bool a_ok = write_all(wa, header() + frames_bytes(3000, 0));
            const int wb = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;   // before A closes
            if (wa >= 0) ::close(wa);
            const std::string b_stream = header() + frames_bytes(3000, 5000);
            const bool b1 = write_all(wb, b_stream.substr(0, 6));       // "RIFF" + 2
            ::usleep(150000);                                           // read on its own
            const bool b_ok = b1 && write_all(wb, b_stream.substr(6));
            const uint32_t got = wait_available(a, 6000);
            ::usleep(100000);                         // anything extra would have landed too
            const uint64_t received = a.frames_received();
            int16_t l = 0, r = 0;
            bool exact = got == 6000 && received == 6000;
            for (int i = 0; exact && i < 6000; ++i) {
                const int want = i < 3000 ? i : 5000 + (i - 3000);
                exact = a.pop(l, r) && l == want && r == -want;
            }
            if (wb >= 0) ::close(wb);
            check("MX-43", "two Pi audio streams back to back with no EOF between them: the second "
                  "WAV header is recognised in-stream and skipped, and all 6000 frames arrive exact "
                  "(jnext-only, no VHDL counterpart)",
                  opened && a_ok && b_ok && exact,
                  fmt("opened=%d a=%d b=%d available=%u received=%llu (want 6000) exact=%d last L=%d",
                      opened ? 1 : 0, a_ok ? 1 : 0, b_ok ? 1 : 0, got,
                      static_cast<unsigned long long>(received), exact ? 1 : 0, l));
        }

        // MX-44..46 — LATENCY. The reader must wake on data at once, give up
        // its thread promptly on close(), and resume promptly after a writer
        // goes. Each is timed against the steady clock with margins that hold
        // on a loaded host and still separate a slower reader. jnext-only, no
        // VHDL counterpart.
        using clk = std::chrono::steady_clock;
        auto ms_since = [](clk::time_point t0) {
            return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        };

        // MX-44 — a write is poppable at once: ten probes, each one frame
        // written after the reader has gone idle, timed until available()
        // shows it; the median must be under 25 ms. (Woken only by its 100 ms
        // poll timeout instead of by the data, the median is ~50 ms.)
        {
            const std::string fifo = (dir / "mx44.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string hdr = header();
            bool ok = w >= 0 && ::write(w, hdr.data(), hdr.size()) == static_cast<ssize_t>(hdr.size());
            std::vector<double> lat;
            for (int i = 0; ok && i < 10; ++i) {
                ::usleep(37000);                              // the reader is back in poll()
                const uint32_t want = a.available() + 1;
                const std::string f = frames_bytes(1, i);
                const clk::time_point t0 = clk::now();
                ok = ::write(w, f.data(), f.size()) == 4;
                while (ok && a.available() < want && ms_since(t0) < 1000) ::usleep(200);
                lat.push_back(ms_since(t0));
            }
            if (w >= 0) ::close(w);
            std::sort(lat.begin(), lat.end());
            const double median = lat.size() == 10 ? (lat[4] + lat[5]) / 2 : 1e9;
            check("MX-44", "a frame written to the Pi's audio FIFO is poppable at once: the median of "
                  "ten write-to-available times is under 25 ms (jnext-only, no VHDL counterpart)",
                  opened && ok && median < 25.0,
                  fmt("opened=%d ok=%d median=%.1f ms (want < 25) max=%.1f ms", opened ? 1 : 0,
                      ok ? 1 : 0, median, lat.empty() ? 0.0 : lat.back()));
        }

        // MX-45 — close() is prompt: with a writer connected but silent the
        // reader sits in poll(); close() 30 ms later must return within
        // 250 ms (~70 ms with the 100 ms poll; ~1 s with a 1000 ms one).
        {
            const std::string fifo = (dir / "mx45.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            ::usleep(30000);
            const clk::time_point t0 = clk::now();
            a.close();
            const double took = ms_since(t0);
            if (w >= 0) ::close(w);
            check("MX-45", "closing the Pi audio reader while its writer is connected but silent "
                  "returns within 250 ms (jnext-only, no VHDL counterpart)",
                  opened && w >= 0 && took < 250.0,
                  fmt("opened=%d writer=%d close took %.1f ms (want < 250)", opened ? 1 : 0,
                      w >= 0 ? 1 : 0, took));
        }

        // MX-46 — a reconnect is prompt: once the reader has seen a writer go
        // (an EOF read, which starts its no-writer pause), a new writer's frame
        // must be available within 100 ms (~20 ms with the 20 ms pause;
        // ~200 ms with a 200 ms one).
        {
            const std::string fifo = (dir / "mx46.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int wa = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string first = header() + frames_bytes(1, 0);
            const bool a_ok = wa >= 0 && ::write(wa, first.data(), first.size()) ==
                                             static_cast<ssize_t>(first.size());
            wait_available(a, 1);
            const uint64_t eofs = a.eof_reads();
            if (wa >= 0) ::close(wa);
            for (int i = 0; i < 2000 && a.eof_reads() == eofs; ++i) ::usleep(500);
            const bool saw_eof = a.eof_reads() > eofs;         // the pause has just begun
            const int wb = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string second = header() + frames_bytes(1, 1);
            const clk::time_point t0 = clk::now();
            const bool b_ok = wb >= 0 && ::write(wb, second.data(), second.size()) ==
                                             static_cast<ssize_t>(second.size());
            while (b_ok && a.available() < 2 && ms_since(t0) < 1000) ::usleep(200);
            const double took = ms_since(t0);
            if (wb >= 0) ::close(wb);
            check("MX-46", "after a writer goes, a new writer's frame reaches the Pi audio reader "
                  "within 100 ms (jnext-only, no VHDL counterpart)",
                  opened && a_ok && saw_eof && b_ok && a.available() == 2 && took < 100.0,
                  fmt("opened=%d a=%d eof=%d b=%d available=%u took %.1f ms (want < 100)",
                      opened ? 1 : 0, a_ok ? 1 : 0, saw_eof ? 1 : 0, b_ok ? 1 : 0, a.available(), took));
        }

        // MX-47 — an idle reader costs nothing: with a writer connected but
        // silent, 300 ms of waiting must use under 30 ms of CPU (it is ~0: the
        // reader sleeps in poll()). A reader that polls for the wrong event, or
        // whose poll() returns at once, spins a core instead (~300 ms).
        // RUSAGE_SELF covers every thread; the reader is the only one running.
        {
            auto cpu_ms = [] {
                rusage u{};
                ::getrusage(RUSAGE_SELF, &u);
                return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1000.0 +
                       (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1000.0;
            };
            const std::string fifo = (dir / "mx47.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            ::usleep(50000);
            const double c0 = cpu_ms();
            ::usleep(300000);
            const double used = cpu_ms() - c0;
            if (w >= 0) ::close(w);
            check("MX-47", "an idle Pi audio reader, its writer connected but silent, uses under "
                  "30 ms of CPU in 300 ms: it sleeps in poll() (jnext-only, no VHDL counterpart)",
                  opened && w >= 0 && used < 30.0,
                  fmt("opened=%d writer=%d cpu=%.1f ms in 300 ms (want < 30)", opened ? 1 : 0,
                      w >= 0 ? 1 : 0, used));
        }

        // MX-34 — RECONNECT: QEMU reopens the FIFO (a restart), and each writer
        // starts with its own WAV header. Writer A leaves half a frame behind
        // when it closes; writer B's header must be skipped and that half frame
        // discarded, so B's frames arrive exact. jnext-only, no VHDL counterpart.
        {
            const std::string fifo = (dir / "mx34.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            auto write_all = [](int fd, const std::string& b) {
                std::size_t off = 0;
                while (fd >= 0 && off < b.size()) {
                    const ssize_t n = ::write(fd, b.data() + off, b.size() - off);
                    if (n <= 0) return false;
                    off += static_cast<std::size_t>(n);
                }
                return fd >= 0;
            };
            const int wa = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const bool a_ok = write_all(wa, header() + frames_bytes(1, 500) + std::string("\x11\x22", 2));
            wait_available(a, 1);
            if (wa >= 0) ::close(wa);
            ::usleep(300000);                         // the reader sees writer A go
            const int wb = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const bool b_ok = write_all(wb, header() + frames_bytes(PiAudio::kPrebuffer, 1000));
            const uint32_t got = wait_available(a, 1 + PiAudio::kPrebuffer);
            int16_t l = 0, r = 0;
            bool exact = got == 1 + PiAudio::kPrebuffer && a.pop(l, r) && l == 500 && r == -500;
            for (int i = 0; exact && i < static_cast<int>(PiAudio::kPrebuffer); ++i)
                exact = a.pop(l, r) && l == 1000 + i && r == -(1000 + i);
            if (wb >= 0) ::close(wb);
            check("MX-34", "when QEMU reopens the Pi's audio FIFO, the new stream's WAV header is "
                  "skipped and a half frame left by the old writer is discarded, so every frame "
                  "arrives exact (jnext-only buffering, no VHDL counterpart)",
                  opened && a_ok && b_ok && exact,
                  fmt("opened=%d a=%d b=%d available=%u (want %u) exact=%d last L=%d R=%d",
                      opened ? 1 : 0, a_ok ? 1 : 0, b_ok ? 1 : 0, got, 1 + PiAudio::kPrebuffer,
                      exact ? 1 : 0, l, r));
        }

        // MX-35 — OPEN ERRORS AND PERMISSIONS: a path that exists and is not a
        // FIFO is refused, a FIFO that cannot be made reports mkfifo's error,
        // and the FIFO is made private (0600: only jnext's user may feed its
        // mixer). jnext-only, no VHDL counterpart.
        {
            const std::string regular = (dir / "mx35.file").string();
            { std::ofstream(regular) << "x"; }
            PiAudio a;
            std::string err_regular, err_missing, err_new;
            const bool refuses_file = !a.open(regular, err_regular) &&
                                      err_regular.find("exists and is not a FIFO") != std::string::npos;
            const std::string missing = (dir / "no-such-dir" / "mx35.fifo").string();
            const bool reports_mkfifo = !a.open(missing, err_missing) &&
                                        err_missing.find("mkfifo") != std::string::npos;
            const std::string fresh = (dir / "mx35.fifo").string();
            const mode_t old_mask = ::umask(022);
            const bool opened = a.open(fresh, err_new);
            ::umask(old_mask);
            struct stat st{};
            const bool is_fifo = ::stat(fresh.c_str(), &st) == 0 && S_ISFIFO(st.st_mode);
            const unsigned mode = static_cast<unsigned>(st.st_mode & 0777);
            a.close();
            check("MX-35", "the Pi's audio FIFO: a path that exists and is not a FIFO is refused, a "
                  "FIFO that cannot be made reports mkfifo's error, and a new one is created 0600 "
                  "(jnext-only, no VHDL counterpart)",
                  refuses_file && reports_mkfifo && opened && is_fifo && mode == 0600,
                  fmt("regular='%s' missing='%s' opened=%d (%s) fifo=%d mode=%03o (want 600)",
                      err_regular.c_str(), err_missing.c_str(), opened ? 1 : 0, err_new.c_str(),
                      is_fifo ? 1 : 0, mode));
        }

        // MX-36 — THE TRIM BOUNDARY: a backlog of exactly 13230 frames (300 ms,
        // the documented maximum) is kept whole; one more frame and it is cut
        // to 4410. Literal numbers, like MX-31/32. jnext-only, no VHDL
        // counterpart.
        {
            struct After { bool ok; int16_t first; uint32_t left; uint64_t dropped; };
            auto backlog = [&](const char* name, int total) {
                const std::string fifo = (dir / name).string();
                PiAudio a;
                std::string err;
                After r{a.open(fifo, err), 0, 0, 0};
                const int w = r.ok ? ::open(fifo.c_str(), O_WRONLY) : -1;
                const std::string stream = header() + frames_bytes(total, 0);
                std::size_t off = 0;
                while (w >= 0 && off < stream.size()) {
                    const ssize_t n = ::write(w, stream.data() + off, stream.size() - off);
                    if (n <= 0) break;
                    off += static_cast<std::size_t>(n);
                }
                int16_t rr = 0;
                r.ok = r.ok && wait_available(a, static_cast<uint32_t>(total)) ==
                                   static_cast<uint32_t>(total) && a.pop(r.first, rr);
                r.left = a.available();
                r.dropped = a.frames_dropped();
                if (w >= 0) ::close(w);
                return r;
            };
            const After at   = backlog("mx36a.fifo", 13230);
            const After past = backlog("mx36b.fifo", 13231);
            check("MX-36", "a Pi I2S backlog of exactly 13230 frames (300 ms) is not trimmed; "
                  "13231 frames are trimmed to 4410 (jnext-only buffering, no VHDL counterpart)",
                  at.ok && at.first == 0 && at.left == 13229 && at.dropped == 0 && past.ok &&
                      past.first == 13231 - 4410 && past.left == 4409 && past.dropped == 13231 - 4410,
                  fmt("13230: ok=%d first=%d left=%u dropped=%llu (want 0/13229/0); 13231: ok=%d "
                      "first=%d left=%u dropped=%llu (want 8821/4409/8821)",
                      at.ok ? 1 : 0, at.first, at.left, static_cast<unsigned long long>(at.dropped),
                      past.ok ? 1 : 0, past.first, past.left,
                      static_cast<unsigned long long>(past.dropped)));
        }

        // MX-37 — NO SPIN WITHOUT A WRITER: once QEMU's end is closed, the
        // reader wakes at most ~50 times a second (a 20 ms pause after each
        // read that finds no writer). Linux's poll() reports a gone writer at
        // once, so without the pause the thread would spin a core after every
        // QEMU exit; the bound is 60 wakeups in 600 ms. (macOS's poll() waits
        // its 100 ms instead, so there the row holds with or without the
        // pause; CI's Linux run is the one that discriminates.) jnext-only, no
        // VHDL counterpart.
        {
            const std::string fifo = (dir / "mx37.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string stream = header() + frames_bytes(10, 0);
            const bool wrote = w >= 0 && ::write(w, stream.data(), stream.size()) ==
                                             static_cast<ssize_t>(stream.size());
            wait_available(a, 10);
            if (w >= 0) ::close(w);
            ::usleep(100000);                         // the reader sees the writer go
            const uint64_t from = a.eof_reads();
            ::usleep(600000);
            const uint64_t wakeups = a.eof_reads() - from;
            check("MX-37", "with no writer on the Pi's audio FIFO, the reader pauses after each "
                  "read that finds none: at most 60 wakeups in 600 ms, so it never spins "
                  "(jnext-only, no VHDL counterpart)",
                  opened && wrote && a.available() == 10 && wakeups >= 1 && wakeups <= 60,
                  fmt("opened=%d wrote=%d available=%u wakeups in 600 ms=%llu (want 1..60)",
                      opened ? 1 : 0, wrote ? 1 : 0, a.available(),
                      static_cast<unsigned long long>(wakeups)));
        }

        // MX-38 — A LIVE WRITER'S PAUSE IS NOT AN END: QEMU writes nothing while
        // the Pi is silent, so the reader's non-blocking read gets EAGAIN with
        // the writer still there. That must not reset the stream: a frame split
        // across a pause longer than the 100 ms poll is reassembled, and no
        // bytes after the pause are taken for a new header. jnext-only, no VHDL
        // counterpart.
        {
            const std::string fifo = (dir / "mx38.fifo").string();
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string frames = frames_bytes(static_cast<int>(PiAudio::kPrebuffer) + 1, 700);
            const std::string before = header() + frames.substr(0, 6);   // one frame and a half
            const std::string after  = frames.substr(6);
            const bool w1 = w >= 0 && ::write(w, before.data(), before.size()) ==
                                          static_cast<ssize_t>(before.size());
            ::usleep(250000);                         // > 100 ms: the reader polls out, reads EAGAIN
            std::size_t off = 0;
            while (w >= 0 && off < after.size()) {
                const ssize_t n = ::write(w, after.data() + off, after.size() - off);
                if (n <= 0) break;
                off += static_cast<std::size_t>(n);
            }
            const uint32_t want = PiAudio::kPrebuffer + 1;
            const uint32_t got = wait_available(a, want);
            int16_t l = 0, r = 0;
            bool exact = got == want;
            for (int i = 0; exact && i < static_cast<int>(want); ++i)
                exact = a.pop(l, r) && l == 700 + i && r == -(700 + i);
            if (w >= 0) ::close(w);
            check("MX-38", "a pause by a live writer of the Pi's audio FIFO (EAGAIN) does not end "
                  "the stream: a frame split across a 250 ms pause is reassembled and every frame "
                  "arrives exact (jnext-only, no VHDL counterpart)",
                  opened && w1 && off == after.size() && exact,
                  fmt("opened=%d w1=%d wrote=%zu/%zu available=%u (want %u) exact=%d last L=%d R=%d",
                      opened ? 1 : 0, w1 ? 1 : 0, off, after.size(), got, want, exact ? 1 : 0, l, r));
        }

        // MX-39 — DESCRIPTOR HYGIENE: the reader holds exactly one descriptor
        // on the FIFO, close-on-exec (QEMU and ffmpeg must not inherit it);
        // opening again does not leak the first; close() releases it and
        // forgets its number, so a second close() (the destructor's) cannot
        // close an unrelated descriptor that reused it. The FIFO's descriptors
        // are found by inode, which works the same on Linux and macOS.
        // jnext-only, no VHDL counterpart.
        {
            const std::string fifo = (dir / "mx39.fifo").string();
            auto fifo_fds = [&](std::vector<int>& out) {
                out.clear();
                struct stat want{};
                if (::stat(fifo.c_str(), &want) != 0) return;
                for (int fd = 0; fd < 1024; ++fd) {
                    struct stat st{};
                    if (::fstat(fd, &st) == 0 && st.st_dev == want.st_dev && st.st_ino == want.st_ino)
                        out.push_back(fd);
                }
            };
            PiAudio a;
            std::string err;
            std::vector<int> fds;
            const bool opened = a.open(fifo, err);
            fifo_fds(fds);
            const bool one = fds.size() == 1;
            const bool cloexec = one && (::fcntl(fds[0], F_GETFD) & FD_CLOEXEC) != 0;
            const bool reopened = a.open(fifo, err);
            fifo_fds(fds);
            const bool no_leak = fds.size() == 1;
            const int held = no_leak ? fds[0] : -1;
            a.close();
            fifo_fds(fds);
            const bool released = fds.empty();
            const int other = ::open("/dev/null", O_RDONLY | O_CLOEXEC);   // likely reuses `held`
            a.close();                                                     // the destructor's call
            const bool other_alive = other >= 0 && ::fcntl(other, F_GETFD) != -1;
            if (other >= 0) ::close(other);
            check("MX-39", "the Pi audio reader holds one close-on-exec descriptor on its FIFO, a "
                  "second open does not leak the first, and close() releases it and forgets it, so "
                  "closing again cannot close a descriptor that reused its number (jnext-only, no "
                  "VHDL counterpart)",
                  opened && one && cloexec && reopened && no_leak && released && other_alive,
                  fmt("opened=%d one=%d cloexec=%d reopened=%d no_leak=%d released=%d held=%d "
                      "other=%d alive=%d", opened ? 1 : 0, one ? 1 : 0, cloexec ? 1 : 0,
                      reopened ? 1 : 0, no_leak ? 1 : 0, released ? 1 : 0, held, other,
                      other_alive ? 1 : 0));
        }

        // MX-40 — A SIGNAL DOES NOT STOP THE READER: poll() returns EINTR when a
        // signal without SA_RESTART lands on the reader thread (poll is never
        // restarted), and the reader must poll again, not give up. A SIGUSR1
        // handler is installed without SA_RESTART and the signal is blocked in
        // this thread, so the process-directed kill() lands on the reader;
        // afterwards a stream must still arrive. (A non-blocking FIFO read()
        // cannot return EINTR, so that branch has no row.) jnext-only, no VHDL
        // counterpart.
        {
            const std::string fifo = (dir / "mx40.fifo").string();
            struct sigaction sa{}, old_sa{};
            sa.sa_handler = [](int) {};
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;                                  // no SA_RESTART
            ::sigaction(SIGUSR1, &sa, &old_sa);
            PiAudio a;
            std::string err;
            const bool opened = a.open(fifo, err);            // the reader thread exists now
            sigset_t usr1, old_mask;
            sigemptyset(&usr1);
            sigaddset(&usr1, SIGUSR1);
            ::pthread_sigmask(SIG_BLOCK, &usr1, &old_mask);   // only the reader can take it
            ::usleep(150000);
            for (int i = 0; i < 5; ++i) {
                ::kill(::getpid(), SIGUSR1);
                ::usleep(30000);
            }
            const int w = opened ? ::open(fifo.c_str(), O_WRONLY) : -1;
            const std::string stream = header() + frames_bytes(10, 0);
            const bool wrote = w >= 0 && ::write(w, stream.data(), stream.size()) ==
                                             static_cast<ssize_t>(stream.size());
            const uint32_t got = wait_available(a, 10);
            if (w >= 0) ::close(w);
            a.close();
            ::pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
            ::sigaction(SIGUSR1, &old_sa, nullptr);
            check("MX-40", "a signal interrupting the Pi audio reader's poll() (EINTR) does not end "
                  "the reader: a stream written afterwards still arrives (jnext-only, no VHDL "
                  "counterpart)",
                  opened && wrote && got == 10,
                  fmt("opened=%d wrote=%d available=%u (want 10)", opened ? 1 : 0, wrote ? 1 : 0, got));
        }
        fs::remove_all(dir, ec);
    }
}

int main() {
    std::printf("Audio (Pi I2S source, POSIX-only) Compliance Tests\n");
    std::printf("====================================================\n\n");
    g_pi_audio();      std::printf("  5.1 Pi I2S source -- done\n");
    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
