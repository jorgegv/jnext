// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.2 CAP-INS (read) and §4.2a (write).
//
// Work package B1 of epic #276. Every read below goes through an accessor that
// already existed or through one this branch ADDED, and nothing here
// reimplements a decode the emulator owns (the sprite attribute decode, the
// palette expansion, the raster derivation and the ULA's screen selection are
// all read from their owners, not recomputed).
//
// THE ADDITIONS, by owner, so the list is checkable a line at a time. §4 names
// five things ("sprites/palette raw forms, `set_matrix_bit`, the DMA slot flag,
// `input_state`"); implementing them took twelve functions, because a raw "form"
// is a getter and a setter and because three of the published return types are
// {ptr, size} VIEWS that need a data pointer to hand out:
//
//   Mmu            nr_page_ptr (x2, const + mutable), rom_image_ptr, get_nr_page
//                  — plus peek(), which is F1 and not one of these
//   SpriteEngine   write_attr_byte_at, write_pattern_byte_at, pattern_ram_data
//   PaletteManager entry_rgb333, set_entry_rgb333
//   Keyboard       set_matrix_bit (made public), matrix_row
//   Copper         program_data
//   Emulator       slot_ran_dma
//
// `input_state()` is the fifth of §4's five and is NOT in that list: it is a
// Debugger method below, composed from Keyboard and Joystick accessors.
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

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "debug/disasm.h"
#include "debug/disasm_text.h"
#include "debug/raster_state.h"
#include "debug/rewind_buffer.h"
#include "debug/trace.h"
#include "peripheral/ctc.h"
#include "peripheral/dma.h"
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
        // The identity-only kinds (GH #339) are refused before anything can be
        // mutated, so no MUTATE line is ever written for one; they are listed so
        // this switch stays total and `-Werror=switch` notices the next kind.
        case MemSpace::Kind::BootRom:
        case MemSpace::Kind::MultifaceRom:
        case MemSpace::Kind::MultifaceRam:
        case MemSpace::Kind::DivMmcRom:
        case MemSpace::Kind::DivMmcRam:
        case MemSpace::Kind::AltRom:
        case MemSpace::Kind::Layer2:
        case MemSpace::Kind::Config:
        case MemSpace::Kind::Floating:
            std::snprintf(buf, sizeof(buf), "mem kind%u:%u:0x%04X",
                          static_cast<unsigned>(space.kind),
                          static_cast<unsigned>(space.index), addr);
            break;
    }
    return buf;
}

/// Kinds the read arbiter NAMES (`Mmu::route()`, GH #339) that `poke` does not
/// write: it refuses them with `Result::Unsupported` until a consumer adds the
/// access with its own rows. `peek` reads `DivMmcRam` (GH #351) and refuses the
/// rest the same way; `poke(DivMmcRam)` stays refused. One total
/// switch, no `default`, so `-Werror=switch` makes a new kind a compile error
/// here instead of a silent fall-through into a store it does not address.
bool identity_only(MemSpace::Kind kind) {
    switch (kind) {
        case MemSpace::Kind::Cpu:
        case MemSpace::Kind::Page:
        case MemSpace::Kind::Rom:
            return false;
        case MemSpace::Kind::BootRom:
        case MemSpace::Kind::MultifaceRom:
        case MemSpace::Kind::MultifaceRam:
        case MemSpace::Kind::DivMmcRom:
        case MemSpace::Kind::DivMmcRam:
        case MemSpace::Kind::AltRom:
        case MemSpace::Kind::Layer2:
        case MemSpace::Kind::Config:
        case MemSpace::Kind::Floating:
            return true;
    }
    return true;
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

}  // namespace

void Debugger::Impl::note_mutation() {
    // The rewind ring is the local machine's: a remote has none to mark.
    if (!emu) return;
    if (RewindBuffer* rb = emu->rewind_buffer()) rb->mark_mutated(emu->clock().get());
}

// ---------------------------------------------------------------------------
// INS-01 — registers
// ---------------------------------------------------------------------------

Z80Registers Debugger::registers() const {
    return impl_->target.registers();
}

// §4.2a — one register at a time. There is deliberately no "set all": the only
// setter the CPU offers assigns the whole struct, which is how a frontend
// clobbers I and R while writing A (a real GDB `G`-packet hazard), so the whole
// struct is read, one field is changed, and it goes back.
Result Debugger::set_register(ClientId by, RegId reg, uint16_t value) {
    if (impl_->rzx_active()) return Result::RefusedRzx;

    // The old value and the name for the MUTATE line, from a copy: the target
    // does the write (`apply_register()` is the one register table).
    Z80Registers r = impl_->target.registers();
    long long old_value = 0;
    const char* name = "?";
    if (!apply_register(r, reg, value, &old_value, &name))
        return Result::Unsupported;   // not a register; see RegId::Count

    const Result res = impl_->target.set_register(reg, value);
    if (res != Result::Ok) return res;
    impl_->log_mutate(by, std::string("reg ") + name, old_value, value);
    impl_->note_mutation();
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

    // The identity-only kinds (GH #339): named by the read arbiter, accessed by no
    // verb yet. `DivMmcRam` (GH #351) and the three spaces below are the readable
    // ones; the facade refuses the rest, whatever the target is.
    switch (space.kind) {
        case MemSpace::Kind::Cpu:
        case MemSpace::Kind::Page:
        case MemSpace::Kind::Rom:
        case MemSpace::Kind::DivMmcRam:
            break;
        case MemSpace::Kind::BootRom:
        case MemSpace::Kind::MultifaceRom:
        case MemSpace::Kind::MultifaceRam:
        case MemSpace::Kind::DivMmcRom:
        case MemSpace::Kind::AltRom:
        case MemSpace::Kind::Layer2:
        case MemSpace::Kind::Config:
        case MemSpace::Kind::Floating:
            return make_refused<size_t>(Result::Unsupported);
    }

    // A target's answer is untrusted (it may be a network peer): the count is
    // clamped to the request, and a short count under an Ok is reported as short.
    Expected<size_t> r = impl_->target.read(space, addr, n, buf);
    if (r.value > n) r.value = n;
    if (r.status == Result::Ok && r.value < n) r.status = Result::RefusedUnavailable;
    return r;
}

