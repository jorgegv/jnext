// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.2 CAP-INS (read) and §4.2a (write).
//
// Work package B1 of epic #276. Every read below goes through an accessor that
// already exists or through one of the five B1 accessor additions; nothing here
// reimplements a decode the emulator owns (the sprite attribute decode, the
// palette expansion, the raster derivation and the ULA's screen selection are
// all read from their owners, not recomputed).
//
// TWO CONTRACTS THIS FILE IS THE IMPLEMENTATION OF:
//
//   OBSERVATION IS SIDE-EFFECT FREE. Reads of guest memory go through
//   `Mmu::peek()` (F1), never `Mmu::read()`; a NextREG read goes through
//   `NextReg::peek()`, never `read()` (which runs the destructive handlers and
//   emits a trace line). INS-05 `port_in` is the ONE exception, and it says so.
//
//   EVERY MUTATION IS LOGGED BY THE BACKEND. Each write verb calls
//   `Impl::log_mutate*`, so §4.2a's `MUTATE <what> <old> -> <new> by <client>`
//   exists for every client's writes without any client emitting it.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include <cstdio>
#include <cstring>

#include "debug/disasm.h"
#include "debug/disasm_text.h"
#include "debug/raster_state.h"
#include "debug/trace.h"
#include "video/palette.h"
#include "video/renderer.h"

