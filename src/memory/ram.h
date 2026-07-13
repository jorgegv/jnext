#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class Ram {
public:
    explicit Ram(size_t size_bytes = 2048 * 1024);
    uint8_t read(uint32_t addr) const;
    void write(uint32_t addr, uint8_t val);
    uint8_t* page_ptr(uint16_t page);       // pointer to start of 8K page
    const uint8_t* page_ptr(uint16_t page) const;
    void reset();
    size_t size() const { return data_.size(); }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    // ── G12 — Nirvana-class write observer (Phase A plumbing) ──────────
    //
    // Real Nirvana-style demos rewrite ULA attribute bytes (RAM
    // 0x5800-0x5AFF bank 5 / shadow 0x7800-0x7AFF bank 7) mid-frame,
    // racing the video beam row by row, to get more colours per
    // character cell than the hardware nominally allows. jnext renders
    // from RAM at scanline-render time and has no way to know a byte
    // was rewritten mid-frame at a specific beam position — it only
    // ever sees the final value. This hook lets a (future) consumer
    // observe every RAM write together with the beam position active
    // at the moment of the write, so a later phase can multiplex the
    // byte that was live for each rendered scanline instead of the
    // final one.
    //
    // Callback params: (addr, val, m1, t)
    //   addr — RAM-space byte address (page*0x2000 + offset), matching
    //          page_ptr()'s addressing — NOT the CPU (0x0000-0xFFFF)
    //          address.
    //   val  — byte written.
    //   m1   — true if the write happened during an M1 (opcode fetch)
    //          cycle. Data writes are never M1 cycles on the Z80 bus;
    //          this is always false for the current callers (kept for
    //          signature symmetry with the CPU-side hook and for any
    //          future self-modifying-code use).
    //   t    — beam-position timestamp, packed as `(vc << 16) | hc`,
    //          where hc/vc are the 7 MHz pixel-tick raster counters
    //          (see z80_cpu.cpp::derive_hc_vc). Packed into one value
    //          rather than two separate parameters so a consumer can
    //          compare/order writes with a plain integer comparison; a
    //          later phase unpacks via `t & 0xFFFF` (hc) / `t >> 16`
    //          (vc). 0 when no beam-position source is wired (e.g. the
    //          Ram+Mmu unit fixture with no CPU running).
    using WriteObserver = std::function<void(uint32_t addr, uint8_t val, bool m1, uint32_t t)>;
    void set_write_observer(WriteObserver obs) { observer_ = std::move(obs); }
    void clear_write_observer() { observer_ = nullptr; }
    // Cheap check for callers on the hot write path (e.g. Mmu's raw
    // pointer write) that want to skip all position-computation work
    // when nothing is listening — a single bool test, no std::function
    // invocation.
    bool has_write_observer() const noexcept { return static_cast<bool>(observer_); }

    // Fire the write observer for a byte that was already written by
    // some other path (e.g. Mmu's fast raw-pointer write, which bypasses
    // write() for performance and must notify manually). Does not touch
    // data_.
    void notify_write(uint32_t addr, uint8_t val, bool m1, uint32_t t) {
        if (observer_) observer_(addr, val, m1, t);
    }

private:
    std::vector<uint8_t> data_;
    WriteObserver observer_;
};
