#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "memory/beam_counters.h"
#include "memory/ram.h"

// Beam-time replay of the SRAM bytes Layer 2 fetches (GH #362).
//
// VHDL authority: cores/zxnext/src/video/layer2.vhd, zxula_timing.vhd.
//
// Layer 2 does not read its bitmap once per frame: it requests one byte per
// 7 MHz tick, one pixel ahead of the one being drawn (layer2.vhd: `hc_eff <=
// hc + 1`; the SRAM address register and the request toggle update at sc =
// "00"). A CPU or DMA write is therefore seen by the first fetch that follows
// it in real time. jnext composes the whole frame at its end, so without this
// log it shows every byte as it is at the END of the frame, including the
// writes made after the beam passed (a scroller that rewrites the bitmap in
// the vertical blank tore). This class keeps a per-frame log of the SRAM
// writes so the renderer can answer "what did the fetch of address A read at
// scanline L, beam position H".
//
// Fetch instants (the pixel at column X is fetched while the counter reads
// X - 1, because `hc_eff = hc + 1`):
//   256x192   phc = X - 1  ->  raw hc c_min_hactive + X
//   320x256   whc = X - 1  ->  raw hc c_min_hactive - 32 + X
//   640x256   whc = col - 1 (one byte, two pixels, per tick), same as 320x256
// (beam_counters.h derives phc / whc from zxula_timing.vhd:476-522.)
//
// Same contract as AttributeMux (attribute_mux.h): a write is visible to a
// fetch when its (line, hc) tag is at or before the fetch instant. The two
// differ in what they store. The SRAM is 2 MB, so there is no dense baseline
// or per-offset array here: the log is sparse and append-only, and each entry
// remembers the byte it overwrote (`prev`), so the frame-start value of an
// address that is written is its first entry's `prev`, and an address that is
// never written is read from the live RAM, which is then also its frame-start
// value. Cost is proportional to the writes, not to the RAM.
//
// It records every SRAM write once armed, not only those inside the displayed
// window: NR 0x12 / NR 0x13 can switch the window mid-frame, and a write to a
// window the beam is not showing may be shown a few rows later. Once armed it
// does NOT stop until the frame ends (a stop at a mid-frame disable would
// leave an earlier row's query blind to a later write).
//
// Arming: the log starts disarmed each frame (start_frame()) and the emulator
// arms it when Layer 2 is enabled at frame start or becomes enabled mid-frame.
// While Layer 2 is off nothing is fetched, so nothing needs logging: the
// first entry's `prev` is the live byte at the instant of arming and any
// address written earlier simply reads back its current value.
//
// Writers (swept with "a byte of SRAM changes during a frame"). Every store
// into the SRAM (`Ram`) goes through Mmu::write(); its four stores call
// Mmu::sram_log_write_() before storing:
//   * Layer 2 write-over (port 0x123B bit 0)        CPU: own position
//   * alt-ROM write-over (NR 0x8C)                  CPU: own position
//   * config-mode ROM-in-SRAM write                 CPU: own position
//   * normal MMU slot (RAM pages)                   CPU: own position
// A DMA transfer (dma_.write_memory), a tape / NEX loader and a debugger poke
// reach the same four stores through Mmu::write() and take the coarse
// (line, hc) tag. NOT logged, and why: the dedicated bank 5 / bank 7 VRAM
// buffers of a Next machine (not SRAM bytes, Layer 2 never fetches them);
// the DivMMC and Multiface backings (SRAM below 0x40000, outside what Layer 2
// can address on a Next); the ROM / RAM fills Emulator::init does and the
// state loads (they precede the frame's baseline). A NEX / snapshot loader's
// own bank writes, which run after init() and before the first
// begin_new_frame(), are not logged either: init(preserve_memory=false) and
// load_state() call stop(), and arm() does nothing until start_frame(). A soft
// reset (init with preserve_memory) is mid-frame and keeps the log running.
//
// Storage: the entries and the slots live in two vectors that keep their size
// from frame to frame; `log_n_` / `slots_n_` say how much of each this frame
// uses, and only a cold helper (grow_()) enlarges them, doubling. The record
// path (record_unchecked_(), which Mmu's store calls after its own checks) is
// therefore straight-line: no growth code, no size division.
//
// No save_state/load_state: everything here is rebuilt each frame
// (Emulator::begin_new_frame -> Mmu::attr_mux_start_frame), exactly like
// AttributeMux.
class SramBeamLog {
    friend class Mmu;   // Mmu::sram_log_record_() calls record_unchecked_()

public:
    /// Reset for a new frame. `hc_origin` is VideoTiming::ula_prefetch_origin_hc();
    /// `sram_bytes` the size of the Ram (the address bound). Disarmed.
    void start_frame(int hc_origin, uint32_t sram_bytes) {
        for (const Slot *s = slots_.data(), *e = s + slots_n_; s != e; ++s) idx_[s->addr] = 0;
        nbanks_ = (sram_bytes >> 14) + ((sram_bytes & 0x3FFFu) != 0);
        dirty_banks_.assign((nbanks_ + 63) / 64, 0);   // a bit for every bank below sram_bytes
        slots_n_ = 0;   // the storage keeps its size
        log_n_ = 0;
        narrow_base_ = beam::phc_raw_hc(hc_origin, -1);   // fetch of column 0
        wide_base_   = beam::whc_raw_hc(hc_origin, -1);
        sram_bytes_ = sram_bytes;
        recording_ = false;
        started_ = true;
        target_line_ = 0;
    }