Expected<size_t> Debugger::poke(ClientId by, MemSpace space, uint32_t addr,
                                size_t n, const uint8_t* buf) {
    if (!buf) return make_refused<size_t>(Result::RefusedUnavailable);
    if (impl_->rzx_active()) return make_refused<size_t>(Result::RefusedRzx);

    // An identity-only kind (GH #339) names no store a verb can write: refused
    // BEFORE anything is written or logged, like every refusal below.
    if (identity_only(space.kind)) return make_refused<size_t>(Result::Unsupported);

    // ROM is read-only in both spellings: `Rom{}` by kind, and a ROM-class page
    // under `Page{}` by its sentinel. Refused BEFORE anything is written, so a
    // refused poke never half-lands.
    if (space.kind == MemSpace::Kind::Rom)
        return make_refused<size_t>(Result::RefusedReadOnly);
    if (space.kind == MemSpace::Kind::Page &&
        (space.index == PAGE_SENTINEL_ROM_LO || space.index == PAGE_SENTINEL_ROM_HI))
        return make_refused<size_t>(Result::RefusedReadOnly);

    // The byte the MUTATE line says was replaced (§4.2a `<old> -> <new>`), read
    // through the target BEFORE the write (a read has no side effect, F1). GH #381:
    // a poke(Page) used to log 0 for every old byte, so a trainer that changed 0x35
    // to 0x00 read "0x0 -> 0x0", as if nothing had happened. It stays 0 when the
    // read is short or refused (an address past the page, a page that does not exist).
    uint8_t old_first = 0;
    if (n > 0) (void)impl_->target.read(space, addr, 1, &old_first);

    Expected<size_t> w = impl_->target.write(space, addr, n, buf);
    if (w.value > n) w.value = n;   // untrusted: never more than was asked

    if (space.kind == MemSpace::Kind::Cpu) {
        // The count is what LANDED. A MUTATE line only for what changed: a byte
        // dropped on ROM mutated nothing, and the line would say it had.
        const size_t landed = w.value;
        if (landed == n && n == 1)
            impl_->log_mutate(by, space_name(space, addr), old_first, buf[0]);
        else if (landed > 0)
            impl_->log_mutate_range(by, space_name(space, addr),
                                    landed == n ? std::to_string(n) + " bytes"
                                                : std::to_string(landed) + " of " +
                                                      std::to_string(n) +
                                                      " bytes (the rest read-only)");
        if (landed > 0) impl_->note_mutation();
        // Ok iff all landed; a target that refused outright (a link that is down)
        // keeps its own answer, a partial write is the read-only refusal.
        const Result st = landed == n ? Result::Ok
                          : (w.status == Result::Ok || w.status == Result::RefusedReadOnly)
                                ? Result::RefusedReadOnly
                                : w.status;
        return Expected<size_t>{st, landed};
    }

    // Page. A page the store does not have is refused before anything is logged, and
    // so is any status outside the local answers (Ok, short, InvalidPage): a target
    // that failed outright (a link that is down) gets its own error back and no
    // MUTATE line for a write that did not happen.
    if (w.status == Result::InvalidPage) return make_refused<size_t>(Result::InvalidPage);
    if (w.status != Result::Ok && w.status != Result::RefusedUnavailable)
        return Expected<size_t>{w.status, w.value};
    const size_t done = w.value;
    if (n == 1) impl_->log_mutate(by, space_name(space, addr), old_first, buf[0]);
    else        impl_->log_mutate_range(by, space_name(space, addr),
                                        std::to_string(done) + " bytes");
    impl_->note_mutation();
    return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
}

// ---------------------------------------------------------------------------
// INS-03 — MMU slots and paging
// ---------------------------------------------------------------------------

std::array<SlotInfo, 8> Debugger::mmu_slots() const {
    return impl_->target.mmu_slots();
}

Result Debugger::set_mmu_slot(ClientId by, int slot, uint8_t page) {
    if (slot < 0 || slot > 7) return Result::RefusedUnavailable;
    if (impl_->rzx_active()) return Result::RefusedRzx;

    const uint8_t old_page = impl_->target.mmu_slots()[static_cast<size_t>(slot)].nr_page;
    const Result r = impl_->target.set_mmu_slot(slot, page);
    if (r != Result::Ok) return r;
    impl_->log_mutate(by, "mmu slot " + std::to_string(slot), old_page, page);
    impl_->note_mutation();
    return Result::Ok;
}

// GH #12 — the ROM image legacy paging selects: `Mmu::current_sram_rom()`,
// which is VHDL `sram_rom` with the machine type and the NR 0x8C locks already
// folded in. A whole `MemSpace`, so a client that wants the ROM behind a slot
// that is RAM right now does not compose one from the paging ports (the
// design's first formula for that, "7FFD b4 | 1FFD b2", was wrong on the Next
// and ignored the locks everywhere).
MemSpace Debugger::rom_select() const {
    return impl_->target.rom_select();
}

PagingPorts Debugger::paging_ports() const {
    if (!impl_->emu) return PagingPorts{};
    PagingPorts p;
    const Mmu& mmu = impl_->emu->mmu();
    p.port_7ffd = mmu.port_7ffd();
    p.port_1ffd = mmu.port_1ffd();
    p.port_dffd = mmu.port_dffd_reg();
    return p;
}

// ---------------------------------------------------------------------------
// INS-04 — NextREG
// ---------------------------------------------------------------------------

uint8_t Debugger::nextreg_peek(uint8_t reg) const {
    return impl_->target.nextreg_read(reg);
}

Result Debugger::nextreg_write(ClientId by, uint8_t reg, uint8_t value) {
    if (impl_->rzx_active()) return Result::RefusedRzx;

    // A MUTATION, and §4.2a lets a handler make one - EXCEPT the one NextREG
    // write that resets the machine: NR 0x02 with bit 0 set and bit 1 clear runs
    // `Emulator::soft_reset()` synchronously (bit 1 is a HARD reset, which is
    // deferred to the loop owner and so harmless here). From inside a delivery
    // that re-`init()`s the machine under the frame running the handler (§5; fix
    // round 1b). Every other register, and this one's other bits, stay writable.
    // Local machine only: a remote has no delivery to be inside.
    if (impl_->is_local() && reg == 0x02 && (value & 0x03) == 0x01) {
        if (const Result nested = impl_->refuse_inside_delivery("nextreg_write");
            nested != Result::Ok)
            return nested;
    }

    const uint8_t old_value = impl_->target.nextreg_read(reg);
    const Result r = impl_->target.nextreg_write(reg, value);
    if (r != Result::Ok) return r;
    char what[24];
    std::snprintf(what, sizeof(what), "nextreg 0x%02X", reg);
    impl_->log_mutate(by, what, old_value, value);
    impl_->note_mutation();
    return Result::Ok;
}

