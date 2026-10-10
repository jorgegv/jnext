#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

/// The Raspberry Pi's sound, as it reaches the Next's mixer over I2S.
/// Design: doc/design/NEXTPI-DESIGN.md §3.7.
///
/// On a real Next the Pi's audio enters the FPGA over I2S and is summed into
/// the mixer next to the beeper, the AY chips and the DAC, gated by NR 0xA2
/// (`I2s`, `Mixer`). With NextPi running under QEMU (`PiQemu`), QEMU's `wav`
/// audio back-end writes the Pi's output — a 44-byte WAV header, then 16-bit
/// stereo PCM at 44.1 kHz — into a FIFO, and this class is its reader:
///
///   * a reader THREAD drains the FIFO into a lock-free ring, so QEMU never
///     blocks on a full pipe, even while jnext is paused;
///   * the emulator POPS one stereo frame per mixer output sample (also
///     44.1 kHz) and latches it into `I2s` (`to_i2s` converts to the 10-bit
///     offset-binary form the hardware uses).
///
/// QEMU's clock is the host's, the mixer's is the emulated machine's, so the
/// two drift and QEMU delivers in bursts (and writes nothing at all while the
/// Pi is silent). The consumer side absorbs that: it PREBUFFERS `kPrebuffer`
/// frames before playing, reports an underrun (silence) when the ring runs dry
/// and prebuffers again, and TRIMS the ring back to `kTarget` frames when it
/// has built up past `kMaxLatency`, so latency stays bounded. If the ring
/// overflowed while nothing consumed (a pause longer than the ring), the next
/// pop flushes it and playback restarts on fresh audio.
///
/// Single producer (the thread), single consumer (the emulator thread).
/// POSIX only, like the rest of NextPi.
class PiAudio {
public:
    static constexpr int      kRate        = 44100;      ///< what QEMU is asked for, and Mixer::SAMPLE_RATE
    static constexpr uint32_t kCapacity    = 1u << 17;   ///< ring frames (~3 s)
    static constexpr uint32_t kPrebuffer   = 2205;       ///< 50 ms before (re)starting
    static constexpr uint32_t kTarget      = 4410;       ///< 100 ms after a trim
    static constexpr uint32_t kMaxLatency  = 13230;      ///< 300 ms: trim beyond this

    PiAudio() = default;
    ~PiAudio();

    PiAudio(const PiAudio&)            = delete;
    PiAudio& operator=(const PiAudio&) = delete;

    /// Create (if missing) and open the FIFO for reading — non-blocking, so it
    /// succeeds before QEMU, the writer, exists — and start the reader thread.
    bool open(const std::string& fifo_path, std::string& error);

    /// Stop the thread and close the FIFO. Idempotent; the destructor calls it.
    void close();

    /// Consumer: the next stereo frame, or false for silence (prebuffering, or
    /// an underrun). Called once per mixer output sample.
    bool pop(int16_t& left, int16_t& right);

    /// A signed 16-bit PCM sample as the Pi's 10-bit I2S input: offset binary,
    /// 0x200 = silence (i2s.vhd:177-180 inverts the sign bit). Pure.
    static uint16_t to_i2s(int16_t sample);

    uint64_t frames_received() const { return received_.load(); }  ///< read from QEMU
    uint64_t frames_dropped()  const { return dropped_.load(); }    ///< ring full, or trimmed
    uint64_t underruns()       const { return underruns_.load(); }  ///< ran dry while playing
    /// Reads that found no writer (QEMU not started yet, or gone). Each is
    /// followed by a 20 ms pause, so this grows by at most ~50 a second — on
    /// Linux poll() reports a gone writer at once, and without the pause the
    /// reader would spin a core. Exposed so a row can pin that.
    uint64_t eof_reads()       const { return eof_reads_.load(); }
    /// Frames buffered and not yet popped.
    uint32_t available() const { return head_.load() - tail_.load(); }

private:
    void run();
    void push(int16_t left, int16_t right);

    int                     fd_ = -1;
    std::thread             thread_;
    std::atomic<bool>       stop_{false};
    std::vector<int16_t>    ring_ = std::vector<int16_t>(2 * kCapacity);
    std::atomic<uint32_t>   head_{0};   ///< producer: frames written (mod 2^32)
    std::atomic<uint32_t>   tail_{0};   ///< consumer: frames read
    bool                    playing_ = false;   ///< consumer-side only
    std::atomic<uint64_t>   received_{0};
    std::atomic<uint64_t>   dropped_{0};
    std::atomic<uint64_t>   underruns_{0};
    std::atomic<uint64_t>   eof_reads_{0};
    std::atomic<bool>       overflowed_{false};   ///< producer dropped a frame: flush on next pop
};