    /// Back to "never started": not recording, and `arm()` does nothing until the
    /// next start_frame(). Frees the log's memory (start_frame() keeps the
    /// storage, so a burst of writes would otherwise stay allocated for the
    /// session). Called on a machine re-initialisation, so a loader's bank writes
    /// before the first frame (NEX load) are not logged.
    void stop() {
        for (const Slot *s = slots_.data(), *e = s + slots_n_; s != e; ++s) idx_[s->addr] = 0;
        dirty_banks_.assign(dirty_banks_.size(), 0);
        std::vector<Slot>().swap(slots_);
        std::vector<Entry>().swap(log_);
        slots_n_ = slots_cap_ = 0;
        log_n_ = log_cap_ = 0;
        started_ = false;
        recording_ = false;
    }

    /// Start recording (idempotent). The first call allocates the address index.
    void arm() {
        if (!started_) return;
        if (idx_.size() < sram_bytes_) idx_.assign(sram_bytes_, 0);
        idx_data_ = idx_.data();
        idx_n_ = static_cast<uint32_t>(idx_.size());
        recording_ = true;
    }

    /// True once start_frame() has been called (the lifecycle guard, as for
    /// AttributeMux::started()).
    bool started() const { return started_; }
    bool recording() const { return recording_; }

    /// True when at least one address was written since arming: the only case
    /// a read can differ from the live RAM.
    bool any_writes() const { return slots_n_ != 0; }

    /// True when no address in the `n` 16 KB SRAM banks from bank `k0` (bank =
    /// address >> 14) was written since arming: read() of any of them returns
    /// the live byte, so Layer 2 may read such a span straight from the RAM.
    /// A bank at or above sram_bytes_ is clean (Mmu::sram_log_record_()
    /// records no address there); every bank below it has a bit (start_frame
    /// sizes the map).
    bool banks_clean(uint32_t k0, uint32_t n) const {
        for (uint32_t k = k0; k - k0 < n && k < nbanks_; ++k)
            if ((dirty_banks_[k >> 6] >> (k & 63)) & 1u) return false;
        return true;
    }

    /// Start a render pass (call before its first apply_changes_for_line; the
    /// debugger's second pass re-walks the same log). O(1): the replay cursors
    /// stay where the last read left them, because read() answers from any
    /// state that its own reads and record_unchecked_() leave a slot in:
    ///   (I) every chain entry before `cursor` (all of them when it is kNone)
    ///       has a key <= `lastkey`, and `curval` is the value of the last of
    ///       them (`prev0` if there is none).
    /// read() at key K returns the last value of the chain's longest prefix
    /// whose keys are all <= K (`prev0` if that prefix is empty). From a state
    /// with (I), a K below `lastkey` replays from the chain head; any other K
    /// continues from `cursor`, and every entry before it has a key <= lastkey
    /// <= K, so it lies in K's prefix too. Either way the walk ends where K's
    /// prefix ends, and the slot keeps (I) with lastkey = K. record_unchecked_()
    /// keeps (I): a new slot starts with an empty prefix, and an append only
    /// adds after `last`, moving an exhausted cursor (kNone) onto the new
    /// entry. A per-slot reset here (cursor = first, lastkey = 0, curval =
    /// prev0) would be one more state with (I), so it could change no read
    /// (GH #362 perf, K7).
    void rewind_to_baseline() { target_line_ = 0; }

    /// Set the scanline subsequent reads resolve against. O(1).
    void apply_changes_for_line(int line) { target_line_ = static_cast<uint16_t>(line); }

    /// Raw hc of the fetch of column X in each Layer 2 mode (see the table above;
    /// beam_counters.h has the derivations). Called per pixel, so the origin
    /// terms are folded once in start_frame().
    uint16_t narrow_fetch_hc(int x) const {
        const int h = narrow_base_ + x;
        return static_cast<uint16_t>(h < 0 ? 0 : h);
    }
    uint16_t wide_fetch_hc(int x) const {
        const int h = wide_base_ + x;
        return static_cast<uint16_t>(h < 0 ? 0 : h);
    }