uint8_t Debugger::nextreg_selected() const {
    if (!impl_->emu) return 0;
    return impl_->emu->nextreg().selected();
}

// ---------------------------------------------------------------------------
// INS-05 — ports. PERTURBING BY NATURE, and the only read in this file that is.
// ---------------------------------------------------------------------------

Expected<uint8_t> Debugger::port_in(ClientId by, uint16_t port) {
    // Refused while an RZX records or plays, as port_out() is. The read goes
    // through PortDispatch::read(), which never touches the RZX log: nothing is
    // recorded and no recorded value is consumed. What it does do is dispatch
    // the LIVE handler, and a port read has side effects (a UART RX pop and its
    // interrupt, the SPI shift, a Multiface strobe that pages memory). During a
    // recording that changes the machine in a way the file cannot replay;
    // during a playback it touches hardware the replay otherwise never reads
    // (GH #283).
    if (impl_->rzx_active()) return make_refused<uint8_t>(Result::RefusedRzx);
    const Expected<uint8_t> in = impl_->target.port_in(port);
    if (in.status != Result::Ok) return in;
    const uint8_t v = in.value;
    char what[24];
    std::snprintf(what, sizeof(what), "port in 0x%04X", port);
    impl_->log_mutate(by, what, v, v);
    impl_->note_mutation();   // the read's side effects are the change (§4.2a wall)
    return make_ok<uint8_t>(v);
}