namespace jnext {
namespace dbg {
namespace {

/// A short, stable spelling of a `MemSpace` for the MUTATE line.
std::string space_name(MemSpace space, uint32_t addr) {
    char buf[40];
    switch (space.kind) {
        case MemSpace::Kind::Cpu:
            std::snprintf(buf, sizeof(buf), "mem cpu:0x%04X", addr & 0xFFFF);
            break;
        case MemSpace::Kind::Page:
            std::snprintf(buf, sizeof(buf), "mem page%u:0x%04X",
                          static_cast<unsigned>(space.index), addr);
            break;
        case MemSpace::Kind::Rom:
            std::snprintf(buf, sizeof(buf), "mem rom%u:0x%04X",
                          static_cast<unsigned>(space.index), addr);
            break;
    }
    return buf;
}

/// INS-15's `PaletteId` superset -> the hardware enum, with `UlaActive`
/// resolved. Returns false for a value outside the nine.
bool resolve_palette(Emulator& emu, PaletteId id, ::PaletteId& out) {
    if (id == PaletteId::UlaActive) {
        // NR 0x43 **bit 1** (`zxnext.vhd:5393,6825`) — NOT bit 0, which is
        // `nr_43_ulanext_en`. `PaletteManager::active_ula_palette()` is that bit.
        out = emu.palette().active_ula_palette() ? ::PaletteId::ULA_SECOND
                                                 : ::PaletteId::ULA_FIRST;
        return true;
    }
    const auto v = static_cast<uint8_t>(id);
    if (v > 7) return false;
    out = static_cast<::PaletteId>(v);
    return true;
}

/// Bytes in one NR 0x50-0x57 page. A `Page` read or write that runs past the
/// end of the page is SHORT rather than wrapping into the next one: the spaces
/// are separate stores, and silently continuing into a neighbour is the bug this
/// bound exists to prevent.
constexpr uint32_t PAGE_BYTES = 0x2000;

}  // namespace

// ---------------------------------------------------------------------------
// INS-01 — registers
// ---------------------------------------------------------------------------

Z80Registers Debugger::registers() const {
    return impl_->emu.cpu().get_registers();
}

// §4.2a — one register at a time. There is deliberately no "set all": the only
// setter the CPU offers assigns the whole struct, which is how a frontend
// clobbers I and R while writing A (a real GDB `G`-packet hazard), so the whole
// struct is read, one field is changed, and it goes back.
Result Debugger::set_register(ClientId by, RegId reg, uint16_t value) {
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;

    Z80Registers r = impl_->emu.cpu().get_registers();
    const uint8_t lo = static_cast<uint8_t>(value & 0xFF);

    // The old value, for the MUTATE line, captured per case below.
    long long old_value = 0;
    const char* name = "?";

    auto hi_of = [](uint16_t v) { return static_cast<uint16_t>(v >> 8); };
    auto lo_of = [](uint16_t v) { return static_cast<uint16_t>(v & 0xFF); };
    auto set_hi = [](uint16_t& pair, uint8_t v) {
        pair = static_cast<uint16_t>((pair & 0x00FF) | (static_cast<uint16_t>(v) << 8));
    };
    auto set_lo = [](uint16_t& pair, uint8_t v) {
        pair = static_cast<uint16_t>((pair & 0xFF00) | v);
    };

    switch (reg) {
        // ── 16-bit pairs ────────────────────────────────────────────────────
        case RegId::AF:  old_value = r.AF;  r.AF  = value; name = "AF";  break;
        case RegId::BC:  old_value = r.BC;  r.BC  = value; name = "BC";  break;
        case RegId::DE:  old_value = r.DE;  r.DE  = value; name = "DE";  break;
        case RegId::HL:  old_value = r.HL;  r.HL  = value; name = "HL";  break;
        case RegId::AF2: old_value = r.AF2; r.AF2 = value; name = "AF'"; break;
        case RegId::BC2: old_value = r.BC2; r.BC2 = value; name = "BC'"; break;
        case RegId::DE2: old_value = r.DE2; r.DE2 = value; name = "DE'"; break;
        case RegId::HL2: old_value = r.HL2; r.HL2 = value; name = "HL'"; break;
        case RegId::IX:  old_value = r.IX;  r.IX  = value; name = "IX";  break;
        case RegId::IY:  old_value = r.IY;  r.IY  = value; name = "IY";  break;
        case RegId::SP:  old_value = r.SP;  r.SP  = value; name = "SP";  break;
        case RegId::PC:
            old_value = r.PC;
            r.PC = value;
            name = "PC";
            // §4.2a, an obligation on THIS implementation rather than a property
            // of the code: setting PC must clear `halted`. Nothing in the tree
            // did it, and without it a frontend that redirects PC out of a HALT
            // leaves the CPU parked at the new address.
            r.halted = false;
            break;

        // ── 8-bit halves ────────────────────────────────────────────────────
        case RegId::A:   old_value = hi_of(r.AF);  set_hi(r.AF,  lo); name = "A";   break;
        case RegId::F:   old_value = lo_of(r.AF);  set_lo(r.AF,  lo); name = "F";   break;
        case RegId::B:   old_value = hi_of(r.BC);  set_hi(r.BC,  lo); name = "B";   break;
        case RegId::C:   old_value = lo_of(r.BC);  set_lo(r.BC,  lo); name = "C";   break;
        case RegId::D:   old_value = hi_of(r.DE);  set_hi(r.DE,  lo); name = "D";   break;
        case RegId::E:   old_value = lo_of(r.DE);  set_lo(r.DE,  lo); name = "E";   break;
        case RegId::H:   old_value = hi_of(r.HL);  set_hi(r.HL,  lo); name = "H";   break;
        case RegId::L:   old_value = lo_of(r.HL);  set_lo(r.HL,  lo); name = "L";   break;
        case RegId::A2:  old_value = hi_of(r.AF2); set_hi(r.AF2, lo); name = "A'";  break;
        case RegId::F2:  old_value = lo_of(r.AF2); set_lo(r.AF2, lo); name = "F'";  break;
        case RegId::B2:  old_value = hi_of(r.BC2); set_hi(r.BC2, lo); name = "B'";  break;
        case RegId::C2:  old_value = lo_of(r.BC2); set_lo(r.BC2, lo); name = "C'";  break;
        case RegId::D2:  old_value = hi_of(r.DE2); set_hi(r.DE2, lo); name = "D'";  break;
        case RegId::E2:  old_value = lo_of(r.DE2); set_lo(r.DE2, lo); name = "E'";  break;
        case RegId::H2:  old_value = hi_of(r.HL2); set_hi(r.HL2, lo); name = "H'";  break;
        case RegId::L2:  old_value = lo_of(r.HL2); set_lo(r.HL2, lo); name = "L'";  break;
        case RegId::IXH: old_value = hi_of(r.IX);  set_hi(r.IX,  lo); name = "IXH"; break;
        case RegId::IXL: old_value = lo_of(r.IX);  set_lo(r.IX,  lo); name = "IXL"; break;
        case RegId::IYH: old_value = hi_of(r.IY);  set_hi(r.IY,  lo); name = "IYH"; break;
        case RegId::IYL: old_value = lo_of(r.IY);  set_lo(r.IY,  lo); name = "IYL"; break;

        // ── the rest ────────────────────────────────────────────────────────
        case RegId::I:    old_value = r.I;    r.I    = lo;        name = "I";    break;
        case RegId::R:    old_value = r.R;    r.R    = lo;        name = "R";    break;
        case RegId::IFF1: old_value = r.IFF1; r.IFF1 = (lo != 0); name = "IFF1"; break;
        case RegId::IFF2: old_value = r.IFF2; r.IFF2 = (lo != 0); name = "IFF2"; break;
        case RegId::IM:   old_value = r.IM;   r.IM   = lo & 0x03; name = "IM";   break;

        case RegId::Count:
            return Result::Unsupported;   // not a register; see RegId::Count
    }

    impl_->emu.cpu().set_registers(r);
    impl_->log_mutate(by, std::string("reg ") + name, old_value, value);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// INS-02 — memory
//
// THE THREE SPACES, and what each one resolves to:
//
//   Cpu       the live map, overlays included, through `Mmu::peek()` (F1).
//   Page{p}   the PHYSICAL 8 K page p of the NR 0x50-0x57 number space,
//             REGARDLESS of any overlay mapped over a slot — that is the point
//             of the slot view. The VHDL routing (`to_sram_page`, page 0x0E ->
//             the bank-7 BRAM buffer) belongs to the Mmu, which is why this goes
//             through `Mmu::page_ptr_for_nr_page()` rather than indexing `Ram`.
//   Rom{i}    one 16 KB ROM IMAGE, read-only.
// ---------------------------------------------------------------------------

Expected<size_t> Debugger::peek(MemSpace space, uint32_t addr, size_t n,
                               uint8_t* buf) const {
    if (!buf) return make_refused<size_t>(Result::RefusedUnavailable);

    Emulator& emu = impl_->emu;
    switch (space.kind) {
        case MemSpace::Kind::Cpu: {
            for (size_t i = 0; i < n; ++i)
                buf[i] = emu.mmu().peek(static_cast<uint16_t>((addr + i) & 0xFFFF));
            return make_ok<size_t>(n);
        }
        case MemSpace::Kind::Page: {
            if (space.index == PAGE_SENTINEL_ROM_LO || space.index == PAGE_SENTINEL_ROM_HI)
                return make_refused<size_t>(Result::InvalidPage);
            if (space.index > 0xFF) return make_refused<size_t>(Result::InvalidPage);
            // The two ROM sentinels are named explicitly, and REDUNDANTLY so on
            // this path: `Mmu::nr_page_ptr()` returns nullptr for every page
            // >= 0xE0, which is what actually refuses them (a mutation removing
            // this line fails no row, and that is recorded rather than hidden).
            // It stays because the sentinels are the DOCUMENTED refusal of
            // INS-02 and a reader should find them where the space is decoded —
            // and because the poke path below cannot share it: there, a sentinel
            // is `RefusedReadOnly`, a different answer from `InvalidPage`.
            if (space.index == PAGE_SENTINEL_ROM_LO || space.index == PAGE_SENTINEL_ROM_HI)
                return make_refused<size_t>(Result::InvalidPage);
            const uint8_t* p = emu.mmu().nr_page_ptr(static_cast<uint8_t>(space.index));
            if (!p) return make_refused<size_t>(Result::InvalidPage);
            // A page is 8 KB. A read that runs past its end is SHORT, not an
            // error: `Expected<size_t>` reports what it managed (INS-02).
            size_t done = 0;
            for (; done < n && addr + done < PAGE_BYTES; ++done)
                buf[done] = p[addr + done];
            return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
        }
        case MemSpace::Kind::Rom: {
            const uint8_t* p = emu.mmu().rom_image_ptr(static_cast<uint8_t>(space.index));
            if (!p) return make_refused<size_t>(Result::InvalidPage);
            size_t done = 0;
            for (; done < n && addr + done < 0x4000; ++done)
                buf[done] = p[addr + done];
            return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
        }
    }
    return make_refused<size_t>(Result::Unsupported);
}

Expected<size_t> Debugger::poke(ClientId by, MemSpace space, uint32_t addr,
                                size_t n, const uint8_t* buf) {
    if (!buf) return make_refused<size_t>(Result::RefusedUnavailable);
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return make_refused<size_t>(Result::RefusedRzx);

    Emulator& emu = impl_->emu;

    // ROM is read-only in both spellings: `Rom{}` by kind, and a ROM-class page
    // under `Page{}` by its sentinel. Refused BEFORE anything is written, so a
    // refused poke never half-lands.
    if (space.kind == MemSpace::Kind::Rom)
        return make_refused<size_t>(Result::RefusedReadOnly);
    if (space.kind == MemSpace::Kind::Page &&
        (space.index == PAGE_SENTINEL_ROM_LO || space.index == PAGE_SENTINEL_ROM_HI))
        return make_refused<size_t>(Result::RefusedReadOnly);

    size_t done = 0;
    const uint8_t old_first = n > 0
        ? (space.kind == MemSpace::Kind::Cpu
               ? emu.mmu().peek(static_cast<uint16_t>(addr & 0xFFFF))
               : 0)
        : 0;

    if (space.kind == MemSpace::Kind::Cpu) {
        // §4.2a: `Mmu::write` OUTSIDE any GuestExecutionScope — through the live
        // map, overlays honoured, ROM ignored, the per-scanline change logs and
        // the attribute mux updated, no latch, no event. Exactly the Memory
        // panel's path, because it is the same function the Memory panel calls.
        for (; done < n; ++done)
            emu.mmu().write(static_cast<uint16_t>((addr + done) & 0xFFFF), buf[done]);
    } else {
        if (space.index > 0xFF) return make_refused<size_t>(Result::InvalidPage);
        uint8_t* p = emu.mmu().nr_page_ptr(static_cast<uint8_t>(space.index));
        if (!p) return make_refused<size_t>(Result::InvalidPage);
        for (; done < n && addr + done < PAGE_BYTES; ++done)
            p[addr + done] = buf[done];
    }

    if (n == 1) impl_->log_mutate(by, space_name(space, addr), old_first, buf[0]);
    else        impl_->log_mutate_range(by, space_name(space, addr),
                                        std::to_string(done) + " bytes");

    return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
}

// ---------------------------------------------------------------------------
// INS-03 — MMU slots and paging
// ---------------------------------------------------------------------------

std::array<SlotInfo, 8> Debugger::mmu_slots() const {
    std::array<SlotInfo, 8> out{};
    const Mmu& mmu = impl_->emu.mmu();
    for (int s = 0; s < 8; ++s) {
        SlotInfo& si = out[s];
        si.nr_page        = mmu.get_nr_page(s);
        si.effective_page = mmu.get_effective_page(s);
        si.is_rom         = mmu.is_slot_rom(s);
        // The COMPLETE address of the backing store, so no client composes one.
        // A ROM slot's effective_page is an un-shifted `ram_` page index, NOT an
        // NR page number, and handing it to `MemSpace::page()` is exactly the
        // mistake round 4 of the design review caught on two frontends at once.
        if (si.is_rom) {
            si.space        = MemSpace::rom(static_cast<uint16_t>(si.effective_page >> 1));
            si.space_offset = static_cast<uint16_t>((si.effective_page & 1) * 0x2000);
        } else {
            si.space        = MemSpace::page(si.nr_page);
            si.space_offset = 0;
        }
    }
    return out;
}

Result Debugger::set_mmu_slot(ClientId by, int slot, uint8_t page) {
    if (slot < 0 || slot > 7) return Result::RefusedUnavailable;
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;

    const uint8_t old_page = impl_->emu.mmu().get_nr_page(slot);
    impl_->emu.mmu().set_page(slot, page);
    impl_->log_mutate(by, "mmu slot " + std::to_string(slot), old_page, page);
    return Result::Ok;
}

PagingPorts Debugger::paging_ports() const {
    PagingPorts p;
    const Mmu& mmu = impl_->emu.mmu();
    p.port_7ffd = mmu.port_7ffd();
    p.port_1ffd = mmu.port_1ffd();
    p.port_dffd = mmu.port_dffd_reg();
    return p;
}

// ---------------------------------------------------------------------------
// INS-04 — NextREG
// ---------------------------------------------------------------------------

uint8_t Debugger::nextreg_peek(uint8_t reg) const {
    // peek(), never read(): read() runs the register's read handler, which for
    // NR 0x2C/0x2E latches the Pi-I2S sample the guest is about to read, and it
    // emits a trace line per read. A panel sweeping 256 registers four times a
    // second would corrupt one and fabricate a thousand phantom reads.
    return impl_->emu.nextreg().peek(reg);
}

Result Debugger::nextreg_write(ClientId by, uint8_t reg, uint8_t value) {
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;

    const uint8_t old_value = impl_->emu.nextreg().peek(reg);
    // The register's own write handler runs, synchronously, side effects
    // included — INS-04. Source is the Debugger, so it fires no NextRegWrite
    // event on itself; that gating is B2's, on the hook that does not exist yet.
    impl_->emu.nextreg().write(reg, value);
    char what[24];
    std::snprintf(what, sizeof(what), "nextreg 0x%02X", reg);
    impl_->log_mutate(by, what, old_value, value);
    return Result::Ok;
}

uint8_t Debugger::nextreg_selected() const {
    return impl_->emu.nextreg().selected();
}

// ---------------------------------------------------------------------------
// INS-05 — ports. PERTURBING BY NATURE, and the only read in this file that is.
// ---------------------------------------------------------------------------

Expected<uint8_t> Debugger::port_in(ClientId by, uint16_t port) {
    if (impl_->emu.rzx_player().is_playing()) {
        // An RZX playback replays the guest's IN values from the recording; an
        // extra dispatch consumes one and diverges everything after it.
        return make_refused<uint8_t>(Result::RefusedRzx);
    }
    const uint8_t v = impl_->emu.port().read(port);
    char what[24];
    std::snprintf(what, sizeof(what), "port in 0x%04X", port);
    impl_->log_mutate(by, what, v, v);
    return make_ok<uint8_t>(v);
}

Result Debugger::port_out(ClientId by, uint16_t port, uint8_t value) {
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;
    impl_->emu.port().write(port, value);
    char what[24];
    std::snprintf(what, sizeof(what), "port out 0x%04X", port);
    impl_->log_mutate_range(by, what, "= 0x" + std::to_string(value));
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// INS-06 / INS-07 / INS-19 — raster, time, machine
// ---------------------------------------------------------------------------

RasterState Debugger::raster() const {
    // The same four arguments `video_panel_raster_state()` passes, and for the
    // same reasons: paused_hc/paused_vc rather than VideoTiming::pos() (the
    // latter only advances while a debugger is attached, the former is derived
    // from the master clock like every other raster consumer), and the LIVE ULA
    // mode registers, because they decide WHAT is being fetched.
    return raster_state_at(impl_->emu.video_timing(),
                           impl_->emu.paused_hc(), impl_->emu.paused_vc(),
                           impl_->emu.ula().get_screen_mode_reg(),
                           impl_->emu.ula().get_shadow_screen_en());
}

Time Debugger::time() const {
    Time t;
    t.master_cycle   = impl_->emu.clock().get();
    t.tstates_total  = impl_->emu.monotonic_tstates();
    t.frame          = frame_tag(impl_->emu);          // F2
    t.cycle_in_frame = t.master_cycle - impl_->emu.current_frame_cycle();
    t.vc_raw         = static_cast<int16_t>(impl_->emu.paused_vc());
    t.hc_raw         = static_cast<int16_t>(impl_->emu.paused_hc());
    return t;
}

MachineInfo Debugger::machine() const {
    MachineInfo mi;
    const MachineTiming& t  = impl_->emu.timing();
    const VideoTiming&   vt = impl_->emu.video_timing();

    mi.type        = impl_->emu.config().type;
    mi.cpu_divisor = impl_->emu.clock().cpu_divisor();

    mi.tstates_per_line          = t.tstates_per_line;
    mi.tstates_per_frame         = t.tstates_per_frame;
    mi.master_cycles_per_line    = t.master_cycles_per_line;
    mi.master_cycles_per_frame   = t.master_cycles_per_frame;

    mi.lines      = t.lines_per_frame;
    // Derived from the live frame length, not a nominal 50: a 60 Hz NR 0x05 or a
    // machine-type change moves it, which is the whole reason INS-19 carries it.
    mi.fps        = 1000.0 / impl_->emu.frame_period_ms();
    mi.hc_max     = vt.hc_max();
    mi.vc_max     = vt.vc_max();
    mi.max_hblank = vt.max_hblank();
    mi.max_vblank = vt.max_vblank();
    const RasterPos origin = vt.display_origin();
    mi.display_origin_hc = origin.hc;
    mi.display_origin_vc = origin.vc;
    mi.vblank_top        = vt.vblank_top();
    return mi;
}

// ---------------------------------------------------------------------------
// INS-08 — sprites
// ---------------------------------------------------------------------------

std::vector<SpriteInfo> Debugger::sprites() const {
    std::vector<SpriteInfo> out;
    out.reserve(SPRITE_COUNT);
    for (size_t i = 0; i < SPRITE_COUNT; ++i)
        out.push_back(impl_->emu.sprites().get_sprite_info(static_cast<uint8_t>(i)));
    return out;
}

Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>>
Debugger::sprite_attr_raw(uint8_t idx) const {
    using Bytes = std::array<uint8_t, SPRITE_ATTR_BYTES>;
    if (idx >= SPRITE_COUNT) return make_refused<Bytes>(Result::RefusedUnavailable);
    Bytes b{};
    for (size_t i = 0; i < SPRITE_ATTR_BYTES; ++i)
        b[i] = impl_->emu.sprites().read_attr_byte(idx, static_cast<uint8_t>(i));
    return make_ok<Bytes>(b);
}

Result Debugger::set_sprite_attr_raw(ClientId by, uint8_t idx,
                                     const uint8_t* bytes, size_t n) {
    if (!bytes || n != SPRITE_ATTR_BYTES) return Result::RefusedUnavailable;
    if (idx >= SPRITE_COUNT) return Result::RefusedUnavailable;
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;

    for (size_t i = 0; i < n; ++i)
        impl_->emu.sprites().write_attr_byte_at(idx, static_cast<uint8_t>(i), bytes[i]);
    impl_->log_mutate_range(by, "sprite " + std::to_string(idx), "5 attribute bytes");
    return Result::Ok;
}

ConstBytes Debugger::pattern_ram() const {
    return ConstBytes{impl_->emu.sprites().pattern_ram_data(), PATTERN_RAM_BYTES};
}

Result Debugger::write_pattern_ram(ClientId by, uint16_t addr,
                                   const uint8_t* src, size_t n) {
    if (!src) return Result::RefusedUnavailable;
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;

    for (size_t i = 0; i < n; ++i)
        impl_->emu.sprites().write_pattern_byte_at(
            static_cast<uint16_t>(addr + i), src[i]);
    impl_->log_mutate_range(by, "sprite pattern 0x" + std::to_string(addr),
                            std::to_string(n) + " bytes");
    return Result::Ok;
}

Expected<uint16_t> Debugger::sprite_palette_rgb333(int bank, uint8_t index) const {
    if (bank != 0 && bank != 1) return make_refused<uint16_t>(Result::RefusedUnavailable);
    const ::PaletteId id = bank ? ::PaletteId::SPRITE_SECOND : ::PaletteId::SPRITE_FIRST;
    return make_ok<uint16_t>(impl_->emu.palette().entry_rgb333(id, index));
}

ClipWindow Debugger::sprite_clip() const {
    ClipWindow w;
    const SpriteEngine& s = impl_->emu.sprites();
    w.x1 = s.clip_x1();
    w.x2 = s.clip_x2();
    w.y1 = s.clip_y1();
    w.y2 = s.clip_y2();
    return w;
}

// ---------------------------------------------------------------------------
// INS-09 — copper
// ---------------------------------------------------------------------------

CopperState Debugger::copper() const {
    CopperState cs;
    const Copper& c = impl_->emu.copper();
    cs.pc      = c.pc();
    cs.running = c.is_running();
    cs.mode    = c.mode();
    cs.program = ConstU16s{c.program_data(), 1024};
    return cs;
}

// ---------------------------------------------------------------------------
// INS-10 — audio
// ---------------------------------------------------------------------------

Expected<std::array<uint8_t, AY_REGISTER_COUNT>>
Debugger::ay_registers(int chip) const {
    using Regs = std::array<uint8_t, AY_REGISTER_COUNT>;
    if (chip < 0 || chip >= static_cast<int>(AY_CHIP_COUNT))
        return make_refused<Regs>(Result::RefusedUnavailable);
    Regs regs{};
    const AyChip& ay = impl_->emu.turbosound().ay(chip);
    for (size_t i = 0; i < AY_REGISTER_COUNT; ++i)
        regs[i] = ay.read_register(static_cast<uint8_t>(i));
    return make_ok<Regs>(regs);
}

bool Debugger::turbosound_enabled() const {
    // The LIVE signal (`turbosound_en_i` from NR 0x08 bit 1), not the NextREG
    // shadow — INS-10's whole point.
    return impl_->emu.turbosound().enabled();
}

AyChipMode Debugger::ay_mode() const {
    // `TurboSound::ay_mode()` is a bool: true = AY curve, false = YM. NOT the
    // whole NR 0x06 field (bits 1:0, four values, one of which holds the chips
    // in reset) — INS-10 asks only for this accessor, and a frontend that needs
    // the reset state reads NR 0x06 through nextreg_peek().
    return impl_->emu.turbosound().ay_mode() ? AyChipMode::Ay : AyChipMode::Ym;
}

StereoMode Debugger::stereo_mode() const {
    return impl_->emu.turbosound().stereo_mode() ? StereoMode::Acb : StereoMode::Abc;
}

uint8_t Debugger::audio_mute_mask() const {
    return impl_->emu.audio_mute_mask();
}

Result Debugger::set_audio_mute_mask(ClientId by, uint8_t mask) {
    const uint8_t old_mask = impl_->emu.audio_mute_mask();
    impl_->emu.set_audio_mute_mask(mask);
    impl_->log_mutate(by, "audio mute mask", old_mask, mask);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// INS-11 — disassembly
//
// All four read guest memory through `memory_reader()`, i.e. through
// `Mmu::peek()`: a disassembly is an observation, and before F1 the panel's own
// read was the one that latched the floating bus.
// ---------------------------------------------------------------------------

DisasmReadFn Debugger::memory_reader() const {
    Emulator* emu = &impl_->emu;
    return [emu](uint16_t addr) -> uint8_t { return emu->mmu().peek(addr); };
}

std::vector<DisasmLine> Debugger::disassemble(uint16_t addr, size_t n,
                                              const SymbolTable* syms) const {
    std::vector<DisasmLine> out;
    out.reserve(n);
    const DisasmReadFn read = memory_reader();
    uint16_t pc = addr;
    for (size_t i = 0; i < n; ++i) {
        DisasmLine line = disasm_one(pc, read);
        if (syms) {
            // ONE substitution rule, shared with the panel's painter and with
            // the clipboard path (`disasm_text::apply_symbols`), so a symbol
            // cannot be spelled one way in a panel and another in a script.
            const std::string m = disasm_text::apply_symbols(line.mnemonic, syms);
            std::snprintf(line.mnemonic, sizeof(line.mnemonic), "%s", m.c_str());
        }
        out.push_back(line);
        const uint16_t next = static_cast<uint16_t>(pc + line.byte_count);
        if (next < pc) { pc = next; break; }   // address-space wrap: stop here
        pc = next;
    }
    return out;
}

int Debugger::instruction_length(uint16_t addr) const {
    return ::instruction_length(addr, memory_reader());
}

bool Debugger::is_call_like(uint16_t addr) const {
    return ::is_call_like(addr, memory_reader());
}

// ---------------------------------------------------------------------------
// INS-12 / INS-13 — call stack and trace
// ---------------------------------------------------------------------------

const std::vector<CallFrame>& Debugger::call_stack() const {
    return impl_->emu.call_stack().frames();
}

bool Debugger::call_stack_enabled() const {
    return impl_->emu.call_stack().enabled();
}

Result Debugger::set_call_stack_enabled(bool enabled) {
    impl_->emu.call_stack().set_enabled(enabled);
    return Result::Ok;
}

bool Debugger::trace_enabled() const { return impl_->emu.trace_log().enabled(); }

Result Debugger::set_trace_enabled(bool enabled) {
    impl_->emu.trace_log().set_enabled(enabled);
    return Result::Ok;
}

Result Debugger::trace_clear() {
    impl_->emu.trace_log().clear();
    return Result::Ok;
}

Result Debugger::trace_resize(size_t entries) {
    if (entries == 0) return Result::RefusedUnavailable;
    impl_->emu.trace_log().resize(entries);
    return Result::Ok;
}

Expected<std::vector<TraceEntry>> Debugger::trace_entries() const {
    using Entries = std::vector<TraceEntry>;
    const TraceLog& log = impl_->emu.trace_log();
    if (!log.enabled()) return make_refused<Entries>(Result::RefusedUnavailable);
    Entries out;
    out.reserve(log.size());
    for (size_t i = 0; i < log.size(); ++i) out.push_back(log.at(i));
    return make_ok<Entries>(std::move(out));
}

Result Debugger::trace_export(const std::string& path) const {
    return impl_->emu.trace_log().export_to_file(path) ? Result::Ok
                                                       : Result::RefusedUnavailable;
}

// ---------------------------------------------------------------------------
// INS-14 — framebuffer
// ---------------------------------------------------------------------------

ConstU32s Debugger::framebuffer() const {
    Emulator& emu = impl_->emu;
    return ConstU32s{emu.get_framebuffer(),
                     static_cast<size_t>(emu.get_framebuffer_width()) *
                         static_cast<size_t>(emu.get_framebuffer_height())};
}

// ---------------------------------------------------------------------------
// INS-15 — palettes, ULA screen state, clip windows
// ---------------------------------------------------------------------------

std::vector<uint16_t> Debugger::palette(PaletteId id) const {
    ::PaletteId hw;
    if (!resolve_palette(impl_->emu, id, hw)) return {};
    std::vector<uint16_t> out;
    out.reserve(PaletteManager::FULL_SIZE);
    for (int i = 0; i < PaletteManager::FULL_SIZE; ++i)
        out.push_back(impl_->emu.palette().entry_rgb333(hw, static_cast<uint8_t>(i)));
    return out;
}

Result Debugger::set_palette(ClientId by, PaletteId id, uint8_t index,
                             uint16_t rgb333) {
    ::PaletteId hw;
    if (!resolve_palette(impl_->emu, id, hw)) return Result::RefusedUnavailable;
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;

    const uint16_t old_value = impl_->emu.palette().entry_rgb333(hw, index);
    impl_->emu.palette().set_entry_rgb333(hw, index, rgb333);
    char what[40];
    std::snprintf(what, sizeof(what), "palette %u[%u]",
                  static_cast<unsigned>(hw), static_cast<unsigned>(index));
    impl_->log_mutate(by, what, old_value, rgb333 & 0x01FF);
    return Result::Ok;
}

uint8_t Debugger::active_ula_palette_bank() const {
    return impl_->emu.palette().active_ula_palette() ? 1 : 0;
}

UlaScreenRegs Debugger::ula_screen_regs() const {
    UlaScreenRegs r;
    const Ula& ula = impl_->emu.ula();
    r.active_bank     = ula.get_shadow_screen_en() ? 7 : 5;
    r.port_ff         = ula.get_screen_mode_reg();
    // NR 0x26 is the COARSE byte; the NR 0x68 b2 fine-scroll flag is a separate
    // register a frontend reads through nextreg_peek(), not part of this byte.
    r.scroll_x        = ula.get_ula_scroll_x_coarse();
    r.scroll_y        = ula.get_ula_scroll_y();
    r.border          = ula.get_border();
    r.second_palette  = impl_->emu.palette().active_ula_palette();
    r.enabled         = ula.ula_enabled();
    return r;
}

ClipWindow Debugger::clip_window(ClipLayer layer) const {
    ClipWindow w;
    switch (layer) {
        case ClipLayer::Layer2: {
            const Layer2& l2 = impl_->emu.layer2();
            w.x1 = l2.clip_x1(); w.x2 = l2.clip_x2();
            w.y1 = l2.clip_y1(); w.y2 = l2.clip_y2();
            break;
        }
        case ClipLayer::Sprites:
            return sprite_clip();
        case ClipLayer::Ula: {
            const Ula& ula = impl_->emu.ula();
            w.x1 = ula.clip_x1(); w.x2 = ula.clip_x2();
            w.y1 = ula.clip_y1(); w.y2 = ula.clip_y2();
            break;
        }
        case ClipLayer::Tilemap: {
            const Tilemap& tm = impl_->emu.tilemap();
            w.x1 = tm.clip_x1(); w.x2 = tm.clip_x2();
            w.y1 = tm.clip_y1(); w.y2 = tm.clip_y2();
            break;
        }
        case ClipLayer::Count:
            break;   // not a window; see ClipLayer::Count
    }
    return w;
}

// ---------------------------------------------------------------------------
// INS-16 — the whole input surface in one read
// ---------------------------------------------------------------------------

InputState Debugger::input_state() const {
    InputState st;
    const Keyboard& kb = impl_->emu.keyboard();
    for (int r = 0; r < 8; ++r) st.matrix[r] = kb.matrix_row(r);
    // ACTIVE-HIGH, as NR 0xB0 / 0xB1 read it back — the opposite polarity from
    // `matrix`, which is why both are documented at the struct.
    st.ext_keys = static_cast<uint16_t>(kb.nr_b0_byte() |
                                        (static_cast<uint16_t>(kb.nr_b1_byte()) << 8));
    const Joystick& joy = impl_->emu.joystick();
    st.joy_left12  = joy.joy_left_bits();
    st.joy_right12 = joy.joy_right_bits();
    st.port_1f     = joy.read_port_1f();
    st.port_37     = joy.read_port_37();
    return st;
}

// ---------------------------------------------------------------------------
// INS-18 — border
// ---------------------------------------------------------------------------

Result Debugger::set_border(ClientId by, uint8_t colour) {
    const uint8_t old_colour = impl_->emu.ula().get_border();
    impl_->emu.ula().set_border(colour);
    impl_->log_mutate(by, "border", old_colour, colour & 0x07);
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