    /// The byte at SRAM address `addr` as the fetch at raw hc `hc` of the
    /// scanline set by apply_changes_for_line() reads it: the last write at or
    /// before (line, hc), else the frame-start value (the first write's
    /// `prev`), else the live byte in `ram`.
    uint8_t read(uint32_t addr, uint16_t hc, const Ram& ram) const {
        const uint32_t ix = (addr < idx_n_) ? idx_data_[addr] : 0;
        if (ix == 0) return ram.read(addr);   // never written this frame
        Slot& s = slots_[ix - 1];
        const uint32_t key = (static_cast<uint32_t>(target_line_) << 16) | hc;
        if (key < s.lastkey) {                // out-of-order query: replay from the start
            s.cursor = s.first;
            s.curval = s.prev0;
        }
        s.lastkey = key;
        while (s.cursor != kNone) {
            const Entry& e = log_[s.cursor];
            if (e.key > key) break;
            s.curval = e.value;
            s.cursor = e.next;
        }
        return s.curval;
    }

    size_t log_size() const { return log_n_; }

private:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    static constexpr uint32_t kMinStorage = 1024;   // entries / slots of a first grow_()

    /// Record that SRAM byte `addr` was overwritten (`prev` -> `value`) while
    /// scanline `line` (framebuffer-row space) was the beam position and `hc`
    /// the raw horizontal position. Checks nothing: the caller guarantees
    /// recording_ and addr < sram_bytes_ (Mmu::sram_log_write_() tests
    /// recording(), then Mmu::sram_log_record_() its own sram_size_, which
    /// attr_mux_start_frame() assigns with sram_bytes_).
    void record_unchecked_(uint16_t line, uint16_t hc, uint32_t addr, uint8_t prev, uint8_t value) {
        const uint32_t n = log_n_;
        if (n == log_cap_) grow_(log_, log_cap_);
        log_[n] = {(static_cast<uint32_t>(line) << 16) | hc, kNone, value};
        log_n_ = n + 1;
        uint32_t& ix = idx_[addr];
        if (ix == 0) {
            const uint32_t k = slots_n_;
            if (k == slots_cap_) grow_(slots_, slots_cap_);
            slots_[k] = {addr, n, n, n, 0, prev, prev};
            slots_n_ = k + 1;
            ix = k + 1;
            dirty_banks_[addr >> 20] |= uint64_t{1} << ((addr >> 14) & 63);
        } else {
            Slot& s = slots_[ix - 1];
            log_[s.last].next = n;
            s.last = n;
            if (s.cursor == kNone) s.cursor = n;
        }
    }

    /// Enlarge `v` (doubling; the elements in use are kept) and publish its new
    /// size as `cap`. Off the record path's straight line.
    template <class T>
    [[gnu::cold, gnu::noinline]] static void grow_(std::vector<T>& v, uint32_t& cap) {
        v.resize(v.empty() ? kMinStorage : v.size() * 2);
        cap = static_cast<uint32_t>(v.size());
    }

    struct Entry {
        uint32_t key;     // (line << 16) | hc
        uint32_t next;    // next entry for the same address, or kNone
        uint8_t  value;
    };
    struct Slot {
        uint32_t addr;
        uint32_t first;   // chain head
        uint32_t last;    // chain tail
        uint32_t cursor;  // next unconsumed entry
        uint32_t lastkey; // key of the previous read
        uint8_t  prev0;   // the byte before the first write: this frame's start value
        uint8_t  curval;  // value after the consumed entries
    };

    std::vector<uint32_t>        idx_;       // addr -> slot index + 1 (0 = never written)
    const uint32_t*              idx_data_ = nullptr;   // idx_.data()/size(), for the per-pixel read
    uint32_t                     idx_n_ = 0;
    mutable std::vector<Slot>    slots_;     // storage: [0, slots_n_) in use, size slots_cap_
    std::vector<Entry>           log_;       // storage: [0, log_n_) in use, size log_cap_
    uint32_t                     slots_n_ = 0;
    uint32_t                     slots_cap_ = 0;
    uint32_t                     log_n_ = 0;
    uint32_t                     log_cap_ = 0;
    std::vector<uint64_t>        dirty_banks_;   // bit k: a slot lies in SRAM bank k (addr >> 14)
    uint32_t                     nbanks_ = 0;    // 16 KB banks below sram_bytes_ (the map's extent)
    uint32_t                     sram_bytes_ = 0;
    int                          narrow_base_ = 0;   // raw hc of column 0's fetch, 256x192
    int                          wide_base_ = 0;     // same, 320x256 / 640x256 (may be < 0 on a bare fixture)
    uint16_t                     target_line_ = 0;
    bool                         started_ = false;
    bool                         recording_ = false;
};