Result Debugger::port_out(ClientId by, uint16_t port, uint8_t value) {
    if (impl_->rzx_active()) return Result::RefusedRzx;
    // The same soft-reset exception as `nextreg_write()`, reached through the
    // NextREG data port (a full 16-bit match on 0x253B) with NR 0x02 selected.
    // Local machine only.
    if (impl_->is_local() && port == 0x253B && impl_->emu->nextreg().selected() == 0x02 &&
        (value & 0x03) == 0x01) {
        if (const Result nested = impl_->refuse_inside_delivery("port_out");
            nested != Result::Ok)
            return nested;
    }
    const Result r = impl_->target.port_out(port, value);
    if (r != Result::Ok) return r;
    char what[24];
    std::snprintf(what, sizeof(what), "port out 0x%04X", port);
    // The value in HEX, as the `0x` says: `std::to_string` here once spelled
    // 0x15 as "0x21" (GH #12).
    char val[12];
    std::snprintf(val, sizeof(val), "= 0x%02X", value);
    impl_->log_mutate_range(by, what, val);
    impl_->note_mutation();
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// INS-06 / INS-07 / INS-19 — raster, time, machine
// ---------------------------------------------------------------------------

// GH #278 WP7 — a PAUSED machine's raster position is taken HERE, at the
// query, for every client. `Emulator::snapshot_raster()` (the clock-derived
// paused_hc/paused_vc) used to be taken only by the Qt debugger's own refresh,
// through its `Emulator*`, so a script or a remote client asking a machine it
// had paused read whatever the last Qt refresh left behind. While paused the
// snapshot is a pure function of the clock and the frame start, neither of
// which moves, so taking it on every query is idempotent; while RUNNING it is
// left alone, exactly as before (the value of the last pause).
static void snapshot_if_paused(Emulator& emu) {
    if (emu.debug_state().paused()) emu.snapshot_raster();
}

// GH #26 WP9 — where the beam is NOW, from the clock: the arithmetic of
// `Emulator::snapshot_raster()`, without storing it. Every caller of raster()
// and time() runs on the emulation thread between instructions (a delivery, a
// pump, a Qt tick), so the clock is always at a boundary and this is exact.
// Running, raster() used to return the LAST PAUSE's snapshot, so a script's
// RAW_VC / CVC / HC_ULA in a `scanline` or `execute` rule read a constant.
// The snapshot itself is still left alone while running (the Qt video panel
// shows it only when paused).
static void live_counters(Emulator& emu, int& vc, int& hc) {
    const uint64_t elapsed = emu.clock().get() - emu.current_frame_cycle();
    const uint64_t mcl     = emu.timing().master_cycles_per_line;
    vc = static_cast<int>(elapsed / mcl);
    hc = static_cast<int>((elapsed % mcl) / 4);
}

RasterState Debugger::raster() const {
    if (!impl_->emu) return RasterState{};
    snapshot_if_paused(*impl_->emu);
    // The clock-derived counters rather than VideoTiming::pos() (the latter
    // only advances while a debugger is attached; the clock is what every other
    // raster consumer uses — paused, they equal the paused_hc/paused_vc
    // snapshot), and the LIVE ULA mode registers, because they decide WHAT is
    // being fetched.
    int vc = 0, hc = 0;
    live_counters(*impl_->emu, vc, hc);
    return raster_state_at(impl_->emu->video_timing(), hc, vc,
                           impl_->emu->ula().get_screen_mode_reg(),
                           impl_->emu->ula().get_shadow_screen_en());
}

Time Debugger::time() const {
    if (!impl_->emu) return Time{};
    snapshot_if_paused(*impl_->emu);
    Time t;
    t.master_cycle   = impl_->emu->clock().get();
    t.tstates_total  = impl_->emu->monotonic_tstates();
    t.frame          = frame_tag(*impl_->emu);          // F2
    t.cycle_in_frame = t.master_cycle - impl_->emu->current_frame_cycle();
    int vc = 0, hc = 0;
    live_counters(*impl_->emu, vc, hc);
    t.vc_raw         = static_cast<int16_t>(vc);
    t.hc_raw         = static_cast<int16_t>(hc);
    return t;
}

uint64_t Debugger::time_epoch() const { return impl_->emu ? impl_->emu->time_epoch() : 0; }

MachineInfo Debugger::machine() const {
    if (!impl_->emu) return MachineInfo{};
    MachineInfo mi;
    const MachineTiming& t  = impl_->emu->timing();
    const VideoTiming&   vt = impl_->emu->video_timing();

    mi.type        = impl_->emu->config().type;
    mi.cpu_divisor = impl_->emu->clock().cpu_divisor();

    mi.tstates_per_line          = t.tstates_per_line;
    mi.tstates_per_frame         = t.tstates_per_frame;
    mi.master_cycles_per_line    = t.master_cycles_per_line;
    mi.master_cycles_per_frame   = t.master_cycles_per_frame;

    mi.lines      = t.lines_per_frame;
    // Derived from the live frame length, not a nominal 50: a 60 Hz NR 0x05 or a
    // machine-type change moves it, which is the whole reason INS-19 carries it.
    mi.fps        = 1000.0 / impl_->emu->frame_period_ms();
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
    // Always exactly SPRITE_COUNT entries, whatever the target returns.
    std::vector<SpriteInfo> out = impl_->target.sprites();
    out.resize(SPRITE_COUNT);
    return out;
}

Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>>
Debugger::sprite_attr_raw(uint8_t idx) const {
    using Bytes = std::array<uint8_t, SPRITE_ATTR_BYTES>;
    if (idx >= SPRITE_COUNT) return make_refused<Bytes>(Result::RefusedUnavailable);
    return impl_->target.sprite_attr_raw(idx);
}

Result Debugger::set_sprite_attr_raw(ClientId by, uint8_t idx,
                                     const uint8_t* bytes, size_t n) {
    if (!bytes || n != SPRITE_ATTR_BYTES) return Result::RefusedUnavailable;
    if (idx >= SPRITE_COUNT) return Result::RefusedUnavailable;
    if (impl_->rzx_active()) return Result::RefusedRzx;

    const Result r = impl_->target.set_sprite_attr_raw(idx, bytes, n);
    if (r != Result::Ok) return r;
    impl_->log_mutate_range(by, "sprite " + std::to_string(idx), "5 attribute bytes");
    impl_->note_mutation();
    return Result::Ok;
}

ConstBytes Debugger::pattern_ram() const {
    // A view of anything but exactly the pattern RAM, or with no data, is empty.
    const ConstBytes v = impl_->target.pattern_ram();
    if (!v.data || v.size != PATTERN_RAM_BYTES) return ConstBytes{};
    return v;
}

Result Debugger::write_pattern_ram(ClientId by, uint16_t addr,
                                   const uint8_t* src, size_t n) {
    if (!src) return Result::RefusedUnavailable;
    if (impl_->rzx_active()) return Result::RefusedRzx;

    const Result r = impl_->target.write_pattern_ram(addr, src, n);
    if (r != Result::Ok) return r;
    char what[32];
    std::snprintf(what, sizeof(what), "sprite pattern 0x%04X", static_cast<unsigned>(addr));
    impl_->log_mutate_range(by, what, std::to_string(n) + " bytes");
    impl_->note_mutation();
    return Result::Ok;
}

Expected<uint16_t> Debugger::sprite_palette_rgb333(int bank, uint8_t index) const {
    if (bank != 0 && bank != 1) return make_refused<uint16_t>(Result::RefusedUnavailable);
    return impl_->target.sprite_palette_rgb333(bank, index);
}

ClipWindow Debugger::sprite_clip() const {
    return impl_->target.sprite_clip();
}

// ---------------------------------------------------------------------------
// INS-09 — copper
// ---------------------------------------------------------------------------

CopperState Debugger::copper() const {
    if (!impl_->emu) return CopperState{};
    CopperState cs;
    const Copper& c = impl_->emu->copper();
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
    if (!impl_->emu) return make_refused<std::array<uint8_t, AY_REGISTER_COUNT>>(Result::Unsupported);
    using Regs = std::array<uint8_t, AY_REGISTER_COUNT>;
    if (chip < 0 || chip >= static_cast<int>(AY_CHIP_COUNT))
        return make_refused<Regs>(Result::RefusedUnavailable);
    Regs regs{};
    const AyChip& ay = impl_->emu->turbosound().ay(chip);
    for (size_t i = 0; i < AY_REGISTER_COUNT; ++i)
        regs[i] = ay.read_register(static_cast<uint8_t>(i));
    return make_ok<Regs>(regs);
}

bool Debugger::turbosound_enabled() const {
    if (!impl_->emu) return false;
    // The LIVE signal (`turbosound_en_i` from NR 0x08 bit 1), not the NextREG
    // shadow — INS-10's whole point.
    return impl_->emu->turbosound().enabled();
}

AyChipMode Debugger::ay_mode() const {
    if (!impl_->emu) return AyChipMode{};
    // `TurboSound::ay_mode()` is a bool: true = AY curve, false = YM. NOT the
    // whole NR 0x06 field (bits 1:0, four values, one of which holds the chips
    // in reset) — INS-10 asks only for this accessor, and a frontend that needs
    // the reset state reads NR 0x06 through nextreg_peek().
    return impl_->emu->turbosound().ay_mode() ? AyChipMode::Ay : AyChipMode::Ym;
}

StereoMode Debugger::stereo_mode() const {
    if (!impl_->emu) return StereoMode{};
    return impl_->emu->turbosound().stereo_mode() ? StereoMode::Acb : StereoMode::Abc;
}

uint8_t Debugger::audio_mute_mask() const {
    if (!impl_->emu) return 0;
    return impl_->emu->audio_mute_mask();
}

Result Debugger::set_audio_mute_mask(ClientId by, uint8_t mask) {
    if (!impl_->emu) return Result::Unsupported;
    const uint8_t old_mask = impl_->emu->audio_mute_mask();
    impl_->emu->set_audio_mute_mask(mask);
    impl_->log_mutate(by, "audio mute mask", old_mask, mask);
    return Result::Ok;
}

uint16_t Debugger::audio_channel_mute_mask() const {
    if (!impl_->emu) return 0;
    return impl_->emu->audio_channel_mute_mask();
}

Result Debugger::set_audio_channel_mute_mask(ClientId by, uint16_t mask) {
    if (!impl_->emu) return Result::Unsupported;
    const uint16_t old_mask = impl_->emu->audio_channel_mute_mask();
    impl_->emu->set_audio_channel_mute_mask(mask);
    // The logged value is the stored one, after masking.
    impl_->log_mutate(by, "audio channel mute mask", old_mask, impl_->emu->audio_channel_mute_mask());
    return Result::Ok;
}

Result Debugger::clear_audio_drops(ClientId by) {
    if (!impl_->emu) return Result::Unsupported;
    uint64_t total = 0;
    for (const AudioDropCounter& c : impl_->emu->audio_drops()) total += c.count;
    impl_->emu->clear_audio_drops();
    impl_->log_mutate(by, "audio drop counters", total, 0);
    return Result::Ok;
}

// INS-25 -- the CTC's four channels. A read of the inline DebugViews: nothing here ticks, writes or
// calls Ctc::read(). The ZC/TO rate walks the chain back at most four hops (zxnext.vhd:4598: channel n is
// clocked by n-1, channel 0 by 3), dividing by each time constant; a timer ends the walk by contributing
// its prescaler. One rounding, at the end.
std::array<CtcChannelState, CTC_CHANNEL_COUNT> Debugger::ctc_state() const {
    if (!impl_->emu) return {};
    const Ctc& ctc = impl_->emu->ctc();
    std::array<CtcChannel::DebugView, CTC_CHANNEL_COUNT> v;
    for (size_t c = 0; c < CTC_CHANNEL_COUNT; ++c) v[c] = ctc.channel(static_cast<int>(c)).debug_view();

    std::array<CtcChannelState, CTC_CHANNEL_COUNT> out{};
    for (size_t c = 0; c < CTC_CHANNEL_COUNT; ++c) {
        CtcChannelState& s = out[c];
        const unsigned tc = v[c].time_constant ? v[c].time_constant : 256u;
        s.counter_mode = v[c].counter_mode;
        s.prescaler = v[c].prescale_256 ? 256 : 16;
        s.time_constant = static_cast<uint16_t>(tc);
        s.count = v[c].count;
        s.running = v[c].running;
        s.int_enabled = v[c].int_enabled;

        uint64_t div = 1;
        size_t at = c;
        bool source = false;
        for (int hop = 0; hop < static_cast<int>(CTC_CHANNEL_COUNT); ++hop) {
            if (!v[at].running) break;
            div *= v[at].time_constant ? v[at].time_constant : 256u;
            if (!v[at].counter_mode) {
                div *= v[at].prescale_256 ? 256u : 16u;
                source = true;
                break;
            }
            at = (at + CTC_CHANNEL_COUNT - 1) % CTC_CHANNEL_COUNT;
        }
        if (source) s.zc_rate_millihz = static_cast<uint32_t>((MASTER_CLOCK_HZ * 1000ULL + div / 2) / div);
    }
    return out;
}

// INS-25 -- the DMA's pacing and transfer state.
DmaState Debugger::dma_state() const {
    if (!impl_->emu) return DmaState{};
    const Dma& dma = impl_->emu->dma();
    const Dma::DebugView v = dma.debug_view();
    DmaState s;
    s.active = dma.state() == Dma::State::TRANSFERRING;
    s.mode = static_cast<uint8_t>(dma.transfer_mode());
    s.prescaler = v.prescaler;
    if (v.prescaler) {
        const uint64_t d = 32ULL * v.prescaler;
        s.rate_millihz = static_cast<uint32_t>((MASTER_CLOCK_HZ * 1000ULL + d / 2) / d);
    }
    s.src = dma.src_addr();
    s.dst = dma.dst_addr();
    s.length = dma.block_length();
    s.counter = dma.counter();
    const bool a_to_b = dma.dir_a_to_b();
    s.src_io = a_to_b ? v.port_a_io : v.port_b_io;
    s.dst_io = a_to_b ? v.port_b_io : v.port_a_io;
    return s;
}

// INS-22 -- the decoded audio chain. Every source is a const accessor or a
// peek: nothing here runs a NextREG read handler or a port read.
AudioState Debugger::audio_state() const {
    if (!impl_->emu) return AudioState{};
    Emulator& emu = *impl_->emu;
    const TurboSound& ts = emu.turbosound();
    AudioState st;

    st.psg_clock_hz = static_cast<uint32_t>(MASTER_CLOCK_HZ / PSG_CLOCK_DIVISOR);
    st.active_chip = static_cast<uint8_t>(ts.selected_index());
    st.turbosound = ts.enabled();
    st.stereo = ts.stereo_mode() ? StereoMode::Acb : StereoMode::Abc;
    st.psg_mode = emu.nextreg().peek(0x06) & 0x03;
    st.held_in_reset = ts.reset_held();

    const uint8_t ports = emu.effective_internal_port_enable(0x84);
    st.ay_ports_enabled = (ports & 0x01) != 0;
    st.dac.port_families = ports & 0xFE;
    for (size_t i = 0; i < DAC_CHANNEL_COUNT; ++i)
        st.dac.channels[i] = emu.dac().channel(static_cast<int>(i));
    st.dac.enabled = emu.dac_enabled();

    st.beeper.ear = emu.beeper().ear();
    st.beeper.mic = emu.beeper().mic();
    st.beeper.tape_ear = emu.beeper().tape_ear();
    st.beeper.speaker_enabled = (emu.nr_08_stored_low() & 0x10) != 0;
    st.beeper.beep_speaker_only = emu.nr_06_internal_speaker_beep();
    st.beeper.excluded = emu.beep_spkr_excl();

    const uint8_t a2 = emu.i2s().nr_a2_ctl();
    st.pi.left_enabled = (a2 & 0x80) != 0;
    st.pi.right_enabled = (a2 & 0x40) != 0;
    st.pi.to_ear = emu.i2s().ear_routed();

    const uint8_t mute = emu.audio_mute_mask();
    const uint16_t channel_mute = emu.audio_channel_mute_mask();
    st.host.output_gain_db = emu.mixer().output_gain_db();
    st.host.beeper_gain_db = emu.mixer().beeper_gain_db();
    st.host.dac_gain_db = emu.mixer().dac_gain_db();
    for (size_t c = 0; c < AY_CHIP_COUNT; ++c)
        st.host.ay_gain_db[c] = emu.mixer().ay_gain_db(static_cast<int>(c));
    st.host.mute_mask = mute;
    st.host.channel_mute_mask = channel_mute;
    // The user setting, not audio_off_: a HALT batch raises that for one call.
    st.host.synthesis_on = !emu.config().silent;
    st.drops = emu.audio_drops();

    for (size_t c = 0; c < AY_CHIP_COUNT; ++c) {
        const int ci = static_cast<int>(c);
        const AyChip& ay = ts.ay(ci);
        AyChipState& cs = st.chips[c];
        for (size_t r = 0; r < AY_REGISTER_COUNT; ++r)
            cs.regs[r] = ay.read_register(static_cast<uint8_t>(r));
        cs.address = ay.address();
        cs.pan = ts.pan(ci);
        cs.mono = ((ts.mono_mode() >> c) & 1) != 0;
        cs.reaches_mix = (ts.selected_index() == ci) || ts.enabled();
        cs.noise_period = cs.regs[6] & 0x1F;

        const AyChip::DebugView dv = ay.debug_view();
        for (size_t k = 0; k < AY_CHANNELS_PER_CHIP; ++k) {
            AyChannelState& ch = cs.channels[k];
            ch.tone_period = static_cast<uint16_t>(((cs.regs[2 * k + 1] & 0x0F) << 8) | cs.regs[2 * k]);
            ch.tone_enabled = ((cs.regs[7] >> k) & 1) == 0;
            ch.noise_enabled = ((cs.regs[7] >> (k + 3)) & 1) == 0;
            ch.volume = cs.regs[8 + k] & 0x0F;
            ch.uses_envelope = (cs.regs[8 + k] & 0x10) != 0;
            ch.amplitude = dv.amplitude[k];
            ch.muted = ((mute >> c) & 1) != 0 ||
                       ((channel_mute >> (AY_CHANNELS_PER_CHIP * c + k)) & 1) != 0;
        }
        cs.envelope.period = static_cast<uint16_t>((cs.regs[12] << 8) | cs.regs[11]);
        cs.envelope.shape = cs.regs[13] & 0x0F;
        cs.envelope.step = dv.env_step;
        cs.envelope.motion = dv.env_hold ? EnvelopeMotion::Holding
                           : dv.env_inc  ? EnvelopeMotion::Rising
                                         : EnvelopeMotion::Falling;
    }
    return st;
}

// ---------------------------------------------------------------------------
// INS-11 — disassembly
//
// All four read guest memory through `memory_reader()`, i.e. through
// `Mmu::peek()` (no floating-bus latch): a disassembly is an observation, and
// before F1 the panel's own read was the one that latched the floating bus. The
// instruction at the PC is the exception (GH #360): its bytes are the ones the
// CPU will fetch, which differ from a peek at a DivMMC instant entry.
// ---------------------------------------------------------------------------

DisasmReadFn Debugger::memory_reader() const {
    if (!impl_->emu) {
        // A non-local target: plain Cpu reads through it, no fetch prediction.
        Target* t = &impl_->target;
        return [t](uint16_t a) -> uint8_t {
            uint8_t b = 0;
            (void)t->read(MemSpace::cpu(), a, 1, &b);
            return b;
        };
    }
    Emulator* emu = impl_->emu;
    // GH #360 F1: the instruction at the PC is read as the CPU WILL fetch it, not
    // as the data view shows it. At a DivMMC instant entry the M1 itself maps
    // DivMMC (design 4.4), so a peek of the PC line shows the bytes of the map
    // before the M1 while the CPU executes others. Every other address is a peek.
    const uint16_t pc = emu->cpu().get_registers().PC;
    uint8_t b[4];
    emu->predict_instruction_bytes(pc, b);
    auto peek = [emu](uint16_t a) -> uint8_t { return emu->mmu().peek(a); };
    const auto mixed = [pc, b, peek](uint16_t a) -> uint8_t {
        const uint16_t off = static_cast<uint16_t>(a - pc);
        return off < 4 ? b[off] : peek(a);
    };
    // Only the bytes of the PC instruction itself are predicted; what follows it
    // is the next instruction, which the CPU fetches after the M1 has run.
    const int n = std::min(4, ::instruction_length(pc, mixed));
    return [pc, b, n, peek](uint16_t addr) -> uint8_t {
        const uint16_t off = static_cast<uint16_t>(addr - pc);
        return off < n ? b[off] : peek(addr);
    };
}

DisasmReadFn Debugger::operand_reader() const {
    // A non-local target has no fetch prediction: the same plain reads.
    if (!impl_->emu) return memory_reader();
    Emulator* emu = impl_->emu;
    return emu->predict_data_reader(emu->cpu().get_registers().PC);
}

std::vector<DisasmLine> Debugger::disassemble(uint16_t addr, size_t n,
                                              const SymbolTable* syms) const {
    std::vector<DisasmLine> out;
    out.reserve(n);
    const DisasmReadFn read = memory_reader();
    const disasm_text::SymbolNamer namer = disasm_text::symbol_namer(
        syms, [this](uint16_t a) { return source_page(a); });
    uint16_t pc = addr;
    for (size_t i = 0; i < n; ++i) {
        DisasmLine line = disasm_one(pc, read);
        if (syms) {
            // ONE substitution rule, shared with the panel's painter and with
            // the clipboard path (`disasm_text::apply_symbols`), so a symbol
            // cannot be spelled one way in a panel and another in a script.
            const std::string m = disasm_text::apply_symbols(line.mnemonic, namer);
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
    if (!impl_->emu) { static const std::vector<CallFrame> none; return none; }
    return impl_->emu->call_stack().frames();
}

bool Debugger::call_stack_enabled() const {
    if (!impl_->emu) return false;
    return impl_->emu->call_stack().enabled();
}

Result Debugger::set_call_stack_enabled(ClientId by, bool enabled) {
    if (!impl_->emu) return Result::Unsupported;
    // GH #434 F-B — the request is the CLIENT's, on its row (so a reconstruct keeps it and a
    // detach drops it); the machine's state is derived from every row by `sync_call_stack()`.
    Impl::Client* c = impl_->find_client(by);
    if (!c) return Result::RefusedUnavailable;
    c->call_stack = enabled;
    impl_->sync_call_stack();
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

bool Debugger::trace_enabled() const { return impl_->emu && impl_->emu->trace_log().enabled(); }

Result Debugger::set_trace_enabled(bool enabled) {
    if (!impl_->emu) return Result::Unsupported;
    impl_->emu->trace_log().set_enabled(enabled);
    // GH #276 B3 — client intent, re-applied across a reconstruct
    // (`Impl::want_trace`).
    impl_->want_trace = enabled;
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

Result Debugger::trace_clear() {
    if (!impl_->emu) return Result::Unsupported;
    impl_->emu->trace_log().clear();
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

Result Debugger::trace_resize(size_t entries) {
    if (!impl_->emu) return Result::Unsupported;
    if (entries == 0) return Result::RefusedUnavailable;
    impl_->emu->trace_log().resize(entries);
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

Expected<std::vector<TraceEntry>> Debugger::trace_entries() const {
    if (!impl_->emu) return make_refused<std::vector<TraceEntry>>(Result::Unsupported);
    using Entries = std::vector<TraceEntry>;
    const TraceLog& log = impl_->emu->trace_log();
    if (!log.enabled()) return make_refused<Entries>(Result::RefusedUnavailable);
    Entries out;
    out.reserve(log.size());
    for (size_t i = 0; i < log.size(); ++i) out.push_back(log.at(i));
    return make_ok<Entries>(std::move(out));
}

Result Debugger::trace_export(const std::string& path) const {
    if (!impl_->emu) return Result::Unsupported;
    return impl_->emu->trace_log().export_to_file(path) ? Result::Ok
                                                       : Result::RefusedUnavailable;
}

// ---------------------------------------------------------------------------
// INS-20 — PC coverage (GH #276 B4)
//
// The set of PCs executed since the last clear. Written by
// `Emulator::step_one_instruction()` through `DebugState::coverage_sink()` — see
// there for why it is that body and not the armed() block §4.2 names, and why it
// asks the CPU whether the opcode was fetched.
//
// SWITCHING IT OFF CLEARS IT. The header promises `coverage()` is all-zero
// while off, and there are two ways to keep that promise: clear on disable, or
// keep the bits and hand out a zero set while off. The second leaves a set that
// no reader can see until coverage is switched back on, at which point bits
// from a PREVIOUS session reappear under a "since clear" that never happened.
// So off is a clear, `on` over `on` is not (idempotent, rows INS-20-02/03), and
// `coverage()` is always the one real set.
//
// NO CLIENT ID: the header declares none (the switch is not a mutation of the
// machine — nothing the guest can see changes), and it is not gated on anyone
// being attached, for the reason `step_one_instruction()` gives.
// ---------------------------------------------------------------------------

Result Debugger::coverage_enable(bool enabled) {
    if (!impl_->emu) return Result::Unsupported;
    impl_->coverage_on = enabled;
    if (!enabled) impl_->coverage.reset();
    impl_->publish_coverage();
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

bool Debugger::coverage_enabled() const { return impl_->emu && impl_->coverage_on; }

Result Debugger::coverage_clear() {
    if (!impl_->emu) return Result::Unsupported;
    impl_->coverage.reset();
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

const CoverageBits& Debugger::coverage() const { if (!impl_->emu) { static const CoverageBits none; return none; } return impl_->coverage; }

// ---------------------------------------------------------------------------
// INS-21 -- memory coverage by execute, read and write (GH #388, E23-25)
//
// The set is `Impl::mem_cov`; `Mmu` writes it through
// `DebugState::mem_coverage_live()` from the one arbiter (mmu.h `route_bits_`).
// SAME RULES AS INS-20: off clears, `on` over `on` does not, the set and its
// switch are backend state (published by `publish_mem_coverage()`, retired by
// `~Debugger()`), so a rewind, a reset, a state load and a cold boot keep the
// bits. A rewind neither removes nor adds a mark: a replay re-executes accesses
// already recorded and a set bit set again is the same bit.
// ---------------------------------------------------------------------------

Result Debugger::mem_coverage_enable(bool enabled) {
    if (!impl_->emu) return Result::Unsupported;
    if (enabled && !impl_->mem_cov) impl_->mem_cov = std::make_unique<MemCoverage>();
    impl_->mem_cov_on = enabled;
    if (!enabled && impl_->mem_cov) impl_->mem_cov->clear();
    impl_->publish_mem_coverage();
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

bool Debugger::mem_coverage_enabled() const { return impl_->emu && impl_->mem_cov_on; }

Result Debugger::mem_coverage_clear() {
    if (!impl_->emu) return Result::Unsupported;
    if (impl_->mem_cov) impl_->mem_cov->clear();
    impl_->bump_revision();   // GH #428
    return Result::Ok;
}

Expected<size_t> Debugger::mem_coverage(MemSpace space, uint32_t addr, size_t n,
                                        uint8_t* out) const {
    if (!impl_->emu) return make_refused<size_t>(Result::Unsupported);
    if (!out) return make_refused<size_t>(Result::RefusedUnavailable);
    const MemCoverage* cov = impl_->mem_cov.get();
    if (space.kind == MemSpace::Kind::Cpu) {
        // The live DATA route, as peek(Cpu): each byte of the window through
        // Mmu::route(), so a Layer 2 read mapping or the DivMMC shows its own map.
        for (size_t i = 0; i < n; ++i) {
            const uint16_t a = static_cast<uint16_t>((addr + i) & 0xFFFF);
            const Mmu::Route r = impl_->emu->mmu().route(a);
            out[i] = cov ? cov->get(r.space, r.offset) : 0;
        }
        return make_ok<size_t>(n);
    }
    // The store's own bounds, as peek() has them: an index outside the 8-bit
    // space, a Page the SRAM does not have (>= 0xE0), a Rom image > 3.
    if (space.index > 0xFF) return make_refused<size_t>(Result::InvalidPage);
    if (space.kind == MemSpace::Kind::Page && space.index >= 0xE0)
        return make_refused<size_t>(Result::InvalidPage);
    if (space.kind == MemSpace::Kind::Rom && space.index > 3)
        return make_refused<size_t>(Result::InvalidPage);
    const uint32_t size = space_size(space.kind);
    size_t done = 0;
    for (; done < n && addr + done < size; ++done)
        out[done] = cov ? cov->get(space, static_cast<uint16_t>(addr + done)) : 0;
    return Expected<size_t>{done == n ? Result::Ok : Result::RefusedUnavailable, done};
}

uint8_t Debugger::mem_coverage_fetch(uint16_t addr) const {
    if (!impl_->emu) return 0;
    if (!impl_->mem_cov) return 0;
    const MemSpace s = source_page(addr);
    return impl_->mem_cov->get(s, space_offset(s, addr));
}

std::vector<MemSpace> Debugger::mem_coverage_spaces() const {
    if (!impl_->emu) return {};
    return impl_->mem_cov ? impl_->mem_cov->spaces() : std::vector<MemSpace>{};
}

// ---------------------------------------------------------------------------
// INS-14 — framebuffer
// ---------------------------------------------------------------------------

ConstU32s Debugger::framebuffer() const {
    if (!impl_->emu) return ConstU32s{};
    Emulator& emu = *impl_->emu;
    return ConstU32s{emu.get_framebuffer(),
                     static_cast<size_t>(emu.get_framebuffer_width()) *
                         static_cast<size_t>(emu.get_framebuffer_height())};
}

// ---------------------------------------------------------------------------
// INS-15 — palettes, ULA screen state, clip windows
// ---------------------------------------------------------------------------

std::vector<uint16_t> Debugger::palette(PaletteId id) const {
    if (!impl_->emu) return {};
    ::PaletteId hw;
    if (!resolve_palette(*impl_->emu, id, hw)) return {};
    std::vector<uint16_t> out;
    out.reserve(PaletteManager::FULL_SIZE);
    for (int i = 0; i < PaletteManager::FULL_SIZE; ++i)
        out.push_back(impl_->emu->palette().entry_rgb333(hw, static_cast<uint8_t>(i)));
    return out;
}

std::vector<uint8_t> Debugger::palette_priority(PaletteId id) const {
    if (!impl_->emu) return {};
    ::PaletteId hw;
    if (!resolve_palette(*impl_->emu, id, hw)) return {};
    std::vector<uint8_t> out;
    out.reserve(PaletteManager::FULL_SIZE);
    for (int i = 0; i < PaletteManager::FULL_SIZE; ++i)
        out.push_back(impl_->emu->palette().entry_priority(hw, static_cast<uint8_t>(i)));
    return out;
}

Result Debugger::set_palette(ClientId by, PaletteId id, uint8_t index,
                             uint16_t rgb333) {
    if (!impl_->emu) return Result::Unsupported;
    ::PaletteId hw;
    if (!resolve_palette(*impl_->emu, id, hw)) return Result::RefusedUnavailable;
    if (impl_->emu->rzx_recorder().is_recording() ||
        impl_->emu->rzx_player().is_playing())
        return Result::RefusedRzx;

    const uint16_t old_value = impl_->emu->palette().entry_rgb333(hw, index);
    impl_->emu->palette().set_entry_rgb333(hw, index, rgb333);
    char what[40];
    std::snprintf(what, sizeof(what), "palette %u[%u]",
                  static_cast<unsigned>(hw), static_cast<unsigned>(index));
    impl_->log_mutate(by, what, old_value, rgb333 & 0x01FF);
    impl_->note_mutation();
    return Result::Ok;
}

uint8_t Debugger::active_ula_palette_bank() const {
    if (!impl_->emu) return 0;
    return impl_->emu->palette().active_ula_palette() ? 1 : 0;
}

UlaScreenRegs Debugger::ula_screen_regs() const {
    if (!impl_->emu) return UlaScreenRegs{};
    UlaScreenRegs r;
    const Ula& ula = impl_->emu->ula();
    r.active_bank     = ula.get_shadow_screen_en() ? 7 : 5;
    r.port_ff         = ula.get_screen_mode_reg();
    // NR 0x26 is the COARSE byte; the NR 0x68 b2 fine-scroll flag is a separate
    // register a frontend reads through nextreg_peek(), not part of this byte.
    r.scroll_x        = ula.get_ula_scroll_x_coarse();
    r.scroll_y        = ula.get_ula_scroll_y();
    r.border          = ula.get_border();
    r.second_palette  = impl_->emu->palette().active_ula_palette();
    r.enabled         = ula.ula_enabled();
    return r;
}

// CAP-02 (GH #276 B4) — the ULA layer's screen memory as a `.SCR` body: 6912
// bytes, or 12288 in a Timex hi-colour / hi-res mode. `Ula::screen_dump()` is
// const and reads the bank and window the LIVE register state selects, so this
// is an observation with no side effect, at any instant — mid-frame, paused, or
// between frames. It is the byte-diffable unit the #20 recorder compares.
std::vector<uint8_t> Debugger::ula_screen_dump() const {
    if (!impl_->emu) return {};
    return impl_->emu->ula().screen_dump();
}

ClipWindow Debugger::clip_window(ClipLayer layer) const {
    if (!impl_->emu && layer != ClipLayer::Sprites) return ClipWindow{};
    ClipWindow w;
    switch (layer) {
        case ClipLayer::Layer2: {
            const Layer2& l2 = impl_->emu->layer2();
            w.x1 = l2.clip_x1(); w.x2 = l2.clip_x2();
            w.y1 = l2.clip_y1(); w.y2 = l2.clip_y2();
            break;
        }
        case ClipLayer::Sprites:
            return sprite_clip();
        case ClipLayer::Ula: {
            const Ula& ula = impl_->emu->ula();
            w.x1 = ula.clip_x1(); w.x2 = ula.clip_x2();
            w.y1 = ula.clip_y1(); w.y2 = ula.clip_y2();
            break;
        }
        case ClipLayer::Tilemap: {
            const Tilemap& tm = impl_->emu->tilemap();
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
    if (!impl_->emu) return InputState{};
    InputState st;
    const Keyboard& kb = impl_->emu->keyboard();
    for (int r = 0; r < 8; ++r) st.matrix[r] = kb.matrix_row(r);
    // ACTIVE-HIGH, as NR 0xB0 / 0xB1 read it back — the opposite polarity from
    // `matrix`, which is why both are documented at the struct.
    // The written state, not the guest's view (Keyboard::latch_guest_view()):
    // inspection shows what is pressed, not what the guest has seen yet.
    st.ext_keys = kb.live_extended_keys();
    const Joystick& joy = impl_->emu->joystick();
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
    if (!impl_->emu) return Result::Unsupported;
    // §4.2a's RZX wall, as every other mutation has it: a recording cannot
    // carry the change and a playback would diverge (GH #12 — this verb alone
    // lacked it).
    if (impl_->emu->rzx_recorder().is_recording() ||
        impl_->emu->rzx_player().is_playing())
        return Result::RefusedRzx;
    const uint8_t old_colour = impl_->emu->ula().get_border();
    impl_->emu->ula().set_border(colour);
    impl_->log_mutate(by, "border", old_colour, colour & 0x07);
    impl_->note_mutation();
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
